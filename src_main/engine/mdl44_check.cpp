// Inspect a release static model through the engine's normal filesystem.
#include "quakedef.h"
#include "cmd.h"
#include "convar.h"
#include "filesystem.h"
#include "filesystem_engine.h"
#include "gl_matsysiface.h"
#include "istudiorender44.h"
#include "materialsystem/imaterialvar.h"
#include "imdl44entitypreview.h"
#include "imdl44modelinfo.h"
#include "mdl44_modelcache.h"
#include "interface.h"
#include "mdl44_model.h"
#include "mdl44_mapcache.h"
#include "mdl44_skeleton.h"
#include "mdl44_animation.h"
#include "mdl44_sequence.h"
#include "mdl44_virtual.h"
#include "mdl44_binding.h"
#include "mdl44_pose.h"
#include "mdl44_flex.h"
#include "lightcache.h"
#include "istudiorender.h"
#include "bspfile.h"
extern IStudioRender *g_pStudioRender;
extern ConVar mat_fullbright;
extern int d_lightstylevalue[256];
extern float g_lightmapScale;
bool WorldLightToMaterialLight( dworldlight_t *, LightDesc_t & );
#include <climits>

// memdbgon must be the last include file.
#include "tier0/memdbgon.h"

static bool ReadModelFile44( const std::string &name, std::vector<unsigned char> &out )
{
    FileHandle_t file = g_pFileSystem->Open( name.c_str(), "rb", "GAME" );
    if ( !file ) return false;
    const unsigned int size = g_pFileSystem->Size( file );
    if ( !size || size > INT_MAX ) { g_pFileSystem->Close( file ); return false; }
    std::vector<unsigned char> contents( size );
    const bool ok = g_pFileSystem->Read( contents.data(), static_cast<int>( size ), file ) == static_cast<int>( size );
    g_pFileSystem->Close( file );
    if ( ok ) out.swap( contents );
    return ok;
}

// Build a virtual model in the same depth-first include order as SE2007.
// Used for native metadata caching and the standalone diagnostic.
static bool LoadVirtualGroup44(const std::string &name, Mdl44Virtual::Model &model, std::vector<std::string> &stack)
{
    if (stack.size() >= 32) { Warning("MDL 44 virtual model: include depth exceeded at %s\n",name.c_str()); return false; }
    for(size_t i=0;i<stack.size();++i)
        if(Mdl44Sequence::EqualName(stack[i],name)) { Warning("MDL 44 virtual model: include cycle at %s\n",name.c_str()); return false; }
    std::vector<unsigned char> bytes; Mdl44Skeleton::Skeleton skeleton;
    Mdl44Animation::Animations anims; Mdl44Sequence::Data sequences;
    if(!ReadModelFile44(name,bytes) || !Mdl44Skeleton::Parse(bytes.data(),bytes.size(),skeleton) ||
       !Mdl44Animation::Parse(bytes.data(),bytes.size(),anims) || !Mdl44Sequence::Parse(bytes.data(),bytes.size(),sequences) ||
       !model.Append(name.c_str(),skeleton,anims,sequences))
    { Warning("MDL 44 virtual model: cannot load %s\n",name.c_str()); return false; }
    stack.push_back(name);
    for(size_t i=0;i<sequences.includes.size();++i)
        if(!LoadVirtualGroup44(sequences.includes[i].name,model,stack)) { stack.pop_back(); return false; }
    stack.pop_back();
    return true;
}

// Metadata is attached to the engine model object, not its mutable legacy name.
// Kept separate until all users of studiohdr_t are migrated.
static ConVar r_mdl44_modelcache("r_mdl44_modelcache", "1", 0, "Cache native v44 sequence metadata when models load.");
struct Mdl44GroupSource
{
    bool attempted, valid;
    std::vector<unsigned char> mdl, ani;
    Mdl44GroupSource() : attempted(false), valid(false) {}
};
struct Mdl44CachedModel
{
    model_t *identity;
    std::string originalName;
    Studio44::studiohdr_t header;
    Mdl44Virtual::Model virtualModel;
    Mdl44Flex::Data flex;
    bool flexValid;
    std::vector<Mdl44GroupSource> sources;
};
static std::vector<Mdl44CachedModel *> s_Mdl44Models;
static Mdl44CachedModel *FindCached44(const model_t *identity)
{
    for(size_t i=0;i<s_Mdl44Models.size();++i) if(s_Mdl44Models[i]->identity==identity) return s_Mdl44Models[i];
    return NULL;
}
bool Mdl44ModelCacheHas(const model_t *model) { return FindCached44(model)!=NULL; }
bool Mdl44ModelCacheHeader(const model_t *model, Studio44::studiohdr_t &out)
{ Mdl44CachedModel *cached=FindCached44(model); if(!cached) return false; out=cached->header; return true; }
void Mdl44ModelCacheDetach(model_t *model)
{
    for(size_t i=0;i<s_Mdl44Models.size();++i)
        if(s_Mdl44Models[i]->identity==model)
        { delete s_Mdl44Models[i]; s_Mdl44Models.erase(s_Mdl44Models.begin()+i); return; }
}
void Mdl44ModelCacheAttach(model_t *model, const char *name)
{
    Mdl44ModelCacheDetach(model);
    if(!r_mdl44_modelcache.GetBool() || !model || !name || !*name) return;
    Mdl44CachedModel *cached=new Mdl44CachedModel; cached->identity=model; cached->originalName=name; cached->flexValid=false;
    std::vector<std::string> stack; std::vector<unsigned char> root;
    if(!ReadModelFile44(name,root) || !Studio44::ReadHeader(root.data(),root.size(),sizeof(cached->header),cached->header) ||
       !LoadVirtualGroup44(name,cached->virtualModel,stack)) { delete cached; return; }
    cached->flexValid=Mdl44Flex::Parse(root.data(),root.size(),cached->flex);
    cached->sources.resize(cached->virtualModel.Groups().size());
    s_Mdl44Models.push_back(cached);
    Msg("MDL 44 model cache: %s: %u groups, %u sequences\n",cached->originalName.c_str(),
        static_cast<unsigned>(cached->virtualModel.Groups().size()),
        static_cast<unsigned>(cached->virtualModel.Sequences().size()));
}
class Mdl44ModelInfo : public IMdl44ModelInfo
{
public:
    virtual bool HasModel(const model_t *model) const { return FindCached44(model)!=NULL; }
    virtual bool GetStudioHeader(const model_t *model, Studio44::studiohdr_t &out) const
    { Mdl44CachedModel *c=FindCached44(model); if(!c) return false; out=c->header; return true; }
    virtual const char *OriginalName(const model_t *model) const
    { Mdl44CachedModel *c=FindCached44(model); return c ? c->originalName.c_str() : NULL; }
    virtual int SequenceCount(const model_t *model) const
    { Mdl44CachedModel *c=FindCached44(model); return c ? static_cast<int>(c->virtualModel.Sequences().size()) : 0; }
    virtual int FindSequence(const model_t *model, const char *name) const
    {
        Mdl44CachedModel *c=FindCached44(model); if(!c || !name) return -1;
        const Mdl44Virtual::Entry *found=c->virtualModel.FindSequence(name);
        return found ? static_cast<int>(found-&c->virtualModel.Sequences()[0]) : -1;
    }
    virtual const char *SequenceName(const model_t *model, int sequence) const
    {
        Mdl44CachedModel *c=FindCached44(model);
        if(!c || sequence<0 || static_cast<size_t>(sequence)>=c->virtualModel.Sequences().size()) return NULL;
        const Mdl44Virtual::Entry &entry=c->virtualModel.Sequences()[sequence];
        return c->virtualModel.Groups()[entry.group].sequences.sequences[entry.index].label.c_str();
    }
    virtual bool SequenceInfo(const model_t *model, int sequence, int &flags, float &duration) const
    {
        Mdl44CachedModel *c=FindCached44(model); Mdl44Virtual::SequenceInfo info;
        if(!c || sequence<0 || !c->virtualModel.GetSequenceInfo(static_cast<size_t>(sequence),info) || !info.hasDuration) return false;
        flags=info.flags; duration=info.duration; return true;
    }
    virtual int FlexControllerCount(const model_t *model) const {
        Mdl44CachedModel *c=FindCached44(model); return c && c->flexValid ? static_cast<int>(c->flex.controllers.size()) : 0;
    }
    virtual bool FlexController(const model_t *model,int index,const char *&name,const char *&type,float &minimum,float &maximum) const {
        Mdl44CachedModel *c=FindCached44(model); if(!c || !c->flexValid || index<0 || static_cast<size_t>(index)>=c->flex.controllers.size()) return false;
        const Mdl44Flex::Controller &f=c->flex.controllers[index];name=f.name.c_str();type=f.type.c_str();minimum=f.min;maximum=f.max;return true;
    }
    virtual int FindAttachment(const model_t *model, const char *name) const;
    virtual bool AttachmentTransform(const model_t *model, int attachment, float time, const Studio44Layer *layers, unsigned int layerCount, float *matrix3x4) const;
};
static Mdl44ModelInfo s_Mdl44ModelInfo;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(Mdl44ModelInfo, IMdl44ModelInfo, MDL44_MODEL_INFO_VERSION, s_Mdl44ModelInfo);

