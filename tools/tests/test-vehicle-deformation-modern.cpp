#define main ClassicBodySuiteMain
#include "test-vehicle-deformation-body.cpp"
#undef main
#include <chrono>
#include <tuple>
struct WorkTiming {unsigned frames=0;double total=0,peak=0;};
static WorkTiming FinishMeasured(RealCar&car,unsigned milliseconds=16) {
 WorkTiming timing;
 for(unsigned i=0;i<150;i++) {
  ++CTimer::frame;CTimer::time+=milliseconds;const unsigned assignments=geometryAssignments;
  auto start=std::chrono::steady_clock::now();D::Update();
  double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  timing.frames++;timing.total+=ms;timing.peak=std::max(timing.peak,ms);
  Check(geometryAssignments-assignments<=1,"actual Modern job commits at most one geometry per frame");
  auto*s=D::Find(&car.car);
  if(!D::job.state&&!D::batch.state&&(!s||s->pending.depth<=0))break;
 }
 Check(!D::job.state&&!D::batch.state,"actual Modern batch completes before bounded pending lifetime");
 Check(timing.frames*milliseconds<2500,"real queued Modern hit finishes before its expiry, including low frame rates");
 return timing;
}
static float SeamGap(RealCar&car) {
 float gap=0;
 for(unsigned a=0;a<car.atomics.size();a++)if(auto*p=car.Part(a)) {
  auto*g=car.atomics[a]->geometry;std::vector<unsigned char>mask(g->v.size());
  for(auto t:g->t)for(auto v:t.v)mask[v]|=D::BodyMaterial(modelInfo,g->matList.materials[t.matId])?1:2;
  std::map<std::tuple<float,float,float>,M::Vec>positions;
  for(unsigned v=0;v<g->v.size();v++)if(mask[v]==1) {
   auto rest=p->rest[v],now=D::V(g->v[v]);auto key=std::make_tuple(rest.x,rest.y,rest.z);
   auto existing=positions.emplace(key,now);if(!existing.second)gap=std::max(gap,M::Length(now-existing.first->second));
  }
 }
 Check(gap<.00001f,"coincident movable UV/normal seam vertices remain coincident after the full module");
 return gap;
}
int main(){try{
 const char*base=std::getenv("VC_DEFORMATION_TEST_GAMEDATA");Check(base&&*base,"local user-owned Modern model archive is supplied");
 std::string data=std::string(base)+"/modelsets/modern";unsigned maxFrames=0;double peakUpdate=0,totalMs=0;float maxGap=0;
 for(const char*name:{"admiral","sentinel","stinger","oceanic"}) {
  Reader r=ArchiveModel(data,std::string(name)+".dff");Model m=ReadModel(r);ReadRenderedTriangles(r,m);auto bounds=ReadBounds(r);auto materials=MaterialMetadata(r);auto col=ReadCarCollision(data+"/models/coll/vehicles.col",name);
  if(std::strcmp(name,"sentinel")==0) {
   RealCar car(m,bounds,materials);CVehicle peer;RpClump peerClump;std::vector<std::unique_ptr<RpAtomic>> peerAtomics;
   for(unsigned i=0;i<car.atomics.size();i++){peerAtomics.emplace_back(new RpAtomic);auto*a=peerAtomics.back().get();a->frame=car.atomics[i]->frame;a->frame.parent=&peerClump.frame;a->flags=car.atomics[i]->flags;a->id=car.atomics[i]->id;a->setGeometry(car.atomics[i]->geometry,0);peerClump.atomics.push_back(a);}
   peer.handle=111;peer.m_rwObject=&peerClump;CPools::pool[111]=&peer;D::ResetTuning();D::SetStrengthPercent(400);auto hit=WallContact(col,1,1);car.Hit(hit);D::RecordCollision(&peer,{hit.point.x,hit.point.y,hit.point.z},{hit.inward.x,hit.inward.y,hit.inward.z},1000,&car.car);
   unsigned frames=0;for(;frames<160;frames++){++CTimer::frame;CTimer::time+=33;D::Update();auto*a=D::Find(&car.car),*b=D::Find(&peer);if(!D::batch.state&&!D::job.state&&(!a||a->pending.depth<=0)&&(!b||b->pending.depth<=0))break;}
   unsigned primary=0,secondary=0;auto*first=D::Find(&car.car),*second=D::Find(&peer);if(first)for(auto&p:first->parts)primary+=p.current!=nullptr;if(second)for(auto&p:second->parts)secondary+=p.current!=nullptr;
   printf("modern/sentinel paired30FPS primaryParts=%u secondaryParts=%u frames=%u\n",primary,secondary,frames+1);
   Check(primary>=3&&secondary==primary,"both cars in a simultaneous Modern collision complete the accepted component batch at30FPS");
   D::SetVehicleDeformationEnabled(false);for(auto&a:peerAtomics)a->setGeometry(nullptr,0);CPools::pool.erase(111);
  }
  if(std::strcmp(name,"sentinel")==0||std::strcmp(name,"stinger")==0) {
   RealCar car(m,bounds,materials);D::ResetTuning();D::SetStrengthPercent(400);auto hit=WallContact(col,1,1);
   unsigned frames=0;for(unsigned repeat=0;repeat<4;repeat++) {
    CTimer::time+=250;car.Hit(hit);auto timing=FinishMeasured(car,33);frames=std::max(frames,timing.frames);car.Safety();maxGap=std::max(maxGap,SeamGap(car));
   }
   Check(car.Envelope(hit,true)-car.Envelope(hit,false)>.18f,"30FPS heavy Modern queue retains a visible frontal dent");
   std::printf("modern/%s 30FPS repeated front400 maxBatchFrames=%u maxBatchTimeMs=%u\n",name,frames,frames*33);
  }
  for(int side:{0,1}) {
   float contours[2]={};
   for(int mode=0;mode<2;mode++) {
    RealCar car(m,bounds,materials);D::ResetTuning();D::SetStrengthPercent(mode?400:100);D::SetMaxDentCentimeters(32);D::SetRadiusPercent(100);
    auto hit=WallContact(col,side?0:1,1);car.Hit(hit);auto timing=FinishMeasured(car);
    auto*s=D::Find(&car.car);Check(s&&s->hasDent,"real Modern strong hit commits geometry on a visible component");
    contours[mode]=car.Envelope(hit,true)-car.Envelope(hit,false);car.Safety();maxGap=std::max(maxGap,SeamGap(car));
    unsigned firstFrames=timing.frames;double firstTotal=timing.total;
    if(mode) {
     Check(contours[1]>(side?.05f:.18f),"actual Modern exterior silhouette changes visibly after one 400 percent impact");
     Check(contours[1]+.00001f>=contours[0],"400 percent cannot weaken the tested Modern impact compared with 100 percent");
     for(unsigned repeat=0;repeat<3;repeat++) {
      CTimer::time+=250;car.Hit(hit);auto next=FinishMeasured(car);timing.frames=std::max(timing.frames,next.frames);timing.total+=next.total;timing.peak=std::max(timing.peak,next.peak);
      car.Safety();maxGap=std::max(maxGap,SeamGap(car));
     }
    }
    maxFrames=std::max(maxFrames,timing.frames);peakUpdate=std::max(peakUpdate,timing.peak);totalMs+=timing.total;
    std::printf("modern/%s %s strength=%d exterior=%.5fm firstFrames=%u firstHostMs=%.3f maxBatchFrames=%u peakHostUpdateMs=%.3f\n",name,side?"side":"front",mode?400:100,contours[mode],firstFrames,firstTotal,timing.frames,timing.peak);
   }
  }
 }
 std::printf("PASS Modern: %u production checks + %u local data checks; 8 real DFF/COL impacts at100/400, repeated safety/UV/protection/seams. MaxBatchFrames=%u peakHostUpdateMs=%.3f totalHostWorkMs=%.3f maxSeamGap=%.8fm. Host timing is not Quest frame time.\n",checks,dataChecks,maxFrames,peakUpdate,totalMs,maxGap);return 0;
}catch(const std::exception&e){std::fprintf(stderr,"Modern deformation FAILED: %s\n",e.what());return 1;}}