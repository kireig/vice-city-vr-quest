#define _CRT_SECURE_NO_WARNINGS
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <cstring>
#include <vector>
#define GTA_VR_WEAPONS 1
#define __ANDROID__ 1
#define nil nullptr
#define ASSERT(x) do { if(!(x)) std::abort(); } while(0)
using int16 = int16_t;
using uint32 = uint32_t;
template<class T> T Max(T a,T b) { return std::max(a,b); }
template<class T> T clamp(T x,T a,T b) { return std::max(a,std::min(x,b)); }
enum { GS_PLAYING_GAME=1, VR_HAND_COUNT=2, VR_DRIVING_DEFAULT=0, VR_DRIVING_IMMERSIVE=1, VR_DRIVING_MOTION=2,
    WEAPONSLOT_UNARMED=0, WEAPONSLOT_HANDGUN=2, WEAPONSLOT_SUBMACHINEGUN=5, TOTAL_WEAPON_SLOTS=10,
    WEAPONSTATE_READY=0, WEAPONSTATE_FIRING, WEAPONSTATE_RELOADING,
    STATUS_PLAYER=1, VEHICLE_APPEARANCE_HELI=7, MI_RCBARON=99,
    ASSOCGRP_STD=0, CAR_DOOR_LF, CAR_DOOR_RF, ANIM_STD_NUM, SOUND_WEAPON_SHOT_FIRED,
    ANIM_BIKE_DRIVEBY_RHS,ANIM_BIKE_DRIVEBY_LHS,ANIM_BIKE_DRIVEBY_FORWARD,PED_HANDR,
    EVENT_GUNSHOT,EVENT_ENTITY_PED,EVENT_ENTITY_VEHICLE,PARTICLE_GUNFLASH,PARTICLE_GUNFLASH_NOANIM };
enum eWeaponType { WEAPONTYPE_UNIDENTIFIED=-1, WEAPONTYPE_UNARMED, WEAPONTYPE_COLT45, WEAPONTYPE_PYTHON,
    WEAPONTYPE_TEC9, WEAPONTYPE_UZI, WEAPONTYPE_SILENCED_INGRAM, WEAPONTYPE_SHOTGUN };
enum AnimationId { ANIM_STD_CAR_DRIVEBY_RIGHT, ANIM_STD_CAR_DRIVEBY_LEFT,
    ANIM_STD_CAR_DRIVEBY_RIGHT_LO, ANIM_STD_CAR_DRIVEBY_LEFT_LO };
