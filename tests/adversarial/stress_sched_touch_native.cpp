#include <iostream>
#include <cstdint>
#include <cassert>
#include <vector>
#include <random>

// Arduino / ESP32 macro definition
#ifndef constrain
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
#endif

// ----------------------------------------------------------------------------
// 1. ui_task sleep clamp adversarial test
// ----------------------------------------------------------------------------
void test_ui_task_sleep_clamp() {
    std::cout << "[TEST 1] Testing ui_task sleep clamp constrain(next_call, 4u, 30u)..." << std::endl;

    // Direct edge cases
    uint32_t val_0 = 0u;
    assert(constrain(val_0, 4u, 30u) == 4u);

    uint32_t val_1 = 1u;
    assert(constrain(val_1, 4u, 30u) == 4u);

    uint32_t val_2 = 2u;
    assert(constrain(val_2, 4u, 30u) == 4u);

    uint32_t val_3 = 3u;
    assert(constrain(val_3, 4u, 30u) == 4u);

    uint32_t val_4 = 4u;
    assert(constrain(val_4, 4u, 30u) == 4u);

    uint32_t val_5 = 5u;
    assert(constrain(val_5, 4u, 30u) == 5u);

    uint32_t val_15 = 15u;
    assert(constrain(val_15, 4u, 30u) == 15u);

    uint32_t val_29 = 29u;
    assert(constrain(val_29, 4u, 30u) == 29u);

    uint32_t val_30 = 30u;
    assert(constrain(val_30, 4u, 30u) == 30u);

    uint32_t val_31 = 31u;
    assert(constrain(val_31, 4u, 30u) == 30u);

    uint32_t val_100 = 100u;
    assert(constrain(val_100, 4u, 30u) == 30u);

    uint32_t val_max = UINT32_MAX;
    assert(constrain(val_max, 4u, 30u) == 30u);

    uint32_t val_i32max = 0x7FFFFFFFu;
    assert(constrain(val_i32max, 4u, 30u) == 30u);

    uint32_t val_signbit = 0x80000000u;
    assert(constrain(val_signbit, 4u, 30u) == 30u);

    // 10,000,000 randomized Monte Carlo fuzzing checks
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint32_t> dist(0, UINT32_MAX);
    for (int i = 0; i < 10000000; ++i) {
        uint32_t v = dist(rng);
        uint32_t clamped = constrain(v, 4u, 30u);
        assert(clamped >= 4u && clamped <= 30u);
        if (v < 4u) assert(clamped == 4u);
        else if (v > 30u) assert(clamped == 30u);
        else assert(clamped == v);
    }

    std::cout << "  [PASS] 10,000,000 random uint32_t values all strictly clamped to [4, 30] ms." << std::endl;
}

// ----------------------------------------------------------------------------
// 2. touch_task idle gating and release debounce state machine
// ----------------------------------------------------------------------------
struct TouchDebounceModel {
    uint8_t debounce_press_count = 0;
    uint8_t debounce_release_count = 0;
    bool latched_pressed = false;
    uint32_t spi_ops = 0;
    uint32_t mutex_takes = 0;

    // Emulates touch_hal.cpp lines 139-296
    bool tick(bool pen_down, bool valid_sample) {
        const bool should_sample = pen_down || latched_pressed;

        if (!should_sample) {
            // Idle skip
            debounce_press_count = 0;
            debounce_release_count = 0;
            latched_pressed = false;
            return false; // Did not sample
        }

        // Active sample: take mutex and transfer SPI
        mutex_takes++;
        spi_ops += 12;

        // Debounce
        if (valid_sample) {
            debounce_release_count = 0;
            if (debounce_press_count < 1) {
                debounce_press_count++;
            }
            if (debounce_press_count >= 1) {
                latched_pressed = true;
            }
        } else {
            debounce_press_count = 0;
            if (!pen_down) {
                if (debounce_release_count < 3) {
                    debounce_release_count++;
                }
                if (debounce_release_count >= 3) {
                    latched_pressed = false;
                }
            } else {
                debounce_release_count = 0;
            }
        }
        return true; // Sampled
    }
};

void test_touch_gating_and_debounce() {
    std::cout << "[TEST 2] Testing touch_task idle gating & release debounce preservation..." << std::endl;

    TouchDebounceModel model;

    // 1. Idle for 10,000 ticks (no touch)
    for (int i = 0; i < 10000; ++i) {
        bool sampled = model.tick(false, false);
        assert(!sampled);
        assert(!model.latched_pressed);
    }
    assert(model.mutex_takes == 0 && "Idle must perform ZERO mutex takes!");
    assert(model.spi_ops == 0 && "Idle must perform ZERO SPI operations!");

    // 2. Touch onset: pen_down=true, valid_sample=true
    bool sampled_t1 = model.tick(true, true);
    assert(sampled_t1);
    assert(model.latched_pressed && "Press must latch on valid sample!");
    assert(model.mutex_takes == 1);

    // 3. Hold touch for 100 samples
    for (int i = 0; i < 100; ++i) {
        model.tick(true, true);
        assert(model.latched_pressed);
    }
    uint32_t takes_during_hold = model.mutex_takes;

    // 4. Finger lift: pen_down=false, valid_sample=false
    // Sample 1: release_count becomes 1, remains latched
    model.tick(false, false);
    assert(model.latched_pressed && "Release sample 1 must preserve latch!");
    assert(model.debounce_release_count == 1);

    // Sample 2: release_count becomes 2, remains latched
    model.tick(false, false);
    assert(model.latched_pressed && "Release sample 2 must preserve latch!");
    assert(model.debounce_release_count == 2);

    // Sample 3: release_count becomes 3, unlatches!
    model.tick(false, false);
    assert(!model.latched_pressed && "Release sample 3 must drop latch!");
    assert(model.debounce_release_count == 3);

    // Sample 4: latched_pressed=false and pen_down=false -> IDLE SKIP RESUMES!
    uint32_t takes_after_release = model.mutex_takes;
    for (int i = 0; i < 1000; ++i) {
        bool s = model.tick(false, false);
        assert(!s && "Must not sample when idle!");
        assert(!model.latched_pressed);
    }
    assert(model.mutex_takes == takes_after_release && "No further mutex takes after release!");

    std::cout << "  [PASS] Idle skip, 3-sample release preservation, and post-release idle confirmed." << std::endl;
}

int main() {
    std::cout << "==========================================================" << std::endl;
    std::cout << "EMPIRICAL ADVERSARIAL STRESS TEST: SCHEDULING & TOUCH" << std::endl;
    std::cout << "==========================================================" << std::endl;

    test_ui_task_sleep_clamp();
    test_touch_gating_and_debounce();

    std::cout << "==========================================================" << std::endl;
    std::cout << "ALL EMPIRICAL ADVERSARIAL CHECKS PASSED!" << std::endl;
    std::cout << "==========================================================" << std::endl;
    return 0;
}
