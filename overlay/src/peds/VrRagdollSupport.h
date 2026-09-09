#pragma once
#include "VrRagdollContacts.h"

namespace VrRagdollSupport {
struct State {
	int handle = -1;
	VrRagdollVehicle::Pose solved;
	float age = 0, speed = 0, turnSpeed = 0;
};
struct Transform {
	bool active = false;
	VrRagdollVehicle::Pose from, to;
	CVector Point(const CVector &p) const { return active ? to.ToWorld(from.ToLocal(p)) : p; }
	CVector Direction(const CVector &v) const {
		return active ? to.Direction(CVector(DotProduct(v,from.right),DotProduct(v,from.forward),DotProduct(v,from.up))) : v;
	}
	CVector InversePoint(const CVector &p) const { return active ? from.ToWorld(to.ToLocal(p)) : p; }
	CVector InverseDirection(const CVector &v) const {
		return active ? from.Direction(CVector(DotProduct(v,to.right),DotProduct(v,to.forward),DotProduct(v,to.up))) : v;
	}
};
// Remember only a genuine, approximately co-moving support. No attachment is
// applied to the simulation: slip, gravity and loss of support remain physical.
inline void Capture(State &support,const VrRagdollPhysics::State &body,
	const VrRagdollContacts::StepContacts &contacts,float debt)
{
	using namespace VrRagdollPhysics;
	if(!contacts.built || contacts.batch->count<=0){support=State();return;}
	int counts[VrRagdollVehicle::MAX_VEHICLES]={};
	float mass[VrRagdollVehicle::MAX_VEHICLES]={},slip[VrRagdollVehicle::MAX_VEHICLES]={};
	bool core[VrRagdollVehicle::MAX_VEHICLES]={};
	for(int i=0;i<SIM_BONES;++i){
		if(!contacts.valid[i] || !body.contact[i].active || body.contact[i].staticWorld) continue;
		const VrRagdollVehicle::Contact &c=contacts.contact[i];
		if(c.vehicleIndex<0 || c.vehicleIndex>=contacts.batch->count || c.normal.z<=0.35f) continue;
		const int v=c.vehicleIndex;const float m=1.0f/invMass[i];
		counts[v]++;mass[v]+=m;core[v]=core[v] || i<=RD_HEAD;
		slip[v]+=m*((body.pos[i]-body.prev[i])/STEP-c.surfaceVelocity).MagnitudeSqr();
	}
	int best=-1;
	for(int v=0;v<contacts.batch->count;++v)
		if(counts[v]>=3 && core[v] && slip[v]<=mass[v]*4.0f && (best<0 || mass[v]>mass[best])) best=v;
	if(best<0){support=State();return;}
	const VrRagdollVehicle::Vehicle &car=contacts.batch->vehicles[best];
	support.handle=car.handle;support.solved=car.sweep[car.sweepSegments];
	support.age=MaxF(0.0f,debt);support.speed=car.linearVelocity.Magnitude();support.turnSpeed=car.angularVelocity.Magnitude();
}
inline Transform Build(const State &support,const VrRagdollVehicle::Pose &current)
{
	Transform result;
	if(support.handle<0 || support.age>0.1f) return result;
	// Discontinuities are not platform motion: never teleport a corpse when a
	// vehicle is reset, abruptly rotated, or its physical prediction was invalid.
	const float limit=0.25f+(support.speed+5.0f)*support.age;
	if((current.origin-support.solved.origin).MagnitudeSqr()>limit*limit ||
	   DotProduct(current.right,support.solved.right)<0.9f ||
	   DotProduct(current.forward,support.solved.forward)<0.9f ||
	   DotProduct(current.up,support.solved.up)<0.9f) return result;
	result.active=true;result.from=support.solved;result.to=current;return result;
}
inline float BoundsPadding(const State &support,float bodyRadius,float frameTime)
{
	if(support.handle<0 || support.age>0.1f) return 0;
	const float interval=VrRagdollPhysics::MinF(0.1f,support.age+frameTime);
	return 0.25f+(support.speed+5.0f+support.turnSpeed*(3.0f+bodyRadius))*interval;
}
}
