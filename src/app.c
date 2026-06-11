/*
 * Include the application header file.
 */
#include "../src/hfiles/app.h"

/*
 * Include hardware and driver headers.
 */
#include "../src/hfiles/oled.h"
#include "../src/hfiles/lcd.h"
#include "../src/hfiles/buttons.h"
#include "../src/hfiles/mpu6500.h"
#include "../src/hfiles/ds18s20.h"
#include "../src/hfiles/timer_delay.h"
#include "../src/hfiles/font5x7.h"
#include "../src/hfiles/utils.h"
#include "../src/hfiles/snake_render.h"
#include "../src/hfiles/kalman.h"

/* External reference to ds18_ready and ds18_temp_x10 from DS18S20 driver. */
extern volatile uint8_t ds18_ready;
extern int ds18_temp_x10;

/*
 * ---------------------------------------------------------------------------
 * Group and member information for the startup sequence.
 *
 * These values should be updated to match the actual project group.
 * ---------------------------------------------------------------------------
 */

/*
 * Group number displayed during startup.
 */
#define GROUP_NUMBER  1U

/*
 * Number of group members.
 */
#define MEMBER_COUNT  3U

/*
 * Group member details.
 *
 * Each member entry contains:
 *   - Name string
 *   - BUPT student ID string
 *   - QM student ID string
 *
 * Adjust the number of entries and strings to match the actual group.
 */
static const char *member_names[] = {
    "MEMBER 1",
    "MEMBER 2",
    "MEMBER 3"
};

static const char *member_bupt_ids[] = {
    "20240001",
    "20240002",
    "20240003"
};

static const char *member_qm_ids[] = {
    "10000001",
    "10000002",
    "10000003"
};

/*
 * ---------------------------------------------------------------------------
 * Timing constants for task scheduling (in milliseconds).
 * ---------------------------------------------------------------------------
 */

/*
 * MPU6500 read interval: 20 ms → 50 Hz.
 */
#define MPU_READ_INTERVAL_MS    20U

/*
 * Button read interval: 20 ms → 50 Hz.
 */
#define BUTTON_READ_INTERVAL_MS 20U

/*
 * OLED status/LCD update interval: 500 ms → 2 Hz.
 */
#define LCD_UPDATE_INTERVAL_MS  500U

/*
 * Temperature read interval: 1000 ms → 1 Hz.
 */
#define TEMP_READ_INTERVAL_MS   1000U

/*
 * Duration to show each startup screen (ms).
 */
#define STARTUP_SCREEN_MS  1000U

/*
 * Duration to show temperature on OLED during startup (ms).
 */
#define STARTUP_TEMP_MS    2000U


/*
 * ---------------------------------------------------------------------------
 * Internal (static) helper functions.
 * ---------------------------------------------------------------------------
 */

/*
 * Prints a formatted temperature value on the LCD at a given row.
 *
 * Parameters:
 *   temp_x10 - Temperature value multiplied by 10.
 *              Example: 253 means 25.3 degrees Celsius.
 *   row      - LCD row (0 for first line, 1 for second line).
 */
static void LCD_PrintTemp(int temp_x10, int row)
{
    char buffer[12];
    int whole;
    int frac;

    whole = temp_x10 / 10;
    frac  = temp_x10 % 10;

    if (frac < 0)
    {
        frac = -frac;
    }

    /*
     * Print the whole part.
     */
    intToStr(whole, buffer);
    lcd_set_cursor(0, row);
    lcd_print(buffer);

    /*
     * Print the decimal point.
     */
    lcd_print(".");

    /*
     * Print the fractional digit.
     */
    intToStr(frac, buffer);
    lcd_print(buffer);

    /*
     * Print the degree and C markers.
     */
    lcd_put_char('C');
}

/*
 * Reads all four buttons and detects falling-edge presses.
 *
 * Parameters:
 *   prev_k1 .. prev_k4 - Pointers to the previous button states.
 *                         These are updated by this function.
 *
 * Return value:
 *   Bitmask of newly pressed buttons:
 *     Bit 0 (0x01) = K1 (UP)
 *     Bit 1 (0x02) = K2 (Sharp/Down)
 *     Bit 2 (0x04) = K3 (Down/Select)
 *     Bit 3 (0x08) = K4 (AS/Back)
 */
