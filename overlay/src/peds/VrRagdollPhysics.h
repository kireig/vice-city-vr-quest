#ifndef VR_RAGDOLL_PHYSICS_H
#define VR_RAGDOLL_PHYSICS_H

// The fixed-size solver depends only on CVector and can also be exercised by
// the host fixture. Units are metres, seconds and approximate kilograms.
#include <math.h>
#include "VrRagdollSettings.h"

namespace VrRagdollPhysics
{

enum {
	RD_PELVIS = 0, RD_SPINE, RD_SPINE1, RD_NECK, RD_HEAD,
	RD_LCLAV, RD_LUARM, RD_LFARM, RD_LHAND,
	RD_RCLAV, RD_RUARM, RD_RFARM, RD_RHAND,
	RD_LTHIGH, RD_LCALF, RD_LFOOT,
	RD_RTHIGH, RD_RCALF, RD_RFOOT,
	SIM_BONES, NUM_BENDS = 4, NUM_HIPS = 2, NUM_TORSO_BENDS = 3,
	NUM_BODY_CONES = 2, SKELETON_STICKS = 18
};

static const float STEP = 1.0f/60.0f;
// CPhysical subtracts 0.008 per 50 Hz game step: 0.008*50*50 = 20 m/s2.
static const float FALL_GRAVITY = 20.0f;
static const float JOINT_RADIUS = 0.17f; // largest point, for broadphase bounds
static const float DAMPING = 0.999f;
static const float GROUND_FRICTION = 0.7f;
static const float GROUND_SLIDING_FRICTION = 0.35f;
static const float SLEEP_SPEED = 0.15f;
static const float SLEEP_AFTER = 0.6f;

// Total 70 kg. Light hands and feet yield to the trunk in constraints;
// gravity itself remains independent of mass.
static const float invMass[SIM_BONES] = {
	1.0f/16.0f, 1.0f/8.0f, 1.0f/10.0f, 1.0f/1.5f, 1.0f/4.5f,
	1.0f, 1.0f/2.2f, 1.0f/1.3f, 1.0f/0.5f,
	1.0f, 1.0f/2.2f, 1.0f/1.3f, 1.0f/0.5f,
	1.0f/6.0f, 1.0f/3.0f, 1.0f,
	1.0f/6.0f, 1.0f/3.0f, 1.0f
};
static const float radius[SIM_BONES] = {
	0.14f, 0.16f, 0.17f, 0.085f, 0.13f,
	0.09f, 0.085f, 0.07f, 0.055f,
	0.09f, 0.085f, 0.07f, 0.055f,
	0.11f, 0.085f, 0.075f, 0.11f, 0.085f, 0.075f
};

struct Stick {
	unsigned char a, b, minPercent;
};
static const Stick sticks[] = {
	{ RD_PELVIS, RD_SPINE, 0 }, { RD_SPINE, RD_SPINE1, 0 },
	{ RD_SPINE1, RD_NECK, 0 }, { RD_NECK, RD_HEAD, 0 },
	{ RD_SPINE1, RD_LCLAV, 0 }, { RD_LCLAV, RD_LUARM, 0 },
	{ RD_LUARM, RD_LFARM, 0 }, { RD_LFARM, RD_LHAND, 0 },
	{ RD_SPINE1, RD_RCLAV, 0 }, { RD_RCLAV, RD_RUARM, 0 },
	{ RD_RUARM, RD_RFARM, 0 }, { RD_RFARM, RD_RHAND, 0 },
	{ RD_PELVIS, RD_LTHIGH, 0 }, { RD_LTHIGH, RD_LCALF, 0 },
	{ RD_LCALF, RD_LFOOT, 0 },
	{ RD_PELVIS, RD_RTHIGH, 0 }, { RD_RTHIGH, RD_RCALF, 0 },
	{ RD_RCALF, RD_RFOOT, 0 },
	// Pelvis and ribcage keep their widths. The old collinear spine
	// diagonals locked the whole back straight; angular limits replace them.
	{ RD_LTHIGH, RD_RTHIGH, 0 }, { RD_LCLAV, RD_RCLAV, 0 },
	{ RD_LCLAV, RD_NECK, 0 }, { RD_RCLAV, RD_NECK, 0 },
	// The hip width alone leaves the hips free to fold up beside the lumbar
	// joint. These short diagonals keep the pelvic frame anatomical while
	// leaving the next lumbar joint free to bend.
	{ RD_LTHIGH, RD_SPINE, 0 }, { RD_RTHIGH, RD_SPINE, 0 }
};
enum { NUM_STICKS = sizeof(sticks)/sizeof(sticks[0]) };

struct Frame {
	CVector right, forward, up;
};

struct Bend {
	unsigned char a, b, c;
	bool pelvis;
	CVector localUpper, localBend;
	float minReach, sideSlack;
};

struct HipLimit {
	unsigned char a, b;
	float side, shareA;
};

struct TorsoBend {
	unsigned char a, c;
	float minReach, shareA;
};

struct BodyCone {
	unsigned char a, b;
	bool pelvis;
	CVector localAxis, localTangent;
	float cosine, sine, shareA;
};

struct Contact {
	bool active;
	bool staticWorld;
	CVector normal, surfaceVelocity;
};

struct State {
	bool asleep;
	float sleepTime, stepDebt, groundZ, age;
	CVector pos[SIM_BONES], prev[SIM_BONES];
	float stickLen[NUM_STICKS], stickShareA[NUM_STICKS];
	Bend bends[NUM_BENDS];
	HipLimit hips[NUM_HIPS];
	TorsoBend torsoBends[NUM_TORSO_BENDS];
	BodyCone bodyCones[NUM_BODY_CONES];
	float restTwistCosine, restTwistSine;
	Frame restPelvis, restTorso, pelvisFrame, torsoFrame;
	Contact contact[SIM_BONES];
	int lastSupportCount;
	float lastMaxSpeedSq, lastMaxTravelSq, lastMaxLengthError, lastMaxSelfOverlap;
};

static inline float
MaxF(float a, float b) { return a > b ? a : b; }
static inline float
MinF(float a, float b) { return a < b ? a : b; }
static inline float
Clamp(float x, float lo, float hi) { return MaxF(lo, MinF(x, hi)); }

static inline CVector
Unit(const CVector &v, const CVector &fallback)
{
	const float lenSq = v.MagnitudeSqr();
	return lenSq > 1e-10f ? v*(1.0f/sqrtf(lenSq)) : fallback;
}

static inline CVector
Perpendicular(const CVector &v)
{
	// This branch is used only for a collapsed frame or an exact 180-degree
	// swing; ordinary hinge directions always come from the anatomy frame.
	const CVector axis = fabsf(v.x) < fabsf(v.z) ?
		CVector(1.0f, 0.0f, 0.0f) : CVector(0.0f, 0.0f, 1.0f);
	return Unit(CrossProduct(v, axis), CVector(0.0f, 1.0f, 0.0f));
}

static inline Frame
MakeFrame(const CVector &across, const CVector &vertical,
	const Frame &fallback)
{
	Frame f;
	f.up = Unit(vertical, fallback.up);
	CVector right = across - f.up*DotProduct(across, f.up);
	if(right.MagnitudeSqr() < 1e-8f)
		right = fallback.right - f.up*DotProduct(fallback.right, f.up);
	const float rightSq = right.MagnitudeSqr();
	f.right = rightSq > 1e-10f ? right*(1.0f/sqrtf(rightSq)) : Perpendicular(f.up);
	f.forward = CrossProduct(f.up, f.right);
	f.right = CrossProduct(f.forward, f.up);
	return f;
}

static inline CVector
Local(const Frame &f, const CVector &v)
{
	return CVector(DotProduct(f.right, v), DotProduct(f.forward, v),
		DotProduct(f.up, v));
}

static inline CVector
World(const Frame &f, const CVector &v)
{
	return f.right*v.x + f.forward*v.y + f.up*v.z;
}

static inline CVector
Carry(const Frame &rest, const Frame &current, const CVector &v)
{
	return World(current, Local(rest, v));
}

static inline CVector
Swing(const CVector &a, const CVector &b, const CVector &v,
	const CVector &halfTurnAxis)
{
	const CVector cross = CrossProduct(a, b);
	const float cosine = Clamp(DotProduct(a, b), -1.0f, 1.0f);
	if(cosine < -0.9999f){
		CVector axis = halfTurnAxis - a*DotProduct(halfTurnAxis, a);
		axis = Unit(axis, Perpendicular(a));
		return axis*(2.0f*DotProduct(axis, v)) - v;
	}
	// Rodrigues without an angle or an axis normalisation.
	return v + CrossProduct(cross, v) +
		CrossProduct(cross, CrossProduct(cross, v))*(1.0f/(1.0f + cosine));
}

static inline void
UpdateFrames(State *s)
{
	// The first lumbar segment is a stable pelvic tilt reference. Extending
	// it to spine1 made the pelvis follow chest bends; the very shallow hip
	// triangle can instead invert during a finite-iteration impact solve.
	const CVector pelvicUp = s->pos[RD_SPINE] - s->pos[RD_PELVIS];
	s->pelvisFrame = MakeFrame(s->pos[RD_RTHIGH] - s->pos[RD_LTHIGH],
		pelvicUp, s->pelvisFrame);
	s->torsoFrame = MakeFrame(s->pos[RD_RCLAV] - s->pos[RD_LCLAV],
		s->pos[RD_NECK] - s->pos[RD_SPINE1], s->torsoFrame);
}

static inline void
Wake(State *s)
{
	s->asleep = false;
	s->sleepTime = 0.0f;
}

static inline CVector
CentreOfMass(const State *s)
{
	CVector centre(0.0f, 0.0f, 0.0f);
	float mass = 0.0f;
	for(int i = 0; i < SIM_BONES; i++){
		const float m = 1.0f/invMass[i];
		centre += s->pos[i]*m;
		mass += m;
	}
	return centre*(1.0f/mass);
}

static inline CVector
LimitVector(const CVector &v, float limit)
{
	const float speedSq = v.MagnitudeSqr();
	return speedSq > limit*limit ? v*(limit/sqrtf(speedSq)) : v;
}

static inline void
SeedMotion(State *s, const CVector &linear, const CVector &angular, float maxLinearSpeed = 16.0f)
{
	Wake(s);
	s->stepDebt = 0.0f;
	// Vehicle takeover can already be moving with a fast bonnet. The usual
	// 16m/s fall cap must not silently discard that shared translation.
	const float limit = maxLinearSpeed > 0.0f && maxLinearSpeed <= 60.0f ? maxLinearSpeed : 16.0f;
	const CVector v = LimitVector(linear, limit);
	const CVector omega = LimitVector(angular, 6.0f);
	const CVector centre = CentreOfMass(s);
	for(int i = 0; i < SIM_BONES; i++)
		s->prev[i] = s->pos[i] -
			(v + CrossProduct(omega, s->pos[i] - centre))*STEP;
}

static inline void
Initialize(State *s, const CVector &rightHint, const CVector &upHint)
{
	Frame fallback;
	fallback.up = Unit(upHint, CVector(0.0f, 0.0f, 1.0f));
	fallback.right = Unit(rightHint - fallback.up*DotProduct(rightHint, fallback.up),
		Perpendicular(fallback.up));
	fallback.forward = CrossProduct(fallback.up, fallback.right);
	s->pelvisFrame = s->torsoFrame = fallback;
	UpdateFrames(s);
	s->restPelvis = s->pelvisFrame;
	s->restTorso = s->torsoFrame;
	s->age = 0.0f;
	s->stepDebt = 0.0f;
	Wake(s);
	s->lastSupportCount = 0;
	s->lastMaxSpeedSq = s->lastMaxTravelSq = s->lastMaxLengthError = 0.0f;
	s->lastMaxSelfOverlap = 0.0f;
	for(int i = 0; i < SIM_BONES; i++){
		s->prev[i] = s->pos[i];
		s->contact[i].active = false;
	}
	for(int i = 0; i < NUM_STICKS; i++){
		s->stickLen[i] = (s->pos[sticks[i].b] - s->pos[sticks[i].a]).Magnitude();
		s->stickShareA[i] = invMass[sticks[i].a]/
			(invMass[sticks[i].a] + invMass[sticks[i].b]);
	}
	static const unsigned char ids[NUM_BENDS][3] = {
		{ RD_LTHIGH, RD_LCALF, RD_LFOOT },
		{ RD_RTHIGH, RD_RCALF, RD_RFOOT },
		{ RD_LUARM, RD_LFARM, RD_LHAND },
		{ RD_RUARM, RD_RFARM, RD_RHAND }
	};
	for(int i = 0; i < NUM_BENDS; i++){
		Bend &b = s->bends[i];
		b.a = ids[i][0]; b.b = ids[i][1]; b.c = ids[i][2];
		b.pelvis = i < 2;
		const Frame &f = b.pelvis ? s->pelvisFrame : s->torsoFrame;
		const CVector upper = s->pos[b.b] - s->pos[b.a];
		const CVector lower = s->pos[b.c] - s->pos[b.b];
		const float upperLen = upper.Magnitude(), lowerLen = lower.Magnitude();
		const CVector direction = Unit(upper, f.up);
		// Arms retain the interrupted animation's bend plane. Knees instead
		// use the anatomical forward hinge: nearly straight animated legs
		// have no measurable bend plane, and a twisted death pose must not
		// permanently turn a sideways/backwards knee into the reference.
		CVector bend = direction*DotProduct(lower, direction) - lower;
		if(bend.MagnitudeSqr() < 0.0001f){
			bend = f.forward*(b.pelvis ? 1.0f : -1.0f);
			bend -= direction*DotProduct(bend, direction);
			if(bend.MagnitudeSqr() < 1e-8f)
				bend = f.right - direction*DotProduct(f.right, direction);
		}
		b.localUpper = Local(f, direction);
		b.localBend = Local(f, Unit(bend, Perpendicular(direction)));
		if(b.pelvis){
			b.localUpper = CVector(0.0f, 0.0f, -1.0f);
			b.localBend = CVector(0.0f, 1.0f, 0.0f);
		}
		// At most 145 degrees knee flexion, 150 degrees elbow flexion.
		// Limits describe anatomy, not the pose at takeover. Strength ramps
		// in after capture instead of admitting an invalid pose forever.
		const float cosine = b.pelvis ? -0.819152f : -0.866025f;
		const float limitSq = upperLen*upperLen + lowerLen*lowerLen +
			2.0f*upperLen*lowerLen*cosine;
		b.minReach = sqrtf(MaxF(0.0f, limitSq));
		b.sideSlack = MaxF(0.015f, (b.pelvis ? 0.09f : 0.06f)*(upperLen + lowerLen));
	}
	for(int i = 0; i < NUM_HIPS; i++){
		HipLimit &h = s->hips[i];
		h.a = (unsigned char)(i == 0 ? RD_LTHIGH : RD_RTHIGH);
		h.b = (unsigned char)(i == 0 ? RD_LCALF : RD_RCALF);
		h.side = i == 0 ? -1.0f : 1.0f;
		h.shareA = invMass[h.a]/(invMass[h.a] + invMass[h.b]);
	}
	static const unsigned char torsoIds[NUM_TORSO_BENDS][3] = {
		{ RD_PELVIS, RD_SPINE, RD_SPINE1 },
		{ RD_SPINE, RD_SPINE1, RD_NECK },
		{ RD_SPINE1, RD_NECK, RD_HEAD }
	};
	// Bend allowance: lumbar 40, upper back 35, neck 65 degrees.
	static const float bendCosine[NUM_TORSO_BENDS] = { 0.766044f, 0.819152f, 0.422618f };
	for(int i = 0; i < NUM_TORSO_BENDS; i++){
		TorsoBend &b = s->torsoBends[i];
		b.a = torsoIds[i][0]; b.c = torsoIds[i][2];
		const int middle = torsoIds[i][1];
		const float upper = (s->pos[middle] - s->pos[b.a]).Magnitude();
		const float lower = (s->pos[b.c] - s->pos[middle]).Magnitude();
		const float reachSq = upper*upper + lower*lower + 2.0f*upper*lower*bendCosine[i];
		b.minReach = sqrtf(MaxF(0.0f, reachSq));
		b.shareA = invMass[b.a]/(invMass[b.a] + invMass[b.c]);
	}
	static const unsigned char coneIds[NUM_BODY_CONES][2] = {
		{ RD_LUARM, RD_LFARM }, { RD_RUARM, RD_RFARM }
	};
	for(int i = 0; i < NUM_BODY_CONES; i++){
		BodyCone &c = s->bodyCones[i];
		c.a = coneIds[i][0]; c.b = coneIds[i][1]; c.pelvis = false;
		const Frame &f = c.pelvis ? s->pelvisFrame : s->torsoFrame;
		c.localAxis = CVector(i == 0 ? -0.6f : 0.6f, 0.0f, -0.8f);
		const CVector local = Local(f, Unit(s->pos[c.b] - s->pos[c.a], World(f, c.localAxis)));
		// Shoulders have a broad 145-degree swing and remain free inside it.
		// Existing extreme poses widen their limit rather than snapping.
		c.cosine = MinF(-0.819152f,
			Clamp(DotProduct(local, c.localAxis), -1.0f, 1.0f));
		c.sine = sqrtf(MaxF(0.0f, 1.0f - c.cosine*c.cosine));
		c.localTangent = Unit(local - c.localAxis*DotProduct(local, c.localAxis),
			Perpendicular(c.localAxis));
		c.shareA = invMass[c.a]/(invMass[c.a] + invMass[c.b]);
	}
	const CVector carriedRight = Swing(s->pelvisFrame.up, s->torsoFrame.up,
		s->pelvisFrame.right, s->pelvisFrame.forward);
	s->restTwistCosine = Clamp(DotProduct(carriedRight, s->torsoFrame.right), -1.0f, 1.0f);
	s->restTwistSine = DotProduct(CrossProduct(carriedRight, s->torsoFrame.right), s->torsoFrame.up);
}

static inline void
SolveDistance(CVector &a, CVector &b, float rest, float shareA, bool minimum)
{
	const CVector delta = b - a;
	const float lenSq = delta.MagnitudeSqr();
	if(lenSq < 1e-10f || (minimum && lenSq >= rest*rest))
		return;
	const float len = sqrtf(lenSq);
	const CVector correction = delta*((len - rest)/len);
	a += correction*shareA;
	b -= correction*(1.0f - shareA);
}

static inline void
SolveBone(State *s, int stick, float length, bool minimum)
{
	const int a = sticks[stick].a, b = sticks[stick].b;
	const bool groundA = s->pos[a].z <= s->groundZ + radius[a] + 0.001f;
	const bool groundB = s->pos[b].z <= s->groundZ + radius[b] + 0.001f;
	if(!s->contact[a].active && !s->contact[b].active && !groundA && !groundB){
		SolveDistance(s->pos[a], s->pos[b], length, s->stickShareA[stick], minimum);
		return;
	}
	// A contacting point cannot spend a bone correction inside the car/floor:
	// collision projection would undo it at the end of every iteration.
	// Redistribute the correction through allowed tangent motion and the
	// unblocked neighbour instead. This uses cached planes, not new queries.
	const CVector delta = s->pos[b] - s->pos[a];
	const float lenSq = delta.MagnitudeSqr();
	if(lenSq < 1e-10f || (minimum && lenSq >= length*length)) return;
	const float len = sqrtf(lenSq), error = len - length;
	const CVector axis = delta*(1.0f/len);
	CVector mobility[2] = { axis, -axis };
	const int nodes[2] = { a, b };
	for(int n = 0; n < 2; ++n){
		const int node = nodes[n];
		const Contact &c = s->contact[node];
		const float normalPart = c.active ? DotProduct(mobility[n], c.normal) : 0.0f;
		if(normalPart*error < 0.0f) mobility[n] -= c.normal*normalPart;
		if(s->pos[node].z <= s->groundZ + radius[node] + 0.001f && mobility[n].z*error < 0.0f)
			mobility[n].z = 0.0f;
	}
	const float wa = invMass[a], wb = invMass[b];
	const float effective = wa*DotProduct(axis,mobility[0]) - wb*DotProduct(axis,mobility[1]);
	if(effective <= 0.05f*(wa+wb)) return;
	s->pos[a] += mobility[0]*(error*wa/effective);
	s->pos[b] += mobility[1]*(error*wb/effective);
}

static inline void
ProjectBend(State *s, const Bend &bend, const CVector &axis, float error,
	float fraction, float strength)
{
	const float wa = invMass[bend.a], wb = invMass[bend.b], wc = invMass[bend.c];
	const float a = 1.0f - fraction;
	const float effectiveMass = wa*a*a + wb + wc*fraction*fraction;
	const CVector correction = axis*(-error*strength/effectiveMass);
	s->pos[bend.a] -= correction*(wa*a);
	s->pos[bend.b] += correction*wb;
	s->pos[bend.c] -= correction*(wc*fraction);
}

static inline void
SolveReach(State *s, int a, int b, float minimum, float strength,
	const CVector &fallback)
{
	const CVector delta = s->pos[b] - s->pos[a];
	const float lengthSq = delta.MagnitudeSqr();
	if(lengthSq >= minimum*minimum || strength <= 0.0f) return;
	const float length = sqrtf(MaxF(0.0f, lengthSq));
	const CVector axis = length > 1e-5f ? delta*(1.0f/length) : fallback;
	const float shareA = invMass[a]/(invMass[a] + invMass[b]);
	const CVector correction = axis*((minimum - length)*strength);
	s->pos[a] -= correction*shareA;
	s->pos[b] += correction*(1.0f - shareA);
}

static inline void
SolveBends(State *s)
{
	const float strength = MinF(1.0f, s->age/0.15f)*0.65f;
	for(int i = 0; i < NUM_BENDS; i++){
		const Bend &bend = s->bends[i];
		const Frame &f = bend.pelvis ? s->pelvisFrame : s->torsoFrame;
		const CVector baseUpper = World(f, bend.localUpper);
		const CVector baseBend = World(f, bend.localBend);
		const CVector upper = Unit(s->pos[bend.b] - s->pos[bend.a], baseUpper);
		const CVector preferred = Swing(baseUpper, upper, baseBend, baseBend);
		const CVector side = Unit(CrossProduct(upper, preferred), f.right);
		const CVector chord = s->pos[bend.c] - s->pos[bend.a];
		const float chordSq = chord.MagnitudeSqr();
		const float fraction = chordSq > 1e-8f ?
			Clamp(DotProduct(s->pos[bend.b] - s->pos[bend.a], chord)/chordSq,
				0.0f, 1.0f) : 0.5f;
		const CVector offset = s->pos[bend.b] - s->pos[bend.a] - chord*fraction;
		const float signedBend = DotProduct(offset, preferred);
		if(signedBend < -0.002f)
			ProjectBend(s, bend, preferred, signedBend + 0.002f, fraction, strength);
		const float lateral = DotProduct(offset, side);
		if(fabsf(lateral) > bend.sideSlack)
			ProjectBend(s, bend, side,
				lateral - Clamp(lateral, -bend.sideSlack, bend.sideSlack),
				fraction, strength*0.5f);
		SolveReach(s, bend.a, bend.c, bend.minReach, strength, upper);
	}
}

static inline void
SolveHips(State *s)
{
	const float strength = MinF(1.0f, s->age/0.15f)*0.65f;
	for(int i = 0; i < NUM_HIPS; i++){
		const HipLimit &h = s->hips[i];
		const CVector delta = s->pos[h.b] - s->pos[h.a];
		const float length = delta.Magnitude();
		if(length < 1e-5f) continue;
		const CVector local = Local(s->pelvisFrame, delta*(1.0f/length));
		// An asymmetric swing envelope, not a 125-degree cone in every
		// direction: 35 extension / 110 flexion, 55 abduction / 20 adduction.
		// The sagittal wedge uses two half planes and fixed trig constants;
		// no per-joint acos/atan or solver allocation is needed.
		const float rawOutward = local.x*h.side;
		const bool beyondBack = 0.819152f*local.y - 0.573576f*local.z < 0.0f;
		const bool beyondFront = 0.342020f*local.y - 0.939693f*local.z < 0.0f;
		if(!beyondBack && !beyondFront && rawOutward >= -0.342020f && rawOutward <= 0.819152f)
			continue;
		const float outward = Clamp(rawOutward, -0.342020f, 0.819152f);
		CVector sagittal = Unit(CVector(0.0f, local.y, local.z), CVector(0.0f, 0.0f, -1.0f));
		const CVector back(0.0f, -0.573576f, -0.819152f);
		const CVector front(0.0f, 0.939693f, 0.342020f);
		if(beyondBack || beyondFront){
			if(beyondBack && beyondFront)
				sagittal = DotProduct(sagittal, back) > DotProduct(sagittal, front) ? back : front;
			else sagittal = beyondBack ? back : front;
		}
		const float plane = sqrtf(MaxF(0.0f, 1.0f - outward*outward));
		const CVector target = World(s->pelvisFrame,
			CVector(outward*h.side, sagittal.y*plane, sagittal.z*plane))*length;
		// A fast impact can violate a limit by almost half a turn. Resolving
		// that full angle in one pass used to throw the knee 20 cm and the
		// attached ankle farther still. Limit angular repair per pass and
		// redistribute blocked motion instead of pushing into the floor/car.
		const CVector correction = LimitVector((delta - target)*strength, length*0.075f);
		const float error = correction.Magnitude();
		if(error < 1e-8f) continue;
		const CVector axis = correction*(1.0f/error);
		CVector mobility[2] = { axis, -axis };
		const int nodes[2] = { h.a, h.b };
		for(int n = 0; n < 2; ++n){
			const Contact &contact = s->contact[nodes[n]];
			const float into = contact.active ? DotProduct(mobility[n], contact.normal) : 0.0f;
			if(into < 0.0f) mobility[n] -= contact.normal*into;
			if(s->pos[nodes[n]].z <= s->groundZ + radius[nodes[n]] + 0.001f && mobility[n].z < 0.0f)
				mobility[n].z = 0.0f;
		}
		const float wa = invMass[h.a], wb = invMass[h.b];
		const float effective = wa*DotProduct(axis, mobility[0]) - wb*DotProduct(axis, mobility[1]);
		if(effective < 0.05f*(wa + wb)) continue;
		float scale = error/effective;
		const float maxMoveSq = MaxF(mobility[0].MagnitudeSqr()*wa*wa,
			mobility[1].MagnitudeSqr()*wb*wb);
		if(maxMoveSq*scale*scale > 0.025f*0.025f)
			scale = 0.025f/sqrtf(maxMoveSq);
		s->pos[h.a] += mobility[0]*(wa*scale);
		s->pos[h.b] += mobility[1]*(wb*scale);
	}
}

static inline void
RotatePair(State *s, int a, int b, const CVector &axis, float sine)
{
	const CVector delta = s->pos[b] - s->pos[a];
	const float cosine = sqrtf(MaxF(0.0f, 1.0f - sine*sine));
	const CVector target = delta*cosine + CrossProduct(axis, delta)*sine +
		axis*(DotProduct(axis, delta)*(1.0f - cosine));
	const CVector correction = delta - target;
	const float shareA = invMass[a]/(invMass[a] + invMass[b]);
	s->pos[a] += correction*shareA;
	s->pos[b] -= correction*(1.0f - shareA);
}

static inline void
SolveTorso(State *s)
{
	const float strength = MinF(1.0f, s->age/0.15f)*0.65f;
	// These are minimum reaches, not rigid diagonals or rest-pose springs:
	// lumbar, upper-back and neck joints can move freely within their limits.
	for(int i = 0; i < NUM_TORSO_BENDS; i++){
		const TorsoBend &b = s->torsoBends[i];
		SolveReach(s, b.a, b.c, b.minReach, strength, s->torsoFrame.up);
	}
	for(int i = 0; i < NUM_BODY_CONES; i++){
		const BodyCone &c = s->bodyCones[i];
		const Frame &f = c.pelvis ? s->pelvisFrame : s->torsoFrame;
		const CVector axis = World(f, c.localAxis);
		const CVector delta = s->pos[c.b] - s->pos[c.a];
		const float length = delta.Magnitude(), along = DotProduct(delta, axis);
		if(length < 1e-5f || along >= c.cosine*length)
			continue;
		const CVector tangent = Unit(delta - axis*along, World(f, c.localTangent));
		const CVector target = (axis*c.cosine + tangent*c.sine)*length;
		const CVector correction = (delta - target)*strength;
		s->pos[c.a] += correction*c.shareA;
		s->pos[c.b] -= correction*(1.0f - c.shareA);
	}
	// Transport the pelvis axis through the current back bend, then limit
	// chest twist to +/-65 degrees about it. This does not oppose legal
	// waist/chest flexion and it has no restoring force within that range.
	const CVector axis = s->torsoFrame.up;
	const CVector carriedRight = Swing(s->pelvisFrame.up, axis,
		s->pelvisFrame.right, s->pelvisFrame.forward);
	const CVector reference = carriedRight*s->restTwistCosine +
		CrossProduct(axis, carriedRight)*s->restTwistSine;
	const CVector side = CrossProduct(axis, reference);
	const float cosine = DotProduct(reference, s->torsoFrame.right);
	if(cosine >= 0.422618f)
		return;
	const float sign = DotProduct(side, s->torsoFrame.right) < 0.0f ? -1.0f : 1.0f;
	const CVector target = reference*0.422618f + side*(sign*0.906308f);
	const float turn = Clamp(DotProduct(CrossProduct(s->torsoFrame.right, target), axis),
		-0.35f, 0.35f)*strength;
	const CVector chest = s->pos[RD_RCLAV] - s->pos[RD_LCLAV];
	const CVector hips = s->pos[RD_RTHIGH] - s->pos[RD_LTHIGH];
	const float chestInertia = MaxF(0.0f, chest.MagnitudeSqr() - DotProduct(chest, axis)*DotProduct(chest, axis))*0.5f;
	const float hipInertia = MaxF(0.0f, hips.MagnitudeSqr() - DotProduct(hips, axis)*DotProduct(hips, axis))*3.0f;
	const float inertia = chestInertia + hipInertia;
	if(inertia > 1e-8f){
		// Share the correction by rotational inertia rather than anchoring
		// the pelvis in world space. Both point-pair centres stay fixed.
		RotatePair(s, RD_LCLAV, RD_RCLAV, axis, turn*(hipInertia/inertia));
		RotatePair(s, RD_LTHIGH, RD_RTHIGH, axis, -turn*(chestInertia/inertia));
	}
}

static inline void
SeparateCapsules(State *s, int a, int b, int c, int d, float separation,
	float trimA, float trimB, const CVector &fallback, float strength,
	CVector *velocity = 0, const CVector *incoming = 0)
{
	// Closest points of two finite bone segments. Trimming only the upper
	// thighs allows the two legs to meet naturally at the pelvis while
	// excluding intersections farther down the limbs.
	const CVector p = s->pos[a]*(1.0f - trimA) + s->pos[b]*trimA;
	const CVector q = s->pos[c]*(1.0f - trimB) + s->pos[d]*trimB;
	const CVector u = s->pos[b] - p, v = s->pos[d] - q, r = p - q;
	const float uu = DotProduct(u, u), vv = DotProduct(v, v);
	const float uv = DotProduct(u, v), ur = DotProduct(u, r), vr = DotProduct(v, r);
	float x = 0.0f, y = 0.0f;
	if(uu > 1e-10f && vv > 1e-10f){
		const float determinant = uu*vv - uv*uv;
		x = determinant > 1e-10f ? Clamp((uv*vr - ur*vv)/determinant, 0.0f, 1.0f) : 0.0f;
		y = (uv*x + vr)/vv;
		if(y < 0.0f){ y = 0.0f; x = Clamp(-ur/uu, 0.0f, 1.0f); }
		else if(y > 1.0f){ y = 1.0f; x = Clamp((uv - ur)/uu, 0.0f, 1.0f); }
	}else if(uu > 1e-10f) x = Clamp(-ur/uu, 0.0f, 1.0f);
	else if(vv > 1e-10f) y = Clamp(vr/vv, 0.0f, 1.0f);
	const CVector delta = p + u*x - q - v*y;
	const float distanceSq = delta.MagnitudeSqr();
	const float contactDistance = separation + (velocity ? 0.005f : 0.0f);
	if(distanceSq >= contactDistance*contactDistance) return;
	const float distance = sqrtf(MaxF(0.0f, distanceSq));
	const float overlap = separation - distance;
	s->lastMaxSelfOverlap = MaxF(s->lastMaxSelfOverlap, overlap);
	if(strength <= 0.0f && !velocity) return;
	const CVector normal = distance > 1e-5f ? delta*(1.0f/distance) : fallback;
	x = trimA + x*(1.0f - trimA);
	y = trimB + y*(1.0f - trimB);
	const float gradient[4] = { 1.0f - x, x, -(1.0f - y), -y };
	const int node[4] = { a, b, c, d };
	if(velocity){
		// Position-only self contact turns the gravity/depenetration repair
		// into a fresh separating velocity every frame. At a crossed-leg
		// resting pose that became a tiny perpetual motor. Resolve only the
		// contact normal, sharing impulse across both bones; tangential and
		// common rigid motion retain their velocity and total momentum.
		float solvedNormal = 0.0f, incomingNormal = 0.0f, effective = 0.0f;
		for(int i = 0; i < 4; ++i){
			solvedNormal += DotProduct(velocity[node[i]], normal)*gradient[i];
			incomingNormal += DotProduct(incoming[node[i]], normal)*gradient[i];
			effective += invMass[node[i]]*gradient[i]*gradient[i];
		}
		if(effective > 1e-6f){
			const float retained = Clamp(solvedNormal, 0.0f, MaxF(0.0f, incomingNormal));
			const float impulse = (retained - solvedNormal)/effective;
			for(int i = 0; i < 4; ++i)
				velocity[node[i]] += normal*(impulse*invMass[node[i]]*gradient[i]);
		}
		return;
	}
	CVector mobility[4];
	float effective = 0.0f, freeEffective = 0.0f;
	for(int i = 0; i < 4; ++i){
		mobility[i] = normal*gradient[i];
		const Contact &contact = s->contact[node[i]];
		const float into = contact.active ? DotProduct(mobility[i], contact.normal) : 0.0f;
		if(into < 0.0f) mobility[i] -= contact.normal*into;
		if(s->pos[node[i]].z <= s->groundZ + radius[node[i]] + 0.001f && mobility[i].z < 0.0f)
			mobility[i].z = 0.0f;
		effective += invMass[node[i]]*gradient[i]*DotProduct(normal, mobility[i]);
		freeEffective += invMass[node[i]]*gradient[i]*gradient[i];
	}
	if(effective < MaxF(1e-6f, freeEffective*0.05f)) return;
	// Deep takeover overlaps resolve gradually, not as an explosive kick.
	float impulse = MinF(overlap, 0.08f)*strength/effective;
	float maxMoveSq = 0.0f;
	for(int i = 0; i < 4; ++i)
		maxMoveSq = MaxF(maxMoveSq, mobility[i].MagnitudeSqr()*invMass[node[i]]*invMass[node[i]]);
	if(maxMoveSq*impulse*impulse > 0.025f*0.025f)
		impulse = 0.025f/sqrtf(maxMoveSq);
	for(int i = 0; i < 4; ++i)
		s->pos[node[i]] += mobility[i]*(impulse*invMass[node[i]]);
}

static inline void
SolveSelfCollision(State *s, CVector *velocity = 0, const CVector *incoming = 0)
{
	// Ten selected pairs rather than all 171 joint pairs. Body volume is
	// represented by two trunk capsules; every lower leg sees both, and
	// upper legs cannot pass through the chest/head or through each other.
	// These constraints act only during overlap, never pull a limb toward
	// a rest pose and share free-space corrections by inverse mass.
	s->lastMaxSelfOverlap = 0.0f;
	const float strength = MinF(1.0f, s->age/0.15f)*0.8f;
	for(int side = 0; side < 2; ++side){
		const int hip = side == 0 ? RD_LTHIGH : RD_RTHIGH;
		const int knee = side == 0 ? RD_LCALF : RD_RCALF;
		const int foot = side == 0 ? RD_LFOOT : RD_RFOOT;
		SeparateCapsules(s, hip, knee, RD_SPINE1, RD_HEAD, 0.21f,
			0.0f, 0.0f, s->pelvisFrame.forward, strength, velocity, incoming);
		SeparateCapsules(s, knee, foot, RD_PELVIS, RD_SPINE1, 0.20f,
			0.0f, 0.0f, s->pelvisFrame.forward, strength, velocity, incoming);
		SeparateCapsules(s, knee, foot, RD_SPINE1, RD_HEAD, 0.21f,
			0.0f, 0.0f, s->pelvisFrame.forward, strength, velocity, incoming);
	}
	SeparateCapsules(s, RD_LTHIGH, RD_LCALF, RD_RTHIGH, RD_RCALF, 0.15f,
		0.25f, 0.25f, -s->pelvisFrame.right, strength, velocity, incoming);
	SeparateCapsules(s, RD_LCALF, RD_LFOOT, RD_RCALF, RD_RFOOT, 0.13f,
		0.0f, 0.0f, -s->pelvisFrame.right, strength, velocity, incoming);
	SeparateCapsules(s, RD_LCALF, RD_LFOOT, RD_RTHIGH, RD_RCALF, 0.14f,
		0.0f, 0.25f, -s->pelvisFrame.right, strength, velocity, incoming);
	SeparateCapsules(s, RD_RCALF, RD_RFOOT, RD_LTHIGH, RD_LCALF, 0.14f,
		0.0f, 0.25f, s->pelvisFrame.right, strength, velocity, incoming);
}

static inline void
ProjectGround(State *s)
{
	for(int i = 0; i < SIM_BONES; i++)
		if(s->pos[i].z < s->groundZ + radius[i])
			s->pos[i].z = s->groundZ + radius[i];
}

static inline void
RecordContact(State *s, int node, const CVector &normal, const CVector &surfaceVelocity, bool staticWorld = false)
{
	s->contact[node].active = true;
	s->contact[node].staticWorld = staticWorld;
	s->contact[node].normal = normal;
	s->contact[node].surfaceVelocity = surfaceVelocity;
}

static inline CVector
Respond(const CVector &velocity, const CVector &incoming,
	const CVector &normal, const CVector &surfaceVelocity, float friction,
	bool boundTangent = false, float timeStep = STEP)
{
	const CVector relative = velocity - surfaceVelocity;
	const float normalSpeed = DotProduct(relative, normal);
	const float incomingNormal = DotProduct(incoming - surfaceVelocity, normal);
	CVector tangent = relative - normal*normalSpeed;
	float tangentSpeed = tangent.Magnitude();
	if(boundTangent && tangentSpeed > 1e-5f){
		// Sphere edges and constrained overlap correction can manufacture
		// tangential speed as well as an outward normal kick. A passive car
		// contact must not increase the point's incoming relative slip energy.
		// Keep its solved direction and existing motion, then apply friction.
		const CVector initialTangent = incoming - surfaceVelocity - normal*incomingNormal;
		const float retained = MinF(tangentSpeed, initialTangent.Magnitude());
		tangent *= retained/tangentSpeed;
		tangentSpeed = retained;
	}
	// Coulomb friction tied to the normal impact and gravity, not a fixed
	// percentage of velocity every frame. No restitution prevents chatter.
	const float impact = MaxF(0.0f, -incomingNormal);
	const float budget = friction*MaxF(impact, FALL_GRAVITY*timeStep*MaxF(normal.z, 0.0f));
	if(tangentSpeed > 1e-5f)
		tangent *= MaxF(0.0f, 1.0f - budget/tangentSpeed);
	// Position correction can resolve a deep overlap in a single step; that
	// distance divided by STEP is not an impact velocity. Keep pre-existing
	// separation, but never create a new outward kick from depenetration.
	const float retainedNormal = MinF(MaxF(0.0f, normalSpeed), MaxF(0.0f, incomingNormal));
	return surfaceVelocity + tangent + normal*retainedNormal;
}

typedef void (*ContactProjector)(State *state, void *context);

static inline void
Step(State *s, ContactProjector projectContacts = 0, void *context = 0, float timeStep = STEP)
{
	if(s->asleep)
		return;
	// prev always encodes velocity over canonical STEP outside this function.
	// Lower priority bodies may update at 20-60 Hz without losing real time.
	timeStep = MaxF(STEP, MinF(3.0f*STEP, timeStep));
	const float damping = timeStep == STEP ? DAMPING : powf(DAMPING, timeStep/STEP);
	CVector incoming[SIM_BONES], velocity[SIM_BONES];
	for(int i = 0; i < SIM_BONES; i++){
		incoming[i] = (s->pos[i] - s->prev[i])*(damping/STEP);
		incoming[i].z -= FALL_GRAVITY*timeStep;
		s->prev[i] = s->pos[i];
		s->pos[i] += incoming[i]*timeStep;
		s->contact[i].active = false;
	}
	s->age = MinF(1.0f, s->age + timeStep);
	// Build CCD planes from free flight before joints alter the trajectory.
	// This also lets all four joint passes propagate a fast bumper contact.
	if(projectContacts)
		projectContacts(s, context);
	for(int iteration = 0; iteration < (timeStep > STEP*1.01f ? 6 : 4); iteration++){
		UpdateFrames(s);
		SolveBends(s);
		SolveHips(s);
		SolveTorso(s);
		for(int i = 0; i < NUM_STICKS; i++){
			float length = s->stickLen[i];
			if(sticks[i].minPercent)
				length *= sticks[i].minPercent*0.01f;
			SolveBone(s, i, length, sticks[i].minPercent != 0);
		}
		SolveSelfCollision(s);
		ProjectGround(s);
		if(projectContacts)
			projectContacts(s, context);
		// The external projection may push a point down; nothing after this
		// final projection may move positions below the floor again.
		ProjectGround(s);
	}
	float maxTravelSq = 0.0f;
	for(int i = 0; i < SIM_BONES; i++){
		const CVector travel = s->pos[i] - s->prev[i];
		maxTravelSq = MaxF(maxTravelSq, travel.MagnitudeSqr());
		velocity[i] = travel*(1.0f/timeStep);
	}
	// Damp only length-changing relative speed: shared translation and
	// rigid tumble retain their momentum instead of being dragged away.
	for(int i = 0; i < SKELETON_STICKS; i++){
		const int a = sticks[i].a, b = sticks[i].b;
		const CVector delta = s->pos[b] - s->pos[a];
		const float lengthSq = delta.MagnitudeSqr();
		if(lengthSq < 1e-10f)
			continue;
		const CVector difference = delta*(DotProduct(velocity[b] - velocity[a], delta)*
			(0.04f/lengthSq));
		velocity[a] += difference*s->stickShareA[i];
		velocity[b] -= difference*(1.0f - s->stickShareA[i]);
	}
	// Re-use the ten pair checks for a passive normal response and measure
	// overlap at the final projected pose, not halfway through a solve pass.
	SolveSelfCollision(s, velocity, incoming);
	int support = 0;
	bool movingSurface = false, externalSupport = false;
	float maxSpeedSq = 0.0f;
	for(int i = 0; i < SIM_BONES; i++){
		const bool ground = s->pos[i].z <= s->groundZ + radius[i] + 0.001f;
		const Contact &c = s->contact[i];
		if(c.active){
			const float vehicleFriction = c.staticWorld ? GROUND_SLIDING_FRICTION :
				0.005f * VrRagdollSettings::Get().gripPercent;
			velocity[i] = Respond(velocity[i], incoming[i], c.normal, c.surfaceVelocity, vehicleFriction, true, timeStep);
			movingSurface = movingSurface || c.surfaceVelocity.MagnitudeSqr() > 0.01f;
			externalSupport = externalSupport || (!c.staticWorld && !ground && c.normal.z > 0.35f);
		}
		if(ground){
			// Keep firm resting contact, but use kinetic friction once a point
			// is moving. The old resting coefficient erased head-shot motion
			// in a single tick even after a real impulse had started sliding.
			const float slideSq = velocity[i].x*velocity[i].x + velocity[i].y*velocity[i].y;
			const float friction = slideSq > 0.1f*0.1f ? GROUND_SLIDING_FRICTION : GROUND_FRICTION;
			velocity[i] = Respond(velocity[i], incoming[i],
				CVector(0.0f, 0.0f, 1.0f), CVector(0.0f, 0.0f, 0.0f), friction, false, timeStep);
		}
		if(ground || (c.active && c.normal.z > 0.35f))
			support++;
		maxSpeedSq = MaxF(maxSpeedSq, velocity[i].MagnitudeSqr());
		s->prev[i] = s->pos[i] - velocity[i]*STEP;
	}
	UpdateFrames(s);
	s->lastSupportCount = support;
	s->lastMaxSpeedSq = maxSpeedSq;
	s->lastMaxTravelSq = maxTravelSq;
	s->lastMaxLengthError = 0.0f;
	for(int i = 0; i < SKELETON_STICKS; i++){
		const float length = (s->pos[sticks[i].b] - s->pos[sticks[i].a]).Magnitude();
		s->lastMaxLengthError = MaxF(s->lastMaxLengthError,
			fabsf(length - s->stickLen[i])/MaxF(0.05f, s->stickLen[i]));
	}
	// Check both solved displacement and retained velocity. Checking before
	// constraints incorrectly slept bodies still being moved by joints.
	// Keep externally supported bodies awake so they respond when the
	// supporting object moves away, including static world objects.
	if(support >= 3 && !movingSurface && !externalSupport && s->lastMaxLengthError < 0.06f &&
		s->lastMaxSelfOverlap < 0.015f &&
		maxSpeedSq < SLEEP_SPEED*SLEEP_SPEED &&
		maxTravelSq < SLEEP_SPEED*timeStep*SLEEP_SPEED*timeStep){
		s->sleepTime += timeStep;
		if(s->sleepTime >= SLEEP_AFTER){
			s->asleep = true;
			for(int i = 0; i < SIM_BONES; i++)
				s->prev[i] = s->pos[i];
		}
	}else
		s->sleepTime = 0.0f;
}

}
#endif
