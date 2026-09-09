#include "ragdoll-test-vector.h"
#include "VrRagdollPhysics.h"
#include <cstdio>
#include <cstdlib>
using namespace VrRagdollPhysics;
static int checks=0;
static void Check(bool value,const char *message){ ++checks; if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);} }
int main()
{
    const int nodes[4]={RD_LCALF,RD_LFOOT,RD_PELVIS,RD_SPINE1};
    for(int mode=0;mode<4;++mode){
        for(int tilt=0;tilt<80;++tilt){
            State s={}; s.groundZ=-100;
            s.pos[nodes[0]]=CVector(0,0,1); s.pos[nodes[1]]=CVector(0,1,1);
            s.pos[nodes[2]]=CVector(mode==3?0:-.19f,0,1); s.pos[nodes[3]]=CVector(mode==3?0:-.19f,1,1);
            const float angle=float(tilt)*.02f;
            for(int i=0;i<4;++i){
                s.prev[nodes[i]]=s.pos[nodes[i]];
                if(mode==1 || mode==2){
                    s.contact[nodes[i]].active=true;
                    s.contact[nodes[i]].normal=CVector((i<2?-1.0f:1.0f)*std::cos(angle),0,std::sin(angle));
                }
            }
            if(mode==2) s.groundZ=.915f;
            const CVector before=CentreOfMass(&s);
            SeparateCapsules(&s,nodes[0],nodes[1],nodes[2],nodes[3],.2f,0,0,CVector(1,0,0),.8f);
            for(int i=0;i<4;++i){
                const CVector movement=s.pos[nodes[i]]-s.prev[nodes[i]];
                Check(std::isfinite(movement.MagnitudeSqr()),"coincident or blocked capsules stay finite");
                Check(movement.Magnitude()<=.02501f,"nearly blocked contact cannot amplify correction above 2.5cm");
                if(mode==1 || mode==2)
                    Check(DotProduct(movement,s.contact[nodes[i]].normal)>-.000001f,"separation obeys blocking car plane");
            }
            if(mode==0 || mode==3)
                Check((CentreOfMass(&s)-before).Magnitude()<.000001f,"free capsule separation conserves centre of mass");
        }
    }
    for(int mode=0;mode<5;++mode){
        State s={}; s.groundZ=-100;
        s.pos[nodes[0]]=CVector(0,0,1); s.pos[nodes[1]]=CVector(0,1,1);
        s.pos[nodes[2]]=CVector(-.19f,0,1); s.pos[nodes[3]]=CVector(-.19f,1,1);
        CVector velocity[SIM_BONES],incoming[SIM_BONES];
        for(int i=0;i<SIM_BONES;++i) velocity[i]=incoming[i]=CVector(0,30,0);
        if(mode==1) velocity[nodes[0]].x=6;
        if(mode==2){ incoming[nodes[0]].x=1;velocity[nodes[0]].x=3; }
        if(mode==3) incoming[nodes[0]].x=velocity[nodes[0]].x=-3;
        if(mode==4){ incoming[nodes[0]].x=3;velocity[nodes[0]].x=2; }
        CVector momentum;
        float energy=0;
        for(int i=0;i<SIM_BONES;++i){ momentum+=velocity[i]/invMass[i];energy+=velocity[i].MagnitudeSqr()/invMass[i]; }
        SeparateCapsules(&s,nodes[0],nodes[1],nodes[2],nodes[3],.2f,0,0,CVector(1,0,0),0,velocity,incoming);
        CVector after;
        float afterEnergy=0;
        for(int i=0;i<SIM_BONES;++i){
            after+=velocity[i]/invMass[i];afterEnergy+=velocity[i].MagnitudeSqr()/invMass[i];
            Check(std::fabs(velocity[i].y-30)<.000001f,"normal response preserves shared car translation and tangential motion");
        }
        Check((after-momentum).Magnitude()<.00001f,"self normal impulse preserves total linear momentum");
        Check(afterEnergy<=energy+.01f,"self normal response cannot create kinetic energy");
        const float relative=velocity[nodes[0]].x-velocity[nodes[2]].x;
        const float expected=mode==2?1.0f:(mode==4?2.0f:0.0f);
        Check(std::fabs(relative-expected)<.00001f,"self contact removes penetration-created separation but preserves existing separation");
    }
    std::printf("PASS: bounded self-contact, singular opposing planes, coincident axes and free-space COM: %d checks\n",checks);
}
