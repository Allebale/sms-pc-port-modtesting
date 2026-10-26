// Real Shadows: the sun's shadows of everything (the level, characters, objects), drawn by the
// renderer from a shadow map (platform/gx, GXPC_ShadowFrame). Off, the game is as it was.
#ifndef PORT_SHADOWS_H
#define PORT_SHADOWS_H

#ifdef __cplusplus
extern "C" {
#endif

// The setting (the pause menu's Settings, Graphics): 0 off, 1 soft, 2 strong.
int port_shadows(void);
// With Real Shadows on: 1 the game's own round shadows under characters are kept, lighter, so that
// they stand on the ground (its trees', buildings' and boats' are left out: zzzzz-ext-31-shadows-
// old-off); 0 none of the game's own.
int port_shadows_round(void);
// From CPolarSubCamera::perform (zzzzz-ext-31-shadows), the main camera's projection just set:
// the sun's direction and the box round Mario for this frame, with the camera's view (world to
// view). camera: the camera (only the game's own, gpCamera, counts).
void port_shadows_main_view(void* camera, float (*view)[4]);

// The renderer's (platform/gx/src/gx_render.cpp, gx_api.cpp).
void GXPC_ShadowFrame(const float view[3][4], const float sun[3], const float focus[3], float strength,
                      float half);
void GXPC_ShadowNotMain(void);
// Around Mario's own draw (port_wet_mario_draw): for the log, whether his batches cast.
void GXPC_ShadowTag(int on);

#ifdef __cplusplus
}
#endif

#endif
