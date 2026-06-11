/*
 * my_ds18s20.c
 * ============
 *
 * Standalone OneWire + DS18S20 temperature sensor driver.
 *
 * Replaces the library-provided OneWire / DS18S20 functions with a
 * self-contained implementation that directly accesses GPIOA registers
 * and the DWT cycle counter.  No library dependencies.
 *
 * OneWire data pin : PA1 (requires external 4.7 kΩ pull-up to 3.3 V)
 * Timing source    : ARM DWT cycle counter (Cortex-M4)
 */

#include "stm32f401xe.h"
#include "../src/hfiles/ds18s20.h"

/* ------------------------------------------------------------------ */
/*  External reference — SystemCoreClock                              */
/* ------------------------------------------------------------------ */

extern uint32_t SystemCoreClock;

/* ------------------------------------------------------------------ */
/*  OneWire pin definitions — PA1                                     */
/* ------------------------------------------------------------------ */

#define OW_PIN          1U
#define OW_PIN_MASK     (1U << OW_PIN)

/* Write-Only Operations — use BSRR for atomic pin control */
#define OW_WRITE_LOW()  (GPIOA->BSRR = (OW_PIN_MASK << 16U)) /* reset  → LOW  */
#define OW_WRITE_HIGH() (GPIOA->BSRR =  OW_PIN_MASK)         /* set    → HIGH */

/* Read */
#define OW_READ()       (((GPIOA->IDR) >> OW_PIN) & 1U)

/* ------------------------------------------------------------------ */
/*  Microsecond delay — DWT cycle counter                             */
/* ------------------------------------------------------------------ */

/*
 * Blocking microsecond delay.
 *
 * Uses the ARM DWT (Data Watchpoint and Trace) cycle counter which runs
 * at the CPU core clock (HCLK).  The counter is enabled on first use if
 * it hasn't already been enabled by DWT_Delay_Init().
 */
static void ow_delay_us(uint32_t us)
{
    uint32_t start, cycles;

    /* Enable the DWT cycle counter if not already running. */
    if (!(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk))
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0U;
        DWT->CTRL   |= DWT_CTRL_CYCCNTENA_Msk;
    }

    cycles = us * (SystemCoreClock / 1000000U);
    start  = DWT->CYCCNT;

    while ((DWT->CYCCNT - start) < cycles)
    {
        /* spin */
    }
}

/* ------------------------------------------------------------------ */
/*  OneWire low-level pin control                                     */
/* ------------------------------------------------------------------ */

/*
 * Drive PA1 LOW (push-pull output, ODR = 0).
 */
static void ow_pin_output_low(void)
{
    /* Push-pull output, ODR = 0 */
    GPIOA->OTYPER &= ~OW_PIN_MASK;
    GPIOA->MODER  &= ~(3U << (OW_PIN * 2U));
    GPIOA->MODER  |=  (1U << (OW_PIN * 2U));     /* 01 = general output */
    OW_WRITE_LOW();
}

/*
 * Release the bus — configure PA1 as high-impedance input.
 *
 * The external pull-up resistor pulls the line HIGH.
 */
static void ow_pin_input(void)
{
    /* Switch to input (high‑Z) — external pull‑up pulls the line HIGH. */
    GPIOA->MODER &= ~(3U << (OW_PIN * 2U));      /* 00 = input           */
    GPIOA->PUPDR &= ~(3U << (OW_PIN * 2U));      /* no pull‑up/down      */
}

/* ------------------------------------------------------------------ */
/*  OneWire protocol primitives                                       */
/* ------------------------------------------------------------------ */

/*
 * Send a Reset pulse on the OneWire bus and check for a presence pulse.
 *
 * Return: 1 = device present, 0 = no device detected.
 */
static uint8_t ow_reset(void)
{
    uint8_t presence;

    /* Drive LOW for ≥ 480 µs */
    ow_pin_output_low();
    ow_delay_us(490U);

    /* Release bus — pull‑up pulls HIGH */
    ow_pin_input();
    ow_delay_us(55U);

    /* Sample: if a device is present it pulls the line LOW */
    presence = (OW_READ() == 0U) ? 1U : 0U;

    /* Wait for the remainder of the presence-detection window */
    ow_delay_us(415U);

    return presence;
}

/*
 * Write one bit to the OneWire bus (standard speed).
 */
