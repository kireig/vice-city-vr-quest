#include <cstdio>
#include <cstdlib>

struct CVector { float x = 0, y = 0, z = 0; };
struct CColPoint { CVector point; };
enum eWeaponType { WEAPONTYPE_UZI, WEAPONTYPE_SNIPERRIFLE };
struct CEntity {
    bool ped = false;
    bool IsPed() const { return ped; }
};
struct CPed : CEntity {
    bool dead = true, owned = false, admit = true;
    CPed() { ped = true; }
    bool DyingOrDead() const { return dead; }
};

static void Require(bool value, const char *label)
{
    if(!value) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}

struct Reply { bool hit = false; CEntity *entity = nullptr; float x = 0; };
struct Trace { bool trace = false; bool flags[8] = {}; };
static Reply replies[2];
static Trace traces[2];
static int worldCalls, beginCalls, bulletCalls;
static bool scopedTrace, bulletTraceState;
static CPed *bulletPed;
static CVector bulletEnd;
static CEntity *bulletShooter;
static int bulletType;

namespace VrRagdoll {
bool SetBulletTrace(bool enabled)
{
    bool previous = scopedTrace;
    scopedTrace = enabled;
    return previous;
}
bool Owns(CPed *ped) { return ped->owned; }
bool Begin(CPed *ped)
{
    ++beginCalls;
    ped->owned = ped->admit;
    return ped->admit;
}
CPed *BulletHit(const CVector &, const CVector &end, int type,
    CColPoint &point, CEntity *shooter)
{
    ++bulletCalls;
    bulletEnd = end;
    bulletType = type;
    bulletShooter = shooter;
    bulletTraceState = scopedTrace;
    point.point.x = 3.0f;
    return bulletPed && bulletPed->owned ? bulletPed : nullptr;
}
}

struct CWorld {
static bool ProcessLineOfSight(const CVector &, const CVector &,
    CColPoint &point, CEntity *&entity, bool buildings, bool vehicles,
    bool peds, bool objects, bool dummies, bool seeThrough,
    bool someObjects, bool shootThrough)
{
    Require(worldCalls < 2, "bounded world traces");
    Trace &trace = traces[worldCalls];
    trace.trace = scopedTrace;
    const bool flags[] = {buildings, vehicles, peds, objects, dummies,
        seeThrough, someObjects, shootThrough};
    for(int i = 0; i < 8; ++i) trace.flags[i] = flags[i];
    const Reply &reply = replies[worldCalls++];
    point.point.x = reply.x;
    entity = reply.entity;
    return reply.hit;
}
};
struct CWeapon {
static bool ProcessLineOfSight(const CVector &, const CVector &, CColPoint &,
    CEntity *&, eWeaponType, CEntity *, bool, bool, bool, bool, bool, bool, bool);
};

// The runner extracts this definition directly from the staged production file.
#include "ragdoll-ray-resolver-production.inc"

static void Reset()
{
    worldCalls = beginCalls = bulletCalls = 0;
    scopedTrace = bulletTraceState = false;
    bulletPed = nullptr;
    bulletEnd = {};
    bulletShooter = nullptr;
    bulletType = -1;
    for(int i = 0; i < 2; ++i) { replies[i] = {}; traces[i] = {}; }
}
static bool Fire(CColPoint &point, CEntity *&entity, CEntity *shooter = nullptr,
    bool peds = true)
{
    const CVector source{}, end{10.0f, 0.0f, 0.0f};
    return CWeapon::ProcessLineOfSight(source, end, point, entity,
        WEAPONTYPE_SNIPERRIFLE, shooter, true, false, peds, true,
        false, true, false);
}

int main()
{
    CEntity wall, shooter;
    CPed corpse, owned;
    owned.owned = true;
    CColPoint point{};
    CEntity *entity = nullptr;

    Reset();
    replies[0] = {true, &wall, 5.0f};
    Require(Fire(point, entity, &shooter), "world blocker retained");
    Require(entity == &wall && point.point.x == 5.0f, "world result unchanged");
    Require(bulletCalls == 1 && bulletEnd.x == 5.0f, "ray clipped at wall");
    Require(bulletShooter == &shooter && bulletType == WEAPONTYPE_SNIPERRIFLE,
        "shot context forwarded");
    Require(traces[0].trace && !scopedTrace && !bulletTraceState,
        "trace state scoped to world query");
    const bool expected[] = {true, false, true, true, false, true, false, true};
    for(int i = 0; i < 8; ++i)
        Require(traces[0].flags[i] == expected[i], "caller flags forwarded");

    Reset();
    replies[0] = {true, &corpse, 4.0f};
    replies[1] = {true, &wall, 8.0f};
    bulletPed = &corpse;
    Require(Fire(point, entity), "lazy corpse custom hit");
    Require(beginCalls == 1 && worldCalls == 2 && bulletCalls == 2 && corpse.owned,
        "lazy admission retraces collider");
    Require(traces[0].trace && traces[1].trace && !scopedTrace,
        "both queries inside trace scope");
    Require(bulletEnd.x == 8.0f && entity == &corpse && point.point.x == 3.0f,
        "custom point and ped override within new wall bound");

    Reset();
    scopedTrace = true;
    Require(!Fire(point, entity), "no geometry remains miss");
    Require(scopedTrace && bulletTraceState, "nested trace state restored");
    Require(bulletEnd.x == 10.0f, "unobstructed full segment");

    Reset();
    corpse.owned = false;
    replies[0] = {true, &corpse, 4.0f};
    Require(Fire(point, entity, nullptr, false), "non-ped query world result");
    Require(beginCalls == 0 && bulletCalls == 0 && !traces[0].flags[2],
        "non-ped query never admits or tests simulated bodies");

    Reset();
    corpse.admit = false;
    replies[0] = {true, &corpse, 4.0f};
    Require(Fire(point, entity), "denied admission leaves stock hit");
    Require(beginCalls == 1 && worldCalls == 1 && !corpse.owned,
        "failed admission does not retrace");
    Require(entity == &corpse && point.point.x == 4.0f && bulletEnd.x == 4.0f,
        "denied corpse still clips the shot");

    Reset();
    bulletPed = &owned;
    Require(Fire(point, entity), "owned body hit without stock world hit");
    Require(entity == &owned && point.point.x == 3.0f && beginCalls == 0,
        "owned body supplies actual contact");

    Reset();
    corpse.owned = false;
    corpse.admit = true;
    replies[0] = {true, &corpse, 8.0f};
    bulletPed = &owned;
    Require(Fire(point, entity), "near owned body precedes farther stock corpse");
    Require(entity == &owned && beginCalls == 0 && worldCalls == 1 &&
        bulletCalls == 1 && !corpse.owned,
        "front body hit does not admit a stock corpse behind it");

    std::puts("PASS: production weapon resolver, seven scope/admission/occlusion scenarios");
}
