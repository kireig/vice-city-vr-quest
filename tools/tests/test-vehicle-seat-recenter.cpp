#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include "vehicle-seat-recenter-helper.inc"
#define nil nullptr
#define GTA_VR_WEAPONS 1
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define ALOG(...) ((void)0)
using float32 = float;
using bool32 = int;
using int32 = int32_t;
using PedState = int;
static unsigned checks, recenters;
static void Check(bool ok, const char *message) { ++checks; if(!ok) throw std::runtime_error(message); }
static bool Near(float a,float b) { return std::fabs(a-b)<0.00002f; }

namespace rw { namespace vulkan {
struct State {
 bool32 firstPersonActive=0, temporalHistoryValid=0, fpFollowHeading=0, fpUseFullBasis=0;
 float headPosition[3]={}, headQuat[4]={0,0,0,1}, headYaw=0, fpLatchedHeadYaw=0, fpAnchorYaw=0;
 float fpLatchedHeadPos[3]={}, fpHeadWorld[3]={}, fpPlayX[3]={},fpPlayY[3]={},fpPlayZ[3]={};
} gvk;
static void resetAtomicMotionHistory() {}
} }
#include "vehicle-seat-recenter-backend.inc"
struct CVector {
 float x=0,y=0,z=0;
 CVector()=default; CVector(float a,float b,float c):x(a),y(b),z(c){}
 CVector operator*(float f)const{return {x*f,y*f,z*f};}
 CVector &operator+=(CVector b){x+=b.x;y+=b.y;z+=b.z;return *this;}
};
struct CMatrix { CVector position; CVector &GetPosition(){return position;} };
struct CEntity {};
struct CPlayerPed;
struct CVehicle {
 CPlayerPed *pDriver=nullptr,*pPassengers[8]={}; int model=175;
 int GetModelIndex(){return model;} CVector GetForward(){return {0,1,0};}
};
struct CPlayerPed:CEntity {
 bool inVehicle=false,dead=false; int state=0,m_objective=0;
 CVehicle *m_pMyVehicle=nullptr; void *m_rwObject=this,*m_pFrames[64]={};
 bool InVehicle(){return inVehicle;} int GetPedState(){return state;} bool DyingOrDead(){return dead;}
};
static CPlayerPed *currentPlayer;
static CPlayerPed *FindPlayerPed(){return currentPlayer;}
static CVehicle *FindPlayerVehicle(){return currentPlayer?currentPlayer->m_pMyVehicle:nullptr;}
struct PlayerInfo { bool remote=false; bool IsPlayerInRemoteMode(){return remote;} };
struct CWorld { static PlayerInfo Players[1]; static int PlayerInFocus; };
PlayerInfo CWorld::Players[1]; int CWorld::PlayerInFocus;
struct { bool m_bMenuActive=false,m_bGameNotLoaded=false,m_bWantToRestart=false,m_bWantToLoad=false; } FrontEndMenuManager;
struct CGame { static bool playingIntro; }; bool CGame::playingIntro;
struct CCutsceneMgr { static bool running,processing; static bool IsRunning(){return running;} static bool IsCutsceneProcessing(){return processing;} };
bool CCutsceneMgr::running, CCutsceneMgr::processing;
struct { bool m_WideScreenOn=false; } TheCamera;
static constexpr int GS_PLAYING_GAME=1;
static int gGameState=GS_PLAYING_GAME;
static CEntity *gCutsceneCameraActor,*gVrPlayerEntity;
static bool gVrFirstPersonActive,gVrInVehicle;
static VrVehicleSeatRecenter gVehicleSeatRecenter;
namespace androidgame {
static bool menu=false; static int cutsceneMode=0;
bool VrMenuConsumesInput(){return menu;} int VrCutsceneMode(){return cutsceneMode;}
void VrRecenterView();
}
namespace OculusVR {
static bool thirdPerson=false;
bool IsQuestVehicleThirdPerson(){return thirdPerson;}
void ResetQuestDrivingInteraction(){++recenters;}
struct DefaultVehicleViewOffset { int seatDistanceCm=22,seatHeightCm=31; };
struct VehicleViewCalibration { int defaultSeatHeightCm=7,defaultSeatDistanceCm=-3,seatHeightCm=11,seatDistanceCm=-8; };
struct VehicleCategoryCalibration { int seatHeightCm=25,seatDistanceCm=19; };
static DefaultVehicleViewOffset gDefaultVehicleViewOffset[1];
static VehicleViewCalibration modelCalibration;
static VehicleCategoryCalibration categoryCalibration;
static int defaultView=0;
static void LoadDrivingSettings(){}
static int GetVehicleCategory(CVehicle*){return 0;}
static int GetDefaultVehicleViewTypeForCurrentVehicle(){return defaultView;}
static VehicleViewCalibration *GetViewCalibration(int){return &modelCalibration;}
static VehicleCategoryCalibration *GetCategoryCalibration(CVehicle*){return &categoryCalibration;}
}
namespace xrvk {
using XrViewStateFlags=unsigned;
static constexpr unsigned XR_VIEW_STATE_POSITION_VALID_BIT=2,XR_VIEW_STATE_ORIENTATION_VALID_BIT=1;
static constexpr unsigned XR_VIEW_STATE_POSITION_TRACKED_BIT=8,XR_VIEW_STATE_ORIENTATION_TRACKED_BIT=4;
static constexpr int XR_SESSION_STATE_FOCUSED=5;
struct ViewState { unsigned viewStateFlags=15; };
struct { bool headPoseTrackedForFrame=false,theaterMode=false; int sessionState=XR_SESSION_STATE_FOCUSED; } g;
}
#define XR_SUCCEEDED(result) ((result)>=0)
// The native offset function has a pre-existing unused categoryIndex local.
#pragma warning(push)
#pragma warning(disable:4189)
#include "vehicle-seat-recenter-production.inc"
#pragma warning(pop)