CON_COMMAND( mdl44_cache_check, "Inspect a loaded model's experimental MDL 44 metadata." )
{
    if(Cmd_Argc()<2 || Cmd_Argc()>3) { Msg("Usage: mdl44_cache_check models/path/model.mdl [sequence]\n"); return; }
    for(size_t i=0;i<s_Mdl44Models.size();++i)
    {
        Mdl44CachedModel *c=s_Mdl44Models[i];
        if(!Mdl44Sequence::EqualName(c->originalName,Cmd_Argv(1))) continue;
        Studio44::studiohdr_t header;
        if (!s_Mdl44ModelInfo.GetStudioHeader(c->identity,header)) return;
        Msg("MDL 44 cache: %s: header version %d, %d sequences\n",c->originalName.c_str(),header.version,s_Mdl44ModelInfo.SequenceCount(c->identity));
        if(Cmd_Argc()==3)
        {
            int seq=s_Mdl44ModelInfo.FindSequence(c->identity,Cmd_Argv(2));
            Msg("  sequence %s: %d%s\n",Cmd_Argv(2),seq,seq<0 ? " (not found)" : "");
        }
        return;
    }
    Msg("MDL 44 cache: %s is not cached\n",Cmd_Argv(1));
}

CON_COMMAND( mdl44_virtual_check, "Inspect MDL 44 included models and virtual sequences." )
{
    if(Cmd_Argc()<2 || Cmd_Argc()>3) { Msg("Usage: mdl44_virtual_check models/path/model.mdl [sequence]\n"); return; }
    Mdl44Virtual::Model model; std::vector<std::string> stack;
    if(!LoadVirtualGroup44(Cmd_Argv(1),model,stack)) return;
    Msg("MDL 44 virtual model: %u groups, %u sequences, %u animations\n",
        static_cast<unsigned>(model.Groups().size()),static_cast<unsigned>(model.Sequences().size()),
        static_cast<unsigned>(model.Animations().size()));
    for(size_t i=0;i<model.Groups().size();++i)
    {
        const Mdl44Virtual::Group &group=model.Groups()[i]; size_t mapped=0,invalid=0;
        for(size_t j=0;j<group.masterBone.size();++j) if(group.masterBone[j]>=0) ++mapped;
        for(size_t j=0;j<group.sequences.sequences.size();++j)
            for(size_t k=0;k<group.sequences.sequences[j].layers.size();++k)
                if(!group.sequences.sequences[j].layers[k].validTiming) ++invalid;
        Msg("  %s: %u/%u bones mapped, %u invalid layers\n",group.name.c_str(),static_cast<unsigned>(mapped),
            static_cast<unsigned>(group.masterBone.size()),static_cast<unsigned>(invalid));
    }
    if(Cmd_Argc()==3)
    {
        const Mdl44Virtual::Entry *entry=model.FindSequence(Cmd_Argv(2));
        if(entry) Msg("  sequence %s -> %s (local %u)\n",Cmd_Argv(2),model.Groups()[entry->group].name.c_str(),static_cast<unsigned>(entry->index));
        else Msg("  sequence %s not found\n",Cmd_Argv(2));
    }
}

static bool LoadModelGeometry44( const std::string &name, Mdl44::StudioModel &model, bool staticOnly )
{
    if ( name.size() < 5 || _stricmp( name.c_str() + name.size() - 4, ".mdl" ) ) return false;
    const std::string base = name.substr( 0, name.size() - 4 );
    std::vector<unsigned char> mdl, vvd, vtx;
    if ( !ReadModelFile44( name, mdl ) || mdl.size() < 156 || std::memcmp( mdl.data(), "IDST", 4 ) ||
         Mdl44Static::Read32( mdl.data(), 4 ) != 44 ||
         ( staticOnly && !( Mdl44Static::Read32( mdl.data(), 152 ) & 16 ) ) ) return false;
    return ReadModelFile44( base + ".vvd", vvd ) &&
           ReadModelFile44( base + ".dx80.vtx", vtx ) &&
           model.Load( mdl.data(), mdl.size(), vvd.data(), vvd.size(), vtx.data(), vtx.size(), staticOnly );
}

static bool LoadStaticModel44( const std::string &name, Mdl44::StaticModel &model )
{ return LoadModelGeometry44( name, model, true ); }

class Studio44MeshSource : public IStudio44MeshSource
{
public:
    Studio44MeshSource( const StudioVertexView::Mesh &mesh, const Mdl44::GroupView &group )
        : m_mesh( mesh ), m_group( group ) {}
    virtual unsigned int VertexCount() const { return static_cast<unsigned int>( m_group.VertexCount() ); }
    virtual unsigned int IndexCount() const { return static_cast<unsigned int>( m_group.IndexCount() ); }
    virtual bool Vertex( unsigned int i, Studio44UploadVertex &out ) const
    {
        Mdl44::OptimizedVertex optimized;
        if ( !m_group.Vertex( i, optimized ) || !m_mesh.Read( optimized.meshVertex, out.vertex ) ) return false;
        for ( unsigned int j=0; j<optimized.boneCount; ++j )
            if ( optimized.weightIndex[j] >= out.vertex.boneCount ) return false;
        const float *tangent = m_mesh.Tangent( optimized.meshVertex );
        if ( !tangent ) return false;
        std::memcpy( out.tangent, tangent, sizeof( out.tangent ) );
        std::memcpy( out.weightIndex, optimized.weightIndex, sizeof( out.weightIndex ) );
        std::memcpy( out.boneId, optimized.boneId, sizeof( out.boneId ) );
        out.boneCount = optimized.boneCount;
        out.meshVertex = optimized.meshVertex;
        return true;
    }
    virtual bool Index( unsigned int i, uint16_t &out ) const { return m_group.Index( i, out ); }
    virtual unsigned int StripCount() const { return static_cast<unsigned int>( m_group.StripCount() ); }
    virtual bool Strip( unsigned int i, Studio44Strip &out ) const
    {
        Mdl44::Strip s;
        if ( !m_group.GetStrip( i, s ) ) return false;
        out.indexCount = s.indexCount; out.indexOffset = s.indexOffset;
        out.vertexCount = s.vertexCount; out.vertexOffset = s.vertexOffset;
        out.boneStateCount = s.boneStateCount; out.boneCount = s.boneCount; out.flags = s.flags;
        return true;
    }
    virtual bool BoneState( unsigned int strip, unsigned int i, int32_t &hardware, int32_t &bone ) const
    { return m_group.BoneState( strip, i, hardware, bone ); }
private:
    StudioVertexView::Mesh m_mesh;
    Mdl44::GroupView m_group;
};

// Eye projection follows R_StudioEyeballPosition in SE2007. Matrices and
// view targets are world-space, as expected by the Eyes shader.
static ConVar r_mdl44_eye_debug("r_mdl44_eye_debug", "0", 0, "Log the MDL 44 eye target and iris projection.");
struct Studio44EyeState { Vector origin, up; float u[4], v[4]; bool valid; };
static bool Studio44EyeProjection(const Mdl44Flex::Eye &eye, const Mdl44Binding::Matrix &bone,
                                  const float *target, Studio44EyeState &out)
{
    Vector origin, up, forward, right;
    for(int r=0;r<3;++r)
    {
        origin[r]=bone.m[r][3]; up[r]=0; forward[r]=0;
        for(int c=0;c<3;++c) { origin[r]+=bone.m[r][c]*eye.origin[c]; up[r]+=bone.m[r][c]*eye.up[c]; forward[r]-=bone.m[r][c]*eye.forward[c]; }
    }
    if(target && std::isfinite(target[0]) && std::isfinite(target[1]) && std::isfinite(target[2]) &&
       (target[0]!=0 || target[1]!=0 || target[2]!=0)) forward=Vector(target[0],target[1],target[2])-origin;
    if(VectorNormalize(forward)<0.001f || VectorNormalize(up)<0.001f) return false;
    CrossProduct(forward,up,right); if(VectorNormalize(right)<0.001f) return false;
    forward+=right*(eye.zoffset*2); if(VectorNormalize(forward)<0.001f) return false;
    CrossProduct(forward,up,right); if(VectorNormalize(right)<0.001f) return false;
    CrossProduct(right,forward,up); if(VectorNormalize(up)<0.001f) return false;
    out.origin=origin; out.up=Vector(eye.up[0],eye.up[1],eye.up[2]);
    for(int j=0;j<3;++j) { out.u[j]=-eye.irisScale*right[j]; out.v[j]=-eye.irisScale*up[j]; }
    out.u[3]=0.5f-DotProduct(origin,Vector(out.u[0],out.u[1],out.u[2]));
    out.v[3]=0.5f-DotProduct(origin,Vector(out.v[0],out.v[1],out.v[2]));
    out.valid=true; return true;
}
static void Studio44SetEyeMaterial(IMaterial *material, const Studio44EyeState &eye)
{
    bool found; IMaterialVar *var=material->FindVar("$eyeorigin",&found,false); if(found) var->SetVecValue(eye.origin.Base(),3);
    var=material->FindVar("$eyeup",&found,false); if(found) var->SetVecValue(eye.up.Base(),3);
    var=material->FindVar("$irisu",&found,false); if(found) var->SetVecValue(eye.u,4);
    var=material->FindVar("$irisv",&found,false); if(found) var->SetVecValue(eye.v,4);
}

