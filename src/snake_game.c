#include "../src/hfiles/snake_game.h"
#include "../src/hfiles/timer_delay.h"

/*
 * Simple pseudo-random number generator for embedded use.
 */
static int simple_random(int *seed)
{
    *seed = (*seed * 1103515245 + 12345) & 0x7FFFFFFF;
    return *seed;
}

/*
 * Places the internal obstacle walls.
 *
 * Obstacles are defined in playable-area coordinates (1..30, 1..14).
 * The set is fixed — it does not change with game level.
 */
static void PlaceObstacles(SnakeGame_t *game)
{
    uint8_t i = 0;
    uint8_t x;

    /*
     * Centre wall block at rows 5-6.
     *
     * Placed away from the Snake's starting position (~ row 10)
     * so the player has room to react.
     */
    for (x = 13; x <= 18; x++)
    {
        game->obstacle_x[i] = x;
        game->obstacle_y[i] = 5;
        i++;
    }
    for (x = 13; x <= 18; x++)
    {
        game->obstacle_x[i] = x;
        game->obstacle_y[i] = 6;
        i++;
    }

    game->obstacle_count = i;
}

/*
 * Initialises the Snake game state for a new session.
 */
void SnakeGame_Init(SnakeGame_t *game)
{
    uint8_t i;

    /*
     * Snake starts with 3 segments near the centre of the playable area.
     */
    game->length = 3;

    /*
     * Place the head near the horizontal centre and at row 10
     * (well clear of the centre obstacle block at rows 5-6).
     */
    game->body_x[0] = (PLAYABLE_MIN_X + PLAYABLE_MAX_X) / 2;   /* 15 */
    game->body_y[0] = 10;
    game->body_x[1] = game->body_x[0] - 1;
    game->body_y[1] = game->body_y[0];
    game->body_x[2] = game->body_x[0] - 2;
    game->body_y[2] = game->body_y[0];

    game->direction      = DIR_RIGHT;
    game->next_direction = DIR_RIGHT;

    game->score = 0;
    game->level = 1;
    game->game_over = 0;

    /*
     * Place obstacles for the current level.
     */
    PlaceObstacles(game);

    /*
     * Place the first food item on a free cell.
     */
    SnakeGame_PlaceFood(game);

    /*
     * Clear any leftover body positions.
     */
    for (i = game->length; i < MAX_SNAKE_LENGTH; i++)
    {
        game->body_x[i] = 0;
        game->body_y[i] = 0;
    }
}

/*
 * Sets a new desired direction for the Snake.
 */
void SnakeGame_SetDirection(SnakeGame_t *game, SnakeDirection direction)
{
    if (game->direction == DIR_RIGHT && direction == DIR_LEFT)  { return; }
    if (game->direction == DIR_LEFT  && direction == DIR_RIGHT) { return; }
    if (game->direction == DIR_UP    && direction == DIR_DOWN)  { return; }
    if (game->direction == DIR_DOWN  && direction == DIR_UP)    { return; }

    game->next_direction = direction;
}

/*
 * Returns 1 if a cell is inside the playable area.
 */
uint8_t SnakeGame_IsPlayable(uint8_t x, uint8_t y)
{
    return (x >= PLAYABLE_MIN_X && x <= PLAYABLE_MAX_X &&
            y >= PLAYABLE_MIN_Y && y <= PLAYABLE_MAX_Y) ? 1 : 0;
}

/*
 * Returns 1 if a cell is part of the border wall or an internal obstacle.
 */
uint8_t SnakeGame_IsObstacle(const SnakeGame_t *game, uint8_t x, uint8_t y)
{
    uint8_t i;

    /*
     * Border walls: cells outside the playable area.
     */
    if (!SnakeGame_IsPlayable(x, y))
    {
        return 1;
    }

    /*
     * Internal obstacles.
     */
    for (i = 0; i < game->obstacle_count; i++)
    {
        if (game->obstacle_x[i] == x && game->obstacle_y[i] == y)
        {
            return 1;
        }
    }
    return 0;
}

/*
 * Checks whether a given grid cell is occupied by any part of the Snake.
 */
static uint8_t IsCellOccupiedBySnake(const SnakeGame_t *game,
                                     uint8_t x, uint8_t y)
{
    uint8_t i;
    for (i = 0; i < game->length; i++)
    {
        if (game->body_x[i] == x && game->body_y[i] == y)
        {
            return 1;
        }
    }
    return 0;
}

