#pragma once

// CVector is supplied by the game (or the host physics fixture). Impulses
// are kg * world units / second; masses belong to the simulated points.
namespace VrRagdollImpact
{
static const float BURST_IMPULSE = 24.0f;
static const float IMPULSE_PER_SECOND = 60.0f;
static const float MAX_POINT_DELTA_SPEED = 4.0f;

inline float
Refill(float available, float dt)
{
	const float next = available + (dt > 0.0f ? dt : 0.0f)*IMPULSE_PER_SECOND;
	return next < BURST_IMPULSE ? next : BURST_IMPULSE;
}

inline float
Take(float &available, float requested)
{
	if(requested <= 0.0f || available <= 0.0f)
		return 0.0f;
	const float taken = requested < available ? requested : available;
	available -= taken;
	return taken;
}

inline void
ApplyBoneImpulse(CVector &prevA, CVector &prevB,
                 float inverseMassA, float inverseMassB,
                 float fractionAlongBone, const CVector &direction,
                 float impulse, float step)
{
	const float t = fractionAlongBone < 0.0f ? 0.0f :
		(fractionAlongBone > 1.0f ? 1.0f : fractionAlongBone);
	float deltaA = impulse*(1.0f-t)*inverseMassA;
	float deltaB = impulse*t*inverseMassB;
	const float largest = deltaA > deltaB ? deltaA : deltaB;
	if(largest > MAX_POINT_DELTA_SPEED){
		const float scale = MAX_POINT_DELTA_SPEED/largest;
		deltaA *= scale;
		deltaB *= scale;
	}
	prevA -= direction*(deltaA*step);
	prevB -= direction*(deltaB*step);
}
}
