// Sandy Mario (the pause menu's Settings, Graphics; off is the original): where Mario's body
// touches sand, sand sticks to him, and it falls away over 20 seconds (water washes it off at once).
//   - walking or standing on sand: his shoes and the bottoms of his legs;
//   - landing on it from a jump or a fall: higher up his legs (a ground pound, higher still);
//   - a belly slide (after a dive) on it: his front, to his chest;
//   - sliding on his seat, or falling on his back, on it: his back (the seat; all of it lying down).
// Each is a part of him (how high from his feet; facing the way he faces, or away), sanded as he
// touches, fading from then. While his own draw is drawn (port_wet_mario_draw), the renderer
// (platform/gx, u_sand) gives those parts a patchy, grainy coat of sand.
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

// The renderer's (platform/gx/src/gx_api.cpp): the sand on what is drawn now (null: none).
extern "C" void GXPC_SetSand(const float* sand);

namespace {

const s32 kLastMs = 20000;  // how long sand stays, fading the whole time
const f32 kStrength[] = {0.0f, 0.55f, 0.8f, 1.0f};  // Off, Light, Normal, Heavy

s32 nowMs()
{
	const s32 ms = (s32)OSTicksToMilliseconds(OSGetTime());
	return ms;
}

struct Patch {
	f32 height;  // how high up him (from his feet)
	s32 at;      // when last sanded (0: never)
};
Patch s_feet, s_front, s_back;
u32 s_was;  // his status last tick

// How much of a patch is left: all at first, then less and less, none after kLastMs.
f32 left(const Patch& p, s32 now)
{
	if (!p.at)
		return 0.0f;
	const s32 age = now - p.at;
	if (age < 0 || age >= kLastMs)
		return 0.0f;
	const f32 t = (f32)age / (f32)kLastMs;
	return 1.0f - t * t;  // slowly at first, faster at the end
}

void sand(Patch& p, f32 height, s32 now)
{
	// a patch still sandy keeps the higher of the two
	p.height = p.at && now - p.at < kLastMs && p.height > height ? p.height : height;
	p.at     = now;
}

const u32 kTypeMask = 0x1C0, kTypeAir = 0x080;  // MarioStatus.hpp: in the air
bool bellySlide(u32 s) { return s == 0x800456 || s == 0x84045D; }
bool seat(u32 s) { return s == 0x50 || s == 0x840452 || s == 0x840453 || s == 0x4808459; }
bool onBack(u32 s) { return s == 0x20460 || s == 0x20462 || s == 0x20464 || s == 0x208B0 || s == 0x208B3; }

void afterMovement()
{
	const int level = port_sandy_mario();
	TMario* m       = gpMarioOriginal;
	const s32 now   = nowMs();
	if (level <= 0 || !m) {
		s_feet.at = s_front.at = s_back.at = 0;
		return;
	}
	// water washes it off
	if ((m->mStatus & 0x2000) || m->checkFlag(MARIO_FLAG_IN_WATER) || m->checkFlag(MARIO_FLAG_IN_SHALLOW_WATER)) {
		if (s_feet.at || s_front.at || s_back.at)
			fprintf(stderr, "[sandy] the water washed the sand off Mario\n");
		s_feet.at = s_front.at = s_back.at = 0;
		s_was     = m->mStatus;
		return;
	}
	const u32 st  = m->mStatus;
	const u32 was = s_was;
	s_was         = st;
	const bool air = (st & kTypeMask) == kTypeAir;
	const TBGCheckData* floor = gpMarioGroundPlane ? *gpMarioGroundPlane : 0;
	if (air || !floor || !floor->isLegal() || m->mPosition.y - m->mFloorPosition.y > 10.0f)
		return;  // not touching the ground
	if (port_puddle_surface_kind(floor->mBGType, floor->unk6) != PORT_PUDDLE_SAND)
		return;  // not sand
	static bool s_told;
	if (!s_told) {
		s_told = true;
		fprintf(stderr, "[sandy] sand sticks to Mario\n");
	}
	const bool landed = (was & kTypeMask) == kTypeAir;
	sand(s_feet, landed ? (was == 0x8008A9 ? 75.0f : 50.0f) : 28.0f, now);  // a ground pound: higher
	if (bellySlide(st))
		sand(s_front, 140.0f, now);
	else if (seat(st))
		sand(s_back, 70.0f, now);
	else if (onBack(st))
		sand(s_back, 160.0f, now);
}

PortExtension s_extension;

struct Register {
	Register()
	{
		memset(&s_extension, 0, sizeof s_extension);
		s_extension.name          = "sandy mario";
		s_extension.afterMovement = afterMovement;
		port_ext_register(&s_extension);
	}
} s_register;

} // namespace

// Around Mario's own draw (port_wet_mario_draw): the sand on him, in the camera's view.
// u_sand: [0] the plane at his feet (up, in the view); [1] his feet's sand's height, and how much
// of his feet's, front's and back's is left; [2] the way he faces (in the view), his back's height.
void port_sandy_mario_draw(int begin, float (*view)[4])
{
	TMario* m       = gpMarioOriginal;
	const int level = port_sandy_mario();
	if (!begin || level <= 0 || !m || !view) {
		GXPC_SetSand(0);
		return;
	}
	const s32 now = nowMs();
	const f32 k   = kStrength[level > 3 ? 3 : level];
	const f32 feet = left(s_feet, now) * k, front = left(s_front, now) * k, back = left(s_back, now) * k;
	if (feet <= 0.0f && front <= 0.0f && back <= 0.0f) {
		GXPC_SetSand(0);
		return;
	}
	f32 u[12];
	for (int r = 0; r < 3; r++)
		u[r] = view[r][1];  // up
	u[3] = -m->mPosition.y - (u[0] * view[0][3] + u[1] * view[1][3] + u[2] * view[2][3]);
	u[4] = s_feet.height, u[5] = feet, u[6] = front, u[7] = back;
	const f32 a  = (f32)m->mFaceAngle.y * (6.2831853f / 65536.0f);
	const f32 fx = sinf(a), fz = cosf(a);
	for (int r = 0; r < 3; r++)
		u[8 + r] = view[r][0] * fx + view[r][2] * fz;
	u[11] = s_back.height;
	(void)front;
	GXPC_SetSand(u);
}
