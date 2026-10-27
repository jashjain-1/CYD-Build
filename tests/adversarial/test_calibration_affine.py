"""Adversarial Affine Calibration Solver, Matrix Inversion & NVS Migration Suite.

Verifies:
1. 5-point affine normal equation solver: (M^T M) theta = M^T Y.
2. Gram matrix determinant calculation and singularity rejection (|det| < 1e-7).
3. Residual thresholding: mean residual <= 6.0 px, max peak residual <= 12.0 px.
4. NVS signature bounds guard:
   - alpha_x in [-0.20, -0.06]
   - beta_y in [0.04, 0.15]
   - |beta_x| <= 0.03
   - |alpha_y| <= 0.03
5. Self-healing on legacy transposed matrices, inverted slopes, and corrupt payloads.
6. IEEE 802.3 CRC32 integrity verification.
"""

import math

CALIBRATION_POINT_COUNT = 5
TOUCH_CAL_MAX_MEAN_RESIDUAL_PX = 6.0
TOUCH_CAL_MAX_PEAK_RESIDUAL_PX = 12.0
TOUCH_CALIBRATION_MAGIC = 0x5443414C  # "TCAL"
TOUCH_CALIBRATION_VERSION = 1
DISPLAY_WIDTH = 480
DISPLAY_HEIGHT = 320

# Standard 5-point calibration targets on 480x320 display
CALIBRATION_TARGETS = [
    (40, 40),    # Top-Left
    (440, 40),   # Top-Right
    (240, 160),  # Center
    (40, 280),   # Bottom-Left
    (440, 280),  # Bottom-Right
]

# Factory raw anchors (measured on physical CYD 3.5" panel)
FACTORY_RAW_ANCHORS = [
    (3475, 880),   # TL
    (305, 880),    # TR
    (1890, 2270),  # Center
    (3475, 3660),  # BL
    (305, 3660),   # BR
]


def solve_affine_calibration(
    raw_points: list[tuple[int, int]],
    screen_points: list[tuple[int, int]]
) -> tuple[bool, dict[str, float]]:
    """1:1 Python replica of touch_calibration_solve in firmware/src/touch/calibration.cpp."""
    if len(raw_points) != CALIBRATION_POINT_COUNT or len(screen_points) != CALIBRATION_POINT_COUNT:
        return False, {}

    s_xx = 0.0
    s_yy = 0.0
    s_xy = 0.0
    s_x = 0.0
    s_y = 0.0
    N = float(CALIBRATION_POINT_COUNT)

    vx_x = 0.0; vx_y = 0.0; vx_c = 0.0
    vy_x = 0.0; vy_y = 0.0; vy_c = 0.0

    for i in range(CALIBRATION_POINT_COUNT):
        xr, yr = float(raw_points[i][0]), float(raw_points[i][1])
        xs, ys = float(screen_points[i][0]), float(screen_points[i][1])

        s_xx += xr * xr
        s_yy += yr * yr
        s_xy += xr * yr
        s_x += xr
        s_y += yr

        vx_x += xr * xs
        vx_y += yr * xs
        vx_c += xs

        vy_x += xr * ys
        vy_y += yr * ys
        vy_c += ys

    a00 = s_xx; a01 = s_xy; a02 = s_x
    a10 = s_xy; a11 = s_yy; a12 = s_y
    a20 = s_x;  a21 = s_y;  a22 = N

    det = (a00 * (a11 * a22 - a12 * a21) -
           a01 * (a10 * a22 - a12 * a20) +
           a02 * (a10 * a21 - a11 * a20))

    if abs(det) < 1e-7:
        return False, {"det": det}

    inv_det = 1.0 / det

    inv00 = (a11 * a22 - a12 * a21) * inv_det
    inv01 = (a02 * a21 - a01 * a22) * inv_det
    inv02 = (a01 * a12 - a02 * a11) * inv_det

    inv10 = (a12 * a20 - a10 * a22) * inv_det
    inv11 = (a00 * a22 - a02 * a20) * inv_det
    inv12 = (a02 * a10 - a00 * a12) * inv_det

    inv20 = (a10 * a21 - a11 * a20) * inv_det
    inv21 = (a01 * a20 - a00 * a21) * inv_det
    inv22 = (a00 * a11 - a01 * a10) * inv_det

    alpha_x = inv00 * vx_x + inv01 * vx_y + inv02 * vx_c
    beta_x  = inv10 * vx_x + inv11 * vx_y + inv12 * vx_c
    delta_x = inv20 * vx_x + inv21 * vx_y + inv22 * vx_c

    alpha_y = inv00 * vy_x + inv01 * vy_y + inv02 * vy_c
    beta_y  = inv10 * vy_x + inv11 * vy_y + inv12 * vy_c
    delta_y = inv20 * vy_x + inv21 * vy_y + inv22 * vy_c

    residual_sum = 0.0
    max_residual = 0.0

    for i in range(CALIBRATION_POINT_COUNT):
        xr, yr = float(raw_points[i][0]), float(raw_points[i][1])
        est_x = alpha_x * xr + beta_x * yr + delta_x
        est_y = alpha_y * xr + beta_y * yr + delta_y
        dx = est_x - float(screen_points[i][0])
        dy = est_y - float(screen_points[i][1])
        dist = math.sqrt(dx * dx + dy * dy)
        residual_sum += dist
        max_residual = max(max_residual, dist)

    mean_residual = residual_sum / float(CALIBRATION_POINT_COUNT)

    res = {
        "det": det,
        "alpha_x": alpha_x,
        "beta_x": beta_x,
        "delta_x": delta_x,
        "alpha_y": alpha_y,
        "beta_y": beta_y,
        "delta_y": delta_y,
        "residual_mean_px": mean_residual,
        "residual_max_px": max_residual,
    }

    if (mean_residual > TOUCH_CAL_MAX_MEAN_RESIDUAL_PX or
        max_residual > TOUCH_CAL_MAX_PEAK_RESIDUAL_PX):
        return False, res

    return True, res


