#pragma once
#include <cmath>

namespace VrRagdollExplosion
{
// Native blast falloff and velocity units, applied at the visible joints.
// Weight changes the accepted impulse response; an explosion never reseeds
// the whole body or discards momentum from an earlier hit.
inline int Apply(const CVector *pos, CVector *prev, int count,
    const CVector &centre, const CVector &up, float radius, float power,
    float nativeMass, float massScale, float step)
{
    if(count < 1 || count > 19 || !(radius > 0.0f) || !std::isfinite(radius) ||
       !(power > 0.0f) || !std::isfinite(power) ||
       !(nativeMass > 0.0f) || !std::isfinite(nativeMass) ||
       !(massScale > 0.0f) || !std::isfinite(massScale) || !(step > 0.0f)) return 0;
    int changed = 0;
    for(int i = 0; i < count; ++i){
        const CVector offset = pos[i]-centre;
        const float distanceSq = offset.MagnitudeSqr();
        if(!std::isfinite(distanceSq) || distanceSq >= radius*radius) continue;
        const float distance = std::sqrt(distanceSq);
        const float falloff = std::fmin(1.0f,(radius-distance)*2.0f/radius);
        CVector delta = offset*(power*(50.0f/1400.0f)*falloff/std::fmax(distance,0.01f));
        const float vertical = DotProduct(delta,up);
        if(vertical < 0.0f) delta -= up*vertical;
        delta += up*(100.0f/nativeMass*falloff);
        const float speed = delta.Magnitude();
        if(!(speed > 0.0f) || !std::isfinite(speed)) continue;
        delta *= std::fmin(1.0f,16.0f/speed)/massScale;
        prev[i] -= delta*step;
        ++changed;
    }
    return changed;
}
}
