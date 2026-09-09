#include "ragdoll-test-vector.h"
#include "VrRagdollPhysics.h"
#include "VrRagdollImpact.h"
#include "VrRagdollImpulse.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace P = VrRagdollPhysics;
namespace I = VrRagdollImpulse;
static int checks;
static void Check(bool condition, const char *message)
{
    ++checks;
    if(!condition){ std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static const unsigned char torso[] = {P::RD_PELVIS,P::RD_SPINE1,P::RD_LUARM,P::RD_RUARM,P::RD_LTHIGH,P::RD_RTHIGH};
static const unsigned char head[] = {P::RD_NECK,P::RD_HEAD,P::RD_LCLAV,P::RD_RCLAV};
static const unsigned char arm[] = {P::RD_LCLAV,P::RD_LUARM,P::RD_LFARM,P::RD_LHAND};
static const unsigned char leg[] = {P::RD_PELVIS,P::RD_LTHIGH,P::RD_LCALF,P::RD_LFOOT};

static P::State Body(bool prone)
{
    P::State s = {};
    const CVector points[P::SIM_BONES] = {
        {0,0,.94f}, {0,0,1.10f}, {0,0,1.31f}, {0,0,1.53f}, {0,0,1.69f},
        {-.12f,0,1.43f}, {-.26f,0,1.45f}, {-.43f,-.035f,1.20f}, {-.53f,.03f,.98f},
        {.12f,0,1.43f}, {.26f,0,1.45f}, {.43f,-.035f,1.20f}, {.53f,.03f,.98f},
        {-.13f,0,.89f}, {-.14f,.035f,.48f}, {-.14f,-.04f,.1f},
        {.13f,0,.89f}, {.14f,.035f,.48f}, {.14f,-.04f,.1f}
    };
    for(int i = 0; i < P::SIM_BONES; ++i)
        s.pos[i] = prone ? CVector(points[i].z,-points[i].x,points[i].y+.65f) : points[i];
    P::Initialize(&s,prone ? CVector(0,-1,0) : CVector(1,0,0),
        prone ? CVector(1,0,0) : CVector(0,0,1));
    s.groundZ = 0;
    return s;
}
static P::State RestingBody()
{
    P::State s = Body(true);
    for(int frame = 0; frame < 1800 && !s.asleep; ++frame) P::Step(&s);
    Check(s.asleep,"integration body rests before impact");
    return s;
}
static CVector PartCentre(const P::State &s, const unsigned char *nodes, int count)
{
    CVector result;
    float mass = 0;
    for(int i = 0; i < count; ++i){
        const float m = 1.0f/P::invMass[nodes[i]];
        result += s.pos[nodes[i]]*m;
        mass += m;
    }
    return result*(1.0f/mass);
}
static CVector Momentum(const P::State &s)
{
    CVector result;
    for(int i = 0; i < P::SIM_BONES; ++i)
        result += (s.pos[i]-s.prev[i])*(1.0f/(P::STEP*P::invMass[i]));
    return result;
}
static CVector Angular(const P::State &s, const CVector &centre)
{
    CVector result;
    for(int i = 0; i < P::SIM_BONES; ++i)
        result += CrossProduct(s.pos[i]-centre,
            (s.pos[i]-s.prev[i])*(1.0f/(P::STEP*P::invMass[i])));
    return result;
}
static float Energy(const P::State &s)
{
    float result = 0;
    for(int i = 0; i < P::SIM_BONES; ++i)
        result += .5f*(s.pos[i]-s.prev[i]).MagnitudeSqr()/(P::STEP*P::STEP*P::invMass[i]);
    return result;
}

static void TestConservation()
{
    const CVector direction(0,1,0);
    const P::State initial = Body(false);
    const CVector centre = PartCentre(initial,torso,6);
    for(int side = -1; side <= 1; ++side){
        P::State s = initial;
        const CVector hit = centre+CVector(.15f*side,0,0);
        const float scale = I::ApplyPartImpulse(s.pos,s.prev,P::invMass,torso,6,
            hit,direction,8,P::STEP);
        Check(scale > 0 && scale <= 1,"torso bullet receives a bounded impulse scale");
        Check((Momentum(s)-direction*(8*scale)).Magnitude()<.0005f,
            "torque introduces no extra COM linear impulse");
        const CVector expected = CrossProduct(hit-centre,direction*(8*scale));
        const CVector measured = Angular(s,centre);
        std::printf("torque side=%d scale=%.6f measured=(%.6f %.6f %.6f) expected=(%.6f %.6f %.6f)\n",
            side,scale,measured.x,measured.y,measured.z,expected.x,expected.y,expected.z);
        Check((Angular(s,centre)-expected).Magnitude()<.004f,
            "opposite surface offsets produce opposite physical angular momentum");
        if(side == 0) Check(Angular(s,centre).Magnitude()<.0001f,
            "centred impulse creates no invented torque");
        if(side == 0){
            for(int i = 0; i < 6; ++i){
                const int node = torso[i];
                const CVector velocity = (s.pos[node]-s.prev[node])*(1.0f/P::STEP);
                Check((velocity-direction*(8.0f/42.4f)).Magnitude()<.00001f,
                    "centred gun-line impulse has no transverse or internal coupling");
            }
        }
        for(int i = 0; i < P::SIM_BONES; ++i){
            bool selected = false;
            for(int j = 0; j < 6; ++j) selected = selected || torso[j] == i;
            if(!selected) Check((s.prev[i]-initial.prev[i]).MagnitudeSqr()==0,
                "unselected parts receive no direct impulse");
        }
    }
    P::State limited = initial;
    const float scale = I::ApplyPartImpulse(limited.pos,limited.prev,P::invMass,torso,6,
        centre+CVector(.2f,.1f,0),direction,10000,P::STEP);
    Check(scale>0 && scale<1,"large impulse uniformly attenuated");
    Check((Momentum(limited)-direction*(10000*scale)).Magnitude()<.002f,
        "velocity clamp preserves the supplied impulse ratio");
    for(int i = 0; i < P::SIM_BONES; ++i)
        Check((limited.pos[i]-limited.prev[i]).Magnitude()/P::STEP<4.0001f,
            "point velocity change capped at four metres per second");

    const unsigned char line[] = {P::RD_PELVIS,P::RD_SPINE,P::RD_SPINE1};
    P::State singular = initial;
    I::ApplyPartImpulse(singular.pos,singular.prev,P::invMass,line,3,
        CVector(.1f,0,1.1f),CVector(0,1,0),16,P::STEP);
    Check(std::isfinite(Energy(singular)),"collinear part inertia remains finite");
    Check((Momentum(singular)-CVector(0,16,0)).Magnitude()<.002f,
        "unrepresentable axial spin does not invent linear recoil");
    const unsigned char duplicate[] = {P::RD_HEAD,P::RD_HEAD};
    P::State invalid = initial;
    Check(I::ApplyPartImpulse(invalid.pos,invalid.prev,P::invMass,duplicate,2,
        centre,direction,8,P::STEP)==0,"duplicate part indices rejected");
    Check(Energy(invalid)==0,"invalid group cannot partially mutate a pose");

    P::State moving = initial;
    for(int i = 0; i < P::SIM_BONES; ++i)
        moving.prev[i] -= CVector(2,1,-.5f)*P::STEP;
    const CVector before = Momentum(moving);
    I::ApplyPartImpulse(moving.pos,moving.prev,P::invMass,torso,6,
        centre+CVector(.1f,0,0),direction,8,P::STEP);
    Check((Momentum(moving)-before-direction*8).Magnitude()<.001f,
        "new impact adds to existing motion instead of replacing it");

    // A cyclic axis permutation is a proper rigid rotation, not a reflection.
    P::State transformed = initial;
    const CVector shift(17,-23,31);
    for(int i = 0; i < P::SIM_BONES; ++i){
        const CVector &v = initial.pos[i];
        transformed.pos[i] = transformed.prev[i] = CVector(v.y,v.z,v.x)+shift;
    }
    P::State original = initial;
    const CVector hit = centre+CVector(.1f,0,0);
    I::ApplyPartImpulse(original.pos,original.prev,P::invMass,torso,6,
        hit,direction,8,P::STEP);
    I::ApplyPartImpulse(transformed.pos,transformed.prev,P::invMass,torso,6,
        CVector(hit.y,hit.z,hit.x)+shift,CVector(1,0,0),8,P::STEP);
    for(int i = 0; i < P::SIM_BONES; ++i){
        const CVector displacement = original.pos[i]-original.prev[i];
        Check((transformed.pos[i]-transformed.prev[i]-
            CVector(displacement.y,displacement.z,displacement.x)).Magnitude()<.000005f,
            "impulse is invariant under rigid map translation and rotation");
    }
}

static void TestGroundedImpacts()
{
    const P::State rest = RestingBody();
    const unsigned char *groups[] = {torso,head,arm,leg};
    const int count[] = {6,4,4,4};
    const int tip[] = {P::RD_RCLAV,P::RD_HEAD,P::RD_LHAND,P::RD_LFOOT};
    const int hitNode[] = {P::RD_SPINE1,P::RD_HEAD,P::RD_LHAND,P::RD_LFOOT};
    const CVector direction = P::Unit(CVector(0,1,-.25f),CVector(0,1,0));
    for(int strength = 0; strength < 2; ++strength){
        for(int group = 0; group < 4; ++group){
            P::State s = rest;
            const float impulse = strength ? 16.0f : 8.0f;
            const CVector hit = s.pos[hitNode[group]]+CVector(0,0,.10f);
            I::ApplyPartImpulse(s.pos,s.prev,P::invMass,groups[group],count[group],
                hit,direction,impulse,P::STEP);
            P::Wake(&s);
            const CVector originalCentre = P::CentreOfMass(&rest);
            float maxLocal = 0, maxShift = 0, peakSpeed = 0, peakEnergy = 0;
            for(int frame = 0; frame < 180; ++frame){
                P::Step(&s);
                const CVector shift = P::CentreOfMass(&s)-originalCentre;
                maxShift = P::MaxF(maxShift,shift.Magnitude());
                maxLocal = P::MaxF(maxLocal,(s.pos[tip[group]]-rest.pos[tip[group]]-shift).Magnitude());
                peakSpeed = P::MaxF(peakSpeed,std::sqrt(s.lastMaxSpeedSq));
                peakEnergy = P::MaxF(peakEnergy,Energy(s));
                for(int i = 0; i < P::SIM_BONES; ++i){
                    Check(std::isfinite(s.pos[i].MagnitudeSqr()),"strong bullet remains finite through solver");
                    Check(s.pos[i].z>=P::radius[i]-.00002f,"strong bullet respects floor");
                }
            }
            std::printf("part-shot %d impulse=%.0fNs local=%.5fm COM=%.5fm peak=%.3fm/s energy=%.3fJ\n",
                group,impulse,maxLocal,maxShift,peakSpeed,peakEnergy);
            Check(peakSpeed<8 && maxShift<.3f && peakEnergy<160,
                "strong bullet remains local and bounded after articulated solve");
            if(group == 0) Check(maxLocal>(strength ? .015f : .005f) && maxLocal<.15f,
                "torso hit retains a visible but heavy multi-tick response");
            if(group == 1) Check(maxLocal>.05f && maxLocal<.4f,
                "head hit visibly articulates without unbounded recoil");
            if(group >= 2) Check(maxLocal>.12f && maxLocal<1.3f,
                "limb hit changes the pose while remaining within articulated reach");
            for(int frame = 0; frame < 1800 && !s.asleep; ++frame) P::Step(&s);
            Check(s.asleep,"strong bullet body settles after impact");
        }
    }
}

static void TestSustainedFire()
{
    const P::State rest = RestingBody();
    P::State s = rest;
    const CVector direction = P::Unit(CVector(0,1,-.25f),CVector(0,1,0));
    // Prospective gameplay budget supplied by the root: 24 Ns burst, 60 Ns/s.
    float credit = 24.0f, total = 0, peakEnergy = 0, peakSpeed = 0, peakError = 0;
    for(int frame = 0; frame < 1200; ++frame){
        credit = P::MinF(24.0f,credit+60.0f*P::STEP);
        const float impulse = P::MinF(16.0f,credit);
        credit -= impulse;
        total += impulse;
        const CVector hit = s.pos[P::RD_LHAND]+CVector(0,0,.08f);
        I::ApplyPartImpulse(s.pos,s.prev,P::invMass,arm,4,hit,direction,impulse,P::STEP);
        P::Wake(&s); P::Step(&s);
        peakEnergy = P::MaxF(peakEnergy,Energy(s));
        peakSpeed = P::MaxF(peakSpeed,std::sqrt(s.lastMaxSpeedSq));
        peakError = P::MaxF(peakError,s.lastMaxLengthError);
        Check(std::isfinite(peakEnergy),"continuous fire energy remains finite");
        for(int i = 0; i < P::SIM_BONES; ++i)
            Check(s.pos[i].z>=P::radius[i]-.00002f,"continuous fire cannot penetrate ground");
    }
    std::printf("sustained part torque impulse=%.1fNs peak=%.3fm/s energy=%.3fJ error=%.4f\n",
        total,peakSpeed,peakEnergy,peakError);
    Check(total<=24+60*20,"continuous fire respects proposed impulse budget");
    Check(peakSpeed<12 && peakEnergy<300 && peakError<.3f,
        "twenty seconds of rate-limited fire cannot accumulate explosive energy");
    for(int frame = 0; frame < 1800 && !s.asleep; ++frame) P::Step(&s);
    Check(s.asleep,"continuous fire body settles after shooting ends");
}

int main()
{
    TestConservation();
    TestGroundedImpacts();
    TestSustainedFire();
    std::printf("part torque: %d checks PASS\n",checks);
}
