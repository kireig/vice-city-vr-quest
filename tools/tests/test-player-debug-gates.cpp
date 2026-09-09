#include <cstdio>
#include <cstdlib>
#include <cstdint>
#define nil nullptr
#define TRUE 1
using uint32=uint32_t;
static int checks,iniReads,initCalls,loadCalls,idleCalls,fovCalls,skipCalls,timeChanges,logCalls;
static void Check(bool value,const char*message){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
[[maybe_unused]] static int GetPrivateProfileIntA(const char*,const char*,int,const char*){++iniReads;return 1;}
[[maybe_unused]] static void Log(const char*,...){++logCalls;}
#define ALOG(...) Log(__VA_ARGS__)
struct CPlayerPed{};static CPlayerPed fixturePlayer;
[[maybe_unused]] static CPlayerPed*FindPlayerPed(){return &fixturePlayer;}
struct CGame {inline static bool playingIntro=true;};
struct CTimer {inline static float scale=1;static void SetTimeScale(float x){scale=x;++timeChanges;}static float GetTimeScale(){return scale;}};
struct CCutsceneMgr {
 inline static bool running=true,processing=false;
 static bool IsRunning(){return running;}static bool IsCutsceneProcessing(){return processing;}
 static const char*GetCutsceneName(){return "INTRO";}static void FinishCutscene(){++skipCalls;}
};
struct CPad {bool disabled=true;static CPad*GetPad(int){static CPad p;return &p;}bool ArePlayerControlsDisabled(){return disabled;}};
struct Camera {bool m_WideScreenOn=false;void FinishCutscene(){++skipCalls;}};static Camera TheCamera;
struct Frontend {bool m_bGameNotLoaded=true,m_bMenuActive=true,m_bStartUpFrontEndRequested=false,m_bWantToRestart=true,m_bWantToLoad=true;};
static Frontend FrontEndMenuManager;
enum {GS_INIT_FRONTEND,GS_FRONTEND,GS_PLAYING_GAME,rsIDLE};static int gGameState=GS_INIT_FRONTEND;
static void LoadingScreen(void*,void*,const char*){++loadCalls;}
[[maybe_unused]] static void InitialiseGame(){++initCalls;}
static void RsEventHandler(int,void*){++idleCalls;}
static void VrRestoreFov(){++fovCalls;}
#include "player-debug-gates-production.inc"
int main(){
 Check(RunFrontend(),"normal or requested quick-start entry completes");
#if MIAMIVR_DEV_TOOLS
 Check(gGameState==GS_PLAYING_GAME&&initCalls==1&&iniReads==1,"explicit developer build retains requested quick start");
 Check(!FrontEndMenuManager.m_bGameNotLoaded&&!FrontEndMenuManager.m_bWantToLoad&&!FrontEndMenuManager.m_bMenuActive,"developer quick-start establishes native new-game frontend state");
 RunStep();Check(skipCalls==1&&CTimer::scale==6,"developer helper skips once and advances scripted intro");
 RunStep();Check(skipCalls==1,"same cutscene is not repeatedly finished");
 CCutsceneMgr::running=false;CGame::playingIntro=false;CPad::GetPad(0)->disabled=false;
 for(int i=0;i<90;++i)RunStep();
 Check(gQuestQuickStartSkipFrames==0&&CTimer::scale==1,"developer intro skip disarms and restores time scale");
 CCutsceneMgr::running=true;RunStep();Check(skipCalls==1,"later gameplay cutscenes remain untouched after disarm");
 Check(logCalls>0,"developer mode preserves its diagnostics");
#else
 Check(gGameState==GS_FRONTEND&&FrontEndMenuManager.m_bGameNotLoaded&&FrontEndMenuManager.m_bStartUpFrontEndRequested,"player build preserves native frontend even when legacy INI returns ON");
 Check(iniReads==0&&initCalls==0,"player startup does not read developer INI or automatically start a game");
 gGameState=GS_PLAYING_GAME;
 for(int i=0;i<1000;++i)RunStep();
 Check(iniReads==0&&initCalls==0&&skipCalls==0&&timeChanges==0&&logCalls==0,"player idle has no quick-start reads, skips, time manipulation or diagnostics");
 Check(CTimer::scale==1,"normal player time scale retained");
#endif
 Check(idleCalls==fovCalls&&idleCalls>0,"ordinary game logic and FOV restoration remain paired");
 Check(loadCalls==1,"frontend loads its normal splash once");
 std::printf("PASS: %d actual Android startup/idle checks (developer=%d).\n",checks,MIAMIVR_DEV_TOOLS);
}
