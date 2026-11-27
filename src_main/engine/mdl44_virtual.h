// Virtual sequence and animation tables following SE2007 virtualmodel_t.
// The caller owns file loading and determines include traversal/lifetime.
#ifndef ENGINE_MDL44_VIRTUAL_H
#define ENGINE_MDL44_VIRTUAL_H
#include "mdl44_sequence.h"

namespace Mdl44Virtual
{
    struct Entry { size_t group, index; };
    struct SequenceInfo { int flags; float duration; bool hasDuration; };
    struct Group
    {
        std::string name;
        Mdl44Skeleton::Skeleton skeleton;
        Mdl44Animation::Animations animations;
        Mdl44Sequence::Data sequences;
        std::vector<int> boneMap, masterBone;
        std::vector<size_t> masterSequence, masterAnimation;
    };
    class Model
    {
    public:
        bool Append(const char *name, const Mdl44Skeleton::Skeleton &skeleton,
                    const Mdl44Animation::Animations &animations, const Mdl44Sequence::Data &sequences)
        {
            if (!name || !*name || skeleton.bones.empty()) return false;
            for(size_t i=0;i<sequences.sequences.size();++i)
                for(size_t j=0;j<sequences.sequences[i].animations.size();++j)
                    if(sequences.sequences[i].animations[j]>=animations.clips.size()) return false;
            Group group; group.name=name; group.skeleton=skeleton; group.animations=animations; group.sequences=sequences;
            const Mdl44Skeleton::Skeleton &master=m_groups.empty()?skeleton:m_groups[0].skeleton;
            if(!Mdl44Sequence::MapBones(master,skeleton,group.masterBone)) return false;
            group.boneMap.assign(master.bones.size(),-1);
            for(size_t i=0;i<group.masterBone.size();++i)
                if(group.masterBone[i]>=0) group.boneMap[group.masterBone[i]]=static_cast<int>(i);
            const size_t groupIndex=m_groups.size(), oldSeq=m_sequences.size(), oldAnim=m_animations.size();
            m_groups.push_back(group);
            Group &added=m_groups.back();
            // As in SE2007, duplicates are looked up among previously added groups.
            for(size_t i=0;i<sequences.sequences.size();++i)
            {
                const Mdl44Sequence::Sequence &seq=sequences.sequences[i]; size_t found=oldSeq;
                for(size_t j=0;j<oldSeq;++j)
                {
                    const Entry &entry=m_sequences[j];
                    if(Mdl44Sequence::EqualName(seq.label,m_groups[entry.group].sequences.sequences[entry.index].label))
                    { found=j; break; }
                }
                if(found==oldSeq)
                {
                    found=m_sequences.size(); Entry entry={groupIndex,i}; m_sequences.push_back(entry);
                }
                else
                {
                    const Entry &old=m_sequences[found];
                    if(m_groups[old.group].sequences.sequences[old.index].flags & 0x0800)
                    { Entry entry={groupIndex,i}; m_sequences[found]=entry; }
                }
                added.masterSequence.push_back(found);
            }
            for(size_t i=0;i<animations.clips.size();++i)
            {
                size_t found=oldAnim;
                for(size_t j=0;j<oldAnim;++j)
                {
                    const Entry &entry=m_animations[j];
                    if(Mdl44Sequence::EqualName(animations.clips[i].name,m_groups[entry.group].animations.clips[entry.index].name))
                    { found=j; break; }
                }
                if(found==oldAnim)
                { found=m_animations.size(); Entry entry={groupIndex,i}; m_animations.push_back(entry); }
                added.masterAnimation.push_back(found);
            }
            return true;
        }
        const std::vector<Group> &Groups() const { return m_groups; }
        const std::vector<Entry> &Sequences() const { return m_sequences; }
        const std::vector<Entry> &Animations() const { return m_animations; }
        bool GetSequenceInfo(size_t index, SequenceInfo &out) const
        {
            if(index>=m_sequences.size()) return false;
            const Entry &entry=m_sequences[index];
            if(entry.group>=m_groups.size() || entry.index>=m_groups[entry.group].sequences.sequences.size()) return false;
            const Group &group=m_groups[entry.group];
            const Mdl44Sequence::Sequence &seq=group.sequences.sequences[entry.index];
            SequenceInfo info={seq.flags,0.0f,false};
            // SE2007 duration requires pose parameters for multi-animation blends.
            if(seq.animations.size()==1 && seq.animations[0]<group.animations.clips.size())
            {
                const Mdl44Animation::Descriptor &clip=group.animations.clips[seq.animations[0]];
                info.duration=clip.fps>0 && clip.frames>1 ? (clip.frames-1)/clip.fps : 0.0f;
                info.hasDuration=true;
            }
            out=info; return true;
        }
        const Entry *FindSequence(const char *name) const
        {
            if(!name) return NULL;
            for(size_t i=0;i<m_sequences.size();++i)
            {
                const Entry &entry=m_sequences[i];
                if(Mdl44Sequence::EqualName(m_groups[entry.group].sequences.sequences[entry.index].label,name)) return &entry;
            }
            return NULL;
        }
    private:
        std::vector<Group> m_groups;
        std::vector<Entry> m_sequences, m_animations;
    };
}
#endif