// The owner must be destroyed before StudioRender and the material system.
class Studio44MeshOwner
{
public:
    explicit Studio44MeshOwner( IStudioRender44 *renderer ) : m_renderer( renderer ) {}
    ~Studio44MeshOwner() { Clear(); }
    void Clear()
    {
        for ( size_t i = 0; i < m_handles.size(); ++i )
        {
            m_renderer->DestroyMesh( m_handles[i].handle );
            for ( size_t j = 0; j < m_handles[i].materials.size(); ++j )
                if ( m_handles[i].materials[j] ) m_handles[i].materials[j]->DecrementReferenceCount();
        }
        m_handles.clear();
    }
    bool Add( IMaterialSystem *materials, const std::vector<IMaterial *> &variants,
              const IStudio44MeshSource &source, size_t body, size_t model, size_t mesh, size_t lod, bool software )
    {
        if ( variants.empty() || !variants[0] ) return false;
        void *handle = software ? m_renderer->CreateSoftwareMesh( materials, variants[0], source ) :
                                  m_renderer->CreateMesh( materials, variants[0], source );
        if ( !handle ) return false;
        Entry entry; entry.handle = handle; entry.body = body; entry.model = model; entry.mesh = mesh; entry.lod = lod; entry.materials = variants; entry.software = software;
        for ( size_t i = 0; i < variants.size(); ++i )
            if ( variants[i] ) variants[i]->IncrementReferenceCount();
        m_handles.push_back( entry );
        return true;
    }
    size_t Count() const { return m_handles.size(); }
    size_t SoftwareCount() const
    { size_t count=0; for(size_t i=0;i<m_handles.size();++i) if(m_handles[i].software) ++count; return count; }
    bool HasSkin( unsigned int skin ) const
    {
        bool found = false;
        for ( size_t i = 0; i < m_handles.size(); ++i )
            if ( m_handles[i].model == 0 && m_handles[i].lod == 0 )
            {
                found = true;
                if ( !Material( m_handles[i], skin ) ) return false;
            }
        return found;
    }
    bool HasOpaque( unsigned int skin ) const { return HasPass( skin, false ); }
    bool HasTranslucent( unsigned int skin ) const { return HasPass( skin, true ); }
    bool Draw( const float *transform, unsigned int skin, float alpha, Mdl44DrawPass pass ) const
    {
        bool drawn = false;
        for ( size_t i = 0; i < m_handles.size(); ++i )
        {
            const Entry &entry = m_handles[i];
            IMaterial *material = Material( entry, skin );
            if ( entry.model == 0 && entry.lod == 0 && material &&
                 ( pass == MDL44_DRAW_ALL || ( pass == MDL44_DRAW_TRANSLUCENT ) == material->IsTranslucent() ) )
                drawn = m_renderer->DrawMesh( entry.handle, material, transform, alpha ) || drawn;
        }
        return drawn;
    }
    bool DrawSkinned( const float *transform, unsigned int skin, const float *bones, unsigned int boneCount,
                      const Mdl44Flex::Data *flexData = NULL, const std::vector<float> *flexWeights = NULL,
                      const std::vector<Studio44EyeState> *eyes = NULL ) const
    {
        bool drawn = false;
        for ( size_t i=0; i<m_handles.size(); ++i )
        {
            const Entry &entry=m_handles[i]; IMaterial *material=Material(entry,skin);
            if ( entry.model == 0 && entry.lod == 0 && material )
            {
                if (flexData && eyes) for(size_t j=0;j<flexData->eyeMeshes.size();++j)
                {
                    const Mdl44Flex::EyeMesh &binding=flexData->eyeMeshes[j];
                    if(binding.body==entry.body && binding.model==entry.model && binding.mesh==entry.mesh && binding.eye<eyes->size() && (*eyes)[binding.eye].valid)
                    { Studio44SetEyeMaterial(material,(*eyes)[binding.eye]); break; }
                }
                std::vector<Studio44VertexDelta> deltas;
                if ( flexData && flexWeights )
                    for ( size_t meshIndex=0; meshIndex<flexData->meshes.size(); ++meshIndex )
                    {
                        const Mdl44Flex::Mesh &mesh=flexData->meshes[meshIndex];
                        if (mesh.body!=entry.body || mesh.model!=entry.model || mesh.mesh!=entry.mesh) continue;
                        deltas.resize(mesh.vertexCount);
                        for(size_t f=0; f<mesh.flexes.size(); ++f)
                        {
                            const Mdl44Flex::Flex &flex=mesh.flexes[f];
                            const float left=Mdl44Flex::Ramp(flex,(*flexWeights)[flex.desc]);
                            const float right=flex.pair ? Mdl44Flex::Ramp(flex,(*flexWeights)[flex.pair]) : left;
                            for(size_t j=0;j<flex.deltas.size();++j)
                            {
                                const Mdl44Flex::Delta &d=flex.deltas[j];
                                const float side=d.side/255.0f, weight=left*(1-side)+right*side;
                                for(int axis=0;axis<3;++axis) { deltas[d.index].position[axis]+=weight*d.position[axis]; deltas[d.index].normal[axis]+=weight*d.normal[axis]; }
                            }
                        }
                        break;
                    }
                if ( !m_renderer->DrawSkinnedMesh(entry.handle,material,transform,bones,boneCount,1.0f,
                      deltas.empty() ? NULL : &deltas[0],static_cast<unsigned int>(deltas.size())) ) return false;
                drawn = true;
            }
        }
        return drawn;
    }
private:
    Studio44MeshOwner( const Studio44MeshOwner & );
    Studio44MeshOwner &operator=( const Studio44MeshOwner & );
    IStudioRender44 *m_renderer;
    struct Entry { void *handle; size_t body, model, mesh, lod; std::vector<IMaterial *> materials; bool software; };
    static IMaterial *Material( const Entry &entry, unsigned int skin )
    {
        if ( entry.materials.empty() ) return NULL;
        return entry.materials[skin < entry.materials.size() ? skin : 0];
    }
    bool HasPass( unsigned int skin, bool translucent ) const
    {
        for ( size_t i = 0; i < m_handles.size(); ++i )
        {
            const Entry &entry = m_handles[i];
            IMaterial *material = Material( entry, skin );
            if ( entry.model == 0 && entry.lod == 0 && material && material->IsTranslucent() == translucent ) return true;
        }
        return false;
    }
    std::vector<Entry> m_handles;
};

static IMaterial *FindStudio44Material( const Mdl44::StaticModel &model, size_t lod, uint16_t material )
{
    const char *name = model.MaterialName( lod, material );
    if ( !name ) return NULL;
    const StudioMaterialView view = model.Materials();
    for ( size_t i = 0; i < view.PathCount(); ++i )
    {
        const std::string fullName = std::string( view.Path( i ) ) + name;
        bool found = false;
        IMaterial *result = materialSystemInterface->FindMaterial( fullName.c_str(), &found, false );
        if ( found && result ) return result;
    }
    if ( !view.PathCount() )
    {
        bool found = false;
        IMaterial *result = materialSystemInterface->FindMaterial( name, &found, false );
        if ( found && result ) return result;
    }
    Warning( "mdl44_upload: material %s was not found for LOD %u\n", name, static_cast<unsigned>( lod ) );
    return NULL;
}

static bool UploadStaticModel44( const Mdl44::StaticModel &model, Studio44MeshOwner &handles, size_t &skipped, bool allowSoftware = false, const Mdl44Flex::Data *flexData = NULL )
{
    const Mdl44Static::Description &description = model.Description();
    for ( size_t b = 0; b < description.bodyParts.size(); ++b )
        for ( size_t m = 0; m < description.bodyParts[b].models.size(); ++m )
            for ( size_t k = 0; k < description.bodyParts[b].models[m].meshes.size(); ++k )
            {
                StudioVertexView::Mesh mesh( StudioVertexView::Mesh::Vvd4, NULL, NULL, 0 );
                if ( !model.Mesh( b, m, k, mesh ) ) return false;
                for ( size_t lod = 0; lod < model.LodCount(); ++lod )
                {
                    std::vector<IMaterial *> variants;
                    for ( size_t skin = 0; skin < description.skinFamilies; ++skin )
                    {
                        uint16_t materialIndex;
                        if ( !model.MeshMaterial( b, m, k, skin, materialIndex ) ) return false;
                        IMaterial *material = FindStudio44Material( model, lod, materialIndex );
                        if ( material && material->NeedsFrameBufferTexture() ) material = NULL;
                        variants.push_back( material );
                    }
                    if ( variants.empty() || !variants[0] ) return false;
                    for ( size_t g = 0; ; ++g )
                    {
                        Mdl44::GroupView group;
                        if ( !model.Group( b, m, lod, k, g, group ) ) break;
                        if ( !group.VertexCount() || !group.IndexCount() ) continue;
                        bool flexMesh=false;
                        if (flexData) for(size_t i=0;i<flexData->meshes.size();++i) if(flexData->meshes[i].body==b && flexData->meshes[i].model==m && flexData->meshes[i].mesh==k) { flexMesh=true; break; }
                        const bool software = !( group.Flags() & 2 ) || flexMesh;
                        if ( software && !allowSoftware ) { ++skipped; continue; }
                        Studio44MeshSource source( mesh, group );
                        if ( !handles.Add( materialSystemInterface, variants, source, b, m, k, lod, software ) ) return false;
                    }
                }
            }
    return handles.Count() != 0;
}

