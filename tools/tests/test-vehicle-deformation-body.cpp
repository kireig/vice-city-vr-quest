#define _CRT_SECURE_NO_WARNINGS
#include "vehicle-deformation-body-native.inc"
#include "vehicle-deformation-body-reader.inc"
struct MaterialDetails {M::Colour colour;std::string texture;};
static std::vector<std::vector<MaterialDetails>> MaterialMetadata(const Reader&r){
 Chunk clump{16,r.U32(8),12,12+r.U32(4)},list=r.Child(clump,26);std::vector<std::vector<MaterialDetails>> all;
 for(auto c:r.Children(list.body,list.end))if(c.id==15){
  Chunk ml=r.Child(c,8),h=r.Child(ml,1);std::vector<Chunk> records;for(auto m:r.Children(ml.body,ml.end))if(m.id==7)records.push_back(m);
  std::vector<MaterialDetails> materials;unsigned next=0;
  for(unsigned i=0;i<r.U32(h.body);++i){int ref=int32_t(r.U32(h.body+4+i*4));if(ref>=0){materials.push_back(materials[size_t(ref)]);continue;}
   Chunk mat=records[next++],s=r.Child(mat,1);MaterialDetails d;std::memcpy(&d.colour,&r.data[s.body+4],4);
   for(auto child:r.Children(mat.body,mat.end))if(child.id==6)for(auto text:r.Children(child.body,child.end))if(text.id==2){d.texture=r.Text(text.body,text.end-text.body);break;}materials.push_back(d);
  }all.push_back(materials);
 }return all;
}
static RwMatrix NativeMatrix(const M::Transform&t){RwMatrix m;m.right=D::RW(t.right);m.up=D::RW(t.forward);m.at=D::RW(t.up);m.pos=D::RW(t.pos);return m;}
// The observed silhouette is the outer skin, including parts the selector did
// not choose. An engine top behind the grille is not an undeformed body panel.
// This test oracle stays independent of BodyMaterial so the paint-only baseline
// cannot make the failing bumper/chassis disappear from the measurement.
static bool ExteriorSurface(const RpMaterial*material){
 if(material->color.alpha!=255)return false;
 std::string name=material->texture?RwTextureGetName(material->texture):"";for(char&c:name)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
 for(const char*protectedName:{"engine","interior","seat","steer","dash","wheel","tyre","tire","glass","windscreen"})if(name.find(protectedName)!=std::string::npos)return false;
 return true;
}
struct RealCar {
 const Model&model;CVehicle car;RpClump clump;CEntity wall;
 std::vector<std::unique_ptr<RpMaterial>> materials;std::vector<std::unique_ptr<RwTexture>> textures;
 std::vector<RpGeometry*> originals;std::vector<std::unique_ptr<RpAtomic>> atomics;std::vector<M::Transform> toCar;
 RealCar(const Model&m,const std::vector<Bound>&bounds,const std::vector<std::vector<MaterialDetails>>&metadata):model(m){
  D::SetVehicleDeformationEnabled(false);Check(aliveGeometry==0,"preceding real-car fixture releases native/private geometry");ClearCounters();CPools::pool.clear();modelInfo=CVehicleModelInfo();CTimer::time+=1000;CTimer::frame++;TheCamera.position=CVector();
  unsigned paint=0;
  for(unsigned i=0;i<m.geometries.size();++i){const auto&g=m.geometries[i];uint32 flags=RpGeometry::NORMALS|uint32(g.mesh.uv.size()<<16);if(!g.mesh.colours.empty())flags|=RpGeometry::PRELIT;
   RpGeometry*n=RpGeometry::create(int(g.mesh.vertices.size()),int(g.mesh.triangles.size()),flags);originals.push_back(n);
   for(unsigned j=0;j<g.paint.size();++j){materials.emplace_back(new RpMaterial);auto*mat=materials.back().get();const auto&d=metadata[i][j];mat->color={d.colour.r,d.colour.g,d.colour.b,d.colour.a};
    if(!d.texture.empty()){textures.emplace_back(new RwTexture);textures.back()->name=d.texture;mat->texture=textures.back().get();}
    n->matList.appendMaterial(mat);if(g.paint[j]){Check(paint+1<NUM_FIRST_MATERIALS,"actual native paint pointer array remains bounded");modelInfo.m_materials1[paint++]=mat;}
   }
   for(unsigned j=0;j<g.mesh.vertices.size();++j){n->v[j]=D::RW(g.mesh.vertices[j].pos);n->n[j]=D::RW(g.mesh.vertices[j].normal);if(n->colors){auto c=g.mesh.colours[j];n->c[j]={c.r,c.g,c.b,c.a};}for(unsigned u=0;u<g.mesh.uv.size();++u)n->uv[u][j]={g.mesh.uv[u][j].u,g.mesh.uv[u][j].v};}
   for(unsigned j=0;j<g.mesh.triangles.size();++j){auto t=g.mesh.triangles[j];n->t[j]={{t.v[0],t.v[1],t.v[2]},t.material};}
   n->morphTargets[0].boundingSphere.center=D::RW(bounds[i].center);n->morphTargets[0].boundingSphere.radius=bounds[i].radius;ListHeader(n);
  }
  for(const auto&a:m.atomics){atomics.emplace_back(new RpAtomic);auto*n=atomics.back().get();n->setGeometry(originals[size_t(a.geometry)],0);int frame=NativeSelectionFrame(m,a.frame);toCar.push_back(ToCar(m,frame));n->frame.matrix=NativeMatrix(toCar.back());n->frame.name=m.frames[size_t(frame)].name;n->frame.parent=&clump.frame;n->flags=NativeDamageVisible(m,a,false)?rpATOMICRENDER:0;
   n->id=m.frames[size_t(a.frame)].name.find("windscreen")!=std::string::npos?ATOMIC_FLAG_WINDSCREEN:0;clump.atomics.push_back(n);
  }
  car.m_rwObject=&clump;car.handle=110;CPools::pool[car.handle]=&car;wall.type=ENTITY_TYPE_BUILDING;D::SetVehicleDeformationEnabled(true);
 }
 ~RealCar(){D::SetVehicleDeformationEnabled(false);for(auto&a:atomics)a->setGeometry(nullptr,0);for(auto*g:originals)g->destroy();CPools::pool.clear();Check(aliveGeometry==0,"real-car teardown balances all native/private geometry refs");}
 void Hit(const Contact&hit){D::RecordCollision(&car,CVector(hit.point.x,hit.point.y,hit.point.z),CVector(hit.inward.x,hit.inward.y,hit.inward.z),1000,&wall);}
 void Finish(){for(unsigned i=0;i<150;++i){++CTimer::frame;CTimer::time+=16;const unsigned assigns=geometryAssignments;D::Update();Check(geometryAssignments-assigns<=1,"full real-model job never exceeds one geometry commit per frame");}Check(!D::batch.state&&!D::job.state,"bounded real-model batch finishes within pending TTL");}
 D::Part*Part(unsigned i){auto*s=D::Find(&car);if(s)for(auto&p:s->parts)if(p.atomic==atomics[i].get())return&p;return nullptr;}
 float Envelope(const Contact&hit,bool rest,bool exterior=true){float extent=-1e10f;M::Vec outward=hit.inward*-1,across=M::Unit(M::Cross({0,0,1},outward));
  for(unsigned i=0;i<atomics.size();++i)if(atomics[i]->flags&rpATOMICRENDER){auto*g=atomics[i]->geometry;auto*part=Part(i);std::vector<uint8> skin(g->v.size(),exterior?0:1);if(exterior)for(auto t:g->t)if(ExteriorSurface(g->matList.materials[t.matId]))for(auto v:t.v)skin[v]=1;
   for(unsigned j=0;j<g->v.size();++j){if(!skin[j])continue;M::Vec base=part?part->rest[j]:D::V(g->v[j]);M::Vec p=toCar[i].Point(base),d=p-hit.point;if(std::fabs(M::Dot(d,across))>.3f||std::fabs(d.z)>.28f)continue;extent=std::max(extent,M::Dot(rest?p:toCar[i].Point(D::V(g->v[j])),outward));}}
  return extent;
 }
 float UpperNose(const Contact&hit){float peak=0;
  for(unsigned i=0;i<atomics.size();++i)if(atomics[i]->frame.name.find("chassis")!=std::string::npos){const auto&input=model.geometries[size_t(model.atomics[i].geometry)];auto*g=atomics[i]->geometry;
   auto*part=Part(i);std::vector<uint8> nonpaint(g->v.size());for(auto t:g->t)if(!input.paint[t.matId])for(auto v:t.v)nonpaint[v]|=D::BodyMaterial(modelInfo,g->matList.materials[t.matId])?1:2;
   for(unsigned j=0;j<g->v.size();++j){auto rest=part?part->rest[j]:D::V(g->v[j]);auto p=toCar[i].Point(rest);if(nonpaint[j]!=1||p.z<hit.point.z-.08f||M::Length(p-hit.point)>1.2f)continue;auto delta=toCar[i].Vector(D::V(g->v[j])-rest);peak=std::max(peak,M::Dot(delta,hit.inward));}
  }return peak;
 }
 void Safety(float maxDent=.32f){bool finite=true,protectedStable=true,uvStable=true,cap=true,winding=true;
  for(unsigned i=0;i<atomics.size();++i){auto*part=Part(i);if(!part)continue;auto*g=atomics[i]->geometry;const auto&input=model.geometries[size_t(model.atomics[i].geometry)];std::vector<uint8> mask(input.mesh.vertices.size());
   for(auto t:input.mesh.triangles)for(auto v:t.v)mask[v]|=D::BodyMaterial(modelInfo,originals[size_t(model.atomics[i].geometry)]->matList.materials[t.material])?1:2;
   for(unsigned j=0;j<g->v.size();++j){M::Vec p=D::V(g->v[j]);finite&=M::Finite(p)&&M::Finite(D::V(g->n[j]));cap&=M::Length(toCar[i].Vector(p-part->rest[j]))<=maxDent+.00002f;
    if(j<input.mesh.vertices.size()){if(mask[j]!=1)protectedStable&=M::Length(p-input.mesh.vertices[j].pos)==0&&M::Length(D::V(g->n[j])-input.mesh.vertices[j].normal)==0;for(unsigned u=0;u<input.mesh.uv.size();++u)uvStable&=g->uv[u][j].u==input.mesh.uv[u][j].u&&g->uv[u][j].v==input.mesh.uv[u][j].v;}
   }
   for(auto t:g->t){M::Vec before=M::Cross(part->rest[t.v[1]]-part->rest[t.v[0]],part->rest[t.v[2]]-part->rest[t.v[0]]),after=M::Cross(D::V(g->v[t.v[1]])-D::V(g->v[t.v[0]]),D::V(g->v[t.v[2]])-D::V(g->v[t.v[0]]));winding&=M::ValidOrientedArea(before,after);}
  }
  Check(finite&&cap&&winding,"actual committed output remains finite, within configured cap and preserves authored oriented area");Check(protectedStable,"actual alpha/engine/shared protected positions and normals remain unchanged");Check(uvStable,"actual original texture coordinates stay unchanged");
 }
};
static void Scenario(const char*name,const Model&m,const std::vector<Bound>&bounds,const std::vector<std::vector<MaterialDetails>>&metadata,const Contact&hit,const char*label,bool front){
 RealCar car(m,bounds,metadata);car.Hit(hit);auto*state=D::Find(&car.car);Check(state!=nullptr,"actual native contact admits real car");D::BeginBatch(*state,&car.car);
 bool bumper=false,chassis=false;std::printf("%s %s candidates:",name,label);
 for(unsigned i=0;i<D::batch.count;++i){auto*a=D::batch.candidates[i].atomic;std::printf(" %s",a->frame.name.c_str());bumper|=a->frame.name.find("bump_front")!=std::string::npos;chassis|=a->frame.name.find("chassis")!=std::string::npos;}
 std::printf("\n");Check(D::batch.count<=4,"real native selector retains bounded candidate budget");if(front)Check(bumper&&chassis,"front collision selects actual unpainted bumper and structural chassis, not only bonnet or wings");
 car.Finish();const float envelope=car.Envelope(hit,true)-car.Envelope(hit,false),nose=car.UpperNose(hit);
 std::printf("%s %s one-hit exteriorEnvelope=%.5fm upperNonpaintNose=%.5fm rawIncludingEngine=%.5fm\n",name,label,envelope,nose,car.Envelope(hit,true,false)-car.Envelope(hit,false,false));
 Check(envelope>.05f,"actual contacted exterior envelope compresses by over5cm after one strong impact");
 // The wall directly loads the upper nose; a corner can primarily load the
 // adjoining wing. The >5cm full exterior criterion above is common to both.
 if(front)Check(nose>(std::string(label)=="front-wall"?.05f:.02f),"actual upper nonpaint chassis nose compresses, beyond low painted valance movement");car.Safety();
 for(unsigned i=0;i<4;++i){car.Hit(hit);car.Finish();car.Safety();}
}
int main(){try{
 const char*data=std::getenv("VC_DEFORMATION_TEST_GAMEDATA");Check(data&&*data,"test reads user-owned local game archive at runtime");
 for(const char*name:{"admiral","sentinel","stinger","oceanic"}){Reader r=ArchiveModel(data,std::string(name)+".dff");Model m=ReadModel(r);ReadRenderedTriangles(r,m);auto bounds=ReadBounds(r);auto materials=MaterialMetadata(r);auto col=ReadCarCollision(std::string(data)+"/models/coll/vehicles.col",name);
  Scenario(name,m,bounds,materials,WallContact(col,1,1),"front-wall",true);Scenario(name,m,bounds,materials,PoleContact(col,1,1,.7f),"front-corner",true);Scenario(name,m,bounds,materials,WallContact(col,0,1),"right-side",false);
  for(int radius:{50,200})for(int limit:{10,60}){
   RealCar car(m,bounds,materials);D::SetStrengthPercent(400);D::SetRadiusPercent(radius);D::SetMaxDentCentimeters(limit);
   for(int hit=0;hit<3;++hit){car.Hit(WallContact(col,1,1));car.Finish();car.Safety(limit*.01f);}
   std::printf("%s maximum strength radius=%d%% cap=%dcm repeated-impact safety passed\n",name,radius,limit);
   D::ResetTuning();
  }
 }
 std::printf("PASS real body: %u full-module checks + %u data parse checks; actual4 classic DFF/COL, rendered BinMesh, candidate roles, exterior envelope, upper nose, repeated topology/protection.\n",checks,dataChecks);return 0;
}catch(const std::exception&e){std::fprintf(stderr,"Real body FAILED: %s\n",e.what());return 1;}}
