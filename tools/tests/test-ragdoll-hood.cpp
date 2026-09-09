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
static P::State Standing()
{
	P::State s = {};
	const CVector points[P::SIM_BONES] = {
		{0, 0, .94f}, {0, 0, 1.10f}, {0, 0, 1.31f}, {0, 0, 1.53f}, {0, 0, 1.69f},
		{-.12f, 0, 1.43f}, {-.26f, 0, 1.45f}, {-.43f, -.035f, 1.20f}, {-.53f, .03f, .98f},
		{.12f, 0, 1.43f}, {.26f, 0, 1.45f}, {.43f, -.035f, 1.20f}, {.53f, .03f, .98f},
		{-.13f, 0, .89f}, {-.14f, .035f, .48f}, {-.14f, -.04f, .1f},
		{.13f, 0, .89f}, {.14f, .035f, .48f}, {.14f, -.04f, .1f}
	};
	for(int i = 0; i < P::SIM_BONES; ++i) s.pos[i] = points[i] + CVector(0, 2.25f, 0);
	P::Initialize(&s, CVector(1,0,0), CVector(0,0,1));
	s.groundZ = 0;
	return s;
}
static void Quad(V::Vehicle &v, CVector a, CVector b, CVector c, CVector d)
{
	Check(V::MakeTriangle(a,b,c,v.triangles[v.triangleCount++]), "first mesh triangle valid");
	Check(V::MakeTriangle(a,c,d,v.triangles[v.triangleCount++]), "second mesh triangle valid");
}
static V::Batch Car(float speed)
{
	V::Batch b;
	b.count=1;
	V::Vehicle &v=b.vehicles[0];
	v.pose.origin=CVector(0,0,0);
	v.pose.right=CVector(1,0,0); v.pose.forward=CVector(0,1,0); v.pose.up=CVector(0,0,1);
	v.linearVelocity=v.surfaceLinearVelocity=CVector(0,speed,0);
	v.angularVelocity=v.surfaceAngularVelocity=CVector(0,0,0);
	v.shapeCount=1;
	v.shapes[0].min=CVector(-.85f,-2,.1f); v.shapes[0].max=CVector(.85f,2,.45f);
	v.shapes[0].center=(v.shapes[0].min+v.shapes[0].max)*.5f; v.shapes[0].radius=0;
	v.boundCenter=CVector(0,0,.7f); v.boundRadius=2.4f;
	Quad(v, {-.8f,1.1f,.65f},{.8f,1.1f,.65f},{.8f,2,.65f},{-.8f,2,.65f});
	Quad(v, {-.8f,.6f,1.3f},{.8f,.6f,1.3f},{.8f,1.1f,.65f},{-.8f,1.1f,.65f});
	Quad(v, {-.8f,-1.2f,1.3f},{.8f,-1.2f,1.3f},{.8f,.6f,1.3f},{-.8f,.6f,1.3f});
	Quad(v, {-.8f,-1.2f,.45f},{-.8f,-1.2f,1.3f},{-.8f,.6f,1.3f},{-.8f,1.1f,.65f});
	Quad(v, {.8f,-1.2f,.45f},{.8f,1.1f,.65f},{.8f,.6f,1.3f},{.8f,-1.2f,1.3f});
	return b;
}
static void Advance(P::State &s,V::Batch &b)
{
	V::Prepare(b,0,P::STEP);
	C::StepContacts c(&b);
	P::Step(&s,C::Project,&c);
	for(int i=0;i<b.count;++i) b.vehicles[i].pose=V::PoseAt(b.vehicles[i],P::STEP);
	for(int i=0;i<P::SIM_BONES;++i){
		Check(std::isfinite(s.pos[i].MagnitudeSqr()), "standing collision stays finite");
		Check(s.pos[i].z>=P::radius[i]-.0001f,"standing collision stays above ground");
	}
}
static CVector MeanVelocity(const P::State &s)
{
	CVector v(0,0,0);
	for(int i=0;i<P::SIM_BONES;++i) v+=(s.pos[i]-s.prev[i])/(P::STEP*P::invMass[i]);
	return v/70;
}
static void StandingHit(float speed)
{
	P::State s=Standing(); V::Batch b=Car(speed);
	float peakSpeed=0,peakHeight=0,peakError=0,carry=0,first=-1;
	int coreInside=0,contacts=0;
	for(int frame=0;frame<300;++frame){
		Advance(s,b);
		peakSpeed=P::MaxF(peakSpeed,std::sqrt(s.lastMaxSpeedSq));
		peakError=P::MaxF(peakError,s.lastMaxLengthError);
		CVector local=b.vehicles[0].pose.ToLocal(P::CentreOfMass(&s));
		bool supported=false;
		for(int i=0;i<P::SIM_BONES;++i){
			peakHeight=P::MaxF(peakHeight,s.pos[i].z);
			if(s.contact[i].active){
				++contacts;
				if(i<=P::RD_HEAD && s.contact[i].normal.z>.35f) supported=true;
			}
			CVector node=b.vehicles[0].pose.ToLocal(s.pos[i]);
			if(i<=P::RD_HEAD && std::fabs(node.x)<.65f && node.y>-.9f && node.y<.4f && node.z>.5f && node.z<1.2f) ++coreInside;
		}
		if(supported && std::fabs(local.x)<.9f && local.y>-.9f && local.y<2.2f){carry+=P::STEP; if(first<0)first=frame*P::STEP;}
	}
	std::printf("standing hit %.0fm/s contacts=%d coreInside=%d peakSpeed=%.3f height=%.3f lengthError=%.3f carry=%.3f first=%.3f\n",speed,contacts,coreInside,peakSpeed,peakHeight,peakError,carry,first);
	Check(contacts>100 && carry>4.5f,"standing pedestrian rolls onto actual bonnet or roof and stays carried");
	Check(coreInside==0,"standing pedestrian core does not pass through cabin");
	Check(peakSpeed<speed*1.5f && peakHeight<2.4f,"standing impact avoids a speed or height catapult");
	Check(peakError<.45f && s.lastMaxLengthError<.06f,"finite-iteration impact distortion remains bounded and recovers");
}

