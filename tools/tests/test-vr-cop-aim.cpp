#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#define nil nullptr
#define sq(x) ((x)*(x))
#define Abs(x) std::fabs(x)
#define Sin(x) std::sin(x)
#define Cos(x) std::cos(x)
#define GTA_VR_WEAPONS
#define RW_VULKAN
#define __ANDROID__
#define FIX_BUGS
using uint8=uint8_t; using uint32=uint32_t;
static const float TWOPI=6.2831853f;
struct CVector {
 float x=0,y=0,z=0;
 CVector()=default; CVector(float a,float b,float c):x(a),y(b),z(c){}
 CVector operator-(const CVector&v)const{return CVector(x-v.x,y-v.y,z-v.z);}
 CVector operator*(float f)const{return CVector(x*f,y*f,z*f);}
 float MagnitudeSqr2D()const{return x*x+y*y;} float MagnitudeSqr()const{return x*x+y*y+z*z;}
 void Normalise(){const float n=std::sqrt(x*x+y*y+z*z);if(n>0){x/=n;y/=n;z/=n;}}
};
static CVector operator*(float f,const CVector&v){return v*f;}
static float DotProduct(const CVector&a,const CVector&b){return a.x*b.x+a.y*b.y+a.z*b.z;}
struct CVector2D {
 float x,y; explicit CVector2D(const CVector&v):x(v.x),y(v.y){}
 CVector2D(float a,float b):x(a),y(b){}
 CVector2D operator-(const CVector2D&v)const{return CVector2D(x-v.x,y-v.y);}
 void Normalise(){const float n=std::sqrt(x*x+y*y);if(n>0){x/=n;y/=n;}}
};
static float DotProduct2D(const CVector2D&a,const CVector2D&b){return a.x*b.x+a.y*b.y;}
enum { RANDOM_CHAR, MISSION_CHAR, PEDTYPE_PLAYER, PEDTYPE_COP, PEDTYPE_CIV, PEDTYPE_GANG7, PEDTYPE_EMERGENCY, PEDTYPE_FIREMAN,
 PED_ATTACK, PED_FLEE_ENTITY, PED_AIM_GUN, WAITSTATE_PLAYANIM_HANDSUP, WAITSTATE_PLAYANIM_HANDSCOWER,
 OBJECTIVE_KILL_CHAR_ON_FOOT, OBJECTIVE_ENTER_CAR_AS_DRIVER, OBJECTIVE_ENTER_CAR_AS_PASSENGER, PEDMOVE_NONE,
 SOUND_PED_HANDS_COWER, SOUND_PED_HANDS_UP, MI_MONEY, PICKUP_MONEY, STAT_GUN_PANIC=1<<10 };
