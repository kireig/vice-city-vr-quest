// Optional local-asset probe. No vehicle geometry is embedded or distributed.
// Run the compiled executable with the user's classic vehicles.col path.
#define main BuiltinHoodFixtureMain
#include "test-ragdoll-hood.cpp"
#undef main
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include "VrRagdollVehicleImpact.h"

struct ColModel {
	std::string name;
	CVector min,max,boundCenter;
	float boundRadius;
	std::vector<V::Shape> shapes;
	std::vector<V::Triangle> triangles;
};
template<class T> static T Read(const std::vector<unsigned char> &data,size_t at)
{
	Check(at<=data.size() && sizeof(T)<=data.size()-at,"COL field lies within asset file");
	T value; std::memcpy(&value,&data[at],sizeof(T)); return value;
}
static CVector ReadVector(const std::vector<unsigned char> &data,size_t at)
{
	return CVector(Read<float>(data,at),Read<float>(data,at+4),Read<float>(data,at+8));
}
static size_t Count(const std::vector<unsigned char> &data,size_t &at)
{
	short count=Read<short>(data,at); Check(count>=0,"COL count is nonnegative"); at+=4; return size_t(count);
}
static std::vector<ColModel> Load(const char *path)
{
	std::ifstream input(path,std::ios::binary); Check(input.good(),"user-provided COL can be opened");
	std::vector<unsigned char> data((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
	std::vector<ColModel> models;
	for(size_t start=0;start<data.size();){
		Check(Read<unsigned>(data,start)==0x4c4c4f43,"classic COLL signature present");
		const size_t end=start+8+Read<unsigned>(data,start+4);
		Check(end>start+72 && end<=data.size(),"COL record fits asset file");
		ColModel m; char name[23]={};std::memcpy(name,&data[start+8],22);m.name=name;
		const size_t body=start+32;
		m.boundRadius=Read<float>(data,body);m.boundCenter=ReadVector(data,body+4);
		m.min=ReadVector(data,body+16);m.max=ReadVector(data,body+28);
		size_t at=body+40,count=Count(data,at);
		for(size_t i=0;i<count;++i,at+=20){
			V::Shape s;s.radius=Read<float>(data,at);s.center=ReadVector(data,at+4);s.min=s.max=s.center;
			if(s.radius>0)m.shapes.push_back(s);
		}
		count=Count(data,at);at+=count*24;
		count=Count(data,at);
		for(size_t i=0;i<count;++i,at+=28){
			V::Shape s;s.radius=0;s.min=ReadVector(data,at);s.max=ReadVector(data,at+12);s.center=(s.min+s.max)*.5f;m.shapes.push_back(s);
		}
		count=Count(data,at);std::vector<CVector> vertices;
		for(size_t i=0;i<count;++i,at+=12)vertices.push_back(ReadVector(data,at));
		count=Count(data,at);
		for(size_t i=0;i<count;++i,at+=16){
			unsigned a=Read<unsigned>(data,at),b=Read<unsigned>(data,at+4),c=Read<unsigned>(data,at+8);
			Check(a<vertices.size() && b<vertices.size() && c<vertices.size(),"COL triangle vertex indices valid");
			V::Triangle t;if(V::MakeTriangle(vertices[a],vertices[b],vertices[c],t))m.triangles.push_back(t);
		}
		Check(at==end,"COL record parsed to declared end");models.push_back(m);start=end;
	}
	return models;
}
static void Select(const ColModel &model,V::Batch &batch,const P::State &s)
{
	V::Vehicle &v=batch.vehicles[0];v.shapeCount=0;v.triangleCount=0;
	CVector lo=s.pos[0],hi=lo;
	for(int i=1;i<P::SIM_BONES;++i){
		lo.x=P::MinF(lo.x,s.pos[i].x);lo.y=P::MinF(lo.y,s.pos[i].y);lo.z=P::MinF(lo.z,s.pos[i].z);
		hi.x=P::MaxF(hi.x,s.pos[i].x);hi.y=P::MaxF(hi.y,s.pos[i].y);hi.z=P::MaxF(hi.z,s.pos[i].z);
	}
	const CVector centre=(lo+hi)*.5f;
	const CVector from=V::PoseAt(v,-P::STEP).ToLocal(centre),to=V::PoseAt(v,P::STEP).ToLocal(centre);
	float shapeScores[V::MAX_SHAPES];
	for(const V::Shape &shape:model.shapes){
		const float extent=shape.radius>0?shape.radius:(shape.max-shape.center).Magnitude();
		const float score=P::MaxF(0,std::sqrt(V::SegmentDistanceSq(shape.center,from,to))-extent);
		int at=v.shapeCount;
		if(at==V::MAX_SHAPES){
			++batch.shapeOverflow;at=0;
			for(int k=1;k<V::MAX_SHAPES;++k)if(shapeScores[k]>shapeScores[at])at=k;
			if(score>=shapeScores[at])continue;
		}else ++v.shapeCount;
		v.shapes[at]=shape;shapeScores[at]=score;
	}
	float triangleScores[V::MAX_TRIANGLES];const CVector middle=v.pose.ToLocal(centre);
	int scanCount=(int)model.triangles.size();if(scanCount>V::MAX_TRIANGLE_SCAN)scanCount=V::MAX_TRIANGLE_SCAN;
	for(int sample=0;sample<scanCount;++sample){
		const V::Triangle &t=model.triangles[size_t(sample)*model.triangles.size()/size_t(scanCount)];
		const float a=(V::ClosestTrianglePoint(t,from)-from).MagnitudeSqr();
		const float b=(V::ClosestTrianglePoint(t,middle)-middle).MagnitudeSqr();
		const float c=(V::ClosestTrianglePoint(t,to)-to).MagnitudeSqr();
		V::KeepTriangle(v,t,P::MinF(a,P::MinF(b,c)),triangleScores,batch.triangleOverflow);
	}
}
static void ActualHit(const ColModel &model,float speed,int brakePercent=0)
{
	P::State s=Standing();
	for(int i=0;i<P::SIM_BONES;++i)s.pos[i].y+=model.max.y-2.0f;
	P::Initialize(&s,CVector(1,0,0),CVector(0,0,1));
	V::Batch batch;batch.count=1;V::Vehicle &car=batch.vehicles[0];
	// A road clearance of 10cm is explicit: this probe does not reconstruct
	// streamed wheel frames or handling suspension, and is not a runtime test.
	car.pose.origin=CVector(0,0,.1f-model.min.z);car.pose.right=CVector(1,0,0);car.pose.forward=CVector(0,1,0);car.pose.up=CVector(0,0,1);
	car.linearVelocity=car.surfaceLinearVelocity=CVector(0,speed,0);car.angularVelocity=car.surfaceAngularVelocity=CVector(0,0,0);
	car.boundCenter=model.boundCenter;car.boundRadius=model.boundRadius;
	float peakSpeed=0,peakHeight=0,peakError=0,lateError=0,carry=0;
	bool braked=false;
	for(int frame=0;frame<180;++frame){
		Select(model,batch,s);Advance(s,batch);
		if(!braked && brakePercent){
			bool collided=false;for(int i=0;i<P::SIM_BONES;++i)collided=collided||s.contact[i].active;
			if(collided){
				// Verified classic handling masses; collision impulse is the
				// explicit test assumption of accelerating 70kg from rest.
				const float mass=model.name=="admiral"?1650.0f:1900.0f;
				car.linearVelocity+=VrRagdollBrake::Impulse(mass,car.linearVelocity,70*speed/50,float(brakePercent))/mass;
				car.surfaceLinearVelocity=car.linearVelocity;braked=true;
			}
		}
		peakSpeed=P::MaxF(peakSpeed,std::sqrt(s.lastMaxSpeedSq));peakError=P::MaxF(peakError,s.lastMaxLengthError);
		if(frame>60)lateError=P::MaxF(lateError,s.lastMaxLengthError);
		bool supported=false;
		for(int i=0;i<P::SIM_BONES;++i){
			peakHeight=P::MaxF(peakHeight,s.pos[i].z);
			if(i<=P::RD_HEAD && s.contact[i].active && s.contact[i].normal.z>.35f)supported=true;
		}
		CVector local=car.pose.ToLocal(P::CentreOfMass(&s));
		if(supported && local.x>model.min.x && local.x<model.max.x && local.y>model.min.y && local.y<model.max.y)carry+=P::STEP;
	}
	const CVector local=car.pose.ToLocal(P::CentreOfMass(&s));
	std::printf("COL %s %.0fm/s brake%d finalSpeed%.3f shapes%zu->%d triangles%zu peakSpeed%.3f height%.3f maxError%.3f lateError%.3f carry%.3fs finalLocal(%.2f,%.2f,%.2f)\n",model.name.c_str(),speed,brakePercent,car.linearVelocity.y,model.shapes.size(),V::MAX_SHAPES,model.triangles.size(),peakSpeed,peakHeight,peakError,lateError,carry,local.x,local.y,local.z);
}

static void MeasuredTakeover(const ColModel &model,float speed,bool legacy)
{
	P::State state=Standing();
	for(int i=0;i<P::SIM_BONES;++i) state.pos[i].y+=model.max.y-2.0f;
	P::Initialize(&state,{1,0,0},{0,0,1});
	V::Batch batch;batch.count=1;V::Vehicle &car=batch.vehicles[0];
	car.pose.origin={0,0,.1f-model.min.z};car.pose.right={1,0,0};car.pose.forward={0,1,0};car.pose.up={0,0,1};
	car.linearVelocity=car.surfaceLinearVelocity={0,speed,0};
	car.angularVelocity=car.surfaceAngularVelocity={0,0,0};
	car.boundCenter=model.boundCenter;car.boundRadius=model.boundRadius;
	V::Contact nativeContact;int firstNode=-1;
	// Find the real model surface's first contact in 1 cm increments. No ped
	// launch or full-car bounding box is used to choose the contact height.
	for(int sample=0;sample<150 && firstNode<0;++sample){
		Select(model,batch,state);V::Prepare(batch,0,0);
		for(int node=0;node<P::SIM_BONES;++node){
			V::Contact contact;
			if(V::Find(batch,state.pos[node],state.pos[node],P::radius[node],contact)){
				nativeContact=contact;firstNode=node;break;
			}
		}
		if(firstNode<0) car.pose.origin.y+=.01f;
	}
	Check(firstNode>=0,"classic car has an actual initial body contact");
	const CVector point=nativeContact.center-nativeContact.normal*P::radius[firstNode];
	P::SeedMotion(&state,legacy?CVector(0,speed*.75f,0):CVector(0,0,0),{0,0,0},60);
	const float applied=legacy?0:VrRagdollVehicleImpact::Apply(&state,point,{0,speed,0},70*speed);
	const float headSpeed=(state.pos[P::RD_HEAD]-state.prev[P::RD_HEAD]).Magnitude()/P::STEP;
	const float legSpeed=P::MaxF((state.pos[P::RD_LCALF]-state.prev[P::RD_LCALF]).Magnitude(),
		(state.pos[P::RD_RCALF]-state.prev[P::RD_RCALF]).Magnitude())/P::STEP;
	if(!legacy && point.z<.85f){
		Check(applied>0 && headSpeed<.001f && legSpeed>.05f,"actual low car contact transfers leg momentum before torso");
	}
	// The native measured impulse is an explicit assumption here (70 kg * speed).
	// Both takeover variants receive the same new brake setting for comparison.
	const float carMass=model.name=="admiral"?1650.0f:1400.0f;
	car.linearVelocity+=VrRagdollBrake::Impulse(carMass,car.linearVelocity,70*speed/50,200)/carMass;
	car.surfaceLinearVelocity=car.linearVelocity;
	float carry=0,peakSpeed=0,peakHeight=0,peakLength=0;
	for(int frame=0;frame<180;++frame){
		Select(model,batch,state);Advance(state,batch);
		peakSpeed=P::MaxF(peakSpeed,std::sqrt(state.lastMaxSpeedSq));
		peakLength=P::MaxF(peakLength,state.lastMaxLengthError);
		bool supported=false;
		for(int node=0;node<P::SIM_BONES;++node){
			peakHeight=P::MaxF(peakHeight,state.pos[node].z);
			if(node<=P::RD_HEAD && state.contact[node].active && state.contact[node].normal.z>.35f)supported=true;
		}
		const CVector local=car.pose.ToLocal(P::CentreOfMass(&state));
		if(supported && local.x>model.min.x && local.x<model.max.x && local.y>model.min.y && local.y<model.max.y)carry+=P::STEP;
	}
	std::printf("TAKEOVER %s speed%.0f %s contactNode%d z%.3f J%.2f head%.2f leg%.2f brakeSpeed%.2f carry%.3fs peakSpeed%.2f height%.2f error%.3f\n",
		model.name.c_str(),speed,legacy?"LEGACY":"MEASURED",firstNode,point.z,applied,headSpeed,legSpeed,
		car.linearVelocity.y,carry,peakSpeed,peakHeight,peakLength);
}
int main(int argc,char **argv)
{
	if(argc!=2){std::printf("SKIP: optional classic COL probe requires a user-provided vehicles.col path\n");return 0;}
	VrRagdollSettings::SetGrip(150);
	std::vector<ColModel> models=Load(argv[1]);
	for(const ColModel &model:models){
		if(model.name=="admiral" || model.name=="oceanic" || model.name=="infernus" || model.name=="police" || model.name=="sentinel" || model.name=="taxi" || model.name=="stinger"){
			ActualHit(model,6);ActualHit(model,12);ActualHit(model,20);ActualHit(model,30);
			if(model.name=="admiral" || model.name=="oceanic"){
				ActualHit(model,12,200);ActualHit(model,20,200);ActualHit(model,30,200);
			}
		}
		if(model.name=="admiral" || model.name=="sentinel" || model.name=="stinger"){
			for(int speed=6;speed<=20;speed+=7){
				MeasuredTakeover(model,float(speed),true);
				MeasuredTakeover(model,float(speed),false);
			}
		}
	}
	std::printf("COL diagnostic completed; real car motion and height remain approximations; %d finite/parse checks\n",checks);
}
