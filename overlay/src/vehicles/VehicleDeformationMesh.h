#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// RenderWare-independent production math/topology, also exercised by host tests.
namespace VehicleDeformationDetail {
struct Vec {
	float x, y, z;
	Vec(float a = 0, float b = 0, float c = 0) : x(a), y(b), z(c) {}
	Vec operator+(Vec b) const { return Vec(x+b.x, y+b.y, z+b.z); }
	Vec operator-(Vec b) const { return Vec(x-b.x, y-b.y, z-b.z); }
	Vec operator*(float f) const { return Vec(x*f, y*f, z*f); }
};
inline float Dot(Vec a, Vec b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec Cross(Vec a, Vec b) { return Vec(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x); }
inline float Length(Vec a) { return std::sqrt(Dot(a,a)); }
inline Vec Unit(Vec a) { float l=Length(a); return l>1.0e-8f ? a*(1.0f/l) : Vec(0,0,1); }
inline bool Finite(Vec a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
inline float Clamp(float f, float lo, float hi) { return std::max(lo,std::min(hi,f)); }
inline unsigned BodyNameRole(const char *name) {
	if(!name) return 0;
	auto has=[&](const char *part){return std::strstr(name,part)!=nullptr;};
	if(has("wheel") || has("windscreen") || has("glass") || has("interior") || has("seat") || has("extra") || has("_vlo")) return 2;
	return has("chassis") || has("bonnet") || has("boot") || has("bump") || has("wing") || has("door") ? 1u : 0u;
}
inline bool ExteriorMaterialName(const char *name) {
	if(!name || !*name) return true;
	// Car-colour materials are only a subset of the exterior. Classic bumpers,
	// grilles and textured body panels are white materials in the model. Exclude
	// identifiable mechanical/cabin textures, not all non-paint surfaces.
	const char *protectedNames[]={"engine","interior","seat","steer","dash","wheel","tyre","tire","glass","windscreen"};
	for(const char *part:protectedNames) for(const char *start=name;*start;start++) {
		const char *a=start,*b=part;
		while(*a && *b && ((*a>='A' && *a<='Z')?*a-'A'+'a':*a)==*b) { a++;b++; }
		if(!*b) return false;
	}
	return true;
}
inline bool IsUpperImpactPanel(const char *name,Vec hit,Vec inward) {
	if(!name) return false;
	return (hit.y>0 && inward.y< -0.5f && std::strstr(name,"bonnet")) ||
	       (hit.y<0 && inward.y> 0.5f && std::strstr(name,"boot"));
}
inline float PanelScore(Vec center,float radius,Vec hit,float reach=1.4f) {
	float distance=Length(center-hit);
	// A frontal impact also reaches the upper panel behind the bumper.
	if(distance>radius+reach) return 1.0e20f;
	return std::max(0.0f,distance-radius)*4.0f+distance*0.25f+radius*0.10f;
}
struct Transform {
	Vec right, forward, up, pos;
	Transform() : right(1,0,0),forward(0,1,0),up(0,0,1) {}
	Vec Vector(Vec p) const { return right*p.x+forward*p.y+up*p.z; }
	Vec Point(Vec p) const { return Vector(p)+pos; }
	Vec TransposeVector(Vec p) const { return Vec(Dot(right,p),Dot(forward,p),Dot(up,p)); }
	bool Inverse(Transform &out) const {
		Vec a=Cross(forward,up), b=Cross(up,right), c=Cross(right,forward);
		float det=Dot(right,a);
		if(!std::isfinite(det) || std::fabs(det)<1.0e-6f) return false;
		float d=1.0f/det;
		out.right=Vec(a.x,b.x,c.x)*d; out.forward=Vec(a.y,b.y,c.y)*d;
		out.up=Vec(a.z,b.z,c.z)*d; out.pos=out.Vector(pos)*-1.0f;
		return Finite(out.pos);
	}
	Transform operator*(const Transform &b) const {
		Transform out; out.right=Vector(b.right); out.forward=Vector(b.forward);
		out.up=Vector(b.up); out.pos=Point(b.pos); return out;
	}
};
struct Vertex { Vec pos, normal, rest; uint8_t mask=0; };
struct Triangle { uint16_t v[3]; uint16_t material; };
inline bool RenderTriangle(const uint16_t *indices,size_t count,size_t offset,bool strip,uint16_t material,Triangle &out) {
	if(!indices || offset+2>=count) return false;
	size_t odd=strip ? (offset&1) : 0;
	out={{indices[offset],indices[offset+1+odd],indices[offset+2-odd]},material};
	return out.v[0]!=out.v[1] && out.v[0]!=out.v[2] && out.v[1]!=out.v[2];
}
struct Colour { uint8_t r,g,b,a; };
struct UV { float u,v; };
struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<Triangle> triangles;
	std::vector<Colour> colours;
	std::vector<std::vector<UV> > uv;
};
struct DentSnapshot { Vec pos, normal; };
inline DentSnapshot SnapshotVertex(const Vertex &v) { return {v.pos,v.normal}; }
inline bool ValidOrientedArea(Vec before,Vec after) {
	float area2=Dot(before,before);
	// Some authored trim has degenerate triangles; preserve that input, but do
	// not allow a valid painted face to collapse or reverse under a new dent.
	return area2<1.0e-14f || (Finite(after) && Dot(before,after)>area2*0.025f);
}
inline bool ValidDentTriangle(const Mesh &m,const Triangle &t,const std::vector<DentSnapshot> &before) {
	const Vertex &a=m.vertices[t.v[0]],&b=m.vertices[t.v[1]],&c=m.vertices[t.v[2]];
	Vec after=Cross(b.pos-a.pos,c.pos-a.pos);
	Vec prior=Cross(before[t.v[1]].pos-before[t.v[0]].pos,before[t.v[2]].pos-before[t.v[0]].pos);
	Vec rest=Cross(b.rest-a.rest,c.rest-a.rest);
	return ValidOrientedArea(prior,after) && ValidOrientedArea(rest,after);
}
inline void BacktrackDentVertex(Vertex &v,const DentSnapshot &before,float factor) {
	if(v.mask!=1) return;
	if(factor==0.0f) { v.pos=before.pos; v.normal=before.normal; return; }
	if(Dot(v.pos-before.pos,v.pos-before.pos)<1.0e-16f) return;
	v.pos=before.pos+(v.pos-before.pos)*factor;
	v.normal=Unit(before.normal+(v.normal-before.normal)*factor);
}
// Exterior faces opt in; a protected face sharing an index vetoes movement.
// Transparent glass and identifiable cabin/mechanical surfaces remain fixed.
inline void MarkTriangle(Mesh &m, const Triangle &t, bool body) {
	for(int i=0;i<3;i++) m.vertices[t.v[i]].mask |= body ? 1 : 2;
}
struct Edge { uint32_t key; uint16_t midpoint; };
inline uint32_t EdgeKey(uint16_t a,uint16_t b) {
	if(a>b) std::swap(a,b); return (uint32_t(a)<<16)|b;
}
inline uint16_t AddMidpoint(Mesh &m,uint16_t a,uint16_t b) {
	Vertex v; v.pos=(m.vertices[a].pos+m.vertices[b].pos)*0.5f;
	v.rest=(m.vertices[a].rest+m.vertices[b].rest)*0.5f;
	v.normal=Unit(m.vertices[a].normal+m.vertices[b].normal);
	v.mask=m.vertices[a].mask|m.vertices[b].mask;
	uint16_t index=static_cast<uint16_t>(m.vertices.size()); m.vertices.push_back(v);
	if(!m.colours.empty()) {
		Colour ca=m.colours[a], cb=m.colours[b];
		m.colours.push_back({uint8_t((unsigned(ca.r)+cb.r)/2),uint8_t((unsigned(ca.g)+cb.g)/2),
		                     uint8_t((unsigned(ca.b)+cb.b)/2),uint8_t((unsigned(ca.a)+cb.a)/2)});
	}
	for(auto &set:m.uv) set.push_back({(set[a].u+set[b].u)*0.5f,(set[a].v+set[b].v)*0.5f});
	return index;
}
// One conforming pass: every incident triangle uses the same edge midpoint.
// Budget failure is transactional, so no isolated half-split/T-junction remains.
inline bool Subdivide(Mesh &m,const Transform &localToCar,float edgeLength,
                      size_t maxVertices,size_t maxTriangles) {
	std::vector<Edge> edges;
	const float limit=edgeLength*edgeLength;
	for(const Triangle &t:m.triangles) for(int i=0;i<3;i++) {
		uint16_t a=t.v[i],b=t.v[(i+1)%3];
		if(m.vertices[a].mask!=1 || m.vertices[b].mask!=1) continue;
		Vec d=localToCar.Vector(m.vertices[a].rest-m.vertices[b].rest);
		if(Dot(d,d)>limit) edges.push_back({EdgeKey(a,b),0});
	}
	std::sort(edges.begin(),edges.end(),[](const Edge&a,const Edge&b){return a.key<b.key;});
	edges.erase(std::unique(edges.begin(),edges.end(),[](const Edge&a,const Edge&b){return a.key==b.key;}),edges.end());
	if(edges.empty() || m.vertices.size()+edges.size()>maxVertices || m.vertices.size()+edges.size()>65535) return false;
	auto find=[&](uint16_t a,uint16_t b)->Edge* {
		uint32_t key=EdgeKey(a,b);
		auto it=std::lower_bound(edges.begin(),edges.end(),key,[](const Edge&e,uint32_t k){return e.key<k;});
		return it!=edges.end() && it->key==key ? &*it : nullptr;
	};
	size_t count=m.triangles.size();
	for(const Triangle&t:m.triangles) for(int i=0;i<3;i++) if(find(t.v[i],t.v[(i+1)%3])) count++;
	if(count>maxTriangles) return false;
	for(Edge &e:edges) e.midpoint=AddMidpoint(m,uint16_t(e.key>>16),uint16_t(e.key));
	std::vector<Triangle> triangles; triangles.reserve(count);
	for(const Triangle&t:m.triangles) {
		uint16_t a=t.v[0],b=t.v[1],c=t.v[2];
		Edge *ab=find(a,b),*bc=find(b,c),*ca=find(c,a);
		auto add=[&](uint16_t x,uint16_t y,uint16_t z){triangles.push_back({{x,y,z},t.material});};
		unsigned bits=(ab?1:0)|(bc?2:0)|(ca?4:0);
		uint16_t x=ab?ab->midpoint:0,y=bc?bc->midpoint:0,z=ca?ca->midpoint:0;
		switch(bits) {
		case 0: add(a,b,c); break;
		case 1: add(a,x,c); add(x,b,c); break;
		case 2: add(a,b,y); add(a,y,c); break;
		case 4: add(a,b,z); add(z,b,c); break;
		case 3: add(x,b,y); add(a,x,c); add(x,y,c); break;
		case 6: add(y,c,z); add(b,y,a); add(y,z,a); break;
		case 5: add(z,a,x); add(c,z,b); add(z,x,b); break;
		case 7: add(a,x,z); add(x,b,y); add(z,y,c); add(x,y,z); break;
		}
	}
	m.triangles.swap(triangles); return true;
}
inline float HitDepth(float impulse,float mass,float strength=1.0f,float threshold=1.0f) {
	// Native collision impulse / receiver mass is in game speed (50 units/s).
	float deltaSpeed=impulse*50.0f/std::max(mass,300.0f);
	return Clamp((deltaSpeed-1.4f*threshold)*0.022f,0.0f,0.24f)*strength;
}
inline bool DentVertex(Vertex &v,const Transform &toCar,const Transform &fromCar,
                       Vec hit,Vec inward,float radius,float depth,float maxDent=0.32f) {
	if(v.mask!=1 || depth<=0.0f) return false;
	// Saturate the field before evaluating its gradient. Letting a 96cm request
	// hit a 32cm per-vertex cap creates a steep rim that can fold the whole panel.
	depth=std::min(depth,maxDent);
	Vec p=toCar.Point(v.pos), d=p-hit;
	float r2=radius*radius, q=std::max(0.0f,1.0f-Dot(d,d)/r2);
	Vec gradient=d*(-4.0f*depth*q/r2);
	// Compression parallel to a flat bonnet only slides its vertices in-plane:
	// even a large logged displacement leaves the bonnet flat. A bounded smooth
	// buckle above/behind a frontal contact gives that panel an actual crease.
	// This car-space field is continuous across authored normal/material seams;
	// protected vertices still veto movement and topology uses the same guard.
	Vec upper=d-inward*0.25f-Vec(0,0,0.45f);
	float bq=std::max(0.0f,1.0f-Dot(upper,upper)/r2);
	float h=Clamp(d.z/0.35f,0.0f,1.0f), height=h*h*(3.0f-2.0f*h);
	float heightGradient=(h>0.0f && h<1.0f)?6.0f*h*(1.0f-h)/0.35f:0.0f;
	float buckleDepth=depth*0.85f*inward.y*inward.y;
	float buckle=buckleDepth*bq*bq*height;
	Vec buckleGradient=upper*(-4.0f*buckleDepth*bq*height/r2)+Vec(0,0,buckleDepth*bq*bq*heightGradient);
	Vec delta=inward*(depth*q*q)+Vec(0,0,buckle), candidate=p+delta;
	if(Dot(delta,delta)<1.0e-12f) return false;
	Vec rest=toCar.Point(v.rest), accumulated=candidate-rest;
	// Reducing the setting limits future dents; it must not repair old damage.
	maxDent=std::max(maxDent,Length(p-rest));
	float length=Length(accumulated);
	if(length>maxDent) candidate=rest+accumulated*(maxDent/length);
	if(Length(candidate-p)<0.0001f) return false;
	// Inverse transpose of both fields' Jacobian preserves authored smoothing
	// and hard edges. Cofactors avoid a matrix allocation per vertex.
	gradient=toCar.TransposeVector(gradient);
	buckleGradient=toCar.TransposeVector(buckleGradient);
	Vec direction=fromCar.Vector(inward), lift=fromCar.Vector(Vec(0,0,1));
	Vec x=Vec(1,0,0)+direction*gradient.x+lift*buckleGradient.x;
	Vec y=Vec(0,1,0)+direction*gradient.y+lift*buckleGradient.y;
	Vec z=Vec(0,0,1)+direction*gradient.z+lift*buckleGradient.z;
	Vec cx=Cross(y,z),cy=Cross(z,x),cz=Cross(x,y);
	if(Dot(x,cx)>0.2f) v.normal=Unit(cx*v.normal.x+cy*v.normal.y+cz*v.normal.z);
	v.pos=fromCar.Point(candidate); return Finite(v.pos) && Finite(v.normal);
}
}
