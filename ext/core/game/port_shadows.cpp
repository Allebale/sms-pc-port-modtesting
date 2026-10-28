// Real Shadows (port_shadows.h): each frame, as the main camera sets its projection, the sun's
// direction and where its shadow map is centred (Mario, or what the camera looks at) go to the
// renderer, which draws the map from what the camera draws and darkens what it shadows.
//
// The sun: the stage's sun in the sky (TSunModel), else the game's light (gpLightManager), else a
// light from high above; never lower than about 25 degrees over the horizon (at sunset the shadows
// would run the length of the stage). With Keep the Original Shadow Direction on, exactly the way
// the game's own shadows fell: straight down, under what casts them.
#include "port_ext.h"
#include "port_shadows.h"

#include <Camera/Camera.hpp>
#include <Camera/SunMgr.hpp>
#include <Camera/SunModel.hpp>
#include <MarioUtil/LightUtil.hpp>
#include <Map/Map.hpp>
#include <Map/MapData.hpp>
#include <Map/MapModel.hpp>
#include <JSystem/J3D/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3D/J3DGraphBase/J3DShape.hpp>
#include <JSystem/JUtility/JUTNameTab.hpp>
#include <Player/MarioAccess.hpp>
#include <dolphin/os.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

const f32 kHalf       = 2000.0f;  // the map covers this far either side of Mario
const f32 kLowest     = 0.42f;    // the sun's direction's least height (sin of about 25 degrees)
const f32 kStrength[] = {0.0f, 0.35f, 0.55f};

// ------------------------------------------------------------------ the level's own shadows
// Some stages have shadows painted in as part of the level: dark see-through shapes on the ground
// under buildings and trees. While Real Shadows are on they are hidden, so that nothing is shadowed
// twice: the map's materials whose names say they are shadows ("kage", Japanese for shadow, or
// "shadow"). Each stage's map materials are in the log, to find the ones these miss.
const int kMostBaked = 256;
J3DShape* s_baked[kMostBaked];
int s_bakedCount;
bool s_bakedHidden;

bool shadowName(const char* name)
{
	char low[64];
	int n = 0;
	for (; name && name[n] && n < 63; n++)
		low[n] = (char)(name[n] >= 'A' && name[n] <= 'Z' ? name[n] + 32 : name[n]);
	low[n] = 0;
	return strstr(low, "kage") || strstr(low, "shadow") || strstr(low, "shade");
}

void findBaked(J3DModel* model, int index)
{
	J3DModelData* data = model ? model->getModelData() : 0;
	JUTNameTab* names  = data ? data->getMaterialName() : 0;
	if (!data || !names)
		return;
	char line[1024];
	int len = snprintf(line, sizeof line, "[shadows] the map's model %d, %d materials:", index, data->getMaterialNum());
	for (u16 i = 0; i < data->getMaterialNum(); i++) {
		const char* name = names->getName((u16)i);
		const bool baked = name && shadowName(name);
		if (len < (int)sizeof line - 40)
			len += snprintf(line + len, sizeof line - len, " %s%s", name ? name : "?", baked ? "(shadow)" : "");
		J3DMaterial* mat = data->getMaterialNodePointer(i);
		if (baked && mat && mat->getShape() && s_bakedCount < kMostBaked)
			s_baked[s_bakedCount++] = mat->getShape();
	}
	fprintf(stderr, "%s\n", line);
}

void stageSetup()
{
	s_bakedCount  = 0;
	s_bakedHidden = false;
	if (!gpMap || !gpMap->getModelManager())
		return;
	TMapModelManager* models = gpMap->getModelManager();
	for (int i = 0; i < models->getJointModelNum(); i++)
		if (TJointModel* joint = models->getJointModel(i))
			findBaked(joint->getModel(), i);
	fprintf(stderr, "[shadows] %d of the level's own shadow shapes found\n", s_bakedCount);
}

// Hidden while Real Shadows are on, shown again when they are off.
void hideBaked(bool hide)
{
	if (hide == s_bakedHidden)
		return;
	s_bakedHidden = hide;
	for (int i = 0; i < s_bakedCount; i++)
		if (hide)
			s_baked[i]->onFlag(J3DShpFlag_Visible);  // set: not drawn (J3DJoint::entryIn)
		else
			s_baked[i]->offFlag(J3DShpFlag_Visible);
}

PortExtension s_extension;

struct Register {
	Register()
	{
		memset(&s_extension, 0, sizeof s_extension);
		s_extension.name       = "shadows";
		s_extension.stageSetup = stageSetup;
		port_ext_register(&s_extension);
	}
} s_register;

} // namespace

// Moving Clouds: under a sun in the sky, cartoon clouds drifting over the stage, high above it,
// and with Real Shadows their shadows under them (the renderer's: the clouds into the sky as the
// scene ends, gx_render.cpp cloudApply; their shadows, gx_shader.cpp: the same noise on a plane
// high above, seen along the sun's light, slowly moving).
static void clouds(float (*view)[4], bool sky, const f32* sun)
{
	static const f32 kCover[] = {0.0f, 0.3f, 0.45f, 0.6f}, kDark[] = {0.0f, 0.55f, 0.6f, 0.65f};
	const int level = port_cloud_shadows();
	if (level <= 0 || level > 3 || !sky) {
		GXPC_CloudFrame(0, 0);
		return;
	}
	f32 inv[12];
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 3; c++)
			inv[r * 4 + c] = view[c][r];
		inv[r * 4 + 3] = -(view[0][r] * view[0][3] + view[1][r] * view[1][3] + view[2][r] * view[2][3]);
	}
	const s32 ms = (s32)OSTicksToMilliseconds(OSGetTime());
	const f32 params[8] = {(f32)(((ms % 6000000) + 6000000) % 6000000) * 0.001f, kCover[level], kDark[level], 0.0f,
	                       sun[0], sun[1], sun[2], 0.0f};
	static int s_told = -1;
	if (level != s_told)
		fprintf(stderr, "[shadows] moving clouds: %s\n", level == 1 ? "few" : level == 2 ? "some" : "many");
	s_told = level;
	GXPC_CloudFrame(inv, params);
}

