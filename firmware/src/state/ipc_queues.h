#ifndef DASHCUE_STATE_IPC_QUEUES_H
#define DASHCUE_STATE_IPC_QUEUES_H

#include "ipc_messages.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#else
typedef void* QueueHandle_t;
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
#define pdTRUE 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define IPC_QUEUE_CAPACITY 16

extern QueueHandle_t g_ui_to_proto_queue;
extern QueueHandle_t g_proto_to_ui_queue;

/**
 * Initialize FreeRTOS IPC queues (16 items each).
 * @return true if both queues successfully created, false otherwise.
 */
bool ipc_queues_init(void);

/**
 * Send command from UI task to Proto task.
 * @param msg Pointer to UiToProtoMsg.
 * @param wait_ticks Max ticks to wait if queue is full.
 * @return true if enqueued, false if dropped / timed out.
 */
bool ipc_send_ui_to_proto(const UiToProtoMsg *msg, TickType_t wait_ticks);

/**
 * Receive command in Proto task from UI task.
 * @param msg Destination pointer for dequeued UiToProtoMsg.
 * @param wait_ticks Max ticks to wait for available message.
 * @return true if message received, false if timed out.
 */
bool ipc_recv_ui_to_proto(UiToProtoMsg *msg, TickType_t wait_ticks);

/**
 * Send event from Proto task to UI task.
 * @param msg Pointer to ProtoToUiMsg.
 * @param wait_ticks Max ticks to wait if queue is full.
 * @return true if enqueued, false if dropped / timed out.
 */
bool ipc_send_proto_to_ui(const ProtoToUiMsg *msg, TickType_t wait_ticks);

/**
 * Receive event in UI task from Proto task.
 * @param msg Destination pointer for dequeued ProtoToUiMsg.
 * @param wait_ticks Max ticks to wait for available message.
 * @return true if message received, false if timed out.
 */
bool ipc_recv_proto_to_ui(ProtoToUiMsg *msg, TickType_t wait_ticks);

/**
 * Get dropped message counts for telemetry and diagnostics.
 */
uint32_t ipc_get_ui_to_proto_drops(void);
uint32_t ipc_get_proto_to_ui_drops(void);
uint32_t ipc_get_total_drops(void);

#ifdef __cplusplus
}
#endif

#endif // DASHCUE_STATE_IPC_QUEUES_H
