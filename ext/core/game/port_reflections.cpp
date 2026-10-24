// Puddle Reflections (port_puddles.h, the settings menu's Graphics page): characters and objects
// reflected in FLUDD's puddles, not only Mario. Game code, with the game's flags and headers.
//
// The game reflects in puddles what has a TMirrorActor that may be "in the mirror" with no mirror
// near (flags 4 and 0x20: Mario, FLUDD, his cap, Yoshi): a copy of its model, drawn mirrored about
// the floor under Mario, through the puddles. As a stage is set up (port_ext.h stageSetup, the
// stage's heap current), each of its actors with a model gets such a copy, unless it has one. A
// copy is drawn only while there are puddles, its actor is near Mario and is neither dead, hidden
// nor clipped out (no ghost of an enemy beaten or a coin taken), and the setting is on.
#include "port_ext.h"
#include "port_puddles.h"

#include <Enemy/Conductor.hpp>
#include <Enemy/Enemy.hpp>
#include <MoveBG/Item.hpp>
#include <MoveBG/MapObjBall.hpp>
#include <NPC/NpcBase.hpp>
#include <Map/MapData.hpp>
#include <JSystem/J3D/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/JDrama/JDRNameRefGen.hpp>
#include <JSystem/JDrama/JDRViewObjPtrList.hpp>
#include <JSystem/JKernel/JKRHeap.hpp>
#include <M3DUtil/MActor.hpp>
#include <M3DUtil/SDLModel.hpp>
#include <Player/MarioAccess.hpp>
#include <Player/ModelWaterManager.hpp>
#include <Strategic/LiveActor.hpp>
#include <Strategic/LiveManager.hpp>
#include <Map/MapMirror.hpp>
#include <Strategic/MirrorActor.hpp>

#include <stdio.h>
#include <string.h>

namespace {

// "鏡シーン" (the mirror scene) in Shift-JIS, as the game names it, written as bytes so that it is
// the same with every compiler's execution character set.
const char kMirrorScene[] = "\x8B\xBE\x83\x56\x81\x5B\x83\x93";

// Room left on the stage's heap: the port's stages have tens of MB spare, and multiplayer's
// puppets and spawns want 1 to 2 MB of it after this.
const s32 kHeapRoom = 4 * 1024 * 1024;

// As far from Mario as a reflection is drawn: puddles go 5000 units from him (TModelWaterManager).
const f32 kNear = 5000.0f;

const int kMostModels = 2048;  // the models that have a copy, the game's and ours

class TPortMirrorActor : public TMirrorActor {
public:
	explicit TPortMirrorActor(TLiveActor* owner)
	    : TMirrorActor("port puddle reflection")
	    , mOwner(owner)
	    , mKind(kindOf(owner))
	{
	}

	virtual void perform(u32 cue, JDrama::TGraphics* graphics)
	{
		if (shown())
			TMirrorActor::perform(cue, graphics);
	}

private:
	bool shown() const
	{
		if (!port_puddle_reflections() || !gpModelWaterManager
		    || (!gpModelWaterManager->getParticleCount() && !port_water_mirror_active()))
			return false;
		if (!port_reflect_kind(mKind))
			return false;
		if (mOwner->checkLiveFlag(LIVE_FLAG_DEAD | LIVE_FLAG_HIDDEN | LIVE_FLAG_CLIPPED_OUT))
			return false;
		if (!gpMarioPos)
			return false;
		const f32 dx = mOwner->mPosition.x - gpMarioPos->x;
		const f32 dz = mOwner->mPosition.z - gpMarioPos->z;
		return dx * dx + dz * dz < kNear * kNear;
	}

	// Which of the settings menu's Reflect switches the actor answers to.
	static int kindOf(TLiveActor* actor)
	{
		if (dynamic_cast<TBaseNPC*>(actor))  // before enemies: an NPC is a TSpineEnemy too
			return PORT_REFLECT_CHARACTERS;
		if (dynamic_cast<TSpineEnemy*>(actor))
			return PORT_REFLECT_ENEMIES;
		if (dynamic_cast<TItem*>(actor) || dynamic_cast<TResetFruit*>(actor))
			return PORT_REFLECT_ITEMS;
		return PORT_REFLECT_OBJECTS;
	}