void port_shadows_main_view(void* camera, float (*view)[4])
{
	if (camera != (void*)gpCamera || !view)
		return;
	port_lights_view(view);  // Fire Lights, seen from the main camera
	{  // Moving Clouds: under the stage's sun in the sky (toward it from Mario), whatever the shadows
		const bool sunUp = gpSunModel && gpSunMgr && gpSunMgr->unk14;
		f32 to[3] = {0.0f, 1.0f, 0.0f};
		if (sunUp) {
			const JGeometry::TVec3<f32>& p = gpSunModel->mPosition;
			const f32 fx = gpMarioPos ? gpMarioPos->x : 0.0f, fy = gpMarioPos ? gpMarioPos->y : 0.0f,
			          fz = gpMarioPos ? gpMarioPos->z : 0.0f;
			to[0] = p.x - fx, to[1] = p.y - fy, to[2] = p.z - fz;
			const f32 l = sqrtf(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
			if (l > 1e-3f)
				to[0] /= l, to[1] /= l, to[2] /= l;
			if (to[1] < kLowest) {  // as the shadows' sun: never lower than about 25 degrees
				const f32 h = sqrtf(to[0] * to[0] + to[2] * to[2]);
				const f32 k = h > 1e-4f ? sqrtf(1.0f - kLowest * kLowest) / h : 0.0f;
				to[0] *= k, to[2] *= k, to[1] = h > 1e-4f ? kLowest : 1.0f;
			}
		}
		clouds(view, sunUp, to);
	}
	const int level = port_shadows();
	hideBaked(level > 0);
	if (level <= 0 || level > 2) {
		static bool s_toldOff;
		if (!s_toldOff)
			fprintf(stderr, "[shadows] off\n");
		s_toldOff = true;
		GXPC_ShadowFrame(view, 0, 0, 0.0f, 0.0f, 0.0f);
		return;
	}
	f32 focus[3];
	if (gpMarioPos) {
		focus[0] = gpMarioPos->x, focus[1] = gpMarioPos->y, focus[2] = gpMarioPos->z;
	} else {
		focus[0] = gpCamera->unk148.x, focus[1] = gpCamera->unk148.y, focus[2] = gpCamera->unk148.z;
	}
	f32 sun[3] = {0.35f, 1.0f, 0.25f};  // high above, a little to the side
	int source = 3;  // 0 the game's own shadows' way, 1 the stage's sun, 2 the game's light, 3 high above
	const bool original = port_shadows_original_direction() != 0;
	const Vec* light    = gpLightManager ? gpLightManager->getLightPos() : 0;
	const bool lit = light && light->y > 0.0f && light->x * light->x + light->y * light->y + light->z * light->z > 1e-6f;
	const bool sky = gpSunModel && gpSunMgr && gpSunMgr->unk14;
	if (original) {
		// exactly where the game's own fall: TMBindShadowManager puts each round, tree's and
		// umbrella's shadow on the ground straight below what casts it (TMBindShadowManager::calcVtx:
		// the request's position, its height the ground's), so the light is straight overhead
		sun[0] = 0.0f, sun[1] = 1.0f, sun[2] = 0.0f;
		source = 0;
	} else if (sky) {
		const JGeometry::TVec3<f32>& p = gpSunModel->mPosition;
		sun[0] = p.x - focus[0], sun[1] = p.y - focus[1], sun[2] = p.z - focus[2];
		source = 1;
	} else if (lit) {
		sun[0] = light->x, sun[1] = light->y, sun[2] = light->z;
		source = 2;
	}
	f32 l = sqrtf(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
	if (l < 1e-3f) {
		sun[0] = 0.0f, sun[1] = 1.0f, sun[2] = 0.0f, l = 1.0f;
	}
	sun[0] /= l, sun[1] /= l, sun[2] /= l;
	if (source != 0 && sun[1] < kLowest) {  // raised to the least height, keeping its way round
		const f32 h = sqrtf(sun[0] * sun[0] + sun[2] * sun[2]);
		const f32 k = h > 1e-4f ? sqrtf(1.0f - kLowest * kLowest) / h : 0.0f;
		sun[0] *= k, sun[2] *= k, sun[1] = kLowest;
		if (h <= 1e-4f)
			sun[1] = 1.0f;
	}
	const f32 reach = port_shadows_distance();
	// the log: when they come on, or the sun's source or the distance changes
	static int s_toldLevel = -1, s_toldSource = -1;
	static f32 s_toldFar;
	if (level != s_toldLevel || source != s_toldSource || reach != s_toldFar) {
		static const char* const kSource[] = {"the game's own shadows' direction", "the stage's sun",
		                                      "the game's light (no sun in the sky)", "high above (no light)"};
		fprintf(stderr, "[shadows] %s, from %s (toward it: %.2f %.2f %.2f), reaching %.0f\n",
		        level == 1 ? "soft" : "strong", kSource[source], sun[0], sun[1], sun[2], reach);
		s_toldLevel  = level;
		s_toldSource = source;
		s_toldFar    = reach;
	}
	GXPC_ShadowFrame(view, sun, focus, kStrength[level], kHalf, reach);
}
