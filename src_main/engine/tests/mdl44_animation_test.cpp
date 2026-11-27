// Standalone: mdl44_animation_test <gman_high.mdl> <gman_high.vvd> <Gman_gestures.mdl> <Gman_gestures.ani>
#include "../mdl44_animation.h"
#include "../mdl44_binding.h"
#include "../mdl44_model.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
static std::vector<unsigned char> Read(const char *name)
{ std::ifstream f(name, std::ios::binary); return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
static void Put32(std::vector<unsigned char> &d, size_t i, uint32_t v) { std::memcpy(d.data()+i, &v, 4); }
int main(int argc, char **argv)
{
    if (argc != 6) return 2;
    const std::vector<unsigned char> mdl=Read(argv[1]), vvd=Read(argv[2]), group=Read(argv[3]), ani=Read(argv[4]), vtx=Read(argv[5]);
    Mdl44Skeleton::Skeleton skeleton; assert(Mdl44Skeleton::Parse(mdl.data(), mdl.size(), skeleton));
    Vvd4Native::Vertices vertices; assert(Vvd4Native::Load(vvd.data(), vvd.size(), skeleton.checksum, 0, vertices));
    std::vector<Mdl44Binding::Vertex> binding; assert(Mdl44Binding::Bind(skeleton, vertices, binding));
    assert(binding.size()==4843);
    for(size_t i=0;i<binding.size();++i) for(unsigned j=0;j<binding[i].count;++j) assert(binding[i].influences[j].bone<70);
    Vvd4Native::Vertices badVertices=vertices; badVertices.vertices[0].data[12]=255;
    assert(!Mdl44Binding::Bind(skeleton, badVertices, binding)); assert(binding.size()==4843);
    badVertices=vertices; badVertices.vertices[0].data[15]=0;
    assert(!Mdl44Binding::Bind(skeleton, badVertices, binding));

    std::vector<Mdl44Animation::LocalPose> bindPose(skeleton.bones.size());
    for(size_t i=0;i<skeleton.bones.size();++i)
    {
        std::memcpy(bindPose[i].position,skeleton.bones[i].position,sizeof(bindPose[i].position));
        std::memcpy(bindPose[i].rotation,skeleton.bones[i].quaternion,sizeof(bindPose[i].rotation));
    }
    std::vector<Mdl44Binding::Matrix> skin; assert(Mdl44Binding::BuildSkinMatrices(skeleton,bindPose,skin));
    float maxError=0;
    for(size_t i=0;i<binding.size();++i)
    {
        StudioVertexView::Vertex original; float transformed[3];
        assert(StudioVertexView::ReadVvd4(vertices.vertices[i].data,48,original));
        assert(Mdl44Binding::DeformPosition(original,binding[i],skin,transformed));
        for(int j=0;j<3;++j) if(std::fabs(original.position[j]-transformed[j])>maxError) maxError=std::fabs(original.position[j]-transformed[j]);
    }
    if(maxError>0.01f) { std::fprintf(stderr,"bind pose error %f\n",maxError); return 12; }
    Mdl44Skeleton::Skeleton invalidSkeleton=skeleton; invalidSkeleton.bones[0].parent=1; invalidSkeleton.bones[1].parent=0;
    assert(!Mdl44Binding::BuildSkinMatrices(invalidSkeleton,bindPose,skin));
    Mdl44::StudioModel geometry;
    assert(!geometry.Load(mdl.data(),mdl.size(),vvd.data(),vvd.size(),vtx.data(),vtx.size()));
    assert(geometry.Load(mdl.data(),mdl.size(),vvd.data(),vvd.size(),vtx.data(),vtx.size(),false));
    assert(geometry.Description().boneCount==70);
    size_t hardwareGroups=0, softwareGroups=0, checkedInfluences=0;
    const Mdl44Static::Description &description=geometry.Description();
    for(size_t b=0;b<description.bodyParts.size();++b) for(size_t m=0;m<description.bodyParts[b].models.size();++m)
    for(size_t k=0;k<description.bodyParts[b].models[m].meshes.size();++k)
    {
        StudioVertexView::Mesh mesh(StudioVertexView::Mesh::Vvd4,NULL,NULL,0); assert(geometry.Mesh(b,m,k,mesh));
        for(size_t g=0;;++g)
        {
            Mdl44::GroupView group; if(!geometry.Group(b,m,0,k,g,group)) break;
            if(!(group.Flags() & 2)) { ++softwareGroups; continue; }
            ++hardwareGroups; int palette[16]; for(int i=0;i<16;++i) palette[i]=-1;
            for(size_t s=0;s<group.StripCount();++s)
            {
                Mdl44::Strip strip; assert(group.GetStrip(s,strip));
                for(size_t j=0;j<strip.boneStateCount;++j)
                {
                    int32_t hardware,bone; assert(group.BoneState(s,j,hardware,bone));
                    if(bone<0) break;
                    assert(hardware>=0 && hardware<16 && bone<70); palette[hardware]=bone;
                }
                for(size_t j=0;j<strip.indexCount;++j)
                {
                    uint16_t index; Mdl44::OptimizedVertex optimized; StudioVertexView::Vertex vertex;
                    assert(group.Index(strip.indexOffset+j,index)); assert(group.Vertex(index,optimized));
                    assert(mesh.Read(optimized.meshVertex,vertex));
                    for(unsigned int w=0;w<optimized.boneCount;++w)
                    {
                        assert(optimized.weightIndex[w]<vertex.boneCount);
                        const int slot=optimized.boneId[w];
                        if(slot<0) continue;
                        if(slot>=16 || palette[slot]!=vertex.bones[optimized.weightIndex[w]])
                        { std::fprintf(stderr,"VTX palette mismatch group=%u strip=%u slot=%d mapped=%d expected=%d\n",static_cast<unsigned>(g),static_cast<unsigned>(s),slot,slot<16?palette[slot]:-1,vertex.bones[optimized.weightIndex[w]]); return 13; }
                        ++checkedInfluences;
                    }
                }
            }
        }
    }
    assert(hardwareGroups>0 && softwareGroups>0 && checkedInfluences>0);
    std::printf("VTX palette: %u hardware groups, %u software groups, %u influences verified\n",static_cast<unsigned>(hardwareGroups),static_cast<unsigned>(softwareGroups),static_cast<unsigned>(checkedInfluences));
    Mdl44Animation::Animations clips; assert(Mdl44Animation::Parse(mdl.data(),mdl.size(),clips));
    assert(clips.clips.size()==14 && clips.clips[0].name=="@idle_subtle" && clips.clips[0].frames==151);
    Mdl44Animation::Data data; Mdl44Animation::Track track;
    assert(Mdl44Animation::Resolve(mdl.data(),mdl.size(),NULL,0,clips,clips.clips[0],data));
    assert(Mdl44Animation::FindTrack(data,0,70,track) && track.flags==12);
    int16_t value=0;
    assert(Mdl44Animation::SampleChannel(data,track,true,0,0,value)==Mdl44Animation::ChannelValue && value==0);
    assert(Mdl44Animation::SampleChannel(data,track,true,1,0,value)==Mdl44Animation::ChannelValue && value==0);
    assert(Mdl44Animation::SampleChannel(data,track,false,0,0,value)==Mdl44Animation::ChannelValue && value==369);
    assert(Mdl44Animation::SampleChannel(data,track,false,1,0,value)==Mdl44Animation::ChannelValue && value==136);
    const unsigned char runs[]={2,4,10,0,20,0,1,2,0xfd,0xff};
    assert(Mdl44Animation::SampleStream(runs,sizeof(runs),0,0,value) && value==10);
    assert(Mdl44Animation::SampleStream(runs,sizeof(runs),0,3,value) && value==20);
    assert(Mdl44Animation::SampleStream(runs,sizeof(runs),0,4,value) && value==-3);
    assert(!Mdl44Animation::SampleStream(runs,sizeof(runs),0,6,value));
    const unsigned char corrupt[]={0,3}; assert(!Mdl44Animation::SampleStream(corrupt,sizeof(corrupt),0,0,value));
    assert(!Mdl44Animation::SampleStream(runs,3,0,0,value));

    Mdl44Animation::Animations extra; assert(Mdl44Animation::Parse(group.data(),group.size(),extra));
    assert(extra.clips.size()==68 && extra.clips[1].name=="@P_leanbackapexDelta");
    assert(Mdl44Animation::Resolve(group.data(),group.size(),ani.data(),ani.size(),extra,extra.clips[1],data));
    assert(Mdl44Animation::FindTrack(data,0,56,track) && track.flags==28);
    assert(Mdl44Animation::SampleChannel(data,track,false,0,0,value)==Mdl44Animation::ChannelValue && value==0);
    assert(!Mdl44Animation::Resolve(group.data(),group.size(),ani.data(),32,extra,extra.clips[1],data));
    // Exercise every available run at both ends of local and external clips.
    for (int pass=0; pass<2; ++pass)
    {
        const std::vector<unsigned char> &owner = pass ? group : mdl;
        const Mdl44Animation::Animations &set = pass ? extra : clips;
        const size_t bones = pass ? 56 : 70;
        for (size_t i=0; i<set.clips.size(); ++i)
        {
            const Mdl44Animation::Descriptor &clip=set.clips[i];
            if (!Mdl44Animation::Resolve(owner.data(),owner.size(),pass?ani.data():NULL,pass?ani.size():0,set,clip,data)) continue;
            if (data.bytes[data.start]==255) continue; // A clip with no bone records.
            for (size_t bone=0;bone<bones;++bone)
            {
                if (!Mdl44Animation::FindTrack(data,static_cast<unsigned>(bone),bones,track)) continue;
                for(int mode=0;mode<2;++mode) for(unsigned axis=0;axis<3;++axis)
                {
                    const Mdl44Animation::ChannelResult first=Mdl44Animation::SampleChannel(data,track,mode==0,axis,0,value);
                    if (first==Mdl44Animation::ChannelAbsent) continue;
                    if (first!=Mdl44Animation::ChannelValue ||
                        Mdl44Animation::SampleChannel(data,track,mode==0,axis,clip.frames-1,value)!=Mdl44Animation::ChannelValue)
                    { std::fprintf(stderr,"bad channel clip=%s bone=%u axis=%u mode=%d frames=%d\n",clip.name.c_str(),static_cast<unsigned>(bone),axis,mode,clip.frames); return 10; }
                }
            }
        }
    }
    std::vector<Mdl44Animation::LocalPose> pose, last;
    for (size_t i=0;i<clips.clips.size();++i)
    {
        const Mdl44Animation::Descriptor &clip=clips.clips[i];
        if (clip.flags & 4) continue;
        assert(Mdl44Animation::Resolve(mdl.data(),mdl.size(),NULL,0,clips,clip,data));
        if (!Mdl44Animation::SamplePose(data,clip,skeleton,0,pose) ||
            !Mdl44Animation::SamplePose(data,clip,skeleton,clip.frames-1,last))
        { std::fprintf(stderr,"bad pose clip=%s frames=%d\n",clip.name.c_str(),clip.frames); return 11; }
        assert(pose.size()==70 && last.size()==70);
    }
    assert(Mdl44Animation::Resolve(mdl.data(),mdl.size(),NULL,0,clips,clips.clips[0],data));
    assert(Mdl44Animation::SamplePose(data,clips.clips[0],skeleton,0,pose));
    assert(Mdl44Animation::SamplePose(data,clips.clips[0],skeleton,25,last));
    assert(std::fabs(pose[0].position[0] - (skeleton.bones[0].position[0] + 369*skeleton.bones[0].positionScale[0])) < 0.0001f);
    assert(std::fabs(pose[0].rotation[0]-last[0].rotation[0]) > 0.00001f || std::fabs(pose[0].position[0]-last[0].position[0]) > 0.00001f);
    assert(Mdl44Binding::BuildSkinMatrices(skeleton,last,skin));
    float maxMovement=0;
    for(size_t i=0;i<binding.size();++i)
    {
        StudioVertexView::Vertex animatedVertex; float animatedPosition[3];
        assert(StudioVertexView::ReadVvd4(vertices.vertices[i].data,48,animatedVertex));
        assert(Mdl44Binding::DeformPosition(animatedVertex,binding[i],skin,animatedPosition));
        for(int j=0;j<3;++j) if(std::fabs(animatedPosition[j]-animatedVertex.position[j])>maxMovement) maxMovement=std::fabs(animatedPosition[j]-animatedVertex.position[j]);
    }
    assert(maxMovement > 0.01f);
    std::printf("bind pose max error %.6f; animated vertex max displacement %.3f\n", maxError, maxMovement);
    Mdl44Skeleton::Skeleton groupSkeleton; assert(Mdl44Skeleton::Parse(group.data(),group.size(),groupSkeleton));
    assert(Mdl44Animation::Resolve(group.data(),group.size(),ani.data(),ani.size(),extra,extra.clips[0],data));
    assert(Mdl44Animation::SamplePose(data,extra.clips[0],groupSkeleton,0,pose) && pose.size()==56);
    assert(!Mdl44Animation::SamplePose(data,extra.clips[0],groupSkeleton,1,pose));
    assert(Mdl44Animation::Resolve(group.data(),group.size(),ani.data(),ani.size(),extra,extra.clips[1],data));
    assert(!Mdl44Animation::SamplePose(data,extra.clips[1],groupSkeleton,0,pose));
    assert(Mdl44Animation::SampleDeltaPose(data,extra.clips[1],groupSkeleton,0,pose));
    assert(pose.size()==groupSkeleton.bones.size());
    // Bone 0 has a delta track. Its translation is a scaled offset, not the bind position.
    assert(Mdl44Animation::FindTrack(data,0,56,track) && (track.flags & Mdl44Animation::Delta));
    for (unsigned axis=0;axis<3;++axis)
    {
        assert(Mdl44Animation::SampleChannel(data,track,false,axis,0,value)==Mdl44Animation::ChannelValue);
        assert(std::fabs(pose[0].position[axis]-value*groupSkeleton.bones[0].positionScale[axis])<0.0001f);
    }
    size_t deltaCount=0;
    for(size_t i=0;i<extra.clips.size();++i)
    {
        const Mdl44Animation::Descriptor &clip=extra.clips[i];
        if(!(clip.flags & 4) || !Mdl44Animation::Resolve(group.data(),group.size(),ani.data(),ani.size(),extra,clip,data)) continue;
        if(!Mdl44Animation::SampleDeltaPose(data,clip,groupSkeleton,0,pose) ||
           !Mdl44Animation::SampleDeltaPose(data,clip,groupSkeleton,clip.frames-1,last))
        { std::fprintf(stderr,"bad delta pose %s\n",clip.name.c_str()); return 13; }
        ++deltaCount;
    }
    assert(deltaCount>0);
    std::printf("sampled %u included delta clips at first and last frame\n",unsigned(deltaCount));
    std::vector<unsigned char> bad=mdl; Put32(bad,184,0x7fffffff);
    assert(!Mdl44Animation::Parse(bad.data(),bad.size(),clips)); assert(clips.clips.size()==14);
    std::printf("bound %u vertices; sampled local and external animation channels; invalid inputs rejected\n",static_cast<unsigned>(binding.size()));
}
