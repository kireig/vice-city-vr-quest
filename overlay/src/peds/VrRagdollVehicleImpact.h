#pragma once
#include "VrRagdollPhysics.h"
#include "VrRagdollImpulse.h"

namespace VrRagdollVehicleImpact
{
// A single measured initial collision is applied to the body part it touched.
// This must run after seeding PRE-response velocity and before extra car braking;
// later articulated mesh contacts own the rest of the collision. No impulse is
// invented when the native contact is missing or far from the visible skeleton.
inline float Apply(VrRagdollPhysics::State *state, const CVector &point,
    const CVector &surfaceVelocity, float impulseNs, float massScale = 1.0f)
{
    namespace P = VrRagdollPhysics;
    if(!state || !(impulseNs > 0.0f) || !std::isfinite(impulseNs) ||
        !(massScale > 0.0f) || !std::isfinite(massScale) ||
        !std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) ||
        !std::isfinite(surfaceVelocity.x) || !std::isfinite(surfaceVelocity.y) || !std::isfinite(surfaceVelocity.z))
        return 0.0f;
    int best = -1;
    float bestDistance = 1.0e10f, along = 0.0f;
    for(int i = 0; i < P::SKELETON_STICKS; ++i){
        const P::Stick &bone = P::sticks[i];
        const CVector edge = state->pos[bone.b]-state->pos[bone.a];
        const float lengthSq = DotProduct(edge,edge);
        const float t = lengthSq > 1.0e-10f ?
            P::Clamp(DotProduct(point-state->pos[bone.a],edge)/lengthSq,0.0f,1.0f) : 0.0f;
        const CVector nearest = state->pos[bone.a]+edge*t;
        const float distance = (point-nearest).Magnitude();
        const float radius = P::MaxF(P::radius[bone.a],P::radius[bone.b]);
        const float separation = distance-radius;
        if(separation < bestDistance){bestDistance=separation;best=i;along=t;}
    }
    // The native pedestrian proxy is coarse. Permit its small skin discrepancy,
    // but never apply a remote impulse from a stale entity/root collision point.
    if(best < 0 || bestDistance > 0.20f) return 0.0f;
    const unsigned char parts[5][5] = {
        {P::RD_PELVIS,P::RD_SPINE,P::RD_SPINE1,P::RD_NECK,P::RD_HEAD},
        {P::RD_LCLAV,P::RD_LUARM,P::RD_LFARM,P::RD_LHAND,0},
        {P::RD_RCLAV,P::RD_RUARM,P::RD_RFARM,P::RD_RHAND,0},
        {P::RD_LTHIGH,P::RD_LCALF,P::RD_LFOOT,0,0},
        {P::RD_RTHIGH,P::RD_RCALF,P::RD_RFOOT,0,0}
    };
    const int part = best < 4 ? 0 : best < 8 ? 1 : best < 12 ? 2 : best < 15 ? 3 : 4;
    const int count = part == 0 ? 5 : part <= 2 ? 4 : 3;
    const P::Stick &bone = P::sticks[best];
    const CVector vA = (state->pos[bone.a]-state->prev[bone.a])*(1.0f/P::STEP);
    const CVector vB = (state->pos[bone.b]-state->prev[bone.b])*(1.0f/P::STEP);
    const CVector relative = surfaceVelocity-(vA*(1.0f-along)+vB*along);
    const float closingSpeed = relative.Magnitude();
    if(!(closingSpeed > 0.05f)) return 0.0f;
    float mass = 0.0f;
    for(int i = 0; i < count; ++i) mass += massScale/P::invMass[parts[part][i]];
    const float requested = P::MinF(impulseNs,mass*closingSpeed);
    // Leg/arm contacts keep their COM torque, while the uniform part scale
    // prevents the light foot/hand from gaining tens of m/s on the first frame.
    const float limit = P::MinF(12.0f,closingSpeed*massScale);
    return requested*VrRagdollImpulse::ApplyPartImpulse(state->pos,state->prev,
        P::invMass,parts[part],count,point,relative,requested,P::STEP,limit,massScale);
}
}
