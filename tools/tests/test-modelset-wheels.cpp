#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define nil nullptr
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
using int32=int;
static int checks;
static void Check(bool ok,const char *message){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
namespace ModelSets {
enum {MODEL_CATEGORY_VEHICLES};
int profile;
bool IsCategoryModernActive(int){return profile==1;}
bool IsXboxActive(){return profile==2;}
}
namespace CGeneral { int faststricmp(const char *a,const char *b){return _stricmp(a,b);} }
struct RwRaster {int width,height;};
struct RwTexDictionary;
struct RwTexture {std::string name;RwRaster raster;RwTexDictionary *owner;bool alive;};
struct RwTexDictionary {std::vector<RwTexture*> textures;bool destroyed=false;};
static std::array<RwTexture,80> textures;
static size_t textureCount;
static RwTexDictionary saved,fresh;
static RwTexDictionary *current;
static int loads,modelLoads,merges,destroys,lookups;
static std::vector<RwTexture*> bound;
static std::vector<std::string> wanted,events;
static const char *modernNames[]={"alloy","classic","lighttruck","lightvan","offroad","rim","saloon","smallcar","sport","truck","tyre"};
// These are the 15 texture references read from the pinned Xbox wheels DFF.
static const char *xboxNames[]={"contour_wheels64","discs","tyre64a","whee_rim64","wheel_alloy64","wheel_classic64","wheel_lighttruck64","wheel_lightvan64","wheel_lightvan64back","wheel_offroad64","wheel_offroad64a","wheel_saloon64","wheel_smallcar64","wheel_sport64","wheel_truck64"};
class CFileLoader {
public:
 static void AddTexDictionaries(RwTexDictionary *dst,RwTexDictionary *src);
 static void ExecuteModelFile(const char *line,RwTexDictionary *savedTxd);
};
static RwTexture *NewTexture(RwTexDictionary &dictionary,const std::string &name,int size){
 Check(textureCount<textures.size(),"fixture texture capacity");
 RwTexture *texture=&textures[textureCount++];*texture={name,{size,size},&dictionary,true};dictionary.textures.push_back(texture);return texture;
}
static RwTexture *RwTexDictionaryFindNamedTexture(RwTexDictionary *dictionary,const char *name){
 ++lookups;
 for(auto *texture:dictionary->textures)if(texture->name==name)return texture;
 return nullptr;
}
static RwRaster *RwTextureGetRaster(RwTexture *texture){return &texture->raster;}
static int RwRasterGetWidth(RwRaster *raster){return raster->width;}
static int RwRasterGetHeight(RwRaster *raster){return raster->height;}
static RwTexDictionary *LoadTexDictionary(const char *path){
 Check(std::strcmp(path,"MODELS\\GENERIC\\WHEELS.TXD")==0,"wrong wheel dictionary path");
 ++loads;events.push_back("load-dictionary");
 for(const auto &name:wanted)NewTexture(fresh,name,64);
 return &fresh;
}
static void RwTexDictionarySetCurrent(RwTexDictionary *dictionary){current=dictionary;events.push_back(dictionary==&saved?"restore":"bind-fresh");}
static void LoadingScreenLoadingFile(const char *){}
static const char *GetFilename(const char *filename){const char *last=std::strrchr(filename,'\\');return last?last+1:filename;}
static void LoadModelFile(const char *){
 ++modelLoads;events.push_back("load-model");
 // Same-name zero-sized startup textures model the original white-wheel bug.
 for(const auto &name:wanted)bound.push_back(RwTexDictionaryFindNamedTexture(current,name.c_str()));
}
static void RwTexDictionaryAddTexture(RwTexDictionary *dictionary,RwTexture *texture){
 auto &old=texture->owner->textures;old.erase(std::remove(old.begin(),old.end(),texture),old.end());
 texture->owner=dictionary;dictionary->textures.push_back(texture);
}
static void RwTexDictionaryForAllTextures(RwTexDictionary *dictionary,RwTexture *(*callback)(RwTexture*,void*),void *data){
 ++merges;events.push_back("merge");Check(current==&saved,"current dictionary not restored before merge");
 auto snapshot=dictionary->textures;for(auto *texture:snapshot)callback(texture,data);
}
static void RwTexDictionaryDestroy(RwTexDictionary *dictionary){
 ++destroys;events.push_back("destroy");
 for(auto *texture:dictionary->textures)texture->alive=false;
 dictionary->textures.clear();dictionary->destroyed=true;
}
#include "modelset-wheels-production.inc"
static void Scenario(int profile,bool wheel,bool lowerCase){
 saved={};fresh={};textureCount=0;current=&saved;loads=modelLoads=merges=destroys=lookups=0;bound.clear();wanted.clear();events.clear();ModelSets::profile=profile;
 if(profile==2)for(const char *name:xboxNames)wanted.emplace_back(name);
 else for(const char *name:modernNames)wanted.emplace_back(name);
 for(const auto &name:wanted)NewTexture(saved,name,0);
 const bool overlay=profile!=0&&wheel;
 const char *path=wheel?(lowerCase?"MODELFILE models\\generic\\wheels.dff":"MODELFILE MODELS\\GENERIC\\WHEELS.DFF"):"MODELFILE MODELS\\GENERIC\\UNRELATED.DFF";
 CFileLoader::ExecuteModelFile(path,&saved);
 Check(modelLoads==1,"MODELFILE must load exactly once");Check(current==&saved,"startup dictionary was not restored");
 Check(loads==(overlay?1:0),"fresh dictionary eligibility differs from selected profile");
 Check(merges==(overlay?1:0)&&destroys==(overlay?1:0),"dictionary lifetime differs from load path");
 Check(bound.size()==wanted.size(),"material bindings missing");
 for(auto *texture:bound){Check(texture!=nullptr&&texture->alive,"material points at dead or absent texture");Check(texture->raster.width==(overlay?64:0),"wheel material kept a dummy raster");Check(texture->owner==&saved,"texture ownership did not survive temporary dictionary destruction");}
 if(overlay){
  Check(events==std::vector<std::string>({"load-dictionary","bind-fresh","load-model","restore","merge","destroy"}),"texture binding/lifetime order changed");
  Check(fresh.destroyed&&fresh.textures.empty(),"temporary dictionary was not drained and destroyed");
  Check(lookups==static_cast<int>(wanted.size())+(profile==1?11:0),"Modern-only diagnostic executed for Xbox");
 }else Check(events==std::vector<std::string>({"load-model"}),"unrelated or Classic path took overlay lifecycle");
}
int main(){
 for(int profile=0;profile<3;profile++){Scenario(profile,true,false);Scenario(profile,true,true);Scenario(profile,false,false);}
 std::printf("PASS %d production wheel MODELFILE/dummy binding/lifetime checks (Classic, Modern, Xbox).\n",checks);
}
