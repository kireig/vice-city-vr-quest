
#include "common.h"

#include "ModelSets.h"
#include "crossplatform.h"

#ifdef __ANDROID__
#include "platform_android.h"
#endif

#ifdef _WIN32
#include <windows.h>
#define MODELSET_STRICMP _stricmp
#define MODELSET_STRNICMP _strnicmp
#else
#include <strings.h>
#define MODELSET_STRICMP strcasecmp
#define MODELSET_STRNICMP strncasecmp
#endif

namespace ModelSets
{
namespace
{
eModelSet gActiveModelSet = MODEL_SET_CLASSIC;
eModelSet gRequestedModelSet = MODEL_SET_CLASSIC;
bool gInitialized;
bool gModernArchivePairAvailable;
bool gXboxAvailable;
eModelSet gActiveCategory[MODEL_CATEGORY_COUNT];
eModelSet gRequestedCategory[MODEL_CATEGORY_COUNT];
enum { MAX_VEGETATION_MODELS = 512, MAX_MANIFEST_MODEL_NAME = 24 };
char gVegetationModels[MAX_VEGETATION_MODELS][MAX_MANIFEST_MODEL_NAME] = {};
int gNumVegetationModels;
bool gVegetationManifestAvailable;
char gGameRoot[1024] = {};
char gSettingsPath[1024] = {};

const char *const gCategorySettingNames[MODEL_CATEGORY_COUNT] = {
	"ModelSetWorld",
	"ModelSetVegetation",
	"ModelSetVehicles",
	"ModelSetPeds",
	"ModelSetWeapons"
};

const char *const gCategoryNames[MODEL_CATEGORY_COUNT] = {
	"WORLD / BUILDINGS",
	"VEGETATION / PALMS",
	"VEHICLES",
	"PEDESTRIANS",
	"WEAPONS"
};

// The HD vegetation source is dramatically heavier than the Classic trees
// (especially the palms, which are repeated hundreds of times around the
// city). The headset-validated baseline uses Modern world textures and weapon
// models while vehicles, pedestrians and vegetation stay Classic. An existing
// INI value still wins after the one-time baseline migration.
const int gCategoryDefaults[MODEL_CATEGORY_COUNT] = {
	1, // world / buildings
	0, // vegetation / palms
	0, // vehicles
	0, // pedestrians
	1  // weapons
};

bool FileExists(const char *path)
{
#ifdef _WIN32
	const DWORD attributes = GetFileAttributesA(path);
	return attributes != INVALID_FILE_ATTRIBUTES &&
		(attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
	FILE *file = fcaseopen(path, "rb");
	if(!file)
		return false;
	fclose(file);
	return true;
#endif
}

void NormalizeRoot(const char *root)
{
	gGameRoot[0] = '\0';
	if(root && root[0] != '\0')
		strncpy(gGameRoot, root, sizeof(gGameRoot)-1);
	gGameRoot[sizeof(gGameRoot)-1] = '\0';
	const size_t length = strlen(gGameRoot);
	if(length > 0 && gGameRoot[length-1] != '\\' &&
	   gGameRoot[length-1] != '/'){
		if(length+1 < sizeof(gGameRoot)){
			#ifdef _WIN32
			gGameRoot[length] = '\\';
			#else
			gGameRoot[length] = '/';
			#endif
			gGameRoot[length+1] = '\0';
		}
	}
	#ifndef _WIN32
	for(char *cursor = gGameRoot; *cursor; cursor++)
		if(*cursor == '\\')
			*cursor = '/';
	#endif
}

void FindSettingsPath()
{
#ifdef _WIN32
	const DWORD length = GetModuleFileNameA(nil, gSettingsPath,
		(DWORD)ARRAY_SIZE(gSettingsPath));
	if(length > 0 && length < ARRAY_SIZE(gSettingsPath)){
		char *separator = strrchr(gSettingsPath, '\\');
		if(!separator)
			separator = strrchr(gSettingsPath, '/');
		if(separator){
			strcpy(separator+1, "vr_settings.ini");
			return;
		}
	}
#endif
	// Android's Win32-profile shim resolves relative paths against the active
	// game-data directory. Keep the same portable filename used by the Quest VR
	// menu instead of baking an app-specific storage path into this subsystem.
#ifdef _WIN32
	snprintf(gSettingsPath, sizeof(gSettingsPath), "%svr_settings.ini", gGameRoot);
#else
	strcpy(gSettingsPath, ".\\vr_settings.ini");
#endif
}

bool IsSafeModelsPath(const char *path)
{
	if(!path || path[0] == '\0' || path[1] == ':')
		return false;
	if(path[0] == '\\' || path[0] == '/')
		return false;
	if(strstr(path, "..") != nil)
		return false;
	return MODELSET_STRNICMP(path, "models\\", 7) == 0 ||
		MODELSET_STRNICMP(path, "models/", 7) == 0 ||
		MODELSET_STRNICMP(path, "txd\\", 4) == 0 ||
		MODELSET_STRNICMP(path, "txd/", 4) == 0;
}

void BuildOverlayPath(eModelSet modelSet, const char *relativePath, char *destination,
	size_t destinationSize)
{
	char normalized[512];
	strncpy(normalized, relativePath, sizeof(normalized)-1);
	normalized[sizeof(normalized)-1] = '\0';
	for(char *cursor = normalized; *cursor; cursor++)
		#ifdef _WIN32
		if(*cursor == '/') *cursor = '\\';
		#else
		if(*cursor == '\\') *cursor = '/';
		#endif
#ifdef _WIN32
	snprintf(destination, destinationSize, "%smodelsets\\%s\\%s", gGameRoot, modelSet == MODEL_SET_XBOX ? "xbox" : "modern", normalized);
#else
	snprintf(destination, destinationSize, "%smodelsets/%s/%s", gGameRoot, modelSet == MODEL_SET_XBOX ? "xbox" : "modern", normalized);
#endif
}

bool IsGta3ArchivePath(const char *relative)
{
	return MODELSET_STRICMP(relative, "models\\gta3.img") == 0 ||
		MODELSET_STRICMP(relative, "models/gta3.img") == 0 ||
		MODELSET_STRICMP(relative, "models\\gta3.dir") == 0 ||
		MODELSET_STRICMP(relative, "models/gta3.dir") == 0;
}

void LoadVegetationManifest()
{
	gNumVegetationModels = 0;
	gVegetationManifestAvailable = false;
	char path[1024];
	BuildOverlayPath(MODEL_SET_MODERN, "vegetation_models.txt", path, sizeof(path));
	FILE *file = fopen(path, "rt");
	if(!file){
		debug("Model set: no vegetation manifest at %s; vegetation cannot be separated from world assets\n", path);
		return;
	}

	char line[256];
	while(gNumVegetationModels < MAX_VEGETATION_MODELS &&
	      fgets(line, sizeof(line), file)){
		char *begin = line;
		while(*begin == ' ' || *begin == '\t')
			begin++;
		char *end = begin + strlen(begin);
		while(end > begin && (end[-1] == '\r' || end[-1] == '\n' ||
		      end[-1] == ' ' || end[-1] == '\t'))
			*--end = '\0';
		if(begin[0] == '\0' || begin[0] == '#' || begin[0] == ';')
			continue;
		char *extension = strrchr(begin, '.');
		if(extension && MODELSET_STRICMP(extension, ".dff") == 0)
			*extension = '\0';
		strncpy(gVegetationModels[gNumVegetationModels], begin,
			MAX_MANIFEST_MODEL_NAME-1);
		gVegetationModels[gNumVegetationModels][MAX_MANIFEST_MODEL_NAME-1] = '\0';
		gNumVegetationModels++;
	}
	fclose(file);
	gVegetationManifestAvailable = gNumVegetationModels > 0;
	debug("Model set: loaded %d vegetation model names\n",
		gNumVegetationModels);
}
}

void InitializeStartup(const char *gameRoot)
{
	if(gInitialized)
		return;
	NormalizeRoot(gameRoot);
	FindSettingsPath();
	char path[1024];
	BuildOverlayPath(MODEL_SET_MODERN, "models/gta3.img", path, sizeof(path));
	gModernArchivePairAvailable = FileExists(path);
	BuildOverlayPath(MODEL_SET_MODERN, "models/gta3.dir", path, sizeof(path));
	gModernArchivePairAvailable = gModernArchivePairAvailable && FileExists(path);
	const char *xboxFiles[] = { "models/gta3.img", "models/gta3.dir",
		"models/coll/vehicles.col", "models/generic/wheels.dff",
		"models/generic/wheels.txd", "vehicle_models.txt" };
	gXboxAvailable = true;
	for(size_t i = 0; i < ARRAY_SIZE(xboxFiles); i++){
		BuildOverlayPath(MODEL_SET_XBOX, xboxFiles[i], path, sizeof(path));
		gXboxAvailable = gXboxAvailable && FileExists(path);
	}
	LoadVegetationManifest();
	int requested = (int)(int32)GetPrivateProfileIntA("VR", "ModelSet",
		MODEL_SET_MODERN, gSettingsPath);
	if(requested < MODEL_SET_CLASSIC || requested >= MODEL_SET_COUNT ||
	   !IsAvailable((eModelSet)requested))
		requested = MODEL_SET_CLASSIC;
	gRequestedModelSet = gActiveModelSet = (eModelSet)requested;
	for(int category = 0; category < MODEL_CATEGORY_COUNT; category++){
		const int fallback = requested == MODEL_SET_XBOX ?
			(category == MODEL_CATEGORY_VEHICLES ? MODEL_SET_XBOX : MODEL_SET_CLASSIC) :
			gCategoryDefaults[category];
		int selected = (int)(int32)GetPrivateProfileIntA("VR",
			gCategorySettingNames[category], fallback, gSettingsPath);
		if(selected < MODEL_SET_CLASSIC || selected >= MODEL_SET_COUNT ||
		   (selected == MODEL_SET_XBOX && category != MODEL_CATEGORY_VEHICLES))
			selected = MODEL_SET_CLASSIC;
		gActiveCategory[category] = gRequestedCategory[category] = (eModelSet)selected;
	}
	gInitialized = true;
	debug("Model sets: preset=%s modern=%d xbox=%d vehicles=%s\n",
		GetName(gActiveModelSet), gModernArchivePairAvailable, gXboxAvailable,
		GetName(GetActiveForCategory(MODEL_CATEGORY_VEHICLES)));
}

eModelSet GetActive() { return gActiveModelSet; }
eModelSet GetRequested() { return gRequestedModelSet; }

void SetRequested(eModelSet modelSet)
{
	if(!IsAvailable(modelSet))
		modelSet = MODEL_SET_CLASSIC;
	gRequestedModelSet = modelSet;
	char value[16];
	sprintf(value, "%d", (int)modelSet);
	WritePrivateProfileStringA("VR", "ModelSet", value, gSettingsPath);
}

void CycleRequested(int direction)
{
	if(direction == 0)
		return;
	int requested = gRequestedModelSet;
	for(int i = 0; i < MODEL_SET_COUNT; i++){
		requested = (requested+MODEL_SET_COUNT+(direction > 0 ? 1 : -1)) % MODEL_SET_COUNT;
		if(IsAvailable((eModelSet)requested)){
			SetRequested((eModelSet)requested);
			return;
		}
	}
}

bool IsModernActive()
{
	if(gActiveModelSet == MODEL_SET_CLASSIC || !gModernArchivePairAvailable)
		return false;
	for(int category = 0; category < MODEL_CATEGORY_COUNT; category++)
		if(GetActiveForCategory((eModelCategory)category) == MODEL_SET_MODERN)
			return true;
	return false;
}

bool IsXboxActive()
{
	return GetActiveForCategory(MODEL_CATEGORY_VEHICLES) == MODEL_SET_XBOX;
}

bool IsRestartRequired()
{
	for(int category = 0; category < MODEL_CATEGORY_COUNT; category++)
		if(IsCategoryRestartRequired((eModelCategory)category))
			return true;
	return false;
}

bool IsAvailable(eModelSet modelSet)
{
	return modelSet == MODEL_SET_CLASSIC ||
		(modelSet == MODEL_SET_MODERN && gModernArchivePairAvailable) ||
		(modelSet == MODEL_SET_XBOX && gXboxAvailable);
}

const char *GetName(eModelSet modelSet)
{
	return modelSet == MODEL_SET_XBOX ? "XBOX" :
		modelSet == MODEL_SET_MODERN ? "MODERN" : "CLASSIC";
}

const char *GetSourceName(eModelSet modelSet)
{
	return modelSet == MODEL_SET_XBOX ? "XBOX VEHICLES" :
		modelSet == MODEL_SET_MODERN ? "MODERN OVERLAY" : "BASE INSTALL";
}

static eModelSet EffectiveCategory(eModelCategory category, eModelSet preset,
	const eModelSet *selections)
{
	if(category < 0 || category >= MODEL_CATEGORY_COUNT || preset == MODEL_SET_CLASSIC)
		return MODEL_SET_CLASSIC;
	const eModelSet selected = selections[category];
	if(!IsAvailable(selected) ||
	   (selected == MODEL_SET_XBOX && category != MODEL_CATEGORY_VEHICLES) ||
	   (selected == MODEL_SET_MODERN && category == MODEL_CATEGORY_VEGETATION &&
	    !gVegetationManifestAvailable))
		return MODEL_SET_CLASSIC;
	return selected;
}

eModelSet GetActiveForCategory(eModelCategory category)
{
	return EffectiveCategory(category, gActiveModelSet, gActiveCategory);
}

eModelSet GetRequestedForCategory(eModelCategory category)
{
	return EffectiveCategory(category, gRequestedModelSet, gRequestedCategory);
}

void SetRequestedForCategory(eModelCategory category, eModelSet modelSet)
{
	if(category < 0 || category >= MODEL_CATEGORY_COUNT)
		return;
	if(!IsAvailable(modelSet) ||
	   (modelSet == MODEL_SET_XBOX && category != MODEL_CATEGORY_VEHICLES) ||
	   (modelSet == MODEL_SET_MODERN && category == MODEL_CATEGORY_VEGETATION &&
	    !gVegetationManifestAvailable))
		modelSet = MODEL_SET_CLASSIC;
	// A category selected from the base preset must enable its overlay too.
	if(modelSet != MODEL_SET_CLASSIC && gRequestedModelSet == MODEL_SET_CLASSIC)
		SetRequested(modelSet);
	gRequestedCategory[category] = modelSet;
	char value[16];
	sprintf(value, "%d", (int)modelSet);
	WritePrivateProfileStringA("VR", gCategorySettingNames[category], value, gSettingsPath);
}

void CycleRequestedCategory(eModelCategory category, int direction)
{
	if(category < 0 || category >= MODEL_CATEGORY_COUNT || direction == 0)
		return;
	int selected = GetRequestedForCategory(category);
	for(int i = 0; i < MODEL_SET_COUNT; i++){
		selected = (selected+MODEL_SET_COUNT+(direction > 0 ? 1 : -1)) % MODEL_SET_COUNT;
		if(!IsAvailable((eModelSet)selected) ||
		   (selected == MODEL_SET_XBOX && category != MODEL_CATEGORY_VEHICLES) ||
		   (selected == MODEL_SET_MODERN && category == MODEL_CATEGORY_VEGETATION &&
		    !gVegetationManifestAvailable))
			continue;
		SetRequestedForCategory(category, (eModelSet)selected);
		return;
	}
}

bool IsCategoryModernActive(eModelCategory category)
{
	return GetActiveForCategory(category) == MODEL_SET_MODERN;
}
bool IsCategoryModernRequested(eModelCategory category)
{
	return GetRequestedForCategory(category) == MODEL_SET_MODERN;
}
bool IsCategoryRestartRequired(eModelCategory category)
{
	return GetActiveForCategory(category) != GetRequestedForCategory(category);
}
bool IsCategoryAvailable(eModelCategory category)
{
	if(category < 0 || category >= MODEL_CATEGORY_COUNT)
		return false;
	return (category == MODEL_CATEGORY_VEHICLES && gXboxAvailable) ||
		(gModernArchivePairAvailable &&
		 (category != MODEL_CATEGORY_VEGETATION || gVegetationManifestAvailable));
}

const char *GetCategoryName(eModelCategory category)
{
	if(category < 0 || category >= MODEL_CATEGORY_COUNT)
		return "UNKNOWN";
	return gCategoryNames[category];
}

bool HasVegetationManifest()
{
	return gVegetationManifestAvailable;
}

bool IsVegetationModel(const char *modelName)
{
	if(!modelName || !gVegetationManifestAvailable)
		return false;
	for(int i = 0; i < gNumVegetationModels; i++)
		if(MODELSET_STRICMP(modelName, gVegetationModels[i]) == 0)
			return true;
	return false;
}

bool GetModernAssetPath(const char *relativePath, char *resolvedPath,
	size_t resolvedPathSize)
{
	if(!relativePath || !resolvedPath || resolvedPathSize == 0 ||
	   !gModernArchivePairAvailable)
		return false;
	BuildOverlayPath(MODEL_SET_MODERN, relativePath, resolvedPath, resolvedPathSize);
	return FileExists(resolvedPath);
}

bool GetXboxAssetPath(const char *relativePath, char *resolvedPath,
	size_t resolvedPathSize)
{
	if(!relativePath || !resolvedPath || resolvedPathSize == 0 || !gXboxAvailable)
		return false;
	BuildOverlayPath(MODEL_SET_XBOX, relativePath, resolvedPath, resolvedPathSize);
	return FileExists(resolvedPath);
}

static bool IsOverlayPath(eModelSet modelSet, const char *path)
{
	if(!path || !gInitialized)
		return false;
	char overlayRoot[1024];
	BuildOverlayPath(modelSet, "", overlayRoot, sizeof(overlayRoot));
	return MODELSET_STRNICMP(path, overlayRoot, strlen(overlayRoot)) == 0;
}
bool IsModernAssetPath(const char *path) { return IsOverlayPath(MODEL_SET_MODERN, path); }
bool IsXboxAssetPath(const char *path) { return IsOverlayPath(MODEL_SET_XBOX, path); }

const char *ResolveAssetPath(const char *originalPath, char *resolvedPath,
	size_t resolvedPathSize)
{
	if(!originalPath || !resolvedPath || resolvedPathSize == 0 ||
	   !gInitialized || (!IsModernActive() && !IsXboxActive()))
		return originalPath;
	const char *relative = originalPath;
	while(relative[0] == '.' &&
	      (relative[1] == '\\' || relative[1] == '/'))
		relative += 2;
	if(!IsSafeModelsPath(relative))
		return originalPath;
	// GTA3 is special: keep the base archive as image zero and let the
	// streaming directory register selected entries from a second Modern image.
	// Loose TXDs still use the ordinary Modern fallback behavior.
	if(IsGta3ArchivePath(relative))
		return originalPath;
	// Xbox replaces only the matching vehicle collision and shared wheels.
	if(IsXboxActive() &&
	   (MODELSET_STRICMP(relative, "models\\coll\\vehicles.col") == 0 ||
	    MODELSET_STRICMP(relative, "models/coll/vehicles.col") == 0 ||
	    MODELSET_STRICMP(relative, "models\\generic\\wheels.dff") == 0 ||
	    MODELSET_STRICMP(relative, "models/generic/wheels.dff") == 0 ||
	    MODELSET_STRICMP(relative, "models\\generic\\wheels.txd") == 0 ||
	    MODELSET_STRICMP(relative, "models/generic/wheels.txd") == 0)){
		BuildOverlayPath(MODEL_SET_XBOX, relative, resolvedPath, resolvedPathSize);
		return FileExists(resolvedPath) ? resolvedPath : originalPath;
	}
	bool useModern = true;
	if(MODELSET_STRNICMP(relative, "models\\coll\\", 12) == 0 ||
	   MODELSET_STRNICMP(relative, "models/coll/", 12) == 0 ||
	   MODELSET_STRNICMP(relative, "models\\generic\\", 15) == 0 ||
	   MODELSET_STRNICMP(relative, "models/generic/", 15) == 0)
		useModern = IsCategoryModernActive(MODEL_CATEGORY_VEHICLES);
	else if(MODELSET_STRICMP(relative, "models\\generic.txd") == 0 ||
	        MODELSET_STRICMP(relative, "models/generic.txd") == 0)
		useModern = IsCategoryModernActive(MODEL_CATEGORY_VEGETATION);
	else if(MODELSET_STRNICMP(relative, "models\\", 7) == 0 ||
	        MODELSET_STRNICMP(relative, "models/", 7) == 0 ||
	        MODELSET_STRNICMP(relative, "txd\\", 4) == 0 ||
	        MODELSET_STRNICMP(relative, "txd/", 4) == 0)
		useModern = IsCategoryModernActive(MODEL_CATEGORY_WORLD);
	if(!useModern)
		return originalPath;
	BuildOverlayPath(MODEL_SET_MODERN, relative, resolvedPath, resolvedPathSize);
	return FileExists(resolvedPath) ? resolvedPath : originalPath;
}
}
