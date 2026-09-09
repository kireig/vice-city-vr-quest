#include "ragdoll-test-vector.h"
#define VR_RAGDOLL_VEHICLE_NO_ENGINE
#define VR_RAGDOLL_WORLD_ENGINE_TEST
#include "VrRagdollVehicle.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <chrono>

// Engine-layout adapter. Geometry and query/gather/response code below are
// the production header; these fixtures supply compressed COL vertices and
// actual finite faces, not an infinite-plane replacement for collision.
struct CompressedVector {
	short x,y,z;
	CompressedVector(CVector p) : x(static_cast<short>(p.x*128)),y(static_cast<short>(p.y*128)),z(static_cast<short>(p.z*128)) {}
	CVector Get() const { return CVector(x/128.f,y/128.f,z/128.f); }
};
struct CColTriangle { unsigned short a,b,c; };
struct CColSphere { CVector center; float radius; };
struct CColBox { CVector min,max; };
struct CColModel {
	CColSphere boundingSphere;
	int numSpheres=0,numBoxes=0,numTriangles=0;
	CColSphere *spheres=0; CColBox *boxes=0;
	CompressedVector *vertices=0; CColTriangle *triangles=0;
};
struct CEntity {
	bool bUsesCollision=true,bRemoveFromWorld=false,isStatic=true;
	unsigned m_scanCode=0; int m_area=0,type=0;
	CVector position{0,0,0},right{1,0,0},forward{0,1,0},up{0,0,1};
	CColModel *model=0;
	bool IsBuilding() const { return type==0; } bool IsObject() const { return type==1; }
	bool IsDummy() const { return type==2; } bool GetIsStatic() const { return isStatic; }
	CColModel *GetColModel() const { return model; }
	CVector GetPosition() const { return position; } CVector GetRight() const { return right; }
	CVector GetForward() const { return forward; } CVector GetUp() const { return up; }
};
inline bool IsAreaVisible(int area) { return area==0 || area==13; }
struct CPtrNode { void *item=0; CPtrNode *next=0; };
struct CPtrList { CPtrNode *first=0; };
enum { ENTITYLIST_BUILDINGS,ENTITYLIST_BUILDINGS_OVERLAP,ENTITYLIST_OBJECTS,ENTITYLIST_OBJECTS_OVERLAP,
	ENTITYLIST_VEHICLES,ENTITYLIST_VEHICLES_OVERLAP,ENTITYLIST_PEDS,ENTITYLIST_PEDS_OVERLAP,
	ENTITYLIST_DUMMIES,ENTITYLIST_DUMMIES_OVERLAP };
