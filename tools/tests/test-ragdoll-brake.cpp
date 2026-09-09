#include "ragdoll-test-vector.h"
#define VR_RAGDOLL_VEHICLE_NO_ENGINE
#include "VrRagdollBrake.h"
#include "VrRagdollContacts.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>

namespace B = VrRagdollBrake;
namespace P = VrRagdollPhysics;
namespace V = VrRagdollVehicle;
namespace C = VrRagdollContacts;
static unsigned checks;
static void Check(bool value, const char *message)
{
	++checks;
	if(!value){ std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static bool Near(float a, float b, float epsilon = 0.001f) { return std::fabs(a-b) < epsilon; }
static bool Zero(const CVector &v) { return v.x == 0 && v.y == 0 && v.z == 0; }

static void ConfirmedHitCalibration()
{
	const float masses[] = {800,1000,2000};
	for(float mass : masses){
		const CVector velocity(0,20,0);
		const CVector impulse = B::Impulse(mass,velocity,28);
		const float loss = -impulse.y/mass;
		Check(Near(loss,4200.0f/(mass+210.0f)), "confirmed impact preserves calibrated mass dependence");
		Check(impulse.x == 0 && impulse.z == 0 && loss > 1.8f && loss < 4.2f, "single pedestrian gives visible bounded horizontal speed loss");
		float gameSpeed = .4f + impulse.y/(50.0f*mass);
		Check(Near(gameSpeed*50.0f,20-loss), "applied CPhysical J/50 gives the predicted SI speed loss");
		std::printf("confirmed-hit car=%.0fkg collision=28game speed=20m/s loss=%.4fm/s=%.3fkm/h\n",mass,loss,loss*3.6f);
	}
	for(float speed : {0.25f,1.0f,5.0f,20.0f,60.0f}){
		CVector impulse = B::Impulse(1000,CVector(speed,0,0),10000);
		Check(Near(-impulse.x/1000,speed*.22f), "extreme collision is capped at22percent current speed");
		Check(speed+impulse.x/1000>0, "extreme collision cannot stop or reverse a moving car");
	}
}

static void InvalidAndHeadingInputs()
{
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float inf = std::numeric_limits<float>::infinity();
	for(float mass : {-1.0f,0.0f,nan,inf}) Check(Zero(B::Impulse(mass,CVector(20,0,0),28)), "invalid car mass has no effect");
	for(float impulse : {-1.0f,0.0f,.001f,nan,inf}) Check(Zero(B::Impulse(1000,CVector(20,0,0),impulse)), "invalid or negligible collision has no effect");
	for(const CVector &velocity : {CVector(0,0,0),CVector(.1f,.1f,20),CVector(nan,0,0),CVector(0,inf,0),CVector(0,20,nan)})
		Check(Zero(B::Impulse(1000,velocity,28)), "parked or invalid car receives no impulse");
	for(const CVector &velocity : {CVector(12,16,5),CVector(-12,-16,-5),CVector(0,-20,4)}){
		CVector impulse=B::Impulse(1000,velocity,28);
		Check(impulse.z==0 && DotProduct(velocity,impulse)<0, "forward reverse and oblique hits always resist current horizontal motion");
		Check(Near(impulse.x*velocity.y-impulse.y*velocity.x,0,.01f), "impulse has no horizontal steering torque");
		Check(Near(impulse.Magnitude()/1000,4200.0f/1210.0f), "vertical velocity does not inflate horizontal braking");
	}
	B::Ledger ledger;
	Check(Zero(B::Limit(ledger,1,3,1000,CVector(-20,0,0),CVector(-3000,0,0))), "stale opposite-heading request cannot propel reversed car");
	Check(Zero(B::Limit(ledger,1,3,1000,CVector(0,20,0),CVector(-3000,0,0))), "perpendicular stale request does not steer car");
	Check(ledger.count==0, "rejected requests do not reserve vehicle capacity");
	CVector projected=B::Limit(ledger,1,3,1000,CVector(12,16,0),CVector(-3000,0,0));
	Check(projected.x<0 && projected.y<0 && Near(projected.Magnitude(),1800), "oblique request retains only opposition to present travel");
}

static void CrowdBudget()
{
	B::Ledger ledger;
	CVector velocity(0,20,0);
	float total=0;
	for(int ped=0;ped<100;++ped){
		const CVector impulse=B::Limit(ledger,10,31,1000,velocity,B::Impulse(1000,velocity,1000));
		total+=impulse.Magnitude(); velocity+=impulse/1000;
		Check(velocity.y>=12.999f && impulse.z==0, "crowd never exceeds35percent original frame speed loss");
	}
	Check(Near(total,7000,.01f) && Near(velocity.y,13), "every pedestrian shares same7000Ns car frame budget");
	Check(ledger.count==1, "crowd uses one fixed vehicle record");
	CVector next=B::Limit(ledger,11,31,1000,velocity,B::Impulse(1000,velocity,1000));
	Check(Near(next.Magnitude(),2860,.01f), "next game frame starts fresh cap from current speed");
	Check(ledger.count==1 && ledger.frame==11, "game frame rollover resets ledger");
	for(int handle=32;handle<39;++handle)
		Check(!Zero(B::Limit(ledger,11,handle,1000,CVector(0,20,0),CVector(0,-2000,0))), "eight different generation handles fit fixed ledger");
	for(int handle=39;handle<44;++handle)
		Check(Zero(B::Limit(ledger,11,handle,1000,CVector(0,20,0),CVector(0,-2000,0))), "overflow cannot evict existing budget and bypass frame cap");
	Check(ledger.count==8 && ledger.overflow==5, "overflow is fixed and observable");
	Check(!Zero(B::Limit(ledger,0,0x1201,1000,CVector(0,20,0),CVector(0,-2000,0))), "unsigned frame wrap resets stale entries");
	Check(ledger.count==1 && ledger.overflow==0, "frame wrap clears overflow");
	Check(!Zero(B::Limit(ledger,0,0x1202,1000,CVector(0,20,0),CVector(0,-2000,0))), "new car pool generation gets separate budget");
	Check(ledger.count==2, "pool generation handles do not alias");
}

static void AdjustableStrength()
{
	const CVector velocity(0,20,0);
	for(float mass : {800.0f,1000.0f,2000.0f}){
		float previous = -1;
		for(float percent : {0.0f,100.0f,200.0f,500.0f,1000.0f,2000.0f}){
			B::Ledger ledger;
			const CVector request = B::Impulse(mass,velocity,28,percent);
			const CVector result = B::Limit(ledger,1,31,mass,velocity,request,percent);
			const float loss = result.Magnitude()/mass;
			Check(loss > previous, "every preset increases actual confirmed-hit braking for common car masses");
			Check(Near(loss,request.Magnitude()/mass), "single hit is scaled once and keeps its configured impulse");
			Check(loss <= 20*float(B::HitFraction(percent))+.001f, "single hit obeys the selected non-reversing cap");
			if(percent==0) Check(Zero(result) && ledger.count==0, "zero strength disables braking without using ledger capacity");
			if(percent==100) Check(Near(loss,B::Impulse(mass,velocity,28).Magnitude()/mass), "100percent retains previous calibrated behavior");
			std::printf("adjustable car=%.0fkg strength=%.0fpercent loss=%.3fkm/h\n",mass,percent,loss*3.6f);
			previous = loss;
		}
	}
	for(float speed : {.25f,1.0f,5.0f,20.0f,60.0f}){
		for(float collision : {.05f,1.0f,28.0f,10000.0f}){
			float previous = -1;
			for(float percent : {0.0f,100.0f,200.0f,500.0f,1000.0f,2000.0f}){
				B::Ledger ledger;
				const CVector travel(speed,0,0);
				const CVector request = B::Impulse(1000,travel,collision,percent);
				const CVector result = B::Limit(ledger,1,31,1000,travel,request,percent);
				const float loss = -result.x/1000;
				Check(loss > previous, "weak and saturated hits both increase monotonically through2000percent");
				Check(speed+result.x/1000 >= speed*.02f-.00001f, "maximum-strength hit cannot reverse the car");
				previous = loss;
			}
		}
	}
	for(float percent : {0.0f,100.0f,200.0f,500.0f,1000.0f,2000.0f}){
		B::Ledger ledger;
		CVector travel(0,20,0);
		float total = 0;
		for(int ped=0;ped<100;++ped){
			const CVector request = B::Impulse(1000,travel,10000,percent);
			const CVector result = B::Limit(ledger,1,31,1000,travel,request,percent);
			travel += result/1000;
			total += result.Magnitude();
			Check(travel.y>=20*(1-float(B::FrameFraction(percent)))-.001f, "crowd obeys configured shared frame cap");
		}
		Check(Near(total,20000*float(B::FrameFraction(percent)),.01f), "crowd reaches the scaled frame cap rather than a hidden35percent cap");
		Check(travel.y>=.199f, "maximum-strength crowd cannot exceed99percent frame speed loss");
		B::Ledger residual;
		const CVector scaled = CVector(0,-100,0)*B::Multiplier(percent);
		const CVector result = B::Limit(residual,1,31,1000,velocity,scaled,percent);
		Check(Near(result.Magnitude(),100*B::Multiplier(percent)), "residual contact request uses exactly one configurable multiplier");
	}
	B::Ledger residual;
	Check(Zero(B::Limit(residual,1,31,1000,velocity,CVector(0,-10000,0),0)), "zero strength blocks even unscaled residual braking");
	const float nan=std::numeric_limits<float>::quiet_NaN(), inf=std::numeric_limits<float>::infinity();
	for(float invalid : {-500.0f,-1.0f,nan,inf,-inf}){
		B::Ledger ledger;
		Check(Zero(B::Impulse(1000,velocity,28,invalid)), "invalid strength cannot produce a direct impulse");
		Check(Zero(B::Limit(ledger,1,31,1000,velocity,CVector(0,-1000,0),invalid)), "invalid strength cannot spend residual credit");
	}
	for(float high : {2001.0f,10000.0f,std::numeric_limits<float>::max()}){
		Check(Near(B::Multiplier(high),20), "finite out-of-range strength clamps at2000percent");
		Check(Near(B::Impulse(1000,velocity,28,high).Magnitude(),B::Impulse(1000,velocity,28,2000).Magnitude()), "oversized direct setting has the2000percent result");
	}
	Check(sizeof(B::Ledger)==144, "adjustable caps retain the144byte fixed vehicle ledger");
}

static void ExtendedCapsAndPhysicalContacts()
{
	for(int percent=0;percent<=500;++percent){
		const double multiplier = percent*.01;
		const double oldHit = double(B::MAX_HIT_FRACTION)*multiplier;
		const double oldFrame = double(B::MAX_FRAME_FRACTION)*multiplier;
		Check(std::fabs(B::HitFraction(float(percent))-(oldHit < .75 ? oldHit : .75)) < .000001,
			"all saved 0..500 values preserve original hit cap");
		Check(std::fabs(B::FrameFraction(float(percent))-(oldFrame < double(.85f) ? oldFrame : double(.85f))) < .000001,
			"all saved 0..500 values preserve original shared frame cap");
	}
	for(int percent=501;percent<=2000;++percent){
		Check(B::Multiplier(float(percent)) > B::Multiplier(float(percent-1)), "every extended gain is meaningful");
		Check(B::HitFraction(float(percent)) > B::HitFraction(float(percent-1)), "saturated hit cap rises for every extended percentage");
		Check(B::FrameFraction(float(percent)) > B::FrameFraction(float(percent-1)), "crowd cap rises for every extended percentage");
		Check(B::HitFraction(float(percent)) < B::FrameFraction(float(percent)) && B::FrameFraction(float(percent)) < 1,
			"single and shared limits retain positive car momentum");
	}
	for(float mass : {800.0f,1000.0f,2000.0f}) for(float speed : {10.0f,20.0f,40.0f,60.0f}){
		VrRagdollReaction::Frame contact;
		for(int point=0;point<P::SIM_BONES;++point)
			Check(VrRagdollReaction::Add(contact,31,mass,CVector(0,speed,0),0,point,1.0f/P::invMass[point],
				CVector(0,1,0),CVector(0,speed,0),CVector(0,0,0),1), "actual inward body momentum contributes once");
		float previousLoss = -1;
		for(float percent : {500.0f,1000.0f,2000.0f}){
			B::Ledger ledger;
			const CVector velocity(0,speed,0);
			const CVector physical = VrRagdollReaction::Impulse(contact.vehicles[0],mass,velocity);
			const CVector impulse = B::Limit(ledger,1,31,mass,velocity,physical*B::Multiplier(percent),percent);
			const float loss = -impulse.y/mass;
			Check(loss > previousLoss, "500/1000/2000 increase physical corpse-contact braking without a fresh full collision");
			Check(loss <= speed*float(B::HitFraction(percent))+.0001f && impulse.z == 0, "extended corpse contact cannot reverse or lift the car");
			Check(Near(impulse.Magnitude(), std::fmin(physical.Magnitude()*B::Multiplier(percent),mass*speed*float(B::HitFraction(percent))), .01f),
				"physical contact is multiplied exactly once after finite-mass reduction");
			previousLoss = loss;
			if(mass == 1000 && speed == 40)
				std::printf("residual-contact 1000kg speed=144km/h strength=%.0fpercent loss=%.3fkm/h\n",percent,loss*3.6f);
		}
		for(int point=0;point<P::SIM_BONES;++point)
			Check(!VrRagdollReaction::Add(contact,31,mass,CVector(0,speed,0),0,point,1.0f/P::invMass[point],
				CVector(0,1,0),CVector(0,speed,0),CVector(0,0,0),1), "extended gain cannot duplicate the same particle impact");
		VrRagdollReaction::BeginFrame(contact);
		Check(!VrRagdollReaction::Add(contact,31,mass,CVector(0,speed,0),0,0,16,
			CVector(0,1,0),CVector(0,speed,0),CVector(0,speed,0),1), "co-moving hood body creates no phantom continuing brake");
	}
	Check(Near(float(B::HitFraction(500)),.75f) && Near(float(B::HitFraction(1000)),.90f) && Near(float(B::HitFraction(2000)),.98f),
		"extended saturated-hit presets remove 75/90/98 percent maximum");
	Check(Near(float(B::FrameFraction(500)),.85f) && Near(float(B::FrameFraction(1000)),.95f) && Near(float(B::FrameFraction(2000)),.99f),
		"extended aggregate presets remove 85/95/99 percent maximum");
}

static void CapacityIndependentHitCache()
{
	B::HitCache cache;
	Check(B::Allows(cache,0x1101,0x2201,100), "first generation-bearing pedestrian/car pair may brake");
	Check(B::Allows(cache,0x1101,0x2201,100), "read-only check does not consume a failed or disabled hit");
	Check(cache.count==0, "no hit record is reserved before actual impulse application");
	Check(B::Record(cache,0x1101,0x2201,100), "successful hit records its pair");
	Check(!B::Allows(cache,0x1101,0x2201,100), "same-frame callback cannot brake twice");
	Check(!B::Record(cache,0x1101,0x2201,200), "duplicate recording cannot refresh the cooldown");
	Check(!B::Allows(cache,0x1101,0x2201,1099), "pair remains blocked for999ms");
	Check(B::Allows(cache,0x1101,0x2201,1100), "pair expires at exactly1000ms from the applied hit");
	Check(B::Record(cache,0x1101,0x2201,1100) && cache.count==1, "expired matching pair reuses its original entry");
	Check(B::Allows(cache,0x1102,0x2201,1100), "recycled pedestrian pool generation does not inherit old cooldown");
	Check(B::Allows(cache,0x1101,0x2202,1100), "recycled car pool generation does not inherit old cooldown");
	Check(B::Record(cache,0x1102,0x2201,1100), "new pedestrian generation gets separate record");
	Check(B::Record(cache,0x1101,0x2202,1100), "new car generation gets separate record");
	Check(cache.count==3, "full pair identity includes both generation handles");
	for(int i=3;i<B::MAX_HIT_PAIRS;++i){
		Check(B::Allows(cache,0x3000+i,0x2201,1100), "braking cache accepts pedestrians beyond the six-body simulation capacity");
		Check(B::Record(cache,0x3000+i,0x2201,1100), "additional independent hit is retained");
	}
	Check(cache.count==64, "hit cache remains fixed at64 distinct pairs");
	Check(!B::Allows(cache,0x5001,0x2201,1100), "full fresh cache declines new pair instead of evicting duplicate protection");
	Check(!B::Record(cache,0x5001,0x2201,1100), "overload cannot overwrite a recent hit");
	Check(!B::Allows(cache,0x1101,0x2201,1100), "overload preserves original pair's cooldown");
	Check(B::Allows(cache,0x5001,0x2201,2100), "expiry frees capacity without allocation");
	Check(B::Record(cache,0x5001,0x2201,2100) && cache.count==64, "expired cache recycles one slot with fixed storage");
	Check(!B::Allows(cache,0x5001,0x2201,2100), "recycled slot protects its new pair");
	Check(!B::Allows(cache,-1,0x2201,2100) && !B::Allows(cache,0x5001,-1,2100), "invalid generation handles reject hit permission");
	Check(!B::Record(cache,-1,0x2201,2100) && !B::Record(cache,0x5001,-1,2100), "invalid generation handles cannot poison the cache");
	B::HitCache wrap;
	const unsigned beforeWrap=std::numeric_limits<unsigned>::max()-499u;
	Check(B::Record(wrap,1,2,beforeWrap), "hit before unsigned timer wrap is recorded");
	Check(!B::Allows(wrap,1,2,0), "timer wrap after500ms does not expire a recent hit");
	Check(!B::Allows(wrap,1,2,499), "wrapped pair is still blocked at999ms");
	Check(B::Allows(wrap,1,2,500), "wrapped pair expires at1000ms");
	Check(B::Record(wrap,1,2,500) && wrap.count==1, "wrapped timestamp is replaced after an actual new hit");
}

static P::State StandingBody()
{
	P::State s={};
	const CVector points[P::SIM_BONES]={
		{0,0,.94f},{0,0,1.10f},{0,0,1.31f},{0,0,1.53f},{0,0,1.69f},
		{-.12f,0,1.43f},{-.26f,0,1.45f},{-.43f,-.035f,1.20f},{-.53f,.03f,.98f},
		{.12f,0,1.43f},{.26f,0,1.45f},{.43f,-.035f,1.20f},{.53f,.03f,.98f},
		{-.13f,0,.89f},{-.14f,.035f,.48f},{-.14f,-.04f,.1f},
		{.13f,0,.89f},{.14f,.035f,.48f},{.14f,-.04f,.1f}
	};
	for(int i=0;i<P::SIM_BONES;++i) s.pos[i]=points[i];
	P::Initialize(&s,CVector(1,0,0),CVector(0,0,1));
	s.groundZ=0;
	P::SeedMotion(&s,CVector(0,16,0),CVector(0,0,0));
	return s;
}

// The collision impulse is supplied as a confirmed engine event. We do not
// reproduce CPhysical's collision solver: this fixture tests the downstream
// callback result together with actual contact geometry and body integration.
static float SeededUprightContact(bool confirmedHit)
{
	P::State s=StandingBody();
	V::Batch batch; batch.count=1;
	V::Vehicle &car=batch.vehicles[0]; car.handle=31; car.mass=1000;
	car.pose.origin=CVector(0,-2.3f,0); car.pose.right=CVector(1,0,0);
	car.pose.forward=CVector(0,1,0); car.pose.up=CVector(0,0,1);
	car.linearVelocity=car.surfaceLinearVelocity=CVector(0,20,0);
	car.angularVelocity=car.surfaceAngularVelocity=CVector(0,0,0);
	car.shapeCount=1; car.shapes[0].radius=0;
	car.shapes[0].min=CVector(-.8f,-2,0); car.shapes[0].max=CVector(.8f,2,1);
	car.shapes[0].center=(car.shapes[0].min+car.shapes[0].max)*.5f;
	car.boundCenter=car.shapes[0].center;
	car.boundRadius=(car.shapes[0].max-car.boundCenter).Magnitude();
	B::Ledger ledger;
	float directLoss=0;
	if(confirmedHit){
		const CVector impulse=B::Limit(ledger,0,31,car.mass,car.linearVelocity,B::Impulse(car.mass,car.linearVelocity,28));
		car.linearVelocity+=impulse/car.mass;
		car.surfaceLinearVelocity=car.linearVelocity;
		directLoss=-impulse.y/car.mass;
	}
	float earlyLoss=0;
	unsigned contacts=0;
	for(unsigned frame=0;frame<180;++frame){
		VrRagdollReaction::Frame reaction;
		V::Prepare(batch,0,P::STEP);
		if(s.asleep && C::ShouldWake(&s,batch)) P::Wake(&s);
		C::StepContacts context(&batch,&reaction,&s,0,.5f);
		P::Step(&s,C::Project,&context);
		car.pose=V::PoseAt(car,P::STEP); contacts+=context.count;
		if(reaction.count){
			const CVector impulse=VrRagdollReaction::Impulse(reaction.vehicles[0],car.mass,car.linearVelocity);
			car.linearVelocity+=impulse/car.mass;
			car.surfaceLinearVelocity=car.linearVelocity;
		}
		if(frame==11) earlyLoss=20-car.linearVelocity.y;
		for(int i=0;i<P::SIM_BONES;++i) Check(B::Finite(s.pos[i]), "combined direct brake and ragdoll contacts stay finite");
	}
	const float totalLoss=20-car.linearVelocity.y;
	Check(contacts>0, "upright seeded body actually reaches production vehicle geometry");
	if(confirmedHit){
		Check(directLoss*3.6f>8 && earlyLoss*3.6f>8, "initial hit is immediately noticeable even when limbs already move with car");
		Check(totalLoss/20<.35f, "single direct hit plus residual contacts remains bounded");
	}else Check(earlyLoss*3.6f<1 && totalLoss*3.6f<1, "old seeded upright case reproduces less than1kmh additional braking");
	std::printf("upright seeded16m/s direct=%d immediate=%.3fkm/h first0.2s=%.3fkm/h total=%.3fkm/h contacts=%u\n",confirmedHit,directLoss*3.6f,earlyLoss*3.6f,totalLoss*3.6f,contacts);
	return earlyLoss;
}

int main()
{
	ConfirmedHitCalibration(); InvalidAndHeadingInputs(); CrowdBudget();
	AdjustableStrength(); ExtendedCapsAndPhysicalContacts(); CapacityIndependentHitCache();
	const float before=SeededUprightContact(false), after=SeededUprightContact(true);
	Check(after>before+2, "confirmed-hit path fixes weak seeded initial collision by more than2m/s");
	std::printf("PASS: confirmed vehicle hit brake, %u checks; fixed ledger=%u bytes\n",checks,unsigned(sizeof(B::Ledger)));
}
