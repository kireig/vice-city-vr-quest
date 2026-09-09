#include "ragdoll-test-vector.h"
#include "VrRagdollMotion.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
static void Check(bool b,const char *why){if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main()
{
    const float w[]={1.0f/10,1.0f/2,1};
    CVector previous[]={{0,0,0},{1,0,0},{2,0,0}};
    CVector current[3],velocity[3];
    for(int i=0;i<3;i++)current[i]=previous[i]+CVector(.1f,.04f,0);
    Check(VrRagdollMotion::RelativeVelocity(current,previous,w,3,.02f,velocity),"sample valid");
    for(int i=0;i<3;i++)Check(velocity[i].Magnitude()<.0001f,"translation cannot be seeded twice");
    current[2].z+=.08f;
    Check(VrRagdollMotion::RelativeVelocity(current,previous,w,3,.02f,velocity),"moving limb sample");
    CVector momentum(0,0,0);
    for(int i=0;i<3;i++)momentum+=velocity[i]*(1.0f/w[i]);
    Check(momentum.Magnitude()<.0001f,"animated limb transfer adds no net body momentum");
    Check(velocity[2].z>3 && velocity[0].z<0,"limb swing survives while heavy body compensates");
    current[2].z+=100;
    Check(VrRagdollMotion::RelativeVelocity(current,previous,w,3,.02f,velocity),"large animation delta");
    momentum=CVector(0,0,0);
    for(int i=0;i<3;i++){Check(velocity[i].Magnitude()<=4.0001f,"teleport delta bounded");momentum+=velocity[i]*(1.0f/w[i]);}
    Check(momentum.Magnitude()<.0001f,"uniform cap preserves momentum balance");
    Check(!VrRagdollMotion::RelativeVelocity(current,previous,w,3,0,velocity),"paused frame ignored");
    Check(!VrRagdollMotion::RelativeVelocity(current,previous,w,3,.2f,velocity),"stale animation ignored");
    std::puts("PASS: animated limb motion, mass balance, translation removal, stale/teleport limits");
}