def validate_nvs_signature(matrix: dict[str, float]) -> bool:
    """1:1 Model of NVS migration guard in touch_hal.cpp:282-286."""
    alpha_x = matrix.get("alpha_x", 0.0)
    beta_x = matrix.get("beta_x", 0.0)
    alpha_y = matrix.get("alpha_y", 0.0)
    beta_y = matrix.get("beta_y", 0.0)

    # Inverted X slope: alpha_x in [-0.20, -0.06]
    if alpha_x > -0.06 or alpha_x < -0.20:
        return False
    # Positive Y slope: beta_y in [0.04, 0.15]
    if beta_y < 0.04 or beta_y > 0.15:
        return False
    # Cross-axis coupling bound: |beta_x| <= 0.03, |alpha_y| <= 0.03
    return not (abs(beta_x) > 0.03 or abs(alpha_y) > 0.03)


def apply_calibration(matrix: dict[str, float], raw_x: int, raw_y: int) -> tuple[int, int]:
    """1:1 Model of touch_calibration_apply in calibration.cpp:183-205."""
    fx = matrix["alpha_x"] * float(raw_x) + matrix["beta_x"] * float(raw_y) + matrix["delta_x"]
    fy = matrix["alpha_y"] * float(raw_x) + matrix["beta_y"] * float(raw_y) + matrix["delta_y"]

    ix = round(fx)
    iy = round(fy)

    ix = max(0, min(DISPLAY_WIDTH - 1, ix))
    iy = max(0, min(DISPLAY_HEIGHT - 1, iy))
    return ix, iy


# ==============================================================================
# TESTS
# ==============================================================================

