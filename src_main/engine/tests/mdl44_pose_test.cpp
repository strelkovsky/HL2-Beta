// mdl44_pose_test <gman_high.mdl> <gman_gestures.mdl> <gman_gestures.ani>
#include "../mdl44_pose.h"
#include "../mdl44_binding.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
static std::vector<unsigned char> Read(const char *p)
{ std::ifstream f(p,std::ios::binary); return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>()); }
static float Distance(const std::vector<Mdl44Animation::LocalPose> &a,const std::vector<Mdl44Animation::LocalPose> &b)
{
    float r=0; for(size_t i=0;i<a.size();++i) for(int k=0;k<3;++k) r+=std::fabs(a[i].position[k]-b[i].position[k]);
    for(size_t i=0;i<a.size();++i) for(int k=0;k<4;++k) r+=std::fabs(a[i].rotation[k]-b[i].rotation[k]); return r;
}
int main(int argc,char **argv)
{
    if(argc!=4) return 2;
    const std::vector<unsigned char> master=Read(argv[1]),group=Read(argv[2]),ani=Read(argv[3]);
    Mdl44Skeleton::Skeleton a,b; Mdl44Sequence::Data seqs; Mdl44Animation::Animations anims;
    assert(Mdl44Skeleton::Parse(master.data(),master.size(),a) && Mdl44Skeleton::Parse(group.data(),group.size(),b));
    assert(Mdl44Sequence::Parse(group.data(),group.size(),seqs) && Mdl44Animation::Parse(group.data(),group.size(),anims));
    std::vector<int> mapping; assert(Mdl44Sequence::MapBones(a,b,mapping));
    Mdl44Pose::Source src={group.data(),ani.data(),group.size(),ani.size(),&b,&anims,&seqs,&mapping};
    std::vector<Mdl44Animation::LocalPose> bind(a.bones.size());
    for(size_t i=0;i<bind.size();++i)
    { std::memcpy(bind[i].position,a.bones[i].position,sizeof(bind[i].position)); std::memcpy(bind[i].rotation,a.bones[i].quaternion,sizeof(bind[i].rotation)); }
    const char *names[]={"HG_headback","HG_forwardright","P_leanback"};
    for(int i=0;i<3;++i)
    {
        const Mdl44Sequence::Sequence *seq=Mdl44Sequence::Find(seqs,names[i]); assert(seq);
        size_t index=seq-&seqs.sequences[0]; std::vector<Mdl44Animation::LocalPose> result=bind;
        if(!Mdl44Pose::Accumulate(src,index,0.35f,1.0f,result)) {std::fprintf(stderr,"could not evaluate %s\n",names[i]); return 3;}
        const float distance=Distance(bind,result); if(distance<0.01f) { std::fprintf(stderr,"no gesture motion %s\n",names[i]); return 4; }
        std::vector<Mdl44Binding::Matrix> matrices; assert(Mdl44Binding::BuildSkinMatrices(a,result,matrices));
        std::vector<Mdl44Animation::LocalPose> zero=bind; assert(Mdl44Pose::Accumulate(src,index,0.35f,0.0f,zero));
        assert(Distance(bind,zero)<0.0001f);
        std::vector<Mdl44Animation::LocalPose> bad=bind; assert(!Mdl44Pose::Accumulate(src,index,1.1f,1.0f,bad)); assert(Distance(bind,bad)==0);
        std::printf("%s: blended %u bones, pose delta %.4f\n",names[i],unsigned(matrices.size()),distance);
    }
}
