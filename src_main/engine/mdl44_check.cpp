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

static bool LoadStaticModel44( const std::string &name, Mdl44::StaticModel &model )
{
    if ( name.size() < 5 || _stricmp( name.c_str() + name.size() - 4, ".mdl" ) ) return false;
    const std::string base = name.substr( 0, name.size() - 4 );
    std::vector<unsigned char> mdl, vvd, vtx;
    if ( !ReadModelFile44( name, mdl ) || mdl.size() < 156 || std::memcmp( mdl.data(), "IDST", 4 ) ||
         Mdl44Static::Read32( mdl.data(), 4 ) != 44 || !( Mdl44Static::Read32( mdl.data(), 152 ) & 16 ) ) return false;
    return ReadModelFile44( base + ".vvd", vvd ) &&
           ReadModelFile44( base + ".dx80.vtx", vtx ) &&
           model.Load( mdl.data(), mdl.size(), vvd.data(), vvd.size(), vtx.data(), vtx.size() );
}

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
        for ( size_t i = 0; i < m_handles.size(); ++i ) m_renderer->DestroyMesh( m_handles[i].handle );
        m_handles.clear();
    }
    bool Add( IMaterialSystem *materials, IMaterial *material, const IStudio44MeshSource &source, size_t model, size_t lod )
    {
        Entry entry = { NULL, model, lod, material->IsTranslucent() };
        m_handles.push_back( entry );
        void *handle = m_renderer->CreateMesh( materials, material, source );
        if ( !handle ) { m_handles.pop_back(); return false; }
        m_handles.back().handle = handle;
        return true;
    }
    size_t Count() const { return m_handles.size(); }
    bool HasOpaque() const { return HasPass( false ); }
    bool HasTranslucent() const { return HasPass( true ); }
    bool Draw( const float *transform, float alpha, Mdl44DrawPass pass ) const
    {
        bool drawn = false;
        for ( size_t i = 0; i < m_handles.size(); ++i )
            if ( m_handles[i].model == 0 && m_handles[i].lod == 0 &&
                 ( pass == MDL44_DRAW_ALL || ( pass == MDL44_DRAW_TRANSLUCENT ) == m_handles[i].translucent ) )
                drawn = m_renderer->DrawMesh( m_handles[i].handle, transform, alpha ) || drawn;
        return drawn;
    }
private:
    Studio44MeshOwner( const Studio44MeshOwner & );
    Studio44MeshOwner &operator=( const Studio44MeshOwner & );
    IStudioRender44 *m_renderer;
    struct Entry { void *handle; size_t model, lod; bool translucent; };
    bool HasPass( bool translucent ) const
    {
        for ( size_t i = 0; i < m_handles.size(); ++i )
            if ( m_handles[i].model == 0 && m_handles[i].lod == 0 && m_handles[i].translucent == translucent ) return true;
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
                    uint16_t materialIndex;
                    if ( !model.MeshMaterial( b, m, k, 0, materialIndex ) ) return false;
                    IMaterial *material = FindStudio44Material( model, lod, materialIndex );
                    if ( !material || material->NeedsFrameBufferTexture() ) return false;
                    for ( size_t g = 0; ; ++g )
                    {
                        Mdl44::GroupView group;
                        if ( !model.Group( b, m, lod, k, g, group ) ) break;
                        if ( !group.VertexCount() || !group.IndexCount() ) continue;
                        if ( !( group.Flags() & 2 ) ) { ++skipped; continue; }
                        Studio44MeshSource source( mesh, group );
                        if ( !handles.Add( materialSystemInterface, material, source, m, lod ) ) return false;
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
    bool loggedOpaque, loggedTranslucent;
    Mdl44MapCache() : gpu( NULL ), loggedOpaque( false ), loggedTranslucent( false ) {}
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
    if ( !cache || !cache->gpu || !transform || skin != 0 || !cache->gpu->Draw( transform, alpha, pass ) ) return false;
    if ( pass != MDL44_DRAW_TRANSLUCENT && cache->gpu->HasOpaque() && !cache->loggedOpaque )
    { Msg( "MDL 44 static prop opaque pass: %s\n", cache->name.c_str() ); cache->loggedOpaque = true; }
    if ( pass != MDL44_DRAW_OPAQUE && cache->gpu->HasTranslucent() && !cache->loggedTranslucent )
    { Msg( "MDL 44 static prop translucent pass: %s\n", cache->name.c_str() ); cache->loggedTranslucent = true; }
    return true;
}

bool Mdl44MapCacheCanDraw( const Mdl44MapCache *cache, unsigned int skin )
{
    return cache && cache->gpu && skin == 0;
}

bool Mdl44MapCacheHasOpaque( const Mdl44MapCache *cache )
{ return cache && cache->gpu && cache->gpu->HasOpaque(); }

bool Mdl44MapCacheHasTranslucent( const Mdl44MapCache *cache )
{ return cache && cache->gpu && cache->gpu->HasTranslucent(); }

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