static uint8_t ReadButtonPresses(uint8_t *prev_k1, uint8_t *prev_k2,
                                 uint8_t *prev_k3, uint8_t *prev_k4)
{
    uint8_t k1, k2, k3, k4;
    uint8_t result = 0;

    /*
     * Read current button states.
     * Buttons are active-low (pressed = LOW = 0).
     */
    k1 = Button_UP_Pressed();     /* K1 → PC8 */
    k2 = Button_Sharp_Pressed();  /* K2 → PC6 */
    k3 = Button_DOWN_Pressed();   /* K3 → PC5 */
    k4 = Button_AS_Pressed();     /* K4 → PC9 */

    /*
     * Detect falling edge: button was not pressed before,
     * but is pressed now.
     */
    if (k1 && !(*prev_k1)) { result |= 0x01; }
    if (k2 && !(*prev_k2)) { result |= 0x02; }
    if (k3 && !(*prev_k3)) { result |= 0x04; }
    if (k4 && !(*prev_k4)) { result |= 0x08; }

    /*
     * Store current states for the next call.
     */
    *prev_k1 = k1;
    *prev_k2 = k2;
    *prev_k3 = k3;
    *prev_k4 = k4;

    return result;
}

/*
 * Reads all four buttons and returns current held states.
 *
 * Return value:
 *   Bitmask of currently held buttons:
 *     Bit 0 (0x01) = K1 (UP)
 *     Bit 1 (0x02) = K2 (Sharp/Down)
 *     Bit 2 (0x04) = K3 (Down/Select)
 *     Bit 3 (0x08) = K4 (AS/Back)
 */
static uint8_t ReadButtonsHeld(void)
{
    uint8_t result = 0;

    if (Button_UP_Pressed())    { result |= 0x01; }
    if (Button_Sharp_Pressed()) { result |= 0x02; }
    if (Button_DOWN_Pressed())  { result |= 0x04; }
    if (Button_AS_Pressed())    { result |= 0x08; }

    return result;
}

/*
 * Displays the main menu on the OLED.
 *
 * Parameters:
 *   selection - Index of the currently highlighted menu option.
 *
 * This function performs a full clear-and-redraw of the menu.
 * It should be called ONCE when entering the menu state.
 *
 * For selection changes use DrawMenuSelection() which only
 * updates the arrow indicators without clearing the screen.
 */
static void DrawMainMenu(uint8_t selection)
{
    OLED_Clear();

    /*
     * Title on page 0.
     */
    OLED_SetCursor(0, 0);
    OLED_PrintString("=== SNAKE GAME ===");

    /*
     * Menu options on pages 2, 3, 4.
     */
    OLED_SetCursor(2, 0);
    OLED_PrintString("  Start (Buttons)");

    OLED_SetCursor(3, 0);
    OLED_PrintString("  Start (Tilt)");

    OLED_SetCursor(4, 0);
    OLED_PrintString("  Calibrate MPU");

    /*
     * Button hints on page 7.
     */
    OLED_SetCursor(7, 0);
    OLED_PrintString("K1/K2:Nav K3:Sel");

    /*
     * Draw the selection arrow for the initial selection.
     */
    OLED_SetCursor((uint8_t)(2 + selection), 0);
    OLED_PrintString(">");
}

/*
 * Updates the menu selection arrow on the OLED without clearing
 * the whole screen.
 *
 * Parameters:
 *   old_selection - Previous menu option index.
 *   new_selection - New menu option index.
 *
 * This function only overwrites two characters (the old '>' and
 * the new '>') so the display does not flicker.
 */
static void DrawMenuSelection(uint8_t old_selection, uint8_t new_selection)
{
    /*
     * Remove the old arrow by writing a space.
     */
    OLED_SetCursor((uint8_t)(2 + old_selection), 0);
    OLED_PrintString(" ");

    /*
     * Draw the new arrow.
     */
    OLED_SetCursor((uint8_t)(2 + new_selection), 0);
    OLED_PrintString(">");
}

/*
 * Displays the calibration screen on the OLED.
 */
static void DrawCalibrateScreen(void)
{
    OLED_Clear();

    OLED_SetCursor(1, 0);
    OLED_PrintString("CALIBRATION");

    OLED_SetCursor(3, 0);
    OLED_PrintString("Hold board LEVEL");

    OLED_SetCursor(4, 0);
    OLED_PrintString("Press K3 to save");

    OLED_SetCursor(5, 0);
    OLED_PrintString("Press K4 to cancel");
}

