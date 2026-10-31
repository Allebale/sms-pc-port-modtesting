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
// From TMapObjTree::touchPlayer and ::controlLeaf (zzzzz-ext-32-vegetation-trees). In the game a
// frond's solid shape is only in the world while the tree is being touched; the fronds that sway by
// themselves must not be solid (they were invisible walls). port_tree_touched: Mario touches the tree.
// port_tree_leaf_collides: false while the tree is not touched, so the game's own call is skipped.
void port_tree_touched(TMapObjTree* tree);
bool port_tree_leaf_collides(TMapObjTree* tree, int index);
// From J3DModelLoaderDataBase::load (zzzzz-ext-32-vegetation-models), each model as it loads: its
// leafy materials' display lists, for the renderer.
void port_veg_model(J3DModelData* data);
// From SMS_MakeSDLModelData (zzzzz-ext-32-vegetation-named), a model made from a file: a plant's
// (by the file's name) sways whole, rooted at its foot, whatever its materials are called.
void port_veg_model_named(J3DModelData* data, const char* path);
extern "C" {
#endif

// The setting (the pause menu's Settings, Graphics): 0 off through 4 very windy.
int port_vegetation(void);

#ifdef __cplusplus
}
#endif

#endif
