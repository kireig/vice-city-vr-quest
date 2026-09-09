#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "VrRagdollSettings.h"

using uint8 = unsigned char;
using uint32 = unsigned int;
template<class T> static T Min(T a, T b) { return a < b ? a : b; }
template<class T> static T Max(T a, T b) { return a > b ? a : b; }

static int checks;
static void Check(bool condition, const char *what)
{
	++checks;
	if(!condition){ std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
static std::map<std::string,int> settingsFile;
static int settingsWrites;
static int GetPrivateProfileIntA(const char *section, const char *key,
	int fallback, const char *path)
{
	Check(std::strcmp(section,"VR") == 0, "ragdoll settings use VR section");
	Check(std::strcmp(path,".\\vr_settings.ini") == 0, "ragdoll settings use existing INI");
	const auto found = settingsFile.find(key);
	return found == settingsFile.end() ? fallback : found->second;
}
static void SaveVrInteger(const char *key, int value)
{
	settingsFile[key] = value;
	++settingsWrites;
}
namespace VrRagdoll {
static bool enabled;
static void SetEnabled(bool value) { enabled = value; }
static bool IsEnabled() { return enabled; }
static void SetBrakePercent(int value) { VrRagdollSettings::SetBrake(value); }
static int GetBrakePercent() { return VrRagdollSettings::Get().brakePercent; }
static void SetGripPercent(int value) { VrRagdollSettings::SetGrip(value); }
static int GetGripPercent() { return VrRagdollSettings::Get().gripPercent; }
static void SetShotPercent(int value) { VrRagdollSettings::SetShot(value); }
static int GetShotPercent() { return VrRagdollSettings::Get().shotPercent; }
static void SetWeightPercent(int value) { VrRagdollSettings::SetWeight(value); }
static int GetWeightPercent() { return VrRagdollSettings::Get().weightPercent; }
inline const char *DebugLine() { return "RD5 ON   ACTIVE 6   SAVED 24"; }
inline const char *VehicleDebugLine() { return "CAR HITS 12   LAST BRAKE 12.5 KM/H"; }
}
namespace OculusVR {
enum { QUEST_VEHICLE_CAL_HAND = 0 };
static int GetQuestVehicleCalibrationItemCount() { return 2; }
static int GetQuestVehicleCalibrationItemForRow(int row) { return row; }
static int GetQuestHolsterPointCount() { return 3; }
static void SetQuestVehicleCalibrationPreview(bool) {}
}
static int GetVrCheatCount() { return 4; }
static int GetVrMissionCategoryCount() { return 5; }
static int GetVrMissionCount(int) { return 6; }

struct TextDraw { std::string text; int y, scale; bool selected; };
static std::vector<TextDraw> draws;
static int drawnCategory;
static void QuestMainCategoryColour(int category, uint8 *r, uint8 *g, uint8 *b)
{ drawnCategory = category; *r = 125; *g = 255; *b = 145; }
static void BeginFullVrMenuPage(const char *heading, const char *subtitle);
static void DrawVrMenuText(const char *text, int centreX, int y, int scale, uint8, uint8, uint8);
static void DrawFullVrMenuRow(const char *text, int y, int scale, bool selected);

#include "ragdoll-menu-production.inc"

static void RecordText(const char *text, int centreX, int y, int scale, bool selected)
{
	const int halfWidth = (int)std::strlen(text)*scale*6/2;
	Check(centreX-halfWidth >= 28 && centreX+halfWidth <= VR_MENU_WIDTH-28,
		"menu text fits within panel width");
	Check(y >= 28 && y+scale*7 < VR_MENU_HEIGHT-28, "menu text fits within panel height");
	for(const char *c = text; *c; ++c){
		bool supported = false;
		for(const auto &glyph : gDebugGlyphs)
			if(glyph.character == *c) supported = true;
		Check(supported, "all ragdoll text uses available glyphs");
	}
	draws.push_back({text,y,scale,selected});
}
static void BeginFullVrMenuPage(const char *heading, const char *subtitle)
{
	RecordText(heading, VR_MENU_WIDTH/2,112,4,false);
	RecordText(subtitle,VR_MENU_WIDTH/2,146,2,false);
}
static void DrawVrMenuText(const char *text, int centreX, int y, int scale, uint8, uint8, uint8)
{ RecordText(text,centreX,y,scale,false); }
static void DrawFullVrMenuRow(const char *text, int y, int scale, bool selected)
{ RecordText(text,VR_MENU_WIDTH/2,y,scale,selected); }

int main()
{
	LoadRagdollSettings();
	Check(!VrRagdoll::IsEnabled() && gRagdolls == 0, "missing INI defaults ragdolls OFF");
	Check(VrRagdoll::GetBrakePercent() == 200 && VrRagdoll::GetGripPercent() == 150 &&
		VrRagdoll::GetShotPercent() == 100, "fresh install loads stronger defaults");
	Check(VrRagdoll::GetWeightPercent() == 200, "missing weight key defaults to the user's 200 percent body mass");
	Check(settingsWrites == 0, "loading leaves the settings file untouched");
	settingsFile["Ragdolls"] = 1;
	settingsFile["RagdollBrakePercent"] = -200;
	settingsFile["RagdollGripPercent"] = 3000;
	settingsFile["RagdollShotPercent"] = -1;
	settingsFile["RagdollWeightPercent"] = -1;
	LoadRagdollSettings();
	Check(VrRagdoll::IsEnabled(), "upgrade preserves existing enabled flag");
	Check(VrRagdoll::GetBrakePercent() == 0 && VrRagdoll::GetGripPercent() == 300 &&
		VrRagdoll::GetShotPercent() == 25, "invalid INI values use production clamps");
	Check(VrRagdoll::GetWeightPercent() == 50, "invalid weight uses the safe lower mass bound");

	gVrMenuPage = VR_MENU_PAGE_TRAFFIC;
	gVrTrafficSelection = VR_TRAFFIC_RAGDOLLS;
	gVrMenuNavigateDirection = 1;
	gVrMenuNavigateRepeatAt = 10.0;
	OpenRagdollMenu();
	Check(gVrMenuPage == VR_MENU_PAGE_RAGDOLL && CurrentMenuItemCount() == 7,
		"opening enters seven-row submenu");
	Check(CurrentMenuSelection() == &gVrRagdollSelection && gVrRagdollSelection == 0,
		"submenu owns its selection and starts at enable row");
	Check(gVrMenuNavigateDirection == 0 && gVrMenuNavigateRepeatAt == 0,
		"opening resets held navigation");
	uint8 r,g,b;
	QuestMenuPageColour(&r,&g,&b);
	Check(drawnCategory == VR_MAIN_TRAFFIC_SETTINGS, "submenu inherits traffic colour");

	const int rows[] = {VR_RAGDOLL_BRAKING,VR_RAGDOLL_GRIP,VR_RAGDOLL_SHOTS,VR_RAGDOLL_WEIGHT};
	const int low[] = {0,0,25,50}, high[] = {2000,300,200,300};
	const char *keys[] = {"RagdollBrakePercent","RagdollGripPercent","RagdollShotPercent","RagdollWeightPercent"};
	int (*getters[])() = {VrRagdoll::GetBrakePercent,VrRagdoll::GetGripPercent,VrRagdoll::GetShotPercent,VrRagdoll::GetWeightPercent};
	for(int knob=0;knob<4;++knob){
		gVrRagdollSelection = rows[knob];
		Check(CurrentMenuValueRepeats(), "numeric controls repeat while held");
		for(int press=0;press<45;++press){
			const int before = getters[knob]();
			AdjustRagdollMenuValue(-1);
			const int expected = knob == 0 && before > 500 ? Max(500,before-100) : Max(low[knob],before-25);
			Check(getters[knob]() == expected, "decrease uses fine steps below500 and coarse steps above500");
			Check(settingsFile[keys[knob]] == getters[knob](), "decrease persists immediately");
		}
		Check(getters[knob]() == low[knob], "decrease clamps without wraparound");
		for(int press=0;press<45;++press){
			const int before = getters[knob]();
			AdjustRagdollMenuValue(1);
			const int step = knob == 0 && before >= 500 ? 100 : 25;
			Check(getters[knob]() == Min(high[knob],before+step), "increase uses fine steps through500 and coarse steps above500");
			Check(settingsFile[keys[knob]] == getters[knob](), "increase persists immediately");
		}
		Check(getters[knob]() == high[knob], "increase clamps without wraparound");
	}
	gVrRagdollSelection = VR_RAGDOLL_BRAKING;
	VrRagdoll::SetBrakePercent(525);
	AdjustRagdollMenuValue(-1);
	Check(VrRagdoll::GetBrakePercent() == 500 && settingsFile["RagdollBrakePercent"] == 500,
		"custom saved value crosses back onto500 without skipping the fine range");
	for(int saved : {500,1000,1750,2000,4000}){
		settingsFile["RagdollBrakePercent"] = saved;
		LoadRagdollSettings();
		Check(VrRagdoll::GetBrakePercent() == Min(saved,2000), "saved extended braking survives reload and clamps at2000");
		Check(settingsFile["RagdollBrakePercent"] == saved, "loading preserves the user's existing INI value");
	}
	VrRagdoll::SetBrakePercent(2000);
	draws.clear(); DrawQuestRagdollPage();
	bool maximumShown = false, extendedRangeShown = false;
	for(const auto &draw : draws){
		maximumShown |= draw.text.find("VEHICLE BRAKING  < 2000% >") != std::string::npos;
		extendedRangeShown |= draw.text.find("0-2000%") != std::string::npos;
	}
	Check(maximumShown && extendedRangeShown, "menu displays actual2000percent strength and updated range");
#if !MIAMIVR_DEV_TOOLS
	for(const auto &draw : draws)
		Check(draw.text.find("RD5 ON") == std::string::npos && draw.text.find("CAR HITS") == std::string::npos,
			"player menu omits temporary ragdoll diagnostics");
#endif
	for(int saved : {50,100,175,300,10000}){
		settingsFile["RagdollWeightPercent"] = saved;
		LoadRagdollSettings();
		Check(VrRagdoll::GetWeightPercent() == Min(saved,300), "saved body weight applies immediately with bounded mass");
		Check(VrRagdoll::GetBrakePercent() == 2000, "changing body weight preserves separate brake gain");
	}
	gVrRagdollSelection = VR_RAGDOLL_WEIGHT;
	draws.clear(); DrawQuestRagdollPage();
	bool weightShown = false;
	for(const auto &draw : draws) weightShown |= draw.text == "BODY WEIGHT  < 300% >";
	Check(weightShown, "body-weight row displays the actual saved mass");
	for(int enabled=0;enabled<2;++enabled){
		VrRagdoll::SetEnabled(enabled != 0);
		gVrRagdollSelection = VR_RAGDOLL_DEFAULTS;
		Check(!CurrentMenuValueRepeats(), "defaults action never auto-repeats");
		const int writesBefore = settingsWrites;
		AdjustRagdollMenuValue(1);
		Check(!VrRagdoll::IsEnabled() && settingsFile["Ragdolls"] == 0, "explicit defaults switches ragdolls OFF and persists the flag");
		Check(settingsWrites == writesBefore+5, "defaults saves four tuning values and OFF flag");
		Check(VrRagdoll::GetBrakePercent() == 200 && VrRagdoll::GetGripPercent() == 150 &&
			VrRagdoll::GetShotPercent() == 100, "defaults restores 200/150/100");
		Check(VrRagdoll::GetWeightPercent() == 200 && settingsFile["RagdollWeightPercent"] == 200,
			"defaults restores and saves the user's mass");
	}
	gVrRagdollSelection = VR_RAGDOLL_ENABLED;
	Check(!CurrentMenuValueRepeats(), "enable action never auto-repeats");
	AdjustRagdollMenuValue(-1);
	Check(VrRagdoll::IsEnabled() && settingsFile["Ragdolls"] == 1,
		"enable toggle applies and saves ON immediately");
	AdjustRagdollMenuValue(-1);
	Check(!VrRagdoll::IsEnabled() && settingsFile["Ragdolls"] == 0,
		"enable toggle applies and saves immediately");
	VrRagdoll::SetBrakePercent(0); VrRagdoll::SetGripPercent(0); VrRagdoll::SetShotPercent(25);
	LoadRagdollSettings();
	Check(VrRagdoll::GetBrakePercent() == 200 && VrRagdoll::GetGripPercent() == 150 &&
		VrRagdoll::GetShotPercent() == 100 && !VrRagdoll::IsEnabled(), "saved tuning survives reload");

	gVrRagdollSelection = VR_RAGDOLL_BRAKING;
	bool held = false;
	double repeatAt = 0,holdStart = 0;
	int pulses = 0;
	for(int t=100;t<=1000;t+=10)
		if(MenuRepeatPulse(true,held,repeatAt,holdStart,(double)t,CurrentMenuValueRepeats())) ++pulses;
	Check(pulses == 7, "held tuning uses existing 420 ms delay and bounded repeat interval");
	Check(!MenuRepeatPulse(false,held,repeatAt,holdStart,1010,true) && !held,
		"releasing tuning trigger resets repeat state");

	for(int row=0;row<VR_RAGDOLL_ITEM_COUNT;++row){
		gVrRagdollSelection = row;
		draws.clear(); DrawQuestRagdollPage();
		int selectedCount = 0;
		int previousBottom = 0;
		for(const auto &draw : draws){
			if(draw.selected) ++selectedCount;
			Check(draw.y >= previousBottom, "ragdoll page rows and help do not overlap");
			previousBottom = draw.y+draw.scale*7;
		}
		Check(selectedCount == 1, "every selectable row is visible and highlighted");
	}
	gVrMenuVisible = true;
	ReturnFromCurrentMenuPage();
	Check(gVrMenuVisible && gVrMenuPage == VR_MENU_PAGE_TRAFFIC &&
		gVrTrafficSelection == VR_TRAFFIC_RAGDOLLS, "B returns to ragdoll entry in traffic");
	OpenRagdollMenu();
	gVrRagdollSelection = VR_RAGDOLL_BACK;
	Check(!CurrentMenuValueRepeats(), "back action never auto-repeats");
	AdjustRagdollMenuValue(1);
	Check(gVrMenuPage == VR_MENU_PAGE_TRAFFIC, "explicit back row returns to traffic");
	std::printf("Ragdoll submenu: %d production-control, persistence, repeat, navigation and layout checks passed.\n",checks);
	return 0;
}
