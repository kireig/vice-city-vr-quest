#include "ragdoll-test-vector.h"
#define VR_RAGDOLL_VEHICLE_NO_ENGINE
#include "VrRagdollContacts.h"
#include <cstdio>
#include <cstdlib>

namespace P = VrRagdollPhysics;
namespace V = VrRagdollVehicle;
namespace C = VrRagdollContacts;
static int checks;
static void Check(bool okay, const char *message)
{
	++checks;
	if(!okay){ std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static CVector Rotate(const CVector &v, const CVector &axis, float angle)
{
	return v * std::cos(angle) + CrossProduct(axis, v) * std::sin(angle) +
		axis * (DotProduct(axis, v) * (1.0f - std::cos(angle)));
}
static P::State Body(float lift)
{
	P::State s = {};
	const CVector points[P::SIM_BONES] = {
		{0, 0, .94f}, {0, 0, 1.10f}, {0, 0, 1.31f}, {0, 0, 1.53f}, {0, 0, 1.69f},
		{-.12f, 0, 1.43f}, {-.26f, 0, 1.45f}, {-.43f, -.035f, 1.20f}, {-.53f, .03f, .98f},
		{.12f, 0, 1.43f}, {.26f, 0, 1.45f}, {.43f, -.035f, 1.20f}, {.53f, .03f, .98f},
		{-.13f, 0, .89f}, {-.14f, .035f, .48f}, {-.14f, -.04f, .1f},
		{.13f, 0, .89f}, {.14f, .035f, .48f}, {.14f, -.04f, .1f}
	};
	const CVector axis = P::Unit(CVector(1, .3f, .1f), CVector(1, 0, 0));
	for(int i = 0; i < P::SIM_BONES; ++i)
		s.pos[i] = Rotate(points[i], axis, 1.5707963f) + CVector(0, 0, lift);
	P::Initialize(&s, Rotate(CVector(1, 0, 0), axis, 1.5707963f), Rotate(CVector(0, 0, 1), axis, 1.5707963f));
	s.groundZ = 0.0f;
	return s;
}
static V::Batch Car(const CVector &origin, const CVector &lo, const CVector &hi, const CVector &velocity)
{
	V::Batch b;
	b.count = 1;
	V::Vehicle &v = b.vehicles[0];
	v.pose.origin = origin;
	v.pose.right = CVector(1, 0, 0); v.pose.forward = CVector(0, 1, 0); v.pose.up = CVector(0, 0, 1);
	v.linearVelocity = v.surfaceLinearVelocity = velocity;
	v.angularVelocity = v.surfaceAngularVelocity = CVector(0, 0, 0);
	v.shapeCount = 1;
	v.shapes[0].min = lo; v.shapes[0].max = hi; v.shapes[0].radius = 0.0f;
	v.shapes[0].center = (lo + hi) * .5f;
	v.boundCenter = v.shapes[0].center;
	v.boundRadius = (hi - v.boundCenter).Magnitude();
	V::Prepare(b, 0.0f, P::STEP);
	return b;
}
static unsigned Advance(P::State &s, V::Batch &batch)
{
	V::Prepare(batch, 0.0f, P::STEP);
	if(s.asleep && C::ShouldWake(&s, batch)) P::Wake(&s);
	C::StepContacts context(&batch);
	P::Step(&s, C::Project, &context);
	for(int n = 0; n < batch.count; ++n){
		V::Vehicle &v = batch.vehicles[n];
		v.pose = V::PoseAt(v, P::STEP);
	}
	for(int i = 0; i < P::SIM_BONES; ++i){
		Check(std::isfinite(s.pos[i].MagnitudeSqr()), "integrated contacts keep body finite");
		Check(s.pos[i].z >= s.groundZ + P::radius[i] - .00001f, "vehicle contacts finish above ground");
	}
	return context.count;
}
static CVector MeanVelocity(const P::State &s)
{
	CVector momentum(0, 0, 0);
	float mass = 0;
	for(int i = 0; i < P::SIM_BONES; ++i){
		momentum += (s.pos[i] - s.prev[i]) / (P::STEP * P::invMass[i]);
		mass += 1.0f / P::invMass[i];
	}
	return momentum / mass;
}
static P::State FloorBody()
{
	P::State s = Body(.8f);
	for(int frame = 0; frame < 900; ++frame) P::Step(&s);
	Check(s.asleep, "test body initially rests asleep on ground");
	return s;
}

static void CarPushesSleepingBody()
{
	P::State s = FloorBody();
	const CVector start = P::CentreOfMass(&s);
	V::Batch batch = Car(CVector(start.x - 3.0f, start.y, 0),
		CVector(-.8f, -2.0f, 0), CVector(.8f, 2.0f, 1.0f), CVector(6, 0, 0));
	float peakSpeed = 0, peakHeight = 0, peakError = 0;
	unsigned contacts = 0;
	bool awakened = false;
	for(int frame = 0; frame < 150; ++frame){
		contacts += Advance(s, batch);
		awakened = awakened || !s.asleep;
		peakSpeed = P::MaxF(peakSpeed, std::sqrt(s.lastMaxSpeedSq));
		peakError = P::MaxF(peakError, s.lastMaxLengthError);
		for(int i = 0; i < P::SIM_BONES; ++i) peakHeight = P::MaxF(peakHeight, s.pos[i].z);
	}
	const CVector displacement = P::CentreOfMass(&s) - start;
	std::printf("car-push contacts=%u displacement=(%.3f,%.3f,%.3f) peak-speed=%.3f peak-height=%.3f max-length-error=%.3f final-error=%.3f\n",
		contacts, displacement.x, displacement.y, displacement.z, peakSpeed, peakHeight, peakError, s.lastMaxLengthError);
	Check(contacts > 0 && awakened, "moving car wakes and contacts sleeping corpse");
	Check(displacement.x > 3.0f, "car pushes corpse along road");
	Check(peakHeight < 1.8f, "side car push does not vertically catapult corpse");
	Check(peakSpeed < 20.0f, "side car push does not accumulate unbounded velocity");
	Check(peakError < .25f && s.lastMaxLengthError < .10f, "car push keeps segment lengths bounded and recovers");
}

static void StaticCarDoesNotWakeAdjacentBody()
{
	P::State s = FloorBody();
	float edge = -1000.0f;
	for(int i = 0; i < P::SIM_BONES; ++i) edge = P::MaxF(edge, s.pos[i].x + P::radius[i]);
	V::Batch batch = Car(CVector(edge + .002f, 0, 0),
		CVector(0, -4, 0), CVector(1, 4, 1), CVector(0, 0, 0));
	const CVector start = P::CentreOfMass(&s);
	for(int frame = 0; frame < 120; ++frame){
		V::Prepare(batch, 0.0f, P::STEP);
		Check(!C::ShouldWake(&s, batch), "adjacent parked car does not wake settled body at contact skin");
		Advance(s, batch);
	}
	Check(s.asleep && (P::CentreOfMass(&s) - start).MagnitudeSqr() == 0.0f, "adjacent static car preserves exact sleeping rest");
}

static void SupportMotionAndRemoval()
{
	P::State s = Body(1.6f);
	V::Batch batch = Car(CVector(0, 0, 0), CVector(-4, -4, 0), CVector(4, 4, .8f), CVector(0, 0, 0));
	float latePeakSpeed = 0;
	for(int frame = 0; frame < 900; ++frame){
		Advance(s, batch);
		if(frame > 800) latePeakSpeed = P::MaxF(latePeakSpeed, std::sqrt(s.lastMaxSpeedSq));
	}
	std::printf("parked-roof support=%d awake=%d late-speed=%.5f height=%.3f\n",
		s.lastSupportCount, !s.asleep, latePeakSpeed, P::CentreOfMass(&s).z);
	Check(!s.asleep && s.lastSupportCount >= 3, "external support remains awake so departing car cannot leave body suspended");
	Check(latePeakSpeed < .20f, "stationary car support does not add repeated acceleration");
	Check(P::CentreOfMass(&s).z > .9f, "car roof supports body above ground");
	batch.vehicles[0].linearVelocity = batch.vehicles[0].surfaceLinearVelocity = CVector(2, 0, 0);
	float lateMaxSpeed = 0;
	for(int frame = 0; frame < 240; ++frame){
		Advance(s, batch);
		if(frame > 120) lateMaxSpeed = P::MaxF(lateMaxSpeed, MeanVelocity(s).Magnitude());
	}
	CVector matched = MeanVelocity(s);
	std::printf("moving-roof mean-speed=(%.4f,%.4f,%.4f) late-peak=%.4f height=%.3f\n",
		matched.x, matched.y, matched.z, lateMaxSpeed, P::CentreOfMass(&s).z);
	Check(matched.x > 1.8f && matched.x < 2.2f && lateMaxSpeed < 2.3f, "resting body follows car speed without repeated kicks");
	const float before = P::CentreOfMass(&s).z;
	batch.count = 0;
	for(int frame = 0; frame < 120; ++frame) Advance(s, batch);
	Check(P::CentreOfMass(&s).z < before - .5f, "body falls when moving vehicle support disappears");
}

static void FastBodyHitsParkedCar()
{
	V::Batch batch = Car(CVector(0, 0, 0), CVector(-.8f, -1, 0), CVector(.8f, 1, 1), CVector(0, 0, 0));
	V::Contact contact;
	Check(V::Find(batch, CVector(-3, 0, .5f), CVector(3, 0, .5f), .1f, contact), "fast body crossing parked car is swept");
	Check(contact.swept && contact.normal.x < -.99f && contact.center.x < -.9f, "parked car preserves incoming entry side");
}

static void DeepInitialOverlapDoesNotLaunch()
{
	P::State s = Body(.5f);
	// A streamed/repositioned collider can initially overlap the body. Its
	// geometric separation is not a physical 1/60-second launch velocity.
	V::Batch batch = Car(CVector(0, 0, 0), CVector(-4, -4, -2), CVector(4, 4, .8f), CVector(0, 0, 0));
	Advance(s, batch);
	float largestOutward = 0;
	int contactCount = 0;
	for(int i = 0; i < P::SIM_BONES; ++i){
		if(!s.contact[i].active) continue;
		const CVector velocity = (s.pos[i] - s.prev[i]) / P::STEP;
		largestOutward = P::MaxF(largestOutward, DotProduct(velocity, s.contact[i].normal));
		++contactCount;
	}
	std::printf("deep-static-overlap contacts=%d peak-outward-speed=%.4f\n", contactCount, largestOutward);
	Check(contactCount > 0, "deep overlap actually generated cached contact planes");
	Check(largestOutward < .01f, "initial static depenetration does not become launch velocity");
}

static void Quad(V::Vehicle &v, const CVector &a, const CVector &b, const CVector &c, const CVector &d)
{
	Check(v.triangleCount + 2 <= V::MAX_TRIANGLES, "integrated car mesh within face budget");
	Check(V::MakeTriangle(a, b, c, v.triangles[v.triangleCount++]), "integrated mesh first triangle valid");
	Check(V::MakeTriangle(a, c, d, v.triangles[v.triangleCount++]), "integrated mesh second triangle valid");
}
static V::Batch MeshCar(const CVector &velocity = CVector(0, 0, 0))
{
	V::Batch batch = Car(CVector(0, 0, 0), CVector(-.85f, -2, .1f), CVector(.85f, 2, .45f), velocity);
	V::Vehicle &v = batch.vehicles[0];
	v.boundCenter = CVector(0, 0, .7f); v.boundRadius = 2.4f;
	Quad(v, CVector(-.8f, 1.1f, .65f), CVector(.8f, 1.1f, .65f), CVector(.8f, 2, .65f), CVector(-.8f, 2, .65f));
	Quad(v, CVector(-.8f, .6f, 1.3f), CVector(.8f, .6f, 1.3f), CVector(.8f, 1.1f, .65f), CVector(-.8f, 1.1f, .65f));
	Quad(v, CVector(-.8f, -1.2f, 1.3f), CVector(.8f, -1.2f, 1.3f), CVector(.8f, .6f, 1.3f), CVector(-.8f, .6f, 1.3f));
	Quad(v, CVector(-.8f, -1.2f, .45f), CVector(-.8f, -1.2f, 1.3f), CVector(-.8f, .6f, 1.3f), CVector(-.8f, 1.1f, .65f));
	Quad(v, CVector(.8f, -1.2f, .45f), CVector(.8f, 1.1f, .65f), CVector(.8f, .6f, 1.3f), CVector(.8f, -1.2f, 1.3f));
	V::Prepare(batch, 0, P::STEP);
	return batch;
}
static void HighSpeedThinRoof()
{
	const float speeds[] = {20, 30, 40};
	for(int test = 0; test < 3; ++test){
		P::State s = Body(1.7f);
		for(int i = 0; i < P::SIM_BONES; ++i){
			s.pos[i].y += .5f;
			s.prev[i] = s.pos[i] + CVector(0, 0, speeds[test] * P::STEP);
		}
		V::Batch batch = MeshCar();
		unsigned contacts = 0;
		float minimumRoofClearance = 10.0f, peakSpeed = 0;
		for(int frame = 0; frame < 30; ++frame){
			contacts += Advance(s, batch);
			peakSpeed = P::MaxF(peakSpeed, std::sqrt(s.lastMaxSpeedSq));
			for(int i = 0; i <= P::RD_HEAD; ++i){
				if(std::fabs(s.pos[i].x) < .65f && s.pos[i].y > -1.1f && s.pos[i].y < .5f)
					minimumRoofClearance = P::MinF(minimumRoofClearance, s.pos[i].z - 1.3f - P::radius[i]);
			}
		}
		std::printf("thin-mesh-roof speed=%.0f contacts=%u min-core-clearance=%.5f peak-speed=%.3f final-COM-z=%.3f\n",
			speeds[test], contacts, minimumRoofClearance, peakSpeed, P::CentreOfMass(&s).z);
		Check(contacts > 0, "fast falling body reaches actual roof mesh");
		Check(minimumRoofClearance >= -.005f, "20-40m/s body core cannot pass through thin cabin roof");
		Check(peakSpeed < speeds[test] + 5.0f, "mesh roof contact does not create additional high speed launch");
	}
}
static void HighSpeedUpperCar()
{
	const float speeds[] = {20, 30, 40};
	for(int test = 0; test < 3; ++test){
		P::State s = Body(.9f);
		for(int i = 0; i < P::SIM_BONES; ++i){
			s.pos[i].y += 2.6f;
			s.prev[i] = s.pos[i];
		}
		V::Batch batch = MeshCar(CVector(0, speeds[test], 0));
		batch.vehicles[0].pose.origin.y = -1.3f;
		unsigned contacts = 0;
		int interiorCoreFrames = 0;
		float peakSpeed = 0, peakHeight = 0;
		for(int frame = 0; frame < 40; ++frame){
			contacts += Advance(s, batch);
			peakSpeed = P::MaxF(peakSpeed, std::sqrt(s.lastMaxSpeedSq));
			for(int i = 0; i < P::SIM_BONES; ++i) peakHeight = P::MaxF(peakHeight, s.pos[i].z);
			for(int i = 0; i <= P::RD_HEAD; ++i){
				CVector local = batch.vehicles[0].pose.ToLocal(s.pos[i]);
				if(std::fabs(local.x) < .65f && local.y > -1.0f && local.y < .4f && local.z > .6f && local.z < 1.3f)
					++interiorCoreFrames;
			}
		}
		std::printf("upper-mesh-car speed=%.0f contacts=%u inside-cabin-core-frames=%d peak-speed=%.3f height=%.3f\n",
			speeds[test], contacts, interiorCoreFrames, peakSpeed, peakHeight);
		Check(contacts > 0, "20-40m/s upper car contacts airborne body");
		Check(interiorCoreFrames == 0, "high speed upper-car collision does not pass body core through cabin");
		Check(peakSpeed < speeds[test] + 20.0f, "upper-car response remains bounded at road speeds");
	}
}

static void CoupledCarBraking()
{
	P::State s = FloorBody();
	const CVector start = P::CentreOfMass(&s);
	V::Batch batch = Car(CVector(start.x-2.0f,start.y,0),CVector(-.8f,-2,0),CVector(.8f,2,1),CVector(20,0,0));
	V::Vehicle &car = batch.vehicles[0]; car.handle=31; car.mass=1000;
	VrRagdollReaction::Frame reaction;
	float totalImpulse=0, lateImpulse=0, minimumSpeed=20;
	unsigned impacts=0;
	for(int frame=0;frame<180;++frame){
		VrRagdollReaction::BeginFrame(reaction);
		V::Prepare(batch,0,P::STEP);
		if(s.asleep && C::ShouldWake(&s,batch)) P::Wake(&s);
		C::StepContacts context(&batch,&reaction,&s,0,1.0f);
		P::Step(&s,C::Project,&context);
		car.pose=V::PoseAt(car,P::STEP);
		if(reaction.count){
			CVector impulse=VrRagdollReaction::Impulse(reaction.vehicles[0],car.mass,car.linearVelocity);
			car.linearVelocity+=impulse*(1.0f/car.mass);
			car.surfaceLinearVelocity=car.linearVelocity;
			totalImpulse+=impulse.Magnitude();
			if(frame>90) lateImpulse+=impulse.Magnitude();
			impacts+=reaction.pointImpacts;
		}
		minimumSpeed=P::MinF(minimumSpeed,car.linearVelocity.x);
	}
	std::printf("coupled-car reaction=%.2fNs speed20->%.4f impacts=%u late-reaction=%.2fNs\n",totalImpulse,car.linearVelocity.x,impacts,lateImpulse);
	Check(car.linearVelocity.x<19.8f && minimumSpeed>15.0f,"real body contact slows car by bounded noticeable amount");
	Check(lateImpulse<1.0f,"matched sliding body does not continuously brake car after initial transfer");
}

int main()
{
	CarPushesSleepingBody(); StaticCarDoesNotWakeAdjacentBody(); SupportMotionAndRemoval(); FastBodyHitsParkedCar(); DeepInitialOverlapDoesNotLaunch();
	HighSpeedThinRoof();
	HighSpeedUpperCar();
	CoupledCarBraking();
	std::printf("PASS: production integrated car/ragdoll contacts, %d checks\n", checks);
}
