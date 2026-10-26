// Fire Lights (the pause menu's Settings, Graphics; off is the original): the fires burning near
// Mario light up what is round them. Each fire the game's particles draw (torches, burning goop,
// Bowser's flames, the hot plates...) is a warm light where it burns, as bright as it is big (how
// many flames it has), flickering, fading to nothing a few hundred units away; the ground and the
// walls round it, and characters coming by, are lit, in the puddles' and the water's reflections
// too (gx_shader.cpp: each light's own view is taken as each camera sets its projection: the main
// camera's, port_shadows_main_view; a mirror's, port_mirror_clip). The fire itself is not lit:
// it is the light.
#include "port_ext.h"
#include "port_shadows.h"

#include <JSystem/JParticle/JPAEmitter.hpp>
#include <JSystem/JParticle/JPAEmitterManager.hpp>
#include <JSystem/JParticle/JPAResourceManager.hpp>
#include <Player/MarioAccess.hpp>
#include <System/EmitterViewObj.hpp>
#include <dolphin/os.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

// The renderer's (platform/gx/src/gx_render.cpp).
extern "C" void GXPC_SetLights(const float* lights, int count);
extern "C" void GXPC_LightsView(const float view[3][4]);

extern "C" int port_fire_lights(void);  // platform/misc/port_puddle_settings.cpp

