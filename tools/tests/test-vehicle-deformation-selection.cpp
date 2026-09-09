// Reuse the read-only local IMG/DFF parser; its standalone test main is not run.
#define main RealMeshSuiteMain
#include "test-vehicle-deformation-realmesh.cpp"
#undef main

struct Bound { D::Vec center; float radius; };
static void ReadRenderedTriangles(Reader const&r,Model &model) {
	Chunk clump{16,r.U32(8),12,12+r.U32(4)};
	Chunk list=r.Child(clump,26);size_t geometry=0;
	for(Chunk c:r.Children(list.body,list.end)) if(c.id==15) {
		Geometry &g=model.geometries[geometry++];
		Chunk extension=r.Child(c,3);bool decoded=false;
		for(Chunk plugin:r.Children(extension.body,extension.end))if(plugin.id==0x50e) {
			uint32_t flags=r.U32(plugin.body),meshes=r.U32(plugin.body+4),total=r.U32(plugin.body+8);
			Check(flags<=1&&meshes<=128&&total<=720000,"actual rendered BinMesh bounded admission");
			g.mesh.triangles.clear();for(auto&v:g.mesh.vertices)v.mask=0;
			size_t p=plugin.body+12;uint32_t counted=0;
			for(uint32_t mesh=0;mesh<meshes;mesh++) {
				uint32_t count=r.U32(p),material=r.U32(p+4);p+=8;
				Check(material<g.paint.size()&&count<=total-counted,"actual BinMesh material/index count valid");counted+=count;
				std::vector<uint16_t> indices;indices.reserve(count);
				// librw casts the serialized int32 index to uint16; modern exporters
				// sign-extend values >=32768. Match the native loader exactly.
				for(uint32_t i=0;i<count;i++,p+=4){uint16_t index=uint16_t(r.U32(p));Check(index<g.mesh.vertices.size(),"actual rendered index in range");indices.push_back(index);}
				for(size_t offset=0;offset+2<count;offset+=flags?1:3) {
					D::Triangle t;if(!D::RenderTriangle(indices.data(),indices.size(),offset,flags==1,uint16_t(material),t))continue;
					g.mesh.triangles.push_back(t);D::MarkTriangle(g.mesh,t,g.paint[material]);
				}
			}
			Check(counted==total&&p==plugin.end,"actual rendered BinMesh fully decoded");decoded=true;
		}
		Check(decoded,"actual model render BinMesh exists");
	}
}
static std::vector<Bound> ReadBounds(Reader const&r) {
	Chunk clump{16,r.U32(8),12,12+r.U32(4)};
	Chunk list=r.Child(clump,26);std::vector<Bound> bounds;
	for(Chunk c:r.Children(list.body,list.end)) if(c.id==15) {
		Chunk s=r.Child(c,1);uint32_t flags=r.U32(s.body),nt=r.U32(s.body+4),nv=r.U32(s.body+8);
		unsigned sets=(flags>>16)&255;if(!sets)sets=flags&128?2:flags&4?1:0;
		size_t p=s.body+16+(Version(s.version)<0x34000?12:0)+(flags&8?nv*4:0)+nv*8*sets+nt*8;
		bounds.push_back({r.Vec(p),r.Float(p+12)});
	}
	return bounds;
}
static bool Body(Model const&m,int index) {
	bool body=false;
	for(unsigned depth=0;index>=0 && depth<16;depth++) {
		const Frame&frame=m.frames[size_t(index)];
		unsigned role=D::BodyNameRole(frame.name.c_str());
		if(role==2) return false;
		body|=role==1;index=frame.parent;
	}
	return body;
}
static bool NativeVisible(Model const&m,Atomic const&a) {
	const std::string &name=m.frames[size_t(a.frame)].name;
	// Model preprocessing removes _lo/_vlo and hides _dam until a native hit.
	return name.find("_dam")==std::string::npos && name.find("_lo")==std::string::npos &&
	       name.find("_vlo")==std::string::npos && Body(m,a.frame);
}
struct Selected { const Atomic *atomic;float score;D::Transform transform; };
static int NativeSelectionFrame(Model const&m,int frame) {
	// Native PreprocessHierarchy destroys these child frames and reattaches
	// component atomics to the dummy without modifying geometry coordinates.
	for(int parent=m.frames[size_t(frame)].parent;parent>=0;parent=m.frames[size_t(parent)].parent) {
		const std::string &name=m.frames[size_t(parent)].name;
		if(name.find("_dummy")!=std::string::npos && (name.find("door_")==0 || name.find("wing_")==0 ||
		   name.find("bump_")==0 || name=="bonnet_dummy" || name=="boot_dummy" || name=="windscreen_dummy"))return parent;
	}
	return frame;
}
static bool NativeDamageVisible(Model const&m,Atomic const&a,bool damaged) {
	if(!damaged)return NativeVisible(m,a);
	const std::string &name=m.frames[size_t(a.frame)].name;
	if(name.find("_lo")!=std::string::npos || name.find("_vlo")!=std::string::npos || !Body(m,a.frame))return false;
	if(name.find("_ok")==std::string::npos)return true;
	int frame=NativeSelectionFrame(m,a.frame);
	for(const Atomic &other:m.atomics)
		if(m.frames[size_t(other.frame)].name.find("_dam")!=std::string::npos && NativeSelectionFrame(m,other.frame)==frame)return false;
	return true;
}
static std::vector<Selected> RankPanels(Model const&m,const std::vector<Bound>&bounds,D::Vec hit,D::Vec inward,bool damaged,bool legacy) {
	std::vector<Selected> selected;Selected upper={nullptr,1.0e20f,D::Transform()};
	for(const Atomic&a:m.atomics) if(NativeDamageVisible(m,a,damaged)) {
		const Geometry &g=m.geometries[size_t(a.geometry)];
		if(std::find(g.paint.begin(),g.paint.end(),true)==g.paint.end())continue;
		int frame=NativeSelectionFrame(m,a.frame);D::Transform t=ToCar(m,frame);
		const Bound&b=bounds[size_t(a.geometry)];
		float radius=b.radius*std::max(D::Length(t.right),std::max(D::Length(t.forward),D::Length(t.up)));
		D::Vec center=t.Point(b.center);float distance=D::Length(center-hit);
		float score=legacy?(distance>radius+.85f?1.0e20f:std::max(0.f,distance-radius)*4.f+distance*.25f+radius*.1f):D::PanelScore(center,radius,hit);
		if(score<1.0e20f)selected.push_back({&a,score,t});
		if(!legacy) for(unsigned depth=0;frame>=0&&depth<16;depth++) {
			const Frame &f=m.frames[size_t(frame)];
			if(D::IsUpperImpactPanel(f.name.c_str(),hit,inward) && (!upper.atomic || score<upper.score))upper={&a,score,t};
			frame=f.parent;
		}
	}
	std::stable_sort(selected.begin(),selected.end(),[](const Selected&a,const Selected&b){return a.score<b.score;});
	if(selected.size()>3)selected.resize(3);
	if(upper.atomic && std::find_if(selected.begin(),selected.end(),[&](const Selected&s){return s.atomic==upper.atomic;})==selected.end()) {
		if(selected.size()==3)selected.back()=upper;else selected.push_back(upper);
	}
	Check(selected.size()<=3,"upper panel reservation retains three-atomic budget");
	return selected;
}
static void SelectionScenario(const char *name,Model const&m,const std::vector<Bound>&bounds,int direction) {
	D::Vec hit;float best=-1.0e20f;
	for(const Atomic&a:m.atomics) if(NativeVisible(m,a)) {
		const Geometry &g=m.geometries[size_t(a.geometry)];D::Transform transform=ToCar(m,NativeSelectionFrame(m,a.frame));
		for(const auto&t:g.mesh.triangles) {
			if(g.mesh.vertices[t.v[0]].mask!=1 || g.mesh.vertices[t.v[1]].mask!=1 || g.mesh.vertices[t.v[2]].mask!=1)continue;
			D::Vec center=transform.Point((g.mesh.vertices[t.v[0]].rest+g.mesh.vertices[t.v[1]].rest+g.mesh.vertices[t.v[2]].rest)*(1.0f/3));
			float score=direction==0?center.y-3*std::fabs(center.x):direction==1?-center.y-3*std::fabs(center.x):
			            direction==2?center.x-0.3f*std::fabs(center.y):-center.x-0.3f*std::fabs(center.y);
			if(score>best){best=score;hit=center;}
		}
	}
	Check(best>-1.0e19f,"actual visible car has deformable paint faces");
	D::Vec inward=direction==0?D::Vec(0,-1,0):direction==1?D::Vec(0,1,0):direction==2?D::Vec(-1,0,0):D::Vec(1,0,0);
	std::vector<Selected> selected=RankPanels(m,bounds,hit,inward,false,false);
	Check(!selected.empty(),"three-candidate actual sphere selection finds a panel");
	unsigned moved=0;float deepest=0;
	std::printf("%s %s selected",name,direction==0?"front":direction==1?"rear":direction==2?"right":"left");
	for(const Selected&s:selected) {
		const Geometry &g=m.geometries[size_t(s.atomic->geometry)];D::Mesh mesh=g.mesh;
		if(mesh.vertices.size()<=4000 && mesh.triangles.size()<=6000)
			for(int pass=0;pass<3;pass++)if(!D::Subdivide(mesh,s.transform,0.45f,12000,24000))break;
		unsigned affected=0;
		for(const auto&v:mesh.vertices) if(v.mask==1) {
			float distance=D::Length(s.transform.Point(v.pos)-hit);
			if(distance<0.85f){affected++;float q=1-distance*distance/(0.85f*0.85f);deepest=std::max(deepest,0.24f*q*q);}
		}
		moved+=affected;
		std::printf(" %s(%u)",m.frames[size_t(s.atomic->frame)].name.c_str(),affected);
	}
	std::printf(" availableDepth=%.3fm\n",deepest);
	Check(moved>0 && deepest>0.015f,"selected actual panels contain nearby movable paint despite protected materials");
}
static void ReportedAdmiralImpacts(Model const&m,const std::vector<Bound>&bounds) {
	for(bool damaged:{false,true})for(D::Vec hit:{D::Vec(.73f,2.24f,-.15f),D::Vec(-.85f,2.30f,-.14f)}) {
		D::Vec inward(0,-1,0);
		auto legacy=RankPanels(m,bounds,hit,inward,damaged,true),selected=RankPanels(m,bounds,hit,inward,damaged,false);
		auto bonnet=[&](const Selected&s){return m.frames[size_t(s.atomic->frame)].name.find("bonnet")!=std::string::npos;};
		Check(std::find_if(legacy.begin(),legacy.end(),bonnet)==legacy.end(),"installed baseline misses modern Admiral bonnet at both recorded hits");
		auto found=std::find_if(selected.begin(),selected.end(),bonnet);
		Check(found!=selected.end(),"reservation includes actual visible bonnet despite malformed authored sphere");
		Check(selected.size()==3,"actual modern collision keeps fixed three-candidate budget");
		const Selected&s=*found;const Atomic&a=*s.atomic;const Bound&bound=bounds[size_t(a.geometry)];
		const std::string &name=m.frames[size_t(a.frame)].name;
		Check(name.find(damaged?"_dam":"_ok")!=std::string::npos,"reservation follows native OK/DAM visibility state");
		D::Mesh mesh=m.geometries[size_t(a.geometry)].mesh;D::Transform inverse;Check(s.transform.Inverse(inverse),"selected bonnet transform invertible");
		D::Vec center=s.transform.Point(bound.center);float nearest=100,furthest=0;
		for(const auto&v:mesh.vertices) {
			D::Vec p=s.transform.Point(v.pos);furthest=std::max(furthest,D::Length(p-center));
			if(v.mask==1)nearest=std::min(nearest,D::Length(p-hit));
		}
		float radius=bound.radius*std::max(D::Length(s.transform.right),std::max(D::Length(s.transform.forward),D::Length(s.transform.up)));
		Check(furthest>radius+1.0f && nearest<.2f,"real authored bonnet bound misses nearby mesh by over one meter");
		if(mesh.vertices.size()<=4000&&mesh.triangles.size()<=6000)
			for(int pass=0;pass<3;pass++)if(!D::Subdivide(mesh,s.transform,.45f,12000,24000))break;
		std::vector<D::DentSnapshot> before;for(const auto&v:mesh.vertices)before.push_back(D::SnapshotVertex(v));
		for(auto&v:mesh.vertices)D::DentVertex(v,s.transform,inverse,hit,inward,.85f,.24f);
		auto valid=[&](){for(const auto&t:mesh.triangles)if(!D::ValidDentTriangle(mesh,t,before))return false;return true;};
		unsigned halvings=0;while(!valid()&&halvings<8) {
			for(size_t i=0;i<mesh.vertices.size();i++)D::BacktrackDentVertex(mesh.vertices[i],before[i],.5f);
			halvings++;
		}
		Check(valid(),"actual reserved bonnet dent survives production topology guard");
		float height=0;unsigned moved=0;
		for(size_t i=0;i<mesh.vertices.size();i++) {
			const auto&v=mesh.vertices[i];D::Vec delta=s.transform.Vector(v.pos-before[i].pos);
			Check(D::Finite(v.pos)&&D::Finite(v.normal),"actual reserved bonnet position and normal finite");
			if(v.mask!=1)Check(D::Length(delta)<.00001f,"protected bonnet vertices stay fixed");
			if(D::Length(delta)>.0001f)moved++;
			height=std::max(height,std::fabs(delta.z));
		}
		Check(height>.08f && moved>50,"recorded frontal hit produces actual bonnet height change beyond in-plane sliding");
		std::printf("modern/admiral recorded(%.2f,%.2f,%.2f) %s nearest%.3f sphereGap%.3f vertices%u zChange%.3fm halves%u; legacy missing, reserved within3\n",hit.x,hit.y,hit.z,name.c_str(),nearest,D::Length(center-hit)-radius,moved,height,halvings);
	}
}
int main(){try{
	const char *data=std::getenv("VC_DEFORMATION_TEST_GAMEDATA");Check(data&&*data,"local game data supplied");
	for(const char*name:{"chassis_hi","bonnet_hi_ok","bump_front_dummy","door_lf_dummy"})Check(D::BodyNameRole(name)==1,"paint body node allowlist");
	for(const char*name:{"wheel_rf_dummy","steering_wheel","windscreen_hi_ok","seat_front","extra1","chassis_vlo","glass"})Check(D::BodyNameRole(name)==2,"protected node denylist overrides chassis ancestor");
	Check(D::IsUpperImpactPanel("bonnet_dummy",D::Vec(0,2,0),D::Vec(0,-1,0)),"frontal hit reserves bonnet");
	Check(D::IsUpperImpactPanel("boot_dummy",D::Vec(0,-2,0),D::Vec(0,1,0)),"rear hit reserves boot");
	Check(!D::IsUpperImpactPanel("bonnet_dummy",D::Vec(0,2,0),D::Vec(-1,0,0)),"side hit keeps spatial selection");
	Check(!D::IsUpperImpactPanel("boot_dummy",D::Vec(0,2,0),D::Vec(0,-1,0)),"frontal hit cannot reserve opposite boot");
	Check(!D::IsUpperImpactPanel("bonnet_dummy",D::Vec(0,-2,0),D::Vec(0,-1,0)),"rear position cannot reserve front bonnet");
	Check(!D::IsUpperImpactPanel("door_rf_dummy",D::Vec(0,2,0),D::Vec(0,-1,0)),"irrelevant door is never upper-panel reservation");
	for(const char*set:{"","/modelsets/modern"}) for(const char*name:{"admiral.dff","sentinel.dff","stinger.dff","oceanic.dff"}) {
		Reader r=ArchiveModel(std::string(data)+set,name);Model m=ReadModel(r);auto bounds=ReadBounds(r);
		ReadRenderedTriangles(r,m);
		Check(bounds.size()==m.geometries.size(),"actual native bounding sphere order matches geometry");
		for(int direction=0;direction<4;direction++)SelectionScenario((std::string(set)+"/"+name).c_str(),m,bounds,direction);
		if(*set && std::strcmp(name,"admiral.dff")==0)ReportedAdmiralImpacts(m,bounds);
	}
	std::printf("PASS: %u actual local DFF selection checks; 32 front/rear/side scenarios plus4 recorded modern Admiral OK/DAM regressions, no assets exported.\n",checks);return 0;
}catch(const std::exception&e){std::fprintf(stderr,"FAILED selection: %s\n",e.what());return 1;}}