/*
 * Moves the Snake one step and handles all game logic.
 */
void SnakeGame_Update(SnakeGame_t *game)
{
    uint8_t i;
    uint8_t new_head_x;
    uint8_t new_head_y;

    if (game->game_over)
    {
        return;
    }

    game->direction = game->next_direction;

    new_head_x = game->body_x[0];
    new_head_y = game->body_y[0];

    switch (game->direction)
    {
    case DIR_UP:    new_head_y--; break;
    case DIR_DOWN:  new_head_y++; break;
    case DIR_LEFT:  new_head_x--; break;
    case DIR_RIGHT: new_head_x++; break;
    default: break;
    }

    /*
     * Wall / obstacle collision:
     * The snake dies if it leaves the playable area or hits an
     * internal obstacle.
     */
    if (SnakeGame_IsObstacle(game, new_head_x, new_head_y))
    {
        game->game_over = 1;
        if (game->score > game->high_score)
        {
            game->high_score = game->score;
        }
        return;
    }

    /*
     * Self-collision:
     * Check against all body segments except the tail (which will
     * move away unless the snake is about to grow).
     */
    for (i = 0; i < game->length - 1; i++)
    {
        if (game->body_x[i] == new_head_x &&
            game->body_y[i] == new_head_y)
        {
            game->game_over = 1;
            if (game->score > game->high_score)
            {
                game->high_score = game->score;
            }
            return;
        }
    }

    /*
     * Shift body segments.
     */
    for (i = game->length - 1; i > 0; i--)
    {
        game->body_x[i] = game->body_x[i - 1];
        game->body_y[i] = game->body_y[i - 1];
    }

    game->body_x[0] = new_head_x;
    game->body_y[0] = new_head_y;

    /*
     * Food detection.
     */
    if (new_head_x == game->food_x && new_head_y == game->food_y)
    {
        game->score += FOOD_SCORE;

        if (game->length < MAX_SNAKE_LENGTH)
        {
            game->body_x[game->length] = game->body_x[game->length - 1];
            game->body_y[game->length] = game->body_y[game->length - 1];
            game->length++;
        }

        SnakeGame_PlaceFood(game);
        SnakeGame_UpdateLevel(game);
    }
}

/*
 * Places the food at a random unoccupied playable cell.
 */
void SnakeGame_PlaceFood(SnakeGame_t *game)
{
    uint8_t x, y;
    int seed;
    int attempts;

    seed = (int)msTicks;

    for (attempts = 0; attempts < 2000; attempts++)
    {
        x = PLAYABLE_MIN_X +
            (uint8_t)(simple_random(&seed) %
                      (PLAYABLE_MAX_X - PLAYABLE_MIN_X + 1));
        y = PLAYABLE_MIN_Y +
            (uint8_t)(simple_random(&seed) %
                      (PLAYABLE_MAX_Y - PLAYABLE_MIN_Y + 1));

        if (!IsCellOccupiedBySnake(game, x, y) &&
            !SnakeGame_IsObstacle(game, x, y))
        {
            game->food_x = x;
            game->food_y = y;
            return;
        }
    }

    /*
     * Fallback: linear scan for a free cell.
     */
    for (x = PLAYABLE_MIN_X; x <= PLAYABLE_MAX_X; x++)
    {
        for (y = PLAYABLE_MIN_Y; y <= PLAYABLE_MAX_Y; y++)
        {
            if (!IsCellOccupiedBySnake(game, x, y) &&
                !SnakeGame_IsObstacle(game, x, y))
            {
                game->food_x = x;
                game->food_y = y;
                return;
            }
        }
    }
}

uint32_t SnakeGame_GetSpeedMs(const SnakeGame_t *game)
{
    switch (game->level)
    {
    case 1:  return SNAKE_SPEED_L1_MS;
    case 2:  return SNAKE_SPEED_L2_MS;
    case 3:
    default: return SNAKE_SPEED_L3_MS;
    }
}

void SnakeGame_UpdateLevel(SnakeGame_t *game)
{
    if (game->score >= LEVEL3_THRESHOLD)
    {
        game->level = 3;
    }
    else if (game->score >= LEVEL2_THRESHOLD)
    {
        game->level = 2;
    }
    else
    {
        game->level = 1;
    }
}
