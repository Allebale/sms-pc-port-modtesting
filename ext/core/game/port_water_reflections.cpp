// Water Reflections and Water Ripples (port_puddles.h, the settings menu's Graphics page): the reflections FLUDD's
// puddles show (Mario, and with Puddle Reflections on, characters and objects) in the stage's own
// water too: the sea, lakes, pools, fountains and streams, and wet ground. Game code, with the
// game's flags and headers.
//
// How the game reflects in puddles (TMirrorModelManager, TModelWaterManager::drawMirror): each
// frame the mirror camera draws the mirror scene (the copies of what is reflected) mirrored about
// one plane, the floor under Mario, into a texture. drawMirror then marks the puddles in the frame's
// alpha, fades that out over a disc around Mario, and lays the texture over what is marked.
//
// Here, as the frame starts (port_mirror_plane, from TMirrorModelManager::perform), the plane is
// chosen: the water surface nearest Mario, when one is within the Reflection Distance and nearer
// than any puddle or wet ground; else the floor under him, as in the game. After drawMirror
// (port_draw_water_mirror, from TModelWaterManager::perform) the same is done for the water or wet
// ground on that plane: its triangles, from the stage's collision, are marked as strongly as the
// Reflections page says, the disc fades them out, and the texture is laid over them. Off, nothing of it
// runs and the game is as it was.
#include "port_ext.h"
#include "port_puddles.h"

#include <Map/Map.hpp>
#include <Map/MapCollisionData.hpp>
#include <Map/MapData.hpp>
#include <Map/MapMirror.hpp>
#include <Camera/Camera.hpp>
#include <Camera/SunMgr.hpp>
#include <Camera/SunModel.hpp>
#include <JSystem/JMath.hpp>
#include <Player/MarioAccess.hpp>
#include <Player/ModelWaterManager.hpp>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <dolphin/os.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

// A water surface is flat: its normal's y at least this.
const f32 kFlat = 0.95f;
// How far above Mario a water surface may be and still be the one he is at (swimming, he is in it).
const f32 kAbove = 200.0f;
// How far below him it may be (looking down from a pier or a cliff).
const f32 kBelowMario = 3000.0f;
// How far apart in height two triangles may be and lie on one plane.
const f32 kSamePlane = 20.0f;
// The marks and the disc are raised this much over the plane, as the game raises its disc.
const f32 kLift = 4.0f;
// To keep from flickering between two planes as near: the other must be nearer by this much.
const f32 kStick = 100.0f;
// At most this many triangles are marked.
const int kMostTriangles = 512;

enum Kind { KIND_NONE, KIND_GROUND, KIND_WATER };

// This frame's plane, and what is on it.
const TBGCheckData* s_plane;
int s_kind;
const TBGCheckData* s_tris[kMostTriangles];
int s_triCount;
f32 s_depthRadius;  // Water Depth reaches further than the reflection: at least this
// Water Depth fades in as the reflection moves to water and out as it leaves, over kDepthFadeMs,
// drawn on the last water's plane (s_depthPlane) while it fades out; its own triangles.
const s32 kDepthFadeMs = 900;
const TBGCheckData* s_depthPlane;
const TBGCheckData* s_depthTris[kMostTriangles];
int s_depthTriCount;
bool s_depthWanted;
f32 s_depthFade;
s32 s_depthFadeAt;

bool on() { return port_water_reflections() && gpMapCollisionData && gpMarioPos; }

bool isWater(const TBGCheckData* d) { return d->isWaterSurface() && d->getNormal().y >= kFlat; }

bool isWetGround(const TBGCheckData* d)
{
	return (d->mBGType & 0x3FFF) == BG_TYPE_WET_GROUND && d->getNormal().y >= kFlat;
}

// The plane's height at x, z.
f32 heightAt(const TBGCheckData* d, f32 x, f32 z)
{
	const JGeometry::TVec3<f32>& n = d->getNormal();
	return -(d->getPlaneDistance() + n.x * x + n.z * z) / n.y;
}

bool samePlane(const TBGCheckData* a, const TBGCheckData* b)
{
	const JGeometry::TVec3<f32>& na = a->getNormal();
	const JGeometry::TVec3<f32>& nb = b->getNormal();
	if (na.x * nb.x + na.y * nb.y + na.z * nb.z < 0.999f)
		return false;
	const f32 x = a->mPoint1.x, z = a->mPoint1.z;
	return fabsf(heightAt(a, x, z) - heightAt(b, x, z)) <= kSamePlane;
}

f32 segmentDistance2(f32 px, f32 pz, f32 ax, f32 az, f32 bx, f32 bz)
{
	const f32 dx = bx - ax, dz = bz - az;
	const f32 len = dx * dx + dz * dz;
	f32 t         = len > 0.0f ? ((px - ax) * dx + (pz - az) * dz) / len : 0.0f;
	t             = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
	const f32 ex = ax + t * dx - px, ez = az + t * dz - pz;
	return ex * ex + ez * ez;
}

// How far, across the ground, a triangle is from x, z: 0 when over it.
f32 distanceTo(const TBGCheckData* d, f32 x, f32 z)
{
	const JGeometry::TVec3<f32>& a = d->mPoint1;
	const JGeometry::TVec3<f32>& b = d->mPoint2;
	const JGeometry::TVec3<f32>& c = d->mPoint3;
	const f32 s1 = (b.x - a.x) * (z - a.z) - (b.z - a.z) * (x - a.x);
	const f32 s2 = (c.x - b.x) * (z - b.z) - (c.z - b.z) * (x - b.x);
	const f32 s3 = (a.x - c.x) * (z - c.z) - (a.z - c.z) * (x - c.x);
	if ((s1 >= 0 && s2 >= 0 && s3 >= 0) || (s1 <= 0 && s2 <= 0 && s3 <= 0))
		return 0.0f;
	f32 m       = segmentDistance2(x, z, a.x, a.z, b.x, b.z);
	const f32 n = segmentDistance2(x, z, b.x, b.z, c.x, c.z);
	const f32 o = segmentDistance2(x, z, c.x, c.z, a.x, a.z);
	m           = n < m ? n : m;
	m           = o < m ? o : m;
	return sqrtf(m);
}

