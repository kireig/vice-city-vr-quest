#ifndef VR_RAGDOLL_POSE_H
#define VR_RAGDOLL_POSE_H

#include "VrRagdollPhysics.h"

// Skin orientations need two axes. A segment direction alone leaves the
// thigh/calf free to roll independently even when their joint positions form
// a perfectly ordinary bent knee. Derive both from the same knee plane.
namespace VrRagdollPose {

using VrRagdollPhysics::Frame;

struct LegFrames {
	Frame upper, lower;
};

static inline LegFrames
MakeLegFrames(const CVector &upperSegment, const CVector &lowerSegment,
              const CVector &restUpper, const CVector &restBend)
{
	using namespace VrRagdollPhysics;
	const CVector upper = Unit(upperSegment, restUpper);
	const CVector lower = Unit(lowerSegment, upper);
	const CVector preferred = Swing(restUpper, upper, restBend, restBend);
	const CVector reference = Unit(CrossProduct(preferred, upper), Perpendicular(upper));
	CVector plane = CrossProduct(upper, lower);
	const float sine = plane.Magnitude();
	// Around a straight knee its cross product is tiny and reverses with
	// millimetres of solver slack. The anatomical plane remains well defined;
	// blend to it before that singularity can flip the calf or shoe 180 deg.
	plane = Unit(plane, reference);
	// An impact may temporarily force the knee sideways or backwards before
	// its positional limit converges. Do not choose +/- the plane normal:
	// that choice itself flips at 90 degrees. Continuously fade an invalid
	// plane to the anatomical reference instead, with no hemisphere branch.
	float confidence = Clamp(DotProduct(plane,reference)*4.0f,0.0f,1.0f);
	confidence *= confidence*(3.0f-2.0f*confidence);
	const float weight = Clamp((sine-0.03f)*(1.0f/0.17f), 0.0f, 1.0f)*confidence;
	const CVector normal = Unit(reference*(1.0f-weight) +
		plane*weight, reference);
	Frame fallback;
	fallback.up = upper;
	fallback.right = reference;
	fallback.forward = CrossProduct(upper, reference);
	LegFrames result;
	result.upper = MakeFrame(normal, upper, fallback);
	result.lower = MakeFrame(normal, lower, result.upper);
	return result;
}

}
#endif
