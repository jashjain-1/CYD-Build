#include "ui_command_id.h"

uint32_t ui_next_command_id(void) {
    static uint32_t s_next = 1;
    const uint32_t value = s_next++;
    if (s_next == 0) {
        s_next = 1; // never hand out the reserved zero id
    }
    return value;
}
