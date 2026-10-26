// 3D Grass (port_grass.h): blades of grass standing on the stages' grass, as each place would have
// it. Delfino Plaza's lawns are the town's, and the Piantas mow them: short, even, in stripes the
// mower leaves. Pinna Park is kept the same. Sirena's hotel lawns, Ricco's verges and the Airstrip
// are tended now and then: a little longer. Bianco Hills, Pianta Village, Noki Bay, Gelato's dunes,
// Corona Mountain and any stage it does not know (a mod's) grow wild: tall, uneven, in clumps,
// with bare patches.
//
// Where: the ground the stage's collision calls grass (material 0x10, as the footsteps' sound and
// Reflections by Surface take it), flat enough to stand on, open to the sky above it. The blades
// are laid out once on a grid across the world (each in its cell, its place, height and look from
// the cell alone, so they are the same every time) in 256-unit tiles round Mario, a few tiles a
// tick, and forgotten when he is far away.
//
// How they move: each sways with the wind (a breeze that rolls across the stage); bends away from
// Mario as he goes through it, and back over a second and a half along the way he went; and lies
// flat under FLUDD's puddles. Near Mario they are whole; farther out, fewer and shorter, gone at
// the edge.
#include "port_ext.h"
#include "port_grass.h"

#include <Camera/Camera.hpp>
#include <Map/Map.hpp>
#include <Map/MapCollisionData.hpp>
#include <Map/MapData.hpp>
#include <Player/MarioAccess.hpp>
#include <Player/ModelWaterManager.hpp>
#include <System/Application.hpp>
#include <System/StageUtil.hpp>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <dolphin/os.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

// ------------------------------------------------------------------ the kinds of grass
struct Style {
	const char* name;
	f32 spacing;     // between blades, units (Mario is about 160 tall)
	f32 low, high;   // the blades' height
	f32 width;       // at the base
	f32 wind;        // how far the breeze bends the tip, times the height
	f32 reach;       // how far from Mario the blades go
	int segments;    // 1 straight, 2 bending
	bool stripes;    // the mower's stripes
	bool clumps;     // uneven: taller and shorter patches, bare spots
	u8 base[3], tip[3];
};

const Style kCut    = {"cut short (the Piantas mow it)", 8.0f, 5.0f, 9.0f, 1.7f, 0.05f, 650.0f, 1, true, false,
                       {46, 112, 30}, {104, 186, 54}};
const Style kTended = {"tended now and then", 9.0f, 9.0f, 17.0f, 1.9f, 0.09f, 700.0f, 1, false, false,
                       {44, 106, 32}, {116, 184, 60}};
const Style kWild   = {"wild", 11.0f, 14.0f, 44.0f, 2.4f, 0.17f, 850.0f, 2, false, true,
                       {38, 90, 28}, {142, 176, 64}};

// The grass of a world (SMS_getShineStage's order), by location.
const Style* styleOfWorld(int world)
{
	switch (world) {
	case 1: return &kCut;     // Delfino Plaza: the town's lawns
	case 5: return &kCut;     // Pinna Park: a park
	case 0:                   // the Airstrip
	case 3:                   // Ricco Harbor's verges
	case 6: return &kTended;  // Sirena Beach: the hotel's lawns
	default: return &kWild;   // Bianco Hills, Gelato Beach, Pianta Village, Noki Bay, Corona, mods' stages
	}
}

const char* const kWorldNames[] = {"the Airstrip", "Delfino Plaza", "Bianco Hills", "Ricco Harbor", "Gelato Beach",
                                   "Pinna Park", "Sirena Beach", "Pianta Village", "Noki Bay", "Corona Mountain"};

const Style* s_style;  // this stage's, or 0 (off)
int s_styleSetting = -1;
int s_styleArea    = -1;

const Style* wantedStyle(int* world)
{
	const int setting = port_grass();
	*world            = SMS_getShineStage(gpApplication.mCurrArea.getStage());
	switch (setting) {
	case 1: return styleOfWorld(*world);
	case 2: return &kCut;
	case 3: return &kWild;
	default: return 0;
	}
}

// ------------------------------------------------------------------ randomness from a place
u32 hash2(int x, int z, u32 salt)
{
	u32 h = (u32)x * 0x8DA6B343u ^ (u32)z * 0xD8163841u ^ salt * 0xCB1AB31Fu;
	h ^= h >> 13;
	h *= 0x5BD1E995u;
	h ^= h >> 15;
	return h;
}
f32 rand01(u32 h) { return (f32)(h & 0xFFFFFF) * (1.0f / 16777216.0f); }

