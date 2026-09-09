#define _CRT_SECURE_NO_WARNINGS
#include "vehicle-deformation-mesh-production.inc"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <fstream>
#include <string>
#include <stdexcept>
#include <limits>
namespace D=VehicleDeformationDetail;
static unsigned checks;
static void Check(bool yes,const char *message){++checks;if(!yes)throw std::runtime_error(message);}
struct Chunk { uint32_t id,version; size_t body,end; };
struct Reader {
	std::vector<uint8_t> data;
	void Range(size_t p,size_t n) const {if(p>data.size() || n>data.size()-p)throw std::runtime_error("DFF range overflow");}
	uint32_t U32(size_t p)const{Range(p,4);uint32_t v;std::memcpy(&v,&data[p],4);return v;}
	uint16_t U16(size_t p)const{Range(p,2);uint16_t v;std::memcpy(&v,&data[p],2);return v;}
	float Float(size_t p)const{Range(p,4);float v;std::memcpy(&v,&data[p],4);return v;}
	D::Vec Vec(size_t p)const{return D::Vec(Float(p),Float(p+4),Float(p+8));}
	std::string Text(size_t p,size_t n)const{Range(p,n);size_t end=0;while(end<n && data[p+end])++end;return std::string(reinterpret_cast<const char*>(&data[p]),end);}
	std::vector<Chunk> Children(size_t p,size_t end)const{
		std::vector<Chunk> out;Range(p,end-p);
		while(p+12<=end){size_t next=p+12+U32(p+4);Check(next<=end,"valid RenderWare child size");out.push_back({U32(p),U32(p+8),p+12,next});p=next;}
		return out;
	}
	Chunk Child(Chunk parent,uint32_t id)const{for(auto c:Children(parent.body,parent.end))if(c.id==id)return c;throw std::runtime_error("DFF required child missing");}
};
static uint32_t Version(uint32_t id){return id&0xffff0000u?((id>>14&0x3ff00u)+0x30000u)|(id>>16&0x3fu):id<<8;}
static Reader ArchiveModel(std::string root,std::string name){
	std::ifstream directory(root+"/models/gta3.dir",std::ios::binary);Check(bool(directory),"local DIR readable");
	char record[32]; uint32_t sector=0,count=0;
	while(directory.read(record,32)){
		std::string entry(record+8,std::find(record+8,record+32,'\0'));
		std::transform(entry.begin(),entry.end(),entry.begin(),[](unsigned char c){return char(std::tolower(c));});
		if(entry==name){std::memcpy(&sector,record,4);std::memcpy(&count,record+4,4);break;}
	}
	Check(count>0 && count<32768,"requested real DFF exists in local IMG");
	std::ifstream archive(root+"/models/gta3.img",std::ios::binary);Check(bool(archive),"local IMG readable");
	archive.seekg(std::streamoff(sector)*2048);Reader reader;reader.data.resize(size_t(count)*2048);
	Check(bool(archive.read(reinterpret_cast<char*>(reader.data.data()),std::streamsize(reader.data.size()))),"selected DFF bytes readable");
	return reader;
}
struct Frame {D::Transform local;int parent;std::string name;};
struct Geometry {D::Mesh mesh;std::vector<bool> paint;};
struct Atomic {int frame,geometry;};
struct Model {std::vector<Frame> frames;std::vector<Geometry> geometries;std::vector<Atomic> atomics;};
static Geometry ReadGeometry(Reader const&r,Chunk chunk){
	Geometry g;Chunk s=r.Child(chunk,1);uint32_t flags=r.U32(s.body),nt=r.U32(s.body+4),nv=r.U32(s.body+8),nm=r.U32(s.body+12);
	Check(!(flags&0x01000000u) && nv<65536 && nt<200000 && nm==1,"classic nonnative single-morph geometry supported");
	size_t p=s.body+16;if(Version(s.version)<0x34000)p+=12;
	g.mesh.vertices.resize(nv);
	if(flags&8){g.mesh.colours.resize(nv);for(uint32_t i=0;i<nv;++i){r.Range(p,4);std::memcpy(&g.mesh.colours[i],&r.data[p],4);p+=4;}}
	unsigned sets=(flags>>16)&255;if(!sets)sets=flags&128?2:flags&4?1:0;Check(sets<=8,"bounded UV sets");
	g.mesh.uv.resize(sets);for(auto &uv:g.mesh.uv){uv.resize(nv);for(auto &v:uv){v={r.Float(p),r.Float(p+4)};p+=8;}}
	g.mesh.triangles.resize(nt);
	for(auto &t:g.mesh.triangles){uint32_t a=r.U32(p),b=r.U32(p+4);t={{uint16_t(a>>16),uint16_t(a),uint16_t(b>>16)},uint16_t(b)};p+=8;}
	p+=16;bool vertices=r.U32(p)!=0,normals=r.U32(p+4)!=0;p+=8;
	Check(vertices,"actual classic vertex positions exist");
	for(auto &v:g.mesh.vertices){v.pos=v.rest=r.Vec(p);p+=12;}
	if(normals)for(auto &v:g.mesh.vertices){v.normal=r.Vec(p);p+=12;}
	Check(p==s.end,"geometry position/normal/UV layout matches actual RW structure");
	Chunk list=r.Child(chunk,8),header=r.Child(list,1);uint32_t count=r.U32(header.body);
	std::vector<Chunk> material;for(auto c:r.Children(list.body,list.end))if(c.id==7)material.push_back(c);
	size_t next=0;
	for(uint32_t i=0;i<count;++i){
		int32_t ref=int32_t(r.U32(header.body+4+4*i));
		if(ref>=0){Check(size_t(ref)<g.paint.size(),"material back-reference valid");g.paint.push_back(g.paint[size_t(ref)]);}
		else{Check(next<material.size(),"material payload present");Chunk m=r.Child(material[next++],1);r.Range(m.body+4,4);
			auto const*c=&r.data[m.body+4];g.paint.push_back(c[3]==255 && ((c[0]==60&&c[1]==255&&c[2]==0)||(c[0]==255&&c[1]==0&&c[2]==175)));}
	}
	for(auto const&t:g.mesh.triangles){Check(t.material<g.paint.size(),"actual triangle material index valid");for(auto v:t.v)Check(v<nv,"actual triangle vertex index valid");D::MarkTriangle(g.mesh,t,g.paint[t.material]);}
	return g;
}
static Model ReadModel(Reader const&r){
	Check(r.U32(0)==16,"actual IMG record contains RW clump");Chunk clump{16,r.U32(8),12,12+r.U32(4)};Model model;
	Chunk frameList=r.Child(clump,14),frameHeader=r.Child(frameList,1);uint32_t nf=r.U32(frameHeader.body);Check(nf<1024,"bounded frame count");
	std::vector<Chunk> extensions;for(auto c:r.Children(frameList.body,frameList.end))if(c.id==3)extensions.push_back(c);
	for(uint32_t i=0;i<nf;++i){
		size_t p=frameHeader.body+4+56*i;Frame frame;frame.local.right=r.Vec(p);frame.local.forward=r.Vec(p+12);frame.local.up=r.Vec(p+24);frame.local.pos=r.Vec(p+36);frame.parent=int32_t(r.U32(p+48));
		if(i<extensions.size())for(auto plugin:r.Children(extensions[i].body,extensions[i].end))if(plugin.id==0x0253f2feu)frame.name=r.Text(plugin.body,plugin.end-plugin.body);
		model.frames.push_back(frame);
	}
	Chunk geometryList=r.Child(clump,26);for(auto c:r.Children(geometryList.body,geometryList.end))if(c.id==15)model.geometries.push_back(ReadGeometry(r,c));
	for(auto c:r.Children(clump.body,clump.end))if(c.id==20){Chunk a=r.Child(c,1);int f=int(r.U32(a.body)),g=int(r.U32(a.body+4));Check(f>=0&&size_t(f)<model.frames.size()&&g>=0&&size_t(g)<model.geometries.size(),"atomic frame/geometry association valid");model.atomics.push_back({f,g});}
	return model;
}
static D::Transform ToCar(Model const&m,int frame,int depth=0){
	Check(depth<64,"acyclic bounded frame hierarchy");auto const&f=m.frames[size_t(frame)];
	if(f.parent<0)return D::Transform();
	Check(size_t(f.parent)<m.frames.size(),"parent frame exists");return ToCar(m,f.parent,depth+1)*f.local;
}
static float Area(D::Mesh const&m,D::Triangle const&t){return D::Length(D::Cross(m.vertices[t.v[1]].pos-m.vertices[t.v[0]].pos,m.vertices[t.v[2]].pos-m.vertices[t.v[0]].pos))*.5f;}
static void ValidateSubdivision(D::Mesh const&before,D::Mesh const&after){
	Check(after.vertices.size()>=before.vertices.size()&&after.uv.size()==before.uv.size(),"subdivision preserves mesh and UV channels");
	Check(after.colours.empty()==before.colours.empty(),"subdivision preserves actual prelight channel presence");
	for(size_t i=0;i<before.vertices.size();++i){
		Check(D::Length(before.vertices[i].pos-after.vertices[i].pos)==0,"original authored vertices are not displaced by subdivision");
		for(size_t set=0;set<before.uv.size();++set)Check(std::memcmp(&before.uv[set][i],&after.uv[set][i],sizeof(D::UV))==0,"original UV data unchanged");
	}
	for(auto const&set:after.uv){Check(set.size()==after.vertices.size(),"interpolated UV count matches vertices");for(auto uv:set)Check(std::isfinite(uv.u)&&std::isfinite(uv.v),"interpolated UV values finite");}
	double beforeArea=0,afterArea=0;D::Vec beforeVector,afterVector;
	for(auto const&t:before.triangles){beforeArea+=Area(before,t);beforeVector=beforeVector+D::Cross(before.vertices[t.v[1]].pos-before.vertices[t.v[0]].pos,before.vertices[t.v[2]].pos-before.vertices[t.v[0]].pos);}
	for(auto const&t:after.triangles){for(auto v:t.v)Check(v<after.vertices.size(),"subdivided triangle indices valid");afterArea+=Area(after,t);afterVector=afterVector+D::Cross(after.vertices[t.v[1]].pos-after.vertices[t.v[0]].pos,after.vertices[t.v[2]].pos-after.vertices[t.v[0]].pos);}
	Check(std::fabs(beforeArea-afterArea)<.0002*(1+beforeArea),"conforming subdivision preserves surface area");
	Check(D::Length(beforeVector-afterVector)<.002f,"subdivision preserves oriented triangle area");
}
static void ValidateDent(D::Mesh const&rest,D::Mesh const&mesh,D::Transform const&toCar){
	Check(rest.vertices.size()==mesh.vertices.size() && rest.triangles.size()==mesh.triangles.size(),"dent does not alter topology");
	for(size_t i=0;i<mesh.vertices.size();++i){auto const&v=mesh.vertices[i];
		Check(D::Finite(v.pos)&&D::Finite(v.normal),"deformed position and normal finite");
		Check(D::Length(toCar.Vector(v.pos-v.rest))<=.32001f,"repeated dents respect32cm cumulative cap");
		if(v.mask!=1)Check(std::memcmp(&v,&rest.vertices[i],sizeof(D::Vertex))==0,"glass/interior/shared protected vertices and normals untouched");
	}
	for(size_t set=0;set<mesh.uv.size();++set)Check(std::memcmp(mesh.uv[set].data(),rest.uv[set].data(),mesh.uv[set].size()*sizeof(D::UV))==0,"dent preserves every UV value");
	for(auto const&t:mesh.triangles){
		D::Vec oldNormal=D::Cross(rest.vertices[t.v[1]].pos-rest.vertices[t.v[0]].pos,rest.vertices[t.v[2]].pos-rest.vertices[t.v[0]].pos);
		D::Vec newNormal=D::Cross(mesh.vertices[t.v[1]].pos-mesh.vertices[t.v[0]].pos,mesh.vertices[t.v[2]].pos-mesh.vertices[t.v[0]].pos);
		if(D::Length(oldNormal)>.000001f){
			if(D::Dot(oldNormal,newNormal)<=0){
				std::printf("Winding failed triangle %u/%u/%u material%u oldArea%.7f newArea%.7f dot%.9f\n",t.v[0],t.v[1],t.v[2],t.material,D::Length(oldNormal)*.5f,D::Length(newNormal)*.5f,D::Dot(oldNormal,newNormal));
				for(auto i:t.v){D::Vec a=toCar.Point(rest.vertices[i].pos),b=toCar.Point(mesh.vertices[i].pos);std::printf("  v%u mask%u rest(%.6f %.6f %.6f) after(%.6f %.6f %.6f)\n",i,mesh.vertices[i].mask,a.x,a.y,a.z,b.x,b.y,b.z);}
			}
			Check(D::Dot(oldNormal,newNormal)>0,"real painted triangle winding does not invert under dent");
		}
	}
}
struct DentStats {unsigned calls=0,backtracks=0,rollbacks=0;};
static void ApplyEvent(D::Mesh &mesh,D::Transform const&toCar,D::Transform const&fromCar,D::Vec hit,D::Vec inward,float radius,float depth,DentStats &stats){
	std::vector<D::DentSnapshot> before;before.reserve(mesh.vertices.size());
	for(auto const&v:mesh.vertices)before.push_back(D::SnapshotVertex(v));
	unsigned changed=0;for(auto &v:mesh.vertices)if(D::DentVertex(v,toCar,fromCar,hit,inward,radius,depth))++changed;
	stats.calls+=changed;if(!changed)return;
	// Match the production job: validate the entire proposed event, halve that
	// event's delta at most eight times, then discard the uncommitted geometry.
	for(unsigned attempt=0;;++attempt){
		bool valid=true;for(auto const&t:mesh.triangles)if(!D::ValidDentTriangle(mesh,t,before)){valid=false;break;}
		if(valid)break;
		if(attempt==8){for(size_t i=0;i<mesh.vertices.size();++i)D::BacktrackDentVertex(mesh.vertices[i],before[i],0);++stats.rollbacks;break;}
		for(size_t i=0;i<mesh.vertices.size();++i)D::BacktrackDentVertex(mesh.vertices[i],before[i],.5f);
		++stats.backtracks;
	}
	for(auto const&t:mesh.triangles)Check(D::ValidDentTriangle(mesh,t,before),"every committed actual triangle preserves rest and previous orientation");
}
static void Exercise(std::string const&name,Model const&model,Atomic const&atomic){
	auto const&frame=model.frames[size_t(atomic.frame)];auto const&geometry=model.geometries[size_t(atomic.geometry)];
	D::Mesh mesh=geometry.mesh;D::Transform toCar=ToCar(model,atomic.frame),fromCar;Check(toCar.Inverse(fromCar),"component-to-car transform invertible");
	const bool bonnet=frame.name=="bonnet_hi_ok";
	for(int pass=0;pass<3;++pass)D::Subdivide(mesh,toCar,.45f,12000,24000);
	ValidateSubdivision(geometry.mesh,mesh);
	unsigned protectedCount=0;for(auto const&v:mesh.vertices)if(v.mask!=1)++protectedCount;
	for(int direction=0;direction<2;++direction){
		D::Mesh deformed=mesh;D::Vec hit;float score=-std::numeric_limits<float>::infinity();
		for(auto const&t:mesh.triangles){
			if(mesh.vertices[t.v[0]].mask!=1||mesh.vertices[t.v[1]].mask!=1||mesh.vertices[t.v[2]].mask!=1)continue;
			D::Vec center=toCar.Point((mesh.vertices[t.v[0]].rest+mesh.vertices[t.v[1]].rest+mesh.vertices[t.v[2]].rest)*(1.f/3));
			float candidate=direction==0?center.y-2*std::fabs(center.x):-center.x-.3f*std::fabs(center.y);
			if(candidate>score){score=candidate;hit=center;}
		}
		Check(std::isfinite(score),"real component has deformable paint faces");
		D::Vec inward=direction==0?D::Vec(0,-1,0):D::Vec(1,0,0);DentStats stats;
		std::printf("Checking %s %s %s hit %.3f,%.3f,%.3f\n",name.c_str(),frame.name.c_str(),direction?"side":"front",hit.x,hit.y,hit.z);
		for(int impact=0;impact<12;++impact){ApplyEvent(deformed,toCar,fromCar,hit,inward,.85f,.24f,stats);ValidateDent(mesh,deformed,toCar);}
		float retained=0;unsigned moved=0;for(auto const&v:deformed.vertices){float d=D::Length(toCar.Vector(v.pos-v.rest));retained=std::max(retained,d);if(d>.00001f)++moved;}
		Check(moved>0&&retained>.001f,"real front/side pole impact retains a visible paint dent after topology safety");
		std::printf("%s %s %s vertices%zu->%zu triangles%zu->%zu moved%u protected%u backtracks%u cancelled%u retainedDepth%.4f\n",name.c_str(),frame.name.c_str(),direction?"side":"front",geometry.mesh.vertices.size(),mesh.vertices.size(),geometry.mesh.triangles.size(),mesh.triangles.size(),moved,protectedCount,stats.backtracks,stats.rollbacks,retained);
	}
	if(bonnet){
		D::Vec hit;float best=0;
		for(auto const&t:geometry.mesh.triangles){
			if(geometry.mesh.vertices[t.v[0]].mask!=1||geometry.mesh.vertices[t.v[1]].mask!=1||geometry.mesh.vertices[t.v[2]].mask!=1)continue;
			float area=Area(geometry.mesh,t);if(area>best){best=area;hit=toCar.Point((geometry.mesh.vertices[t.v[0]].rest+geometry.mesh.vertices[t.v[1]].rest+geometry.mesh.vertices[t.v[2]].rest)*(1.f/3));}
		}
		Check(best>0,"sparse bonnet has real paint triangle");float nearest=100;
		for(auto const&v:geometry.mesh.vertices)if(v.mask==1)nearest=std::min(nearest,D::Length(toCar.Point(v.rest)-hit));
		float radius=nearest*.7f;Check(radius>.01f,"sparse bonnet impact lies between original vertices");
		D::Mesh before=geometry.mesh,after=mesh;DentStats oldStats,newStats;unsigned oldMoved=0,newMoved=0;
		ApplyEvent(before,toCar,fromCar,hit,D::Vec(0,0,-1),radius,.06f,oldStats);
		ApplyEvent(after,toCar,fromCar,hit,D::Vec(0,0,-1),radius,.06f,newStats);
		for(auto const&v:before.vertices)oldMoved+=D::Length(v.pos-v.rest)>.00001f?1u:0u;
		for(auto const&v:after.vertices)newMoved+=D::Length(v.pos-v.rest)>.00001f?1u:0u;
		Check(oldMoved==0&&newMoved>0,"subdivision makes an actual sparse-bonnet local dent possible");ValidateDent(mesh,after,toCar);
		std::printf("%s sparse bonnet local pole radius%.4f affected paint%u->%u\n",name.c_str(),radius,oldMoved,newMoved);
	}
}
int main(){try{
	const char *data=std::getenv("VC_DEFORMATION_TEST_GAMEDATA");Check(data&&*data,"test runner supplies user-owned game data path");
	for(std::string name:{"admiral.dff","sentinel.dff","stinger.dff","oceanic.dff"}){
		Reader reader=ArchiveModel(data,name);Model model=ReadModel(reader);unsigned count=0;
		for(auto const&atomic:model.atomics){auto const&frame=model.frames[size_t(atomic.frame)];if(frame.name=="chassis_hi"||frame.name=="bonnet_hi_ok"){Exercise(name,model,atomic);++count;}}
		Check(count==2,"classic model chassis and sparse bonnet actually exercised");
	}
	std::printf("Actual local DFF deformation: %u checks PASS; no model/texture assets exported\n",checks);return 0;
}catch(std::exception const&e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
