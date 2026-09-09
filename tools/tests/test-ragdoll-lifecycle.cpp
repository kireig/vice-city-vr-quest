#include <cstdio>
#include <cstdlib>
#include <limits>
#include "VrRagdollSchedule.h"
#define nil nullptr
struct CVector {
    float x = 0, y = 0, z = 0;
    CVector operator-(const CVector &rhs) const { return {x-rhs.x,y-rhs.y,z-rhs.z}; }
    float MagnitudeSqr() const { return x*x+y*y+z*z; }
};
struct CPed { int id = 0; };
struct CTimer {
    static unsigned now;
    static unsigned GetTimeInMilliseconds() { return now; }
};
unsigned CTimer::now = 1000;
// Distinctive retained data catches payload moves, evictions and accidental
// ownership replacement. Lifecycle definitions below are production extracts.
struct Slot {
    CPed *ped = nullptr;
    bool asleep = false, live = false;
    unsigned priorityUntil = 0;
    CVector pos[19], prev[19];
    int payload[256] = {};
};
static constexpr int MAX_POSES = 220;
static Slot slots[MAX_POSES];
static int active;
static bool enabled;
static bool bulletTrace;
static void ForgetAnimatedPose(CPed*) {}
#include "ragdoll-lifecycle-production.inc"
static unsigned checks;
static void Require(bool value, const char *label)
{
    ++checks;
    if(!value) { std::fprintf(stderr,"FAIL: %s\n",label); std::exit(1); }
}
static void Validate()
{
    int count = 0;
    for(int i = 0; i < MAX_POSES; ++i){
        const Slot &s = slots[i];
        if(!s.ped) continue;
        ++count;
        Require(Owns(s.ped) && FindSlot(s.ped) == &slots[i], "exact unique owner lookup");
        for(int j = i+1; j < MAX_POSES; ++j)
            Require(slots[j].ped != s.ped, "no duplicate ped owners");
        for(int j = 0; j < 19; ++j){
            Require(s.pos[j].x == float(s.ped->id+j), "retained joint positions preserved");
            Require(s.prev[j].y == float(s.ped->id-j), "retained joint velocities preserved");
        }
        for(int j = 0; j < 256; ++j)
            Require(s.payload[j] == s.ped->id*1000+j, "entire retained pose payload preserved");
    }
    Require(count == active && active <= MAX_POSES, "ownership count equals bounded ped-pool storage");
}
static Slot *Admit(CPed *ped, bool asleep = false, bool live = false)
{
    if(Slot *existing = FindSlot(ped)) return existing;
    Slot *s = AcquireSlot();
    if(!s) return nullptr;
    *s = {};
    s->ped = ped;
    s->asleep = asleep;
    s->live = live;
    for(int j = 0; j < 19; ++j){
        s->pos[j].x = float(ped->id+j);
        s->prev[j].y = float(ped->id-j);
    }
    for(int j = 0; j < 256; ++j) s->payload[j] = ped->id*1000+j;
    ++active;
    return s;
}
static void Reset()
{
    for(auto &s : slots) s = {};
    active = 0;
    bulletTrace = false;
    CTimer::now = 1000;
}
int main()
{
    CPed peds[2*MAX_POSES+1];
    for(int i = 0; i < 2*MAX_POSES+1; ++i) peds[i].id = i+1;
    Reset();
    Require(!Owns(nullptr) && Thaw(nullptr) == nullptr, "null cannot own or wake a pose");
    Slot *owners[MAX_POSES] = {};
    for(int i = 0; i < MAX_POSES; ++i){
        owners[i] = Admit(&peds[i]);
        Require(owners[i] != nullptr, "220 simultaneous awake bodies admit without waiting for another to settle");
        Require(Admit(&peds[i]) == owners[i], "repeated admission keeps same owner");
    }
    Require(active == MAX_POSES && AcquireSlot() == nullptr, "only complete ped-pool occupancy exhausts storage");
    Require(Admit(&peds[MAX_POSES]) == nullptr, "impossible extra ped cannot overflow fixed ownership storage");
    Validate();
    for(int i = 0; i < MAX_POSES; ++i){
        const bool beforeSleep = owners[i]->asleep;
        CTimer::now += 13;
        Require(Thaw(owners[i]) == owners[i] && FindSlot(&peds[i]) == owners[i], "waking under full pressure never moves or evicts an owner");
        Require(owners[i]->priorityUntil == CTimer::now+350 && owners[i]->asleep == beforeSleep,
            "thaw grants scheduling priority without overwriting pose state");
    }
    Validate();
    Require(!SetBulletTrace(true), "first bullet trace returns previous disabled state");
    for(auto &ped : peds) Require(SkipBulletCollision(&ped) == Owns(&ped), "only owned peds defer native bullet collision");
    Require(SetBulletTrace(false) && !SkipBulletCollision(&peds[0]), "restoring trace flag restores ordinary collision dispatch");
    for(int i = 0; i < MAX_POSES; ++i) Release(&peds[i]);
    Require(active == 0 && AcquireSlot() != nullptr, "all owner releases return bounded storage to empty");
    // Settled corpses, live knockdowns and moving deaths share retention.
    Reset();
    for(int i = 0; i < MAX_POSES; ++i){
        owners[i] = Admit(&peds[i], i%3 == 0, i%5 == 0);
        Require(owners[i] != nullptr, "mixed settled/live/falling burst has no 6/30-body admission cliff");
    }
    Validate();
    for(int i = 0; i < MAX_POSES; ++i){
        Slot *same = owners[i];
        Require(Thaw(same) == same && Owns(&peds[i]), "all saved and awake bodies remain interactive at full capacity");
        Release(&peds[i]);
        Require(!Owns(&peds[i]) && active == MAX_POSES-1, "release removes exactly its owner");
        Release(&peds[i]);
        Require(active == MAX_POSES-1, "repeated release cannot decrement another owner");
        Slot *replacement = Admit(&peds[MAX_POSES+i]);
        Require(replacement == same && active == MAX_POSES, "freed record is reused without moving any other pose");
        if(i%23 == 0) Validate();
    }
    Validate();
    CTimer::now = std::numeric_limits<unsigned>::max()-99u;
    Slot *wrapped = FindSlot(&peds[MAX_POSES]);
    Require(Thaw(wrapped) == wrapped && unsigned(wrapped->priorityUntil-CTimer::now) == 350u,
        "priority duration remains350ms through native timer wrap");
    for(auto &ped : peds) Release(&ped);
    Require(active == 0, "all mixed and reused generations release cleanly");
    Validate();
    std::printf("PASS: %u production lifecycle checks; 220 awake and mixed owners, stable thaw, full-pool recycling, no work-budget admission refusal.\n",checks);
}
