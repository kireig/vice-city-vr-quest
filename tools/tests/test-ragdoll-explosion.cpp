#include "ragdoll-test-vector.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>
#define Max(a,b) ((a) > (b) ? (a) : (b))
#define Min(a,b) ((a) < (b) ? (a) : (b))
#define nil nullptr
#define GTA_VR_WEAPONS
#define VR_RAGDOLL_VEHICLE_NO_ENGINE
#include "VrRagdollPhysics.h"
#include "VrRagdollPose.h"
#include "VrRagdollMotion.h"
#include "VrRagdollImpact.h"
#include "VrRagdollBounds.h"
#include "VrRagdollSupport.h"
#include "VrRagdollExplosion.h"
#include "ragdoll-explosion-bones.inc"
using uint8=uint8_t; using int8=int8_t; using uint32=uint32_t; using int32=int32_t; using int16=int16_t;
enum {NUMPEDS=220,RANDOM_CHAR=0,PED_IDLE=1,PED_FALL=2,PED_DIE=3,PED_DEAD=4,ANIM_STD_NUM=999};
struct RwMatrix {CVector right{1,0,0},up{0,1,0},at{0,0,1},pos; int flags=0;};
struct CMatrix : RwMatrix { CVector operator*(const CVector &v)const{return pos+right*v.x+up*v.y+at*v.z;} };
static void Invert(const CMatrix &a,CMatrix &b){b=a;b.pos=-a.pos;}
namespace rw {struct HAnimHierarchy {enum {PUSH=1,POP=2};};}
struct RpHAnimHierarchy {int numNodes=19, missing=-1; RwMatrix matrices[40]; struct Info{int flags=0;}nodeInfo[40];};
struct RpClump {RpHAnimHierarchy hierarchy;};
static int hierarchyReads=0,groundReads=0,poolReads=0,androidLogCalls=0;
#define __ANDROID__
#define ANDROID_LOG_INFO 4
[[maybe_unused]] static int __android_log_print(int,const char*,const char*,...){++androidLogCalls;return 0;}
static RpHAnimHierarchy *GetAnimHierarchyFromSkinClump(RpClump *c){++hierarchyReads;return c ? &c->hierarchy:nil;}
static RwMatrix *RpHAnimHierarchyGetMatrixArray(RpHAnimHierarchy*h){return h->matrices;}
static int RpHAnimIDGetIndex(RpHAnimHierarchy*h,int tag);
struct CEntity{};
struct CPed : CEntity {
    int index=0,generation=1,CharCreatedBy=RANDOM_CHAR,m_nPedState=PED_DIE;
    bool live=true,player=false,bInVehicle=false,bIsInWater=false,bIsVisible=true,bRemoveFromWorld=false,bExplosionProof=false;
    float m_fMass=70; CMatrix matrix; CVector m_vecMoveSpeed,m_vecTurnSpeed; RpClump clump; bool hasClump=true;
    void SetPedState(int state){m_nPedState=state;}
    void ApplyMoveForce(CVector force){m_vecMoveSpeed+=force/m_fMass;}
    void ApplyMoveForce(float x,float y,float z){ApplyMoveForce(CVector(x,y,z));}
    bool IsPlayer()const{return player;} bool DyingOrDead()const{return m_nPedState==PED_DIE||m_nPedState==PED_DEAD;}
    RpClump*GetClump(){return hasClump?&clump:nil;} CVector GetPosition()const{return matrix.pos;}
    CMatrix GetMatrix()const{return matrix;} CVector GetRight()const{return matrix.right;}
    CVector GetForward()const{return matrix.up;} CVector GetUp()const{return matrix.at;}
    int GetModelIndex()const{return 7;}
};
static CPed peds[NUMPEDS];
static CPed*FindPlayerPed(){return nil;}
struct CPedPool {
    int GetJustIndex_NoFreeAssert(CPed*p){++poolReads;return p->index;}
    int GetSize()const{return NUMPEDS;}
    CPed*GetSlot(int i){++poolReads;return peds[i].live?&peds[i]:nil;}
};
struct CVehicle {bool bRemoveFromWorld=false;void*m_rwObject=this;VrRagdollVehicle::Pose pose;bool IsCar()const{return true;}
    CVector GetPosition()const{return pose.origin;}CVector GetRight()const{return pose.right;}
    CVector GetForward()const{return pose.forward;}CVector GetUp()const{return pose.up;}};
static CVehicle fixtureCar; static CPedPool pedPool;
struct CPools {static CPedPool*GetPedPool(){return &pedPool;}static int GetPedRef(CPed*p){return p->index*256+p->generation;}
    static CVehicle*GetVehicle(int handle){return handle==1?&fixtureCar:nil;}};