static void ow_write_bit(uint8_t bit)
{
    if (bit)
    {
        /* Write "1" slot: brief low pulse */
        ow_pin_output_low();
        ow_delay_us(3U);
        ow_pin_input();
        ow_delay_us(62U);
    }
    else
    {
        /* Write "0" slot: extended low pulse */
        ow_pin_output_low();
        ow_delay_us(62U);
        ow_pin_input();
        ow_delay_us(3U);
    }
}

/*
 * Read one bit from the OneWire bus (standard speed).
 */
static uint8_t ow_read_bit(void)
{
    uint8_t bit;

    /* Start the read slot with a brief low pulse */
    ow_pin_output_low();
    ow_delay_us(2U);
    ow_pin_input();
    ow_delay_us(8U);

    /* Sample the line */
    bit = OW_READ();

    /* Wait for the remainder of the read slot */
    ow_delay_us(55U);

    return bit;
}

/*
 * Write one byte (LSB first) to the OneWire bus.
 */
static void ow_write_byte(uint8_t data)
{
    uint8_t i;
    for (i = 0U; i < 8U; i++)
    {
        ow_write_bit(data & 0x01U);
        data >>= 1U;
    }
}

/*
 * Read one byte (LSB first) from the OneWire bus.
 */
static uint8_t ow_read_byte(void)
{
    uint8_t i, data = 0U;
    for (i = 0U; i < 8U; i++)
    {
        data >>= 1U;
        if (ow_read_bit())
        {
            data |= 0x80U;
        }
    }
    return data;
}

/* ------------------------------------------------------------------ */
/*  DS18S20 state — replaces library globals                          */
/* ------------------------------------------------------------------ */

/* Milliseconds remaining until the in‑flight conversion completes. */
static volatile uint16_t ds18_timer = 0U;

/* 1 = conversion in progress; 0 = idle. */
static volatile uint8_t  ds18_busy  = 0U;

/* 1 = conversion result ready to read; 0 = waiting. */
volatile uint8_t ds18_ready = 0U;

/* Last-read temperature in tenths of a degree Celsius. */
int ds18_temp_x10 = 0;

/*
 * Maximum conversion time in milliseconds.
 *
 * DS18S20 (9‑bit) needs up to  93.75 ms.
 * DS18B20 (12‑bit) needs up to 750    ms.
 *
 * We use the longer value for safety.
 */
#define DS18_CONV_TIME_MS  750U

/* ------------------------------------------------------------------ */
/*  DS18S20 public functions  (override library symbols)              */
/* ------------------------------------------------------------------ */

/*
 * Called every 1 ms from the TIM2 interrupt handler.
 *
 * Counts down the conversion timer and sets ds18_ready when the
 * conversion is complete.
 */
void DS18S20_Tick_1ms(void)
{
    if (ds18_busy)
    {
        if (ds18_timer > 0U)
        {
            ds18_timer--;
        }
        if (ds18_timer == 0U)
        {
            ds18_ready = 1U;
            ds18_busy  = 0U;
        }
    }
}

/*
 * Initialise the OneWire bus (PA1).
 *
 * Enables the GPIOA peripheral clock, then configures PA1 as a
 * high‑impedance input.  The external pull‑up resistor holds the bus
 * HIGH during idle.
 */
void OneWire_Init(void)
{
    /* Make sure the GPIOA clock is enabled. */
    RCC->AHB1ENR |= 0x00000001U;
    __DSB();

    /* No pull‑up / pull‑down — rely on external 4.7 kΩ pull‑up */
    GPIOA->PUPDR &= ~(3U << (OW_PIN * 2U));      /* 00 = no PU/PD       */
    GPIOA->MODER &= ~(3U << (OW_PIN * 2U));      /* 00 = input (high‑Z) */
}

/*
 * Start a non‑blocking DS18S20 temperature conversion.
 *
 * Sends the Skip ROM (0xCC) and Convert T (0x44) commands.  The
 * conversion runs in the background; DS18S20_Tick_1ms() tracks the
 * conversion time and sets ds18_ready when it completes.
 */
