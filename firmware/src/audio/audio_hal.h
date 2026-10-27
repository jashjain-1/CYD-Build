#ifndef DASHCUE_AUDIO_HAL_H
#define DASHCUE_AUDIO_HAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Onboard speaker output for the CYD audio port (GPIO26 / DAC2 / 8002A amp).
 *
 * Re-architected for zero heap allocation and zero software timer interrupts:
 * alert tones are synthesized on-the-fly in fixed stack chunks (64 frames = 256 bytes)
 * and streamed directly to hardware I2S DMA on Core 1 (audio_task, priority 3).
 *
 * Output selection is a cascading choice:
 *   AUDIO_OUT_PHONE - DashCue alert tones stay on the phone (Bluetooth/media
 *                     routing is the phone's decision). The HUD speaker stays
 *                     silent and only feedback clicks are emitted.
 *   AUDIO_OUT_CYD   - DashCue alert tones play on the HUD speaker with volume
 *                     and EQ applied, delivering crisp in-cabin notifications.
 * Music and navigation voice follow the active A2DP Bluetooth route.
 */
typedef enum {
    AUDIO_OUT_PHONE = 0,
    AUDIO_OUT_CYD = 1
} audio_out_t;

/** Full clean DAC dynamic range (0..255) through the onboard 8002A amplifier.
 *  Peak swing around midscale 128 is full 127 counts (output range 1..255),
 *  delivering loud, punchy, distortion-free alerts to the 8 ohm 0.5W speaker. */
#define AUDIO_SAFE_PEAK_COUNTS 127
#define AUDIO_MAX_VOLUME_PCT   100

void audio_hal_init(void);

void audio_hal_set_output(audio_out_t out);
audio_out_t audio_hal_get_output(void);

/** 0..100 inclusive; values above 100 are clamped. */
void audio_hal_set_volume_pct(uint32_t pct);
uint32_t audio_hal_get_volume_pct(void);

/**
 * 3-band tone control in dB (-12..+12), applied streaming to every generated tone.
 * Low shelf 60 Hz, peaking 1 kHz, high shelf 6 kHz.
 */
void audio_hal_set_eq(int8_t bass_db, int8_t mid_db, int8_t treble_db);
void audio_hal_get_eq(int8_t *bass_db, int8_t *mid_db, int8_t *treble_db);
/** Cycle through the curated presets (Flat -> Bass -> Treble -> Flat). */
const char *audio_hal_cycle_eq_preset(void);

/** Plays are no-ops while the output is PHONE, so the HUD never claims sound. */
void audio_hal_play_alert(void);  // 4-pulse X-band style chirp
void audio_hal_play_click(void);  // touch confirmation tick
void audio_hal_play_beep(void);   // pairing / confirmation double beep
bool audio_hal_is_playing(void);

typedef void (*audio_duck_cb_t)(bool duck);
void audio_hal_set_duck_callback(audio_duck_cb_t cb);

/** Dynamic 0V clamping on GPIO 26 (DAC2 / 8002A amp) to kill idle amplifier hiss/buzz. */
void audio_hal_clamp_zero_volts(void);
void audio_hal_idle_state(void);

/** Notify Audio HAL of external A2DP streaming state so post-alert handover clamps to 0V if idle. */
void audio_hal_notify_a2dp_state(bool is_streaming);
bool audio_hal_is_a2dp_streaming(void);

#ifdef __cplusplus
}
#endif

#endif // DASHCUE_AUDIO_HAL_H