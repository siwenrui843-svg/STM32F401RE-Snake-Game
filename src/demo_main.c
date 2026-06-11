/*
 * Embedded Tilt-Controlled Snake Game
 * ====================================
 *
 * Main program for the STM32F401RE NUCLEO development board.
 *
 * Hardware:
 *   - STM32F401RE microcontroller (ARM Cortex-M4)
 *   - MPU6500 6-axis IMU (I2C)
 *   - 128x64 OLED display (I2C, SSD1315/SSD1306)
 *   - 16x2 LCD character display (4-bit parallel)
 *   - DS1820 / DS18S20 temperature sensor (OneWire)
 *   - 4 OLED buttons (K1–K4)
 *
 * This file initialises all hardware peripherals and runs the
 * application state machine in a non-blocking infinite loop.
 *
 * The TIM2 interrupt handler provides a 1 ms time base and
 * drives the DS18S20 non-blocking temperature conversion timer.
 */

/*
 * Include the STM32F401 device-specific header file.
 *
 * This provides register definitions, peripheral structures,
 * interrupt numbers, and core definitions.
 */
#include "stm32f401xe.h"

/*
 * Include the timer and delay header file.
 *
 * Functions: TIM2_Init_1ms(), delayMs(), DWT_Delay_Init(), delayUs()
 * Variable:  msTicks (volatile, incremented every 1 ms by TIM2 ISR)
 */
#include "../src/hfiles/timer_delay.h"

/*
 * Include the I2C1 bus header file.
 *
 * Functions: I2C1_Init()
 * I2C1 is shared by the OLED display and MPU6500 sensor.
 */
#include "../src/hfiles/i2c1_bus.h"

/*
 * Include the OLED display header file.
 *
 * Functions: OLED_Init(), OLED_Clear(), OLED_SetCursor(),
 *            OLED_PrintString(), OLED_PrintChar(), etc.
 */
#include "../src/hfiles/oled.h"

/*
 * Include the OneWire header file.
 *
 * Functions: OneWire_Init()
 * Used by the DS18S20 temperature sensor.
 */
#include "../src/hfiles/onewire.h"

/*
 * Include the buttons header file.
 *
 * Functions: Buttons_Init()
 * Configures PC9, PC8, PC6, PC5 as input pins for the OLED buttons.
 */
#include "../src/hfiles/buttons.h"

/*
 * Include the LCD header file.
 *
 * Functions: lcd_init()
 * Prepares the 16x2 character LCD display.
 */
#include "../src/hfiles/lcd.h"

/*
 * Include the MPU6500 sensor header file.
 *
 * Functions: MPU6500_Init(), MPU6500_ReadWhoAmI()
 * The MPU6500 provides accelerometer and gyroscope data via I2C.
 */
#include "../src/hfiles/mpu6500.h"

/*
 * Include the DS18S20 temperature sensor header file.
 *
 * Functions: DS18S20_StartConversion()
 * Variables: ds18_ready, ds18_temp_x10
 */
#include "../src/hfiles/ds18s20.h"

/*
 * Include the application state machine header file.
 *
 * This is the core of the Snake game application.
 * Functions: App_Init(), App_Run()
 * Type:      AppContext_t
 */
#include "../src/hfiles/app.h"


/*
 * ---------------------------------------------------------------------------
 * Main function — program entry point.
 * ---------------------------------------------------------------------------
 *
 * This is where execution begins after reset. The function initialises
 * all hardware, runs the startup sequence, and then enters an infinite
 * loop that calls the application state machine.
 */
