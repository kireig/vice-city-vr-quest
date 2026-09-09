#pragma once

#include <cmath>
#include <limits>
#include "VrRagdollSettings.h"

// Extra vehicle resistance for a confirmed pedestrian hit. The threefold
// impulse gain is deliberately game feel, independent of the body weight.
// Input collision impulse is CPhysical's kg * units/game-tick;
// output is horizontal SI momentum, converted back by the caller with /50.
namespace VrRagdollBrake
{
enum { MAX_VEHICLES = 8, MAX_HIT_PAIRS = 64 };
static const float IMPULSE_GAIN = 3.0f;
static const float EFFECTIVE_MASS = 210.0f;
static const float MAX_HIT_FRACTION = 0.22f;
static const float MAX_FRAME_FRACTION = 0.35f;
static const float LEGACY_HIT_FRACTION = 0.75f;
static const float LEGACY_FRAME_FRACTION = 0.85f;
static const float HARD_HIT_FRACTION = 0.98f;
static const float HARD_FRAME_FRACTION = 0.99f;
static const unsigned HIT_COOLDOWN_MS = 1000u;
static const float MIN_CAR_SPEED = 0.25f;
static const float MIN_GAME_IMPULSE = 0.05f;

struct VehicleBudget {
	int handle;
	double remaining;
};
struct Ledger {
	VehicleBudget vehicles[MAX_VEHICLES];
	unsigned frame, overflow;
	int count;
	bool initialized;
	Ledger() : frame(0), overflow(0), count(0), initialized(false) {}
};

// Separate from the six simulated bodies: a confirmed engine collision must
// still slow a car when the ragdoll pool is full. Full, unexpired caches decline
// new pairs rather than evicting a recent hit and allowing duplicate impulses.
struct HitPair {
	int pedHandle, carHandle;
	unsigned time;
};
struct HitCache {
	HitPair pairs[MAX_HIT_PAIRS];
	int count;
	HitCache() : count(0) {}
};
inline bool Allows(const HitCache &cache, int pedHandle, int carHandle, unsigned now)
{
	if(pedHandle < 0 || carHandle < 0) return false;
	bool available = cache.count < MAX_HIT_PAIRS;
	for(int i = 0; i < cache.count; ++i){
		const HitPair &pair = cache.pairs[i];
		const bool expired = unsigned(now - pair.time) >= HIT_COOLDOWN_MS;
		if(pair.pedHandle == pedHandle && pair.carHandle == carHandle) return expired;
		if(expired) available = true;
	}
	return available;
}
// Call only after applying a nonzero impulse. Failed or disabled requests must
// not use the pair's one-second credit. Unsigned subtraction handles timer wrap.
inline bool Record(HitCache &cache, int pedHandle, int carHandle, unsigned now)
{
	if(pedHandle < 0 || carHandle < 0) return false;
	int reusable = -1;
	for(int i = 0; i < cache.count; ++i){
		const HitPair &pair = cache.pairs[i];
		const bool expired = unsigned(now - pair.time) >= HIT_COOLDOWN_MS;
		if(pair.pedHandle == pedHandle && pair.carHandle == carHandle){
			if(!expired) return false;
			reusable = i;
			break;
		}
		if(expired && reusable < 0) reusable = i;
	}
	if(reusable < 0){
		if(cache.count == MAX_HIT_PAIRS) return false;
		reusable = cache.count++;
	}
	HitPair &pair = cache.pairs[reusable];
	pair.pedHandle = pedHandle;
	pair.carHandle = carHandle;
	pair.time = now;
	return true;
}

inline float Multiplier(float percent)
{
	if(!std::isfinite(percent) || percent <= 0.0f) return 0.0f;
	if(percent > VrRagdollSettings::MAX_BRAKE) percent = VrRagdollSettings::MAX_BRAKE;
	return percent * 0.01f;
}
// Retain the complete 0..500 response curve. Above its old ceiling, also
// increase the safety budget: raising only the impulse multiplier would leave
// hard/fast hits stuck at the same 75% loss. Even the maximum keeps a positive
// remainder of current velocity, and all bodies still share one frame budget.
inline double ExtendedFraction(float percent, float base, float legacy, float at1000, float hard)
{
	const double multiplier = Multiplier(percent);
	if(multiplier <= 5.0){
		const double fraction = double(base)*multiplier;
		return fraction < legacy ? fraction : legacy;
	}
	if(multiplier <= 10.0)
		return legacy+(double(at1000)-legacy)*(multiplier-5.0)/5.0;
	return at1000+(double(hard)-at1000)*(multiplier-10.0)/10.0;
}
inline double HitFraction(float percent)
{
	return ExtendedFraction(percent, MAX_HIT_FRACTION, LEGACY_HIT_FRACTION, 0.90f, HARD_HIT_FRACTION);
}
inline double FrameFraction(float percent)
{
	return ExtendedFraction(percent, MAX_FRAME_FRACTION, LEGACY_FRAME_FRACTION, 0.95f, HARD_FRAME_FRACTION);
}

inline bool Finite(const CVector &v)
{
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline double HorizontalSpeed(const CVector &velocity)
{
	return std::sqrt(double(velocity.x)*velocity.x + double(velocity.y)*velocity.y);
}
inline CVector Opposing(double amount, const CVector &velocity, double speed)
{
	const double x = -double(velocity.x)*amount/speed;
	const double y = -double(velocity.y)*amount/speed;
	const double limit = std::numeric_limits<float>::max();
	if(!std::isfinite(x) || !std::isfinite(y) || std::fabs(x) > limit || std::fabs(y) > limit)
		return CVector(0.0f, 0.0f, 0.0f);
	return CVector(float(x), float(y), 0.0f);
}

inline CVector Impulse(float mass, const CVector &velocity, float gameImpulse, float percent = 100.0f,
	float bodyMassScale = 1.0f)
{
	if(!std::isfinite(mass) || mass <= 0.0f || !Finite(velocity) ||
		!std::isfinite(gameImpulse) || gameImpulse < MIN_GAME_IMPULSE ||
		!std::isfinite(bodyMassScale) || bodyMassScale <= 0.0f)
		return CVector(0.0f, 0.0f, 0.0f);
	const double speed = HorizontalSpeed(velocity);
	if(speed < MIN_CAR_SPEED) return CVector(0.0f, 0.0f, 0.0f);
	// The native proxy still reports a nominal-mass hit. Convert the extra
	// resistance estimate using the selected body mass and finite car mass;
	// the independent brake gain and existing per-hit/frame limits follow.
	double amount = double(gameImpulse)*50.0*IMPULSE_GAIN*mass*bodyMassScale/
		(double(mass) + EFFECTIVE_MASS*bodyMassScale)*Multiplier(percent);
	const double limit = double(mass)*speed*HitFraction(percent);
	if(amount > limit) amount = limit;
	return Opposing(amount, velocity, speed);
}

// One ledger is shared by every body. Overflow declines additional cars
// rather than allocating or evicting an already spent vehicle budget.
// Generation-bearing pool handles prevent a recycled car using old credit.
inline CVector Limit(Ledger &ledger, unsigned frame, int carHandle, float mass,
	const CVector &velocity, const CVector &requested, float percent = 100.0f)
{
	if(!ledger.initialized || ledger.frame != frame){
		ledger.frame = frame;
		ledger.count = 0;
		ledger.overflow = 0;
		ledger.initialized = true;
	}
	if(Multiplier(percent) == 0.0f || carHandle < 0 || !std::isfinite(mass) || mass <= 0.0f ||
		!Finite(velocity) || !Finite(requested)) return CVector(0.0f, 0.0f, 0.0f);
	const double speed = HorizontalSpeed(velocity);
	if(speed < MIN_CAR_SPEED) return CVector(0.0f, 0.0f, 0.0f);
	// If the car changes direction before commit, retain only resistance
	// along its current travel; a stale request cannot accelerate it.
	double amount = -(double(requested.x)*velocity.x + double(requested.y)*velocity.y)/speed;
	if(amount <= 0.0) return CVector(0.0f, 0.0f, 0.0f);
	int index = 0;
	for(; index < ledger.count; ++index) if(ledger.vehicles[index].handle == carHandle) break;
	if(index == ledger.count){
		if(ledger.count == MAX_VEHICLES){ ++ledger.overflow; return CVector(0.0f, 0.0f, 0.0f); }
		VehicleBudget &entry = ledger.vehicles[ledger.count++];
		entry.handle = carHandle;
		entry.remaining = double(mass)*speed*FrameFraction(percent);
	}
	VehicleBudget &entry = ledger.vehicles[index];
	// requested has already been scaled by Impulse or the residual-contact
	// caller. Only the safety caps scale here, so the knob is applied once.
	const double hitLimit = double(mass)*speed*HitFraction(percent);
	if(amount > hitLimit) amount = hitLimit;
	if(amount > entry.remaining) amount = entry.remaining;
	const CVector result = Opposing(amount, velocity, speed);
	if(result.x != 0.0f || result.y != 0.0f) entry.remaining -= amount;
	return result;
}
} // namespace VrRagdollBrake