// A smooth noise, 0 to 1, varying over `size` units: clumps of taller grass and bare patches.
f32 noise(f32 x, f32 z, f32 size, u32 salt)
{
	const f32 fx = x / size, fz = z / size;
	const int ix = (int)floorf(fx), iz = (int)floorf(fz);
	f32 tx = fx - ix, tz = fz - iz;
	tx = tx * tx * (3.0f - 2.0f * tx);
	tz = tz * tz * (3.0f - 2.0f * tz);
	const f32 a = rand01(hash2(ix, iz, salt)), b = rand01(hash2(ix + 1, iz, salt));
	const f32 c = rand01(hash2(ix, iz + 1, salt)), d = rand01(hash2(ix + 1, iz + 1, salt));
	return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
}

// ------------------------------------------------------------------ the ground
bool isGrass(const TBGCheckData* d)
{
	return d && d->unk6 == 0x10 && d->getNormal().y >= 0.6f && !d->isWaterSurface();
}

f32 heightAt(const TBGCheckData* d, f32 x, f32 z)
{
	const JGeometry::TVec3<f32>& n = d->getNormal();
	return -(d->getPlaneDistance() + n.x * x + n.z * z) / n.y;
}

bool over(const TBGCheckData* d, f32 x, f32 z)
{
	const JGeometry::TVec3<f32>& a = d->mPoint1;
	const JGeometry::TVec3<f32>& b = d->mPoint2;
	const JGeometry::TVec3<f32>& c = d->mPoint3;
	const f32 s1 = (b.x - a.x) * (z - a.z) - (b.z - a.z) * (x - a.x);
	const f32 s2 = (c.x - b.x) * (z - b.z) - (c.z - b.z) * (x - b.x);
	const f32 s3 = (a.x - c.x) * (z - c.z) - (a.z - c.z) * (x - c.x);
	return (s1 >= 0 && s2 >= 0 && s3 >= 0) || (s1 <= 0 && s2 <= 0 && s3 <= 0);
}

// The static map's grass triangles within radius of x, z (the game's 1024-unit grid), at most n.
int grassNear(f32 x, f32 z, f32 radius, const TBGCheckData** out, int n)
{
	const TMapCollisionData* map = gpMapCollisionData;
	if (!map)
		return 0;
	const int across = map->unk8, down = map->unkC;
	if (across <= 0 || down <= 0)
		return 0;
	int x0 = (int)((x - radius + map->mGridExtentX) * (1.0f / 1024));
	int x1 = (int)((x + radius + map->mGridExtentX) * (1.0f / 1024));
	int z0 = (int)((z - radius + map->mGridExtentY) * (1.0f / 1024));
	int z1 = (int)((z + radius + map->mGridExtentY) * (1.0f / 1024));
	x0     = x0 < 0 ? 0 : x0;
	z0     = z0 < 0 ? 0 : z0;
	x1     = x1 >= across ? across - 1 : x1;
	z1     = z1 >= down ? down - 1 : z1;
	int count = 0;
	for (int gz = z0; gz <= z1; gz++)
		for (int gx = x0; gx <= x1; gx++)
			for (const TBGCheckList* l = map->getGridRoot14(gx, gz).getGroundList(); l; l = l->getNext()) {
				const TBGCheckData* d = l->unk8;
				if (!isGrass(d))
					continue;
				bool seen = false;
				for (int i = 0; i < count && !seen; i++)
					seen = out[i] == d;
				if (!seen && count < n)
					out[count++] = d;
			}
	return count;
}

// ------------------------------------------------------------------ the tiles
const f32 kTile      = 256.0f;
const int kMostTiles = 120;
const int kMostBlades = 1400;  // in a tile: (256 / 8)^2 at the closest spacing, and more

struct Blade {
	f32 x, y, z, h;
	f32 dirX, dirZ;  // the way its face looks (its width across it)
	u16 phase;
	u8 shade, keep;  // its colour's variation; how far out it is still drawn (0 to 255)
};

struct Tile {
	int tx, tz;
	int count;
	bool used;
	Blade* blades;  // malloc: the host's memory, not the game's
	f32 minY, maxY;
};

Tile s_tiles[kMostTiles];
int s_bladeTotal;

