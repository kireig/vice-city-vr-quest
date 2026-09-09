#include "ragdoll-test-vector.h"
#define Max(a,b) ((a) > (b) ? (a) : (b))
#define Min(a,b) ((a) < (b) ? (a) : (b))
#include "VrRagdollPhysics.h"
#include <cstdio>
#include <cstdlib>

using namespace VrRagdollPhysics;

static int checks = 0, failures = 0;
static void Check(bool okay, const char *message)
{
	++checks;
	if(!okay){
		++failures;
		if(failures <= 24) std::fprintf(stderr, "FAIL: %s\n", message);
	}
}

static CVector Rotate(const CVector &v, const CVector &axis, float angle)
{
	return v*std::cos(angle) + CrossProduct(axis,v)*std::sin(angle) +
		axis*(DotProduct(axis,v)*(1.0f-std::cos(angle)));
}

static State Body(float scale=1.0f)
{
	State s = {};
	const CVector p[SIM_BONES] = {
		{0,0,.94f},{0,0,1.10f},{0,0,1.31f},{0,0,1.53f},{0,0,1.69f},
		{-.12f,0,1.43f},{-.26f,0,1.45f},{-.43f,-.035f,1.20f},{-.53f,.03f,.98f},
		{.12f,0,1.43f},{.26f,0,1.45f},{.43f,-.035f,1.20f},{.53f,.03f,.98f},
		{-.13f,0,.89f},{-.14f,.035f,.48f},{-.14f,-.04f,.1f},
		{.13f,0,.89f},{.14f,.035f,.48f},{.14f,-.04f,.1f}
	};
	for(int i=0;i<SIM_BONES;++i) s.pos[i]=p[i]*scale;
	Initialize(&s,CVector(1,0,0),CVector(0,0,1));
	s.groundZ=0;
	return s;
}

static float PointSegment(const CVector &p,const CVector &a,const CVector &b)
{
	const CVector ab=b-a;
	const float fraction=ab.MagnitudeSqr()>1e-10f ?
		Clamp(DotProduct(p-a,ab)/ab.MagnitudeSqr(),0,1):0;
	return (p-a-ab*fraction).Magnitude();
}

// Sample the entire shin, not only its joint spheres: a shin crossing the
// chest can have both endpoints outside the chest. The conservative core
// radius below leaves a tolerance for the four-pass position solver.
static float LowerLegTrunkClearance(const State &s,int knee,int foot)
{
	float distance=1000;
	for(int sample=0;sample<=8;++sample){
		const CVector p=s.pos[knee]+(s.pos[foot]-s.pos[knee])*(float(sample)/8);
		distance=MinF(distance,PointSegment(p,s.pos[RD_PELVIS],s.pos[RD_SPINE1]));
		distance=MinF(distance,PointSegment(p,s.pos[RD_SPINE1],s.pos[RD_NECK]));
	}
	return distance;
}

static void ResetVelocity(State &s)
{
	for(int i=0;i<SIM_BONES;++i) s.prev[i]=s.pos[i];
	Wake(&s);
}

static void TestGroundedBoneRepair()
{
	for(int side=0;side<2;++side){
		State s=Body();
		const int knee=side?RD_RCALF:RD_LCALF,foot=side?RD_RFOOT:RD_LFOOT;
		int shin=-1;
		for(int stick=0;stick<SKELETON_STICKS;++stick)
			if(sticks[stick].a==knee && sticks[stick].b==foot) shin=stick;
		Check(shin>=0,"fixture finds the real lower-leg bone");
		if(shin<0) continue;
		s.pos[foot]=CVector(side?.14f:-.14f,0,radius[foot]);
		s.pos[knee]=s.pos[foot]+CVector(0,0,.20f);
		const CVector grounded=s.pos[foot];
		// Floor contact alone must engage the constrained solve. In the old
		// fast path only a car contact did, so this compressed shin repair
		// pulled the foot below the floor and ground projection undid it.
		SolveBone(&s,shin,s.stickLen[shin],false);
		std::printf("grounded-shin side=%d floor-depth=%.5f length-error=%.6f\n",side,
			radius[foot]-s.pos[foot].z,std::fabs((s.pos[foot]-s.pos[knee]).Magnitude()-s.stickLen[shin]));
		Check(s.pos[foot].z>=radius[foot]-.000001f,"bone repair never pushes a grounded foot below its plane");
		Check((s.pos[foot]-grounded).Magnitude()<.00001f,"blocked foot stays supported while its free knee repairs length");
		Check(std::fabs((s.pos[foot]-s.pos[knee]).Magnitude()-s.stickLen[shin])<.00001f,
			"floor-aware repair restores compressed shin through free endpoint");
	}
}