// The point of a triangle nearest x, z across the ground (x, z itself when over it).
void closestPoint(const TBGCheckData* d, f32 x, f32 z, f32* outX, f32* outZ)
{
	if (distanceTo(d, x, z) == 0.0f) {
		*outX = x;
		*outZ = z;
		return;
	}
	const JGeometry::TVec3<f32>* p[3] = {&d->mPoint1, &d->mPoint2, &d->mPoint3};
	f32 best = -1.0f;
	for (int i = 0; i < 3; i++) {
		const JGeometry::TVec3<f32>& a = *p[i];
		const JGeometry::TVec3<f32>& b = *p[(i + 1) % 3];
		const f32 dx = b.x - a.x, dz = b.z - a.z;
		const f32 len = dx * dx + dz * dz;
		f32 t         = len > 0.0f ? ((x - a.x) * dx + (z - a.z) * dz) / len : 0.0f;
		t             = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
		const f32 cx = a.x + t * dx, cz = a.z + t * dz;
		const f32 e  = (cx - x) * (cx - x) + (cz - z) * (cz - z);
		if (best < 0.0f || e < best) {
			best  = e;
			*outX = cx;
			*outZ = cz;
		}
	}
}

// Whether the water at x, z is open to the sky as far up as top: no land over it. A stage's sea
// can run on under the land (the plaza's under its streets), where nothing of it is seen.
bool openWater(const TBGCheckData* d, f32 x, f32 z, f32 top)
{
	if (!gpMap)
		return true;
	const f32 h = heightAt(d, x, z);
	const TBGCheckData* floor;
	const f32 g = gpMap->checkGroundIgnoreWaterSurface(x, (top > h ? top : h) + 50.0f, z, &floor);
	return g <= h + 10.0f;
}

// Each ground triangle of the stage's collision within radius of x, z, once: the static map's
// and the moving one's, from the 1024-unit grid the game keeps them in.
template <class F>
void eachGroundNear(f32 x, f32 z, f32 radius, F& f)
{
	const TMapCollisionData* map = gpMapCollisionData;
	const int across             = map->unk8;
	const int down               = map->unkC;
	if (across <= 0 || down <= 0)
		return;
	int x0 = (int)((x - radius + map->mGridExtentX) * (1.0f / 1024));
	int x1 = (int)((x + radius + map->mGridExtentX) * (1.0f / 1024));
	int z0 = (int)((z - radius + map->mGridExtentY) * (1.0f / 1024));
	int z1 = (int)((z + radius + map->mGridExtentY) * (1.0f / 1024));
	x0     = x0 < 0 ? 0 : x0;
	z0     = z0 < 0 ? 0 : z0;
	x1     = x1 >= across ? across - 1 : x1;
	z1     = z1 >= down ? down - 1 : z1;
	for (int gz = z0; gz <= z1; gz++)
		for (int gx = x0; gx <= x1; gx++)
			for (int g = 0; g < 2; g++) {
				const TBGCheckListRoot& root = g ? map->getGridRoot18(gx, gz) : map->getGridRoot14(gx, gz);
				for (const TBGCheckList* l = root.getGroundList(); l; l = l->getNext())
					if (l->unk8)
						f(l->unk8);
			}
}

// The nearest water surface Mario is at, or over.
struct FindWater {
	f32 x, y, z;
	const TBGCheckData* best;
	f32 bestDistance;
	void operator()(const TBGCheckData* d)
	{
		if (!isWater(d))
			return;
		const f32 h = heightAt(d, x, z);
		if (h > y + kAbove || h < y - kBelowMario)
			return;
		// as near as the nearest of its points with no land over the water: the one nearest
		// Mario, its corners and its middle (a pier over the sea is land over it where he stands)
		f32 px[5], pz[5];
		closestPoint(d, x, z, &px[0], &pz[0]);
		px[1] = d->mPoint1.x, pz[1] = d->mPoint1.z;
		px[2] = d->mPoint2.x, pz[2] = d->mPoint2.z;
		px[3] = d->mPoint3.x, pz[3] = d->mPoint3.z;
		px[4] = (px[1] + px[2] + px[3]) / 3.0f, pz[4] = (pz[1] + pz[2] + pz[3]) / 3.0f;
		f32 distance = -1.0f;
		for (int i = 0; i < 5; i++) {
			const f32 dx = px[i] - x, dz = pz[i] - z;
			const f32 e  = sqrtf(dx * dx + dz * dz);
			if ((distance < 0.0f || e < distance) && openWater(d, px[i], pz[i], y + kAbove))
				distance = e;
		}
		if (distance < 0.0f)
			return;  // all under land
		if (!best || distance < bestDistance) {
			best         = d;
			bestDistance = distance;
		}
	}
};

// The nearest wet ground on the floor under Mario.
struct FindWetGround {
	f32 x, z;
	const TBGCheckData* ground;
	f32 bestDistance;
	bool found;
	void operator()(const TBGCheckData* d)
	{
		if (!isWetGround(d) || !samePlane(d, ground))
			return;
		const f32 distance = distanceTo(d, x, z);
		if (!found || distance < bestDistance) {
			found        = true;
			bestDistance = distance;
		}
	}
};

// The nearest of FLUDD's puddles on the floor, as far across the ground from Mario.
bool nearestPuddle(f32 radius, f32* out)
{
	TModelWaterManager* water = gpModelWaterManager;
	if (!water)
		return false;
	bool found = false;
	f32 best   = radius;
	for (int i = 0; i < water->getParticleCount(); i++) {
		if ((water->mParticleFlagSOA[i] & 0xf) != 2)
			continue;
		const f32 dx = water->mParticlePositionSOA[i].x - gpMarioPos->x;
		const f32 dz = water->mParticlePositionSOA[i].z - gpMarioPos->z;
		const f32 d  = sqrtf(dx * dx + dz * dz);
		if (d < best) {
			best  = d;
			found = true;
		}
	}
	*out = best;
	return found;
}

// What to mark on the plane: the water or the wet ground on it within the disc.
struct Collect {
	f32 x, z, radius;
	const TBGCheckData* plane;
	bool water;
	void operator()(const TBGCheckData* d)
	{
		if (s_triCount >= kMostTriangles)
			return;
		if (water ? !isWater(d) : !isWetGround(d))
			return;
		if (!samePlane(d, plane) || distanceTo(d, x, z) >= radius)
			return;
		for (int i = 0; i < s_triCount; i++)  // in both grids, or in more than one cell
			if (s_tris[i] == d)
				return;
		s_tris[s_triCount++] = d;
	}
};

