// mdl44_sequence_test <master.mdl> <included.mdl>
#include "../mdl44_sequence.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
static std::vector<unsigned char> Read(const char *path)
{ std::ifstream f(path,std::ios::binary); return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>()); }
int main(int argc,char **argv)
{
    if(argc!=3) return 2;
    std::vector<unsigned char> a=Read(argv[1]),b=Read(argv[2]);
    Mdl44Sequence::Data master,include;
    if(!Mdl44Sequence::Parse(a.data(),a.size(),master)) { std::puts("master sequence parse failed"); return 1; }
    if(!Mdl44Sequence::Parse(b.data(),b.size(),include)) { std::puts("included sequence parse failed"); return 1; }
    assert(master.sequences.size()==7 && master.includes.size()==3);
    assert(Mdl44Sequence::Find(master,"IDLE_SUBTLE") == &master.sequences[0]);
    assert(!Mdl44Sequence::Find(master,"no_such_sequence"));
    bool foundInclude=false,foundGesture=false;
    for(size_t i=0;i<master.includes.size();++i) if(Mdl44Sequence::EqualName(master.includes[i].name,"models/gman_gestures.mdl")) foundInclude=true;
    for(size_t i=0;i<include.sequences.size();++i) if(Mdl44Sequence::EqualName(include.sequences[i].label,"HG_headback")) foundGesture=true;
    assert(foundInclude && foundGesture);
    const Mdl44Sequence::Sequence *lean=Mdl44Sequence::Find(include,"P_leanback");
    assert(lean && lean->layers.size()==2 && lean->boneWeights.size()==56);
    assert(Mdl44Sequence::EqualName(include.sequences[lean->layers[0].sequence].label,"P_leanbackapexDelta"));
    assert(Mdl44Sequence::EqualName(include.sequences[lean->layers[1].sequence].label,"P_leanbackloopDelta"));
    assert(lean->layers[1].flags==64);
    Mdl44Skeleton::Skeleton masterSkeleton,localSkeleton;
    assert(Mdl44Skeleton::Parse(a.data(),a.size(),masterSkeleton) && Mdl44Skeleton::Parse(b.data(),b.size(),localSkeleton));
    std::vector<int> map; assert(Mdl44Sequence::MapBones(masterSkeleton,localSkeleton,map));
    size_t matched=0; for(size_t i=0;i<map.size();++i) if(map[i]>=0) ++matched;
    assert(matched>0);
    std::vector<unsigned char> corrupt=a; uint32_t bad=0x7fffffff; std::memcpy(corrupt.data()+192,&bad,4);
    assert(!Mdl44Sequence::Parse(corrupt.data(),corrupt.size(),master));
    assert(master.sequences.size()==7);
    std::printf("master: %u sequences, %u includes; gestures: %u sequences; %u/%u bones mapped\n",
        unsigned(master.sequences.size()),unsigned(master.includes.size()),unsigned(include.sequences.size()),unsigned(matched),unsigned(map.size()));
}
