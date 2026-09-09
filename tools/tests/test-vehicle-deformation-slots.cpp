#pragma warning(push)
#pragma warning(disable:4715 4716)
#define main ExistingFullRuntimeFixtureMain
#include "vehicle-deformation-slots-runtime-fixture.inc"
#undef main
#pragma warning(pop)

static void GlobalUpdate(){
#if SLOT_TEST_HAS_UPDATE
 D::Update();
#endif
}

struct Fleet:Scene {
 static const unsigned count=D::MAX_CARS+1;
 CVehicle extra[count];RpClump extraClump[count];RpAtomic extraAtomic[count];RpGeometry *rejected=nullptr;
 Fleet(){
  for(unsigned i=0;i<count;++i){
   extra[i].handle=200+int(i);extra[i].m_rwObject=&extraClump[i];extraClump[i].atomics.push_back(&extraAtomic[i]);extraAtomic[i].frame.parent=&extraClump[i].frame;
   extraAtomic[i].setGeometry(original,0);CPools::pool[extra[i].handle]=&extra[i];
  }
 }
 ~Fleet(){
  D::SetVehicleDeformationEnabled(false);
  for(unsigned i=0;i<count;++i){D::Release(&extra[i]);extraAtomic[i].setGeometry(nullptr,0);extraClump[i].alive=false;extra[i].m_rwObject=nullptr;CPools::pool.erase(extra[i].handle);}
  if(rejected)rejected->destroy();
 }
 void Reject(unsigned i,int reason){
  if(reason==0)extraAtomic[i].frame.name="wheel_lf";
  if(reason==3)extraAtomic[i].flags=0;
  if(reason==1||reason==2){
   if(!rejected){
    rejected=RpGeometry::create(4,2,RpGeometry::NORMALS);rejected->matList.appendMaterial(&paint);
    rejected->v=original->v;rejected->morphTargets[0].vertices=rejected->v.data();for(auto&n:rejected->n)n={0,0,1};
    rejected->t[0]={{0,1,2},0};rejected->t[1]={{0,1,2},0};rejected->morphTargets[0].boundingSphere.radius=.6f;
    if(reason==1)rejected->flags|=RpGeometry::NATIVE;
    if(reason==2){rejected->matList.appendMaterial(&glass);rejected->t[1].matId=1;}
    ListHeader(rejected);
   }
   extraAtomic[i].setGeometry(rejected,0);
  }
 }
 void HitExtra(unsigned i){D::RecordCollision(&extra[i],CVector(),CVector(0,0,-1),400,&wall);}
 void ProcessExtra(unsigned i,unsigned ticks=24){for(unsigned tick=0;tick<ticks;++tick){++CTimer::frame;CTimer::time+=16;D::Process(&extra[i]);}}
};
static unsigned failures;
static void CheckAdmission(bool admitted,const char *label){
 if(!admitted)++failures;
 std::printf("%s: next valid car admission %s; occupied=%u privateVertices=%zu\n",label,admitted?"PASS":"FAIL",unsigned(std::count_if(std::begin(D::states),std::end(D::states),[](const D::State&s){return s.owner!=nullptr;})),D::privateVertices);
}
static void RejectedOwners(int reason){
 Fleet s;D::SetVehicleDeformationEnabled(true);
 for(unsigned i=0;i<D::MAX_CARS;++i){s.Reject(i,reason);s.HitExtra(i);s.ProcessExtra(i);}
 Check(D::privateVertices==0&&D::privateBytes==0,"all rejected hits leave no private geometry allocated");
 s.Hit();bool admitted=D::Find(&s.cars[0])!=nullptr;
 const char *labels[]={"Rejected body frame","Unsupported geometry","Paint fully vetoed by glass","No visible candidates"};CheckAdmission(admitted,labels[reason]);
 if(admitted){s.Finish();Check(s.atomics[0].geometry!=s.original,"released empty slot admits a real later mesh commit");}
}
static void ExpiredUnrenderedOwners(bool renderAfterExpiry){
 Fleet s;D::SetVehicleDeformationEnabled(true);
 for(unsigned i=0;i<D::MAX_CARS;++i)s.HitExtra(i);
 // Collision admission precedes PreRender. These close cars can remain
 // unrendered (behind the headset), so expiry cannot rely on their callback.
 CTimer::time+=2601;++CTimer::frame;
 if(renderAfterExpiry)for(unsigned i=0;i<D::MAX_CARS;++i){++CTimer::frame;D::Process(&s.extra[i]);}
 GlobalUpdate();
 Check(D::privateVertices==0,"unrendered expired work has no private geometry");
 s.Hit();bool admitted=D::Find(&s.cars[0])!=nullptr;CheckAdmission(admitted,renderAfterExpiry?"Expired pending cleared in PreRender":"Expired pending with no PreRender");
 if(admitted){s.Finish();Check(s.atomics[0].geometry!=s.original,"expired pending owner does not prevent valid dent");}
}
static void NoPreRenderProgress(){
 Scene s;D::SetVehicleDeformationEnabled(true);s.Hit();
 for(unsigned frame=0;frame<80;++frame){
  ++CTimer::frame;CTimer::time+=16;const unsigned assignments=geometryAssignments;
  GlobalUpdate();
  Check(geometryAssignments-assignments<=1,"actual global Update retains one commit per frame");
 }
 bool committed=s.atomics[0].geometry!=s.original;
 std::printf("No owner PreRender: valid queued impact commit %s\n",committed?"PASS":"FAIL");
 if(!committed)++failures;
 Check(s.atomics[1].geometry==s.original,"offscreen work preserves other model instances");
}
static void PreserveRealDent(){
 Fleet s;D::SetVehicleDeformationEnabled(true);s.Hit();s.Finish();auto *dent=s.atomics[0].geometry;
 Check(dent!=s.original,"preservation fixture starts with a committed real dent");const auto vertices=dent->v;
 for(unsigned i=0;i<D::MAX_CARS-1;++i){s.Reject(i,0);s.HitExtra(i);s.ProcessExtra(i);}
 s.Hit(1);bool admitted=D::Find(&s.cars[1])!=nullptr;CheckAdmission(admitted,"One real dent plus rejected empty owners");
 if(admitted)s.Finish();
 Check(s.atomics[0].geometry==dent&&dent->alive&&vertices.size()==dent->v.size()&&std::memcmp(vertices.data(),dent->v.data(),vertices.size()*sizeof(RwV3d))==0,"reclaiming empty slots cannot repair or alter a nearby committed dent");
 if(admitted)Check(s.atomics[1].geometry!=s.original,"later valid car commits while earlier dent survives");
}
int main(){
 for(int reason=0;reason<4;++reason)RejectedOwners(reason);
 ExpiredUnrenderedOwners(false);ExpiredUnrenderedOwners(true);PreserveRealDent();NoPreRenderProgress();
 Check(failures==0,"empty or expired owners must not starve later vehicles");
 std::printf("Vehicle deformation slots: %u full-production checks PASS; rejected/no-visible/expired owners, later commit and existing dent preservation\n",checks);return 0;
}