int main(void)
{
    /*
     * Global application context.
     *
     * This structure holds the entire state of the application:
     * current state, Snake game, gesture detector, timing, and
     * sensor data.
     *
     * It is declared static to avoid placing a large structure on
     * the stack.
     */
    static AppContext_t app;

    /* =================================================================
     * HARDWARE INITIALISATION
     * =================================================================
     */

    /*
     * Initialise TIM2 to generate a 1 millisecond time base.
     *
     * This is required for:
     *   - delayMs() (blocking millisecond delay)
     *   - msTicks (free-running millisecond counter)
     *   - DS18S20_Tick_1ms() (non-blocking temperature conversion timer,
     *     called from the TIM2 interrupt handler)
     *
     * Function declared in: timer_delay.h
     */
    TIM2_Init_1ms();

    /*
     * Initialise the I2C1 peripheral.
     *
     * I2C1 is used for communication with:
     *   - OLED display  (address 0x3C)
     *   - MPU6500 sensor (address 0x68)
     *
     * This must be called before OLED_Init() and MPU6500_Init().
     *
     * Function declared in: i2c1_bus.h
     */
    I2C1_Init();

    /*
     * Initialise the OLED display.
     *
     * This sends the required startup command sequence to configure
     * the SSD1315/SSD1306 controller for 128x64 pixel operation.
     *
     * Function declared in: oled.h
     */
    OLED_Init();

    /*
     * Initialise the DWT (Data Watchpoint and Trace) cycle counter.
     *
     * This allows delayUs() to create accurate microsecond delays.
     * Microsecond delays are required for OneWire communication
     * with the DS18S20 temperature sensor.
     *
     * Function declared in: timer_delay.h
     */
    DWT_Delay_Init();

    /*
     * Enable the GPIOA peripheral clock.
     *
     * PA1 is used as the OneWire data pin for the DS18S20 temperature
     * sensor.  The GPIOA clock must be enabled before any GPIOA
     * registers are written, otherwise the pin configuration performed
     * by OneWire_Init() has no effect and all OneWire communication
     * silently fails.
     */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    /*
     * Initialise the OneWire bus.
     *
     * This configures PA1 as the OneWire data pin for the DS18S20
     * temperature sensor.
     *
     * Function declared in: onewire.h
     */
    OneWire_Init();

    /*
     * Initialise the OLED button input pins.
     *
     * Configures PC9 (K4), PC8 (K1), PC6 (K2), and PC5 (K3) as
     * input pins with internal pull-up resistors.
     *
     * Function declared in: buttons.h
     */
    Buttons_Init();

    /*
     * Initialise the 16x2 LCD display.
     *
     * This prepares the LCD in 4-bit mode. The LCD is used as a
     * secondary status display for score, level, temperature, and
     * game state.
     *
     * Function declared in: lcd.h
     */
    lcd_init();

    /*
     * Initialise the MPU6500 sensor.
     *
     * This wakes the MPU6500 from sleep mode by writing to the
     * power management register (PWR_MGMT_1 = 0x6B).
     *
     * Function declared in: mpu6500.h
     */
    MPU6500_Init();

    /*
     * Start the first DS18S20 temperature conversion.
     *
     * A short delay of 250 ms is inserted beforehand to allow the
     * OneWire bus to stabilise and the DS18S20 to complete its
     * internal power-on sequence.  Without this delay the first
     * conversion command may be missed by the sensor.
     *
     * The conversion is non-blocking. The result will be read when
     * ds18_ready becomes 1 (set by DS18S20_Tick_1ms() in the TIM2
     * interrupt handler after the conversion time has elapsed).
     *
     * Function declared in: ds18s20.h
     */
    delayMs(250);
    DS18S20_StartConversion();

    /* =================================================================
     * APPLICATION INITIALISATION
     * =================================================================
     */

    /*
     * Initialise the application state machine.
     *
     * This sets the application to the STARTUP state and prepares
     * the Snake game, gesture detector, and renderer.
     *
     * Function declared in: app.h
     */
    App_Init(&app);

    /* =================================================================
     * MAIN LOOP
     * =================================================================
     *
     * The main loop runs forever. Each iteration calls App_Run()
     * which handles all game logic, sensor reading, display updates,
     * and state transitions using non-blocking timer-based scheduling.
     *
     * No blocking delays are used in the main loop. All timing is
     * managed by comparing msTicks values.
     */
    while (1)
    {
        /*
         * Run one iteration of the application state machine.
         *
         * This function uses msTicks to decide which tasks should
         * execute on each call. It returns quickly so that the loop
         * can iterate at a high rate, ensuring responsive button
         * and sensor handling.
         */
        App_Run(&app);
    }

    /*
     * The program never reaches this point.
     */
    return 0;
}
