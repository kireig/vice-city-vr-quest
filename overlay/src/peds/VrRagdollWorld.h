#pragma once

#include "VrRagdollPhysics.h"
#include "VrRagdollVehicle.h"
#if !defined(VR_RAGDOLL_WORLD_NO_ENGINE) && !defined(VR_RAGDOLL_WORLD_ENGINE_TEST)
#include "World.h"
#endif

// Static COL geometry, copied from the nearby sectors. No entity pointers
// survive a gather, so unloading a building cannot invalidate a sleeping ped.
namespace VrRagdollWorld
{
namespace P = VrRagdollPhysics;
namespace V = VrRagdollVehicle;
enum { MAX_TRIANGLES = 96, MAX_SHAPES = 16, MAX_SECTORS = 16,
	MAX_LIST_NODES = 256, MAX_PRIMITIVE_SCAN = 4096, MAX_NORMALS = 3,
	MAX_PROBES = P::SIM_BONES + P::SKELETON_STICKS * 2 };
static const float CACHE_MARGIN = .55f, CACHE_SECONDS = .20f;

struct Bounds { CVector min, max; };
inline Bounds Around(const CVector &a, const CVector &b, float radius)
{
	Bounds r;
	r.min = CVector(V::MinF(a.x,b.x)-radius,V::MinF(a.y,b.y)-radius,V::MinF(a.z,b.z)-radius);
	r.max = CVector(V::MaxF(a.x,b.x)+radius,V::MaxF(a.y,b.y)+radius,V::MaxF(a.z,b.z)+radius);
	return r;
}
inline bool Overlap(const Bounds &a, const Bounds &b)
{
	return a.min.x <= b.max.x && a.max.x >= b.min.x &&
		a.min.y <= b.max.y && a.max.y >= b.min.y && a.min.z <= b.max.z && a.max.z >= b.min.z;
}
inline bool Contains(const Bounds &outer, const Bounds &inner)
{
	return outer.min.x <= inner.min.x && outer.max.x >= inner.max.x &&
		outer.min.y <= inner.min.y && outer.max.y >= inner.max.y &&
		outer.min.z <= inner.min.z && outer.max.z >= inner.max.z;
}
inline Bounds RequiredBounds(const P::State *state, float timeStep)
{
	Bounds bounds = Around(state->pos[0],state->pos[0],P::radius[0]);
	for(int i = 0; i < P::SIM_BONES; ++i){
		const CVector predicted = state->pos[i] + (state->pos[i]-state->prev[i])*(timeStep/P::STEP) +
			CVector(0,0,-P::FALL_GRAVITY*timeStep*timeStep);
		const Bounds p = Around(state->pos[i],predicted,P::radius[i]);
		bounds.min = CVector(V::MinF(bounds.min.x,p.min.x),V::MinF(bounds.min.y,p.min.y),V::MinF(bounds.min.z,p.min.z));
		bounds.max = CVector(V::MaxF(bounds.max.x,p.max.x),V::MaxF(bounds.max.y,p.max.y),V::MaxF(bounds.max.z,p.max.z));
	}
	return bounds;
}
struct WorldShape { V::Shape shape; V::Pose pose; Bounds bounds; };
struct GatherStats {
	unsigned sectors, listNodes, primitiveScans, rejected, overflow;
	GatherStats() : sectors(0), listNodes(0), primitiveScans(0), rejected(0), overflow(0) {}
};
struct Cache {
	V::Triangle triangles[MAX_TRIANGLES];
	WorldShape shapes[MAX_SHAPES];
	int triangleCount, shapeCount;
	Bounds bounds;
	float remaining;
	bool valid, incomplete;
	GatherStats stats;
	Cache() : triangleCount(0), shapeCount(0), remaining(0), valid(false), incomplete(false) {}
};
static_assert(sizeof(Cache)*220 < 4*1024*1024, "World geometry must fit the retained ped budget");

inline void BeginGather(Cache &cache, const Bounds &needed)
{
	cache.triangleCount = cache.shapeCount = 0;
	cache.bounds = Around(needed.min,needed.max,CACHE_MARGIN);
	cache.remaining = CACHE_SECONDS;
	cache.valid = true; cache.incomplete = false; cache.stats = GatherStats();
}
inline bool Reuse(Cache &cache, const Bounds &needed, float elapsed)
{
	cache.remaining -= elapsed;
	return cache.valid && cache.remaining > 0 && Contains(cache.bounds,needed);
}
inline void Overflow(Cache &cache) { cache.incomplete = true; ++cache.stats.overflow; }
inline float DistanceSq(const Bounds &bounds, const CVector &point)
{
	const CVector closest(V::ClampF(point.x,bounds.min.x,bounds.max.x),
		V::ClampF(point.y,bounds.min.y,bounds.max.y),V::ClampF(point.z,bounds.min.z,bounds.max.z));
	return (point-closest).MagnitudeSqr();
}
inline void AddTriangle(Cache &cache, const CVector &a, const CVector &b, const CVector &c)
{
	V::Triangle triangle;
	if(!V::MakeTriangle(a,b,c,triangle) || !Overlap(cache.bounds,Bounds{triangle.min,triangle.max})){
		++cache.stats.rejected; return;
	}
	for(int i = 0; i < cache.triangleCount; ++i){
		const V::Triangle &old = cache.triangles[i];
		if((old.min-triangle.min).MagnitudeSqr() > 1.e-10f || (old.max-triangle.max).MagnitudeSqr() > 1.e-10f) continue;
		const CVector vertices[3] = {old.a,old.b,old.c};
		bool same = true;
		for(int vertex = 0; vertex < 3; ++vertex)
			same = same && ((vertices[vertex]-a).MagnitudeSqr() < 1.e-10f ||
				(vertices[vertex]-b).MagnitudeSqr() < 1.e-10f || (vertices[vertex]-c).MagnitudeSqr() < 1.e-10f);
		if(same){ ++cache.stats.rejected; return; }
	}
	int index = cache.triangleCount;
	if(index == MAX_TRIANGLES){
		Overflow(cache);
		const CVector center = (cache.bounds.min+cache.bounds.max)*.5f;
		float farthest = (V::ClosestTrianglePoint(triangle,center)-center).MagnitudeSqr();
		index = -1;
		for(int i = 0; i < MAX_TRIANGLES; ++i){
			float distance = (V::ClosestTrianglePoint(cache.triangles[i],center)-center).MagnitudeSqr();
			if(distance > farthest){ farthest = distance; index = i; }
		}
		if(index < 0) return;
	}else ++cache.triangleCount;
	cache.triangles[index] = triangle;
}
inline void AddShape(Cache &cache, const V::Shape &shape, const V::Pose &pose)
{
	WorldShape world; world.shape = shape; world.pose = pose;
	const CVector localCenter = shape.radius > 0 ? shape.center : (shape.min+shape.max)*.5f;
	const CVector extent = shape.radius > 0 ? CVector(shape.radius,shape.radius,shape.radius) : (shape.max-shape.min)*.5f;
	const CVector center = pose.ToWorld(localCenter);
	const CVector worldExtent = shape.radius > 0 ? extent : CVector(
		fabsf(pose.right.x)*extent.x+fabsf(pose.forward.x)*extent.y+fabsf(pose.up.x)*extent.z,
		fabsf(pose.right.y)*extent.x+fabsf(pose.forward.y)*extent.y+fabsf(pose.up.y)*extent.z,
		fabsf(pose.right.z)*extent.x+fabsf(pose.forward.z)*extent.y+fabsf(pose.up.z)*extent.z);
	world.bounds.min = center-worldExtent; world.bounds.max = center+worldExtent;
	if(!Overlap(cache.bounds,world.bounds)){ ++cache.stats.rejected; return; }
	int index = cache.shapeCount;
	if(index == MAX_SHAPES){
		Overflow(cache);
		const CVector bodyCenter = (cache.bounds.min+cache.bounds.max)*.5f;
		float farthest = DistanceSq(world.bounds,bodyCenter); index = -1;
		for(int i = 0; i < MAX_SHAPES; ++i){
			float distance = DistanceSq(cache.shapes[i].bounds,bodyCenter);
			if(distance > farthest){ farthest = distance; index = i; }
		}
		if(index < 0) return;
	}else ++cache.shapeCount;
	cache.shapes[index] = world;
}

struct Contact {
	CVector center, normal;
	float time, depth;
	bool hit, swept;
	Contact() : time(2), depth(-1), hit(false), swept(false) {}
};
struct QueryStats {
	unsigned boundsTests, narrowTests, fallbackQueries;
	QueryStats() : boundsTests(0), narrowTests(0), fallbackQueries(0) {}
};
inline void Consider(Contact &best, const CVector &center, const CVector &normal, float time, float depth, bool swept)
{
	if((swept && (!best.swept || time < best.time)) || (!swept && !best.swept && depth > best.depth)){
		best.hit = true; best.swept = swept; best.center = center; best.normal = normal; best.time = time; best.depth = depth;
	}
}
inline void RememberFace(Contact *planes, int &count, const V::Triangle &face,
	const CVector &center, const CVector &normal, bool swept)
{
	if(fabsf(DotProduct(face.normal,normal)) < .99999f) return;
	for(int i = 0; i < count; ++i)
		if(DotProduct(planes[i].normal,normal) > .99999f &&
			fabsf(DotProduct(center-planes[i].center,normal)) < .00001f) return;
	if(count == MAX_NORMALS) return;
	planes[count].center = center; planes[count].normal = normal; planes[count++].swept = swept;
}
inline Contact Find(const Cache &cache, const CVector &from, const CVector &to, float radius, QueryStats *stats = 0,
	bool fullSweep = true)
{
	Contact best;
	Contact testedPlanes[MAX_NORMALS]; int testedCount = 0;
	const Bounds sweepBounds = Around(from,to,radius+V::CONTACT_SKIN);
	for(int i = 0; i < cache.triangleCount+cache.shapeCount; ++i){
		const bool triangle = i < cache.triangleCount;
		const V::Triangle *face = triangle ? &cache.triangles[i] : 0;
		const WorldShape *shape = triangle ? 0 : &cache.shapes[i-cache.triangleCount];
		if(stats) ++stats->boundsTests;
		if(!Overlap(sweepBounds,triangle ? Bounds{face->min,face->max} : shape->bounds)) continue;
		bool redundant = false;
		for(int plane = 0; triangle && plane < testedCount; ++plane){
			const Contact &tested = testedPlanes[plane];
			if(fabsf(DotProduct(tested.normal,face->normal)) <= .99999f) continue;
			// A face-interior hit already supplies the earliest contact with
			// its supporting plane. Coplanar tessellation/duplicate COL faces
			// cannot improve it; parallel faces behind a swept hit are later.
			// Edge hits have a different normal and deliberately take the full
			// finite-triangle path. This is not merging triangles into an
			// infinite solid plane, so openings still remain open.
			const float separation = DotProduct(face->a-tested.center,tested.normal)+radius+V::CONTACT_SKIN;
			if(fabsf(separation) < .00001f || (tested.swept && separation < 0)){ redundant = true; break; }
		}
		if(redundant) continue;
		if(stats) ++stats->narrowTests;
		const CVector start = triangle ? from : shape->pose.ToLocal(from), end = triangle ? to : shape->pose.ToLocal(to);
		CVector projected, normal; float depth, time;
		if(!fullSweep){
			// Later solver passes already have the free-flight contact planes.
			// Test new finite overlaps cheaply. Only a motion crossing a face
			// needs another sweep; do not repeat rounded-edge CCD for all the
			// triangles near a resting point on every constraint iteration.
			const bool overlap = triangle ? V::ProjectTriangle(*face,end,radius,projected,normal,depth) :
				V::ProjectShape(shape->shape,end,radius,projected,normal,depth);
			if(overlap){
				Consider(best,triangle ? projected : shape->pose.ToWorld(projected),
					triangle ? normal : shape->pose.Direction(normal),1,depth,false);
				if(triangle && DotProduct(start-face->a,face->normal)*DotProduct(end-face->a,face->normal) >= 0)
					RememberFace(testedPlanes,testedCount,*face,projected,normal,false);
			}
			if(triangle){
				const float a = DotProduct(start-face->a,face->normal), b = DotProduct(end-face->a,face->normal);
				if(a*b >= 0 || fabsf(a-b) < 1.e-6f) continue;
			}else if(overlap) continue;
		}
		const bool startOverlap = triangle ? V::ProjectTriangle(*face,start,radius,projected,normal,depth) :
			V::ProjectShape(shape->shape,start,radius,projected,normal,depth);
		// Keep the entry side of an existing overlap. A thin wall or a small
		// box must not eject a fast point from its opposite face.
		if(startOverlap && depth > 1.e-6f && DotProduct(end-start,normal) < -1.e-8f){
			Consider(best,triangle ? projected : shape->pose.ToWorld(projected),
				triangle ? normal : shape->pose.Direction(normal),0,depth,true);
			if(triangle) RememberFace(testedPlanes,testedCount,*face,projected,normal,true);
			continue;
		}
		const bool swept = triangle ? V::SweepTriangle(*face,start,end,radius,time,normal) :
			shape->shape.radius > 0 ? V::SweepSphere(start,end,shape->shape.center,shape->shape.radius+radius,time,normal) :
			V::SweepBox(shape->shape,start,end,radius,time,normal);
		if(swept){
			projected = start+(end-start)*time+normal*V::CONTACT_SKIN;
			Consider(best,triangle ? projected : shape->pose.ToWorld(projected),
				triangle ? normal : shape->pose.Direction(normal),time,0,true);
			if(triangle) RememberFace(testedPlanes,testedCount,*face,projected,normal,true);
		}else if(triangle ? V::ProjectTriangle(*face,end,radius,projected,normal,depth) :
			V::ProjectShape(shape->shape,end,radius,projected,normal,depth)){
			Consider(best,triangle ? projected : shape->pose.ToWorld(projected),
				triangle ? normal : shape->pose.Direction(normal),1,depth,false);
			if(triangle) RememberFace(testedPlanes,testedCount,*face,projected,normal,false);
		}
	}
	return best;
}

typedef bool (*FallbackQuery)(const CVector &,const CVector &,float,Contact &);
struct Probe { unsigned char a,b; float t,radius; };
struct StepContacts {
	const Cache *cache;
	CVector start[P::SIM_BONES], incoming[P::SIM_BONES], normals[P::SIM_BONES][MAX_NORMALS];
	unsigned char normalCount[P::SIM_BONES];
	Probe probes[MAX_PROBES]; int probeCount;
	Contact planes[MAX_PROBES][MAX_NORMALS];
	unsigned char planeCount[MAX_PROBES];
	CVector previous[MAX_PROBES];
	Contact fallback[P::SIM_BONES];
	FallbackQuery fallbackQuery;
	QueryStats stats;
	float timeStep;
	bool built;
	StepContacts(const Cache &geometry, const P::State *state, float dt = P::STEP, FallbackQuery query = 0) :
		cache(&geometry), probeCount(P::SIM_BONES), fallbackQuery(query), timeStep(dt), built(false)
	{
		const float damping = dt == P::STEP ? P::DAMPING : powf(P::DAMPING,dt/P::STEP);
		for(int i = 0; i < P::SIM_BONES; ++i){
			start[i] = state->pos[i]; normalCount[i] = 0;
			incoming[i] = (state->pos[i]-state->prev[i])*(damping/P::STEP);
			incoming[i].z -= P::FALL_GRAVITY*dt;
			probes[i] = Probe{static_cast<unsigned char>(i),static_cast<unsigned char>(i),0,P::radius[i]};
		}
		for(int i = 0; i < P::SKELETON_STICKS; ++i){
			const int a = P::sticks[i].a, b = P::sticks[i].b;
			const float distance = (start[b]-start[a]).Magnitude();
			// Long thighs/shins/forearms need interior volume: endpoint spheres
			// alone allow a thin post through the middle of a straight limb.
			if(distance <= (P::radius[a]+P::radius[b])*1.35f) continue;
			const int count = distance > (P::radius[a]+P::radius[b])*2.0f ? 2 : 1;
			for(int p = 1; p <= count; ++p){
				const float t = static_cast<float>(p)/static_cast<float>(count+1);
				probes[probeCount++] = Probe{static_cast<unsigned char>(a),static_cast<unsigned char>(b),t,
					P::radius[a]*(1-t)+P::radius[b]*t};
			}
		}
		for(int i = 0; i < probeCount; ++i){
			planeCount[i] = 0;
			previous[i] = start[probes[i].a]*(1-probes[i].t)+start[probes[i].b]*probes[i].t;
		}
	}
};
inline void Remember(StepContacts &context, P::State *state, int node, const CVector &normal)
{
	P::RecordContact(state,node,normal,CVector(0,0,0),true);
	for(int i = 0; i < context.normalCount[node]; ++i)
		if(DotProduct(context.normals[node][i],normal) > .98f) return;
	if(context.normalCount[node] < MAX_NORMALS)
		context.normals[node][context.normalCount[node]++] = normal;
}
inline void Apply(StepContacts &context, P::State *state, const Probe &probe, const Contact &contact)
{
	const float a = 1-probe.t, b = probe.t;
	const CVector point = state->pos[probe.a]*a+state->pos[probe.b]*b;
	const float depth = DotProduct(contact.center-point,contact.normal);
	if(depth < -.005f) return;
	if(depth > 0){
		const float mobility = P::invMass[probe.a]*a*a+P::invMass[probe.b]*b*b;
		state->pos[probe.a] += contact.normal*(depth*P::invMass[probe.a]*a/mobility);
		if(probe.a != probe.b) state->pos[probe.b] += contact.normal*(depth*P::invMass[probe.b]*b/mobility);
	}
	Remember(context,state,probe.a,contact.normal);
	if(probe.a != probe.b) Remember(context,state,probe.b,contact.normal);
}
inline void Project(P::State *state, void *opaque)
{
	StepContacts &context = *static_cast<StepContacts*>(opaque);
	bool uncovered = context.cache->incomplete;
	if(!context.built && !uncovered)
		for(int i = 0; i < P::SIM_BONES; ++i)
			uncovered = uncovered || !Contains(context.cache->bounds,Around(context.start[i],state->pos[i],P::radius[i]));
	if(!context.built && uncovered && context.fallbackQuery){
		for(int i = 0; i < P::SIM_BONES; ++i){
			if((state->pos[i]-context.start[i]).MagnitudeSqr() < .0001f) continue;
			++context.stats.fallbackQueries;
			context.fallbackQuery(context.start[i],state->pos[i],P::radius[i],context.fallback[i]);
		}
	}
	for(int i = 0; i < context.probeCount; ++i){
		const Probe &probe = context.probes[i];
		const CVector from = context.previous[i];
		for(int plane = 0; plane < context.planeCount[i]; ++plane)
			Apply(context,state,probe,context.planes[i][plane]);
		for(int corner = 0; corner < 2; ++corner){
			const CVector to = state->pos[probe.a]*(1-probe.t)+state->pos[probe.b]*probe.t;
			if(context.built && (to-from).MagnitudeSqr() < .000004f) break;
			Contact contact = Find(*context.cache,from,to,probe.radius,&context.stats,!context.built);
			if(!contact.hit) break;
			Apply(context,state,probe,contact);
			bool known = false;
			for(int plane = 0; plane < context.planeCount[i]; ++plane)
				known = known || DotProduct(context.planes[i][plane].normal,contact.normal) > .98f;
			if(!known && context.planeCount[i] < MAX_NORMALS)
				context.planes[i][context.planeCount[i]++] = contact;
		}
		if(i < P::SIM_BONES && context.fallback[i].hit) Apply(context,state,probe,context.fallback[i]);
		context.previous[i] = state->pos[probe.a]*(1-probe.t)+state->pos[probe.b]*probe.t;
	}
	context.built = true;
}
inline void ResolveVelocity(P::State *state, const StepContacts &context)
{
	// Constraints and the car can push against a wall after free flight.
	// Close every encountered static normal, not just the last contact in
	// State. This also prevents a position correction becoming an impact kick.
	for(int i = 0; i < P::SIM_BONES; ++i){
		if(!context.normalCount[i]) continue;
		CVector velocity = (state->pos[i]-state->prev[i])*(1/P::STEP);
		for(int pass = 0; pass < 2; ++pass)
			for(int n = 0; n < context.normalCount[i]; ++n){
				const CVector &normal = context.normals[i][n];
				float outward = V::MaxF(0,DotProduct(context.incoming[i],normal));
				float speed = DotProduct(velocity,normal);
				velocity += normal*(V::ClampF(speed,0,outward)-speed);
			}
		state->prev[i] = state->pos[i]-velocity*P::STEP;
	}
}

#ifndef VR_RAGDOLL_WORLD_NO_ENGINE
inline bool StaticEntity(CEntity *entity)
{
	return entity && entity->bUsesCollision && !entity->bRemoveFromWorld && IsAreaVisible(entity->m_area) &&
		(entity->IsBuilding() || entity->IsDummy() || (entity->IsObject() && entity->GetIsStatic()));
}
inline bool ScanPrimitive(Cache &cache)
{
	if(cache.stats.primitiveScans == MAX_PRIMITIVE_SCAN){ Overflow(cache); return false; }
	++cache.stats.primitiveScans; return true;
}
inline void GatherEntity(Cache &cache, CEntity *entity)
{
	if(!StaticEntity(entity)) return;
	CColModel *model = entity->GetColModel();
	if(!model) return;
	V::Pose pose; pose.origin = entity->GetPosition(); pose.right = entity->GetRight();
	pose.forward = entity->GetForward(); pose.up = entity->GetUp();
	const CVector center = pose.ToWorld(model->boundingSphere.center);
	if(!Overlap(cache.bounds,Around(center,center,model->boundingSphere.radius))) return;
	for(int i = 0; i < model->numSpheres; ++i){
		if(!ScanPrimitive(cache)) return;
		V::Shape shape = {}; shape.center = model->spheres[i].center; shape.radius = model->spheres[i].radius;
		if(shape.radius > 0) AddShape(cache,shape,pose);
	}
	for(int i = 0; i < model->numBoxes; ++i){
		if(!ScanPrimitive(cache)) return;
		V::Shape shape = {}; shape.min = model->boxes[i].min; shape.max = model->boxes[i].max;
		AddShape(cache,shape,pose);
	}
	if(!model->triangles || !model->vertices) return;
	for(int i = 0; i < model->numTriangles; ++i){
		if(!ScanPrimitive(cache)) return;
		const CColTriangle &face = model->triangles[i];
		AddTriangle(cache,pose.ToWorld(model->vertices[face.a].Get()),
			pose.ToWorld(model->vertices[face.b].Get()),pose.ToWorld(model->vertices[face.c].Get()));
	}
}
inline void Gather(const P::State *state, Cache &cache, float timeStep = P::STEP)
{
	const Bounds needed = RequiredBounds(state,timeStep);
	if(Reuse(cache,needed,timeStep)) return;
	BeginGather(cache,needed);
	const int minX = CWorld::GetClampedSectorIndexX(cache.bounds.min.x), maxX = CWorld::GetClampedSectorIndexX(cache.bounds.max.x);
	const int minY = CWorld::GetClampedSectorIndexY(cache.bounds.min.y), maxY = CWorld::GetClampedSectorIndexY(cache.bounds.max.y);
	const int lists[] = {ENTITYLIST_BUILDINGS,ENTITYLIST_BUILDINGS_OVERLAP,ENTITYLIST_OBJECTS,
		ENTITYLIST_OBJECTS_OVERLAP,ENTITYLIST_DUMMIES,ENTITYLIST_DUMMIES_OVERLAP};
	CWorld::AdvanceCurrentScanCode();
	for(int y = minY; y <= maxY; ++y) for(int x = minX; x <= maxX; ++x){
		if(cache.stats.sectors == MAX_SECTORS){ Overflow(cache); return; }
		++cache.stats.sectors;
		CSector *sector = CWorld::GetSector(x,y);
		for(int list = 0; list < 6; ++list)
			for(CPtrNode *node = sector->m_lists[lists[list]].first; node; node = node->next){
				if(cache.stats.listNodes == MAX_LIST_NODES || cache.stats.primitiveScans == MAX_PRIMITIVE_SCAN){
					Overflow(cache); return;
				}
				++cache.stats.listNodes;
				CEntity *entity = static_cast<CEntity*>(node->item);
				if(!entity || entity->m_scanCode == CWorld::GetCurrentScanCode()) continue;
				entity->m_scanCode = CWorld::GetCurrentScanCode();
				GatherEntity(cache,entity);
			}
	}
}
inline bool FallbackSweep(const CVector &from, const CVector &to, float radius, Contact &contact)
{
	CVector direction = V::Unit(to-from,CVector(0,0,1));
	CColPoint hit; CEntity *entity = 0;
	// Rare overflow safeguard: one native centre sweep per moving joint,
	// never a world scan per constraint iteration. Ordinary contacts use the
	// exact rounded finite COL geometry above, including edges and corners.
	if(!CWorld::ProcessLineOfSight(from,to+direction*radius,hit,entity,true,false,false,true,true,false) ||
		!StaticEntity(entity)) return false;
	CVector normal = V::Unit(hit.normal,-direction);
	if(DotProduct(normal,direction) > 0) normal *= -1;
	contact.hit = contact.swept = true; contact.normal = normal;
	contact.center = hit.point+normal*(radius+V::CONTACT_SKIN); contact.depth = 0; contact.time = 0;
	return true;
}
#endif
}
