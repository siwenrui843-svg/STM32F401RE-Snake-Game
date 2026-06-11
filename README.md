# Embedded Tilt-Controlled Snake Game

## Group Members

| Name | BUPT Student ID | QM Student ID |
|------|----------------|---------------|
| MEMBER 1 | 20240001 | 10000001 |
| MEMBER 2 | 20240002 | 10000002 |
| MEMBER 3 | 20240003 | 10000003 |

## Hardware Used

- STM32F401RE NUCLEO Development Board
- MPU6500 6-axis IMU (Accelerometer + Gyroscope) via I2C
- 128x64 OLED Display (SSD1315/SSD1306) with 4 buttons via I2C
- 16x2 LCD Character Display (4-bit parallel mode)
- DS18S20/DS1820 Temperature Sensor (OneWire)
- 2x 4.7k Pull-up Resistors
- Breadboard and Jumper Wires

## Software / IDE

- Keil MDK uVision 5.43
- Programming Language: C
- Target: STM32F401RETx

## Pin Connections

| Component | Signal | NUCLEO Pin | MCU Port |
|-----------|--------|-----------|----------|
| I2C Bus   | SCL    | CN10 Pin 3 | PB8      |
| I2C Bus   | SDA    | CN10 Pin 5 | PB9      |
| DS1820    | DQ     | CN7 Pin 30 | PA1      |
| OLED K1   | Button | CN10 Pin 2 | PC8      |
| OLED K2   | Button | CN10 Pin 4 | PC6      |
| OLED K3   | Button | CN10 Pin 6 | PC5      |
| OLED K4   | Button | CN10 Pin 1 | PC9      |
| LCD       | RS     | CN5 Pin 2  | PC7      |
| LCD       | E      | CN5 Pin 1  | PA9      |
| LCD       | DB7    | CN9 Pin 8  | PA8      |
| LCD       | DB6    | CN9 Pin 7  | PB10     |
| LCD       | DB5    | CN9 Pin 6  | PB4      |
| LCD       | DB4    | CN9 Pin 5  | PB5      |
| Power     | VCC    | CN7 Pin 16 | 3.3V     |
| Power     | GND    | CN7 Pin 20/22 | GND   |

## How to Run

1. Open `MiniProjectDemo.uvprojx` in Keil uVision 5.
2. Add the new source files to the project:
   - `src/snake_game.c`
   - `src/snake_render.c`
   - `src/gesture.c`
   - `src/app.c`
3. Ensure `src/demo_main.c` is the main source file.
4. Connect the NUCLEO board via USB.
5. Build the project (Project → Build Target or F7).
6. Download to the board (Flash → Download or F8).
7. Press the reset button on the NUCLEO board to start.

## Controls

### Main Menu
- K1 (PC8): Move cursor up
- K2 (PC6): Move cursor down
- K3 (PC5): Select option
- K4 (PC9): (reserved)

### Button Control Mode
- K1: Move Snake UP
- K2: Move Snake DOWN
- K3: Move Snake LEFT
- K4: Pause game

### Tilt Control Mode
- Tilt board forward (pitch up): Snake moves UP
- Tilt board backward (pitch down): Snake moves DOWN
- Tilt board left (roll left): Snake moves LEFT
- Tilt board right (roll right): Snake moves RIGHT

### During Gameplay
- K4: Pause
- From Pause: K3 = Resume, K4 = Quit to Menu

## Game Features

- 3 difficulty levels based on score
- Score increases by 10 for each food item eaten
- Snake grows when eating food
- Wall collision detection
- Self-collision detection
- MPU6500 calibration with dead-zone filtering
- Temperature display on LCD
- High score tracking

## File Structure

```
src/
  demo_main.c          - Main program (hardware init + main loop)
  app.c / hfiles/app.h - Application state machine
  snake_game.c / hfiles/snake_game.h       - Snake game engine
  snake_render.c / hfiles/snake_render.h   - OLED game rendering
  gesture.c / hfiles/gesture.h             - Tilt detection
  (existing files: ui_display.c, utils.c, plus libraries)
```

## Known Limitations

- Tilt control requires proper MPU6500 calibration before use
- The DS18S20 temperature sensor may take up to 750ms per conversion
- OLED rendering uses differential updates; the first frame after
  starting a new game performs a full screen clear
- Button debouncing is handled in software (20ms sampling)
