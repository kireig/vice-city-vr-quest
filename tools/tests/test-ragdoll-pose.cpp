#include "ragdoll-test-vector.h"
#define Max(a,b) ((a) > (b) ? (a) : (b))
#define Min(a,b) ((a) < (b) ? (a) : (b))
#include "VrRagdollPose.h"
#include <cstdio>
#include <cstdlib>

using namespace VrRagdollPhysics;
using namespace VrRagdollPose;
static unsigned checks;
static void Check(bool okay, const char *message)
{
	++checks;
	if(!okay){ std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static CVector Rotate(const CVector &v, const CVector &axis, float angle)
{
	return v*std::cos(angle)+CrossProduct(axis,v)*std::sin(angle)+
		axis*(DotProduct(axis,v)*(1.0f-std::cos(angle)));
}
static void FrameCheck(const Frame &f)
{
	Check(std::isfinite(f.right.x)&&std::isfinite(f.forward.y)&&std::isfinite(f.up.z),"finite skin frame");
	Check(std::fabs(f.right.MagnitudeSqr()-1)<.00001f&&std::fabs(f.forward.MagnitudeSqr()-1)<.00001f&&
		std::fabs(f.up.MagnitudeSqr()-1)<.00001f,"unit skin frame");
	Check(DotProduct(CrossProduct(f.right,f.forward),f.up)>.99999f,"orthonormal skin frame without mirror/scale");
}
int main()
{
	const CVector down(0,0,-1), forward(0,1,0), lower(0,-.6f,-.8f);
	const CVector rotationAxis = Unit(CVector(.4f,.2f,.9f),CVector(1,0,0));
	const LegFrames rest = MakeLegFrames(down,lower,down,forward);
	// Bone-local bases vary by model; takeover must preserve any captured
	// orthonormal basis, not only a fixture where bone axes match world axes.
	for(int pose=0;pose<128;pose++){
		const float angle=pose*.049f;
		const CVector skin[3]={Rotate(CVector(1,0,0),rotationAxis,angle),
			Rotate(CVector(0,1,0),rotationAxis,angle),Rotate(CVector(0,0,1),rotationAxis,angle)};
		for(int axis=0;axis<3;axis++){
			Check((Carry(rest.upper,rest.upper,skin[axis])-skin[axis]).Magnitude()<.000001f,"exact thigh takeover for arbitrary model axes");
			Check((Carry(rest.lower,rest.lower,skin[axis])-skin[axis]).Magnitude()<.000001f,"exact calf/foot takeover for arbitrary model axes");
		}
		const LegFrames turned=MakeLegFrames(Rotate(down,rotationAxis,angle),Rotate(lower,rotationAxis,angle),
			Rotate(down,rotationAxis,angle),Rotate(forward,rotationAxis,angle));
		for(int axis=0;axis<3;axis++){
			const CVector expected=Rotate(skin[axis],rotationAxis,angle);
			Check((Carry(rest.upper,turned.upper,skin[axis])-expected).Magnitude()<.000002f,"thigh follows rigid body tumble");
			Check((Carry(rest.lower,turned.lower,skin[axis])-expected).Magnitude()<.000002f,"calf/foot follows rigid body tumble");
		}
	}
	float oldDisagreement=0;
	for(int sample=0;sample<1200;sample++){
		const float t=sample*.01f;
		const CVector upper=Unit(CVector(.45f*std::sin(t),.7f*std::cos(t),-.8f),down);
		const CVector bend=Swing(down,upper,forward,forward);
		const CVector normal=Unit(CrossProduct(bend,upper),CVector(-1,0,0));
		const CVector rolledBend=Rotate(bend,upper,.6f*std::sin(1.7f*t));
		const float flexion=.3f+1.9f*(.5f+.5f*std::sin(t*.8f));
		const CVector shin=upper*std::cos(flexion)-rolledBend*std::sin(flexion);
		const LegFrames current=MakeLegFrames(upper,shin,down,forward);
		FrameCheck(current.upper); FrameCheck(current.lower);
		Check(DotProduct(current.upper.up,upper)>.99999f&&DotProduct(current.lower.up,shin)>.99999f,"skin lengths point at actual joints");
		Check(DotProduct(current.upper.right,current.lower.right)>.99999f,"bent thigh and calf share knee hinge axis");
		Check(DotProduct(current.upper.right,normal)>.80f,"knee frame retains anatomical side");
		const CVector oldThigh=Swing(down,upper,rest.upper.right,rest.upper.right);
		const CVector oldCalf=Swing(lower,shin,rest.lower.right,rest.lower.right);
		oldDisagreement=MaxF(oldDisagreement,1.0f-DotProduct(oldThigh,oldCalf));
		// The exact transform used for a child toe preserves its local offset
		// when the foot shares the calf frame; no world-axis special casing.
		const CVector toe(.03f,.15f,-.02f);
		const CVector moved=Carry(rest.lower,current.lower,World(rest.lower,toe));
		Check((Local(current.lower,moved)-toe).Magnitude()<.000001f,"passthrough foot child remains rigid");
	}
	Check(oldDisagreement>.10f,"fixture reproduces independent shortest-arc calf/thigh roll disagreement");
	// Move either side of straightness with small transverse solver jitter.
	// The signed cross product flips, but shoes must not flip with it.
	CVector previousNormal=rest.upper.right;
	for(int sample=0;sample<2000;sample++){
		const float phase=sample*.13f;
		const CVector shin=Unit(down+CVector(.008f*std::sin(phase),.008f*std::cos(phase),0),down);
		const LegFrames current=MakeLegFrames(down,shin,down,forward);
		FrameCheck(current.upper); FrameCheck(current.lower);
		Check(DotProduct(current.lower.right,previousNormal)>.9998f,"no calf roll flip around straight knee");
		previousNormal=current.lower.right;
	}
	// Contact projection can transiently exceed the knee plane limit. Crossing
	// either side of a 90-degree twist must not trigger a +/- normal branch.
	for(int side=-1;side<=1;side+=2){
		previousNormal=rest.upper.right;
		for(int sample=0;sample<=3200;sample++){
			const float roll=side*sample*.001f;
			const CVector shin=down*.5f-Rotate(forward,down,roll)*.8660254f;
			const LegFrames current=MakeLegFrames(down,shin,down,forward);
			FrameCheck(current.upper); FrameCheck(current.lower);
			Check(DotProduct(current.upper.right,previousNormal)>.999f,"continuous thigh orientation across sideways and reverse knee transient");
			Check(DotProduct(current.upper.right,rest.upper.right)>.10f,"invalid knee cannot mirror thigh frame");
			previousNormal=current.upper.right;
		}
	}
	const LegFrames degenerate=MakeLegFrames(CVector(),CVector(),down,forward);
	FrameCheck(degenerate.upper); FrameCheck(degenerate.lower);
	std::printf("ragdoll pose: %u checks; old independent-frame max disagreement %.4f\n",checks,oldDisagreement);
	return 0;
}
