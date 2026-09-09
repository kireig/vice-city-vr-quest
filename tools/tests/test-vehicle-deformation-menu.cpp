#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <initializer_list>
using uint8=unsigned char;
template<class T>static T Min(T a,T b){return a<b?a:b;}
template<class T>static T Max(T a,T b){return a>b?a:b;}
static unsigned checks;
static void Check(bool value,const char*why){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}

// Engine and backend substitutes observe the actual extracted menu dispatch.
// Geometry cleanup and backend idempotence are tested by the deformation module.
namespace VehicleDeformation {
static bool enabled;
static int setterCalls;
static int strength=100,radius=100,maxDent=32,threshold=100;
inline bool IsVehicleDeformationEnabled(){return enabled;}
void SetVehicleDeformationEnabled(bool value){enabled=value;++setterCalls;}
static int GetStrengthPercent(){return strength;}
static int GetRadiusPercent(){return radius;}
static int GetMaxDentCentimeters(){return maxDent;}
static int GetThresholdPercent(){return threshold;}
static void SetStrengthPercent(int value){strength=Min(400,Max(25,value));}
static void SetRadiusPercent(int value){radius=Min(200,Max(50,value));}
static void SetMaxDentCentimeters(int value){maxDent=Min(60,Max(10,value));}
static void SetThresholdPercent(int value){threshold=Min(200,Max(25,value));}
static void ResetTuning(){strength=100;radius=100;maxDent=32;threshold=100;}
}
static std::map<std::string,int> ini;
static std::vector<std::string> writes;
static int GetPrivateProfileIntA(const char*section,const char*key,int fallback,const char*path)
{
 Check(std::strcmp(section,"VR")==0&&std::strcmp(path,".\\vr_settings.ini")==0,"load uses the existing VR INI section and path");
 auto found=ini.find(key);return found==ini.end()?fallback:found->second;
}
static void WritePrivateProfileStringA(const char*section,const char*key,const char*value,const char*path)
{
 Check(std::strcmp(section,"VR")==0&&std::strcmp(path,".\\vr_settings.ini")==0,"save uses the existing VR INI section and path");
 ini[key]=std::atoi(value);writes.push_back(key);
}
enum {VR_MENU_PAGE_SETTINGS,VR_MENU_PAGE_TRAFFIC,VR_MENU_PAGE_RAGDOLL,VR_MENU_PAGE_DEFORMATION,VR_MENU_PAGE_CHEATS,VR_MENU_PAGE_MISSIONS,VR_MENU_PAGE_VEHICLE_CALIBRATION};
enum {QUEST_PHYSICS_DIRECTOR_OFF,QUEST_PHYSICS_DIRECTOR_MEASURE,QUEST_PHYSICS_DIRECTOR_ADAPTIVE,QUEST_PHYSICS_DIRECTOR_MODE_COUNT};
enum {QUEST_PHYSICS_PRESET_QUALITY,QUEST_PHYSICS_PRESET_BALANCED,QUEST_PHYSICS_PRESET_COUNT};
enum {QUEST_VEHICLE_VISUAL_STOCK,QUEST_VEHICLE_VISUAL_REDUCED,QUEST_VEHICLE_VISUAL_MODE_COUNT};
static int gVrMenuPage=VR_MENU_PAGE_TRAFFIC,gVrTrafficSelection;
static int gVrDeformationSelection,gVrMenuNavigateDirection;
static double gVrMenuNavigateRepeatAt,gVrMenuIncreaseRepeatAt,gVrMenuDecreaseRepeatAt;
static bool gVrMenuVisible,gVrMenuShortcutDown,gVrCheatShortcutDown,gVrMenuSelectDown,gVrMenuBackDown,gVrMenuIncreaseDown,gVrMenuDecreaseDown;
static bool inVehicle;
static int shortcutOpens;
static void*FindPlayerVehicle(){return inVehicle?&inVehicle:nullptr;}
static void OpenCheatMenu(){++shortcutOpens;gVrMenuPage=VR_MENU_PAGE_CHEATS;}
namespace OculusVR{static void SetQuestVehicleCalibrationPreview(bool){} inline void InvalidateQuestWeaponLaserOverrides(){}}
struct PadInput{bool b=false,leftStickClick=false;};
static int gTrafficPedPercent=135,gTrafficCarPercent=100;
static int director=QUEST_PHYSICS_DIRECTOR_ADAPTIVE,fixturePreset=QUEST_PHYSICS_PRESET_QUALITY,visual=QUEST_VEHICLE_VISUAL_STOCK;
static int applyTrafficCalls,ragdollOpenCalls;
static int QuestPhysicsDirectorGetMode(){return director;}
static void QuestPhysicsDirectorSetMode(int mode){director=mode;}
static const char*QuestPhysicsDirectorGetModeName(){return "MEASURE";}
static int QuestPhysicsDirectorGetPreset(){return fixturePreset;}
static void QuestPhysicsDirectorSetPreset(int value){fixturePreset=value;}
static const char*QuestPhysicsDirectorGetPresetName(){return "BALANCED";}
static int QuestVehicleVisualBudgetGetMode(){return visual;}
static void QuestVehicleVisualBudgetSetMode(int value){visual=value;}
static const char*QuestVehicleVisualBudgetGetModeName(){return "STOCK";}
static void ApplyTrafficSettings(){++applyTrafficCalls;}
static void OpenRagdollMenu(){++ragdollOpenCalls;gVrMenuPage=VR_MENU_PAGE_RAGDOLL;}
namespace ModelSets {
enum {MODEL_CATEGORY_VEHICLES,MODEL_SET_MODERN};
static bool modern;
static int GetActiveForCategory(int){return modern?MODEL_SET_MODERN:-1;}
}
struct Row {std::string text;int y,scale;bool selected,available,warning;};
static std::vector<Row> drawnRows;
static std::vector<Row> texts;
enum {VR_MENU_WIDTH=1024};
static void BeginFullVrMenuPage(const char*heading,const char*subtitle){drawnRows.clear();texts.clear();texts.push_back({heading,112,4,false,true,false});texts.push_back({subtitle,146,2,false,true,false});}
static void DrawVrMenuText(const char*text,int,int y,int scale,int,int,int){texts.push_back({text,y,scale,false,true,false});}
static void DrawFullVrMenuRow(const char*text,int y,int scale,bool selected,bool available=true,bool warning=false)
{drawnRows.push_back({text,y,scale,selected,available,warning});}
#include "vehicle-deformation-menu-production.inc"