static void Head(float x,float y,float z,float yaw=0) {
 const float pos[3]={x,y,z},quat[4]={0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};
 rw::vulkan::setHeadPose(pos,yaw,quat);
}
static void Anchor() {
 CMatrix m;m.position={10,20,1.4f};OculusVR::ApplyQuestVehicleViewOffset(&m);
 const float right[3]={1,0,0},up[3]={0,0,1},forward[3]={0,1,0};
 if(gVrInVehicle)rw::vulkan::setFirstPersonAnchorBasis(&m.position.x,right,up,forward,1.5707963268f,1);
 else rw::vulkan::setFirstPersonAnchor(&m.position.x,1.5707963268f,1,1);
}
static CVector Eye() {
 float r[3],u[3],f[3];CVector p;
 Check(rw::vulkan::getFirstPersonViewFrame(r,u,f,&p.x)!=0,"native view exists");return p;
}
static void Reset(CPlayerPed &p,CVehicle &car) {
 p={};car={};currentPlayer=&p;p.state=PED_DRIVING;p.m_objective=OBJECTIVE_NONE;p.m_pFrames[PED_HEAD]=&p;
 p.m_pMyVehicle=&car;p.inVehicle=true;car.pDriver=&p;
 gVehicleSeatRecenter.Reset();recenters=0;rw::vulkan::gvk={};gVrInVehicle=false;
 gGameState=GS_PLAYING_GAME;FrontEndMenuManager={};CGame::playingIntro=false;
 CCutsceneMgr::running=CCutsceneMgr::processing=false;TheCamera.m_WideScreenOn=false;
 CWorld::Players[0].remote=false;androidgame::menu=false;OculusVR::thirdPerson=false;
 xrvk::g={};xrvk::UpdateTracking(0,2,{});Head(0,1.7f,0);Anchor();
}
static void Tick(bool post=true) { EvaluateSeat(post);Anchor(); }

