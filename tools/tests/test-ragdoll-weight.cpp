#define main ExistingHoodCases
#include "test-ragdoll-hood.cpp"
#undef main
#include "VrRagdollVehicleImpact.h"
#include "VrRagdollSupport.h"
#include <initializer_list>
#include <limits>
#include "ragdoll-weight-production.inc"

static CVector Velocity(const P::State&s,int i){return (s.pos[i]-s.prev[i])/P::STEP;}
static void FixedImpulseInertia(){
 for(int part=0;part<6;++part)for(float impulse:{1.0f,1000.0f}){
  P::State nominal=Standing();const CVector point=nominal.pos[part==0?P::RD_PELVIS:part==1?P::RD_HEAD:part==2?P::RD_LHAND:part==3?P::RD_RHAND:part==4?P::RD_LFOOT:P::RD_RFOOT]+CVector(.07f,0,0);
  VrRagdoll::SetWeightPercent(100);ProductionBullet(&nominal,part,point,{0,1,0},impulse);
  float peak=0;for(int i=0;i<P::SIM_BONES;++i)peak=P::MaxF(peak,Velocity(nominal,i).Magnitude());
  Check(peak>0,"actual bullet call applies a nonzero part impulse");
  for(int weight:{50,100,150,200,250,300}){
   VrRagdoll::SetWeightPercent(weight);P::State weighted=Standing();ProductionBullet(&weighted,part,point,{0,1,0},impulse);
   const float scale=weight*.01f;
   CVector momentum,nominalMomentum,torque,nominalTorque;const CVector origin=nominal.pos[P::RD_PELVIS];
   for(int i=0;i<P::SIM_BONES;++i){
    Check((Velocity(weighted,i)*scale-Velocity(nominal,i)).Magnitude()<.0003f,"mass scales both linear and angular velocity inversely even at the safety limit");
    const CVector j=Velocity(weighted,i)*(scale/P::invMass[i]),baseJ=Velocity(nominal,i)/P::invMass[i];
    momentum+=j;nominalMomentum+=baseJ;torque+=CrossProduct(weighted.pos[i]-origin,j);nominalTorque+=CrossProduct(nominal.pos[i]-origin,baseJ);
   }
   Check((momentum-nominalMomentum).Magnitude()<.003f,"same accepted bullet impulse preserves physical linear momentum at every body weight");
   Check((torque-nominalTorque).Magnitude()<.003f,"same contact impulse preserves physical angular momentum at every body weight");
  }
 }
}
static void InitialVehicleImpulse(){
 for(float height:{.45f,1.6f})for(float impulse:{1.0f,2100.0f}){
  const CVector point(height<.9f?-.14f:0,2.25f,height);
  P::State nominal=Standing();const float accepted=VrRagdollVehicleImpact::Apply(&nominal,point,{0,30,0},impulse,1);
  Check(accepted>0,"actual local vehicle impulse reaches tested body part");
  for(float scale:{.5f,1.f,1.5f,2.f,3.f}){
   P::State weighted=Standing();const float applied=VrRagdollVehicleImpact::Apply(&weighted,point,{0,30,0},impulse,scale);
   Check(std::fabs(applied-accepted)<.003f,"mass does not multiply the accepted measured car impulse");
   for(int i=0;i<P::SIM_BONES;++i)
    Check((Velocity(weighted,i)*scale-Velocity(nominal,i)).Magnitude()<.0005f,"real bumper and torso impact paths respond less sharply with increased inertia");
  }
 }
}
static void ContactMassAndBrakeIndependence(){
 float previous=0,referenceRaw=0,referenceMass=0;
 for(int weight:{50,100,150,200,250,300}){
  VrRagdoll::SetWeightPercent(weight);P::State body=Standing();V::Batch car=Car(30);car.vehicles[0].handle=1;car.vehicles[0].mass=1000;
  V::Prepare(car,0,P::STEP);VrRagdollReaction::Frame reaction;VrRagdollReaction::BeginFrame(reaction);
  C::StepContacts context(&car,&reaction,&body,0,1);P::Step(&body,C::Project,&context);
  Check(reaction.count==1&&reaction.pointImpacts>0,"real CCD contact records participating body points");
  const auto &request=reaction.vehicles[0];const float scale=weight*.01f;
  if(weight==50){referenceRaw=request.rawImpulse/scale;referenceMass=request.effectiveMass/scale;}
  Check(std::fabs(request.rawImpulse/scale-referenceRaw)<.003f&&std::fabs(request.effectiveMass/scale-referenceMass)<.003f,
   "actual contact path scales point mass rather than the capped participating share");
  const float physical=VrRagdollReaction::Impulse(request,1000,{0,30,0}).Magnitude();
  Check(physical>previous,"heavier contacting bodies transfer more momentum to the finite-mass car");previous=physical;
  for(int brake:{0,100,200}){
   VrRagdollSettings::SetBrake(brake);
   const float adjusted=physical*VrRagdollBrake::Multiplier(float(brake));
   Check(std::fabs(adjusted-physical*brake*.01f)<.001f&&VrRagdoll::GetWeightPercent()==weight,"brake gain is independent of selected body mass");
  }
 }
}
static void GravityAndExistingVelocity(){
 P::State reference=Standing();P::SeedMotion(&reference,{.2f,.1f,0},{.1f,0,.2f});reference.groundZ=-100;
 for(int weight:{50,150,200,300}){
  P::State nominal=reference,weighted=reference;
  for(int frame=0;frame<30;++frame){
   VrRagdoll::SetWeightPercent(100);P::Step(&nominal);
   VrRagdoll::SetWeightPercent(weight);P::Step(&weighted);
   for(int i=0;i<P::SIM_BONES;++i)Check((nominal.pos[i]-weighted.pos[i]).Magnitude()==0&&
    (nominal.prev[i]-weighted.prev[i]).Magnitude()==0,"changing mass leaves gravity, elapsed time, damping and pre-existing motion exactly unchanged");
  }
 }
 VrRagdoll::SetWeightPercent(-100);Check(VrRagdoll::GetWeightPercent()==50,"public setter clamps low mass");
 VrRagdoll::SetWeightPercent(10000);Check(VrRagdoll::GetWeightPercent()==300,"public setter clamps high mass");
 VrRagdoll::SetWeightPercent(100);VrRagdollSettings::SetBrake(200);
}
static void IntegratedRangeSafety(){
 for(int weight:{50,100,300})for(float height:{.45f,1.6f}){
  P::State body=Standing();VrRagdoll::SetWeightPercent(weight);
  Check(VrRagdollVehicleImpact::Apply(&body,{height<.9f?-.14f:0,2.25f,height},{0,30,0},2100,VrRagdollSettings::WeightScale())>0,
   "range integration begins with a real high-speed bumper or torso impulse");
  P::Wake(&body);
  for(int frame=0;frame<900&&!body.asleep;++frame){
   P::Step(&body);
   for(int i=0;i<P::SIM_BONES;++i)Check(std::isfinite(body.pos[i].MagnitudeSqr())&&std::isfinite(Velocity(body,i).MagnitudeSqr())&&
    body.pos[i].z>=P::radius[i]-.0001f,"light/default/heavy bodies keep finite motion and ground separation through a complete fall");
   Check(body.lastMaxLengthError<.65f,"full solver keeps bones connected across the mass range after high-speed impact");
  }
  Check(body.asleep,"every mass-range impact settles using the unchanged sleep and gravity rules");
 }
 VrRagdoll::SetWeightPercent(100);
}
int main(){FixedImpulseInertia();InitialVehicleImpulse();ContactMassAndBrakeIndependence();GravityAndExistingVelocity();IntegratedRangeSafety();
 std::printf("PASS: body weight %d checks; actual bullet call, local vehicle impulse, contact mass, finite car reaction, independent gain, unchanged gravity/time.\n",checks);}
