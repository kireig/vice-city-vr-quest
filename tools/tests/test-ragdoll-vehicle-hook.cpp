#include "ragdoll-test-vector.h"
#include <cstdio>
#include <cstdlib>
#define nil nullptr
#define SQR(x) ((x)*(x))
static constexpr int RANDOM_CHAR=1,MISSION_CHAR=2,MI_TRAIN=3;
static float Max(float a,float b){return a>b?a:b;}
static bool enabled=true;
namespace VrRagdoll {static bool IsEnabled(){return enabled;}}
struct CColPoint{CVector point,normal;int pieceA=0,pieceB=0,surfaceA=0,surfaceB=0;};
static CColPoint aColPoints[4];
static int numCollisions=4;
struct CPhysical {
    bool ped=false,vehicle=false,bHasContacted=false,bHasHitWall=false;
    bool bUsesCollision=true,bRemoveFromWorld=false;float m_fMass=1400,m_fDamageImpulse=0;
    int model=0,response=0;CVector position,m_vecMoveSpeed,m_vecTurnSpeed,m_vecMoveFriction,m_vecTurnFriction;
    bool IsPed()const{return ped;}bool IsVehicle()const{return vehicle;}int GetModelIndex()const{return model;}
    const CVector &GetPosition()const{return position;}
    bool ApplyCollision(CPhysical *other,CColPoint&,float &a,float &b){
        const float ia[4]={0,3,8,1},ib[4]={0,5,2,9};
        const int index=response++;a=ia[index];b=ib[index];
        m_vecMoveSpeed+=CVector(1,2,3);other->m_vecMoveSpeed+=CVector(3,2,1);
        m_vecTurnSpeed+=CVector(4,5,6);other->m_vecTurnSpeed+=CVector(6,5,4);
        return index!=0;
    }
    void SetDamagedPieceRecord(int,float amount,CPhysical*,const CVector&){m_fDamageImpulse=amount;}
    bool ApplyFriction(CPhysical*,float,CColPoint&){return false;}
};
struct CVehicle:CPhysical {
    bool road=true,heli=false,plane=false;
    CVehicle(){vehicle=true;}
    bool IsCar()const{return road;}bool IsRealHeli()const{return heli;}bool IsRealPlane()const{return plane;}
};
struct CPed:CPhysical {
    bool player=false,bInVehicle=false,bIsInWater=false,control=true,clump=true;
    int CharCreatedBy=RANDOM_CHAR,calls=0;CVector incoming,turn,point;float amount=0;
    CPed(){ped=true;}
    bool IsPlayer()const{return player;}bool IsPedInControl()const{return control;}
    void *GetClump()const{return clump?(void*)this:nullptr;}
    void KillPedWithCar(CVehicle*,float j,const CVector *v,const CVector *p,const CVector *w){
        ++calls;amount=j;incoming=*v;point=*p;turn=*w;
    }
};
static struct Camera{CVector position;const CVector &GetPosition()const{return position;}} TheCamera;
static struct Audio{void ReportCollision(CPhysical*,CPhysical*,int,int,float,float){}} DMAudio;
struct CSurfaceTable {static float GetAdhesiveLimit(const CColPoint&){return 0;}};
#include "ragdoll-vehicle-hook-production.inc"
static int checks;
static void Check(bool value,const char *label){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
static void Gate(){
    CPed ped;CVehicle car;ped.position={0,2,0};CVector before;
    car.m_vecMoveSpeed={0,2.0f/50,0};
    Check(IsVrVehicleKnockdown(&ped,&car,1,&before),"low Stinger impulse falls when car approaches");
    Check(!IsVrVehicleKnockdown(&ped,&car,0,&before),"no impulse is not a hit");
    Check(!IsVrVehicleKnockdown(&ped,&car,1,nullptr),"legacy path without snapshot does not guess");
    car.m_vecMoveSpeed={};Check(!IsVrVehicleKnockdown(&ped,&car,1,&before),"parked contact does not fall");
    car.m_vecMoveSpeed={0,-.1f,0};Check(!IsVrVehicleKnockdown(&ped,&car,1,&before),"departing car does not fall");
    car.m_vecMoveSpeed={.1f,0,0};Check(!IsVrVehicleKnockdown(&ped,&car,1,&before),"tangential contact does not fall");
    car.m_vecMoveSpeed={0,.04f,0};before=car.m_vecMoveSpeed;
    Check(!IsVrVehicleKnockdown(&ped,&car,1,&before),"co-moving body gets no fresh low-speed knockdown");before={};
    enabled=false;Check(!IsVrVehicleKnockdown(&ped,&car,1,&before),"OFF preserves native threshold");enabled=true;
    ped.player=true;Check(!IsVrVehicleKnockdown(&ped,&car,1,&before),"player excluded");ped.player=false;
    ped.CharCreatedBy=MISSION_CHAR;Check(!IsVrVehicleKnockdown(&ped,&car,1,&before),"mission actor excluded");ped.CharCreatedBy=RANDOM_CHAR;
    ped.bInVehicle=true;Check(!IsVrVehicleKnockdown(&ped,&car,1,&before),"occupant excluded");ped.bInVehicle=false;
    ped.bIsInWater=true;Check(!IsVrVehicleKnockdown(&ped,&car,1,&before),"water actor excluded");ped.bIsInWater=false;
    ped.control=false;Check(!IsVrVehicleKnockdown(&ped,&car,1,&before),"fall/death does not restart");ped.control=true;
    ped.clump=false;Check(!IsVrVehicleKnockdown(&ped,&car,1,&before),"missing skeleton excluded");ped.clump=true;
    ped.position={0,40,0};Check(!IsVrVehicleKnockdown(&ped,&car,1,&before),"distant actor keeps native handling");ped.position={0,2,0};
    car.road=false;Check(!IsVrVehicleKnockdown(&ped,&car,1,&before),"bike or boat excluded");car.road=true;
    car.heli=true;Check(!IsVrVehicleKnockdown(&ped,&car,1,&before),"helicopter excluded");car.heli=false;
    car.plane=true;Check(!IsVrVehicleKnockdown(&ped,&car,1,&before),"plane excluded");car.plane=false;
    car.bUsesCollision=false;Check(!IsVrVehicleKnockdown(&ped,&car,1,&before),"non-colliding car excluded");car.bUsesCollision=true;
    car.bRemoveFromWorld=true;Check(!IsVrVehicleKnockdown(&ped,&car,1,&before),"deleted car excluded");car.bRemoveFromWorld=false;
    car.m_fMass=0;Check(!IsVrVehicleKnockdown(&ped,&car,1,&before),"zero-mass car excluded");
}
int main(){
    Gate();
    for(int mode=0;mode<4;++mode)for(int order=0;order<2;++order){
        CPed ped;CVehicle car;ped.m_vecMoveSpeed={.01f,.02f,.03f};ped.m_vecTurnSpeed={.04f,.05f,.06f};
        const CVector v=ped.m_vecMoveSpeed,w=ped.m_vecTurnSpeed;
        CPhysical *a=order?(CPhysical*)&ped:&car,*b=order?(CPhysical*)&car:&ped;
        a->bHasContacted=(mode&1)!=0;b->bHasContacted=(mode&2)!=0;
        for(int i=0;i<4;++i)aColPoints[i].point={float(i),.25f,.45f};
        RunPair(a,b);
        Check(ped.calls==1,"both collision processing orders deliver exactly one callback");
        Check((ped.incoming-v).Magnitude()<1e-6f,"callback receives velocity before every native impulse");
        Check((ped.turn-w).Magnitude()<1e-6f,"callback receives angular velocity before response");
        Check(ped.point.x==float(order?2:3),"callback preserves strongest actual contact point in every friction branch");
        Check(ped.amount==float(order?8:9),"brake callback keeps native collision impulse unchanged");
    }
    CPed ped;CVehicle car;enabled=false;RunPair(&ped,&car);Check(ped.calls==0,"OFF retains old ped-first callback order");
    std::printf("Vehicle production hook: %d checks passed\n",checks);
}