// The water on the depth's plane, out to its radius.
struct CollectDepth {
	f32 x, z, radius;
	const TBGCheckData* plane;
	void operator()(const TBGCheckData* d)
	{
		if (s_depthTriCount >= kMostTriangles || !isWater(d) || !samePlane(d, plane)
		    || distanceTo(d, x, z) >= radius)
			return;
		for (int i = 0; i < s_depthTriCount; i++)
			if (s_depthTris[i] == d)
				return;
		s_depthTris[s_depthTriCount++] = d;
	}
};

bool cameraAbove(const TBGCheckData* plane)
{
	if (!gpCamera)
		return true;
	const JGeometry::TVec3<f32>& c = gpCamera->mPosition;
	return c.y > heightAt(plane, c.x, c.z);
}

// The disc's rim: as drawMirror's, on the plane.
void discPoint(int i, f32 radius, JGeometry::TVec3<f32>* out)
{
	out->x = gpMarioPos->x + JMASSin(i * 0x2000) * radius;
	out->z = gpMarioPos->z + JMASCos(i * 0x2000) * radius;
	out->y = heightAt(s_plane, out->x, out->z) + kLift;
}

void drawDisc(f32 radius, u8 centre)
{
	JGeometry::TVec3<f32> rim[8];
	for (int i = 0; i < 8; i++)
		discPoint(i, radius, &rim[i]);
	GXBegin(GX_TRIANGLEFAN, GX_VTXFMT0, 10);
	GXPosition3f32(gpMarioPos->x, heightAt(s_plane, gpMarioPos->x, gpMarioPos->z) + kLift, gpMarioPos->z);
	GXColor4u8(0xff, 0xff, 0xff, centre);
	for (int i = 0; i <= 8; i++) {
		const JGeometry::TVec3<f32>& p = rim[i & 7];
		GXPosition3f32(p.x, p.y, p.z);
		GXColor4u8(0, 0, 0, 0);
	}
	GXEnd();
}

// Water Ripples and Puddle Ripples: each point of the reflection shows the mirror's picture from a
// little beside it (drawn below, with the rest of what the port does to the reflection).
//
// Water (Water Ripples): two crossing waves that roll on with the game's clock (still while it is
// paused). Puddles and wet ground (Puddle Ripples): still, until something disturbs them: where a
// puddle forms (each drop of FLUDD's water that lands) and where Mario's feet fall as he runs
// through one, the water around rings out at full strength and calms, evenly, to still in two
// seconds (by the clock, not the game's ticks, whose rate the frame rate may change).
// Puddle disturbances: where, and when (milliseconds).
struct RingSource {
	f32 x, z;
	s32 start;
};
const int kMostRings   = 24;
const s32 kRingLife    = 2000;   // ms: full strength to still
const f32 kRingSpread  = 160.0f; // how far around it the water is stirred
const f32 kRingLength  = 50.0f;  // its waves' length
const f32 kRingSpeed   = 120.0f; // how fast they run out, units a second
RingSource s_rings[kMostRings];
int s_ringCount;
int s_ringNext;  // the next to make room for, when all are in use

int now() { return gpModelWaterManager ? gpModelWaterManager->unk5E00 : 0; }

s32 nowMs() { return (s32)OSTicksToMilliseconds(OSGetTime()); }

void addRing(f32 x, f32 z)
{
	RingSource& r = s_ringCount < kMostRings ? s_rings[s_ringCount++] : s_rings[s_ringNext++ % kMostRings];
	r.x     = x;
	r.z     = z;
	r.start = nowMs();
}

bool ringsLive()
{
	// a ring from the future is one a save state brought back from another run's clock: done
	const s32 t = nowMs();
	for (int i = 0; i < s_ringCount; i++)
		if (t - s_rings[i].start >= 0 && t - s_rings[i].start < kRingLife)
			return true;
	return false;
}

// Each tick (from port_mirror_plane): new rings where drops have landed in puddles and where
// Mario's feet are as he runs through them.
void updateRings(const TBGCheckData* ground)
{
	static u8 s_wasPuddle[256];
	static f32 s_lastX, s_lastZ;
	static s32 s_lastStep;
	TModelWaterManager* water = gpModelWaterManager;
	if (!port_puddle_ripples() || !water || !gpMarioPos) {
		s_ringCount = 0;
		return;
	}
	const f32 mx = gpMarioPos->x, mz = gpMarioPos->z;
	const int count = water->getParticleCount();
	int made        = 0;
	bool puddleAtFeet = false;
	for (int i = 0; i < 256; i++) {
		const bool puddle = i < count && (water->mParticleFlagSOA[i] & 0xf) == 2;
		if (puddle) {
			const JGeometry::TVec3<f32>& p = water->mParticlePositionSOA[i];
			const f32 dx = p.x - mx, dz = p.z - mz;
			const f32 d2 = dx * dx + dz * dz;
			if (!s_wasPuddle[i] && made < 3 && d2 < 3000.0f * 3000.0f) {  // a drop has just landed
				addRing(p.x, p.z);
				made++;
			}
			if (d2 < 80.0f * 80.0f)
				puddleAtFeet = true;
		}
		s_wasPuddle[i] = puddle;
	}
	if (!puddleAtFeet && ground && ground->isLegal() && isWetGround(ground))
		puddleAtFeet = true;
	// on the ground, and moving: a ring from his feet every few steps
	const f32 dx = mx - s_lastX, dz = mz - s_lastZ;
	s_lastX = mx;
	s_lastZ = mz;
	const bool onGround = ground && ground->isLegal()
	                      && gpMarioPos->y - heightAt(ground, mx, mz) < 10.0f && ground->getNormal().y > 0.5f;
	if (puddleAtFeet && onGround && dx * dx + dz * dz > 2.0f * 2.0f && dx * dx + dz * dz < 200.0f * 200.0f
	    && nowMs() - s_lastStep >= 300) {
		addRing(mx, mz);
		s_lastStep = nowMs();
	}
}

// ------------------------------------------------------------------ the reflection, drawn
// The disc around Mario is drawn as a fine web of rings, each point keeping its place. Its texture
// coordinates into the mirror's picture are worked out here, as the mirror camera's matrix would
// give them from the place (TMirrorCamera::drawSetting), so that they can be moved and repeated:
//
//   Ripples        the point shows the picture from a little beside it (Water Ripples, Puddle
//                  Ripples, above).
//   Viewing Angle  how strongly it reflects follows how slanting the view is, as real water does
//                  (more looking across it, less looking down into it).
//   Blur           the picture is taken several times around the point and averaged, as widely
//                  as Reflection Blur says.
//   Sun Glints     where the sun's light bounces off the water (or the ripples' slopes) straight to
//                  the camera, it shines (the sun as the stage places it; none indoors).
//   Water Depth    the sea, lakes and pools darken toward deep blue-green as the ground under them
//                  falls away (the stage's collision, under each point).
//
// As the game draws it (drawMirror): the frame's alpha holds the marks (the puddles, or the
// water); the disc's alpha times the picture's multiplies it; the colour already there is darkened
// by it and the picture added in its place; the alpha is left clear.
const int kRings    = 48;
const int kSegments = 96;
const int kVerts    = (kRings + 1) * kSegments;
const int kMostTaps = 7;