static P::State Board(V::Batch &b)
{
	P::State s=Standing(); b=Car(6);
	for(int frame=0;frame<180;++frame) Advance(s,b);
	Check(s.lastSupportCount>=3,"support fixture first boards from a standing collision");
	return s;
}
static void MovingSupport()
{
	V::Batch b; P::State s=Board(b);
	CVector before=b.vehicles[0].pose.ToLocal(P::CentreOfMass(&s));
	int supportFrames=0; float largestError=0;
	for(int frame=0;frame<240;++frame){
		const float speed=frame<120 ? 6+3*(frame+1)*P::STEP : 12-4*(frame-119)*P::STEP;
		b.vehicles[0].linearVelocity=b.vehicles[0].surfaceLinearVelocity=CVector(0,speed,0);
		Advance(s,b);
		if(s.lastSupportCount>=3)++supportFrames;
		largestError=P::MaxF(largestError,s.lastMaxLengthError);
	}
	CVector after=b.vehicles[0].pose.ToLocal(P::CentreOfMass(&s));
	std::printf("bonnet accelerate3/brake4 relative-shift=%.4fm support=%d/240 speed=%.4f vs4 max-length-error=%.4f\n",(after-before).Magnitude(),supportFrames,MeanVelocity(s).y,largestError);
	Check(supportFrames>220 && (after-before).Magnitude()<.7f,"moderate acceleration and braking retain real support");
	Check(std::fabs(MeanVelocity(s).y-4)<.25f,"supported body matches changed car speed through friction");
	const float supportedHeight=P::CentreOfMass(&s).z;
	b.count=0;
	for(int frame=0;frame<120;++frame)Advance(s,b);
	Check(P::CentreOfMass(&s).z<supportedHeight-.35f,"removing bonnet support makes body fall");
}
static void CoMovingHandoff()
{
	V::Batch b; P::State board=Board(b);
	const CVector localStart=b.vehicles[0].pose.ToLocal(P::CentreOfMass(&board));
	float drift[2]={}; int support[2]={};
	for(int mode=0;mode<2;++mode){
		P::State s=board; V::Batch car=b;
		car.vehicles[0].linearVelocity=car.vehicles[0].surfaceLinearVelocity=CVector(0,30,0);
		P::SeedMotion(&s,CVector(0,30,0),CVector(0,0,0),mode ? 34.0f :16.0f);
		Check(std::fabs(MeanVelocity(s).y-(mode?30.0f:16.0f))<.001f,"vehicle seed preserves requested bounded shared translation");
		for(int frame=0;frame<60;++frame){
			Advance(s,car);
			if(s.lastSupportCount>=3)++support[mode];
		}
		drift[mode]=(car.vehicles[0].pose.ToLocal(P::CentreOfMass(&s))-localStart).Magnitude();
		if(mode){
			// A subsequent brake changes surface velocity; no pose attachment
			// or repeated seeding is allowed to force the body to remain aboard.
			car.vehicles[0].linearVelocity=car.vehicles[0].surfaceLinearVelocity=CVector(0,10,0);
			for(int frame=0;frame<120;++frame)Advance(s,car);
			const CVector escaped=car.vehicles[0].pose.ToLocal(P::CentreOfMass(&s));
			Check(escaped.y>2.5f && P::CentreOfMass(&s).z<.6f,"hard braking naturally sheds carried body rather than gluing it");
		}
	}
	std::printf("co-moving30 handoff drift legacy16=%.4fm carried=%d/60 preserved30=%.4fm carried=%d/60\n",drift[0],support[0],drift[1],support[1]);
	Check(drift[0]>.2f && drift[1]<.02f && drift[1]<drift[0]*.1f,"vehicle-aware seed prevents artificial fast-bonnet slip");
	Check(support[1]>=58,"co-moving body stays physically supported at30m/s");
}

