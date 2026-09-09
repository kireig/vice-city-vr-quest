#include <cstdio>
#include <cstdlib>
#include "ragdoll-test-vector.h"
#define VR_RAGDOLL_VEHICLE_NO_ENGINE
#include "VrRagdollVehicle.h"

using namespace VrRagdollVehicle;
static int checks;
static void Check(bool value, const char *what)
{
	++checks;
	if(!value){ std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
static bool Near(float a, float b, float epsilon = 0.0002f) { return std::fabs(a - b) < epsilon; }
static bool NearVector(const CVector &a, const CVector &b, float epsilon = 0.0002f) { return (a - b).Magnitude() < epsilon; }
static Shape Box(const CVector &lo, const CVector &hi)
{
	Shape s;
	s.min = lo; s.max = hi; s.center = (lo + hi) * 0.5f; s.radius = 0.0f;
	return s;
}
static Batch Fixture(bool sphere = false)
{
	Batch b;
	b.count = 1;
	Vehicle &v = b.vehicles[0];
	v.pose.origin = CVector(0.0f, 0.0f, 0.0f);
	v.pose.right = CVector(1.0f, 0.0f, 0.0f);
	v.pose.forward = CVector(0.0f, 1.0f, 0.0f);
	v.pose.up = CVector(0.0f, 0.0f, 1.0f);
	v.linearVelocity = v.angularVelocity = CVector(0.0f, 0.0f, 0.0f);
	v.surfaceLinearVelocity = v.surfaceAngularVelocity = CVector(0.0f, 0.0f, 0.0f);
	v.shapeCount = 1;
	v.shapes[0] = Box(CVector(-1.0f, -1.0f, -0.5f), CVector(1.0f, 1.0f, 0.5f));
	v.boundCenter = CVector(0.0f, 0.0f, 0.0f);
	v.boundRadius = 1.5f;
	if(sphere){ v.shapes[0].radius = 0.5f; v.boundRadius = 0.5f; }
	Prepare(b, 0.0f, 1.0f / 60.0f);
	return b;
}

static void StationaryAndRoundedCorners()
{
	Batch b = Fixture();
	Contact c;
	CVector p(-1.05f, 0.0f, 0.0f);
	Check(Find(b, p, p, 0.1f, c), "stationary car side contact");
	Check(Near(c.center.x, -1.102f) && NearVector(c.normal, CVector(-1.0f, 0.0f, 0.0f)), "nearest box face normal and radius");
	Check(!c.swept && Near(c.surfaceVelocity.MagnitudeSqr(), 0.0f), "stationary contact has no invented speed");
	p = CVector(1.08f, 1.08f, 0.0f);
	Check(!Find(b, p, p, 0.1f, c), "diagonal corner outside actual rounded box stays empty");
	Check(!Find(b, CVector(1.08f, 1.08f, -3.0f), CVector(1.08f, 1.08f, 3.0f), 0.1f, c), "swept diagonal corner avoids expanded-AABB ghost hit");
	Check(Find(b, CVector(1.06f, -3.0f, 0.0f), CVector(1.06f, 3.0f, 0.0f), 0.1f, c), "rounded vertical edge sweep detected");
	Check(c.swept && Near(c.normal.x, 0.6f, 0.001f) && Near(c.normal.y, -0.8f, 0.001f), "edge sweep gets geometric normal");
	Check(Find(b, CVector(3.0f, 3.0f, 2.5f), CVector(0.0f, 0.0f, -0.5f), 0.1f, c), "high speed corner to inside detected");
	Check(c.swept && c.normal.x > 0.0f && c.normal.y > 0.0f && c.normal.z > 0.0f, "corner entry preserves entering face instead of nearest exit");
}

static void SleepingBodySweeps()
{
	for(int shape = 0; shape < 2; ++shape){
		Batch b = Fixture(shape != 0);
		Vehicle &v = b.vehicles[0];
		v.pose.origin.x = -3.0f;
		v.linearVelocity = v.surfaceLinearVelocity = CVector(360.0f, 0.0f, 0.0f);
		Prepare(b, 0.0f, 1.0f / 60.0f);
		CVector sleeping(0.0f, 0.0f, 0.0f);
		Contact c;
		QueryStats stats;
		Check(Find(b, sleeping, sleeping, 0.1f, c, &stats), "car crossing entirely through sleeping particle is detected");
		Check(c.swept && c.normal.x > 0.999f, "sleeping body hit is classified as swept with forward normal");
		Check(c.center.x > 3.59f && Near(c.surfaceVelocity.x, 360.0f), "swept point transported to end pose and gets actual car speed");
		Check(stats.vehicleTests == 1 && stats.contacts == 1 && stats.sweeps == 1, "bounded query counters report swept contact");
		// Starting on last step's skin must not suppress a very fast sweep.
		v.pose.origin.x = -1.102f;
		if(shape) v.pose.origin.x = -0.602f;
		Prepare(b, 0.0f, 1.0f / 60.0f);
		Check(Find(b, sleeping, sleeping, 0.1f, c) && c.swept, "existing separated skin contact does not tunnel on next fast move");
		// Signed intervals are needed when Update follows rather than precedes
		// vehicle movement in a host frame.
		v.pose.origin.x = 3.0f;
		Prepare(b, -1.0f / 60.0f, 0.0f);
		Check(Find(b, sleeping, sleeping, 0.1f, c) && c.swept, "past-frame signed sweep also detects complete crossing");
	}
}

static void NoRepeatLaunch()
{
	Contact c;
	c.normal = CVector(1.0f, 0.0f, 0.0f);
	c.surfaceVelocity = CVector(7.0f, 0.0f, 0.0f);
	CVector velocity(0.0f, 2.0f, -1.0f);
	for(int n = 0; n < 1000; ++n) velocity = MatchSurfaceNormal(velocity, c);
	Check(NearVector(velocity, CVector(7.0f, 2.0f, -1.0f)), "1000 resting contacts clamp once without cumulative kick");
	Check(NearVector(MatchSurfaceNormal(CVector(9.0f, 2.0f, -1.0f), c), CVector(9.0f, 2.0f, -1.0f)), "separating motion is preserved");
	c.surfaceVelocity = CVector(0.0f, 0.0f, 0.0f);
	Check(NearVector(MatchSurfaceNormal(CVector(-2.0f, 1.0f, 0.0f), c), CVector(0.0f, 1.0f, 0.0f)), "parked contact removes inward speed without adding rebound");
}

static void RotationAndOpenSpace()
{
	Batch b = Fixture();
	Vehicle &v = b.vehicles[0];
	v.pose.right = CVector(0.0f, 1.0f, 0.0f);
	v.pose.forward = CVector(-1.0f, 0.0f, 0.0f);
	v.surfaceAngularVelocity = CVector(0.0f, 0.0f, 2.0f);
	Prepare(b, 0.0f, 0.0f);
	CVector p = v.pose.ToWorld(CVector(-1.05f, 0.0f, 0.0f));
	Contact c;
	Check(Find(b, p, p, 0.1f, c), "rotated car side collision");
	Check(NearVector(c.normal, CVector(0.0f, -1.0f, 0.0f)), "OBB collision normal rotates with car");
	Check(Near(c.surfaceVelocity.x, 2.004f) && Near(c.surfaceVelocity.y, 0.0f), "contact speed includes angular lever arm at surface");
	b = Fixture();
	Vehicle &turning = b.vehicles[0];
	turning.shapes[0] = Box(CVector(-2.0f, -0.1f, -0.5f), CVector(2.0f, 0.1f, 0.5f));
	turning.boundRadius = 2.1f;
	turning.angularVelocity = turning.surfaceAngularVelocity = CVector(0.0f, 0.0f, 31.4159265f);
	Prepare(b, 0.0f, 0.05f);
	p = CVector(1.2f, 1.2f, 0.0f);
	Check(turning.sweepSegments == MAX_SWEEP_SEGMENTS, "angular sweep work is capped");
	Check(Find(b, p, p, 0.1f, c) && c.swept, "rotating thin box crossing a resting particle between endpoints");
	Check(std::isfinite(c.center.x) && std::isfinite(c.surfaceVelocity.y), "angular hit produces finite position and speed");
	// A low chassis and shorter cabin leave the space above the bonnet open.
	b = Fixture();
	Vehicle &car = b.vehicles[0];
	car.shapeCount = 2;
	car.shapes[0] = Box(CVector(-1.0f, -2.0f, -0.5f), CVector(1.0f, 2.0f, 0.3f));
	car.shapes[1] = Box(CVector(-0.8f, -0.7f, 0.3f), CVector(0.8f, 0.7f, 1.1f));
	car.boundRadius = 2.5f;
	Prepare(b, 0.0f, 1.0f / 60.0f);
	p = CVector(0.0f, 1.5f, 0.8f);
	Check(!Find(b, p, p, 0.1f, c), "space above bonnet is not filled by outer bounding OBB");
}

static unsigned randomState = 0x7183ad25u;
static float RandomCoordinate()
{
	randomState = randomState * 1664525u + 1013904223u;
	return float(randomState >> 8) * (6.0f / 16777216.0f) - 3.0f;
}
static void BoxSweepReference()
{
	Shape box = Box(CVector(-1.0f, -0.7f, -0.5f), CVector(1.0f, 0.7f, 0.5f));
	for(int n = 0; n < 3000; ++n){
		CVector from(RandomCoordinate(), RandomCoordinate(), RandomCoordinate());
		CVector to(RandomCoordinate(), RandomCoordinate(), RandomCoordinate());
		CVector p, normal;
		float depth, t;
		if(ProjectShape(box, from, 0.1f, p, normal, depth)) continue;
		bool hit = SweepBox(box, from, to, 0.1f, t, normal);
		if(hit){
			CVector hitNormal = normal;
			CVector at = from + (to - from) * t;
			Check(ProjectShape(box, at, 0.1f, p, normal, depth), "random swept hit lies on real rounded box surface");
			Check(depth < 0.0002f, "random swept entry is first surface rather than interior");
			Check(Near(hitNormal.MagnitudeSqr(), 1.0f) && DotProduct(hitNormal, to - from) <= 0.0001f, "random swept normal is unit length and opposes incoming motion");
		}else{
			// A sampled reference cannot prove a narrow miss, but any sampled
			// overlap deeper than the contact skin MUST have an analytic hit.
			bool sampledOverlap = false;
			for(int step = 1; step <= 128 && !sampledOverlap; ++step){
				CVector at = from + (to - from) * (float(step) / 128.0f);
				sampledOverlap = ProjectShape(box, at, 0.1f, p, normal, depth) && depth > CONTACT_SKIN;
			}
			Check(!sampledOverlap, "random swept analytic miss agrees with sampled volume crossing");
		}
	}
}

static void AddQuad(Vehicle &v, const CVector &a, const CVector &b, const CVector &c, const CVector &d)
{
	Check(v.triangleCount + 2 <= MAX_TRIANGLES, "mesh fixture fits triangle budget");
	Check(MakeTriangle(a, b, c, v.triangles[v.triangleCount++]), "mesh fixture first triangle valid");
	Check(MakeTriangle(a, c, d, v.triangles[v.triangleCount++]), "mesh fixture second triangle valid");
}
static Batch MeshCar()
{
	Batch b = Fixture();
	Vehicle &v = b.vehicles[0];
	v.shapes[0] = Box(CVector(-.8f, -2.0f, .10f), CVector(.8f, 2.0f, .45f));
	v.shapeCount = 3;
	v.shapes[1] = Box(CVector(-.86f, -.95f, .10f), CVector(-.70f, -.65f, .50f));
	v.shapes[2] = Box(CVector(.70f, -.95f, .10f), CVector(.86f, -.65f, .50f));
	v.boundCenter = CVector(0, 0, .7f); v.boundRadius = 2.4f;
	// Thin real surface mesh: bonnet, sloped windshield, roof and side glass.
	AddQuad(v, CVector(-.8f, 1.2f, .65f), CVector(.8f, 1.2f, .65f), CVector(.8f, 2, .65f), CVector(-.8f, 2, .65f));
	AddQuad(v, CVector(-.75f, .7f, 1.3f), CVector(.75f, .7f, 1.3f), CVector(.8f, 1.2f, .65f), CVector(-.8f, 1.2f, .65f));
	AddQuad(v, CVector(-.75f, -.7f, 1.3f), CVector(.75f, -.7f, 1.3f), CVector(.75f, .7f, 1.3f), CVector(-.75f, .7f, 1.3f));
	AddQuad(v, CVector(-.75f, -.7f, .5f), CVector(-.75f, -.7f, 1.3f), CVector(-.75f, .7f, 1.3f), CVector(-.75f, .7f, .5f));
	AddQuad(v, CVector(.75f, -.7f, .5f), CVector(.75f, .7f, .5f), CVector(.75f, .7f, 1.3f), CVector(.75f, -.7f, 1.3f));
	Prepare(b, 0, 1.0f / 60.0f);
	return b;
}
static void MeshAtRoadSpeeds()
{
	const float speeds[] = {20.0f, 30.0f, 40.0f};
	for(int i = 0; i < 3; ++i){
		const float speed = speeds[i];
		Batch b = MeshCar();
		Contact c;
		QueryStats stats;
		CVector from(.2f, 0, 1.55f), to = from - CVector(0, 0, speed / 60.0f);
		Check(Find(b, from, to, .07f, c, &stats), "20-40m/s falling point hits thin roof triangle");
		Check(c.swept && c.normal.z > .999f && c.center.z >= 1.37f, "thin roof sweep stays on incoming upper side");
		Check(stats.triangleTests == unsigned(b.vehicles[0].triangleCount), "triangle query counter bounded by selected faces");
		// Winding-independent underside: a moving point below cannot cross up.
		Check(Find(b, CVector(.2f, 0, 1.1f), CVector(.2f, 0, 1.1f + speed / 60.0f), .07f, c), "thin roof underside is double sided");
		Check(c.swept && c.normal.z < -.999f, "underside sweep normal opposes approach");
		b.vehicles[0].linearVelocity = b.vehicles[0].surfaceLinearVelocity = CVector(0, speed, 0);
		Prepare(b, 0, 1.0f / 60.0f);
		CVector sleeping(0, 1.10f, 1.05f);
		Check(Find(b, sleeping, sleeping, .07f, c), "20-40m/s car upper windshield hits stationary point");
		Check(c.swept && c.normal.y > .5f && c.normal.z > .4f, "windshield slope retains correct upper forward normal");
		Check(Near(c.surfaceVelocity.y, speed), "mesh contact inherits actual road-car speed");
		// Confirm the regression specifically requires the mesh.
		b.vehicles[0].triangleCount = 0;
		Check(!Find(b, sleeping, sleeping, .07f, c), "old lower spheres-boxes alone miss upper-car regression");
	}
	Batch b = MeshCar();
	Contact c;
	CVector empty(0, 1.75f, 1.05f);
	Check(!Find(b, empty, empty, .07f, c), "actual bonnet triangles leave empty volume above them open");
	Check(Find(b, CVector(0, 1.75f, .9f), CVector(0, 1.75f, .5f), .07f, c) && c.center.z > .72f, "thin bonnet supports a falling point");
	Check(Find(b, CVector(.2f, 0, 1.34f), CVector(.2f, 0, .9f), .07f, c) && c.center.z >= 1.37f,
		"point initially intersecting thin roof cannot cross to opposite side");
}
static void TriangleBudgetAndReference()
{
	Vehicle v;
	float scores[MAX_TRIANGLES];
	unsigned overflow = 0;
	for(int i = 0; i < MAX_TRIANGLES * 3; ++i){
		Triangle t;
		float z = float(MAX_TRIANGLES * 3 - i);
		Check(MakeTriangle(CVector(-1, -1, z), CVector(1, -1, z), CVector(0, 1, z), t), "selection fixture triangle valid");
		KeepTriangle(v, t, z * z, scores, overflow);
	}
	Check(v.triangleCount == MAX_TRIANGLES && overflow == MAX_TRIANGLES * 2, "triangle selection and overflow strictly bounded");
	for(int i = 0; i < v.triangleCount; ++i) Check(v.triangles[i].a.z <= float(MAX_TRIANGLES), "near roof faces replace distant triangles");
	Triangle t;
	Check(MakeTriangle(CVector(-1, -.7f, .3f), CVector(1, -.7f, .7f), CVector(.1f, 1, .8f), t), "sloped reference triangle valid");
	for(int n = 0; n < 2000; ++n){
		CVector from(RandomCoordinate(), RandomCoordinate(), RandomCoordinate()), to(RandomCoordinate(), RandomCoordinate(), RandomCoordinate());
		CVector p, normal;
		float depth, time;
		if(ProjectTriangle(t, from, .1f, p, normal, depth)) continue;
		bool hit = SweepTriangle(t, from, to, .1f, time, normal);
		if(hit){
			CVector hitNormal = normal;
			Check(ProjectTriangle(t, from + (to - from) * time, .1f, p, normal, depth), "random CCD entry lies on rounded triangle");
			Check(depth < .0002f && Near(hitNormal.MagnitudeSqr(), 1.0f), "random triangle hit is a surface entry with unit normal");
			Check(DotProduct(hitNormal, to - from) <= .0001f, "random triangle normal opposes incoming motion");
		}else{
			bool overlap = false;
			for(int sample = 1; sample <= 128 && !overlap; ++sample)
				overlap = ProjectTriangle(t, from + (to - from) * (float(sample) / 128.0f), .1f, p, normal, depth) && depth > CONTACT_SKIN;
			Check(!overlap, "random analytic triangle miss agrees with sampled volume");
		}
	}
}

int main()
{
	StationaryAndRoundedCorners(); SleepingBodySweeps(); NoRepeatLaunch(); RotationAndOpenSpace(); BoxSweepReference();
	MeshAtRoadSpeeds(); TriangleBudgetAndReference();
	std::printf("vehicle-contact-tests: %d checks passed\n", checks);
	return 0;
}