struct Ripple {
	Mtx tex;     // world place to the mirror texture (s, t, q)
	const TBGCheckData* plane;
	int rippleKind;  // 0 none, 1 water's waves, 2 puddle rings
	f32 amp;     // how far beside, in the game's units
	f32 k;       // 2 pi over the waves' length
	f32 phase;
	int time;
	s32 ms;      // the clock, for puddles
	f32 radius;
};

void setUpRipple(Ripple* r, const TBGCheckData* plane, int rippleKind, int strength, f32 radius)
{
	TMirrorCamera* mirror = gpMirrorModelManager->unk24;
	Mtx project;
	C_MTXLightPerspective(project, mirror->unk80 * gpCamera->mFovy, gpCamera->mAspect, 0.5f, -0.5f, 0.5f, 0.5f);
	MTXConcat(project, mirror->unk30, r->tex);
	r->plane      = plane;
	r->rippleKind = strength ? rippleKind : 0;
	r->radius     = radius;
	r->time       = now();
	r->ms         = nowMs();
	r->amp        = 0.0f;
	r->k          = 1.0f;
	if (r->rippleKind == 2) {
		r->amp = strength == 2 ? 10.0f : 5.0f;
		r->k   = 6.2831853f / kRingLength;
	} else if (r->rippleKind == 1) {
		const f32 spacing = radius / kRings;
		const f32 length  = spacing * 5.0f > 140.0f ? spacing * 5.0f : 140.0f;
		r->amp            = strength == 2 ? 10.0f : 4.0f;
		r->k              = 6.2831853f / length;
	}
	r->phase = (f32)r->time * 0.06f;
}

// How far beside x, z the picture is taken (the ripples), and the slope they give the surface.
void rippleAt(const Ripple& r, f32 x, f32 z, f32* ox, f32* oz, f32* sx, f32* sz)
{
	*ox = *oz = *sx = *sz = 0.0f;
	if (r.rippleKind == 1) {
		const f32 a1 = x * r.k + r.phase, a2 = (x + z) * r.k * 1.7f + r.phase * 1.3f;
		const f32 b1 = z * r.k * 1.1f - r.phase * 0.9f, b2 = (x - z) * r.k * 1.5f + r.phase * 1.1f;
		*ox = r.amp * (sinf(a1) + 0.5f * sinf(a2));
		*oz = r.amp * (cosf(b1) + 0.5f * cosf(b2));
		*sx = r.amp * r.k * (cosf(a1) + 0.85f * cosf(a2)) * 0.25f;
		*sz = -r.amp * r.k * (1.1f * sinf(b1) + 0.75f * sinf(b2)) * 0.25f;
	} else if (r.rippleKind == 2) {
		// round each disturbance, waves running out from it, as strong as it is young (100% as it
		// starts, 0% two seconds on) and fading to its edge; together, never more than one at full
		f32 tx = 0.0f, tz = 0.0f, total = 0.0f, gx = 0.0f, gz = 0.0f;
		const f32 run = r.k * kRingSpeed / 1000.0f;
		for (int i = 0; i < s_ringCount; i++) {
			const RingSource& src = s_rings[i];
			const s32 age         = r.ms - src.start;
			if (age < 0 || age >= kRingLife)
				continue;
			const f32 dx = x - src.x, dz = z - src.z;
			const f32 d2 = dx * dx + dz * dz;
			if (d2 >= kRingSpread * kRingSpread || d2 < 1.0f)
				continue;
			const f32 d      = sqrtf(d2);
			const f32 edge   = 1.0f - d / kRingSpread;
			const f32 weight = (1.0f - (f32)age / kRingLife) * edge * edge;
			const f32 phase  = d * r.k - age * run;
			tx += dx * weight * sinf(phase) / d;
			tz += dz * weight * sinf(phase) / d;
			gx += dx * weight * cosf(phase) / d;
			gz += dz * weight * cosf(phase) / d;
			total += weight;
		}
		if (total > 1.0f) {
			tx /= total, tz /= total, gx /= total, gz /= total;
		}
		*ox = r.amp * tx, *oz = r.amp * tz;
		*sx = r.amp * r.k * gx * 0.25f, *sz = r.amp * r.k * gz * 0.25f;
	}
}

void texCoordAt(const Ripple& r, f32 sx, f32 sz, f32* s, f32* t)
{
	const f32 sy = heightAt(r.plane, sx, sz) + kLift;
	const f32 a  = r.tex[0][0] * sx + r.tex[0][1] * sy + r.tex[0][2] * sz + r.tex[0][3];
	const f32 b  = r.tex[1][0] * sx + r.tex[1][1] * sy + r.tex[1][2] * sz + r.tex[1][3];
	f32 q        = r.tex[2][0] * sx + r.tex[2][1] * sy + r.tex[2][2] * sz + r.tex[2][3];
	if (fabsf(q) < 0.0001f)
		q = q < 0.0f ? -0.0001f : 0.0001f;
	*s = a / q;
	*t = b / q;
}

// This frame's web: each point's place, its alpha, the picture's coordinates for each tap, and
// how brightly it glints.
f32 s_vx[kVerts], s_vy[kVerts], s_vz[kVerts];
u8 s_va[kVerts], s_vg[kVerts];
f32 s_vs[kMostTaps][kVerts], s_vt[kMostTaps][kVerts];
int s_taps;
u8 s_sun[3];

int vertexIndex(int ring, int segment) { return ring * kSegments + segment; }

const int kCoarseRings = 12, kCoarseSegments = 24;  // Water Depth's web

// How strongly the view reflects, by how slanting it is (Viewing Angle): 1 at Off.
f32 viewFactor(int level, f32 cosine)
{
	if (!level)
		return 1.0f;
	const f32 m  = 1.0f - cosine;
	const f32 m3 = m * m * m;
	return level == 2 ? 0.3f + 1.4f * m3 : 0.6f + 0.8f * m3;
}

