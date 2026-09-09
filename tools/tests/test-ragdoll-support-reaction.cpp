#include "ragdoll-test-vector.h"
#define VR_RAGDOLL_VEHICLE_NO_ENGINE
#include "VrRagdollContacts.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace P = VrRagdollPhysics;
namespace R = VrRagdollReaction;
static int checks;
static void Require(bool ok, const char *what)
{
    ++checks;
    if(!ok){ std::fprintf(stderr,"FAIL %s\n",what); std::exit(1); }
}
static void Case(int weight,int steps,const CVector &normal)
{
    const float dt=steps*P::STEP;
    const CVector surface(0,20,0);
    P::State state={};
    VrRagdollSettings::SetWeight(weight);
    float acceptedImpulse[2]={};
    const int node=P::RD_PELVIS;
    for(int falling=0;falling<2;++falling){
        const CVector velocity=surface-CVector(0,0,falling ? 3.f : 0.f);
        for(int i=0;i<P::SIM_BONES;++i){
            state.pos[i]=CVector(float(i)*.1f,0,1);
            state.prev[i]=state.pos[i]-velocity*P::STEP;
        }
        const P::State before=state;
        VrRagdollVehicle::Batch batch;
        R::Frame frame;
        VrRagdollContacts::StepContacts context(&batch,&frame,&state,0,1,dt);
        Require((state.pos[node]-before.pos[node]).MagnitudeSqr()==0 &&
            (state.prev[node]-before.prev[node]).MagnitudeSqr()==0,
            "reaction capture does not move or reseed the solver body");
        const bool accepted=R::Add(frame,1,1650,surface,0,node,
            context.massScale/P::invMass[node],normal,surface,context.incoming[node],1);
        Require(accepted==bool(falling),"only existing downward velocity produces an impact; new gravity is not an impact");
        acceptedImpulse[falling]=accepted ? frame.vehicles[0].rawImpulse : 0;
        if(accepted){
            const float old=frame.vehicles[0].rawImpulse;
            Require(!R::Add(frame,1,1650,surface,0,node,context.massScale/P::invMass[node],
                normal,surface,context.incoming[node],1),"repeated contact passes do not duplicate one particle mass");
            Require(frame.vehicles[0].rawImpulse==old,"duplicate contact leaves physical request unchanged");
            const CVector braking=R::Impulse(frame.vehicles[0],1650,surface)*VrRagdollBrake::Multiplier(200);
            Require(braking.x==0 && braking.y<0 && braking.z==0,"weight-scaled response is horizontal opposing braking");
        }
    }
    std::printf("weight=%d dt=%d/60 normal=(%.3f,%.3f,%.3f) comovingJ=%.6f fallingJ=%.6f\n",
        weight,steps,normal.x,normal.y,normal.z,acceptedImpulse[0],acceptedImpulse[1]);
}
int main()
{
    // Upward/forward normals occur on a COL sphere or sloped bonnet surface.
    for(int weight : {100,200}) for(int steps : {1,2,3})
        for(float forward : {.15f,.6f,.96f})
            Case(weight,steps,CVector(0,forward,std::sqrt(1-forward*forward)));
    VrRagdollSettings::SetWeight(100);
    std::printf("PASS %d production Contacts/Reaction support checks; no current-gravity brake at 60/30/20Hz.\n",checks);
}
