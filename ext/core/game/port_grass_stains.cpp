// Grass Stains (the pause menu's Settings, Graphics; off is the original): where Mario's clothes
// rub along grass, they take green stains, as clothes do: only where there is friction.
//   - a belly slide (after a dive) on grass: his front (the overalls' knees and bib, his shirt's
//     front, his gloves, the toes of his shoes, a little on his cap);
//   - sliding on his seat, or falling on his back, on grass: his back;
//   - skidding to a stop on grass: his shoes.
// The longer he slides, the deeper the stain. Not running, landing or standing on it: no friction.
// Each cloth takes it as it would (the renderer, gx_shader.cpp, u_gstain, tells them by colour):
// the denim most, the white gloves show it plainly, the red shirt and cap less, the brown leather
// shoes least. Stains are stubborn: they fade over the Grass Stain Duration setting's time (three
// minutes at first, or never by themselves), and water washes them out in a few seconds.
#include "port_ext.h"
#include "port_puddles.h"
#include "port_shadows.h"

#include <Map/MapCollisionData.hpp>
#include <Player/Mario.hpp>
#include <Player/MarioAccess.hpp>
#include <Player/MarioFlags.hpp>
#include <dolphin/os.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

// The renderer's (platform/gx/src/gx_api.cpp): the grass stains on what is drawn now (null: none).
extern "C" void GXPC_SetGrassStains(const float* stains);

namespace {

const f32 kStrength[] = {0.0f, 0.5f, 0.75f, 1.0f};  // Off, Light, Normal, Strong
const f32 kWashSeconds = 4.0f;    // in water: washed out

f32 s_front, s_back, s_shoes;  // how stained, 0 to 1
s32 s_lastMs;
u32 s_was;
f32 s_lastX, s_lastZ;

s32 nowMs() { return (s32)OSTicksToMilliseconds(OSGetTime()); }

const u32 kTypeMask = 0x1C0, kTypeAir = 0x080;  // MarioStatus.hpp: in the air
bool bellySlide(u32 s) { return s == 0x800456 || s == 0x84045D; }
bool seat(u32 s) { return s == 0x50 || s == 0x840452 || s == 0x840453 || s == 0x4808459; }
bool onBack(u32 s) { return s == 0x20460 || s == 0x20462 || s == 0x20464 || s == 0x208B0 || s == 0x208B3; }
bool skid(u32 s) { return s == 0x4000445 || s == 0x443; }

void rub(f32& stain, f32 amount)
{
	stain += amount;
	if (stain > 1.0f)
		stain = 1.0f;
}

void fade(f32& stain, f32 amount)
{
	stain -= amount;
	if (stain < 0.0f)
		stain = 0.0f;
}

void afterMovement()
{
	const int level = port_grass_stains();
	TMario* m       = gpMarioOriginal;
	const s32 now   = nowMs();
	f32 dt          = s_lastMs ? (f32)(now - s_lastMs) * 0.001f : 0.0f;
	s_lastMs        = now;
	if (dt < 0.0f || dt > 0.25f)
		dt = 0.0f;  // a pause, a load: not time rubbing
	if (level <= 0 || !m) {
		s_front = s_back = s_shoes = 0.0f;
		return;
	}
	const u32 st  = m->mStatus;
	const u32 was = s_was;
	s_was         = st;
	const f32 dx = m->mPosition.x - s_lastX, dz = m->mPosition.z - s_lastZ;
	s_lastX = m->mPosition.x, s_lastZ = m->mPosition.z;
	const f32 moved = dt > 0.0f ? sqrtf(dx * dx + dz * dz) / dt : 0.0f;  // units a second
	// water washes them out; otherwise they fade, slowly
	const bool wet = (st & 0x2000) || m->checkFlag(MARIO_FLAG_IN_WATER) || m->checkFlag(MARIO_FLAG_IN_SHALLOW_WATER);
	const f32 fadeSeconds = port_grass_stain_seconds();  // Grass Stain Duration (0: only water)
	const f32 out = wet ? dt / kWashSeconds : fadeSeconds > 0.0f ? dt / fadeSeconds : 0.0f;
	fade(s_front, out), fade(s_back, out), fade(s_shoes, out);
	if (wet)
		return;
	if ((st & kTypeMask) == kTypeAir)
		return;
	const TBGCheckData* floor = gpMarioGroundPlane ? *gpMarioGroundPlane : 0;
	if (!floor || !floor->isLegal() || m->mPosition.y - m->mFloorPosition.y > 10.0f)
		return;  // not touching the ground
	if (port_puddle_surface_kind(floor->mBGType, floor->unk6) != PORT_PUDDLE_GRASS)
		return;  // not grass
	// friction: how hard he rubs along it (faster, harder), from when he hits it
	const f32 speed = moved > 600.0f ? 1.0f : moved / 600.0f;
	const bool hit  = st != was;
	if (bellySlide(st)) {
		rub(s_front, (hit ? 0.25f : 0.0f) + dt * (0.15f + 0.6f * speed));
		rub(s_shoes, dt * 0.15f);  // his toes drag
	} else if (seat(st) || onBack(st)) {
		rub(s_back, (hit ? 0.2f : 0.0f) + dt * (0.15f + 0.5f * speed));
	} else if (skid(st)) {
		rub(s_shoes, dt * (0.1f + 0.3f * speed));
	} else
		return;
	static bool s_told;
	if (!s_told) {
		s_told = true;
		fprintf(stderr, "[stains] the grass stains Mario's clothes\n");
	}
}

void stageSetup()
{
	s_lastMs = 0;
	s_lastX = s_lastZ = 0.0f;
}

PortExtension s_extension;

struct Register {
	Register()
	{
		memset(&s_extension, 0, sizeof s_extension);
		s_extension.name          = "grass stains";
		s_extension.stageSetup    = stageSetup;
		s_extension.afterMovement = afterMovement;
		port_ext_register(&s_extension);
	}
} s_register;

} // namespace

// Around Mario's own draw (port_wet_mario_draw): the stains on him, in the camera's view.
// u_gstain: [0] the plane at his feet (up, in the view); [1] his front's, back's and shoes'
// stains, 1 while he stands (0 lying down: then his height above his feet says nothing); [2] the
// way he faces (in the view).
void port_grass_stains_draw(int begin, float (*view)[4])
{
	TMario* m       = gpMarioOriginal;
	const int level = port_grass_stains();
	if (!begin || level <= 0 || !m || !view) {
		GXPC_SetGrassStains(0);
		return;
	}
	const f32 k = kStrength[level > 3 ? 3 : level];
	if ((s_front + s_back + s_shoes) * k <= 0.0f) {
		GXPC_SetGrassStains(0);
		return;
	}
	f32 u[12];
	for (int r = 0; r < 3; r++)
		u[r] = view[r][1];  // up
	u[3] = -m->mPosition.y - (u[0] * view[0][3] + u[1] * view[1][3] + u[2] * view[2][3]);
	u[4] = s_front * k, u[5] = s_back * k, u[6] = s_shoes * k;
	const u32 st = m->mStatus;
	u[7] = bellySlide(st) || seat(st) || onBack(st) ? 0.0f : 1.0f;
	const f32 a  = (f32)m->mFaceAngle.y * (6.2831853f / 65536.0f);
	const f32 fx = sinf(a), fz = cosf(a);
	for (int r = 0; r < 3; r++)
		u[8 + r] = view[r][0] * fx + view[r][2] * fz;
	u[11] = 0.0f;
	GXPC_SetGrassStains(u);
}
