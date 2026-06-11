#include "../src/hfiles/snake_render.h"
#include "../src/hfiles/oled.h"
#include <string.h>

/*
 * ---------------------------------------------------------------------------
 * Framebuffer — 128 columns × 8 pages = 1024 bytes.
 *
 * Each byte in the SSD1306 GDDRAM represents 8 vertical pixels within one
 * page.  The display is 128×64 pixels, organised as 8 pages of 8 rows each.
 *
 * Because each grid cell is 4×4 pixels, two grid rows share one OLED page:
 *   - Even grid rows → bits 3:0 of the page byte
 *   - Odd  grid rows → bits 7:4 of the page byte
 *
 * Every Draw*() function now writes into this framebuffer first.  When all
 * elements (walls, food, snake) have been rendered, the whole framebuffer
 * is flushed to the OLED in one pass.  This separates wall and snake
 * rendering completely — walls are never overwritten by snake movement.
 * ---------------------------------------------------------------------------
 */
static uint8_t fb[1024];       /* fb[page * 128 + column] — current frame */
static uint8_t fb_last[1024];  /* last flushed frame — for dirty-page detection */


/*
 * ---------------------------------------------------------------------------
 * Pre-computed 4-byte patterns for each cell type and row parity.
 *
 * All even-row patterns only touch bits 3:0; all odd-row patterns only
 * touch bits 7:4.  This guarantees that two cells sharing the same OLED
 * page never interfere with each other when OR-ed into the framebuffer.
 * ---------------------------------------------------------------------------
 */

/* Snake body — solid 4×4 block */
static const uint8_t P_SNAKE_EVEN[4] = { 0x0F, 0x0F, 0x0F, 0x0F };
static const uint8_t P_SNAKE_ODD[4]  = { 0xF0, 0xF0, 0xF0, 0xF0 };

/* Snake head — identical to body (can be customised later) */
static const uint8_t P_HEAD_EVEN[4]  = { 0x0F, 0x0F, 0x0F, 0x0F };
static const uint8_t P_HEAD_ODD[4]   = { 0xF0, 0xF0, 0xF0, 0xF0 };

/* Food — circular 4×4 pattern */
static const uint8_t P_FOOD_EVEN[4]  = { 0x06, 0x0F, 0x0F, 0x06 };
static const uint8_t P_FOOD_ODD[4]   = { 0x60, 0xF0, 0xF0, 0x60 };

/* Wall / obstacle — checkerboard for high visibility */
static const uint8_t P_WALL_EVEN[4]  = { 0x00, 0x0F, 0x00, 0x0F };
static const uint8_t P_WALL_ODD[4]   = { 0xF0, 0x00, 0xF0, 0x00 };

/* Danger sign — filled triangle (△) */
static const uint8_t P_DANGER_EVEN[4] = { 0x04, 0x07, 0x07, 0x04 };
static const uint8_t P_DANGER_ODD[4]  = { 0x40, 0x70, 0x70, 0x40 };

/* End sign — cross (✕) */
static const uint8_t P_END_EVEN[4] = { 0x09, 0x06, 0x06, 0x09 };
static const uint8_t P_END_ODD[4]  = { 0x90, 0x60, 0x60, 0x90 };


/*
 * ---------------------------------------------------------------------------
 * Internal helper — writes one pattern into the framebuffer.
 *
 * The pattern is OR-ed so that cells on opposite halves of the same OLED
 * page (different grid-row parity) accumulate correctly.
 * ---------------------------------------------------------------------------
 */
static void FB_Write(uint8_t grid_x, uint8_t grid_y,
                     const uint8_t pattern[4])
{
    uint8_t page      = grid_y / 2U;
    uint8_t col_start = grid_x * 4U;
    uint8_t i;

    for (i = 0; i < 4U; i++)
    {
        fb[(uint16_t)page * 128U + col_start + i] |= pattern[i];
    }
}


/*
 * ---------------------------------------------------------------------------
 * Per-element drawing helpers (all write into the framebuffer).
 * ---------------------------------------------------------------------------
 */

static void DrawWall(uint8_t grid_x, uint8_t grid_y)
{
    FB_Write(grid_x, grid_y,
             (grid_y & 1U) ? P_WALL_ODD : P_WALL_EVEN);
}

static void DrawSnakeBody(uint8_t grid_x, uint8_t grid_y)
{
    FB_Write(grid_x, grid_y,
             (grid_y & 1U) ? P_SNAKE_ODD : P_SNAKE_EVEN);
}

static void DrawSnakeHead(uint8_t grid_x, uint8_t grid_y)
{
    FB_Write(grid_x, grid_y,
             (grid_y & 1U) ? P_HEAD_ODD : P_HEAD_EVEN);
}

static void DrawFood(uint8_t grid_x, uint8_t grid_y)
{
    FB_Write(grid_x, grid_y,
             (grid_y & 1U) ? P_FOOD_ODD : P_FOOD_EVEN);
}