void DS18S20_StartConversion(void)
{
    /* Don't start a new conversion if one is already in progress. */
    if (ds18_busy)
    {
        return;
    }

    /* Check for sensor presence. */
    if (!ow_reset())
    {
        /* No sensor responded — don't arm the timer. */
        return;
    }

    /* Skip ROM — only one device on the bus. */
    ow_write_byte(0xCCU);

    /* Convert T — start temperature measurement. */
    ow_write_byte(0x44U);

    /* Arm the software timer. */
    ds18_timer = DS18_CONV_TIME_MS;
    ds18_busy  = 1U;
    ds18_ready = 0U;
}

/*
 * Read the last temperature result from the DS18S20.
 *
 * This function must only be called AFTER ds18_ready has been set to 1
 * (i.e. the conversion started by DS18S20_StartConversion() has
 * completed).
 *
 * Return: Temperature in tenths of a degree Celsius.
 *         Example: 253 means 25.3 °C.
 */
int DS18S20_ReadTemp_x10_Now(void)
{
    uint8_t lsb, msb;
    int16_t raw;

    if (!ow_reset())
    {
        /* Sensor not responding — return the last good reading. */
        return ds18_temp_x10;
    }

    /* Skip ROM */
    ow_write_byte(0xCCU);

    /* Read Scratchpad */
    ow_write_byte(0xBEU);

    lsb = ow_read_byte();
    msb = ow_read_byte();

    /*
     * DS18B20 temperature format (family code 0x28):
     *   12-bit signed value, LSB = 0.0625 degC.
     *
     * Conversion to tenths of a degree:
     *   temp_x10 = raw * 0.625  =  raw * 10 / 16
     *
     *   Example:  25.0 degC -> raw = 400 -> 250
     *             -0.5 degC -> raw =  -8 ->  -5
     *
     * Note: integer arithmetic — multiply before divide to avoid
     * truncation.  raw is int16_t so the product fits in int32_t.
     */
    raw = (int16_t)(((uint16_t)msb << 8U) | lsb);
    ds18_temp_x10 = ((int)raw * 10) / 16;

    return ds18_temp_x10;
}

/* ------------------------------------------------------------------ */
/*  Remaining DS18S20 API — required to prevent linker pulling in     */
/*  ds18s20.o from the library (which would create duplicate globals) */
/* ------------------------------------------------------------------ */

/*
 * Blocking raw scratchpad read.
 *
 * Starts a conversion, waits 750 ms, then reads the two temperature
 * bytes from the scratchpad.
 */
void DS18S20_ReadRaw(uint8_t *lsb, uint8_t *msb)
{
    uint32_t i;

    if (lsb == ((void *)0) || msb == ((void *)0))
    {
        return;
    }

    /* Reset and send Convert T command */
    if (!ow_reset())
    {
        *lsb = 0U;
        *msb = 0U;
        return;
    }
    ow_write_byte(0xCCU); /* Skip ROM */
    ow_write_byte(0x44U); /* Convert T */

    /* Wait for conversion (750 ms, polled) */
    for (i = 0U; i < 750000U; i++)
    {
        ow_delay_us(1U);
    }

    /* Reset and send Read Scratchpad command */
    if (!ow_reset())
    {
        *lsb = 0U;
        *msb = 0U;
        return;
    }
    ow_write_byte(0xCCU); /* Skip ROM */
    ow_write_byte(0xBEU); /* Read Scratchpad */

    *lsb = ow_read_byte();
    *msb = ow_read_byte();
}

/*
 * Blocking temperature read in tenths of a degree Celsius.
 */
int DS18S20_ReadTemp_x10(void)
{
    uint8_t lsb, msb;
    int16_t raw;

    DS18S20_ReadRaw(&lsb, &msb);
    raw = (int16_t)(((uint16_t)msb << 8U) | lsb);
    ds18_temp_x10 = ((int)raw * 10) / 16;
    return ds18_temp_x10;
}

/*
 * Blocking temperature read in whole degrees Celsius.
 */
int DS18S20_ReadTempInt(void)
{
    return DS18S20_ReadTemp_x10() / 10;
}

/*
 * Read the OneWire family code from the sensor ROM.
 *
 * Uses the Read ROM command (0x33) — only valid when one device is
 * on the bus.  The family code is the first byte of the ROM.
 */
uint8_t DS18_ReadFamilyCode(void)
{
    if (!ow_reset())
    {
        return 0U;
    }
    ow_write_byte(0x33U); /* Read ROM */
    return ow_read_byte();
}
