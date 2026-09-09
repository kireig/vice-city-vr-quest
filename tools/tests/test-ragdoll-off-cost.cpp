#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <initializer_list>
#define nil nullptr
static unsigned checks, slotReads, cacheReads, metricCalls, clocks, enteredDynamicUpdate;
static void Check(bool value,const char*message){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
struct CVector {}; struct CColPoint {}; struct CEntity {}; struct CVehicle {};
static unsigned bulletEntry,vehicleWake,vehicleEntry;
static void WakeSavedByVehicles(){++vehicleWake;}
struct CPed {};
struct Slot {CPed*ped=nullptr;bool asleep=false;float pose=0;};
struct AnimatedPose {CPed*ped=nullptr;};
static constexpr int MAX_POSES=220,MAX_ANIMATED_POSES=32;
template<class T,int N,unsigned &Counter>struct CountedArray{T data[N];T&operator[](int i){++Counter;return data[i];}};
static CountedArray<Slot,MAX_POSES,slotReads> slots;
static CountedArray<AnimatedPose,MAX_ANIMATED_POSES,cacheReads> animatedPoses;
static bool enabled;
static int active,sleepingCount,lastUpdated,lastDeferred;
namespace VrRagdollMetrics {
static bool sampling;
enum {UPDATE};
static void BeginFrame(int,int,int){++metricCalls;}
struct Scope{Scope(int){if(sampling)++clocks;}};
}
#include "ragdoll-off-cost-production.inc"
static void ClearCounters(){slotReads=cacheReads=metricCalls=clocks=enteredDynamicUpdate=0;}
int main(){
 CPed ped;
 CVector v; CColPoint p;
 for(int count:{0,1}){
  active=count;enabled=false;slots.data[0].ped=&ped;slots.data[0].asleep=true;slots.data[0].pose=42.5f;ClearCounters();
  for(int hit=0;hit<1000;++hit)VehicleImpact(&ped,nullptr,v,12.0f,&v,nullptr);
  Check(slotReads==0&&vehicleEntry==0,"OFF vehicle impacts never search empty or retained owner pools");
  Check(slots.data[0].asleep&&slots.data[0].pose==42.5f,"OFF vehicle impacts leave retained body untouched");
 }
 enabled=true;active=1;ClearCounters();VehicleImpact(&ped,nullptr,v,12.0f,&v,nullptr);
 Check(slotReads>0&&vehicleEntry==1,"ON vehicle impacts still look up the actual owner");
 slots.data[0].ped=nullptr;
 active=1; enabled=false; RunWakeGate(); BulletHit(v,v,0,p,nullptr);
 Check(vehicleWake==0&&bulletEntry==0,"OFF cannot reawaken retained bodies from cars or bullets");
 enabled=true; RunWakeGate(); BulletHit(v,v,0,p,nullptr);
 Check(vehicleWake==1&&bulletEntry==1,"ON vehicle and bullet wake paths remain enabled");
 active=0; enabled=false;
 enabled=true;
 for(auto &entry:animatedPoses.data)entry.ped=&ped;
 SetEnabled(false);
 Check(cacheReads==MAX_ANIMATED_POSES,"OFF clears capture owner pointers once");
 for(auto &entry:animatedPoses.data)Check(entry.ped==nullptr,"no stale capture survives OFF");
 ClearCounters();VrRagdollMetrics::sampling=true;
 for(int frame=0;frame<1000;++frame){Update();Check(!Owns(&ped),"OFF empty does not own ped");Release(&ped);SetEnabled(false);}
 Check(slotReads==0&&cacheReads==0,"OFF empty has no body or animation-array scan");
 Check(metricCalls==0&&clocks==0&&enteredDynamicUpdate==0,"OFF empty never reaches profiler or dynamic update");
 Check(!VrRagdollMetrics::sampling,"last profiling window cannot time disabled work");
 // Switching OFF retains the body and pose; the existing update continues
 // until the ordinary fall/sleep/release lifecycle ends it.
 enabled=true;active=1;slots.data[0].ped=&ped;slots.data[0].pose=42.5f;
 SetEnabled(false);ClearCounters();Update();
 Check(enteredDynamicUpdate==1&&Owns(&ped),"OFF retains already active owner and fall processing");
 Check(slots.data[0].pose==42.5f,"toggle does not rewrite accepted body pose");
 Release(&ped);Check(active==0&&slots.data[0].ped==nullptr,"active retained owner releases normally");
 ClearCounters();Update();Release(&ped);Check(slotReads==0&&cacheReads==0&&enteredDynamicUpdate==0,"last release returns to zero-scan disabled path");
 SetEnabled(true);ClearCounters();Update();Check(metricCalls==1&&enteredDynamicUpdate==0,"ON empty retains existing profiler behavior");
 animatedPoses.data[2].ped=&ped;Release(&ped);Check(animatedPoses.data[2].ped==nullptr,"ON ped destruction still forgets captured animation");
 std::printf("PASS: %u production OFF/lifecycle checks; 1000 idle disabled updates have zero scans, profiler calls or dynamic-update entry.\n",checks);
}
