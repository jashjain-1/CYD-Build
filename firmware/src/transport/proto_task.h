#ifndef DASHCUE_TRANSPORT_PROTO_TASK_H
#define DASHCUE_TRANSPORT_PROTO_TASK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROTO_TASK_STACK_SIZE 10240u
#define PROTO_TASK_PRIORITY   3u
#define PROTO_TASK_CORE_ID    0u

/**
 * Initialize protocol task and start FreeRTOS task on Core 0.
 * @return true if task created successfully.
 */
bool proto_task_start(void);

/**
 * Stop protocol task.
 */
void proto_task_stop(void);

#ifdef __cplusplus
}
#endif

#endif // DASHCUE_TRANSPORT_PROTO_TASK_H
