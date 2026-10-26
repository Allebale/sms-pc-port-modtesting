// Real Shadows (port_shadows.h): each frame, as the main camera sets its projection, the sun's
// direction and where its shadow map is centred (Mario, or what the camera looks at) go to the
// renderer, which draws the map from what the camera draws and darkens what it shadows.
//
// The sun: the stage's (TSunModel, where Sun Glints take it from) while it shows; else, as indoors,
// a light from high above. Never lower than about 25 degrees over the horizon: at sunset the
// shadows would run the length of the stage.
#include "port_shadows.h"

#include <Camera/Camera.hpp>
#include <Camera/SunMgr.hpp>
#include <Camera/SunModel.hpp>
#include <Player/MarioAccess.hpp>

#include <math.h>
#include <stdio.h>

namespace {

const f32 kHalf       = 2000.0f;  // the map covers this far either side of Mario
const f32 kLowest     = 0.42f;    // the sun's direction's least height (sin of about 25 degrees)
const f32 kStrength[] = {0.0f, 0.35f, 0.55f};

} // namespace

void port_shadows_main_view(void* camera, float (*view)[4])
{
	if (camera != (void*)gpCamera || !view)
		return;
	const int level = port_shadows();
	if (level <= 0 || level > 2) {
		static bool s_toldOff;
		if (!s_toldOff)
			fprintf(stderr, "[shadows] off\n");
		s_toldOff = true;
		GXPC_ShadowFrame(view, 0, 0, 0.0f, 0.0f);
		return;
	}
	f32 focus[3];
	if (gpMarioPos) {
		focus[0] = gpMarioPos->x, focus[1] = gpMarioPos->y, focus[2] = gpMarioPos->z;
	} else {
		focus[0] = gpCamera->unk148.x, focus[1] = gpCamera->unk148.y, focus[2] = gpCamera->unk148.z;
	}
	f32 sun[3] = {0.35f, 1.0f, 0.25f};  // high above, a little to the side
	if (gpSunModel && gpSunMgr && gpSunMgr->unk14) {
		const JGeometry::TVec3<f32>& s = gpSunModel->mPosition;
		sun[0] = s.x - focus[0], sun[1] = s.y - focus[1], sun[2] = s.z - focus[2];
	}
	f32 l = sqrtf(sun[0] * sun[0] + sun[1] * sun[1] + sun[2] * sun[2]);
	if (l < 1e-3f) {
		sun[0] = 0.0f, sun[1] = 1.0f, sun[2] = 0.0f, l = 1.0f;
	}
	sun[0] /= l, sun[1] /= l, sun[2] /= l;
	if (sun[1] < kLowest) {  // raised to the least height, keeping its way round
		const f32 h = sqrtf(sun[0] * sun[0] + sun[2] * sun[2]);
		const f32 k = h > 1e-4f ? sqrtf(1.0f - kLowest * kLowest) / h : 0.0f;
		sun[0] *= k, sun[2] *= k, sun[1] = kLowest;
		if (h <= 1e-4f)
			sun[1] = 1.0f;
	}
	// the log: when they come on, or the sun's source changes (the stage's, or high above)
	static int s_toldLevel = -1;
	static bool s_toldStageSun;
	const bool stageSun = gpSunModel && gpSunMgr && gpSunMgr->unk14;
	if (level != s_toldLevel || stageSun != s_toldStageSun) {
		fprintf(stderr, "[shadows] %s, the sun %s (toward it: %.2f %.2f %.2f)\n", level == 1 ? "soft" : "strong",
		        stageSun ? "the stage's" : "high above (none in this stage)", sun[0], sun[1], sun[2]);
		s_toldLevel    = level;
		s_toldStageSun = stageSun;
	}
	GXPC_ShadowFrame(view, sun, focus, kStrength[level], kHalf);
}
