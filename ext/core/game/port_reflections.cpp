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
#include <Map/MapData.hpp>
#include <JSystem/J3D/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/JDrama/JDRNameRefGen.hpp>
#include <JSystem/JDrama/JDRViewObjPtrList.hpp>
#include <JSystem/JKernel/JKRHeap.hpp>
#include <M3DUtil/MActor.hpp>
#include <M3DUtil/SDLModel.hpp>
#include <MoveBG/Item.hpp>
#include <MoveBG/MapObjBase.hpp>
#include <Player/MarioAccess.hpp>
#include <Player/ModelWaterManager.hpp>
#include <Strategic/LiveActor.hpp>
#include <Strategic/LiveManager.hpp>
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

// How far below the reflecting floor an object may stand and still be reflected: the floor is a
// plane under Mario, and what stands on it sits a little into it.
const f32 kBelow = 20.0f;

// The reflections seen near Mario are logged, each once a stage (up to kMostLogged): whose
// copy it is (the game's or ours), what of, and where, so that one that should not be there
// can be found. kLogNear: how near Mario, across the ground.
const int kMostLogged = 64;
const f32 kLogNear    = 1500.0f;

const int kMostModels = 2048;  // the models that have a copy, the game's and ours

class TPortMirrorActor : public TMirrorActor {
public:
	explicit TPortMirrorActor(TLiveActor* owner)
	    : TMirrorActor("port puddle reflection")
	    , mOwner(owner)
	    , mMapObj(dynamic_cast<TMapObjBase*>(owner))
	    , mNozzleBox(dynamic_cast<TNozzleBox*>(owner))
	{
	}

	virtual void perform(u32 cue, JDrama::TGraphics* graphics)
	{
		if (shown())
			TMirrorActor::perform(cue, graphics);
	}

	TLiveActor* owner() const { return mOwner; }

	bool shown() const
	{
		if (!port_puddle_reflections() || !gpModelWaterManager || !gpModelWaterManager->getParticleCount())
			return false;
		if (mOwner->checkLiveFlag(LIVE_FLAG_DEAD | LIVE_FLAG_HIDDEN | LIVE_FLAG_CLIPPED_OUT))
			return false;
		if (mOwner->checkLiveFlag(LIVE_FLAG_UNK200) || !drawn())
			return false;
		if (!gpMarioPos)
			return false;
		const f32 dx = mOwner->mPosition.x - gpMarioPos->x;
		const f32 dz = mOwner->mPosition.z - gpMarioPos->z;
		if (dx * dx + dz * dz >= kNear * kNear)
			return false;
		// Reflections are mirrored about the floor under Mario (TMirrorModelManager), so what is
		// below it would come up mirrored: objects a stage keeps under its floor until they are
		// wanted, those on lower ground. Only what is at the floor or above it is reflected.
		const TBGCheckData* floor = gpMarioGroundPlane ? *gpMarioGroundPlane : nullptr;
		if (!floor || !floor->isLegal())
			return false;
		// The copy is drawn where the model's joints are, and that is not always where its actor
		// says it is: an object put away under the floor can keep its place above it, or its model
		// can be left where it last was. So each of them must be at the floor or above it: the
		// actor, the model's own place and its first joint, which the copy is drawn from.
		const JGeometry::TVec3<f32>& n = floor->getNormal();
		const f32 d                    = floor->getPlaneDistance();
		J3DModel* model                = const_cast<TPortMirrorActor*>(this)->getUnk10();
		if (!above(n, d, mOwner->mPosition.x, mOwner->mPosition.y, mOwner->mPosition.z))
			return false;
		if (model) {
			MtxPtr base = model->getBaseTRMtx();
			MtxPtr root = model->getAnmMtx(0);
			if (!above(n, d, base[0][3], base[1][3], base[2][3])
			    || !above(n, d, root[0][3], root[1][3], root[2][3]))
				return false;
		}
		return true;
	}

private:
	// Whether the object itself is drawn now. Besides being hidden or clipped out (the flags
	// above), an object waiting for its moment can be put to sleep (a map object: drawn no more
	// until it is woken) or have its model's parts switched off,
	// while it stays where it will appear. Its reflection follows suit.
	bool drawn() const
	{
		if (mMapObj) {
			if (mMapObj->checkLiveFlag(LIVE_FLAG_UNK4000))  // asleep (TMapObjBase::sleep)
				return false;
		}
		// A nozzle box whose nozzle Mario has not got back yet stands in its place as a faint
		// ghost (TNozzleBox::makeModelInvalid: an animation of its look). A copy has the model's
		// shape but not that look, so it would show a solid box: not reflected until it is real.
		if (mNozzleBox && !mNozzleBox->unk15C)
			return false;
		J3DModel* model = const_cast<TPortMirrorActor*>(this)->getUnk10();
		if (model && model->getModelData() && model->getModelData()->getShapeNum() > 0
		    && !model->getShapePacket(0)->isVisible())
			return false;  // its parts switched off (SMS_HideAllShapePacket)
		return true;
	}