static void ContactAwareBone()
{
	P::State s=Standing();
	const int a=P::RD_PELVIS,b=P::RD_SPINE;
	const float rest=s.stickLen[0];
	s.pos[b].z+=.12f;
	P::RecordContact(&s,a,CVector(0,0,-1),CVector(0,0,0));
	const CVector blocked=s.pos[a];
	P::SolveBone(&s,0,rest,false);
	Check((s.pos[a]-blocked).Magnitude()<1e-6f,"joint correction does not push contacting endpoint through solid plane");
	Check(std::fabs((s.pos[b]-s.pos[a]).Magnitude()-rest)<1e-6f,"free endpoint resolves full bone length in one pass");
	P::SeedMotion(&s,CVector(0,100,0),CVector(0,0,0),1000);
	Check(MeanVelocity(s).Magnitude()<16.01f,"invalid vehicle seed bound falls back to ordinary safe limit");
}

static void GripAdjustment()
{
	float lateralDrift[2]={};
	for(int mode=0;mode<2;++mode){
		VrRagdollSettings::SetGrip(150);
		V::Batch car; P::State s=Board(car);
		const CVector before=car.vehicles[0].pose.ToLocal(P::CentreOfMass(&s));
		VrRagdollSettings::SetGrip(mode?150:0);
		for(int frame=0;frame<120;++frame){
			car.vehicles[0].linearVelocity=car.vehicles[0].surfaceLinearVelocity=CVector(1.5f*(frame+1)*P::STEP,6,0);
			Advance(s,car);
		}
		lateralDrift[mode]=std::fabs(car.vehicles[0].pose.ToLocal(P::CentreOfMass(&s)).x-before.x);
	}
	VrRagdollSettings::SetGrip(150);
	std::printf("lateral bonnet acceleration1.5 grip0 drift=%.4fm grip150 drift=%.4fm\n",lateralDrift[0],lateralDrift[1]);
	Check(lateralDrift[0]>.8f && lateralDrift[1]<.2f,"live grip coefficient changes supported body slip through surface friction");
}
static void TangentialProjectionIsNotVelocity()
{
	const CVector up(0,0,1),stopped(0,0,0),moving(0,30,0);
	CVector response=P::Respond(CVector(20,0,10),stopped,up,stopped,0,true);
	Check(response.Magnitude()<1e-6f,"deep stationary vehicle projection cannot manufacture tangent or normal launch");
	response=P::Respond(CVector(20,30,10),moving,up,moving,0,true);
	Check((response-moving).Magnitude()<1e-6f,"co-moving projection retains surface translation without sideways kick");
	const CVector incoming(2,30,0);
	response=P::Respond(CVector(20,30,0),incoming,up,moving,0,true);
	Check(std::fabs(response.x-2)<1e-6f && std::fabs(response.y-30)<1e-6f,"real incoming relative limb motion survives vehicle projection cap");
	response=P::Respond(CVector(20,30,0),incoming,up,moving,.75f,true);
	Check(std::fabs(response.x-1.75f)<1e-6f,"Coulomb budget is applied after retaining genuine tangent speed");
}
int main()
{
	VrRagdollSettings::SetGrip(150);
	StandingHit(6); StandingHit(12); StandingHit(20); StandingHit(30);
	MovingSupport(); CoMovingHandoff(); ContactAwareBone(); GripAdjustment(); TangentialProjectionIsNotVelocity();
	std::printf("PASS: standing bonnet contacts, changing support, seed continuity, %d checks\n",checks);
	return 0;
}
