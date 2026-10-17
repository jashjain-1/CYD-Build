#include "ipc_queues.h"
#include "app_state.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)

QueueHandle_t g_ui_to_proto_queue = NULL;
QueueHandle_t g_proto_to_ui_queue = NULL;

static volatile uint32_t s_ui_to_proto_drops = 0;
static volatile uint32_t s_proto_to_ui_drops = 0;

bool ipc_queues_init(void) {
    if (g_ui_to_proto_queue == NULL) {
        g_ui_to_proto_queue = xQueueCreate(IPC_QUEUE_CAPACITY, sizeof(UiToProtoMsg));
    }
    if (g_proto_to_ui_queue == NULL) {
        g_proto_to_ui_queue = xQueueCreate(IPC_QUEUE_CAPACITY, sizeof(ProtoToUiMsg));
    }
    return (g_ui_to_proto_queue != NULL && g_proto_to_ui_queue != NULL);
}

bool ipc_send_ui_to_proto(const UiToProtoMsg *msg, TickType_t wait_ticks) {
    if (g_ui_to_proto_queue == NULL || msg == NULL) {
        return false;
    }
    if (xQueueSend(g_ui_to_proto_queue, msg, wait_ticks) != pdPASS) {
        s_ui_to_proto_drops++;
        app_state_inc_queue_drop();
        return false;
    }
    return true;
}

bool ipc_recv_ui_to_proto(UiToProtoMsg *msg, TickType_t wait_ticks) {
    if (g_ui_to_proto_queue == NULL || msg == NULL) {
        return false;
    }
    return (xQueueReceive(g_ui_to_proto_queue, msg, wait_ticks) == pdPASS);
}

bool ipc_send_proto_to_ui(const ProtoToUiMsg *msg, TickType_t wait_ticks) {
    if (g_proto_to_ui_queue == NULL || msg == NULL) {
        return false;
    }
    if (xQueueSend(g_proto_to_ui_queue, msg, wait_ticks) != pdPASS) {
        s_proto_to_ui_drops++;
        app_state_inc_queue_drop();
        return false;
    }
    return true;
}

bool ipc_recv_proto_to_ui(ProtoToUiMsg *msg, TickType_t wait_ticks) {
    if (g_proto_to_ui_queue == NULL || msg == NULL) {
        return false;
    }
    return (xQueueReceive(g_proto_to_ui_queue, msg, wait_ticks) == pdPASS);
}

uint32_t ipc_get_ui_to_proto_drops(void) {
    return s_ui_to_proto_drops;
}

uint32_t ipc_get_proto_to_ui_drops(void) {
    return s_proto_to_ui_drops;
}

uint32_t ipc_get_total_drops(void) {
    return s_ui_to_proto_drops + s_proto_to_ui_drops;
}

#else
// Host native in-memory FIFO queue implementation for unit tests
#include <queue>

static std::queue<UiToProtoMsg> s_host_ui_to_proto;
static std::queue<ProtoToUiMsg> s_host_proto_to_ui;

QueueHandle_t g_ui_to_proto_queue = (QueueHandle_t)1;
QueueHandle_t g_proto_to_ui_queue = (QueueHandle_t)2;

static uint32_t s_ui_to_proto_drops = 0;
static uint32_t s_proto_to_ui_drops = 0;

bool ipc_queues_init(void) {
    while (!s_host_ui_to_proto.empty()) s_host_ui_to_proto.pop();
    while (!s_host_proto_to_ui.empty()) s_host_proto_to_ui.pop();
    return true;
}

bool ipc_send_ui_to_proto(const UiToProtoMsg *msg, TickType_t wait_ticks) {
    (void)wait_ticks;
    if (msg == NULL) return false;
    if (s_host_ui_to_proto.size() >= IPC_QUEUE_CAPACITY) {
        s_ui_to_proto_drops++;
        app_state_inc_queue_drop();
        return false;
    }
    s_host_ui_to_proto.push(*msg);
    return true;
}

bool ipc_recv_ui_to_proto(UiToProtoMsg *msg, TickType_t wait_ticks) {
    (void)wait_ticks;
    if (msg == NULL) return false;
    if (s_host_ui_to_proto.empty()) return false;
    *msg = s_host_ui_to_proto.front();
    s_host_ui_to_proto.pop();
    return true;
}

bool ipc_send_proto_to_ui(const ProtoToUiMsg *msg, TickType_t wait_ticks) {
    (void)wait_ticks;
    if (msg == NULL) return false;
    if (s_host_proto_to_ui.size() >= IPC_QUEUE_CAPACITY) {
        s_proto_to_ui_drops++;
        app_state_inc_queue_drop();
        return false;
    }
    s_host_proto_to_ui.push(*msg);
    return true;
}

bool ipc_recv_proto_to_ui(ProtoToUiMsg *msg, TickType_t wait_ticks) {
    (void)wait_ticks;
    if (msg == NULL) return false;
    if (s_host_proto_to_ui.empty()) return false;
    *msg = s_host_proto_to_ui.front();
    s_host_proto_to_ui.pop();
    return true;
}

uint32_t ipc_get_ui_to_proto_drops(void) {
    return s_ui_to_proto_drops;
}

uint32_t ipc_get_proto_to_ui_drops(void) {
    return s_proto_to_ui_drops;
}

uint32_t ipc_get_total_drops(void) {
    return s_ui_to_proto_drops + s_proto_to_ui_drops;
}

#endif
