#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstdint>
#include "ragdoll-test-vector.h"
#include "quest-melee-weapon-types.inc"
using uint32 = uint32_t;
#define nil nullptr
#define DEGTORAD(a) ((a)*0.017453292519943295f)
template<class T> T Max(T a,T b) { return std::max(a,b); }
static int checks;
static void Require(bool v,const char *text) {
    ++checks; if(!v) { std::fprintf(stderr,"FAIL: %s\n",text); std::exit(1); }
}

// Engine-equivalent affine matrix operations; no rendering or game world is
// mocked as a passing hit. The production detector below must emit a real sweep.
struct CMatrix {
    CVector r{1,0,0}, f{0,1,0}, u{0,0,1}, p;
    CVector &GetRight(){return r;} CVector &GetForward(){return f;}
    CVector &GetUp(){return u;} CVector &GetPosition(){return p;}
    void SetUnity(){*this=CMatrix();}
    void SetRotate(float x,float y,float z){
        const float cX=cosf(x),sX=sinf(x),cY=cosf(y),sY=sinf(y),cZ=cosf(z),sZ=sinf(z);
        r={cZ*cY-sZ*sX*sY,cZ*sX*sY+sZ*cY,-cX*sY};
        f={-sZ*cX,cZ*cX,sX};
        u={sZ*sX*cY+cZ*sY,sZ*sY-cZ*sX*cY,cX*cY}; p={};
    }
    void SetRotateX(float a){SetRotate(a,0,0);}
    void SetRotateY(float a){SetRotate(0,a,0);}
    void SetRotateZ(float a){SetRotate(0,0,a);}
    CVector Rotate(const CVector &v)const{return r*v.x+f*v.y+u*v.z;}
    CVector operator*(const CVector &v)const{return Rotate(v)+p;}
    CMatrix operator*(const CMatrix &v)const{
        CMatrix m; m.r=Rotate(v.r);m.f=Rotate(v.f);m.u=Rotate(v.u);m.p=*this*v.p;return m;
    }
};
static constexpr int VR_HAND_COUNT=2,WEAPON_VALUE_SCALE=2;
struct WeaponCalibration { int offsetX=0,offsetY=0,offsetZ=0,rotationX=0,rotationY=0,rotationZ=0; };
static WeaponCalibration profiles[2];
static WeaponCalibration *GetCalibration(int hand,int){return &profiles[hand];}
static CMatrix gGripMatrix[2],gAimMatrix[2],camera;
static bool gPoseValid[2],gameplay=true,inVehicle=false;
static int gHeldSlot[2],type[2];
static float gGrip[2],gTrigger[2];
static uint32 testFrame=100,testTime=1000;
static float testDt=1.0f/90.0f;
struct CTimer {
    static uint32 GetFrameCounter(){return testFrame;}
    static uint32 GetTimeInMillisecondsNonClipped(){return testTime;}
    static float GetTimeStepNonClippedInSeconds(){return testDt;}
};
static bool IsGameplayAvailable(){return gameplay;}
static bool FindPlayerVehicle(){return inVehicle;}
static bool IsSupportHand(int){return false;}
static int GetVrWeaponTypeForSlot(int slot){return type[slot==WEAPONSLOT_UNARMED?0:1];}
static CVector VectorFromArray(const float *v){return {v[0],v[1],v[2]};}
namespace rw { namespace vulkan {
static bool getFirstPersonViewFrame(float *r,float *u,float *f,float *p){
    const CVector v[4]={camera.r,camera.u,camera.f,camera.p};float *out[4]={r,u,f,p};
    for(int a=0;a<4;++a){out[a][0]=v[a].x;out[a][1]=v[a].y;out[a][2]=v[a].z;}return true;
}
}}
#include "quest-melee-types.inc"
static MeleeStrike gMeleeStrike[2];
static MeleeMotion gMeleeMotion[2];
#include "quest-melee-production.inc"

