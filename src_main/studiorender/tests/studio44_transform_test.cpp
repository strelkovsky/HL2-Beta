#include "../studio44_transform.h"
#include <cassert>
#include <cmath>
#include <cstdio>
static void CheckPoint(const float (&matrix)[4][4],const float point[3],const float expected[3])
{
    for(int r=0;r<3;++r)
    {
        float value=matrix[3][r];
        for(int k=0;k<3;++k) value+=matrix[k][r]*point[k];
        assert(std::fabs(value-expected[r])<0.00001f);
    }
    assert(matrix[0][3]==0 && matrix[1][3]==0 && matrix[2][3]==0 && matrix[3][3]==1);
}
int main()
{
    const float world[]={0,-1,0,10, 1,0,0,20, 0,0,1,30};
    const float bone[]={0,0,1,1, 0,1,0,2, -1,0,0,3};
    const float point[]={2,4,6}, skinned[]={4,27,31}, rigid[]={6,22,36};
    float matrix[4][4];
    Studio44Transform::ToMaterial(world,bone,matrix); CheckPoint(matrix,point,skinned);
    Studio44Transform::ToMaterial(world,NULL,matrix); CheckPoint(matrix,point,rigid);
    StudioVertexView::Vertex vertex = {}; vertex.position[0]=2; vertex.position[1]=1;
    vertex.normal[0]=1; vertex.boneCount=2; vertex.bones[0]=0; vertex.bones[1]=1;
    vertex.weights[0]=0.25f; vertex.weights[1]=0.75f;
    const float bones[]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,-1,0,4, 1,0,0,0, 0,0,1,0};
    const float tangent[]={0,1,0,-1}; float pos[3],normal[3],resultTangent[4];
    assert(Studio44Transform::SkinVertex(vertex,tangent,bones,2,pos,normal,resultTangent));
    assert(std::fabs(pos[0]-2.75f)<0.0001f && std::fabs(pos[1]-1.75f)<0.0001f);
    assert(std::fabs(normal[0]-0.25f)<0.0001f && std::fabs(normal[1]-0.75f)<0.0001f);
    assert(std::fabs(resultTangent[0]+0.75f)<0.0001f && std::fabs(resultTangent[1]-0.25f)<0.0001f && resultTangent[3]==-1);
    vertex.bones[1]=2; assert(!Studio44Transform::SkinVertex(vertex,tangent,bones,2,pos,normal,resultTangent));
    std::puts("StudioRender 44 skinned and rigid world transforms verified");
}