static void TestHipDirections()
{
	// Old symmetric 125-degree cone allows a knee directly behind the chest
// and above the hips to either side. Neither is an anatomical hip motion.
	const CVector illegal[] = {
		Unit(CVector(0,-1,.2f),CVector(0,-1,0)),
		Unit(CVector(-1,0,.3f),CVector(-1,0,0)),
		Unit(CVector(1,0,.3f),CVector(1,0,0))
	};
	for(int side=0;side<2;++side){
		for(int direction=0;direction<3;++direction){
			State s=Body();
			const int hip=side?RD_RTHIGH:RD_LTHIGH, knee=side?RD_RCALF:RD_LCALF;
			s.pos[knee]=s.pos[hip]+illegal[direction]*.41f;
			s.age=1;
			for(int pass=0;pass<80;++pass) SolveHips(&s);
			const CVector leg=Local(s.pelvisFrame,Unit(s.pos[knee]-s.pos[hip],CVector(0,0,-1)));
			const float backward=std::atan2(-leg.y,-leg.z)*57.2957795f;
			const float sideways=std::atan2(std::fabs(leg.x),std::sqrt(leg.y*leg.y+leg.z*leg.z))*57.2957795f;
			std::printf("hip side=%d case=%d backward=%.2f sideways=%.2f\n",side,direction,backward,sideways);
			if(direction==0) Check(backward<55,"hip cannot extend backwards through torso");
			else Check(sideways<65,"hip cannot rotate sideways above pelvis");
		}
	}
}

static void TestMalformedTakeover()
{
	for(int side=0;side<2;++side){
		State s=Body();
		const int hip=side?RD_RTHIGH:RD_LTHIGH, knee=side?RD_RCALF:RD_LCALF, foot=side?RD_RFOOT:RD_LFOOT;
		// Valid segment lengths but an almost 180-degree folded knee.
		s.pos[knee]=s.pos[hip]+CVector(0,.06f,-.405f);
		s.pos[foot]=s.pos[hip]+CVector(.012f,0,-.003f);
		Initialize(&s,CVector(1,0,0),CVector(0,0,1));
		s.age=1;
		for(int pass=0;pass<80;++pass) SolveBends(&s);
		const float reach=(s.pos[foot]-s.pos[hip]).Magnitude();
		std::printf("folded-takeover side=%d hip-foot-reach=%.4f\n",side,reach);
		Check(reach>.19f,"interrupted fold cannot permanently disable knee flexion limit");

		s=Body();
		s.pos[knee]=s.pos[hip]+CVector(0,.015f,.41f);
		s.pos[foot]=s.pos[knee]+CVector(0,-.03f,-.38f);
		Initialize(&s,CVector(1,0,0),CVector(0,0,1));
		s.age=1;
		for(int pass=0;pass<80;++pass) SolveHips(&s);
		const float upward=DotProduct(Unit(s.pos[knee]-s.pos[hip],-s.pelvisFrame.up),s.pelvisFrame.up);
		std::printf("inverted-takeover side=%d thigh-up-dot=%.4f\n",side,upward);
		Check(upward<.60f,"interrupted pose cannot legalise thigh alongside head");

		s=Body();
		s.pos[knee]=s.pos[hip]+CVector(0,.035f,-.41f);
		s.pos[foot]=s.pos[knee]+CVector(.25f,0,-.29f);
		Initialize(&s,CVector(1,0,0),CVector(0,0,1));
		s.age=1;
		for(int pass=0;pass<80;++pass) SolveBends(&s);
		const CVector chord=s.pos[foot]-s.pos[hip];
		const float fraction=Clamp(DotProduct(s.pos[knee]-s.pos[hip],chord)/chord.MagnitudeSqr(),0,1);
		const float sideways=std::fabs(DotProduct(s.pos[knee]-s.pos[hip]-chord*fraction,s.pelvisFrame.right));
		std::printf("sideways-knee-takeover side=%d lateral-bend=%.4f\n",side,sideways);
		Check(sideways<.09f,"interrupted animation cannot redefine knee as a sideways hinge");
	}
}

// ADL detects the production self-contact phase. The fallback lets the same
// regression run against a historical solver that has no self-contact phase;
// it must fail the geometry assertion instead of failing to compile.
template<class T> static auto SelfContact(T *s,int) -> decltype(SolveSelfCollision(s),void())
{
	SolveSelfCollision(s);
}
template<class T> static void SelfContact(T *,long) {}

