#include "common.h"
#include "VehicleDeformation.h"

#if defined(GTA_VR_WEAPONS) && defined(__ANDROID__) && defined(LIBRW)
#include "VehicleDeformationMesh.h"
#include "Vehicle.h"
#include "Camera.h"
#include "ModelInfo.h"
#include "VehicleModelInfo.h"
#include "NodeName.h"
#include "Pools.h"
#include "Timer.h"
#include "VisibilityPlugins.h"
#include <cstring>
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
#include <android/log.h>
#endif

namespace VehicleDeformation {
bool enabled = false;
namespace {
using namespace VehicleDeformationDetail;
const unsigned MAX_CARS=16, MAX_PARTS=24, MAX_ATOMICS=128;
const size_t MAX_VERTICES=192000, MAX_TRIANGLES=256000;
const size_t MAX_BYTES=32*1024*1024;
const size_t MAX_JOB_BYTES=16*1024*1024;
const unsigned VERTEX_WORK=8192, TRIANGLE_WORK=12288, COOLDOWN_MS=200;
const unsigned PANEL_PROBES=64;

struct Hit {
	Vec point, normal; float depth=0, impulse=0; uint32 time=0;
	float radius=0.85f, maxDent=0.32f;
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
	int32 otherHandle=-1, otherModel=-1, otherType=0;
#endif
};
struct Part {
	RpAtomic *atomic=nullptr;
	RpGeometry *original=nullptr, *current=nullptr;
	std::vector<Vec> rest;
	size_t vertices=0, triangles=0, bytes=0;
};
struct State {
	CEntity *owner=nullptr;
	RpClump *clump=nullptr;
	int32 handle=0;
	uint32 lastUse=0, lastDent=0;
	bool hasDent=false;
	Hit pending;
	Part parts[MAX_PARTS];
};
State states[MAX_CARS];
size_t privateVertices=0, privateTriangles=0, privateBytes=0;
uint32 workFrame=~0u;
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
#define DEFORM_DIAG(...) __VA_ARGS__
struct Diagnostics {
	uint32 contacts=0, admitted=0, noSlot=0, noPanel=0, commits=0;
	uint32 discarded=0, weak=0, lastReport=0;
	int32 model=-1;
	int32 handle=-1, otherHandle=-1, otherModel=-1, otherType=0;
	float impulse=0, depth=0, displacement=0;
	Vec localPoint;
};
Diagnostics diagnostics;
void ReportDiagnostics(const char *event,bool force=false) {
#if defined(__ANDROID__)
	uint32 now=CTimer::GetTimeInMilliseconds();
	if(!force && now-diagnostics.lastReport<2000) return;
	diagnostics.lastReport=now;
	__android_log_print(ANDROID_LOG_INFO,"MiamiVR",
		"[VehicleDeformation] %s model=%d contacts=%u admitted=%u weak=%u noSlot=%u noPanel=%u commits=%u discarded=%u impulse=%.2f depth=%.3f displacement=%.3f point=%.2f,%.2f,%.2f receiver=%d other=%d/%d type=%d",
		event,diagnostics.model,diagnostics.contacts,diagnostics.admitted,diagnostics.weak,
		diagnostics.noSlot,diagnostics.noPanel,diagnostics.commits,diagnostics.discarded,
		diagnostics.impulse,diagnostics.depth,diagnostics.displacement,
		diagnostics.localPoint.x,diagnostics.localPoint.y,diagnostics.localPoint.z,
		diagnostics.handle,diagnostics.otherHandle,diagnostics.otherModel,diagnostics.otherType);
#else
	(void)event;
	(void)force;
#endif
}
#else
#define DEFORM_DIAG(...) ((void)0)
#endif

struct Job {
	State *state=nullptr;
	RpAtomic *atomic=nullptr;
	RpGeometry *source=nullptr, *result=nullptr;
	Part *part=nullptr;
	Transform toCar, fromCar;
	Hit hit;
	Mesh mesh;
	std::vector<uint8_t> bodyMaterial;
	std::vector<uint16> renderMaterial;
	std::vector<uint32_t> materialCounts, materialOffsets;
	std::vector<Vec> rest;
	std::vector<DentSnapshot> before;
	unsigned phase=0, cursor=0, passes=0, changed=0, backtracks=0, meshIndex=0, indexCursor=0;
	size_t bytes=0;
	bool subdivide=false, renderSource=false;
	Vec boundMin, boundMax;
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
	float displacement=0;
	float heightChange=0;
#endif
};
Job job;
struct Candidate { RpAtomic *atomic=nullptr; Transform transform; float score=1.0e20f; };
struct Batch {
	State *state=nullptr;
	Hit hit;
	Candidate candidates[4];
	unsigned count=0, next=0;
};
Batch batch;

Vec V(const CVector &v) { return Vec(v.x,v.y,v.z); }
Vec V(const RwV3d &v) { return Vec(v.x,v.y,v.z); }
RwV3d RW(Vec v) { RwV3d out; out.x=v.x; out.y=v.y; out.z=v.z; return out; }
Transform Matrix(const RwMatrix *m) {
	Transform t; t.right=V(m->right); t.forward=V(m->up); t.up=V(m->at); t.pos=V(m->pos); return t;
}
Transform Matrix(const CMatrix &m) {
	Transform t; t.right=V(m.GetRight()); t.forward=V(m.GetForward());
	t.up=V(m.GetUp()); t.pos=V(m.GetPosition()); return t;
}
Transform Matrix(const CollisionPose &m) {
	Transform t;t.right=Vec(m.right[0],m.right[1],m.right[2]);
	t.forward=Vec(m.forward[0],m.forward[1],m.forward[2]);
	t.up=Vec(m.up[0],m.up[1],m.up[2]);t.pos=Vec(m.position[0],m.position[1],m.position[2]);return t;
}
bool Near(CVehicle *v) { return (v->GetPosition()-TheCamera.GetPosition()).MagnitudeSqr()<=40.0f*40.0f; }
bool Eligible(CVehicle *v) {
	return v && v->IsCar() && v->pHandling && !v->IsRealHeli() && !v->IsRealPlane() &&
	       v->m_rwObject && RwObjectGetType(v->m_rwObject)==rpCLUMP && !v->bRemoveFromWorld;
}
State *Find(CEntity *owner) {
	for(State &s:states) if(s.owner==owner) return &s;
	return nullptr;
}
void CancelJob(const char *reason=nullptr) {
	(void)reason;
	DEFORM_DIAG(if(reason && job.state) { diagnostics.discarded++; ReportDiagnostics(reason); });
	if(job.source) job.source->destroy();
	if(job.result) job.result->destroy();
	job=Job();
}
void CancelBatch() { CancelJob(); batch=Batch(); }
struct RestoreContext { State *state; bool restore; RpAtomic *wanted; bool found; };
RpAtomic *RestoreCB(RpAtomic *atomic,void *data) {
	RestoreContext &c=*static_cast<RestoreContext*>(data);
	if(atomic==c.wanted) c.found=true;
	if(c.restore) for(Part &p:c.state->parts)
		if(p.atomic==atomic && atomic->geometry==p.current && p.original && p.current!=p.original)
			atomic->setGeometry(p.original,0);
	return atomic;
}
void ReleaseState(State &s,bool restore) {
	if(job.state==&s || batch.state==&s) CancelBatch();
	if(restore && s.clump) {
		RestoreContext context={&s,true,nullptr,false};
		RpClumpForAllAtomics(s.clump,RestoreCB,&context);
	}
	for(Part &p:s.parts) {
		if(p.original) p.original->destroy();
		privateVertices-=p.vertices; privateTriangles-=p.triangles; privateBytes-=p.bytes;
		p=Part();
	}
	s.owner=nullptr; s.clump=nullptr; s.pending=Hit(); s.hasDent=false;
}
bool Live(State &s) {
	CVehicle *v=CPools::GetVehicle(s.handle);
	return v && static_cast<CEntity*>(v)==s.owner && v->m_rwObject==reinterpret_cast<RwObject*>(s.clump);
}
State *Acquire(CVehicle *v) {
	State *s=Find(v);
	if(s && s->clump!=v->GetClump()) { ReleaseState(*s,false); s=nullptr; }
	if(s) return s;
	for(State &candidate:states) if(!candidate.owner) { s=&candidate; break; }
	if(!s) for(State &candidate:states)
		if(!candidate.hasDent && candidate.pending.depth<=0 && batch.state!=&candidate) {
			ReleaseState(candidate,false); s=&candidate; break;
		}
	if(!s) {
		// Do not visibly repair a nearby car merely because another car was hit.
		// Only distant/stale states can be evicted from the fixed capacity.
		for(State &candidate:states) {
			if(batch.state==&candidate) continue;
			bool live=Live(candidate);
			if(live && Near(CPools::GetVehicle(candidate.handle))) continue;
			if(!s || candidate.lastUse<s->lastUse) s=&candidate;
		}
		if(!s) return nullptr;
		ReleaseState(*s,Live(*s));
	}
	s->owner=v; s->clump=v->GetClump(); s->handle=CPools::GetVehicleRef(v);
	s->lastUse=CTimer::GetTimeInMilliseconds();
	return s;
}
bool BodyFrame(RpAtomic *a) {
	if(!(RpAtomicGetFlags(a)&rpATOMICRENDER)) return false;
	if(CVisibilityPlugins::GetAtomicId(a)&ATOMIC_FLAG_WINDSCREEN) return false;
	bool body=false;
	RwFrame *frame=RpAtomicGetFrame(a);
	for(unsigned depth=0; frame && depth<16; depth++,frame=RwFrameGetParent(frame)) {
		unsigned role=BodyNameRole(GetFrameNodeName(frame));
		if(role==2) return false;
		body |= role==1;
	}
	return body;
}
bool BodyMaterial(const CVehicleModelInfo &mi,RpMaterial *material) {
	if(!material || material->color.alpha!=255) return false;
	for(unsigned i=0;i<NUM_FIRST_MATERIALS && mi.m_materials1[i];i++) if(mi.m_materials1[i]==material) return true;
	for(unsigned i=0;i<NUM_SECOND_MATERIALS && mi.m_materials2[i];i++) if(mi.m_materials2[i]==material) return true;
	return ExteriorMaterialName(material->texture ? RwTextureGetName(material->texture) : nullptr);
}
bool Supported(RpGeometry *g) {
	return g && !(g->flags&(rw::Geometry::NATIVE|rw::Geometry::NATIVEINSTANCE)) &&
	       g->numMorphTargets==1 && !rw::Skin::get(g) && g->numVertices>0 &&
	       g->numVertices<=65535 && g->numTriangles>0 && g->numTriangles<=120000 &&
	       g->numTexCoordSets>=0 && g->numTexCoordSets<=8 &&
	       g->matList.numMaterials>0 && g->matList.numMaterials<=128 &&
	       g->morphTargets[0].vertices && g->triangles &&
	       (!g->meshHeader || (g->meshHeader->numMeshes<=128 && g->meshHeader->totalIndices<=720000 &&
	                          (g->meshHeader->flags==0 || g->meshHeader->flags==rw::MeshHeader::TRISTRIP)));
}
struct SelectContext {
	State *state; const CVehicleModelInfo *model; Transform rootInverse;
	unsigned count;
	Candidate upper, chassis;
};
RpAtomic *SelectCB(RpAtomic *a,void *data) {
	SelectContext &c=*static_cast<SelectContext*>(data);
	if(++c.count>MAX_ATOMICS || !BodyFrame(a) || !Supported(a->geometry)) return a;
	RpGeometry *g=a->geometry;
	bool exterior=false;
	for(int i=0;i<g->matList.numMaterials;i++) exterior|=BodyMaterial(*c.model,g->matList.materials[i]);
	if(!exterior) return a;
	Transform t=c.rootInverse*Matrix(RwFrameGetLTM(RpAtomicGetFrame(a))), inverse;
	if(!t.Inverse(inverse)) return a;
	// Replacement DFF spheres can describe a different frame entirely. Probe
	// actual geometry with a fixed event budget instead of ranking those spheres.
	Vec low=V(g->morphTargets[0].vertices[0]),high=low;
	const unsigned probes=std::min(PANEL_PROBES,unsigned(g->numVertices));
	for(unsigned i=1;i<probes;i++) {
		Vec p=V(g->morphTargets[0].vertices[size_t(i)*(g->numVertices-1)/(probes-1)]);
		low=Vec(std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z));
		high=Vec(std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z));
	}
	Vec center=t.Point((low+high)*0.5f);
	float radius=(Length(high-low)*0.5f+0.15f)*std::max(Length(t.right),std::max(Length(t.forward),Length(t.up)));
	if(!Finite(center) || !std::isfinite(radius) || radius<0) return a;
	// Prefer the local panel around the contact over a whole-chassis sphere.
	float score=PanelScore(center,radius,c.state->pending.point,std::max(1.4f,c.state->pending.radius+0.5f));
	const char *frameName=GetFrameNodeName(RpAtomicGetFrame(a));
	if(frameName && std::strstr(frameName,"chassis") &&
	   (!c.chassis.atomic || score<c.chassis.score)) {
		c.chassis.atomic=a;c.chassis.transform=t;c.chassis.score=score;
	}
	// A frontal collision also loads the upper panel behind the bumper. Keep
	// that panel in the bounded queue even when trim is closer to the contact.
	RwFrame *f=RpAtomicGetFrame(a);
	for(unsigned depth=0;f && depth<16;depth++,f=RwFrameGetParent(f)) {
		if(IsUpperImpactPanel(GetFrameNodeName(f),c.state->pending.point,c.state->pending.normal)) {
			if(!c.upper.atomic || score<c.upper.score) { c.upper.atomic=a;c.upper.transform=t;c.upper.score=score; }
			break;
		}
		if(f==RpClumpGetFrame(c.state->clump)) break;
	}
	for(unsigned i=0;i<3;i++) if(score<batch.candidates[i].score) {
		for(unsigned j=2;j>i;j--) batch.candidates[j]=batch.candidates[j-1];
		batch.candidates[i].atomic=a; batch.candidates[i].transform=t; batch.candidates[i].score=score;
		batch.count=std::min(3u,batch.count+1); break;
	}
	return a;
}
void BeginBatch(State &s,CVehicle *vehicle) {
	Transform rootInverse;
	if(!Matrix(RwFrameGetLTM(RpClumpGetFrame(s.clump))).Inverse(rootInverse)) { s.pending=Hit(); return; }
	const CVehicleModelInfo *model=static_cast<CVehicleModelInfo*>(CModelInfo::GetModelInfo(vehicle->GetModelIndex()));
	batch.state=&s; batch.hit=s.pending;
	// Pending age was checked before selection. Once admitted, this bounded
	// batch needs its own work deadline; waiting behind the first car must not
	// cancel the second car halfway through the same collision's body panels.
	batch.hit.time=CTimer::GetTimeInMilliseconds();
	SelectContext context={&s,model,rootInverse,0,Candidate(),Candidate()};
	RpClumpForAllAtomics(s.clump,SelectCB,&context);
	// A corner hit can rank bumper/wing/bonnet ahead of the main body. Leaving
	// that body rigid hides the other dents behind the original nose silhouette.
	// Keep the chassis and upper panel. If the closest three are all separate
	// panels, append the body instead of leaving a rigid fender over the dent.
	// This adds at most one event job; the per-frame work limits are unchanged.
	const Candidate required[]={context.chassis,context.upper};
	for(const Candidate &candidate:required) if(candidate.atomic) {
		bool selected=false;
		for(unsigned i=0;i<batch.count;i++) selected|=batch.candidates[i].atomic==candidate.atomic;
		if(!selected) {
			unsigned index=std::min(3u,batch.count);
			if(batch.count==4) while(index>0 &&
			   (batch.candidates[index].atomic==context.chassis.atomic ||
			    batch.candidates[index].atomic==context.upper.atomic)) index--;
			batch.candidates[index]=candidate;batch.count=std::min(4u,batch.count+1);
		}
	}
	s.pending=Hit();
	if(!batch.count) { batch=Batch(); DEFORM_DIAG(diagnostics.noPanel++; ReportDiagnostics("no-panel")); }
}
void BeginCandidate() {
	State &s=*batch.state;
	if(!Live(s) || uint32(CTimer::GetTimeInMilliseconds()-batch.hit.time)>2500) { CancelBatch(); return; }
	CVehicle *vehicle=CPools::GetVehicle(s.handle);
	if(!Eligible(vehicle) || !Near(vehicle)) { CancelBatch(); return; }
	if(batch.next>=batch.count) { batch=Batch(); return; }
	Candidate candidate=batch.candidates[batch.next++];
	RestoreContext context={&s,false,candidate.atomic,false};
	RpClumpForAllAtomics(s.clump,RestoreCB,&context);
	if(!context.found || !BodyFrame(candidate.atomic) || !Supported(candidate.atomic->geometry)) return;
	Part *part=nullptr;
	for(Part &p:s.parts) if(p.atomic==candidate.atomic) { part=&p; break; }
	if(!part) for(Part &p:s.parts) if(!p.atomic) { part=&p; break; }
	if(!part) return;
	RpGeometry *source=candidate.atomic->geometry;
	if(part->original && part->current!=source) {
		// Another system replaced this atomic's geometry while the car survived.
		// Retain that new baseline instead of restoring an obsolete model pointer.
		part->original->destroy();
		privateVertices-=part->vertices; privateTriangles-=part->triangles; privateBytes-=part->bytes;
		*part=Part();
	}
	bool renderSource=source->meshHeader && source->meshHeader->numMeshes>0;
	size_t triangleCapacity=size_t(source->numTriangles);
	if(renderSource) {
		// This upper bound includes degenerate strip connectors; emitted triangles
		// are checked again before push_back. Never trust GeoStruct's matIds when
		// a DFF's actual render BinMesh carries a different material assignment.
		triangleCapacity=std::min(size_t(120000),size_t(source->meshHeader->totalIndices));
		if(source->meshHeader->flags==0) triangleCapacity=std::min(size_t(120000),size_t(source->meshHeader->totalIndices/3));
	}
	size_t transient=size_t(source->numVertices)*(sizeof(Vertex)+sizeof(DentSnapshot)+12+24+8*source->numTexCoordSets*2+8)+
	                 triangleCapacity*(sizeof(Triangle)+sizeof(rw::Triangle)+6);
	// Check the original-size budget before allocating scratch arrays; subdivision
	// has a second budget check before a result geometry can be created.
	if(transient>MAX_JOB_BYTES || privateVertices-part->vertices+source->numVertices>MAX_VERTICES ||
	   privateTriangles-part->triangles+source->numTriangles>MAX_TRIANGLES) return;
	job.state=&s; job.atomic=candidate.atomic; job.part=part; job.source=source;
	source->addRef(); job.toCar=candidate.transform; job.toCar.Inverse(job.fromCar);
	job.hit=batch.hit; job.subdivide=!part->original && source->numVertices<=4000 && source->numTriangles<=6000;
	job.mesh.vertices.resize(source->numVertices); job.renderSource=renderSource;
	if(renderSource) {
		job.mesh.triangles.reserve(triangleCapacity);
		uint32 countedIndices=0;
		for(uint32 i=0;i<source->meshHeader->numMeshes;i++) {
			const rw::Mesh &mesh=source->meshHeader->getMeshes()[i];
			int material=-1;
			for(int j=0;j<source->matList.numMaterials;j++) if(source->matList.materials[j]==mesh.material) { material=j; break; }
			if(material<0 || (mesh.numIndices && !mesh.indices) || mesh.numIndices>source->meshHeader->totalIndices) { CancelJob(); return; }
			if(mesh.numIndices>source->meshHeader->totalIndices-countedIndices) { CancelJob(); return; }
			countedIndices+=mesh.numIndices;
			job.renderMaterial.push_back(uint16(material));
		}
		if(countedIndices!=source->meshHeader->totalIndices) { CancelJob(); return; }
	}else job.mesh.triangles.resize(source->numTriangles);
	if(source->colors) job.mesh.colours.resize(source->numVertices);
	job.mesh.uv.resize(source->numTexCoordSets);
	for(auto &set:job.mesh.uv) set.resize(source->numVertices);
	job.bodyMaterial.resize(source->matList.numMaterials);
	const CVehicleModelInfo *model=static_cast<CVehicleModelInfo*>(CModelInfo::GetModelInfo(vehicle->GetModelIndex()));
	for(int i=0;i<source->matList.numMaterials;i++) job.bodyMaterial[i]=BodyMaterial(*model,source->matList.materials[i])?1:0;
}
size_t EstimateBytes(const Mesh &m) {
	// CPU geometry, saved rest positions, Vulkan CPU/GPU interleaved VBOs, and
	// CPU/GPU indices. Original model memory is shared and was already resident.
	size_t vertexStride=28+8*std::max(size_t(1),m.uv.size());
	return m.vertices.size()*(12+12+4+8*m.uv.size()+12+vertexStride*2)+m.triangles.size()*(8+6+6+6);
}
bool ValidateJob() {
	if(!job.state || !Live(*job.state)) return false;
	CVehicle *v=CPools::GetVehicle(job.state->handle);
	if(!Eligible(v) || !Near(v) || uint32(CTimer::GetTimeInMilliseconds()-job.hit.time)>2500) return false;
	RestoreContext context={job.state,false,job.atomic,false};
	RpClumpForAllAtomics(job.state->clump,RestoreCB,&context);
	return context.found && job.atomic->geometry==job.source && BodyFrame(job.atomic);
}
void AdvanceJob(unsigned &vertexWork,unsigned &triangleWork,bool &subdivided,bool &committed) {
	if(!ValidateJob()) { CancelJob("invalid-job"); return; }
	RpGeometry *s=job.source;
	Mesh &m=job.mesh;
	unsigned end;
	switch(job.phase) {
	case 0: // Copy CPU vertices in bounded chunks. Source has a lifetime ref.
		end=std::min(unsigned(s->numVertices),job.cursor+(VERTEX_WORK-vertexWork));
		vertexWork+=end-job.cursor;
		for(unsigned i=job.cursor;i<end;i++) {
			Vertex &v=m.vertices[i]; v.pos=V(s->morphTargets[0].vertices[i]);
			v.normal=s->morphTargets[0].normals ? V(s->morphTargets[0].normals[i]) : Vec(0,0,1);
			v.rest=job.part->current==s && job.part->rest.size()==m.vertices.size()?job.part->rest[i]:v.pos;
			if(s->colors) { const RwRGBA &c=s->colors[i]; m.colours[i]={c.red,c.green,c.blue,c.alpha}; }
			for(unsigned u=0;u<m.uv.size();u++) m.uv[u][i]={s->texCoords[u][i].u,s->texCoords[u][i].v};
		}
		job.cursor=end;
		if(end==unsigned(s->numVertices)) { job.cursor=0; job.phase++; }
		break;
	case 1: // Body masks veto indices shared with any protected material.
		if(job.renderSource) {
			rw::MeshHeader *header=s->meshHeader;
			bool strip=header->flags==rw::MeshHeader::TRISTRIP;
			while(job.meshIndex<header->numMeshes && triangleWork<TRIANGLE_WORK) {
				const rw::Mesh &mesh=header->getMeshes()[job.meshIndex];
				if(job.indexCursor+2>=mesh.numIndices) { job.meshIndex++; job.indexCursor=0; continue; }
				Triangle t; triangleWork++;
				bool triangle=RenderTriangle(mesh.indices,mesh.numIndices,job.indexCursor,strip,job.renderMaterial[job.meshIndex],t);
				job.indexCursor+=strip?1:3;
				if(t.v[0]>=s->numVertices || t.v[1]>=s->numVertices || t.v[2]>=s->numVertices) { CancelJob(); return; }
				if(!triangle) continue;
				if(m.triangles.size()>=120000) { CancelJob(); return; }
				m.triangles.push_back(t); MarkTriangle(m,t,job.bodyMaterial[t.material]!=0);
			}
			if(job.meshIndex==header->numMeshes) {
				job.cursor=0; job.phase++;
				if(m.triangles.empty()) { CancelJob(); return; }
				job.subdivide=job.subdivide && m.triangles.size()<=6000;
			}
			break;
		}
		end=std::min(unsigned(s->numTriangles),job.cursor+(TRIANGLE_WORK-triangleWork));
		triangleWork+=end-job.cursor;
		for(unsigned i=job.cursor;i<end;i++) {
			const rw::Triangle &t=s->triangles[i];
			if(t.v[0]>=s->numVertices || t.v[1]>=s->numVertices || t.v[2]>=s->numVertices || t.matId>=job.bodyMaterial.size()) { CancelJob(); return; }
			m.triangles[i]={{t.v[0],t.v[1],t.v[2]},t.matId}; MarkTriangle(m,m.triangles[i],job.bodyMaterial[t.matId]!=0);
		}
		job.cursor=end;
		if(end==unsigned(s->numTriangles)) { job.cursor=0; job.phase++; }
		break;
	case 2:
		// Only sparse classic meshes are refined, at most three bounded passes.
		if(!job.subdivide || job.passes==3) job.phase++;
		else if(!subdivided) {
			subdivided=true;
			if(Subdivide(m,job.toCar,0.45f,12000,24000)) job.passes++; else job.phase++;
		}
		break;
	case 3:
		if(job.before.empty()) job.before.resize(m.vertices.size());
		end=std::min(unsigned(m.vertices.size()),job.cursor+(VERTEX_WORK-vertexWork));
		vertexWork+=end-job.cursor;
		for(unsigned i=job.cursor;i<end;i++) {
			job.before[i]=SnapshotVertex(m.vertices[i]);
			if(DentVertex(m.vertices[i],job.toCar,job.fromCar,job.hit.point,job.hit.normal,job.hit.radius,job.hit.depth,job.hit.maxDent)) job.changed++;
		}
		job.cursor=end;
		if(end==m.vertices.size()) {
			job.cursor=0; job.phase++;
			if(!job.changed) { CancelJob("no-movable-vertices"); return; }
		}
		break;
	case 4:
		// A smooth continuous field can still invert a sparse/skinny triangle.
		// Validate the actual piecewise-linear mesh against both its previous and
		// original shape, sharing the ordinary per-frame triangle budget.
		end=std::min(unsigned(m.triangles.size()),job.cursor+(TRIANGLE_WORK-triangleWork));
		while(job.cursor<end) {
			triangleWork++;
			if(!ValidDentTriangle(m,m.triangles[job.cursor++],job.before)) {
				if(++job.backtracks>8) { CancelJob("topology"); return; }
				job.cursor=0; job.phase=5; break;
			}
		}
		if(job.phase==4 && job.cursor==m.triangles.size()) { job.cursor=0; job.phase=6; }
		break;
	case 5:
		end=std::min(unsigned(m.vertices.size()),job.cursor+(VERTEX_WORK-vertexWork));
		vertexWork+=end-job.cursor;
		for(unsigned i=job.cursor;i<end;i++) BacktrackDentVertex(m.vertices[i],job.before[i],0.5f);
		job.cursor=end;
		if(end==m.vertices.size()) { job.cursor=0; job.phase=4; }
		break;
	case 6: {
		job.bytes=EstimateBytes(m);
		size_t transient=m.vertices.size()*(sizeof(Vertex)+sizeof(DentSnapshot)+12+24+16*m.uv.size()+8)+
		                 m.triangles.size()*(sizeof(Triangle)+sizeof(rw::Triangle)+6);
		if(privateVertices-job.part->vertices+m.vertices.size()>MAX_VERTICES ||
		   privateTriangles-job.part->triangles+m.triangles.size()>MAX_TRIANGLES ||
		   privateBytes-job.part->bytes+job.bytes>MAX_BYTES || transient>MAX_JOB_BYTES) { CancelJob("budget"); return; }
		// Explicit triangle lists avoid a costly strip builder and preserve winding.
		uint32 flags=(s->flags&~rw::Geometry::TRISTRIP)|(uint32(s->numTexCoordSets)<<16);
		job.result=rw::Geometry::create(int32(m.vertices.size()),int32(m.triangles.size()),flags);
		if(!job.result) { CancelJob(); return; }
		for(int i=0;i<s->matList.numMaterials;i++) if(job.result->matList.appendMaterial(s->matList.materials[i])<0) { CancelJob(); return; }
		job.materialCounts.resize(s->matList.numMaterials,0); job.materialOffsets.resize(s->matList.numMaterials,0);
		job.rest.resize(m.vertices.size());
		job.boundMin=job.boundMax=m.vertices[0].pos;
		job.phase++;
		break;
	}
	case 7:
		end=std::min(unsigned(m.vertices.size()),job.cursor+(VERTEX_WORK-vertexWork));
		vertexWork+=end-job.cursor;
		for(unsigned i=job.cursor;i<end;i++) {
			const Vertex &v=m.vertices[i]; job.result->morphTargets[0].vertices[i]=RW(v.pos); job.rest[i]=v.rest;
			DEFORM_DIAG(job.displacement=std::max(job.displacement,Length(job.toCar.Vector(v.pos-v.rest)));
			job.heightChange=std::max(job.heightChange,std::fabs(job.toCar.Vector(v.pos-job.before[i].pos).z)));
			if(job.result->morphTargets[0].normals) job.result->morphTargets[0].normals[i]=RW(v.normal);
			if(job.result->colors) { const Colour &c=m.colours[i]; RwRGBA &o=job.result->colors[i]; o.red=c.r;o.green=c.g;o.blue=c.b;o.alpha=c.a; }
			for(unsigned u=0;u<m.uv.size();u++) { job.result->texCoords[u][i].u=m.uv[u][i].u; job.result->texCoords[u][i].v=m.uv[u][i].v; }
			job.boundMin=Vec(std::min(job.boundMin.x,v.pos.x),std::min(job.boundMin.y,v.pos.y),std::min(job.boundMin.z,v.pos.z));
			job.boundMax=Vec(std::max(job.boundMax.x,v.pos.x),std::max(job.boundMax.y,v.pos.y),std::max(job.boundMax.z,v.pos.z));
		}
		job.cursor=end;
		if(end==m.vertices.size()) { job.cursor=0; job.phase++; }
		break;
	case 8:
		end=std::min(unsigned(m.triangles.size()),job.cursor+(TRIANGLE_WORK-triangleWork));
		triangleWork+=end-job.cursor;
		for(unsigned i=job.cursor;i<end;i++) {
			const Triangle &t=m.triangles[i]; rw::Triangle &out=job.result->triangles[i];
			out.v[0]=t.v[0];out.v[1]=t.v[1];out.v[2]=t.v[2];out.matId=t.material;
			job.materialCounts[t.material]+=3;
		}
		job.cursor=end;
		if(end==m.triangles.size()) { job.cursor=0; job.phase++; }
		break;
	case 9: {
		rw::MeshHeader *header=job.result->allocateMeshes(int32(job.materialCounts.size()),uint32(m.triangles.size()*3),false);
		if(!header) { CancelJob(); return; }
		for(unsigned i=0;i<job.materialCounts.size();i++) {
			header->getMeshes()[i].numIndices=job.materialCounts[i];
			header->getMeshes()[i].material=s->matList.materials[i];
		}
		header->setupIndices(); job.phase++;
		break;
	}
	case 10:
		end=std::min(unsigned(m.triangles.size()),job.cursor+(TRIANGLE_WORK-triangleWork));
		triangleWork+=end-job.cursor;
		for(unsigned i=job.cursor;i<end;i++) {
			const Triangle &t=m.triangles[i];
			uint16 *indices=job.result->meshHeader->getMeshes()[t.material].indices+job.materialOffsets[t.material];
			indices[0]=t.v[0];indices[1]=t.v[1];indices[2]=t.v[2]; job.materialOffsets[t.material]+=3;
		}
		job.cursor=end;
		if(end==m.triangles.size()) { job.cursor=0; job.phase++; }
		break;
	case 11: {
		// The only render-visible mutation. One global job commit per real frame;
		// Vulkan instances its private VBO during ordinary rendering, behind fences.
		rw::Sphere &bound=job.result->morphTargets[0].boundingSphere;
		bound.center=RW((job.boundMin+job.boundMax)*0.5f); bound.radius=Length(job.boundMax-job.boundMin)*0.5f+0.01f;
		Part &part=*job.part;
		if(!part.original) { part.original=job.source; part.original->addRef(); }
		privateVertices=privateVertices-part.vertices+m.vertices.size();
		privateTriangles=privateTriangles-part.triangles+m.triangles.size();
		privateBytes=privateBytes-part.bytes+job.bytes;
		part.atomic=job.atomic; part.current=job.result; part.vertices=m.vertices.size();
		part.triangles=m.triangles.size(); part.bytes=job.bytes; part.rest.swap(job.rest);
		job.atomic->setGeometry(job.result,0);
		DEFORM_DIAG(const bool firstDent=!job.state->hasDent);
		job.state->lastDent=CTimer::GetTimeInMilliseconds(); job.state->hasDent=true;
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
		diagnostics.commits++; diagnostics.displacement=job.displacement;
		// Report this committed hit, not a weaker collision received meanwhile.
		diagnostics.model=CPools::GetVehicle(job.state->handle)->GetModelIndex();
		diagnostics.handle=job.state->handle;diagnostics.otherHandle=job.hit.otherHandle;
		diagnostics.otherModel=job.hit.otherModel;diagnostics.otherType=job.hit.otherType;
		diagnostics.impulse=job.hit.impulse; diagnostics.depth=job.hit.depth;
		diagnostics.localPoint=job.hit.point;
		ReportDiagnostics("commit",firstDent);
#if defined(__ANDROID__)
		// Early parts expose the body and panels without per-frame logs.
		if(diagnostics.commits<=4 || firstDent)
			__android_log_print(ANDROID_LOG_INFO,"MiamiVR",
				"[VehicleDeformation] panel model=%d frame=%s flags=%x vertices=%u changed=%u height=%.3f displacement=%.3f normal=%.2f,%.2f,%.2f receiver=%d other=%d/%d",
				diagnostics.model,GetFrameNodeName(RpAtomicGetFrame(job.atomic)),
				unsigned(CVisibilityPlugins::GetAtomicId(job.atomic)),unsigned(m.vertices.size()),job.changed,
				job.heightChange,job.displacement,job.hit.normal.x,job.hit.normal.y,job.hit.normal.z,
				job.state->handle,job.hit.otherHandle,job.hit.otherModel);
#endif
#endif
		committed=true;
		CancelJob();
		break;
	}
	}
}
}

