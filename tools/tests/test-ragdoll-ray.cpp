#include "ragdoll-test-vector.h"
#include "VrRagdollRay.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

static int checks;
static void Check(bool value) { ++checks; if(!value){ std::fprintf(stderr,"Ray check %d failed\n",checks); std::exit(1); } }
int main()
{
    const CVector a(0,0,-0.5f), b(0,0,0.5f);
    CVector normal;
    float t;
    Check(VrRagdollRay::Capsule(CVector(-2,0,0),CVector(2,0,0),a,b,0.2f,t,normal));
    Check(std::fabs(t-0.45f)<0.00001f && normal.x<-0.99f);
    // A wall before the near surface must stop the shot; a wall between
    // the near surface and bone axis must still allow the body hit.
    Check(!VrRagdollRay::Capsule(CVector(-2,0,0),CVector(-0.21f,0,0),a,b,0.2f,t,normal));
    Check(VrRagdollRay::Capsule(CVector(-2,0,0),CVector(-0.1f,0,0),a,b,0.2f,t,normal));
    Check(VrRagdollRay::Capsule(CVector(0,0,2),CVector(0,0,-2),a,b,0.2f,t,normal));
    Check(std::fabs(t-0.325f)<0.00001f && normal.z>0.99f);
    Check(VrRagdollRay::Capsule(CVector(0,0,0),CVector(1,0,0),a,b,0.2f,t,normal) && t==0);
    Check(!VrRagdollRay::Capsule(CVector(0,0,0),CVector(0,0,0),a,b,0.2f,t,normal));
    Check(VrRagdollRay::Capsule(CVector(-1,0,0),CVector(1,0,0),CVector(0,0,0),CVector(0,0,0),0.2f,t,normal));
    Check(std::fabs(t-0.4f)<0.00001f);
    for(int i=0;i<360;i++){
        const float angle=i*0.01745329252f;
        const CVector direction(std::cos(angle),std::sin(angle),0);
        Check(VrRagdollRay::Capsule(direction*2,direction*-2,a,b,0.2f,t,normal));
        Check(std::fabs(t-0.45f)<0.0001f && DotProduct(direction,normal)>0.999f);
    }
    float nearT,farT;
    Check(VrRagdollRay::Capsule(CVector(-3,0,0),CVector(3,0,0),a,b,0.2f,nearT,normal));
    Check(VrRagdollRay::Capsule(CVector(-3,0,0),CVector(3,0,0),a+CVector(1,0,0),b+CVector(1,0,0),0.2f,farT,normal));
    Check(nearT<farT);
    std::printf("Finite corpse capsule ray: %d checks passed\n",checks);
}