/*
 * Displays the pause screen on the OLED.
 */
static void DrawPauseScreen(void)
{
    OLED_Clear();

    OLED_SetCursor(2, 0);
    OLED_PrintString("=== PAUSED ===");

    OLED_SetCursor(4, 0);
    OLED_PrintString("K3: Resume");

    OLED_SetCursor(5, 0);
    OLED_PrintString("K4: Menu");
}

/*
 * Displays the game-over screen on the OLED.
 *
 * Parameters:
 *   score     - Final score of the completed game.
 *   high_score - All-time high score.
 */
static void DrawGameOverScreen(uint16_t score, uint16_t high_score)
{
    char buffer[12];

    OLED_Clear();

    OLED_SetCursor(1, 0);
    OLED_PrintString("=== GAME OVER ===");

    OLED_SetCursor(3, 0);
    OLED_PrintString("Score: ");
    intToStr((int)score, buffer);
    OLED_PrintString(buffer);

    OLED_SetCursor(4, 0);
    OLED_PrintString("Best:  ");
    intToStr((int)high_score, buffer);
    OLED_PrintString(buffer);

    OLED_SetCursor(6, 0);
    OLED_PrintString("K3: Restart");

    OLED_SetCursor(7, 0);
    OLED_PrintString("K4: Menu");
}

/*
 * Updates the LCD display based on the current application state.
 *
 * Parameters:
 *   app - Pointer to the application context.
 */
static void UpdateLCD(const AppContext_t *app)
{
    char buffer[12];

    switch (app->state)
    {
    case APP_STATE_MENU:
        /*
         * Line 1: "H:XXX S:XXX" (high score, last score)
         * Line 2: "Temp XX.XC"
         */
        lcd_set_cursor(0, 0);
        lcd_print("H:");
        intToStr((int)app->game.high_score, buffer);
        lcd_print(buffer);

        lcd_print(" S:");
        intToStr((int)app->last_game_score, buffer);
        lcd_print(buffer);

        /*
         * Pad remaining characters on the line.
         */
        lcd_print("    ");

        LCD_PrintTemp(app->temperature_x10, 1);
        break;

    case APP_STATE_CALIBRATE:
        lcd_set_cursor(0, 0);
        lcd_print("CALIBRATING...  ");
        LCD_PrintTemp(app->temperature_x10, 1);
        break;

    case APP_STATE_PLAYING:
        /*
         * Line 1: "H:XXX S:XXX L:X"
         * Line 2: "Temp XX.XC"
         */
        lcd_set_cursor(0, 0);
        lcd_print("H:");
        intToStr((int)app->game.high_score, buffer);
        lcd_print(buffer);

        lcd_print(" S:");
        intToStr((int)app->game.score, buffer);
        lcd_print(buffer);

        lcd_print(" L:");
        intToStr((int)app->game.level, buffer);
        lcd_print(buffer);

        lcd_print("  ");

        LCD_PrintTemp(app->temperature_x10, 1);
        break;

    case APP_STATE_PAUSED:
        lcd_set_cursor(0, 0);
        lcd_print("PAUSED          ");
        LCD_PrintTemp(app->temperature_x10, 1);
        break;

    case APP_STATE_GAME_OVER:
        lcd_set_cursor(0, 0);
        lcd_print("GAMEOVER S:");
        intToStr((int)app->game.score, buffer);
        lcd_print(buffer);
        lcd_print("  ");
        LCD_PrintTemp(app->temperature_x10, 1);
        break;

    case APP_STATE_STARTUP:
    default:
        LCD_PrintTemp(app->temperature_x10, 0);
        lcd_set_cursor(0, 1);
        lcd_print("Starting...     ");
        break;
    }
}

/*
 * Reads the MPU6500 sensor data and updates the application context.
 *
 * Parameters:
 *   app - Pointer to the application context.
 */
static void ReadMPU(AppContext_t *app)
{
    MPU6500_ReadAccelRaw(&app->accel_x, &app->accel_y, &app->accel_z);
    MPU6500_ReadGyroRaw(&app->gyro_x, &app->gyro_y, &app->gyro_z);
}

