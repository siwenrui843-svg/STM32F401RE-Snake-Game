#ifndef GESTURE_H
#define GESTURE_H

#include <stdint.h>
#include "snake_game.h"

/*
 * Tilt threshold in degrees.
 *
 * When the tilt angle (after calibration) exceeds this threshold,
 * a direction change is triggered.
 *
 * A smaller value makes the control more sensitive.
 * A larger value reduces accidental direction changes.
 */
#define TILT_THRESHOLD_DEG  15.0f

/*
 * Dead-zone half-width in degrees.
 *
 * Angles within ±DEAD_ZONE_DEG of zero are considered neutral and
 * do not produce any direction command. This prevents the Snake from
 * moving when the board is held level.
 */
#define DEAD_ZONE_DEG  5.0f

/*
 * Low-pass filter coefficient for smoothing tilt angles.
 *
 * filtered = alpha * new_angle + (1 - alpha) * previous_filtered
 *
 * alpha = 0.0 → fully smooth, no response to changes
 * alpha = 1.0 → no filtering, raw value
 *
 * A value of 0.15 provides moderate smoothing suitable for hand-held
 * tilt control.
 */
#define TILT_FILTER_ALPHA  0.15f

/*
 * Structure that holds the tilt-gesture processing state.
 *
 * This includes calibration offsets, filtered angles, and the
 * last direction command sent from tilt input.
 */
typedef struct {
    /*
     * Calibration offset for the roll angle in degrees.
     *
     * During calibration the current roll is stored as the zero reference.
     * Subsequent readings subtract this offset.
     */
    float roll_offset;

    /*
     * Calibration offset for the pitch angle in degrees.
     */
    float pitch_offset;

    /*
     * Current filtered roll angle (degrees), with calibration applied.
     */
    float filtered_roll;

    /*
     * Current filtered pitch angle (degrees), with calibration applied.
     */
    float filtered_pitch;

    /*
     * Last direction command issued by the tilt gesture detector.
     *
     * This prevents repeated direction changes when the board is held
     * at a steady tilt.
     */
    SnakeDirection last_tilt_direction;

    /*
     * Calibration complete flag.
     *
     * 0 - not yet calibrated.
     * 1 - calibration offsets have been recorded.
     */
    uint8_t calibrated;

    /*
     * Counter used to debounce direction changes.
     *
     * A new tilt direction must be held for several consecutive readings
     * before the direction change is accepted.
     */
    uint8_t debounce_count;

    /*
     * The candidate direction during debouncing.
     */
    SnakeDirection candidate_direction;

} Gesture_t;

/*
 * Initialises the gesture detection module.
 *
 * Parameters:
 *   g - Pointer to the Gesture_t structure to initialise.
 *
 * After initialisation, the module is not yet calibrated.
 * Gesture_Calibrate() must be called before tilt directions
 * can be reliably detected.
 */
void Gesture_Init(Gesture_t *g);

/*
 * Performs MPU6500 calibration by recording the current roll and pitch
 * angles as the zero reference.
 *
 * Parameters:
 *   g    - Pointer to the gesture state.
 *   ax   - Raw x-axis accelerometer reading.
 *   ay   - Raw y-axis accelerometer reading.
 *   az   - Raw z-axis accelerometer reading.
 *
 * The current roll and pitch are computed and stored as offsets.
 * Subsequent calls to Gesture_Update() will subtract these offsets.
 */
void Gesture_Calibrate(Gesture_t *g, int16_t ax, int16_t ay, int16_t az);

/*
 * Computes the roll angle from raw accelerometer readings.
 *
 * Parameters:
 *   ay - Raw y-axis accelerometer reading.
 *   az - Raw z-axis accelerometer reading.
 *
 * Return value:
 *   Roll angle in degrees.
 *
 * Roll is rotation around the x-axis.
 * Formula: roll = atan2(ay, az) * 180 / PI
 */
float Gesture_ComputeRoll(int16_t ay, int16_t az);

/*
 * Computes the pitch angle from raw accelerometer readings.
 *
 * Parameters:
 *   ax - Raw x-axis accelerometer reading.
 *   ay - Raw y-axis accelerometer reading.
 *   az - Raw z-axis accelerometer reading.
 *
 * Return value:
 *   Pitch angle in degrees.
 *
 * Pitch is rotation around the y-axis.
 * Formula: pitch = atan2(-ax, sqrt(ay*ay + az*az)) * 180 / PI
 */
float Gesture_ComputePitch(int16_t ax, int16_t ay, int16_t az);

/*
 * Updates the gesture detector with a new set of raw accelerometer readings.
 *
 * Parameters:
 *   g  - Pointer to the gesture state.
 *   ax - Raw x-axis accelerometer reading.
 *   ay - Raw y-axis accelerometer reading.
 *   az - Raw z-axis accelerometer reading.
 *
 * Return value:
 *   The Snake direction determined from the current tilt, or
 *   DIR_RIGHT (255) with a special flag if no direction change
 *   is requested.
 *
 * This function:
 * 1. Computes roll and pitch from the raw accelerometer values.
 * 2. Applies calibration offsets.
 * 3. Applies a low-pass filter for smoothing.
 * 4. Applies dead-zone logic.
 * 5. Determines the dominant tilt axis and maps it to a Snake direction.
 *
 * The caller should check the returned direction using
 * Gesture_HasNewDirection() before applying it.
 */
SnakeDirection Gesture_Update(Gesture_t *g, int16_t ax, int16_t ay, int16_t az);

/*
 * Checks whether the gesture detector has a new direction command.
 *
 * Parameters:
 *   g - Pointer to the gesture state.
 *
 * Return value:
 *   1 if a new direction is available, 0 otherwise.
 *
 * This function should be called after Gesture_Update() to determine
 * whether the Snake direction should be changed.
 */
uint8_t Gesture_HasNewDirection(const Gesture_t *g);

/*
 * Returns the new direction from the gesture detector and clears the
 * internal new-direction flag.
 *
 * Parameters:
 *   g - Pointer to the gesture state.
 *
 * Return value:
 *   The Snake direction determined by tilt.
 */
SnakeDirection Gesture_GetDirection(Gesture_t *g);

/*
 * Returns the current filtered and calibrated roll angle.
 *
 * Parameters:
 *   g - Pointer to the gesture state.
 *
 * Return value:
 *   Filtered roll angle in degrees.
 */
float Gesture_GetRoll(const Gesture_t *g);

/*
 * Returns the current filtered and calibrated pitch angle.
 *
 * Parameters:
 *   g - Pointer to the gesture state.
 *
 * Return value:
 *   Filtered pitch angle in degrees.
 */
float Gesture_GetPitch(const Gesture_t *g);

#endif /* GESTURE_H */
