// 3D Grass: blades of grass on the stages' grass (ext/core/game/port_grass.cpp), as each place
// would have it: cut short where the Piantas look after it (Delfino Plaza, Pinna Park), wild where
// nobody does (Bianco Hills, Pianta Village...). They sway in the wind, bend away from Mario as he
// goes through them and lie flat under FLUDD's puddles. Off, the game is as it was.
#ifndef PORT_GRASS_H
#define PORT_GRASS_H

#ifdef __cplusplus
extern "C" {
#endif

// The setting (the pause menu's Settings, Graphics): 0 off, 1 by location, 2 always cut, 3 always
// wild.
int port_grass(void);
// From port_draw_footprints (TModelWaterManager::perform, as the stage's ground is drawn): the
// blades near Mario. view: the camera's (world to view).
void port_grass_draw(float (*view)[4]);

#ifdef __cplusplus
}
#endif

#endif
