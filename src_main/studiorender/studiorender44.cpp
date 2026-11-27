#include "istudiorender44.h"
#include "interface.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imesh.h"
#include <vector>

// memdbgon must be the last include file.
#include "tier0/memdbgon.h"

namespace
{
    struct BoneState { int32_t hardware, bone; };
    struct StripData { Studio44Strip strip; std::vector<BoneState> states; };
    struct UploadedMesh
    {
        IMaterialSystem *materials;
        IMaterial *material;
        IMesh *mesh;
        std::vector<StripData> strips;
    };

    class CStudioRender44 : public IStudioRender44
    {
    public:
        virtual void *CreateMesh( IMaterialSystem *materials, IMaterial *material, const IStudio44MeshSource &source )
        {
            const unsigned int vertexCount = source.VertexCount(), indexCount = source.IndexCount();
            if ( !materials || !material || !vertexCount || vertexCount > 65535 || !indexCount || indexCount > 0x7fffffff ) return NULL;
            std::vector<Studio44UploadVertex> vertices( vertexCount );
            std::vector<uint16_t> indices( indexCount );
            for ( unsigned int i = 0; i < vertexCount; ++i )
            {
                Studio44UploadVertex &v = vertices[i];
                if ( !source.Vertex( i, v ) || v.boneCount > 3 ) return NULL;
                for ( unsigned int j = 0; j < v.boneCount; ++j )
                    if ( v.weightIndex[j] >= 3 || v.boneId[j] < -1 ) return NULL;
            }
            for ( unsigned int i = 0; i < indexCount; ++i )
                if ( !source.Index( i, indices[i] ) || indices[i] >= vertexCount ) return NULL;

            UploadedMesh *result = new UploadedMesh;
            if ( !result ) return NULL;
            result->materials = materials;
            const unsigned int stripCount = source.StripCount();
            for ( unsigned int i = 0; i < stripCount; ++i )
            {
                StripData data;
                if ( !source.Strip( i, data.strip ) || data.strip.indexOffset > indexCount ||
                     data.strip.indexCount > indexCount - data.strip.indexOffset || data.strip.boneCount > 4 )
                { delete result; return NULL; }
                for ( unsigned int j = 0; j < data.strip.boneStateCount; ++j )
                {
                    BoneState state;
                    if ( !source.BoneState( i, j, state.hardware, state.bone ) ||
                         ( state.bone >= 0 && ( state.bone != 0 || state.hardware < 0 || state.hardware >= NUM_MODEL_TRANSFORMS ) ) )
                    { delete result; return NULL; }
                    data.states.push_back( state );
                }
                result->strips.push_back( data );
            }
            if ( result->strips.empty() ) { delete result; return NULL; }
            IMesh *mesh = materials->CreateStaticMesh( MATERIAL_VERTEX_FORMAT_MODEL, false );
            if ( !mesh ) { delete result; return NULL; }
            CMeshBuilder builder;
            builder.Begin( mesh, MATERIAL_HETEROGENOUS, static_cast<int>( vertexCount ), static_cast<int>( indexCount ) );
            for ( unsigned int i = 0; i < vertexCount; ++i )
            {
                const Studio44UploadVertex &v = vertices[i];
                builder.Position3fv( v.vertex.position );
                builder.Normal3fv( v.vertex.normal );
                builder.TexCoord2fv( 0, v.vertex.texcoord );
                builder.UserData( v.tangent );
                builder.Color4ub( 255, 255, 255, 255 );
                float total = 0.0f;
                for ( unsigned int j = 0; j < v.boneCount; ++j ) total += v.vertex.weights[v.weightIndex[j]];
                if ( total == 0.0f ) total = 1.0f;
                unsigned int j = 0;
                for ( ; j < v.boneCount; ++j )
                {
                    if ( v.boneId[j] == -1 )
                    {
                        builder.BoneWeight( j, 0.0f );
                        builder.BoneMatrix( j, BONE_MATRIX_INDEX_INVALID );
                    }
                    else
                    {
                        builder.BoneWeight( j, v.vertex.weights[v.weightIndex[j]] / total );
                        builder.BoneMatrix( j, v.boneId[j] );
                    }
                }
                for ( ; j < 4; ++j )
                {
                    builder.BoneWeight( j, 0.0f );
                    builder.BoneMatrix( j, BONE_MATRIX_INDEX_INVALID );
                }
                builder.AdvanceVertex();
            }
            for ( unsigned int i = 0; i < indexCount; ++i )
            {
                builder.Index( indices[i] ); builder.AdvanceIndex();
            }
            builder.End();
            material->IncrementReferenceCount();
            result->material = material;
            result->mesh = mesh;
            return result;
        }

        virtual void DestroyMesh( void *handle )
        {
            UploadedMesh *mesh = static_cast<UploadedMesh *>( handle );
            if ( !mesh ) return;
            mesh->materials->DestroyStaticMesh( mesh->mesh );
            mesh->material->DecrementReferenceCount();
            delete mesh;
        }

        virtual bool DrawMesh( void *handle, IMaterial *material, const float *transform, float alpha )
        {
            UploadedMesh *mesh = static_cast<UploadedMesh *>( handle );
            if ( !mesh || !material || !transform ) return false;
            float matrix[4][4] = {};
            for ( int i = 0; i < 3; ++i )
                for ( int j = 0; j < 4; ++j ) matrix[j][i] = transform[i * 4 + j];
            matrix[3][3] = 1.0f;
            bool pushed[NUM_MODEL_TRANSFORMS] = {};
            mesh->materials->MatrixMode( MATERIAL_MODEL );
            mesh->materials->PushMatrix();
            mesh->materials->LoadMatrix( &matrix[0][0] );
            material->AlphaModulate( alpha );
            material->ColorModulate( 1.0f, 1.0f, 1.0f );
            mesh->materials->Bind( material );
            for ( size_t i = 0; i < mesh->strips.size(); ++i )
            {
                const StripData &data = mesh->strips[i];
                mesh->materials->SetNumBoneWeights( data.strip.boneCount );
                for ( size_t j = 0; j < data.states.size(); ++j )
                {
                    const BoneState &state = data.states[j];
                    if ( state.bone < 0 ) break;
                    mesh->materials->MatrixMode( MATERIAL_MODEL_MATRIX( state.hardware ) );
                    if ( state.hardware && !pushed[state.hardware] )
                    { mesh->materials->PushMatrix(); pushed[state.hardware] = true; }
                    mesh->materials->LoadMatrix( &matrix[0][0] );
                }
                mesh->mesh->SetPrimitiveType( data.strip.flags & 2 ? MATERIAL_TRIANGLE_STRIP : MATERIAL_TRIANGLES );
                mesh->mesh->SetColorMesh( NULL );
                mesh->mesh->Draw( data.strip.indexOffset, data.strip.indexCount );
            }
            for ( int i = NUM_MODEL_TRANSFORMS - 1; i > 0; --i )
                if ( pushed[i] ) { mesh->materials->MatrixMode( MATERIAL_MODEL_MATRIX( i ) ); mesh->materials->PopMatrix(); }
            mesh->materials->SetNumBoneWeights( 0 );
            mesh->materials->MatrixMode( MATERIAL_MODEL );
            mesh->materials->PopMatrix();
            material->AlphaModulate( 1.0f );
            return true;
        }
    };
}

EXPOSE_SINGLE_INTERFACE( CStudioRender44, IStudioRender44, STUDIO_RENDER_44_INTERFACE_VERSION );
