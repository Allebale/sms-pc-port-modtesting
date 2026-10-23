#ifndef PORT_PUDDLES_H
#define PORT_PUDDLES_H

// Puddle Lifetime (platform/misc/port_puddle_settings.cpp): how many times longer
// the puddles FLUDD's water leaves last, from the settings menu's Graphics page.
// The game's water (TModelWaterManager, zzzzz-ext-24-puddle-lifetime) asks it every
// tick. Plain C, for the game and the settings menu alike.

#ifdef __cplusplus
extern "C" {
#endif

// 1 (Original), 2, 4 or 8.
int port_puddle_life_scale(void);

#ifdef __cplusplus
}
#endif

#endif
