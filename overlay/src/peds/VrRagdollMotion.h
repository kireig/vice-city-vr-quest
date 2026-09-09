#pragma once
#include <cmath>

namespace VrRagdollMotion
{
// Carry the animated limbs' relative motion into the takeover. Translation
// comes from the game's actual velocity, not the render sampling interval.
// Remove the mass-weighted common velocity before applying a uniform cap.
inline bool RelativeVelocity(const CVector *current, const CVector *previous,
    const float *inverseMass, int count, float dt, CVector *velocity, float limit = 4.0f)
{
    if(count <= 0 || dt < 0.001f || dt > 0.1f) return false;
    CVector mean(0,0,0);
    float mass=0;
    for(int i=0;i<count;i++){
        velocity[i]=(current[i]-previous[i])*(1.0f/dt);
        const float m=1.0f/inverseMass[i];
        mean+=velocity[i]*m;
        mass+=m;
    }
    mean*=1.0f/mass;
    float largest=0;
    for(int i=0;i<count;i++){
        velocity[i]-=mean;
        const float speedSq=DotProduct(velocity[i],velocity[i]);
        if(speedSq>largest) largest=speedSq;
    }
    const float scale=largest>limit*limit ? limit/std::sqrt(largest) : 1.0f;
    for(int i=0;i<count;i++) velocity[i]*=scale;
    return true;
}
}
