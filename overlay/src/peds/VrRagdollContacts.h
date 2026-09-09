#pragma once

#include "VrRagdollPhysics.h"
#include "VrRagdollVehicle.h"

namespace VrRagdollContacts
{
struct StepContacts {
	const VrRagdollVehicle::Batch *batch;
	VrRagdollVehicle::Contact contact[VrRagdollPhysics::SIM_BONES];
	bool valid[VrRagdollPhysics::SIM_BONES];
	bool built;
	unsigned count;
	VrRagdollReaction::Frame *reaction;
	CVector incoming[VrRagdollPhysics::SIM_BONES];
	int bodySlot;
	float reactionScale;
	float massScale;
	explicit StepContacts(const VrRagdollVehicle::Batch *b) : batch(b), built(false), count(0),
		reaction(0), bodySlot(-1), reactionScale(0.0f), massScale(VrRagdollSettings::WeightScale()) {}
	StepContacts(const VrRagdollVehicle::Batch *b, VrRagdollReaction::Frame *frame,
		const VrRagdollPhysics::State *state, int slot, float scale,
		float timeStep = VrRagdollPhysics::STEP) : batch(b), built(false), count(0),
		reaction(frame), bodySlot(slot), reactionScale(scale), massScale(VrRagdollSettings::WeightScale())
	{
		// Capture before Step overwrites prev and constraints alter positions.
		const float damping = timeStep == VrRagdollPhysics::STEP ? VrRagdollPhysics::DAMPING :
			powf(VrRagdollPhysics::DAMPING,timeStep/VrRagdollPhysics::STEP);
		for(int i = 0; i < VrRagdollPhysics::SIM_BONES; ++i){
			incoming[i] = (state->pos[i] - state->prev[i]) * (damping / VrRagdollPhysics::STEP);
			// Reaction measures pre-impact momentum. Current-step gravity is
			// support load, not a fresh collision that should brake the car.
		}
	}
};

// CCD is evaluated once per point per simulation step. Constraint passes
// reuse the resulting local contact planes until the next step.
inline void Project(VrRagdollPhysics::State *state, void *opaque)
{
	StepContacts &context = *static_cast<StepContacts*>(opaque);
	if(!context.built){
		for(int i = 0; i < VrRagdollPhysics::SIM_BONES; i++){
			context.valid[i] = VrRagdollVehicle::Find(*context.batch,
				state->prev[i], state->pos[i], VrRagdollPhysics::radius[i], context.contact[i]);
			if(context.valid[i]){
				context.count++;
				const VrRagdollVehicle::Contact &c = context.contact[i];
				if(context.reaction && c.vehicleIndex >= 0 && c.vehicleIndex < context.batch->count){
					const VrRagdollVehicle::Vehicle &car = context.batch->vehicles[c.vehicleIndex];
					VrRagdollReaction::Add(*context.reaction, car.handle, car.mass, car.linearVelocity,
						context.bodySlot, i, context.massScale / VrRagdollPhysics::invMass[i], c.normal,
						c.surfaceVelocity, context.incoming[i], context.reactionScale);
				}
			}
		}
		context.built = true;
	}
	for(int i = 0; i < VrRagdollPhysics::SIM_BONES; i++){
		if(!context.valid[i])
			continue;
		const VrRagdollVehicle::Contact &c = context.contact[i];
		const float depth = DotProduct(c.center-state->pos[i], c.normal);
		if(depth > 0.0f)
			state->pos[i] += c.normal*depth;
		// A joint can pull the point away from a previously active plane.
		// Do not retain that plane's velocity/friction response once separated.
		state->contact[i].active = depth >= -0.005f;
		if(state->contact[i].active)
			VrRagdollPhysics::RecordContact(state, i, c.normal, c.surfaceVelocity);
	}
}

inline bool ShouldWake(const VrRagdollPhysics::State *state,
	const VrRagdollVehicle::Batch &batch)
{
	for(int i = 0; i < VrRagdollPhysics::SIM_BONES; i++){
		VrRagdollVehicle::Contact c;
		if(VrRagdollVehicle::Find(batch, state->pos[i], state->pos[i],
			VrRagdollPhysics::radius[i], c) &&
			(c.swept || c.penetration > 0.015f || c.surfaceVelocity.MagnitudeSqr() > 0.01f))
			return true;
	}
	return false;
}
}
