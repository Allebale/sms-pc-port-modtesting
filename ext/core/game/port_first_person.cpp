// The first-person camera, built in (port_first_person.h): the settings menu's First Person page
// and the FIRST_PERSON key switch it. It does what the First Person mod does through the mod API,
// with the same hooks (port_ext.h):
//  - camera: what is drawn each tick is seen from Mario's eyes, looking the view's own way. The
//    game's camera still follows him behind, but the C-stick no longer reaches it (padRead), so
//    walls it bumps into cannot stop the view turning; the C-stick turns and tilts the view.
//  - padRead: the control stick is turned by the difference between the view and the game's
//    camera, as the game moves Mario relative to its camera, so up runs the way the view looks;
//    R all the way down loses its click (the stance), keeping its strength, so he keeps moving.
//  - playerAim: while R is held, his body and FLUDD aim where the view looks, up and down too.
//  - hidePlayerModel / hiddenPlayerParts: all of him, or his head and FLUDD, out of the view.
// During cutscenes and talks the game's own camera is left as it is.
#include "port_ext.h"
#include "port_first_person.h"
#include <Camera/Camera.hpp>
#include <Player/Mario.hpp>
#include <Player/MarioStatus.hpp>
#include <Player/WaterGun.hpp>
#include <System/MarDirector.hpp>
#include <dolphin/pad.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern "C" int port_active_frame_rate;
extern "C" void port_savestate_notify(const char* text);

