#ifndef DASHCUE_UI_COMMAND_ID_H
#define DASHCUE_UI_COMMAND_ID_H

#include <stdint.h>

/**
 * Monotonic command id for messages originated by the HUD UI.
 *
 * Command ids are per-sender and are the dedupe key on the phone; zero is
 * reserved here because it makes an unset id obvious in a trace. Only the UI
 * task calls this, so no lock is required.
 */
uint32_t ui_next_command_id(void);

#endif // DASHCUE_UI_COMMAND_ID_H
