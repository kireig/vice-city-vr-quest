#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include <initializer_list>
static unsigned long long allocationCalls;
void*operator new(std::size_t bytes){++allocationCalls;void*p=std::malloc(bytes?bytes:1);if(!p)throw std::bad_alloc();return p;}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
void*operator new[](std::size_t bytes){return ::operator new(bytes);}
void operator delete[](void*p)noexcept{std::free(p);}
void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
using uint8=uint8_t;using uint16=uint16_t;using uint32=uint32_t;using int32=int32_t;
static unsigned checks;
static void Check(bool value,const char*why){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static unsigned poolLookups,clumpWalks,atomicVisits,geometryCreates,geometryAssignments,geometryLocks,materialAppends,matrixReads;
static int aliveGeometry;
struct RwV3d{float x=0,y=0,z=0;};
struct RwRGBA{uint8 red=100,green=110,blue=120,alpha=255;};
struct RwTexCoords{float u=0,v=0;};
struct RwMatrix{RwV3d right={1,0,0},up={0,1,0},at={0,0,1},pos;};
struct RwObject{int type;explicit RwObject(int t):type(t){}};
enum {rpATOMIC=1,rpCLUMP=2,rpATOMICRENDER=4,ATOMIC_FLAG_WINDSCREEN=8};
struct RwFrame{RwMatrix matrix;RwFrame*parent=nullptr;std::string name="chassis";};
struct RwTexture{std::string name;};
static const char*RwTextureGetName(RwTexture*t){return t->name.c_str();}
struct RpMaterial {
 int refs=1;RwRGBA color;RwTexture*texture=nullptr;
 void addRef(){Check(refs>0,"material addRef cannot touch a dead material");++refs;}
 void destroy(){Check(refs>0,"material reference cannot underflow");--refs;}
};
namespace rw {
struct Sphere{RwV3d center;float radius=1;};
struct Triangle{uint16 v[3]={};uint16 matId=0;};
struct MaterialList {
 int numMaterials=0;std::vector<RpMaterial*> storage;RpMaterial**materials=nullptr;
 // Matches native geometry.cpp: append takes one ref, deinit releases each.
 int appendMaterial(RpMaterial*m){++materialAppends;storage.push_back(m);materials=storage.data();m->addRef();return numMaterials++;}
 void deinit(){for(auto*m:storage)m->destroy();storage.clear();materials=nullptr;numMaterials=0;}
};
struct Mesh{uint32 numIndices=0;RpMaterial*material=nullptr;uint16*indices=nullptr;};
struct MeshHeader{
 enum {TRISTRIP=1};uint32 flags=0,numMeshes=0,totalIndices=0;
 std::vector<Mesh> meshes;std::vector<uint16> data;
 Mesh*getMeshes(){return meshes.data();}
 const Mesh*getMeshes()const{return meshes.data();}
 void setupIndices(){uint16*p=data.data();for(auto&m:meshes){m.indices=p;p+=m.numIndices;}Check(p==data.data()+data.size(),"mesh material ranges exactly partition all indices");}
};
struct Morph{RwV3d*vertices=nullptr,*normals=nullptr;Sphere boundingSphere;};
struct Geometry {
 enum {TRISTRIP=1,PRELIT=8,NORMALS=16,NATIVE=0x1000000,NATIVEINSTANCE=0x2000000};
 uint32 flags;int numMorphTargets=1,numVertices,numTriangles,numTexCoordSets,refCount=1;bool alive=true,skin=false;
 MaterialList matList;Morph morphTargets[1];Triangle*triangles;RwRGBA*colors=nullptr;RwTexCoords*texCoords[8]={};MeshHeader*meshHeader=nullptr;
 std::vector<RwV3d> v,n;std::vector<Triangle> t;std::vector<RwRGBA> c;std::vector<RwTexCoords> uv[8];std::unique_ptr<MeshHeader> header;
 Geometry(int nv,int nt,uint32 f):flags(f),numVertices(nv),numTriangles(nt),numTexCoordSets((f>>16)&255),v(nv),t(nt){
  ++aliveGeometry;morphTargets[0].vertices=v.data();triangles=t.data();
  if(f&NORMALS){n.resize(nv);morphTargets[0].normals=n.data();}
  if(f&PRELIT){c.resize(nv);colors=c.data();}
  for(int i=0;i<numTexCoordSets;++i){uv[i].resize(nv);texCoords[i]=uv[i].data();}
 }
 static Geometry*create(int nv,int nt,uint32 flags);
 void addRef(){Check(alive&&refCount>0,"geometry addRef cannot touch a dead mesh");++refCount;}
 void destroy(){Check(alive&&refCount>0,"geometry reference cannot underflow or access freed mesh");if(--refCount==0){alive=false;--aliveGeometry;matList.deinit();}}
 void lock(int){++geometryLocks;Check(alive,"lock requires live geometry");}
 void unlock(){++geometryLocks;Check(alive,"unlock requires live geometry");}
 MeshHeader*allocateMeshes(int nmesh,uint32 indices,bool noIndices){Check(alive&&!noIndices,"deformation uses explicit triangle indices");header.reset(new MeshHeader);header->numMeshes=uint32(nmesh);header->totalIndices=indices;header->meshes.resize(nmesh);header->data.resize(indices);for(auto&m:header->meshes)m.indices=header->data.data();meshHeader=header.get();return meshHeader;}
};
static std::vector<std::unique_ptr<Geometry>> tombstones;
Geometry*Geometry::create(int nv,int nt,uint32 flags){++geometryCreates;tombstones.emplace_back(new Geometry(nv,nt,flags));return tombstones.back().get();}
struct Skin{static void*get(Geometry*g){Check(g->alive,"skin query cannot touch freed geometry");return g->skin?g:nullptr;}};
}
using RpGeometry=rw::Geometry;
struct RpAtomic: RwObject {
 RpGeometry*geometry=nullptr;RwFrame frame;uint32 flags=rpATOMICRENDER,id=0;
 RpAtomic():RwObject(rpATOMIC){}
 // Matches native clump.cpp: old ref released first, new ref acquired second.
 void setGeometry(RpGeometry*g,uint32){++geometryAssignments;if(geometry)geometry->destroy();if(g)g->addRef();geometry=g;}
};
struct RpClump: RwObject{RwFrame frame;std::vector<RpAtomic*>atomics;bool alive=true;RpClump():RwObject(rpCLUMP){}};
static int RwObjectGetType(RwObject*o){return o->type;}
static uint32 RpAtomicGetFlags(RpAtomic*a){return a->flags;}
static RwFrame*RpAtomicGetFrame(RpAtomic*a){return &a->frame;}
static RwFrame*RwFrameGetParent(RwFrame*f){return f->parent;}
static const char*GetFrameNodeName(RwFrame*f){return f->name.c_str();}
static RwMatrix*RwFrameGetLTM(RwFrame*f){++matrixReads;return &f->matrix;}
static RwFrame*RpClumpGetFrame(RpClump*c){Check(c->alive,"clump frame cannot be read after destruction");return &c->frame;}
using AtomicCallback=RpAtomic*(*)(RpAtomic*,void*);
static void RpClumpForAllAtomics(RpClump*c,AtomicCallback callback,void*data){++clumpWalks;Check(c->alive,"mesh walk cannot touch a destroyed clump");for(auto*a:c->atomics){++atomicVisits;Check(a->geometry&&a->geometry->alive,"atomic cannot retain dead geometry");callback(a,data);}}
class CVector{public:float x,y,z;CVector(float a=0,float b=0,float c=0):x(a),y(b),z(c){}CVector operator-(const CVector&b)const{return CVector(x-b.x,y-b.y,z-b.z);}float MagnitudeSqr()const{return x*x+y*y+z*z;}};
class CMatrix{public:CVector right={1,0,0},forward={0,1,0},up={0,0,1},pos;const CVector&GetRight()const{return right;}const CVector&GetForward()const{return forward;}const CVector&GetUp()const{return up;}const CVector&GetPosition()const{return pos;}};
enum{ENTITY_TYPE_NOTHING,ENTITY_TYPE_BUILDING,ENTITY_TYPE_OBJECT,ENTITY_TYPE_VEHICLE,ENTITY_TYPE_PED};
class CEntity{public:RwObject*m_rwObject=nullptr;int type=ENTITY_TYPE_NOTHING;int GetType()const{return type;}};
class CVehicle:public CEntity{public:
 CMatrix matrix;void*pHandling=reinterpret_cast<void*>(1);bool car=true,heli=false,plane=false,bRemoveFromWorld=false;float m_fMass=1000;int handle=0,model=0;
 CVehicle(){type=ENTITY_TYPE_VEHICLE;}bool IsCar()const{return car;}bool IsRealHeli()const{return heli;}bool IsRealPlane()const{return plane;}
 RpClump*GetClump(){return static_cast<RpClump*>(m_rwObject);}const CVector&GetPosition()const{return matrix.pos;}const CMatrix&GetMatrix()const{++matrixReads;return matrix;}int GetModelIndex()const{return model;}
};
struct Camera{CVector position;const CVector&GetPosition()const{return position;}}TheCamera;
enum{NUM_FIRST_MATERIALS=8,NUM_SECOND_MATERIALS=8};
struct CVehicleModelInfo{RpMaterial*m_materials1[NUM_FIRST_MATERIALS]={},*m_materials2[NUM_SECOND_MATERIALS]={};};
static CVehicleModelInfo modelInfo;
namespace CModelInfo{static CVehicleModelInfo*GetModelInfo(int){return &modelInfo;}}
namespace CVisibilityPlugins{static uint32 GetAtomicId(RpAtomic*a){return a->id;}}
namespace CPools{static std::map<int,CVehicle*>pool;static CVehicle*GetVehicle(int32 id){++poolLookups;auto it=pool.find(id);return it==pool.end()?nullptr:it->second;}static int32 GetVehicleRef(CVehicle*v){++poolLookups;return v->handle;}}
struct CTimer{static uint32 frame,time;static uint32 GetFrameCounter(){return frame;}static uint32 GetTimeInMilliseconds(){return time;}};
uint32 CTimer::frame=1,CTimer::time=1000;
#include "vehicle-deformation-runtime-production.inc"
namespace D=VehicleDeformation;
static void ClearCounters(){poolLookups=clumpWalks=atomicVisits=geometryCreates=geometryAssignments=geometryLocks=materialAppends=matrixReads=0;allocationCalls=0;}
static void ListHeader(RpGeometry*g){
 auto*h=g->allocateMeshes(g->matList.numMaterials,uint32(g->numTriangles*3),false);
 for(int i=0;i<g->matList.numMaterials;++i)h->meshes[i].material=g->matList.materials[i];
 for(const auto&t:g->t)h->meshes[t.matId].numIndices+=3;
 h->setupIndices();std::vector<unsigned> offsets(g->matList.numMaterials);
 for(const auto&t:g->t)for(uint16 index:t.v)h->meshes[t.matId].indices[offsets[t.matId]++]=index;
}
static void DenseSkinny(RpGeometry*g){
 // A genuine thin authored face whose in-plane dent remains inverted after
 // eight half-steps. More than 4000 vertices keeps the original topology.
 g->numVertices=4001;g->v.resize(4001);g->n.resize(4001);g->c.resize(4001);g->uv[0].resize(4001);
 g->morphTargets[0].vertices=g->v.data();g->morphTargets[0].normals=g->n.data();g->colors=g->c.data();g->texCoords[0]=g->uv[0].data();
 g->v[0]={-.4f,0,0};g->v[1]={.4f,0,0};g->v[2]={0,1.0e-6f,0};
 for(auto&n:g->n)n={0,0,1};g->t[0]={{0,1,2},0};g->t[1]={{0,1,2},0};ListHeader(g);
}
struct Scene {
 RpMaterial paint,glass;RpGeometry*original;CVehicle cars[2];RpClump clumps[2];RpAtomic atomics[2];CEntity wall;
 Scene(){
  D::SetVehicleDeformationEnabled(false);Check(aliveGeometry==0,"previous scene released every native/private geometry reference");
  CPools::pool.clear();CTimer::frame+=10;CTimer::time+=1000;TheCamera.position=CVector();wall.type=ENTITY_TYPE_BUILDING;
  modelInfo=CVehicleModelInfo();modelInfo.m_materials1[0]=&paint;glass.color.alpha=100;
  original=RpGeometry::create(4,2,RpGeometry::NORMALS|RpGeometry::PRELIT|(1u<<16));original->matList.appendMaterial(&paint);
  original->v={{-.4f,-.4f,0},{.4f,-.4f,0},{.4f,.4f,0},{-.4f,.4f,0}};original->morphTargets[0].vertices=original->v.data();
  original->t[0]={{0,1,2},0};original->t[1]={{0,2,3},0};for(auto&n:original->n)n={0,0,1};original->morphTargets[0].boundingSphere.radius=.6f;
  ListHeader(original);
  for(int i=0;i<2;++i){cars[i].handle=100+i;cars[i].m_rwObject=&clumps[i];clumps[i].atomics.push_back(&atomics[i]);atomics[i].frame.parent=&clumps[i].frame;atomics[i].setGeometry(original,0);CPools::pool[cars[i].handle]=&cars[i];}
 }
 ~Scene(){D::SetVehicleDeformationEnabled(false);for(int i=0;i<2;++i){D::Release(&cars[i]);atomics[i].setGeometry(nullptr,0);clumps[i].alive=false;cars[i].m_rwObject=nullptr;}original->destroy();Check(aliveGeometry==0&&paint.refs==1&&glass.refs==1,"scene teardown balances all geometry and material ownership");CPools::pool.clear();}
 void Hit(int car=0,float impulse=400,CVector point=CVector()){D::RecordCollision(&cars[car],point,CVector(0,0,-1),impulse,&wall);}
 void Frame(){
  ++CTimer::frame;CTimer::time+=16;const unsigned assignments=geometryAssignments;D::Process(&cars[0]);
  const unsigned phase=D::job.phase,cursor=D::job.cursor,passes=D::job.passes,backtracks=D::job.backtracks;
  const auto*state=D::job.state;const auto*source=D::job.source;const auto*result=D::job.result;
  const bool worked=D::workFrame==CTimer::frame;
  const auto allocations=allocationCalls;const auto walks=clumpWalks;
  for(int pass=0;pass<4;++pass)for(auto&car:cars)D::Process(&car);
  Check(geometryAssignments-assignments<=1,"actual Process commits at most one atomic assignment in a frame");
  // If the first vehicle had no pending work, the second may legitimately
  // begin the global job; otherwise repeated eyes/passes must do no more work.
  if(worked){
   Check(D::job.state==state&&D::job.source==source&&D::job.result==result&&D::job.phase==phase&&D::job.cursor==cursor&&D::job.passes==passes&&D::job.backtracks==backtracks,
    "repeated PreRender calls cannot advance the same job twice in one frame");
   Check(allocationCalls==allocations&&clumpWalks==walks,"repeated eyes perform no extra job allocation or geometry walk");
  }
 }
 void Finish(int maximum=160){for(int i=0;i<maximum;++i)Frame();}
};
static void DisabledAndFreshRelease(){
 Scene scene;ClearCounters();for(int i=0;i<1000000;++i){D::RecordCollision(&scene.cars[0],CVector(),CVector(0,0,-1),400,&scene.wall);D::Process(&scene.cars[0]);D::PrepareDetachedAtomic(&scene.atomics[0]);D::SetVehicleDeformationEnabled(false);}
 Check(!allocationCalls&&!poolLookups&&!clumpWalks&&!atomicVisits&&!geometryCreates&&!geometryAssignments&&!geometryLocks&&!materialAppends&&!matrixReads,"four million OFF calls perform no allocations, lookup, mesh walk, copy, assignment or lock");
 CVehicle constructor;constructor.pHandling=nullptr;CEntity base;D::Release(&constructor);D::Release(&base);D::Release(nullptr);
 Check(poolLookups==0&&clumpWalks==0,"fresh constructor and base/null release never read derived/pool/clump data");
}
static void IsolationRepairDetach(){
 Scene s;D::SetVehicleDeformationEnabled(true);s.Hit();s.Finish();RpGeometry*dent=s.atomics[0].geometry;
 Check(dent!=s.original&&dent->alive&&s.atomics[1].geometry==s.original,"only the hit car receives a live private geometry");
 Check(s.original->numVertices==4&&s.original->v[0].z==0&&s.original->v[3].z==0,"native shared mesh vertices remain byte-stable");
 bool moved=false;for(auto v:dent->v)moved|=v.z<-.001f;Check(moved,"actual production mesh job dents real output vertices");
 Check(dent->matList.materials[0]==&s.paint&&s.paint.refs==3,"private mesh shares the exact native paint pointer and holds one material ref");
 s.paint.color.red=237;Check(dent->matList.materials[0]->color.red==237&&s.original->matList.materials[0]->color.red==237,"native recoloring reaches both shared material users");
 Check(dent->meshHeader&&dent->meshHeader->data.size()==size_t(dent->numTriangles*3),"staged job commits a complete explicit native mesh header");
 RpAtomic detached;detached.setGeometry(dent,0);D::PrepareDetachedAtomic(&detached);Check(detached.geometry==s.original,"detached debris immediately receives the original native mesh");detached.setGeometry(nullptr,0);
 D::Release(&s.cars[0]);Check(s.atomics[0].geometry==s.original&&!dent->alive&&D::privateVertices==0&&D::privateBytes==0,"repair/delete release restores original and frees every private mesh");
 s.Hit();s.Finish();dent=s.atomics[0].geometry;Check(dent!=s.original,"repaired car can receive a later dent");D::SetVehicleDeformationEnabled(false);
 Check(s.atomics[0].geometry==s.original&&!dent->alive&&D::privateTriangles==0&&aliveGeometry==1,"ON to OFF restores all originals and releases private geometry");
 ClearCounters();for(int i=0;i<100;++i)D::SetVehicleDeformationEnabled(false);Check(!allocationCalls&&!clumpWalks&&!poolLookups&&!geometryAssignments,"stable OFF never repeats transition cleanup");
}
static void PendingSelectionAndVisibility(){
 Scene s;D::SetVehicleDeformationEnabled(true);s.Hit(0,400,CVector(.1f,0,0));s.Hit(0,800,CVector(.2f,0,0));s.Hit(0,800,CVector(.3f,0,0));
 auto*state=D::Find(&s.cars[0]);Check(state&&std::fabs(state->pending.point.x-.2f)<.0001f,"queue keeps strongest raw impulse after depth saturation and earliest equal-strength contact");
 D::Process(&s.cars[0]);s.cars[0].matrix.pos=CVector(100,0,0);s.Hit(1);s.Finish();
 Check(s.atomics[1].geometry!=s.original,"offscreen job owner cannot block another visible car's pending dent");
}
static void CancelAndReuse(){
 for(bool viaOff:{false,true})for(unsigned phase:{0u,1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u}){
  Scene s;if(phase==5)DenseSkinny(s.original);D::SetVehicleDeformationEnabled(true);
  if(phase==5)D::RecordCollision(&s.cars[0],CVector(),CVector(0,-1,0),400,&s.wall);else s.Hit();
  // Pause the actual production phase functions at each resource boundary.
  // Process may legally fold several cheap phases into one engine frame.
  D::BeginBatch(*D::Find(&s.cars[0]),&s.cars[0]);D::BeginCandidate();
  for(int i=0;i<80&&D::job.state&&D::job.phase<phase;++i){
   unsigned vertices=0,triangles=0;bool subdivided=false,committed=false;
   D::AdvanceJob(vertices,triangles,subdivided,committed);
  }
  Check(D::job.state&&D::job.phase==phase,"cancel fixture reaches the intended actual source/result phase");
  if(viaOff)D::SetVehicleDeformationEnabled(false);else D::Release(&s.cars[0]);
  Check(!D::job.state&&!D::batch.state&&s.atomics[0].geometry==s.original&&aliveGeometry==1&&D::privateBytes==0,"release cancels an in-flight source/result job without leaking refs");
 }
 {
  Scene s;D::SetVehicleDeformationEnabled(true);s.Hit();D::Process(&s.cars[0]);s.cars[0].m_rwObject=nullptr;ClearCounters();
  D::Release(static_cast<CEntity*>(&s.cars[0]));
  Check(!D::job.state&&poolLookups==0&&clumpWalks==0&&aliveGeometry==1,"base-entity release safely cancels an owner whose current clump is already null");
  s.cars[0].m_rwObject=&s.clumps[0];
 }
 Scene s;D::SetVehicleDeformationEnabled(true);s.Hit();s.Finish();RpGeometry*oldDent=s.atomics[0].geometry;
 // Exercise the defensive new-clump path after a legacy external destruction.
 s.atomics[0].setGeometry(nullptr,0);s.clumps[0].alive=false;
 RpClump replacement;RpAtomic newAtomic;newAtomic.setGeometry(s.original,0);newAtomic.frame.parent=&replacement.frame;replacement.atomics.push_back(&newAtomic);
 s.cars[0].m_rwObject=&replacement;s.cars[0].handle+=100;CPools::pool.erase(100);CPools::pool[s.cars[0].handle]=&s.cars[0];s.Hit();s.Finish();
 Check(!oldDent->alive&&newAtomic.geometry!=s.original,"slot generation and replaced clump start a new independent dent without stale mesh access");
 D::Release(&s.cars[0]);newAtomic.setGeometry(nullptr,0);replacement.alive=false;
 s.clumps[0].alive=true;s.atomics[0].setGeometry(s.original,0);s.cars[0].m_rwObject=&s.clumps[0];
}
static void MixedMaterials(){
 Scene s;s.original->matList.appendMaterial(&s.glass);s.original->t[1].matId=1;ListHeader(s.original);
 D::SetVehicleDeformationEnabled(true);s.Hit(0,400,CVector(.35f,-.35f,0));s.Finish();RpGeometry*dent=s.atomics[0].geometry;
 Check(dent!=s.original&&dent->numVertices==4,"mixed body/glass atomic can dent the unshared body vertex without refining protected edges");
 Check(dent->v[1].z<-.001f&&dent->v[0].z==0&&dent->v[2].z==0&&dent->v[3].z==0,"indices shared with a protected material veto actual runtime vertex movement");
 Check(dent->matList.numMaterials==2&&dent->matList.materials[0]==&s.paint&&dent->matList.materials[1]==&s.glass,"staged geometry preserves all original material identities including glass");
 Check(dent->meshHeader->getMeshes()[0].numIndices==3&&dent->meshHeader->getMeshes()[1].numIndices==3,"both body and protected material indices remain correctly partitioned");
}
static void TopologyRejection(){
 for(bool priorDent:{false,true}){
  Scene s;DenseSkinny(s.original);D::SetVehicleDeformationEnabled(true);
  if(priorDent){s.Hit();s.Finish();Check(s.atomics[0].geometry!=s.original,"thin face accepts a valid out-of-plane first dent");}
  RpGeometry*previous=s.atomics[0].geometry;
  const auto positions=previous->v,normals=previous->n;
  const int previousRefs=previous->refCount,originalRefs=s.original->refCount,materialRefs=s.paint.refs,geometryAlive=aliveGeometry;
  const size_t bytes=D::privateBytes,vertices=D::privateVertices,triangles=D::privateTriangles;
  const unsigned creates=geometryCreates,assignments=geometryAssignments;
  D::RecordCollision(&s.cars[0],CVector(),CVector(0,-1,0),400,&s.wall);
  D::BeginBatch(*D::Find(&s.cars[0]),&s.cars[0]);D::BeginCandidate();
  unsigned peakBacktracks=0;
  for(int i=0;i<100&&D::job.state;++i){
   unsigned vertexWork=0,triangleWork=0;bool subdivided=false,committed=false;
   D::AdvanceJob(vertexWork,triangleWork,subdivided,committed);
   peakBacktracks=std::max(peakBacktracks,D::job.backtracks);
   Check(vertexWork<=D::VERTEX_WORK&&triangleWork<=D::TRIANGLE_WORK&&!committed,"topology rejection shares real work budgets and cannot commit invalid output");
  }
  Check(!D::job.state&&peakBacktracks==8,"actual oriented-area guard rejects a naturally inverted face after eight bounded backtracks");
  s.Frame();
  Check(s.atomics[0].geometry==previous&&previous->alive&&s.atomics[1].geometry==s.original,"guard failure preserves the previous visible geometry for both cars");
  Check(positions.size()==previous->v.size()&&std::memcmp(positions.data(),previous->v.data(),positions.size()*sizeof(RwV3d))==0&&
        std::memcmp(normals.data(),previous->n.data(),normals.size()*sizeof(RwV3d))==0,"failed dent leaves previous positions and normals byte-identical");
  Check(previous->refCount==previousRefs&&s.original->refCount==originalRefs&&s.paint.refs==materialRefs&&aliveGeometry==geometryAlive,"guard failure balances source refs without changing geometry or material ownership");
  Check(D::privateBytes==bytes&&D::privateVertices==vertices&&D::privateTriangles==triangles&&geometryCreates==creates&&geometryAssignments==assignments,
        "guard failure leaves private accounting unchanged and creates or assigns no output geometry");
 }
}
static void RenderedTopology(){
 {
  Scene s;s.original->matList.appendMaterial(&s.glass);s.original->t[1].matId=1;ListHeader(s.original);
  // Reproduce the HD DFF mismatch: GeoStruct material IDs disagree with the
  // actual rendered BinMesh. Even mesh order must not imply a material ID.
  s.original->t[0].matId=1;s.original->t[1].matId=0;
  std::swap(s.original->meshHeader->meshes[0],s.original->meshHeader->meshes[1]);
  D::SetVehicleDeformationEnabled(true);s.Hit(0,400,CVector(.35f,-.35f,0));s.Finish();auto*dent=s.atomics[0].geometry;
  Check(dent!=s.original&&dent->numVertices==4&&dent->numTriangles==2,"rendered material pointer mapping survives contradictory GeoStruct IDs and mesh ordering");
  Check(dent->v[1].z<-.001f&&dent->v[0].z==0&&dent->v[2].z==0&&dent->v[3].z==0,"actual rendered glass faces protect shared vertices despite incorrect native triangle material IDs");
  Check(dent->meshHeader->meshes[0].material==&s.paint&&dent->meshHeader->meshes[1].material==&s.glass&&
        dent->meshHeader->meshes[0].numIndices==3&&dent->meshHeader->meshes[1].numIndices==3,"output triangles remain bound to the original rendered material pointers");
 }
 {
  Scene s;DenseSkinny(s.original);s.original->v[0]={-.4f,-.4f,0};s.original->v[1]={.4f,-.4f,0};s.original->v[2]={.4f,.4f,0};s.original->v[3]={-.4f,.4f,0};
  for(auto&t:s.original->t)t={{0,0,0},0};
  auto*h=s.original->allocateMeshes(1,10,false);h->flags=rw::MeshHeader::TRISTRIP;s.original->flags|=RpGeometry::TRISTRIP;
  h->meshes[0].material=&s.paint;h->meshes[0].numIndices=10;h->setupIndices();
  const uint16 indices[]={0,1,3,2,2,0,0,1,3,2};std::copy(std::begin(indices),std::end(indices),h->data.begin());
  D::SetVehicleDeformationEnabled(true);s.Hit();s.Finish();auto*dent=s.atomics[0].geometry;
  Check(dent!=s.original&&dent->numVertices==4001&&dent->numTriangles==4,"runtime decodes rendered strips and drops degenerate connectors instead of copying incorrect native triangles");
  Check(dent->meshHeader->flags==0&&!(dent->flags&RpGeometry::TRISTRIP)&&dent->meshHeader->totalIndices==12,"rendered strips are committed as an explicit complete triangle list");
  for(const auto&t:dent->t){
   const auto&a=dent->v[t.v[0]],&b=dent->v[t.v[1]],&c=dent->v[t.v[2]];
   Check((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)>.1f,"strip parity including skipped degenerate connectors preserves every face winding");
  }
 }
}
static void CandidateBatch(){
 {
  Scene s;for(auto&a:s.atomics){a.frame.name="door_rf_dummy";a.frame.matrix.pos={12,0,0};}
  D::SetVehicleDeformationEnabled(true);s.Hit(0);s.Hit(1);ClearCounters();
  D::Process(&s.cars[0]);const unsigned walks=clumpWalks,visits=atomicVisits;
  Check(!D::batch.state&&D::workFrame==CTimer::frame,"no-panel selection still consumes this frame's selection allowance");
  D::Process(&s.cars[1]);
  Check(clumpWalks==walks&&atomicVisits==visits,"another failed car cannot repeat bounded vertex probes in the same frame");
 }
 {
  Scene s;RpAtomic extra[3];for(auto&a:extra){a.setGeometry(s.original,0);a.frame.parent=&s.clumps[0].frame;s.clumps[0].atomics.push_back(&a);}
  D::SetVehicleDeformationEnabled(true);s.Hit();s.Finish();int changed=0;
  for(auto*a:s.clumps[0].atomics)changed+=a->geometry!=s.original;
  Check(changed==3,"one hit processes at most its three selected component candidates, with one commit per frame");
  D::SetVehicleDeformationEnabled(false);for(auto&a:extra){Check(a.geometry==s.original,"OFF restores every completed component in the batch");a.setGeometry(nullptr,0);}s.clumps[0].atomics.resize(1);
 }
 {
  Scene s;RpGeometry*protectedPart=RpGeometry::create(4,2,RpGeometry::NORMALS);
  protectedPart->matList.appendMaterial(&s.paint);protectedPart->matList.appendMaterial(&s.glass);
  for(int i=0;i<4;++i)protectedPart->v[i]={s.original->v[i].x*.5f,s.original->v[i].y*.5f,0};
  protectedPart->t[0]={{0,1,2},0};protectedPart->t[1]={{0,1,2},1};protectedPart->morphTargets[0].boundingSphere.radius=.3f;
  ListHeader(protectedPart);
  RpAtomic first;first.setGeometry(protectedPart,0);first.frame.parent=&s.clumps[0].frame;s.clumps[0].atomics.insert(s.clumps[0].atomics.begin(),&first);
  D::SetVehicleDeformationEnabled(true);s.Hit();s.Finish();
  Check(first.geometry==protectedPart&&s.atomics[0].geometry!=s.original,"selected component with no movable paint vertices cannot swallow a hit intended for the next valid chassis candidate");
  D::SetVehicleDeformationEnabled(false);first.setGeometry(nullptr,0);protectedPart->destroy();s.clumps[0].atomics.erase(s.clumps[0].atomics.begin());
 }
}
static void ExteriorMaterials(){
 // These actual classic bumper/body textures never occur in m_materials1/2.
 // Before this fix an entire frontal bumper was discarded during selection.
 for(const char*name:{"admiral868bit128","admiral86body8bit64","sentinel868bit128","stinger86body64","oceanicbody128c",""}){
  Scene s;RwTexture texture;texture.name=name;s.paint.texture=&texture;
  modelInfo.m_materials1[0]=nullptr;s.atomics[0].frame.name="bump_front_dummy";
  D::SetVehicleDeformationEnabled(true);s.Hit();s.Finish();
  Check(s.atomics[0].geometry!=s.original,"unpainted opaque exterior bumper produces a private deformed mesh");
  float dent=0;for(const auto&v:s.atomics[0].geometry->v)dent=std::max(dent,std::fabs(v.z));
  Check(dent>.05f,"unpainted front-facing exterior retains a geometric dent over five centimetres");
 }
 for(const char*name:{"enginetopb4bit64","INTERIOR64","seat_black","wheel128","TYRE","windscreen","dashboard"}){
  Scene s;RwTexture texture;texture.name=name;s.paint.texture=&texture;
  modelInfo.m_materials1[0]=nullptr;
  D::SetVehicleDeformationEnabled(true);s.Hit();s.Finish();
  Check(s.atomics[0].geometry==s.original,"identified engine cabin and wheel textures remain protected inside mixed chassis frames");
 }
}
static void Unsupported(){
 for(int reason=0;reason<7;++reason){
  Scene s;D::SetVehicleDeformationEnabled(true);
  switch(reason){case 0:s.atomics[0].frame.name="wheel_lf";break;case 1:s.atomics[0].frame.name="glass";break;case 2:s.atomics[0].id=ATOMIC_FLAG_WINDSCREEN;break;case 3:s.original->flags|=RpGeometry::NATIVE;break;case 4:s.original->flags|=RpGeometry::NATIVEINSTANCE;break;case 5:s.original->skin=true;break;case 6:s.paint.color.alpha=100;break;}
  ClearCounters();s.Hit();s.Finish();Check(s.atomics[0].geometry==s.original&&geometryCreates==0&&geometryAssignments==0,"unsupported glass/wheel/windscreen/native/nativeinstance/skinned geometry remains untouched");
 }
}
static void InvalidUpperPanelBounds(){
 for(float sign:{-1.0f,1.0f}) {
  Scene s;RpAtomic extra[3];
  s.original->morphTargets[0].boundingSphere.center={0,sign,0};
  for(int i=0;i<2;i++){extra[i].setGeometry(s.original,0);extra[i].frame.name=i?"door_rf_dummy":"door_lf_dummy";}
  RpGeometry*upper=RpGeometry::create(4,2,RpGeometry::NORMALS);
  upper->matList.appendMaterial(&s.paint);
  for(int i=0;i<4;i++){upper->v[i]={s.original->v[i].x,s.original->v[i].y+sign*.8f,.45f};upper->n[i]={0,0,1};}
  upper->t[0]={{0,1,2},0};upper->t[1]={{0,2,3},0};ListHeader(upper);
  // Authored sphere is above the roof although the real panel is beside the hit.
  upper->morphTargets[0].boundingSphere.center={0,sign,5};upper->morphTargets[0].boundingSphere.radius=.6f;
  extra[2].setGeometry(upper,0);extra[2].frame.name=sign>0?"bonnet_dummy":"boot_dummy";
  for(auto&a:extra){a.frame.parent=&s.clumps[0].frame;s.clumps[0].atomics.push_back(&a);}
  D::SetVehicleDeformationEnabled(true);
  D::RecordCollision(&s.cars[0],CVector(0,sign,0),CVector(0,-sign,0),400,&s.wall);
  D::BeginBatch(*D::Find(&s.cars[0]),&s.cars[0]);
  Check(D::batch.count>=3&&D::batch.count<=4,"upper-panel reservation uses at most one extra job beyond the nearest three");
  bool selected=false;for(unsigned i=0;i<D::batch.count;i++)selected|=D::batch.candidates[i].atomic==&extra[2];
  Check(selected,"actual production selection admits impact-side bonnet/boot despite incorrect authored sphere");
  bool chassis=false;for(unsigned i=0;i<D::batch.count;i++)chassis|=D::batch.candidates[i].atomic==&s.atomics[0];
  Check(chassis,"extra upper panel does not displace the structural chassis");
  s.Finish();Check(extra[2].geometry!=upper,"reserved panel completes a real private-mesh commit");
  float rise=0;for(const auto&v:extra[2].geometry->v)rise=std::max(rise,v.z-.45f);
  Check(rise>.08f,"front/rear collision produces visible out-of-plane bending on selected upper panel");
  D::SetVehicleDeformationEnabled(false);
  for(auto&a:extra)a.setGeometry(nullptr,0);upper->destroy();s.clumps[0].atomics.resize(1);
 }
}
static void LiveTuning(){
 D::ResetTuning();
 Check(D::GetStrengthPercent()==100&&D::GetRadiusPercent()==100&&D::GetMaxDentCentimeters()==32&&D::GetThresholdPercent()==100,"default tuning preserves the accepted deformation curve");
 for(int extreme:{-100000,100000}){
  D::SetStrengthPercent(extreme);D::SetRadiusPercent(extreme);D::SetMaxDentCentimeters(extreme);D::SetThresholdPercent(extreme);
  Check(D::GetStrengthPercent()==(extreme<0?25:400)&&D::GetRadiusPercent()==(extreme<0?50:200)&&D::GetMaxDentCentimeters()==(extreme<0?10:60)&&D::GetThresholdPercent()==(extreme<0?25:200),"INI and menu inputs clamp to supported physical ranges");
 }
 D::ResetTuning();
 float crush[3]={};
 for(int i=0;i<3;++i){
  Scene s;D::ResetTuning();D::SetVehicleDeformationEnabled(true);
  D::SetStrengthPercent(i?200:100);D::SetMaxDentCentimeters(i==2?10:60);
  s.Hit(0,140);s.Finish();
  for(auto v:s.atomics[0].geometry->v)crush[i]=std::max(crush[i],-v.z);
 }
 Check(crush[1]>crush[0]*1.8f,"doubling strength increases the actual committed mesh dent for the same impact");
 Check(crush[2]>.09f&&crush[2]<=.10001f,"maximum dent setting caps actual mesh displacement");
 {
  Scene s;D::ResetTuning();D::SetVehicleDeformationEnabled(true);D::SetThresholdPercent(200);s.Hit(0,70);
  Check(D::Find(&s.cars[0])==nullptr,"higher threshold rejects a weak impact before allocating a car slot");
  D::SetThresholdPercent(25);s.Hit(0,70);s.Finish();
  Check(s.atomics[0].geometry!=s.original,"lower threshold admits the same weak impact");
 }
 {
  Scene s;D::ResetTuning();D::SetVehicleDeformationEnabled(true);D::SetStrengthPercent(200);D::SetRadiusPercent(150);D::SetMaxDentCentimeters(48);s.Hit();
  auto *state=D::Find(&s.cars[0]);Check(state!=nullptr,"tuned impact is queued");
  const auto captured=state->pending;D::ResetTuning();
  Check(D::IsVehicleDeformationEnabled(),"reset numeric tuning preserves the enabled option");
  D::BeginBatch(*state,&s.cars[0]);
  Check(D::batch.hit.depth==captured.depth&&D::batch.hit.radius==captured.radius&&D::batch.hit.maxDent==captured.maxDent&&captured.radius>1.27f&&captured.maxDent>.47f,"menu changes cannot mix parameters within a queued mesh job");
  s.Finish();
 }
 for(int radius:{50,200}){
  Scene s;D::ResetTuning();D::SetVehicleDeformationEnabled(true);D::SetStrengthPercent(400);D::SetRadiusPercent(radius);D::SetMaxDentCentimeters(60);
  for(int hit=0;hit<4;++hit){s.Hit();s.Finish();auto*g=s.atomics[0].geometry;
   for(auto v:g->v)Check(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&std::fabs(v.z)<=.60001f,"maximum strength repeated impacts remain finite and bounded");
   for(auto t:g->t){const auto&a=g->v[t.v[0]],&b=g->v[t.v[1]],&c=g->v[t.v[2]];Check((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)>0,"extreme tuning preserves committed face winding");}
  }
 }
 D::ResetTuning();
}
int main(){DisabledAndFreshRelease();IsolationRepairDetach();PendingSelectionAndVisibility();CancelAndReuse();MixedMaterials();TopologyRejection();RenderedTopology();CandidateBatch();ExteriorMaterials();Unsupported();InvalidUpperPanelBounds();LiveTuning();std::printf("PASS: actual VehicleDeformation runtime %u checks; four million OFF calls, real mesh jobs, refs, clone isolation, cleanup, all phase cancellation, topology rejection, rendered material mapping, strips, frame budget and live tuning.\n",checks);}