static Mdl44GroupSource *Mdl44GetGroupSource(Mdl44CachedModel *cached, size_t index)
{
    if(!cached || index>=cached->sources.size() || index>=cached->virtualModel.Groups().size()) return NULL;
    Mdl44GroupSource &source=cached->sources[index];
    if(!source.attempted)
    {
        source.attempted=true;
        const Mdl44Virtual::Group &group=cached->virtualModel.Groups()[index];
        Studio44::studiohdr_t header;
        if(ReadModelFile44(group.name,source.mdl) && Studio44::ReadHeader(source.mdl.data(),source.mdl.size(),sizeof(header),header) &&
           static_cast<uint32_t>(header.checksum)==group.skeleton.checksum)
        {
            source.valid=true;
            if(header.szanimblocknameindex>0)
            {
                std::string aniName;
                if(Mdl44Sequence::RelativeString(source.mdl.data(),static_cast<size_t>(header.length),0,header.szanimblocknameindex,aniName) && !aniName.empty())
                    ReadModelFile44(aniName,source.ani);
            }
        }
    }
    return source.valid ? &source : NULL;
}

// Local attachment records follow the 92-byte SE2007 mstudioattachment_t layout.
static bool Mdl44AttachmentRecord(Mdl44CachedModel *cached, int index, size_t &offset, Mdl44GroupSource *&source)
{
    if (!cached || index < 0 || index >= cached->header.numlocalattachments || cached->header.localattachmentindex < 0) return false;
    source=Mdl44GetGroupSource(cached,0); if (!source) return false;
    const size_t length=static_cast<size_t>(cached->header.length), start=static_cast<size_t>(cached->header.localattachmentindex);
    if (length>source->mdl.size() || start>length || static_cast<size_t>(cached->header.numlocalattachments)>(length-start)/92) return false;
    offset=start+static_cast<size_t>(index)*92;
    return true;
}
int Mdl44ModelInfo::FindAttachment(const model_t *model, const char *name) const
{
    Mdl44CachedModel *cached=FindCached44(model); if (!cached || !name) return -1;
    for(int i=0;i<cached->header.numlocalattachments;++i)
    {
        size_t at; Mdl44GroupSource *src;
        if (!Mdl44AttachmentRecord(cached,i,at,src)) return -1;
        std::string candidate;
        if (Mdl44Sequence::RelativeString(src->mdl.data(),static_cast<size_t>(cached->header.length),at,
                                         Mdl44Skeleton::ReadI32(src->mdl.data(),at),candidate) &&
            Mdl44Sequence::EqualName(candidate,name)) return i;
    }
    return -1;
}
bool Mdl44ModelInfo::AttachmentTransform(const model_t *model, int attachment, float time, const Studio44Layer *layers, unsigned int layerCount, float *output) const
{
    Mdl44CachedModel *cached=FindCached44(model); size_t at; Mdl44GroupSource *src;
    if (!output || !Mdl44AttachmentRecord(cached,attachment,at,src)) return false;
    const Mdl44Skeleton::Skeleton &skeleton=cached->virtualModel.Groups()[0].skeleton;
    int bone=Mdl44Skeleton::ReadI32(src->mdl.data(),at+8);
    if (bone<0 || static_cast<size_t>(bone)>=skeleton.bones.size()) return false;
    std::vector<Mdl44Animation::LocalPose> pose(skeleton.bones.size());
    for(size_t i=0;i<skeleton.bones.size();++i) {
        std::memcpy(pose[i].position,skeleton.bones[i].position,sizeof(pose[i].position));
        std::memcpy(pose[i].rotation,skeleton.bones[i].quaternion,sizeof(pose[i].rotation));
    }
    // Match the same base idle currently used by the native G-Man renderer.
    const Mdl44Virtual::Group &root=cached->virtualModel.Groups()[0];
    if (!_stricmp(cached->originalName.c_str(),"models/gman_high.mdl")) {
        const Mdl44Sequence::Sequence *seq=Mdl44Sequence::Find(root.sequences,"idle_subtle");
        if (seq && seq->animations.size()==1 && seq->animations[0]<root.animations.clips.size()) {
            const Mdl44Animation::Descriptor &clip=root.animations.clips[seq->animations[0]];
            Mdl44Animation::Data data; std::vector<Mdl44Animation::LocalPose> sampled;
            const int frame=std::isfinite(time) && time>0 ? static_cast<int>(std::fmod(double(time)*clip.fps,clip.frames)) : 0;
            if (Mdl44Animation::Resolve(src->mdl.data(),src->mdl.size(),src->ani.empty()?NULL:src->ani.data(),src->ani.size(),root.animations,clip,data) &&
                Mdl44Animation::SamplePose(data,clip,skeleton,frame,sampled)) pose.swap(sampled);
        }
    }
    if (layers && layerCount<=8) for(unsigned int i=0;i<layerCount;++i) {
        const Studio44Layer &layer=layers[i];
        if(layer.sequence<0 || static_cast<size_t>(layer.sequence)>=cached->virtualModel.Sequences().size()) continue;
        const Mdl44Virtual::Entry &entry=cached->virtualModel.Sequences()[layer.sequence];
        const Mdl44Virtual::Group &group=cached->virtualModel.Groups()[entry.group];
        Mdl44GroupSource *bytes=Mdl44GetGroupSource(cached,entry.group); if(!bytes) continue;
        Mdl44Pose::Source source={bytes->mdl.data(),bytes->ani.empty()?NULL:bytes->ani.data(),bytes->mdl.size(),bytes->ani.size(),
            &group.skeleton,&group.animations,&group.sequences,&group.masterBone};
        Mdl44Pose::Accumulate(source,entry.index,layer.cycle,layer.weight,pose);
    }
    std::vector<int> chain;
    for (int b=bone; b>=0; b=skeleton.bones[b].parent) {
        if (static_cast<size_t>(b)>=skeleton.bones.size() || chain.size()>=skeleton.bones.size()) return false;
        chain.push_back(b);
    }
    Mdl44Binding::Matrix world={}; world.m[0][0]=world.m[1][1]=world.m[2][2]=1;
    while(!chain.empty()) {
        const int current=chain.back(); chain.pop_back();
        Mdl44Binding::Matrix local; if (!Mdl44Binding::LocalMatrix(pose[current],local)) return false;
        world=Mdl44Binding::Multiply(world,local);
    }
    Mdl44Binding::Matrix local; std::memcpy(local.m,src->mdl.data()+at+12,sizeof(local.m));
    for(int r=0;r<3;++r) for(int c=0;c<4;++c) if(!std::isfinite(local.m[r][c])) return false;
    world=Mdl44Binding::Multiply(world,local); std::memcpy(output,world.m,sizeof(world.m)); return true;
}

static bool Mdl44RigidEntityDraw(const char *name, const float *transform, unsigned int skin);
static bool Mdl44RigidEntityBounds(const char *name, float *mins, float *maxs);
static void Mdl44RigidEntityReleaseGpu();
static void Mdl44RigidEntityShutdown();

