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
bool artwork_store_valid_for(uint32_t artwork_id);

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
 * Pre-scaled rendering cache: bilinear-upscales the valid artwork to
 * (target_w x target_h) RGB565 ONCE per artwork generation and keeps the
 * result in a cache buffer that is allocated once and never resized (same
 * target size every call). Returns nullptr when no valid artwork exists or
 * the cache allocation could not happen at that moment. Callers must NOT
 * free the returned pointer; it lives across calls. Purpose: the media
 * screen previously let LVGL scale the 64x64 source with antialiasing at
 * paint time, which allocates and frees a destination-sized transform
 * buffer on EVERY repaint (the media progress bar repaints every second),
 * fragmenting the heap until Bluedroid's HCI reassembler failed its ~1 KB
 * allocation under A2DP streaming and StoreProhibited-crashed the BT host
 * (measured 2026-10-26). Drawing the pre-scaled image 1:1 with antialias
 * off makes repaints pure blits with zero temporary allocations.
 */
const uint8_t *artwork_store_prescale(uint32_t target_w, uint32_t target_h);

/*
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
