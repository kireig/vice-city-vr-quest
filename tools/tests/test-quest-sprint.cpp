#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "quest-sprint-weapon-types.inc"
#define GTA_VR_WEAPONS
#define PC_PLAYER_CONTROLS
#define DEGTORAD(a) ((a)*0.017453292519943295f)
static constexpr float PI=3.14159265358979323846f;
#include "quest-sprint-anims.inc"
static int checks;
static void Require(bool v,const char *label){++checks;if(!v){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
enum { WEAPONFLAG_HEAVY=1,WEAPONFLAG_CANAIM_WITHARM=2,PED_IDLE=0,PED_ATTACK=1 };
struct CWeaponInfo {
    int flags=0;
    bool IsFlagSet(int f)const{return (flags&f)!=0;}
    static CWeaponInfo *GetWeaponInfo(eWeaponType);
};
static CWeaponInfo infos[WEAPONTYPE_TOTALWEAPONS];
CWeaponInfo *CWeaponInfo::GetWeaponInfo(eWeaponType t){return &infos[t];}
struct CWeapon {
    eWeaponType m_eWeaponType=WEAPONTYPE_UNARMED;
    bool twoHanded=false;
    bool IsType2Handed()const{return twoHanded;}
};
struct CPad {
    int horizontal=0,vertical=0;bool sprint=false;
    int GetPedWalkLeftRight()const{return horizontal;}
    int GetPedWalkUpDown()const{return vertical;}
    bool GetSprint()const{return sprint;}
};
static bool cheat=false,physical=true;
static int held[2]={-1,-1};
namespace OculusVR {
static bool IsRunWithoutLimitsEnabled(){return cheat;}
static bool IsPhysicalWeaponInteractionActive(){return physical;}
static int GetHeldWeaponSlot(int h){return held[h];}
}
struct CameraMode{bool mouse=true;bool Using3rdPersonMouseCam()const{return mouse;}};
static struct Camera{CameraMode Cams[1];int ActiveCam=0;} TheCamera;
static void *RpAnimBlendClumpGetAssociation(void*,int){return nullptr;}
struct CPlayerPed {
    CWeapon weapons[TOTAL_WEAPON_SLOTS];int current=WEAPONSLOT_MELEE,rebuilt=0;
    int m_nPedState=PED_IDLE;void *m_pPointGunAt=nullptr;bool bIsDucking=false;
    float m_fWalkAngle=0;bool canStrafe=true;AssocGroupId m_animGroup=ASSOCGRP_PLAYER;
    CWeapon *GetWeapon(){return &weapons[current];}
    CWeapon &GetWeapon(int slot){return weapons[slot];}
    bool HasWeaponSlot(int slot)const{return slot>=0&&slot<TOTAL_WEAPON_SLOTS;}
    void ReApplyMoveAnims(){++rebuilt;}
    bool CanStrafeOrMouseControl()const{return canStrafe;}
    void *GetClump(){return nullptr;}
    int GetFireAnimGround(CWeaponInfo*,bool){return 0;}
    int GetMeleeStartAnim(CWeaponInfo*){return 0;}
    void ProcessAnimGroups();float DoWeaponSmoothSpray();bool MovementDisabledBecauseOfTargeting();
};
#include "quest-sprint-production.inc"
static const char *SprintName(AssocGroupId group){
    switch(group){
    case ASSOCGRP_PLAYER:return aPlayerAnimations[2];
    case ASSOCGRP_PLAYERROCKET:return aPlayerWithRocketAnimations[2];
    case ASSOCGRP_PLAYER1ARMED:return aPlayer1ArmedAnimations[2];
    case ASSOCGRP_PLAYER2ARMED:return aPlayer2ArmedAnimations[2];
    case ASSOCGRP_PLAYERBBBAT:return aPlayerBBBatAnimations[2];
    case ASSOCGRP_PLAYERCHAINSAW:return aPlayerChainsawAnimations[2];
    case ASSOCGRP_PLAYERBACK:return aPlayerStrafeBackAnimations[2];
    case ASSOCGRP_PLAYERLEFT:return aPlayerStrafeLeftAnimations[2];
    case ASSOCGRP_PLAYERRIGHT:return aPlayerStrafeRightAnimations[2];
    default:return "weapon strafe";
    }
}
int main(){
    CPlayerPed player;CPad pad;pad.vertical=128;
    for(eWeaponType weapon:{WEAPONTYPE_SHOTGUN,WEAPONTYPE_ROCKETLAUNCHER,WEAPONTYPE_CHAINSAW,
        WEAPONTYPE_MINIGUN,WEAPONTYPE_BASEBALLBAT,WEAPONTYPE_KATANA,WEAPONTYPE_COLT45}){
        const bool restricted=weapon==WEAPONTYPE_ROCKETLAUNCHER||weapon==WEAPONTYPE_CHAINSAW||weapon==WEAPONTYPE_MINIGUN;
        infos[weapon].flags=restricted?WEAPONFLAG_HEAVY:0;
        player.GetWeapon()->m_eWeaponType=weapon;
        player.GetWeapon()->twoHanded=weapon==WEAPONTYPE_SHOTGUN;
        for(bool tracked:{false,true})
        for(int hand=0;hand<2;++hand){
            physical=tracked;
            held[0]=held[1]=-1;held[hand]=WEAPONSLOT_MELEE;
            for(int mode=0;mode<3;++mode)
            for(float angle:{0.0f,DEGTORAD(90.0f),DEGTORAD(-90.0f),DEGTORAD(180.0f)}){
                TheCamera.Cams[0].mouse=mode!=1;player.canStrafe=mode!=2;
                player.m_fWalkAngle=angle;cheat=true;player.m_animGroup=ASSOCGRP_PLAYERROCKET;
                player.m_nPedState=PED_ATTACK;player.m_pPointGunAt=&player;
                player.ProcessAnimGroups();
                const AssocGroupId expected=mode!=0||angle==0?ASSOCGRP_PLAYER:
                    angle==DEGTORAD(90.0f)?ASSOCGRP_PLAYERLEFT:
                    angle==DEGTORAD(-90.0f)?ASSOCGRP_PLAYERRIGHT:ASSOCGRP_PLAYERBACK;
                Require(player.m_animGroup==expected,"cheat preserves unarmed forward/left/right/back movement group");
                const char *motion=expected==ASSOCGRP_PLAYER?"SPRINT_civi":
                    expected==ASSOCGRP_PLAYERLEFT?"run_left":
                    expected==ASSOCGRP_PLAYERRIGHT?"run_right":"run_back";
                Require(std::strcmp(SprintName(player.m_animGroup),motion)==0,"actual root-motion mapping retains requested direction");
                Require(ShouldPlayerSprint(&player,&pad),"cheat requests sprint on movement without button");
                Require(!IsHeavyWeaponMobilityRestricted(&player),"cheat bypasses held heavy flag");
                Require(!player.MovementDisabledBecauseOfTargeting(),"cheat bypasses weapon aim movement clamp");
                Require(player.DoWeaponSmoothSpray()==0.0f,"cheat bypasses weapon attack steering and reverse clamp");
            }
            TheCamera.Cams[0].mouse=true;player.canStrafe=true;
            cheat=false;player.m_fWalkAngle=0;player.ProcessAnimGroups();
            Require(IsHeavyWeaponMobilityRestricted(&player)==restricted,"OFF restores held heavy restriction");
            pad.sprint=false;Require(!ShouldPlayerSprint(&player,&pad),"OFF requires sprint input");
            pad.sprint=true;Require(ShouldPlayerSprint(&player,&pad)==!restricted,"OFF preserves normal sprint gate");pad.sprint=false;
            Require(player.MovementDisabledBecauseOfTargeting(),"OFF restores two-arm targeting stop");
            if(weapon==WEAPONTYPE_SHOTGUN||weapon==WEAPONTYPE_ROCKETLAUNCHER||weapon==WEAPONTYPE_CHAINSAW||
               weapon==WEAPONTYPE_MINIGUN||weapon==WEAPONTYPE_BASEBALLBAT)
                Require(std::strcmp(SprintName(player.m_animGroup),"SPRINT_civi")!=0,"OFF restores weapon animation mapping");
        }
    }
    cheat=true;pad.vertical=0;Require(!ShouldPlayerSprint(&player,&pad),"stationary cheat does not request movement");
    Require(!ShouldPlayerSprint(&player,nullptr),"missing input is safe");
    cheat=false;physical=true;held[0]=held[1]=-1;player.m_fWalkAngle=0;player.ProcessAnimGroups();
    Require(player.m_animGroup==ASSOCGRP_PLAYER,"holstered inventory does not slow movement");
    std::printf("Quest production unlimited sprint: %d checks passed\n",checks);
}