void forgetTiles()
{
	for (int i = 0; i < kMostTiles; i++) {
		free(s_tiles[i].blades);
		s_tiles[i].blades = 0;
		s_tiles[i].used   = false;
	}
	s_bladeTotal = 0;
}

Tile* findTile(int tx, int tz)
{
	for (int i = 0; i < kMostTiles; i++)
		if (s_tiles[i].used && s_tiles[i].tx == tx && s_tiles[i].tz == tz)
			return &s_tiles[i];
	return 0;
}

// A slot for a new tile: a free one, else the one farthest from Mario.
Tile* slotFor(f32 mx, f32 mz)
{
	Tile* worst = 0;
	f32 worstD  = -1.0f;
	for (int i = 0; i < kMostTiles; i++) {
		Tile& t = s_tiles[i];
		if (!t.used)
			return &t;
		const f32 dx = (t.tx + 0.5f) * kTile - mx, dz = (t.tz + 0.5f) * kTile - mz;
		const f32 d = dx * dx + dz * dz;
		if (d > worstD)
			worstD = d, worst = &t;
	}
	if (worst) {
		s_bladeTotal -= worst->count;
		free(worst->blades);
		worst->blades = 0;
		worst->used   = false;
	}
	return worst;
}

void makeTile(Tile* t, int tx, int tz)
{
	const Style& st = *s_style;
	t->tx = tx, t->tz = tz;
	t->used   = true;
	t->count  = 0;
	t->blades = 0;
	t->minY = 1e9f, t->maxY = -1e9f;
	const f32 x0 = tx * kTile, z0 = tz * kTile;
	const TBGCheckData* tris[96];
	const int n = grassNear(x0 + kTile * 0.5f, z0 + kTile * 0.5f, kTile * 0.75f, tris, 96);
	if (!n)
		return;
	t->blades = (Blade*)malloc(sizeof(Blade) * kMostBlades);
	if (!t->blades)
		return;
	// the world's grid of cells, so that a blade is the same whichever tile finds it
	const int c0x = (int)ceilf(x0 / st.spacing), c1x = (int)ceilf((x0 + kTile) / st.spacing);
	const int c0z = (int)ceilf(z0 / st.spacing), c1z = (int)ceilf((z0 + kTile) / st.spacing);
	const TBGCheckData* last = tris[0];
	for (int cz = c0z; cz < c1z && t->count < kMostBlades; cz++)
		for (int cx = c0x; cx < c1x && t->count < kMostBlades; cx++) {
			const u32 h = hash2(cx, cz, 0x9E37u);
			const f32 x = (cx + rand01(h)) * st.spacing;
			const f32 z = (cz + rand01(h * 0x2545F491u + 7u)) * st.spacing;
			// wild grass: bare where the clump noise is low
			f32 clump = 1.0f;
			if (st.clumps) {
				clump = noise(x, z, 140.0f, 0x51u) * 0.7f + noise(x, z, 45.0f, 0x77u) * 0.3f;
				if (clump < 0.28f || rand01(h >> 3) > clump * 1.6f)
					continue;
			}
			const TBGCheckData* tri = over(last, x, z) ? last : 0;
			for (int i = 0; i < n && !tri; i++)
				if (over(tris[i], x, z))
					tri = tris[i];
			if (!tri)
				continue;
			last          = tri;
			const f32 y   = heightAt(tri, x, z);
			// open to the sky: the ground found from a little above is this grass, not a bench or
			// a step on it
			const TBGCheckData* top = 0;
			const f32 g             = gpMap->checkGroundIgnoreWaterSurface(x, y + 60.0f, z, &top);
			if (top != tri && fabsf(g - y) > 2.0f)
				continue;
			Blade& b = t->blades[t->count++];
			b.x = x, b.y = y - 1.0f, b.z = z;
			f32 hgt = st.low + (st.high - st.low) * rand01(h >> 5);
			if (st.clumps)
				hgt *= 0.55f + 0.7f * clump;
			b.h = hgt;
			const f32 yaw = rand01(h >> 9) * 6.2831853f;
			b.dirX = cosf(yaw), b.dirZ = sinf(yaw);
			b.phase = (u16)(h >> 7);
			b.shade = (u8)(h >> 17);
			b.keep  = (u8)(hash2(cx, cz, 0x3Cu) >> 24);
			if (b.y < t->minY)
				t->minY = b.y;
			if (b.y + b.h > t->maxY)
				t->maxY = b.y + b.h;
		}
	s_bladeTotal += t->count;
}

