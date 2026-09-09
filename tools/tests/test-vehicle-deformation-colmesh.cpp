// Local-only diagnostic: read the user's COL/DFF at runtime. No model vertices
// or collision geometry are embedded in this test or exported to an asset file.
#include "vehicle-deformation-selection-reader.inc"

struct SphereContact { D::Vec center;float radius;unsigned index; };
struct CarCollision { std::string name;D::Vec lo,hi;std::vector<SphereContact>spheres; };
static CarCollision ReadCarCollision(const std::string&path,const std::string&name) {
	std::ifstream in(path,std::ios::binary);Check(bool(in),"local vehicles.col readable");
	Reader r;r.data.assign(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());
	for(size_t start=0;start<r.data.size();) {
		Check(r.U32(start)==0x4c4c4f43,"native COLL signature");size_t end=start+8+r.U32(start+4);
		Check(end<=r.data.size()&&end>start+72,"native COL record range");
		if(r.Text(start+8,22)==name) {
			CarCollision car;car.name=name;car.lo=r.Vec(start+48);car.hi=r.Vec(start+60);
			size_t p=start+72;unsigned count=r.U16(p);p+=4;Check(count<=64,"bounded classic vehicle collision spheres");
			for(unsigned i=0;i<count;i++,p+=20)car.spheres.push_back({r.Vec(p+4),r.Float(p),i});
			Check(r.U16(p)==0&&r.U16(p+4)==0,"selected car has no line/box primitives");
			return car;
		}
		start=end;
	}
	throw std::runtime_error("classic car COL record not found");
}
struct Contact { D::Vec point,inward;unsigned sphere;float firstTouch; };
static Contact WallContact(const CarCollision&car,int axis,float sign) {
	Contact result;result.firstTouch=-1.0e20f;
	D::Vec out=axis==0?D::Vec(sign,0,0):D::Vec(0,sign,0);
	for(const SphereContact&s:car.spheres) {
		float support=D::Dot(s.center,out)+s.radius;
		if(support>result.firstTouch)result={s.center+out*s.radius,out*-1.0f,s.index,support};
	}
	return result;
}
static Contact PoleContact(const CarCollision&car,int axis,float sign,float offset) {
	// First touch with a vertical 10cm-radius cylinder. Its infinite height makes
	// the sphere center's Z the exact sphere-cylinder contact height.
	Contact result;result.firstTouch=-1.0e20f;const float poleRadius=0.10f;
	for(const SphereContact&s:car.spheres) {
		float along=axis==0?s.center.x:s.center.y,side=axis==0?s.center.y:s.center.x;
		float d=offset-side,r=s.radius+poleRadius;
		if(std::fabs(d)>=r)continue;
		float advance=std::sqrt(r*r-d*d),support=along*sign+advance;
		if(support>result.firstTouch) {
			D::Vec outward=axis==0?D::Vec(sign*advance/r,d/r,0):D::Vec(d/r,sign*advance/r,0);
			result={s.center+outward*s.radius,outward*-1.0f,s.index,support};
		}
	}
	Check(result.firstTouch>-1.0e19f,"pole trajectory intersects actual collision sphere");return result;
}
static int CollapsedFrame(const Model&m,int frame) {
	// CVehicleModelInfo::PreprocessHierarchy uses VEHICLE_FLAG_COLLAPSE for these
	// native components; MoveObjectsCB simply reattaches atomics to that dummy.
	int parent=m.frames[size_t(frame)].parent;
	for(unsigned depth=0;parent>=0&&depth<16;depth++) {
		const Frame&f=m.frames[size_t(parent)];
		bool component=f.name.find("_dummy")!=std::string::npos &&
			(f.name.find("door_")==0||f.name.find("wing_")==0||f.name.find("bump_")==0||
			 f.name=="bonnet_dummy"||f.name=="boot_dummy"||f.name=="windscreen_dummy");
		if(component)return parent;parent=f.parent;
	}
	return frame;
}
struct Panel {std::string name;D::Mesh mesh;D::Transform toCar,fromCar;D::Vec center;float radius;};
static std::vector<Panel> Panels(const Model&m,const std::vector<Bound>&bounds) {
	std::vector<Panel> panels;
	for(const Atomic&a:m.atomics)if(NativeVisible(m,a)) {
		const Geometry&g=m.geometries[size_t(a.geometry)];
		if(std::find(g.paint.begin(),g.paint.end(),true)==g.paint.end())continue;
		Panel panel;panel.name=m.frames[size_t(a.frame)].name;panel.mesh=g.mesh;
		panel.toCar=ToCar(m,CollapsedFrame(m,a.frame));Check(panel.toCar.Inverse(panel.fromCar),"native component frame invertible");
		const Bound&b=bounds[size_t(a.geometry)];panel.center=panel.toCar.Point(b.center);
		panel.radius=b.radius*std::max(D::Length(panel.toCar.right),std::max(D::Length(panel.toCar.forward),D::Length(panel.toCar.up)));
		if(panel.mesh.vertices.size()<=4000&&panel.mesh.triangles.size()<=6000)
			for(int pass=0;pass<3;pass++)if(!D::Subdivide(panel.mesh,panel.toCar,.45f,12000,24000))break;
		panels.push_back(panel);
	}
	return panels;
}
static float StableDent(const Panel&panel,const Contact&contact,float depth,unsigned&halvings,unsigned&cancelled) {
	D::Mesh mesh=panel.mesh;std::vector<D::DentSnapshot> before;
	for(const auto&v:mesh.vertices)before.push_back(D::SnapshotVertex(v));
	for(auto&v:mesh.vertices)D::DentVertex(v,panel.toCar,panel.fromCar,contact.point,contact.inward,.85f,depth);
	auto valid=[&](){for(const auto&t:mesh.triangles)if(!D::ValidDentTriangle(mesh,t,before))return false;return true;};
	unsigned iteration=0;
	while(!valid()&&iteration<8) {
		for(size_t i=0;i<mesh.vertices.size();i++)D::BacktrackDentVertex(mesh.vertices[i],before[i],.5f);
		iteration++;halvings++;
	}
	if(!valid()){cancelled++;return 0;}
	float deepest=0;for(size_t i=0;i<mesh.vertices.size();i++)deepest=std::max(deepest,D::Length(panel.toCar.Vector(mesh.vertices[i].pos-before[i].pos)));
	return deepest;
}
struct Summary {unsigned contacts=0,zeroField=0,subCentimeter=0,cancelled=0;float smallestField=1,smallestStable=1,largestDistance=0;};
static void Measure(const std::string&car,const char*scenario,const Contact&contact,const std::vector<Panel>&panels,Summary&summary) {
	std::vector<std::pair<float,size_t>> selected;
	float allNearest=1.0e20f;
	for(size_t i=0;i<panels.size();i++) {
		const Panel&p=panels[i];float score=D::PanelScore(p.center,p.radius,contact.point);
		if(score<1.0e20f)selected.push_back({score,i});
		for(const auto&v:p.mesh.vertices)if(v.mask==1)allNearest=std::min(allNearest,D::Length(p.toCar.Point(v.pos)-contact.point));
	}
	std::stable_sort(selected.begin(),selected.end());if(selected.size()>3)selected.resize(3);
	float nearest=1.0e20f,raw=0,stable=0,stableSmall=0;unsigned halvings=0,cancelled=0,moved=0;
	std::string names;
	for(const auto&candidate:selected) {
		const Panel&p=panels[candidate.second];names+=(names.empty()?"":"+")+p.name;
		for(const auto&v:p.mesh.vertices)if(v.mask==1) {
			float distance=D::Length(p.toCar.Point(v.pos)-contact.point);nearest=std::min(nearest,distance);
			if(distance<.85f){moved++;float q=1-distance*distance/(.85f*.85f);raw=std::max(raw,.24f*q*q);}
		}
		stable=std::max(stable,StableDent(p,contact,.24f,halvings,cancelled));
		unsigned smallHalvings=0,smallCancelled=0;
		stableSmall=std::max(stableSmall,StableDent(p,contact,.08f,smallHalvings,smallCancelled));
	}
	summary.contacts++;summary.zeroField+=moved==0?1u:0u;summary.subCentimeter+=stable<.01f?1u:0u;summary.cancelled+=cancelled;
	summary.smallestField=std::min(summary.smallestField,raw);summary.smallestStable=std::min(summary.smallestStable,stable);summary.largestDistance=std::max(summary.largestDistance,nearest);
	std::printf("%s %-16s sphere%u COL(%.3f,%.3f,%.3f) nearestAll%.3f selected%.3f fieldMax%.4f stable24%.4f stable08%.4f halves%u cancelled%u candidates=%s\n",car.c_str(),scenario,contact.sphere,contact.point.x,contact.point.y,contact.point.z,allNearest,nearest,raw,stable,stableSmall,halvings,cancelled,names.c_str());
}
int main(){try{
	const char*data=std::getenv("VC_DEFORMATION_TEST_GAMEDATA");Check(data&&*data,"local game data provided");Summary summary;
	for(const char*car:{"admiral","sentinel","stinger","oceanic"}) {
		Reader reader=ArchiveModel(data,std::string(car)+".dff");Model model=ReadModel(reader);ReadRenderedTriangles(reader,model);auto panels=Panels(model,ReadBounds(reader));
		CarCollision col=ReadCarCollision(std::string(data)+"/models/coll/vehicles.col",car);
		Measure(car,"front-wall",WallContact(col,1,1),panels,summary);
		Measure(car,"right-wall",WallContact(col,0,1),panels,summary);
		Measure(car,"left-wall",WallContact(col,0,-1),panels,summary);
		for(float offset:{-.70f,0.f,.70f}) {char scenario[48];std::snprintf(scenario,sizeof(scenario),"front-pole-x%+.2f",offset);Measure(car,scenario,PoleContact(col,1,1,offset),panels,summary);}
		for(float offset:{-1.8f,0.f,1.8f}) {char scenario[48];std::snprintf(scenario,sizeof(scenario),"right-pole-y%+.1f",offset);Measure(car,scenario,PoleContact(col,0,1,offset),panels,summary);}
	}
	std::printf("COL-vs-render diagnostic: %u contacts; zeroField%u stableBelow1cm%u cancelledPanels%u maxNearest%.4fm minimumField%.5fm minimumStable%.5fm; %u parse/math checks. No headset motion/performance claim.\n",summary.contacts,summary.zeroField,summary.subCentimeter,summary.cancelled,summary.largestDistance,summary.smallestField,summary.smallestStable,checks);return 0;
}catch(const std::exception&e){std::fprintf(stderr,"FAILED COL-vs-render: %s\n",e.what());return 1;}}
