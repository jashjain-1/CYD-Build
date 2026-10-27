#include "artwork_store.h"
#include "protocol/crc32.h"
#include <stdlib.h>
#include <string.h>


namespace {
// Static .bss, not heap. This buffer first lived in .bss, was moved to a lazy
// heap allocation when static DRAM sat 7,924 B over the 70,000 B gate, and is
// now moved BACK: the A2DP product build stepped over that gate in the other
// direction, and a mid-session heap allocation of 8,192 B sits in the middle of
// the pool and bisects the largest free block exactly when streaming needs a
// big contiguous chunk (measured 2026-10-26: largest block fell 30,708 ->
// 17,396 B as the artwork screen painted). Static DRAM now has headroom again
// (build RAM 60,120 B; +8,192 B = 68,312 B, inside the 70,000 B gate), the
// buffer is carved out once at boot when the pool is still contiguous, and the
// mid-run allocation/never-free pattern that fragments the heap is gone.
// Size and lifetime are unchanged; the accessors below still guard.
static uint8_t s_pixels_buf[ARTWORK_BYTES];
uint8_t *s_pixels = nullptr;
uint32_t s_expected_id = 0;
uint32_t s_expected_bytes = 0;
uint32_t s_expected_crc = 0;
uint32_t s_written = 0;
uint32_t s_crc = 0;
uint32_t s_generation = 0;
bool     s_in_progress = false;
bool     s_valid = false;
uint32_t s_current_valid_id = 0;
uint32_t s_dominant_rgb = 0;
uint32_t s_width = ARTWORK_W;
uint32_t s_height = ARTWORK_H;

// ---------------------------------------------------------------- prescale
// One-time bilinear upscale cache (see artwork_store.h for the rationale).
// The cache buffer is sized for its first successful request and reused for
// every following request of the same byte size (the media screen always
// asks for the same tile size), so steady state has ZERO allocations.
static uint8_t *s_scaled_buf = nullptr;
static uint32_t s_scaled_w = 0;
static uint32_t s_scaled_h = 0;
static uint32_t s_scaled_bytes = 0;
static uint32_t s_scaled_gen = 0; // generation the cache was computed for

// Bilinear RGB565 -> RGB565 upscale with 8.8 fixed-point coordinates.
static void prescale_bilinear(uint32_t target_w, uint32_t target_h) {
    const uint32_t sw = (s_width > 0) ? s_width : 1;
    const uint32_t sh = (s_height > 0) ? s_height : 1;
    for (uint32_t y = 0; y < target_h; ++y) {
        const uint32_t fy = (sh == 1) ? 0u : ((y * (sh - 1) * 256) / (target_h - 1));
        const uint32_t sy0 = fy >> 8;
        const uint32_t sy1 = (sy0 + 1 >= sh) ? sh - 1 : sy0 + 1;
        const uint32_t wy = (fy & 0xFF) >> 3; // 5 bits so x/y weights sum to 32
        for (uint32_t x = 0; x < target_w; ++x) {
            const uint32_t fx = (sw == 1) ? 0u : ((x * (sw - 1) * 256) / (target_w - 1));
            const uint32_t sx0 = fx >> 8;
            const uint32_t sx1 = (sx0 + 1 >= sw) ? sw - 1 : sx0 + 1;
            const uint32_t wx = (fx & 0xFF) >> 3;
            const uint16_t p00 = s_pixels[sy0 * s_width + sx0];
            const uint16_t p10 = s_pixels[sy0 * s_width + sx1];
            const uint16_t p01 = s_pixels[sy1 * s_width + sx0];
            const uint16_t p11 = s_pixels[sy1 * s_width + sx1];
            uint32_t out = 0;
            for (int c = 0; c < 3; ++c) {
                const uint32_t sh0 = (uint32_t)(c * 5);
                const uint32_t a = (uint32_t)((p00 >> sh0) & 0x1F);
                const uint32_t b = (uint32_t)((p10 >> sh0) & 0x1F);
                const uint32_t cc = (uint32_t)((p01 >> sh0) & 0x1F);
                const uint32_t d = (uint32_t)((p11 >> sh0) & 0x1F);
                uint32_t acc = ((a * (32 - wx) + b * wx) * (32 - wy) +
                                (cc * (32 - wx) + d * wx) * wy) / 32;
                if (acc > 31) acc = 31;
                out |= (acc << sh0);
            }
            s_scaled_buf[y * s_scaled_w + x] = (uint16_t)out;
        }
    }
}

extern "C" bool artwork_store_prescale_alloc(uint32_t target_w, uint32_t target_h) {
    const uint32_t bytes = target_w * target_h * 2u;
    if (s_scaled_buf != nullptr) {
        if (s_scaled_bytes == bytes && s_scaled_w == target_w && s_scaled_h == target_h) {
            return true; // already committed to this size: zero churn
        }
        uint8_t *p = (uint8_t *)realloc(s_scaled_buf, bytes);
        if (p == nullptr) return false;
        s_scaled_buf = p;
    } else {
        uint8_t *p = (uint8_t *)malloc(bytes);
        if (p == nullptr) return false;
        s_scaled_buf = p;
    }
    s_scaled_w = target_w;
    s_scaled_h = target_h;
    s_scaled_bytes = bytes;
    s_scaled_gen = 0; // force recompute on the next access
    return true;
}

extern "C" const uint8_t *artwork_store_prescale(uint32_t target_w, uint32_t target_h) {
    if (!s_valid || s_pixels == nullptr || s_scaled_buf == nullptr) return nullptr;
    if (target_w == 0 || target_h == 0 ||
        s_scaled_w != target_w || s_scaled_h != target_h ||
        s_scaled_bytes != target_w * target_h * 2u) {
        return nullptr; // cache not sized for this request
    }
    if (s_scaled_gen != s_generation) {
        prescale_bilinear(target_w, target_h);
        s_scaled_gen = s_generation;
    }
    return s_scaled_buf;
}

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
    // Static buffer: allocation cannot fail and never moves, so the pointer is
    // only captured once. Callers still treat nullptr as a valid failure path.
    s_pixels = s_pixels_buf;
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
    // The bilinear prescale cache is NOT allocated here. Measured 2026-10-26:
    // committing its 48,672 B heap block before the BT stacks start stole the
    // largest contiguous block (BLE init fell from largest=77,812 to 31,732)
    // and the display stage ended at 9 KB free — the A2DP build then reset 14
    // times in a churn soak. The media screen renders the native 64x64 source
    // with a nearest-neighbour transform (antialias off, no transform buffer)
    // instead; only a caller with a verified >96 KB largest-block budget may
    // enable the cache via artwork_store_prescale_alloc().
}

extern "C" void artwork_store_reset(void) {
    s_in_progress = false;
    s_expected_id = 0;
    s_expected_bytes = 0;
    s_expected_crc = 0;
    s_written = 0;
    s_crc = 0;
    s_valid = false;
    s_current_valid_id = 0;
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
        s_current_valid_id = artwork_id;
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
    s_current_valid_id = 0;
    // Bumping the generation is what makes the UI notice; without it
    // refreshArtwork() would early-return and keep painting the dead cover.
    s_generation++;
}

extern "C" bool artwork_store_valid(void) {
    return s_valid;
}

extern "C" bool artwork_store_valid_for(uint32_t artwork_id) {
    return s_valid && (artwork_id != 0) && (s_current_valid_id == artwork_id);
}

extern "C" uint32_t artwork_store_dominant_rgb(void) {
    return s_valid ? s_dominant_rgb : 0;
}
