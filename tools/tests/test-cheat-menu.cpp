#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))

template<class T> static T Min(T a, T b) { return a < b ? a : b; }
template<class T> static T Max(T a, T b) { return a > b ? a : b; }
static float Abs(float value) { return value < 0 ? -value : value; }
static int checks;
static void Check(bool condition, const char *what)
{
	++checks;
	if(!condition){ std::fprintf(stderr, "FAIL: %s\n", what); std::exit(1); }
}
namespace OculusVR {
enum { QUEST_VEHICLE_CAL_HAND = 0 };
static int GetQuestVehicleCalibrationItemCount() { return 2; }
static int GetQuestVehicleCalibrationItemForRow(int row) { return row; }
static int GetQuestHolsterPointCount() { return 3; }
static void SetQuestVehicleCalibrationPreview(bool) {}
}
static int nativeCount = 69;
static int activatedSource = -1, activationCount, cycledSource = -1, cycleDirection;
static bool godMode, unlimitedRun, neverWanted;
static int GetVrCheatCount() { return nativeCount; }
static int GetVrMissionCategoryCount() { return 3; }
static int GetVrMissionCount(int) { return 10; }
static const char *GetVrCheatName(int source);
static bool GetVrCheatToggleState(int source, bool *enabled)
{
	if(source != 5 && source != 6 && source != 9) return false;
	*enabled = source == 5 ? godMode : source == 6 ? unlimitedRun : neverWanted;
	return true;
}
static bool ActivateVrCheat(int source)
{
	Check(source >= 0 && source < nativeCount, "activation uses a native source index");
	activatedSource = source;
	++activationCount;
	if(source == 5) godMode = !godMode;
	if(source == 6) unlimitedRun = !unlimitedRun;
	if(source == 9) neverWanted = !neverWanted;
	return source != 0; // Vehicle spawning can be unavailable without a player.
}
static bool CycleVrCheatSelection(int source, int direction)
{
	Check(source >= 0 && source < nativeCount, "model cycling uses a native source index");
	if(source < 0 || source > 4 || direction == 0) return false;
	cycledSource = source;
	cycleDirection = direction;
	return true;
}
struct DrawnRow { std::string text; int y; bool selected, off, on; };
static std::vector<DrawnRow> rows;
static std::string title;
static void BeginFullVrMenuPage(const char *heading, const char * = nullptr)
{ rows.clear(); title = heading; }
static void DrawVrMenuText(const char *, int, int, int, int, int, int) {}
static void DrawFullVrMenuRow(const char *text, int y, int, bool selected,
	bool = true, bool off = false, bool on = false)
{ rows.push_back({text, y, selected, off, on}); }

#include "cheat-menu-production.inc"

static const char *GetVrCheatName(int source)
{
	Check(source >= 0 && source < 69, "draw uses a known native cheat");
	return nativeNames[source];
}

static void CheckDrawSelection(int selected)
{
	int found = 0;
	for(int i = 0; i < (int)rows.size(); ++i){
		Check(rows[i].y >= 150 && rows[i].y < 670, "cheat rows stay above footer");
		if(rows[i].selected){ ++found; Check(i == selected, "draw highlight matches input row"); }
	}
	Check(found == 1, "one selected row is visible");
}

