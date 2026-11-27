// Inspect a release static model through the engine's normal filesystem.
#include "quakedef.h"
#include "cmd.h"
#include "convar.h"
#include "filesystem.h"
#include "filesystem_engine.h"
#include "gl_matsysiface.h"
#include "istudiorender44.h"
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
struct Mdl44CachedModel
{
    model_t *identity;
    std::string originalName;
    Studio44::studiohdr_t header;
    Mdl44Virtual::Model virtualModel;
};
static std::vector<Mdl44CachedModel *> s_Mdl44Models;
static Mdl44CachedModel *FindCached44(const model_t *identity)
{
    for(size_t i=0;i<s_Mdl44Models.size();++i) if(s_Mdl44Models[i]->identity==identity) return s_Mdl44Models[i];
    return NULL;
}
bool Mdl44ModelCacheHas(const model_t *model) { return FindCached44(model)!=NULL; }
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
    Mdl44CachedModel *cached=new Mdl44CachedModel; cached->identity=model; cached->originalName=name;
    std::vector<std::string> stack; std::vector<unsigned char> root;
    if(!ReadModelFile44(name,root) || !Studio44::ReadHeader(root.data(),root.size(),sizeof(cached->header),cached->header) ||
       !LoadVirtualGroup44(name,cached->virtualModel,stack)) { delete cached; return; }
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
              const IStudio44MeshSource &source, size_t model, size_t lod, bool software )
    {
        if ( variants.empty() || !variants[0] ) return false;
        void *handle = software ? m_renderer->CreateSoftwareMesh( materials, variants[0], source ) :
                                  m_renderer->CreateMesh( materials, variants[0], source );
        if ( !handle ) return false;
        Entry entry; entry.handle = handle; entry.model = model; entry.lod = lod; entry.materials = variants; entry.software = software;
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
    bool DrawSkinned( const float *transform, unsigned int skin, const float *bones, unsigned int boneCount ) const
    {
        bool drawn = false;
        for ( size_t i=0; i<m_handles.size(); ++i )
        {
            const Entry &entry=m_handles[i]; IMaterial *material=Material(entry,skin);
            if ( entry.model == 0 && entry.lod == 0 && material )
            {
                if ( !m_renderer->DrawSkinnedMesh(entry.handle,material,transform,bones,boneCount,1.0f) ) return false;
                drawn = true;
            }
        }
        return drawn;
    }
private:
    Studio44MeshOwner( const Studio44MeshOwner & );
    Studio44MeshOwner &operator=( const Studio44MeshOwner & );
    IStudioRender44 *m_renderer;
    struct Entry { void *handle; size_t model, lod; std::vector<IMaterial *> materials; bool software; };
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

static bool UploadStaticModel44( const Mdl44::StaticModel &model, Studio44MeshOwner &handles, size_t &skipped, bool allowSoftware = false )
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
                        const bool software = !( group.Flags() & 2 );
                        if ( software && !allowSoftware ) { ++skipped; continue; }
                        Studio44MeshSource source( mesh, group );
                        if ( !handles.Add( materialSystemInterface, variants, source, m, lod, software ) ) return false;
                    }
                }
            }
    return handles.Count() != 0;
}

static bool Mdl44RigidEntityDraw(const char *name, const float *transform, unsigned int skin);
static bool Mdl44RigidEntityBounds(const char *name, float *mins, float *maxs);
static void Mdl44RigidEntityReleaseGpu();
static void Mdl44RigidEntityShutdown();

// A deliberately isolated preview while sequence selection and VCD playback
// are being implemented. It does not change the legacy model loader.
class Mdl44EntityPreview : public IMdl44EntityPreview
{
public:
    Mdl44EntityPreview() : m_gpu(NULL), m_attempted(false), m_valid(false), m_logged(false) {}
    void ReleaseGpu() { delete m_gpu; m_gpu=NULL; }
    void Shutdown()
    {
        ReleaseGpu(); m_model=Mdl44::StudioModel(); m_mdl.clear(); m_ani.clear();
        m_skeleton=Mdl44Skeleton::Skeleton(); m_anims=Mdl44Animation::Animations(); m_sequences=Mdl44Sequence::Data();
        m_attempted=false; m_valid=false; m_logged=false;
    }
    virtual bool Draw(const char *name, const float *transform, unsigned int skin, float time)
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
        std::vector<Mdl44Binding::Matrix> matrices;
        const int frame=std::isfinite(time) && time>0 ? static_cast<int>(std::fmod(double(time)*clip.fps,clip.frames)) : 0;
        if (!Mdl44Animation::Resolve(m_mdl.data(),m_mdl.size(),m_ani.empty()?NULL:m_ani.data(),m_ani.size(),m_anims,clip,data) ||
            !Mdl44Animation::SamplePose(data,clip,m_skeleton,frame,pose) ||
            !Mdl44Binding::BuildSkinMatrices(m_skeleton,pose,matrices) || matrices.empty()) return false;
        if (!m_gpu)
        {
            CreateInterfaceFn factory=Sys_GetFactory("studiorender.dll");
            IStudioRender44 *renderer=factory ? static_cast<IStudioRender44 *>(factory(STUDIO_RENDER_44_INTERFACE_VERSION,NULL)) : NULL;
            if (!renderer || !materialSystemInterface) return false;
            Studio44MeshOwner *gpu=new Studio44MeshOwner(renderer); size_t skipped=0;
            if (!UploadStaticModel44(m_model,*gpu,skipped,true) || skipped) { delete gpu; return false; }
            m_gpu=gpu;
        }
        if (!m_gpu->DrawSkinned(transform,skin,&matrices[0].m[0][0],static_cast<unsigned>(matrices.size()))) return false;
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
