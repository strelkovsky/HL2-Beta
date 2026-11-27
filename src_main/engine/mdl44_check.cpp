// Inspect a release static model through the engine's normal filesystem.
#include "quakedef.h"
#include "cmd.h"
#include "convar.h"
#include "filesystem.h"
#include "filesystem_engine.h"
#include "gl_matsysiface.h"
#include "istudiorender44.h"
#include "mdl44_model.h"
#include "mdl44_mapcache.h"
#include "mdl44_skeleton.h"
#include "mdl44_animation.h"
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
              const IStudio44MeshSource &source, size_t model, size_t lod )
    {
        if ( variants.empty() || !variants[0] ) return false;
        void *handle = m_renderer->CreateMesh( materials, variants[0], source );
        if ( !handle ) return false;
        Entry entry; entry.handle = handle; entry.model = model; entry.lod = lod; entry.materials = variants;
        for ( size_t i = 0; i < variants.size(); ++i )
            if ( variants[i] ) variants[i]->IncrementReferenceCount();
        m_handles.push_back( entry );
        return true;
    }
    size_t Count() const { return m_handles.size(); }
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
    struct Entry { void *handle; size_t model, lod; std::vector<IMaterial *> materials; };
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

static bool UploadStaticModel44( const Mdl44::StaticModel &model, Studio44MeshOwner &handles, size_t &skipped )
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
                        if ( !( group.Flags() & 2 ) ) { ++skipped; continue; }
                        Studio44MeshSource source( mesh, group );
                        if ( !handles.Add( materialSystemInterface, variants, source, m, lod ) ) return false;
                    }
                }
            }
    return handles.Count() != 0;
}

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
    if (!UploadStaticModel44(model,gpu,skipped)) { Warning("mdl44_skin_draw_test: upload failed\n"); return; }
    const float transform[12]={1,0,0,0, 0,1,0,0, 0,0,1,0};
    if (!gpu.DrawSkinned(transform,0,&matrices[0].m[0][0],static_cast<unsigned>(matrices.size())))
    { Warning("mdl44_skin_draw_test: GPU draw failed\n"); return; }
    Msg("mdl44_skin_draw_test: %s clip '%s' frame %d: GPU draw completed, %u hardware groups uploaded; %u software groups skipped\n",
        name.c_str(),clip.name.c_str(),frame,static_cast<unsigned>(gpu.Count()),static_cast<unsigned>(skipped));
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
