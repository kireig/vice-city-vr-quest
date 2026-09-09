#pragma once

// Skin matrices are in world space while the game entity keeps its animation
// root. Cache a conservative sphere around the moving skeleton for visibility
// and sector membership. This does not add physics contacts or move that root.
namespace VrRagdollBounds {
struct Bounds {
    CVector centre;
    float radius;
};

inline Bounds Calculate(const CVector *points, const float *radii, int count)
{
    const float skinMargin = 0.18f; // fingers, toes and clothing beyond joint spheres
    CVector lo = points[0], hi = points[0];
    for(int i = 0; i < count; i++){
        const float r = radii[i] + skinMargin;
        lo.x = Min(lo.x, points[i].x-r); hi.x = Max(hi.x, points[i].x+r);
        lo.y = Min(lo.y, points[i].y-r); hi.y = Max(hi.y, points[i].y+r);
        lo.z = Min(lo.z, points[i].z-r); hi.z = Max(hi.z, points[i].z+r);
    }
    Bounds result;
    result.centre = (lo+hi)*0.5f;
    result.radius = (hi-lo).Magnitude()*0.5f;
    return result;
}
}
