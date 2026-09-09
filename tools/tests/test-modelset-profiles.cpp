#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <chrono>
#include <cstdarg>
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <filesystem>
#pragma warning(disable: 4100 4505)
#define nil nullptr
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define FIX_BUGS
using int32=int32_t; using uint32=uint32_t;
static std::map<std::string,int> settings;
static int writes,checks;
static uint32 GetPrivateProfileIntA(const char*,const char *key,int fallback,const char*){
 auto it=settings.find(key);return uint32(it==settings.end()?fallback:it->second);
}
static bool WritePrivateProfileStringA(const char*,const char *key,const char *value,const char*){
 ++writes;settings[key]=std::atoi(value);return true;
}
static FILE *fcaseopen(const char *p,const char *m){return std::fopen(p,m);}
static void debug(const char*,...){}
enum{MITYPE_VEHICLE,MITYPE_PED,MITYPE_WEAPON,MITYPE_SIMPLE,MITYPE_TIME,MITYPE_CLUMP};
enum{MODELINFOSIZE=4,STREAM_OFFSET_TXD=16,STREAM_OFFSET_COL=32,STREAM_OFFSET_ANIM=48};
struct CBaseModelInfo{
 const char *name;int type,txd;
 int GetModelType()const{return type;}int GetTxdSlot()const{return txd;}
 const char *GetModelName()const{return name;}
};
static CBaseModelInfo catalog[4]={{"stinger",MITYPE_VEHICLE,0},{"building",MITYPE_SIMPLE,1},{"cop",MITYPE_PED,2},{"palm",MITYPE_SIMPLE,3}};
struct CModelInfo{
 static CBaseModelInfo *GetModelInfo(int n){return n>=0&&n<4?&catalog[n]:nullptr;}
 static CBaseModelInfo *GetModelInfo(const char *name,int *id){for(int i=0;i<4;i++)if(!strcmp(catalog[i].name,name)){*id=i;return &catalog[i];}return nullptr;}
};
struct CTxdStore{
 static int FindTxdSlot(const char *name){for(int i=0;i<4;i++)if(!strcmp(catalog[i].name,name))return i;return -1;}
 static int AddTxdSlot(const char*){return 4;}
};
struct CColStore{static int FindColSlot(const char*){return 0;}static int AddColSlot(const char*){return 0;}};
struct CAnimManager{static int RegisterAnimBlock(const char*){return 0;}};
struct CDirectory{
 struct DirectoryInfo{uint32 offset,size;char name[24];};
 int extras=0;void AddItem(DirectoryInfo,int){++extras;}
};
static std::vector<std::string> images;
static std::vector<CDirectory::DirectoryInfo> entries;
static size_t entryCursor;
static int CdStreamGetNumImages(){return int(images.size());}
static const char *CdStreamGetImageName(int n){return images[size_t(n)].c_str();}
static bool CdStreamAddImagePath(const char *p,bool){images.push_back(p);return true;}
struct CFileMgr{
 static int OpenFile(const char*,const char*){entryCursor=0;return 1;}
 static bool Read(int,char *dst,size_t n){if(entryCursor==entries.size())return false;memcpy(dst,&entries[entryCursor++],n);return true;}
 static void CloseFile(int){}
};
struct StreamInfo{
 uint32 m_position=0,size=0;int m_nextID=-1;
 uint32 GetCdSize()const{return size;}
 void SetCdPosnAndSize(uint32 p,uint32 s){m_position=p;size=s;}
};
struct CStreaming{
 static int ms_streamingBufferSize;
 static CDirectory *ms_pExtraObjectsDir;
 static StreamInfo ms_aInfoForModel[64];
 static void LoadCdDirectory(const char*,int);
};
static CDirectory extraDir;
int CStreaming::ms_streamingBufferSize;
CDirectory *CStreaming::ms_pExtraObjectsDir=&extraDir;
StreamInfo CStreaming::ms_aInfoForModel[64];
#include "modelsets-production.inc"
static void require(bool b,const char *msg){++checks;if(!b){fprintf(stderr,"FAIL %s\n",msg);exit(1);}}
static void touch(const std::filesystem::path &p,const char *data="x"){
 std::filesystem::create_directories(p.parent_path());FILE*f=fopen(p.string().c_str(),"wb");require(f!=nullptr,"create fixture");fputs(data,f);fclose(f);
}
static void reset(const std::filesystem::path &root){ModelSets::gInitialized=false;writes=0;ModelSets::InitializeStartup(root.string().c_str());}
static void fillEntries(){entries.clear();for(const char *name:{"stinger.dff","stinger.txd","building.dff","building.txd","cop.dff","cop.txd","unknown.dff","bogus.col","bogus.ifp"}){CDirectory::DirectoryInfo d={11,2,{}};strcpy(d.name,name);entries.push_back(d);}}
static void resetStreams(){for(auto &s:CStreaming::ms_aInfoForModel)s=StreamInfo{};extraDir.extras=0;}
int main(){
 using namespace ModelSets;
 const auto root=std::filesystem::temp_directory_path()/std::filesystem::path("vc-modelsets-"+std::to_string(std::rand())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 settings={{"ModelSet",1},{"ModelSetVehicles",0}};reset(root);
 require(GetActive()==MODEL_SET_CLASSIC&&!IsAvailable(MODEL_SET_XBOX),"missing overlays fall back without INI rewrite");require(writes==0,"startup read only");
 for(const char *name:{"gta3.img","gta3.dir"})touch(root/"modelsets/modern/models"/name);
 touch(root/"modelsets/modern/vegetation_models.txt","palm\n");
 touch(root/"modelsets/modern/models/coll/vehicles.col");touch(root/"modelsets/modern/models/coll/world.col");
 touch(root/"modelsets/modern/txd/map.txd");
 for(const char *name:{"models/gta3.img","models/gta3.dir","models/coll/vehicles.col","models/generic/wheels.dff","models/generic/wheels.txd"})touch(root/"modelsets/xbox"/name);
 reset(root);require(!IsAvailable(MODEL_SET_XBOX),"incomplete Xbox profile unavailable");
 touch(root/"modelsets/xbox/vehicle_models.txt","stinger\n");
 reset(root);require(IsAvailable(MODEL_SET_XBOX)&&GetActiveForCategory(MODEL_CATEGORY_VEHICLES)==MODEL_SET_CLASSIC,"legacy bool keeps Classic");
 CycleRequestedCategory(MODEL_CATEGORY_VEHICLES,1);require(GetRequestedForCategory(MODEL_CATEGORY_VEHICLES)==MODEL_SET_MODERN,"Classic to Modern");
 CycleRequestedCategory(MODEL_CATEGORY_VEHICLES,1);require(settings["ModelSetVehicles"]==2&&GetRequestedForCategory(MODEL_CATEGORY_VEHICLES)==MODEL_SET_XBOX,"Modern to Xbox persists 2");
 require(GetActiveForCategory(MODEL_CATEGORY_VEHICLES)==MODEL_SET_CLASSIC&&IsRestartRequired(),"no loaded assets swapped");
 reset(root);require(IsXboxActive()&&IsModernActive()&&writes==0,"mixed Xbox/Modern after restart read only");
 char out[1024];
 for(const char *name:{"models/coll/vehicles.col","models/generic/wheels.dff","models/generic/wheels.txd"})require(IsXboxAssetPath(ResolveAssetPath(name,out,sizeof(out))),"Xbox vehicle loose assets");
 for(const char *name:{"models/gta3.img","models/gta3.dir","models/coll/world.col","../models/generic/wheels.dff","C:/models/generic/wheels.dff"})require(ResolveAssetPath(name,out,sizeof(out))==name,"base archive and unrelated paths preserved");
 require(IsModernAssetPath(ResolveAssetPath("txd/map.txd",out,sizeof(out))),"modern world remains enabled");
 images.clear();require(CdStreamAddModelSetImages("models/gta3.img")&&images.size()==3,"base modern xbox registered");
 fillEntries();resetStreams();
 for(int n:{2,1,0})CStreaming::LoadCdDirectory(images[size_t(n)].c_str(),n);
 require((CStreaming::ms_aInfoForModel[0].m_position>>24)==2&&(CStreaming::ms_aInfoForModel[16].m_position>>24)==2,"Xbox DFF+TXD survive reverse directory traversal");
 require((CStreaming::ms_aInfoForModel[1].m_position>>24)==1&&(CStreaming::ms_aInfoForModel[17].m_position>>24)==1,"Modern world retained");
 require((CStreaming::ms_aInfoForModel[2].m_position>>24)==0,"Classic pedestrian retained");
 resetStreams();CStreaming::LoadCdDirectory(images[2].c_str(),2);
 require(extraDir.extras==0&&CStreaming::ms_aInfoForModel[1].size==0&&CStreaming::ms_aInfoForModel[32].size==0&&CStreaming::ms_aInfoForModel[48].size==0,"Xbox cannot replace props collisions or animations");
 settings={{"ModelSet",0},{"ModelSetVehicles",0},{"ModelSetWorld",0},{"ModelSetWeapons",0}};reset(root);
 CycleRequestedCategory(MODEL_CATEGORY_VEHICLES,-1);require(GetRequested()==MODEL_SET_XBOX&&GetRequestedForCategory(MODEL_CATEGORY_VEHICLES)==MODEL_SET_XBOX,"Xbox selectable directly from Classic preset");
 reset(root);images.clear();CdStreamAddModelSetImages("models/gta3.img");require(images.size()==2&&IsXboxActive()&&!IsModernActive(),"Xbox only opens one overlay");
 SetRequested(MODEL_SET_CLASSIC);reset(root);images.clear();CdStreamAddModelSetImages("models/gta3.img");require(images.size()==1&&!IsXboxActive()&&!IsModernActive(),"Classic does not register overlays");
 settings={{"ModelSet",2},{"ModelSetVehicles",2},{"ModelSetWorld",2},{"ModelSetPeds",999}};reset(root);
 require(GetActiveForCategory(MODEL_CATEGORY_WORLD)==MODEL_SET_CLASSIC&&GetActiveForCategory(MODEL_CATEGORY_PEDS)==MODEL_SET_CLASSIC,"invalid/nonvehicle Xbox settings cannot route other categories");
 settings={{"ModelSet",2}};reset(root);require(IsXboxActive()&&!IsModernActive(),"Xbox preset defaults to vehicles only");
 printf("PASS %d model-set startup/menu/path/IMG registration and production directory routing checks\n",checks);
}