static void EntryAndMotion() {
 CPlayerPed p;CVehicle car;Reset(p,car);
 p.inVehicle=false;car.pDriver=nullptr;p.state=PED_ENTER_CAR;p.m_objective=OBJECTIVE_ENTER_CAR_AS_DRIVER;
 for(int i=0;i<16;i++){Head(0,1.7f,0);Tick(false);Tick();}
 Check(recenters==0,"no new recenter during native entry animation");
 // Native PedSetInCarCB enables occupancy at completion. The XR renderer
 // still accepts orientation-only frames, so its automatic basis latch can
 // capture a stale physical position at exactly that completed-seat frame.
 p.state=PED_DRIVING;p.m_objective=OBJECTIVE_NONE;p.inVehicle=true;car.pDriver=&p;
 xrvk::UpdateTracking(0,2,{xrvk::XR_VIEW_STATE_ORIENTATION_VALID_BIT});Tick();
 Check(recenters==0,"completed seat with stale XR position stays pending");
 xrvk::UpdateTracking(0,2,{});
 Head(.35f,1.42f,-.18f,.31f);Anchor();
 const CVector old=Eye();
 Check(std::fabs(old.x-10)>.3f,"actual old backend retains stale-entry lateral offset after tracking recovers");
 Tick(false);Check(recenters==0,"pre-physics does not latch seat");
 Tick();Check(recenters==1,"first completed post-physics seat recenters once");
 CVector eye=Eye();Check(Near(eye.x,10)&&Near(eye.y,20.19f)&&Near(eye.z,1.78f),"all physical entry displacement removed, custom default offsets preserved");
 Check(Near(rw::vulkan::gvk.fpLatchedHeadYaw,.31f),"actual current head facing is neutral forward");
 for(int i=0;i<240;i++){
  Head(.35f+.001f*i,1.42f,-.18f,.31f+.001f*i);Tick(false);Tick();
  Check(recenters==1,"steady driving never repeats recenter or releases wheel grabs");
 }
 eye=Eye();Check(std::fabs(eye.x-10)>.2f,"physical leaning still displaces native rendered view");
 androidgame::menu=true;Tick();androidgame::menu=false;Tick();
 xrvk::g.headPoseTrackedForFrame=false;Tick();xrvk::UpdateTracking(0,2,{});Tick();
 Check(recenters==1,"menus and tracking recovery do not rearm an already centered seat");
 androidgame::VrRecenterView();Anchor();Tick();
 Check(recenters==2,"manual recenter is respected without a second automatic reset");
 Check(OculusVR::gDefaultVehicleViewOffset[0].seatHeightCm==31&&OculusVR::modelCalibration.defaultSeatDistanceCm==-3,"saved offset values never overwritten");
 OculusVR::defaultView=-1;Anchor();eye=Eye();
 Check(Near(eye.y,20.11f)&&Near(eye.z,1.76f),"immersive category and model offsets remain additive");
 OculusVR::defaultView=0;
}

