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
	PORT_PUDDLE_WATER,      // Water Reflections: the sea, lakes, pools, fountains and streams
	PORT_PUDDLE_WET_GROUND, // Water Reflections: wet ground itself, puddle or not
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

// The settings menu's Reflect switches (its Other page): what is reflected, in puddles and water
// alike. Each 1 (on) unless switched off.
enum {
	PORT_REFLECT_MARIO,      // Mario and Yoshi: the game's own copies
	PORT_REFLECT_CHARACTERS, // the people (NPCs)
	PORT_REFLECT_ENEMIES,    // enemies and bosses
	PORT_REFLECT_ITEMS,      // coins, fruit and other things to take
	PORT_REFLECT_OBJECTS,    // everything else: crates, trees, doors, platforms
	PORT_REFLECT_KINDS
};
int port_reflect_kind(int kind);

// The REFLECTIONS_TOGGLE key (R by default): every reflection in puddles and water off, Mario's
// too, and back on as the settings have them. For this run only; on to start with.
int port_reflections_on(void);
void port_reflections_toggle(void);

// Water Reflections: 1 when the stage's own water and wet ground show the reflections puddles
// do (ext/core/game/port_water_reflections.cpp); 0 as the game has it.
int port_water_reflections(void);
// Water Ripples: 0 off (the reflection in water still), 1 gentle, 2 strong. Puddles and wet
// ground stay still.
int port_water_ripples(void);
// Puddle Ripples: 0 off (puddles always still), 1 gentle, 2 strong: rings spread in the puddles'
// and wet ground's reflection from each drop of FLUDD's water as it lands and from Mario's feet.
int port_puddle_ripples(void);
// Whether water or wet ground shows reflections this frame.
int port_water_mirror_active(void);
// Whether this frame's reflections are mirrored about a water surface, not the floor under Mario
// (FLUDD's puddles then show none: they are on another plane).
int port_water_mirror_on_water(void);

#ifdef __cplusplus
}
#endif

#endif
