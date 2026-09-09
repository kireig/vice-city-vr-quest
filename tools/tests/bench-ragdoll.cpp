#include "ragdoll-test-vector.h"
#include "VrRagdollPhysics.h"
#include <chrono>
#include <cstdio>
using namespace VrRagdollPhysics;

static State Body(int variation)
{
    State s = {};
    const CVector points[SIM_BONES] = {
        {0,0,.94f},{0,0,1.10f},{0,0,1.31f},{0,0,1.53f},{0,0,1.69f},
        {-.12f,0,1.43f},{-.26f,0,1.45f},{-.43f,-.035f,1.20f},{-.53f,.03f,.98f},
        {.12f,0,1.43f},{.26f,0,1.45f},{.43f,-.035f,1.20f},{.53f,.03f,.98f},
        {-.13f,0,.89f},{-.14f,.035f,.48f},{-.14f,-.04f,.1f},
        {.13f,0,.89f},{.14f,.035f,.48f},{.14f,-.04f,.1f}
    };
    for(int i=0;i<SIM_BONES;++i) s.pos[i]=points[i]+CVector(0,0,1);
    Initialize(&s,CVector(1,0,0),CVector(0,0,1));
    s.groundZ=0;
    SeedMotion(&s,CVector(1,.3f*variation,0),CVector(.4f*variation,.7f,.3f));
    return s;
}
int main()
{
    volatile float checksum=0;
    double best=1e20;
    for(int repeat=0;repeat<5;++repeat){
        const auto begin=std::chrono::steady_clock::now();
        for(int fall=0;fall<120;++fall){
            State bodies[6];
            for(int b=0;b<6;++b) bodies[b]=Body(b);
            for(int frame=0;frame<120;++frame)
                for(int b=0;b<6;++b){ Wake(&bodies[b]); Step(&bodies[b]); }
            for(int b=0;b<6;++b) checksum+=bodies[b].pos[RD_HEAD].x;
        }
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        if(ms<best) best=ms;
        std::printf("run%d six-awake-solver-step-ms=%.6f\n",repeat,ms/(120*120));
    }
    std::printf("HOST ONLY best-six-body-step-ms=%.6f state-bytes=%zu checksum=%.3f\n",best/(120*120),sizeof(State),float(checksum));
    return std::isfinite(float(checksum))?0:1;
}