static void SuppressionAndLifecycle() {
 CPlayerPed p;CVehicle car;
 for(int guard=0;guard<16;guard++){
  Reset(p,car);
  if(guard==0)FrontEndMenuManager.m_bMenuActive=true;
  if(guard==1)FrontEndMenuManager.m_bGameNotLoaded=true;
  if(guard==2)FrontEndMenuManager.m_bWantToRestart=true;
  if(guard==3)FrontEndMenuManager.m_bWantToLoad=true;
  if(guard==4)CGame::playingIntro=true;
  if(guard==5)CCutsceneMgr::running=true;
  if(guard==6)CCutsceneMgr::processing=true;
  if(guard==7)TheCamera.m_WideScreenOn=true;
  if(guard==8)CWorld::Players[0].remote=true;
  if(guard==9)OculusVR::thirdPerson=true;
  if(guard==10)androidgame::menu=true;
  if(guard==11)p.m_rwObject=nullptr;
  if(guard==12)p.m_pFrames[PED_HEAD]=nullptr;
  if(guard==13)p.dead=true;
  if(guard==14)gGameState=0;
  if(guard==15)xrvk::g.theaterMode=true;
  for(int i=0;i<4;i++){Tick(false);Tick();}
  Check(recenters==0,"unsafe/non-seat frame never consumes pending center");
  FrontEndMenuManager={};CGame::playingIntro=false;CCutsceneMgr::running=CCutsceneMgr::processing=false;
  TheCamera.m_WideScreenOn=false;CWorld::Players[0].remote=false;OculusVR::thirdPerson=false;
  androidgame::menu=false;p.m_rwObject=&p;p.m_pFrames[PED_HEAD]=&p;p.dead=false;gGameState=GS_PLAYING_GAME;xrvk::g.theaterMode=false;
  Tick();Tick();Check(recenters==1,"postponed entry captures exactly once when safe");
 }
 for(int state:{PED_SEEK_CAR,PED_SEEK_IN_BOAT,PED_OPEN_DOOR,PED_CARJACK,PED_ENTER_CAR,PED_STEAL_CAR,PED_EXIT_CAR,PED_DRAG_FROM_CAR}){
  Reset(p,car);p.state=state;Tick();Check(recenters==0,"all native transition states defer recenter");
 }
 for(int objective:{OBJECTIVE_ENTER_CAR_AS_DRIVER,OBJECTIVE_ENTER_CAR_AS_PASSENGER,OBJECTIVE_LEAVE_CAR,OBJECTIVE_LEAVE_CAR_AND_DIE}){
  Reset(p,car);p.m_objective=objective;Tick();Check(recenters==0,"all entry/exit objectives defer recenter");
 }
 Reset(p,car);car.pDriver=nullptr;Tick();Check(recenters==0,"InVehicle without registered seat is not enough");
 car.pPassengers[7]=&p;Tick();Check(recenters==1&&gVehicleSeatRecenter.seat==8,"passenger seat is recognized");
 car.pPassengers[7]=nullptr;car.pDriver=&p;Tick();Check(recenters==2&&gVehicleSeatRecenter.seat==0,"seat change in same car recenters");
 p.inVehicle=false;p.state=PED_IDLE;Tick();p.inVehicle=true;p.state=PED_DRIVING;Tick();Check(recenters==3,"leave and reenter same car rearms");
 CVehicle other;other.pDriver=&p;p.m_pMyVehicle=&other;Tick();Check(recenters==4,"different vehicle rearms");
 CPlayerPed replacement=p;replacement.m_pMyVehicle=&other;other.pDriver=&replacement;currentPlayer=&replacement;Tick();Check(recenters==5,"new player identity rearms");
 currentPlayer=nullptr;EvaluateSeat(true);Check(!gVehicleSeatRecenter.centered,"missing player clears pending state without dereference");
 currentPlayer=&replacement;Tick();Check(recenters==6,"player returning already seated centers once");
 gVehicleSeatRecenter.Reset();Tick();Check(recenters==7,"restart reset supports address reuse on load/new game");
}

static void TrackingTruthTable() {
 CPlayerPed p;CVehicle car;
 for(unsigned mask=0;mask<16;mask++)for(unsigned views=0;views<4;views++)for(int focused=0;focused<2;focused++)for(int success=0;success<2;success++){
  Reset(p,car);xrvk::g.sessionState=focused?xrvk::XR_SESSION_STATE_FOCUSED:0;
  xrvk::UpdateTracking(success?0:-1,views,{mask});
  const bool expected=mask==15&&views==2&&focused&&success;
  Check(xrvk::hasTrackedGameplayHeadPose()==expected,"exact native flags/view/focus/result predicate");
  Tick();Check((recenters==1)==expected,"invalid or stale XR position cannot be latched into seat");
  xrvk::g.sessionState=xrvk::XR_SESSION_STATE_FOCUSED;xrvk::UpdateTracking(0,2,{});Tick();
  Check(recenters==1,"first fully tracked frame consumes pending entry once");
 }
}

int main(){try{EntryAndMotion();SuppressionAndLifecycle();TrackingTruthTable();
 std::printf("PASS: %u production seat/XR/backend checks; old entry offset reproduced, centered seat and later lean verified.\n",checks);return 0;
 }catch(const std::exception&e){std::fprintf(stderr,"FAIL after %u checks: %s\n",checks,e.what());return 1;}}