static void TestSegmentCrossing()
{
	for(int side=0;side<2;++side){
		for(int region=0;region<2;++region){
		State s=Body();
		const int knee=side?RD_RCALF:RD_LCALF, foot=side?RD_RFOOT:RD_LFOOT;
		// Neither endpoint is in the thorax core, but the middle of the shin
		// passes exactly through it. This exposes sphere-only self collisions.
		const CVector centre=region?s.pos[RD_HEAD]:(s.pos[RD_SPINE]+s.pos[RD_SPINE1])*.5f;
		s.pos[knee]=centre+CVector(0,.23f,0);
		s.pos[foot]=centre-CVector(0,.23f,0);
		s.groundZ=-100;
		s.age=1;
		const CVector before=CentreOfMass(&s);
		const float initial=region?PointSegment(s.pos[RD_HEAD],s.pos[knee],s.pos[foot]):LowerLegTrunkClearance(s,knee,foot);
		Check(initial<.001f,"crossing fixture starts inside torso or head");
		for(int pass=0;pass<80;++pass) SelfContact(&s,0);
		const float clearance=region?PointSegment(s.pos[RD_HEAD],s.pos[knee],s.pos[foot]):LowerLegTrunkClearance(s,knee,foot);
		std::printf("shin-crossing side=%d region=%d core-clearance=%.4f COM-shift=%.6f\n",side,region,clearance,(CentreOfMass(&s)-before).Magnitude());
		Check(clearance>.13f,"shin middle cannot pass through torso or head while endpoints miss it");
		Check((CentreOfMass(&s)-before).Magnitude()<.001f,"free-space internal collisions conserve centre of mass");
		}
	}

	State s=Body();
	s.pos[RD_RCALF]=s.pos[RD_LCALF];
	s.pos[RD_RFOOT]=s.pos[RD_LFOOT];
	s.groundZ=-100;
	s.age=1;
	const CVector before=CentreOfMass(&s);
	for(int pass=0;pass<80;++pass) SelfContact(&s,0);
	float separation=1000;
	for(int sample=0;sample<=8;++sample){
		const CVector p=s.pos[RD_LCALF]+(s.pos[RD_LFOOT]-s.pos[RD_LCALF])*(float(sample)/8);
		separation=MinF(separation,PointSegment(p,s.pos[RD_RCALF],s.pos[RD_RFOOT]));
	}
	std::printf("coincident-shins separation=%.4f COM-shift=%.6f\n",separation,(CentreOfMass(&s)-before).Magnitude());
	Check(separation>.09f,"exactly coincident lower legs separate without arbitrary world offset");
	Check((CentreOfMass(&s)-before).Magnitude()<.001f,"coincident leg correction conserves centre of mass");
}

static void TestCrossedChest()
{
	for(int pose=0;pose<4;++pose){
		State s=Body();
		// Segment lengths stay close to their ordinary values. Both feet are
		// deliberately pushed into the chest/head region after initialization.
		for(int side=0;side<2;++side){
			const int knee=side?RD_RCALF:RD_LCALF, foot=side?RD_RFOOT:RD_LFOOT;
			const float sign=side?1.0f:-1.0f;
			s.pos[knee]=CVector(sign*.10f,.35f,1.10f);
			s.pos[foot]=CVector(sign*.025f,.025f,1.31f);
		}
		const CVector axis=Unit(CVector(1,.2f,.3f),CVector(1,0,0));
		for(int i=0;i<SIM_BONES;++i) s.pos[i]=Rotate(s.pos[i],axis,float(pose)*1.17f);
		float lift=0;
		for(int i=0;i<SIM_BONES;++i) lift=MaxF(lift,radius[i]+.5f-s.pos[i].z);
		for(int i=0;i<SIM_BONES;++i) s.pos[i].z+=lift;
		ResetVelocity(s);
		for(int frame=0;frame<900;++frame) Step(&s);
		const float shin=MinF(LowerLegTrunkClearance(s,RD_LCALF,RD_LFOOT),LowerLegTrunkClearance(s,RD_RCALF,RD_RFOOT));
		const float headFoot=MinF((s.pos[RD_HEAD]-s.pos[RD_LFOOT]).Magnitude(),(s.pos[RD_HEAD]-s.pos[RD_RFOOT]).Magnitude());
		std::printf("crossed-chest pose=%d shin-core=%.4f head-foot=%.4f sleep=%d length-error=%.4f\n",pose,shin,headFoot,int(s.asleep),s.lastMaxLengthError);
		Check(shin>.12f,"settled shins do not occupy central torso volume");
		Check(headFoot>.18f,"settled feet do not occupy head volume");
		Check(s.asleep,"crossed-body recovery settles without permanent jitter");
		Check(s.lastMaxLengthError<.065f,"crossed-body recovery preserves bone lengths");
	}
}