namespace {

// The game's fires (its particles' numbers, MarioUtil's Particles.hpp)
const s32 kFires[] = {
	0x65,  0x1DC,                // the goop's new fires
	0x12C, 0x1F1,                // the hot plates
	0x135, 0x136, 0x137, 0x1ED,  // burning (goop, things set alight)
	0x1BB, 0x1BC, 0x1F2,         // Bowser Jr.'s fire
	0x1C0, 0x1C1, 0x1C2, 0x1C3, 0x1F3,  // Bowser's
	0x1C4, 0x1C5, 0x1F5,         // the stages' fires (torches, braziers)
	0x1D1, 0x1D2,                // Petey's
};
const int kFireCount = sizeof kFires / sizeof kFires[0];

const int kMostLights = 8;
const f32 kRadius     = 520.0f;   // where a fire's light fades to nothing (a big one, more)
const f32 kMerge      = 160.0f;   // fires this close are one light
const f32 kSeen       = 4000.0f;  // only fires this near Mario

// What a fire's emitters are made from (its data, the particles' resource 0), found once a
// manager is known: an emitter made from one is a fire.
JPAEmitterManager* s_manager;
const void* s_fireInfo[kFireCount];

void findFires(JPAEmitterManager* manager)
{
	s_manager = manager;
	memset(s_fireInfo, 0, sizeof s_fireInfo);
	JPAResourceManager* res = manager ? manager->unkA4[0] : 0;
	JPAEmitterResource* emitters = res ? res->getEmitterResource() : 0;
	int found = 0;
	for (int i = 0; i < kFireCount && emitters; i++) {
		JPAEmitterData* data = emitters->getByUserIndex((u16)kFires[i]);
		if (data && data->getLinkInfo()) {
			s_fireInfo[i] = data->getLinkInfo()[0];
			found++;
		}
	}
	fprintf(stderr, "[lights] %d kinds of fire known\n", found);
}

bool isFire(const JPABaseEmitter* e)
{
	for (int i = 0; i < kFireCount; i++)
		if (s_fireInfo[i] && (const void*)e->mEmitterDataBlockInfo == s_fireInfo[i])
			return true;
	return false;
}

struct Light {
	f32 x, y, z, flames;
};

void afterMovement()
{
	static bool s_wasOn;
	const bool on = port_fire_lights() != 0;
	if (on != s_wasOn)
		fprintf(stderr, "[lights] Fire Lights %s\n", on ? "on" : "off");
	s_wasOn = on;
	TMarioParticleManager* particles = gpMarioParticleManager;
	JPAEmitterManager* manager       = particles ? particles->unk3B8 : 0;
	if (!on || !manager || !gpMarioPos) {
		GXPC_SetLights(0, 0);
		return;
	}
	if (manager != s_manager)
		findFires(manager);

	// every fire burning near Mario, those close together one light
	const f32 mx = gpMarioPos->x, my = gpMarioPos->y, mz = gpMarioPos->z;
	Light found[32];
	int count = 0;
	for (int group = 0; group < 8; group++)
		for (JSULink<JPABaseEmitter>* link = manager->unk44[group].getFirst(); link; link = link->getNext()) {
			JPABaseEmitter* e = link->getObject();
			if (!e || !isFire(e))
				continue;
			const u32 flames = e->getParticleNumber();
			if (!flames)
				continue;
			const JGeometry::TVec3<f32>& p = e->getGlobalTranslation();
			const f32 dx = p.x - mx, dy = p.y - my, dz = p.z - mz;
			if (dx * dx + dy * dy + dz * dz > kSeen * kSeen)
				continue;
			int k = 0;
			while (k < count) {
				const f32 ex = found[k].x - p.x, ey = found[k].y - p.y, ez = found[k].z - p.z;
				if (ex * ex + ey * ey + ez * ez < kMerge * kMerge)
					break;
				k++;
			}
			if (k == count) {
				if (count == 32)
					continue;
				found[count].x = p.x, found[count].y = p.y, found[count].z = p.z, found[count].flames = 0.0f;
				count++;
			}
			found[k].flames += (f32)flames;
		}

	// the nearest 8
	f32 lights[kMostLights * 8];
	int n = 0;
	const f32 t = (f32)OSTicksToMilliseconds(OSGetTime()) * 0.001f;
	while (n < kMostLights && count > 0) {
		int best = 0;
		f32 bestD = 1e30f;
		for (int k = 0; k < count; k++) {
			const f32 dx = found[k].x - mx, dy = found[k].y - my, dz = found[k].z - mz;
			const f32 d = dx * dx + dy * dy + dz * dz;
			if (d < bestD)
				bestD = d, best = k;
		}
		const Light& l = found[best];
		// as bright as it is big; a flicker of its own (from where it is)
		const f32 size    = l.flames < 40.0f ? l.flames / 40.0f : 1.0f;
		const f32 seed    = l.x * 0.013f + l.z * 0.021f;
		const f32 flicker = 0.85f + 0.09f * sinf(t * 13.0f + seed) + 0.06f * sinf(t * 23.7f + seed * 1.7f);
		f32* o = lights + n * 8;
		o[0] = l.x, o[1] = l.y + 40.0f, o[2] = l.z;  // the light from the flames, a little above where they start
		o[3] = kRadius * (0.7f + 0.6f * size);
		o[4] = 1.0f, o[5] = 0.55f, o[6] = 0.22f;  // warm: orange
		o[7] = (0.45f + 0.75f * size) * flicker;
		n++;
		found[best] = found[--count];
	}
	GXPC_SetLights(lights, n);
	static int s_told = -1;
	if ((n > 0) != (s_told > 0)) {
		fprintf(stderr, n ? "[lights] %d fire lights near Mario\n" : "[lights] no fires near Mario\n", n);
		s_told = n;
	}
}

void stageSetup()
{
	s_manager = 0;  // the particles' resources are the new stage's
	GXPC_SetLights(0, 0);
}

PortExtension s_extension;

struct Register {
	Register()
	{
		memset(&s_extension, 0, sizeof s_extension);
		s_extension.name          = "fire lights";
		s_extension.stageSetup    = stageSetup;
		s_extension.afterMovement = afterMovement;
		port_ext_register(&s_extension);
	}
} s_register;

} // namespace

// From the cameras, as each sets its projection: the lights seen from it (port_shadows.h).
void port_lights_view(float (*view)[4]) { GXPC_LightsView(view); }