enum { WEAPONTYPE_UNARMED, WEAPONTYPE_GUN, WEAPONTYPE_MELEE, WEAPONTYPE_CAMERA, TOTAL_WEAPON_SLOTS=10 };
struct CEntity { CVector pos; void RegisterReference(CEntity**){} CVector GetPosition()const{return pos;} };
struct CWeapon { int m_eWeaponType=WEAPONTYPE_GUN; bool IsTypeMelee()const{return m_eWeaponType==WEAPONTYPE_MELEE;} };
struct Stats { int m_flags=0; };
struct CWanted {
 int level=0,calls=0; bool m_bIgnoredByEveryone=false,m_bIgnoredByCops=false;
 int GetWantedLevel()const{return level;}
 void SetWantedLevelNoDrop(int n){++calls;if(n>level)level=n;}
};
struct CVehicle:CEntity { bool alarm=false,car=true,bIsLawEnforcer=false; bool IsCar()const{return car;} bool IsAlarmOn()const{return alarm;} };
struct CPed:CEntity {
 bool playerFlag=false,control=true,dead=false,visible=true,bCrouchWhenScared=false,bCrouchWhenShooting=false,bKindaStayInSamePlace=false,bRichFromMugging=false;
 int CharCreatedBy=RANDOM_CHAR,m_nWaitState=-1,m_nPedState=-1,m_nPedType=PEDTYPE_COP,m_objective=-1,losCalls=0,duckCalls=0;
 CVector forward=CVector(0,-1,0); CPed*m_leader=nil; CEntity*m_pLookTarget=nil,*m_fleeFrom=nil; Stats stats,*m_pedStats=&stats; CWeapon weapon;
 bool IsPlayer()const{return playerFlag;} bool IsPedInControl()const{return control;} bool DyingOrDead()const{return dead;}
 CVector GetForward()const{return forward;} CWeapon*GetWeapon(){return &weapon;}
 bool OurPedCanSeeThisOne(CPed*,bool objects){++losCalls;return visible&&objects;}
 void ClearLeader(){m_leader=nil;} bool IsGangMember()const{return m_nPedType==PEDTYPE_GANG7;}
 void RegisterThreatWithGangPeds(CPed*){} void SetWaitState(int state,int*){m_nWaitState=state;}
 void Say(int){} void SetMoveState(int){} void SetDuck(int){++duckCalls;}
 CPed*CheckForAimedGun(); void ReactToPointGun(CEntity*);
};
struct CPlayerPed:CPed {
 CWanted wanted,*m_pWanted=&wanted; CVehicle*m_pMyVehicle=nil; CWeapon inventory[TOTAL_WEAPON_SLOTS];
 CPlayerPed(){playerFlag=true;m_nPedType=PEDTYPE_PLAYER;pos=CVector(0,0,1);}
 CWeapon&GetWeapon(int slot){return inventory[slot];}
 void SetWantedLevelNoDrop(int n){m_pWanted->SetWantedLevelNoDrop(n);}
};
struct CCopPed:CPed {
 bool m_bIsDisabledCop=false,m_bZoneDisabled=false,m_bIsInPursuit=false; unsigned m_randomSeed=0; CVehicle*m_pMyVehicle=nil;
 void ScanForCrimes();
};
static CPlayerPed *gPlayer=nil; static CVehicle*gVehicle=nil; static unsigned gFrame=1000;
static CPlayerPed*FindPlayerPed(){return gPlayer;} static CVehicle*FindPlayerVehicle(){return gVehicle;}
namespace CTimer { static uint32 GetFrameCounter(){return gFrame;} }
namespace CCullZones { static bool noPolice=false; [[maybe_unused]] static bool NoPolice(){return noPolice;} }
namespace CGeneral { static int GetRandomNumberInRange(int low,int){return low;} static int GetRandomNumber(){return 0;} }
namespace CWorld { static float FindGroundZFor3DCoord(float,float,float z,bool *found){*found=true;return z;} }
namespace CPickups { static void GenerateNewOne(CVector,int,int,int){} }
namespace OculusVR {
 static int heldSlots[2]={1,-1},aimCalls=0; static bool tracking=true;
 static CVector mockSource(0,0,1),mockDirection(0,3,0);
 static bool IsTrackedWeaponHeld(int hand){return heldSlots[hand]>=0;}
 static int GetHeldWeaponSlot(int hand){return heldSlots[hand];}
 static bool IsPhysicalGunType(int type){return type==WEAPONTYPE_GUN||type==WEAPONTYPE_CAMERA;}
 static bool GetTrackedWeaponAim(int,int,CVector*s,CVector*d){++aimCalls;*s=mockSource;*d=mockDirection;return tracking;}
 bool GetHeldTrackedWeaponAim(CVector*,CVector*);
}
#include "vr-cop-aim-production.inc"
static unsigned checks;
static void Check(bool value,const char *message){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static void Reset(CPlayerPed &player,CCopPed &cop){
 player=CPlayerPed();player.m_pWanted=&player.wanted;player.m_pedStats=&player.stats;
 cop=CCopPed();cop.m_pedStats=&cop.stats;cop.pos=CVector(0,5,1);
 gPlayer=&player;gVehicle=nil;gFrame=(gFrame+4)&~3u;CCullZones::noPolice=false;
 OculusVR::heldSlots[0]=1;OculusVR::heldSlots[1]=-1;OculusVR::tracking=true;OculusVR::aimCalls=0;
 OculusVR::mockSource=CVector(0,0,1);OculusVR::mockDirection=CVector(0,3,0);
}
int main(){
 CPlayerPed player;CCopPed cop;
 Reset(player,cop);cop.ScanForCrimes();Check(player.wanted.level==2&&player.wanted.calls==1,"tracked aim delivers native two-star police response without trigger or lock-on");
 Check(cop.losCalls==1,"visible target is checked against occlusion");
 cop.ScanForCrimes();Check(player.wanted.calls==1&&cop.losCalls==1,"already alerted cops do not repeat threat scans");
 Reset(player,cop);cop.visible=false;cop.ScanForCrimes();Check(player.wanted.level==0&&cop.losCalls==1,"police cannot react through walls");
 Reset(player,cop);cop.forward=CVector(0,1,0);cop.ScanForCrimes();Check(player.wanted.level==0&&cop.losCalls==0,"cop facing away does not see barrel at back");
 Reset(player,cop);cop.pos.x=.6f;cop.ScanForCrimes();Check(player.wanted.level==0&&cop.losCalls==0,"off-target barrel is not police threat");
 Reset(player,cop);cop.pos.z=1.9f;cop.ScanForCrimes();Check(player.wanted.level==0&&cop.losCalls==0,"barrel above or below body does not alert police");
 Reset(player,cop);cop.pos.y=15.1f;cop.ScanForCrimes();Check(player.wanted.level==0&&OculusVR::aimCalls==0,"native police threat range filters before ray/LOS work");
 Reset(player,cop);cop.pos.y=15.0f;cop.ScanForCrimes();Check(player.wanted.level==2,"native 15m boundary preserved");
 Reset(player,cop);cop.pos.y=-5;cop.ScanForCrimes();Check(player.wanted.level==0&&cop.losCalls==0,"cop behind muzzle is not aimed at");
 for(int kind=0;kind<6;kind++){
  Reset(player,cop);
  if(kind==0)OculusVR::heldSlots[0]=-1;
  if(kind==1)player.inventory[1].m_eWeaponType=WEAPONTYPE_CAMERA;
  if(kind==2)player.inventory[1].m_eWeaponType=WEAPONTYPE_MELEE;
  if(kind==3)OculusVR::tracking=false;
  if(kind==4)OculusVR::heldSlots[0]=TOTAL_WEAPON_SLOTS;
  if(kind==5)player.dead=true;
  cop.ScanForCrimes();Check(player.wanted.level==0&&cop.losCalls==0,"holster, non-gun, invalid hand pose/slot or dead player is not gun threat");
 }
 for(int guard=0;guard<7;guard++){
  Reset(player,cop);
  if(guard==0)player.wanted.m_bIgnoredByEveryone=true;
  if(guard==1)player.wanted.m_bIgnoredByCops=true;
  if(guard==2)CCullZones::noPolice=true;
  if(guard==3)cop.m_bIsDisabledCop=true;
  if(guard==4)cop.m_bZoneDisabled=true;
  if(guard==5)cop.control=false;
  if(guard==6)cop.CharCreatedBy=MISSION_CHAR;
  cop.ScanForCrimes();Check(player.wanted.level==0&&OculusVR::aimCalls==0,"ignored, disabled, driving/fallen or scripted cop retains native protection");
 }
 Reset(player,cop);cop.CharCreatedBy=MISSION_CHAR;cop.bCrouchWhenScared=true;cop.ScanForCrimes();Check(player.wanted.level==2,"native scripted-ped opt-in to threat response remains allowed");
 Reset(player,cop);cop.m_leader=&player;cop.ScanForCrimes();Check(player.wanted.level==0,"native player follower does not become hostile");
 Reset(player,cop);player.wanted.level=5;cop.ScanForCrimes();Check(player.wanted.level==5&&player.wanted.calls==0,"aim never reduces existing wanted level or resets its timer");
 Reset(player,cop);OculusVR::heldSlots[0]=-1;OculusVR::heldSlots[1]=1;cop.ScanForCrimes();Check(player.wanted.level==2,"left-hand tracked gun is detected");
 Reset(player,cop);cop.bCrouchWhenShooting=true;cop.ScanForCrimes();Check(player.wanted.level==2&&cop.duckCalls==1,"native cover response still runs");
 Reset(player,cop);cop.m_nPedType=PEDTYPE_CIV;cop.ReactToPointGun(&player);Check(cop.m_nWaitState==WAITSTATE_PLAYANIM_HANDSUP&&player.wanted.level==0,"native civilian reaction is unchanged");
 // The ray calculation is shared once per frame; each cop is staggered to
 // one of four frames. Occluded targets keep the wanted level at zero.
 Reset(player,cop);unsigned los=0,expectedLos=0,tracked=0;
 for(unsigned frame=0;frame<240;frame++){
  ++gFrame;OculusVR::aimCalls=0;
  for(unsigned seed=0;seed<12;seed++){
   cop.m_randomSeed=seed;cop.visible=false;cop.ScanForCrimes();
   if(((gFrame+seed)&3)==0)++expectedLos;
  }
  tracked+=unsigned(OculusVR::aimCalls);los=unsigned(cop.losCalls);
  Check(OculusVR::aimCalls==1,"all cop scans share held-ray calculation for the frame");
 }
 Check(los==expectedLos&&los==720,"12 cops perform exactly one staggered sight check each per four frames");
 Check(tracked==240,"world aim ray remains once per frame");
 Reset(player,cop);CVehicle vehicle;vehicle.alarm=true;vehicle.pos=CVector(0,1,1);gVehicle=&vehicle;OculusVR::heldSlots[0]=-1;cop.ScanForCrimes();Check(player.wanted.level==1,"existing car alarm crime still works");
 Reset(player,cop);vehicle.alarm=false;vehicle.bIsLawEnforcer=true;cop.m_objective=OBJECTIVE_ENTER_CAR_AS_DRIVER;cop.m_pMyVehicle=&vehicle;player.m_pMyVehicle=&vehicle;OculusVR::heldSlots[0]=-1;cop.ScanForCrimes();Check(player.wanted.level==1,"existing stolen cop car crime still works");
 std::printf("VR police aim production: %u checks passed; blocked 12-cop/240-frame case LOS=%u ray-builds=%u\n",checks,los,tracked);
}
