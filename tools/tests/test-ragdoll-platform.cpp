#include "ragdoll-platform-fixture.inc"
namespace V=VrRagdollVehicle;
namespace R=VrRagdoll;
static P::State Prone(){
 P::State s={};
 const CVector points[P::SIM_BONES]={
  {0,0,.94f},{0,0,1.10f},{0,0,1.31f},{0,0,1.53f},{0,0,1.69f},
  {-.12f,0,1.43f},{-.26f,0,1.45f},{-.43f,-.035f,1.20f},{-.53f,.03f,.98f},
  {.12f,0,1.43f},{.26f,0,1.45f},{.43f,-.035f,1.20f},{.53f,.03f,.98f},
  {-.13f,0,.89f},{-.14f,.035f,.48f},{-.14f,-.04f,.1f},
  {.13f,0,.89f},{.14f,.035f,.48f},{.14f,-.04f,.1f}};
 for(int i=0;i<P::SIM_BONES;++i)s.pos[i]=CVector(points[i].x,points[i].z-1,1.7f-points[i].y);
 P::Initialize(&s,{1,0,0},{0,1,0});s.groundZ=0;return s;
}
static V::Batch Deck(){
 V::Batch b;b.count=1;auto&v=b.vehicles[0];v.handle=5;v.mass=1200;
 v.pose.origin={0,0,0};v.pose.right={1,0,0};v.pose.forward={0,1,0};v.pose.up={0,0,1};
 v.linearVelocity=v.surfaceLinearVelocity={0,0,0};v.angularVelocity=v.surfaceAngularVelocity={0,0,0};
 v.boundCenter={0,0,.7f};v.boundRadius=3.5f;
 // Actual COL triangle contact, not a bespoke plane/support callback.
 Check(V::MakeTriangle({-1.6f,-2,1.3f},{1.6f,-2,1.3f},{1.6f,2,1.3f},v.triangles[v.triangleCount++]),"deck first COL face");
 Check(V::MakeTriangle({-1.6f,-2,1.3f},{1.6f,2,1.3f},{-1.6f,2,1.3f},v.triangles[v.triangleCount++]),"deck second COL face");
 return b;
}
static CVector MeanPosition(const P::State&s){return P::CentreOfMass(&s);}
static CVector MeanVelocity(const P::State&s){CVector sum;float mass=0;for(int i=0;i<P::SIM_BONES;++i){const float m=1/P::invMass[i];sum+=(s.pos[i]-s.prev[i])*(m/P::STEP);mass+=m;}return sum/mass;}
static void Run(int hz,int weight,int motion){
 for(auto&s:R::slots)s=R::Slot();R::active=1;R::lastUpdated=R::lastDeferred=0;
 CPed ped;auto&slot=R::slots[0];slot.ped=&ped;static_cast<P::State&>(slot)=Prone();V::platform=Deck();
 VrRagdollSettings::SetWeight(weight);VrRagdollSettings::SetGrip(150);VrRagdollSettings::SetBrake(200);
 for(int i=0;i<360;++i){V::Prepare(V::platform,0,P::STEP);VrRagdollContacts::StepContacts context(&V::platform);P::Step(&slot,VrRagdollContacts::Project,&context);}
 Check(slot.lastSupportCount>=3,"fixture settles on real COL triangles before driving");
 V::platform.vehicles[0].linearVelocity=V::platform.vehicles[0].surfaceLinearVelocity={0,16,0};P::SeedMotion(&slot,{0,16,0},{0,0,0},34);
 R::RefreshBounds(&slot);CTimer::dt=1.f/hz;
 CVector previousLocal=V::platform.vehicles[0].pose.ToLocal(MeanPosition(slot)),previousDelta;
 float peakJump=0,peakSecondDifference=0,peakSlip=0,peakHeight=0,maxError=0;int supported=0;
 for(int frame=0;frame<hz*4;++frame){
  auto&car=V::platform.vehicles[0];const float t=float(frame)/hz;
  float speed=16,omega=0;
  if(motion==1)speed=t<2?16+2*t:20-3*(t-2);
  if(motion==2)omega=.12f;
  car.linearVelocity=car.surfaceLinearVelocity=car.pose.forward*speed;car.angularVelocity=car.surfaceAngularVelocity={0,0,omega};
  CTimer::now+=unsigned(1000.f/hz);R::Update();
  // Exact engine order: ragdoll Update precedes native vehicle movement.
  car.pose=V::PoseAt(car,CTimer::dt);
  const auto presentation=VrRagdollSupport::Build(slot.support,car.pose);
  const CVector local=car.pose.ToLocal(presentation.Point(MeanPosition(slot))),delta=local-previousLocal;
  if(frame>hz){peakJump=Max(peakJump,delta.Magnitude());peakSecondDifference=Max(peakSecondDifference,(delta-previousDelta).Magnitude());}
  previousLocal=local;previousDelta=delta;peakSlip=Max(peakSlip,(MeanVelocity(slot)-car.linearVelocity).Magnitude());peakHeight=Max(peakHeight,MeanPosition(slot).z);maxError=Max(maxError,slot.lastMaxLengthError);
  supported+=slot.lastSupportCount>=3;
  for(int i=0;i<P::SIM_BONES;++i)Check(std::isfinite(slot.pos[i].MagnitudeSqr()),"production scheduler moving-platform pose finite");
 }
 std::printf("platform %dHz weight%d motion%d jump=%.4f secondDiff=%.4f slip=%.4f height=%.3f length=%.3f support%d/%d\n",hz,weight,motion,peakJump,peakSecondDifference,peakSlip,peakHeight,maxError,supported,hz*4);
 Check(supported>hz*3,"ordinary driving retains platform support");Check(maxError<.12f,"support cannot stretch limbs");
 Check(peakJump<.01f&&peakSecondDifference<.01f,"car-relative presentation has no fixed-step sawtooth or direction reversal");
}
int main(){for(int hz:{30,60,72})for(int weight:{100,200})for(int motion=0;motion<3;++motion)Run(hz,weight,motion);std::printf("Platform integration diagnostics: %u checks PASS\n",checks);return 0;}