// ------------------------------------------------------------------ what presses the grass down
// Mario where he is now, and his way over the last second and a half (each point fading), and the
// puddles FLUDD has made: each pushes the blades near it away, and down.
struct Press {
	f32 x, y, z, r, strength;
};
const int kTrail = 24;
const s32 kTrailMs = 1500;
struct TrailPoint {
	f32 x, y, z;
	s32 at;
};
TrailPoint s_trail[kTrail];
int s_trailCount, s_trailNext;

s32 nowMs() { return (s32)OSTicksToMilliseconds(OSGetTime()); }

const int kMostPress = 160;
Press s_press[kMostPress];
int s_pressCount;

void gatherPress(f32 mx, f32 my, f32 mz)
{
	s_pressCount = 0;
	const s32 now = nowMs();
	Press& m = s_press[s_pressCount++];
	m.x = mx, m.y = my, m.z = mz, m.r = 36.0f, m.strength = 1.0f;
	for (int i = 0; i < s_trailCount && s_pressCount < kMostPress; i++) {
		const TrailPoint& p = s_trail[i];
		const s32 age       = now - p.at;
		if (age < 0 || age >= kTrailMs)
			continue;
		Press& q = s_press[s_pressCount++];
		q.x = p.x, q.y = p.y, q.z = p.z, q.r = 32.0f;
		const f32 left = 1.0f - (f32)age / kTrailMs;
		q.strength     = left * left * 0.9f;
	}
	TModelWaterManager* water = gpModelWaterManager;
	const int count           = water ? water->getParticleCount() : 0;
	for (int i = 0; i < count && s_pressCount < kMostPress; i++) {
		if ((water->mParticleFlagSOA[i] & 0xf) != 2)  // landed: a puddle
			continue;
		const JGeometry::TVec3<f32>& p = water->mParticlePositionSOA[i];
		const f32 dx = p.x - mx, dz = p.z - mz;
		if (dx * dx + dz * dz > 1000.0f * 1000.0f)
			continue;
		Press& q = s_press[s_pressCount++];
		q.x = p.x, q.y = p.y, q.z = p.z, q.r = 28.0f, q.strength = 0.75f;
	}
}

// ------------------------------------------------------------------ each tick
void afterMovement()
{
	int world          = -1;
	const Style* want  = gpMarioPos && gpMap ? wantedStyle(&world) : 0;
	const int area     = gpApplication.mCurrArea.getStage();
	if (want != s_style || port_grass() != s_styleSetting || area != s_styleArea) {
		forgetTiles();
		s_style        = want;
		s_styleSetting = port_grass();
		s_styleArea    = area;
		s_trailCount = s_trailNext = 0;
		if (want)
			fprintf(stderr, "[grass] %s: %s grass\n",
			        world >= 0 && world < 10 ? kWorldNames[world] : "this stage", want->name);
		else
			fprintf(stderr, "[grass] off\n");
	}
	if (!s_style || !gpMarioPos)
		return;
	const f32 mx = gpMarioPos->x, my = gpMarioPos->y, mz = gpMarioPos->z;
	// Mario's way, a point each time he has moved 14 units
	const TrailPoint* lastPoint = s_trailCount ? &s_trail[(s_trailNext + kTrail - 1) % kTrail] : 0;
	const f32 ldx = lastPoint ? mx - lastPoint->x : 1e9f, ldz = lastPoint ? mz - lastPoint->z : 0.0f;
	if (ldx * ldx + ldz * ldz > 14.0f * 14.0f) {
		TrailPoint& p = s_trail[s_trailNext];
		p.x = mx, p.y = my, p.z = mz, p.at = nowMs();
		s_trailNext = (s_trailNext + 1) % kTrail;
		if (s_trailCount < kTrail)
			s_trailCount++;
	}
	// the tiles round Mario, nearest first, two a tick
	const f32 reach = s_style->reach + kTile;
	const int tx0 = (int)floorf((mx - reach) / kTile), tx1 = (int)floorf((mx + reach) / kTile);
	const int tz0 = (int)floorf((mz - reach) / kTile), tz1 = (int)floorf((mz + reach) / kTile);
	for (int made = 0; made < 2;) {
		int bx = 0, bz = 0;
		f32 best = 1e30f;
		for (int tz = tz0; tz <= tz1; tz++)
			for (int tx = tx0; tx <= tx1; tx++) {
				const f32 dx = (tx + 0.5f) * kTile - mx, dz = (tz + 0.5f) * kTile - mz;
				const f32 d = dx * dx + dz * dz;
				if (d < best && d < reach * reach && !findTile(tx, tz))
					best = d, bx = tx, bz = tz;
			}
		if (best >= 1e30f)
			break;
		Tile* slot = slotFor(mx, mz);
		if (!slot)
			break;
		makeTile(slot, bx, bz);
		made++;
	}
}

