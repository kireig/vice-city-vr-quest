#include "common.h"

#include "VrRagdoll.h"
#include "VrRagdollPhysics.h"
#include "VrRagdollImpact.h"
#include "VrRagdollExplosion.h"
#include "VrRagdollContacts.h"
#include "VrRagdollSupport.h"
#include "VrRagdollMetrics.h"
#include "VrRagdollRay.h"
#include "VrRagdollMotion.h"
#include "VrRagdollImpulse.h"
#include "VrRagdollBrake.h"
#include "VrRagdollSettings.h"
#include "VrRagdollPose.h"
#include "VrRagdollBounds.h"
#include "VrRagdollSchedule.h"
#include "VrRagdollVehicleImpact.h"
#include "VrRagdollWorld.h"
#include "ColPoint.h"
#include "SurfaceTable.h"
#include "Bones.h"
#include "Camera.h"
#include "Particle.h"
#include "Ped.h"
#include "Pools.h"
#include "RwHelper.h"
#include "Timer.h"
#include "World.h"
#include "WeaponType.h"

// Capture the interrupted skin pose into articulated particles, then rebuild
// world-space bone matrices after animation. Unsimulated nodes follow their
// nearest simulated ancestor. Contact queries and simulation work are bounded.

namespace VrRagdoll
{

enum {
	MAX_POSES = NUMPEDS,
	MAX_NODES = 40,
	// At most three solver steps for one selected body per update.
	MAX_STEPS = 3,
};

using namespace VrRagdollPhysics;

static const int simBoneTags[SIM_BONES] = {
	BONE_pelvis, BONE_spine, BONE_spine1, BONE_neck, BONE_head,
	BONE_l_clavicle, BONE_l_upperarm, BONE_l_forearm, BONE_l_hand,
	BONE_r_clavicle, BONE_r_upperarm, BONE_r_forearm, BONE_r_hand,
	BONE_l_thigh, BONE_l_calf, BONE_l_foot,
	BONE_r_thigh, BONE_r_calf, BONE_r_foot
};

// The segment whose direction carries each bone's orientation. End bones
// reuse the segment that arrives at them, so their twist follows the limb.
static const uint8 dirFrom[SIM_BONES] = {
	RD_PELVIS, RD_SPINE, RD_SPINE1, RD_NECK, RD_NECK,
	RD_LCLAV, RD_LUARM, RD_LFARM, RD_LFARM,
	RD_RCLAV, RD_RUARM, RD_RFARM, RD_RFARM,
	RD_LTHIGH, RD_LCALF, RD_LCALF,
	RD_RTHIGH, RD_RCALF, RD_RCALF
};
static const uint8 dirTo[SIM_BONES] = {
	RD_SPINE1, RD_SPINE1, RD_NECK, RD_HEAD, RD_HEAD,
	RD_LUARM, RD_LFARM, RD_LHAND, RD_LHAND,
	RD_RUARM, RD_RFARM, RD_RHAND, RD_RHAND,
	RD_LCALF, RD_LFOOT, RD_LFOOT,
	RD_RCALF, RD_RFOOT, RD_RFOOT
};


struct Slot : State {
	CPed *ped;
	VrRagdollBounds::Bounds bounds;
	bool boundsLinked;
	int linkedSectors[6];
	// A live slot rides a knockdown the ped may yet survive; it converts
	// to a death ragdoll in place or hands the body back to the
	// animations at the getup.
	bool live;
	float waitTime;
	uint32 priorityUntil;
	bool vehicleSeeded;
	VrRagdollWorld::Cache world;
	VrRagdollSupport::State support;
	float bulletCredit;
	int nodeCount;
	bool poseCached;
	RwMatrix cachedPose[MAX_NODES];
	// The pose the death interrupted, kept as the rotation reference.
	CVector deathDir[SIM_BONES];
	CVector deathRight[SIM_BONES];
	CVector deathUp[SIM_BONES];
	CVector deathAt[SIM_BONES];
	VrRagdollPose::LegFrames deathLegFrames[NUM_HIPS];
	int16 simMatrix[SIM_BONES];
	// Nodes the simulation does not carry, expressed in their driver's
	// death basis so they follow it rigidly.
	int16 passDriver[MAX_NODES];
	CVector passRight[MAX_NODES];
	CVector passUp[MAX_NODES];
	CVector passAt[MAX_NODES];
	CVector passPos[MAX_NODES];
};

// One retained state per possible ped; scheduler budgets work independently.
// Admission never depends on another body having finished falling.
static Slot slots[MAX_POSES];
enum { MAX_ANIMATED_POSES = 32 };
struct AnimatedPose {
	CPed *ped;
	RpClump *clump;
	uint32 time;
	int nodeCount;
	int16 matrix[SIM_BONES];
	CVector positions[SIM_BONES], relativeVelocity[SIM_BONES];
	bool hasVelocity;
};
static AnimatedPose animatedPoses[MAX_ANIMATED_POSES];
static bool enabled;
static int active;
static bool bulletTrace;
static int sleepingCount;
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
static int lastUpdated, lastDeferred;
static int shotHits, shotMisses, shotThaws, shotBusy;
#endif
static VrRagdollBrake::Ledger vehicleBrakeLedger;
static VrRagdollBrake::HitCache vehicleHitCache;
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
static unsigned vehicleImpactCalls, vehicleBrakeEvents;
static float lastVehicleBrakeKmh;
static float lastVehicleBeforeKmh, lastVehicleAfterKmh;
#endif
// Developer diagnostics distinguish the admission gates.
enum { DENY_OFF, DENY_KIND, DENY_RANGE, DENY_SLOTS, DENY_BONES,
	DENY_COUNT };
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
static int taken;
static int denied[DENY_COUNT];
#endif

static const float TAKE_RANGE_SQ = 30.0f*30.0f;

struct AdmissionRetry {
	int handle;
	RpClump *clump;
	uint32 after;
};
static AdmissionRetry admissionRetries[MAX_POSES];
static uint32 admissionFrame = ~uint32(0);
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
static unsigned lateAdmissions, blastEvents;

static void AdmissionEvent(const char *event, unsigned count, CPed *ped, int reason)
{
#ifdef __ANDROID__
	if(count <= 4 || (count & (count-1)) == 0)
		__android_log_print(ANDROID_LOG_INFO,"QuestRagdoll",
			"event=%s count=%u reason=%d active=%d taken=%d model=%d state=%d",
			event,count,reason,active,taken,ped ? ped->GetModelIndex() : -1,
			ped ? int(ped->m_nPedState) : -1);
#else
	(void)event; (void)count; (void)ped; (void)reason;
#endif
}

#endif

static bool DenyAdmission(int reason, CPed *ped)
{
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
	++denied[reason];
	AdmissionEvent("denied",unsigned(denied[reason]),ped,reason);
#else
	(void)reason; (void)ped;
#endif
	return false;
}


void
SetEnabled(bool on)
{
	// Active bodies finish their fall either way -- dropping the override
	// mid-air would snap the corpse back to the pose the animation left.
	if(enabled == on) return;
	enabled = on;
	if(!on)
		for(int i = 0; i < MAX_ANIMATED_POSES; i++)
			animatedPoses[i].ped = nil;
}

bool
IsEnabled(void)
{
	return enabled;
}

void SetBrakePercent(int value) { VrRagdollSettings::SetBrake(value); }
int GetBrakePercent(void) { return VrRagdollSettings::Get().brakePercent; }
void SetGripPercent(int value) { VrRagdollSettings::SetGrip(value); }
int GetGripPercent(void) { return VrRagdollSettings::Get().gripPercent; }
void SetShotPercent(int value) { VrRagdollSettings::SetShot(value); }
int GetShotPercent(void) { return VrRagdollSettings::Get().shotPercent; }
void SetWeightPercent(int value) { VrRagdollSettings::SetWeight(value); }
int GetWeightPercent(void) { return VrRagdollSettings::Get().weightPercent; }

static void SeedVelocity(Slot *slot, CPed *ped);

static Slot*
FindSlot(CPed *ped)
{
	for(int i = 0; i < MAX_POSES; i++)
		if(slots[i].ped == ped)
			return &slots[i];
	return nil;
}

bool Owns(CPed *ped) { return active != 0 && ped != nil && FindSlot(ped) != nil; }

static VrRagdollSupport::Transform Presentation(const Slot *slot)
{
	if(slot->support.handle<0 || slot->support.age>0.1f) return VrRagdollSupport::Transform();
	CVehicle *car=CPools::GetVehicle(slot->support.handle);
	if(!car || !car->IsCar() || !car->m_rwObject || car->bRemoveFromWorld)
		return VrRagdollSupport::Transform();
	VrRagdollVehicle::Pose pose;
	pose.origin=car->GetPosition();pose.right=car->GetRight();pose.forward=car->GetForward();pose.up=car->GetUp();
	return VrRagdollSupport::Build(slot->support,pose);
}

static void ApplyPresentation(RwMatrix *mats,int count,const VrRagdollSupport::Transform &presentation)
{
	if(!presentation.active) return;
	for(int i=0;i<count;++i){
		RwMatrix &m=mats[i];
		m.pos=presentation.Point(m.pos);m.right=presentation.Direction(m.right);
		m.up=presentation.Direction(m.up);m.at=presentation.Direction(m.at);
	}
}

bool GetBounds(CPed *ped, CVector &centre, float &radius)
{
	if(ped == nil || active == 0) return false;
	const Slot *slot = FindSlot(ped);
	if(slot == nil) return false;
	centre = Presentation(slot).Point(slot->bounds.centre);
	radius = slot->bounds.radius+VrRagdollSupport::BoundsPadding(slot->support,
		slot->bounds.radius,Min(0.1f,CTimer::GetTimeStepInSeconds()));
	return true;
}

bool GetClumpBounds(void *clump, CVector &centre, float &radius)
{
	if(clump == nil || active == 0) return false;
	for(int i = 0; i < MAX_POSES; i++){
		const Slot &slot = slots[i];
		if(slot.ped == nil || slot.ped->GetClump() != clump) continue;
		centre = Presentation(&slot).Point(slot.bounds.centre);
		radius = slot.bounds.radius+VrRagdollSupport::BoundsPadding(slot.support,
			slot.bounds.radius,Min(0.1f,CTimer::GetTimeStepInSeconds()));
		return true;
	}
	return false;
}

static void RefreshBounds(Slot *slot)
{
	slot->bounds = VrRagdollBounds::Calculate(slot->pos, radius, SIM_BONES);
}

// Only called at the beginning of CWorld::Process, outside sector traversal.
// Capture/hit callbacks may run inside collision iteration and must defer this.
static void LinkBounds(Slot *slot)
{
	CPed *ped = slot->ped;
	if(ped->m_entryInfoList.first == nil){ slot->boundsLinked = false; return; }
	CVector c;
	// Sector membership is updated before vehicle physics. Cover both the
	// solved pose and its short presentation transport to this frame's car.
	float r;
	GetBounds(ped,c,r);
	const int sectors[6] = {
		CWorld::GetClampedSectorIndexX(c.x-r), CWorld::GetClampedSectorIndexX(c.x+r),
		CWorld::GetClampedSectorIndexX(c.x), CWorld::GetClampedSectorIndexY(c.y-r),
		CWorld::GetClampedSectorIndexY(c.y+r), CWorld::GetClampedSectorIndexY(c.y)
	};
	bool changed = !slot->boundsLinked;
	for(int i = 0; i < 6; i++) changed |= sectors[i] != slot->linkedSectors[i];
	if(!changed) return;
	ped->RemoveAndAdd();
	for(int i = 0; i < 6; i++) slot->linkedSectors[i] = sectors[i];
	slot->boundsLinked = true;
}

bool SetBulletTrace(bool on) { const bool old = bulletTrace; bulletTrace = on; return old; }
bool SkipBulletCollision(CPed *ped) { return bulletTrace && Owns(ped); }

static Slot *AcquireSlot(void)
{
	for(int i = 0; i < MAX_POSES; i++)
		if(slots[i].ped == nil) return &slots[i];
	return nil;
}

static Slot *Thaw(Slot *slot)
{
	// The pose stays with its owner; waking never waits for a free solver slot.
	if(slot) slot->priorityUntil = CTimer::GetTimeInMilliseconds()+350;
	return slot;
}

static bool
StartRagdoll(CPed *ped, bool live)
{
	if(!enabled){
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
		denied[DENY_OFF]++;
#endif
		return false;
	}
	if(ped == nil || ped->IsPlayer() || ped->bInVehicle ||
	   ped->bIsInWater || ped->CharCreatedBy != RANDOM_CHAR ||
	   ped->GetClump() == nil){
		return DenyAdmission(DENY_KIND,ped);
	}
	if((ped->GetPosition() - TheCamera.GetPosition()).MagnitudeSqr() >
	   TAKE_RANGE_SQ){
		return DenyAdmission(DENY_RANGE,ped);
	}
	Slot *slot = FindSlot(ped);
	if(slot){
		// Keep the already articulated motion through repeated knockdown and
		// death callbacks. Physical contacts supply new momentum at their hit
		// points; native canned launch velocities must not reseed every joint.
		if(live){
			slot = Thaw(slot);
			Wake(slot);
			slot->poseCached = false;
		}else
			slot->live = false;
		return true;
	}
	slot = AcquireSlot();
	if(slot == nil){
		return DenyAdmission(DENY_SLOTS,ped);
	}

	RpHAnimHierarchy *hier = GetAnimHierarchyFromSkinClump(ped->GetClump());
	if(hier == nil || hier->numNodes > MAX_NODES){
		return DenyAdmission(DENY_BONES,ped);
	}
	RwMatrix *mats = RpHAnimHierarchyGetMatrixArray(hier);

	int simOfNode[MAX_NODES];
	for(int i = 0; i < hier->numNodes; i++)
		simOfNode[i] = -1;
	for(int i = 0; i < SIM_BONES; i++){
		int idx = RpHAnimIDGetIndex(hier, simBoneTags[i]);
		if(idx < 0 || idx >= hier->numNodes){
			return DenyAdmission(DENY_BONES,ped);
		}
		slot->simMatrix[i] = idx;
		simOfNode[idx] = i;
		slot->pos[i] = mats[idx].pos;
		slot->deathRight[i] = mats[idx].right;
		slot->deathUp[i] = mats[idx].up;
		slot->deathAt[i] = mats[idx].at;
	}

	Initialize(slot, slot->deathRight[RD_PELVIS], slot->deathAt[RD_PELVIS]);
	SeedVelocity(slot, ped);

	for(int i = 0; i < SIM_BONES; i++){
		CVector dir = slot->pos[dirTo[i]] - slot->pos[dirFrom[i]];
		float len = dir.Magnitude();
		slot->deathDir[i] = len > 1e-5f ?
			dir*(1.0f/len) : CVector(0.0f, 0.0f, 1.0f);
	}
	for(int i = 0; i < NUM_HIPS; i++){
		const Bend &knee = slot->bends[i];
		slot->deathLegFrames[i] = VrRagdollPose::MakeLegFrames(
			slot->pos[knee.b]-slot->pos[knee.a], slot->pos[knee.c]-slot->pos[knee.b],
			World(slot->pelvisFrame,knee.localUpper), World(slot->pelvisFrame,knee.localBend));
	}

	// Reconstruct each node's parent the way updateMatrices walks the
	// tree, then park every unsimulated node on its nearest simulated
	// ancestor, expressed in that driver's death basis.
	int parentOf[MAX_NODES];
	int stack[MAX_NODES + 1];
	int sp = 0, parent = -1;
	stack[sp++] = parent;
	for(int i = 0; i < hier->numNodes; i++){
		parentOf[i] = parent;
		int flags = hier->nodeInfo[i].flags;
		if(flags & rw::HAnimHierarchy::PUSH && sp <= MAX_NODES)
			stack[sp++] = parent;
		parent = i;
		if(flags & rw::HAnimHierarchy::POP && sp > 0)
			parent = stack[--sp];
	}
	for(int i = 0; i < hier->numNodes; i++){
		slot->passDriver[i] = -1;
		if(simOfNode[i] >= 0)
			continue;
		int up = parentOf[i];
		while(up >= 0 && simOfNode[up] < 0)
			up = parentOf[up];
		int driver = up >= 0 ? simOfNode[up] : RD_PELVIS;
		slot->passDriver[i] = driver;
		const CVector dr = slot->deathRight[driver];
		const CVector du = slot->deathUp[driver];
		const CVector da = slot->deathAt[driver];
		const CVector rel = CVector(mats[i].pos) - slot->pos[driver];
		slot->passPos[i] = CVector(DotProduct(rel, dr),
			DotProduct(rel, du), DotProduct(rel, da));
		const CVector nr = mats[i].right, nu = mats[i].up,
			na = mats[i].at;
		slot->passRight[i] = CVector(DotProduct(nr, dr),
			DotProduct(nr, du), DotProduct(nr, da));
		slot->passUp[i] = CVector(DotProduct(nu, dr),
			DotProduct(nu, du), DotProduct(nu, da));
		slot->passAt[i] = CVector(DotProduct(na, dr),
			DotProduct(na, du), DotProduct(na, da));
	}

	bool found = false;
	const CVector &pelvis = slot->pos[RD_PELVIS];
	float ground = CWorld::FindGroundZFor3DCoord(pelvis.x, pelvis.y,
		pelvis.z + 0.5f, &found);
	slot->groundZ = found ? ground : pelvis.z - 100.0f;
	slot->live = live;
	slot->vehicleSeeded = false;
	slot->support = VrRagdollSupport::State();
	slot->world.valid = false;
	slot->waitTime = 0.0f;
	slot->priorityUntil = CTimer::GetTimeInMilliseconds()+350;
	slot->asleep = false;
	slot->sleepTime = 0.0f;
	slot->stepDebt = 0.0f;
	slot->bulletCredit = VrRagdollImpact::BURST_IMPULSE;
	slot->nodeCount = hier->numNodes;
	slot->poseCached = false;
	RefreshBounds(slot);
	slot->boundsLinked = false;
	slot->ped = ped;
	active++;
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
	taken++;
	AdmissionEvent("take",unsigned(taken),ped,-1);
#endif
	return true;
}

// Transfer translation and angular velocity as coherent rigid-body motion.
static void ObserveAnimatedPose(CPed *ped)
{
	if(!enabled || ped->IsPlayer() || ped->DyingOrDead() || ped->bInVehicle ||
	   ped->CharCreatedBy != RANDOM_CHAR || ped->GetClump() == nil ||
	   (ped->GetPosition()-TheCamera.GetPosition()).MagnitudeSqr() > TAKE_RANGE_SQ) return;
	VrRagdollMetrics::Scope captureTime(VrRagdollMetrics::ANIMATION);
	const uint32 now = CTimer::GetTimeInMilliseconds();
	AnimatedPose *sample = nil, *oldest = &animatedPoses[0];
	for(int i = 0; i < MAX_ANIMATED_POSES; i++){
		AnimatedPose *p = &animatedPoses[i];
		if(p->ped == ped){ sample = p; break; }
		if(p->ped == nil || (oldest->ped != nil && now-p->time > now-oldest->time)) oldest = p;
	}
	// Keep this frame's samples when more peds are visible than the cache holds.
	// Otherwise stable render order can evict every previous-frame pose.
	if(sample == nil && oldest->ped != nil && oldest->time == now) return;
	RpHAnimHierarchy *hier = GetAnimHierarchyFromSkinClump(ped->GetClump());
	if(hier == nil) return;
	const bool fresh = sample == nil || sample->clump != ped->GetClump() || sample->nodeCount != hier->numNodes;
	if(sample == nil) sample = oldest;
	if(fresh){
		sample->ped = nil;
		for(int i = 0; i < SIM_BONES; i++){
			const int idx = RpHAnimIDGetIndex(hier,simBoneTags[i]);
			if(idx < 0 || idx >= hier->numNodes) return;
			sample->matrix[i] = idx;
		}
		sample->ped = ped;
		sample->clump = ped->GetClump();
		sample->nodeCount = hier->numNodes;
		sample->hasVelocity = false;
	}else if(now == sample->time) return;
	RwMatrix *mats = RpHAnimHierarchyGetMatrixArray(hier);
	CVector current[SIM_BONES];
	CMatrix inverse;
	Invert(ped->GetMatrix(),inverse);
	for(int i = 0; i < SIM_BONES; i++) current[i] = inverse*CVector(mats[sample->matrix[i]].pos);
	if(!fresh){
		sample->hasVelocity = VrRagdollMotion::RelativeVelocity(current,sample->positions,
			invMass,SIM_BONES,(now-sample->time)*0.001f,sample->relativeVelocity);
		if(sample->hasVelocity)
			for(int i = 0; i < SIM_BONES; i++){
				const CVector v = sample->relativeVelocity[i];
				sample->relativeVelocity[i] = ped->GetRight()*v.x+ped->GetForward()*v.y+ped->GetUp()*v.z;
			}
	}
	for(int i = 0; i < SIM_BONES; i++) sample->positions[i] = current[i];
	sample->time = now;
}

static void ForgetAnimatedPose(CPed *ped)
{
	for(int i = 0; i < MAX_ANIMATED_POSES; i++)
		if(animatedPoses[i].ped == ped) animatedPoses[i].ped = nil;
}

static void InheritAnimatedMotion(Slot *slot, CPed *ped)
{
	if(slot->age > 2.0f*STEP) return;
	const uint32 now = CTimer::GetTimeInMilliseconds();
	for(int i = 0; i < MAX_ANIMATED_POSES; i++){
		const AnimatedPose &p = animatedPoses[i];
		if(p.ped != ped || p.clump != ped->GetClump() || !p.hasVelocity || now-p.time > 100) continue;
		for(int n = 0; n < SIM_BONES; n++) slot->prev[n] -= p.relativeVelocity[n]*STEP;
		return;
	}
}

static void
SeedVelocity(Slot *slot, CPed *ped)
{
	SeedMotion(slot, ped->m_vecMoveSpeed*50.0f, ped->m_vecTurnSpeed*50.0f);
	InheritAnimatedMotion(slot,ped);
	slot->poseCached = false;
}

bool
Begin(CPed *ped)
{
	return StartRagdoll(ped, false);
}

bool
BeginFall(CPed *ped)
{
	return StartRagdoll(ped, true);
}

// UpdateRpHAnim has just supplied a current pose. A death first seen outside
// the admission radius must still be eligible when it later comes into view.
static Slot *TryLateAdmission(CPed *ped)
{
	if(!enabled || ped == nil || !ped->DyingOrDead() || !ped->bIsVisible ||
	   ped->bRemoveFromWorld || ped->IsPlayer() || ped->bInVehicle || ped->bIsInWater ||
	   ped->CharCreatedBy != RANDOM_CHAR || ped->GetClump() == nil ||
	   (ped->GetPosition()-TheCamera.GetPosition()).MagnitudeSqr() > TAKE_RANGE_SQ) return nil;
	CPedPool *pool = CPools::GetPedPool();
	if(pool == nil) return nil;
	const int index = pool->GetJustIndex_NoFreeAssert(ped);
	if(index < 0 || index >= MAX_POSES || index >= pool->GetSize() || pool->GetSlot(index) != ped) return nil;
	AdmissionRetry &retry = admissionRetries[index];
	const int handle = CPools::GetPedRef(ped);
	const uint32 now = CTimer::GetTimeInMilliseconds();
	if(retry.handle == handle && retry.clump == ped->GetClump() && (int32)(retry.after-now) > 0) return nil;
	const uint32 frame = CTimer::GetFrameCounter();
	if(admissionFrame == frame) return nil;
	admissionFrame = frame; // One hierarchy capture/ground query per game frame, shared by both eyes.
	retry.handle = handle; retry.clump = ped->GetClump(); retry.after = now+1000;
	if(!Begin(ped)) return nil;
	Slot *slot = FindSlot(ped);
	// An old corpse's native root velocity is no longer its visible momentum.
	if(ped->m_nPedState == PED_DEAD)
		SeedMotion(slot,CVector(0.0f,0.0f,0.0f),CVector(0.0f,0.0f,0.0f));
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
	AdmissionEvent("late",++lateAdmissions,ped,-1);
#endif
	return slot;
}

void ExplosionImpulse(const CVector &centre, float blastRadius, float power)
{
	if(!enabled || active == 0 || !(blastRadius > 0.0f) || !(power > 0.0f)) return;
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
	int affected = 0;
#endif
	for(int i = 0; i < MAX_POSES; ++i){
		Slot *slot = &slots[i];
		CPed *ped = slot->ped;
		if(ped == nil || !ped->bIsVisible || ped->bRemoveFromWorld || ped->bInVehicle || ped->bExplosionProof) continue;
		const VrRagdollSupport::Transform presentation = Presentation(slot);
		const CVector query = presentation.InversePoint(centre);
		const float reach = blastRadius+slot->bounds.radius;
		if((slot->bounds.centre-query).MagnitudeSqr() > reach*reach) continue;
		if(!VrRagdollExplosion::Apply(slot->pos,slot->prev,SIM_BONES,query,
		   presentation.InverseDirection(CVector(0.0f,0.0f,1.0f)),blastRadius,power,
		   ped->m_fMass,VrRagdollSettings::WeightScale(),STEP)) continue;
		Thaw(slot);
		Wake(slot);
		slot->world.valid = false;
		slot->poseCached = false;
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
		++affected;
#endif
	}
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
	if(affected) AdmissionEvent("blast",++blastEvents,nil,affected);
#endif
}

void VehicleImpact(CPed *ped, CVehicle *car, const CVector &incomingGameSpeed, float collisionImpulse,
    const CVector *contactPoint, const CVector *incomingGameTurn)
{
	if(!enabled || ped == nil) return;
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
	++vehicleImpactCalls;
#endif
	Slot *slot = FindSlot(ped);
	if(slot && !slot->vehicleSeeded){
		// A body already travelling on a fast car must not lose coherent
		// translation merely because the generic death seed caps at 16 m/s.
		const CVector carSpeed = car ? car->m_vecMoveSpeed*50.0f : CVector(0.0f,0.0f,0.0f);
		const float seedLimit = Min(60.0f,Max(16.0f,Sqrt(carSpeed.x*carSpeed.x+carSpeed.y*carSpeed.y)+4.0f));
		// Only a fresh takeover needs its pre-collision seed restored. Native
		// whole-body response must not erase articulation on repeated callbacks.
		if(slot->age <= 2.0f*STEP){
			SeedMotion(slot, incomingGameSpeed*50.0f,
				(incomingGameTurn ? *incomingGameTurn : ped->m_vecTurnSpeed)*50.0f,seedLimit);
			InheritAnimatedMotion(slot,ped);
		}
		if(contactPoint && car){
			const CVector surfaceSpeed = car->GetSpeed(*contactPoint-car->GetPosition())*50.0f;
			VrRagdollVehicleImpact::Apply(slot,*contactPoint,surfaceSpeed,collisionImpulse*50.0f,
				VrRagdollSettings::WeightScale());
		}
		slot->vehicleSeeded = true;
		Wake(slot);
		slot->priorityUntil = CTimer::GetTimeInMilliseconds()+350;
		slot->poseCached = false;
	}
	// The engine already accelerated the ped before KillPedWithCar runs.
	// Its measured collision impulse retains the impact momentum; another
	// joint contact may only see a co-moving leg or an upward-facing bonnet.
	// Pool-generation handles debounce the real ped/car pair independently
	// of the visual scheduler's current selection.
	if(!enabled || GetBrakePercent() == 0 || ped->IsPlayer() ||
	   ped->CharCreatedBy != RANDOM_CHAR || ped->bInVehicle ||
	   car == nil || !car->IsCar() ||
	   !car->bUsesCollision || car->bRemoveFromWorld || car->IsRealHeli() ||
	   car->IsRealPlane() || car->m_fMass <= 0.0f) return;
	const int pedHandle = CPools::GetPedRef(ped), carHandle = CPools::GetVehicleRef(car);
	const uint32 now = CTimer::GetTimeInMilliseconds();
	if(!VrRagdollBrake::Allows(vehicleHitCache,pedHandle,carHandle,now)) return;
	const CVector before = car->m_vecMoveSpeed*50.0f;
	const CVector requested = VrRagdollBrake::Impulse(car->m_fMass,before,collisionImpulse,float(GetBrakePercent()),
		VrRagdollSettings::WeightScale());
	const CVector impulse = VrRagdollBrake::Limit(vehicleBrakeLedger,CTimer::GetFrameCounter(),
		carHandle,car->m_fMass,before,requested,float(GetBrakePercent()));
	const float amount = impulse.Magnitude();
	if(amount <= 0.0f) return;
	car->ApplyMoveForce(impulse*(1.0f/50.0f));
	VrRagdollBrake::Record(vehicleHitCache,pedHandle,carHandle,now);
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
	++vehicleBrakeEvents;
	const CVector after = car->m_vecMoveSpeed*50.0f;
	const float beforeKmh = Sqrt(before.x*before.x+before.y*before.y)*3.6f;
	const float afterKmh = Sqrt(after.x*after.x+after.y*after.y)*3.6f;
	lastVehicleBrakeKmh = beforeKmh-afterKmh;
	lastVehicleBeforeKmh = beforeKmh;
	lastVehicleAfterKmh = afterKmh;
#endif
	VrRagdollMetrics::Count(VrRagdollMetrics::CAR_BRAKES);
	VrRagdollMetrics::AddBrakeImpulse(amount);
#if defined(__ANDROID__) && defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
	// One developer diagnostic row per accepted initial impact.
	__android_log_print(ANDROID_LOG_INFO,"QuestRagdollBrake",
		"event=%u car=%d hit_game=%.2f setting=%d owned=%d before_kmh=%.2f after_kmh=%.2f delta_kmh=%.2f",
		vehicleBrakeEvents,carHandle,collisionImpulse,GetBrakePercent(),slot != nil,beforeKmh,afterKmh,lastVehicleBrakeKmh);
#endif
}

#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
const char*
DebugLine(void)
{
	static char line[120];
	snprintf(line, sizeof(line),
		"RD8 %s  BODIES %d/%d  SLEEP %d  UPDATE %d  WAIT %d",
		enabled ? "ON" : "OFF",active,MAX_POSES,sleepingCount,lastUpdated,lastDeferred);
	return line;
}

const char *VehicleDebugLine(void)
{
	static char line[120];
	snprintf(line,sizeof(line),"BRAKES %u/%u  LAST %.1f > %.1f KM/H  LOSS %.1f",
		vehicleBrakeEvents,vehicleImpactCalls,lastVehicleBeforeKmh,lastVehicleAfterKmh,lastVehicleBrakeKmh);
	return line;
}

#endif

struct StepContacts {
	VrRagdollContacts::StepContacts *vehicle;
	VrRagdollWorld::StepContacts *world;
	bool hasVehicle;
};

static void
ProjectContacts(State *state, void *context)
{
	VrRagdollMetrics::Scope contactTime(VrRagdollMetrics::CONTACT);
	StepContacts &contacts = *static_cast<StepContacts*>(context);
	if(contacts.hasVehicle) VrRagdollContacts::Project(state,contacts.vehicle);
	VrRagdollWorld::Project(state,contacts.world);
}

static void WakeSavedByVehicles(void)
{
	if(sleepingCount == 0) return;
	VrRagdollMetrics::Scope vehicleTime(VrRagdollMetrics::VEHICLE);
	CVehiclePool *pool = CPools::GetVehiclePool();
	if(pool == nil) return;
	bool near[MAX_POSES] = {};
	float bounds[MAX_POSES] = {};
	for(int s = 0; s < MAX_POSES; s++){
		if(slots[s].ped == nil || !slots[s].asleep) continue;
		float r2 = 0.0f;
		for(int n = 0; n < SIM_BONES; n++)
			r2 = Max(r2, (slots[s].pos[n]-slots[s].pos[RD_PELVIS]).MagnitudeSqr());
		bounds[s] = Sqrt(r2)+JOINT_RADIUS;
	}
	// One shared pool scan for all saved poses. Look slightly ahead so a
	// fast car wakes the body before its actual fixed-step contact arrives.
	for(int i = 0; i < pool->GetSize(); i++){
		CVehicle *car = pool->GetSlot(i);
		VrRagdollMetrics::Count(VrRagdollMetrics::POOL_CHECKS);
		if(car == nil || !car->IsCar() || !car->bUsesCollision || car->bRemoveFromWorld ||
		   car->IsRealHeli() || car->IsRealPlane() ||
		   car->m_vecMoveSpeed.MagnitudeSqr()+car->m_vecTurnSpeed.MagnitudeSqr() < 0.000004f) continue;
		CColModel *col = car->GetColModel();
		if(col == nil) continue;
		const CVector center = car->GetMatrix()*col->boundingSphere.center;
		const CVector travel = car->m_vecMoveSpeed*5.0f;
		for(int s = 0; s < MAX_POSES; s++){
			if(slots[s].ped == nil || !slots[s].asleep || near[s]) continue;
			const float r = bounds[s]+col->boundingSphere.radius;
			near[s] = VrRagdollVehicle::SegmentDistanceSq(slots[s].pos[RD_PELVIS],center-travel,center+travel) <= r*r;
		}
	}
	static int cursor;
	int queries = 0;
	for(int n = 0; n < MAX_POSES && queries < 2; n++){
		const int s = (cursor+n)%MAX_POSES;
		if(!near[s]) continue;
		queries++;
		VrRagdollVehicle::Batch vehicles;
		VrRagdollVehicle::Gather(slots[s].pos,SIM_BONES,JOINT_RADIUS,0.1f,vehicles);
		VrRagdollMetrics::Count(VrRagdollMetrics::POOL_CHECKS,vehicles.poolChecks);
		VrRagdollMetrics::Count(VrRagdollMetrics::TRIANGLE_SCANS,vehicles.triangleScans);
		VrRagdollMetrics::Count(VrRagdollMetrics::TRIANGLE_OVERFLOW,vehicles.triangleOverflow);
		VrRagdollVehicle::Prepare(vehicles,0.0f,0.1f);
		if(!VrRagdollContacts::ShouldWake(&slots[s],vehicles)) continue;
		Slot *awake = Thaw(&slots[s]);
		if(awake == nil) break;
		Wake(awake);
		awake->stepDebt = 0.0f;
		awake->poseCached = false;
		VrRagdollMetrics::Count(VrRagdollMetrics::WAKEUPS);
	}
	cursor = (cursor+1)%MAX_POSES;
}

void
Update(void)
{
	if(!enabled && active == 0){
		// No empty pool scan or profiler clock query while the option is off.
		// End the old profiling window so its sample flag cannot time later
		// scopes after all retained bodies have gone away.
		sleepingCount = 0;
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
		lastUpdated = lastDeferred = 0;
#endif
		VrRagdollMetrics::sampling = false;
		return;
	}
	sleepingCount = 0;
	for(int s = 0; s < MAX_POSES; s++)
		if(slots[s].ped && slots[s].asleep) sleepingCount++;
	VrRagdollMetrics::BeginFrame(active,active-sleepingCount,sleepingCount);
	VrRagdollMetrics::Scope updateTime(VrRagdollMetrics::UPDATE);
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
	lastUpdated = lastDeferred = 0;
#endif
	if(active == 0) return;
	float dt = CTimer::GetTimeStepInSeconds();
	if(dt <= 0.0f) return;
	dt = Min(0.1f,dt);
	if(enabled) WakeSavedByVehicles();
	const uint32 now = CTimer::GetTimeInMilliseconds();
	VrRagdollSchedule::Candidate candidates[MAX_POSES] = {};
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
	int waiting = 0;
#endif
	for(int s = 0; s < MAX_POSES; s++){
		Slot *slot = &slots[s];
		if(slot->ped == nil) continue;
		slot->support.age += dt;
		slot->bulletCredit = VrRagdollImpact::Refill(slot->bulletCredit,dt);
		if(slot->live){
			if(slot->ped->DyingOrDead()) slot->live = false;
			else if(slot->ped->m_nPedState != PED_FALL){
				CPed *ped = slot->ped;
				slot->ped = nil;
				active--;
				if(ped->m_entryInfoList.first) ped->RemoveAndAdd();
				continue;
			}
		}
		if(slot->asleep){
			slot->stepDebt = 0.0f;
			slot->waitTime = 0.0f;
			slot->world.valid = false; // Refresh static objects after a later wake.
			continue;
		}
		slot->waitTime += dt;
		slot->stepDebt += dt;
		// Bound catch-up after a stall; independent waitTime prevents starvation.
		if(slot->stepDebt > 0.1f){
			VrRagdollMetrics::Count(VrRagdollMetrics::DROPPED_STEPS,
				(unsigned)((slot->stepDebt-0.1f)/STEP));
			slot->stepDebt = 0.1f;
		}
		VrRagdollSchedule::Candidate &candidate = candidates[s];
		candidate.ready = slot->stepDebt >= STEP;
		candidate.urgent = (int32)(slot->priorityUntil-now) > 0 ||
			(slot->lastSupportCount > 0 && slot->lastMaxSpeedSq > 4.0f);
		candidate.pending = slot->waitTime;
		candidate.distanceSq = (slot->bounds.centre-TheCamera.GetPosition()).MagnitudeSqr();
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
		if(candidate.ready) waiting++;
#endif
	}
	int selected[VrRagdollSchedule::MAX_UPDATES];
	const int selectedCount = VrRagdollSchedule::Select(candidates,MAX_POSES,selected);
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
	lastDeferred = waiting-selectedCount;
#endif
	int remainingSteps = VrRagdollSchedule::MAX_STEPS;
	int remainingPasses = VrRagdollSchedule::MAX_PASSES;
	VrRagdollReaction::Frame reaction;
	VrRagdollReaction::BeginFrame(reaction);
	for(int update = 0; update < selectedCount; update++){
		const int s = selected[update];
		Slot *slot = &slots[s];
		// Contacts close relative inward speed; they do not apply another
		// launch impulse. They must run during FALL and DIE as well as DEAD,
		// otherwise the entire fast-hit flight passes through the car.
		VrRagdollVehicle::Batch vehicles;
		{
			VrRagdollMetrics::Scope vehicleTime(VrRagdollMetrics::VEHICLE);
			float maxTravelSq = 0.0f;
			if(!slot->asleep)
				for(int i = 0; i < SIM_BONES; i++)
					maxTravelSq = Max(maxTravelSq, (slot->pos[i]-slot->prev[i]).MagnitudeSqr());
			const float span = Min(0.1f, slot->stepDebt+STEP);
			const float bodyTravel = Sqrt(maxTravelSq)*span/STEP + 0.5f*FALL_GRAVITY*span*span;
			VrRagdollVehicle::Gather(slot->pos, SIM_BONES, JOINT_RADIUS,
				span, vehicles, bodyTravel);
			VrRagdollMetrics::Count(VrRagdollMetrics::POOL_CHECKS, vehicles.poolChecks);
			VrRagdollMetrics::Count(VrRagdollMetrics::CANDIDATES, vehicles.count);
			VrRagdollMetrics::Count(VrRagdollMetrics::CANDIDATE_OVERFLOW, vehicles.candidateOverflow);
			VrRagdollMetrics::Count(VrRagdollMetrics::SHAPE_OVERFLOW, vehicles.shapeOverflow);
			VrRagdollMetrics::Count(VrRagdollMetrics::TRIANGLE_SCANS, vehicles.triangleScans);
			VrRagdollMetrics::Count(VrRagdollMetrics::TRIANGLE_OVERFLOW, vehicles.triangleOverflow);
			VrRagdollMetrics::Count(VrRagdollMetrics::UNSUPPORTED_MODELS, vehicles.unsupportedModels);
		}
		// The body drifts, the floor under it may not be the floor it
		// died over; one query per frame keeps the plane honest.
		bool found = false;
		const CVector &pelvis = slot->pos[RD_PELVIS];
		float ground;
		{
			VrRagdollMetrics::Scope groundTime(VrRagdollMetrics::GROUND);
			ground = CWorld::FindGroundZFor3DCoord(pelvis.x,
				pelvis.y, pelvis.z + 0.5f, &found);
			VrRagdollMetrics::Count(VrRagdollMetrics::GROUND_QUERIES);
		}
		if(found)
			slot->groundZ = ground;
		{
			VrRagdollMetrics::Scope worldTime(VrRagdollMetrics::WORLD);
			const float span = Min(0.1f,slot->stepDebt);
			// Cache expiry follows real waiting time even under heavy scheduling
			// pressure, while the prediction span remains bounded for integration.
			slot->world.remaining -= Max(0.0f,slot->waitTime-span);
			const bool rebuild = !slot->world.valid || slot->world.remaining <= span ||
				!VrRagdollWorld::Contains(slot->world.bounds,VrRagdollWorld::RequiredBounds(slot,span));
			VrRagdollWorld::Gather(slot,slot->world,span);
			if(rebuild){
				VrRagdollMetrics::Count(VrRagdollMetrics::WORLD_QUERIES);
				VrRagdollMetrics::Count(VrRagdollMetrics::WORLD_TRIANGLES,slot->world.stats.primitiveScans);
				VrRagdollMetrics::Count(VrRagdollMetrics::WORLD_OVERFLOW,slot->world.stats.overflow);
			}
		}
		// Update runs BEFORE native vehicle physics. The captured matrix is at
		// the previous game-frame time; debt already includes the new frame dt.
		float vehicleTime = dt-slot->stepDebt;
		const int bodyBudget = Min(MAX_STEPS, remainingSteps-(selectedCount-update-1));
		int steps = 0;
		while(slot->stepDebt >= STEP && steps < bodyBudget &&
		      remainingPasses >= 4*(selectedCount-update)){
			// Preserve the original 60 Hz contact solve for current impacts and
			// light scenes. Only background work combines elapsed intervals.
			const bool fineStep = selectedCount <= VrRagdollSchedule::PRIORITY_UPDATES ||
				(update < VrRagdollSchedule::PRIORITY_UPDATES && candidates[s].urgent) ||
				remainingPasses-4*(selectedCount-update-1) < 6;
			const float stepTime = fineStep ? STEP : Min(3.0f*STEP,slot->stepDebt);
			const int passes = stepTime > STEP*1.01f ? 6 : 4;
			VrRagdollContacts::StepContacts contacts(&vehicles,&reaction,slot,update,
				slot->ped->bUsesCollision ? 0.5f : 1.0f,stepTime);
			VrRagdollWorld::StepContacts worldContacts(slot->world,slot,stepTime,VrRagdollWorld::FallbackSweep);
			StepContacts allContacts = {&contacts,&worldContacts,vehicles.count != 0};
			if(vehicles.count){
				VrRagdollMetrics::Scope prepareTime(VrRagdollMetrics::VEHICLE);
				VrRagdollVehicle::Prepare(vehicles, vehicleTime, vehicleTime+stepTime);
			}
			{
				VrRagdollMetrics::Scope solveTime(VrRagdollMetrics::SOLVE);
				Step(slot,ProjectContacts,&allContacts,stepTime);
				VrRagdollWorld::ResolveVelocity(slot,worldContacts);
			}
			VrRagdollMetrics::ObserveStep(slot->lastMaxLengthError, slot->lastMaxSpeedSq);
			VrRagdollMetrics::Count(VrRagdollMetrics::STEPS);
			VrRagdollMetrics::Count(VrRagdollMetrics::SOLVER_PASSES,passes);
			VrRagdollMetrics::Count(VrRagdollMetrics::CONTACTS, contacts.count);
			VrRagdollMetrics::Count(VrRagdollMetrics::WORLD_FALLBACKS,worldContacts.stats.fallbackQueries);
			slot->poseCached = false;
			slot->stepDebt -= stepTime;
			VrRagdollSupport::Capture(slot->support,*slot,contacts,slot->stepDebt);
			vehicleTime += stepTime;
			remainingSteps--;
			remainingPasses -= passes;
			steps++;
			if(slot->asleep){
				slot->stepDebt = 0.0f;
				break;
			}
		}
		RefreshBounds(slot);
		slot->waitTime = 0.0f;
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
		lastUpdated++;
#endif
	}
	// Update even when off screen: rendering must find the new body sectors.
	for(int s = 0; s < MAX_POSES; s++){
		Slot *slot = &slots[s];
		if(slot->ped == nil) continue;
		LinkBounds(slot);
	}
	{
		VrRagdollMetrics::Scope reactionTime(VrRagdollMetrics::VEHICLE);
		VrRagdollVehicle::CommitReactions(reaction,&vehicleBrakeLedger,CTimer::GetFrameCounter());
		VrRagdollMetrics::Count(VrRagdollMetrics::CAR_BRAKES,reaction.appliedVehicles);
		VrRagdollMetrics::Count(VrRagdollMetrics::BRAKE_POINTS,reaction.pointImpacts);
		VrRagdollMetrics::AddBrakeImpulse(reaction.committedImpulse);
	}
}

void
Apply(CPed *ped)
{
	if(!enabled && active == 0) return;
	Slot *slot = FindSlot(ped);
	if(slot == nil) slot = TryLateAdmission(ped);
	if(slot == nil){
		ObserveAnimatedPose(ped);
		return;
	}
	VrRagdollMetrics::Scope poseTime(VrRagdollMetrics::POSE);
	RpHAnimHierarchy *hier = GetAnimHierarchyFromSkinClump(ped->GetClump());
	if(hier == nil || hier->numNodes != slot->nodeCount)
		return;
	RwMatrix *mats = RpHAnimHierarchyGetMatrixArray(hier);
	const VrRagdollSupport::Transform presentation=Presentation(slot);
	if(slot->asleep && slot->poseCached){
		for(int i = 0; i < hier->numNodes; i++)
			mats[i] = slot->cachedPose[i];
		ApplyPresentation(mats,hier->numNodes,presentation);
		return;
	}

	CVector simRight[SIM_BONES], simUp[SIM_BONES], simAt[SIM_BONES];
	VrRagdollPose::LegFrames legFrames[NUM_HIPS];
	for(int i = 0; i < NUM_HIPS; i++){
		const Bend &knee = slot->bends[i];
		legFrames[i] = VrRagdollPose::MakeLegFrames(
			slot->pos[knee.b]-slot->pos[knee.a], slot->pos[knee.c]-slot->pos[knee.b],
			World(slot->pelvisFrame,knee.localUpper), World(slot->pelvisFrame,knee.localBend));
	}
	for(int i = 0; i < SIM_BONES; i++){
		if(i >= RD_LTHIGH){
			const int leg = i >= RD_RTHIGH ? 1 : 0;
			const bool thigh = i == RD_LTHIGH || i == RD_RTHIGH;
			const Frame &from = thigh ? slot->deathLegFrames[leg].upper : slot->deathLegFrames[leg].lower;
			const Frame &to = thigh ? legFrames[leg].upper : legFrames[leg].lower;
			// Captured skin axes may differ between models. Carry them through
			// the geometric leg frames instead of assuming a bone-local axis.
			// Calf and foot share one frame, including passthrough toe nodes.
			simRight[i] = Carry(from,to,slot->deathRight[i]);
			simUp[i] = Carry(from,to,slot->deathUp[i]);
			simAt[i] = Carry(from,to,slot->deathAt[i]);
		}else{
			// Hip/shoulder width supplies a second axis, including when the
			// spine turns through 180 degrees. Arms inherit trunk twist before
			// swinging to their own simulated segments.
			const bool pelvis = i == RD_PELVIS || i == RD_SPINE;
			const Frame &rest = pelvis ? slot->restPelvis : slot->restTorso;
			const Frame &current = pelvis ? slot->pelvisFrame : slot->torsoFrame;
			const CVector right = Carry(rest, current, slot->deathRight[i]);
			const CVector up = Carry(rest, current, slot->deathUp[i]);
			const CVector at = Carry(rest, current, slot->deathAt[i]);
			if(i == RD_PELVIS){
				simRight[i] = right;
				simUp[i] = up;
				simAt[i] = at;
			}else{
				const CVector original = Carry(rest, current, slot->deathDir[i]);
				const CVector direction = Unit(slot->pos[dirTo[i]] -
					slot->pos[dirFrom[i]], original);
				simRight[i] = Swing(original, direction, right, right);
				simUp[i] = Swing(original, direction, up, right);
				simAt[i] = Swing(original, direction, at, right);
			}
		}
		RwMatrix *m = &mats[slot->simMatrix[i]];
		m->right = simRight[i];
		m->up = simUp[i];
		m->at = simAt[i];
		m->pos = slot->pos[i];
		m->flags = 0;
	}
	for(int i = 0; i < hier->numNodes; i++){
		int driver = slot->passDriver[i];
		if(driver < 0)
			continue;
		const CVector dr = simRight[driver], du = simUp[driver],
			da = simAt[driver];
		RwMatrix *m = &mats[i];
		m->right = dr*slot->passRight[i].x + du*slot->passRight[i].y +
			da*slot->passRight[i].z;
		m->up = dr*slot->passUp[i].x + du*slot->passUp[i].y +
			da*slot->passUp[i].z;
		m->at = dr*slot->passAt[i].x + du*slot->passAt[i].y +
			da*slot->passAt[i].z;
		m->pos = slot->pos[driver] + dr*slot->passPos[i].x +
			du*slot->passPos[i].y + da*slot->passPos[i].z;
		m->flags = 0;
	}
	if(slot->asleep){
		for(int i = 0; i < hier->numNodes; i++)
			slot->cachedPose[i] = mats[i];
		slot->poseCached = true;
	}
	ApplyPresentation(mats,hier->numNodes,presentation);
}

static float
BulletImpulse(int weaponType)
{
	// Gameplay-tuned impulse, independent of damage. Shotguns trace three
	// or five pellets, so each ray only receives its share of the shot.
	switch(weaponType){
	case WEAPONTYPE_COLT45: return 8.0f;
	case WEAPONTYPE_PYTHON: return 14.0f;
	case WEAPONTYPE_SHOTGUN:
	case WEAPONTYPE_SPAS12_SHOTGUN: return 5.0f;
	case WEAPONTYPE_STUBBY_SHOTGUN: return 3.0f;
	case WEAPONTYPE_TEC9:
	case WEAPONTYPE_UZI:
	case WEAPONTYPE_SILENCED_INGRAM:
	case WEAPONTYPE_MP5:
	case WEAPONTYPE_UZI_DRIVEBY: return 6.0f;
	case WEAPONTYPE_M4:
	case WEAPONTYPE_RUGER: return 10.0f;
	case WEAPONTYPE_SNIPERRIFLE:
	case WEAPONTYPE_LASERSCOPE: return 16.0f;
	case WEAPONTYPE_M60: return 10.0f;
	case WEAPONTYPE_MINIGUN: return 8.0f;
	case WEAPONTYPE_HELICANNON: return 8.0f;
	default: return 0.0f;
	}
}

CPed *
BulletHit(const CVector &source, const CVector &end, int weaponType,
          CColPoint &point, CEntity *shooter)
{
	if(!enabled || active == 0)
		return nil;
	VrRagdollMetrics::Scope bulletTime(VrRagdollMetrics::BULLET);
	const float requested = BulletImpulse(weaponType)*(float(GetShotPercent())*0.01f);
	const CVector ray = end-source;
	const float length = ray.Magnitude();
	if(requested <= 0.0f || length < 0.001f)
		return nil;
	Slot *hitSlot = nil;
	int hitStick = -1;
	float hitParam = 2.0f;
	CVector hitNormal;
	VrRagdollSupport::Transform hitPresentation;
	for(int sl = 0; sl < MAX_POSES; sl++){
		Slot *slot = &slots[sl];
		if(slot->ped == nil || slot->ped == shooter)
			continue;
		const float bound = slot->bounds.radius;
		const VrRagdollSupport::Transform presentation=Presentation(slot);
		const CVector querySource=presentation.InversePoint(source),queryEnd=presentation.InversePoint(end);
		if(VrRagdollVehicle::SegmentDistanceSq(slot->bounds.centre,querySource,queryEnd) > bound*bound)
			continue;
		// Real skeletal segments only: the artificial torso braces are
		// structural constraints, not additional invisible bullet targets.
		for(int i = 0; i < SKELETON_STICKS; i++){
			float param;
			CVector normal;
			const float hitRadius = Max(radius[sticks[i].a], radius[sticks[i].b]);
			if(VrRagdollRay::Capsule(querySource,queryEnd,slot->pos[sticks[i].a],
				slot->pos[sticks[i].b],hitRadius,param,normal) && param < hitParam){
				hitParam = param;
				hitStick = i;
				hitSlot = slot;
				hitNormal = presentation.Direction(normal);
				hitPresentation = presentation;
			}
		}
	}
	if(hitSlot == nil){
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
		shotMisses++;
#endif
		return nil;
	}
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
	shotHits++;
#endif
	CPed *hitPed = hitSlot->ped;
	const int b = sticks[hitStick].b;
	const CVector hitPos = source+ray*hitParam;
	const CVector direction = ray*(1.0f/length);
	static const unsigned char parts[][6] = {
		{RD_PELVIS,RD_SPINE1,RD_LUARM,RD_RUARM,RD_LTHIGH,RD_RTHIGH},
		{RD_NECK,RD_HEAD,RD_LCLAV,RD_RCLAV,0,0},
		{RD_LCLAV,RD_LUARM,RD_LFARM,RD_LHAND,0,0},
		{RD_RCLAV,RD_RUARM,RD_RFARM,RD_RHAND,0,0},
		{RD_PELVIS,RD_LTHIGH,RD_LCALF,RD_LFOOT,0,0},
		{RD_PELVIS,RD_RTHIGH,RD_RCALF,RD_RFOOT,0,0}
	};
	int part = 0;
	if(b == RD_HEAD || b == RD_NECK) part = 1;
	else if(b >= RD_LCLAV && b <= RD_LHAND) part = 2;
	else if(b >= RD_RCLAV && b <= RD_RHAND) part = 3;
	else if(b >= RD_LTHIGH && b <= RD_LFOOT) part = 4;
	else if(b >= RD_RTHIGH && b <= RD_RFOOT) part = 5;
	const bool wasSaved = hitSlot->asleep;
	hitSlot = Thaw(hitSlot);
	if(hitSlot){
		if(wasSaved){
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
			shotThaws++;
#endif
			hitSlot->bulletCredit = VrRagdollImpact::BURST_IMPULSE;
		}
		const float impulse = VrRagdollImpact::Take(hitSlot->bulletCredit,requested);
		if(impulse > 0.0f){
			if(hitSlot->asleep)
				VrRagdollMetrics::Count(VrRagdollMetrics::WAKEUPS);
			Wake(hitSlot);
			hitSlot->poseCached = false;
			VrRagdollImpulse::ApplyPartImpulse(hitSlot->pos,hitSlot->prev,invMass,
				parts[part],part == 0 ? 6 : 4,hitPresentation.InversePoint(hitPos),
				hitPresentation.InverseDirection(direction),impulse,STEP,
				VrRagdollImpulse::MAX_POINT_DELTA_SPEED,VrRagdollSettings::WeightScale());
		}
	}
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
	else shotBusy++;
#endif
	VrRagdollMetrics::Count(VrRagdollMetrics::HITS);
	point.point = hitPos;
	point.normal = hitNormal;
	int piece = PEDPIECE_TORSO;
	if(b == RD_HEAD || b == RD_NECK) piece = PEDPIECE_HEAD;
	else if(b >= RD_LCLAV && b <= RD_LHAND) piece = PEDPIECE_LEFTARM;
	else if(b >= RD_RCLAV && b <= RD_RHAND) piece = PEDPIECE_RIGHTARM;
	else if(b >= RD_LTHIGH && b <= RD_LFOOT) piece = PEDPIECE_LEFTLEG;
	else if(b >= RD_RTHIGH && b <= RD_RFOOT) piece = PEDPIECE_RIGHTLEG;
	else if(b <= RD_SPINE) piece = PEDPIECE_MID;
	point.Set(0.0f, SURFACE_DEFAULT, 0, SURFACE_PED, piece);
	return hitPed;
}

void
Release(CPed *ped)
{
	// SetEnabled(false) clears the animation cache once. An empty disabled
	// system therefore has no owner pointer to forget in either fixed array.
	if(ped == nil || (!enabled && active == 0)) return;
	ForgetAnimatedPose(ped);
	Slot *slot = FindSlot(ped);
	if(slot){
		slot->ped = nil;
		active--;
	}
}

}
