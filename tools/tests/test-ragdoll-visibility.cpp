#include "ragdoll-test-vector.h"
#include <cstdio>
#include <cstdlib>
#define VR_RAGDOLL_VEHICLE_NO_ENGINE
#include "VrRagdollSupport.h"
#define nil nullptr
#define Min(a,b) ((a)<(b)?(a):(b))
#define Max(a,b) ((a)>(b)?(a):(b))
#define sq(x) ((x)*(x))
#define Sqrt(x) std::sqrt(x)
#define __ANDROID__
#define RW_VULKAN
#include "VrRagdollBounds.h"
using int32=int;
using RwV3d=CVector;
struct CMatrix { CVector pos; CVector operator*(const CVector&v)const{return pos+v;} };
struct Sphere { CVector center; float radius=1.0f; };
struct ColModel { Sphere boundingSphere; };
struct ModelInfo { ColModel col; ColModel*GetColModel(){return &col;} };
namespace CModelInfo { static ModelInfo ordinary; static ModelInfo*GetModelInfo(int){return &ordinary;} }
struct CRect { float left,top,right,bottom; CRect(float l,float t,float r,float b):left(l),top(t),right(r),bottom(b){} };
struct CPed;
namespace VrRagdoll { bool GetBounds(CPed*,CVector&,float&); bool GetClumpBounds(void*,CVector&,float&); }
struct CEntity {
 bool pedKind=false; CMatrix m_matrix; int m_modelIndex=0;
 bool IsPed()const{return pedKind;} CVector GetPosition()const{return m_matrix.pos;}
 CVector GetBoundCentre(); void GetBoundCentre(CVector&); float GetBoundRadius(); bool GetIsOnScreen();
};
struct CPhysical:CEntity { CRect GetBoundRect(); };
struct RpClump { int alpha=255; };
struct EntryList { void*first=nil; };
struct CPed:CPhysical {
 EntryList m_entryInfoList; RpClump clump; int links=0; CVector lastCentre; float lastRadius=0;
 int lastSectors[6]={};
 CPed(){pedKind=true;m_entryInfoList.first=this;}
 RpClump*GetClump(){return &clump;}
 void RemoveAndAdd();
};
struct CVehicle:CPhysical {
 void*m_rwObject=this;bool bRemoveFromWorld=false;
 CVector right=CVector(1,0,0),forward=CVector(0,1,0),up=CVector(0,0,1);
 bool IsCar()const{return true;}
 CVector GetRight()const{return right;}CVector GetForward()const{return forward;}CVector GetUp()const{return up;}
};
namespace CPools {
 static CVehicle*vehicle=nil;static int currentHandle=71;
 static CVehicle*GetVehicle(int handle){return handle==currentHandle ? vehicle : nil;}
}
namespace CTimer {static float frameTime=1.0f/72.0f;static float GetTimeStepInSeconds(){return frameTime;}}
namespace CWorld {
 static int Index(float value){const int i=int((value+1000.0f)/50.0f);return i<0?0:i>39?39:i;}
 static int GetClampedSectorIndexX(float v){return Index(v);}
 static int GetClampedSectorIndexY(float v){return Index(v);}
 static bool los=true;static CVector lastLosTarget;static int losCalls;
 static bool GetIsLineOfSightClear(const CVector&,const CVector&to,bool,bool,bool,bool,bool,bool){++losCalls;lastLosTarget=to;return los;}
}
struct Camera {
 CVector pos,forward=CVector(1,0,0),lastSphereCentre;float lastSphereRadius=0;CMatrix matrix;bool forceHidden=false;
 const CVector&GetPosition()const{return pos;} const CVector&GetForward()const{return forward;} CMatrix&GetCameraMatrix(){return matrix;}
 bool IsSphereVisible(CVector centre,float r,CMatrix* =nil){
  lastSphereCentre=centre;lastSphereRadius=r;if(forceHidden)return false;
  const CVector relative=centre-pos;const float along=DotProduct(relative,forward);
  return along+r>0 && (relative-forward*along).Magnitude()<Max(0.0f,along)+r;
 }
} TheCamera;
static const float VR_PED_OBSERVABLE_RANGE=100,VR_PED_HEAD_CONE_DOT=.2f;
namespace FixturePhysics { static const float radius[19]={.14f,.16f,.17f,.085f,.13f,.09f,.085f,.07f,.055f,.09f,.085f,.07f,.055f,.11f,.085f,.075f,.11f,.085f,.075f}; }
namespace VrRagdoll {
 static constexpr int MAX_POSES=220,SIM_BONES=19,RD_PELVIS=0;
 struct Slot {CPed*ped=nil;CVector pos[19];VrRagdollBounds::Bounds bounds;VrRagdollSupport::State support;bool boundsLinked=false,asleep=true,live=false;int linkedSectors[6]={};};
 static Slot slots[MAX_POSES];static int active;static bool enabled=true;
 using namespace FixturePhysics;
 static void ForgetAnimatedPose(CPed*){}
}
struct RwFrame { CMatrix matrix; };
struct RpAtomic { RwFrame frame; RpClump*clump=nil;int draws=0,alphaDraws=0,lastAlpha=-1; };
static RwFrame*RpAtomicGetFrame(RpAtomic*a){return &a->frame;}
static CMatrix*RwFrameGetLTM(RwFrame*f){return &f->matrix;}
static RpClump*RpAtomicGetClump(RpAtomic*a){return a->clump;}
static void RwV3dSub(RwV3d*out,const RwV3d*a,const RwV3d*b){*out=*a-*b;}
static float RwV3dDotProduct(const RwV3d*a,const RwV3d*b){return DotProduct(*a,*b);}
#define RENDERCALLBACK(a) (++(a)->draws)
struct CVisibilityPlugins {
 static CVector cameraPosition;static RwV3d*ms_pCameraPosn;static float ms_pedLod1Dist;
 static int GetClumpAlpha(RpClump*c){return c->alpha;}
 static void RenderAlphaAtomic(RpAtomic*a,int alpha){++a->alphaDraws;a->lastAlpha=alpha;}
 static RpAtomic*RenderPedCB(RpAtomic*);
};
CVector CVisibilityPlugins::cameraPosition;
RwV3d*CVisibilityPlugins::ms_pCameraPosn=&CVisibilityPlugins::cameraPosition;
float CVisibilityPlugins::ms_pedLod1Dist=400;
#pragma warning(push)
// The engine namespace imports the joint radius array; public getters also
// use radius as their output argument. This deliberate name overlap is benign.
#pragma warning(disable:4459)
#include "ragdoll-visibility-production.inc"
#pragma warning(pop)
void CPed::RemoveAndAdd(){
 ++links;const CRect rect=GetBoundRect();GetBoundCentre(lastCentre);lastRadius=GetBoundRadius();
 lastSectors[0]=CWorld::GetClampedSectorIndexX(rect.left);lastSectors[1]=CWorld::GetClampedSectorIndexX(rect.right);lastSectors[2]=CWorld::GetClampedSectorIndexX((rect.left+rect.right)*.5f);
 lastSectors[3]=CWorld::GetClampedSectorIndexY(rect.top);lastSectors[4]=CWorld::GetClampedSectorIndexY(rect.bottom);lastSectors[5]=CWorld::GetClampedSectorIndexY((rect.top+rect.bottom)*.5f);
}
using namespace VrRagdoll;
static unsigned checks;
static void Check(bool okay,const char*message){++checks;if(!okay){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static bool Same(const CVector&a,const CVector&b,float tolerance=.00002f){return (a-b).Magnitude()<tolerance;}
static void Reset(){for(auto&s:slots)s=Slot();active=0;TheCamera=Camera();CWorld::los=true;CWorld::losCalls=0;CModelInfo::ordinary=ModelInfo();CVisibilityPlugins::cameraPosition=CVector();CPools::vehicle=nil;CPools::currentHandle=71;CTimer::frameTime=1.f/72.f;}
static Slot*Admit(CPed&ped,int index,CVector centre){
 Slot*s=&slots[index];*s=Slot();s->ped=&ped;
 for(int i=0;i<19;i++)s->pos[i]=centre+CVector(float(i%3)*.14f-.14f,float(i%5)*.12f-.24f,float(i%7)*.12f-.36f);
 RefreshBounds(s);++active;return s;
}
int main(){
 Reset();
 // Conservative sphere contains every joint sphere plus the skin extension,
 // including stretched transient poses and arbitrary world translations.
 for(int trial=0;trial<128;trial++){
  CVector p[19],translated[19];const CVector shift(100.3f,-220.5f,9.1f);
  for(int i=0;i<19;i++){p[i]=CVector(std::sin(float(i+trial))*.9f,std::cos(float(i*3-trial))*.7f,float(i%7)*.15f);translated[i]=p[i]+shift;}
  const auto b=VrRagdollBounds::Calculate(p,radius,19),moved=VrRagdollBounds::Calculate(translated,radius,19);
  Check(Same(moved.centre,b.centre+shift,.00005f)&&std::fabs(b.radius-moved.radius)<.00005f,"bounds follow translation independently of entity root");
  for(int i=0;i<19;i++)Check((p[i]-b.centre).Magnitude()+radius[i]+.18f<=b.radius+.00001f,"sphere encloses joints plus fingers/toes/clothing margin");
 }
 CPed ped;ped.m_matrix.pos=CVector(-100,0,0);Slot*s=Admit(ped,0,CVector(10,0,0));
 CVector centre;float boundRadius=0;
 Check(GetBounds(&ped,centre,boundRadius)&&Same(centre,s->bounds.centre),"owned entity resolves cached world skeleton bounds");
 Check(GetClumpBounds(ped.GetClump(),centre,boundRadius)&&Same(centre,s->bounds.centre),"atomic clump resolves same world skeleton bounds");
 Check(Same(ped.GetBoundCentre(),centre)&&ped.GetBoundRadius()==boundRadius,"production entity getters override stale root only while owned");
 Check(ped.GetIsOnScreen(),"actual body in view survives entity frustum check while old root is behind viewer");
 TheCamera.forward=CVector(-1,0,0);Check(!ped.GetIsOnScreen(),"ragdoll still culls when body is behind viewer rather than disabling culling");TheCamera.forward=CVector(1,0,0);
 CPed ordinary;ordinary.m_matrix.pos=CVector(100,0,0);CModelInfo::ordinary.col.boundingSphere.center=CVector(0,0,1);
 Check(Same(ordinary.GetBoundCentre(),CVector(100,0,1))&&ordinary.GetBoundRadius()==1,"unowned ped retains original model bounds");
 CEntity object;object.m_matrix.pos=CVector(200,0,0);Check(Same(object.GetBoundCentre(),CVector(200,0,1))&&object.GetBoundRadius()==1,"non-peds retain original bounds");
 Check(!GetBounds(nil,centre,boundRadius)&&!GetClumpBounds(nil,centre,boundRadius),"null lookup cannot return unused slot data");
 Check(Same(VrPedSpatialPosition(&ped),s->bounds.centre)&&Same(VrPedSpatialPosition(&ordinary),ordinary.GetPosition()),"population uses ragdoll position and leaves ordinary ped root unchanged");
 // Broad phase links use exact world-centred sphere, with no root mutation.
 const CVector root=ped.GetPosition();LinkBounds(s);Check(ped.links==1&&s->boundsLinked&&Same(ped.lastCentre,s->bounds.centre),"first deferred link uses owned body sphere");
 const int initialLinks=ped.links;for(int i=0;i<120;i++)LinkBounds(s);Check(ped.links==initialLinks,"unchanged sector footprint causes no repeated linked-list churn");
 s->bounds.centre.x+=.05f;LinkBounds(s);Check(ped.links==initialLinks,"within-sector body motion does not relink");
 s->bounds.centre.x+=100;LinkBounds(s);Check(ped.links==initialLinks+1,"travelling corpse links destination sectors");
 for(int i=0;i<6;i++)Check(ped.lastSectors[i]==s->linkedSectors[i],"cached footprint agrees with production physical bound rectangle");
 Check(Same(ped.GetPosition(),root),"visibility relinking does not teleport native ped root");
 // A midpoint crosses a boundary while min/max sectors stay the same.
 s->bounds.centre=CVector(-.01f,10,0);s->bounds.radius=2;LinkBounds(s);const int midpointLinks=ped.links;const int oldLeft=s->linkedSectors[0],oldRight=s->linkedSectors[1];
 s->bounds.centre.x=.01f;LinkBounds(s);Check(ped.links==midpointLinks+1&&s->linkedSectors[0]==oldLeft&&s->linkedSectors[1]==oldRight,"primary/overlap sector role changes trigger relink at midpoint");
 ped.m_entryInfoList.first=nil;LinkBounds(s);Check(!s->boundsLinked&&ped.links==midpointLinks+1,"removed entity is not reinserted by ragdoll update");
 ped.m_entryInfoList.first=&ped;LinkBounds(s);Check(s->boundsLinked&&ped.links==midpointLinks+2,"re-entered entity refreshes its sector cache");
 // Atomic LOD follows the moving body's sphere and preserves the normal
 // alpha-render callback; it never uses the abandoned animation root.
 RpAtomic atomic;atomic.clump=ped.GetClump();atomic.frame.matrix.pos=root;s->bounds.centre=CVector(10,0,0);s->bounds.radius=1;
 CVisibilityPlugins::RenderPedCB(&atomic);Check(atomic.draws==1,"nearby ragdoll draws when atomic animation root is beyond LOD range");
 s->bounds.centre=CVector(20.5f,0,0);CVisibilityPlugins::RenderPedCB(&atomic);Check(atomic.draws==2,"sphere edge remains visible across LOD centre boundary");
 s->bounds.centre=CVector(21.1f,0,0);CVisibilityPlugins::RenderPedCB(&atomic);Check(atomic.draws==2,"body beyond radius-expanded LOD still culls");
 s->bounds.centre=CVector(10,0,0);ped.clump.alpha=100;CVisibilityPlugins::RenderPedCB(&atomic);Check(atomic.alphaDraws==1&&atomic.lastAlpha==100,"corpse alpha/fade rendering remains intact");
 RpAtomic ordinaryAtomic;ordinaryAtomic.clump=ordinary.GetClump();ordinaryAtomic.frame.matrix.pos=CVector(25,0,0);CVisibilityPlugins::RenderPedCB(&ordinaryAtomic);Check(ordinaryAtomic.draws==0,"ordinary ped atomic keeps original LOD limit");
 // Observable body fallback samples the actual low corpse centre, with no
 // standing-ped head-height offset accidentally placing its LOS over a wall.
 TheCamera.forceHidden=true;s->bounds.centre=CVector(20,0,.2f);s->bounds.radius=1;CWorld::los=false;
 Check(!IsVrPedObservable(&ped)&&Same(CWorld::lastLosTarget,s->bounds.centre),"hidden ragdoll observable test uses corpse centre without standing height offset");
 CWorld::los=true;Check(IsVrPedObservable(&ped),"head-facing ragdoll remains observable even outside mono frustum");
 // Selected simulation records rebuild bounds once; deferred and sleeping
 // owners retain their cache and are still linked, including beyond slot30.
 Reset();CPed saved;Slot*savedSlot=Admit(saved,219,CVector(200,20,0));savedSlot->bounds.centre=CVector(15,5,1);const auto savedBounds=savedSlot->bounds;
 Slot*sim=Admit(ped,0,CVector(10,0,0));sim->pos[0].x+=4;const auto expected=VrRagdollBounds::Calculate(sim->pos,radius,19);
 RefreshBounds(sim);
 ProductionFinishBoundsUpdate();Check(Same(sim->bounds.centre,expected.centre)&&Same(savedSlot->bounds.centre,savedBounds.centre),"update refreshes simulated bodies and preserves saved pose cache");
 Check(saved.links==1&&GetClumpBounds(saved.GetClump(),centre,boundRadius)&&Same(centre,savedBounds.centre),"saved corpse remains linked and queryable");
 // Exact live-getup block must remove ownership before calling native relink,
 // otherwise the body can remain stranded in old ragdoll sectors after getup.
 const int beforeGetup=ped.links;ProductionLiveGetup(sim);Check(sim->ped==nil&&active==1&&ped.links==beforeGetup+1,"surviving getup releases and relinks exactly once");
 Check(Same(ped.lastCentre,ped.GetPosition()+CModelInfo::ordinary.col.boundingSphere.center),"getup relink sees restored ordinary model bounds");
 const int beforeRelease=saved.links;Release(&saved);Check(active==0&&!GetBounds(&saved,centre,boundRadius)&&saved.links==beforeRelease,"destructor release clears ownership without reinserting dying entity");
 // Real production LinkBounds -> RemoveAndAdd -> GetBoundRect must use
 // the SAME padded presentation sphere as the cached footprint. The old
 // provisional code padded only its cache and inserted smaller geometry.
 Reset();CPed carried;Slot*ride=Admit(carried,0,CVector(49.2f,20,1));
 CVehicle car;car.m_matrix.pos=CVector(49,20,0);CPools::vehicle=&car;
 ride->support.handle=71;ride->support.age=1.f/90.f;ride->support.speed=30;ride->support.turnSpeed=0;
 ride->support.solved.origin=car.GetPosition();ride->support.solved.right=car.GetRight();
 ride->support.solved.forward=car.GetForward();ride->support.solved.up=car.GetUp();
 const auto rawBounds=ride->bounds;const CVector rawPoint=ride->pos[0];
 LinkBounds(ride);
 Check(carried.lastRadius>rawBounds.radius+.8f,"actual sector insertion receives predictive padding, not just cached sector indices");
 for(int i=0;i<6;++i)Check(carried.lastSectors[i]==ride->linkedSectors[i],"supported-body cached and actual inserted footprints match");
 const int initialRight=carried.lastSectors[1];
 car.m_matrix.pos.x+=.3f;
 const auto shown=Presentation(ride);Check(shown.active,"current live car enables a bounded presentation transform");
 Check(GetBounds(&carried,centre,boundRadius)&&Same(centre,rawBounds.centre+CVector(.3f,0,0)),"entity bounds follow displayed support translation");
 CVector clumpCentre;float clumpRadius;
 Check(GetClumpBounds(carried.GetClump(),clumpCentre,clumpRadius)&&Same(clumpCentre,centre)&&clumpRadius==boundRadius,"entity and atomic culling agree on presentation bounds");
 for(int i=0;i<19;++i){
  const CVector point=shown.Point(ride->pos[i]);
  Check((point-centre).Magnitude()+radius[i]<boundRadius,"padded displayed sphere contains every transported joint");
  Check(CWorld::GetClampedSectorIndexX(point.x+radius[i])<=initialRight,"pre-world inserted sector envelope contains completed-frame presentation");
 }
 Check(Same(ride->bounds.centre,rawBounds.centre)&&Same(ride->pos[0],rawPoint),"presentation/bounds queries never move solver state");
 LinkBounds(ride);for(int i=0;i<6;++i)Check(carried.lastSectors[i]==ride->linkedSectors[i],"relinked rendered footprint remains identical to actual sector rectangle");
 CPools::currentHandle=72;
 Check(!Presentation(ride).active&&GetBounds(&carried,centre,boundRadius)&&Same(centre,rawBounds.centre),"recycled vehicle handle cannot carry old corpse presentation");
 CPools::currentHandle=71;car.bRemoveFromWorld=true;Check(!Presentation(ride).active,"removed support does not transport the body");
 car.bRemoveFromWorld=false;ride->support.age=.11f;Check(!Presentation(ride).active&&GetBounds(&carried,centre,boundRadius)&&boundRadius==rawBounds.radius,"expired support drops transport and extra footprint");
 std::printf("ragdoll visibility production: %u checks passed (geometry, entity, sectors, saved poses, LOD, cleanup/getup)\n",checks);
}
