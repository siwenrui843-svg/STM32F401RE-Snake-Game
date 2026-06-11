/*
 * Include the gesture detection header.
 */
#include "../src/hfiles/gesture.h"

/*
 * Include the ARM math library for atan2f() and sqrtf().
 *
 * The STM32F401RE contains a single-precision FPU, so float
 * operations are efficient.
 */
#include <math.h>

/*
 * ---------------------------------------------------------------------------
 * Approximate value of PI as a single-precision float.
 * ---------------------------------------------------------------------------
 */
#define PI_F  3.14159265f

/*
 * ---------------------------------------------------------------------------
 * Constant for converting radians to degrees.
 *
 * degrees = radians * (180 / PI)
 * ---------------------------------------------------------------------------
 */
#define RAD_TO_DEG  (180.0f / PI_F)

/*
 * ---------------------------------------------------------------------------
 * Number of consecutive identical tilt readings required before
 * a direction change is accepted.
 *
 * This debouncing prevents the Snake from changing direction
 * due to momentary sensor noise or hand vibration.
 * ---------------------------------------------------------------------------
 */
#define DEBOUNCE_COUNT_NEEDED  3U

/*
 * ---------------------------------------------------------------------------
 * Public functions.
 * ---------------------------------------------------------------------------
 */

/*
 * Initialises the gesture detection state.
 *
 * See the header file for full documentation.
 */
void Gesture_Init(Gesture_t *g)
{
    if (g == ((void *)0))
    {
        return;
    }

    g->roll_offset         = 0.0f;
    g->pitch_offset        = 0.0f;
    g->filtered_roll       = 0.0f;
    g->filtered_pitch      = 0.0f;
    g->last_tilt_direction = DIR_RIGHT;
    g->calibrated          = 0;
    g->debounce_count      = 0;
    g->candidate_direction = DIR_RIGHT;
}

/*
 * Computes the roll angle from raw accelerometer readings.
 *
 * See the header file for full documentation.
 */
float Gesture_ComputeRoll(int16_t ay, int16_t az)
{
    /*
     * roll = atan2(ay, az) * 180 / PI
     *
     * This gives the rotation angle around the x-axis in degrees.
     */
    return atan2f((float)ay, (float)az) * RAD_TO_DEG;
}

/*
 * Computes the pitch angle from raw accelerometer readings.
 *
 * See the header file for full documentation.
 */
float Gesture_ComputePitch(int16_t ax, int16_t ay, int16_t az)
{
    float denom;

    /*
     * denom = sqrt(ay^2 + az^2)
     */
    denom = sqrtf((float)ay * (float)ay + (float)az * (float)az);

    /*
     * Avoid division by zero. If the board is in free-fall or the
     * sensor is not connected, denom could be zero.
     */
    if (denom < 0.01f)
    {
        return 0.0f;
    }

    /*
     * pitch = atan2(-ax, sqrt(ay^2 + az^2)) * 180 / PI
     */
    return atan2f((float)(-ax), denom) * RAD_TO_DEG;
}

/*
 * Performs calibration by storing the current tilt as the zero reference.
 *
 * See the header file for full documentation.
 */
void Gesture_Calibrate(Gesture_t *g, int16_t ax, int16_t ay, int16_t az)
{
    float raw_roll;
    float raw_pitch;

    if (g == ((void *)0))
    {
        return;
    }

    /*
     * Compute the current raw roll and pitch angles.
     */
    raw_roll  = Gesture_ComputeRoll(ay, az);
    raw_pitch = Gesture_ComputePitch(ax, ay, az);

    /*
     * Store these as the calibration offsets.
     * Future readings will subtract these offsets.
     */
    g->roll_offset  = raw_roll;
    g->pitch_offset = raw_pitch;

    /*
     * Reset the filtered values to zero (calibrated state).
     */
    g->filtered_roll  = 0.0f;
    g->filtered_pitch = 0.0f;

    /*
     * Reset the direction state.
     */
    g->last_tilt_direction = DIR_RIGHT;
    g->debounce_count      = 0;
    g->candidate_direction = DIR_RIGHT;

    g->calibrated = 1;
}

/*
 * Updates the gesture detector with new raw accelerometer data.
 *
 * See the header file for full documentation.
 */