enum { LOOKING_FORWARD, LOOKING_BEHIND, LOOKING_LEFT, LOOKING_RIGHT };
struct CVector {
    float x=0,y=0,z=0; CVector()=default; CVector(float a,float b,float c):x(a),y(b),z(c){}
    CVector operator+(const CVector& b) const { return {x+b.x,y+b.y,z+b.z}; }
    CVector operator-(const CVector& b) const { return {x-b.x,y-b.y,z-b.z}; }
    CVector operator*(float b) const { return {x*b,y*b,z*b}; }
    CVector& operator+=(const CVector& b) { x+=b.x;y+=b.y;z+=b.z;return *this; }
    float Magnitude() const { return std::sqrt(x*x+y*y+z*z); }
};
CVector operator*(float a,const CVector& b) { return b*a; }
float DotProduct(const CVector& a,const CVector& b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
struct CMatrix { CVector right={1,0,0},forward={0,1,0},up={0,0,1},position;
    CVector operator*(const CVector& p) const { return position+right*p.x+forward*p.y+up*p.z; } };
struct CEntity {};
struct CColPoint { CVector point; };
struct CVehicleModelInfo { CVector GetFrontSeatPosn() { return {0.4f,0.1f,0.2f}; } };
struct CColModel { struct { CVector max={1,2,0.8f}; } boundingBox; };
struct CControllerState {
    int16 Square=0,Cross=0,Circle=0,Triangle=0,LeftShoulder1=0,RightShoulder1=0,
        LeftShoulder2=0,RightShoulder2=0,LeftShock=0,RightShock=0,RightStickX=0;
    void Clear() { *this=CControllerState(); }
};
struct CPad {
    CControllerState PCTempJoyState,NewState,OldState;
    int mode=0; bool disabled=false;
    static CPad* GetPad(int);
    int GetMode() { return mode; }
    bool ArePlayerControlsDisabled() { return disabled; }
    bool GetCarGunFired(); bool GetLookLeft(); bool GetLookRight(); bool GetLookBehindForCar();
};
CPad gPad;
CPad* CPad::GetPad(int) { return &gPad; }
#define CURMODE mode
struct CPlayerInfo { bool m_bDriveByAllowed=true; bool IsPlayerInRemoteMode() { return false; } };
struct CWeaponInfo {
    int m_nWeaponSlot=0,m_nModelId=0; uint32 m_nFiringRate=70,m_nReload=1250;
    float m_fRange=50; CVector m_vecFireOffset={0,0.3f,0};
    static CWeaponInfo* GetWeaponInfo(eWeaponType t) {
        static CWeaponInfo info;
        info.m_nWeaponSlot=t>=WEAPONTYPE_TEC9 && t<=WEAPONTYPE_SILENCED_INGRAM ? 5 :
            t==WEAPONTYPE_COLT45 || t==WEAPONTYPE_PYTHON ? 2 : 0;
        return &info;
    }
};
struct CVehicle;
struct CPed;
int gCarShots=0,gTrackedShots=0,gLastHand=-1;
bool gLastLeft=false,gActiveFire=false;
CVector gRaySource,gRayTarget;
struct CWeapon {
    eWeaponType m_eWeaponType=WEAPONTYPE_UNARMED;
    uint32 m_nTimer=0;
    int m_eWeaponState=WEAPONSTATE_READY,m_nAmmoInClip=30,m_nAmmoTotal=120;
    void Update(int,void*);
    void Reload() {}
    bool Fire(CPed*,CVector*) { if(m_nAmmoInClip<=0) return false; ASSERT(gActiveFire); --m_nAmmoInClip; ++gTrackedShots; return true; }
    bool FireFromCar(CVehicle*,bool,bool);
    bool FireInstantHitFromCar(CVehicle*,bool left,bool right);
    static bool ProcessLineOfSight(const CVector&,const CVector&,CColPoint&,CEntity*&,eWeaponType,CEntity*,bool,bool,bool,bool,bool,bool,bool);
    static void DoDriveByAutoAiming(CEntity*,CVehicle*,CVector*,CVector*) {}
    CWeaponInfo* GetInfo() { return CWeaponInfo::GetWeaponInfo(m_eWeaponType); }
};
struct CPed : CEntity {
    int m_audioEntityId=0,m_currentWeapon=0; eWeaponType m_storedWeapon=WEAPONTYPE_UNIDENTIFIED;
    CVehicle* m_pMyVehicle=nullptr;
    CWeapon weapons[TOTAL_WEAPON_SLOTS]; bool has[TOTAL_WEAPON_SLOTS]={};
    bool HasWeaponSlot(int slot) { return has[slot]; }
    CWeapon* GetWeapon() { return &weapons[m_currentWeapon]; }
    CWeapon& GetWeapon(int slot) { return weapons[slot]; }
    void SetCurrentWeapon(eWeaponType t) { m_currentWeapon=CWeaponInfo::GetWeaponInfo(t)->m_nWeaponSlot; }
    bool IsPlayer() { return true; }
    void RemoveWeaponModel(int) {}
    void* GetClump() { return this; }
    void RemoveWeaponWhenEnteringVehicle();
    void TransformToNode(CVector&,int);
};
struct CPlayerPed : CPed { CPlayerInfo info; bool dead=false; bool DyingOrDead() { return dead; }
    CPlayerInfo* GetPlayerInfoForThisPlayerPed() { return &info; } };
struct CVehicle : CEntity {
    CPed* pDriver=nullptr; CPed* pPassengers[1]={}; bool bLowVehicle=false;
    int kind=0,m_audioEntityId=0;
    CMatrix matrix; CVector m_vecMoveSpeed;
    CMatrix& GetMatrix() { return matrix; }
    CVector& GetForward() { return matrix.forward; } CVector& GetRight() { return matrix.right; }
    CVehicleModelInfo* GetModelInfo() { static CVehicleModelInfo info;return &info; }
    CColModel* GetColModel() { static CColModel model;return &model; }
    bool IsVehicle() { return true; }
    bool IsBike() { return kind==1; } bool IsBoat() { return kind==2; }
    bool IsCar() { return kind==0 || kind==3 || kind==4; }
    bool IsRealHeli() { return kind==3; } bool IsRealPlane() { return kind==4; }
    int GetVehicleAppearance() { return kind==3 ? VEHICLE_APPEARANCE_HELI : 0; }
    int GetModelIndex() { return 175; }
    int GetStatus() { return STATUS_PLAYER; }
};
struct CBike : CVehicle { int m_bikeAnimType=0;void DoDriveByShootings(); };
struct CAutomobile : CVehicle { float m_weaponDoorTimerLeft=0,m_weaponDoorTimerRight=0;
    void DoDriveByShootings(); void ProcessOpenDoor(int,int,float) {} };
CVehicle* gVehicle=nullptr;
CPlayerPed gPlayer;
CVehicle* FindPlayerVehicle() { return gVehicle; }
CPlayerPed* FindPlayerPed() { return &gPlayer; }
void CPed::TransformToNode(CVector& v,int) { v=m_pMyVehicle->GetMatrix()*(v+CVector(-0.4f,0.2f,0.7f)); }
struct CTimer { static uint32 now; static bool paused;
    static uint32 GetTimeInMilliseconds() { return now; } static float GetTimeStep() { return 1; }
    static bool GetIsPaused() { return paused; } };
uint32 CTimer::now=1000; bool CTimer::paused=false;
void CWeapon::Update(int,void*) { if(m_eWeaponState==WEAPONSTATE_RELOADING && CTimer::now>m_nTimer) { m_nAmmoInClip=30; m_eWeaponState=WEAPONSTATE_READY; } }
struct CStats { static float GetPercentageProgress() { return 0; } };
struct CGeneral { inline static int random=128; static int GetRandomNumber() { return random; } };
struct CEventList { static void RegisterEvent(int,int,CEntity*,CEntity*,int) {} };
struct CParticle { static void AddParticle(int,const CVector&,const CVector&,void* = nullptr,float=0) {} };
struct CPointLights { enum {LIGHT_POINT,FOG_NONE}; static void AddLight(int,const CVector&,const CVector&,float,float,float,float,int,bool) {} };
struct { void PlayOneShot(int,int,float) {} } DMAudio;
struct CCam {
    enum { MODE_TOPDOWN,MODE_1STPERSON,MODE_CAM_ON_A_STRING,MODE_BEHINDBOAT,MODE_BEHINDCAR };
    int Mode=MODE_1STPERSON,DirectionWasLooking=LOOKING_FORWARD;
    bool LookingBehind=false,LookingLeft=false,LookingRight=false;
    CVehicle* CamTargetEntity=nullptr; CVector Source,SourceBeforeLookBehind;
    void LookLeft() { LookingLeft=true; } void LookRight() { LookingRight=true; }
    void LookBehind() { LookingBehind=true; }
    void UpdateDirection();
};
struct { CCam Cams[1]; int ActiveCam=0; bool m_bObbeCinematicCarCamOn=false,m_bJust_Switched=false,m_WideScreenOn=false;
    bool GetLookingLRBFirstPerson() { return Cams[0].LookingLeft || Cams[0].LookingRight; } } TheCamera;
struct { bool m_bGameNotLoaded=false,m_bMenuActive=false,m_bWantToRestart=false,m_bWantToLoad=false; } FrontEndMenuManager;
struct CGame { static bool playingIntro; }; bool CGame::playingIntro=false;
struct CCutsceneMgr { static bool running; static bool IsRunning() { return running; } static bool IsCutsceneProcessing() { return running; } }; bool CCutsceneMgr::running=false;
struct CWorld { static CPlayerInfo Players[1]; static int PlayerInFocus; static bool bIncludeBikers; static CEntity* pIgnoreEntity; };
CPlayerInfo CWorld::Players[1]; int CWorld::PlayerInFocus=0; bool CWorld::bIncludeBikers=false;CEntity* CWorld::pIgnoreEntity=nullptr;
bool CWeapon::ProcessLineOfSight(const CVector& from,const CVector& to,CColPoint&,CEntity*& victim,eWeaponType,CEntity* shooter,bool,bool,bool,bool,bool,bool,bool) {
    ASSERT(CWorld::pIgnoreEntity==shooter && CWorld::bIncludeBikers);
    gRaySource=from;gRayTarget=to;++gCarShots;
    CVehicle* vehicle=(CVehicle*)shooter;gLastLeft=DotProduct(to-from,vehicle->GetRight())<0;
    victim=nullptr;return false;
}
int gGameState=GS_PLAYING_GAME; bool gVrFirstPersonActive=true;
struct CAnimBlendAssociation { float blendDelta=1; bool running=false; bool IsRunning() { return running; } } gAnim;
CAnimBlendAssociation* RpAnimBlendClumpGetAssociation(void*,int) { return &gAnim; }
struct CAnimManager { static CAnimBlendAssociation* AddAnimation(void*,int,int) { return &gAnim; } };
namespace androidgame {
struct PadInput { bool a=false,b=false,x=false,y=false,menu=false,leftStickClick=false,rightStickClick=false;
    float leftGrip=0,rightGrip=0,leftTrigger=0,rightTrigger=0,rightStickX=0,rightStickY=0; };
bool menu=false;
const PadInput& GetPadInput();
bool VrMenuConsumesInput() { return menu; }
int VrPadBinding(int); int VrPadBindingDefault(int);
void VrApplyPadBindings(CControllerState*,const PadInput&,bool);
}
androidgame::PadInput gPadInput;
namespace androidgame { const PadInput& GetPadInput() { return gPadInput; } }
void VrUpdateFirstPersonAnchor(bool) {}
enum { VR_MENU_PAGE_VEHICLE=1 };
int gVrVehicleSelection=0;
struct MenuRow { std::string text;int y,scale;bool selected; };
std::vector<MenuRow> menuRows;
void DrawFullVrMenuRow(const char*text,int y,int scale,bool selected,bool,bool) { menuRows.push_back({text,y,scale,selected}); }
std::map<std::string,int> driveSettings;
int driveSettingWrites=0;
int GetPrivateProfileIntA(const char*,const char*key,int fallback,const char*) { auto found=driveSettings.find(key);return found==driveSettings.end()?fallback:found->second; }
namespace OculusVR {
int gCarDrivingType=0,gBikeDrivingType=0,gBoatDrivingType=0;
CVehicle* gButtonDriveByVehicle=nullptr;int gButtonDriveByDirection=-1;
bool gVehicleForwardFireEnabled=false;
int menuKind=0;
const char*const kSettingsPath="fixture.ini";
void SaveSetting(const char*key,int value) { driveSettings[key]=value;++driveSettingWrites; }
bool thirdPerson=false,gImmersiveCarHornPressed=false,gVrRadioChangeJustPressed=false,gVrRadioButtonDown=false;
bool gHandsEnabled=true,gWeaponCalibrationPreview=false,gPoseValid[2]={true,true};
bool gTriggerPressed[2]={},gTriggerJustPressed[2]={},gTriggerJustReleased[2]={},gAttachedMissionWeaponForced=false;
float gTrigger[2]={}; int gHeldSlot[2]={-1,-1}; uint32 capture=0;
void LoadDrivingSettings() {} void LoadSettings() {}
bool IsQuestVehicleThirdPerson() { return thirdPerson; }
int GetQuestVehicleKind() { return menuKind; }
bool IsQuestVehicleKindThirdPerson() { return thirdPerson; }
bool IsQuestVehicleKindDrivingDefault() { return (menuKind==1 ? gBikeDrivingType:gCarDrivingType)==VR_DRIVING_DEFAULT; }
bool HasQuestDefaultVehicleViewOffsetTarget() { return thirdPerson || IsQuestVehicleKindDrivingDefault(); }
bool HasQuestVehicleSeatCalibrationTarget() { return true; }
int GetBikeModelIndex(int) { return 0; }
bool IsImmersiveDrivingActive(); bool IsVrCarDrivingActive(); bool IsImmersiveVehicleSidearm(int);
bool IsVrBikeDrivingActive();bool IsImmersiveBikeSidearm(int);
int GetQuestVehicleButtonFireDirection(CVehicle*);
void ApplyQuestVehicleButtonInput(CControllerState*,bool);
bool IsTrackedWeaponHeld(int hand) { return gHeldSlot[hand]>=0 && (!FindPlayerVehicle() || IsImmersiveDrivingActive()); }
bool IsTrackedWeaponTriggerPressed(int); bool IsTrackedWeaponTriggerJustPressed(int);
bool IsQuestDrivingHandUnavailable(int hand) { return gHeldSlot[hand]>=0; }
uint32 UpdateImmersiveBikeInput(const float*,const androidgame::PadInput&,uint32 blocked) { return capture & ~blocked; }
uint32 UpdateImmersiveCarInput(const float*,uint32) { return 0; }
void UpdateMotionInput(const androidgame::PadInput&,bool) {}
void UpdatePoses(const androidgame::PadInput& in) { gTrigger[0]=in.leftTrigger;gTrigger[1]=in.rightTrigger; }
bool IsTrackedDetonatorHandReserved(int) { return false; }
void UpdateTrackedDetonatorInput(bool) {} void UpdateManualReloadInput(bool) {}
int holsterHand=-1,holsterSlot=-1;
void UpdateHolsterInput(uint32) { if(holsterHand>=0) { gHeldSlot[holsterHand]=holsterSlot;holsterHand=-1; } }
void UpdateWeaponCalibrationSupportPreview() {}
void ResetInteraction() { gHeldSlot[0]=gHeldSlot[1]=-1; }
void UpdateMelee() {} void UpdateScope() {}
bool IsSupportHand(int) { return false; } bool IsAuxiliaryHandReserved(int) { return false; }
int GetHeldWeaponSlot(int hand) { return gHeldSlot[hand]; }
bool GetTrackedWeaponAim(int hand,int,CVector* source,CVector* direction) {
    if(!gPoseValid[hand]) return false;
    source->x=float(hand+1); direction->y=1; return true;
}
void BeginTrackedWeaponFire(int hand,int,const CVector& source,const CVector&) { ASSERT(source.x==float(hand+1));gLastHand=hand;gActiveFire=true; }
void EndTrackedWeaponFire() { gActiveFire=false; }
}
#include "vehicle-driveby-production.inc"
namespace androidgame {
int VrPadBindingDefault(int source) { return kVrPadBindingDefault[source]; }
int VrPadBinding(int source) { return kVrPadBindingDefault[source]; }
}
int checks=0,failures=0;
void Check(bool ok,const char* what) { ++checks; if(!ok) { ++failures;std::fprintf(stderr,"FAIL %d: %s\n",checks,what); } }
CAutomobile car;
void Reset(int mode=0,int driving=0,bool third=false) {
    using namespace OculusVR;
    gPad=CPad();gPad.mode=mode;gPadInput={};gPlayer=CPlayerPed();car=CAutomobile();
    car.pDriver=&gPlayer;gVehicle=&car;gPlayer.m_pMyVehicle=&car;
    gPlayer.has[5]=true;gPlayer.weapons[5].m_eWeaponType=WEAPONTYPE_UZI;
    gPlayer.has[2]=true;gPlayer.weapons[2].m_eWeaponType=WEAPONTYPE_COLT45;
    gPlayer.RemoveWeaponWhenEnteringVehicle();
    TheCamera.Cams[0]=CCam();TheCamera.Cams[0].CamTargetEntity=&car;
    TheCamera.Cams[0].Mode=third ? CCam::MODE_CAM_ON_A_STRING : CCam::MODE_1STPERSON;
    gCarDrivingType=gBikeDrivingType=driving;thirdPerson=third;
    gHeldSlot[0]=gHeldSlot[1]=-1;gPoseValid[0]=gPoseValid[1]=true;
    for(int h=0;h<2;h++)gTriggerPressed[h]=gTriggerJustPressed[h]=gTriggerJustReleased[h]=false;
    capture=0;gHandsEnabled=true;gAnim=CAnimBlendAssociation();androidgame::menu=false;
    FrontEndMenuManager.m_bMenuActive=false;CTimer::paused=false;CCutsceneMgr::running=false;
    TheCamera.m_WideScreenOn=false;gVrFirstPersonActive=true;CTimer::now=1000;
    gCarShots=gTrackedShots=0;gLastHand=-1;gLastLeft=false;gRaySource={};gRayTarget={};
    holsterHand=-1;holsterSlot=-1;
    gVehicleForwardFireEnabled=false;
    gButtonDriveByVehicle=nullptr;gButtonDriveByDirection=-1;CGeneral::random=128;
}
void Frame(bool draw=true) {
    CaptureButtons(0);
    OculusVR::ApplyTouchInput(&gPad.PCTempJoyState);
    gPad.OldState=gPad.NewState;gPad.NewState=gPad.PCTempJoyState;
    TheCamera.Cams[0].UpdateDirection();
    if(draw){ if(gVehicle->IsBike())((CBike*)gVehicle)->DoDriveByShootings();else car.DoDriveByShootings(); }
    CTimer::now+=100;
}
int main() {
    using namespace OculusVR;
    driveSettings.clear();driveSettingWrites=0;gVehicleForwardFireEnabled=true;LoadForwardFireSetting();
    Check(!IsQuestVehicleForwardFireEnabled() && driveSettingWrites==0,"missing forward-fire key loads OFF without writing preferences");
    for(int saved:{0,1}) {
        driveSettings["DefaultVehicleForwardFire"]=saved;LoadForwardFireSetting();
        Check(IsQuestVehicleForwardFireEnabled()==(saved!=0) && driveSettingWrites==0,"explicit saved forward-fire flag survives load without writes");
    }
    SetQuestVehicleForwardFireEnabled(false);Check(!IsQuestVehicleForwardFireEnabled() && driveSettings["DefaultVehicleForwardFire"]==0 && driveSettingWrites==1,"setter applies and persists OFF immediately");
    SetQuestVehicleForwardFireEnabled(true);Check(IsQuestVehicleForwardFireEnabled() && driveSettings["DefaultVehicleForwardFire"]==1 && driveSettingWrites==2,"setter applies and persists ON immediately");
#ifdef HAS_FORWARD_FIRE_MENU
    for(int kind=0;kind<3;kind++)for(int driving=0;driving<3;driving++)for(int third=0;third<2;third++) {
        Reset(0,driving,third!=0);menuKind=kind;gVrVehicleSelection=VR_VEHICLE_FORWARD_FIRE;
        const bool visible=kind!=2 && (driving==0 || third!=0);
        Check(IsMenuItemVisible(VR_MENU_PAGE_VEHICLE,VR_VEHICLE_FORWARD_FIRE)==visible,"forward option appears only for effective DEFAULT car/bike settings");
        Check(!ForwardValueRepeats(),"forward boolean remains a discrete menu action");
        menuRows.clear();DrawForwardMenu();int found=0;
        for(size_t row=0;row<menuRows.size();++row) {
            const MenuRow& current=menuRows[row];
            if(current.text.find("DEFAULT FORWARD FIRE (B)")!=std::string::npos) { ++found;Check(current.text.find("< OFF >")!=std::string::npos,"new row presents actual OFF state and B binding"); }
            Check(current.y+current.scale*7+4<694,"vehicle row and highlight stay above footer");
            if(row)Check(menuRows[row-1].y+menuRows[row-1].scale*7+9<=current.y,"vehicle rows remain separate");
        }
        Check(found==(visible?1:0)&&menuRows.back().text=="BACK TO SETTINGS","visible forward row is unique and Back remains last");
        if(visible) {
            const int writes=driveSettingWrites;DispatchForward(false,false);
            Check(driveSettingWrites==writes,"no activation pulse does not toggle or save");
            DispatchForward(true,false);
            Check(IsQuestVehicleForwardFireEnabled()&&driveSettings["DefaultVehicleForwardFire"]==1&&driveSettingWrites==writes+1,"actual positive menu dispatch applies and saves, including third person");
            DispatchForward(false,true);
            Check(!IsQuestVehicleForwardFireEnabled()&&driveSettings["DefaultVehicleForwardFire"]==0&&driveSettingWrites==writes+2,"actual negative menu dispatch switches OFF and saves once");
        }
    }
#else
    Check(false,"baseline has no forward-fire option in vehicle settings");
#endif
    for(int mode=0;mode<4;mode++)for(int third=0;third<2;third++)for(int side=0;side<3;side++) {
        Reset(mode,VR_DRIVING_DEFAULT,third!=0);gPadInput.b=true;
        gPadInput.leftGrip=side==1 ? 0.2f : 1.0f;gPadInput.rightGrip=side==0 ? 0.2f : 1.0f;
        Frame();
        Check(gPad.GetCarGunFired(),"grip+B reaches native fire button in every pad mode");
        Check(gCarShots==1,"single/both grips+B actually reaches FireFromCar");
        Check(gLastLeft==(side!=1),"both grips choose driver-side window; single right chooses right");
        Check(!gPad.GetLookBehindForCar(),"drive-by never becomes look-behind");
        Check(gPlayer.weapons[5].m_nAmmoInClip==29,"native drive-by consumes ammo");
        Check(gPad.NewState.LeftShoulder1==0 && (mode==3 || gPad.NewState.RightShoulder1==0),"grips do not leak horn/handbrake");
    }
    for(int driving=VR_DRIVING_IMMERSIVE;driving<=VR_DRIVING_MOTION;driving++)for(int mode=0;mode<4;mode++) {
        Reset(mode,driving,true);gPadInput.b=true;gPadInput.rightGrip=1;Frame();
        Check(gCarShots==1 && !gLastLeft,"third-person physical-mode setting falls back to classic drive-by");
        Check(gTrackedShots==0,"third-person fires only native gun");
    }
    for(int blocked=0;blocked<5;blocked++) {
        Reset();gPadInput.b=true;gPadInput.leftGrip=gPadInput.rightGrip=1;
        if(blocked==0)androidgame::menu=true;
        if(blocked==1)FrontEndMenuManager.m_bMenuActive=true;
        if(blocked==2)CTimer::paused=true;
        if(blocked==3)CCutsceneMgr::running=true;
        if(blocked==4)gPad.disabled=true;
        Frame();Check(gCarShots==0 && gTrackedShots==0,"menu/disabled gameplay prevents shooting");
    }
    Reset();gPadInput.b=true;Frame();Check(gCarShots==0 && !gPad.GetCarGunFired(),"car B without grip cannot fire with default OFF");
    SetQuestVehicleForwardFireEnabled(true);Frame();Check(gCarShots==1 && DotProduct(gRayTarget-gRaySource,car.GetForward())>49.9f,"opt-in car B without grip fires forward immediately");
    SetQuestVehicleForwardFireEnabled(false);Frame();Check(gCarShots==1 && !gPad.GetCarGunFired(),"turning option OFF cancels a held B command immediately");
    Reset();gPadInput.leftGrip=1;Frame();Check(gCarShots==0,"grip without B does not shoot");
    Reset();gPadInput.b=true;gPadInput.leftGrip=1;gPlayer.info.m_bDriveByAllowed=false;Frame();Check(gCarShots==0,"mission drive-by prohibition retained");
    Reset();gPadInput.b=true;gPadInput.leftGrip=1;gPlayer.m_currentWeapon=2;Frame();Check(gCarShots==0,"native drive-by still requires SMG");
    Reset();gPadInput.b=true;gPadInput.leftGrip=1;gPlayer.weapons[5].m_nAmmoInClip=0;Frame();Check(gCarShots==0,"empty native weapon does not shoot");
    Reset();gPadInput.b=true;gPadInput.leftGrip=1;gAnim.running=true;Frame();Check(gCarShots==0,"native raise-gun animation remains authoritative");gAnim.running=false;Frame();Check(gCarShots==1,"fire starts after native animation finishes");
    for(int driving=1;driving<=2;driving++)for(int hand=0;hand<2;hand++)for(int grips=0;grips<4;grips++) {
        Reset(0,driving);gHeldSlot[hand]=2;capture=3;
        gPadInput.leftGrip=(grips&1)?1.0f:0.0f;gPadInput.rightGrip=(grips&2)?1.0f:0.0f;gPadInput.b=true;
        Frame();Check(gTrackedShots==1 && gLastHand==hand,"physical sidearm B uses correct held-hand pose independent of steering grips");
        Check(gCarShots==0 && gPad.NewState.Circle==0,"physical fire consumes B once");
        Frame();Check(gTrackedShots==1,"semiautomatic weapon fires once per B press");
        gPadInput.b=false;Frame();gPadInput.b=true;Frame();Check(gTrackedShots==2,"release/repress rearms physical sidearm");
    }
    Reset(0,1);gHeldSlot[1]=5;gPadInput.b=true;Frame();Frame();Check(gTrackedShots==2,"held automatic sidearm repeats B at cooldown");
    Reset(0,1);gHeldSlot[1]=2;gPoseValid[1]=false;gPadInput.b=true;Frame();Check(gTrackedShots==0,"invalid held-hand tracking blocks physical shot");
    Reset(0,1);gHeldSlot[1]=2;gPadInput.b=true;androidgame::menu=true;Frame();Check(gTrackedShots==0,"VR menu suppresses physical trigger");
    Reset(0,1);gPadInput.b=true;gPadInput.leftGrip=gPadInput.rightGrip=1;Frame();Check(gTrackedShots==0 && gCarShots==1,"physical mode without held gun uses existing native SMG");
    Reset(0,1);gHeldSlot[1]=2;gPadInput.rightTrigger=1;Frame();Check(gTrackedShots==0,"accelerator does not fire physical gun");
    for(int hand=0;hand<2;hand++)for(int driving=1;driving<=2;driving++) {
        Reset(0,driving);gPadInput.rightTrigger=1;gPadInput.leftTrigger=1;Frame();
        holsterHand=hand;holsterSlot=2;Frame();
        Check(gTrackedShots==0 && !gTriggerPressed[hand],"drawing while driving does not inherit accelerator or brake trigger");
        gPadInput.b=true;Frame();
        Check(gTrackedShots==1 && gLastHand==hand && gCarShots==0,"B after drawing starts a fresh tracked trigger edge");
        Reset(0,driving);holsterHand=hand;holsterSlot=2;gPadInput.b=true;
        gPadInput.leftGrip=gPadInput.rightGrip=1;Frame();
        Check(gTrackedShots==1 && gLastHand==hand && gCarShots==0 && gPad.NewState.Circle==0,
            "same-frame grip+B draw transfers fire ownership to the new held gun");
        holsterHand=hand;holsterSlot=-1;Frame();
        Check(gTrackedShots==1 && gCarShots==1,"same-frame holster release restores native B fallback without a stale held shot");
    }
    Reset();gVehicle=nullptr;gHeldSlot[1]=2;gPadInput.rightTrigger=.56f;Frame(false);
    Check(gTriggerPressed[1] && gTriggerJustPressed[1],"on-foot trigger still begins at upper threshold");
    gPadInput.rightTrigger=.50f;Frame(false);Check(gTriggerPressed[1] && !gTriggerJustPressed[1],"on-foot trigger retains hysteresis");
    gPadInput.rightTrigger=.40f;Frame(false);Check(!gTriggerPressed[1] && gTriggerJustReleased[1],"on-foot trigger releases below lower threshold");
    gPadInput.rightTrigger=.50f;Frame(false);Check(!gTriggerPressed[1],"on-foot trigger remains released in dead band");
    for(int hands=0;hands<2;hands++) {
        Reset();gHandsEnabled=hands!=0;gPadInput.b=true;gPadInput.leftGrip=gPadInput.rightGrip=1;Frame();Check(gCarShots==1,"hidden hands do not disable default drive-by");
    }
    CBike bike;Reset();bike.kind=1;bike.pDriver=&gPlayer;gVehicle=&bike;gPadInput.b=true;Frame(false);Check(!gPad.GetCarGunFired(),"default bike also requires forward-fire opt-in");
    Reset();gVehicle=nullptr;gPadInput.b=true;gPadInput.leftGrip=gPadInput.rightGrip=1;Frame(false);Check(gPad.NewState.LeftShoulder2==0 && gPad.NewState.RightShoulder2==0,"on-foot mapping unaffected");
    for(int kind=0;kind<2;kind++)for(int driving=0;driving<3;driving++)for(int third=0;third<2;third++)
    for(int mode=0;mode<4;mode++)for(int direction=0;direction<4;direction++)for(int angle=0;angle<3;angle++) {
        Reset(mode,driving,third!=0);
        if(kind){bike=CBike();bike.kind=1;bike.pDriver=&gPlayer;gVehicle=&bike;gPlayer.m_pMyVehicle=&bike;TheCamera.Cams[0].CamTargetEntity=&bike;}
        const float yaw=1.57079632679f*angle,pitch=0.2f;
        gVehicle->matrix.right={std::cos(yaw),std::sin(yaw),0};
        gVehicle->matrix.forward={-std::sin(yaw)*std::cos(pitch),std::cos(yaw)*std::cos(pitch),std::sin(pitch)};
        gVehicle->matrix.up={std::sin(yaw)*std::sin(pitch),-std::cos(yaw)*std::sin(pitch),std::cos(pitch)};
        gVehicle->matrix.position={17,-11,4};gVehicle->m_vecMoveSpeed={0.2f,0.1f,0.05f};
        gPadInput.b=true;gPadInput.leftGrip=direction==1||direction==3 ? 1.0f:0.0f;
        gPadInput.rightGrip=direction==2||direction==3 ? 1.0f:0.0f;
        capture=driving ? 3u:0u;
        gVehicleForwardFireEnabled=true;
        if(direction==0 && driving!=0 && !third) {
            Frame();Check(gCarShots==0 && !gPad.GetCarGunFired(),"opt-in never enables empty-hand B forward fire in physical control modes");
            continue;
        }
        Frame();Check(gCarShots==1,"all vehicle views/control types send a real projectile to LOS");
        CVector expected=direction==0 ? gVehicle->GetForward() : gVehicle->GetRight()*(direction==2 ? 1.0f:-1.0f);
        CVector ray=gRayTarget-gRaySource;
        Check(ray.Magnitude()>49.9f && DotProduct(ray,expected)>49.9f,"world-space projectile follows forward/left/right on rotated tilted vehicle");
        Check(!CWorld::bIncludeBikers && CWorld::pIgnoreEntity==nullptr,"LOS temporary self-exclusion restored");
        const uint32 next= gPlayer.weapons[5].m_nTimer;
        CTimer::now=next;Frame();Check(gCarShots==1,"cooldown rejects exact deadline as native code requires");
        CTimer::now=next+1;Frame();Check(gCarShots==2 && gPlayer.weapons[5].m_nAmmoInClip==28,"held B repeats after cooldown and consumes exactly one round");
        gPadInput.b=false;Frame();Check(gCarShots==2,"releasing B clears cached fire command");
    }
    Reset(3,1);gPadInput.b=true;gPadInput.a=true;Frame();Check(gPad.NewState.LeftShoulder1==255,"explicit A handbrake survives button-shot cleanup");
    for(int kind=0;kind<2;kind++)for(int direction=0;direction<3;direction++) {
        Reset();
        gVehicleForwardFireEnabled=true;
        if(kind){bike=CBike();bike.kind=1;bike.pDriver=&gPlayer;gVehicle=&bike;gPlayer.m_pMyVehicle=&bike;TheCamera.Cams[0].CamTargetEntity=&bike;}
        gPadInput.b=true;gPadInput.leftGrip=direction==1?1.0f:0.0f;gPadInput.rightGrip=direction==2?1.0f:0.0f;
        gPlayer.weapons[5].m_nAmmoInClip=1;const uint32 start=CTimer::now;
        Frame();CWeapon& gun=gPlayer.weapons[5];
        Check(gCarShots==1 && gun.m_nAmmoInClip==0 && gun.m_eWeaponState==WEAPONSTATE_RELOADING,
            "last button-shot round enters the actual native FireFromCar reload branch");
        Check(gun.m_nTimer==start+gun.GetInfo()->m_nReload,"button-shot caller preserves the full native reload deadline");
        Frame();Check(gCarShots==1 && gun.m_nAmmoInClip==0,"held B cannot bypass an unfinished reload");
        CTimer::now=start+gun.GetInfo()->m_nReload+1;Frame();
        Check(gCarShots==2 && gun.m_nAmmoInClip==29,"button shooting resumes after the engine completes reload");
    }
    Reset();car.kind=3;gPadInput.b=true;Frame(false);Check(GetQuestVehicleButtonFireDirection(&car)<0,"helicopter weapon bindings untouched");
    Reset();CVehicle other;gPadInput.b=true;Frame();Check(GetQuestVehicleButtonFireDirection(&other)<0,"cached shot belongs only to actual vehicle");
    for(int kind=0;kind<2;kind++)for(int driving=0;driving<3;driving++)for(int third=0;third<2;third++)for(int mode=0;mode<4;mode++) {
        Reset(mode,driving,third!=0);
        if(kind){bike=CBike();bike.kind=1;bike.pDriver=&gPlayer;gVehicle=&bike;gPlayer.m_pMyVehicle=&bike;TheCamera.Cams[0].CamTargetEntity=&bike;}
        gPadInput.b=true;Frame();Check(gCarShots==0 && !gPad.GetCarGunFired(),"OFF suppresses B-only native firing for both vehicle kinds, all views, modes and pad layouts");
        gPadInput.b=false;gPadInput.rightGrip=1;Frame();Check(gCarShots==0,"right grip without B never fires, including pad mode3 R1 binding");
    }
    if(failures) { std::printf("FAIL: %d of %d production-path checks.\n",failures,checks);return 1; }
    std::printf("PASS: %d checks using actual Quest input, trigger, native camera dispatch, car firing and ammo paths.\n",checks);
}