static void Pose(int hand,float x,float angle=0.0f){
    CMatrix local;local.SetRotateZ(angle);local.p={x,0,1.2f};
    gGripMatrix[hand]=camera*local; gAimMatrix[hand]=camera*local;
}
static void Tick(){++testFrame;testTime+=static_cast<uint32>(testDt*1000.0f+0.5f);UpdateMelee();}
static void Reset(int hand,int weapon=WEAPONTYPE_KATANA){
    camera=CMatrix();gameplay=true;inVehicle=false;testTime=1000;testFrame=100;
    type[0]=WEAPONTYPE_UNARMED;type[1]=weapon;
    for(int i=0;i<2;++i){gMeleeMotion[i]=MeleeMotion();gMeleeStrike[i]=MeleeStrike();
        gPoseValid[i]=i==hand;gHeldSlot[i]=i==hand?WEAPONSLOT_MELEE:-1;
        gGrip[i]=gTrigger[i]=1;profiles[i]=WeaponCalibration();Pose(i,0);}
    Tick();Require(!gMeleeStrike[hand].pending,"baseline must not hit");
}
static bool Consume(int hand,CVector *tipOut=nullptr){
    int slot,weapon;CVector a,b,r0,r1;float speed;
    const bool hit=ConsumePhysicalMeleeStrike(hand,&slot,&weapon,&a,&b,&speed,&r0,&r1);
    if(hit){Require(slot==gHeldSlot[hand],"held inventory slot preserved");
        Require(weapon==type[1],"held weapon type preserved");
        Require(speed>=0.08f,"sweep carries deliberate speed");
        if(tipOut)*tipOut=b;
        if(weapon==WEAPONTYPE_KATANA)
            Require((b-r1).Magnitude()>0.93f,"katana retains full 94 cm blade without render cache");}
    return hit;
}
int main(){
    for(int hand=0;hand<2;++hand)for(float hz:{15.0f,30.0f,60.0f,90.0f,120.0f}){
        testDt=1.0f/hz;Reset(hand);
        // Draw directly into a swing: no calm sample or prior successful hit.
        Pose(hand,2.0f*testDt);Tick();CVector tip;
        Require(Consume(hand,&tip),"first katana swing works without preliminary pause");
        CMatrix expected;Require(BuildWeaponModelMatrix(hand,profiles[hand],&expected),"calibrated model valid");
        Require((tip-expected*MeleeModelTip(WEAPONTYPE_KATANA)).Magnitude()<1e-5f,"sweep uses current input pose");
        Pose(hand,2.0f*testDt+0.1f*testDt);Tick();
        Require(Consume(hand),"slow tail after fast swing stays active");
        ResolvePhysicalMeleeStrike(hand,true);
        Pose(hand,4.0f*testDt);Tick();Require(!Consume(hand),"one contact cannot repeat immediately");
        for(int i=0;i<static_cast<int>(0.24f/testDt)+2;++i){Pose(hand,4.0f*testDt);Tick();(void)Consume(hand);}
        Pose(hand,6.0f*testDt);Tick();Require(Consume(hand),"second deliberate swing rearms");
        Reset(hand);Pose(hand,2.0f);Tick();Require(!Consume(hand),"tracking teleport cannot attack");
        Reset(hand);gPoseValid[hand]=false;Tick();Require(!Consume(hand),"missing tracking cannot attack");
        Reset(hand);testDt=0.15f;Pose(hand,0.1f);Tick();Require(!Consume(hand),"long testFrame gap establishes a new baseline");testDt=1.0f/hz;
        Reset(hand);
        for(int i=0;i<20;++i){camera.SetRotateZ(0.1f*static_cast<float>(i));camera.p={0.4f*i,0,0};Pose(hand,0);Tick();
            Require(!Consume(hand),"walking and snap turning a stationary held blade cannot attack");}
    }
    for(int weapon:{WEAPONTYPE_BASEBALLBAT,WEAPONTYPE_KNIFE,WEAPONTYPE_MACHETE}){
        testDt=1.0f/90.0f;Reset(1,weapon);Pose(1,0.04f);Tick();Require(Consume(1),"other tracked melee weapons still emit sweeps");
    }
    std::printf("Quest production melee detector: %d checks passed\n",checks);
}