	static bool above(const JGeometry::TVec3<f32>& n, f32 d, f32 x, f32 y, f32 z)
	{
		return n.x * x + n.y * y + n.z * z + d >= -kBelow;
	}

	TLiveActor* mOwner;
	TMapObjBase* mMapObj;
	TNozzleBox* mNozzleBox;
};

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

const TMirrorActor* s_logged[kMostLogged];
int s_loggedCount;

bool logged(const TMirrorActor* mirror)
{
	for (int i = 0; i < s_loggedCount; i++)
		if (s_logged[i] == mirror)
			return true;
	return false;
}

// Each copy in the mirror scene that is on and near Mario, logged once a stage.
void logReflections()
{
	if (s_loggedCount >= kMostLogged || !gpMarioPos)
		return;
	JDrama::TViewObjPtrListT<JDrama::TViewObj>* scene
	    = JDrama::TNameRefGen::search<JDrama::TViewObjPtrListT<JDrama::TViewObj> >(kMirrorScene);
	if (!scene)
		return;
	JGadget::TList_pointer<JDrama::TViewObj*>& children = scene->getChildren();
	for (JGadget::TList_pointer<JDrama::TViewObj*>::iterator it = children.begin();
	     it != children.end() && s_loggedCount < kMostLogged; ++it) {
		TMirrorActor* mirror = dynamic_cast<TMirrorActor*>(*it);
		if (!mirror || !mirror->getUnk10() || logged(mirror))
			continue;
		TPortMirrorActor* ours = dynamic_cast<TPortMirrorActor*>(mirror);
		if (ours ? !ours->shown() : !mirror->unk18)
			continue;
		MtxPtr root  = mirror->getUnk10()->getAnmMtx(0);
		const f32 dx = root[0][3] - gpMarioPos->x;
		const f32 dz = root[2][3] - gpMarioPos->z;
		if (dx * dx + dz * dz >= kLogNear * kLogNear)
			continue;
		s_logged[s_loggedCount++] = mirror;
		const char* name = ours ? ours->owner()->getName() : mirror->getName();
		char hex[64]     = "";
		for (int i = 0; name && name[i] && i < 24; i++)
			snprintf(hex + i * 2, 3, "%02X", (unsigned char)name[i]);
		fprintf(stderr,
		        "[puddles] reflection near Mario: %s copy of \"%s\" (%s), flags 0x%02X, at %.0f %.0f %.0f"
		        " (Mario at %.0f %.0f %.0f)\n",
		        ours ? "our" : "the game's", name ? name : "?", hex, (unsigned)mirror->unk1A, root[0][3],
		        root[1][3], root[2][3], gpMarioPos->x, gpMarioPos->y, gpMarioPos->z);
	}
}

void stageSetup()
{
	if (!port_puddle_reflections() || !gpConductor)
		return;
	JDrama::TViewObjPtrListT<JDrama::TViewObj>* scene
	    = JDrama::TNameRefGen::search<JDrama::TViewObjPtrListT<JDrama::TViewObj> >(kMirrorScene);
	if (!scene)
		return;

	s_loggedCount = 0;

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
// strongly each reflects is set on the settings menu's Other page (port_puddles.h).
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
void afterMovement()
{
	static int s_tick;
	if (port_puddle_reflections() && gpModelWaterManager && gpModelWaterManager->getParticleCount()
	    && ++s_tick % 30 == 0)
		logReflections();

	static unsigned s_floorLogged = 0xFFFFFFFFu, s_seen = 0xFFFFFFFFu;
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
	if (++s_steady != 15 || key == s_floorLogged)  // a moment: 15 ticks
		return;
	s_floorLogged = key;
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
		port_ext_register(&s_extension);
	}
} s_register;

} // namespace

extern "C" unsigned port_puddle_surface_alpha(unsigned type, unsigned material)
{
	return surfaceAlpha(type, material);
}