void stageSetup()
{
	forgetTiles();
	s_styleArea  = -1;
	s_trailCount = s_trailNext = 0;
}

PortExtension s_extension;

struct Register {
	Register()
	{
		memset(&s_extension, 0, sizeof s_extension);
		s_extension.name          = "grass";
		s_extension.stageSetup    = stageSetup;
		s_extension.afterMovement = afterMovement;
		port_ext_register(&s_extension);
	}
} s_register;

// ------------------------------------------------------------------ drawing
struct Vert {
	f32 x, y, z;
	u8 r, g, b;
};
const int kChunk = 65535 / 9 * 9;
Vert s_verts[kChunk];
int s_vertCount;

void flushVerts()
{
	if (!s_vertCount)
		return;
	GXBegin(GX_TRIANGLES, GX_VTXFMT0, (u16)s_vertCount);
	for (int i = 0; i < s_vertCount; i++) {
		const Vert& v = s_verts[i];
		GXPosition3f32(v.x, v.y, v.z);
		GXColor4u8(v.r, v.g, v.b, 255);
	}
	GXEnd();
	s_vertCount = 0;
}

inline void put(f32 x, f32 y, f32 z, const f32* c)
{
	Vert& v = s_verts[s_vertCount++];
	v.x = x, v.y = y, v.z = z;
	v.r = (u8)(c[0] > 255.0f ? 255.0f : c[0]);
	v.g = (u8)(c[1] > 255.0f ? 255.0f : c[1]);
	v.b = (u8)(c[2] > 255.0f ? 255.0f : c[2]);
}

} // namespace

