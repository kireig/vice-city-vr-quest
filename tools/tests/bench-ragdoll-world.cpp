#include "ragdoll-test-vector.h"
#define VR_RAGDOLL_VEHICLE_NO_ENGINE
#define VR_RAGDOLL_WORLD_NO_ENGINE
#include "VrRagdollWorld.h"
#include <chrono>
#include <cstdio>
namespace P = VrRagdollPhysics;
namespace W = VrRagdollWorld;
static P::State Body()
{
    P::State s = {};
    const CVector points[P::SIM_BONES] = {
        {0,0,.94f},{0,0,1.10f},{0,0,1.31f},{0,0,1.53f},{0,0,1.69f},
        {-.12f,0,1.43f},{-.26f,0,1.45f},{-.43f,-.035f,1.20f},{-.53f,.03f,.98f},
        {.12f,0,1.43f},{.26f,0,1.45f},{.43f,-.035f,1.20f},{.53f,.03f,.98f},
        {-.13f,0,.89f},{-.14f,.035f,.48f},{-.14f,-.04f,.1f},
        {.13f,0,.89f},{.14f,.035f,.48f},{.14f,-.04f,.1f}
    };
    for(int i=0;i<P::SIM_BONES;i++) s.pos[i]=points[i];
    P::Initialize(&s,{1,0,0},{0,0,1}); s.groundZ=0;
    P::SeedMotion(&s,{0,6,0},{1,0,0});
    return s;
}
int main()
{
    volatile float checksum=0;
    for(int triangleCount : {0,4,96}){
        W::Cache cache;
        W::BeginGather(cache,W::Around({-5,-5,-5},{5,5,5},0));
        for(int i=0;i<triangleCount/2;i++){
            // Ordinary floor + wall, then deliberately overlapping COL faces.
            if(i%2){ W::AddTriangle(cache,{-5,.5f,0},{5,.5f,0},{5,.5f,5});
                W::AddTriangle(cache,{-5,.5f,0},{5,.5f,5},{-5,.5f,5}); }
            else { W::AddTriangle(cache,{-5,-5,0},{5,-5,0},{5,5,0});
                W::AddTriangle(cache,{-5,-5,0},{5,5,0},{-5,5,0}); }
        }
        const auto start=std::chrono::steady_clock::now();
        unsigned long long bounds=0,narrow=0;
        for(int fall=0;fall<24;fall++){
            P::State state=Body();
            for(int frame=0;frame<90;frame++){
                P::Wake(&state);
                W::StepContacts contact(cache,&state);
                P::Step(&state,triangleCount?W::Project:nullptr,&contact);
                if(triangleCount) W::ResolveVelocity(&state,contact);
                bounds+=contact.stats.boundsTests; narrow+=contact.stats.narrowTests;
            }
            checksum+=state.pos[P::RD_HEAD].x;
        }
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        std::printf("HOST ONLY triangles=%d per-step-ms=%.6f worst18-step-ms=%.6f bounds/step=%.1f narrow/step=%.1f cache-bytes=%zu\n",
            triangleCount,ms/2160,ms/120,double(bounds)/2160,double(narrow)/2160,sizeof(W::Cache));
    }
    return std::isfinite(float(checksum))?0:1;
}
