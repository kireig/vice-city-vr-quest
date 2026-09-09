#include "ragdoll-test-vector.h"
#include "../../src/weapons/WeaponType.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <cstdint>
#include <fstream>
#include <vector>
#include <cstring>
#include <cassert>
#define FIX_BUGS
#define nil nullptr
using int32=int32_t;
static float Sqrt(float x){return std::sqrt(x);}
static float Max(float a,float b){return a>b?a:b;}
static float Min(float a,float b){return a<b?a:b;}
static float sq(float x){return x*x;}
static float Abs(float x){return std::fabs(x);}
struct CVector2D {
    float x=0,y=0;CVector2D()=default;CVector2D(float a,float b):x(a),y(b){}
    float MagnitudeSqr()const{return x*x+y*y;}
    CVector2D operator-(const CVector2D&v)const{return {x-v.x,y-v.y};}
    void Normalise(){const float m=std::sqrt(MagnitudeSqr());if(m>0){x/=m;y/=m;}}
};
static float CrossProduct2D(CVector2D a,CVector2D b){return a.x*b.y-a.y*b.x;}
struct CEntity {
    virtual ~CEntity()=default;
    bool vehicle=false;CVector position;
    bool IsVehicle()const{return vehicle;}
    const CVector&GetPosition()const{return position;}
};
struct CVehicle:CEntity {
    CVehicle(){vehicle=true;}
    CEntity *pDriver=nullptr;float health=1000;bool specialHeli=false;
    bool IsHeli()const{return specialHeli;}
#pragma warning(push)
#pragma warning(disable:4100)
#include "heli-empty-blowup-production.inc"
#pragma warning(pop)
};
struct TestMatrix {CVector pos;CVector operator*(const CVector&v)const{return pos+v;}};
enum{NUM_HELIS=4,HELI_CATALINA=3,HELI_STATUS_HOVER=0,HELI_STATUS_SHOT_DOWN=4,PARTICLE_SPARK=1};
struct CHeli:CVehicle {
    CHeli(){specialHeli=true;}
    bool bBulletProof=false,bExplosionProof=false;int m_heliType=0,m_numSwat=4;
    int m_nBulletDamage=0,m_heliStatus=HELI_STATUS_HOVER,m_nExplosionTimer=0;
    float m_fAngularSpeed=0;
    static CHeli *pHelis[NUM_HELIS];
    const CVector&GetPosition()const{return position;}
    TestMatrix GetMatrix()const{return {position};}
    static bool TestBulletCollision(CVector*,CVector*,CVector*,int32);
    static bool TestSniperCollision(CVector*,CVector*);
    static bool TestRocketCollision(CVector*);
};
CHeli *CHeli::pHelis[NUM_HELIS]={};
struct CColPoint {CVector point,normal;int surfaceA=0,pieceA=0,surfaceB=0,pieceB=0;};
struct CColLine {CVector p0,p1;};
struct CColSphere {CVector center;float radius=0;int surface=0,piece=0;};
enum{DIR_X_POS,DIR_X_NEG,DIR_Y_POS,DIR_Y_NEG,DIR_Z_POS,DIR_Z_NEG};
struct CompressedVector {CVector value;CVector Get()const{return value;}};
struct CColTriangle {unsigned a=0,b=0,c=0;int surface=0;};
struct CColTrianglePlane {
    CVector normal;float dist=0;int dir=0;
    void Set(const CVector&,const CVector&,const CVector&);
    void GetNormal(CVector&n)const{n=normal;}
    float CalcPoint(const CVector&v)const{return DotProduct(normal,v)-dist;}
};
struct CStoredCollPoly {CVector verts[3];bool valid=false;};
struct CCollision {
    static float DistToLine(const CVector*,const CVector*,const CVector*);
    static bool ProcessLineSphere(const CColLine&,const CColSphere&,CColPoint&,float&);
    static bool ProcessLineTriangle(const CColLine&,const CompressedVector*,const CColTriangle&,
        const CColTrianglePlane&,CColPoint&,float&,CStoredCollPoly*);
};
struct CGeneral {static bool GetRandomTrueFalse(){return true;}};
struct CTimer {static int GetTimeInMilliseconds(){return 1000;}};
#include "heli-native-production.inc"
static CEntity gPlayer,gWall;
static CEntity *FindPlayerPed(){return &gPlayer;}
static CVehicle *gPlayerCar=nullptr;
static CVehicle *FindPlayerVehicle(){return gPlayerCar;}
struct CWeaponInfo {float m_fRange=90;};
struct CWorld {
    inline static bool bIncludeBikers=false,bIncludeDeadPeds=false,bIncludeCarTyres=false;
    inline static CEntity *pIgnoreEntity=nullptr;
};
static CVector gAimSource,gAimDirection(0,1,0),gHitPoint,gResultTarget,gResultTraceSource;
static CEntity *gHitEntity=nullptr,*gResultVictim=nullptr,*gIgnoreDuringTrace=nullptr;
static bool gFlagsDuringTrace=false;
static int gLOSCalls=0,gOccupantCalls=0,gSparks=0,gSparkRotation=0;
static std::vector<CColSphere> gActualSpheres;
static std::vector<CompressedVector> gActualVertices;
static std::vector<CColTriangle> gActualTriangles;
static CHeli *gActualHeli=nullptr;
static CVector GetVrVehicleBulletTraceSource(CVehicle*,CVector source,CVector direction){return source+direction*.25f;}
static bool ProcessLineOfSight(CVector source,CVector target,CColPoint&point,CEntity*&victim,eWeaponType,CEntity*,bool,bool,bool,bool,bool,bool,bool){
    ++gLOSCalls;gIgnoreDuringTrace=CWorld::pIgnoreEntity;
    gFlagsDuringTrace=CWorld::bIncludeBikers&&CWorld::bIncludeDeadPeds&&CWorld::bIncludeCarTyres;
    victim=gHitEntity;if(victim)point.point=gHitPoint;
    if(gActualHeli){
        const CVector ray=target-source;
        float fraction=victim?DotProduct(point.point-source,ray)/ray.MagnitudeSqr():1;
        for(CColSphere sphere:gActualSpheres){
            sphere.center+=gActualHeli->position;
            if(CCollision::ProcessLineSphere({source,target},sphere,point,fraction))victim=gActualHeli;
        }
        const CColLine localLine={source-gActualHeli->position,target-gActualHeli->position};
        for(const CColTriangle &tri:gActualTriangles){
            CColTrianglePlane plane;plane.Set(gActualVertices[tri.a].Get(),gActualVertices[tri.b].Get(),gActualVertices[tri.c].Get());
            CColPoint trianglePoint;
            if(CCollision::ProcessLineTriangle(localLine,gActualVertices.data(),tri,plane,trianglePoint,fraction,nullptr)){
                point=trianglePoint;point.point+=gActualHeli->position;victim=gActualHeli;
            }
        }
    }
    return victim!=nullptr;
}
static void CheckForShootingVehicleOccupant(CEntity**,CColPoint*,eWeaponType,CVector,CVector){++gOccupantCalls;}
struct CParticle {
    static void AddParticle(int,CVector,CVector,void* =nullptr,float=0,int rot=0){++gSparks;gSparkRotation=rot;}
};
struct CCam {enum{MODE_M16_1STPERSON=1,MODE_HELICANNON_1STPERSON=2};};
static struct Camera {struct WeaponMode{int Mode=0;}PlayerWeaponMode;} TheCamera;
enum Route{ROUTE_NONE,ROUTE_INSTANT,ROUTE_SHOTGUN,ROUTE_SNIPER,ROUTE_M16};
static Route gRoute=ROUTE_NONE;
static bool gOriginal=false;
struct CWeapon {
    eWeaponType m_eWeaponType=WEAPONTYPE_M4;
    CWeaponInfo info;
    bool Dispatch(CEntity*,CVector*);
    bool FireInstantHit(CEntity*,CVector*);
    bool FireShotgun(CEntity*,CVector*){gRoute=ROUTE_SHOTGUN;return true;}
    bool FireSniper(CEntity*){gRoute=ROUTE_SNIPER;return true;}
    bool FireM16_1stPerson(CEntity*);
};
#include "heli-weapon-production.inc"
bool CWeapon::FireInstantHit(CEntity*shooter,CVector*source){
    gRoute=ROUTE_INSTANT;
    if(gOriginal)RunOriginalTracked(shooter,source,&info,m_eWeaponType);
    else RunFixed(shooter,source,&info,m_eWeaponType);
    return true;
}
bool CWeapon::FireM16_1stPerson(CEntity*){
    gRoute=ROUTE_M16;RunM16Special(gAimSource,gAimSource+gAimDirection*info.m_fRange,m_eWeaponType);return true;
}
static int checks=0;
static void Check(bool value,const char*label){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
static void Reset(CHeli &heli){
    heli=CHeli{};for(auto &p:CHeli::pHelis)p=nullptr;CHeli::pHelis[0]=&heli;
    gAimSource={};gAimDirection={0,1,0};gHitEntity=nullptr;gHitPoint={};
    gPlayerCar=nullptr;gLOSCalls=gOccupantCalls=gSparks=gSparkRotation=0;
    gResultVictim=nullptr;gRoute=ROUTE_NONE;gOriginal=false;
    gActualHeli=nullptr;
    CWorld::pIgnoreEntity=nullptr;TheCamera.PlayerWeaponMode.Mode=0;
}
static void Shoot(CWeapon&w){CVector source=gAimSource;Check(w.Dispatch(&gPlayer,&source),"native dispatch accepts firearm");}
template<class T>static T ReadCol(const std::vector<unsigned char>&data,size_t offset){
    Check(offset<=data.size()&&sizeof(T)<=data.size()-offset,"COL read is bounded");
    T value;std::memcpy(&value,data.data()+offset,sizeof(T));return value;
}
static CVector ReadColVector(const std::vector<unsigned char>&data,size_t offset){
    return {ReadCol<float>(data,offset),ReadCol<float>(data,offset+4),ReadCol<float>(data,offset+8)};
}
static void ActualCol(const char*path){
    std::ifstream input(path,std::ios::binary);Check(input.good(),"local COL asset is readable");
    const std::vector<unsigned char> data((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    for(size_t start=0;start<data.size();){
        Check(ReadCol<unsigned>(data,start)==0x4c4c4f43,"native COLL signature");
        const size_t end=start+8+ReadCol<unsigned>(data,start+4);
        Check(end>start+72&&end<=data.size(),"COL model fits asset");
        char name[23]={};std::memcpy(name,data.data()+start+8,22);
        if(std::strcmp(name,"chopper")==0){
            size_t at=start+72;const short count=ReadCol<short>(data,at);at+=4;
            Check(count>0&&count<256,"bounded native helicopter sphere count");
            for(int i=0;i<count;++i,at+=20){
                CColSphere sphere;sphere.radius=ReadCol<float>(data,at);sphere.center=ReadColVector(data,at+4);
                gActualSpheres.push_back(sphere);
            }
            for(int i=0;i<2;++i,at+=4)Check(ReadCol<short>(data,at)==0,"native chopper has no line or box primitives");
            const short vertices=ReadCol<short>(data,at);at+=4;
            Check(vertices>=0&&vertices<256,"bounded chopper vertex count");
            for(int i=0;i<vertices;++i,at+=12)gActualVertices.push_back({ReadColVector(data,at)});
            const short triangles=ReadCol<short>(data,at);at+=4;
            Check(triangles>=0&&triangles<256,"bounded chopper triangle count");
            for(int i=0;i<triangles;++i,at+=16){
                CColTriangle tri;tri.a=ReadCol<unsigned>(data,at);tri.b=ReadCol<unsigned>(data,at+4);tri.c=ReadCol<unsigned>(data,at+8);
                Check(tri.a<gActualVertices.size()&&tri.b<gActualVertices.size()&&tri.c<gActualVertices.size(),"chopper triangle indices in range");
                gActualTriangles.push_back(tri);
            }
            Check(at==end,"entire special helicopter collider parsed");break;
        }
        start=end;
    }
    Check(!gActualSpheres.empty(),"actual special chopper model found");
    CWeapon gun;gun.m_eWeaponType=WEAPONTYPE_RUGER;gun.info.m_fRange=90;
    CHeli heli;
    for(CVector from:{CVector(0,-40,1.2f),CVector(0,40,1.2f),CVector(-15,-40,1.2f),CVector(15,-40,1.2f)}){
        Reset(heli);gActualHeli=&heli;gAimSource=from;
        gAimDirection=CVector(0,0,1.2f)-from;gAimDirection.Normalise();
        Shoot(gun);Check(gResultVictim==&heli,"native actual COL creates direct heli victim");
        Check(heli.m_nBulletDamage==4,"actual front/rear/oblique collider does not shield its damage proxy");
        CColPoint first;float fraction=1;
        for(const CColSphere &sphere:gActualSpheres)CCollision::ProcessLineSphere({from,from+gAimDirection*90},sphere,first,fraction);
        for(const CColTriangle &tri:gActualTriangles){
            CColTrianglePlane plane;plane.Set(gActualVertices[tri.a].Get(),gActualVertices[tri.b].Get(),gActualVertices[tri.c].Get());
            CCollision::ProcessLineTriangle({from,from+gAimDirection*90},gActualVertices.data(),tri,plane,first,fraction,nullptr);
        }
        const float clippedDistance=CCollision::DistToLine(&from,&first.point,&heli.position);
        if(from.x==0&&from.y<0)Check(clippedDistance>5,"actual rear tail entry reproduces the first patch self-shielding bug");
        std::printf("Actual chopper ray from(%.1f,%.1f,%.1f): firstCOLDistance %.3f damage %d\n",from.x,from.y,from.z,clippedDistance,heli.m_nBulletDamage);
        Reset(heli);gActualHeli=&heli;gAimSource=from;gAimDirection=CVector(0,0,1.2f)-from;gAimDirection.Normalise();
        gHitEntity=&gWall;gHitPoint=from+gAimDirection*10;Shoot(gun);
        Check(gResultVictim==&gWall&&heli.m_nBulletDamage==0,"nearer wall still blocks actual helicopter geometry");
    }
    Reset(heli);gActualHeli=&heli;gAimSource={0,-40,1.2f};gAimDirection={0,1,0};
    CHeli behind;behind.position={0,25,0};CHeli::pHelis[1]=&behind;Shoot(gun);
    Check(heli.m_nBulletDamage==4&&behind.m_nBulletDamage==0,"direct target extension stops at target instead of hitting helicopters beyond");
    Reset(heli);gActualHeli=&heli;gAimSource={0,-40,1.2f};gAimDirection={0,1,0};gun.info.m_fRange=34;
    Shoot(gun);Check(gResultVictim==&heli&&heli.m_nBulletDamage==0,"actual tail hit cannot extend past original weapon range into central proxy");
    gActualHeli=nullptr;gActualSpheres.clear();gActualVertices.clear();gActualTriangles.clear();
}
int main(int argc,char**argv){
    CHeli heli;CWeapon gun;
    const eWeaponType generic[]={WEAPONTYPE_COLT45,WEAPONTYPE_PYTHON,WEAPONTYPE_UZI,
        WEAPONTYPE_TEC9,WEAPONTYPE_SILENCED_INGRAM,WEAPONTYPE_MP5,WEAPONTYPE_M4,
        WEAPONTYPE_RUGER,WEAPONTYPE_M60,WEAPONTYPE_MINIGUN,WEAPONTYPE_HELICANNON};
    for(auto type:generic)for(int hand=0;hand<2;++hand)for(float range:{30.0f,45.0f,90.0f}){
        Reset(heli);gun.m_eWeaponType=type;gun.info.m_fRange=range;
        gAimSource={hand?-.3f:.3f,1,1.4f};gAimDirection={.1f,.9f,.3f};gAimDirection.Normalise();
        heli.position=gAimSource+gAimDirection*(range*.75f);
        gOriginal=true;Shoot(gun);
        Check(heli.m_nBulletDamage==0&&gSparks==0,"pre-fix tracked bullets miss special damage callback");
        gOriginal=false;Shoot(gun);
        Check(gRoute==ROUTE_INSTANT,"all generic bullet types retain original dispatch");
        Check(heli.m_nBulletDamage==4&&gSparks==16,"one tracked shot applies special damage once");
        Check(gSparkRotation==(type==WEAPONTYPE_M4?4:1),"native spark weapon semantics");
        Check((gResultTarget-gAimSource-gAimDirection*range).Magnitude()<.0001f,"tracked range unchanged");
        Check(gFlagsDuringTrace&&!CWorld::bIncludeBikers&&!CWorld::bIncludeDeadPeds&&!CWorld::bIncludeCarTyres,"world query flags restored");
        Check(gLOSCalls==2,"fix adds no extra world trace");
    }
    Reset(heli);gun.m_eWeaponType=WEAPONTYPE_RUGER;gun.info.m_fRange=90;heli.position={0,60,5};
    gAimDirection=heli.position;gAimDirection.Normalise();
    for(int i=0;i<175;++i)Shoot(gun);
    Check(heli.m_nBulletDamage==700&&heli.m_heliStatus==HELI_STATUS_HOVER,"175 generic hits preserve original threshold");
    Shoot(gun);Check(heli.m_nBulletDamage==704&&heli.m_heliStatus==HELI_STATUS_SHOT_DOWN,"176th hit starts original shoot-down");
    Check(heli.m_nExplosionTimer==11000,"original explosion delay preserved");
    Reset(heli);heli.position={0,60,0};gHitEntity=&gWall;gHitPoint={0,20,0};
    for(int i=0;i<176;++i)Shoot(gun);
    Check(heli.m_nBulletDamage==0&&gSparks==0,"wall stops all special damage beyond cover");
    Check(gResultVictim==&gWall&&gOccupantCalls==176,"world hit remains available to normal damage");
    Reset(heli);heli.position={0,60,0};gHitEntity=&gWall;gHitPoint={0,80,0};Shoot(gun);
    Check(heli.m_nBulletDamage==4,"wall behind helicopter does not shield it");
    Reset(heli);heli.position={0,60,0};gHitEntity=&heli;gHitPoint={0,58,0};Shoot(gun);
    Check(heli.m_nBulletDamage==4&&gResultVictim==&heli,"direct model LOS also needs and receives special damage");
    for(CVector pos:{CVector(0,96,0),CVector(6,60,0),CVector(0,-6,0)}){
        Reset(heli);heli.position=pos;Shoot(gun);Check(heli.m_nBulletDamage==0,"native finite segment and five-metre proxy miss");
    }
    Reset(heli);heli.position={0,60,0};heli.bBulletProof=true;Shoot(gun);
    Check(heli.m_nBulletDamage==0,"mission bulletproof protection preserved");
    Reset(heli);heli.position={0,60,0};CVehicle own,ordinary;own.pDriver=&gPlayer;
    CWorld::pIgnoreEntity=&gWall;gPlayerCar=&own;gHitEntity=&ordinary;gHitPoint={0,20,0};Shoot(gun);
    Check(gIgnoreDuringTrace==&own&&CWorld::pIgnoreEntity==&gWall,"shooting from cabin preserves own-car ignore and outer state");
    Check(gResultVictim==&ordinary&&gOccupantCalls==1,"ordinary flyable vehicle keeps regular impact target");
    Check((gResultTraceSource-gAimSource-gAimDirection*.25f).Magnitude()<.0001f,"existing cabin tracer origin preserved");
    // Production CVehicle's empty virtual method is inherited by CHeli. The
    // source audit verifies InflictDamage reaches it and CHeli never overrides.
    Reset(heli);heli.health=0;heli.BlowUpCar(&gPlayer);
    Check(heli.m_heliStatus==HELI_STATUS_HOVER,"zero vehicle health and inherited explosion callback cannot shoot CHeli down");
    for(auto type:{WEAPONTYPE_M4,WEAPONTYPE_M60,WEAPONTYPE_HELICANNON}){
        Reset(heli);heli.position={0,60,0};gun.m_eWeaponType=type;
        TheCamera.PlayerWeaponMode.Mode=CCam::MODE_M16_1STPERSON;Shoot(gun);
        Check(gRoute==ROUTE_M16&&heli.m_nBulletDamage==(type==WEAPONTYPE_M4?4:20),"first-person native path retains single heavy damage callback");
    }
    for(auto type:{WEAPONTYPE_SHOTGUN,WEAPONTYPE_SPAS12_SHOTGUN,WEAPONTYPE_STUBBY_SHOTGUN}){
        Reset(heli);gun.m_eWeaponType=type;Shoot(gun);
        Check(gRoute==ROUTE_SHOTGUN&&heli.m_nBulletDamage==0,"shotgun retains separate native pellet route without added special hits");
    }
    for(auto type:{WEAPONTYPE_SNIPERRIFLE,WEAPONTYPE_LASERSCOPE}){
        Reset(heli);gun.m_eWeaponType=type;Shoot(gun);
        Check(gRoute==ROUTE_SNIPER&&heli.m_nBulletDamage==0,"sniper retains moving bullet route");
        heli.position={0,60,0};CVector source(-.43f,0,1.5f),target(-.43f,90,1.5f);
        Check(CHeli::TestSniperCollision(&source,&target)&&heli.m_heliStatus==HELI_STATUS_SHOT_DOWN,"native pilot sphere still brings helicopter down");
        Reset(heli);heli.position={0,60,0};heli.bBulletProof=true;
        Check(!CHeli::TestSniperCollision(&source,&target),"sniper respects native bulletproof mission protection");
    }
    Reset(heli);heli.position={0,60,0};CVector rocket(0,55,0);
    Check(CHeli::TestRocketCollision(&rocket)&&heli.m_heliStatus==HELI_STATUS_SHOT_DOWN,"native rocket route already worked independently");
    if(argc>1)ActualCol(argv[1]);
    std::printf("Helicopter native production regression: %d checks passed\n",checks);
}
