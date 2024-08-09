//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $Header: $
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShader.h"

#include "colorout_vs30.inc"
#include "colorout_ps30.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


BEGIN_VS_SHADER_FLAGS( ColorOut, "Help for ColorOut", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( PASSCOUNT, SHADER_PARAM_TYPE_INTEGER, "1", "Number of passes for this material" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		SET_FLAGS2( MATERIAL_VAR2_SUPPORTS_HW_SKINNING );
	}

	SHADER_INIT
	{
	}

	SHADER_FALLBACK
	{
		if (!g_pHardwareConfig->SupportsPixelShaders_3_0())
			return "Wireframe";

		return nullptr;
	}

	
	SHADER_DRAW
	{
		SHADOW_STATE
		{
			//pShaderShadow->EnableDepthTest( true );
			//pShaderShadow->EnableDepthWrites( true );
			//pShaderShadow->EnableBlending( false );
			////pShaderShadow->BlendFunc( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
			//pShaderShadow->BlendFunc( SHADER_BLEND_ONE, SHADER_BLEND_ONE );

			
			pShaderShadow->EnableDepthTest( true );
			pShaderShadow->EnableDepthWrites( true );
			pShaderShadow->EnableBlending( false );
			pShaderShadow->BlendFunc( SHADER_BLEND_ONE, SHADER_BLEND_ZERO );

			// Set stream format (note that this shader supports compression)
			unsigned int flags = VERTEX_POSITION | VERTEX_FORMAT_COMPRESSED;
			int nTexCoordCount = 1;
			int userDataSize = 0;
			pShaderShadow->VertexShaderVertexFormat( flags, nTexCoordCount, nullptr, userDataSize );

			DECLARE_STATIC_VERTEX_SHADER( colorout_vs30 );
			SET_STATIC_VERTEX_SHADER( colorout_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( colorout_ps30 );
			SET_STATIC_PIXEL_SHADER( colorout_ps30 );
		}
		DYNAMIC_STATE
		{
			
			float color[4];
			color[0] = params[PASSCOUNT]->GetFloatValue();
			color[1] = 1.0f - params[PASSCOUNT]->GetFloatValue();
			color[2] = 0.0f;
			color[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( 1, color, 1 );

			DECLARE_DYNAMIC_VERTEX_SHADER( colorout_vs30 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( SKINNING, pShaderAPI->GetCurrentNumBones() > 0 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( COMPRESSED_VERTS, (int)vertexCompression );
			SET_DYNAMIC_VERTEX_SHADER( colorout_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( colorout_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( colorout_ps30 );

		}
		Draw();

		SHADOW_STATE
		{

			pShaderShadow->EnableDepthTest( true );
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableBlending( true );
			pShaderShadow->BlendFunc( SHADER_BLEND_ONE, SHADER_BLEND_ONE );
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, nullptr, 0 );
			pShaderShadow->PolyMode( SHADER_POLYMODEFACE_FRONT, SHADER_POLYMODE_LINE );
								
			DECLARE_STATIC_VERTEX_SHADER( colorout_vs30 );
			SET_STATIC_VERTEX_SHADER( colorout_vs30 );

			DECLARE_STATIC_PIXEL_SHADER(colorout_ps30);
			SET_STATIC_PIXEL_SHADER( colorout_ps30 );
		}
		DYNAMIC_STATE
		{
			float color[4];
			color[0] = 0.0f;
			color[1] = 0.1f;
			color[2] = 0.1f;
			color[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( 0, color, 1 );
		
			DECLARE_DYNAMIC_VERTEX_SHADER( colorout_vs30 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( SKINNING, pShaderAPI->GetCurrentNumBones() > 0 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( COMPRESSED_VERTS, (int)vertexCompression );
			SET_DYNAMIC_VERTEX_SHADER( colorout_vs30 );
		
			DECLARE_DYNAMIC_PIXEL_SHADER( colorout_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( colorout_ps30 );
		}
		Draw();
	}
END_SHADER


