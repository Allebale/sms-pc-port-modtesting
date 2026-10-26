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
// Shadow Distance: how far the far map reaches either side of Mario, in units.
float port_shadows_distance(void);
// Keep the Original Shadow Direction: 1 the sun's shadows fall exactly where the game's own fell
// (along its light, as TMBindShadowManager casts them); 0 from the stage's sun in the sky.
int port_shadows_original_direction(void);
// From CPolarSubCamera::perform (zzzzz-ext-31-shadows), the main camera's projection just set:
// the sun's direction and the box round Mario for this frame, with the camera's view (world to
// view). camera: the camera (only the game's own, gpCamera, counts).
void port_shadows_main_view(void* camera, float (*view)[4]);

// The renderer's (platform/gx/src/gx_render.cpp, gx_api.cpp).
// half: the near map's reach either side of focus; reach: the far one's.
void GXPC_ShadowFrame(const float view[3][4], const float sun[3], const float focus[3], float strength,
                      float half, float reach);
void GXPC_ShadowNotMain(void);
// Around Mario's own draw (port_wet_mario_draw): for the log, whether his batches cast.
void GXPC_ShadowTag(int on);

#ifdef __cplusplus
}
#endif

#endif
