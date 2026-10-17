#ifndef DASHCUE_UI_ARTWORK_STORE_H
#define DASHCUE_UI_ARTWORK_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// 64x64 keeps the artwork buffer at 8,192 B (spec §1.1). The media hero tile
// is 84x84 and LVGL scales this 1.31x with bilinear antialiasing, so a 64 px
// source stays sharp at size while the BLE bulk lane streams 8 x 1 KB chunks.
#define ARTWORK_W 64
#define ARTWORK_H 64
#define ARTWORK_BYTES (ARTWORK_W * ARTWORK_H * 2)

#ifdef __cplusplus
extern "C" {
#endif

void artwork_store_init(void);
void artwork_store_reset(void);

bool artwork_store_begin(uint32_t artwork_id, uint32_t width, uint32_t height,
                         uint32_t total_bytes, uint32_t total_crc32);

bool artwork_store_chunk(uint32_t artwork_id, uint32_t offset,
                         const uint8_t *data, size_t length);

/** Returns true only when the id, byte count and CRC all match. */
bool artwork_store_end(uint32_t artwork_id, uint32_t received_crc32);

const uint8_t *artwork_store_data(void);
uint32_t artwork_store_width(void);
uint32_t artwork_store_height(void);
uint32_t artwork_store_generation(void);
bool artwork_store_valid(void);

/**
 * Mark the current artwork as no longer valid and bump the generation.
 *
 * Called on a track change and on disconnect so the previous cover is hidden
 * the instant it stops describing what is playing. It deliberately does not
 * touch an in-flight transfer's offset/CRC state: only [valid] is cleared, so
 * ARTWORK_BEGIN for the next track still starts from a clean slate.
 */
void artwork_store_invalidate(void);

/**
 * Dominant accent colour of the current artwork, extracted once when the
 * transfer completes. Returned as a packed 0xRRGGBB value. Returns 0 when no
 * artwork is valid, so callers can fall back to the fixed brand accent.
 *
 * This powers Spotify/Android-Auto-style theming: the media screen tints its
 * accent surfaces (title, progress, controls, badge) from the album art
 * instead of the static cyan. Extracting here — on the received pixels — keeps
 * it identical whatever app produced the art.
 */
uint32_t artwork_store_dominant_rgb(void);

#ifdef __cplusplus
}
#endif

#endif // DASHCUE_UI_ARTWORK_STORE_H
