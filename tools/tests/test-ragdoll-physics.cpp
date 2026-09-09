#include "ragdoll-test-vector.h"
// common.h exports these function macros before including the solver.
// Keep them active here to catch Android/engine include incompatibilities.
#define Max(a,b) ((a) > (b) ? (a) : (b))
#define Min(a,b) ((a) < (b) ? (a) : (b))
#include "VrRagdollPhysics.h"
#include <cstdio>
#include <cstdlib>

using namespace VrRagdollPhysics;

static void Check(bool okay, const char *message)
{
	if(!okay){ std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static CVector Rotate(const CVector &v, const CVector &axis, float angle)
{
	return v*std::cos(angle) + CrossProduct(axis, v)*std::sin(angle) +
		axis*(DotProduct(axis, v)*(1.0f-std::cos(angle)));
}

static State Body(float angle = 0.35f, float lift = 0.5f)
{
	State s = {};
	const CVector points[SIM_BONES] = {
		{0, 0, 0.94f}, {0, 0, 1.10f}, {0, 0, 1.31f}, {0, 0, 1.53f}, {0, 0, 1.69f},
		{-.12f, 0, 1.43f}, {-.26f, 0, 1.45f}, {-.43f, -.035f, 1.20f}, {-.53f, .03f, .98f},
		{.12f, 0, 1.43f}, {.26f, 0, 1.45f}, {.43f, -.035f, 1.20f}, {.53f, .03f, .98f},
		{-.13f, 0, .89f}, {-.14f, .035f, .48f}, {-.14f, -.04f, .1f},
		{.13f, 0, .89f}, {.14f, .035f, .48f}, {.14f, -.04f, .1f}
	};
	const CVector axis = Unit(CVector(1, .3f, .1f), CVector(1, 0, 0));
	for(int i=0; i<SIM_BONES; i++)
		s.pos[i] = Rotate(points[i], axis, angle) + CVector(0, 0, lift);
	Initialize(&s, Rotate(CVector(1,0,0),axis,angle), Rotate(CVector(0,0,1),axis,angle));
	s.groundZ = 0;
	return s;
}

static void CheckFrame(const Frame &f)
{
	Check(std::isfinite(f.right.x) && std::isfinite(f.forward.y) && std::isfinite(f.up.z), "finite frame");
	Check(std::fabs(f.right.MagnitudeSqr()-1)<0.0001f &&
		std::fabs(f.forward.MagnitudeSqr()-1)<0.0001f &&
		std::fabs(f.up.MagnitudeSqr()-1)<0.0001f, "normalised frame");
	Check(std::fabs(DotProduct(f.right,f.up))<0.0001f &&
		std::fabs(DotProduct(f.right,f.forward))<0.0001f, "orthogonal frame");
	Check(DotProduct(CrossProduct(f.right,f.forward),f.up)>.9999f, "right-handed frame");
}

static void TestMassAndSeed()
{
	State s=Body();
	const CVector centre=CentreOfMass(&s), linear(1.7f,-2.1f,.8f), angular(.6f,-.3f,.9f);
	s.asleep=true; s.sleepTime=5;
	SeedMotion(&s,linear,angular);
	Check(!s.asleep && s.sleepTime==0, "seeding wakes settled body");
	CVector momentum; float totalMass=0;
	for(int i=0;i<SIM_BONES;i++){
		const CVector velocity=(s.pos[i]-s.prev[i])/STEP;
		const CVector expected=linear+CrossProduct(angular,s.pos[i]-centre);
		Check((velocity-expected).Magnitude()<.0001f,"coherent rigid seed");
		momentum+=velocity*(1/invMass[i]); totalMass+=1/invMass[i];
	}
	Check(std::fabs(totalMass-70)<.001f,"70 kg total mass");
	Check((momentum-linear*totalMass).Magnitude()<.001f,"spin does not add COM launch");
	CVector a(0,0,0), b(2,0,0);
	const float wa=invMass[RD_PELVIS], wb=invMass[RD_LHAND];
	const CVector before=a*(1/wa)+b*(1/wb);
	SolveDistance(a,b,1,wa/(wa+wb),false);
	Check(std::fabs((b-a).Magnitude()-1)<1e-6f,"pair length solved");
	Check(((a*(1/wa)+b*(1/wb))-before).Magnitude()<1e-5f,"pair conserves COM");
	Check(a.x<.04f && b.x<1.04f,"heavy pelvis resists light hand");
}

static void TestFreeFall()
{
	State s=Body(.7f,5);
	s.groundZ=-1000;
	const CVector start=CentreOfMass(&s);
	SeedMotion(&s,CVector(2,-1,0),CVector(0,0,0));
	float expectedZ=start.z, expectedV=0;
	for(int frame=0;frame<30;frame++){
		expectedV=expectedV*DAMPING-FALL_GRAVITY*STEP;
		expectedZ+=expectedV*STEP;
		Step(&s);
		Check(!s.asleep,"airborne body cannot sleep");
	}
	Check(std::fabs(CentreOfMass(&s).z-expectedZ)<.002f,"free fall follows game gravity and units");
	Check(s.lastMaxLengthError<.003f,"shared gravity does not stretch skeleton");
}

static void TestTakeoverAndDegeneracy()
{
	for(int pose=0;pose<4;pose++){
		State s=Body(float(pose),.2f);
		if(pose==1){ s.pos[RD_LHAND]=s.pos[RD_LUARM]+CVector(.01f,0,0); }
		if(pose==2){ for(int i=0;i<SIM_BONES;i++) s.pos[i]=CVector(0,0,1); }
		if(pose==3){ for(int i=0;i<SIM_BONES;i++) s.pos[i]=CVector(0,0,float(i)*.03f); }
		CVector original[SIM_BONES];
		for(int i=0;i<SIM_BONES;i++) original[i]=s.pos[i];
		Initialize(&s,CVector(0,0,1),CVector(0,0,1));
		// Capturing a pose must not snap the model. An invalid interrupted
		// animation must then recover to anatomical limits instead of making
		// those limits permanently wider for this corpse.
		SolveBends(&s);
		for(int i=0;i<SIM_BONES;i++)
			Check((s.pos[i]-original[i]).Magnitude()<.0021f,"no instant pose change on capture");
		CheckFrame(s.pelvisFrame); CheckFrame(s.torsoFrame);
		for(int frame=0;frame<120;frame++) Step(&s);
		CheckFrame(s.pelvisFrame); CheckFrame(s.torsoFrame);
	}
	const CVector from(0,0,1), to(0,0,-1), half(1,0,0);
	const CVector swung=Swing(from,to,CVector(0,1,0),half);
	Check((swung-CVector(0,-1,0)).Magnitude()<1e-5f,"antiparallel swing uses anatomical axis");
}

static float BendSide(const State &s, const Bend &bend)
{
	const Frame &f=bend.pelvis?s.pelvisFrame:s.torsoFrame;
	const CVector original=World(f,bend.localUpper), originalBend=World(f,bend.localBend);
	const CVector upper=Unit(s.pos[bend.b]-s.pos[bend.a],original);
	const CVector preferred=Swing(original,upper,originalBend,originalBend);
	const CVector chord=s.pos[bend.c]-s.pos[bend.a];
	const float fraction=chord.MagnitudeSqr()>1e-8f?
		Clamp(DotProduct(s.pos[bend.b]-s.pos[bend.a],chord)/chord.MagnitudeSqr(),0,1):.5f;
	return DotProduct(s.pos[bend.b]-s.pos[bend.a]-chord*fraction,preferred);
}

static void TestSignedHinges()
{
	for(int limb=0;limb<NUM_BENDS;limb++){
		State s=Body(.81f,1);
		s.age=1;
		const Bend b=s.bends[limb];
		const Frame &f=b.pelvis?s.pelvisFrame:s.torsoFrame;
		const CVector upper=World(f,b.localUpper), preferred=World(f,b.localBend);
		// Deliberately put the lower segment on the forbidden side of its
		// captured anatomical plane, in a non-world-aligned body pose.
		s.pos[b.c]=s.pos[b.b]+upper*.3f+preferred*.22f;
		Check(BendSide(s,b)<-.05f,"test starts with forbidden joint bend");
		for(int iteration=0;iteration<60;iteration++) SolveBends(&s);
		Check(BendSide(s,b)>-.004f,"signed hinge removes reverse bend");
		Check((s.pos[b.c]-s.pos[b.a]).Magnitude()>=b.minReach-.00001f,"hinge prevents overfolding");
	}
	State a=Body(.6f,1), b=a;
	const CVector z(0,0,1), translation(.7f,-2,0);
	for(int i=0;i<SIM_BONES;i++) b.pos[i]=Rotate(a.pos[i],z,1.07f)+translation;
	Initialize(&b,Rotate(a.restPelvis.right,z,1.07f),a.restPelvis.up);
	const CVector linear(1,.6f,.2f), angular(.5f,.2f,.1f);
	SeedMotion(&a,linear,angular);
	SeedMotion(&b,Rotate(linear,z,1.07f),Rotate(angular,z,1.07f));
	float maximum=0;
	for(int frame=0;frame<75;frame++){
		Step(&a); Step(&b);
		for(int i=0;i<SIM_BONES;i++)
			maximum=MaxF(maximum,(b.pos[i]-Rotate(a.pos[i],z,1.07f)-translation).Magnitude());
	}
	std::printf("rotated-scene maximum difference=%.6fm\n",maximum);
	Check(maximum<.004f,"joint limits follow anatomy under arbitrary world heading");
}

static void TestFalls()
{
	const float angles[]={.35f,1.5707963f,2.6f,-1.15f};
	for(int pose=0;pose<4;pose++){
		State s=Body(angles[pose],pose==2?2.5f:.8f);
		SeedMotion(&s,CVector(pose==3?6.0f:1.0f,.4f,0),CVector(.4f,.2f,.1f));
		int sleepFrame=-1;
		float peakError=0, peakSpeed=0;
		for(int frame=0;frame<1200;frame++){
			Step(&s);
			peakError=MaxF(peakError,s.lastMaxLengthError);
			peakSpeed=MaxF(peakSpeed,std::sqrt(s.lastMaxSpeedSq));
			for(int i=0;i<SIM_BONES;i++){
				Check(std::isfinite(s.pos[i].x)&&std::isfinite(s.pos[i].y)&&std::isfinite(s.pos[i].z),"finite falling joints");
				Check(s.pos[i].z>=s.groundZ+radius[i]-.00001f,"final contacts above floor");
			}
			if(s.asleep && sleepFrame<0) sleepFrame=frame;
		}
		std::printf("pose%d sleep=%.3fs max-edge-error=%.4f peak-speed=%.3f final-error=%.4f support=%d final-speed=%.4f\n",
			pose,float(sleepFrame)/60,peakError,peakSpeed,s.lastMaxLengthError,s.lastSupportCount,std::sqrt(s.lastMaxSpeedSq));
		Check(sleepFrame>=0 && sleepFrame<720,"fall settles and sleeps within 12 seconds");
		Check(s.lastMaxLengthError<.06f,"sleep preserves bone lengths");
		Check(peakSpeed<30,"fall does not explode");
		CheckFrame(s.pelvisFrame); CheckFrame(s.torsoFrame);
		const CVector settled=s.pos[RD_PELVIS];
		for(int frame=0;frame<600;frame++) Step(&s);
		Check((settled-s.pos[RD_PELVIS]).MagnitudeSqr()==0,"sleep removes long-term jitter");
		SeedMotion(&s,CVector(0,0,2),CVector(0,0,0));
		Step(&s); Check(!s.asleep,"settled body wakes and moves");
	}
}

static void TestVariedFalls()
{
	int slept=0;
	float peakSpeed=0, finalError=0;
	for(int trial=0;trial<32;trial++){
		State s=Body(float(trial)*.371f,.5f+float(trial%5)*.4f);
		float shift=0;
		for(int i=0;i<SIM_BONES;i++) shift=MaxF(shift,radius[i]+.05f-s.pos[i].z);
		for(int i=0;i<SIM_BONES;i++) s.pos[i].z+=shift;
		SeedMotion(&s,CVector(float(trial%7)-3,float(trial%3)-1,float(trial%4)),
			CVector(float(trial%3)-1,float(trial%5)-2,.5f));
		for(int frame=0;frame<900;frame++){
			Step(&s);
			peakSpeed=MaxF(peakSpeed,std::sqrt(s.lastMaxSpeedSq));
			for(int i=0;i<SIM_BONES;i++){
				Check(std::isfinite(s.pos[i].MagnitudeSqr()),"varied falls remain finite");
				Check(s.pos[i].z>=radius[i]-.00001f,"varied falls do not penetrate floor");
			}
		}
		if(s.asleep) slept++;
		finalError=MaxF(finalError,s.lastMaxLengthError);
	}
	std::printf("varied-falls slept=%d/32 peak-speed=%.3f final-max-length-error=%.4f\n",slept,peakSpeed,finalError);
	Check(slept==32,"all varied falls settle within 15 seconds");
	Check(peakSpeed<30,"varied falls do not gain unbounded velocity");
}

static void PlatformContact(State *s, void *context)
{
	const float height=*static_cast<const float*>(context);
	for(int i=0;i<SIM_BONES;i++){
		if(s->pos[i].z<height+radius[i]) s->pos[i].z=height+radius[i];
		s->contact[i].active=s->pos[i].z<=height+radius[i]+.001f;
		if(s->contact[i].active)
			RecordContact(s,i,CVector(0,0,1),CVector(0,0,0));
	}
}

static void TestExternalSupport()
{
	State s=Body(.7f,3);
	float height=2;
	for(int frame=0;frame<600;frame++) Step(&s,PlatformContact,&height);
	Check(s.lastSupportCount>=3 && s.lastMaxSpeedSq<SLEEP_SPEED*SLEEP_SPEED,
		"stationary platform actually supports a settled body");
	Check(!s.asleep && s.sleepTime==0,"untracked external support cannot freeze body");
	const float before=s.pos[RD_PELVIS].z;
	for(int frame=0;frame<30;frame++) Step(&s);
	Check(s.pos[RD_PELVIS].z<before-.1f,"body falls when its external support disappears");
}

static void TestContactVelocity()
{
	const CVector normal(1,0,0), stationary(0,0,0);
	CVector response=Respond(CVector(24,0,0),stationary,normal,stationary,0);
	Check(response.MagnitudeSqr()==0,"deep static depenetration does not launch body");
	response=Respond(CVector(24,0,0),stationary,normal,CVector(5,0,0),0);
	Check((response-CVector(5,0,0)).Magnitude()<1e-6f,"moving contact matches car speed without excess kick");
	response=Respond(CVector(24,2,0),CVector(7,2,0),normal,stationary,0);
	Check((response-CVector(7,2,0)).Magnitude()<1e-6f,"pre-existing outgoing and tangential motion survives");
	response=Respond(CVector(4,2,0),CVector(7,2,0),normal,stationary,0);
	Check((response-CVector(4,2,0)).Magnitude()<1e-6f,"response does not undo solved reduction of outgoing speed");
	response=Respond(CVector(-4,2,0),CVector(-8,2,0),normal,stationary,0);
	Check((response-CVector(0,2,0)).Magnitude()<1e-6f,"inward velocity is removed without bounce");
}

static void TestHipAndKneeArticulation()
{
	State s=Body(0,1);
	s.age=1;
	const float oldHeelLimit=(s.pos[RD_LFOOT]-s.pos[RD_PELVIS]).Magnitude()*.55f;
	const CVector lower=s.pos[RD_LFOOT]-s.pos[RD_LCALF];
	// The captured knee already has about 16 degrees of bend; another 120
	// puts it inside the 145-degree anatomical limit.
	s.pos[RD_LFOOT]=s.pos[RD_LCALF]+Rotate(lower,CVector(1,0,0),-2.0943951f);
	const CVector desiredFoot=s.pos[RD_LFOOT];
	Check((desiredFoot-s.pos[RD_PELVIS]).Magnitude()<oldHeelLimit*.85f,
		"deep legal knee flex exposes the obsolete pelvis-to-foot restriction");
	for(int iteration=0;iteration<12;iteration++){
		UpdateFrames(&s); SolveBends(&s); SolveHips(&s);
		for(int i=0;i<NUM_STICKS;i++)
			SolveDistance(s.pos[sticks[i].a],s.pos[sticks[i].b],
				s.stickLen[i]*(sticks[i].minPercent?sticks[i].minPercent*.01f:1.0f),
				s.stickShareA[i],sticks[i].minPercent!=0);
	}
	std::printf("folded-knee heel-to-pelvis=%.3fm obsolete-minimum=%.3fm correction=%.6fm\n",
		(s.pos[RD_LFOOT]-s.pos[RD_PELVIS]).Magnitude(),oldHeelLimit,(s.pos[RD_LFOOT]-desiredFoot).Magnitude());
	Check((s.pos[RD_LFOOT]-desiredFoot).Magnitude()<.005f,"legal knee flex is not tied to pelvis by artificial links");
	for(int hip=0;hip<NUM_HIPS;hip++){
		s=Body(.55f,1); s.age=1;
		const HipLimit h=s.hips[hip];
		const float length=(s.pos[h.b]-s.pos[h.a]).Magnitude();
		s.pos[h.b]=s.pos[h.a]+s.pelvisFrame.up*length;
		const CVector before=s.pos[h.a]*(1/invMass[h.a])+s.pos[h.b]*(1/invMass[h.b]);
		for(int iteration=0;iteration<50;iteration++) SolveHips(&s);
		const CVector direction=Unit(s.pos[h.b]-s.pos[h.a],-s.pelvisFrame.up);
		Check(DotProduct(direction,-s.pelvisFrame.up)>=-.344f,"hip cannot swing past 110 degrees into inverted body direction");
		const CVector after=s.pos[h.a]*(1/invMass[h.a])+s.pos[h.b]*(1/invMass[h.b]);
		Check((after-before).Magnitude()<.0001f,"hip angular correction preserves pair COM");
	}
	// Extreme takeover remains continuous at capture; it must no longer
	// permanently weaken the anatomical limits after the blend-in.
	s=Body(0,1);
	s.pos[RD_LCALF]=s.pos[RD_LTHIGH]+CVector(0,.04f,.4f);
	const CVector captured=s.pos[RD_LCALF];
	Initialize(&s,CVector(1,0,0),CVector(0,0,1));
	SolveHips(&s);
	Check((s.pos[RD_LCALF]-captured).Magnitude()<.0001f,"captured extreme pose does not snap into hip cone");
}

static void TestStrongLaunches()
{
	for(int trial=0;trial<12;trial++){
		State s=Body(float(trial)*.41f,3);
		const CVector launch(float(trial%3-1)*40,30,float(trial%4)*15);
		SeedMotion(&s,launch,CVector(15,-20,30));
		Check(std::isfinite(CentreOfMass(&s).MagnitudeSqr()),"initial centre is finite");
		float peakSpeed=0;
		for(int frame=0;frame<1200;frame++){
			Step(&s);
			peakSpeed=MaxF(peakSpeed,std::sqrt(s.lastMaxSpeedSq));
			for(int i=0;i<SIM_BONES;i++){
				Check(std::isfinite(s.pos[i].MagnitudeSqr()),"high-speed launch remains finite");
				Check(s.pos[i].z>=s.groundZ+radius[i]-.00001f,"high-speed launch respects final ground contact");
			}
		}
		Check(peakSpeed<45,"capped game launch does not gain explosive solver speed");
		if(!s.asleep || s.lastMaxLengthError>=.06f)
			std::printf("unsettled strong-launch trial=%d peak=%.3f length-error=%.5f final-speed=%.5f\n",
				trial,peakSpeed,s.lastMaxLengthError,std::sqrt(s.lastMaxSpeedSq));
		Check(s.asleep && s.lastMaxLengthError<.06f,"strong launch settles with joint lengths restored");
	}
}

static float AngleDegrees(const CVector &a, const CVector &b)
{
	return std::acos(Clamp(DotProduct(Unit(a,CVector(0,0,1)),Unit(b,CVector(0,0,1))),-1,1))*57.2957795f;
}

static void ProjectPose(State &s, int iterations, bool legacyMidline=false)
{
	for(int iteration=0;iteration<iterations;iteration++){
		UpdateFrames(&s); SolveBends(&s); SolveHips(&s);
		if(!legacyMidline) SolveTorso(&s);
		for(int i=0;i<NUM_STICKS;i++)
			SolveDistance(s.pos[sticks[i].a],s.pos[sticks[i].b],s.stickLen[i],s.stickShareA[i],false);
		if(legacyMidline){
			// These are the exact three v2 midline restrictions for this
			// initially straight fixture, retained here only for comparison.
			SolveDistance(s.pos[RD_PELVIS],s.pos[RD_SPINE1],s.stickLen[0]+s.stickLen[1],
				invMass[RD_PELVIS]/(invMass[RD_PELVIS]+invMass[RD_SPINE1]),false);
			SolveDistance(s.pos[RD_SPINE],s.pos[RD_NECK],s.stickLen[1]+s.stickLen[2],
				invMass[RD_SPINE]/(invMass[RD_SPINE]+invMass[RD_NECK]),false);
			SolveDistance(s.pos[RD_PELVIS],s.pos[RD_HEAD],
				(s.stickLen[0]+s.stickLen[1]+s.stickLen[2]+s.stickLen[3])*.85f,
				invMass[RD_PELVIS]/(invMass[RD_PELVIS]+invMass[RD_HEAD]),true);
		}
	}
	UpdateFrames(&s);
}

static State FlexedTorso()
{
	State s=Body(0,2); s.age=1;
	const CVector right(1,0,0);
	for(int i=RD_SPINE1;i<=RD_RHAND;i++)
		s.pos[i]=s.pos[RD_SPINE]+Rotate(s.pos[i]-s.pos[RD_SPINE],right,28.0f/57.2957795f);
	for(int i=RD_NECK;i<=RD_RHAND;i++)
		s.pos[i]=s.pos[RD_SPINE1]+Rotate(s.pos[i]-s.pos[RD_SPINE1],right,25.0f/57.2957795f);
	s.pos[RD_HEAD]=s.pos[RD_NECK]+Rotate(s.pos[RD_HEAD]-s.pos[RD_NECK],right,45.0f/57.2957795f);
	UpdateFrames(&s);
	return s;
}

static float RelativeTwist(const State &s)
{
	const CVector carried=Swing(s.pelvisFrame.up,s.torsoFrame.up,s.pelvisFrame.right,s.pelvisFrame.forward);
	const CVector reference=carried*s.restTwistCosine+CrossProduct(s.torsoFrame.up,carried)*s.restTwistSine;
	return AngleDegrees(reference,s.torsoFrame.right);
}

static void TestTorsoMobility()
{
	const State posed=FlexedTorso();
	State current=posed, legacy=posed;
	ProjectPose(current,60); ProjectPose(legacy,60,true);
	const float newLumbar=AngleDegrees(current.pos[RD_SPINE]-current.pos[RD_PELVIS],current.pos[RD_SPINE1]-current.pos[RD_SPINE]);
	const float oldLumbar=AngleDegrees(legacy.pos[RD_SPINE]-legacy.pos[RD_PELVIS],legacy.pos[RD_SPINE1]-legacy.pos[RD_SPINE]);
	const float newThoracic=AngleDegrees(current.pos[RD_SPINE1]-current.pos[RD_SPINE],current.pos[RD_NECK]-current.pos[RD_SPINE1]);
	const float newNeck=AngleDegrees(current.pos[RD_NECK]-current.pos[RD_SPINE1],current.pos[RD_HEAD]-current.pos[RD_NECK]);
	float displacement=0;
	for(int i=0;i<SIM_BONES;i++) displacement=MaxF(displacement,(current.pos[i]-posed.pos[i]).Magnitude());
	std::printf("legal-torso flex lumbar=%.3f upper=%.3f neck=%.3f degrees correction=%.6fm legacy-lumbar=%.3f\n",
		newLumbar,newThoracic,newNeck,displacement,oldLumbar);
	Check(displacement<.0005f && newLumbar>27.9f && newThoracic>24.9f && newNeck>44.9f,
		"legal back and neck articulation has no restoring spring");
	Check(oldLumbar<newLumbar*.65f,"original collinear braces demonstrably removed lumbar flex");
	Check(DotProduct(current.pelvisFrame.up,CVector(0,0,1))>.9999f,
		"chest flexion does not drag lower pelvic orientation with it");
	// Every legal pose can tumble through arbitrary orientations without a
	// hidden world-up constraint trying to straighten the body.
	float maxTumbleCorrection=0;
	const CVector axis=Unit(CVector(.3f,.8f,.5f),CVector(1,0,0));
	for(int trial=0;trial<24;trial++){
		State s=posed;
		for(int i=0;i<SIM_BONES;i++) s.pos[i]=Rotate(posed.pos[i],axis,float(trial)*.29f)+CVector(2,-3,1);
		CVector desired[SIM_BONES];
		for(int i=0;i<SIM_BONES;i++) desired[i]=s.pos[i];
		UpdateFrames(&s); ProjectPose(s,12);
		for(int i=0;i<SIM_BONES;i++) maxTumbleCorrection=MaxF(maxTumbleCorrection,(s.pos[i]-desired[i]).Magnitude());
		CheckFrame(s.pelvisFrame); CheckFrame(s.torsoFrame);
	}
	std::printf("legal-pose arbitrary-tumble correction=%.6fm\n",maxTumbleCorrection);
	Check(maxTumbleCorrection<.001f,"torso limits follow anatomical frames throughout rigid tumble");
}

static void TestTorsoTwistAndLimits()
{
	for(int trial=0;trial<2;trial++){
		State s=Body(0,2); s.age=1;
		const float twist=trial==0?45.0f:125.0f;
		for(int i=RD_NECK;i<=RD_RHAND;i++)
			s.pos[i]=s.pos[RD_SPINE1]+Rotate(s.pos[i]-s.pos[RD_SPINE1],CVector(0,0,1),twist/57.2957795f);
		UpdateFrames(&s);
		const CVector com=CentreOfMass(&s);
		const float before=RelativeTwist(s);
		ProjectPose(s,180);
		const float after=RelativeTwist(s);
		std::printf("chest-twist before=%.3f after=%.3f degrees COM-error=%.7fm\n",before,after,(CentreOfMass(&s)-com).Magnitude());
		Check((CentreOfMass(&s)-com).Magnitude()<.0001f,"internal twist limits preserve whole-body COM");
		if(trial==0) Check(std::fabs(after-before)<.02f,"legal chest twist is free of restoring torque");
		else Check(after<67 && after>60,"extreme chest twist is limited without locking chest to hips");
	}
	for(int joint=0;joint<NUM_TORSO_BENDS;joint++){
		State s=Body(0,2); s.age=1;
		const TorsoBend &b=s.torsoBends[joint];
		s.pos[b.c]=s.pos[b.a]+CVector(.015f,0,0);
		for(int iteration=0;iteration<80;iteration++) SolveTorso(&s);
		Check((s.pos[b.c]-s.pos[b.a]).Magnitude()>b.minReach-.002f,
			"back or neck cannot collapse past its anatomical reach bound");
	}
}

int main()
{
	TestMassAndSeed(); TestFreeFall(); TestTakeoverAndDegeneracy(); TestSignedHinges(); TestFalls(); TestVariedFalls(); TestExternalSupport(); TestContactVelocity(); TestHipAndKneeArticulation(); TestStrongLaunches(); TestTorsoMobility(); TestTorsoTwistAndLimits();
	std::puts("PASS: production ragdoll mass, gravity, contacts, sleep, takeover and frame invariants");
}
