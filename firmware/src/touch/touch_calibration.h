#ifndef DASHCUE_TOUCH_TOUCH_CALIBRATION_H
#define DASHCUE_TOUCH_TOUCH_CALIBRATION_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "calibration.h"

#define TOUCH_CAL_NUM_POINTS 5
#define TOUCH_CAL_MAGIC      0x4443414Cu // "DCAL"
#define TOUCH_CAL_VERSION    1u

#define TOUCH_MAX_MEAN_RESIDUAL_PX 6.0f
#define TOUCH_MAX_PEAK_RESIDUAL_PX 12.0f

/**
 * Five-point calibration targets.
 *
 * This used to be a second, full calibration implementation (solve/transform/
 * NVS load/save) duplicating the C API in calibration.cpp — two solvers, two
 * residual policies, one of which was never called. The only thing the UI ever
 * used was the target coordinates, so that is all that remains here; the live
 * solve/apply/NVS path is the single implementation in calibration.cpp.
 */
class TouchCalibration {
public:
    /** Reference screen coordinate for target index 0..4 (clamped to 0). */
    static void getTargetCoordinate(uint8_t index, int16_t &sx, int16_t &sy) {
        static const int16_t kTargets[TOUCH_CAL_NUM_POINTS][2] = {
            { 40,  40},    // P0: Top-Left
            {440,  40},    // P1: Top-Right
            {240, 160},    // P2: Center
            { 40, 280},    // P3: Bottom-Left
            {440, 280}     // P4: Bottom-Right
        };
        if (index >= TOUCH_CAL_NUM_POINTS) index = 0;
        sx = kTargets[index][0];
        sy = kTargets[index][1];
    }
};

#endif // DASHCUE_TOUCH_TOUCH_CALIBRATION_H