void buildWeb(const Ripple& r, u8 centre)
{
	const int view  = port_reflection_view_angle();
	const int glint = port_sun_glints();
	bool sun        = glint && gpSunModel && gpSunMgr && gpSunMgr->unk14;
	if (sun) {
		const bool sunset = (gpSunMgr->unk15 & 2) != 0;
		s_sun[0] = 255, s_sun[1] = sunset ? 170 : 245, s_sun[2] = sunset ? 90 : 220;
	}
	const JGeometry::TVec3<f32>& cam = gpCamera->mPosition;
	const JGeometry::TVec3<f32>& n0  = r.plane->getNormal();

	const f32 b = (f32)port_reflection_blur() * 3.0f;  // how far around, in the game's units
	s_taps      = b > 0.0f ? kMostTaps : 1;

	for (int ring = 0; ring <= kRings; ring++) {
		const f32 along = (f32)ring / kRings;
		for (int seg = 0; seg < kSegments; seg++) {
			const int i     = vertexIndex(ring, seg);
			const f32 angle = (f32)seg * (6.2831853f / kSegments);
			const f32 x     = gpMarioPos->x + sinf(angle) * r.radius * along;
			const f32 z     = gpMarioPos->z + cosf(angle) * r.radius * along;
			const f32 y     = heightAt(r.plane, x, z) + kLift;
			s_vx[i] = x, s_vy[i] = y, s_vz[i] = z;

			f32 ox, oz, slopeX, slopeZ;
			rippleAt(r, x, z, &ox, &oz, &slopeX, &slopeZ);

			// to the camera, and how slanting
			f32 vx = cam.x - x, vy = cam.y - y, vz = cam.z - z;
			const f32 vl = sqrtf(vx * vx + vy * vy + vz * vz);
			if (vl > 0.0f)
				vx /= vl, vy /= vl, vz /= vl;
			f32 cosine = fabsf(vx * n0.x + vy * n0.y + vz * n0.z);
			cosine     = cosine > 1.0f ? 1.0f : cosine;
			f32 a      = centre * (1.0f - along) * viewFactor(view, cosine);
			s_va[i]    = (u8)(a > 255.0f ? 255.0f : a);

			// the taps: the middle, and around it as wide as the surface is rough
			texCoordAt(r, x + ox, z + oz, &s_vs[0][i], &s_vt[0][i]);
			if (s_taps > 1) {
				for (int t = 1; t < s_taps; t++) {
					const f32 ta = (t - 1) * (6.2831853f / (kMostTaps - 1)) + 0.4f;
					texCoordAt(r, x + ox + sinf(ta) * b, z + oz + cosf(ta) * b, &s_vs[t][i], &s_vt[t][i]);
				}
			}

			// the sun's glint: its light bounced off the surface (tilted by the ripples' slope)
			s_vg[i] = 0;
			if (sun) {
				const JGeometry::TVec3<f32>& sp = gpSunModel->mPosition;
				f32 lx = sp.x - x, ly = sp.y - y, lz = sp.z - z;
				const f32 ll = sqrtf(lx * lx + ly * ly + lz * lz);
				if (ll > 0.0f && ly > 0.0f) {
					lx /= ll, ly /= ll, lz /= ll;
					f32 hx = lx + vx, hy = ly + vy, hz = lz + vz;
					const f32 hl = sqrtf(hx * hx + hy * hy + hz * hz);
					f32 nx = n0.x - slopeX, ny = n0.y, nz = n0.z - slopeZ;
					const f32 nl = sqrtf(nx * nx + ny * ny + nz * nz);
					if (hl > 0.0f && nl > 0.0f) {
						f32 d = (hx * nx + hy * ny + hz * nz) / (hl * nl);
						if (d > 0.0f) {
							// a sharp highlight: d to the 256th
							for (int p = 0; p < 8; p++)
								d *= d;
							f32 g = d * (glint == 2 ? 2.0f : 1.0f) * (1.0f - along) * 255.0f;
							s_vg[i] = (u8)(g > 255.0f ? 255.0f : g);
						}
					}
				}
			}
		}
	}
}

enum WebColour { WEB_ALPHA, WEB_GREY, WEB_GLINT };

void drawWeb(int tap, int colour, u8 grey, bool tex)
{
	GXClearVtxDesc();
	GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
	GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	if (tex)
		GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	if (tex) {
		GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
		GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
	}
	for (int ring = 0; ring < kRings; ring++) {
		GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, (u16)(2 * (kSegments + 1)));
		for (int seg = 0; seg <= kSegments; seg++) {
			for (int k = 0; k < 2; k++) {
				const int i = vertexIndex(ring + 1 - k, seg % kSegments);
				GXPosition3f32(s_vx[i], s_vy[i], s_vz[i]);
				if (colour == WEB_ALPHA)
					GXColor4u8(0xff, 0xff, 0xff, s_va[i]);
				else if (colour == WEB_GREY)
					GXColor4u8(grey, grey, grey, s_va[i]);
				else
					GXColor4u8((u8)(s_sun[0] * s_vg[i] / 255), (u8)(s_sun[1] * s_vg[i] / 255),
					           (u8)(s_sun[2] * s_vg[i] / 255), 0xff);
				if (tex)
					GXTexCoord2f32(s_vs[tap][i], s_vt[tap][i]);
			}
		}
		GXEnd();
	}
}

bool anyGlint()
{
	for (int i = 0; i < kVerts; i++)
		if (s_vg[i])
			return true;
	return false;
}

