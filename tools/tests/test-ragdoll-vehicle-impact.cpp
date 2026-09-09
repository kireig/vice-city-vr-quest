#define main ExistingHoodCases
#include "test-ragdoll-hood.cpp"
#undef main
#include "VrRagdollVehicleImpact.h"
#include <limits>
#include <initializer_list>

static CVector Momentum(const P::State &state){
    CVector result;
    for(int i=0;i<P::SIM_BONES;++i)
        result+=(state.pos[i]-state.prev[i])/(P::STEP*P::invMass[i]);
    return result;
}
static CVector AngularMomentum(const P::State &state,const CVector &centre){
    CVector result;
    for(int i=0;i<P::SIM_BONES;++i)
        result+=CrossProduct(state.pos[i]-centre,(state.pos[i]-state.prev[i])/(P::STEP*P::invMass[i]));
    return result;
}
static void HitAtHeight(float height,float speed){
    P::State state=Standing();
    P::SeedMotion(&state,{0,0,0},{0,0,0});
    const CVector centre=P::CentreOfMass(&state);
    const CVector point(height<.9f?-.14f:0,2.25f,height),direction(0,1,0);
    const float applied=VrRagdollVehicleImpact::Apply(&state,point,direction*speed,70*speed);
    Check(applied>0 && applied<=70*speed,"measured contact applies bounded real momentum");
    const CVector actual=Momentum(state),expected=direction*applied;
    Check((actual-expected).Magnitude()<.002f,"contact impulse preserves whole-body linear momentum");
    Check(std::fabs(actual.z)<.002f,"horizontal contact invents no whole-body upward launch");
    const CVector torque=AngularMomentum(state,centre);
    const CVector expectedTorque=CrossProduct(point-centre,expected);
    Check((torque-expectedTorque).Magnitude()<.008f+expectedTorque.Magnitude()*.005f,"actual surface point preserves COM torque");
    for(int i=0;i<P::SIM_BONES;++i){
        const CVector velocity=(state.pos[i]-state.prev[i])/P::STEP;
        Check(velocity.Magnitude()<=P::MinF(12.0f,speed)+.001f,"contact cannot catapult a light joint beyond closing speed");
    }
    if(height<.9f){
        const float head=(state.pos[P::RD_HEAD]-state.prev[P::RD_HEAD]).Magnitude()/P::STEP;
        const float calf=(state.pos[P::RD_LCALF]-state.prev[P::RD_LCALF]).Magnitude()/P::STEP;
        Check(head<.0001f && calf>0.05f,"initial bumper hit pushes leg while upper body keeps its own velocity");
        Check(torque.x>0,"low hit pitches upper body back toward approaching bonnet");
    }else Check(torque.x<0,"high hit has opposite torque without a scripted bonnet bias");
    std::printf("vehicle contact height%.2f speed%.0f applied%.2fNs torqueX%.3f\n",height,speed,applied,torque.x);
}
int main(){
    for(float speed:{2.0f,6.0f,12.0f,20.0f,30.0f}){
        HitAtHeight(.45f,speed);HitAtHeight(.75f,speed);HitAtHeight(1.6f,speed);
    }
    P::State state=Standing(),original=state;
    Check(VrRagdollVehicleImpact::Apply(nullptr,{},{},10)==0,"missing body does not hit");
    Check(VrRagdollVehicleImpact::Apply(&state,{50,50,50},{0,20,0},700)==0,"far native proxy contact does not hit");
    Check(VrRagdollVehicleImpact::Apply(&state,{-0.14f,2.25f,.45f},{0,20,0},0)==0,"zero measured impulse does not hit");
    Check(VrRagdollVehicleImpact::Apply(&state,{-0.14f,2.25f,.45f},{0,20,0},std::numeric_limits<float>::infinity())==0,"nonfinite measured impulse does not hit");
    for(int i=0;i<P::SIM_BONES;++i)Check((state.prev[i]-original.prev[i]).Magnitude()==0,"rejected contact leaves pose velocity intact");
    P::SeedMotion(&state,{0,10,0},{0,0,0});
    Check(VrRagdollVehicleImpact::Apply(&state,{-0.14f,2.25f,.45f},{0,10,0},700)==0,"co-moving actor receives no repeated initial kick");
    std::printf("Vehicle initial contact: %d checks passed\n",checks);
}