struct CSector { CPtrList m_lists[10]; };
struct CColPoint { CVector point,normal; };
struct CWorld {
	static CSector sectors[80][80]; static unsigned code,queries;
	static std::vector<CEntity*> entities;
	static int GetClampedSectorIndexX(float x) { return static_cast<int>(VrRagdollVehicle::ClampF((x+2400)/50,0,79)); }
	static int GetClampedSectorIndexY(float y) { return static_cast<int>(VrRagdollVehicle::ClampF((y+2000)/50,0,79)); }
	static CSector *GetSector(int x,int y) { return &sectors[y][x]; }
	static void AdvanceCurrentScanCode() { ++code; } static unsigned GetCurrentScanCode() { return code; }
	static bool ProcessLineOfSight(const CVector &from,const CVector &to,CColPoint &hit,CEntity *&entity,
		bool buildings,bool vehicles,bool peds,bool objects,bool dummies,bool seeThrough) {
		++queries;
		if(!buildings || vehicles || peds || !objects || !dummies || seeThrough) std::abort();
		float earliest=2;
		for(CEntity *e : entities){
			if(!e->bUsesCollision || e->bRemoveFromWorld || !IsAreaVisible(e->m_area) || !e->model) continue;
			VrRagdollVehicle::Pose pose{e->position,e->right,e->forward,e->up};
			for(int i=0;i<e->model->numTriangles;++i){
				const CColTriangle &f=e->model->triangles[i]; VrRagdollVehicle::Triangle triangle;
				VrRagdollVehicle::MakeTriangle(pose.ToWorld(e->model->vertices[f.a].Get()),
					pose.ToWorld(e->model->vertices[f.b].Get()),pose.ToWorld(e->model->vertices[f.c].Get()),triangle);
				float time; CVector normal;
				if(VrRagdollVehicle::SweepTriangle(triangle,from,to,0,time,normal) && time<earliest){
					earliest=time; hit.point=from+(to-from)*time; hit.normal=normal; entity=e;
				}
			}
		}
		return earliest<=1;
	}
};
CSector CWorld::sectors[80][80]; unsigned CWorld::code=0,CWorld::queries=0;
std::vector<CEntity*> CWorld::entities;
#include "VrRagdollWorld.h"
#include "VrRagdollContacts.h"
namespace W=VrRagdollWorld;
namespace P=VrRagdollPhysics;
namespace V=VrRagdollVehicle;
namespace VC=VrRagdollContacts;
static unsigned checks;
static void Check(bool yes,const char *message) {
	++checks; if(!yes){ std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static P::State Body(CVector origin=CVector(0,0,0)) {
	P::State s={};
	const CVector points[P::SIM_BONES]={
		{0,0,.94f},{0,0,1.10f},{0,0,1.31f},{0,0,1.53f},{0,0,1.69f},
		{-.12f,0,1.43f},{-.26f,0,1.45f},{-.43f,-.035f,1.20f},{-.53f,.03f,.98f},
		{.12f,0,1.43f},{.26f,0,1.45f},{.43f,-.035f,1.20f},{.53f,.03f,.98f},
		{-.13f,0,.89f},{-.14f,.035f,.48f},{-.14f,-.04f,.1f},
		{.13f,0,.89f},{.14f,.035f,.48f},{.14f,-.04f,.1f}};
	for(int i=0;i<P::SIM_BONES;++i) s.pos[i]=points[i]+origin;
	P::Initialize(&s,CVector(1,0,0),CVector(0,0,1)); s.groundZ=0;
	return s;
}
static void Wall(W::Cache &cache,float y=0) {
	W::AddTriangle(cache,CVector(-5,y,-5),CVector(5,y,-5),CVector(5,y,5));
	W::AddTriangle(cache,CVector(-5,y,-5),CVector(5,y,5),CVector(-5,y,5));
}
static W::Cache Empty() { W::Cache cache; W::BeginGather(cache,W::Around(CVector(-10,-10,-10),CVector(10,10,10),0)); return cache; }
static void Geometry() {
	W::Cache cache=Empty(); Wall(cache);
	Wall(cache); Check(cache.triangleCount==2,"duplicate static COL triangles do not multiply solver cost");
	for(float dt : {P::STEP,2*P::STEP,3*P::STEP}) for(float speed : {5.f,20.f,70.f,150.f}){
		float start=-.4f; const CVector from(0,start,1),to(0,start+speed*dt,1);
		W::Contact hit=W::Find(cache,from,to,.1f);
		bool reaches=to.y>=-.1f;
		Check(hit.hit==reaches,"finite COL wall CCD at variable timestep");
		if(hit.hit){ Check(hit.normal.y<-.99f,"wall entry normal"); Check(hit.center.y<=-.1f,"wall skin clearance"); }
	}
	W::Contact overlap=W::Find(cache,CVector(0,-.03f,1),CVector(0,2,1),.1f);
	Check(overlap.hit && overlap.swept && overlap.center.y<=-.1f,"initial overlap cannot cross thin face");
	Check(!W::Find(cache,CVector(7,-1,1),CVector(7,1,1),.1f).hit,"finite wall does not block empty space beside edge");
	Check(W::Find(cache,CVector(5.07f,-1,1),CVector(5.07f,1,1),.1f).hit,"sphere radius catches finite triangle edge");
	Check(!W::Find(cache,CVector(5.15f,-1,1),CVector(5.15f,1,1),.1f).hit,"rounded edge has no expanded square corner");
	V::Pose pose{CVector(0,0,0),CVector(.70710678f,.70710678f,0),CVector(-.70710678f,.70710678f,0),CVector(0,0,1)};
	V::Shape box={}; box.min=CVector(-2,-.03f,-2); box.max=CVector(2,.03f,2);
	W::Cache shapeCache=Empty(); W::AddShape(shapeCache,box,pose);
	W::Contact boxHit=W::Find(shapeCache,pose.ToWorld(CVector(0,-1,0)),pose.ToWorld(CVector(0,2,0)),.1f);
	Check(boxHit.hit && DotProduct(boxHit.normal,pose.forward)<-.999f,"rotated COL box uses local axes");
	Check(pose.ToLocal(boxHit.center).y<=-.13f,"rotated box radius clearance");
	W::Contact boxInside=W::Find(shapeCache,pose.ToWorld(CVector(0,-.025f,0)),pose.ToWorld(CVector(0,2,0)),.1f);
	Check(boxInside.hit && pose.ToLocal(boxInside.center).y<=-.13f,"overlapping thin box preserves entry side");
	W::Cache sphereCache=Empty(); V::Shape sphere={}; sphere.radius=.4f;
	W::AddShape(sphereCache,sphere,pose);
	Check(W::Find(sphereCache,CVector(0,-2,0),CVector(0,2,0),.1f).hit,"COL sphere swept collision");
	Check(!W::Find(sphereCache,CVector(1,-2,0),CVector(1,2,0),.1f).hit,"COL sphere finite boundary");
}
static void Response() {
	W::Cache cache=Empty(); Wall(cache);
	W::AddTriangle(cache,CVector(0,-5,-5),CVector(0,5,5),CVector(0,5,-5));
	W::AddTriangle(cache,CVector(0,-5,-5),CVector(0,-5,5),CVector(0,5,5));
	P::State state=Body(CVector(-2,-2,0));
	for(int i=0;i<P::SIM_BONES;++i) state.prev[i]=state.pos[i]-CVector(70,70,0)*P::STEP;
	unsigned narrow=0,broad=0; float maxSpeed=0,maxStretch=0;
	for(int step=0;step<180;++step){
		W::StepContacts context(cache,&state,3*P::STEP);
		P::Step(&state,W::Project,&context,3*P::STEP); W::ResolveVelocity(&state,context);
		narrow+=context.stats.narrowTests; broad+=context.stats.boundsTests;
		for(int i=0;i<P::SIM_BONES;++i){
			Check(std::isfinite(state.pos[i].x),"corner response stays finite");
			Check(state.pos[i].x <= -P::radius[i]+.025f && state.pos[i].y <= -P::radius[i]+.025f,"body stays on entry side in corner");
			maxSpeed=V::MaxF(maxSpeed,((state.pos[i]-state.prev[i])/P::STEP).Magnitude());
		}
		if(step>20) for(int i=0;i<P::SKELETON_STICKS;++i){
			float ratio=(state.pos[P::sticks[i].a]-state.pos[P::sticks[i].b]).Magnitude()/state.stickLen[i];
			maxStretch=V::MaxF(maxStretch,fabsf(ratio-1));
		}
	}
	std::printf("Corner: maxSpeed=%.3f m/s settled max length error=%.4f broad=%u narrow=%u\n",maxSpeed,maxStretch,broad,narrow);
	Check(maxSpeed<110,"wall normal projection does not amplify incoming 99m/s impact");
	Check(maxStretch<.15f,"contact solver retains skeletal lengths after impact");
	// An isolated long shin crosses a thin post between clear endpoint spheres.
	P::State limb=Body(CVector(20,20,20));
	limb.pos[P::RD_LCALF]=CVector(-.19f,-.4f,1); limb.pos[P::RD_LFOOT]=CVector(.19f,-.4f,1);
	for(int i=0;i<P::SIM_BONES;++i) limb.prev[i]=limb.pos[i];
	W::Cache post=Empty(); W::AddTriangle(post,CVector(-.08f,0,.5f),CVector(.08f,0,.5f),CVector(0,0,1.5f));
	W::StepContacts context(post,&limb);
	limb.pos[P::RD_LCALF].y=.4f; limb.pos[P::RD_LFOOT].y=.4f;
	W::Project(&limb,&context);
	Check(context.probeCount>P::SIM_BONES,"long limbs get interior probes");
	Check((limb.pos[P::RD_LCALF].y+limb.pos[P::RD_LFOOT].y)*.5f < 0,"interior shin volume catches thin post");
	P::State late=Body(CVector(0,-1,0)); W::Cache wall=Empty(); Wall(wall);
	W::StepContacts lateContext(wall,&late); W::Project(&late,&lateContext);
	for(int i=0;i<P::SIM_BONES;++i) late.pos[i].y+=2;
	W::Project(&late,&lateContext);
	Check(late.pos[P::RD_PELVIS].y<=-P::radius[P::RD_PELVIS],"new wall crossed by a later joint correction is still swept");
	W::Cache platform=Empty();
	W::AddTriangle(platform,CVector(-5,-5,2),CVector(5,-5,2),CVector(5,5,2));
	W::AddTriangle(platform,CVector(-5,-5,2),CVector(5,5,2),CVector(-5,5,2));
	P::State resting=Body(CVector(0,0,2));
	for(int frame=0;frame<720 && !resting.asleep;++frame){
		W::StepContacts floorContext(platform,&resting);
		P::Step(&resting,W::Project,&floorContext); W::ResolveVelocity(&resting,floorContext);
		for(int i=0;i<P::SIM_BONES;++i) Check(resting.pos[i].z>=2+P::radius[i]-.015f,"raised COL platform preserves radius clearance");
	}
	Check(resting.asleep,"static COL support can sleep despite original ground plane two metres lower");
}
struct ModelFixture {
	std::vector<CompressedVector> vertices;
	std::vector<CColTriangle> faces;
	CColSphere sphere{CVector(0,0,0),.25f}; CColBox box{CVector(-1,-.05f,-1),CVector(1,.05f,1)};
	CColModel model;
	ModelFixture() {
		vertices={CVector(-5,0,-5),CVector(5,0,-5),CVector(5,0,5),CVector(-5,0,5),CVector(100,100,100),CVector(101,100,100),CVector(100,101,100)};
		faces={{0,1,2},{0,2,3}}; model.boundingSphere={CVector(0,0,0),200}; Sync();
	}
	void Sync(){ model.vertices=vertices.data(); model.triangles=faces.data(); model.numTriangles=static_cast<int>(faces.size()); }
};
static void ResetWorld() {
	for(auto &row:CWorld::sectors) for(auto &sector:row) for(auto &list:sector.m_lists) list.first=0;
	CWorld::entities.clear(); CWorld::queries=0;
}
static void GatherTests() {
	ResetWorld(); ModelFixture fixture; CEntity entity; entity.model=&fixture.model;
	CPtrNode node{&entity,0}; CWorld::GetSector(48,40)->m_lists[ENTITYLIST_BUILDINGS].first=&node;
	CWorld::entities.push_back(&entity);
	P::State state=Body(CVector(1,-.5f,0)); W::Cache cache;
	W::Gather(&state,cache); Check(cache.triangleCount==2,"gather decodes actual compressed COL faces");
	Check(W::Find(cache,CVector(1,-1,1),CVector(1,1,1),.1f).hit,"gathered finite COL face blocks sphere sweep");
	unsigned scanCode=CWorld::code; W::Gather(&state,cache);
	Check(CWorld::code==scanCode,"unchanged coverage reuses static cache");
	for(int i=0;i<P::SIM_BONES;++i){ state.pos[i].x+=1.5f; state.prev[i].x+=1.5f; }
	W::Gather(&state,cache); Check(CWorld::code>scanCode,"travel outside cached coverage gathers again");
	scanCode=CWorld::code; for(int i=0;i<14;++i) W::Gather(&state,cache);
	Check(CWorld::code>scanCode,"static cache expires so streamed object changes are refreshed");
	entity.position=CVector(5,0,0); entity.right=CVector(0,1,0); entity.forward=CVector(-1,0,0);
	W::BeginGather(cache,W::Around(CVector(5,0,0),CVector(5,0,0),2)); W::GatherEntity(cache,&entity);
	Check(W::Find(cache,CVector(4,0,1),CVector(6,0,1),.1f).hit,"COL transform includes entity orientation and translation");
	entity.position=CVector(0,0,0); entity.right=CVector(1,0,0); entity.forward=CVector(0,1,0);
	fixture.model.numSpheres=1; fixture.model.spheres=&fixture.sphere; fixture.model.numBoxes=1; fixture.model.boxes=&fixture.box;
	W::BeginGather(cache,W::Around(CVector(0,0,0),CVector(0,0,0),2)); W::GatherEntity(cache,&entity);
	Check(cache.shapeCount==2,"gather retains COL spheres and boxes");
	for(int exclusion=0;exclusion<4;++exclusion){
		entity.type=exclusion==0?1:0; entity.isStatic=exclusion!=0; entity.bUsesCollision=exclusion!=1;
		entity.bRemoveFromWorld=exclusion==2; entity.m_area=exclusion==3?1:0;
		W::BeginGather(cache,W::Around(CVector(0,0,0),CVector(0,0,0),2)); W::GatherEntity(cache,&entity);
		Check(cache.shapeCount+cache.triangleCount==0,"nonstatic/disabled/removed/hidden-area entity excluded");
	}
	entity=CEntity(); entity.model=&fixture.model; fixture.model.numBoxes=fixture.model.numSpheres=0;
	fixture.faces.assign(W::MAX_PRIMITIVE_SCAN+10,CColTriangle{4,5,6}); fixture.faces.back()={0,1,2}; fixture.Sync();
	state=Body(CVector(1,-.5f,0)); cache=W::Cache(); W::Gather(&state,cache);
	Check(cache.incomplete && cache.stats.primitiveScans==W::MAX_PRIMITIVE_SCAN,"large COL decode budget is hard capped and marked incomplete");
	for(int i=0;i<P::SIM_BONES;++i) state.prev[i]=state.pos[i]-CVector(0,60,0)*P::STEP;
	W::StepContacts context(cache,&state,P::STEP,W::FallbackSweep);
	for(int i=0;i<P::SIM_BONES;++i) state.pos[i].y+=1;
	for(int iteration=0;iteration<7;++iteration) W::Project(&state,&context);
	Check(context.stats.fallbackQueries==P::SIM_BONES && CWorld::queries==P::SIM_BONES,"overflow engine sweep budget once per moving joint, never per constraint iteration");
	Check(state.pos[P::RD_PELVIS].y<0,"overflow fallback catches omitted finite COL wall");
	fixture.faces.assign(2,CColTriangle{0,1,2}); fixture.Sync();
	std::vector<CPtrNode> nodes(W::MAX_LIST_NODES+20);
	for(size_t i=0;i<nodes.size();++i) nodes[i]=CPtrNode{&entity,i+1<nodes.size()?&nodes[i+1]:0};
	CWorld::GetSector(48,40)->m_lists[ENTITYLIST_BUILDINGS].first=nodes.data();
	cache=W::Cache(); state=Body(CVector(1,-.5f,0)); W::Gather(&state,cache);
	Check(cache.stats.listNodes==W::MAX_LIST_NODES && cache.incomplete,"duplicate sector nodes cannot bypass traversal budget");
	Check(cache.stats.primitiveScans==2,"scan code deduplicates entities across lists");
	ResetWorld(); state=Body(CVector(-1000,-1000,0)); state.pos[P::RD_HEAD]=CVector(1000,1000,1); state.prev[P::RD_HEAD]=state.pos[P::RD_HEAD];
	cache=W::Cache(); W::Gather(&state,cache);
	Check(cache.stats.sectors==W::MAX_SECTORS && cache.incomplete,"abnormal body spans cannot exceed sector budget");
	std::printf("Cache: %zu bytes/body; %zu bytes/220 peds; gather caps %d nodes / %d primitives; fallback %u queries\n",
		sizeof(W::Cache),sizeof(W::Cache)*220,W::MAX_LIST_NODES,W::MAX_PRIMITIVE_SCAN,context.stats.fallbackQueries);
}
static void Cost() {
	W::Cache cache=Empty();
	for(int i=0;i<W::MAX_TRIANGLES;++i){ float y=-.3f+static_cast<float>(i)*.006f;
		W::AddTriangle(cache,CVector(-5,y,-5),CVector(5,y,-5),CVector(0,y,5)); }
	P::State initial=Body(CVector(0,-.6f,0));
	for(int i=0;i<P::SIM_BONES;++i) initial.prev[i]=initial.pos[i]-CVector(0,50,0)*P::STEP;
	const auto start=std::chrono::steady_clock::now(); unsigned long long narrow=0,broad=0;
	const int bodies=120;
	for(int k=0;k<bodies;++k){ P::State state=initial; W::StepContacts context(cache,&state,3*P::STEP);
		P::Step(&state,W::Project,&context,3*P::STEP); W::ResolveVelocity(&state,context);
		narrow+=context.stats.narrowTests; broad+=context.stats.boundsTests; }
	const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
	std::printf("Deliberately dense 96-face contact: %.3f host ms/12 body-steps; bounds=%llu narrow=%llu per12. Not a Quest FPS measurement.\n",
		ms*12/bodies,broad*12/bodies,narrow*12/bodies);
	ModelFixture fixture; fixture.faces.assign(W::MAX_PRIMITIVE_SCAN,CColTriangle{4,5,6}); fixture.Sync();
	CEntity entity; entity.model=&fixture.model; W::Cache gathered;
	const auto gatherStart=std::chrono::steady_clock::now();
	for(int k=0;k<120;++k){ W::BeginGather(gathered,W::Around(CVector(0,0,0),CVector(0,0,0),2)); W::GatherEntity(gathered,&entity); }
	const double gatherMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-gatherStart).count();
	const auto reuseStart=std::chrono::steady_clock::now(); unsigned reused=0;
	const W::Bounds required=W::RequiredBounds(&initial,P::STEP);
	W::BeginGather(gathered,required);
	for(int k=0;k<120000;++k){ gathered.remaining=1; reused+=W::Reuse(gathered,required,P::STEP)?1u:0u; }
	const double reuseMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-reuseStart).count();
	Check(reused==120000,"cache-hit timing executes reuse path");
	std::printf("4096 primitive bounded gather: %.3f host ms/12 gathers; cached coverage %.6f ms/12 checks.\n",gatherMs*.1,reuseMs*.0001);
}
static void CarQuad(V::Vehicle &car,CVector a,CVector b,CVector c,CVector d) {
	V::MakeTriangle(a,b,c,car.triangles[car.triangleCount++]);
	V::MakeTriangle(a,c,d,car.triangles[car.triangleCount++]);
}
static V::Batch PushCar(bool corner) {
	V::Batch batch; batch.count=1; V::Vehicle &car=batch.vehicles[0]; car.handle=1; car.mass=1400;
	car.pose.forward=corner?CVector(.70710678f,.70710678f,0):CVector(0,1,0);
	car.pose.right=corner?CVector(.70710678f,-.70710678f,0):CVector(1,0,0);
	car.pose.up=CVector(0,0,1); car.pose.origin=car.pose.forward*(-6);
	car.linearVelocity=car.surfaceLinearVelocity=car.angularVelocity=car.surfaceAngularVelocity=CVector(0,0,0);
	car.shapeCount=1; car.shapes[0].min=CVector(-.8f,-2,.1f); car.shapes[0].max=CVector(.8f,2,.45f);
	car.shapes[0].center=(car.shapes[0].min+car.shapes[0].max)*.5f; car.shapes[0].radius=0;
	car.boundCenter=CVector(0,0,.7f); car.boundRadius=2.4f;
	CarQuad(car,{-.8f,1.1f,.65f},{.8f,1.1f,.65f},{.8f,2,.65f},{-.8f,2,.65f});
	CarQuad(car,{-.8f,.6f,1.3f},{.8f,.6f,1.3f},{.8f,1.1f,.65f},{-.8f,1.1f,.65f});
	CarQuad(car,{-.8f,-1.2f,1.3f},{.8f,-1.2f,1.3f},{.8f,.6f,1.3f},{-.8f,.6f,1.3f});
	return batch;
}
struct Combined {
	VC::StepContacts *car; W::StepContacts *world;
	bool corner; unsigned overwrites=0,retained=0;
};
static void ProjectCombined(P::State *state,void *opaque) {
	Combined &context=*static_cast<Combined*>(opaque);
	bool previous[P::SIM_BONES];
	for(int i=0;i<P::SIM_BONES;++i) previous[i]=state->contact[i].active && state->contact[i].staticWorld;
	// Exactly the production VrRagdoll.cpp order: vehicle first, world last.
	VC::Project(state,context.car);
	for(int i=0;i<P::SIM_BONES;++i)
		if(previous[i] && state->contact[i].active && !state->contact[i].staticWorld) ++context.overwrites;
	W::Project(state,context.world);
	for(int i=0;i<P::SIM_BONES;++i){
		const bool closeWall=state->pos[i].y>=-P::radius[i]-.004f ||
			(context.corner && state->pos[i].x>=-P::radius[i]-.004f);
		if(closeWall && context.car->valid[i] && context.world->normalCount[i]){
			Check(state->contact[i].active && state->contact[i].staticWorld,"world contact survives vehicle overwriting shared contact state");
			++context.retained;
		}
	}
}
static void VehicleIntoWall() {
	unsigned totalCar=0,totalWorld=0,totalOverwrites=0,totalRetained=0;
	for(bool corner : {false,true}) for(float dt : {P::STEP,2*P::STEP}) for(float speed : {5.f,15.f,30.f}){
		V::Batch batch=PushCar(corner); V::Vehicle &car=batch.vehicles[0];
		const float stop=corner?-(2+.8f+.35f/.70710678f):-(2+.35f);
		float distance=-6;
		P::State state=Body(car.pose.forward*(corner?-1.8f:-1.2f));
		W::Cache world=Empty(); Wall(world);
		if(corner){
			W::AddTriangle(world,CVector(0,-5,-5),CVector(0,5,5),CVector(0,5,-5));
			W::AddTriangle(world,CVector(0,-5,-5),CVector(0,-5,5),CVector(0,5,5));
		}
		unsigned carHits=0,worldHits=0,overwrites=0,retained=0; float maxSpeed=0,maxPenetration=0,maxEnergySpeed=0;
		float maxCoreHeight=0,maxComSpeed=0,settledSpeed=0;
		int peakNode=-1,peakFrame=-1;
		for(int frame=0;frame<240;++frame){
			const float travel=V::MinF(speed*dt,V::MaxF(0,stop-distance));
			car.linearVelocity=car.surfaceLinearVelocity=car.pose.forward*(travel/dt);
			V::Prepare(batch,0,dt);
			if(state.asleep && VC::ShouldWake(&state,batch)) P::Wake(&state);
			VC::StepContacts vehicleContacts(&batch,0,&state,0,0,dt);
			W::StepContacts worldContacts(world,&state,dt);
			Combined combined{&vehicleContacts,&worldContacts,corner};
			P::Step(&state,ProjectCombined,&combined,dt); W::ResolveVelocity(&state,worldContacts);
			carHits+=vehicleContacts.count; overwrites+=combined.overwrites; retained+=combined.retained;
			float energySpeedSq=0; CVector meanVelocity(0,0,0);
			for(int i=0;i<P::SIM_BONES;++i){
				worldHits+=worldContacts.normalCount[i]?1u:0u;
				const float velocity=((state.pos[i]-state.prev[i])/P::STEP).Magnitude();
				Check(std::isfinite(velocity) && std::isfinite(state.pos[i].MagnitudeSqr()),"car/wall combined contacts remain finite");
				if(velocity>maxSpeed){ maxSpeed=velocity; peakNode=i; peakFrame=frame; }
				energySpeedSq+=velocity*velocity/(70*P::invMass[i]);
				meanVelocity+=(state.pos[i]-state.prev[i])/(P::STEP*70*P::invMass[i]);
				if(i<=P::RD_HEAD) maxCoreHeight=V::MaxF(maxCoreHeight,state.pos[i].z);
				maxPenetration=V::MaxF(maxPenetration,state.pos[i].y+P::radius[i]);
				if(corner) maxPenetration=V::MaxF(maxPenetration,state.pos[i].x+P::radius[i]);
				Check(state.pos[i].y<.02f && (!corner || state.pos[i].x<.02f),"car cannot push head pelvis or limb centre through static wall");
			}
			maxEnergySpeed=V::MaxF(maxEnergySpeed,sqrtf(energySpeedSq));
			maxComSpeed=V::MaxF(maxComSpeed,meanVelocity.Magnitude()); settledSpeed=sqrtf(energySpeedSq);
			distance+=travel; car.pose.origin=car.pose.forward*distance;
			Check(distance<=stop+.00001f,"fixture car stops before static wall");
		}
		std::printf("car->%s %.0fm/s dt%.4f car=%u world=%u overwritten=%u restored=%u maxSpeed=%.3f node%d frame%d rms=%.3f COM=%.3f height=%.3f settled=%.3f radiusPen=%.4f\n",
			corner?"corner":"wall",speed,dt,carHits,worldHits,overwrites,retained,maxSpeed,peakNode,peakFrame,maxEnergySpeed,maxComSpeed,maxCoreHeight,settledSpeed,maxPenetration);
		Check(carHits>0 && worldHits>0,"approaching car actually drives ragdoll into wall contacts");
		Check(maxPenetration<.035f,"combined contacts retain joint radius clearance at wall");
		// A rotating foot can legitimately move faster than the car. Bound
		// total kinetic energy using body mass, driving speed and the initial
		// standing body's available fall height; also check centre/height and
		// final motion so a transient joint bound cannot hide a body launch.
		Check(maxEnergySpeed*maxEnergySpeed<speed*speed+2*P::FALL_GRAVITY*1.75f,"combined contacts do not add energy beyond drive speed and standing fall");
		Check(maxSpeed<2*speed+8 && maxCoreHeight<3.5f,"combined contacts avoid an extremity or torso catapult");
		Check(settledSpeed<.5f,"combined contacts settle after car stops before the wall");
		totalCar+=carHits; totalWorld+=worldHits; totalOverwrites+=overwrites; totalRetained+=retained;
	}
	Check(totalOverwrites>0 && totalRetained>0,"combined fixture exercises shared contact overwrite and restoration");
	std::printf("Combined car/wall totals: car=%u world=%u overwritten=%u restored=%u\n",totalCar,totalWorld,totalOverwrites,totalRetained);
}
int main() { Geometry(); GatherTests(); Cost(); Response(); VehicleIntoWall(); std::printf("World collision: %u checks PASS\n",checks); }