void RecordCollision(CVehicle *vehicle,const CVector &worldPoint,const CVector &receiverInwardNormal,
                     float impulse,const CEntity *other,const CollisionPose *collisionPose) {
	if(!enabled) return;
	DEFORM_DIAG(diagnostics.contacts++);
	if(!Eligible(vehicle) || !other || !Near(vehicle) || !std::isfinite(impulse)) return;
	// Entity classification is intentionally independent of mutable mission flags.
	if(other->GetType()!=ENTITY_TYPE_BUILDING && other->GetType()!=ENTITY_TYPE_OBJECT && other->GetType()!=ENTITY_TYPE_VEHICLE) return;
	float depth=HitDepth(impulse,vehicle->m_fMass,GetStrengthPercent()*0.01f,GetThresholdPercent()*0.01f);
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
	diagnostics.model=vehicle->GetModelIndex(); diagnostics.impulse=impulse; diagnostics.depth=depth;
	diagnostics.handle=CPools::GetVehicleRef(vehicle);diagnostics.otherType=other->GetType();
	diagnostics.otherHandle=diagnostics.otherModel=-1;
	if(other->GetType()==ENTITY_TYPE_VEHICLE) {
		CVehicle *peer=static_cast<CVehicle*>(const_cast<CEntity*>(other));
		diagnostics.otherHandle=CPools::GetVehicleRef(peer);diagnostics.otherModel=peer->GetModelIndex();
	}
#endif
	Vec point=V(worldPoint),normal=V(receiverInwardNormal);
	if(depth<0.025f) { DEFORM_DIAG(diagnostics.weak++; ReportDiagnostics("weak")); return; }
	if(!Finite(point) || !Finite(normal) || Dot(normal,normal)<0.25f) return;
	Transform inverse;
	if(!(collisionPose?Matrix(*collisionPose):Matrix(vehicle->GetMatrix())).Inverse(inverse)) return;
	DEFORM_DIAG(diagnostics.localPoint=inverse.Point(point));
	State *state=Acquire(vehicle);
	if(!state) { DEFORM_DIAG(diagnostics.noSlot++; ReportDiagnostics("no-slot")); return; }
	DEFORM_DIAG(diagnostics.admitted++);
	if(impulse>state->pending.impulse) {
		state->pending.point=inverse.Point(point); state->pending.normal=Unit(inverse.Vector(normal));
		state->pending.depth=depth; state->pending.impulse=impulse; state->pending.time=CTimer::GetTimeInMilliseconds();
		// A queued impact retains one set of parameters through every mesh phase.
		state->pending.radius=0.85f*GetRadiusPercent()*0.01f;
		state->pending.maxDent=GetMaxDentCentimeters()*0.01f;
		DEFORM_DIAG(state->pending.otherHandle=diagnostics.otherHandle;state->pending.otherModel=diagnostics.otherModel;
		state->pending.otherType=diagnostics.otherType);
	}
	state->lastUse=CTimer::GetTimeInMilliseconds();
}
void Update() {
	if(!enabled) return;
	uint32 now=CTimer::GetTimeInMilliseconds();
	State *next=nullptr;
	for(State &state:states) {
		if(!state.owner) continue;
		if(!Live(state)) { ReleaseState(state,false); continue; }
		if(batch.state==&state) continue;
		if(state.pending.depth>0 && now-state.pending.time>2500) {
			state.pending=Hit(); DEFORM_DIAG(diagnostics.discarded++);
		}
		if(!state.hasDent && state.pending.depth<=0) { ReleaseState(state,false); continue; }
		CVehicle *vehicle=CPools::GetVehicle(state.handle);
		if(state.pending.depth<=0 || !Eligible(vehicle) || !Near(vehicle) ||
		   (state.hasDent && now-state.lastDent<COOLDOWN_MS)) continue;
		if(!next || now-state.pending.time>now-next->pending.time) next=&state;
	}
	// Existing jobs progress even if their owner is behind the viewer, hidden by
	// a first-person camera or omitted from a full render visibility list.
	if(batch.state) Process(nullptr);
	else if(next) Process(CPools::GetVehicle(next->handle));
}
void Process(CVehicle *vehicle) {
	if(!enabled) return;
	uint32 frame=CTimer::GetFrameCounter(),now=CTimer::GetTimeInMilliseconds();
	if(workFrame==frame) return;
	if(!batch.state) {
		if(!Eligible(vehicle) || !Near(vehicle)) return;
		State *s=Find(vehicle);
		if(!s || s->pending.depth<=0.0f) return;
		if(now-s->pending.time>2500) { s->pending=Hit(); DEFORM_DIAG(diagnostics.discarded++); return; }
		if(s->hasDent && now-s->lastDent<COOLDOWN_MS) return;
		workFrame=frame; // Selection is event work even when no panel qualifies.
		BeginBatch(*s,vehicle);
	}
	if(!batch.state) return;
	workFrame=frame;
	unsigned vertexWork=0,triangleWork=0;
	bool subdivided=false,committed=false;
	// Cheap phase transitions run immediately; all copy/deform/index loops share
	// one per-frame budget. Allocation is bounded by MAX_JOB_BYTES separately.
	// A subdivision pass (<=24k triangles) and one GPU-visible commit are the
	// additional event operations, each permitted at most once per frame.
	// Selection separately probes at most MAX_ATOMICS*PANEL_PROBES vertices.
	for(unsigned transition=0;transition<24 && batch.state && !committed;transition++) {
		if(!job.state) { BeginCandidate(); if(!job.state) continue; }
		unsigned phase=job.phase,cursor=job.cursor,passes=job.passes;
		AdvanceJob(vertexWork,triangleWork,subdivided,committed);
		if(job.state && phase==job.phase && cursor==job.cursor && passes==job.passes) break;
	}
}
void Release(CEntity *entity) {
	if(!enabled) return;
	// Safe from CEntity::~CEntity: no derived fields/pool access is required.
	State *s=Find(entity);
	if(s) ReleaseState(*s,entity && entity->m_rwObject==reinterpret_cast<RwObject*>(s->clump));
}
void PrepareDetachedAtomic(RpAtomic *atomic) {
	if(!enabled || !atomic) return;
	for(State &s:states) for(Part &p:s.parts)
		if(p.original && p.current==atomic->geometry && p.original!=p.current) {
			atomic->setGeometry(p.original,0); return;
		}
}
void SetVehicleDeformationEnabled(bool value) {
	if(enabled==value) return;
	if(!value) {
		CancelBatch();
		for(State &s:states) if(s.owner) ReleaseState(s,Live(s));
	}
	enabled=value;
	if(value) { workFrame=~0u; DEFORM_DIAG(diagnostics=Diagnostics(); ReportDiagnostics("enabled",true)); }
}
#undef DEFORM_DIAG
}
#else
namespace VehicleDeformation {
bool enabled=false;
void SetVehicleDeformationEnabled(bool) {}
void RecordCollision(CVehicle*,const CVector&,const CVector&,float,const CEntity*,const CollisionPose*) {}
void Process(CVehicle*) {}
void Update() {}
void Release(CEntity*) {}
void PrepareDetachedAtomic(RpAtomic*) {}
}
#endif