// The reflection over what the frame's alpha marks, on plane, over the disc of radius around Mario
// (the game's centre alpha). Leaves the alpha clear in the disc, colour and alpha updates as the game's disc does
// (colour on, alpha on, destination alpha 0); the caller puts back the rest.
void drawReflection(MtxPtr view, const TBGCheckData* plane, f32 radius, u8 centre, int rippleKind, int rippleStrength)
{
	Ripple r;
	setUpRipple(&r, plane, rippleKind, rippleStrength, radius);
	buildWeb(r, centre);

	Mtx identity;
	MTXIdentity(identity);
	gpMirrorModelManager->unk24->drawSetting(identity);  // the mirror's picture, as texture 0
	GXSetCurrentMtx(GX_PNMTX0);
	GXLoadPosMtxImm(view, GX_PNMTX0);
	GXSetCullMode(GX_CULL_NONE);
	GXSetNumChans(1);
	GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
	GXSetNumTevStages(1);
	GXSetZCompLoc(GX_TRUE);
	GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);

	// the sun's glints, added where the marks are (before they are worn down)
	if (anyGlint()) {
		GXSetNumTexGens(0);
		GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
		GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
		GXSetBlendMode(GX_BM_BLEND, GX_BL_DSTALPHA, GX_BL_ONE, GX_LO_NOOP);
		GXSetColorUpdate(GX_TRUE);
		GXSetAlphaUpdate(GX_FALSE);
		GXSetDstAlpha(GX_FALSE, 0);
		drawWeb(0, WEB_GLINT, 0, false);
	}

	// the marks times the disc's alpha times the picture's (where something is reflected)
	GXSetNumTexGens(1);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
	GXSetBlendMode(GX_BM_BLEND, GX_BL_ZERO, GX_BL_SRCALPHA, GX_LO_NOOP);
	GXSetColorUpdate(GX_FALSE);
	GXSetAlphaUpdate(GX_TRUE);
	GXSetDstAlpha(GX_FALSE, 0);
	drawWeb(0, WEB_ALPHA, 0, true);

	// the colour there darkened by as much ...
	GXSetNumTexGens(0);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	GXSetBlendMode(GX_BM_BLEND, GX_BL_ZERO, GX_BL_INVDSTALPHA, GX_LO_NOOP);
	GXSetColorUpdate(GX_TRUE);
	GXSetAlphaUpdate(GX_FALSE);
	drawWeb(0, WEB_ALPHA, 0, false);

	// ... and the picture added in its place, each tap its share
	GXSetNumTexGens(1);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
	GXSetBlendMode(GX_BM_BLEND, GX_BL_DSTALPHA, GX_BL_ONE, GX_LO_NOOP);
	const u8 share = (u8)(255 / s_taps);
	for (int t = 0; t < s_taps; t++)
		drawWeb(t, WEB_GREY, share, true);

	// the alpha left clear
	GXSetNumTexGens(0);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP);
	GXSetColorUpdate(GX_FALSE);
	GXSetAlphaUpdate(GX_TRUE);
	GXSetDstAlpha(GX_TRUE, 0);
	drawWeb(0, WEB_ALPHA, 0, false);

	GXClearVtxDesc();
	GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
	GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX3x4, GX_TG_POS, 0x1e, 0, 0x7d);
	GXSetColorUpdate(GX_TRUE);
}

// Water Depth: the water darkened toward deep blue-green where the ground under it falls away.
// Drawn over the water's marks (strength 255) on a coarse web, before the reflection's marks.
const u8 kDeep[3] = {16, 56, 72};

void drawDepth(MtxPtr view, f32 radius, const TBGCheckData* plane, f32 fade)
{
	const int level = port_water_depth();
	if (!level || !gpMap || !plane)
		return;
	const f32 most = level == 2 ? 0.75f : 0.45f;
	static f32 px[(kCoarseRings + 1) * kCoarseSegments], py[(kCoarseRings + 1) * kCoarseSegments],
	    pz[(kCoarseRings + 1) * kCoarseSegments];
	static u8 pa[(kCoarseRings + 1) * kCoarseSegments];
	for (int ring = 0; ring <= kCoarseRings; ring++)
		for (int seg = 0; seg < kCoarseSegments; seg++) {
			const int i     = ring * kCoarseSegments + seg;
			const f32 along = (f32)ring / kCoarseRings;
			const f32 angle = (f32)seg * (6.2831853f / kCoarseSegments);
			const f32 x     = gpMarioPos->x + sinf(angle) * radius * along;
			const f32 z     = gpMarioPos->z + cosf(angle) * radius * along;
			const f32 h     = heightAt(plane, x, z);
			const TBGCheckData* floor;
			const f32 g = gpMap->checkGroundIgnoreWaterSurface(x, h, z, &floor);
			f32 depth   = h - g;
			depth       = depth < 0.0f ? 0.0f : depth > 5000.0f ? 5000.0f : depth;
			// deeper, darker: half of it by 400 units down, nearly all by 1500
			const f32 dark = 1.0f - expf(-depth / 580.0f);
			// fading out over the last fifth of the disc
			const f32 edge = along > 0.8f ? (1.0f - along) * 5.0f : 1.0f;
			px[i] = x, py[i] = h + kLift, pz[i] = z;
			pa[i] = (u8)(most * dark * edge * fade * 255.0f);
		}

	GXClearVtxDesc();
	GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
	GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GXSetCurrentMtx(GX_PNMTX0);
	GXLoadPosMtxImm(view, GX_PNMTX0);
	GXSetNumTexGens(0);
	GXSetNumTevStages(1);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	for (int pass = 0; pass < 2; pass++) {
		if (pass == 0) {
			// the marks times how dark here
			GXSetBlendMode(GX_BM_BLEND, GX_BL_ZERO, GX_BL_SRCALPHA, GX_LO_NOOP);
			GXSetColorUpdate(GX_FALSE);
			GXSetAlphaUpdate(GX_TRUE);
		} else {
			// the deep colour laid over by as much
			GXSetBlendMode(GX_BM_BLEND, GX_BL_DSTALPHA, GX_BL_INVDSTALPHA, GX_LO_NOOP);
			GXSetColorUpdate(GX_TRUE);
			GXSetAlphaUpdate(GX_FALSE);
		}
		for (int ring = 0; ring < kCoarseRings; ring++) {
			GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, (u16)(2 * (kCoarseSegments + 1)));
			for (int seg = 0; seg <= kCoarseSegments; seg++)
				for (int k = 0; k < 2; k++) {
					const int i = (ring + 1 - k) * kCoarseSegments + seg % kCoarseSegments;
					GXPosition3f32(px[i], py[i], pz[i]);
					GXColor4u8(kDeep[0], kDeep[1], kDeep[2], pa[i]);
				}
			GXEnd();
		}
	}
	GXSetColorUpdate(GX_FALSE);
	GXSetAlphaUpdate(GX_TRUE);
}

// Whether the port draws the disc itself (anything of the above in use), not the game's.
bool ownDisc(int rippleKind)
{
	if (port_reflection_view_angle() || port_sun_glints() || port_reflection_blur())
		return true;
	if (rippleKind == 1)
		return port_water_ripples() != 0;
	return port_puddle_ripples() && ringsLive();
}

