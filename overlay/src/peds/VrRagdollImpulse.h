#pragma once

#include <cmath>

// CVector and its dot/cross helpers are supplied by the game or host fixture.
// Inputs use metres, seconds and kilograms. No State, allocation or engine query.
namespace VrRagdollImpulse
{
static const int MAX_PART_POINTS = 6;
static const float MAX_POINT_DELTA_SPEED = 4.0f;

// Apply one impulse at its actual surface contact to a small articulated part.
// Translation carries exactly J; rotation around the mass centre adds no linear
// momentum. A uniform safety scale preserves their ratio when a light part would
// receive an excessive velocity change. Return that scale, or zero for no effect.
// The caller supplies valid, distinct indices into pos/prev/inverseMass arrays.
inline float ApplyPartImpulse(const CVector *pos, CVector *prev,
    const float *inverseMass, const unsigned char *nodes, int count,
    const CVector &hitPoint, const CVector &direction, float impulse, float step,
    float maxPointDeltaSpeed = MAX_POINT_DELTA_SPEED, float massScale = 1.0f)
{
    if(count < 1 || count > MAX_PART_POINTS || impulse <= 0.0f || step <= 0.0f ||
        !(maxPointDeltaSpeed > 0.0f) || !std::isfinite(maxPointDeltaSpeed) ||
        !(massScale > 0.0f) || !std::isfinite(massScale))
        return 0.0f;
    const float directionSq = DotProduct(direction,direction);
    if(directionSq < 1.0e-12f) return 0.0f;
    float mass[MAX_PART_POINTS], totalMass = 0.0f;
    CVector offset[MAX_PART_POINTS], radius[MAX_PART_POINTS];
    const CVector origin = pos[nodes[0]];
    CVector centre(0.0f,0.0f,0.0f);
    for(int i = 0; i < count; ++i){
        if(inverseMass[nodes[i]] <= 0.0f) return 0.0f;
        for(int j = 0; j < i; ++j)
            if(nodes[i] == nodes[j]) return 0.0f;
        mass[i] = massScale/inverseMass[nodes[i]];
        offset[i] = pos[nodes[i]]-origin;
        centre += offset[i]*mass[i];
        totalMass += mass[i];
    }
    centre *= 1.0f/totalMass;
    const CVector linearImpulse = direction*(impulse/std::sqrt(directionSq));
    const CVector torque = CrossProduct((hitPoint-origin)-centre,linearImpulse);

    // A double-precision 3x3 solve is tiny compared with a collision query and
    // keeps nearly collinear limb points stable. Trace regularization suppresses
    // unrepresentable axial spin; it does not create artificial transverse axes.
    double xx = 0, yy = 0, zz = 0, xy = 0, xz = 0, yz = 0;
    for(int i = 0; i < count; ++i){
        radius[i] = offset[i]-centre;
        const double x = radius[i].x, y = radius[i].y, z = radius[i].z;
        const double m = mass[i];
        xx += m*(y*y+z*z); yy += m*(x*x+z*z); zz += m*(x*x+y*y);
        xy -= m*x*y; xz -= m*x*z; yz -= m*y*z;
    }
    const double regularization = (xx+yy+zz)*0.0001+totalMass*0.000001;
    xx += regularization; yy += regularization; zz += regularization;
    const double c00 = yy*zz-yz*yz, c01 = xz*yz-xy*zz;
    const double c02 = xy*yz-xz*yy, c11 = xx*zz-xz*xz;
    const double c12 = xy*xz-xx*yz, c22 = xx*yy-xy*xy;
    const double determinant = xx*c00+xy*c01+xz*c02;
    if(!(determinant > 0.0)) return 0.0f;
    const CVector omega(
        float((c00*torque.x+c01*torque.y+c02*torque.z)/determinant),
        float((c01*torque.x+c11*torque.y+c12*torque.z)/determinant),
        float((c02*torque.x+c12*torque.y+c22*torque.z)/determinant));

    CVector spin[MAX_PART_POINTS], meanSpin(0.0f,0.0f,0.0f);
    for(int i = 0; i < count; ++i){
        spin[i] = CrossProduct(omega,radius[i]);
        meanSpin += spin[i]*mass[i];
    }
    // Remove only floating-point COM drift, including at far map coordinates.
    meanSpin *= 1.0f/totalMass;
    const CVector translation = linearImpulse*(1.0f/totalMass);
    CVector delta[MAX_PART_POINTS];
    float largestSq = 0.0f;
    for(int i = 0; i < count; ++i){
        delta[i] = translation+spin[i]-meanSpin;
        const float speedSq = DotProduct(delta[i],delta[i]);
        if(speedSq > largestSq) largestSq = speedSq;
    }
    // Preserve the nominal accepted impulse budget at every body weight.
    // A fixed velocity ceiling would hide increased mass on hard impacts:
    // both light and heavy limbs would still be kicked to that same ceiling.
    const float deltaLimit = maxPointDeltaSpeed/massScale;
    const float scale = largestSq > deltaLimit*deltaLimit ?
        deltaLimit/std::sqrt(largestSq) : 1.0f;
    for(int i = 0; i < count; ++i)
        prev[nodes[i]] -= delta[i]*(step*scale);
    return scale;
}
}
