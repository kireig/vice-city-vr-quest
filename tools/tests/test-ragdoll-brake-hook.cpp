#define MIAMIVR_DEV_TOOLS 1 // Verify developer speed telemetry against actual forces.
#include "ragdoll-test-vector.h"
#include "VrRagdollBrake.h"
#include "VrRagdollSettings.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <initializer_list>

#define nil nullptr
typedef unsigned int uint32;
enum { RANDOM_CHAR = 1, MISSION_CHAR = 2 };

// Engine substitutes preserve CPhysical's metres-per-50-Hz-tick impulse units.
// The callback itself is extracted unchanged from production by the runner.
static int nextPedHandle = 1;
struct CPed {
    CVector m_vecMoveSpeed, m_vecTurnSpeed;
    bool player = false, bInVehicle = false;
    int CharCreatedBy = RANDOM_CHAR, handle = nextPedHandle++;
    bool IsPlayer() const { return player; }
};
struct CVehicle {
    float m_fMass = 1000.0f;
    CVector m_vecMoveSpeed = CVector(.4f, 0.0f, .02f);
    CVector position, m_vecTurnSpeed;
    bool bUsesCollision = true, bRemoveFromWorld = false;
    bool car = true, helicopter = false, plane = false;
    int handle = 1, forceCalls = 0;
    bool IsCar() const { return car; }
    bool IsRealHeli() const { return helicopter; }
    bool IsRealPlane() const { return plane; }
    const CVector &GetPosition() const { return position; }
    CVector GetSpeed(const CVector &offset) const { return m_vecMoveSpeed + CrossProduct(m_vecTurnSpeed, offset); }
    void ApplyMoveForce(const CVector &impulse) {
        ++forceCalls;
        m_vecMoveSpeed += impulse / m_fMass;
    }
};
struct CTimer {
    static unsigned frame, time;
    static unsigned GetFrameCounter() { return frame; }
    static unsigned GetTimeInMilliseconds() { return time; }
};
unsigned CTimer::frame = 100, CTimer::time = 100000;
struct CPools {
    static int GetVehicleRef(CVehicle *car) { return car->handle; }
    static int GetPedRef(CPed *ped) { return ped->handle; }
};
struct Slot {
    CPed *ped = nullptr;
    bool poseCached = true;
    unsigned seedCalls = 0, inheritCalls = 0, wakeCalls = 0;
    CVector seededLinear, seededAngular;
    float seedLimit = 0;
    float age = 0;
    bool vehicleSeeded = false;
    unsigned priorityUntil = 0;
    unsigned localImpactCalls = 0;
    CVector contactPoint, surfaceSpeed;
    float measuredImpulse = 0;
    float bodyMassScale = 0;
};
static const float STEP = 1.0f/60.0f;
static Slot slots[6];
static bool enabled = true;
static unsigned ownerLookups;
static Slot *FindSlot(CPed *ped) {
    ++ownerLookups;
    for(Slot &slot : slots) if(slot.ped && slot.ped == ped) return &slot;
    return nullptr;
}
// Capture callback arguments only: production SeedMotion stability and velocity
// limiting belong to the solver fixture, not an imitation in this hook test.
static void SeedMotion(Slot *slot, const CVector &linear, const CVector &angular, float limit) {
    ++slot->seedCalls;
    slot->seededLinear = linear;
    slot->seededAngular = angular;
    slot->seedLimit = limit;
}
static void InheritAnimatedMotion(Slot *slot, CPed *) { ++slot->inheritCalls; }
static void Wake(Slot *slot) { ++slot->wakeCalls; }
namespace VrRagdollVehicleImpact {
// This fixture verifies the engine callback's argument forwarding. The actual
// local impulse distribution is covered by the vehicle-impact solver fixture.
static void Apply(Slot *slot, const CVector &point, const CVector &surfaceSpeed, float impulse, float massScale) {
    ++slot->localImpactCalls;
    slot->contactPoint = point;
    slot->surfaceSpeed = surfaceSpeed;
    slot->measuredImpulse = impulse;
    slot->bodyMassScale = massScale;
}
}
static float Sqrt(float value) { return std::sqrt(value); }
static float Min(float a, float b) { return a < b ? a : b; }
static float Max(float a, float b) { return a > b ? a : b; }
namespace VrRagdollMetrics {
enum Counter { CAR_BRAKES, BRAKE_POINTS };
static void Count(Counter, unsigned = 1) {}
static void AddBrakeImpulse(float) {}
}

