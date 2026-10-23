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
// Reflections by Surface: 1 when how strongly a puddle reflects follows the floor under it
// (sand less, smooth floors fully); 0 every puddle as strongly, as the game has it.
int port_puddle_surface(void);
// The surfaces Reflections by Surface tells apart (ext/core/game/port_reflections.cpp sorts a
// floor into one by its type and sound material), each with its strength on the settings menu's
// Other page.
enum {
	PORT_PUDDLE_STONE,  // smooth stone: the plaza's tiles, its carpets, and every floor not below
	PORT_PUDDLE_METAL,
	PORT_PUDDLE_CLAY,   // clay roof tiles
	PORT_PUDDLE_WOOD,
	PORT_PUDDLE_SAND,
	PORT_PUDDLE_GRASS,
	PORT_PUDDLE_CANVAS,
	PORT_PUDDLE_WET,    // wet and slippery floors
	PORT_PUDDLE_SURFACES
};
// How strongly a puddle on that surface reflects, in tenths of a percent of the game's own
// (where every puddle is full): 75 is 7.5%.
int port_puddle_surface_strength(int surface);
// How strongly a puddle on a floor of that type (mBGType) and sound material (its triangle's
// unk6) reflects, 0 to 255 (ext/core/game/port_reflections.cpp).
unsigned port_puddle_surface_alpha(unsigned type, unsigned material);

// Reflection Distance: the radius, in the game's units, of the disc around Mario that puddle
// reflections are drawn over, fading out to its edge: 500, 750, 1000 (Original), 2000, 3000 or 5000.
float port_puddle_reflection_distance(void);

#ifdef __cplusplus
}
#endif

#endif
