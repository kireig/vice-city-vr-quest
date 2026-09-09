#include "ragdoll-test-vector.h"
#include "VrRagdollImpact.h"
#include "VrRagdollPhysics.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>

static void Check(bool condition, const char *message)
{
	if(!condition){ std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

namespace P = VrRagdollPhysics;

static P::State RestingBody()
{
	P::State s = {};
	const CVector points[P::SIM_BONES] = {
		{0,0,.94f}, {0,0,1.10f}, {0,0,1.31f}, {0,0,1.53f}, {0,0,1.69f},
		{-.12f,0,1.43f}, {-.26f,0,1.45f}, {-.43f,-.035f,1.20f}, {-.53f,.03f,.98f},
		{.12f,0,1.43f}, {.26f,0,1.45f}, {.43f,-.035f,1.20f}, {.53f,.03f,.98f},
		{-.13f,0,.89f}, {-.14f,.035f,.48f}, {-.14f,-.04f,.1f},
		{.13f,0,.89f}, {.14f,.035f,.48f}, {.14f,-.04f,.1f}
	};
	for(int i=0;i<P::SIM_BONES;i++)
		s.pos[i]=CVector(points[i].z,-points[i].x,points[i].y+.65f);
	P::Initialize(&s,CVector(0,-1,0),CVector(1,0,0));
	s.groundZ=0;
	for(int frame=0;frame<900 && !s.asleep;frame++) P::Step(&s);
	Check(s.asleep,"integration fixture settles before being shot");
	return s;
}

static void TestGroundedShots()
{
	const P::State rested=RestingBody();
	const int endpoints[][2]={
		{P::RD_SPINE,P::RD_SPINE1}, {P::RD_NECK,P::RD_HEAD},
		{P::RD_LFARM,P::RD_LHAND}, {P::RD_LCALF,P::RD_LFOOT}
	};
	for(int shot=0;shot<4;shot++){
		P::State s=rested;
		const int a=endpoints[shot][0], b=endpoints[shot][1];
		const CVector centre=P::CentreOfMass(&s);
		const CVector direction=P::Unit(CVector(0,1,-.5f),CVector(0,1,0));
		VrRagdollImpact::ApplyBoneImpulse(s.prev[a],s.prev[b],P::invMass[a],P::invMass[b],
			.65f,direction,2.4f,P::STEP);
		P::Wake(&s);
		float local=0, translation=0, retainedFirst=0;
		for(int frame=0;frame<90;frame++){
			P::Step(&s);
			const CVector comShift=P::CentreOfMass(&s)-centre;
			translation=P::MaxF(translation,comShift.Magnitude());
			local=P::MaxF(local,(s.pos[b]-rested.pos[b]-comShift).Magnitude());
			if(frame==0) retainedFirst=(s.pos[b]-s.prev[b]).Magnitude()/P::STEP;
			for(int node=0;node<P::SIM_BONES;node++)
				Check(s.pos[node].z>=P::radius[node]-.00001f,"local bullet reaction respects ground contact");
		}
		std::printf("grounded-shot%d local=%.6fm COM=%.6fm first-speed=%.4fm/s\n",shot,local,translation,retainedFirst);
		Check(translation<.01f,"single shot articulates locally without launching the whole body");
		if(shot==1) Check(retainedFirst>.08f && local>.006f,"head hit retains visible multi-tick motion after contact response");
		if(shot>=2) Check(local>.06f && local<.3f,"grounded limb hit moves the struck chain within bounded range");
		Check(s.asleep,"body settles again after local shot");
	}
	P::State down=rested;
	VrRagdollImpact::ApplyBoneImpulse(down.prev[P::RD_NECK],down.prev[P::RD_HEAD],
		P::invMass[P::RD_NECK],P::invMass[P::RD_HEAD],.65f,CVector(0,0,-1),4,P::STEP);
	P::Wake(&down);
	for(int frame=0;frame<60;frame++){
		P::Step(&down);
		Check(down.pos[P::RD_HEAD].z<rested.pos[P::RD_HEAD].z+.005f,
			"downward grounded head shot does not invent an upward recoil");
	}
}

static void TestSustainedFireThroughSolver()
{
	P::State s=RestingBody();
	const CVector centre=P::CentreOfMass(&s);
	const CVector direction=P::Unit(CVector(0,1,-.2f),CVector(0,1,0));
	float credit=VrRagdollImpact::BURST_IMPULSE;
	float peakSpeed=0, peakError=0, maxShift=0;
	for(int frame=0;frame<600;frame++){
		credit=VrRagdollImpact::Refill(credit,P::STEP);
		const float impulse=VrRagdollImpact::Take(credit,2.5f);
		VrRagdollImpact::ApplyBoneImpulse(s.prev[P::RD_LFARM],s.prev[P::RD_LHAND],
			P::invMass[P::RD_LFARM],P::invMass[P::RD_LHAND],.8f,direction,impulse,P::STEP);
		P::Wake(&s); P::Step(&s);
		peakSpeed=P::MaxF(peakSpeed,std::sqrt(s.lastMaxSpeedSq));
		peakError=P::MaxF(peakError,s.lastMaxLengthError);
		maxShift=P::MaxF(maxShift,(P::CentreOfMass(&s)-centre).Magnitude());
		for(int node=0;node<P::SIM_BONES;node++){
			Check(std::isfinite(s.pos[node].MagnitudeSqr()),"sustained impacts remain finite");
			Check(s.pos[node].z>=P::radius[node]-.00001f,"sustained impacts cannot penetrate the floor");
		}
	}
	std::printf("sustained-grounded-fire peak-speed=%.3fm/s max-edge-error=%.3f COM-shift=%.3fm\n",peakSpeed,peakError,maxShift);
	Check(peakSpeed<12 && peakError<.20f,"rate-limited rapid fire cannot explode the articulated chain");
	Check(maxShift<.25f,"rapid limb hits cannot carry away the heavy grounded torso");
	for(int frame=0;frame<900 && !s.asleep;frame++) P::Step(&s);
	Check(s.asleep,"rapid-fire body settles after shooting stops");
}

int main()
{
	TestGroundedShots(); TestSustainedFireThroughSolver();
	const float step = 1.0f/60.0f;
	CVector a(0,0,0), b(0,0,0);
	const CVector dir(0,1,0);
	VrRagdollImpact::ApplyBoneImpulse(a,b,1.0f/8.0f,1.0f/2.0f,0.25f,dir,4.0f,step);
	const float speedA = -a.y/step, speedB = -b.y/step;
	Check(std::fabs(8.0f*speedA+2.0f*speedB-4.0f)<0.00001f,
		"distributed hit conserves the supplied linear impulse");
	Check(std::fabs(speedA-0.375f)<0.00001f && std::fabs(speedB-0.5f)<0.00001f,
		"heavier body points respond according to inverse mass");

	a=b=CVector(0,0,0);
	VrRagdollImpact::ApplyBoneImpulse(a,b,1,1,0,dir,100,step);
	Check(std::fabs(-a.y/step-VrRagdollImpact::MAX_POINT_DELTA_SPEED)<0.00001f,
		"large impact cannot launch a light endpoint with unbounded velocity");
	Check(b.MagnitudeSqr()==0,"endpoint hit does not directly kick the remote end");

	float credit=VrRagdollImpact::BURST_IMPULSE;
	float total=0;
	for(int i=0;i<100;i++) total+=VrRagdollImpact::Take(credit,2.0f);
	Check(std::fabs(total-VrRagdollImpact::BURST_IMPULSE)<0.00001f && credit==0,"same-frame pellet burst is bounded");
	for(int frame=0;frame<60;frame++){
		credit=VrRagdollImpact::Refill(credit,step);
		total+=VrRagdollImpact::Take(credit,10.0f);
	}
	Check(std::fabs(total-(VrRagdollImpact::BURST_IMPULSE+VrRagdollImpact::IMPULSE_PER_SECOND))<0.001f,"sustained fire respects impulse-per-second budget");
	credit=VrRagdollImpact::Refill(credit,100.0f);
	Check(credit==VrRagdollImpact::BURST_IMPULSE,"long idle time cannot accumulate an oversized first hit");
	Check(VrRagdollImpact::Take(credit,-1.0f)==0,"invalid negative impulse is ignored");
	std::puts("ragdoll impact: momentum, mass, burst and sustained-fire limits PASS");
	return 0;
}
