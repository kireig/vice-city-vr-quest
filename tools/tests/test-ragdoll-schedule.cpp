#define MIAMIVR_DEV_TOOLS 1 // This fixture inspects the developer scheduler counters.
#include "ragdoll-test-vector.h"
template<class T, class U> static T Min(T a,U b){return a<b?a:T(b);}
template<class T, class U> static T Max(T a,U b){return a>b?a:T(b);}
#define VR_RAGDOLL_VEHICLE_NO_ENGINE
#define VR_RAGDOLL_WORLD_NO_ENGINE
#include "VrRagdollContacts.h"
#include "VrRagdollSupport.h"
#include "VrRagdollWorld.h"
#include "VrRagdollBounds.h"
#include "VrRagdollImpact.h"
#include "VrRagdollSchedule.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#define nil nullptr
using uint32 = unsigned;
using int32 = int;
static float Sqrt(float x){return std::sqrt(x);}
namespace P=VrRagdollPhysics;
namespace S=VrRagdollSchedule;
static unsigned checks;
static void Check(bool okay,const char*why){++checks;if(!okay){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
enum { PED_FALL=1, PED_GETUP=2 };
struct CPed {
 bool dead=true,bUsesCollision=false;
 int m_nPedState=PED_FALL,links=0;
 struct {void*first=nullptr;} m_entryInfoList;
 bool DyingOrDead()const{return dead;}
 void RemoveAndAdd(){++links;}
};
struct CTimer {
 static unsigned now;
 static float dt;
 static unsigned GetTimeInMilliseconds(){return now;}
 static unsigned GetFrameCounter(){return now;}
 static float GetTimeStepInSeconds(){return dt;}
};
unsigned CTimer::now=1000;
float CTimer::dt=P::STEP;
struct Camera {CVector pos;const CVector&GetPosition()const{return pos;}} TheCamera;
namespace CWorld {
static float floorZ=0;
static float FindGroundZFor3DCoord(float,float,float,bool*found){*found=true;return floorZ;}
}
namespace VrRagdollMetrics {
#ifndef VR_RAGDOLL_FIXTURE_EXTERNAL_STATE
static bool sampling=false;
#endif
enum Metric {UPDATE,VEHICLE,CONTACT,GROUND,SOLVE,WORLD,WORLD_QUERIES,WORLD_TRIANGLES,WORLD_OVERFLOW,WORLD_FALLBACKS,SOLVER_PASSES,DROPPED_STEPS,POOL_CHECKS,CANDIDATES,CANDIDATE_OVERFLOW,
 SHAPE_OVERFLOW,TRIANGLE_SCANS,TRIANGLE_OVERFLOW,UNSUPPORTED_MODELS,GROUND_QUERIES,STEPS,CONTACTS,CAR_BRAKES,BRAKE_POINTS};
struct Scope{explicit Scope(Metric){}};
static int stepCalls,passCalls,measuredSteps,measuredPasses;
static void BeginFrame(int,int,int){stepCalls=passCalls=measuredSteps=measuredPasses=0;}
static void Count(Metric metric,unsigned count=1){if(metric==STEPS)stepCalls+=count;if(metric==SOLVER_PASSES)passCalls+=count;}
static void ObserveStep(float,float){}
static void AddBrakeImpulse(float){}
}
namespace VrRagdollVehicle {
static int gathers;
// Scene query substitutes return empty vehicle geometry. Scheduler and body
// integration below are production code; floor contacts remain in the solver.
static void Gather(const CVector*,int,float,float,Batch&batch,float=0){++gathers;batch=Batch();}
static void CommitReactions(VrRagdollReaction::Frame&frame,VrRagdollBrake::Ledger*,unsigned){frame.committed=true;}
}
namespace VrRagdollWorld {
static void Gather(const P::State*state,Cache&cache,float dt){BeginGather(cache,RequiredBounds(state,dt));}
static bool FallbackSweep(const CVector&,const CVector&,float,Contact&){return false;}
}
namespace VrRagdoll {
using namespace P;
enum {MAX_POSES=220,MAX_STEPS=3};
struct Slot:State {
 CPed*ped=nullptr;bool live=false,poseCached=false;
 float waitTime=0,bulletCredit=0;unsigned priorityUntil=0;
 VrRagdollBounds::Bounds bounds;
 VrRagdollWorld::Cache world;
 VrRagdollSupport::State support;
};
struct ProjectCounter {ContactProjector project;void*context;int calls;};
static void CountProjection(State*state,void*context)
{
 ProjectCounter*counter=static_cast<ProjectCounter*>(context);++counter->calls;
 counter->project(state,counter->context);
}
// Run the real solver unchanged and independently count its contact passes.
// A step projects once before solving, then once per constraint iteration.
static void Step(Slot*slot,ContactProjector project,void*context,float dt)
{
 ProjectCounter counter={project,context,0};
 P::Step(slot,CountProjection,&counter,dt);
 ++VrRagdollMetrics::measuredSteps;
 VrRagdollMetrics::measuredPasses+=Max(0,counter.calls-1);
}
static Slot slots[MAX_POSES];
static int active,sleepingCount,lastUpdated,lastDeferred;
#ifndef VR_RAGDOLL_FIXTURE_EXTERNAL_STATE
static bool enabled=true;
#endif
static VrRagdollBrake::Ledger vehicleBrakeLedger;
static int refreshes[MAX_POSES],linked;
static void WakeSavedByVehicles(){}
static void RefreshBounds(Slot*slot){++refreshes[slot-slots];slot->bounds=VrRagdollBounds::Calculate(slot->pos,radius,SIM_BONES);}
static void LinkBounds(Slot*){++linked;}
#include "ragdoll-schedule-production.inc"
}

static P::State Body(float height=0)
{
 P::State state={};
 const CVector points[P::SIM_BONES]={
  {0,0,.94f},{0,0,1.10f},{0,0,1.31f},{0,0,1.53f},{0,0,1.69f},
  {-.12f,0,1.43f},{-.26f,0,1.45f},{-.43f,-.035f,1.20f},{-.53f,.03f,.98f},
  {.12f,0,1.43f},{.26f,0,1.45f},{.43f,-.035f,1.20f},{.53f,.03f,.98f},
  {-.13f,0,.89f},{-.14f,.035f,.48f},{-.14f,-.04f,.1f},
  {.13f,0,.89f},{.14f,.035f,.48f},{.14f,-.04f,.1f}};
 for(int i=0;i<P::SIM_BONES;++i)state.pos[i]=points[i]+CVector(0,0,height);
 P::Initialize(&state,CVector(1,0,0),CVector(0,0,1));state.groundZ=0;
 return state;
}
static CVector Centre(const P::State&state,bool velocity=false)
{
 CVector total;float mass=0;
 for(int i=0;i<P::SIM_BONES;++i){const float m=1.0f/P::invMass[i];mass+=m;total+=(velocity?(state.pos[i]-state.prev[i])/P::STEP:state.pos[i])*m;}
 return total/mass;
}
static void VariableStepGravity()
{
 float positions[3]={},speeds[3]={};int result=0;
 for(int hz:{60,30,20}){
  P::State state=Body(100);state.groundZ=-1000;
  P::SeedMotion(&state,CVector(4,-2,0),CVector(0,0,0));
  const CVector start=Centre(state);
  for(int tick=0;tick<hz*3/10;++tick)P::Step(&state,nullptr,nullptr,1.0f/hz);
  const CVector speed=Centre(state,true),distance=Centre(state)-start;
  Check(std::fabs(speed.z+6.0f)<.08f,"20/30/60Hz gravity retains physical elapsed time");
  Check(std::fabs(speed.x-4.0f)<.12f&&std::fabs(speed.y+2.0f)<.06f,"canonical prev encoding retains horizontal momentum across coarse steps");
  Check(std::fabs(distance.x-1.2f)<.08f,"coarse integration cannot triple locomotion speed");
  positions[result]=distance.z;speeds[result++]=speed.z;
  std::printf("time-slice freefall %dHz dz=%.4fm vz=%.4fm/s vx=%.4fm/s\n",hz,distance.z,speed.z,speed.x);
 }
 Check(std::fabs(positions[0]-positions[2])<.12f&&std::fabs(speeds[0]-speeds[2])<.04f,"frequency change adds only expected integration error");
}
static void VariableStepFalls()
{
 for(int hz:{60,30,20})for(int pose=0;pose<6;++pose){
  P::State state=Body(.5f+float(pose%3)*.75f);
  const CVector centre=state.pos[P::RD_PELVIS];
  if(pose>=3)for(int i=0;i<P::SIM_BONES;++i){
   const CVector offset=state.pos[i]-centre;
   state.pos[i]=centre+CVector(offset.x,offset.z,-offset.y);
  }
  P::Initialize(&state,CVector(1,0,0),pose>=3?CVector(0,1,0):CVector(0,0,1));
  P::SeedMotion(&state,CVector(float(pose%2)*3,2,1),CVector(1.5f,-.7f,pose*.3f));
  float maxError=0,peakSpeed=0,elapsed=0;
  for(int tick=0;tick<hz*16&&!state.asleep;++tick){
   P::Step(&state,nullptr,nullptr,1.0f/hz);
   elapsed+=1.0f/hz;
   maxError=Max(maxError,state.lastMaxLengthError);peakSpeed=Max(peakSpeed,std::sqrt(state.lastMaxSpeedSq));
   for(int i=0;i<P::SIM_BONES;++i){
    const CVector velocity=(state.pos[i]-state.prev[i])/P::STEP;
    Check(std::isfinite(state.pos[i].x)&&std::isfinite(state.pos[i].y)&&std::isfinite(state.pos[i].z)&&
     std::isfinite(velocity.x)&&std::isfinite(velocity.y)&&std::isfinite(velocity.z),"coarse fall retains finite joint positions and velocities");
    Check(state.pos[i].z>=state.groundZ+P::radius[i]-.00001f,"coarse fall cannot project feet or limbs below ground");
   }
  }
  const float feet=Min((state.pos[P::RD_HEAD]-state.pos[P::RD_LFOOT]).Magnitude(),(state.pos[P::RD_HEAD]-state.pos[P::RD_RFOOT]).Magnitude());
  const float trunk=(state.pos[P::RD_NECK]-state.pos[P::RD_PELVIS]).Magnitude();
  std::printf("time-slice fall %dHz pose=%d sleep=%d after=%.2fs boneError=%.4f peakError=%.4f feet/head=%.3f trunk=%.3f peakSpeed=%.3f\n",hz,pose,int(state.asleep),elapsed,state.lastMaxLengthError,maxError,feet,trunk,peakSpeed);
  Check(state.asleep,"20/30/60Hz articulated fall eventually sleeps");
  Check(state.lastMaxLengthError<.065f,"coarse settled pose retains bone lengths");
  Check(feet>.75f&&trunk>.42f,"coarse settled pose cannot collapse feet into the head or flatten the trunk");
  Check(peakSpeed<40.0f,"coarse floor constraints do not create an explosive launch");
 }
}
static void ResetOwners(int count,CPed*peds,bool falling=true)
{
 using namespace VrRagdoll;
 for(int i=0;i<MAX_POSES;++i){slots[i]=Slot();refreshes[i]=0;}
 active=count;sleepingCount=lastUpdated=lastDeferred=linked=0;
 CTimer::now=1000;CTimer::dt=P::STEP;TheCamera.pos=CVector();
 CWorld::floorZ=-10000;VrRagdollVehicle::gathers=0;
 for(int i=0;i<count;++i){
  peds[i]=CPed();slots[i].ped=&peds[i];static_cast<P::State&>(slots[i])=Body(10);
  slots[i].asleep=!falling;slots[i].groundZ=CWorld::floorZ;
  slots[i].bounds=VrRagdollBounds::Calculate(slots[i].pos,P::radius,P::SIM_BONES);
 }
}
static void SchedulerPressure()
{
 using namespace VrRagdoll;
 CPed peds[MAX_POSES];
 for(int count:{1,6,7,12,30,60,220}){
  ResetOwners(count,peds);
  for(int i=0;i<count;++i)slots[i].stepDebt=.09f;
  Update();
  Check(active==count,"work pressure never gives any owner back to native death animation");
  Check(lastUpdated==Min(count,S::MAX_UPDATES)&&VrRagdollMetrics::stepCalls<=S::MAX_STEPS&&VrRagdollMetrics::measuredPasses<=S::MAX_PASSES,
   "one engine update respects12-body,18-step and72-pass budgets at every occupancy");
  Check(VrRagdollMetrics::passCalls==VrRagdollMetrics::measuredPasses&&VrRagdollMetrics::stepCalls==VrRagdollMetrics::measuredSteps,
   "solver pass counters match independently observed production solver projection calls");
  Check(VrRagdollMetrics::stepCalls>=lastUpdated,"every selected body receives at least one integration step");
  Check(VrRagdollVehicle::gathers==lastUpdated&&linked==count,"only selected bodies gather contacts while every retained owner links bounds");
  for(int i=0;i<count;++i)Check(slots[i].ped==&peds[i],"persistent owner identity survives selection pressure");
 }
 ResetOwners(MAX_POSES,peds);
 int lastRefresh[MAX_POSES]={},maxWait=0;
 for(int frame=1;frame<=240;++frame){
  CTimer::now=1000+unsigned(frame*1000/60);linked=0;VrRagdollVehicle::gathers=0;
  for(int i=0;i<6;++i){slots[i].priorityUntil=CTimer::now+350;slots[i].bounds.centre=CVector();}
  for(int i=6;i<MAX_POSES;++i)slots[i].bounds.centre=CVector(1000,1000,0);
  int before[MAX_POSES];for(int i=0;i<MAX_POSES;++i)before[i]=refreshes[i];
  Update();
  Check(active==MAX_POSES&&lastUpdated<=S::MAX_UPDATES&&VrRagdollMetrics::stepCalls<=S::MAX_STEPS&&VrRagdollMetrics::measuredPasses<=S::MAX_PASSES,
   "continuous220-body pressure preserves all owners inside the same bounded work budget");
  Check(VrRagdollMetrics::passCalls==VrRagdollMetrics::measuredPasses&&VrRagdollMetrics::stepCalls==VrRagdollMetrics::measuredSteps,
   "continuous mixed fine and coarse work reports its actual independent projection count");
  for(int i=0;i<MAX_POSES;++i){
   if(refreshes[i]!=before[i]){maxWait=Max(maxWait,frame-lastRefresh[i]);lastRefresh[i]=frame;}
   Check(frame-lastRefresh[i]<=40,"far bodies cannot starve behind six continuously urgent nearby bodies");
   Check(slots[i].stepDebt<=.10001f&&slots[i].ped==&peds[i],"catch-up debt stays bounded without discarding owner state");
  }
 }
 for(int i=0;i<MAX_POSES;++i)Check(refreshes[i]>=5,"every retained body gets repeated service under sustained pressure");
 std::printf("production scheduler220-body pressure maxWait=%dframes budget=%dupdates/%dsteps/%dpasses\n",maxWait,S::MAX_UPDATES,S::MAX_STEPS,S::MAX_PASSES);
 ResetOwners(MAX_POSES,peds,false);Update();
 Check(lastUpdated==0&&VrRagdollMetrics::stepCalls==0&&VrRagdollMetrics::measuredPasses==0&&linked==MAX_POSES,"220 sleeping corpses retain visibility without physics work");
 slots[219].asleep=false;slots[219].priorityUntil=CTimer::now+350;Update();
 Check(lastUpdated==1&&refreshes[219]==1&&slots[219].ped==&peds[219],"last retained body wakes without a free active slot");
 slots[219].live=true;peds[219].dead=false;peds[219].m_nPedState=PED_GETUP;peds[219].m_entryInfoList.first=&peds[219];Update();
 Check(slots[219].ped==nullptr&&active==219&&peds[219].links==1,"live getup alone returns ownership and relinks ordinary body");
}
int main(){VariableStepGravity();VariableStepFalls();SchedulerPressure();std::printf("PASS: %u scheduler/time-slice checks; production Update,20/30/60Hz physics,220 owners,12 updates/18 steps/72 passes.\n",checks);}
