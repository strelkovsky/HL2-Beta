#include "../mdl44_flex.h"
#include <fstream>
#include <cstdio>
#include <cassert>
int main(int argc,char **argv) {
 if(argc!=2)return 2;std::ifstream file(argv[1],std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
 Mdl44Flex::Data data; if(!Mdl44Flex::Parse(bytes.data(),bytes.size(),data)){std::puts("parse failed");return 1;}
 std::printf("controllers=%zu descriptions=%zu rules=%zu flex_meshes=%zu\n",data.controllers.size(),data.descriptions.size(),data.rules.size(),data.meshes.size());
 assert(data.controllers.size()==54 && data.descriptions.size()==71 && data.rules.size()==63 && !data.meshes.empty());
 std::printf("eyes=%zu eye_meshes=%zu\n",data.eyes.size(),data.eyeMeshes.size());assert(data.eyes.size()==2 && data.eyeMeshes.size()==2);
 std::vector<float> controls(data.controllers.size(),0),output; assert(Mdl44Flex::Evaluate(data,controls.data(),controls.size(),output));
 int jaw=-1;for(size_t i=0;i<data.controllers.size();++i)if(data.controllers[i].name=="jaw_drop")jaw=(int)i;assert(jaw>=0);auto baseline=output;
 controls[jaw]=1;assert(Mdl44Flex::Evaluate(data,controls.data(),controls.size(),output));int changed=0;for(size_t i=0;i<output.size();++i)if(std::fabs(output[i]-baseline[i])>0.0001f)++changed;std::printf("jaw_drop changes %d flex outputs\n",changed);assert(changed>0);
 size_t moved=0;
 for(const auto &mesh:data.meshes) for(const auto &flex:mesh.flexes) {
  float before=Mdl44Flex::Ramp(flex,baseline[flex.desc]);
  float after=Mdl44Flex::Ramp(flex,output[flex.desc]);
  if(std::fabs(after-before)>0.0001f) for(const auto &delta:flex.deltas)
   if(std::fabs(delta.position[0])+std::fabs(delta.position[1])+std::fabs(delta.position[2])>0.0001f) ++moved;
 }
 std::printf("jaw_drop changes %zu vertex deltas\n",moved);assert(moved>0);
 // A neutral lid controller must move the eyelid outputs, as in SE2007.
 const Mdl44Flex::Eye &eye=data.eyes[0];std::vector<float> face(data.descriptions.size(),0);
 face[eye.upper[1]]=1;face[eye.lower[1]]=1;const float up[3]={eye.up[0],eye.up[1],eye.up[2]};
 const float forward[3]={eye.forward[0],eye.forward[1],eye.forward[2]};
 assert(Mdl44Flex::Eyelids(eye,up,forward,face));
 std::printf("neutral eyelids: %.4f %.4f\n",face[eye.upperLid],face[eye.lowerLid]);
 assert(std::fabs(face[eye.upperLid])+std::fabs(face[eye.lowerLid])>0.001f);
 auto invalid=bytes; std::memset(invalid.data()+276,0xff,4); Mdl44Flex::Data parsed;assert(!Mdl44Flex::Parse(invalid.data(),invalid.size(),parsed));return 0;
}
