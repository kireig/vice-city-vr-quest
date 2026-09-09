#include "vehicle-deformation-mesh-production.inc"
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <initializer_list>
using namespace VehicleDeformationDetail;
static unsigned checks=0;
static void Check(bool condition,const char *message) {
	++checks;
	if(!condition) { std::fprintf(stderr,"FAILED: %s (check %u)\n",message,checks); std::exit(1); }
}
static bool Close(Vec a,Vec b,float eps=0.0001f) { return Length(a-b)<eps; }
static Mesh Quad() {
	Mesh m;
	for(Vec p:{Vec(-1,-1,0),Vec(1,-1,0),Vec(1,1,0),Vec(-1,1,0)}) {
		Vertex v; v.pos=v.rest=p; v.normal=Vec(0,0,1);v.mask=1;m.vertices.push_back(v);
		m.colours.push_back({uint8_t((p.x+1)*100),uint8_t((p.y+1)*100),100,255});
	}
	m.uv.resize(2);
	for(const Vertex&v:m.vertices) {m.uv[0].push_back({v.pos.x,v.pos.y});m.uv[1].push_back({v.pos.x*2,v.pos.y*3});}
	m.triangles={{{0,1,2},0},{{0,2,3},0}}; return m;
}
static float Area(const Mesh&m) {
	float area=0;
	for(const Triangle&t:m.triangles) {
		Check(t.v[0]<m.vertices.size() && t.v[1]<m.vertices.size() && t.v[2]<m.vertices.size(),"valid triangle indices");
		Vec a=m.vertices[t.v[0]].pos,b=m.vertices[t.v[1]].pos,c=m.vertices[t.v[2]].pos;
		Vec cross=Cross(b-a,c-a);Check(cross.z>0,"preserve winding/nondegenerate triangle");area+=cross.z*0.5f;
	}
	return area;
}
static void TestTransforms() {
	for(unsigned i=0;i<60;i++) {
		float angle=float(i)*0.1f;Transform a,b;
		a.right=Vec(std::cos(angle),std::sin(angle),0)*1.7f;
		a.forward=Vec(-std::sin(angle),std::cos(angle),0)*0.6f;
		a.up=Vec(0,0,1.3f);a.pos=Vec(352,-1042,17);
		Check(a.Inverse(b),"scaled atomic matrix invertible");
		Vec p(float(i)*0.01f,-2,0.25f);
		Check(Close(b.Point(a.Point(p)),p,0.0004f),"scaled transform roundtrip");
		Transform root; root.pos=Vec(-3,15,42);Transform inv;
		Check(root.Inverse(inv),"root inverse");
		Check(Close((inv*(root*a)).Point(p),a.Point(p),0.0004f),"stale RW world transform cancels");
	}
	Transform broken,inv;broken.right=Vec();Check(!broken.Inverse(inv),"singular atomic rejected");
}
static void TestSubdivision() {
	Mesh m=Quad();Transform identity;
	for(int i=0;i<3;i++) Check(Subdivide(m,identity,0.45f,12000,24000),"classic panel refinement");
	Check(std::fabs(Area(m)-4.0f)<0.0001f,"subdivision preserves total surface area");
	for(const Vertex &v:m.vertices) Check(Close(v.pos,v.rest),"subdivision preserves rest position");
	for(size_t i=0;i<m.vertices.size();i++) {
		Vec p=m.vertices[i].pos;
		Check(std::fabs(m.uv[0][i].u-p.x)<0.0001f && std::fabs(m.uv[0][i].v-p.y)<0.0001f,"UV0 interpolated");
		Check(std::fabs(m.uv[1][i].u-p.x*2)<0.0001f && std::fabs(m.uv[1][i].v-p.y*3)<0.0001f,"UV1 interpolated");
		Check(m.colours[i].a==255 && m.colours[i].b==100,"vertex alpha/colour retained");
		Check(Close(m.vertices[i].normal,Vec(0,0,1)),"authored normal interpolation");
	}
	// Any vertex lying strictly within a triangle edge would be a T-junction.
	for(const Triangle&t:m.triangles) for(int edge=0;edge<3;edge++) {
		Vec a=m.vertices[t.v[edge]].pos,b=m.vertices[t.v[(edge+1)%3]].pos,d=b-a;
		for(const Vertex &v:m.vertices) {
			float fraction=Dot(v.pos-a,d)/Dot(d,d);
			Check(!(fraction>0.00001f && fraction<0.99999f && Length(v.pos-(a+d*fraction))<0.00001f),"no T-junctions");
		}
	}
	Mesh limited=Quad();Check(!Subdivide(limited,identity,0.45f,4,24000),"vertex budget rejects whole pass");
	Check(limited.vertices.size()==4 && limited.triangles.size()==2,"budget rejection leaves topology untouched");
	Check(!Subdivide(limited,identity,0.45f,12000,2),"triangle budget rejects whole pass");
	Check(limited.vertices.size()==4 && limited.triangles.size()==2,"triangle rejection remains transactional");
	// Unequal edges exercise partial split patterns as well as the four-way split.
	for(float sx:{0.25f,0.6f,1.0f,2.0f}) for(float sy:{0.25f,0.6f,1.0f,2.0f}) {
		Mesh triangle;for(Vec p:{Vec(0,0,0),Vec(sx,0,0),Vec(0,sy,0)}) {
			Vertex v;v.pos=v.rest=p;v.normal=Vec(0,0,1);v.mask=1;triangle.vertices.push_back(v);
		}
		triangle.triangles={{{0,1,2},0}};Subdivide(triangle,identity,0.8f,12000,24000);
		Check(std::fabs(Area(triangle)-sx*sy*0.5f)<0.0001f,"partial edge split preserves area/winding");
	}
}
static void TestDents() {
	Transform identity;
	Check(HitDepth(1,1000)==0,"suspension/resting force below threshold");
	Check(HitDepth(100,1000)>0.07f,"real impact yields visible dent");
	Check(HitDepth(100,1000)>HitDepth(100,2000),"equal impulse deforms lighter receiver more");
	Check(HitDepth(1.0e8f,1000)==0.24f,"event depth bounded");
	Vertex outside;outside.pos=outside.rest=Vec(1,0,0);outside.normal=Vec(0,0,1);outside.mask=1;
	Check(!DentVertex(outside,identity,identity,Vec(),Vec(0,0,-1),0.85f,0.2f),"distant body does not deform");
	for(uint8_t mask:{uint8_t(0),uint8_t(2),uint8_t(3)}) {
		Vertex glass;glass.mask=mask;glass.normal=Vec(0,0,1);
		Check(!DentVertex(glass,identity,identity,Vec(),Vec(0,0,-1),0.85f,0.2f),"protected and shared glass indices fixed");
	}
	Mesh panel=Quad();for(int pass=0;pass<3;pass++) Subdivide(panel,identity,0.45f,12000,24000);
	unsigned moved=0;float deepest=0;
	for(Vertex&v:panel.vertices) {if(DentVertex(v,identity,identity,Vec(),Vec(0,0,-1),0.85f,0.2f)) moved++; deepest=std::max(deepest,-v.pos.z);}
	Check(moved>12 && deepest>0.19f,"low-poly panel now has visible localized dent");
	for(Vertex&v:panel.vertices) {
		Vec original=v.rest;
		if(Length(original)>0.85f) Check(Close(original,v.pos),"dent preserves far silhouette");
		for(int i=0;i<100;i++) DentVertex(v,identity,identity,Vec(),Vec(0,0,-1),0.85f,0.24f);
		Check(Length(v.pos-v.rest)<=0.32001f,"repeated impacts bounded by original shell");
		Check(Finite(v.normal) && std::fabs(Length(v.normal)-1)<0.0001f,"repeated normals finite and unit");
	}
	Mesh mixed=Quad();for(Vertex&v:mixed.vertices)v.mask=0;
	MarkTriangle(mixed,mixed.triangles[0],true);MarkTriangle(mixed,mixed.triangles[1],false);
	Check(mixed.vertices[0].mask==3 && mixed.vertices[2].mask==3,"shared material seam vetoes glass movement");
	Check(mixed.vertices[1].mask==1 && mixed.vertices[3].mask==2,"body-only vertex remains editable");
	// Analytic normal must agree with geometric finite-difference surface tangents.
	for(int x=-7;x<=7;x++) for(int y=-7;y<=7;y++) {
		Vec pos(float(x)*0.06f,float(y)*0.06f,0);const float eps=0.0005f;
		Vertex a,b,c;a.pos=a.rest=pos;a.normal=Vec(0,0,1);a.mask=1;
		b=a;c=a;b.pos.x+=eps;b.rest=b.pos;c.pos.y+=eps;c.rest=c.pos;
		DentVertex(a,identity,identity,Vec(),Vec(0,0,-1),0.85f,0.2f);
		DentVertex(b,identity,identity,Vec(),Vec(0,0,-1),0.85f,0.2f);
		DentVertex(c,identity,identity,Vec(),Vec(0,0,-1),0.85f,0.2f);
		Vec geometric=Unit(Cross(b.pos-a.pos,c.pos-a.pos));
		Check(Dot(geometric,a.normal)>0.9999f,"normal matches deformed surface");
	}
}
static void TestSkinnyTriangleBacktracking() {
	Transform identity;
	unsigned rawFlips=0,backtracks=0,visible=0;
	for(int shape=0;shape<24;shape++) {
		Mesh protectedMesh;
		float thin=0.001f+float(shape)*0.0007f;
		for(Vec p:{Vec(0.75f,0.25f,0),Vec(0.73f,0.30f+thin,0),Vec(0.45f,0.45f,0)}) {
			Vertex v;v.pos=v.rest=p;v.normal=Vec(0,0,1);v.mask=1;protectedMesh.vertices.push_back(v);
		}
		protectedMesh.triangles={{{0,1,2},0}};
		Mesh naive=protectedMesh;
		for(int impact=0;impact<16;impact++) {
			std::vector<DentSnapshot> before;
			for(const Vertex&v:protectedMesh.vertices)before.push_back(SnapshotVertex(v));
			for(Vertex&v:naive.vertices)DentVertex(v,identity,identity,Vec(0.08f,0.51f,0),Vec(0,-1,0),0.85f,0.24f);
			for(Vertex&v:protectedMesh.vertices)DentVertex(v,identity,identity,Vec(0.08f,0.51f,0),Vec(0,-1,0),0.85f,0.24f);
			if(!ValidDentTriangle(naive,naive.triangles[0],before))rawFlips++;
			unsigned attempt=0;
			while(!ValidDentTriangle(protectedMesh,protectedMesh.triangles[0],before) && attempt<8) {
				for(size_t i=0;i<before.size();i++)BacktrackDentVertex(protectedMesh.vertices[i],before[i],0.5f);
				attempt++;backtracks++;
			}
			if(!ValidDentTriangle(protectedMesh,protectedMesh.triangles[0],before))
				for(size_t i=0;i<before.size();i++)BacktrackDentVertex(protectedMesh.vertices[i],before[i],0);
			Check(ValidDentTriangle(protectedMesh,protectedMesh.triangles[0],before),"skinny painted face never flips or collapses after bounded backtracking");
			for(const Vertex&v:protectedMesh.vertices)Check(Finite(v.normal)&&Length(v.pos-v.rest)<0.32001f,"backtracking respects normals and depth budget");
		}
		for(const Vertex&v:protectedMesh.vertices)if(Length(v.pos-v.rest)>0.03f)visible++;
	}
	Check(rawFlips>10,"regression fixture actually reproduces unsafe piecewise-linear field");
	Check(backtracks>10,"topology line search exercised");
	Check(visible>20,"topology protection retains visible dents");
}
static void TestFrontalPanelBuckling() {
	Transform identity;
	unsigned bent=0;
	for(float sign:{-1.0f,1.0f}) for(int x=-5;x<=5;x++) for(int y=-5;y<=5;y++) {
		Vec p(float(x)*0.07f,sign*(0.25f+float(y)*0.06f),0.45f);
		Vertex a,b,c;a.pos=a.rest=p;a.normal=Vec(0,0,1);a.mask=1;
		b=c=a;const float eps=0.0002f;b.pos.x+=eps;b.rest=b.pos;c.pos.y+=eps;c.rest=c.pos;
		for(Vertex *v:{&a,&b,&c}) DentVertex(*v,identity,identity,Vec(),Vec(0,sign,0),0.85f,0.24f);
		if(a.pos.z-p.z>0.08f) bent++;
		Vec geometric=Unit(Cross(b.pos-a.pos,c.pos-a.pos));
		Check(Dot(geometric,a.normal)>0.9999f,"frontal buckle normals match actual surface tangents");
		Check(Length(a.pos-a.rest)<=0.32001f,"upper-panel buckle retains total crush limit");
		Vertex seam=a;seam.pos=seam.rest=p;seam.normal=Vec(1,0,0);
		DentVertex(seam,identity,identity,Vec(),Vec(0,sign,0),0.85f,0.24f);
		Check(Close(a.pos,seam.pos),"normal seams receive identical deformation positions");
	}
	Check(bent>180,"frontal/rear impacts bend a flat upper panel rather than merely sliding it");
	for(Vec p:{Vec(0,0,-0.2f),Vec(0,0,0),Vec(0,2,0.45f)}) {
		Vertex v;v.pos=v.rest=p;v.normal=Vec(0,0,1);v.mask=1;
		DentVertex(v,identity,identity,Vec(),Vec(0,1,0),0.85f,0.24f);
		Check(v.pos.z==p.z,"underbody/contact-level/distant panels do not gain artificial lift");
	}
}
static void TestRenderedIndexDecoding() {
	const uint16_t strip[]={0,1,2,3,3,4,4,5,6,7};
	const uint16_t list[]={8,9,10,10,11,8};
	Triangle t;
	Check(RenderTriangle(strip,10,0,true,17,t)&&t.v[0]==0&&t.v[1]==1&&t.v[2]==2&&t.material==17,"even strip triangle uses rendered material");
	Check(RenderTriangle(strip,10,1,true,17,t)&&t.v[0]==1&&t.v[1]==3&&t.v[2]==2,"odd strip triangle alternates native winding");
	Check(!RenderTriangle(strip,10,2,true,17,t)&&!RenderTriangle(strip,10,3,true,17,t),"degenerate strip connectors omitted");
	Check(RenderTriangle(strip,10,7,true,23,t)&&t.v[0]==5&&t.v[1]==7&&t.v[2]==6,"degenerate omission does not reset winding parity");
	Check(RenderTriangle(list,6,3,false,5,t)&&t.v[0]==10&&t.v[1]==11&&t.v[2]==8&&t.material==5,"render triangle list material/index decoding");
	Check(!RenderTriangle(list,6,5,false,5,t)&&!RenderTriangle(nullptr,6,0,false,5,t),"short/null index buffer rejected");
}
int main() {
	auto start=std::chrono::steady_clock::now();
	TestTransforms();TestSubdivision();TestDents();TestSkinnyTriangleBacktracking();TestRenderedIndexDecoding();TestFrontalPanelBuckling();
	{
		Transform identity;
		Vertex v;v.mask=1;v.rest=Vec();v.pos=Vec(0,0,-.3f);v.normal=Vec(0,0,1);
		const Vertex before=v;
		Check(!DentVertex(v,identity,identity,Vec(5,0,0),Vec(0,0,-1),.85f,.24f,.1f),"lowering max dent cannot repair an old dent outside the next impact radius");
		Check(Close(v.pos,before.pos)&&Close(v.normal,before.normal),"distant old deformation remains unchanged after lowering cap");
		DentVertex(v,identity,identity,v.pos,Vec(0,0,-1),.85f,.01f,.1f);
		Check(Close(v.pos,before.pos),"weak new impact after lowering cap cannot pull existing thirty centimetre dent back to ten");
	}
	double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
	std::printf("PASS: %u production vehicle deformation mesh checks (%.3f ms host fixture; not Quest performance).\n",checks,ms);
	return 0;
}
