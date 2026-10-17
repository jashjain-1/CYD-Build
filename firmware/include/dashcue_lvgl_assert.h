/* DashCue LVGL assert handler.
 *
 * Included by LVGL through LV_ASSERT_CUSTOM_INCLUDE (see tools/gen_lv_conf.py).
 *
 * Why this exists: LVGL's built-in handler is `while(1);` - a failed assert shows
 * up on a flashed board as a frozen panel with no serial output and no reset, so
 * neither the operator nor the diagnostics counters can tell what happened. Rule 9
 * and §15.5 want the opposite: a visible cause and a recovery path.
 *
 * The handler prints the failing location and then restarts. The boot crash counter
 * in state/app_state.cpp counts the restart, and three resets inside 60 s put the
 * HUD into SafeMode with the diagnostics screen (§15.5).
 *
 * The macro expands to a call, because LVGL pastes LV_ASSERT_HANDLER directly before
 * a closing brace (`do { ... LV_ASSERT_HANDLER } while(0)`), so a braced construct
 * would not compile. The implementation is in src/diagnostics/lvgl_assert.cpp.
 */
#ifndef DASHCUE_LVGL_ASSERT_H
#define DASHCUE_LVGL_ASSERT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Never returns. Declared here so the LVGL library build only needs the prototype. */
void dashcue_lvgl_assert_failed(const char *file, int line);

#ifdef __cplusplus
}
#endif

#define LV_ASSERT_HANDLER dashcue_lvgl_assert_failed(__FILE__, __LINE__);

#endif /* DASHCUE_LVGL_ASSERT_H */
