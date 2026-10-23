#ifndef PORT_PUDDLES_H
#define PORT_PUDDLES_H

// FLUDD's puddles, from the settings menu's Graphics page (platform/misc/port_puddle_settings.cpp):
// Puddle Lifetime, how many times longer they last, and Puddle Reflections.
// The game's water (TModelWaterManager, zzzzz-ext-24-puddle-lifetime) asks it every
// tick. Plain C, for the game and the settings menu alike.

#ifdef __cplusplus
extern "C" {
#endif

// 1 (Original), 2, 4 or 8.
int port_puddle_life_scale(void);
// Puddle Reflections: 1 when characters and objects are reflected in the puddles, not only
// Mario (ext/core/game/port_reflections.cpp). Their reflections are made as an area loads.
int port_puddle_reflections(void);

#ifdef __cplusplus
}
#endif

#endif