// A deliberately isolated preview while sequence selection and VCD playback
// are being implemented. It does not change the legacy model loader.
static ConVar r_mdl44_gman_gesture("r_mdl44_gman_gesture", "", 0, "Preview a G-Man gesture by name (not VCD playback).");
static ConVar r_mdl44_gman_gesture_cycle("r_mdl44_gman_gesture_cycle", "-1", 0, "G-Man gesture cycle 0..1; -1 plays a loop.");
class Mdl44EntityPreview : public IMdl44EntityPreview
{
public:
    Mdl44EntityPreview() : m_gpu(NULL), m_attempted(false), m_valid(false), m_logged(false), m_gestureAttempted(false), m_gestureValid(false) {}
    void ReleaseGpu() { delete m_gpu; m_gpu=NULL; }
    void Shutdown()
    {
        ReleaseGpu(); m_model=Mdl44::StudioModel(); m_mdl.clear(); m_ani.clear();
        m_skeleton=Mdl44Skeleton::Skeleton(); m_anims=Mdl44Animation::Animations(); m_sequences=Mdl44Sequence::Data();
        m_gestureMdl.clear(); m_gestureAni.clear(); m_gestureMap.clear(); m_gestureName.clear(); m_loggedSceneLayers.clear();
        m_gestureSkeleton=Mdl44Skeleton::Skeleton(); m_gestureAnims=Mdl44Animation::Animations(); m_gestureSequences=Mdl44Sequence::Data();
        m_attempted=false; m_valid=false; m_logged=false; m_gestureAttempted=false; m_gestureValid=false;
    }
    virtual bool Draw(const model_t *identity, const char *name, const float *transform, unsigned int skin, float time,
                      const Studio44Layer *layers, unsigned int layerCount, const float *flexWeights, unsigned int flexCount, const float *viewTarget)
    {
        if (!name || !transform) return false;
        if (_stricmp(name,"models/gman_high.mdl")) return Mdl44RigidEntityDraw(name,transform,skin);
        if (!m_attempted)
        {
            m_attempted=true;
            if (!ReadModelFile44(name,m_mdl) || !Mdl44Skeleton::Parse(m_mdl.data(),m_mdl.size(),m_skeleton) ||
                !Mdl44Animation::Parse(m_mdl.data(),m_mdl.size(),m_anims) || m_anims.clips.empty() ||
                !Mdl44Sequence::Parse(m_mdl.data(),m_mdl.size(),m_sequences) || m_sequences.sequences.empty() ||
                !LoadModelGeometry44(name,m_model,false))
            { Warning("MDL 44 G-Man preview: unable to load model or animation data\n"); return false; }
            ReadModelFile44("models/gman_high.ani",m_ani); m_valid=true;
        }
        if (!m_valid) return false;
        // The idle sequence references a local animation; sequence indices are
        // not animation indices, especially for included models.
        const Mdl44Sequence::Sequence *sequence=Mdl44Sequence::Find(m_sequences,"idle_subtle");
        if (!sequence || sequence->animations.size()!=1 || sequence->animations[0]>=m_anims.clips.size()) return false;
        const Mdl44Animation::Descriptor &clip=m_anims.clips[sequence->animations[0]];
        Mdl44Animation::Data data;
        std::vector<Mdl44Animation::LocalPose> pose;
        std::vector<Mdl44Binding::Matrix> matrices, animatedBones;
        const int frame=std::isfinite(time) && time>0 ? static_cast<int>(std::fmod(double(time)*clip.fps,clip.frames)) : 0;
        if (!Mdl44Animation::Resolve(m_mdl.data(),m_mdl.size(),m_ani.empty()?NULL:m_ani.data(),m_ani.size(),m_anims,clip,data) ||
            !Mdl44Animation::SamplePose(data,clip,m_skeleton,frame,pose)) return false;
        const char *gesture=r_mdl44_gman_gesture.GetString();
        if (gesture && *gesture)
        {
            if (!m_gestureAttempted)
            {
                m_gestureAttempted=true;
                m_gestureValid=ReadModelFile44("models/Gman_gestures.mdl",m_gestureMdl) &&
                    ReadModelFile44("models/Gman_gestures.ani",m_gestureAni) &&
                    Mdl44Skeleton::Parse(m_gestureMdl.data(),m_gestureMdl.size(),m_gestureSkeleton) &&
                    Mdl44Animation::Parse(m_gestureMdl.data(),m_gestureMdl.size(),m_gestureAnims) &&
                    Mdl44Sequence::Parse(m_gestureMdl.data(),m_gestureMdl.size(),m_gestureSequences) &&
                    Mdl44Sequence::MapBones(m_skeleton,m_gestureSkeleton,m_gestureMap);
            }
            const Mdl44Sequence::Sequence *g=m_gestureValid ? Mdl44Sequence::Find(m_gestureSequences,gesture) : NULL;
            bool ok=false;
            if (g)
            {
                float cycle=r_mdl44_gman_gesture_cycle.GetFloat();
                if(cycle<0 && g->animations.size()==1 && g->animations[0]<m_gestureAnims.clips.size())
                {
                    const Mdl44Animation::Descriptor &gestureClip=m_gestureAnims.clips[g->animations[0]];
                    const float duration=gestureClip.fps>0 ? (gestureClip.frames-1)/gestureClip.fps : 0;
                    cycle=duration>0 && std::isfinite(time) && time>0 ? static_cast<float>(std::fmod(time/duration,1.0)) : 0;
                }
                Mdl44Pose::Source source={m_gestureMdl.data(),m_gestureAni.data(),m_gestureMdl.size(),m_gestureAni.size(),
                    &m_gestureSkeleton,&m_gestureAnims,&m_gestureSequences,&m_gestureMap};
                ok=Mdl44Pose::Accumulate(source,static_cast<size_t>(g-&m_gestureSequences.sequences[0]),cycle,1.0f,pose);
            }
            if (m_gestureName!=gesture)
            {
                if (ok) Msg("MDL 44 G-Man gesture preview: %s\n",gesture);
                else Warning("MDL 44 G-Man gesture preview: cannot evaluate %s\n",gesture);
                m_gestureName=gesture;
            }
        }
        else m_gestureName.clear();
        // Networked overlays use the same virtual sequence table as the server.
        Mdl44CachedModel *cached=FindCached44(identity);
        if(cached && layers && layerCount<=8)
        {
            for(unsigned int i=0;i<layerCount;++i)
            {
                const Studio44Layer &layer=layers[i];
                if(layer.sequence<0 || static_cast<size_t>(layer.sequence)>=cached->virtualModel.Sequences().size()) continue;
                const Mdl44Virtual::Entry &entry=cached->virtualModel.Sequences()[layer.sequence];
                const Mdl44Virtual::Group &group=cached->virtualModel.Groups()[entry.group];
                Mdl44GroupSource *bytes=Mdl44GetGroupSource(cached,entry.group);
                if(!bytes) continue;
                Mdl44Pose::Source source={bytes->mdl.data(),bytes->ani.empty()?NULL:bytes->ani.data(),bytes->mdl.size(),bytes->ani.size(),
                    &group.skeleton,&group.animations,&group.sequences,&group.masterBone};
                if(Mdl44Pose::Accumulate(source,entry.index,layer.cycle,layer.weight,pose))
                {
                    bool logged=false; for(size_t j=0;j<m_loggedSceneLayers.size();++j) if(m_loggedSceneLayers[j]==layer.sequence) logged=true;
                    if(!logged) { Msg("MDL 44 scene layer: %s\n",group.sequences.sequences[entry.index].label.c_str()); m_loggedSceneLayers.push_back(layer.sequence); }
                }
            }
        }
        if (!Mdl44Binding::BuildSkinMatrices(m_skeleton,pose,matrices,&animatedBones) || matrices.empty()) return false;
        if (!m_gpu)
        {
            CreateInterfaceFn factory=Sys_GetFactory("studiorender.dll");
            IStudioRender44 *renderer=factory ? static_cast<IStudioRender44 *>(factory(STUDIO_RENDER_44_INTERFACE_VERSION,NULL)) : NULL;
            if (!renderer || !materialSystemInterface) return false;
            Studio44MeshOwner *gpu=new Studio44MeshOwner(renderer); size_t skipped=0;
            if (!UploadStaticModel44(m_model,*gpu,skipped,true,cached && cached->flexValid ? &cached->flex : NULL) || skipped) { delete gpu; return false; }
            m_gpu=gpu;
        }
        // The native preview bypasses CModelRender::StudioSetupLighting, so
        // set the same ambient cube and local world lights before drawing.
        if (cached && g_pStudioRender && materialSystemInterface)
        {
            Vector position;
            for(int r=0;r<3;++r)
                position[r]=transform[r*4+3] + transform[r*4]*cached->header.illumposition[0] +
                    transform[r*4+1]*cached->header.illumposition[1] + transform[r*4+2]*cached->header.illumposition[2];
            LightingState_t lighting;
            ITexture *cubemap=LightcacheGet(position,lighting);
            if (cubemap) materialSystemInterface->BindLocalCubemap(cubemap);
            if (mat_fullbright.GetInt()==1)
            {
                Vector white[6]; for(int i=0;i<6;++i) white[i].Init(1,1,1);
                g_pStudioRender->SetAmbientLightColors(white,white);
                g_pStudioRender->SetLocalLights(0,NULL);
            }
            else
            {
                g_pStudioRender->SetAmbientLightColors(lighting.r_boxcolor,lighting.totalboxcolor);
                LightDesc_t lights[MAXLOCALLIGHTS]; int count=0;
                for(int i=0;i<lighting.numlights && count<MAXLOCALLIGHTS;++i)
                    if(WorldLightToMaterialLight(lighting.locallight[i],lights[count]))
                    {
                        const int style=lighting.locallight[i]->style;
                        const float scale=(style>=0 && style<256) ? g_lightmapScale * d_lightstylevalue[style] / 264.0f : 1.0f;
                        for(int j=0;j<3;++j) lights[count].m_Color[j]*=scale;
                        ++count;
                    }
                g_pStudioRender->SetLocalLights(count,lights);
            }
        }
        std::vector<float> flexResult;
        const bool haveFlex=cached && cached->flexValid && flexWeights &&
            Mdl44Flex::Evaluate(cached->flex,flexWeights,flexCount,flexResult);
        std::vector<Studio44EyeState> eyeStates;
        if(cached && cached->flexValid)
        {
            eyeStates.resize(cached->flex.eyes.size());
            Mdl44Binding::Matrix modelToWorld; std::memcpy(modelToWorld.m,transform,sizeof(modelToWorld.m));
            // SE2007 applies the animated eye controls and clamps the target
            // in the space of the model's eyes attachment before projecting.
            Vector adjustedTarget;
            const float *eyeTarget=viewTarget;
            if(viewTarget && (viewTarget[0]!=0 || viewTarget[1]!=0 || viewTarget[2]!=0))
            {
                const int attachment=s_Mdl44ModelInfo.FindAttachment(identity,"eyes"); size_t at; Mdl44GroupSource *source;
                if(Mdl44AttachmentRecord(cached,attachment,at,source))
                {
                    const int bone=Mdl44Skeleton::ReadI32(source->mdl.data(),at+8);
                    if(bone>=0 && static_cast<size_t>(bone)<animatedBones.size())
                    {
                        Mdl44Binding::Matrix attachmentLocal; std::memcpy(attachmentLocal.m,source->mdl.data()+at+12,sizeof(attachmentLocal.m));
                        const Mdl44Binding::Matrix world=Mdl44Binding::Multiply(Mdl44Binding::Multiply(modelToWorld,animatedBones[bone]),attachmentLocal);
                        matrix3x4_t attachmentWorld; std::memcpy(&attachmentWorld[0][0],world.m,sizeof(world.m));
                        Vector local; VectorITransform(Vector(viewTarget[0],viewTarget[1],viewTarget[2]),attachmentWorld,local);
                        if(local.x<6) local.x=6;
                        const float distance=VectorNormalize(local);
                        QAngle eyeAngles(0,0,0);
                        if(flexWeights) for(size_t i=0;i<cached->flex.controllers.size() && i<flexCount;++i)
                        {
                            if(!_stricmp(cached->flex.controllers[i].name.c_str(),"eyes_updown")) eyeAngles.x=flexWeights[i];
                            if(!_stricmp(cached->flex.controllers[i].name.c_str(),"eyes_rightleft")) eyeAngles.y=flexWeights[i];
                        }
                        Vector deflection; AngleVectors(eyeAngles,&deflection); deflection.x=0;
                        local+=deflection*(local.x*local.x); VectorNormalize(local);
                        const float maxDeflection=0.866f;
                        if(local.x<maxDeflection)
                        {
                            const float side=local.y*local.y+local.z*local.z;
                            if(side>0) { const float scale=std::sqrt((1-maxDeflection*maxDeflection)/side); local.x=maxDeflection; local.y*=scale; local.z*=scale; }
                            else local.Init(1,0,0);
                        }
                        local*=distance; VectorTransform(local,attachmentWorld,adjustedTarget); eyeTarget=adjustedTarget.Base();
                    }
                }
            }
            for(size_t i=0;i<eyeStates.size();++i)
            {
                const Mdl44Flex::Eye &eye=cached->flex.eyes[i];
                eyeStates[i].valid=false;
                if(eye.bone>=0 && static_cast<size_t>(eye.bone)<animatedBones.size())
                    Studio44EyeProjection(eye,Mdl44Binding::Multiply(modelToWorld,animatedBones[eye.bone]),eyeTarget,eyeStates[i]);
            }
        }
        if (r_mdl44_eye_debug.GetBool() && !eyeStates.empty())
        {
            static float nextEyeLog=0;
            if(time>=nextEyeLog)
            {
                Msg("MDL 44 eyes: %u eyes, first valid %d, target %.1f %.1f %.1f\n",static_cast<unsigned>(eyeStates.size()),eyeStates[0].valid ? 1 : 0,
                    viewTarget ? viewTarget[0] : 0,viewTarget ? viewTarget[1] : 0,viewTarget ? viewTarget[2] : 0);
                nextEyeLog=time+2;
            }
        }
        if (!m_gpu->DrawSkinned(transform,skin,&matrices[0].m[0][0],static_cast<unsigned>(matrices.size()),
             cached && cached->flexValid ? &cached->flex : NULL, haveFlex ? &flexResult : NULL, &eyeStates)) return false;
        if (!m_logged)
        {
            Msg("MDL 44 G-Man preview: sequence '%s', clip '%s' (%u hardware, %u software groups)\n",sequence->label.c_str(),clip.name.c_str(),
                static_cast<unsigned>(m_gpu->Count()-m_gpu->SoftwareCount()),static_cast<unsigned>(m_gpu->SoftwareCount()));
            m_logged=true;
        }
        return true;
    }
    virtual bool RigidBounds(const char *name, float *mins, float *maxs)
    { return Mdl44RigidEntityBounds(name,mins,maxs); }
private:
    Studio44MeshOwner *m_gpu;
    bool m_attempted, m_valid, m_logged;
    std::vector<unsigned char> m_mdl, m_ani;
    Mdl44::StudioModel m_model;
    Mdl44Skeleton::Skeleton m_skeleton;
    Mdl44Animation::Animations m_anims;
    Mdl44Sequence::Data m_sequences;
    bool m_gestureAttempted, m_gestureValid;
    std::vector<unsigned char> m_gestureMdl, m_gestureAni;
    Mdl44Skeleton::Skeleton m_gestureSkeleton;
    Mdl44Animation::Animations m_gestureAnims;
    Mdl44Sequence::Data m_gestureSequences;
    std::vector<int> m_gestureMap;
    std::string m_gestureName;
    std::vector<int> m_loggedSceneLayers;
};
static Mdl44EntityPreview s_Mdl44EntityPreview;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(Mdl44EntityPreview, IMdl44EntityPreview, MDL44_ENTITY_PREVIEW_VERSION, s_Mdl44EntityPreview);
void Mdl44EntityPreviewReleaseGpu() { Mdl44RigidEntityReleaseGpu(); s_Mdl44EntityPreview.ReleaseGpu(); }
void Mdl44EntityPreviewShutdown() { Mdl44RigidEntityShutdown(); s_Mdl44EntityPreview.Shutdown(); }

