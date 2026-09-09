#pragma once
#include <cmath>

namespace VrRagdollRay
{
inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// First surface intersection, not the closest approach to the bone axis.
// The finite ray never reaches through an earlier world occluder.
inline bool Capsule(const CVector &from, const CVector &to,
    const CVector &a, const CVector &b, float radius, float &fraction, CVector &normal)
{
    const CVector ray = to-from, bone = b-a, rel = from-a;
    const float raySq = DotProduct(ray,ray), boneSq = DotProduct(bone,bone);
    if(raySq < 1.0e-12f || radius <= 0.0f) return false;
    const float along = boneSq > 1.0e-12f ? Clamp01(DotProduct(rel,bone)/boneSq) : 0.0f;
    const CVector initial = rel-bone*along;
    float first = 2.0f;
    if(DotProduct(initial,initial) <= radius*radius) first = 0.0f;
    if(boneSq > 1.0e-12f){
        const float br = DotProduct(bone,ray), bo = DotProduct(bone,rel);
        const float aa = boneSq*raySq-br*br;
        const float bb = boneSq*DotProduct(rel,ray)-bo*br;
        const float cc = boneSq*(DotProduct(rel,rel)-radius*radius)-bo*bo;
        const float disc = bb*bb-aa*cc;
        if(aa > 1.0e-12f && disc >= 0.0f){
            const float t = (-bb-std::sqrt(disc))/aa;
            const float y = bo+t*br;
            if(t >= 0.0f && t <= 1.0f && y >= 0.0f && y <= boneSq && t < first) first = t;
        }
    }
    for(int i = 0; i < 2; ++i){
        const CVector offset = from-(i == 0 ? a : b);
        const float bb = DotProduct(offset,ray);
        const float cc = DotProduct(offset,offset)-radius*radius;
        const float disc = bb*bb-raySq*cc;
        if(disc < 0.0f) continue;
        const float t = (-bb-std::sqrt(disc))/raySq;
        if(t >= 0.0f && t <= 1.0f && t < first) first = t;
    }
    if(first > 1.0f) return false;
    fraction = first;
    const CVector hit = from+ray*first;
    const float hitAlong = boneSq > 1.0e-12f ? Clamp01(DotProduct(hit-a,bone)/boneSq) : 0.0f;
    normal = hit-(a+bone*hitAlong);
    const float nSq = DotProduct(normal,normal);
    normal = nSq > 1.0e-12f ? normal*(1.0f/std::sqrt(nSq)) : ray*(-1.0f/std::sqrt(raySq));
    return true;
}
}
