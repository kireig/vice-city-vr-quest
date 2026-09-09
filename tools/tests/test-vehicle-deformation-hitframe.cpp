#pragma warning(push)
#pragma warning(disable:4715 4716)
#define main ExistingFullRuntimeFixtureMain
#include "vehicle-deformation-hitframe-runtime-fixture.inc"
#undef main
#pragma warning(pop)
namespace M=VehicleDeformationDetail;
static CVector CV(M::Vec v){return CVector(v.x,v.y,v.z);}
static RwMatrix RWMatrix(const M::Transform&t){RwMatrix m;m.right=D::RW(t.right);m.up=D::RW(t.forward);m.at=D::RW(t.up);m.pos=D::RW(t.pos);return m;}
struct FrontScene:Scene {
 M::Transform component,root;
 FrontScene(){
  component.forward={0,0,-1};component.up={0,1,0};component.pos={0,2,.7f};
  atomics[0].frame.name="bump_front";
  Pose(0,M::Vec());
 }
 void Pose(float angle,M::Vec p){
  root=M::Transform();root.right={std::cos(angle),std::sin(angle),0};root.forward={-std::sin(angle),std::cos(angle),0};root.pos=p;
  cars[0].matrix.right=CV(root.right);cars[0].matrix.forward=CV(root.forward);cars[0].matrix.up=CV(root.up);cars[0].matrix.pos=CV(root.pos);
  clumps[0].frame.matrix=RWMatrix(root);atomics[0].frame.matrix=RWMatrix(root*component);
  TheCamera.position=CV(root.pos);
 }
 void LocalHit(M::Vec point,M::Vec inward,float impulse){D::RecordCollision(&cars[0],CV(root.Point(point)),CV(root.Vector(inward)),impulse,&wall);}
 void Side(unsigned side){
  component=M::Transform();component.forward={0,0,-1};
  if(side==0){component.right={1,0,0};component.up={0,1,0};component.pos={0,2,.7f};}
  if(side==1){component.right={-1,0,0};component.up={0,-1,0};component.pos={0,-2,.7f};}
  if(side==2){component.right={0,-1,0};component.up={1,0,0};component.pos={1,0,.7f};}
  if(side==3){component.right={0,1,0};component.up={-1,0,0};component.pos={-1,0,.7f};}
  atomics[0].frame.name=side==0?"bump_front":side==1?"bump_rear":side==2?"door_right":"door_left";
 }
};
#if HITFRAME_HAS_COLLISION_POSE
using CapturedPose=D::CollisionPose;
#else
// The before witness still runs the unmodified old RecordCollision. The old
// API cannot accept the captured transform and consequently uses rolled-back B.
struct CapturedPose {float right[3],forward[3],up[3],position[3];};
#endif
static CapturedPose Capture(const M::Transform&t){
 CapturedPose p={};const M::Vec values[]={t.right,t.forward,t.up,t.pos};float*fields[]={p.right,p.forward,p.up,p.position};
 for(unsigned i=0;i<4;++i){fields[i][0]=values[i].x;fields[i][1]=values[i].y;fields[i][2]=values[i].z;}return p;
}
static void RecordCaptured(FrontScene&s,CVector point,CVector normal,const CapturedPose&pose){
#if HITFRAME_HAS_COLLISION_POSE
 D::RecordCollision(&s.cars[0],point,normal,800,&s.wall,&pose);
#else
 (void)pose;D::RecordCollision(&s.cars[0],point,normal,800,&s.wall);
#endif
}
static std::vector<RwV3d> FrontImpact(float initialAngle,float movedAngle,bool moveDuringJob,bool repeated){
 FrontScene s;s.Pose(initialAngle,{5,4,0});D::SetVehicleDeformationEnabled(true);
 const M::Vec hit(.1f,2,.7f),normal(0,-1,0);
 s.LocalHit(hit,normal,800);auto*state=D::Find(&s.cars[0]);
 Check(state&&M::Length(state->pending.point-hit)<.000003f&&M::Length(state->pending.normal-normal)<.000003f,"actual RecordCollision maps front point and inward normal into receiver car coordinates");
 if(moveDuringJob){D::BeginBatch(*state,&s.cars[0]);D::BeginCandidate();Check(D::job.state!=nullptr,"fixture begins the true staged mesh job before turning");}
 s.Pose(initialAngle+movedAngle,{12,-7,1});
 if(repeated){
  if(moveDuringJob){Check(M::Length(D::job.hit.point-hit)<.000003f,"in-flight job keeps captured local front hit after translation and rotation");}
  else {
   s.LocalHit({.4f,1.7f,.7f},{-1,0,0},400);s.LocalHit({.4f,1.7f,.7f},{-1,0,0},800);
   Check(M::Length(state->pending.point-hit)<.000003f&&M::Length(state->pending.normal-normal)<.000003f,"weaker and equal later side contacts cannot displace earliest strongest front contact");
  }
 }
 for(unsigned f=0;f<100;++f){
  if(moveDuringJob)s.Pose(initialAngle+movedAngle+f*.001f,{12+f*.005f,-7,1});
  ++CTimer::frame;CTimer::time+=16;D::Update();
 }
 auto*g=s.atomics[0].geometry;Check(g!=s.original,"queued front collision commits nonzero deformation after car movement");
 float maxDepth=0,sideDelta=0;
 for(auto v:g->v){M::Vec p=s.component.Point(D::V(v));maxDepth=std::max(maxDepth,2-p.y);}
 Check(maxDepth>.03f&&maxDepth<=.33f,"committed front plane moves inward in its own car frame");
 for(unsigned i=0;i<4;++i)sideDelta=std::max(sideDelta,std::fabs(g->v[i].x-s.original->v[i].x));
 Check(sideDelta<.00001f,"front impact does not turn into a sideward displacement of painted vertices");
 std::printf("hitframe heading%.2f queuedTurn%.2f activeJob%d repeated%d vertices%d frontDepth%.6f sideDelta%.7f\n",initialAngle,movedAngle,moveDuringJob,repeated,g->numVertices,maxDepth,sideDelta);
 return g->v;
}
static void StrongestContactChanges(){
 FrontScene s;D::SetVehicleDeformationEnabled(true);s.Pose(.7f,{4,3,0});
 s.LocalHit({0,2,.7f},{0,-1,0},600);s.Pose(-.5f,{6,2,0});
 s.LocalHit({.4f,1.7f,.7f},{-1,0,0},800);auto*state=D::Find(&s.cars[0]);
 Check(state&&M::Length(state->pending.point-M::Vec(.4f,1.7f,.7f))<.000003f&&M::Length(state->pending.normal-M::Vec(-1,0,0))<.000003f,"a genuinely stronger later side contact intentionally replaces a front contact in receiver-local space");
 Check(state->pending.impulse==800,"repeated-contact queue compares raw collision impulse after depth saturation");
}
static std::vector<RwV3d> ContactSnapshot(unsigned side,float heading,bool rollback){
 FrontScene s;s.Side(side);s.Pose(heading,{5,4,0});D::SetVehicleDeformationEnabled(true);
 const M::Vec localPoint=s.component.Point({.1f,0,0}),localNormal=s.component.up*-1;
 const CVector worldPoint=CV(s.root.Point(localPoint)),worldNormal=CV(s.root.Vector(localNormal));
 const CapturedPose captured=Capture(s.root);
 if(rollback)s.Pose(heading+.5f,{5.7f,3.1f,.2f});
 RecordCaptured(s,worldPoint,worldNormal,captured);auto*state=D::Find(&s.cars[0]);
 Check(state!=nullptr,"real collision snapshot still admits the receiver");
 const float pointError=M::Length(state->pending.point-localPoint),normalError=M::Length(state->pending.normal-localNormal);
 std::printf("snapshot side%u heading%.2f rollback%d pointError%.7fm normalError%.7f\n",side,heading,rollback,pointError,normalError);
 Check(pointError<.000004f&&normalError<.000004f,"contact-time matrix keeps native manifold front/rear/side coordinates despite B position rollback before callback");
 for(unsigned f=0;f<100;++f){s.Pose(heading+1.2f+f*.001f,{12+f*.003f,-7,1});++CTimer::frame;CTimer::time+=16;D::Update();}
 auto*g=s.atomics[0].geometry;Check(g!=s.original,"captured front/rear/side manifold produces a visible private geometry after queued car movement");
 float depth=0;for(auto v:g->v)depth=std::max(depth,-v.z);
 Check(depth>.03f&&depth<.33f,"snapshot contact dents inward on the originally contacted panel");
 return g->v;
}
int main(){
 const auto expected=FrontImpact(0,0,false,false);float maxDifference=0;
 for(float heading:{0.f,.7f,-2.f})for(float queuedTurn:{-1.2f,1.4f})for(bool activeJob:{false,true}){
  const auto vertices=FrontImpact(heading,queuedTurn,activeJob,true);
  Check(vertices.size()==expected.size(),"world motion cannot change local subdivision topology");
  for(unsigned i=0;i<vertices.size();++i){const float d=M::Length(D::V(vertices[i])-D::V(expected[i]));maxDifference=std::max(maxDifference,d);Check(d<.00002f,"moving/turning car produces the same local front dent as stationary baseline");}
 }
 StrongestContactChanges();
 for(unsigned side=0;side<4;++side){
  const auto baseline=ContactSnapshot(side,0,false);
  for(float heading:{0.f,.8f,-2.f,3.14159265f}){
   const auto replay=ContactSnapshot(side,heading,true);Check(replay.size()==baseline.size(),"B rollback snapshot retains panel topology");
   for(unsigned i=0;i<replay.size();++i)Check(M::Length(D::V(replay[i])-D::V(baseline[i]))<.00002f,"captured pre-rollback transform reproduces the same dent for every car orientation and panel side");
  }
 }
 std::printf("Hit frame audit: %u checks PASS; maximum moved/stationary mesh difference %.8fm.\n",checks,maxDifference);return 0;
}