SnakeDirection Gesture_Update(Gesture_t *g, int16_t ax, int16_t ay, int16_t az)
{
    float raw_roll;
    float raw_pitch;
    float cal_roll;
    float cal_pitch;
    float abs_roll;
    float abs_pitch;
    SnakeDirection new_dir;

    if (g == ((void *)0))
    {
        return DIR_RIGHT;
    }

    /*
     * If the system has not been calibrated, return the current
     * direction unchanged.
     */
    if (!g->calibrated)
    {
        return g->last_tilt_direction;
    }

    /*
     * Step 1: Compute raw roll and pitch from the accelerometer.
     */
    raw_roll  = Gesture_ComputeRoll(ay, az);
    raw_pitch = Gesture_ComputePitch(ax, ay, az);

    /*
     * Step 2: Apply calibration offsets.
     */
    cal_roll  = raw_roll  - g->roll_offset;
    cal_pitch = raw_pitch - g->pitch_offset;

    /*
     * Step 3: Apply low-pass filter for smoothing.
     *
     * filtered = alpha * new + (1 - alpha) * previous_filtered
     */
    g->filtered_roll  = TILT_FILTER_ALPHA * cal_roll
                      + (1.0f - TILT_FILTER_ALPHA) * g->filtered_roll;

    g->filtered_pitch = TILT_FILTER_ALPHA * cal_pitch
                      + (1.0f - TILT_FILTER_ALPHA) * g->filtered_pitch;

    /*
     * Step 4: Determine whether the tilt exceeds the dead zone
     *         and threshold.
     *
     * Use the absolute value of the filtered angles.
     */
    abs_roll  = g->filtered_roll;
    abs_pitch = g->filtered_pitch;

    if (abs_roll  < 0.0f) { abs_roll  = -abs_roll;  }
    if (abs_pitch < 0.0f) { abs_pitch = -abs_pitch; }

    /*
     * If neither axis exceeds the threshold, no direction change.
     * Return the last direction to maintain current movement.
     */
    if (abs_roll < TILT_THRESHOLD_DEG && abs_pitch < TILT_THRESHOLD_DEG)
    {
        /*
         * Both axes are within the dead zone / below threshold.
         * Keep the current direction. Reset any partial debounce.
         */
        g->debounce_count      = 0;
        g->candidate_direction = g->last_tilt_direction;
        return g->last_tilt_direction;
    }

    /*
     * Step 5: Determine the dominant tilt axis.
     *
     * The axis with the larger absolute angle determines the
     * direction of movement.
     */
    if (abs_pitch > abs_roll)
    {
        /*
         * Pitch is dominant — move UP or DOWN.
         *
         * Positive pitch (board tilted forward / away from user)
         * → Snake moves UP.
         *
         * Negative pitch (board tilted backward / toward user)
         * → Snake moves DOWN.
         */
        if (g->filtered_pitch > TILT_THRESHOLD_DEG)
        {
            new_dir = DIR_UP;
        }
        else
        {
            new_dir = DIR_DOWN;
        }
    }
    else
    {
        /*
         * Roll is dominant — move LEFT or RIGHT.
         *
         * Positive roll (board tilted right)
         * → Snake moves RIGHT.
         *
         * Negative roll (board tilted left)
         * → Snake moves LEFT.
         */
        if (g->filtered_roll > TILT_THRESHOLD_DEG)
        {
            new_dir = DIR_RIGHT;
        }
        else
        {
            new_dir = DIR_LEFT;
        }
    }

    /*
     * Step 6: Debounce the direction change.
     *
     * The same direction must be read for several consecutive
     * updates before it is accepted.
     */
    if (new_dir == g->candidate_direction)
    {
        g->debounce_count++;

        if (g->debounce_count >= DEBOUNCE_COUNT_NEEDED)
        {
            /*
             * Direction change is confirmed.
             *
             * Only update if the direction is actually different
             * from the last applied direction.
             */
            if (new_dir != g->last_tilt_direction)
            {
                g->last_tilt_direction = new_dir;
            }
            /*
             * Keep the debounce count at the threshold so that
             * further identical readings are also accepted.
             */
            g->debounce_count = DEBOUNCE_COUNT_NEEDED;
        }
    }
    else
    {
        /*
         * The direction has changed since the last reading.
         * Start a new debounce sequence.
         */
        g->candidate_direction = new_dir;
        g->debounce_count      = 1;
    }

    return g->last_tilt_direction;
}

/*
 * Returns the current direction from the gesture detector.
 *
 * See the header file for full documentation.
 */
SnakeDirection Gesture_GetDirection(Gesture_t *g)
{
    if (g == ((void *)0))
    {
        return DIR_RIGHT;
    }
    return g->last_tilt_direction;
}

/*
 * Checks whether the gesture detector has a new direction available.
 *
 * See the header file for full documentation.
 */
uint8_t Gesture_HasNewDirection(const Gesture_t *g)
{
    if (g == ((void *)0))
    {
        return 0;
    }
    /*
     * The gesture detector always provides a direction (the last
     * determined tilt direction). The calling application code
     * compares this against the game's current direction to decide
     * whether a change is needed.
     */
    return 1;
}

/*
 * Returns the filtered roll angle.
 *
 * See the header file for full documentation.
 */
float Gesture_GetRoll(const Gesture_t *g)
{
    if (g == ((void *)0))
    {
        return 0.0f;
    }
    return g->filtered_roll;
}

/*
 * Returns the filtered pitch angle.
 *
 * See the header file for full documentation.
 */
float Gesture_GetPitch(const Gesture_t *g)
{
    if (g == ((void *)0))
    {
        return 0.0f;
    }
    return g->filtered_pitch;
}