struct Mdl44MapCache
{
    Mdl44::StaticModel model;
    Studio44MeshOwner *gpu;
    std::string name;
    bool loggedOpaque, loggedTranslucent, loggedVariant;
    Mdl44MapCache() : gpu( NULL ), loggedOpaque( false ), loggedTranslucent( false ), loggedVariant( false ) {}
};

Mdl44MapCache *Mdl44MapCacheCreate( const char *name )
{
    if ( !name ) return NULL;
    Mdl44MapCache *cache = new Mdl44MapCache;
    if ( !cache ) return NULL;
    if ( !LoadStaticModel44( name, cache->model ) ) { delete cache; return NULL; }
    cache->name = name;
    return cache;
}

bool Mdl44MapCacheCreateGpu( Mdl44MapCache *cache )
{
    if ( !cache ) return false;
    if ( cache->gpu ) return true;
    CreateInterfaceFn factory = Sys_GetFactory( "studiorender.dll" );
    IStudioRender44 *renderer = factory ? static_cast<IStudioRender44 *>( factory( STUDIO_RENDER_44_INTERFACE_VERSION, NULL ) ) : NULL;
    if ( !renderer || !materialSystemInterface ) return false;
    Studio44MeshOwner *gpu = new Studio44MeshOwner( renderer );
    if ( !gpu ) return false;
    size_t skipped = 0;
    if ( !UploadStaticModel44( cache->model, *gpu, skipped ) || skipped ) { delete gpu; return false; }
    cache->gpu = gpu;
    return true;
}

bool Mdl44MapCacheReleaseGpu( Mdl44MapCache *cache )
{
    if ( !cache || !cache->gpu ) return false;
    delete cache->gpu;
    cache->gpu = NULL;
    return true;
}

void Mdl44MapCacheDestroy( Mdl44MapCache *cache )
{
    Mdl44MapCacheReleaseGpu( cache );
    delete cache;
}

bool Mdl44MapCacheBounds( const Mdl44MapCache *cache, float *mins, float *maxs, float *illumination )
{
    if ( !cache || !mins || !maxs || !illumination ) return false;
    const Mdl44Static::Description &d = cache->model.Description();
    const bool hasViewBounds = d.viewMin[0] != 0 || d.viewMin[1] != 0 || d.viewMin[2] != 0 ||
                               d.viewMax[0] != 0 || d.viewMax[1] != 0 || d.viewMax[2] != 0;
    std::memcpy( mins, hasViewBounds ? d.viewMin : d.hullMin, 12 );
    std::memcpy( maxs, hasViewBounds ? d.viewMax : d.hullMax, 12 );
    std::memcpy( illumination, d.illumination, 12 );
    return true;
}

bool Mdl44MapCacheDraw( Mdl44MapCache *cache, const float *transform, unsigned int skin, float alpha, Mdl44DrawPass pass )
{
    if ( !cache || !cache->gpu || !transform || !cache->gpu->HasSkin( skin ) || !cache->gpu->Draw( transform, skin, alpha, pass ) ) return false;
    if ( pass != MDL44_DRAW_TRANSLUCENT && cache->gpu->HasOpaque( skin ) && !cache->loggedOpaque )
    { Msg( "MDL 44 static prop opaque pass: %s\n", cache->name.c_str() ); cache->loggedOpaque = true; }
    if ( pass != MDL44_DRAW_OPAQUE && cache->gpu->HasTranslucent( skin ) && !cache->loggedTranslucent )
    { Msg( "MDL 44 static prop translucent pass: %s\n", cache->name.c_str() ); cache->loggedTranslucent = true; }
    if ( skin && !cache->loggedVariant )
    { Msg( "MDL 44 static prop skin %u: %s\n", skin, cache->name.c_str() ); cache->loggedVariant = true; }
    return true;
}