// Production globals used by the extracted callback; diagnostics do not affect
// the numeric assertions. The hit cache is separate from fixture owner records.
static VrRagdollBrake::Ledger vehicleBrakeLedger;
static VrRagdollBrake::HitCache vehicleHitCache;
static unsigned vehicleImpactCalls, vehicleBrakeEvents;
static float lastVehicleBrakeKmh, lastVehicleBeforeKmh, lastVehicleAfterKmh;

void VehicleImpact(CPed*,CVehicle*,const CVector&,float,const CVector* = nullptr,const CVector* = nullptr);
#include "ragdoll-brake-hook-production.inc"

static unsigned checks;
static void Check(bool condition, const char *message) {
    ++checks;
    if(!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static bool Near(float a, float b, float tolerance = .0001f) {
    return std::fabs(a-b) <= tolerance;
}
static void Reset() {
    for(Slot &slot : slots) slot = Slot();
    enabled = true;
    ++CTimer::frame;
    CTimer::time = 100000;
    VrRagdollSettings::Get() = VrRagdollSettings::Values();
    VrRagdollSettings::SetWeight(100); // Reference force expectations use nominal body mass.
    vehicleBrakeLedger = VrRagdollBrake::Ledger();
    vehicleHitCache = VrRagdollBrake::HitCache();
    vehicleImpactCalls = vehicleBrakeEvents = 0;
    lastVehicleBrakeKmh = lastVehicleBeforeKmh = lastVehicleAfterKmh = 0;
}
static Slot &Admit(CPed &ped, int index = 0) {
    slots[index] = Slot(); slots[index].ped = &ped;
    return slots[index];
}
static void ImmediateAndDuplicate() {
    Reset(); CPed ped; CVehicle car; Slot &slot = Admit(ped);
    Check(GetBrakePercent() == 200, "actual settings default to200percent braking");
    // The engine has already accelerated the pedestrian to the car's speed.
    // This must not erase the collision impulse or postpone the brake to Step.
    ped.m_vecMoveSpeed = car.m_vecMoveSpeed;
    ped.m_vecTurnSpeed = CVector(.01f,.02f,-.03f);
    const CVector before = car.m_vecMoveSpeed;
    VehicleImpact(&ped, &car, ped.m_vecMoveSpeed, 28.0f);
    const float initialLoss = (before.x-car.m_vecMoveSpeed.x)*180.0f;
    Check(car.forceCalls == 1 && Near(initialLoss,24.9917355f,.002f),
        "default confirmed hit removes24.99kmh immediately before any body simulation step");
    Check(car.m_vecMoveSpeed.y == before.y && car.m_vecMoveSpeed.z == before.z,
        "callback cannot lift or deflect the vehicle");
    Check(vehicleHitCache.count == 1, "applied impact records the pedestrian and car independently of its slot");
    Check(vehicleImpactCalls == 1 && vehicleBrakeEvents == 1 &&
        Near(lastVehicleBrakeKmh, initialLoss, .001f) &&
        Near(lastVehicleBeforeKmh,72,.001f) && Near(lastVehicleAfterKmh,72-initialLoss,.001f),
        "diagnostics report measured before and after car speed from actual force application");
    Check(slot.seedCalls == 1 && slot.inheritCalls == 1 && !slot.poseCached,
        "visual motion seed and animated limb inheritance still execute");
    Check(Near(slot.seededLinear.x, before.x*50.0f) &&
        Near(slot.seededAngular.z, ped.m_vecTurnSpeed.z*50.0f) && Near(slot.seedLimit,24),
        "visual velocity uses SI units with a car-relative limit instead of the generic16mps cap");
    const float after = car.m_vecMoveSpeed.x;
    VehicleImpact(&ped, &car, ped.m_vecMoveSpeed, 28.0f);
    Check(car.forceCalls == 1 && Near(car.m_vecMoveSpeed.x, after),
        "repeated callback for same pair cannot brake twice in one frame");
    Check(slot.seedCalls == 1 && slot.inheritCalls == 1 && slot.vehicleSeeded,
        "repeated engine callback cannot erase existing ragdoll articulation with another full-body launch");
    ++CTimer::frame; CTimer::time += 16;
    VehicleImpact(&ped, &car, ped.m_vecMoveSpeed, 28.0f);
    Check(car.forceCalls == 1 && Near(car.m_vecMoveSpeed.x, after),
        "next-frame callback is still suppressed by pair cooldown");
    ++CTimer::frame; CTimer::time = 100999;
    VehicleImpact(&ped, &car, ped.m_vecMoveSpeed, 28.0f);
    Check(car.forceCalls == 1, "cooldown rejects the same pair for999ms");
    ++CTimer::frame; CTimer::time = 101000;
    VehicleImpact(&ped, &car, ped.m_vecMoveSpeed, 28.0f);
    Check(car.forceCalls == 2 && car.m_vecMoveSpeed.x < after && vehicleHitCache.count==1,
        "a new confirmed impact can brake the same pair after1000ms");
    Check(vehicleImpactCalls == 5 && vehicleBrakeEvents == 2,
        "diagnostics distinguish dispatches from accepted pair events");
    std::printf("callback default immediate %.2f -> %.2f km/h; repeated pair accepts again after1000ms\n",
        before.x*180.0f, after*180.0f);
}
static void StrengthAndCapacity() {
    float previous = -1;
    for(int percent : {0,100,200,500,1000,2000}) {
        Reset(); VrRagdollSettings::SetBrake(percent);
        CPed occupied[6], ped; CVehicle car;
        for(int i=0;i<6;++i) Admit(occupied[i],i);
        ped.m_vecMoveSpeed = car.m_vecMoveSpeed;
        const float before = car.m_vecMoveSpeed.x;
        Check(FindSlot(&ped)==nullptr, "confirmed hit can precede ownership admission while unrelated fixture records exist");
        VehicleImpact(&ped, &car, ped.m_vecMoveSpeed, 28);
        const float loss=(before-car.m_vecMoveSpeed.x)*180.0f;
        Check(loss>previous, "actual callback produces increasing0/100/200/500/1000/2000percent braking independently of owner admission");
        if(percent==0) Check(car.forceCalls==0 && vehicleHitCache.count==0,
            "zero setting consumes no car impulse or pair credit");
        if(percent==100) Check(Near(loss,12.4958678f,.002f), "100percent matches the previous12.50kmh callback calibration");
        if(percent==200) Check(Near(loss,24.9917355f,.002f), "200percent doubles the previous calibrated speed loss");
        if(percent==500) Check(Near(loss,54,.002f), "500percent remains materially stronger than200percent");
        if(percent==1000) Check(Near(loss,64.8f,.002f), "1000percent overcomes the old75percent per-hit ceiling");
        if(percent==2000) Check(Near(loss,70.56f,.002f), "2000percent allows a98percent speed loss without reversing");
        for(const Slot &slot : slots) Check(slot.seedCalls==0 && slot.inheritCalls==0,
            "capacity-independent braking does not alter unrelated active bodies");
        std::printf("callback before-owner-admission strength=%dpercent loss=%.3fkm/h\n",percent,loss);
        previous=loss;
    }
}
static void SharedFrameBudget() {
    for(int percent : {0,100,200,500,1000,2000}) {
        Reset(); VrRagdollSettings::SetBrake(percent); CPed peds[10]; CVehicle car;
        const float initial = car.m_vecMoveSpeed.x;
        for(int i = 0; i < 10; ++i) {
            if(i<6) Admit(peds[i], i);
            VehicleImpact(&peds[i], &car, CVector(.4f,0,0), 10000.0f);
        }
        const float fraction=float(VrRagdollBrake::FrameFraction(float(percent)));
        Check(Near(car.m_vecMoveSpeed.x, initial*(1-fraction), .0002f),
            "all pedestrians share the configured frame cap regardless of visual slot ownership");
        Check(vehicleHitCache.count==car.forceCalls,
            "bodies blocked by exhausted frame budget do not consume pair credit");
        if(percent>0 && car.m_vecMoveSpeed.x*50 >= VrRagdollBrake::MIN_CAR_SPEED) {
            ++CTimer::frame; CPed newPed;
            const float before = car.m_vecMoveSpeed.x;
            VehicleImpact(&newPed, &car, CVector(0,0,0), 10000.0f);
            Check(car.m_vecMoveSpeed.x < before, "new frame has a fresh car budget even for an unowned body");
        }
        std::printf("callback shared-frame strength=%dpercent initial=%.2f final=%.2fkm/h\n",percent,initial*180,initial*(1-fraction)*180);
    }
}
static void EligibilityAndRetry() {
    for(int reason = 0; reason < 18; ++reason) {
        Reset(); CPed ped; CVehicle car; Admit(ped);
        float impulse = 28.0f;
        switch(reason) {
        case 0: enabled = false; break;
        case 1: car.car = false; break;
        case 2: car.helicopter = true; break;
        case 3: car.plane = true; break;
        case 4: car.bUsesCollision = false; break;
        case 5: car.bRemoveFromWorld = true; break;
        case 6: car.m_fMass = 0; break;
        case 7: impulse = 0; break;
        case 8: impulse = -1; break;
        case 9: car.m_vecMoveSpeed = CVector(0,0,.2f); break;
        case 10: ped.player = true; break;
        case 11: ped.CharCreatedBy = MISSION_CHAR; break;
        case 12: ped.bInVehicle = true; break;
        case 13: ped.handle = -1; break;
        case 14: car.handle = -1; break;
        case 15: VrRagdollSettings::SetBrake(0); break;
        case 16: impulse = std::numeric_limits<float>::quiet_NaN(); break;
        case 17: car.m_fMass = std::numeric_limits<float>::quiet_NaN(); break;
        }
        const CVector before = car.m_vecMoveSpeed;
        VehicleImpact(&ped, &car, CVector(.4f,0,0), impulse);
        Check(car.forceCalls == 0 && car.m_vecMoveSpeed.x == before.x &&
            car.m_vecMoveSpeed.y == before.y && car.m_vecMoveSpeed.z == before.z,
            "excluded or zero-force events do not alter vehicle speed");
        Check(vehicleHitCache.count==0, "excluded event does not consume a later valid pair impact");
    }
    Reset(); CPed ped; CVehicle car;
    VehicleImpact(nullptr, &car, CVector(.4f,0,0), 28);
    VehicleImpact(&ped, nullptr, CVector(.4f,0,0), 28);
    VehicleImpact(&ped, &car, CVector(.4f,0,0), 0);
    VrRagdollSettings::SetBrake(0);
    VehicleImpact(&ped, &car, CVector(.4f,0,0), 28);
    Check(car.forceCalls==0 && vehicleHitCache.count==0,
        "null, zero impulse and disabled calls preserve later pair credit");
    VrRagdollSettings::SetBrake(200);
    VehicleImpact(&ped, &car, CVector(.4f,0,0), 28);
    Check(car.forceCalls==1 && vehicleHitCache.count==1,
        "valid unowned pedestrian still brakes after null zero and disabled callbacks");
}
static void GenerationHandlesAndTimerWrap() {
    Reset(); CPed ped; CVehicle car;
    VehicleImpact(&ped,&car,CVector(.4f,0,0),28);
    ++CTimer::frame; ped.handle += 0x100;
    VehicleImpact(&ped,&car,CVector(.4f,0,0),28);
    Check(car.forceCalls==2 && vehicleHitCache.count==2,
        "reused pedestrian pointer with new pool generation gets its own pair credit");
    ++CTimer::frame; car.handle += 0x100;
    VehicleImpact(&ped,&car,CVector(.4f,0,0),28);
    Check(car.forceCalls==3 && vehicleHitCache.count==3,
        "reused vehicle pointer with new pool generation gets its own pair credit");
    Reset(); CPed wrapPed; CVehicle wrapCar;
    CTimer::time=std::numeric_limits<unsigned>::max()-499u;
    VehicleImpact(&wrapPed,&wrapCar,CVector(.4f,0,0),28);
    ++CTimer::frame; CTimer::time=499;
    VehicleImpact(&wrapPed,&wrapCar,CVector(.4f,0,0),28);
    Check(wrapCar.forceCalls==1, "actual callback preserves cooldown999ms across timer wrap");
    ++CTimer::frame; CTimer::time=500;
    VehicleImpact(&wrapPed,&wrapCar,CVector(.4f,0,0),28);
    Check(wrapCar.forceCalls==2, "actual callback permits a new impact1000ms across timer wrap");
}
static void CarRelativeSeedArguments() {
    for(float speed : {0.0f,10.0f,30.0f,80.0f}) {
        Reset(); CPed ped; CVehicle car; Slot &slot=Admit(ped);
        car.m_vecMoveSpeed=CVector(speed/50,0,0);
        VehicleImpact(&ped,&car,car.m_vecMoveSpeed,0);
        const float expected=Min(60,Max(16,speed+4));
        Check(Near(slot.seededLinear.x,speed) && Near(slot.seedLimit,expected),
            "callback passes coherent car translation and the16..60mps car-relative seed limit");
        if(speed==30) Check(slot.seedLimit>=34 && Near(slot.seededLinear.x,30),
            "30mps body motion is passed intact with34mps allowance to production SeedMotion");
        Check(car.forceCalls==0 && vehicleHitCache.count==0,
            "visual seed-only callback does not use a collision braking token");
    }
}
static void LocalImpactArguments() {
    Reset(); CPed ped; CVehicle car; Slot &slot=Admit(ped);
    car.position=CVector(4,5,6); car.m_vecTurnSpeed=CVector(0,0,.04f);
    ped.m_vecTurnSpeed=CVector(10,20,30);
    const CVector contact=car.position+CVector(2,-1,1);
    const CVector beforeTurn(.01f,.02f,.03f),incoming(.1f,.2f,.3f);
    const CVector surface=car.GetSpeed(contact-car.GetPosition())*50;
    VehicleImpact(&ped,&car,incoming,28,&contact,&beforeTurn);
    Check(slot.localImpactCalls==1 && Near((slot.contactPoint-contact).Magnitude(),0),
        "native collision position reaches the local body impulse unchanged");
    Check(Near((slot.surfaceSpeed-surface).Magnitude(),0) && Near(slot.measuredImpulse,1400),
        "local impulse receives contact surface velocity including rotation and measured SI impulse");
    Check(Near((slot.seededAngular-beforeTurn*50).Magnitude(),0) &&
        Near((slot.seededLinear-incoming*50).Magnitude(),0),
        "new ragdoll seeds pre-collision movement rather than already accelerated native motion");
    Check(slot.priorityUntil==CTimer::time+350 && slot.vehicleSeeded && slot.wakeCalls==1,
        "first vehicle hit wakes the persistent body and gives it350ms scheduler priority");
    VehicleImpact(&ped,&car,incoming,28,&contact,&beforeTurn);
    Check(slot.localImpactCalls==1 && slot.seedCalls==1,
        "repeated native callback cannot apply measured local impulse or full-body seed twice");
    Reset(); CPed oldPed; CVehicle nextCar; Slot &old=Admit(oldPed); old.age=.5f;
    VehicleImpact(&oldPed,&nextCar,incoming,28,&contact,&beforeTurn);
    Check(old.localImpactCalls==1 && old.seedCalls==0 && old.inheritCalls==0 && old.vehicleSeeded,
        "existing articulated corpse accepts a local strike without replacing all joint velocities");
}
static void MassAndDirection() {
    float loss[3]; const float masses[] = {800,1000,2000};
    for(int i=0;i<3;++i) {
        Reset(); CPed ped; CVehicle car; Admit(ped);
        car.m_fMass = masses[i];
        car.m_vecMoveSpeed = CVector(0,-.4f,.03f);
        VehicleImpact(&ped, &car, CVector(0,-.4f,0), 28);
        loss[i] = .4f + car.m_vecMoveSpeed.y;
        Check(car.forceCalls == 1 && car.m_vecMoveSpeed.y < 0 && loss[i] > 0,
            "reverse motion slows without reversing its sign");
        Check(car.m_vecMoveSpeed.x == 0 && car.m_vecMoveSpeed.z == .03f,
            "reverse impact remains horizontal and aligned with current motion");
    }
    Check(loss[0] > loss[1] && loss[1] > loss[2], "heavier vehicles lose less speed from the same confirmed impulse");
}
static void ConfigurableBodyWeight() {
    float lastLoss = 0;
    for(int weight : {50,100,150,200,250,300}) {
        Reset(); CPed ped; CVehicle car; Slot &slot = Admit(ped);
        VrRagdollSettings::SetWeight(weight);
        const CVector contact(0,0,.5f), incoming(0,0,0);
        VehicleImpact(&ped,&car,incoming,2,&contact);
        const float loss = .4f-car.m_vecMoveSpeed.x;
        Check(Near(slot.bodyMassScale,weight*.01f), "actual callback forwards saved body weight to measured local impulse");
        Check(loss > lastLoss && car.m_vecMoveSpeed.x > 0, "heavier bodies create stronger bounded initial vehicle resistance");
        Check(GetBrakePercent() == 200 && slot.measuredImpulse == 100, "mass keeps brake gain and native measured impulse independent");
        lastLoss = loss;
    }
    Reset(); VrRagdollSettings::SetWeight(300); VrRagdollSettings::SetBrake(0);
    CPed ped; CVehicle car; Slot &slot=Admit(ped); const CVector contact;
    VehicleImpact(&ped,&car,CVector(),2,&contact);
    Check(car.forceCalls==0 && slot.bodyMassScale==3, "zero brake gain disables extra car resistance without disabling body inertia");
    Reset(); enabled=false; VrRagdollSettings::SetWeight(300); CPed offPed; CVehicle offCar;
    for(int i=0;i<100000;++i) VehicleImpact(&offPed,&offCar,CVector(),2,&contact);
    Check(offCar.forceCalls==0 && vehicleHitCache.count==0 && vehicleBrakeLedger.count==0,
        "disabled ragdolls do no mass-related force or contact-ledger work");
}
static void DisabledRetainedOwner() {
    for(bool owned : {false,true}) {
        Reset(); CPed ped; CVehicle car;
        if(owned) Admit(ped);
        enabled=false; ownerLookups=0;
        for(int hit=0;hit<1000;++hit) VehicleImpact(&ped,&car,CVector(.4f,0,0),28);
        Check(ownerLookups==0 && vehicleImpactCalls==0 && vehicleBrakeEvents==0,
            "OFF real callback performs no owner lookup or diagnostic work");
        Check(car.forceCalls==0 && vehicleHitCache.count==0,
            "OFF real callback cannot brake a car or consume impact credit");
        for(const Slot&slot:slots) Check(slot.seedCalls==0 && slot.inheritCalls==0 && slot.wakeCalls==0 && slot.localImpactCalls==0,
            "OFF real callback never reseeds, wakes or impulses retained bodies");
    }
}
int main() {
    DisabledRetainedOwner();
    ImmediateAndDuplicate(); StrengthAndCapacity(); SharedFrameBudget();
    EligibilityAndRetry(); GenerationHandlesAndTimerWrap(); CarRelativeSeedArguments(); LocalImpactArguments(); MassAndDirection();
    ConfigurableBodyWeight();
    std::printf("PASS: extracted production vehicle brake callback %u checks\n", checks);
}
