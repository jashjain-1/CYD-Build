#include "artwork_store.h"
#include "protocol/crc32.h"
#include <stdlib.h>
#include <string.h>

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include <esp_heap_caps.h>
#endif

namespace {
// Heap, not .bss. The 8,192 B artwork buffer was the single largest user
// allocation in static DRAM and pushed the board 7,924 B over its 70,000 B gate.
// Moving it to the heap is a pure relocation: every accessor already guards a
// null buffer, the size and lifetime are unchanged, and `.dram0.bss` drops by
// exactly ARTWORK_BYTES. The block is allocated once on the first transfer and
// deliberately never freed — releasing and re-taking an 8 KB block on every
// track change is how you fragment a heap that must keep >= 45 KB free.
uint8_t *s_pixels = nullptr;
uint32_t s_expected_id = 0;
uint32_t s_expected_bytes = 0;
uint32_t s_expected_crc = 0;
uint32_t s_written = 0;
uint32_t s_crc = 0;
uint32_t s_generation = 0;
bool     s_in_progress = false;
bool     s_valid = false;
uint32_t s_dominant_rgb = 0;
uint32_t s_width = ARTWORK_W;
uint32_t s_height = ARTWORK_H;

// Extract a dominant accent (0xRRGGBB) from the received RGB565 buffer.
//
// We want a *vivid* accent that survives being shown on a near-black cockpit
// background, so this is a weighted vote rather than a plain mean: a plain
// mean of an album cover collapses to a muddy mid-tone (dark backgrounds
// dominate the pixel count). Each pixel votes for a coarse 4x4x4 bucket in
// RGB space, weighted by how colourful and how bright it is. Near-greys and
// near-blacks carry almost no weight, so a single saturated subject (the
// album's hero colour) wins. The winning bucket's mean is then clamped to a
// minimum luminance so the accent never disappears into the background.
static uint8_t *ensure_pixels() {
    if (s_pixels != nullptr) return s_pixels;
#if defined(ESP_PLATFORM) || defined(ARDUINO)
    s_pixels = (uint8_t *)heap_caps_malloc(ARTWORK_BYTES, MALLOC_CAP_8BIT);
#else
    s_pixels = (uint8_t *)malloc(ARTWORK_BYTES);
#endif
    return s_pixels;
}

static uint32_t extract_dominant_rgb(const uint8_t *px, size_t len) {
    // 4x4x4 = 64 buckets; accumulate sum and count per channel.
    uint32_t r_sum[64], g_sum[64], b_sum[64], cnt[64];
    memset(r_sum, 0, sizeof(r_sum));
    memset(g_sum, 0, sizeof(g_sum));
    memset(b_sum, 0, sizeof(b_sum));
    memset(cnt, 0, sizeof(cnt));

    const size_t pixels = len / 2;
    for (size_t i = 0; i < pixels; ++i) {
        const uint16_t v = (uint16_t)px[i * 2] | ((uint16_t)px[i * 2 + 1] << 8);
        const uint8_t r = (uint8_t)((((v >> 11) & 0x1F) * 255 + 15) / 31);
        const uint8_t g = (uint8_t)((((v >> 5) & 0x3F) * 255 + 31) / 63);
        const uint8_t b = (uint8_t)((((v >> 0) & 0x1F) * 255 + 15) / 31);

        const uint8_t mx = (r > g ? r : g) > b ? (r > g ? r : g) : b;
        const uint8_t mn = (r < g ? r : g) < b ? (r < g ? r : g) : b;
        const uint8_t sat = mx == 0 ? 0 : (uint8_t)(((uint16_t)(mx - mn) * 255) / mx);

        // Weight = saturation * brightness. Grey and black pixels barely vote.
        const uint32_t weight = ((uint32_t)sat * mx) / 255;
        if (weight == 0) continue;

        const int bucket = ((r >> 6) << 4) | ((g >> 6) << 2) | (b >> 6);
        r_sum[bucket] += (uint32_t)r * weight;
        g_sum[bucket] += (uint32_t)g * weight;
        b_sum[bucket] += (uint32_t)b * weight;
        cnt[bucket] += weight;
    }

    int best = -1;
    uint32_t best_weight = 0;
    for (int i = 0; i < 64; ++i) {
        if (cnt[i] > best_weight) { best_weight = cnt[i]; best = i; }
    }
    if (best < 0 || cnt[best] == 0) return 0;

    uint32_t r = r_sum[best] / cnt[best];
    uint32_t g = g_sum[best] / cnt[best];
    uint32_t b = b_sum[best] / cnt[best];

    // Floor the luminance so the accent stays visible on the dark cockpit bg.
    const uint32_t lum = (r * 30 + g * 59 + b * 11) / 100;
    const uint32_t target = 150;
    if (lum > 0 && lum < target) {
        const uint32_t scale = (target * 256) / lum;
        r = (r * scale) >> 8; g = (g * scale) >> 8; b = (b * scale) >> 8;
        if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    }

    return (r << 16) | (g << 8) | b;
}
}