bool Mdl44MapCacheCanDraw( const Mdl44MapCache *cache, unsigned int skin )
{
    return cache && cache->gpu && cache->gpu->HasSkin( skin );
}

bool Mdl44MapCacheHasOpaque( const Mdl44MapCache *cache, unsigned int skin )
{ return cache && cache->gpu && cache->gpu->HasOpaque( skin ); }

bool Mdl44MapCacheHasTranslucent( const Mdl44MapCache *cache, unsigned int skin )
{ return cache && cache->gpu && cache->gpu->HasTranslucent( skin ); }

// Rigid v44 models are safe to draw with the existing static-prop renderer.
// Cache failed lookups too, since ordinary animated entities are not supported here.
struct Mdl44RigidEntry { std::string name; Mdl44MapCache *cache; bool logged; };
static std::vector<Mdl44RigidEntry> s_Mdl44RigidEntities;
static Mdl44RigidEntry *GetRigidEntity44(const char *name)
{
    if (!name || !*name) return NULL;
    for(size_t i=0;i<s_Mdl44RigidEntities.size();++i)
        if(Mdl44Sequence::EqualName(s_Mdl44RigidEntities[i].name,name)) return &s_Mdl44RigidEntities[i];
    Mdl44RigidEntry entry; entry.name=name; entry.logged=false; entry.cache=Mdl44MapCacheCreate(name);
    if (entry.cache && entry.cache->model.Description().boneCount!=1)
    { Mdl44MapCacheDestroy(entry.cache); entry.cache=NULL; }
    s_Mdl44RigidEntities.push_back(entry);
    return &s_Mdl44RigidEntities.back();
}
static bool Mdl44RigidEntityBounds(const char *name,float *mins,float *maxs)
{
    Mdl44RigidEntry *entry=GetRigidEntity44(name); float illumination[3];
    return entry && entry->cache && Mdl44MapCacheBounds(entry->cache,mins,maxs,illumination);
}
static bool Mdl44RigidEntityDraw(const char *name,const float *transform,unsigned int skin)
{
    Mdl44RigidEntry *entry=GetRigidEntity44(name);
    if(!entry || !entry->cache || !Mdl44MapCacheCreateGpu(entry->cache) ||
       !Mdl44MapCacheDraw(entry->cache,transform,skin,1.0f,MDL44_DRAW_ALL)) return false;
    if(!entry->logged) { Msg("MDL 44 rigid entity: %s\n",name); entry->logged=true; }
    return true;
}
static void Mdl44RigidEntityReleaseGpu()
{
    for(size_t i=0;i<s_Mdl44RigidEntities.size();++i) Mdl44MapCacheReleaseGpu(s_Mdl44RigidEntities[i].cache);
}
static void Mdl44RigidEntityShutdown()
{
    for(size_t i=0;i<s_Mdl44RigidEntities.size();++i) Mdl44MapCacheDestroy(s_Mdl44RigidEntities[i].cache);
    s_Mdl44RigidEntities.clear();
}

CON_COMMAND( mdl44_skeleton_check, "Validate an MDL 44 skeleton." )
{
    if ( Cmd_Argc() != 2 ) { Msg( "Usage: mdl44_skeleton_check models/path/model.mdl\n" ); return; }
    std::vector<unsigned char> data;
    Mdl44Skeleton::Skeleton skeleton;
    if ( !ReadModelFile44( Cmd_Argv( 1 ), data ) ||
         !Mdl44Skeleton::Parse( data.data(), data.size(), skeleton ) )
    { Warning( "mdl44_skeleton_check: could not parse %s\n", Cmd_Argv( 1 ) ); return; }
    size_t roots = 0;
    for ( size_t i = 0; i < skeleton.bones.size(); ++i )
        if ( skeleton.bones[i].parent == -1 ) ++roots;
    Msg( "mdl44_skeleton_check: %s: %u bones, %u roots, checksum %08x\n", Cmd_Argv( 1 ),
         static_cast<unsigned>( skeleton.bones.size() ), static_cast<unsigned>( roots ), skeleton.checksum );
}

CON_COMMAND( mdl44_binding_check, "Validate MDL 44 skeleton and VVD vertex influences." )
{
    if ( Cmd_Argc() != 2 ) { Msg( "Usage: mdl44_binding_check models/path/model.mdl\n" ); return; }
    const std::string name = Cmd_Argv(1);
    if ( name.size() < 5 || _stricmp( name.c_str() + name.size() - 4, ".mdl" ) )
    { Warning( "mdl44_binding_check: expected a .mdl file\n" ); return; }
    std::vector<unsigned char> mdl, vvd;
    Mdl44Skeleton::Skeleton skeleton; Vvd4Native::Vertices vertices;
    std::vector<Mdl44Binding::Vertex> bound;
    if ( !ReadModelFile44(name, mdl) || !Mdl44Skeleton::Parse(mdl.data(), mdl.size(), skeleton) ||
         !ReadModelFile44(name.substr(0, name.size()-4) + ".vvd", vvd) ||
         !Vvd4Native::Load(vvd.data(), vvd.size(), skeleton.checksum, 0, vertices) ||
         !Mdl44Binding::Bind(skeleton, vertices, bound) )
    { Warning( "mdl44_binding_check: invalid model, vertex data or bone influences: %s\n", name.c_str() ); return; }
    Msg( "mdl44_binding_check: %s: %u vertices linked to %u bones\n", name.c_str(),
         static_cast<unsigned>(bound.size()), static_cast<unsigned>(skeleton.bones.size()) );
}

CON_COMMAND( mdl44_animation_check, "Inspect MDL 44 animation descriptors and blocks." )
{
    if ( Cmd_Argc() != 2 ) { Msg( "Usage: mdl44_animation_check models/path/model.mdl\n" ); return; }
    const std::string name = Cmd_Argv(1);
    if ( name.size() < 5 || _stricmp( name.c_str() + name.size() - 4, ".mdl" ) )
    { Warning( "mdl44_animation_check: expected a .mdl file\n" ); return; }
    std::vector<unsigned char> mdl, ani; Mdl44Animation::Animations animations;
    if ( !ReadModelFile44(name, mdl) || !Mdl44Animation::Parse(mdl.data(), mdl.size(), animations) )
    { Warning( "mdl44_animation_check: invalid animation descriptors: %s\n", name.c_str() ); return; }
    ReadModelFile44(name.substr(0, name.size()-4) + ".ani", ani);
    size_t resolved = 0, external = 0, sectioned = 0;
    for ( size_t i = 0; i < animations.clips.size(); ++i )
    {
        const Mdl44Animation::Descriptor &clip = animations.clips[i]; Mdl44Animation::Data data;
        if ( clip.block > 0 ) ++external;
        if ( clip.sectionFrames ) ++sectioned;
        if ( Mdl44Animation::Resolve(mdl.data(), mdl.size(), ani.empty() ? NULL : ani.data(), ani.size(), animations, clip, data) ) ++resolved;
    }
    Msg( "mdl44_animation_check: %s: %u local clips, %u external-block clips, %u sectioned, %u data blocks resolved\n",
         name.c_str(), static_cast<unsigned>(animations.clips.size()), static_cast<unsigned>(external),
         static_cast<unsigned>(sectioned), static_cast<unsigned>(resolved) );
}

CON_COMMAND( mdl44_pose_check, "Sample a non-delta MDL 44 animation and deform vertices on the CPU." )
{
    if ( Cmd_Argc() != 4 ) { Msg( "Usage: mdl44_pose_check models/path/model.mdl <clip index> <frame>\n" ); return; }
    const std::string name=Cmd_Argv(1);
    if (name.size()<5 || _stricmp(name.c_str()+name.size()-4,".mdl"))
    { Warning("mdl44_pose_check: expected a .mdl file\n"); return; }
    const int clipIndex=atoi(Cmd_Argv(2)), frame=atoi(Cmd_Argv(3));
    std::vector<unsigned char> mdl,vvd,ani; Mdl44Skeleton::Skeleton skeleton; Vvd4Native::Vertices vertices;
    Mdl44Animation::Animations anims; std::vector<Mdl44Binding::Vertex> binding;
    if (!ReadModelFile44(name,mdl) || !Mdl44Skeleton::Parse(mdl.data(),mdl.size(),skeleton) ||
        !Mdl44Animation::Parse(mdl.data(),mdl.size(),anims) ||
        !ReadModelFile44(name.substr(0,name.size()-4)+".vvd",vvd) ||
        !Vvd4Native::Load(vvd.data(),vvd.size(),skeleton.checksum,0,vertices) ||
        !Mdl44Binding::Bind(skeleton,vertices,binding) || clipIndex<0 || static_cast<size_t>(clipIndex)>=anims.clips.size() || frame<0)
    { Warning("mdl44_pose_check: invalid model, vertex data or clip index: %s\n",name.c_str()); return; }
    ReadModelFile44(name.substr(0,name.size()-4)+".ani",ani);
    const Mdl44Animation::Descriptor &clip=anims.clips[clipIndex]; Mdl44Animation::Data data;
    std::vector<Mdl44Animation::LocalPose> pose; std::vector<Mdl44Binding::Matrix> matrices;
    if (!Mdl44Animation::Resolve(mdl.data(),mdl.size(),ani.empty()?NULL:ani.data(),ani.size(),anims,clip,data) ||
        !Mdl44Animation::SamplePose(data,clip,skeleton,frame,pose) || !Mdl44Binding::BuildSkinMatrices(skeleton,pose,matrices))
    { Warning("mdl44_pose_check: cannot sample clip '%s' frame %d (delta, sectioned or invalid data)\n",clip.name.c_str(),frame); return; }
    float maxDisplacement=0;
    for (size_t i=0;i<binding.size();++i)
    {
        StudioVertexView::Vertex source; float result[3];
        if (!StudioVertexView::ReadVvd4(vertices.vertices[i].data,48,source) ||
            !Mdl44Binding::DeformPosition(source,binding[i],matrices,result))
        { Warning("mdl44_pose_check: could not deform vertex %u\n",static_cast<unsigned>(i)); return; }
        const float dx=result[0]-source.position[0],dy=result[1]-source.position[1],dz=result[2]-source.position[2];
        const float distance=std::sqrt(dx*dx+dy*dy+dz*dz);
        if (distance>maxDisplacement) maxDisplacement=distance;
    }
    Msg("mdl44_pose_check: %s clip '%s' frame %d/%d: %u bones, %u vertices, max bind-pose displacement %.3f\n",
        name.c_str(),clip.name.c_str(),frame,clip.frames,static_cast<unsigned>(pose.size()),
        static_cast<unsigned>(binding.size()),maxDisplacement);
}

