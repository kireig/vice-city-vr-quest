#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
using uint8=unsigned char;
#define nil nullptr
enum {VR_MENU_WIDTH=1024,VR_MENU_HEIGHT=768,QUEST_CPU_PERFORMANCE_COUNT=3,VR_OCCLUSION_CULLING_AUTHORED=2};
static int gVrGraphicsSelection,gQuestRenderScalePercent=175,gQuestMsaaSamples=4,gSpatialAaMode=1,gPs2AlphaTest=1,gGenerateMipmaps=1;
static int gFoliageSoftness=3,gRenderDiagnostics=1,gViceCityColorEnabled=1,gQuestCpuPerformanceMode=2,gQuestGpuPerformanceMode=2;
static int gDynamicLights=2,gOcclusionCullingMode=2,gDistanceFog=1,gQuestQuickTestStart=1;
namespace xrvk {
enum {RENDER_SCALE_FALLBACK_NONE=0};
struct RenderScaleStatus {float effectivePercent=100;unsigned actualWidth=2064,actualHeight=2272,recommendedWidth=2064,recommendedHeight=2272;int fallbackReason=0,previousFallbackReason=0,previousFallbackRequestedPercent=175,previousFallbackPercent=100;};
static bool valid=true;static RenderScaleStatus status;
static bool getRenderScaleStatus(RenderScaleStatus*out){*out=status;return valid;}
static int getActivePerformanceMode(){return 2;}
static int getActiveGpuPerformanceMode(){return 2;}
static const char*getRenderScaleFallbackReasonName(int){return "GAME RENDERER ALLOCATION";}
}
namespace VrCullViz {static bool active;static int calls;inline bool IsActive(){++calls;return active;}inline const char*StatusLine(){++calls;return "CULL SNAPSHOT: DRAWN 123 OUTSIDE 456 OCCLUDED 789";}}
namespace CRenderer {static const char*GetVrOcclusionCullingModeName(){return "AGGRESSIVE EXPERIMENTAL";}}
namespace CParticleObject {static const char*GetVrFountainQualityName(){return "OPTIMIZED";}}
namespace CShadows {static bool IsRenderEnabled(){return true;}}
static bool QuestProfilerIsEnabled(){return true;}
struct Rect {int left,top,right,bottom;};
struct Text {std::string text;Rect bounds;};
struct Row {std::string text;int y,scale;bool selected;};
static std::vector<Text> texts;static std::vector<Row> drawnRows;static std::vector<Rect> highlights;
static unsigned checks;
static void Check(bool value,const char*why){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static bool Overlap(Rect a,Rect b){return a.left<b.right&&a.right>b.left&&a.top<b.bottom&&a.bottom>b.top;}
static void FillVrMenuRect(int l,int t,int r,int b,uint8,uint8,uint8,uint8){if(l==85&&r==VR_MENU_WIDTH-85)highlights.push_back({l,t,r,b});}
static void DrawVrMenuText(const char*text,int center,int y,int scale,uint8,uint8,uint8){int width=int(std::strlen(text))*6*scale;texts.push_back({text,{center-width/2,y,center+width/2,y+7*scale}});}
static void QuestMenuPageColour(uint8*r,uint8*g,uint8*b){*r=100;*g=225;*b=255;}
static void DrawFullVrMenuRow(const char*,int,int,bool,bool=true,bool=false,bool=false,bool=false);
#include "graphics-menu-layout-production.inc"
static void DrawFullVrMenuRow(const char*text,int y,int scale,bool selected,bool available,bool warning,bool positive,bool highlight){drawnRows.push_back({text,y,scale,selected});DrawFullVrMenuRowNative(text,y,scale,selected,available,warning,positive,highlight);}
static void Clear(){texts.clear();drawnRows.clear();highlights.clear();VrCullViz::calls=0;}
static void Verify(){
#if MIAMIVR_DEV_TOOLS
 Check(VR_GRAPHICS_ITEM_COUNT==20,"opt-in developer build includes its visualizer row");
#else
 Check(VR_GRAPHICS_ITEM_COUNT==18&&VrCullViz::calls==0,"shipping graphics has no visualizer or quick-test row or backend calls");
 for(const auto&t:texts)Check(t.text.find("CULLING VISUALIZER")==std::string::npos&&t.text.find("CULL SNAPSHOT")==std::string::npos&&t.text.find("QUICK TEST START")==std::string::npos,"shipping text omits developer visualizer and quick-start controls");
#endif
 Check(drawnRows.size()==VR_GRAPHICS_ITEM_COUNT,"all graphics settings plus Back drawn exactly once");
 Check(drawnRows.back().text=="BACK TO SETTINGS","Back remains last independent menu item");
 Check(highlights.size()==1,"exactly selected controller row highlighted");
 for(size_t i=0;i<drawnRows.size();i++){
  Check(drawnRows[i].selected==(int(i)==gVrGraphicsSelection),"selection and drawn highlight use identical row index");
  Check(drawnRows[i].scale==2,"graphics rows use compact readable font");
  Rect extent={85,drawnRows[i].y-5,VR_MENU_WIDTH-85,drawnRows[i].y+drawnRows[i].scale*7+4};
  Check(extent.top>=160&&extent.bottom<674,"row and selection background fit below subtitle and before status footer");
  if(i)Check(drawnRows[i-1].y+drawnRows[i-1].scale*7+4<=extent.top,"adjacent row selection areas do not overlap");
  if(drawnRows[i].selected)Check(highlights[0].top==extent.top&&highlights[0].bottom==extent.bottom,"actual shared row painter matches row selection bounds");
 }
 for(size_t i=0;i<texts.size();i++){
  const Rect a=texts[i].bounds;
  Check(a.left>=0&&a.right<=VR_MENU_WIDTH&&a.top>=0&&a.bottom<=VR_MENU_HEIGHT,"all visible text remains inside menu surface");
  for(size_t j=i+1;j<texts.size();j++)Check(!Overlap(a,texts[j].bounds),"header, all rows and simultaneous status lines remain separate");
 }
 for(const Text&t:texts)if(t.bounds.top>=674)Check(!Overlap(highlights[0],t.bounds),"Back selection background cannot cover footer text");
}
int main(){
 for(int scenario=0;scenario<4;scenario++)for(int selected=0;selected<VR_GRAPHICS_ITEM_COUNT;selected++){
  Clear();gVrGraphicsSelection=selected;xrvk::valid=scenario!=0;
  xrvk::status.fallbackReason=scenario==3?1:0;xrvk::status.previousFallbackReason=scenario>=2?1:0;VrCullViz::active=scenario==2;
  DrawQuestGraphicsPage();Verify();
 }
 std::printf("Graphics layout: %u checks PASS; %d rows, %d rendered states, dev-tools %d; every selectable row and footer separate.\n",checks,VR_GRAPHICS_ITEM_COUNT,4*VR_GRAPHICS_ITEM_COUNT,MIAMIVR_DEV_TOOLS);
}