namespace {

const float kPi = 3.14159265f;

bool s_stageReady;
bool s_wasOn;          // first person, as last seen (a change says so on screen)
bool s_viewIsOurs;     // the camera was put at his eyes this tick
float s_yaw, s_pitch;  // the view: degrees, 0 is +Z, 90 is +X; up positive
float s_gameYaw;       // the game camera's, across the ground
bool s_haveYaw, s_haveGameYaw;
float s_eyeY;
bool s_haveEye;
f32 s_fovyGiven, s_fovyOwn; // the field of view given to the camera, and its own (as the mod API keeps them)

// controller 1 as read, before this changed it
PADStatus s_raw;
u16 s_prevButtons;
bool s_haveRaw;

float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

TMario* mario() { return s_stageReady ? gpMarioOriginal : 0; }

bool cutscene()
{
	TMarDirector* d = gpMarDirector;
	return d
	       && (d->isTalkOrDemoModeNow() || d->checkFlag(TMarDirector::DIRECTOR_FLAG_DEMO_PENDING)
	           || (gpCamera && gpCamera->isDemoCamera()));
}

// The view is Mario's now: switched on, Mario there, no cutscene or talk.
bool active() { return port_fp_on() && mario() && !cutscene(); }

bool rHeld() { return s_haveRaw && ((s_raw.button & PAD_TRIGGER_R) || s_raw.triggerRight >= 26); }

// FLUDD's spray nozzle out, or Yoshi: R sprays (other nozzles use R their own way)
bool spraysWithR()
{
	TMario* m = mario();
	if (!m)
		return false;
	if (m->onYoshi())
		return true;
	return m->checkFlag(MARIO_FLAG_HAS_FLUDD) && m->mWaterGun && m->mWaterGun->mCurrentNozzle == 0;
}

// While R is held his body and FLUDD aim the view's way.
bool aiming() { return s_viewIsOurs && s_haveYaw && port_fp_aim_fludd() && rHeld(); }

s16 angle(float degrees) { return (s16)(int)(degrees * (65536.0f / 360.0f)); }

// ------------------------------------------------------------------ hooks

void stageSetup()
{
	s_stageReady = true;
	s_haveYaw    = false;
	s_haveEye    = false;
}

void moveStage() { s_stageReady = false; }

void padRead(void* status)
{
	PADStatus* pads = (PADStatus*)status;
	if (!pads)
		return;
	s_prevButtons = s_haveRaw ? s_raw.button : 0;
	s_raw         = pads[0];
	s_haveRaw     = pads[0].err == PAD_ERR_NONE;
	// the controller's toggle button, on its press, while a stage is played
	const unsigned button = port_fp_button();
	if (button && s_haveRaw && mario() && (s_raw.button & button) && !(s_prevButtons & button))
		port_fp_toggle();
	if (!s_viewIsOurs || !s_haveYaw || !s_haveGameYaw || !port_fp_on())
		return;
	PADStatus& s = pads[0];
	s.substickX = s.substickY = 0;
	// The stick's angle, clockwise from up, gives the world yaw (the game camera's) minus it:
	// turn it clockwise by the game camera's yaw minus the view's.
	const float d  = (s_gameYaw - s_yaw) * kPi / 180.0f;
	const float cd = cosf(d), sd = sinf(d);
	const float a = s.stickX, b = s.stickY;
	s.stickX      = (s8)clampf(floorf(a * cd + b * sd + 0.5f), -127.0f, 127.0f);
	s.stickY      = (s8)clampf(floorf(-a * sd + b * cd + 0.5f), -127.0f, 127.0f);
	// R all the way down plants him to aim (the stance), from R's click; its analog part is the
	// strength. The view aims, so keep him moving, spraying at full strength.
	if ((s.button & PAD_TRIGGER_R) && port_fp_run_spray() && spraysWithR()) {
		s.button &= (u16)~PAD_TRIGGER_R;
		s.triggerRight = 255;
	}
}

void camera(void* cam, float* eye, float* at, float* fovy)
{
	if (cam != (void*)gpCamera)
		return;
	CPolarSubCamera* c = (CPolarSubCamera*)cam;
	// the field of view given last tick goes back to the camera's own, unless it set one itself
	if (s_fovyGiven != 0.0f) {
		if (c->mFovy == s_fovyGiven)
			c->mFovy = s_fovyOwn;
		s_fovyGiven = 0.0f;
		*fovy       = c->mFovy;
	}
	const bool on = port_fp_on() != 0;
	if (on != s_wasOn) {
		s_wasOn   = on;
		s_haveYaw = false; // from the game camera again when it starts
		port_savestate_notify(on ? "First person" : "Third person");
	}
	s_viewIsOurs = false;
	if (!active()) {
		s_haveEye = false;
		return;
	}
	s_viewIsOurs = true;
	TMario* m    = mario();

	// which way the game camera looks, across the ground
	const float gx = at[0] - eye[0], gz = at[2] - eye[2];
	if (gx * gx + gz * gz > 0.0001f) {
		s_gameYaw     = atan2f(gx, gz) * 180.0f / kPi;
		s_haveGameYaw = true;
	}
	if (!s_haveYaw) { // just switched on: look the way the game camera did
		s_yaw     = s_haveGameYaw ? s_gameYaw : m->mFaceAngle.y * (360.0f / 65536.0f);
		s_pitch   = 0.0f;
		s_haveYaw = true;
	}

	// the C-stick turns and tilts the view; L looks the way he faces
	const float dt = 1.0f / (float)(port_active_frame_rate > 0 ? port_active_frame_rate : 30);
	if (s_haveRaw) {
		float x = s_raw.substickX / 127.0f, y = s_raw.substickY / 127.0f;
		if (port_fp_invert_x())
			x = -x;
		if (port_fp_invert_y())
			y = -y;
		if (fabsf(x) > 0.2f) // right turns right: the yaw goes down
			s_yaw -= x * (float)port_fp_turn_speed() * dt;
		if (fabsf(y) > 0.2f)
			s_pitch = clampf(s_pitch + y * (float)port_fp_tilt_speed() * dt, -75.0f, 75.0f);
		if ((s_raw.button & PAD_TRIGGER_L) && !(s_prevButtons & PAD_TRIGGER_L)) {
			s_yaw   = m->mFaceAngle.y * (360.0f / 65536.0f);
			s_pitch = 0.0f;
		}
	}
	s_yaw = fmodf(s_yaw, 360.0f);

	// Standing still with his body showing, he turns to face the view, so looking down shows him
	// the right way round; spraying, he faces it too (his body aims it, playerAim).
	const bool still = s_haveRaw && abs(s_raw.stickX) < 26 && abs(s_raw.stickY) < 26;
	if (still && fabsf(m->mForwardVel) < 1.0f && !m->checkStatusType(MARIO_STATUS_FLAG_JUMPING)
	    && !m->checkStatusType(MARIO_STATUS_FLAG_SWIMMING)
	    && (aiming() || port_fp_model() != PORT_FP_MODEL_HIDDEN)) {
		m->mFaceAngle.y    = angle(s_yaw);
		m->mModelFaceAngle = m->mFaceAngle.y;
	}

	const float dx = sinf(s_yaw * kPi / 180.0f), dz = cosf(s_yaw * kPi / 180.0f);
	// the eyes: above his feet (higher on Yoshi), a little smoothed up and down
	const float y = m->mPosition.y + (float)port_fp_eye_height() + (m->onYoshi() ? 80.0f : 0.0f);
	s_eyeY        = s_haveEye ? s_eyeY + (y - s_eyeY) * 0.5f : y;
	s_haveEye     = true;

	const float tilt = s_pitch * kPi / 180.0f;
	const float cp = cosf(tilt), sp = sinf(tilt);
	eye[0] = m->mPosition.x + dx * 10.0f;
	eye[1] = s_eyeY;
	eye[2] = m->mPosition.z + dz * 10.0f;
	at[0]  = eye[0] + dx * cp * 1000.0f;
	at[1]  = eye[1] + sp * 1000.0f;
	at[2]  = eye[2] + dz * cp * 1000.0f;
	// its field of view, for the camera's culling and screen effects too
	const f32 fov = (f32)port_fp_fov();
	if (fov != c->mFovy) {
		s_fovyOwn   = c->mFovy;
		s_fovyGiven = fov;
		c->mFovy    = fov;
	}
	*fovy = fov;
}

int playerAim(void* m, short* yaw, short* tilt)
{
	if (m != (void*)mario() || !aiming())
		return 0;
	*yaw  = angle(s_yaw);
	*tilt = angle(-clampf(s_pitch, -89.0f, 89.0f)); // the gun angle: negative up
	return 1;
}

int hidePlayerModel(void* m)
{
	return m == (void*)mario() && s_viewIsOurs && port_fp_model() == PORT_FP_MODEL_HIDDEN ? 1 : 0;
}

unsigned hiddenPlayerParts(void* m)
{
	return m == (void*)mario() && s_viewIsOurs && port_fp_model() == PORT_FP_MODEL_BODY
	           ? PORT_PLAYER_PART_HEAD | PORT_PLAYER_PART_FLUDD
	           : 0;
}

// Filled by name: a hook added to PortExtension anywhere never shifts it.
PortExtension s_extension;

struct Register {
	Register()
	{
		memset(&s_extension, 0, sizeof s_extension);
		s_extension.name              = "first-person";
		s_extension.stageSetup        = stageSetup;
		s_extension.moveStage         = moveStage;
		s_extension.padRead           = padRead;
		s_extension.camera            = camera;
		s_extension.playerAim         = playerAim;
		s_extension.hidePlayerModel   = hidePlayerModel;
		s_extension.hiddenPlayerParts = hiddenPlayerParts;
		port_ext_register(&s_extension);
	}
} s_register;

} // namespace