void port_grass_draw(float (*view)[4])
{
	if (!s_style || !gpMarioPos || !view || !s_bladeTotal)
		return;
	const Style& st = *s_style;
	const f32 mx = gpMarioPos->x, my = gpMarioPos->y, mz = gpMarioPos->z;
	gatherPress(mx, my, mz);

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
	GXSetNumTexGens(0);
	GXSetNumTevStages(1);
	GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
	GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP);
	GXSetZCompLoc(GX_TRUE);
	GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
	GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
	GXSetColorUpdate(GX_TRUE);
	GXSetAlphaUpdate(GX_FALSE);
	GXSetDstAlpha(GX_FALSE, 0);

	const f32 t     = (f32)nowMs() * 0.001f;
	const f32 reach = st.reach;
	// the breeze: from one way, its strength rolling across the stage
	const f32 windX = 0.8f, windZ = 0.6f;
	Press close[64];  // (not `near`: windows.h defines it as nothing)
	for (int i = 0; i < kMostTiles; i++) {
		const Tile& tile = s_tiles[i];
		if (!tile.used || !tile.count)
			continue;
		const f32 cx = (tile.tx + 0.5f) * kTile, cz = (tile.tz + 0.5f) * kTile;
		const f32 tdx = cx - mx, tdz = cz - mz;
		if (tdx * tdx + tdz * tdz > (reach + kTile) * (reach + kTile))
			continue;
		// behind the camera (view space z > 0 is behind it), with a tile's margin
		const f32 cy = (tile.minY + tile.maxY) * 0.5f;
		const f32 vz = view[2][0] * cx + view[2][1] * cy + view[2][2] * cz + view[2][3];
		if (vz > kTile)
			continue;
		// the presses that reach into this tile
		int nn = 0;
		for (int k = 0; k < s_pressCount && nn < 64; k++) {
			const Press& p = s_press[k];
			if (p.x + p.r < tile.tx * kTile || p.x - p.r > (tile.tx + 1) * kTile || p.z + p.r < tile.tz * kTile
			    || p.z - p.r > (tile.tz + 1) * kTile || p.y < tile.minY - 60.0f || p.y > tile.maxY + 60.0f)
				continue;
			close[nn++] = p;
		}
		for (int k = 0; k < tile.count; k++) {
			const Blade& b = tile.blades[k];
			const f32 dx = b.x - mx, dz = b.z - mz;
			const f32 d  = sqrtf(dx * dx + dz * dz);
			if (d > reach)
				continue;
			// fewer farther out (whole to 45% of the way, then thinning), shorter at the edge
			const f32 out = d / reach;
			if (out > 0.45f && (f32)b.keep > 255.0f * (1.0f - (out - 0.45f) * 1.2f))
				continue;
			f32 h = b.h * (out > 0.8f ? (1.0f - out) * 5.0f : 1.0f);
			if (h < 0.5f)
				continue;
			// the breeze
			const f32 ph   = (f32)b.phase * (6.2831853f / 65536.0f);
			const f32 gust = 0.6f + 0.4f * sinf(t * 0.7f + (b.x * windX + b.z * windZ) * 0.0035f);
			const f32 sway = (sinf(t * 2.1f + ph + (b.x + b.z) * 0.01f) * 0.6f + 0.4f) * gust * st.wind * h;
			f32 tipX = windX * sway, tipZ = windZ * sway, tipDown = 0.0f;
			// pressed: away from what presses, and down
			for (int p = 0; p < nn; p++) {
				const Press& q = close[p];
				const f32 px = b.x - q.x, pz = b.z - q.z;
				const f32 pd2 = px * px + pz * pz;
				if (pd2 >= q.r * q.r || fabsf(q.y - b.y) > 60.0f)
					continue;
				const f32 pd  = sqrtf(pd2);
				const f32 amt = (1.0f - pd / q.r) * q.strength;
				const f32 inv = pd > 0.01f ? 1.0f / pd : 0.0f;
				tipX += px * inv * amt * h * 0.9f;
				tipZ += pz * inv * amt * h * 0.9f;
				tipDown += amt * h * 0.55f;
			}
			if (tipDown > h * 0.8f)
				tipDown = h * 0.8f;
			// colour: darker at the root, lighter at the tip; each blade a little its own; the
			// mower's stripes on cut lawns
			f32 shade = 0.86f + 0.28f * (b.shade / 255.0f);
			if (st.stripes)
				shade *= ((int)floorf((b.x * 0.6f + b.z * 0.8f) / 110.0f) & 1) ? 1.07f : 0.94f;
			if (st.clumps && (b.shade & 7) == 0)  // wild grass: a few dry, yellowed blades
				shade *= 1.12f;
			const f32 base[3] = {st.base[0] * shade, st.base[1] * shade, st.base[2] * shade};
			f32 tip[3]        = {st.tip[0] * shade, st.tip[1] * shade, st.tip[2] * shade};
			if (st.clumps && (b.shade & 7) == 0)
				tip[0] *= 1.35f, tip[2] *= 0.85f;
			const f32 w  = st.width * 0.5f;
			const f32 ax = b.dirX * w, az = b.dirZ * w;
			const f32 topY = b.y + h - tipDown;
			if (s_vertCount + 9 > kChunk)
				flushVerts();
			if (st.segments == 1 || out > 0.55f) {
				put(b.x - ax, b.y, b.z - az, base);
				put(b.x + ax, b.y, b.z + az, base);
				put(b.x + tipX, topY, b.z + tipZ, tip);
			} else {
				// two segments: the middle bends a third as far as the tip, so the blade curves
				const f32 midX = b.x + tipX * 0.3f, midZ = b.z + tipZ * 0.3f;
				const f32 midY = b.y + (h - tipDown) * 0.55f;
				const f32 mid[3] = {(base[0] + tip[0]) * 0.5f, (base[1] + tip[1]) * 0.5f, (base[2] + tip[2]) * 0.5f};
				const f32 mx2 = ax * 0.65f, mz2 = az * 0.65f;
				put(b.x - ax, b.y, b.z - az, base);
				put(b.x + ax, b.y, b.z + az, base);
				put(midX - mx2, midY, midZ - mz2, mid);
				put(b.x + ax, b.y, b.z + az, base);
				put(midX + mx2, midY, midZ + mz2, mid);
				put(midX - mx2, midY, midZ - mz2, mid);
				put(midX - mx2, midY, midZ - mz2, mid);
				put(midX + mx2, midY, midZ + mz2, mid);
				put(b.x + tipX, topY, b.z + tipZ, tip);
			}
		}
	}
	flushVerts();
}
