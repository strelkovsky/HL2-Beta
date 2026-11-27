#include "shaderlib/CShader.h"

// SE2007 intro modes, adapted to the older framebuffer binding API.
BEGIN_SHADER( IntroScreenSpaceEffect, "Blend the intro camera and player view" )
    BEGIN_SHADER_PARAMS
        SHADER_PARAM( MODE, SHADER_PARAM_TYPE_INTEGER, "0", "Intro blend mode" )
    END_SHADER_PARAMS

    SHADER_INIT_PARAMS()
    {
        params[BASETEXTURE]->SetStringValue( "_rt_IntroFB" );
        if ( !params[MODE]->IsDefined() ) params[MODE]->SetIntValue( 0 );
    }
    SHADER_INIT { LoadTexture( BASETEXTURE ); }
    SHADER_FALLBACK
    {
        if ( g_pHardwareConfig->GetDXSupportLevel() < 90 ) return "Wireframe";
        return 0;
    }
    SHADER_DRAW
    {
        SHADOW_STATE
        {
            pShaderShadow->EnableDepthWrites( false );
            pShaderShadow->EnableDepthTest( false );
            pShaderShadow->EnableTexture( SHADER_TEXTURE_STAGE0, true );
            pShaderShadow->EnableTexture( SHADER_TEXTURE_STAGE1, true );
            pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, 0, 0, 0 );
            pShaderShadow->SetVertexShader( "screenspaceeffect_vs20" );
            pShaderShadow->SetPixelShader( "introscreenspaceeffect_ps20" );
            pShaderShadow->EnableBlending( true );
            pShaderShadow->BlendFunc( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE );
        }
        DYNAMIC_STATE
        {
            pShaderAPI->SetDefaultState();
            BindTexture( SHADER_TEXTURE_STAGE0, BASETEXTURE, FRAME );
            pShaderAPI->BindFBTexture( SHADER_TEXTURE_STAGE1 );
            int mode = params[MODE]->GetIntValue();
            if ( mode < 0 || mode > 9 ) mode = 0;
            pShaderAPI->SetPixelShaderIndex( mode );
            float alpha[4] = { params[ALPHA]->GetFloatValue(), 0, 0, 0 };
            pShaderAPI->SetPixelShaderConstant( 0, alpha, 1 );
        }
        Draw();
    }
END_SHADER