int main()
{
	using namespace VrCheatMenu;
	const int expectedCounts[] = {9, 4, 3, 13, 8, 5, 10, 11, 6};
	int seen[69] = {};
	Check(ActualBackendCheatCount() == nativeCount, "actual backend count includes exactly the 69 retained cheats");
	Check(VR_SPECIAL_SPAWN_NEXT_CAR == 0 && VR_SPECIAL_GOD_MODE == 5 &&
		VR_SPECIAL_RUN_WITHOUT_LIMITS == 6 && VR_SPECIAL_NEVER_WANTED == 9 && VR_SPECIAL_CHEAT_COUNT == 10,
		"actual special enum agrees with vehicle and toggle dispatch after removal");
	Check(std::strcmp(nativeNames[SourceIndex(GENERAL, 0, nativeCount)], "GOD MODE") == 0,
		"General begins with God Mode after removing the no-op mission cheat");
	Check((int)sizeof(kSourceCategories) == GetVrCheatCount(), "all backend cheats are categorised");
	for(int category = 0; category < CATEGORY_COUNT; ++category){
		Check(Count(category, nativeCount) == expectedCounts[category], "expected category size");
		Check(Count(category, nativeCount)+1 <= VR_CHEAT_ITEMS_PER_PAGE, "every category and Back fit one page");
		OpenCheatMenu();
		Check(gVrCheatCategory == -1 && gVrCheatCategorySelection == 0, "opening starts at category root");
		Check(CurrentMenuSelection() == &gVrCheatCategorySelection, "root navigation selects categories");
		Check(CurrentMenuItemCount() == CATEGORY_COUNT+2, "root includes missions and Back");
		Check(!CurrentMenuValueRepeats(), "root trigger cannot repeat an action");
		const int before = activationCount;
		gVrCheatCategorySelection = category;
		DrawQuestCheatPage();
		CheckDrawSelection(category);
		Check((int)rows.size() == CATEGORY_COUNT+2, "draw has every root entry");
		Check(!CycleQuestCheatMenuSelection(1), "cycling root cannot activate a cheat");
		ActivateQuestCheatMenuSelection();
		Check(gVrCheatCategory == category && gVrCheatSelection == 0, "A opens the chosen category at its first row");
		Check(activationCount == before, "opening a category never activates its first cheat");
		Check(CurrentMenuSelection() == &gVrCheatSelection, "submenu has independent row selection");
		Check(CurrentMenuItemCount() == expectedCounts[category]+1, "submenu input count includes Back");
		Check(!CurrentMenuValueRepeats(), "held trigger never repeats cheat activation");
		for(int item = 0; item < expectedCounts[category]; ++item){
			const int source = SourceIndex(category, item, nativeCount);
			Check(source >= 0 && source < 69, "every submenu row resolves to source");
			++seen[source];
			gVrCheatSelection = item;
			DrawQuestCheatPage();
			CheckDrawSelection(item);
			Check(title == std::string("CHEATS / ")+CategoryName(category), "heading names active category");
			Check(rows[item].text == nativeNames[source], "display and action share the source mapping");
			Check(rows[item].text != "PASS CURRENT MISSION", "removed mission-pass entry cannot be rendered or activated");
			Check(rows.back().text == "BACK TO CHEAT CATEGORIES", "Back is present in every category");
			const bool isToggle = source == 5 || source == 6 || source == 9;
			Check(rows[item].off == isToggle && !rows[item].on, "toggle state uses mapped source before activation");
			activatedSource = -1;
			ActivateQuestCheatMenuSelection();
			Check(activatedSource == source, "A dispatches exact displayed cheat");
			Check(gVrCheatCategory == category, "activation remains in current category");
			Check(std::strcmp(gVrCheatStatus, source == 0 ? "UNAVAILABLE RIGHT NOW" : "CHEAT ACTIVATED") == 0,
				"activation feedback survives categorisation");
			DrawQuestCheatPage();
			Check(rows[item].on == isToggle && !rows[item].off, "toggle state updates after activation");
			for(int direction : {-1, 1}){
				cycledSource = -1;
				const bool changed = CycleQuestCheatMenuSelection(direction);
				const bool model = source >= 0 && source <= 4;
				Check(changed == model, "only vehicle selectors cycle");
				Check(!model || (cycledSource == source && cycleDirection == direction), "model direction and exact source preserved");
			}
		}
		gVrCheatSelection = expectedCounts[category];
		DrawQuestCheatPage();
		CheckDrawSelection(gVrCheatSelection);
		Check(!CycleQuestCheatMenuSelection(1), "Back row cannot cycle a model");
		const int beforeBack = activationCount;
		ActivateQuestCheatMenuSelection();
		Check(gVrCheatCategory == -1 && gVrCheatCategorySelection == category, "Back row remembers parent category");
		Check(activationCount == beforeBack, "Back row does not activate any cheat");
		ActivateQuestCheatMenuSelection();
		ReturnFromCurrentMenuPage();
		Check(gVrMenuPage == VR_MENU_PAGE_CHEATS && gVrCheatCategory == -1 &&
			gVrCheatCategorySelection == category, "B returns to exact parent category");
		ReturnFromCurrentMenuPage();
		Check(gVrMenuPage == VR_MENU_PAGE_SETTINGS && gVrMenuSelection == VR_MAIN_CHEATS, "second B returns to settings cheat row");
	}
	for(int source = 0; source < 69; ++source) Check(seen[source] == 1, "each of 69 native cheats appears exactly once");
	Check(activationCount == 69, "all 69 cheats dispatched exactly once");
	OpenCheatMenu();
	gVrCheatCategorySelection = CATEGORY_COUNT;
	ActivateQuestCheatMenuSelection();
	Check(gVrMenuPage == VR_MENU_PAGE_MISSIONS && gVrMissionCategory == -1, "mission selector opens its own categories");
	gVrMissionCategory = 2;
	gVrMissionCategorySelection = 2;
	ReturnFromCurrentMenuPage();
	Check(gVrMenuPage == VR_MENU_PAGE_MISSIONS && gVrMissionCategory == -1 &&
		gVrMissionCategorySelection == 2, "mission Back preserves mission category choice");
	ReturnFromCurrentMenuPage();
	Check(gVrMenuPage == VR_MENU_PAGE_CHEATS && gVrCheatCategorySelection == CATEGORY_COUNT,
		"mission root Back highlights Mission Selector");
	gVrCheatCategorySelection = CATEGORY_COUNT+1;
	ActivateQuestCheatMenuSelection();
	Check(gVrMenuPage == VR_MENU_PAGE_SETTINGS, "root Back row returns to settings");
	for(int category : {-2, -1, (int)CATEGORY_COUNT, CATEGORY_COUNT+1}){
		Check(Count(category, nativeCount) == 0, "invalid category is empty");
		Check(SourceIndex(category, 0, nativeCount) == -1, "invalid category cannot dispatch");
	}
	Check(SourceIndex(GENERAL, -1, nativeCount) == -1, "negative row cannot dispatch");
	Check(SourceIndex(GENERAL, 9, nativeCount) == -1, "Back row has no native index");
	Check(SourceIndex(GENERAL, 9, nativeCount+1) == 69, "future backend cheat remains reachable in General");
	Check(Count(GENERAL, 0) == 0, "empty backend still supports a Back-only menu");

	ResetMenuNavigationRepeat();
	Check(MenuNavigationPulse(-0.9f, 1000.0) == 1, "stick tap advances one row");
	Check(MenuNavigationPulse(-0.9f, 1429.0) == 0, "held stick waits for deliberate repeat");
	Check(MenuNavigationPulse(-0.9f, 1430.0) == 1, "held stick starts repeating");
	Check(MenuNavigationPulse(-0.9f, 1540.0) == 1, "held stick repeats at existing interval");
	Check(MenuNavigationPulse(0.9f, 1541.0) == -1, "reversing stick moves immediately");
	OpenCheatMenu();
	Check(gVrMenuNavigateDirection == 0 && gVrMenuNavigateRepeatAt == 0.0, "opening clears old navigation repeat");
	bool down = false;
	double repeatAt = 0.0, holdStart = 0.0;
	Check(MenuRepeatPulse(true, down, repeatAt, holdStart, 1000.0, false), "trigger presses once");
	Check(!MenuRepeatPulse(true, down, repeatAt, holdStart, 9000.0, false), "held trigger cannot repeatedly cycle models");
	Check(!MenuRepeatPulse(false, down, repeatAt, holdStart, 9010.0, false), "trigger release resets latch");
	Check(MenuRepeatPulse(true, down, repeatAt, holdStart, 9020.0, false), "fresh trigger tap works");
	std::printf("PASS: %d checks; all 69 retained cheats, no mission-pass entry, nine submenus, rendering, toggle/model dispatch, Back and navigation.\n", checks);
}
