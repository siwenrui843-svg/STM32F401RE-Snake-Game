#ifndef SNAKE_GAME_H
#define SNAKE_GAME_H

#include <stdint.h>

/*
 * Grid dimensions for the Snake game.
 *
 * The OLED display is 128x64 pixels.
 * Each grid cell is 4x4 pixels, giving a 32-column by 16-row total grid.
 *
 * The outer 1-cell perimeter is a WALL (border).  The playable area is
 * the inner 30 columns × 14 rows.
 */
#define GRID_WIDTH   32U
#define GRID_HEIGHT  16U

/*
 * Playable area boundaries (inclusive).
 *
 * Columns 1..30 and rows 1..14 are the playable cells.
 * Columns 0 and 31, rows 0 and 15 are border walls.
 */
#define PLAYABLE_MIN_X  1U
#define PLAYABLE_MAX_X  (GRID_WIDTH  - 2U)   /* 30 */
#define PLAYABLE_MIN_Y  1U
#define PLAYABLE_MAX_Y  (GRID_HEIGHT - 2U)   /* 14 */

/*
 * Maximum length the Snake can reach.
 */
#define MAX_SNAKE_LENGTH 100U

/*
 * Maximum number of obstacle cells on the map.
 */
#define MAX_OBSTACLES  40U

/*
 * Number of points added to the score when the Snake eats one food item.
 */
#define FOOD_SCORE  10

/*
 * Score thresholds at which the game level increases.
 */
#define LEVEL2_THRESHOLD 30
#define LEVEL3_THRESHOLD 60

/*
 * Snake movement interval in milliseconds for each level.
 *
 * Higher levels → shorter interval → faster snake.
 */
#define SNAKE_SPEED_L1_MS  180U
#define SNAKE_SPEED_L2_MS  120U
#define SNAKE_SPEED_L3_MS   70U

/*
 * Snake direction enumeration.
 */
typedef enum {
    DIR_UP    = 0,
    DIR_DOWN  = 1,
    DIR_LEFT  = 2,
    DIR_RIGHT = 3
} SnakeDirection;

/*
 * Structure that holds the complete state of one Snake game session.
 */
typedef struct {
    /*
     * Snake body segment positions.  Index 0 is the head.
     */
    uint8_t body_x[MAX_SNAKE_LENGTH];
    uint8_t body_y[MAX_SNAKE_LENGTH];

    /*
     * Current length of the Snake, in number of segments.
     */
    uint8_t length;

    /*
     * Current and next movement direction.
     */
    SnakeDirection direction;
    SnakeDirection next_direction;

    /*
     * Food position on the grid.
     */
    uint8_t food_x;
    uint8_t food_y;

    /*
     * Current score and high score.
     */
    uint16_t score;
    uint16_t high_score;

    /*
     * Current game level (1, 2, or 3).
     */
    uint8_t level;

    /*
     * Game-over flag (1 = game over).
     */
    uint8_t game_over;

    /*
     * Obstacle wall positions.
     */
    uint8_t obstacle_x[MAX_OBSTACLES];
    uint8_t obstacle_y[MAX_OBSTACLES];
    uint8_t obstacle_count;

} SnakeGame_t;

/*
 * Initialises the Snake game state for a new game session.
 *
 * The Snake starts near the centre of the playable area, moving right,
 * with an initial length of 3 segments.  Food is placed randomly.
 * Obstacles are placed according to the current level.
 * The score is reset to 0, level to 1, and high_score is preserved.
 */
void SnakeGame_Init(SnakeGame_t *game);

/*
 * Sets the desired direction for the Snake.
 * Rejected if it would reverse the Snake into itself.
 */
void SnakeGame_SetDirection(SnakeGame_t *game, SnakeDirection direction);

/*
 * Moves the Snake forward one cell and handles all game logic:
 * wall collision, obstacle collision, self-collision, food eating,
 * scoring, growth, new food placement, and level update.
 */
void SnakeGame_Update(SnakeGame_t *game);

/*
 * Places the food at a random position inside the playable area
 * that is not occupied by the Snake or by an obstacle.
 */
void SnakeGame_PlaceFood(SnakeGame_t *game);

/*
 * Returns the movement delay in ms for the current game level.
 *
 * Higher levels return shorter intervals, making the snake faster.
 */
uint32_t SnakeGame_GetSpeedMs(const SnakeGame_t *game);

/*
 * Updates the game level based on the current score.
 */
void SnakeGame_UpdateLevel(SnakeGame_t *game);

/*
 * Returns 1 if the given cell is part of the border wall or
 * an internal obstacle.
 */
uint8_t SnakeGame_IsObstacle(const SnakeGame_t *game, uint8_t x, uint8_t y);

/*
 * Returns 1 if the given cell is inside the playable area
 * (not a border wall cell).
 */
uint8_t SnakeGame_IsPlayable(uint8_t x, uint8_t y);

#endif /* SNAKE_GAME_H */
