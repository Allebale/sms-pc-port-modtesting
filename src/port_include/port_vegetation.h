// Moving Vegetation (ext/core/game/port_vegetation.cpp): plants sway in the wind, bend as Mario goes
// through them, and FLUDD's water pushes them aside. Palm trees' fronds (TMapObjTree's leaves,
// each on its own spring already) are driven by the wind and droop under the spray; the leafy
// parts of every model (found by their materials' names as each model loads) are moved by the
// renderer. Off, the game is as it was.
#ifndef PORT_VEGETATION_H
#define PORT_VEGETATION_H

#ifdef __cplusplus
class TMapObjTree;
class J3DModelData;
// From TMapObjTree::perform (zzzzz-ext-32-vegetation-trees), each tick before its leaves move: the
// wind's and the spray's push on them.
void port_tree_sway(TMapObjTree* tree);
// From J3DModelLoaderDataBase::load (zzzzz-ext-32-vegetation-models), each model as it loads: its
// leafy materials' display lists, for the renderer.
void port_veg_model(J3DModelData* data);
extern "C" {
#endif

// The setting (the pause menu's Settings, Graphics): 0 off, 1 gentle, 2 breezy, 3 windy.
int port_vegetation(void);

#ifdef __cplusplus
}
#endif

#endif