/*
 * Checks and reads the DS1820 temperature if a new reading is ready.
 *
 * Called on every main-loop iteration so that ds18_ready is caught
 * as soon as the 1 ms timer ISR sets it.  Once read, a new
 * conversion is started immediately to keep the pipeline running.
 */
static void ReadTemperature(AppContext_t *app)
{
    if (ds18_ready)
    {
        ds18_temp_x10 = DS18S20_ReadTemp_x10_Now();
        DS18S20_StartConversion();
        app->temperature_x10 = ds18_temp_x10;
    }
}

/*
 * Processes button input during the PLAYING state (button-control mode).
 *
 * Button mapping:
 *   K1 = UP,  K2 = LEFT,  K3 = RIGHT,  K4 = DOWN
 *
 * K4 also doubles as the pause trigger: a long press (> 500 ms)
 * pauses the game.  Short taps change direction to DOWN.
 */
static void ProcessPlayingButtons(AppContext_t *app, uint8_t btns)
{
    /*
     * K1 (bit 0) → UP
     * K2 (bit 1) → LEFT
     * K3 (bit 2) → RIGHT
     * K4 (bit 3) → DOWN (direction only; pause handled in caller)
     */
    if (btns & 0x01)
    {
        SnakeGame_SetDirection(&app->game, DIR_UP);
    }
    else if (btns & 0x02)
    {
        SnakeGame_SetDirection(&app->game, DIR_LEFT);
    }
    else if (btns & 0x04)
    {
        SnakeGame_SetDirection(&app->game, DIR_RIGHT);
    }
    else if (btns & 0x08)
    {
        SnakeGame_SetDirection(&app->game, DIR_DOWN);
    }
}

/*
 * Processes tilt input during the PLAYING state (tilt-control mode).
 *
 * Parameters:
 *   app - Pointer to the application context.
 */
static void ProcessPlayingTilt(AppContext_t *app)
{
    SnakeDirection tilt_dir;

    /*
     * Update the gesture detector with the latest accelerometer data.
     */
    tilt_dir = Gesture_Update(&app->gesture,
                              app->accel_x, app->accel_y, app->accel_z);

    /*
     * If the tilt direction differs from the current game direction,
     * apply the change.
     */
    if (tilt_dir != app->game.direction)
    {
        SnakeGame_SetDirection(&app->game, tilt_dir);
    }
}


/*
 * ---------------------------------------------------------------------------
 * Public functions.
 * ---------------------------------------------------------------------------
 */

/*
 * Initialises the whole application context.
 *
 * See the header file for full documentation.
 */
void App_Init(AppContext_t *app)
{
    if (app == ((void *)0))
    {
        return;
    }

    /*
     * Set the application to the startup state.
     */
    app->state         = APP_STATE_STARTUP;
    app->control_mode  = CONTROL_MODE_BUTTON;
    app->menu_selection = MENU_OPTION_START_BUTTON;
    app->temperature_x10 = 0;
    app->last_game_score = 0;
    app->startup_stage   = 0;

    /*
     * Initialise timing variables.
     */
    app->state_entry_time = msTicks;
    app->last_mpu_time    = msTicks;
    app->last_button_time = msTicks;
    app->last_snake_time  = msTicks;
    app->last_temp_time   = msTicks;
    app->last_lcd_time    = msTicks;
    app->last_oled_time   = msTicks;

    /*
     * Initialise sensor data.
     */
    app->accel_x = 0;
    app->accel_y = 0;
    app->accel_z = 0;
    app->gyro_x  = 0;
    app->gyro_y  = 0;
    app->gyro_z  = 0;
    app->mpu_whoami = 0;
    app->temp_ready = 0;

    /*
     * Initialise the Snake game engine.
     */
    SnakeGame_Init(&app->game);

    /*
     * Initialise the gesture detector.
     */
    Gesture_Init(&app->gesture);

    /*
     * Initialise the Snake renderer.
     */
    SnakeRender_Init();

    /*
     * Read the MPU6500 WHO_AM_I register to check sensor presence.
     */
    app->mpu_whoami = MPU6500_ReadWhoAmI();
}

/*
 * Runs one iteration of the application state machine.
 *
 * See the header file for full documentation.
 */