extern "C" void artwork_store_init(void) {
    ensure_pixels();
}

extern "C" void artwork_store_reset(void) {
    s_in_progress = false;
    s_expected_id = 0;
    s_expected_bytes = 0;
    s_expected_crc = 0;
    s_written = 0;
    s_crc = 0;
    s_valid = false;
    s_width = ARTWORK_W;
    s_height = ARTWORK_H;
}

extern "C" bool artwork_store_begin(uint32_t artwork_id, uint32_t width,
                                    uint32_t height, uint32_t total_bytes,
                                    uint32_t total_crc32) {
    // Supports Tier 1 (64x64, 8,192 B) and Tier 2 fallback thumbnail (32x32, 2,048 B).
    const bool is_64 = (width == 64 && height == 64 && total_bytes == 64 * 64 * 2);
    const bool is_32 = (width == 32 && height == 32 && total_bytes == 32 * 32 * 2);
    if (!is_64 && !is_32) return false;
    if (artwork_id == 0) return false;
    // A transfer needs its buffer. Failing here (rather than at the first
    // chunk) keeps the phone's state machine on the documented reject path.
    if (ensure_pixels() == nullptr) return false;

    s_expected_id = artwork_id;
    s_expected_bytes = total_bytes;
    s_expected_crc = total_crc32;
    s_width = width;
    s_height = height;
    s_written = 0;
    s_crc = 0;
    s_in_progress = true;
    return true;
}

extern "C" bool artwork_store_chunk(uint32_t artwork_id, uint32_t offset,
                                    const uint8_t *data, size_t length) {
    if (!s_in_progress || artwork_id != s_expected_id) return false;
    if (data == nullptr || length == 0 || s_pixels == nullptr) return false;
    if (offset != s_written) return false;                  // gaps/reorder -> reject
    if (offset + length > ARTWORK_BYTES) return false;      // bounds before memcpy

    memcpy(&s_pixels[offset], data, length);
    s_crc = crc32_update(s_crc, data, length);
    s_written += (uint32_t)length;
    return true;
}

extern "C" bool artwork_store_end(uint32_t artwork_id, uint32_t received_crc32) {
    if (!s_in_progress || artwork_id != s_expected_id) return false;
    s_in_progress = false;

    const bool ok = (s_written == s_expected_bytes) &&
                    (s_crc == s_expected_crc) &&
                    (received_crc32 == s_crc);
    if (ok && s_pixels != nullptr) {
        s_valid = true;
        s_generation++;
        s_dominant_rgb = extract_dominant_rgb(s_pixels, s_written);
    }
    return ok;
}

extern "C" const uint8_t *artwork_store_data(void) {
    return s_pixels;
}

extern "C" uint32_t artwork_store_width(void) {
    return s_valid ? s_width : ARTWORK_W;
}

extern "C" uint32_t artwork_store_height(void) {
    return s_valid ? s_height : ARTWORK_H;
}

extern "C" uint32_t artwork_store_generation(void) {
    return s_generation;
}

extern "C" void artwork_store_invalidate(void) {
    if (!s_valid) return;
    s_valid = false;
    // Bumping the generation is what makes the UI notice; without it
    // refreshArtwork() would early-return and keep painting the dead cover.
    s_generation++;
}

extern "C" bool artwork_store_valid(void) {
    return s_valid;
}

extern "C" uint32_t artwork_store_dominant_rgb(void) {
    return s_valid ? s_dominant_rgb : 0;
}