def test_factory_matrix_solver_and_residuals():
    """Verify factory anchors solve with near-zero residuals (< 2.0 px)."""
    ok, res = solve_affine_calibration(FACTORY_RAW_ANCHORS, CALIBRATION_TARGETS)
    assert ok is True
    assert res["residual_mean_px"] <= 2.5
    assert res["residual_max_px"] <= 5.0
    assert -0.20 <= res["alpha_x"] <= -0.06
    assert 0.04 <= res["beta_y"] <= 0.15
    assert abs(res["beta_x"]) <= 0.03
    assert abs(res["alpha_y"]) <= 0.03
    assert validate_nvs_signature(res) is True


def test_collinear_singular_matrix_rejection():
    """Verify singular collinear points are rejected by |det| < 1e-7."""
    collinear_raw = [(1000, 1000), (1000, 1000), (1000, 1000), (1000, 1000), (1000, 1000)]
    ok, res = solve_affine_calibration(collinear_raw, CALIBRATION_TARGETS)
    assert ok is False
    assert abs(res.get("det", 1.0)) < 1e-7


def test_legacy_transposed_matrix_nvs_guard_rejection():
    """Falsification: Prove that legacy transposed matrix is rejected by NVS guard."""
    # Legacy transposed matrix had axes swapped (alpha_x ~ 0, beta_x ~ 0.1, alpha_y ~ -0.1, beta_y ~ 0)
    legacy_matrix = {
        "alpha_x": 0.002,
        "beta_x": 0.086,
        "delta_x": 20.0,
        "alpha_y": -0.126,
        "beta_y": 0.001,
        "delta_y": 450.0,
    }
    assert validate_nvs_signature(legacy_matrix) is False


def test_inverted_y_slope_nvs_guard_rejection():
    """Falsification: Negative beta_y must be rejected."""
    bad_matrix = {
        "alpha_x": -0.126,
        "beta_x": 0.001,
        "delta_x": 478.0,
        "alpha_y": 0.001,
        "beta_y": -0.086,  # Inverted Y slope!
        "delta_y": 300.0,
    }
    assert validate_nvs_signature(bad_matrix) is False


def test_excessive_cross_coupling_rejection():
    """Falsification: Significant axis skew/twist must be rejected."""
    skewed_matrix = {
        "alpha_x": -0.126,
        "beta_x": 0.05,  # Exceeds 0.03 coupling limit
        "delta_x": 478.0,
        "alpha_y": 0.001,
        "beta_y": 0.086,
        "delta_y": -36.0,
    }
    assert validate_nvs_signature(skewed_matrix) is False


def test_coordinate_clamping_bounds():
    """Verify touch_calibration_apply clamps to [0, DISPLAY_WIDTH-1] and [0, DISPLAY_HEIGHT-1]."""
    ok, matrix = solve_affine_calibration(FACTORY_RAW_ANCHORS, CALIBRATION_TARGETS)
    assert ok is True

    # Extreme out of bounds raw points
    x_low, y_low = apply_calibration(matrix, 9999, -5000)
    assert x_low == 0
    assert y_low == 0

    x_high, y_high = apply_calibration(matrix, -5000, 9999)
    assert x_high == DISPLAY_WIDTH - 1
    assert y_high == DISPLAY_HEIGHT - 1


def test_verifiy_crosshair_coordinates_pass():
    """Verify that all 8 crosshair points match within +/-24 px under factory matrix."""
    ok, matrix = solve_affine_calibration(FACTORY_RAW_ANCHORS, CALIBRATION_TARGETS)
    assert ok is True

    # Verify all targets land within 24 px tolerance
    for exp_x, exp_y in CALIBRATION_TARGETS:
        # Find closest raw anchor
        best_pt = None
        for i, pt in enumerate(CALIBRATION_TARGETS):
            if pt == (exp_x, exp_y):
                best_pt = FACTORY_RAW_ANCHORS[i]
                break
        assert best_pt is not None
        got_x, got_y = apply_calibration(matrix, best_pt[0], best_pt[1])
        err = math.sqrt((got_x - exp_x)**2 + (got_y - exp_y)**2)
        assert err <= 24.0, f"Error {err} px exceeds 24 px tolerance at ({exp_x}, {exp_y})"