void App_Run(AppContext_t *app)
{
    uint32_t now;
    uint8_t btn_presses;
    uint8_t btn_held;
    uint32_t elapsed;

    /*
     * Static variables for button edge detection.
     * These persist across function calls.
     */
    static uint8_t prev_k1 = 0;
    static uint8_t prev_k2 = 0;
    static uint8_t prev_k3 = 0;
    static uint8_t prev_k4 = 0;

    /*
     * Tracks the last startup stage for which the OLED content
     * has been drawn.  Initialised to 0xFF (an impossible stage
     * value) so that stage 0 is always drawn on entry.
     *
     * This prevents OLED_Clear() + redraw from running on every
     * loop iteration, which would cause visible flicker.
     */
    static uint8_t startup_stage_drawn = 0xFF;

    if (app == ((void *)0))
    {
        return;
    }

    now     = msTicks;
    elapsed = now - app->state_entry_time;

    /*
     * -----------------------------------------------------------------
     * Periodic tasks that run regardless of state:
     * - Temperature reading (checked every iteration so that
     *   ds18_ready is caught as soon as the timer ISR sets it).
     * -----------------------------------------------------------------
     */
    ReadTemperature(app);

    /*
     * -----------------------------------------------------------------
     * State machine dispatch.
     * -----------------------------------------------------------------
     */
    switch (app->state)
    {

    /* -------------------------------------------------------------- */
    case APP_STATE_STARTUP:
    {
        /*
         * Startup sequence:
         *   Stage 0: Display group number for 1 second.
         *   Stage 1..N: Display each member for 1 second.
         *   Stage N+1: Display temperature for 2 seconds.
         *   Stage N+2: Transition to MENU.
         *
         * The OLED content is only redrawn when the stage changes
         * (tracked by startup_stage_drawn).  This prevents
         * OLED_Clear() from being called on every loop iteration,
         * which would cause visible flicker.
         */

        /*
         * Stage-specific drawing: only executed once per stage.
         */
        if (app->startup_stage != startup_stage_drawn)
        {
            startup_stage_drawn = app->startup_stage;

            if (app->startup_stage == 0)
            {
                /*
                 * Show group number.
                 */
                OLED_Clear();
                OLED_SetCursor(3, 0);
                OLED_PrintString("Group ");
                {
                    char buf[4];
                    intToStr((int)GROUP_NUMBER, buf);
                    OLED_PrintString(buf);
                }
            }
            else
            {
                uint8_t member_idx = app->startup_stage - 1;

                if (member_idx < MEMBER_COUNT)
                {
                    /*
                     * Show one member's details.
                     */
                    OLED_Clear();

                    /* Name on page 1 */
                    OLED_SetCursor(1, 0);
                    OLED_PrintString(member_names[member_idx]);

                    /* BUPT ID on page 3 */
                    OLED_SetCursor(3, 0);
                    OLED_PrintString(member_bupt_ids[member_idx]);

                    /* QM ID on page 5 */
                    OLED_SetCursor(5, 0);
                    OLED_PrintString(member_qm_ids[member_idx]);
                }
                else if (member_idx == MEMBER_COUNT)
                {
                    /*
                     * After all members, show temperature on the OLED.
                     */
                    OLED_Clear();
                    OLED_SetCursor(0, 0);
                    OLED_PrintString("Temperature:");

                    {
                        char buf[12];
                        int whole = app->temperature_x10 / 10;
                        int frac  = app->temperature_x10 % 10;
                        if (frac < 0) { frac = -frac; }

                        OLED_SetCursor(3, 30);
                        intToStr(whole, buf);
                        OLED_PrintString(buf);
                        OLED_PrintString(".");
                        intToStr(frac, buf);
                        OLED_PrintString(buf);
                        OLED_PrintChar(font_degree);
                        OLED_PrintString("C");
                    }

                    /*
                     * Also show temperature on LCD.
                     */
                    UpdateLCD(app);
                }
            }
        }

        /*
         * Timing-based stage transitions (checked every iteration).
         */
        if (app->startup_stage == 0)
        {
            if (elapsed >= STARTUP_SCREEN_MS)
            {
                app->startup_stage = 1;
                app->state_entry_time = now;
            }
        }
        else
        {
            uint8_t member_idx = app->startup_stage - 1;

            if (member_idx < MEMBER_COUNT)
            {
                if (elapsed >= STARTUP_SCREEN_MS)
                {
                    app->startup_stage++;
                    app->state_entry_time = now;
                }
            }
            else if (member_idx == MEMBER_COUNT)
            {
                if (elapsed >= STARTUP_TEMP_MS)
                {
                    app->startup_stage++;
                    app->state_entry_time = now;
                }
            }
            else
            {
                /*
                 * Startup complete — transition to MENU.
                 */
                OLED_Clear();

                app->state = APP_STATE_MENU;
                app->state_entry_time = now;
                app->menu_selection = MENU_OPTION_START_BUTTON;

                DrawMainMenu(app->menu_selection);
                UpdateLCD(app);
            }
        }
        break;
    }

    /* -------------------------------------------------------------- */
    case APP_STATE_MENU:
    {
        /*
         * Read buttons at 50 Hz for menu navigation.
         */
        if ((now - app->last_button_time) >= BUTTON_READ_INTERVAL_MS)
        {
            btn_presses = ReadButtonPresses(&prev_k1, &prev_k2,
                                            &prev_k3, &prev_k4);
            app->last_button_time = now;

            /*
             * K1 (bit 0) = move selection up.
             */
            if (btn_presses & 0x01)
            {
                uint8_t old_sel = app->menu_selection;

                if (app->menu_selection > 0)
                {
                    app->menu_selection--;
                }
                else
                {
                    app->menu_selection = MENU_OPTION_COUNT - 1;
                }
                DrawMenuSelection(old_sel, app->menu_selection);
            }

            /*
             * K2 (bit 1) = move selection down.
             */
            if (btn_presses & 0x02)
            {
                uint8_t old_sel = app->menu_selection;

                if (app->menu_selection < (MENU_OPTION_COUNT - 1))
                {
                    app->menu_selection++;
                }
                else
                {
                    app->menu_selection = 0;
                }
                DrawMenuSelection(old_sel, app->menu_selection);
            }

            /*
             * K3 (bit 2) = select the current option.
             */
            if (btn_presses & 0x04)
            {
                switch (app->menu_selection)
                {
                case MENU_OPTION_START_BUTTON:
                    /*
                     * Start a new game with button control.
                     */
                    app->control_mode = CONTROL_MODE_BUTTON;
                    SnakeGame_Init(&app->game);
                    SnakeRender_Reset();
                    OLED_Clear();
                    SnakeRender_Draw(&app->game);

                    app->state = APP_STATE_PLAYING;
                    app->state_entry_time = now;
                    app->last_snake_time = now;
                    break;

                case MENU_OPTION_START_TILT:
                    /*
                     * Start a new game with tilt control.
                     * If the MPU is not calibrated, go to calibration first.
                     */
                    if (!app->gesture.calibrated)
                    {
                        app->state = APP_STATE_CALIBRATE;
                        app->state_entry_time = now;
                        DrawCalibrateScreen();
                    }
                    else
                    {
                        app->control_mode = CONTROL_MODE_TILT;
                        SnakeGame_Init(&app->game);
                        SnakeRender_Reset();
                        OLED_Clear();
                        SnakeRender_DrawBorder();
                        SnakeRender_Draw(&app->game);

                        app->state = APP_STATE_PLAYING;
                        app->state_entry_time = now;
                        app->last_snake_time = now;
                    }
                    break;

                case MENU_OPTION_CALIBRATE:
                    app->state = APP_STATE_CALIBRATE;
                    app->state_entry_time = now;
                    DrawCalibrateScreen();
                    break;
                }
                UpdateLCD(app);
            }
        }

        /*
         * Update LCD periodically.
         */
        if ((now - app->last_lcd_time) >= LCD_UPDATE_INTERVAL_MS)
        {
            UpdateLCD(app);
            app->last_lcd_time = now;
        }
        break;
    }

    /* -------------------------------------------------------------- */
    case APP_STATE_CALIBRATE:
    {
        /*
         * Read the MPU6500 at 50 Hz to get stable readings for calibration.
         */
        if ((now - app->last_mpu_time) >= MPU_READ_INTERVAL_MS)
        {
            ReadMPU(app);
            app->last_mpu_time = now;

            /*
             * Display current roll and pitch on OLED for user feedback.
             */
            {
                float roll  = Gesture_ComputeRoll(app->accel_y, app->accel_z);
                float pitch = Gesture_ComputePitch(app->accel_x,
                                                    app->accel_y,
                                                    app->accel_z);
                char buf[12];

                OLED_SetCursor(1, 90);
                OLED_PrintString("R:");
                intToStr((int)roll, buf);
                OLED_PrintString(buf);
                OLED_PrintString("  ");

                OLED_SetCursor(2, 90);
                OLED_PrintString("P:");
                intToStr((int)pitch, buf);
                OLED_PrintString(buf);
                OLED_PrintString("  ");
            }
        }

        /*
         * Read buttons for calibration actions.
         */
        if ((now - app->last_button_time) >= BUTTON_READ_INTERVAL_MS)
        {
            btn_presses = ReadButtonPresses(&prev_k1, &prev_k2,
                                            &prev_k3, &prev_k4);
            app->last_button_time = now;

            /*
             * K3 = confirm calibration.
             */
            if (btn_presses & 0x04)
            {
                Gesture_Calibrate(&app->gesture,
                                  app->accel_x, app->accel_y, app->accel_z);

                /*
                 * Show confirmation briefly, then return to menu.
                 */
                OLED_Clear();
                OLED_SetCursor(3, 0);
                OLED_PrintString("Calibrated!");
                OLED_SetCursor(4, 0);
                OLED_PrintString("Roll/Pitch saved");
                delayMs(1000);

                app->state = APP_STATE_MENU;
                app->state_entry_time = now;
                DrawMainMenu(app->menu_selection);
                UpdateLCD(app);
            }

            /*
             * K4 = cancel calibration.
             */
            if (btn_presses & 0x08)
            {
                app->state = APP_STATE_MENU;
                app->state_entry_time = now;
                DrawMainMenu(app->menu_selection);
                UpdateLCD(app);
            }
        }

        /*
         * Update LCD periodically.
         */
        if ((now - app->last_lcd_time) >= LCD_UPDATE_INTERVAL_MS)
        {
            UpdateLCD(app);
            app->last_lcd_time = now;
        }
        break;
    }

    /* -------------------------------------------------------------- */
    case APP_STATE_PLAYING:
    {
        /*
         * Task 1: Read MPU6500 at 50 Hz.
         */
        if ((now - app->last_mpu_time) >= MPU_READ_INTERVAL_MS)
        {
            ReadMPU(app);
            app->last_mpu_time = now;
        }

        /*
         * Task 2: Read buttons at 50 Hz.
         */
        if ((now - app->last_button_time) >= BUTTON_READ_INTERVAL_MS)
        {
            static uint32_t k4_press_start = 0;
            static uint8_t  k4_was_held    = 0;

            btn_presses = ReadButtonPresses(&prev_k1, &prev_k2,
                                            &prev_k3, &prev_k4);
            btn_held     = ReadButtonsHeld();
            app->last_button_time = now;

            /*
             * K4 long-press detection:  if K4 is held for more than
             * 500 ms the game pauses.  Short taps on K4 are treated
             * as the DOWN direction inside ProcessPlayingButtons().
             */
            if (btn_held & 0x08)
            {
                if (!k4_was_held)
                {
                    /* K4 just pressed — record the press time. */
                    k4_press_start = now;
                    k4_was_held = 1;
                }
                else if ((now - k4_press_start) >= 500U)
                {
                    /* Long press detected — pause the game. */
                    k4_was_held = 0;  /* reset so it only fires once */
                    app->state = APP_STATE_PAUSED;
                    app->state_entry_time = now;
                    DrawPauseScreen();
                    UpdateLCD(app);
                    break;  /* exit the PLAYING handler */
                }
            }
            else
            {
                k4_was_held = 0;
            }

            /*
             * In button-control mode, translate buttons to direction.
             */
            if (app->control_mode == CONTROL_MODE_BUTTON)
            {
                ProcessPlayingButtons(app, btn_held);
            }
        }

        /*
         * Task 3: In tilt-control mode, process tilt input at 50 Hz.
         */
        if (app->control_mode == CONTROL_MODE_TILT)
        {
            if ((now - app->last_mpu_time) < MPU_READ_INTERVAL_MS)
            {
                /* MPU was just read; process tilt. */
                ProcessPlayingTilt(app);
            }
        }

        /*
         * Task 4: Update the Snake game engine at the correct rate.
         *
         * Non-blocking timing check: the snake only moves when the
         * elapsed time since the last move exceeds the current speed
         * interval. This ensures that level changes are applied
         * immediately — if the level increases, the new (shorter)
         * speed interval takes effect on the next iteration.
         *
         * Button / MPU reads happen before the snake update so
         * controls stay responsive regardless of snake speed.
         */
        uint32_t snake_speed_ms = SnakeGame_GetSpeedMs(&app->game);
        if ((now - app->last_snake_time) >= snake_speed_ms)
        {
            SnakeGame_Update(&app->game);
            SnakeRender_Draw(&app->game);
            app->last_snake_time = now;

            /*
             * If the game ended during this update, transition to
             * GAME_OVER state.
             */
            if (app->game.game_over)
            {
                app->last_game_score = app->game.score;
                app->state = APP_STATE_GAME_OVER;
                app->state_entry_time = now;
                DrawGameOverScreen(app->game.score,
                                   app->game.high_score);
                UpdateLCD(app);
                break;  /* exit the PLAYING handler */
            }
        }

        /*
         * Update LCD every iteration so level changes are immediately
         * visible, and controls appear responsive.
         */
        if ((now - app->last_lcd_time) >= LCD_UPDATE_INTERVAL_MS)
        {
            UpdateLCD(app);
            app->last_lcd_time = now;
        }
        break;
    }

    /* -------------------------------------------------------------- */
    case APP_STATE_PAUSED:
    {
        /*
         * Read buttons for pause menu actions.
         */
        if ((now - app->last_button_time) >= BUTTON_READ_INTERVAL_MS)
        {
            btn_presses = ReadButtonPresses(&prev_k1, &prev_k2,
                                            &prev_k3, &prev_k4);
            app->last_button_time = now;

            /*
             * K3 = resume game.
             */
            if (btn_presses & 0x04)
            {
                /*
                 * Resume: restore the game display.
                 */
                SnakeRender_Reset();
                OLED_Clear();
                SnakeRender_DrawBorder();
                SnakeRender_Draw(&app->game);

                app->state = APP_STATE_PLAYING;
                app->state_entry_time = now;
                app->last_snake_time = now;
                UpdateLCD(app);
            }

            /*
             * K4 = return to main menu.
             */
            if (btn_presses & 0x08)
            {
                app->state = APP_STATE_MENU;
                app->state_entry_time = now;
                app->last_game_score = app->game.score;
                DrawMainMenu(app->menu_selection);
                UpdateLCD(app);
            }
        }

        /*
         * Update LCD periodically.
         */
        if ((now - app->last_lcd_time) >= LCD_UPDATE_INTERVAL_MS)
        {
            UpdateLCD(app);
            app->last_lcd_time = now;
        }
        break;
    }

    /* -------------------------------------------------------------- */
    case APP_STATE_GAME_OVER:
    {
        /*
         * Read buttons for game-over actions.
         */
        if ((now - app->last_button_time) >= BUTTON_READ_INTERVAL_MS)
        {
            btn_presses = ReadButtonPresses(&prev_k1, &prev_k2,
                                            &prev_k3, &prev_k4);
            app->last_button_time = now;

            /*
             * K3 = restart with same control mode.
             */
            if (btn_presses & 0x04)
            {
                SnakeGame_Init(&app->game);
                SnakeRender_Reset();
                OLED_Clear();
                SnakeRender_DrawBorder();
                SnakeRender_Draw(&app->game);

                app->state = APP_STATE_PLAYING;
                app->state_entry_time = now;
                app->last_snake_time = now;
                UpdateLCD(app);
            }

            /*
             * K4 = return to main menu.
             */
            if (btn_presses & 0x08)
            {
                app->state = APP_STATE_MENU;
                app->state_entry_time = now;
                DrawMainMenu(app->menu_selection);
                UpdateLCD(app);
            }
        }

        /*
         * Update LCD periodically.
         */
        if ((now - app->last_lcd_time) >= LCD_UPDATE_INTERVAL_MS)
        {
            UpdateLCD(app);
            app->last_lcd_time = now;
        }
        break;
    }

    } /* end outer switch */
}