void drawTriangles(const TBGCheckData* const* tris, int count, u8 alpha)
{
	if (!count)
		return;
	GXBegin(GX_TRIANGLES, GX_VTXFMT0, (u16)(count * 3));
	for (int i = 0; i < count; i++) {
		const TBGCheckData* d = tris[i];
		GXPosition3f32(d->mPoint1.x, d->mPoint1.y + kLift, d->mPoint1.z);
		GXColor4u8(0xff, 0xff, 0xff, alpha);
		GXPosition3f32(d->mPoint2.x, d->mPoint2.y + kLift, d->mPoint2.z);
		GXColor4u8(0xff, 0xff, 0xff, alpha);
		GXPosition3f32(d->mPoint3.x, d->mPoint3.y + kLift, d->mPoint3.z);
		GXColor4u8(0xff, 0xff, 0xff, alpha);
	}
	GXEnd();
}

void drawMarks(u8 alpha) { drawTriangles(s_tris, s_triCount, alpha); }

} // namespace

const TBGCheckData* choosePlane(const TBGCheckData* ground);

// From TMirrorModelManager::perform (zzzzz-ext-25-water-reflections), as each frame starts: the
// plane the mirror scene is mirrored about this frame. ground: the floor under Mario, the game's.
const TBGCheckData* port_mirror_plane(const TBGCheckData* ground)
{
	s_plane    = ground;
	s_kind     = KIND_NONE;
	s_triCount = 0;
	updateRings(ground);
	const TBGCheckData* plane = choosePlane(ground);
	// Water Depth: on the water while the reflection is on it, fading out on the last one after
	s_depthWanted   = s_kind == KIND_WATER && port_water_depth();
	s_depthTriCount = 0;
	if (s_depthWanted)
		s_depthPlane = s_plane;
	if (s_depthPlane && gpMarioPos && (s_depthWanted || s_depthFade > 0.0f)) {
		s_depthRadius = port_puddle_reflection_distance();
		s_depthRadius = s_depthRadius > 2500.0f ? s_depthRadius : 2500.0f;
		CollectDepth depth = {gpMarioPos->x, gpMarioPos->z, s_depthRadius, s_depthPlane};
		eachGroundNear(gpMarioPos->x, gpMarioPos->z, s_depthRadius, depth);
	}
	return plane;
}

const TBGCheckData* choosePlane(const TBGCheckData* ground)
{
	if (!on())
		return ground;
	const f32 radius = port_puddle_reflection_distance();
	const f32 x = gpMarioPos->x, y = gpMarioPos->y, z = gpMarioPos->z;

	// on the floor: the puddles, and the wet ground that lies on it. FLUDD's puddles near Mario
	// always keep the reflection on the floor, so that they never lose theirs to water near by.
	f32 groundDistance = radius;
	bool groundFound   = nearestPuddle(radius, &groundDistance);
	const bool puddles = groundFound;
	if (ground && ground->isLegal()) {
		FindWetGround wet = {x, z, ground, 0.0f, false};
		eachGroundNear(x, z, radius, wet);
		if (wet.found && (!groundFound || wet.bestDistance < groundDistance)) {
			groundDistance = wet.bestDistance;
			groundFound    = true;
		}
	}

	FindWater water = {x, y, z, nullptr, 0.0f};
	eachGroundNear(x, z, radius, water);
	bool useWater = !puddles && water.best && water.bestDistance < radius && cameraAbove(water.best);
	if (useWater && groundFound) {
		static bool s_wasWater;
		const f32 stick = s_wasWater ? kStick : -kStick;
		useWater        = water.bestDistance < groundDistance + stick;
		s_wasWater      = useWater;
	}

	// the log: each time the reflection moves from the floor to water or back
	static int s_logged = -1;
	static s32 s_loggedAt;
	if ((int)useWater != s_logged && nowMs() - s_loggedAt > 1000) {
		s_logged   = useWater;
		s_loggedAt = nowMs();
		if (useWater)
			fprintf(stderr, "[puddles] reflections on water (its surface at %.0f, Mario at %.0f, %.0f away)\n",
			         heightAt(water.best, x, z), y, water.bestDistance);
		else
			fprintf(stderr, "[puddles] reflections on the floor (%s)\n",
			         puddles ? "FLUDD's puddles near" : "no open water near, or wet ground nearer");
	}

	if (useWater) {
		s_plane = water.best;
		s_kind  = KIND_WATER;
	} else if (ground && ground->isLegal()) {
		s_kind = KIND_GROUND;
	} else {
		return ground;
	}
	Collect collect = {x, z, radius, s_plane, s_kind == KIND_WATER};
	eachGroundNear(x, z, radius, collect);
	if (s_kind == KIND_GROUND && !s_triCount)
		s_kind = KIND_NONE;  // no wet ground: the floor's puddles, as the game has them
	return s_plane;
}

// Whether water or wet ground shows reflections this frame (port_reflections.cpp draws the copies
// of characters and objects then, as it does while there are puddles).
extern "C" int port_water_mirror_active(void) { return s_kind != KIND_NONE && s_triCount > 0; }

extern "C" int port_water_mirror_on_water(void) { return s_kind == KIND_WATER; }

// From TModelWaterManager::perform (zzzzz-ext-25-water-reflections), after drawMirror: the
// reflection in the water or wet ground on this frame's plane. view: the camera's.
// Water Depth, each frame: fading toward wanted or not, and drawn (its own marks, then cleared).
void drawDepthFrame(MtxPtr view)
{
	const s32 t  = nowMs();
	s32 dt       = t - s_depthFadeAt;
	s_depthFadeAt = t;
	dt           = dt < 0 ? 0 : dt > 200 ? 200 : dt;  // a pause or a save state: no jump
	const f32 step = (f32)dt / kDepthFadeMs;
	if (s_depthWanted)
		s_depthFade = s_depthFade + step > 1.0f ? 1.0f : s_depthFade + step;
	else
		s_depthFade = s_depthFade - step < 0.0f ? 0.0f : s_depthFade - step;
	if (s_depthFade <= 0.0f || !s_depthPlane || !s_depthTriCount || !gpMarioPos || !port_water_depth())
		return;
	// eased: slow to start and to finish
	const f32 fade = s_depthFade * s_depthFade * (3.0f - 2.0f * s_depthFade);

	GXClearVtxDesc();
	GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
	GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GXSetCurrentMtx(GX_PNMTX0);
	GXLoadPosMtxImm(view, GX_PNMTX0);
	GXSetCullMode(GX_CULL_NONE);
	GXSetNumChans(1);
	GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
	GXSetZCompLoc(GX_TRUE);
	GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
	GXSetNumTexGens(0);
	GXSetNumTevStages(1);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	// the water marked, where it is seen
	GXSetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP);
	GXSetColorUpdate(GX_FALSE);
	GXSetAlphaUpdate(GX_TRUE);
	GXSetDstAlpha(GX_FALSE, 0);
	GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
	drawTriangles(s_depthTris, s_depthTriCount, 255);
	drawDepth(view, s_depthRadius, s_depthPlane, fade);
	// the marks cleared
	GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP);
	GXSetColorUpdate(GX_FALSE);
	GXSetAlphaUpdate(GX_TRUE);
	GXSetDstAlpha(GX_TRUE, 0);
	GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
	drawTriangles(s_depthTris, s_depthTriCount, 0);
	GXSetColorUpdate(GX_TRUE);
	GXSetAlphaUpdate(GX_FALSE);
	GXSetDstAlpha(GX_FALSE, 0);
}

