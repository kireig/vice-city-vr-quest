#include "ragdoll-test-vector.h"
#define VR_RAGDOLL_VEHICLE_NO_ENGINE
#include "VrRagdollSupport.h"
#include "VrRagdollRay.h"
#include "VrRagdollImpulse.h"
struct CEntity {};
struct RwMatrix {CVector pos,right,up,at;};
struct CVehicle {
 VrRagdollVehicle::Pose pose;bool car=true,bRemoveFromWorld=false;void*m_rwObject=this;int handle=5;
 bool IsCar()const{return car;}CVector GetPosition()const{return pose.origin;}CVector GetRight()const{return pose.right;}CVector GetForward()const{return pose.forward;}CVector GetUp()const{return pose.up;}
};
static CVehicle *liveCar=nullptr;static unsigned carLookups;
namespace CPools {static CVehicle*GetVehicle(int handle){++carLookups;return liveCar&&liveCar->handle==handle?liveCar:nullptr;}}
enum {PEDPIECE_TORSO,PEDPIECE_HEAD,PEDPIECE_LEFTARM,PEDPIECE_RIGHTARM,PEDPIECE_LEFTLEG,PEDPIECE_RIGHTLEG,PEDPIECE_MID,SURFACE_DEFAULT,SURFACE_PED};
struct CColPoint {CVector point,normal;int piece=0;void Set(float,int,int,int,int p){piece=p;}};
#define VR_RAGDOLL_FIXTURE_EXTERNAL_STATE
namespace VrRagdoll { static bool enabled=true; }
namespace VrRagdollMetrics { static bool sampling=false; }
#include "ragdoll-presentation-fixture.inc"
namespace R=VrRagdoll;
namespace U=VrRagdollSupport;
namespace V=VrRagdollVehicle;
static V::Pose Identity(){V::Pose p;p.origin={0,0,0};p.right={1,0,0};p.forward={0,1,0};p.up={0,0,1};return p;}
static void Init(R::Slot&s,CPed&p){
 s=R::Slot();s.ped=&p;s.bulletCredit=100;
 const CVector points[P::SIM_BONES]={
  {0,0,.94f},{0,0,1.10f},{0,0,1.31f},{0,0,1.53f},{0,0,1.69f},
  {-.12f,0,1.43f},{-.26f,0,1.45f},{-.43f,-.035f,1.20f},{-.53f,.03f,.98f},
  {.12f,0,1.43f},{.26f,0,1.45f},{.43f,-.035f,1.20f},{.53f,.03f,.98f},
  {-.13f,0,.89f},{-.14f,.035f,.48f},{-.14f,-.04f,.1f},
  {.13f,0,.89f},{.14f,.035f,.48f},{.14f,-.04f,.1f}};
 for(int i=0;i<P::SIM_BONES;++i)s.pos[i]=points[i];P::Initialize(&s,{1,0,0},{0,0,1});R::RefreshBounds(&s);
 s.support.handle=5;s.support.solved=Identity();s.support.age=.05f;s.support.speed=20;s.support.turnSpeed=.15f;
}
static void GeometryAndRay(){
 R::active=1;CPed ped;CVehicle car;liveCar=&car;car.pose=Identity();car.pose.origin={0,1,0};
 car.pose.right=V::Rotate(car.pose.right,{0,0,1},.05f);car.pose.forward=V::Rotate(car.pose.forward,{0,0,1},.05f);
 auto&s=R::slots[0];Init(s,ped);const auto transform=R::Presentation(&s);Check(transform.active,"valid generation support transport active");
 CVector raw[P::SIM_BONES],prev[P::SIM_BONES];for(int i=0;i<P::SIM_BONES;++i){raw[i]=s.pos[i];prev[i]=s.prev[i];}
 RwMatrix matrices[P::SIM_BONES+8];
 for(int i=0;i<P::SIM_BONES+8;++i){auto&m=matrices[i];m.pos=s.pos[i%P::SIM_BONES];m.right={1,0,0};m.up={0,1,0};m.at={0,0,1};}
 R::ApplyPresentation(matrices,P::SIM_BONES+8,transform);
 CVector boundCentre;float boundRadius;Check(R::GetBounds(&ped,boundCentre,boundRadius),"actual entity bounds return supported body");
 CVector clumpCentre;float clumpRadius;Check(R::GetClumpBounds(ped.GetClump(),clumpCentre,clumpRadius),"actual clump bounds return supported body");
 Check((clumpCentre-boundCentre).Magnitude()<1e-6f&&clumpRadius==boundRadius,"clump and entity bounds use same presentation envelope");
 for(int i=0;i<P::SIM_BONES+8;++i){Check((matrices[i].pos-transform.Point(raw[i%P::SIM_BONES])).Magnitude()<1e-6f,"actual Apply block transforms every simulated and passthrough world matrix exactly once");Check((matrices[i].pos-boundCentre).Magnitude()+P::radius[i%P::SIM_BONES]<boundRadius,"presented matrix stays inside actual culling bounds");}
 for(int i=0;i<P::SIM_BONES;++i)Check((raw[i]-s.pos[i]).Magnitude()==0&&(prev[i]-s.prev[i]).Magnitude()==0,"presentation does not move simulated positions or inject velocity");
 const CVector head=transform.Point(s.pos[P::RD_HEAD]);const CVector direction=transform.Direction({1,0,0});
 CColPoint hit;Check(R::BulletHit(head-direction*2,head+direction*2,0,hit,nullptr)==&ped,"actual BulletHit hits visually transported head rather than stale solver position");
 Check(hit.piece==PEDPIECE_HEAD&&std::fabs((hit.point-head).Magnitude()-P::radius[P::RD_HEAD])<.001f,"reported world hit point and body part match presented geometry");
 Check(DotProduct(hit.normal,-direction)>.99f,"reported normal is in presented world frame");
 CVector momentum;for(int i=0;i<P::SIM_BONES;++i)momentum+=(s.pos[i]-s.prev[i])/(P::STEP*P::invMass[i]);
 Check(momentum.Magnitude()>7.9f&&DotProduct(momentum/momentum.Magnitude(),transform.InverseDirection(direction))>.999f,"actual part impulse is mapped back to solver coordinates without changing force");
 const CVector query=head+direction*2;Check((transform.Point(transform.InversePoint(query))-query).Magnitude()<.00001f,"point mapping round-trips");
}
static void SleepingCache(){
 CPed ped;CVehicle car;liveCar=&car;car.pose=Identity();car.pose.origin={0,.5f,0};
 auto&s=R::slots[0];Init(s,ped);s.asleep=true;s.poseCached=false;
 RwMatrix raw[27],shown[27];
 for(int i=0;i<27;++i){raw[i].pos=s.pos[i%P::SIM_BONES];raw[i].right={1,0,0};raw[i].up={0,1,0};raw[i].at={0,0,1};shown[i]=raw[i];}
 R::ProductionCacheWrite(&s,shown,27);
 Check(s.poseCached,"actual sleeping cache write marks solved pose ready");
 for(int i=0;i<27;++i){Check((s.cachedPose[i].pos-raw[i].pos).Magnitude()==0,"sleep cache stores raw solved positions before presentation");Check((shown[i].pos-R::Presentation(&s).Point(raw[i].pos)).Magnitude()<1e-6f,"first sleeping render applies current transform after saving raw cache");}
 for(int frame=0;frame<4;++frame){
  car.pose.origin={0,.55f+.04f*frame,0};car.pose.right=V::Rotate(CVector(1,0,0),{0,0,1},.01f*frame);car.pose.forward=V::Rotate(CVector(0,1,0),{0,0,1},.01f*frame);
  const auto transform=R::Presentation(&s);R::ProductionCacheReplay(&s,shown,27);
  for(int i=0;i<27;++i){Check((shown[i].pos-transform.Point(raw[i].pos)).Magnitude()<1e-6f,"actual cached branch follows current support once per render without cumulative transform");Check((s.cachedPose[i].pos-raw[i].pos).Magnitude()==0,"replaying moving sleeping pose never modifies raw cache");Check((shown[i].right-transform.Direction(raw[i].right)).Magnitude()<1e-6f,"cached bones retain current supported orientation");}
 }
 s.support.age=.101f;R::ProductionCacheReplay(&s,shown,27);
 for(int i=0;i<27;++i)Check((shown[i].pos-raw[i].pos).Magnitude()==0,"expired support returns raw sleeping pose consistently with bounds and ray geometry");
 s.support.age=0;liveCar=nullptr;R::ProductionCacheReplay(&s,shown,27);
 for(int i=0;i<27;++i)Check((shown[i].pos-raw[i].pos).Magnitude()==0,"destroyed vehicle cannot persist an earlier transformed pose in cache");
}
static void LifetimeAndSlip(){
 CPed ped;CVehicle car;liveCar=&car;car.pose=Identity();auto&s=R::slots[0];Init(s,ped);
 Check(R::Presentation(&s).active,"stopped car still supports coherent presentation");
 car.handle=6;Check(!R::Presentation(&s).active,"reused vehicle pool slot cannot inherit old support generation");car.handle=5;
 liveCar=nullptr;Check(!R::Presentation(&s).active,"destroyed support returns identity without stale dereference");liveCar=&car;
 car.bRemoveFromWorld=true;Check(!R::Presentation(&s).active,"removed support cannot carry a displayed corpse");car.bRemoveFromWorld=false;
 car.pose.origin={10,0,0};Check(!R::Presentation(&s).active,"teleported car cannot snap displayed body");car.pose=Identity();
 car.pose.right={0,1,0};car.pose.forward={-1,0,0};Check(!R::Presentation(&s).active,"abrupt quarter-turn rejects unsupported visual snap");car.pose=Identity();
 s.support.age=.101f;Check(!R::Presentation(&s).active,"stale support expires after bounded scheduling delay");
 V::Batch empty;VrRagdollContacts::StepContacts noContacts(&empty);U::Capture(s.support,s,noContacts,0);Check(s.support.handle<0,"no contacts clears prior support without reading unbuilt contact arrays");
 carLookups=0;for(int i=0;i<1000;++i)Check(!R::Presentation(&s).active,"unsupported body uses identity");Check(carLookups==0,"unsupported and OFF bodies add no vehicle lookups");
 V::Batch batch;batch.count=1;batch.vehicles[0].handle=5;batch.vehicles[0].pose=Identity();batch.vehicles[0].linearVelocity=batch.vehicles[0].angularVelocity={0,0,0};V::Prepare(batch,0,P::STEP);
 VrRagdollContacts::StepContacts contacts(&batch);contacts.built=true;
 for(int i=0;i<P::SIM_BONES;++i){contacts.valid[i]=i<5;if(i<5){contacts.contact[i].vehicleIndex=0;contacts.contact[i].normal={0,0,1};contacts.contact[i].surfaceVelocity={0,0,0};P::RecordContact(&s,i,{0,0,1},{0,0,0});s.prev[i]=s.pos[i];}}
 U::Capture(s.support,s,contacts,0);Check(s.support.handle==5,"three or more resting core contacts establish support");
 for(int i=0;i<5;++i)s.prev[i]=s.pos[i]-CVector(4,0,0)*P::STEP;
 U::Capture(s.support,s,contacts,0);Check(s.support.handle<0,"hard braking/slip releases presentation transport instead of gluing corpse to car");
}
int main(){VrRagdollSettings::SetWeight(100);GeometryAndRay();SleepingCache();LifetimeAndSlip();std::printf("Presentation integration: %u checks PASS; actual Apply matrix/cache blocks, bounds, BulletHit, impulse frame, generation/loss/teleport/slip\n",checks);return 0;}
