#ifndef DASHCUE_TRANSPORT_PROTO_TASK_H
#define DASHCUE_TRANSPORT_PROTO_TASK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 7,168 B: measured high-water 3,540 B on 2026-10-20 (was 10,240). The 2x margin
// exists because an overflow here is a crash, not a slow frame. Re-measure via
// the STACK line in the periodic report before changing it again.
#define PROTO_TASK_STACK_SIZE 7168u
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