static void Reset()
{
 ini.clear();writes.clear();VehicleDeformation::enabled=false;VehicleDeformation::setterCalls=0;
 gVrMenuPage=VR_MENU_PAGE_TRAFFIC;gVrTrafficSelection=VR_TRAFFIC_VEHICLE_DEFORMATION;
 gTrafficPedPercent=135;gTrafficCarPercent=100;director=QUEST_PHYSICS_DIRECTOR_ADAPTIVE;
 fixturePreset=QUEST_PHYSICS_PRESET_QUALITY;visual=QUEST_VEHICLE_VISUAL_STOCK;
 applyTrafficCalls=ragdollOpenCalls=0;ModelSets::modern=false;
 VehicleDeformation::ResetTuning();gVrDeformationSelection=0;gVrMenuNavigateDirection=0;gVrMenuNavigateRepeatAt=0;
 gVrMenuVisible=gVrMenuShortcutDown=gVrCheatShortcutDown=false;inVehicle=false;shortcutOpens=0;
}
static void LoadAndPersist()
{
 Reset();LoadVehicleDeformationSettings();
 Check(!VehicleDeformation::enabled&&VehicleDeformation::setterCalls==1,"missing INI explicitly loads default OFF through the module setter");
 Check(writes.empty()&&ini.empty(),"loading default OFF does not create or rewrite settings");
 for(int saved:{0,1,-1,27}){
  Reset();ini["VehicleDeformation"]=saved;LoadVehicleDeformationSettings();
  Check(VehicleDeformation::enabled==(saved!=0),"saved boolean loads using the existing nonzero convention");
  Check(writes.empty(),"reading a saved option performs no settings writes");
  DispatchTraffic(true,false);
  Check(gVrMenuPage==VR_MENU_PAGE_DEFORMATION&&writes.empty(),"traffic row opens submenu without changing or saving a setting");
  DispatchDeformation(true,false);
  Check(VehicleDeformation::enabled==(saved==0)&&ini["VehicleDeformation"]==(saved==0?1:0),"actual positive dispatch applies and immediately saves the new state");
  Check(writes.size()==1&&writes[0]=="VehicleDeformation","toggle only writes its own setting");
  const bool expected=VehicleDeformation::enabled;VehicleDeformation::enabled=!expected;
  LoadVehicleDeformationSettings();Check(VehicleDeformation::enabled==expected,"reload restores the state persisted by the menu");
 }
 Reset();OpenDeformationMenu();DispatchDeformation(false,true);
 Check(VehicleDeformation::enabled&&ini["VehicleDeformation"]==1,"left trigger toggles OFF to ON once");
 DispatchDeformation(false,true);
 Check(!VehicleDeformation::enabled&&ini["VehicleDeformation"]==0,"left trigger toggles ON to OFF once");
 Reset();DispatchTraffic(false,false);
 Check(VehicleDeformation::setterCalls==0&&writes.empty(),"no input pulse performs no deformation work");
 gVrMenuPage=VR_MENU_PAGE_SETTINGS;DispatchTraffic(true,true);
 Check(VehicleDeformation::setterCalls==0&&writes.empty(),"traffic action cannot run from another menu page");
}
static void ResetDefaultsAndIsolation()
{
 for(bool enabled:{false,true}){
  Reset();VehicleDeformation::enabled=enabled;gTrafficPedPercent=250;gTrafficCarPercent=15;
  ini["Ragdolls"]=1;ini["RagdollBrakePercent"]=1750;ini["Unrelated"]=37;
  gVrTrafficSelection=VR_TRAFFIC_DEFAULTS;DispatchTraffic(true,false);
  Check(!VehicleDeformation::enabled&&ini["VehicleDeformation"]==0&&VehicleDeformation::setterCalls==1,"traffic defaults explicitly switch deformation OFF and save once");
  Check(gTrafficPedPercent==135&&gTrafficCarPercent==100&&applyTrafficCalls==1,"existing traffic density defaults remain intact");
  Check(ini["Ragdolls"]==1&&ini["RagdollBrakePercent"]==1750&&ini["Unrelated"]==37&&ragdollOpenCalls==0,"traffic defaults preserve all ragdoll and unrelated saved settings");
 }
 for(int item=0;item<VR_TRAFFIC_ITEM_COUNT;++item){
  if(item==VR_TRAFFIC_VEHICLE_DEFORMATION||item==VR_TRAFFIC_DEFAULTS)continue;
  Reset();VehicleDeformation::enabled=true;gVrTrafficSelection=item;DispatchTraffic(true,false);
  Check(VehicleDeformation::enabled&&VehicleDeformation::setterCalls==0&&ini.count("VehicleDeformation")==0,"other traffic rows never alter or persist the deformation option");
  if(item==VR_TRAFFIC_BACK)Check(gVrMenuPage==VR_MENU_PAGE_SETTINGS,"back still returns to settings");
  if(item==VR_TRAFFIC_RAGDOLLS)Check(ragdollOpenCalls==1&&gVrMenuPage==VR_MENU_PAGE_RAGDOLL,"ragdoll row retains its existing submenu action");
 }
}
static void NoHeldToggle()
{
 Reset();bool wasDown=false;double repeatAt=0,holdStart=0;
 OpenDeformationMenu();
 for(int tick=0;tick<1000;++tick){
  const bool pulse=MenuRepeatPulse(true,wasDown,repeatAt,holdStart,tick*16.0,DeformationValueRepeats());
  DispatchDeformation(pulse,false);
 }
 Check(VehicleDeformation::enabled&&VehicleDeformation::setterCalls==1&&writes.size()==1,"a16-second held trigger toggles and persists exactly once");
 Check(!MenuRepeatPulse(false,wasDown,repeatAt,holdStart,16000,DeformationValueRepeats()),"release rearms the actual input latch without an action");
 DispatchDeformation(MenuRepeatPulse(true,wasDown,repeatAt,holdStart,16016,DeformationValueRepeats()),false);
 Check(!VehicleDeformation::enabled&&VehicleDeformation::setterCalls==2&&writes.size()==2,"a second deliberate press toggles OFF exactly once");
 for(int item=0;item<VR_TRAFFIC_ITEM_COUNT;++item){
  gVrTrafficSelection=item;
  Check(TrafficValueRepeats()==(item==VR_TRAFFIC_PEDESTRIANS||item==VR_TRAFFIC_VEHICLES),"existing density rows alone retain value repeat");
 }
}
static void Layout()
{
 Check(VR_TRAFFIC_ITEM_COUNT==9,"traffic now contains nine reachable rows");
 for(bool enabled:{false,true})for(bool modern:{false,true})for(int selected=0;selected<VR_TRAFFIC_ITEM_COUNT;++selected){
  Reset();VehicleDeformation::enabled=enabled;ModelSets::modern=modern;gVrTrafficSelection=selected;
  DrawQuestTrafficPage();Check(drawnRows.size()==VR_TRAFFIC_ITEM_COUNT,"every enum entry has a visible initialized row");
  std::set<std::string> labels;
  for(int i=0;i<VR_TRAFFIC_ITEM_COUNT;++i){
   const Row&row=drawnRows[i];Check(!row.text.empty()&&labels.insert(row.text).second,"row labels are initialized and unique");
   Check(row.selected==(i==selected)&&row.available,"selection maps to the correct available row");
   Check(row.y==166+i*36&&row.scale==2,"nine rows use the intended36px spacing");
   Check(row.y+row.scale*7+4<trafficStatusY,"row highlight cannot overlap the first status readout");
   Check(int(row.text.size())*6*row.scale<=1024-170,"labels fit inside the menu row width");
   for(char c:row.text){bool supported=false;for(const auto&glyph:gDebugGlyphs)supported|=glyph.character==c;Check(supported,"all label characters exist in the actual Quest bitmap font");}
  }
  Check(drawnRows[VR_TRAFFIC_VEHICLE_DEFORMATION].text==std::string("VEHICLE DEFORMATION  < OPEN - ")+(enabled?"ON":"OFF")+" >","deformation row displays submenu access and live module state");
  Check(!drawnRows[VR_TRAFFIC_VEHICLE_DEFORMATION].warning,"the new option is independent of the modern-car visual warning");
  Check(drawnRows[VR_TRAFFIC_RAGDOLLS].text=="RAGDOLL  < OPEN >","existing ragdoll menu text remains unchanged");
  Check(drawnRows[VR_TRAFFIC_DEFAULTS].text.find("DEFORM OFF")!=std::string::npos,"traffic reset states the new OFF default");
 }
}
static void TuningAndNavigation()
{
 using namespace VehicleDeformation;
 const char*keys[]={"VehicleDeformationStrengthPercent","VehicleDeformationRadiusPercent","VehicleDeformationMaxDentCm","VehicleDeformationThresholdPercent"};
 const int low[]={25,50,10,25},high[]={400,200,60,200},defaults[]={100,100,32,100},steps[]={25,10,2,25};
 const int items[]={VR_DEFORMATION_STRENGTH,VR_DEFORMATION_RADIUS,VR_DEFORMATION_MAX_DENT,VR_DEFORMATION_THRESHOLD};
 int(*getters[])()={GetStrengthPercent,GetRadiusPercent,GetMaxDentCentimeters,GetThresholdPercent};
 Reset();LoadVehicleDeformationSettings();
 for(int i=0;i<4;i++)Check(getters[i]()==defaults[i],"missing tuning keys load their explicit defaults");
 Check(ini.empty()&&writes.empty(),"first load never creates an INI or overwrites an existing profile");
 for(int knob=0;knob<4;knob++)for(int saved:{-999,37,125,999}){
  Reset();ini[keys[knob]]=saved;LoadVehicleDeformationSettings();
  Check(getters[knob]()==Min(high[knob],Max(low[knob],saved)),"existing tuning loads through its matching backend clamp");
  Check(ini[keys[knob]]==saved&&writes.empty(),"loading even out-of-range legacy values never rewrites the user's INI");
 }
 for(int knob=0;knob<4;knob++){
  Reset();OpenDeformationMenu();gVrDeformationSelection=items[knob];
  Check(DeformationValueRepeats(),"only tuning rows support held changes");
  for(int direction:{-1,1})for(int i=0;i<250;i++){
   const int previous=getters[knob]();const size_t count=writes.size();
   DispatchDeformation(direction>0,direction<0);
   Check(getters[knob]()==Min(high[knob],Max(low[knob],previous+direction*steps[knob])),"actual tuning dispatch uses correct increment and bounded direction");
   Check(writes.size()==count+1&&writes.back()==keys[knob]&&ini[keys[knob]]==getters[knob](),"each adjustment immediately persists the actual backend value and only its key");
  }
  LoadVehicleDeformationSettings();Check(getters[knob]()==high[knob],"saved upper bound survives reload");
 }
 for(bool state:{false,true}){
  Reset();enabled=state;strength=400;radius=200;maxDent=60;threshold=25;
  ini["RagdollWeightPercent"]=200;ini["VehicleDeformation"]=state?1:0;
  OpenDeformationMenu();gVrDeformationSelection=VR_DEFORMATION_DEFAULTS;
  Check(!DeformationValueRepeats(),"reset remains a discrete action");
  DispatchDeformation(true,false);
  Check(!enabled&&ini["VehicleDeformation"]==0&&writes.size()==5,"explicit reset switches deformation OFF and saves four tuning keys plus the flag");
  for(int i=0;i<4;i++)Check(getters[i]()==defaults[i]&&ini[keys[i]]==defaults[i],"reset restores all exact defaults including 32cm");
  Check(ini["RagdollWeightPercent"]==200,"deformation reset cannot alter ragdoll settings");
 }
 Reset();gVrMenuNavigateDirection=1;gVrMenuNavigateRepeatAt=500;OpenDeformationMenu();
 Check(gVrDeformationSelection==VR_DEFORMATION_ENABLED&&gVrMenuNavigateDirection==0&&gVrMenuNavigateRepeatAt==0,"opening selects enabled row and resets navigation repeat");
 gVrDeformationSelection=VR_DEFORMATION_BACK;Check(!DeformationValueRepeats(),"Back never repeats");DispatchDeformation(true,false);
 Check(gVrMenuPage==VR_MENU_PAGE_TRAFFIC&&gVrTrafficSelection==VR_TRAFFIC_VEHICLE_DEFORMATION,"submenu Back restores the traffic entry selection");
 OpenDeformationMenu();DispatchBack();Check(gVrMenuPage==VR_MENU_PAGE_TRAFFIC&&gVrTrafficSelection==VR_TRAFFIC_VEHICLE_DEFORMATION,"physical B uses the same fast return path");
 Reset();bool wasDown=false;double repeatAt=0,holdStart=0;
 for(int tick=0;tick<1000;tick++){
  const bool pulse=MenuRepeatPulse(true,wasDown,repeatAt,holdStart,tick*16.0,gVrMenuPage==VR_MENU_PAGE_TRAFFIC?TrafficValueRepeats():DeformationValueRepeats());
  if(gVrMenuPage==VR_MENU_PAGE_TRAFFIC)DispatchTraffic(pulse,false);else DispatchDeformation(pulse,false);
 }
 Check(gVrMenuPage==VR_MENU_PAGE_DEFORMATION&&!enabled&&writes.empty(),"holding the opener cannot also toggle the new submenu's first row");
 for(int selected=0;selected<VR_DEFORMATION_ITEM_COUNT;selected++){
  gVrDeformationSelection=selected;DrawQuestDeformationPage();
  Check(drawnRows.size()==7,"all seven deformation settings and navigation entries are drawn");
  std::vector<Row> all=texts;all.insert(all.end(),drawnRows.begin(),drawnRows.end());
  for(size_t i=0;i<all.size();i++){
   Check(all[i].text.size()*6*all[i].scale<=1024-56&&all[i].y>=28&&all[i].y+7*all[i].scale<740,"submenu labels/help fit the display");
   if(all[i].text.empty())continue;
   for(size_t j=i+1;j<all.size();j++)if(!all[j].text.empty())Check(all[i].y+7*all[i].scale<=all[j].y||all[j].y+7*all[j].scale<=all[i].y,"submenu rows and all help lines remain separate");
   for(char c:all[i].text){bool supported=false;for(const auto&glyph:gDebugGlyphs)supported|=glyph.character==c;Check(supported,"submenu uses supported bitmap glyphs");}
  }
  for(int i=0;i<7;i++)Check(drawnRows[i].selected==(i==selected),"submenu highlight follows the actual selected index");
 }
}
static void VehicleCheatShortcut()
{
 Reset();PadInput in;in.b=true;inVehicle=true;
 for(int i=0;i<50;i++)DispatchShortcut(in,true);
 Check(shortcutOpens==0&&!gVrMenuVisible&&gVrCheatShortcutDown,"vehicle B chord leaves gameplay active while latching the held chord");
 Check(!VrMenuConsumesInput(),"actual CapturePad and weapon-input gate leaves vehicle B available for gameplay");
 inVehicle=false;DispatchShortcut(in,true);
 Check(shortcutOpens==0,"leaving vehicle while holding B cannot open cheats accidentally");
 in.b=false;DispatchShortcut(in,true);in.b=true;DispatchShortcut(in,true);
 Check(shortcutOpens==1&&gVrMenuVisible,"a new on-foot chord still opens cheats");
 Check(VrMenuConsumesInput(),"normal open VR menu still owns input");
 for(int i=0;i<50;i++)DispatchShortcut(in,true);
 Check(shortcutOpens==1,"held on-foot shortcut toggles only once");
 in.b=false;DispatchShortcut(in,true);in.b=true;DispatchShortcut(in,false);
 Check(shortcutOpens==1,"normal B without both-grip modifier never opens cheats");
}
int main(){LoadAndPersist();ResetDefaultsAndIsolation();NoHeldToggle();Layout();TuningAndNavigation();VehicleCheatShortcut();std::printf("PASS: vehicle deformation menu %u checks; actual submenu load/save/reset/repeat/layout and vehicle cheat-shortcut suppression.\n",checks);}
