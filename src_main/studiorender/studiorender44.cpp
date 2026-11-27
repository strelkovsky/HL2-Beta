#include "istudiorender44.h"
#include "studio44_transform.h"
#include "interface.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imesh.h"
#include <vector>
#include <cmath>

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
        bool software;
        std::vector<Studio44UploadVertex> sourceVertices;
        std::vector<uint16_t> sourceIndices;
        std::vector<StripData> strips;
    };

    class CStudioRender44 : public IStudioRender44
    {
    public:
        virtual void *CreateMesh( IMaterialSystem *materials, IMaterial *material, const IStudio44MeshSource &source )
        { return Create( materials, material, source, false ); }

        virtual void *CreateSoftwareMesh( IMaterialSystem *materials, IMaterial *material, const IStudio44MeshSource &source )
        { return Create( materials, material, source, true ); }

    private:
        void *Create( IMaterialSystem *materials, IMaterial *material, const IStudio44MeshSource &source, bool software )
        {
            const unsigned int vertexCount = source.VertexCount(), indexCount = source.IndexCount();
            if ( !materials || !material || !vertexCount || vertexCount > 65535 || !indexCount || indexCount > 0x7fffffff ) return NULL;
            std::vector<Studio44UploadVertex> vertices( vertexCount );
            std::vector<uint16_t> indices( indexCount );
            for ( unsigned int i = 0; i < vertexCount; ++i )
            {
                Studio44UploadVertex &v = vertices[i];
                if ( !source.Vertex( i, v ) || v.boneCount > 3 || v.vertex.boneCount > 3 ) return NULL;
                if ( software )
                {
                    if ( !v.vertex.boneCount ) return NULL;
                    for ( unsigned int j=0; j<v.vertex.boneCount; ++j )
                        if ( v.vertex.bones[j] < 0 || v.vertex.bones[j] >= 128 || !std::isfinite(v.vertex.weights[j]) || v.vertex.weights[j] < 0.0f ) return NULL;
                }
                else for ( unsigned int j = 0; j < v.boneCount; ++j )
                    if ( v.weightIndex[j] >= v.vertex.boneCount || v.boneId[j] < -1 || v.boneId[j] >= NUM_MODEL_TRANSFORMS ) return NULL;
            }
            for ( unsigned int i = 0; i < indexCount; ++i )
                if ( !source.Index( i, indices[i] ) || indices[i] >= vertexCount ) return NULL;

            UploadedMesh *result = new UploadedMesh;
            if ( !result ) return NULL;
            result->materials = materials;
            result->mesh = NULL;
            result->software = software;
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
                         ( state.bone >= 0 && ( state.bone >= 128 || state.hardware < 0 || state.hardware >= NUM_MODEL_TRANSFORMS ) ) )
                    { delete result; return NULL; }
                    data.states.push_back( state );
                }
                result->strips.push_back( data );
            }
            if ( result->strips.empty() ) { delete result; return NULL; }
            if ( software )
            {
                result->sourceVertices.swap(vertices); result->sourceIndices.swap(indices);
                material->IncrementReferenceCount(); result->material=material;
                return result;
            }
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

    public:
        virtual void DestroyMesh( void *handle )
        {
            UploadedMesh *mesh = static_cast<UploadedMesh *>( handle );
            if ( !mesh ) return;
            if ( mesh->mesh ) mesh->materials->DestroyStaticMesh( mesh->mesh );
            mesh->material->DecrementReferenceCount();
            delete mesh;
        }

        virtual bool DrawMesh( void *handle, IMaterial *material, const float *transform, float alpha )
        {
            return Draw( handle, material, transform, NULL, 0, alpha );
        }

        virtual bool DrawSkinnedMesh( void *handle, IMaterial *material, const float *transform,
                                      const float *boneMatrices, unsigned int boneCount, float alpha )
        {
            if ( !boneMatrices || !boneCount || boneCount > 128 ) return false;
            return Draw( handle, material, transform, boneMatrices, boneCount, alpha );
        }

    private:
        bool DrawSoftware( UploadedMesh *mesh, IMaterial *material, const float *transform,
                           const float *bones, unsigned int boneCount, float alpha )
        {
            if ( !bones || !boneCount ) return false;
            std::vector<Studio44UploadVertex> transformed=mesh->sourceVertices;
            for ( size_t i=0; i<transformed.size(); ++i )
            {
                Studio44UploadVertex &v=transformed[i]; const Studio44UploadVertex &src=mesh->sourceVertices[i];
                if (!Studio44Transform::SkinVertex(src.vertex,src.tangent,bones,boneCount,
                    v.vertex.position,v.vertex.normal,v.tangent)) return false;
            }
            float matrix[4][4]; Studio44Transform::ToMaterial(transform,NULL,matrix);
            mesh->materials->MatrixMode(MATERIAL_MODEL); mesh->materials->PushMatrix(); mesh->materials->LoadMatrix(&matrix[0][0]);
            mesh->materials->SetNumBoneWeights(0); material->AlphaModulate(alpha); material->ColorModulate(1,1,1);
            mesh->materials->Bind(material);
            IMesh *dynamic=mesh->materials->GetDynamicMesh(false,0,0,material);
            if (!dynamic) { material->AlphaModulate(1); mesh->materials->MatrixMode(MATERIAL_MODEL); mesh->materials->PopMatrix(); return false; }
            CMeshBuilder builder; builder.Begin(dynamic,MATERIAL_HETEROGENOUS,static_cast<int>(transformed.size()),static_cast<int>(mesh->sourceIndices.size()));
            for(size_t i=0;i<transformed.size();++i)
            {
                const Studio44UploadVertex &v=transformed[i];
                builder.Position3fv(v.vertex.position); builder.Normal3fv(v.vertex.normal); builder.TexCoord2fv(0,v.vertex.texcoord);
                if (material->NeedsTangentSpace()) builder.UserData(v.tangent);
                builder.Color4ub(255,255,255,255); builder.AdvanceVertex();
            }
            for(size_t i=0;i<mesh->sourceIndices.size();++i) { builder.Index(mesh->sourceIndices[i]); builder.AdvanceIndex(); }
            builder.End();
            for(size_t i=0;i<mesh->strips.size();++i)
            {
                const Studio44Strip &strip=mesh->strips[i].strip;
                dynamic->SetPrimitiveType(strip.flags & 2 ? MATERIAL_TRIANGLE_STRIP : MATERIAL_TRIANGLES);
                dynamic->SetColorMesh(NULL); dynamic->Draw(strip.indexOffset,strip.indexCount);
            }
            mesh->materials->MatrixMode(MATERIAL_MODEL); mesh->materials->PopMatrix(); material->AlphaModulate(1);
            return true;
        }

        bool Draw( void *handle, IMaterial *material, const float *transform,
                   const float *bones, unsigned int boneCount, float alpha )
        {
            UploadedMesh *mesh = static_cast<UploadedMesh *>( handle );
            if ( !mesh || !material || !transform ) return false;
            if ( mesh->software ) return DrawSoftware( mesh, material, transform, bones, boneCount, alpha );
            // Validate before changing the material-system matrix stacks.
            for ( size_t i=0; i<mesh->strips.size(); ++i )
                for ( size_t j=0; j<mesh->strips[i].states.size(); ++j )
                {
                    const BoneState &state=mesh->strips[i].states[j];
                    if ( state.bone < 0 ) break;
                    if ( bones ? static_cast<unsigned int>(state.bone) >= boneCount : state.bone != 0 ) return false;
                }
            float matrix[4][4];
            Studio44Transform::ToMaterial( transform, NULL, matrix );
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
                    float boneMatrix[4][4];
                    Studio44Transform::ToMaterial( transform, bones ? bones + static_cast<size_t>(state.bone) * 12 : NULL, boneMatrix );
                    mesh->materials->LoadMatrix( &boneMatrix[0][0] );
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
