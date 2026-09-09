#pragma once

class CPed;
class CVehicle;
class CVector;
struct CColPoint;
class CEntity;

// Verlet ragdoll for dying ambient peds: a particle per joint, distance
// constraints along the skeleton, and the world-space hierarchy matrices
// rewritten after the animation update. The ped state machine retains its
// existing fall/death transitions. Vehicle contacts also feed a bounded
// braking impulse back into the car.
namespace VrRagdoll
{
void SetEnabled(bool enabled);
bool IsEnabled(void);
// Live Quest-menu settings; percentages are clamped by the setters.
void SetBrakePercent(int value);
int GetBrakePercent(void);
void SetGripPercent(int value);
int GetGripPercent(void);
void SetShotPercent(int value);
int GetShotPercent(void);
// Uniform body mass/inertia, independent of gravity, time and brake gain.
void SetWeightPercent(int value);
int GetWeightPercent(void);
// Takes the fall over at SetDie. False when the option is off, the ped
// does not qualify (player, mission char, in a vehicle, in water, too
// far) or its skeleton is unsupported. Storage covers the entire ped pool;
// another body's unfinished fall does not prevent admission.
bool Begin(CPed *ped);
// Takes a live ped the moment a vehicle knocks it flying, so the flight
// itself ragdolls: released back to the animations when the ped survives
// into its getup, converted in place when death arrives mid-flight.
bool BeginFall(CPed *ped);
// Preserve the visual seed and react once to the engine's measured impact.
// collisionImpulse is in engine units (Ns / 50), before scripted launch.
void VehicleImpact(CPed *ped, CVehicle *car, const CVector &incomingGameSpeed, float collisionImpulse,
    const CVector *contactPoint = 0, const CVector *incomingGameTurn = 0);
// Runs before native explosion death callbacks: only existing owners receive
// this impulse; newly killed peds retain their native velocity seed once.
void ExplosionImpulse(const CVector &centre, float radius, float power);
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
// Developer counters for the RAGDOLL page.
const char *DebugLine(void);
const char *VehicleDebugLine(void);
#else
inline const char *DebugLine(void) { return ""; }
inline const char *VehicleDebugLine(void) { return ""; }
#endif
// Resolve the visible bone capsules within the nearest world obstruction.
// Return the real ped and hit point so weapon damage, traces and blood use
// the same contact; living fallers and dying/saved corpses all participate.
CPed *BulletHit(const CVector &source, const CVector &end, int weaponType,
    CColPoint &point, CEntity *shooter);
bool Owns(CPed *ped);
// Cached world-space skin bounds, including sleeping archived bodies.
bool GetBounds(CPed *ped, CVector &centre, float &radius);
bool GetClumpBounds(void *clump, CVector &centre, float &radius);
// Only the weapon's scoped world trace excludes our animated collision proxy.
bool SetBulletTrace(bool on);
bool SkipBulletCollision(CPed *ped);
// Advances every active ragdoll; once per CWorld::Process, so a paused
// game freezes the bodies with everything else.
void Update(void);
// Overwrites the ped's bone matrices with the ragdoll pose; called right
// after UpdateRpHAnim so the existing PreRender overrides compose on top.
void Apply(CPed *ped);
void Release(CPed *ped);
}