static void DrawDanger(uint8_t grid_x, uint8_t grid_y)
{
    FB_Write(grid_x, grid_y,
             (grid_y & 1U) ? P_DANGER_ODD : P_DANGER_EVEN);
}

static void DrawEnd(uint8_t grid_x, uint8_t grid_y)
{
    FB_Write(grid_x, grid_y,
             (grid_y & 1U) ? P_END_ODD : P_END_EVEN);
}


/*
 * ---------------------------------------------------------------------------
 * Flushes only the dirty cells of the framebuffer to the OLED.
 *
 * Each 4×4-pixel grid cell is compared against the "last flushed" copy.
 * Only cells that changed are sent over I2C.  For a typical snake move
 * (head advances → 1 cell, tail clears → 1 cell) this means just
 * 2 cells = 8 data bytes + 2 cursor sets ≈ 14 I2C transactions total.
 *
 * This is roughly 30× fewer I2C transactions than the old per-page
 * approach, making the flush time negligible (~4 ms).
 * ---------------------------------------------------------------------------
 */
static void FB_Flush(void)
{
    uint8_t gy;

    for (gy = 0; gy < GRID_HEIGHT; gy++)
    {
        uint8_t gx;
        for (gx = 0; gx < GRID_WIDTH; gx++)
        {
            uint8_t  page      = gy / 2U;
            uint8_t  col_start = gx * 4U;
            uint16_t offset    = (uint16_t)page * 128U + col_start;
            uint8_t  i;

            /* Skip if this cell hasn't changed. */
            {
                uint8_t dirty = 0;
                for (i = 0; i < 4U; i++)
                {
                    if (fb[offset + i] != fb_last[offset + i])
                    {
                        dirty = 1;
                        break;
                    }
                }
                if (!dirty) { continue; }
            }

            /* Cell changed — send just these 4 bytes. */
            OLED_SetCursor(page, col_start);
            for (i = 0; i < 4U; i++)
            {
                OLED_Data(fb[offset + i]);
            }

            /* Remember what we just sent. */
            for (i = 0; i < 4U; i++)
            {
                fb_last[offset + i] = fb[offset + i];
            }
        }
    }
}


/*
 * ---------------------------------------------------------------------------
 * Public functions.
 * ---------------------------------------------------------------------------
 */

void SnakeRender_Init(void)
{
    /*
     * Clear both framebuffers so the first Draw() flushes every
     * page that has content.
     */
    memset(fb,      0, sizeof(fb));
    memset(fb_last, 0, sizeof(fb_last));
}

void SnakeRender_Reset(void)
{
    /*
     * Clear the "last flushed" buffer so the next Draw() sends
     * a full frame.  The active framebuffer is rebuilt from
     * scratch inside Draw() anyway.
     */
    memset(fb_last, 0, sizeof(fb_last));
}

void SnakeRender_DrawBorder(void)
{
    uint8_t x, y;

    /* Top and bottom border rows — skip portal openings */
    for (x = 0; x < GRID_WIDTH; x++)
    {
        if (x != PORTAL_COL)
        {
            DrawWall(x, 0);
            DrawWall(x, (uint8_t)(GRID_HEIGHT - 1U));
        }
    }

    /* Left and right border columns — skip portal openings */
    for (y = 1; y < (uint8_t)(GRID_HEIGHT - 1U); y++)
    {
        if (y != PORTAL_ROW)
        {
            DrawWall(0, y);
            DrawWall((uint8_t)(GRID_WIDTH - 1U), y);
        }
    }
}

void SnakeRender_Draw(const SnakeGame_t *game)
{
    uint8_t i;

    if (game == ((void *)0)) { return; }

    /*
     * 1. Clear the framebuffer.
     */
    memset(fb, 0, sizeof(fb));

    /*
     * 2. Draw static elements: border walls and internal obstacles.
     *    These are always drawn first so they are never accidentally
     *    erased by snake movement.
     */
    SnakeRender_DrawBorder();

    for (i = 0; i < game->obstacle_count; i++)
    {
        DrawWall(game->obstacle_x[i], game->obstacle_y[i]);
    }

    /*
     * 3. Draw food.
     */
    DrawFood(game->food_x, game->food_y);

    /*
     * 4. Draw danger sign (triangle).
     */
    DrawDanger(game->danger_x, game->danger_y);

    /*
     * 5. Draw end sign (cross / X).
     */
    DrawEnd(game->end_x, game->end_y);

    /*
     * 6. Draw snake body segments (tail to head).
     *    The head is drawn last so it appears on top.
     */
    for (i = game->length; i > 0; i--)
    {
        uint8_t idx = (uint8_t)(i - 1U);

        if (idx == 0U)
        {
            DrawSnakeHead(game->body_x[idx], game->body_y[idx]);
        }
        else
        {
            DrawSnakeBody(game->body_x[idx], game->body_y[idx]);
        }
    }

    /*
     * 7. Flush the completed framebuffer to the OLED.
     */
    FB_Flush();
}
