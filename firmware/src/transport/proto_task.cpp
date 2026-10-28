#ifndef DASHCUE_NATIVE_TEST

#include "proto_task.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <pb_encode.h>
#include <pb_decode.h>

#include "ble_peripheral.h"
#include "dashcue_config.h"
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif
#include "dashcue_version.h"
#include "state/app_state.h"
#include "state/ipc_queues.h"
#include "ui/artwork_store.h"
#include "protocol/framing.h"
#include "protocol/fragmentation.h"
#include "protocol/dedup_cache.h"
#include "protocol/generated/dashcue.pb.h"

namespace {

static TaskHandle_t s_proto_task_handle = nullptr;
static volatile bool s_proto_task_running = false;

static ReassemblyEngine s_reassembly;
static DedupCache       s_dedup;
static CreditFlowController s_flow;

static uint32_t s_out_msg_id = 1;
static uint32_t s_heartbeat_seq = 0;
static uint32_t s_last_heartbeat_tx_ms = 0;

// Static TX buffers to avoid placing buffers on the FreeRTOS task stack.
// Outbound HUD frames (DeviceStatus, Acks, CmdResults) are < 128 B, so 512 B provides
// a 4x safety margin while reclaiming 3,604 B of static DRAM.
static uint8_t s_tx_frame_buf[512];
static uint8_t s_tx_frag_buf[256];

static bool proto_send_framed_msg(dashcue_v1_MessageType msg_type,
                                   uint32_t msg_id,
                                   uint8_t flags,
                                   const uint8_t *payload,
                                   size_t payload_len) {
    if (!ble_peripheral_is_connected()) {
        return false;
    }

    size_t frame_len = 0;

    FrameHeader hdr;
    hdr.magic = DASHCUE_FRAME_MAGIC;
    hdr.version = DASHCUE_FRAME_VERSION;
    hdr.flags = flags;
    hdr.msg_type = (uint16_t)msg_type;
    hdr.msg_id = msg_id;
    hdr.length = (uint32_t)payload_len;
    hdr.reserved = 0;

    if (!frame_encode(&hdr, payload, payload_len, s_tx_frame_buf, &frame_len, sizeof(s_tx_frame_buf))) {
        return false;
    }

    uint16_t mtu = ble_peripheral_get_mtu();
    if (mtu < 23) mtu = 23;

    uint8_t total_frags = fragment_calc_count(frame_len, mtu);
    uint8_t lane = (msg_type == dashcue_v1_MessageType_MT_ROUTE_OVERVIEW ||
                    msg_type == dashcue_v1_MessageType_MT_ARTWORK_CHUNK) ?
                   DASHCUE_LANE_BULK : DASHCUE_LANE_CONTROL;

    for (uint8_t idx = 0; idx < total_frags; ++idx) {
        size_t frag_len = 0;
        if (fragment_pack(s_tx_frame_buf, frame_len, lane, idx, mtu, 8, s_tx_frag_buf, &frag_len, sizeof(s_tx_frag_buf))) {
            ble_peripheral_send_notify(s_tx_frag_buf, frag_len);
        }
    }
    return true;
}

void proto_send_hello_ack(uint32_t correlation, bool accepted, uint32_t reject_reason) {
    dashcue_v1_HelloAck ack = dashcue_v1_HelloAck_init_zero;
    ack.proto_major = DASHCUE_PROTO_MAJOR;
    ack.proto_minor = DASHCUE_PROTO_MINOR;
    // CAP_ROUTE_OVERVIEW is deliberately not advertised: the HUD dropped its
    // schematic map (the phone MapLibre view owns geometry).
    ack.capabilities = dashcue_v1_Capability_CAP_ARTWORK |
                       dashcue_v1_Capability_CAP_TOUCH_CALIBRATION |
                       dashcue_v1_Capability_CAP_DIAGNOSTICS |
                       dashcue_v1_Capability_CAP_SEEK;
    strncpy(ack.fw_version, DASHCUE_FW_VERSION_STRING, sizeof(ack.fw_version) - 1);
    ack.fw_version[sizeof(ack.fw_version) - 1] = '\0';
    strncpy(ack.hw_revision, DASHCUE_HW_REVISION, sizeof(ack.hw_revision) - 1);
    ack.hw_revision[sizeof(ack.hw_revision) - 1] = '\0';
    ack.max_msg_size = DASHCUE_MAX_PAYLOAD_SIZE;
    ack.mtu = ble_peripheral_get_mtu();
    ack.accepted = accepted ? 1 : 0;
    ack.reject_reason = reject_reason;

    uint8_t pb_buf[256];
    pb_ostream_t stream = pb_ostream_from_buffer(pb_buf, sizeof(pb_buf));
    if (pb_encode(&stream, dashcue_v1_HelloAck_fields, &ack)) {
        proto_send_framed_msg(dashcue_v1_MessageType_MT_HELLO_ACK,
                              correlation,
                              dashcue_v1_EnvelopeFlag_EF_IS_RESPONSE,
                              pb_buf,
                              stream.bytes_written);
    }
}

void proto_send_cmd_result(uint32_t command_id, dashcue_v1_CmdResult_Status status, uint32_t reason) {
    dashcue_v1_CmdResult res = dashcue_v1_CmdResult_init_zero;
    res.command_id = command_id;
    res.status = status;
    res.reason = reason;
    res.observed_at_ms = millis();

    uint8_t pb_buf[64];
    pb_ostream_t stream = pb_ostream_from_buffer(pb_buf, sizeof(pb_buf));
    if (pb_encode(&stream, dashcue_v1_CmdResult_fields, &res)) {
        proto_send_framed_msg(dashcue_v1_MessageType_MT_CMD_RESULT,
                              command_id,
                              dashcue_v1_EnvelopeFlag_EF_IS_RESPONSE,
                              pb_buf,
                              stream.bytes_written);
    }
}

void proto_send_error(uint32_t correlation, dashcue_v1_ErrorCode code, const char *text) {
    dashcue_v1_ErrorMsg err = dashcue_v1_ErrorMsg_init_zero;
    err.correlation = correlation;
    err.code = code;
    err.detail = 0;
    if (text != nullptr) {
        strncpy(err.text, text, sizeof(err.text) - 1);
        err.text[sizeof(err.text) - 1] = '\0';
    }

    uint8_t pb_buf[128];
    pb_ostream_t stream = pb_ostream_from_buffer(pb_buf, sizeof(pb_buf));
    if (pb_encode(&stream, dashcue_v1_ErrorMsg_fields, &err)) {
        proto_send_framed_msg(dashcue_v1_MessageType_MT_ERROR,
                              correlation,
                              dashcue_v1_EnvelopeFlag_EF_IS_RESPONSE,
                              pb_buf,
                              stream.bytes_written);
    }
}

void proto_send_heartbeat(uint32_t now_ms) {
    dashcue_v1_Heartbeat hb = dashcue_v1_Heartbeat_init_zero;
    hb.phone_ts_ms = 0; // HUD has no RTC clock
    hb.tz_offset_min = 0;
    hb.credits = 8;     // grant 8 credits to phone
    hb.speed_kmh_x10 = app_state_get_speed_kmh_x10();
    hb.seq = s_heartbeat_seq++;
    hb.rtt_ms = 0;

    uint8_t pb_buf[64];
    pb_ostream_t stream = pb_ostream_from_buffer(pb_buf, sizeof(pb_buf));
    if (pb_encode(&stream, dashcue_v1_Heartbeat_fields, &hb)) {
        proto_send_framed_msg(dashcue_v1_MessageType_MT_HEARTBEAT,
                              s_out_msg_id++,
                              dashcue_v1_EnvelopeFlag_EF_NONE,
                              pb_buf,
                              stream.bytes_written);
    }
}

void proto_send_device_status(uint32_t now_s) {
    dashcue_v1_DeviceStatus status;
    app_state_get_device_status(&status, now_s);

    uint8_t pb_buf[128];
    pb_ostream_t stream = pb_ostream_from_buffer(pb_buf, sizeof(pb_buf));
    if (pb_encode(&stream, dashcue_v1_DeviceStatus_fields, &status)) {
        proto_send_framed_msg(dashcue_v1_MessageType_MT_DEVICE_STATUS,
                              s_out_msg_id++,
                              dashcue_v1_EnvelopeFlag_EF_NONE,
                              pb_buf,
                              stream.bytes_written);
    }
}

void proto_send_diag_resp(uint32_t request_seq) {
    dashcue_v1_DiagResp *resp = (dashcue_v1_DiagResp *)malloc(sizeof(dashcue_v1_DiagResp));
    if (resp == nullptr) return;
    memset(resp, 0, sizeof(*resp));
    resp->request_seq = request_seq;
    resp->has_status = true;
    app_state_get_device_status(&resp->status, millis() / 1000u);
    resp->log_lines_count = 0;
    resp->log_total_lines = 0;

    uint8_t pb_buf[256];
    pb_ostream_t stream = pb_ostream_from_buffer(pb_buf, sizeof(pb_buf));
    if (pb_encode(&stream, dashcue_v1_DiagResp_fields, resp)) {
        proto_send_framed_msg(dashcue_v1_MessageType_MT_DIAG_RESP,
                              request_seq,
                              dashcue_v1_EnvelopeFlag_EF_IS_RESPONSE,
                              pb_buf,
                              stream.bytes_written);
    }
    free(resp);
}

void proto_send_settings_state(void) {
    dashcue_v1_SettingsState st = dashcue_v1_SettingsState_init_zero;
    st.has_settings = true;
    app_state_get_settings(&st.settings);
    st.confirmed = true;

    uint8_t pb_buf[64];
    pb_ostream_t stream = pb_ostream_from_buffer(pb_buf, sizeof(pb_buf));
    if (pb_encode(&stream, dashcue_v1_SettingsState_fields, &st)) {
        proto_send_framed_msg(dashcue_v1_MessageType_MT_SETTINGS_STATE,
                              s_out_msg_id++,
                              dashcue_v1_EnvelopeFlag_EF_NONE,
                              pb_buf,
                              stream.bytes_written);
    }
}

void proto_send_snapshot_request(uint32_t request_seq) {
    dashcue_v1_SnapshotRequest req = dashcue_v1_SnapshotRequest_init_zero;
    req.request_seq = request_seq;

    uint8_t pb_buf[32];
    pb_ostream_t stream = pb_ostream_from_buffer(pb_buf, sizeof(pb_buf));
    if (pb_encode(&stream, dashcue_v1_SnapshotRequest_fields, &req)) {
        proto_send_framed_msg(dashcue_v1_MessageType_MT_SNAPSHOT_REQUEST,
                              s_out_msg_id++,
                              dashcue_v1_EnvelopeFlag_EF_NONE,
                              pb_buf,
                              stream.bytes_written);
    }
}

void handle_incoming_frame(const FrameHeader *hdr, const uint8_t *payload, size_t payload_len, uint32_t now_ms) {
    // Check deduplication cache for commands (§9.5)
    if (hdr->msg_type == dashcue_v1_MessageType_MT_MEDIA_CMD ||
        hdr->msg_type == dashcue_v1_MessageType_MT_NAV_CMD ||
        hdr->msg_type == dashcue_v1_MessageType_MT_SETTINGS_SET) {
        dashcue_v1_CmdResult cached_res;
        if (dedup_check_and_record(&s_dedup, hdr->msg_id, now_ms, &cached_res)) {
            // Duplicate detected! Replay cached result
            app_state_inc_dup_command();
            proto_send_cmd_result(cached_res.command_id, cached_res.status, cached_res.reason);
            return;
        }
    }

    pb_istream_t stream = pb_istream_from_buffer(payload, payload_len);

    switch (hdr->msg_type) {
        case dashcue_v1_MessageType_MT_HELLO: {
            dashcue_v1_Hello hello = dashcue_v1_Hello_init_zero;
            if (pb_decode(&stream, dashcue_v1_Hello_fields, &hello)) {
                if (hello.proto_major != DASHCUE_PROTO_MAJOR) {
                    proto_send_hello_ack(hdr->msg_id, false, dashcue_v1_ErrorCode_EC_VERSION_MISMATCH);
                } else {
                    proto_send_hello_ack(hdr->msg_id, true, dashcue_v1_ErrorCode_EC_NONE);
                    app_state_set_peripheral_state(HUD_STATE_READY);
                    // Send initial state snapshot per §9.6
                    proto_send_device_status(now_ms / 1000u);
                    proto_send_settings_state();

                    ProtoToUiMessage ui_msg;
                    memset(&ui_msg, 0, sizeof(ui_msg));
                    ui_msg.type = IPC_EVT_BLE_STATE;
                    ui_msg.data.ble_state.state = HUD_STATE_READY;
                    ui_msg.data.ble_state.is_connected = true;
                    ui_msg.data.ble_state.mtu = ble_peripheral_get_mtu();
                    ipc_send_proto_to_ui(&ui_msg, 0);
                }
            } else {
                proto_send_error(hdr->msg_id, dashcue_v1_ErrorCode_EC_DECODE, "Failed to decode HELLO");
            }
            break;
        }

        case dashcue_v1_MessageType_MT_HEARTBEAT: {
            dashcue_v1_Heartbeat hb = dashcue_v1_Heartbeat_init_zero;
            if (pb_decode(&stream, dashcue_v1_Heartbeat_fields, &hb)) {
                app_state_on_heartbeat_rx(&hb, now_ms);

                ProtoToUiMessage ui_msg;
                memset(&ui_msg, 0, sizeof(ui_msg));
                ui_msg.type = IPC_EVT_HEARTBEAT;
                memcpy(&ui_msg.data.heartbeat, &hb, sizeof(hb));
                ipc_send_proto_to_ui(&ui_msg, 0);
            }
            break;
        }

        case dashcue_v1_MessageType_MT_MEDIA_STATE: {
            dashcue_v1_MediaState ms = dashcue_v1_MediaState_init_zero;
            if (pb_decode(&stream, dashcue_v1_MediaState_fields, &ms)) {
                app_state_on_media_state(&ms, now_ms);

                ProtoToUiMessage ui_msg;
                memset(&ui_msg, 0, sizeof(ui_msg));
                ui_msg.type = IPC_EVT_MEDIA_STATE;
                ipc_send_proto_to_ui(&ui_msg, 0);

            }
            break;
        }

        case dashcue_v1_MessageType_MT_NAV_STATE: {
            dashcue_v1_NavState ns = dashcue_v1_NavState_init_zero;
            if (pb_decode(&stream, dashcue_v1_NavState_fields, &ns)) {
                app_state_on_nav_state(&ns, now_ms);

                ProtoToUiMessage ui_msg;
                memset(&ui_msg, 0, sizeof(ui_msg));
                ui_msg.type = IPC_EVT_NAV_STATE;
                ipc_send_proto_to_ui(&ui_msg, 0);
            }
            break;
        }

        case dashcue_v1_MessageType_MT_ROUTE_OVERVIEW: {
            // Accepted for forward compatibility but intentionally discarded:
            // the HUD has no map surface and the phone renders the route. Do
            // not queue it (a 120-point route must never be copied per event).
            dashcue_v1_RouteOverview ro = dashcue_v1_RouteOverview_init_zero;
            (void)pb_decode(&stream, dashcue_v1_RouteOverview_fields, &ro);
            break;
        }

        case dashcue_v1_MessageType_MT_ARTWORK_BEGIN: {
            dashcue_v1_ArtworkBegin begin = dashcue_v1_ArtworkBegin_init_zero;
            if (pb_decode(&stream, dashcue_v1_ArtworkBegin_fields, &begin)) {
#if defined(ESP_PLATFORM)
                // Heap guard: do NOT kick off an 8 KB variable-size fragment
                // transfer when the pool is thin (see the MediaState case for
                // the measured crash). The self-healing snapshot-request path
                // re-triggers the transfer once the heap recovers.
                const uint32_t largest =
                    heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
                const bool starved = largest < DASHCUE_HEAP_GUARD_BYTES;
#else
                const uint32_t largest = 0;
                const bool starved = false;
#endif
                if (!starved) {
                    artwork_store_begin(begin.artwork_id, begin.width,
                                        begin.height, begin.total_bytes,
                                        begin.total_crc32);
                }
            }
            break;
        }

        case dashcue_v1_MessageType_MT_ARTWORK_CHUNK: {
            dashcue_v1_ArtworkChunk chunk = dashcue_v1_ArtworkChunk_init_zero;
            if (pb_decode(&stream, dashcue_v1_ArtworkChunk_fields, &chunk)) {
                artwork_store_chunk(chunk.artwork_id, chunk.offset,
                                    chunk.data.bytes, chunk.data.size);
            }
            break;
        }

        case dashcue_v1_MessageType_MT_ARTWORK_END: {
            dashcue_v1_ArtworkEnd end = dashcue_v1_ArtworkEnd_init_zero;
            if (pb_decode(&stream, dashcue_v1_ArtworkEnd_fields, &end)) {
                if (artwork_store_end(end.artwork_id, end.received_crc32)) {
                    ProtoToUiMessage ui_msg;
                    memset(&ui_msg, 0, sizeof(ui_msg));
                    ui_msg.type = IPC_EVT_ARTWORK_READY;
                    ui_msg.data.artwork.generation = artwork_store_generation();
                    ipc_send_proto_to_ui(&ui_msg, 0);
                } else {
                    app_state_inc_crc_error();
                    proto_send_error(end.artwork_id, dashcue_v1_ErrorCode_EC_CRC, "Artwork CRC or length mismatch");
                    artwork_store_reset();
                }
            }
            break;
        }

        case dashcue_v1_MessageType_MT_SETTINGS_SET: {
            dashcue_v1_SettingsSet ss = dashcue_v1_SettingsSet_init_zero;
            if (pb_decode(&stream, dashcue_v1_SettingsSet_fields, &ss)) {
                if (ss.has_settings) {
                    app_state_on_settings_set(&ss.settings);
                    dedup_update_result(&s_dedup, ss.command_id, dashcue_v1_CmdResult_Status_ST_CONFIRMED, 0, now_ms);
                    proto_send_cmd_result(ss.command_id, dashcue_v1_CmdResult_Status_ST_CONFIRMED, 0);
                    proto_send_settings_state();

                    ProtoToUiMessage ui_msg;
                    memset(&ui_msg, 0, sizeof(ui_msg));
                    ui_msg.type = IPC_EVT_SETTINGS_STATE;
                    ui_msg.data.settings_state.has_settings = true;
                    memcpy(&ui_msg.data.settings_state.settings, &ss.settings, sizeof(ss.settings));
                    ui_msg.data.settings_state.confirmed = true;
                    ipc_send_proto_to_ui(&ui_msg, 0);
                }
            }
            break;
        }

        case dashcue_v1_MessageType_MT_SNAPSHOT_REQUEST: {
            proto_send_device_status(now_ms / 1000u);
            proto_send_settings_state();
            break;
        }

        case dashcue_v1_MessageType_MT_DIAG_REQ: {
            dashcue_v1_DiagReq req = dashcue_v1_DiagReq_init_zero;
            uint32_t req_seq = hdr->msg_id;
            if (pb_decode(&stream, dashcue_v1_DiagReq_fields, &req)) {
                if (req.command_id != 0) {
                    req_seq = req.command_id;
                }
            }
            proto_send_diag_resp(req_seq);
            break;
        }

        case dashcue_v1_MessageType_MT_CMD_RESULT: {
            dashcue_v1_CmdResult res = dashcue_v1_CmdResult_init_zero;
            if (pb_decode(&stream, dashcue_v1_CmdResult_fields, &res)) {
                ProtoToUiMessage ui_msg;
                memset(&ui_msg, 0, sizeof(ui_msg));
                ui_msg.type = IPC_EVT_CMD_RESULT;
                memcpy(&ui_msg.data.cmd_result, &res, sizeof(res));
                ipc_send_proto_to_ui(&ui_msg, 0);
            }
            break;
        }

        case dashcue_v1_MessageType_MT_ERROR: {
            dashcue_v1_ErrorMsg err = dashcue_v1_ErrorMsg_init_zero;
            if (pb_decode(&stream, dashcue_v1_ErrorMsg_fields, &err)) {
                ProtoToUiMessage ui_msg;
                memset(&ui_msg, 0, sizeof(ui_msg));
                ui_msg.type = IPC_EVT_ERROR;
                memcpy(&ui_msg.data.error, &err, sizeof(err));
                ipc_send_proto_to_ui(&ui_msg, 0);
            }
            break;
        }

        default:
            proto_send_error(hdr->msg_id, dashcue_v1_ErrorCode_EC_UNKNOWN_TYPE, "Unknown message type");
            break;
    }
}

void handle_outgoing_ui_command(const UiToProtoMessage *cmd) {
    if (cmd == nullptr) return;

    switch (cmd->type) {
        case IPC_CMD_MEDIA: {
            uint8_t pb_buf[64];
            pb_ostream_t stream = pb_ostream_from_buffer(pb_buf, sizeof(pb_buf));
            if (pb_encode(&stream, dashcue_v1_MediaCmd_fields, &cmd->data.media_cmd)) {
                proto_send_framed_msg(dashcue_v1_MessageType_MT_MEDIA_CMD,
                                      cmd->data.media_cmd.command_id,
                                      dashcue_v1_EnvelopeFlag_EF_ACK_REQUIRED,
                                      pb_buf,
                                      stream.bytes_written);
            }
            break;
        }

        case IPC_CMD_NAV: {
            uint8_t pb_buf[64];
            pb_ostream_t stream = pb_ostream_from_buffer(pb_buf, sizeof(pb_buf));
            if (pb_encode(&stream, dashcue_v1_NavCmd_fields, &cmd->data.nav_cmd)) {
                proto_send_framed_msg(dashcue_v1_MessageType_MT_NAV_CMD,
                                      cmd->data.nav_cmd.command_id,
                                      dashcue_v1_EnvelopeFlag_EF_ACK_REQUIRED,
                                      pb_buf,
                                      stream.bytes_written);
            }
            break;
        }

        case IPC_CMD_SETTINGS_SET: {
            uint8_t pb_buf[64];
            pb_ostream_t stream = pb_ostream_from_buffer(pb_buf, sizeof(pb_buf));
            if (pb_encode(&stream, dashcue_v1_SettingsSet_fields, &cmd->data.settings_set)) {
                proto_send_framed_msg(dashcue_v1_MessageType_MT_SETTINGS_SET,
                                      cmd->data.settings_set.command_id,
                                      dashcue_v1_EnvelopeFlag_EF_ACK_REQUIRED,
                                      pb_buf,
                                      stream.bytes_written);
            }
            break;
        }

        case IPC_CMD_SNAPSHOT_REQ: {
            uint8_t pb_buf[32];
            pb_ostream_t stream = pb_ostream_from_buffer(pb_buf, sizeof(pb_buf));
            if (pb_encode(&stream, dashcue_v1_SnapshotRequest_fields, &cmd->data.snapshot_req)) {
                proto_send_framed_msg(dashcue_v1_MessageType_MT_SNAPSHOT_REQUEST,
                                      cmd->data.snapshot_req.request_seq,
                                      dashcue_v1_EnvelopeFlag_EF_NONE,
                                      pb_buf,
                                      stream.bytes_written);
            }
            break;
        }

        case IPC_CMD_PAIR_PHONE: {
            ble_peripheral_start_pairing_window(cmd->data.pair_phone.timeout_s);
            break;
        }

        case IPC_CMD_CANCEL_PAIRING: {
            ble_peripheral_cancel_pairing_window();
            break;
        }

        case IPC_CMD_FORGET_PHONE: {
            ble_peripheral_forget_phone();
            break;
        }

        case IPC_CMD_DIAG_REQ: {
            uint8_t pb_buf[32];
            pb_ostream_t stream = pb_ostream_from_buffer(pb_buf, sizeof(pb_buf));
            if (pb_encode(&stream, dashcue_v1_DiagReq_fields, &cmd->data.diag_req)) {
                proto_send_framed_msg(dashcue_v1_MessageType_MT_DIAG_REQ,
                                      cmd->data.diag_req.command_id,
                                      dashcue_v1_EnvelopeFlag_EF_ACK_REQUIRED,
                                      pb_buf,
                                      stream.bytes_written);
            }
            break;
        }

        default:
            break;
    }
}

void proto_task_entry(void *param) {
    (void)param;
    reassembly_init(&s_reassembly);
    dedup_init(&s_dedup);
    flow_control_init(&s_flow, DASHCUE_INITIAL_CREDITS);

    // Note: NimBLE is initialised by setup() before this task starts. The host's
    // mutexes are created by the initialising task, so initialising from here
    // tripped `npl_freertos_mutex_pend` on the first boot. This task only uses the
    // peripheral afterwards (advertising, notify), which is thread-safe.
    s_proto_task_running = true;

    while (s_proto_task_running) {
        uint32_t now_ms = millis();

        // 1. Process incoming raw BLE RX packets
        BleRxPacket rx_pkt;
        while (g_ble_rx_queue != nullptr && xQueueReceive(g_ble_rx_queue, &rx_pkt, 0) == pdPASS) {
            const uint8_t *msg_bytes = nullptr;
            size_t msg_len = 0;

            ReassemblyStatus status = reassembly_feed(&s_reassembly,
                                                      rx_pkt.data,
                                                      rx_pkt.len,
                                                      now_ms,
                                                      &msg_bytes,
                                                      &msg_len);

            if (status == REASSEMBLY_COMPLETE) {
                FrameHeader hdr;
                memset(&hdr, 0, sizeof(hdr));
                const uint8_t *payload = nullptr;
                size_t payload_len = 0;

                dashcue_v1_ErrorCode err = frame_decode(msg_bytes, msg_len, &hdr, &payload, &payload_len);
                if (err == dashcue_v1_ErrorCode_EC_NONE) {
                    handle_incoming_frame(&hdr, payload, payload_len, now_ms);
                } else {
                    if (err == dashcue_v1_ErrorCode_EC_CRC) {
                        app_state_inc_crc_error();
                    }
                    proto_send_error(0, err, "Frame decode error");
                }
            } else if (status == REASSEMBLY_TIMEOUT) {
                app_state_inc_frag_timeout();
            }
        }

        // 2. Process outgoing UI commands from ui_task
        UiToProtoMessage cmd_msg;
        while (g_ui_to_proto_queue != nullptr && ipc_recv_ui_to_proto(&cmd_msg, 0)) {
            handle_outgoing_ui_command(&cmd_msg);
        }

        // 3. Periodic house-keeping
        reassembly_tick(&s_reassembly, now_ms);
        ble_peripheral_tick(now_ms);

        HudPeripheralState old_st = app_state_get_peripheral_state();
        app_state_check_freshness(now_ms);
        HudPeripheralState new_st = app_state_get_peripheral_state();

        if (old_st != new_st) {
            ProtoToUiMessage ui_msg;
            memset(&ui_msg, 0, sizeof(ui_msg));
            ui_msg.type = IPC_EVT_BLE_STATE;
            ui_msg.data.ble_state.state = new_st;
            ui_msg.data.ble_state.is_connected = ble_peripheral_is_connected();
            ui_msg.data.ble_state.mtu = ble_peripheral_get_mtu();
            ipc_send_proto_to_ui(&ui_msg, 0);
        }

        // 4. Heartbeat timer (every 2.0 s per spec §9.6)
        if (ble_peripheral_is_connected() &&
            (new_st == HUD_STATE_READY || new_st == HUD_STATE_STALE)) {
            if (now_ms - s_last_heartbeat_tx_ms >= HEARTBEAT_INTERVAL_MS) {
                s_last_heartbeat_tx_ms = now_ms;
                proto_send_heartbeat(now_ms);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    vTaskDelete(nullptr);
}

} // namespace

bool proto_task_start(void) {
    if (s_proto_task_handle != nullptr) {
        return true;
    }

    BaseType_t rc = xTaskCreatePinnedToCore(
        proto_task_entry,
        "proto_task",
        PROTO_TASK_STACK_SIZE,
        nullptr,
        PROTO_TASK_PRIORITY,
        &s_proto_task_handle,
        PROTO_TASK_CORE_ID
    );

    return (rc == pdPASS);
}

void proto_task_stop(void) {
    s_proto_task_running = false;
    s_proto_task_handle = nullptr;
}

#endif // DASHCUE_NATIVE_TEST
