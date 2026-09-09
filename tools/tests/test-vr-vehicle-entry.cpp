#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <cassert>
#include <deque>
#include "vr-vehicle-entry-helper.inc"
#include "vr-vehicle-entry-enums.inc"
#define __ANDROID__ 1
#define GTA_VR_WEAPONS 1
#define CANCELLABLE_CAR_ENTER 1
#define nil nullptr
using int16=int16_t;
static unsigned checks;
static void Check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
template<class T> static T Abs(T value){return value<0?-value:value;}
enum eDoors {DOOR_FRONT_LEFT,DOOR_FRONT_RIGHT,DOOR_REAR_LEFT,DOOR_REAR_RIGHT};
struct CVector {float x=0,y=0,z=0;float Magnitude()const{return std::sqrt(x*x+y*y+z*z);}};
struct CAnimBlendAssociation {
 int animId=0;float blendDelta=0,timeLeft=1;
 void (*callback)(CAnimBlendAssociation*,void*)=nullptr;void *context=nullptr;
 float GetTimeLeft(){return timeLeft;}
 void SetFinishCallback(void(*cb)(CAnimBlendAssociation*,void*),void *arg){callback=cb;context=arg;}
 void Finish(){Check(callback!=nullptr,"native animation registered its continuation");callback(this,context);}
};
struct CPad {bool disabled=false;int16 x=0,y=0;int accelerate=0,brake=0;
 static CPad *GetPad(int);bool ArePlayerControlsDisabled(){return disabled;}
 int16 GetPedWalkLeftRight(){return x;}int16 GetPedWalkUpDown(){return y;}
 int GetAccelerate(){return accelerate;}int GetBrake(){return brake;}
};
static CPad Pads[1];CPad *CPad::GetPad(int){return &Pads[0];}
struct CPed;struct CPlayerPed;struct CCopPed;
struct DamageState {int door[4]={DOOR_STATUS_OK,DOOR_STATUS_OK,DOOR_STATUS_OK,DOOR_STATUS_OK};void SetDoorStatus(eDoors d,int value){door[d]=value;}int GetDoorStatus(eDoors d){return door[d];}};
struct CVehicle {
 CPed *pDriver=nullptr,*pPassengers[3]={};bool bIsVan=false,bIsBus=false,bLowVehicle=false,bIsHandbrakeOn=false,bIsBeingCarJacked=false;
 bool bike=false,openTop=false,upsideDown=false,missing=false,open=false;int status=STATUS_ABANDONED,appearance=0,m_nDoorLock=CARLOCK_UNLOCKED;
 unsigned m_nNumGettingIn=1,m_nGettingInFlags=0,m_nNumMaxPassengers=3;int VehicleCreatedBy=0;
 struct {int m_nCruiseSpeed=17;} AutoPilot;
 CVector m_vecMoveSpeed,right,up;DamageState Damage;
 bool IsBike(){return bike;}bool IsCar(){return !bike;}bool IsUpsideDown(){return upsideDown;}bool IsOpenTopCar(){return openTop;}
 int GetStatus(){return status;}void SetStatus(int s){status=s;}int GetVehicleAppearance(){return appearance;}
 CVector GetRight(){return right;}CVector GetUp(){return up;}
 bool IsDoorMissing(eDoors){return missing;}bool IsDoorFullyOpen(eDoors){return open;}bool IsDoorClosed(eDoors){return !open;}
 bool CanPedOpenLocks(CPed*){return m_nDoorLock==CARLOCK_UNLOCKED;}
 void ProcessOpenDoor(int,int anim,float){open=anim!=ANIM_STD_CAR_CLOSE_DOOR_LHS;}
 bool IsRoomForPedToLeaveCar(int,void*){return true;}
 void MakeNonDraggedPedsLeaveVehicle(CPed*,CPed*,CPlayerPed*&,CCopPed*&){}
};
struct CAutomobile:CVehicle {int m_nWheelsOnGround=4;};
struct CBike:CVehicle {int m_bikeAnimType=0;bool bIsBeingPickedUp=false;};
struct CPed {
 bool player=true,bCancelEnteringCar=false,bDontDragMeOutCar=false,bGonnaKillTheCarJacker=false,bBusJacked=false,bUsePedNodeSeek=false,bFleeAfterExitingCar=false,bInVehicle=false;
 int m_nPedState=PED_IDLE,m_nLastPedState=PED_IDLE,m_nPedType=PEDTYPE_PLAYER1,m_vehDoor=CAR_DOOR_LF,m_objective=OBJECTIVE_ENTER_CAR_AS_DRIVER,CharCreatedBy=0,m_leaveCarTimer=0;
 float m_fHealth=100;CVehicle *m_pMyVehicle=nullptr;CPed *m_pedInObjective=nullptr;CAnimBlendAssociation *m_pVehicleAnim=nullptr;
 void *m_pLastPathNode=nullptr,*m_pNextPathNode=nullptr;struct Stats {int m_temper=0,m_fear=0;} stats;Stats *m_pedStats=&stats;
 unsigned quit=0,restored=0,fallen=0,entered=0,removedWeapon=0,wanted=0;
 bool IsPlayer(){return player;}bool EnteringCar(){return m_nPedState==PED_ENTER_CAR||m_nPedState==PED_CARJACK;}
 bool IsNotInWreckedVehicle(){return m_pMyVehicle&&m_pMyVehicle->status!=STATUS_WRECKED;}bool DyingOrDead(){return m_fHealth<=0;}
 void *GetClump(){return this;}void QuitEnteringCar(){++quit;m_nPedState=PED_IDLE;m_pVehicleAnim=nullptr;}
 void RestorePreviousObjective(){++restored;}void SetFall(int,int,bool){++fallen;}
 void SetWantedLevelNoDrop(int){++wanted;}bool IsGangMember(){return false;}
 void SetObjective(int objective,void*){m_objective=objective;}void ClearObjective(){}void SetWanderPath(float){}
 void SetBeingDraggedFromCar(CVehicle*,int,bool){}void RegisterThreatWithGangPeds(CPed*){}
 void RemoveWeaponWhenEnteringVehicle(){++removedWeapon;}void SetPedState(int state){m_nPedState=state;}void Say(int){}void SetRadioStation(){}
 void ProcessEntryCancellation();void ProcessBaselineCancellation();
 static void PedAnimAlignCB(CAnimBlendAssociation*,void*);static void PedAnimDoorOpenCB(CAnimBlendAssociation*,void*);
 static void PedAnimGetInCB(CAnimBlendAssociation*,void*);static void PedAnimDoorCloseCB(CAnimBlendAssociation*,void*);
 static void PedAnimPullPedOutCB(CAnimBlendAssociation*,void*){}
 static void PedSetInCarCB(CAnimBlendAssociation*,void *arg){CPed *p=(CPed*)arg;++p->entered;p->bInVehicle=true;p->m_nPedState=PED_DRIVING;p->m_pMyVehicle->pDriver=p;}
};
struct CPlayerPed:CPed {CCopPed *m_pArrestingCop=nullptr;};
struct CCopPed:CPed {int m_bDragsPlayerFromCar=0;void SetArrestPlayer(CPed*){}};
static CPlayerPed *testPlayer=nullptr;static CPlayerPed *FindPlayerPed(){return testPlayer;}
struct CGeneral {static float GetRandomNumberInRange(float,float){return 0;}};
struct CTimer {static int GetTimeInMilliseconds(){return 0;}};
struct CAnimManager {
 static std::deque<CAnimBlendAssociation> animations;
 static CAnimBlendAssociation *AddAnimation(void*,int,int id){animations.emplace_back();animations.back().animId=id;return &animations.back();}
 static CAnimBlendAssociation *BlendAnimation(void *clump,int group,int id,float){return AddAnimation(clump,group,id);}
};
std::deque<CAnimBlendAssociation> CAnimManager::animations;
// Native source warnings concern untested invalid door enum inputs. The fixture
// deliberately supplies the four real door values; no implementation rewriting.
#pragma warning(push)
#pragma warning(disable:4701 4703)
#include "vr-vehicle-entry-production.inc"
#pragma warning(pop)
static void Begin(CPlayerPed &p,CAutomobile &car,int x,int y){
 p.m_nPedState=PED_IDLE;p.ProcessEntryCancellation();
 p.m_pMyVehicle=&car;p.m_nPedState=PED_ENTER_CAR;p.bCancelEnteringCar=false;p.bInVehicle=false;
 Pads[0].x=int16(x);Pads[0].y=int16(y);p.m_pVehicleAnim=CAnimManager::AddAnimation(&p,0,ANIM_STD_CAR_ALIGN_DOOR_LHS);
 p.ProcessEntryCancellation();CPed::PedAnimAlignCB(p.m_pVehicleAnim,&p);
}
static void FinishEntry(CPlayerPed &p){
 for(int stage=0;stage<4 && p.EnteringCar();++stage){Check(p.m_pVehicleAnim!=nullptr,"entry has active native animation");p.m_pVehicleAnim->Finish();}
 Check(p.entered==1&&p.bInVehicle&&p.m_nPedState==PED_DRIVING,"native open-getin-close callbacks reach seated state exactly once");
}
static void ExerciseCallbacks(){
 // Prove the report using the exact old cancellation block and native callback.
 CPlayerPed old;CAutomobile oldCar;testPlayer=&old;Begin(old,oldCar,0,-128);
 Check(old.m_pVehicleAnim->animId==ANIM_STD_CAR_OPEN_DOOR_LHS,"closed door first plays opening animation");
 old.ProcessBaselineCancellation();Check(old.bCancelEnteringCar,"baseline incorrectly cancels held approach movement");
 old.m_pVehicleAnim->Finish();Check(old.quit==1&&!old.bInVehicle&&oldCar.Damage.GetDoorStatus(DOOR_FRONT_LEFT)==DOOR_STATUS_SWINGING,"baseline opens door then stops instead of entering");
 oldCar.open=true;Begin(old,oldCar,0,-128);old.ProcessBaselineCancellation();
 Check(!old.bCancelEnteringCar,"already-open door bypasses old cancellation stage");FinishEntry(old);
 // Held movement must survive the complete real callback chain for every door.
 for(int type=0;type<4;++type)for(int door:{CAR_DOOR_LF,CAR_DOOR_RF,CAR_DOOR_LR,CAR_DOOR_RR}){
  CPlayerPed p;CAutomobile car;testPlayer=&p;p.m_vehDoor=door;
  if(door!=CAR_DOOR_LF)p.m_objective=OBJECTIVE_ENTER_CAR_AS_PASSENGER;
  car.bLowVehicle=type==1;car.bIsVan=type==2;car.bIsBus=type==3;Begin(p,car,0,-128);
  for(int frame=0;frame<80;++frame){Pads[0].x=int16(frame%2?128:0);Pads[0].y=int16(frame%2?0:-128);p.ProcessEntryCancellation();Check(!p.bCancelEnteringCar,"held approach and head-relative axis rotation do not cancel");}
  FinishEntry(p);p.ProcessEntryCancellation();Check(p.quit==0,"one attempt completes without quitting");
 }
 // Recenter then move is still an explicit cancellation, on either axis.
 for(int axis=0;axis<2;++axis){CPlayerPed p;CAutomobile car;testPlayer=&p;Begin(p,car,0,-128);
  Pads[0].x=Pads[0].y=0;p.ProcessEntryCancellation();Pads[0].x=int16(axis?128:0);Pads[0].y=int16(axis?0:-128);p.ProcessEntryCancellation();
  Check(p.bCancelEnteringCar,"fresh movement after neutral intentionally cancels");p.m_pVehicleAnim->Finish();Check(p.quit==1&&!p.bInVehicle,"native callback honors deliberate cancellation");
  Begin(p,car,0,-128);p.ProcessEntryCancellation();Check(!p.bCancelEnteringCar,"new attempt does not inherit armed cancellation");FinishEntry(p);
 }
 {CPlayerPed p;CAutomobile car;testPlayer=&p;car.m_nDoorLock=CARLOCK_LOCKED;Begin(p,car,0,-128);
  Check(p.bCancelEnteringCar,"native locked-door path still requests abort");p.m_pVehicleAnim->Finish();Check(p.quit==1&&!p.bInVehicle,"locked vehicle cannot be entered");}
 for(int reason=0;reason<3;++reason){CPlayerPed p;CAutomobile car;testPlayer=&p;Begin(p,car,0,-128);
  if(reason==0)car.m_vecMoveSpeed.x=.21f;if(reason==1)p.m_fHealth=0;if(reason==2)car.status=STATUS_WRECKED;
  p.m_pVehicleAnim->Finish();Check(!p.bInVehicle&&p.entered==0,"moving/dead/wrecked native guard remains effective");if(reason==0)Check(p.fallen==1,"moving car still makes entrant fall");}
 {CPlayerPed p;CAutomobile car;testPlayer=&p;Begin(p,car,0,-128);
  Pads[0].x=Pads[0].y=0;p.ProcessEntryCancellation();Pads[0].disabled=true;Pads[0].y=-128;p.ProcessEntryCancellation();Pads[0].disabled=false;p.ProcessEntryCancellation();
  Check(!p.bCancelEnteringCar,"actual entry observer resets when controls are disabled");FinishEntry(p);}
 {CPlayerPed p;CAutomobile car;testPlayer=&p;p.player=false;Begin(p,car,0,-128);p.ProcessEntryCancellation();
  Check(!p.bCancelEnteringCar,"NPC entry does not consume player cancellation input");FinishEntry(p);}
 for(bool cancel:{false,true}){CPlayerPed p;CPed driver;CAutomobile car;testPlayer=&p;driver.player=false;driver.m_nPedState=PED_DRIVING;car.pDriver=&driver;car.bLowVehicle=true;
  Begin(p,car,0,-128);p.m_nPedState=PED_CARJACK;
  if(cancel){Pads[0].x=Pads[0].y=0;p.ProcessEntryCancellation();Pads[0].y=-128;}
  p.ProcessEntryCancellation();Check(!p.bCancelEnteringCar,"occupied car waits for native pull-out stage before cancellation");
  p.m_pVehicleAnim->Finish();Check(p.m_pVehicleAnim->callback==CPed::PedAnimPullPedOutCB,"native occupied-door callback schedules occupant pull-out");
  p.ProcessEntryCancellation();Check(p.bCancelEnteringCar==cancel,"fresh-movement cancellation carries into native carjack stage while held approach does not");
  p.m_nPedState=PED_IDLE;p.ProcessEntryCancellation();}
}
static void ExerciseLifetime(){
 VrVehicleEntry::CancelInput input;int a=0,b=0,car1=0,car2=0;
 Check(!input.Update(&a,&car1,true,true,128,0),"attempt begins unarmed with held movement");
 Check(!input.Update(&a,&car1,true,true,0,0),"neutral arms without cancellation");
 Check(input.Update(&a,&car1,true,true,128,0),"new movement cancels");
 Check(!input.Update(&b,&car1,true,true,128,0),"different player cannot inherit armed latch");
 input.Update(&b,&car1,true,true,0,0);Check(!input.Update(&b,&car2,true,true,128,0),"different vehicle resets latch");
 input.Update(&b,&car2,true,true,0,0);Check(!input.Update(&b,&car2,false,true,128,0),"end of entry resets latch");
 Check(!input.Update(&b,&car2,true,true,128,0),"same-player same-vehicle retry starts unarmed");
 input.Update(&b,&car2,true,true,0,0);Check(!input.Update(&b,&car2,true,false,128,0),"disabled controls reset latch");
 Check(!input.Update(&b,&car2,true,true,128,0),"resumed controls do not convert held input into cancel");
 Check(!input.Update(&b,nullptr,true,true,0,0),"cleared vehicle reference resets latch");
 input.Update(&a,&car1,true,true,0,0);for(int x=-16;x<=16;++x)Check(!input.Update(&a,&car1,true,true,x,0),"neutral noise cannot cancel");
 Check(input.Update(&a,&car1,true,true,32,0),"intent threshold bounded below Quest mapper deadzone");
}
int main(){try{ExerciseCallbacks();ExerciseLifetime();std::printf("Quest vehicle entry: %u checks PASS; extracted native cancellation plus align/open/get-in/close callbacks\n",checks);return 0;}catch(std::exception const&e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
