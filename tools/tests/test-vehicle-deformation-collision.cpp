#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#define __ANDROID__ 1
#define GTA_VR_WEAPONS 1
using uint16=std::uint16_t;
struct CVector {
 float x,y,z;
 CVector(float a=0,float b=0,float c=0):x(a),y(b),z(c){}
 CVector operator-() const{return CVector(-x,-y,-z);}
};
static bool Same(CVector a,CVector b){return std::fabs(a.x-b.x)<.00001f&&std::fabs(a.y-b.y)<.00001f&&std::fabs(a.z-b.z)<.00001f;}
static unsigned typeReads,carReads,poseReads,checks;
struct CEntity {
 int type=1;unsigned references=0;
 CVector position,right=CVector(1,0,0),forward=CVector(0,1,0),up=CVector(0,0,1);
 bool IsVehicle() const{++typeReads;return type==1;}
 bool IsPed() const{++typeReads;return type==2;}
 CVector &GetRight(){++poseReads;return right;}
 CVector &GetForward(){++poseReads;return forward;}
 CVector &GetUp(){++poseReads;return up;}
 CVector &GetPosition(){++poseReads;return position;}
 void RegisterReference(CEntity **){++references;}
};
struct CColPoint {CVector point,normal;uint16 pieceA=3,pieceB=9;int surfaceB=0;float impulseA=11,impulseB=17;bool accepted=true;};
struct CPhysical:CEntity {
 uint16 m_nDamagePieceType=0;float m_fDamageImpulse=0;CEntity *m_pDamageEntity=nullptr;CVector m_vecDamageNormal;
 CVector rollbackShift;bool bIsInSafePosition=false;unsigned rollbackCalls=0,responses=0;
 void UnsetIsInSafePosition(){position=CVector(position.x+rollbackShift.x,position.y+rollbackShift.y,position.z+rollbackShift.z);bIsInSafePosition=false;++rollbackCalls;}
 void SetDamagedPieceRecord(uint16,float,CEntity*,CVector);
 bool ApplyCollision(CPhysical *B,CColPoint &p,float &a,float &b){if(!p.accepted)return false;a=p.impulseA;b=p.impulseB;++responses;++B->responses;if(B->bIsInSafePosition)B->UnsetIsInSafePosition();return true;}
 bool ApplyCollisionAlt(CPhysical*,CColPoint &p,float &a,CVector&,CVector&){if(!p.accepted)return false;a=p.impulseA;++responses;return true;}
};
struct CVehicle:CPhysical {bool car=true,boat=false;bool IsCar() const{++carReads;return car;}bool IsBoat()const{return boat;}};
enum{SURFACE_WOOD_SOLID=7};
namespace VehicleDeformation {
 bool enabled=false;unsigned calls=0,retained=0;
 inline bool IsVehicleDeformationEnabled(){return enabled;}
 struct CollisionPose{float right[3],forward[3],up[3],position[3];};
 struct Event{CVehicle*receiver;const CEntity*other;CVector point,normal,localPoint,localNormal;float impulse;bool poseSupplied;};
 Event events[16];
 static CVector Local(CVector value,CVector r,CVector f,CVector u){return CVector(value.x*r.x+value.y*r.y+value.z*r.z,value.x*f.x+value.y*f.y+value.z*f.z,value.x*u.x+value.y*u.y+value.z*u.z);}
 void RecordCollision(CVehicle *receiver,const CVector&point,const CVector&normal,float impulse,const CEntity*other,const CollisionPose*pose=nullptr){
  ++calls;unsigned slot=0;for(;slot<retained;slot++)if(events[slot].receiver==receiver)break;
  if(slot<retained&&impulse<=events[slot].impulse)return;
  if(slot==retained){if(retained==16)std::abort();retained++;}
  CVector p=receiver->position,r=receiver->right,f=receiver->forward,u=receiver->up;
  if(pose){p=CVector(pose->position[0],pose->position[1],pose->position[2]);r=CVector(pose->right[0],pose->right[1],pose->right[2]);f=CVector(pose->forward[0],pose->forward[1],pose->forward[2]);u=CVector(pose->up[0],pose->up[1],pose->up[2]);}
  CVector d(point.x-p.x,point.y-p.y,point.z-p.z);
  events[slot]={receiver,other,point,normal,Local(d,r,f,u),Local(normal,r,f,u),impulse,pose!=nullptr};
 }
}
using Site=void(*)(CPhysical*,CPhysical*,CColPoint*,int);
using LegacySite=void(*)(CPhysical*,CPhysical*,CColPoint*,float,float);
#include "vehicle-deformation-collision-production.inc"
static void Check(bool yes,const char*message){++checks;if(!yes){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static void Reset(bool enabled=true){VehicleDeformation::enabled=enabled;VehicleDeformation::calls=VehicleDeformation::retained=0;typeReads=carReads=poseReads=0;}
static CColPoint Contact(){CColPoint p;p.point=CVector(4,-7,2);p.normal=CVector(.6f,.8f,0);return p;}
static const VehicleDeformation::Event&Event(CVehicle *car){for(unsigned i=0;i<VehicleDeformation::retained;i++)if(VehicleDeformation::events[i].receiver==car)return VehicleDeformation::events[i];std::abort();}
static void EveryNativeResponse(){
 Check(sizeof(sites)/sizeof(sites[0])==10,"all eight dynamic/simple and two immovable accepted response loops extracted");
 unsigned stationary=0;
 for(unsigned s=0;s<10;s++){
  CVehicle a,b;CColPoint p=Contact();Reset();sites[s](&a,&b,&p,1);
  Check(VehicleDeformation::retained==2,"accepted car contact records both participating cars");
  auto ea=Event(&a),eb=Event(&b);
  Check(ea.other==&b&&eb.other==&a,"opposing vehicle identities retained");
  Check(Same(ea.point,p.point)&&Same(eb.point,p.point),"original generated contact point retained for both");
  Check(Same(ea.normal,p.normal)&&Same(eb.normal,-p.normal),"receiver inward normal signs are opposite");
  Check(ea.impulse==11&&eb.impulse==(immovable[s]?11:17),"immovable B uses accepted contact impulse; dynamic B retains its own impulse");
  Check(ea.poseSupplied&&eb.poseSupplied&&poseReads==8,"two poses captured once per manifold, not per response");
  Check(a.responses==1&&b.responses==(immovable[s]?0u:1u),"native response count and immovable B behavior untouched");
  a.m_fDamageImpulse=b.m_fDamageImpulse=9999;Reset();sites[s](&a,&b,&p,1);
  Check(VehicleDeformation::retained==2,"unrelated native strongest damage record cannot suppress a valid deformation contact");
  Check(a.m_fDamageImpulse==9999&&b.m_fDamageImpulse==9999,"paired deformation does not change native damage state");
  Reset(false);sites[s](&a,&b,&p,1);
  Check(VehicleDeformation::calls==0&&typeReads==0&&carReads==0&&poseReads==0,"OFF performs no type checks, pose copies or optional calls");
  Reset();p.accepted=false;sites[s](&a,&b,&p,1);
  Check(VehicleDeformation::calls==0,"refused native response cannot manufacture a dent impulse");
  if(immovable[s])stationary++;
 }
 Check(stationary==2,"both immovable-B friction response branches covered");
}
static void RollbackAndOrder(){
 for(unsigned s=0;s<10;s++)if(!immovable[s]){
  CVehicle a,b;b.position=CVector(10,0,0);b.rollbackShift=CVector(-.5f,0,0);b.bIsInSafePosition=true;
  CColPoint p[2]={Contact(),Contact()};p[0].point=CVector(11,2,3);p[1].point=CVector(12,4,3);p[1].impulseA=23;p[1].impulseB=29;
  Reset();sites[s](&a,&b,p,2);
  Check(b.rollbackCalls==1&&b.position.x==9.5f,"accepted native B rollback occurs exactly once");
  Check(Event(&b).impulse==29&&Same(Event(&b).localPoint,CVector(2,4,3)),"stronger later manifold point uses original B pose after rollback");
  Check(poseReads==8,"all manifold points reuse the two initial poses");
  Check(a.responses==2&&b.responses==2,"all native responses still run");
  // A/B order reverses the native contact normal. The world hit on each car
  // remains unchanged, including a 90-degree rotated front-facing receiver.
  CVehicle moving,parked;parked.position=CVector(20,10,0);parked.right=CVector(0,1,0);parked.forward=CVector(-1,0,0);
  CColPoint one=Contact();one.point=CVector(18,10,0);one.normal=CVector(-1,0,0);one.impulseA=one.impulseB=40;
  Reset();sites[s](&moving,&parked,&one,1);auto before=Event(&parked);
  one.normal=-one.normal;Reset();sites[s](&parked,&moving,&one,1);auto after=Event(&parked);
  Check(Same(before.localPoint,CVector(0,2,0))&&Same(after.localPoint,before.localPoint),"A/B order preserves rotated receiver front-local contact");
  Check(Same(before.localNormal,CVector(0,-1,0))&&Same(after.localNormal,before.localNormal),"A/B order preserves receiver-local inward normal");
 }
}
static void Eligibility(){
 CColPoint p=Contact();CVehicle a,b;
 Reset(false);VehicleDeformationCollisionContext off(nullptr,nullptr);RecordVehicleDeformationPair(off,nullptr,nullptr,p,1,1);
 Check(!off.active&&VehicleDeformation::calls==0&&poseReads==0&&typeReads==0,"disabled context never dereferences participants");
 for(int kind:{0,2,3}){
  Reset();a.type=kind;b.type=kind;sites[0](&a,&b,&p,1);
  Check(VehicleDeformation::calls==0&&poseReads==0,"ped and nonvehicle-only pairs avoid pose work");
 }
 a.type=1;b.type=2;Reset();sites[0](&a,&b,&p,1);Check(poseReads==0&&VehicleDeformation::calls==0,"vehicle versus ped cannot do deformation work");
 a.type=b.type=1;a.car=false;b.car=true;Reset();sites[0](&a,&b,&p,1);Check(VehicleDeformation::retained==1&&Event(&b).receiver==&b,"noncar vehicle excluded as receiver");
 a.car=true;b.type=0;Reset();sites[4](&a,&b,&p,1);Check(VehicleDeformation::retained==1&&Event(&a).receiver==&a,"wall contact still records moving car only");
 a.type=b.type=1;Reset();CColPoint repeated[3]={Contact(),Contact(),Contact()};repeated[0].impulseA=22;repeated[1].point.x=99;repeated[1].impulseA=22;repeated[2].impulseA=21;
 sites[0](&a,&b,repeated,3);Check(VehicleDeformation::retained==2&&Event(&a).point.x==4,"module strongest/earliest coalescing prevents duplicate impacts");
}
static void LegacyRegression(){
#ifdef HAVE_LEGACY_COLLISION
 CVehicle a,b;CColPoint p=Contact();Reset();Legacy::sites[8](&a,&b,&p,11,17);
 Check(VehicleDeformation::retained==1&&Event(&a).receiver==&a,"installed immovable-B baseline demonstrably omits target car");
 Reset();sites[4](&a,&b,&p,1);Check(VehicleDeformation::retained==2,"same accepted immovable contact now reaches target car");
 b.m_fDamageImpulse=0;b.position=CVector(10,0,0);b.rollbackShift=CVector(-.5f,0,0);b.bIsInSafePosition=true;
 p.point=CVector(11,2,3);Reset();Legacy::EarlyRollback(&a,&b,p,17);Legacy::sites[1](&a,&b,&p,11,17);
 Check(Same(Event(&b).localPoint,CVector(1,2,3)),"legacy early hook protects first equal-strength contact");
 p.point=CVector(12,4,3);Legacy::sites[1](&a,&b,&p,23,29);
 Check(Same(Event(&b).localPoint,CVector(2.5f,4,3)),"installed baseline stronger later contact uses rolled-back B pose and shifts dent by0.5m");
 Check(b.m_fDamageImpulse==29&&Same(b.m_vecDamageNormal,p.normal)&&b.m_pDamageEntity==&a,"legacy standard damage metadata retained in comparison");
 std::puts("BASELINE REPRODUCED: immovable B omitted; stronger later B contact shifted0.500m after rollback.");
#endif
}
int main(){EveryNativeResponse();RollbackAndOrder();Eligibility();LegacyRegression();std::printf("Vehicle deformation collision adapter: %u checks PASS (10 actual accepted-response prefixes, paired poses, baseline regressions; native force code unchanged).\n",checks);}
