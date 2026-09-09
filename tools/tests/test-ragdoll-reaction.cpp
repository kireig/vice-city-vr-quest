#include "ragdoll-test-vector.h"
#define VR_RAGDOLL_VEHICLE_NO_ENGINE
#include "VrRagdollContacts.h"
#include <cstdio>
#include <cstdlib>
namespace P = VrRagdollPhysics;
namespace R = VrRagdollReaction;
namespace V = VrRagdollVehicle;
namespace C = VrRagdollContacts;
static unsigned checks;
static void Check(bool value, const char *message)
{
	++checks;
	if(!value){ std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static bool Near(float a, float b, float epsilon = .0002f) { return std::fabs(a-b) < epsilon; }
static float AddBody(R::Frame &frame, int handle, float carMass, float speed, int body, float scale = 1.0f)
{
	float mass = 0;
	for(int i = 0; i < P::SIM_BONES; ++i){
		float particle = 1.0f/P::invMass[i]; mass += particle;
		R::Add(frame, handle, carMass, CVector(speed,0,0), body, i, particle,
			CVector(1,0,0), CVector(speed,0,0), CVector(0,0,0), scale);
	}
	return mass * scale;
}
static void MassAndEngineUnits()
{
	const float masses[] = {800,1000,2000}, speeds[] = {5,20,40};
	for(int m = 0; m < 3; ++m) for(int speed = 0; speed < 3; ++speed){
		R::Frame f;
		float bodyMass = AddBody(f, 7, masses[m], speeds[speed], 0);
		Check(f.count == 1 && f.pointImpacts == unsigned(P::SIM_BONES), "one body mass counted once for a car");
		CVector impulse = R::Impulse(f.vehicles[0], masses[m], CVector(speeds[speed],0,0));
		float expectedLoss = bodyMass * speeds[speed] / (masses[m] + bodyMass);
		Check(Near(-impulse.x/masses[m], expectedLoss), "finite-mass speed loss matches 70kg pedestrian momentum");
		Check(impulse.y == 0 && impulse.z == 0, "reaction cannot create lateral or vertical car impulse");
		// Production ApplyMoveForce adds Jengine / mass to units-per-tick.
		float engineSpeed = speeds[speed]/50.0f + impulse.x/(50.0f*masses[m]);
		Check(Near(engineSpeed*50.0f, speeds[speed]-expectedLoss), "Ns to CPhysical impulse uses exact divide by50");
		std::printf("reaction car=%.0fkg body=%.0fkg speed=%.0fm/s loss=%.4fm/s\n",masses[m],bodyMass,speeds[speed],expectedLoss);
	}
	R::Frame f;
	float partialMass = AddBody(f, 7, 1000, 30, 0, .5f);
	Check(Near(-R::Impulse(f.vehicles[0],1000,CVector(30,0,0)).x/1000, partialMass*30/(1000+partialMass)), "live-ped supplemental scale is half effective body mass");
}
static void AggregateCapsAndDuplicates()
{
	R::Frame f;
	for(int step=0;step<3;++step) for(int body=0;body<R::MAX_BODIES;++body) AddBody(f,1,400,40,body);
	Check(f.pointImpacts == R::MAX_BODIES*P::SIM_BONES && f.duplicates == 2*R::MAX_BODIES*P::SIM_BONES, "catch-up steps cannot count same particles repeatedly");
	CVector impulse = R::Impulse(f.vehicles[0],400,CVector(40,0,0));
	Check(Near(-impulse.x/400,40*R::MAX_SPEED_FRACTION), "all six slots share one maximum car speed loss budget");
	CVector now(0,12,4);
	impulse = R::Impulse(f.vehicles[0],400,now);
	Check(impulse.x==0 && impulse.y<0 && impulse.z==0 && now.y+impulse.y/400>0, "commit follows current horizontal direction and cannot reverse");
	f.committed = true;
	Check(!R::Add(f,1,400,CVector(40,0,0),0,0,16,CVector(1,0,0),CVector(40,0,0),CVector(0,0,0),1), "sealed frame cannot add more braking after commit");
	R::BeginFrame(f);
	Check(f.count==0 && f.pointImpacts==0 && !f.committed, "new frame resets bounded accumulator");
	for(int car=0;car<R::MAX_VEHICLES+3;++car) AddBody(f,car,1000,20,0);
	Check(f.count==R::MAX_VEHICLES && f.overflow==3*P::SIM_BONES, "reaction vehicle capacity cannot grow without bound");
}
static void NoPhantomBraking()
{
	R::Frame f;
	for(int frame=0;frame<300;++frame){
		R::BeginFrame(f);
		Check(!R::Add(f,1,1000,CVector(20,0,0),0,0,16,CVector(1,0,0),CVector(20,0,0),CVector(20,0,0),1), "co-moving contact creates no repeated brake");
		Check(!R::Add(f,1,1000,CVector(20,0,0),0,0,16,CVector(1,0,0),CVector(20,0,0),CVector(19.8f,0,0),1), "small throttle and solver damping differences do not brake continuously");
		Check(!R::Add(f,1,1000,CVector(20,0,0),0,0,16,CVector(0,0,1),CVector(20,0,0),CVector(0,0,-20),1), "roof gravity or vertical impact cannot slow horizontal car");
		Check(!R::Add(f,1,1000,CVector(0,0,0),0,0,16,CVector(1,0,0),CVector(0,0,0),CVector(-20,0,0),1), "parked or deeply overlapping car is not propelled");
		Check(!R::Add(f,1,1000,CVector(20,0,0),0,0,16,CVector(-1,0,0),CVector(20,0,0),CVector(40,0,0),1), "rear surface or separating traffic cannot create propulsion");
		Check(f.count==0, "non-impact contacts leave accumulator empty");
	}
}
static void ActualContactCallback()
{
	P::State state={};
	for(int i=0;i<P::SIM_BONES;++i) state.pos[i]=state.prev[i]=CVector(0,0,.4f);
	V::Batch batch; batch.count=1;
	V::Vehicle &v=batch.vehicles[0];
	v.handle=17; v.mass=1000;
	v.pose.origin=CVector(-.3f,0,0); v.pose.right=CVector(1,0,0); v.pose.forward=CVector(0,1,0); v.pose.up=CVector(0,0,1);
	v.linearVelocity=v.surfaceLinearVelocity=CVector(20,0,0);
	v.angularVelocity=v.surfaceAngularVelocity=CVector(0,0,0);
	v.boundCenter=CVector(0,0,.4f); v.boundRadius=1.0f;
	v.shapeCount=1; v.shapes[0].radius=0; v.shapes[0].min=CVector(-.1f,-.6f,0); v.shapes[0].max=CVector(.1f,.6f,.8f); v.shapes[0].center=CVector(0,0,.4f);
	V::Prepare(batch,0,P::STEP);
	R::Frame reaction;
	C::StepContacts context(&batch,&reaction,&state,0,1.0f);
	// Simulate arbitrary solver corrections after constructor: they must
	// not replace the captured physical velocity with depenetration speed.
	for(int i=0;i<P::SIM_BONES;++i) state.pos[i].x+=.02f;
	for(int pass=0;pass<4;++pass) C::Project(&state,&context);
	Check(context.count==P::SIM_BONES && reaction.pointImpacts==P::SIM_BONES, "production cached-plane callback emits reaction only once");
	Check(Near(reaction.vehicles[0].rawImpulse,70.0f*20.0f,.002f), "reaction uses pre-constraint incoming momentum");
	Check(Near(-R::Impulse(reaction.vehicles[0],1000,CVector(20,0,0)).x/1000,70.0f*20/1070), "production callback produces expected car slowing");
}
int main()
{
	MassAndEngineUnits(); AggregateCapsAndDuplicates(); NoPhantomBraking(); ActualContactCallback();
	std::printf("PASS: ragdoll vehicle reaction, %u checks\n",checks);
}
