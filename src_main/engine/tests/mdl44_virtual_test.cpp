// mdl44_virtual_test <gman_high.mdl> <male_shared.mdl> <breen_anims.mdl> <gman_gestures.mdl>
#include "../mdl44_virtual.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
static std::vector<unsigned char> Read(const char *p)
{ std::ifstream f(p,std::ios::binary); return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>()); }
int main(int argc,char **argv)
{
    if(argc!=5) return 2;
    Mdl44Virtual::Model model;
    // Header decoding also accepts unaligned buffers and rejects truncation.
    std::vector<unsigned char> first=Read(argv[1]), unaligned(first.size()+1);
    std::memcpy(unaligned.data()+1,first.data(),first.size());
    Studio44::studiohdr_t header={};
    assert(Studio44::ReadHeader(unaligned.data()+1,first.size(),sizeof(header),header));
    assert(header.version==44 && header.numbones==70 && header.numlocalseq==7);
    assert(!Studio44::ReadHeader(unaligned.data()+1,100,sizeof(header),header));
    assert(header.version==44 && header.numbones==70);
    for(int i=1;i<argc;++i)
    {
        std::vector<unsigned char> bytes=Read(argv[i]); Mdl44Skeleton::Skeleton skeleton;
        Mdl44Animation::Animations anims; Mdl44Sequence::Data seqs;
        const bool sk=Mdl44Skeleton::Parse(bytes.data(),bytes.size(),skeleton);
        const bool an=Mdl44Animation::Parse(bytes.data(),bytes.size(),anims);
        const bool se=Mdl44Sequence::Parse(bytes.data(),bytes.size(),seqs);
        if(!sk || !an || !se)
        { std::fprintf(stderr,"could not parse %s: skeleton=%d anim=%d sequences=%d\n",argv[i],sk,an,se); return 1; }
        assert(model.Append(argv[i],skeleton,anims,seqs));
    }
    assert(model.Groups().size()==4);
    const Mdl44Virtual::Entry *idle=model.FindSequence("idle_subtle");
    const Mdl44Virtual::Entry *head=model.FindSequence("HG_headback");
    const Mdl44Virtual::Entry *lean=model.FindSequence("P_leanback");
    assert(idle && idle->group==0 && head && head->group==3 && lean && lean->group==3);
    const Mdl44Virtual::Group &gesture=model.Groups()[3];
    assert(gesture.masterBone.size()==56 && gesture.boneMap.size()==70);
    for(size_t i=0;i<gesture.masterBone.size();++i) assert(gesture.masterBone[i]>=0 && gesture.boneMap[gesture.masterBone[i]]==int(i));
    assert(!model.FindSequence("not_a_sequence"));
    std::printf("%u groups, %u virtual sequences, %u animations; G-Man gestures mapped to group %u\n",
        unsigned(model.Groups().size()),unsigned(model.Sequences().size()),unsigned(model.Animations().size()),unsigned(head->group));
}