void port_draw_water_mirror(MtxPtr view)
{
	drawDepthFrame(view);
	if (!port_water_mirror_active() || !gpMirrorModelManager || !gpMirrorModelManager->unk24
	    || !gpModelWaterManager)
		return;
	const int tenths = port_puddle_surface_strength(s_kind == KIND_WATER ? PORT_PUDDLE_WATER
	                                                                      : PORT_PUDDLE_WET_GROUND);
	const u8 strength = (u8)((tenths < 0 ? 0 : tenths > 1000 ? 1000 : tenths) * 255 / 1000);
	if (!strength)
		return;
	const f32 radius = port_puddle_reflection_distance();

	GXClearVtxDesc();
	GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
	GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GXSetCurrentMtx(GX_PNMTX0);
	GXLoadPosMtxImm(view, GX_PNMTX0);
	GXSetCullMode(GX_CULL_NONE);
	GXSetNumChans(1);
	GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_VTX, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
	GXSetZCompLoc(GX_TRUE);
	GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);

	// 1: the water marked in the frame's alpha, where it is seen
	GXSetNumTexGens(0);
	GXSetNumTevStages(1);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	GXSetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP);
	GXSetColorUpdate(GX_FALSE);
	GXSetAlphaUpdate(GX_TRUE);
	GXSetDstAlpha(GX_FALSE, 0);
	GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
	drawMarks(strength);

	// 2-3: the reflection over them (the port's own disc, or as the game's)
	const int rippleKind     = s_kind == KIND_WATER ? 1 : 2;
	const int rippleStrength = s_kind == KIND_WATER ? port_water_ripples() : port_puddle_ripples();
	if (gpCamera && ownDisc(rippleKind)) {
		drawReflection(view, s_plane, radius, gpModelWaterManager->unk5D64, rippleKind, rippleStrength);
	} else {
		Mtx identity;
		MTXIdentity(identity);
		gpMirrorModelManager->unk24->drawSetting(identity);
		GXSetCurrentMtx(GX_PNMTX0);
		GXLoadPosMtxImm(view, GX_PNMTX0);
		GXSetNumTexGens(1);
		GXSetNumTevStages(1);
		GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
		GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
		GXSetBlendMode(GX_BM_BLEND, GX_BL_ZERO, GX_BL_SRCALPHA, GX_LO_NOOP);
		GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
		GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX3x4, GX_TG_POS, 0x1e, 0, 0x7d);
		drawDisc(radius, gpModelWaterManager->unk5D64);
		GXSetBlendMode(GX_BM_BLEND, GX_BL_DSTALPHA, GX_BL_INVDSTALPHA, GX_LO_NOOP);
		GXSetColorUpdate(GX_TRUE);
		GXSetAlphaUpdate(GX_TRUE);
		GXSetDstAlpha(GX_TRUE, 0);
		drawDisc(radius, gpModelWaterManager->unk5D64);
	}

	// 4: what of the marks lies outside the disc, cleared too
	GXSetNumTexGens(0);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP);
	GXSetColorUpdate(GX_FALSE);
	GXSetAlphaUpdate(GX_TRUE);
	GXSetDstAlpha(GX_TRUE, 0);
	GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
	GXClearVtxDesc();
	GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
	GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GXSetCurrentMtx(GX_PNMTX0);
	GXLoadPosMtxImm(view, GX_PNMTX0);
	drawMarks(0);

	GXSetColorUpdate(GX_TRUE);
	GXSetAlphaUpdate(GX_FALSE);
	GXSetDstAlpha(GX_FALSE, 0);
}

// From TModelWaterManager::drawMirror (zzzzz-ext-25-water-reflections), with the puddles marked:
// the reflection over them drawn by the port (ripples, Viewing Angle, Blur, Sun Glints), 1; or 0,
// the game's own disc, as ever.
int port_draw_puddle_reflection(MtxPtr view, f32 radius, u8 centre)
{
	if (!gpCamera || !gpMirrorModelManager || !gpMirrorModelManager->unk24 || !gpMarioGroundPlane
	    || !*gpMarioGroundPlane || !(*gpMarioGroundPlane)->isLegal() || !ownDisc(2))
		return 0;
	drawReflection(view, *gpMarioGroundPlane, radius, centre, 2, port_puddle_ripples());
	return 1;
}

// From TMirrorModelManager::perform (zzzzz-ext-25-water-reflections), each tick: whether, in the
// real mirror's area mirror (-1: none), FLUDD's puddles near Mario take the reflection from it
// (Puddles Beat Mirrors). They do from when one is within the Reflection Distance to when none is
// within 200 units more, so that the mirror does not flicker on and off at the edge.
int port_puddles_beat_mirror(int mirror)
{
	static bool s_active;
	bool active = false;
	if (mirror != -1 && port_puddles_beat_mirrors() && gpMarioPos) {
		f32 distance;
		active = nearestPuddle(port_puddle_reflection_distance() + (s_active ? 200.0f : 0.0f), &distance);
	}
	if (active != s_active)
		fprintf(stderr, "[puddles] %s\n",
		        active ? "puddles near Mario in a real mirror's area: they take the reflection (Puddles Beat Mirrors)"
		               : "the real mirror's reflection back");
	s_active = active;
	return active;
}

namespace {
// A new area: the last area's water is gone.
void resetDepth()
{
	s_depthPlane    = nullptr;
	s_depthTriCount = 0;
	s_depthWanted   = false;
	s_depthFade     = 0.0f;
}
PortExtension s_depthExtension;
struct RegisterDepth {
	RegisterDepth()
	{
		memset(&s_depthExtension, 0, sizeof s_depthExtension);
		s_depthExtension.name       = "water depth";
		s_depthExtension.stageSetup = resetDepth;
		s_depthExtension.moveStage  = resetDepth;
		port_ext_register(&s_depthExtension);
	}
} s_registerDepth;
} // namespace
