#ifndef MINIGORE2_PLUGIN_H
#define MINIGORE2_PLUGIN_H

#include "elf_loader.h"

/* Optional game-specific plugin for Minigore 2: Zombies (libminigore2.so).
 * Automatically detects whether the loaded ELF exports Minigore 2's
 * xt::java, xt::Imgui, and StateGame symbols. Safe no-op for any other game. */
int  minigore2_plugin_install(elf_module_t *mod);
int  minigore2_plugin_active(void);

/* Direct dual-stick injection (move_x, move_y, aim_x, aim_y in [-1, 1]).
 * Returns 1 if active and applied, 0 if caller should use virtual touch sticks. */
int  minigore2_set_sticks(float move_x, float move_y, float aim_x, float aim_y);

/* Returns 1 when inside an active gameplay level (not in menus/paused),
 * and writes the player's projected screen coordinates to *sx, *sy. */
int  minigore2_in_level(void);
int  minigore2_player_screen_pos(float *sx, float *sy);

/* Immediate-mode UI D-pad button snapping (returns 1 if a button was found) */
int  minigore2_ui_nav_count(void);
int  minigore2_ui_nav_find(float cur_x, float cur_y, int dir_x, int dir_y,
                           float *out_x, float *out_y);

#endif /* MINIGORE2_PLUGIN_H */
