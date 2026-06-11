#ifndef APP_H
#define APP_H

#include <stdint.h>
#include "snake_game.h"
#include "gesture.h"

/*
 * Application state enumeration.
 *
 * The application state machine uses these states to manage
 * the full user experience from startup to gameplay.
 */
typedef enum {
    /*
     * Startup sequence: shows group number, member details,
     * and initial temperature before transitioning to the menu.
     */
    APP_STATE_STARTUP = 0,

    /*
     * Main menu: the user can select control mode, calibrate
     * the MPU6500, or start a game.
     */
    APP_STATE_MENU = 1,

    /*
     * MPU6500 calibration: the system records the current tilt
     * angles as the zero reference for tilt-controlled gameplay.
     */
    APP_STATE_CALIBRATE = 2,

    /*
     * Game is running: the Snake moves, the display updates,
     * and user input (buttons or tilt) is processed.
     */
    APP_STATE_PLAYING = 3,

    /*
     * Game is paused: the Snake freezes and the pause screen
     * is displayed. The user can resume or return to the menu.
     */
    APP_STATE_PAUSED = 4,

    /*
     * Game over: the final score is shown. The user can restart
     * or return to the menu.
     */
    APP_STATE_GAME_OVER = 5
} AppState;

/*
 * Control mode enumeration.
 */
typedef enum {
    CONTROL_MODE_BUTTON = 0,
    CONTROL_MODE_TILT   = 1
} ControlMode;

/*
 * Main menu option enumeration.
 */
typedef enum {
    MENU_OPTION_START_BUTTON = 0,
    MENU_OPTION_START_TILT   = 1,
    MENU_OPTION_CALIBRATE    = 2,
    MENU_OPTION_COUNT        = 3
} MenuOption;

/*
 * Main application structure.
 *
 * Holds all state for the application, including the current state
 * machine state, the Snake game, gesture detector, and timing data.
 */
typedef struct {
    /*
     * Current application state.
     */
    AppState state;

    /*
     * Snake game instance.
     */
    SnakeGame_t game;

    /*
     * Gesture detector for tilt control.
     */
    Gesture_t gesture;

    /*
     * Currently selected control mode.
     */
    ControlMode control_mode;

    /*
     * Current temperature reading (value × 10).
     */
    int temperature_x10;

    /*
     * Currently selected menu option index.
     */
    uint8_t menu_selection;

    /*
     * Last game's final score (displayed on LCD after game over).
     */
    uint16_t last_game_score;

    /*
     * Startup sub-stage counter.
     *
     * Used to sequence through the startup screens.
     */
    uint8_t startup_stage;

    /*
     * Timestamp (from msTicks) of the last state transition,
     * used for timing within each state.
     */
    uint32_t state_entry_time;

    /*
     * Timestamps for scheduling periodic tasks within the
     * PLAYING state (non-blocking timing).
     */
    uint32_t last_mpu_time;
    uint32_t last_button_time;
    uint32_t last_snake_time;
    uint32_t last_temp_time;
    uint32_t last_lcd_time;
    uint32_t last_oled_time;

    /*
     * Raw MPU6500 accelerometer readings, updated at ~50 Hz.
     */
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;

    /*
     * Raw MPU6500 gyroscope readings (for potential future use).
     */
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;

    /*
     * MPU6500 WHO_AM_I value.
     * 112 (0x70) indicates the sensor is present.
     */
    uint8_t mpu_whoami;

    /*
     * DS18S20 temperature conversion ready flag (mirror/copy).
     */
    uint8_t temp_ready;

} AppContext_t;

/*
 * Initialises the application context.
 *
 * Parameters:
 *   app - Pointer to the AppContext_t structure to initialise.
 *
 * This function sets the application to the STARTUP state,
 * initialises the Snake game and gesture detector, and
 * prepares the startup sequence.
 */
void App_Init(AppContext_t *app);

/*
 * Runs one iteration of the application state machine.
 *
 * Parameters:
 *   app - Pointer to the application context.
 *
 * This function should be called in the main loop as often as
 * possible. It uses non-blocking timer-based logic to decide
 * what action to perform on each call.
 *
 * Each state handler performs the appropriate updates:
 * - Reading buttons/sensors
 * - Updating the game engine
 * - Refreshing the displays
 * - Transitioning to other states
 */
void App_Run(AppContext_t *app);

#endif /* APP_H */
