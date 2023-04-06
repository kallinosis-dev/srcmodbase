//========= Copyright � 1996-2007, Valve Corporation, All rights reserved. ============//
//
// Purpose: Ice surface shader (for the ice gun)
//
//=============================================================================//

#include "BaseVSShader.h"
#include "paintblob_helper.h"
#include "cpp_shader_constant_register_map.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

DEFINE_FALLBACK_SHADER( paintblob, paintblob_dx9 )

BEGIN_VS_SHADER( paintblob_dx9, "paintblob" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( BACKSURFACE, SHADER_PARAM_TYPE_BOOL, "0.0", "specify that this is the back surface of the ice" )

		SHADER_PARAM( NORMALMAP, SHADER_PARAM_TYPE_TEXTURE, "models/shadertest/shader1_normal", "normal map" )
		SHADER_PARAM( SPECMASKTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "shadertest/BaseTexture", "specular reflection mask" )
		SHADER_PARAM( LIGHTWARPTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "shadertest/BaseTexture", "1D ramp texture for tinting scalar diffuse term" )
		SHADER_PARAM( FRESNELWARPTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "shadertest/BaseTexture", "1D ramp texture for controlling fresnel falloff" )
		SHADER_PARAM( OPACITYTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "shadertest/BaseTexture", "1D ramp texture for controlling fresnel falloff" )
		SHADER_PARAM( ENVMAP, SHADER_PARAM_TYPE_TEXTURE, "", "environment map" )

		SHADER_PARAM( UVSCALE, SHADER_PARAM_TYPE_FLOAT, "0.02", "uv projection scale" )
		SHADER_PARAM( BUMPSTRENGTH, SHADER_PARAM_TYPE_FLOAT, "1.0", "bump map strength" )
		SHADER_PARAM( FRESNELBUMPSTRENGTH, SHADER_PARAM_TYPE_FLOAT, "1.0", "bump map strength for fresnel" )
		SHADER_PARAM( TRANSLUCENTFRESNELMINMAXEXP, SHADER_PARAM_TYPE_VEC3, "[0.8 1.0 1.0]", "fresnel params" )

		SHADER_PARAM( INTERIOR, SHADER_PARAM_TYPE_BOOL, "1", "Enable interior layer" )
		SHADER_PARAM( INTERIORFOGSTRENGTH, SHADER_PARAM_TYPE_FLOAT, "0.06", "fog strength (scales with thickness of the interior volume)" )
		SHADER_PARAM( INTERIORBACKGROUNDBOOST, SHADER_PARAM_TYPE_FLOAT, "7", "boosts the brightness of bright background pixels" )
		SHADER_PARAM( INTERIORAMBIENTSCALE, SHADER_PARAM_TYPE_FLOAT, "0.3", "scales ambient light in the interior volume" );
		SHADER_PARAM( INTERIORBACKLIGHTSCALE, SHADER_PARAM_TYPE_FLOAT, "0.3", "scales backlighting in the interior volume" );
		SHADER_PARAM( INTERIORREFRACTSTRENGTH, SHADER_PARAM_TYPE_FLOAT, "0.015", "strength of bumped refract of the background seen through the interior" )

		SHADER_PARAM( DIFFUSESCALE, SHADER_PARAM_TYPE_FLOAT, "1.0", "" )
		SHADER_PARAM( PHONGEXPONENT, SHADER_PARAM_TYPE_FLOAT, "1.0", "specular exponent" )
		SHADER_PARAM( PHONGBOOST, SHADER_PARAM_TYPE_FLOAT, "1.0", "specular boost" )
		SHADER_PARAM( PHONGEXPONENT2, SHADER_PARAM_TYPE_FLOAT, "1.0", "specular exponent" )
		SHADER_PARAM( PHONGBOOST2, SHADER_PARAM_TYPE_FLOAT, "1.0", "specular boost" )
		SHADER_PARAM( RIMLIGHTEXPONENT, SHADER_PARAM_TYPE_FLOAT, "1.0", "rim light exponent" )
		SHADER_PARAM( RIMLIGHTBOOST, SHADER_PARAM_TYPE_FLOAT, "1.0", "rim light boost" )
		SHADER_PARAM( BASECOLORTINT, SHADER_PARAM_TYPE_VEC3, "[1.0 1.0 1.0]", "base texture tint" )

		SHADER_PARAM( UVPROJOFFSET, SHADER_PARAM_TYPE_VEC3, "[0 0 0]", "Center for UV projection" )
		SHADER_PARAM( BBMIN, SHADER_PARAM_TYPE_VEC3, "[0 0 0]", "" )
		SHADER_PARAM( BBMAX, SHADER_PARAM_TYPE_VEC3, "[0 0 0]", "" )

		SHADER_PARAM( CONTACTSHADOWS, SHADER_PARAM_TYPE_BOOL, "1", "" )

		SHADER_PARAM( BUMPFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "" )

	END_SHADER_PARAMS

	void SetupVarsIceSurface( PaintBlobVars_t &info )
	{
		info.m_nBackSurface = BACKSURFACE;
		info.m_nBaseTexture = BASETEXTURE;
		info.m_nNormalMap = NORMALMAP;
		info.m_nSpecMap = SPECMASKTEXTURE;
		info.m_nLightWarpTexture = LIGHTWARPTEXTURE;
		info.m_nFresnelWarpTexture = FRESNELWARPTEXTURE;
		info.m_nOpacityTexture = OPACITYTEXTURE;
		info.m_nEnvMap = ENVMAP;
		info.m_nBumpStrength = BUMPSTRENGTH;
		info.m_nFresnelBumpStrength = FRESNELBUMPSTRENGTH;
		info.m_nUVScale = UVSCALE;

		info.m_nInteriorEnable = INTERIOR;
		info.m_nInteriorFogStrength = INTERIORFOGSTRENGTH;
		info.m_nInteriorBackgroundBoost = INTERIORBACKGROUNDBOOST;
		info.m_nInteriorAmbientScale = INTERIORAMBIENTSCALE;
		info.m_nInteriorBackLightScale = INTERIORBACKLIGHTSCALE;
		info.m_nInteriorRefractStrength = INTERIORREFRACTSTRENGTH;

		info.m_nFresnelParams = TRANSLUCENTFRESNELMINMAXEXP;
		info.m_nDiffuseScale = DIFFUSESCALE;
		info.m_nSpecExp = PHONGEXPONENT;
		info.m_nSpecScale = PHONGBOOST;
		info.m_nSpecExp2 = PHONGEXPONENT2;
		info.m_nSpecScale2 = PHONGBOOST2;
		info.m_nRimLightExp = RIMLIGHTEXPONENT;
		info.m_nRimLightScale = RIMLIGHTBOOST;
		info.m_nBaseColorTint = BASECOLORTINT;
		info.m_nUVProjOffset = UVPROJOFFSET;
		info.m_nBBMin = BBMIN;
		info.m_nBBMax = BBMAX;
		info.m_nFlashlightTexture = FLASHLIGHTTEXTURE;
		info.m_nFlashlightTextureFrame = FLASHLIGHTTEXTUREFRAME;
		info.m_nBumpFrame = BUMPFRAME;
		info.m_nContactShadows = CONTACTSHADOWS;
	}

	SHADER_INIT_PARAMS()
	{
		PaintBlobVars_t info;
		SetupVarsIceSurface( info );
		InitParamsPaintBlob( this, params, pMaterialName, info );
	}

	SHADER_FALLBACK
	{
		// TODO: Reasonable fallback
		if ( g_pHardwareConfig->GetDXSupportLevel() < 90 )
		{
			return "Wireframe";
		}

		return nullptr;
	}

	SHADER_INIT
	{
		PaintBlobVars_t info;
		SetupVarsIceSurface( info );
		InitPaintBlob( this, params, info );
	}

	SHADER_DRAW
	{
		PaintBlobVars_t info;
		SetupVarsIceSurface( info );
		DrawPaintBlob( this, params, pShaderAPI, pShaderShadow, info, vertexCompression );
	}

END_SHADER