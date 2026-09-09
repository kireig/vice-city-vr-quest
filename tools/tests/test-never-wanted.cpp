#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <cmath>
#include <cstddef>
#define nil nullptr
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define VALIDATE_SIZE(type,size)
#define FIX_SIGNIFICANT_BUGS
using int32=int32_t;using uint32=uint32_t;using int16=int16_t;using uint8=uint8_t;
struct CVector{float x=0,y=0,z=0;};
#include "../../src/core/Wanted.h"
static int checks=0;
static void Check(bool value,const char*label){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
template<class T>static T Min(T a,T b){return a<b?a:b;}
template<class T>static T Max(T a,T b){return a>b?a:b;}
static void Error(const char*){std::abort();}
static uint32 timeMs=1000;
struct CTimer{static uint32 GetTimeInMilliseconds(){return timeMs;}};
struct CStats{inline static int WantedStarsAttained=0,WantedStarsEvaded=0;};
struct CDarkel{static bool FrenzyOnGoing(){return false;}};
struct CCullZones{static int GetWantedLevelDrop(){return 0;}};
static struct Audio{int reports=0;void ReportCrime(eCrimeType,CVector){++reports;}}DMAudio;
static CVector FindPlayerCoors(){return {};}
struct CGeneral{static float GetRandomNumberInRange(float,float){return 3;}};
enum{OBJECTIVE_NONE=0,PED_NONE=0};
class CCopPed {
public:
    bool m_bIsInPursuit=true,dead=false;
    int m_objective=4,m_prevObjective=5,m_nLastPedState=6,wanders=0;
    bool DyingOrDead(){return dead;}
    void SetWanderPath(float){++wanders;}
};
int32 CWanted::WorkOutPolicePresence(CVector,float){return 0;}
#pragma warning(push)
// These conversions are unchanged native game code (float crime amounts and
// byte cop counters); retain strict warnings for the fixture itself.
#pragma warning(disable:4244 4018 4389)
#include "never-wanted-production.inc"
#pragma warning(pop)
static int Queued(const CWanted&w){int n=0;for(const auto&c:w.m_aCrimes)if(c.m_nType!=CRIME_NONE)++n;return n;}
static void Fresh(CWanted&w){CWanted::bNoWantedCheat=false;CWanted::SetMaximumWantedLevel(6);w.Initialise();timeMs=1000;DMAudio.reports=0;}
static void Zero(const CWanted&w,const char*label){Check(w.m_nWantedLevel==0&&w.m_nChaos==0&&w.m_nMinChaos==0&&w.m_nMinWantedLevel==0,label);}
int main(){
    CWanted w;Fresh(w);
    Check(!CWanted::bNoWantedCheat&&!w.IsIgnoredByCops()&&!w.IsIgnored(),"default leaves normal police behavior");
    w.SetWantedLevel(3);Check(w.GetWantedLevel()==3,"OFF native scripted wanted applies");
    w.RegisterCrime(CRIME_SHOOT_COP,{},100,false);Check(Queued(w)==1,"OFF native deferred crime queues");
    w.Suspend();Check(w.m_nMinWantedLevel==3&&w.m_nMinChaos>0,"native suspension creates restorable minimum");
    CCopPed cops[10];cops[9].dead=true;
    for(int i=0;i<10;++i)w.m_pCops[i]=&cops[i];w.m_CurrentCops=10;
    const int levelCap=CWanted::MaximumWantedLevel,chaosCap=CWanted::nMaximumWantedLevel;
    w.SetNoWantedCheat(true);
    Zero(w,"enable clears current and suspended wanted");
    Check(Queued(w)==0&&w.m_CurrentCops==0,"enable clears pending crimes and pursuit count");
    Check(w.IsIgnoredByCops()&&w.IsIgnored(),"cheat uses effective ignore without changing mission state");
    Check(!w.m_bIgnoredByCops&&!w.m_bIgnoredByEveryone,"enable does not mutate mission-owned flags");
    Check(CWanted::MaximumWantedLevel==levelCap&&CWanted::nMaximumWantedLevel==chaosCap,"enable preserves saved story caps");
    for(int i=0;i<10;++i){
        Check(!w.m_pCops[i]&&!cops[i].m_bIsInPursuit,"all pursuit slots released");
        Check(cops[i].m_objective==OBJECTIVE_NONE&&cops[i].m_prevObjective==OBJECTIVE_NONE&&cops[i].m_nLastPedState==PED_NONE,"pursuing objectives cleared");
        Check(cops[i].wanders==(i==9?0:1),"living cops resume wandering and dead cops stay dead");
    }
    for(int level=0;level<=6;++level){
        w.SetWantedLevel(level);Zero(w,"ON blocks direct scripted wanted");
        w.SetWantedLevelNoDrop(level);Zero(w,"ON blocks no-drop script/cop path");
        w.CheatWantedLevel(level);Zero(w,"ON blocks raise-wanted cheat path");
    }
    for(int crime=CRIME_HIT_PED;crime<NUM_CRIME_TYPES;++crime){
        const auto type=static_cast<eCrimeType>(crime);
        w.RegisterCrime(type,{},uint32(crime),false);
        w.RegisterCrime_Immediately(type,{},uint32(100+crime),false);
        w.ReportCrimeNow(type,{},false);
        Check(Queued(w)==0,"ON neither delayed nor immediate crimes remain queued");
        Zero(w,"ON crime reporter cannot increase chaos or wanted");
    }
    Check(DMAudio.reports==0,"ON suppresses new police radio crime reports");
    w.m_nChaos=9000;w.m_nMinChaos=1200;w.m_nMinWantedLevel=4;w.UpdateWantedLevel();
    Zero(w,"central update closes direct chaos and suspension bypass");
    w.m_pCops[0]=&cops[0];w.m_CurrentCops=1;cops[0].m_bIsInPursuit=true;
    w.m_nChaos=5000;w.m_nWantedLevel=6;w.Update();
    Zero(w,"per-frame enforcement clears restored transient wanted state");
    Check(!w.m_pCops[0]&&!cops[0].m_bIsInPursuit&&w.m_CurrentCops==0,"tick releases late added pursuit");
    Check(w.NumOfHelisRequired()==0,"ON requests no wanted helicopters");
    for(int flags=0;flags<4;++flags){
        w.m_bIgnoredByCops=(flags&1)!=0;w.m_bIgnoredByEveryone=(flags&2)!=0;
        w.SetNoWantedCheat(false);
        Check(w.IsIgnoredByCops()==((flags&1)!=0)&&w.IsIgnored()==(flags!=0),"OFF restores effective native ignore semantics");
        Check(w.m_bIgnoredByCops==((flags&1)!=0)&&w.m_bIgnoredByEveryone==((flags&2)!=0),"OFF preserves mission flags unchanged");
        w.SetNoWantedCheat(true);
        Check(w.IsIgnoredByCops()&&w.IsIgnored(),"ON overrides either mission ignore value without writing it");
    }
    w.m_bIgnoredByCops=false;w.m_bIgnoredByEveryone=false;
    w.SetNoWantedCheat(false);Zero(w,"disabling does not resurrect suspended old stars");
    timeMs+=30000;w.Update();Check(Queued(w)==0&&w.GetWantedLevel()==0,"disabling has no delayed crime debt");
    w.ReportCrimeNow(CRIME_SHOOT_COP,{},false);Check(w.GetWantedLevel()==1&&DMAudio.reports==1,"OFF normal crime and police radio resume");
    w.SetWantedLevel(5);Check(w.GetWantedLevel()==5&&w.NumOfHelisRequired()==1,"OFF native scripted pursuit and helicopter requirement resume");
    for(int cap=0;cap<=6;++cap){
        Fresh(w);CWanted::SetMaximumWantedLevel(cap);
        const int originalChaosCap=CWanted::nMaximumWantedLevel;
        w.SetNoWantedCheat(true);w.SetWantedLevel(6);w.ReportCrimeNow(CRIME_SHOOT_HELI,{},false);w.Update();
        Check(CWanted::MaximumWantedLevel==cap&&CWanted::nMaximumWantedLevel==originalChaosCap,"ON never mutates either serialized maximum");
        w.SetNoWantedCheat(false);w.SetWantedLevel(6);Check(w.GetWantedLevel()==cap,"OFF continues current story maximum instead of forcing six");
    }
    Fresh(w);w.SetNoWantedCheat(true);
    CWanted loaded;loaded.Initialise();
    Check(CWanted::bNoWantedCheat&&loaded.IsIgnoredByCops(),"new/load player inherits session toggle");
    loaded.SetWantedLevel(6);Zero(loaded,"new/load player remains never wanted");
    loaded.Reset();Check(CWanted::bNoWantedCheat&&loaded.IsIgnoredByCops(),"native reset leaves session toggle active");
    loaded.SetNoWantedCheat(false);loaded.SetWantedLevel(2);
    Check(loaded.GetWantedLevel()==2&&!loaded.IsIgnoredByCops(),"OFF after player recreation restores native behavior");
    std::printf("Never-wanted production regression: %d checks passed\n",checks);
}
