#pragma once

#include <cmath>
#include "VrRagdollReaction.h"
#include "VrRagdollBrake.h"
#include "VrRagdollSettings.h"

// Allocation-free, one-way contacts for ragdoll particles and road vehicles.
// Uses bounded real collision spheres, boxes and triangles, NOT the car's
// whole bounding box (which would make the empty space above a bonnet solid).
// Include common.h first. The geometry can be tested without the game by
// defining VR_RAGDOLL_VEHICLE_NO_ENGINE and providing CVector/Dot/Cross.
#ifndef VR_RAGDOLL_VEHICLE_NO_ENGINE
#include "Pools.h"
#endif

namespace VrRagdollVehicle
{
enum { MAX_VEHICLES = 8, MAX_SHAPES = 12, MAX_TRIANGLES = 32,
	MAX_TRIANGLE_SCAN = 256, MAX_SWEEP_SEGMENTS = 4 };
static const float CONTACT_SKIN = 0.002f;

inline float MinF(float a, float b) { return a < b ? a : b; }
inline float MaxF(float a, float b) { return a > b ? a : b; }
inline float ClampF(float a, float lo, float hi) { return MaxF(lo, MinF(a, hi)); }
inline CVector Unit(const CVector &a, const CVector &fallback)
{
	float d = DotProduct(a, a);
	return d > 1.0e-12f ? a * (1.0f / std::sqrt(d)) : fallback;
}
inline CVector Rotate(const CVector &v, const CVector &omega, float seconds)
{
	float speed2 = DotProduct(omega, omega);
	if(speed2 < 1.0e-10f || seconds == 0.0f) return v;
	float speed = std::sqrt(speed2), angle = speed * seconds;
	CVector axis = omega * (1.0f / speed);
	float c = std::cos(angle), s = std::sin(angle);
	return v * c + CrossProduct(axis, v) * s + axis * (DotProduct(axis, v) * (1.0f - c));
}

struct Pose {
	CVector origin, right, forward, up;
	CVector ToLocal(const CVector &world) const {
		CVector d = world - origin;
		return CVector(DotProduct(d, right), DotProduct(d, forward), DotProduct(d, up));
	}
	CVector Direction(const CVector &local) const {
		return right * local.x + forward * local.y + up * local.z;
	}
	CVector ToWorld(const CVector &local) const { return origin + Direction(local); }
};

struct Shape {
	// A sphere has radius > 0; otherwise min/max define a local axis box.
	CVector min, max, center;
	float radius;
};

struct Triangle {
	CVector a, b, c, normal, min, max;
};

struct Vehicle {
	int handle;
	float mass;
	Pose pose;
	CVector linearVelocity, angularVelocity;
	CVector surfaceLinearVelocity, surfaceAngularVelocity;
	Shape shapes[MAX_SHAPES];
	int shapeCount;
	Triangle triangles[MAX_TRIANGLES];
	int triangleCount;
	Pose sweep[MAX_SWEEP_SEGMENTS + 1];
	int sweepSegments;
	CVector boundCenter;
	float boundRadius;
	CVector sweepBoundStart, sweepBoundEnd;
	float sweepBoundArcRadius;
	Vehicle() : handle(-1), mass(0.0f), shapeCount(0), triangleCount(0), sweepSegments(1),
		boundRadius(0.0f), sweepBoundArcRadius(0.0f) {}
};

struct Batch {
	Vehicle vehicles[MAX_VEHICLES];
	int count;
	unsigned poolChecks, candidateOverflow, shapeOverflow, unsupportedModels;
	unsigned triangleScans, triangleOverflow;
	float fromTime, toTime;
	Batch() : count(0), poolChecks(0), candidateOverflow(0), shapeOverflow(0),
		unsupportedModels(0), triangleScans(0), triangleOverflow(0), fromTime(0.0f), toTime(0.0f) {}
};

struct QueryStats {
	unsigned vehicleTests, shapeTests, triangleTests, contacts, sweeps;
	QueryStats() : vehicleTests(0), shapeTests(0), triangleTests(0), contacts(0), sweeps(0) {}
};

struct Contact {
	int vehicleIndex;
	CVector center, normal, surfaceVelocity;
	float penetration;
	bool swept;
	Contact() : vehicleIndex(-1), center(0.0f, 0.0f, 0.0f), normal(0.0f, 0.0f, 0.0f),
		surfaceVelocity(0.0f, 0.0f, 0.0f), penetration(0.0f), swept(false) {}
};

inline Pose PoseAt(const Vehicle &v, float seconds)
{
	Pose p;
	p.origin = v.pose.origin + v.linearVelocity * seconds;
	p.right = Rotate(v.pose.right, v.angularVelocity, seconds);
	p.forward = Rotate(v.pose.forward, v.angularVelocity, seconds);
	p.up = Rotate(v.pose.up, v.angularVelocity, seconds);
	return p;
}

// Times are seconds relative to the captured current vehicle matrix. Call
// once per solver step, before Find calls. Also usable for the sleeping-body
// wake check over an entire game frame. Translation is swept continuously;
// rotation uses at most four chords (~0.15 rad each before the cap). Very
// fast flips remain an approximation, rather than expensive mesh CCD.
inline void Prepare(Batch &batch, float fromTime, float toTime)
{
	batch.fromTime = fromTime;
	batch.toTime = toTime;
	for(int i = 0; i < batch.count; ++i){
		Vehicle &v = batch.vehicles[i];
		float angle = std::sqrt(DotProduct(v.angularVelocity, v.angularVelocity)) * std::fabs(toTime - fromTime);
		v.sweepSegments = 1 + (int)MinF(angle / 0.15f, float(MAX_SWEEP_SEGMENTS - 1));
		for(int n = 0; n <= v.sweepSegments; ++n)
			v.sweep[n] = PoseAt(v, fromTime + (toTime - fromTime) * (float(n) / float(v.sweepSegments)));
		v.sweepBoundStart = v.sweep[0].ToWorld(v.boundCenter);
		v.sweepBoundEnd = v.sweep[v.sweepSegments].ToWorld(v.boundCenter);
		// Include the arc of an off-origin model bound during rotation.
		v.sweepBoundArcRadius = MinF(2.0f, angle) * std::sqrt(DotProduct(v.boundCenter, v.boundCenter));
	}
}

inline float SegmentDistanceSq(const CVector &point, const CVector &a, const CVector &b)
{
	CVector d = b - a;
	float d2 = DotProduct(d, d);
	float t = d2 > 1.0e-12f ? ClampF(DotProduct(point - a, d) / d2, 0.0f, 1.0f) : 0.0f;
	CVector diff = point - (a + d * t);
	return DotProduct(diff, diff);
}

inline bool MakeTriangle(const CVector &a, const CVector &b, const CVector &c, Triangle &t)
{
	CVector cross = CrossProduct(b - a, c - a);
	float areaSq = DotProduct(cross, cross);
	if(areaSq < 1.0e-10f) return false;
	t.a = a; t.b = b; t.c = c;
	t.normal = cross * (1.0f / std::sqrt(areaSq));
	t.min = CVector(MinF(a.x, MinF(b.x, c.x)), MinF(a.y, MinF(b.y, c.y)), MinF(a.z, MinF(b.z, c.z)));
	t.max = CVector(MaxF(a.x, MaxF(b.x, c.x)), MaxF(a.y, MaxF(b.y, c.y)), MaxF(a.z, MaxF(b.z, c.z)));
	return true;
}

// Closest point over the seven triangle Voronoi regions. No mesh allocation,
// triangle-plane cache construction, or collision-world ray query is needed.
inline CVector ClosestTrianglePoint(const Triangle &t, const CVector &p)
{
	CVector ab = t.b - t.a, ac = t.c - t.a, ap = p - t.a;
	float d1 = DotProduct(ab, ap), d2 = DotProduct(ac, ap);
	if(d1 <= 0.0f && d2 <= 0.0f) return t.a;
	CVector bp = p - t.b;
	float d3 = DotProduct(ab, bp), d4 = DotProduct(ac, bp);
	if(d3 >= 0.0f && d4 <= d3) return t.b;
	float vc = d1 * d4 - d3 * d2;
	if(vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) return t.a + ab * (d1 / (d1 - d3));
	CVector cp = p - t.c;
	float d5 = DotProduct(ab, cp), d6 = DotProduct(ac, cp);
	if(d6 >= 0.0f && d5 <= d6) return t.c;
	float vb = d5 * d2 - d1 * d6;
	if(vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) return t.a + ac * (d2 / (d2 - d6));
	float va = d3 * d6 - d5 * d4;
	if(va <= 0.0f && d4 >= d3 && d5 >= d6)
		return t.b + (t.c - t.b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
	float denom = 1.0f / (va + vb + vc);
	return t.a + ab * (vb * denom) + ac * (vc * denom);
}

inline bool InsideTriangle(const Triangle &t, const CVector &p)
{
	return DotProduct(CrossProduct(t.b - t.a, p - t.a), t.normal) >= -1.0e-6f &&
		DotProduct(CrossProduct(t.c - t.b, p - t.b), t.normal) >= -1.0e-6f &&
		DotProduct(CrossProduct(t.a - t.c, p - t.c), t.normal) >= -1.0e-6f;
}

inline bool ProjectTriangle(const Triangle &t, const CVector &point, float radius,
	CVector &projected, CVector &normal, float &depth)
{
	float r = radius + CONTACT_SKIN;
	if(point.x < t.min.x - r || point.x > t.max.x + r ||
		point.y < t.min.y - r || point.y > t.max.y + r ||
		point.z < t.min.z - r || point.z > t.max.z + r) return false;
	CVector closest = ClosestTrianglePoint(t, point), delta = point - closest;
	float d2 = DotProduct(delta, delta);
	if(d2 > r * r) return false;
	normal = Unit(delta, t.normal);
	depth = MaxF(0.0f, radius - std::sqrt(d2));
	projected = closest + normal * r;
	return true;
}

// Selection is independent of engine ownership, so tests can verify the
// fixed cap. Existing entries are replaced only by a closer face. Equal
// scores keep stable model order rather than changing contacts each frame.
inline void KeepTriangle(Vehicle &v, const Triangle &t, float score,
	float *scores, unsigned &overflow)
{
	int at = v.triangleCount;
	if(at == MAX_TRIANGLES){
		++overflow; at = 0;
		for(int i = 1; i < MAX_TRIANGLES; ++i) if(scores[i] > scores[at]) at = i;
		if(score >= scores[at]) return;
	}else ++v.triangleCount;
	v.triangles[at] = t; scores[at] = score;
}

// Exact static sphere-vs-sphere or sphere-vs-box projection. For a point
// inside a box use the nearest face. This keeps collision volume faithful
// at corners, unlike treating an expanded box as a static sphere collider.
inline bool ProjectShape(const Shape &shape, const CVector &point, float radius,
	CVector &projected, CVector &normal, float &depth)
{
	if(shape.radius > 0.0f){
		CVector delta = point - shape.center;
		float r = shape.radius + radius, d2 = DotProduct(delta, delta);
		if(d2 > (r + CONTACT_SKIN) * (r + CONTACT_SKIN)) return false;
		float d = std::sqrt(d2);
		normal = Unit(delta, CVector(0.0f, 0.0f, 1.0f));
		depth = MaxF(0.0f, r - d);
		projected = shape.center + normal * (r + CONTACT_SKIN);
		return true;
	}
	CVector closest(ClampF(point.x, shape.min.x, shape.max.x),
		ClampF(point.y, shape.min.y, shape.max.y), ClampF(point.z, shape.min.z, shape.max.z));
	CVector delta = point - closest;
	float d2 = DotProduct(delta, delta);
	if(d2 > (radius + CONTACT_SKIN) * (radius + CONTACT_SKIN)) return false;
	if(d2 > 1.0e-12f){
		float d = std::sqrt(d2);
		normal = delta * (1.0f / d);
		depth = MaxF(0.0f, radius - d);
		projected = closest + normal * (radius + CONTACT_SKIN);
	}else{
		float faces[6] = { point.x - shape.min.x, shape.max.x - point.x,
			point.y - shape.min.y, shape.max.y - point.y,
			point.z - shape.min.z, shape.max.z - point.z };
		int face = 0;
		for(int k = 1; k < 6; ++k) if(faces[k] < faces[face]) face = k;
		normal = CVector(0.0f, 0.0f, 0.0f);
		if(face < 2) normal.x = face == 0 ? -1.0f : 1.0f;
		else if(face < 4) normal.y = face == 2 ? -1.0f : 1.0f;
		else normal.z = face == 4 ? -1.0f : 1.0f;
		depth = faces[face] + radius;
		projected = point + normal * (depth + CONTACT_SKIN);
	}
	return true;
}

inline bool SweepSphere(const CVector &from, const CVector &to, const CVector &center,
	float radius, float &hitTime, CVector &normal)
{
	CVector d = to - from, m = from - center;
	float a = DotProduct(d, d), b = DotProduct(m, d), c = DotProduct(m, m) - radius * radius;
	// An existing overlap is handled by static projection. A point moving
	// out of a volume must be allowed to leave it, rather than being pulled
	// back to the original entry face on every solver iteration.
	if(c < -1.0e-8f || a < 1.0e-12f || b >= 0.0f) return false;
	float discriminant = b * b - a * c;
	if(discriminant < 0.0f) return false;
	hitTime = (-b - std::sqrt(discriminant)) / a;
	if(hitTime < 0.0f || hitTime > 1.0f) return false;
	normal = Unit(from + d * hitTime - center, CVector(0.0f, 0.0f, 1.0f));
	return true;
}

inline bool SweepCapsule(const CVector &from, const CVector &to, const CVector &a,
	const CVector &b, float radius, float &hitTime, CVector &normal)
{
	CVector edge = b - a, delta = to - from, offset = from - a;
	float lengthSq = DotProduct(edge, edge), best = 2.0f;
	CVector bestNormal(0.0f, 0.0f, 0.0f);
	if(lengthSq > 1.0e-12f){
		float startAlong = DotProduct(offset, edge) / lengthSq;
		float deltaAlong = DotProduct(delta, edge) / lengthSq;
		CVector radial = offset - edge * startAlong, speed = delta - edge * deltaAlong;
		float qa = DotProduct(speed, speed), qb = DotProduct(radial, speed);
		float qc = DotProduct(radial, radial) - radius * radius, disc = qb * qb - qa * qc;
		if(qa > 1.0e-12f && qb < 0.0f && qc >= -1.0e-8f && disc >= 0.0f){
			float time = (-qb - std::sqrt(disc)) / qa;
			float along = startAlong + deltaAlong * time;
			if(time >= 0.0f && time <= 1.0f && along >= 0.0f && along <= 1.0f){
				best = time; bestNormal = Unit(radial + speed * time, CVector(0.0f, 0.0f, 1.0f));
			}
		}
	}
	float time;
	CVector n;
	if(SweepSphere(from, to, a, radius, time, n) && time < best){ best = time; bestNormal = n; }
	if(SweepSphere(from, to, b, radius, time, n) && time < best){ best = time; bestNormal = n; }
	if(best > 1.0f) return false;
	hitTime = best; normal = bestNormal;
	return true;
}

// Double-sided sphere/triangle CCD: two offset faces and three rounded
// edges. The radius supplies thickness even to a single roof triangle, so
// a 40 m/s body cannot jump from above to below it between fixed steps.
inline bool SweepTriangle(const Triangle &t, const CVector &from, const CVector &to,
	float radius, float &hitTime, CVector &normal)
{
	float start[3] = {from.x, from.y, from.z}, finish[3] = {to.x, to.y, to.z};
	float lo[3] = {t.min.x, t.min.y, t.min.z}, hi[3] = {t.max.x, t.max.y, t.max.z};
	float enter = 0.0f, leave = 1.0f;
	for(int axis = 0; axis < 3; ++axis){
		float d = finish[axis] - start[axis];
		if(std::fabs(d) < 1.0e-9f){
			if(start[axis] < lo[axis] - radius || start[axis] > hi[axis] + radius) return false;
		}else{
			float a = (lo[axis] - radius - start[axis]) / d, b = (hi[axis] + radius - start[axis]) / d;
			enter = MaxF(enter, MinF(a, b)); leave = MinF(leave, MaxF(a, b));
			if(enter > leave) return false;
		}
	}
	CVector delta = to - from, projected, startNormal;
	float depth;
	// A point already intersecting a thin surface must not pass through its
	// opposite side merely because the entry began before this interval.
	if(ProjectTriangle(t, from, radius, projected, startNormal, depth) &&
		depth > 1.0e-6f && DotProduct(delta, startNormal) < -1.0e-8f){
		hitTime = 0.0f; normal = startNormal;
		return true;
	}
	float d0 = DotProduct(from - t.a, t.normal), dd = DotProduct(delta, t.normal);
	float best = 2.0f;
	CVector bestNormal(0.0f, 0.0f, 0.0f);
	for(int side = 0; side < 2; ++side){
		float sign = side ? 1.0f : -1.0f;
		if(dd * sign >= -1.0e-9f || d0 * sign < radius - 1.0e-6f) continue;
		float time = (sign * radius - d0) / dd;
		if(time < 0.0f || time > 1.0f) continue;
		CVector n = t.normal * sign;
		if(InsideTriangle(t, from + delta * time - n * radius)){
			best = time; bestNormal = n;
		}
	}
	CVector vertices[3] = {t.a, t.b, t.c};
	for(int edge = 0; edge < 3; ++edge){
		float time;
		CVector n;
		if(SweepCapsule(from, to, vertices[edge], vertices[(edge + 1) % 3], radius, time, n) && time < best){
			best = time; bestNormal = n;
		}
	}
	if(best > 1.0f) return false;
	hitTime = best; normal = bestNormal;
	return true;
}

// Continuous sphere-box test against the rounded box: six face slabs and
// twelve edge capsules (caps also cover all corners). An expanded AABB
// alone creates false hits outside diagonal corners at high speed.
inline bool SweepBox(const Shape &shape, const CVector &from, const CVector &to,
	float radius, float &hitTime, CVector &normal)
{
	CVector unusedPoint, unusedNormal;
	float unusedDepth;
	if(ProjectShape(shape, from, radius, unusedPoint, unusedNormal, unusedDepth) && unusedDepth > 1.0e-6f) return false;
	float start[3] = { from.x, from.y, from.z }, finish[3] = { to.x, to.y, to.z };
	float lo[3] = { shape.min.x, shape.min.y, shape.min.z };
	float hi[3] = { shape.max.x, shape.max.y, shape.max.z };
	// Cheap conservative slab reject before rounded edges/corners.
	float enter = 0.0f, leave = 1.0f;
	for(int axis = 0; axis < 3; ++axis){
		float d = finish[axis] - start[axis];
		if(std::fabs(d) < 1.0e-9f){
			if(start[axis] < lo[axis] - radius || start[axis] > hi[axis] + radius) return false;
		}else{
			float t0 = (lo[axis] - radius - start[axis]) / d;
			float t1 = (hi[axis] + radius - start[axis]) / d;
			enter = MaxF(enter, MinF(t0, t1)); leave = MinF(leave, MaxF(t0, t1));
			if(enter > leave) return false;
		}
	}
	float best = 2.0f;
	CVector bestNormal(0.0f, 0.0f, 0.0f);
	for(int axis = 0; axis < 3; ++axis){
		int u = (axis + 1) % 3, w = (axis + 2) % 3;
		float delta = finish[axis] - start[axis];
		for(int side = 0; side < 2; ++side){
			float sign = side ? 1.0f : -1.0f;
			if(delta * sign >= -1.0e-9f) continue;
			float t = ((side ? hi[axis] : lo[axis]) + sign * radius - start[axis]) / delta;
			if(t < 0.0f || t > 1.0f || t >= best) continue;
			float pu = start[u] + (finish[u] - start[u]) * t;
			float pw = start[w] + (finish[w] - start[w]) * t;
			if(pu < lo[u] || pu > hi[u] || pw < lo[w] || pw > hi[w]) continue;
			best = t;
			bestNormal = CVector(axis == 0 ? sign : 0.0f, axis == 1 ? sign : 0.0f, axis == 2 ? sign : 0.0f);
		}
		// The four edges parallel to this axis, tested as infinite cylinders
		// with a range check; sphere end caps complete each capsule.
		for(int su = 0; su < 2; ++su) for(int sw = 0; sw < 2; ++sw){
			float eu = su ? hi[u] : lo[u], ew = sw ? hi[w] : lo[w];
			float mu = start[u] - eu, mw = start[w] - ew;
			float du = finish[u] - start[u], dw = finish[w] - start[w];
			float a = du * du + dw * dw, b = mu * du + mw * dw;
			float c = mu * mu + mw * mw - radius * radius, disc = b * b - a * c;
			if(a > 1.0e-12f && c > 0.0f && b < 0.0f && disc >= 0.0f){
				float t = (-b - std::sqrt(disc)) / a;
				float p = start[axis] + delta * t;
				if(t >= 0.0f && t <= 1.0f && t < best && p >= lo[axis] && p <= hi[axis]){
					float n[3] = { 0.0f, 0.0f, 0.0f };
					n[u] = mu + du * t; n[w] = mw + dw * t;
					best = t; bestNormal = Unit(CVector(n[0], n[1], n[2]), CVector(0.0f, 0.0f, 1.0f));
				}
			}
			// Visit the eight distinct corners just once (axis == 0).
			if(axis == 0) for(int end = 0; end < 2; ++end){
				CVector corner(end ? hi[0] : lo[0], eu, ew), n;
				float t;
				if(SweepSphere(from, to, corner, radius, t, n) && t < best){ best = t; bestNormal = n; }
			}
		}
	}
	if(best > 1.0f) return false;
	hitTime = best; normal = bestNormal;
	return true;
}

// Return one contact; solver iterations resolve overlaps between collision
// volumes. center is the projected particle CENTER, already radius-offset.
// For a sweep the entry point follows the moving car through the remainder
// of the interval, so a fast car cannot pass fully through a sleeping body.
inline bool Find(const Batch &batch, const CVector &previous, const CVector &current,
	float radius, Contact &result, QueryStats *stats = 0)
{
	bool found = false;
	float bestDepth = -1.0f, bestSweepTime = 2.0f;
	float pointTravel = std::sqrt(DotProduct(current - previous, current - previous));
	for(int vIndex = 0; vIndex < batch.count; ++vIndex){
		const Vehicle &v = batch.vehicles[vIndex];
		if(stats) ++stats->vehicleTests;
		const Pose &endPose = v.sweep[v.sweepSegments];
		CVector localEnd = endPose.ToLocal(current);
		// Broad rejection of particles against the swept bounding sphere.
		float broadRadius = v.boundRadius + radius + CONTACT_SKIN + pointTravel + v.sweepBoundArcRadius;
		if(SegmentDistanceSq(current, v.sweepBoundStart, v.sweepBoundEnd) > broadRadius * broadRadius) continue;
		CVector localPath[MAX_SWEEP_SEGMENTS + 1];
		for(int n = 0; n <= v.sweepSegments; ++n)
			localPath[n] = v.sweep[n].ToLocal(previous + (current - previous) * (float(n) / float(v.sweepSegments)));
		for(int sIndex = 0; sIndex < v.shapeCount + v.triangleCount; ++sIndex){
			bool isTriangle = sIndex >= v.shapeCount;
			if(stats){ if(isTriangle) ++stats->triangleTests; else ++stats->shapeTests; }
			const Shape *shape = isTriangle ? 0 : &v.shapes[sIndex];
			const Triangle *triangle = isTriangle ? &v.triangles[sIndex - v.shapeCount] : 0;
			CVector projected, normal;
			float depth;
			bool overlap = isTriangle ? ProjectTriangle(*triangle, localEnd, radius, projected, normal, depth) :
				ProjectShape(*shape, localEnd, radius, projected, normal, depth);
			bool swept = false;
			float sweepTime = 2.0f;
			// Test entry even if the endpoint overlaps: nearest-face static
			// projection can otherwise spit a fast penetrating particle out
			// through the far side of the car, producing a backwards shove.
			for(int segment = 0; segment < v.sweepSegments; ++segment){
				float t0 = float(segment) / float(v.sweepSegments), t1 = float(segment + 1) / float(v.sweepSegments);
				const CVector &a = localPath[segment], &b = localPath[segment + 1];
				float hitTime;
				CVector hitNormal;
				bool hit = isTriangle ? SweepTriangle(*triangle, a, b, radius, hitTime, hitNormal) :
					shape->radius > 0.0f ? SweepSphere(a, b, shape->center, shape->radius + radius, hitTime, hitNormal) :
					SweepBox(*shape, a, b, radius, hitTime, hitNormal);
				if(hit){
					sweepTime = t0 + (t1 - t0) * hitTime;
					projected = a + (b - a) * hitTime + hitNormal * CONTACT_SKIN;
					if(isTriangle && hitTime == 0.0f){
						CVector initialNormal;
						float initialDepth;
						ProjectTriangle(*triangle, a, radius, projected, initialNormal, initialDepth);
					}
					normal = hitNormal;
					swept = true;
					break;
				}
			}
			if(!overlap && !swept) continue;
			CVector worldNormal = Unit(endPose.Direction(normal), CVector(0.0f, 0.0f, 1.0f));
			CVector worldCenter = endPose.ToWorld(projected);
			float worldDepth = MaxF(0.0f, DotProduct(worldCenter - current, worldNormal));
			// Prefer an earlier swept entry; otherwise deepest current overlap.
			if(found && (swept ? sweepTime >= bestSweepTime : bestSweepTime <= 1.0f || worldDepth <= bestDepth)) continue;
			found = true; bestDepth = worldDepth;
			if(swept) bestSweepTime = sweepTime;
			result.center = worldCenter;
			result.vehicleIndex = vIndex;
			result.normal = worldNormal;
			CVector surfacePoint = worldCenter - worldNormal * radius;
			result.surfaceVelocity = v.surfaceLinearVelocity + CrossProduct(v.surfaceAngularVelocity, surfacePoint - endPose.origin);
			result.penetration = worldDepth;
			result.swept = swept;
		}
	}
	if(found && stats){ ++stats->contacts; if(result.swept) ++stats->sweeps; }
	return found;
}

// Useful to host tests and callers without a full solver. Zero restitution:
// only close the inward RELATIVE normal speed, never add a repeated kick.
inline CVector MatchSurfaceNormal(const CVector &velocity, const Contact &contact)
{
	float inward = DotProduct(velocity - contact.surfaceVelocity, contact.normal);
	return inward < 0.0f ? velocity - contact.normal * inward : velocity;
}

#ifndef VR_RAGDOLL_VEHICLE_NO_ENGINE
// Scan the fixed vehicle pool for one scheduled body or wake query. No pool
// pointers survive the function. Keep the
// eight nearest swept candidates. A candidate contributes at most twelve
// nearby volumes plus 32 actual triangle faces. At most 256 faces/car are
// inspected when selecting the nearby faces (uniformly across an oversized
// custom model), and all limits have counters. Boats, bikes and aircraft
// remain excluded. Both mixed and triangle-only road-car models work.
inline void Gather(const CVector *points, int count, float radius, float frameSpan, Batch &batch, float bodyTravel = 0.0f)
{
	batch.count = 0; batch.poolChecks = 0; batch.candidateOverflow = 0;
	batch.shapeOverflow = 0; batch.unsupportedModels = 0;
	batch.triangleScans = 0; batch.triangleOverflow = 0;
	if(count <= 0 || CPools::GetVehiclePool() == 0) return;
	CVector lo = points[0], hi = points[0];
	for(int n = 1; n < count; ++n){
		lo.x = MinF(lo.x, points[n].x); lo.y = MinF(lo.y, points[n].y); lo.z = MinF(lo.z, points[n].z);
		hi.x = MaxF(hi.x, points[n].x); hi.y = MaxF(hi.y, points[n].y); hi.z = MaxF(hi.z, points[n].z);
	}
	CVector bodyCenter = (lo + hi) * 0.5f;
	float bodyRadius = std::sqrt(DotProduct(hi - bodyCenter, hi - bodyCenter)) + radius + MaxF(0.0f, bodyTravel);
	float span = ClampF(std::fabs(frameSpan), 0.0f, 0.1f);
	CVehicle *candidates[MAX_VEHICLES];
	float scores[MAX_VEHICLES];
	int candidateCount = 0;
	CVehiclePool *pool = CPools::GetVehiclePool();
	for(int i = 0; i < pool->GetSize(); ++i){
		CVehicle *car = pool->GetSlot(i);
		++batch.poolChecks;
		if(car == 0 || !car->IsCar() || !car->bUsesCollision || car->bRemoveFromWorld || car->IsRealHeli() || car->IsRealPlane()) continue;
		CColModel *col = car->GetColModel();
		if(col == 0) continue;
		CVector offset = car->GetRight() * col->boundingSphere.center.x + car->GetForward() * col->boundingSphere.center.y + car->GetUp() * col->boundingSphere.center.z;
		CVector center = car->GetPosition() + offset;
		CVector travel = car->m_vecMoveSpeed * (50.0f * span);
		float turn = std::sqrt(DotProduct(car->m_vecTurnSpeed, car->m_vecTurnSpeed)) * 50.0f * span;
		float bound = col->boundingSphere.radius + bodyRadius + CONTACT_SKIN + MinF(2.0f, turn) * std::sqrt(DotProduct(offset, offset));
		float score = SegmentDistanceSq(bodyCenter, center - travel, center + travel);
		if(score > bound * bound) continue;
		if((col->numSpheres == 0 || col->spheres == 0) && (col->numBoxes == 0 || col->boxes == 0) &&
			(col->numTriangles <= 0 || col->triangles == 0 || col->vertices == 0)){
			++batch.unsupportedModels; continue;
		}
		int at = candidateCount;
		if(candidateCount == MAX_VEHICLES){
			++batch.candidateOverflow;
			at = 0;
			for(int j = 1; j < candidateCount; ++j) if(scores[j] > scores[at]) at = j;
			if(score >= scores[at]) continue;
		}else ++candidateCount;
		candidates[at] = car; scores[at] = score;
	}
	for(int i = 0; i < candidateCount; ++i){
		CVehicle *car = candidates[i];
		CColModel *col = car->GetColModel();
		Vehicle &v = batch.vehicles[batch.count++];
		v.handle = CPools::GetVehicleRef(car);
		v.mass = car->m_fMass;
		v.pose.origin = car->GetPosition();
		v.pose.right = Unit(car->GetRight(), CVector(1.0f, 0.0f, 0.0f));
		v.pose.forward = Unit(car->GetForward() - v.pose.right * DotProduct(car->GetForward(), v.pose.right), CVector(0.0f, 1.0f, 0.0f));
		v.pose.up = Unit(CrossProduct(v.pose.right, v.pose.forward), CVector(0.0f, 0.0f, 1.0f));
		// Gather runs before ProcessControl applies queued friction. Predict
		// geometry with the same velocity supplied to its contact response.
		v.linearVelocity = (car->m_vecMoveSpeed + car->m_vecMoveFriction) * 50.0f;
		v.angularVelocity = (car->m_vecTurnSpeed + car->m_vecTurnFriction) * 50.0f;
		v.surfaceLinearVelocity = (car->m_vecMoveSpeed + car->m_vecMoveFriction) * 50.0f;
		v.surfaceAngularVelocity = (car->m_vecTurnSpeed + car->m_vecTurnFriction) * 50.0f;
		v.boundCenter = col->boundingSphere.center; v.boundRadius = col->boundingSphere.radius;
		v.shapeCount = 0;
		v.triangleCount = 0;
		float shapeScores[MAX_SHAPES];
		CVector localBodyFrom = PoseAt(v, -span).ToLocal(bodyCenter);
		CVector localBodyTo = PoseAt(v, span).ToLocal(bodyCenter);
		// Select by distance to the ragdoll, allowing the car's full swept
		// travel. We intentionally do not fall back to an invisible outer box.
		for(int type = 0; type < 2; ++type){
			int shapes = type == 0 ? (col->spheres ? col->numSpheres : 0) : (col->boxes ? col->numBoxes : 0);
			for(int j = 0; j < shapes; ++j){
				Shape shape;
				float extent;
				if(type == 0){
					shape.center = col->spheres[j].center; shape.radius = col->spheres[j].radius;
					if(shape.radius <= 0.0f) continue;
					shape.min = shape.max = shape.center; extent = shape.radius;
				}else{
					shape.min = col->boxes[j].min; shape.max = col->boxes[j].max; shape.radius = 0.0f;
					shape.center = (shape.min + shape.max) * 0.5f;
					extent = std::sqrt(DotProduct(shape.max - shape.center, shape.max - shape.center));
				}
				float distance = std::sqrt(SegmentDistanceSq(shape.center, localBodyFrom, localBodyTo));
				float score = MaxF(0.0f, distance - extent);
				int at = v.shapeCount;
				if(at == MAX_SHAPES){
					++batch.shapeOverflow; at = 0;
					for(int k = 1; k < MAX_SHAPES; ++k) if(shapeScores[k] > shapeScores[at]) at = k;
					if(score >= shapeScores[at]) continue;
				}else ++v.shapeCount;
				v.shapes[at] = shape; shapeScores[at] = score;
			}
		}
		if(col->numTriangles > 0 && col->triangles != 0 && col->vertices != 0){
			float triangleScores[MAX_TRIANGLES];
			int scanCount = col->numTriangles < MAX_TRIANGLE_SCAN ? col->numTriangles : MAX_TRIANGLE_SCAN;
			batch.triangleOverflow += unsigned(col->numTriangles - scanCount);
			CVector localBodyMid = v.pose.ToLocal(bodyCenter);
			for(int sample = 0; sample < scanCount; ++sample){
				++batch.triangleScans;
				int index = sample * int(col->numTriangles) / scanCount;
				const CColTriangle &face = col->triangles[index];
				Triangle triangle;
				if(!MakeTriangle(col->vertices[face.a].Get(), col->vertices[face.b].Get(), col->vertices[face.c].Get(), triangle)) continue;
				CVector da = ClosestTrianglePoint(triangle, localBodyFrom) - localBodyFrom;
				CVector db = ClosestTrianglePoint(triangle, localBodyMid) - localBodyMid;
				CVector dc = ClosestTrianglePoint(triangle, localBodyTo) - localBodyTo;
				float score = MinF(DotProduct(da, da), MinF(DotProduct(db, db), DotProduct(dc, dc)));
				KeepTriangle(v, triangle, score, triangleScores, batch.triangleOverflow);
			}
		}
	}
	Prepare(batch, 0.0f, span);
}

// Flush once after all six ragdoll slots have run. Pool generation handles
// avoid retaining game pointers. CPhysical stores metres per 50 Hz tick,
// so divide an SI impulse in Ns by 50 before ApplyMoveForce divides by mass.
inline void CommitReactions(VrRagdollReaction::Frame &frame,
	VrRagdollBrake::Ledger *ledger = 0, unsigned gameFrame = 0)
{
	if(frame.committed) return;
	frame.committed = true;
	for(int i = 0; i < frame.count; ++i){
		const VrRagdollReaction::Vehicle &request = frame.vehicles[i];
		CVehicle *car = CPools::GetVehiclePool() ? CPools::GetVehicle(request.handle) : 0;
		if(car == 0 || !car->IsCar() || !car->bUsesCollision || car->bRemoveFromWorld ||
			car->m_fMass <= 0.0f || car->IsRealHeli() || car->IsRealPlane()){
			++frame.invalidVehicles;
			continue;
		}
		const int brakePercent = VrRagdollSettings::Get().brakePercent;
		CVector impulse = VrRagdollReaction::Impulse(request, car->m_fMass, car->m_vecMoveSpeed * 50.0f)*
			VrRagdollBrake::Multiplier(float(brakePercent));
		if(ledger) impulse = VrRagdollBrake::Limit(*ledger,gameFrame,request.handle,
			car->m_fMass,car->m_vecMoveSpeed*50.0f,impulse,float(brakePercent));
		float amount = std::sqrt(DotProduct(impulse, impulse));
		if(amount <= 0.0f) continue;
		car->ApplyMoveForce(impulse * (1.0f / 50.0f));
		frame.committedImpulse += amount;
		++frame.appliedVehicles;
	}
}
#endif
} // namespace VrRagdollVehicle
