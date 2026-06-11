#ifndef SNAKE_RENDER_H
#define SNAKE_RENDER_H

#include "snake_game.h"

/*
 * Initialises the Snake renderer.
 *
 * Must be called once before any other renderer function.
 */
void SnakeRender_Init(void);

/*
 * Draws the complete Snake game frame on the OLED display.
 *
 * This function builds a full framebuffer in RAM, rendering walls,
 * obstacles, food, and the snake body.  The completed framebuffer is
 * then flushed to the OLED in one pass.
 *
 * Because every frame is a full redraw, walls and obstacles are
 * never accidentally erased by snake movement — the rendering of
 * static and dynamic elements is completely separated.
 *
 * Parameters:
 *   game - Pointer to the current Snake game state.
 */
void SnakeRender_Draw(const SnakeGame_t *game);

/*
 * Resets the renderer state.
 *
 * Call this when starting a new game or returning from a menu so
 * that the next Draw() starts from a clean framebuffer.
 */
void SnakeRender_Reset(void);

/*
 * Draws the border wall into the framebuffer.
 *
 * This function is also called internally by SnakeRender_Draw().
 * It can be called directly when an explicit border-only redraw
 * is needed (e.g. after OLED_Clear() on game start).
 */
void SnakeRender_DrawBorder(void);

#endif /* SNAKE_RENDER_H */