// GPU diagnostic: invoke during an active game frame. It draws once using the
// current camera; this is not entity integration or an animation player.
CON_COMMAND( mdl44_skin_draw_test, "Upload and draw one MDL 44 skinned pose for validation." )
{
    if ( Cmd_Argc() != 4 ) { Msg( "Usage: mdl44_skin_draw_test models/path/model.mdl <clip index> <frame>\n" ); return; }
    const std::string name=Cmd_Argv(1); const int clipIndex=atoi(Cmd_Argv(2)), frame=atoi(Cmd_Argv(3));
    if (name.size()<5 || _stricmp(name.c_str()+name.size()-4,".mdl") || clipIndex<0 || frame<0)
    { Warning("mdl44_skin_draw_test: invalid arguments\n"); return; }
    std::vector<unsigned char> mdl,ani; Mdl44Skeleton::Skeleton skeleton; Mdl44Animation::Animations anims; Mdl44::StudioModel model;
    if (!ReadModelFile44(name,mdl) || !Mdl44Skeleton::Parse(mdl.data(),mdl.size(),skeleton) ||
        !Mdl44Animation::Parse(mdl.data(),mdl.size(),anims) || static_cast<size_t>(clipIndex)>=anims.clips.size() ||
        !LoadModelGeometry44(name,model,false))
    { Warning("mdl44_skin_draw_test: invalid model or clip index: %s\n",name.c_str()); return; }
    ReadModelFile44(name.substr(0,name.size()-4)+".ani",ani);
    const Mdl44Animation::Descriptor &clip=anims.clips[clipIndex]; Mdl44Animation::Data data;
    std::vector<Mdl44Animation::LocalPose> pose; std::vector<Mdl44Binding::Matrix> matrices;
    if (!Mdl44Animation::Resolve(mdl.data(),mdl.size(),ani.empty()?NULL:ani.data(),ani.size(),anims,clip,data) ||
        !Mdl44Animation::SamplePose(data,clip,skeleton,frame,pose) || !Mdl44Binding::BuildSkinMatrices(skeleton,pose,matrices))
    { Warning("mdl44_skin_draw_test: cannot sample clip '%s' frame %d\n",clip.name.c_str(),frame); return; }
    CreateInterfaceFn factory=Sys_GetFactory("studiorender.dll");
    IStudioRender44 *renderer=factory ? static_cast<IStudioRender44 *>(factory(STUDIO_RENDER_44_INTERFACE_VERSION,NULL)) : NULL;
    if (!renderer || !materialSystemInterface) { Warning("mdl44_skin_draw_test: StudioRender 44 unavailable\n"); return; }
    Studio44MeshOwner gpu(renderer); size_t skipped=0;
    if (!UploadStaticModel44(model,gpu,skipped,true)) { Warning("mdl44_skin_draw_test: upload failed\n"); return; }
    const float transform[12]={1,0,0,0, 0,1,0,0, 0,0,1,0};
    if (!gpu.DrawSkinned(transform,0,&matrices[0].m[0][0],static_cast<unsigned>(matrices.size())))
    { Warning("mdl44_skin_draw_test: GPU draw failed\n"); return; }
    Msg("mdl44_skin_draw_test: %s clip '%s' frame %d: draw completed, %u hardware groups and %u software groups uploaded; %u skipped\n",
        name.c_str(),clip.name.c_str(),frame,static_cast<unsigned>(gpu.Count()-gpu.SoftwareCount()),
        static_cast<unsigned>(gpu.SoftwareCount()),static_cast<unsigned>(skipped));
}

CON_COMMAND( mdl44_check, "Validate and inspect a static MDL 44 model and its VVD/VTX files." )
{
    if ( Cmd_Argc() != 2 )
    {
        Msg( "Usage: mdl44_check models/path/model.mdl\n" );
        return;
    }
    const std::string name( Cmd_Argv( 1 ) );
    if ( name.size() < 5 || _stricmp( name.c_str() + name.size() - 4, ".mdl" ) )
    {
        Warning( "mdl44_check: expected a .mdl path\n" );
        return;
    }
    Mdl44::StaticModel model;
    if ( !LoadStaticModel44( name, model ) )
    {
        Warning( "mdl44_check: unsupported or inconsistent static model %s\n", name.c_str() );
        return;
    }
    const Mdl44Static::Description &description = model.Description();
    size_t meshes = 0;
    for ( size_t b = 0; b < description.bodyParts.size(); ++b )
        for ( size_t m = 0; m < description.bodyParts[b].models.size(); ++m )
            meshes += description.bodyParts[b].models[m].meshes.size();
    Msg( "mdl44_check: %s OK: %u body parts, %u meshes, %u materials, %u skin families\n",
         name.c_str(), static_cast<unsigned>( description.bodyParts.size() ), static_cast<unsigned>( meshes ),
         static_cast<unsigned>( description.materials.size() ), description.skinFamilies );
}

CON_COMMAND( mdl44_upload, "Test creation and release of static MDL 44 GPU meshes." )
{
    if ( Cmd_Argc() != 2 ) { Msg( "Usage: mdl44_upload models/path/model.mdl\n" ); return; }
    const std::string name( Cmd_Argv( 1 ) );
    Mdl44::StaticModel model;
    if ( !LoadStaticModel44( name, model ) ) { Warning( "mdl44_upload: could not load static model %s\n", name.c_str() ); return; }
    CreateInterfaceFn factory = Sys_GetFactory( "studiorender.dll" );
    IStudioRender44 *renderer = factory ? static_cast<IStudioRender44 *>( factory( STUDIO_RENDER_44_INTERFACE_VERSION, NULL ) ) : NULL;
    if ( !renderer || !materialSystemInterface ) { Warning( "mdl44_upload: StudioRender 44 is unavailable\n" ); return; }

    Studio44MeshOwner handles( renderer );
    size_t skipped = 0;
    const bool ok = UploadStaticModel44( model, handles, skipped );
    const size_t uploaded = handles.Count();
    handles.Clear();
    if ( ok && uploaded )
        Msg( "mdl44_upload: %s OK: created and released %u GPU meshes with materials, skipped %u groups\n", name.c_str(), static_cast<unsigned>( uploaded ), static_cast<unsigned>( skipped ) );
    else
        Warning( "mdl44_upload: could not upload all meshes for %s (created %u, skipped %u)\n", name.c_str(), static_cast<unsigned>( uploaded ), static_cast<unsigned>( skipped ) );
}

CON_COMMAND( mdl44_cache_test, "Test releasing and recreating a static MDL 44 GPU cache." )
{
    if ( Cmd_Argc() != 2 ) { Msg( "Usage: mdl44_cache_test models/path/model.mdl\n" ); return; }
    const char *name = Cmd_Argv( 1 );
    Mdl44MapCache *cache = Mdl44MapCacheCreate( name );
    if ( !cache ) { Warning( "mdl44_cache_test: could not load %s\n", name ); return; }
    const bool first = Mdl44MapCacheCreateGpu( cache );
    const bool released = Mdl44MapCacheReleaseGpu( cache );
    const bool second = Mdl44MapCacheCreateGpu( cache );
    const bool releasedAgain = Mdl44MapCacheReleaseGpu( cache );
    Mdl44MapCacheDestroy( cache );
    if ( first && released && second && releasedAgain )
        Msg( "mdl44_cache_test: %s OK: GPU cache created and released twice\n", name );
    else
        Warning( "mdl44_cache_test: %s failed (create %d, release %d, recreate %d, release %d)\n", name, first, released, second, releasedAgain );
}