	TLiveActor* mOwner;
	int mKind;
};

// The game's own copies of Mario and Yoshi (Mario, his hands, cap and FLUDD, flag 4; Yoshi and
// his tongue, flags 4 and 0x20), with their flags as the game set them. Reflect Mario and Yoshi
// off, each is told not to be in the mirror (flag 1) while no real mirror is near (the hotel's
// keep him), and given back its flags when it is on.
const int kMostPlayerCopies = 64;
TMirrorActor* s_playerCopies[kMostPlayerCopies];
u16 s_playerFlags[kMostPlayerCopies];
int s_playerCount;
bool s_playerHidden;

const J3DModel* s_models[kMostModels];
int s_modelCount;

bool known(const J3DModel* model)
{
	for (int i = 0; i < s_modelCount; i++)
		if (s_models[i] == model)
			return true;
	return false;
}

void remember(const J3DModel* model)
{
	if (s_modelCount < kMostModels)
		s_models[s_modelCount++] = model;
}

void rememberPlayerCopies(JDrama::TViewObjPtrListT<JDrama::TViewObj>* scene)
{
	s_playerCount  = 0;
	s_playerHidden = false;
	JGadget::TList_pointer<JDrama::TViewObj*>& children = scene->getChildren();
	for (JGadget::TList_pointer<JDrama::TViewObj*>::iterator it = children.begin();
	     it != children.end() && s_playerCount < kMostPlayerCopies; ++it) {
		TMirrorActor* mirror = dynamic_cast<TMirrorActor*>(*it);
		if (!mirror || dynamic_cast<TPortMirrorActor*>(mirror) || (mirror->unk1A & 1)
		    || !(mirror->unk1A & (4 | 0x20)))
			continue;
		s_playerCopies[s_playerCount] = mirror;
		s_playerFlags[s_playerCount]  = mirror->unk1A;
		s_playerCount++;
	}
}

void showPlayerCopies()
{
	const bool hide = !port_reflect_kind(PORT_REFLECT_MARIO) && gpMirrorModelManager
	                  && !gpMirrorModelManager->isUnk18Present();
	if (hide == s_playerHidden)
		return;
	s_playerHidden = hide;
	for (int i = 0; i < s_playerCount; i++)
		s_playerCopies[i]->unk1A = (u16)(hide ? s_playerFlags[i] | 1 : s_playerFlags[i]);
}

// Leaving the stage: its copies go with it.
void moveStage() { s_playerCount = 0; }

void stageSetup()
{
	JDrama::TViewObjPtrListT<JDrama::TViewObj>* scene
	    = JDrama::TNameRefGen::search<JDrama::TViewObjPtrListT<JDrama::TViewObj> >(kMirrorScene);
	if (!scene)
		return;
	rememberPlayerCopies(scene);
	if (!port_puddle_reflections() || !gpConductor)
		return;

	// the models the game gave a copy already (Mario, FLUDD, Yoshi, coins...)
	s_modelCount = 0;
	JGadget::TList_pointer<JDrama::TViewObj*>& children = scene->getChildren();
	for (JGadget::TList_pointer<JDrama::TViewObj*>::iterator it = children.begin(); it != children.end(); ++it)
		if (TMirrorActor* mirror = dynamic_cast<TMirrorActor*>(*it))
			remember(mirror->getUnk10());

	JKRHeap* heap   = JKRHeap::getCurrentHeap();
	const s32 start = heap ? heap->getTotalFreeSize() : 0;
	int made        = 0;
	bool full       = false;
	for (JGadget::TList<TLiveManager*>::iterator it = gpConductor->unk10.begin();
	     !full && it != gpConductor->unk10.end(); ++it)
		for (int i = 0; *it && i < (*it)->getObjNum(); i++) {
			TLiveActor* actor = (*it)->getObj(i);
			if (!actor || !actor->getMActor())
				continue;
			J3DModel* model = actor->getMActor()->getModel();
			if (!model || known(model))
				continue;
			if (!heap || heap->getTotalFreeSize() < kHeapRoom || s_modelCount >= kMostModels) {
				full = true;
				break;
			}
			// 0x20: in puddles, by its own place; 8: an SDLModel, copied as one
			const u16 flags = (u16)(0x20 | (dynamic_cast<SDLModel*>(model) ? 8 : 0));
			TPortMirrorActor* mirror = new TPortMirrorActor(actor);
			mirror->init(model, flags);
			remember(model);
			made++;
		}
	fprintf(stderr, "[puddles] reflections for %d characters and objects (%d KB)%s\n", made,
	        heap ? (int)((start - heap->getTotalFreeSize()) / 1024) : 0,
	        full ? "; the stage's memory is low, so not for all of them" : "");
}

// ------------------------------------------------------------------ Reflections by Surface
// The surface a puddle lies on, by its floor's type (mBGType, without the shadow and camera
// flags) and the sound material its triangle carries (unk6, what Mario's steps and bumps sound
// like). What each material number means is in the game's sound data, not its code: these were
// told apart by walking Delfino Plaza with the log below. A floor not listed counts as smooth
// stone (0x00: the plaza's tiles, and its carpets, which are pictures on the same floor). How
// strongly each reflects is set on the settings menu's Reflections page (port_puddles.h).
int surfaceOf(unsigned type, unsigned material)
{
	switch (type & 0x3FFF) {
	case BG_TYPE_SAND:
	case BG_TYPE_SEA_FLOOR:
		return PORT_PUDDLE_SAND;
	case BG_TYPE_WET_GROUND:
	case BG_TYPE_HYDROPHOBIC:
		return PORT_PUDDLE_WET;
	default:
		break;
	}
	switch (material) {
	case 0x05: return PORT_PUDDLE_SAND;
	case 0x07: return PORT_PUDDLE_WOOD;   // painted wood (the boardwalks)
	case 0x0C:                             // metal (the corrugated roofs)
	case 0x0D: return PORT_PUDDLE_METAL;  // painted metal (manhole covers)
	case 0x10: return PORT_PUDDLE_GRASS;
	case 0x12: return PORT_PUDDLE_CLAY;   // clay roof tiles
	case 0x17: return PORT_PUDDLE_CANVAS; // the bouncy awnings
	default: return PORT_PUDDLE_STONE;
	}
}

// Tenths of a percent of the game's own strength.
int floorStrength(unsigned type, unsigned material)
{
	return port_puddle_surface_strength(surfaceOf(type, material));
}

u8 surfaceAlpha(unsigned type, unsigned material)
{
	const int tenths = floorStrength(type, material);
	return (u8)((tenths < 0 ? 0 : tenths > 1000 ? 1000 : tenths) * 255 / 1000);
}

// While Reflections by Surface is on: the floor under Mario, logged each time it changes and
// stays changed a moment, so that the material numbers can be told apart by walking.
// The log: why puddles show no reflection, when it is not this code's doing. The game draws
// none while Mario is in a real mirror's room (TMirrorModelManager: the mirror's plane, not the
// floor, is reflected about then), nor while the floor under him is none (in the air off a ledge,
// say). Each change of either is logged once it has lasted a moment.
void logMirrorState()
{
	static int s_state = -1, s_seen = -1, s_steady;
	if (!gpMirrorModelManager || !gpMarioGroundPlane)
		return;
	const TBGCheckData* floor = *gpMarioGroundPlane;
	const int state = gpMirrorModelManager->isUnk18Present() ? 1 : (!floor || !floor->isLegal()) ? 2 : 0;
	if (state != s_seen) {
		s_seen   = state;
		s_steady = 0;
		return;
	}
	if (++s_steady != 30 || state == s_state)
		return;
	s_state = state;
	if (state == 1)
		fprintf(stderr, "[puddles] Mario is in a real mirror's area (mirror %d): the game shows no puddle "
		        "reflections here\n", gpMirrorModelManager->getUnk18());
	else if (state == 2)
		fprintf(stderr, "[puddles] no floor under Mario: no puddle reflections\n");
	else
		fprintf(stderr, "[puddles] puddle reflections shown (no real mirror near, floor under Mario)\n");
}

void afterMovement()
{
	showPlayerCopies();
	logMirrorState();

	static unsigned s_logged = 0xFFFFFFFFu, s_seen = 0xFFFFFFFFu;
	static int s_steady;
	if (!port_puddle_surface() || !gpMarioGroundPlane || !*gpMarioGroundPlane)
		return;
	const TBGCheckData* floor = *gpMarioGroundPlane;
	if (!floor->isLegal())
		return;
	const unsigned key = (unsigned)floor->mBGType << 8 | floor->unk6;
	if (key != s_seen) {
		s_seen   = key;
		s_steady = 0;
		return;
	}
	if (++s_steady != 15 || key == s_logged)  // a moment: 15 ticks
		return;
	s_logged = key;
	const int tenths = floorStrength(floor->mBGType, floor->unk6);
	fprintf(stderr, "[puddles] Mario stands on: floor type 0x%04X, material 0x%02X (reflects %d.%d%%)\n",
	        floor->mBGType, floor->unk6, tenths / 10, tenths % 10);
}

// Filled by name: a hook added to PortExtension anywhere never shifts it.
PortExtension s_extension;

struct Register {
	Register()
	{
		memset(&s_extension, 0, sizeof s_extension);
		s_extension.name          = "reflections";
		s_extension.stageSetup    = stageSetup;
		s_extension.afterMovement = afterMovement;
		s_extension.moveStage     = moveStage;
		port_ext_register(&s_extension);
	}
} s_register;

} // namespace

extern "C" int port_puddle_surface_kind(unsigned type, unsigned material) { return surfaceOf(type, material); }

extern "C" unsigned port_puddle_surface_alpha(unsigned type, unsigned material)
{
	return surfaceAlpha(type, material);
}
