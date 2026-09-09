#include <cstdio>
#include <cstdlib>
#include "devtools-cull-production.inc"
int main(){
 for(int i=0;i<1000;++i){
  VrCullViz::Toggle(); VrCullViz::Render();
  if(VrCullViz::IsActive() || *VrCullViz::StatusLine()){
   std::fprintf(stderr,"FAIL shipping cull tools can be activated\n");return 1;
  }
 }
 std::puts("PASS: 1000 direct shipping Toggle/Render attempts stay inactive, no status and no engine dependencies.");
}