static void TestVariedAnatomyFalls()
{
	int slept=0,peakTrial=-1,peakFrame=-1;
	float minimumShin=1000,minimumHeadFoot=1000,minimumTorso=1000,peakSpeed=0,maximumError=0;
	for(int trial=0;trial<48;++trial){
		const float scale=.9f+.1f*float(trial%3);
		State s=Body(scale);
		const CVector axis=Unit(CVector(1,.13f*float(trial%5),.17f*float(trial%7)),CVector(1,0,0));
		const float angle=float(trial)*.371f;
		for(int i=0;i<SIM_BONES;++i) s.pos[i]=Rotate(s.pos[i],axis,angle);
		Initialize(&s,Rotate(CVector(1,0,0),axis,angle),Rotate(CVector(0,0,1),axis,angle));
		float lift=0;
		for(int i=0;i<SIM_BONES;++i) lift=MaxF(lift,radius[i]+.3f+float(trial%4)*.6f-s.pos[i].z);
		for(int i=0;i<SIM_BONES;++i) s.pos[i].z+=lift;
		SeedMotion(&s,CVector(float(trial%7)-3,float(trial%3)-1,float(trial%4)),
			CVector(float(trial%3)-1,float(trial%5)-2,.5f));
		for(int frame=0;frame<1200;++frame){
			Step(&s);
			if(std::sqrt(s.lastMaxSpeedSq)>peakSpeed){
				peakSpeed=std::sqrt(s.lastMaxSpeedSq);
				peakTrial=trial; peakFrame=frame;
			}
			for(int i=0;i<SIM_BONES;++i){
				Check(std::isfinite(s.pos[i].MagnitudeSqr()),"falling anatomy remains finite");
				Check(s.pos[i].z>=radius[i]-.00002f,"anatomical constraints preserve ground clearance");
			}
		}
		if(s.asleep) ++slept;
		const float shin=MinF(LowerLegTrunkClearance(s,RD_LCALF,RD_LFOOT),LowerLegTrunkClearance(s,RD_RCALF,RD_RFOOT));
		const float headFoot=MinF((s.pos[RD_HEAD]-s.pos[RD_LFOOT]).Magnitude(),(s.pos[RD_HEAD]-s.pos[RD_RFOOT]).Magnitude());
		const float torso=(s.pos[RD_NECK]-s.pos[RD_PELVIS]).Magnitude()/scale;
		minimumShin=MinF(minimumShin,shin);
		minimumHeadFoot=MinF(minimumHeadFoot,headFoot);
		minimumTorso=MinF(minimumTorso,torso);
		maximumError=MaxF(maximumError,s.lastMaxLengthError);
		if(shin<=.12f || headFoot<=.18f || torso<=.42f || !s.asleep)
			std::printf("fall trial=%d scale=%.2f shin-core=%.4f head-foot=%.4f torso=%.4f asleep=%d\n",trial,scale,shin,headFoot,torso,int(s.asleep));
		Check(shin>.12f,"varied fall keeps lower legs out of torso core");
		Check(headFoot>.18f,"varied fall keeps feet out of head");
		Check(torso>.42f,"varied fall retains a recognisable trunk length");
		Check(s.lastMaxLengthError<.065f,"varied fall retains bone lengths");
	}
	std::printf("48 anatomy falls slept=%d peak-speed=%.3f (trial=%d frame=%d) max-final-length-error=%.4f min-shin-core=%.4f min-head-foot=%.4f min-trunk=%.4f\n",
		slept,peakSpeed,peakTrial,peakFrame,maximumError,minimumShin,minimumHeadFoot,minimumTorso);
	Check(slept==48,"all varied anatomy falls settle within 20 seconds");
	Check(peakSpeed<30,"anatomy corrections do not create explosive launch speed");
}

int main()
{
	TestGroundedBoneRepair();
	TestHipDirections();
	TestMalformedTakeover();
	TestSegmentCrossing();
	TestCrossedChest();
	TestVariedAnatomyFalls();
	std::printf("anatomy checks=%d failures=%d\n",checks,failures);
	return failures ? 1:0;
}