struct CTimer {static uint32 now,frame;static uint32 GetTimeInMilliseconds(){return now;}static uint32 GetFrameCounter(){return frame;}};
uint32 CTimer::now=2000,CTimer::frame=1;
struct Camera {CVector pos;CVector GetPosition()const{return pos;}};static Camera TheCamera;
namespace VrRagdollWorld {struct Cache{bool valid=false;};}
namespace VrRagdollMetrics {enum{ANIMATION,POSE};struct Scope{explicit Scope(int){}};}
struct CVector2D {float x,y;CVector2D(float a,float b):x(a),y(b){}};
enum {NUMSECTORS_X=1,NUMSECTORS_Y=1,ENTITYLIST_VEHICLES=0,ENTITYLIST_PEDS=1,ENTITYLIST_OBJECTS=2};
struct CPtrList {int type=0;};struct CSector {CPtrList m_lists[3]={{0},{1},{2}};};static CSector sector;
struct CWorld {
    static float FindGroundZFor3DCoord(float,float,float,bool*found){++groundReads;*found=true;return 0;}
    static int GetSectorIndexX(float){return 0;} static int GetSectorIndexY(float){return 0;}
    static CSector*GetSector(int,int){return &sector;}
    static void TriggerExplosion(const CVector&,float,float,CEntity*,bool);
    static void TriggerExplosionSectorList(CPtrList&,const CVector&,float,float,CEntity*,bool);
};
#pragma warning(push)
#pragma warning(disable:4244) // Native hierarchy indices are int16, admitted only below 40 nodes.
#include "ragdoll-explosion-production.inc"
#pragma warning(pop)
static int RpHAnimIDGetIndex(RpHAnimHierarchy*h,int tag){for(int i=0;i<19;++i)if(VrRagdoll::simBoneTags[i]==tag)return h->missing==i?-1:i;return -1;}
#include "ragdoll-explosion-world.inc"
// The native per-ped force expression and SetDie takeover gate are extracted
// separately; object/vehicle destruction and event reporting are engine adapters.
#include "ragdoll-explosion-native.inc"
void CWorld::TriggerExplosionSectorList(CPtrList&list,const CVector&where,float radius,float power,CEntity*,bool){
    if(list.type!=ENTITYLIST_PEDS)return;
    for(CPed &ped:peds)if(ped.live&&!ped.bExplosionProof&&!ped.bInVehicle){
        const CVector distance=ped.GetPosition()-where;const float magnitude=distance.Magnitude();
        if(magnitude>=radius)continue;
        NativeForce(ped,distance,magnitude,radius,power);
        if(!ped.DyingOrDead())NativeDie(ped);
    }
}
static unsigned checks=0;
static void Check(bool b,const char*m){++checks;if(!b){std::fprintf(stderr,"FAIL %s (check %u)\n",m,checks);std::exit(1);}}
static bool Near(CVector a,CVector b,float eps=0.0001f){return(a-b).Magnitude()<eps;}
static void Fill(CPed&p,int i,CVector root={0,0,0}){
    const int generation=p.generation;p=CPed();p.index=i;p.generation=generation;p.matrix.pos=root;
    const CVector points[19]={{0,0,.94f},{0,0,1.10f},{0,0,1.31f},{0,0,1.53f},{0,0,1.69f},
        {-.12f,0,1.43f},{-.26f,0,1.45f},{-.43f,-.035f,1.20f},{-.53f,.03f,.98f},
        {.12f,0,1.43f},{.26f,0,1.45f},{.43f,-.035f,1.20f},{.53f,.03f,.98f},
        {-.13f,0,.89f},{-.14f,.035f,.48f},{-.14f,-.04f,.1f},{.13f,0,.89f},{.14f,.035f,.48f},{.14f,-.04f,.1f}};
    for(int n=0;n<19;++n)p.clump.hierarchy.matrices[n].pos=root+points[n];
}
static void Reset(){
    VrRagdoll::SetEnabled(true);
    for(CPed&p:peds){VrRagdoll::Release(&p);p.live=false;++p.generation;}
    TheCamera.pos={0,0,0};CTimer::now+=2000;++CTimer::frame;hierarchyReads=groundReads=poolReads=0;
}
[[maybe_unused]] static CVector Velocity(const VrRagdoll::Slot*s,int n){return(s->pos[n]-s->prev[n])/VrRagdollPhysics::STEP;}
int main(){
    using namespace VrRagdoll;using namespace VrRagdollPhysics;
    Reset();Fill(peds[0],0,{31,0,0});CPed &p=peds[0];p.m_nPedState=PED_IDLE;
    NativeDie(p);Check(FindSlot(&p)==nil,"native SetDie rejects distant death");Check(p.m_nPedState==PED_DIE,"native death state retained");
    TheCamera.pos={20,0,0};Apply(&p);
#if OLD_BASELINE
    Check(FindSlot(&p)==nil,"520 reproduction: approaching distant casualty keeps stock animation");
#else
    Check(FindSlot(&p)!=nil,"approaching distant casualty takes current death pose");
    Check(groundReads==1,"late capture one ground query");Check(p.m_nPedState==PED_DIE,"late capture never revives ped");
#endif
    Reset();Fill(peds[0],0);Check(Begin(&p),"fresh corpse admission");Slot *s=FindSlot(&p);s->asleep=true;s->poseCached=true;
    // Native root is distant, the actual visible joints are beside the explosion.
    p.matrix.pos={100,0,0};CVector before=s->prev[RD_PELVIS];
    CWorld::TriggerExplosion({-2,0,0},10,300,nil,false);
#if OLD_BASELINE
    Check(s->asleep&&Near(s->prev[RD_PELVIS],before),"520 reproduction: blast beside visible saved corpse has no solver impulse");
    std::printf("BASELINE: %u checks; both defects reproduced with original 520 methods.\n",checks);return 0;
#else
    Check(!s->asleep&&!s->poseCached,"blast wakes visible saved corpse");Check(!Near(s->prev[RD_PELVIS],before),"blast reaches joints despite remote native root");
    const CVector v1=Velocity(s,RD_PELVIS);CWorld::TriggerExplosion({-2,0,0},10,300,nil,false);
    Check(Near(Velocity(s,RD_PELVIS),v1*2,.0005f),"repeat explosion adds momentum without reset");
    for(int n=0;n<SIM_BONES;++n)Check(Near(s->pos[n],p.clump.hierarchy.matrices[n].pos),"impulse preserves body pose");
    Reset();Fill(peds[0],0);p.m_nPedState=PED_IDLE;
    CWorld::TriggerExplosion({-2,0,0},10,300,nil,false);s=FindSlot(&p);Check(s!=nil,"RPG native death gate admits fresh corpse");
    const CVector expected=p.m_vecMoveSpeed*50;
    for(int n=0;n<SIM_BONES;++n)Check(Near(Velocity(s,n),expected,.0005f),"new casualty receives native explosion seed exactly once");
    // Car presentation changes the query frame, not solver coordinates.
    Reset();Fill(peds[0],0);Check(Begin(&p),"support corpse");s=FindSlot(&p);s->asleep=true;
    s->support.handle=1;s->support.age=.04f;s->support.speed=15;s->support.solved={{0,0,0},{1,0,0},{0,1,0},{0,0,1}};
    fixtureCar.pose=s->support.solved;fixtureCar.pose.origin={.4f,0,0};const auto transform=Presentation(s);Check(transform.active,"real support presentation active");
    CVector savedPos[19],savedPrev[19];for(int n=0;n<19;++n){savedPos[n]=s->pos[n];savedPrev[n]=s->prev[n];}
    const CVector centre=transform.Point(s->pos[RD_PELVIS])+CVector(-.2f,0,0);
    CVector reference[19];for(int n=0;n<19;++n)reference[n]=savedPrev[n];
    VrRagdollExplosion::Apply(savedPos,reference,19,transform.InversePoint(centre),transform.InverseDirection({0,0,1}),1,300,70,2,STEP);
    ExplosionImpulse(centre,1,300);for(int n=0;n<19;++n){Check(Near(reference[n],s->prev[n]),"presented blast maps to same solver delta");Check(Near(savedPos[n],s->pos[n]),"support blast does not teleport joints");}
    Check(s->support.handle==1,"presentation support survives impulse until normal solver update");
    // Admission work is shared between eyes and bodies, with retry fairness.
    Reset();Fill(peds[0],0);Fill(peds[1],1);p.clump.hierarchy.missing=RD_HEAD;
    Apply(&p);const int reads=hierarchyReads;for(int eye=0;eye<10;++eye){Apply(&p);Apply(&peds[1]);}
    Check(hierarchyReads==reads,"one capture attempt per frame even after unsupported skeleton");
    ++CTimer::frame;CTimer::now+=14;Apply(&p);Apply(&peds[1]);Check(FindSlot(&peds[1])!=nil,"bad skeleton cooldown does not starve healthy corpse");
    p.clump.hierarchy.missing=-1;++p.generation;++CTimer::frame;Apply(&p);Check(FindSlot(&p)!=nil,"recycled native generation bypasses prior denial");
    Reset();Fill(peds[0],0);p.m_nPedState=PED_DEAD;p.m_vecMoveSpeed={.2f,0,.2f};Apply(&p);s=FindSlot(&p);Check(s!=nil,"late settled corpse admission");
    for(int n=0;n<19;++n)Check(Velocity(s,n).MagnitudeSqr()<1e-7f,"old corpse is not relaunched from stale native speed");
    Reset();Fill(peds[0],0);p.live=false;Apply(&p);Check(active==0,"deleted native pool identity cannot be admitted");
    p.live=true;p.bIsVisible=false;Apply(&p);Check(active==0,"invisible corpse not resurrected");p.bIsVisible=true;p.bRemoveFromWorld=true;Apply(&p);Check(active==0,"removing corpse not resurrected");
    Reset();SetEnabled(false);Fill(peds[0],0);Apply(&p);ExplosionImpulse({0,0,0},10,300);Check(active==0&&hierarchyReads==0&&poolReads==0&&groundReads==0,"OFF empty has no admission scan/query/capture");
    androidLogCalls=0;const int deniedBefore=denied[DENY_OFF];
    for(int death=0;death<1000;++death){Check(!Begin(&p),"OFF native death rejects takeover");Check(!BeginFall(&p),"OFF native fall rejects takeover");}
    #if MIAMIVR_DEV_TOOLS
    Check(denied[DENY_OFF]==deniedBefore+2000,"developer build retains OFF denial count without logs");
#else
    Check(denied[DENY_OFF]==deniedBefore,"player build has no diagnostic counter work when OFF");
#endif
    Check(androidLogCalls==0&&hierarchyReads==0&&poolReads==0&&groundReads==0,"OFF Begin and BeginFall never enter Android logging or capture/pool/ground work");

    SetEnabled(true);Check(Begin(&p),"owner before disable");s=FindSlot(&p);s->asleep=true;before=s->prev[0];SetEnabled(false);ExplosionImpulse({-2,0,0},10,300);Check(s->asleep&&Near(before,s->prev[0]),"OFF does not wake saved body");
    // Full native capacity, repeated blast, owner release and address recycling.
    for(int cycle=0;cycle<4;++cycle){Reset();for(int i=0;i<NUMPEDS;++i){Fill(peds[i],i,{float(i%10),0,0});Check(Begin(&peds[i]),"full native pool admission");}
        Check(active==NUMPEDS,"all 220 owners survive");
        for(int shot=0;shot<5;++shot){CWorld::TriggerExplosion({-1,0,0},20,300,nil,false);for(int i=0;i<NUMPEDS;++i){Check(FindSlot(&peds[i])!=nil,"repeated RPG retains owner");Apply(&peds[i]);}}
        for(int i=0;i<NUMPEDS;++i){Release(&peds[i]);++peds[i].generation;Check(Begin(&peds[i]),"destructor recycle remains admissible");}Check(active==NUMPEDS,"recycling has no leaked owners");}
    Reset();Fill(peds[0],0);Check(BeginFall(&p),"surviving knockdown admission");s=FindSlot(&p);before=s->prev[0];Check(BeginFall(&p)&&Near(before,s->prev[0]),"repeated fall callback preserves articulated motion");Check(Begin(&p)&&!s->live,"death converts existing fall in place");
    // Pure helper: radial falloff, per-weight response, invalid/outside input.
    const CVector points[3]={{2,0,1},{9.99f,0,1},{11,0,1}},up{0,0,1};CVector a[3],b[3];
    for(int i=0;i<3;++i)a[i]=b[i]=points[i];
    Check(VrRagdollExplosion::Apply(points,a,3,{0,0,1},up,10,300,70,1,STEP)==2,"blast affects only joints inside radius");
    Check(VrRagdollExplosion::Apply(points,b,3,{0,0,1},up,10,300,70,2,STEP)==2,"weighted blast reaches same joints");
    for(int i=0;i<3;++i)Check(Near((points[i]-a[i])*.5f,points[i]-b[i]),"double mass halves blast velocity change");
    Check((points[1]-a[1]).Magnitude()<(points[0]-a[0]).Magnitude()*.01f,"edge falloff tends to zero");Check(Near(points[2],a[2]),"no force outside blast");
    const CVector old=a[0];Check(!VrRagdollExplosion::Apply(points,a,3,{0,0,0},up,10,std::numeric_limits<float>::quiet_NaN(),70,1,STEP)&&Near(old,a[0]),"invalid power no mutation");
    Check(!VrRagdollExplosion::Apply(points,a,3,{0,0,0},up,0,300,70,1,STEP),"zero radius no mutation");
#if MIAMIVR_DEV_TOOLS
    Check(androidLogCalls>0,"developer build retains bounded enabled diagnostics");
#else
    Check(androidLogCalls==0,"player build never calls Android ragdoll diagnostic logger while ON or OFF");
#endif
    std::printf("FIXED: %u checks PASS; actual capture/apply/release, native death seed, world hook order, 220 owners, repeated RPG, support query and OFF gates.\n",checks);return 0;
#endif
}
