#pragma once

#include <cmath>
#include "VrRagdollSchedule.h"

// Momentum feedback for one-way ragdoll/vehicle contacts. CVector helpers
// come from common.h (or the host fixture). This layer owns no game objects.
namespace VrRagdollReaction
{
// Body indices are the scheduler's bounded update indices for this frame,
// rather than persistent ped slots. Retained sleeping corpses need no masks.
enum { MAX_VEHICLES = 8, MAX_BODIES = VrRagdollSchedule::MAX_UPDATES, MAX_POINTS = 19 };
static const float MIN_CLOSING_SPEED = 0.5f;
static const float MIN_CAR_SPEED = 0.25f;
static const float MAX_SPEED_FRACTION = 0.35f;

struct Vehicle {
	int handle;
	float rawImpulse, effectiveMass;
	unsigned pointMask[MAX_BODIES];
};
struct Frame {
	Vehicle vehicles[MAX_VEHICLES];
	int count;
	unsigned pointImpacts, duplicates, overflow, appliedVehicles, invalidVehicles;
	float requestedImpulse, committedImpulse;
	bool committed;
	Frame() : count(0), pointImpacts(0), duplicates(0), overflow(0),
		appliedVehicles(0), invalidVehicles(0), requestedImpulse(0.0f), committedImpulse(0.0f), committed(false) {}
};
inline void BeginFrame(Frame &frame)
{
	frame.count = 0;
	frame.pointImpacts = frame.duplicates = frame.overflow = 0;
	frame.appliedVehicles = frame.invalidVehicles = 0;
	frame.requestedImpulse = frame.committedImpulse = 0.0f;
	frame.committed = false;
}

// Only actual inward contact momentum contributes. Geometry correction,
// joint-solver speed, floor gravity, and stationary overlap cannot propel
// the car: the resulting reaction is strictly horizontal braking.
inline bool Add(Frame &frame, int handle, float carMass, const CVector &carVelocity,
	int body, int point, float pointMass, const CVector &normal,
	const CVector &surfaceVelocity, const CVector &incomingVelocity, float scale)
{
	if(frame.committed || handle < 0 || carMass <= 0.0f || pointMass <= 0.0f || scale <= 0.0f ||
		body < 0 || body >= MAX_BODIES || point < 0 || point >= MAX_POINTS) return false;
	float speedSq = carVelocity.x * carVelocity.x + carVelocity.y * carVelocity.y;
	if(speedSq < MIN_CAR_SPEED * MIN_CAR_SPEED) return false;
	float speed = std::sqrt(speedSq);
	CVector direction(carVelocity.x / speed, carVelocity.y / speed, 0.0f);
	float alignment = DotProduct(normal, direction);
	if(alignment <= 0.05f) return false;
	float closing = DotProduct(surfaceVelocity - incomingVelocity, normal);
	if(closing < MIN_CLOSING_SPEED) return false;
	// This is the participating body-mass share, not the menu percentage.
	// User strength is applied once at commit, after finite-mass reduction.
	if(scale > 1.0f) scale = 1.0f;
	int index = 0;
	for(; index < frame.count; ++index) if(frame.vehicles[index].handle == handle) break;
	if(index == frame.count){
		if(frame.count == MAX_VEHICLES){ ++frame.overflow; return false; }
		Vehicle &v = frame.vehicles[frame.count++];
		v.handle = handle; v.rawImpulse = v.effectiveMass = 0.0f;
		for(int b = 0; b < MAX_BODIES; ++b) v.pointMask[b] = 0;
	}
	Vehicle &v = frame.vehicles[index];
	unsigned bit = 1u << point;
	// Three catch-up steps and repeated passes still describe the same
	// particle mass in one game frame, not three new pedestrians.
	if(v.pointMask[body] & bit){ ++frame.duplicates; return false; }
	v.pointMask[body] |= bit;
	float mass = pointMass * scale;
	float impulse = mass * closing * alignment;
	v.rawImpulse += impulse;
	v.effectiveMass += mass * alignment * alignment;
	frame.requestedImpulse += impulse;
	++frame.pointImpacts;
	return true;
}

inline CVector Impulse(const Vehicle &v, float currentMass, const CVector &currentVelocity)
{
	float speedSq = currentVelocity.x * currentVelocity.x + currentVelocity.y * currentVelocity.y;
	if(currentMass <= 0.0f || speedSq < MIN_CAR_SPEED * MIN_CAR_SPEED) return CVector(0.0f, 0.0f, 0.0f);
	float speed = std::sqrt(speedSq);
	// Finite car mass reduces the one-way body impulse. A 70 kg body and a
	// 1000 kg car at 30 m/s yield 1962.6 Ns, or 1.963 m/s car speed loss.
	float amount = v.rawImpulse * currentMass / (currentMass + v.effectiveMass);
	// Bound the physical request before the separate, configurable brake gain.
	// CommitReactions then shares Brake::Limit with the direct-hit callback.
	float limit = currentMass * speed * MAX_SPEED_FRACTION;
	if(amount > limit) amount = limit;
	return CVector(-currentVelocity.x * (amount / speed), -currentVelocity.y * (amount / speed), 0.0f);
}
} // namespace VrRagdollReaction
