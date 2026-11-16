/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2013-2018 Robert Beckebans
Copyright (C) 2022 Stephen Pridham
Copyright (C) 2026 Justin Marshall
Copyright (C) 2016 Johannes Ohlemacher (http://github.com/eXistence/fhDOOM)

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "precompiled.h"
#pragma hdrstop

#include "RenderCommon.h"

/*
Both the OpenGL and the Vulkan renderers resolve every shader through
renderprog decls under platform/renderprogs. The parts that are identical
for both backends are implemented once for the shared idRenderProgManager_Common
base in this file; each backend derives its own idRenderProgManager from it.

The decl backed managers are adapted from the Wolfenstein: The New Order
render prog manager reconstructed under references/wolfnto-reversedengineered
(renderprogmanager.h/.cpp). That material is decompiled proprietary code and
is not GPL-compatible; this is a re-implementation on this engine's idlib.
The integer based API of the legacy manager is kept so the backend code does
not change.
*/

#include "DeclRenderProg.h"
#include "RenderProgParm.h"

/*
================================================================================================
FindRenderProgDecl

Resolves a shader name with its builtin suffix to the renderprog decl that
implements it.
================================================================================================
*/
static idDeclRenderProg * FindRenderProgDecl( const char * name )
{
	idStr declName( name );
	declName.StripFileExtension();
	const idDeclRenderProg * decl = static_cast< const idDeclRenderProg * >(
		declManager->FindType( DECL_RENDERPROG, declName.c_str(), false ) );
	return const_cast< idDeclRenderProg * >( decl );
}

/*
================================================================================================
R_ReloadShaders

Drops the compiled programs; decl text is reloaded through the decl
manager (reloadDecls) and the programs compile again on the next bind.
================================================================================================
*/
static void R_ReloadShaders( const idCmdArgs &args )
{
	renderProgManager.KillAllShaders();
	renderProgManager.LoadAllShaders();
}

/*
================================================================================================
idRenderProgManager_Common::FindShader()

Resolves a shader name plus its builtin suffix to the renderprog decl of the
same name; the returned index only identifies the shader entry, the
program lives in the decl.
================================================================================================
*/
int idRenderProgManager_Common::FindShader( const char* name, rpStage_t stage, const char* nameOutSuffix, uint32 features, bool builtin, vertexLayoutType_t vertexLayout )
{
	idStr shaderName( name );
	shaderName.StripFileExtension();
	idStr declName( shaderName );
	declName += nameOutSuffix;

	idList<shaderEntry_t>& shaderList = ( stage == SHADER_STAGE_VERTEX ) ? vertexShaders : fragmentShaders;
	for( int i = 0; i < shaderList.Num(); i++ )
	{
		if( shaderList[i].name.Icmp( declName.c_str() ) == 0 )
		{
			return i;
		}
	}

	shaderEntry_t shader;
	shader.name = declName;
	shader.decl = FindRenderProgDecl( declName.c_str() );
	if( shader.decl == nullptr )
	{
		common->Warning( "no renderprog decl for %s shader %s",
						 ( stage == SHADER_STAGE_VERTEX ) ? "vertex" : "fragment", declName.c_str() );
	}
	return shaderList.Append( shader );
}

/*
================================================================================================
idRenderProgManager_Common::IsShaderBound()
================================================================================================
*/
bool idRenderProgManager_Common::IsShaderBound() const
{
	return ( currentDecl != nullptr );
}

/*
================================================================================================
idRenderProgManager_Common::SetRenderParm()
================================================================================================
*/
void idRenderProgManager_Common::SetRenderParm( renderParm_t rp, const float value[4] )
{
	SetUniformValue( rp, value );
}

/*
================================================================================================
idRenderProgManager_Common::SetRenderParms()
================================================================================================
*/
void idRenderProgManager_Common::SetRenderParms( renderParm_t rp, const float values[], int num )
{
	for( int i = 0; i < num; i++ )
	{
		SetRenderParm( ( renderParm_t )( rp + i ), values + ( i * 4 ) );
	}
}

/*
================================================================================================
idRenderProgManager_Common::GetGLSLParmName()
================================================================================================
*/
const char* idRenderProgManager_Common::GetGLSLParmName( int rp ) const
{
	assert( rp >= 0 && rp < RENDERPARM_TOTAL );
	const char * parmName = renderProgParms.GetParmNameForIndex( rp );
	return ( parmName != nullptr ) ? parmName : "<badParmIndex>";
}

/*
================================================================================================
idRenderProgManager_Common::SetUniformValue()
================================================================================================
*/
void idRenderProgManager_Common::SetUniformValue( const renderParm_t rp, const float value[4] )
{
	for( int i = 0; i < 4; i++ )
	{
		glslUniforms[rp][i] = value[i];
	}
}

/*
================================================================================================
idRenderProgManager_Common::ZeroUniforms()
================================================================================================
*/
void idRenderProgManager_Common::ZeroUniforms()
{
	memset( glslUniforms.Ptr(), 0, glslUniforms.Allocated() );
}

#if defined( USE_VULKAN )

idRenderProgManager renderProgManager;

/*
================================================================================================
idRenderProgManager::idRenderProgManager()
================================================================================================
*/
idRenderProgManager::idRenderProgManager()
{
	counter = 0;
	currentData = 0;
	currentDescSet = 0;
	currentParmBufferOffset = 0;
}

/*
================================================================================================
idRenderProgManager::~idRenderProgManager()
================================================================================================
*/
idRenderProgManager::~idRenderProgManager()
{
}

/*
================================================================================================
idRenderProgManager::Init()

Every builtin shader resolves to the renderprog decl named after the legacy
builtin table entry; the Vulkan programs (SPIR-V modules, layouts,
pipelines) are built lazily on first bind, so a decl that fails to compile
reports at the first draw that uses it instead of at startup. The decl
manager registers the renderprogs folder during its own Init(), before the
renderer initializes, so the lookups below find every decl.
================================================================================================
*/
void idRenderProgManager::Init()
{
	common->Printf( "----- Initializing Render Shaders -----\n" );

	for( int i = 0; i < MAX_BUILTINS; i++ )
	{
		builtinShaders[i] = -1;
	}

	// RB: added checks for GPU skinning
	struct builtinShaders_t
	{
		int					index;
		const char*			name;
		const char*			nameOutSuffix;
		uint32				shaderFeatures;
		bool				requireGPUSkinningSupport;
		rpStage_t			stages;
		vertexLayoutType_t	layout;
	} builtins[] =
	{
		{ BUILTIN_GUI, "builtin/gui", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_COLOR, "builtin/color", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		// RB begin
		{ BUILTIN_COLOR_SKINNED, "builtin/color", "_skinned", BIT( USE_GPU_SKINNING ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_VERTEX_COLOR, "builtin/vertex_color", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_LIGHTING, "builtin/lighting/ambient_lighting", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_LIGHTING_SKINNED, "builtin/lighting/ambient_lighting", "_skinned", BIT( USE_GPU_SKINNING ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_AMBIENT_LIGHTING_IBL, "builtin/lighting/ambient_lighting_IBL", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_LIGHTING_IBL_SKINNED, "builtin/lighting/ambient_lighting_IBL", "_skinned", BIT( USE_GPU_SKINNING ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_LIGHTING_IBL_PBR, "builtin/lighting/ambient_lighting_IBL", "_PBR", BIT( USE_PBR ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_LIGHTING_IBL_PBR_SKINNED, "builtin/lighting/ambient_lighting_IBL", "_PBR_skinned", BIT( USE_GPU_SKINNING | USE_PBR ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_AMBIENT_LIGHTGRID_IBL, "builtin/lighting/ambient_lightgrid_IBL", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_LIGHTGRID_IBL_SKINNED, "builtin/lighting/ambient_lightgrid_IBL", "_skinned", BIT( USE_GPU_SKINNING ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_LIGHTGRID_IBL_PBR, "builtin/lighting/ambient_lightgrid_IBL", "_PBR", BIT( USE_PBR ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_LIGHTGRID_IBL_PBR_SKINNED, "builtin/lighting/ambient_lightgrid_IBL", "_PBR_skinned", BIT( USE_GPU_SKINNING | USE_PBR ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_SMALL_GEOMETRY_BUFFER, "builtin/gbuffer", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_SMALL_GEOMETRY_BUFFER_SKINNED, "builtin/gbuffer", "_skinned", BIT( USE_GPU_SKINNING ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		// RB end
		{ BUILTIN_TEXTURED, "builtin/texture", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_TEXTURE_VERTEXCOLOR, "builtin/texture_color", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_TEXTURE_VERTEXCOLOR_SRGB, "builtin/texture_color", "_sRGB", BIT( USE_SRGB ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_TEXTURE_VERTEXCOLOR_SKINNED, "builtin/texture_color_skinned", "", 0, true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_TEXTURE_VERTEXCOLOR_SOFT, "builtin/texture_color", "_soft", BIT( USE_SOFT_PARTICLE ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_TEXTURE_VERTEXCOLOR_SKINNED_SOFT, "builtin/texture_color_skinned", "_soft", BIT( USE_SOFT_PARTICLE ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_TEXTURE_TEXGEN_VERTEXCOLOR, "builtin/texture_color_texgen", "",  0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		// RB begin
		{ BUILTIN_INTERACTION, "builtin/lighting/interaction", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_INTERACTION_SKINNED, "builtin/lighting/interaction", "_skinned", BIT( USE_GPU_SKINNING ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_INTERACTION_AMBIENT, "builtin/lighting/interactionAmbient", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_INTERACTION_AMBIENT_SKINNED, "builtin/lighting/interactionAmbient_skinned", "", 0, true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_INTERACTION_SHADOW_MAPPING_SPOT, "builtin/lighting/interactionSM", "_spot", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_INTERACTION_SHADOW_MAPPING_SPOT_SKINNED, "builtin/lighting/interactionSM", "_spot_skinned", BIT( USE_GPU_SKINNING ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_INTERACTION_SHADOW_MAPPING_POINT, "builtin/lighting/interactionSM", "_point", BIT( LIGHT_POINT ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_INTERACTION_SHADOW_MAPPING_POINT_SKINNED, "builtin/lighting/interactionSM", "_point_skinned", BIT( USE_GPU_SKINNING ) | BIT( LIGHT_POINT ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_INTERACTION_SHADOW_MAPPING_PARALLEL, "builtin/lighting/interactionSM", "_parallel", BIT( LIGHT_PARALLEL ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_INTERACTION_SHADOW_MAPPING_PARALLEL_SKINNED, "builtin/lighting/interactionSM", "_parallel_skinned", BIT( USE_GPU_SKINNING ) | BIT( LIGHT_PARALLEL ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		// PBR variants
		{ BUILTIN_PBR_INTERACTION, "builtin/lighting/interaction", "_PBR", BIT( USE_PBR ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_PBR_INTERACTION_SKINNED, "builtin/lighting/interaction", "_skinned_PBR", BIT( USE_GPU_SKINNING ) | BIT( USE_PBR ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_PBR_INTERACTION_AMBIENT, "builtin/lighting/interactionAmbient", "_PBR", BIT( USE_PBR ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_PBR_INTERACTION_AMBIENT_SKINNED, "builtin/lighting/interactionAmbient_skinned", "_PBR", BIT( USE_PBR ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_PBR_INTERACTION_SHADOW_MAPPING_SPOT, "builtin/lighting/interactionSM", "_spot_PBR", BIT( USE_PBR ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_PBR_INTERACTION_SHADOW_MAPPING_SPOT_SKINNED, "builtin/lighting/interactionSM", "_spot_skinned_PBR", BIT( USE_GPU_SKINNING ) | BIT( USE_PBR ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_PBR_INTERACTION_SHADOW_MAPPING_POINT, "builtin/lighting/interactionSM", "_point_PBR", BIT( LIGHT_POINT ) | BIT( USE_PBR ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_PBR_INTERACTION_SHADOW_MAPPING_POINT_SKINNED, "builtin/lighting/interactionSM", "_point_skinned_PBR", BIT( USE_GPU_SKINNING ) | BIT( LIGHT_POINT ) | BIT( USE_PBR ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_PBR_INTERACTION_SHADOW_MAPPING_PARALLEL, "builtin/lighting/interactionSM", "_parallel_PBR", BIT( LIGHT_PARALLEL ) | BIT( USE_PBR ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_PBR_INTERACTION_SHADOW_MAPPING_PARALLEL_SKINNED, "builtin/lighting/interactionSM", "_parallel_skinned_PBR", BIT( USE_GPU_SKINNING ) | BIT( LIGHT_PARALLEL ) | BIT( USE_PBR ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		// debug stuff
		{ BUILTIN_DEBUG_LIGHTGRID, "builtin/debug/lightgrid", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_DEBUG_LIGHTGRID_SKINNED, "builtin/debug/lightgrid", "_skinned", BIT( USE_GPU_SKINNING ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_DEBUG_OCTAHEDRON, "builtin/debug/octahedron", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_DEBUG_OCTAHEDRON_SKINNED, "builtin/debug/octahedron", "_skinned", BIT( USE_GPU_SKINNING ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		// RB end

		{ BUILTIN_ENVIRONMENT, "builtin/legacy/environment", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_ENVIRONMENT_SKINNED, "builtin/legacy/environment_skinned", "",  0, true , SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT},
		{ BUILTIN_BUMPY_ENVIRONMENT, "builtin/legacy/bumpyenvironment", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_BUMPY_ENVIRONMENT_SKINNED, "builtin/legacy/bumpyenvironment_skinned", "", 0, true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_DEPTH, "builtin/depth", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_DEPTH_SKINNED, "builtin/depth_skinned", "", 0, true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_BLENDLIGHT, "builtin/fog/blendlight", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_FOG, "builtin/fog/fog", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_FOG_SKINNED, "builtin/fog/fog_skinned", "", 0, true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_DIFFUSE_CUBE, "builtin/legacy/diffuseCube", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_SKYBOX, "builtin/legacy/skybox", "",  0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_WOBBLESKY, "builtin/legacy/wobblesky", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_ATMOS_SKY, "builtin/atmos/atmos_sky", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_ATMOS_SKY_CUBE, "builtin/atmos/atmos_sky_cube", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_GLASSWARP, "builtin/legacy/glassWarp", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_POSTPROCESS, "builtin/post/postprocess", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		// RB begin
		{ BUILTIN_SCREEN, "builtin/post/screen", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_TONEMAP, "builtin/post/tonemap", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_BRIGHTPASS, "builtin/post/tonemap", "_brightpass", BIT( BRIGHTPASS ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_HDR_GLARE_CHROMATIC, "builtin/post/hdr_glare_chromatic", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_HDR_DEBUG, "builtin/post/tonemap", "_debug", BIT( HDR_DEBUG ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_SMAA_EDGE_DETECTION, "builtin/post/SMAA_edge_detection", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_SMAA_BLENDING_WEIGHT_CALCULATION, "builtin/post/SMAA_blending_weight_calc", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_SMAA_NEIGHBORHOOD_BLENDING, "builtin/post/SMAA_final", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_AMBIENT_OCCLUSION, "builtin/SSAO/AmbientOcclusion_AO", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_OCCLUSION_AND_OUTPUT, "builtin/SSAO/AmbientOcclusion_AO", "_write", BIT( BRIGHTPASS ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_OCCLUSION_BLUR, "builtin/SSAO/AmbientOcclusion_blur", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_OCCLUSION_BLUR_AND_OUTPUT, "builtin/SSAO/AmbientOcclusion_blur", "_write", BIT( BRIGHTPASS ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_OCCLUSION_MINIFY, "builtin/SSAO/AmbientOcclusion_minify", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_OCCLUSION_RECONSTRUCT_CSZ, "builtin/SSAO/AmbientOcclusion_minify", "_mip0", BIT( BRIGHTPASS ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_DEEP_GBUFFER_RADIOSITY_SSGI, "builtin/SSGI/DeepGBufferRadiosity_radiosity", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_DEEP_GBUFFER_RADIOSITY_BLUR, "builtin/SSGI/DeepGBufferRadiosity_blur", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_DEEP_GBUFFER_RADIOSITY_BLUR_AND_OUTPUT, "builtin/SSGI/DeepGBufferRadiosity_blur", "_write", BIT( BRIGHTPASS ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		// RB end
		{ BUILTIN_BINK, "builtin/video/bink", "",  0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		// SRS - Added Bink shader without sRGB to linear conversion for testVideo cmd
		{ BUILTIN_BINK_SRGB, "builtin/video/bink", "_sRGB", BIT( USE_SRGB ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		// SRS end
		{ BUILTIN_BINK_GUI, "builtin/video/bink_gui", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_MOTION_BLUR, "builtin/post/motionBlur", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		// RB begin
		{ BUILTIN_DEBUG_SHADOWMAP, "builtin/debug/debug_shadowmap", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		// RB end
	};
	int numBuiltins = sizeof( builtins ) / sizeof( builtins[0] );

	for( int i = 0; i < numBuiltins; i++ )
	{
		if( builtins[i].requireGPUSkinningSupport && !glConfig.gpuSkinningAvailable )
		{
			// RB: don't try to load shaders that would break the GLSL compiler in the OpenGL driver
			continue;
		}

		// the decls carry the full builtin name plus the table suffix,
		// e.g. builtin/lighting/interactionSM + _spot
		idStr declName( builtins[i].name );
		declName.StripFileExtension();
		declName += builtins[i].nameOutSuffix;

		idDeclRenderProg * decl = FindRenderProgDecl( declName.c_str() );
		if( decl == nullptr )
		{
			common->Warning( "no renderprog decl for builtin shader %s", declName.c_str() );
		}

		shaderEntry_t shader;
		shader.name = declName;
		shader.decl = decl;
		const int vertexShaderIndex = vertexShaders.Append( shader );
		const int fragmentShaderIndex = fragmentShaders.Append( shader );

		programEntry_t program;
		program.name = declName;
		program.decl = decl;
		program.vertexShaderIndex = vertexShaderIndex;
		program.fragmentShaderIndex = fragmentShaderIndex;
		builtinShaders[builtins[i].index] = glslPrograms.Append( program );
	}

	glslUniforms.SetNum( RENDERPARM_TOTAL, vec4_zero );

	r_useHalfLambertLighting.ClearModified();
	r_useHDR.ClearModified();
	r_usePBR.ClearModified();
	r_pbrDebug.ClearModified();

	cmdSystem->AddCommand( "reloadShaders", R_ReloadShaders, CMD_FL_RENDERER, "reloads shaders" );

	counter = 0;

	// Create Vertex Descriptions
	CreateVertexDescriptions();

	// Create Descriptor Pools
	CreateDescriptorPools( descriptorPools );

	for( int i = 0; i < NUM_FRAME_DATA; ++i )
	{
		parmBuffers[ i ] = new idUniformBuffer();
		parmBuffers[ i ]->AllocBufferObject( NULL, MAX_DESC_SETS * MAX_DESC_SET_UNIFORMS * sizeof( idVec4 ), BU_DYNAMIC );
	}

	// Placeholder: mainly for optionalSkinning
	emptyUBO.AllocBufferObject( NULL, sizeof( idVec4 ), BU_DYNAMIC );
}

/*
================================================================================================
idRenderProgManager::LoadAllShaders()

The Vulkan programs upload lazily on the next bind; dropping the uploaded
state here makes every program rebuild its modules and layouts from the
current decl data.
================================================================================================
*/
void idRenderProgManager::LoadAllShaders()
{
	for( int i = 0; i < glslPrograms.Num(); ++i )
	{
		FreeDeclProgram( glslPrograms[i] );
	}
}

/*
================================================================================================
idRenderProgManager::KillAllShaders()
================================================================================================
*/
void idRenderProgManager::KillAllShaders()
{
	Unbind();
	for( int i = 0; i < glslPrograms.Num(); ++i )
	{
		FreeDeclProgram( glslPrograms[i] );
	}
}

/*
================================================================================================
idRenderProgManager::Shutdown()

Releases the uploaded programs and the per frame descriptor machinery; the
builtin decl table survives so a later Init() finds everything again.
================================================================================================
*/
void idRenderProgManager::Shutdown()
{
	KillAllShaders();

	for( int i = 0; i < NUM_FRAME_DATA; ++i )
	{
		if( parmBuffers[ i ] != NULL )
		{
			parmBuffers[ i ]->FreeBufferObject();
			delete parmBuffers[ i ];
			parmBuffers[ i ] = NULL;
		}
	}

	emptyUBO.FreeBufferObject();

	for( int i = 0; i < NUM_FRAME_DATA; ++i )
	{
		if( descriptorPools[ i ] != VK_NULL_HANDLE )
		{
			vkDestroyDescriptorPool( vkcontext.device, descriptorPools[ i ], NULL );
		}
	}

	memset( descriptorSets, 0, sizeof( descriptorSets ) );
	memset( descriptorPools, 0, sizeof( descriptorPools ) );

	counter = 0;
	currentData = 0;
	currentDescSet = 0;
}

/*
================================================================================================
idRenderProgManager::BindDecl()

Checks the decl state, uploads the Vulkan program of the current program
entry if needed, and refreshes the cached joint and skinning state.
================================================================================================
*/
void idRenderProgManager::BindDecl( idDeclRenderProg * decl )
{
	currentDecl = decl;
	currentUsesJoints = false;
	currentOptionalSkinning = false;
	if( decl == nullptr )
	{
		return;
	}
	// a purged decl has its data stripped; reparse the real source so the
	// cached decl pointer heals after a level load
	if( decl->GetState() == DS_UNPARSED )
	{
		decl->EnsureNotPurged();
	}
	// a decl that failed to parse holds the green default program; try the
	// real source once and never draw the green stub
	if( decl->GetState() == DS_DEFAULTED )
	{
		decl->RecoverFromDefaulted();
	}
	if( decl->GetState() == DS_DEFAULTED )
	{
		currentDecl = nullptr;
		return;
	}
	if( currentRenderProgram < 0 || currentRenderProgram >= glslPrograms.Num() )
	{
		currentDecl = nullptr;
		return;
	}
	programEntry_t & program = glslPrograms[ currentRenderProgram ];
	UploadDeclProgram( program );
	if( program.vertexModule == VK_NULL_HANDLE )
	{
		// the decl has no usable Vulkan program; keep no program bound so
		// the draw is skipped instead of drawing with the wrong program
		common->Warning( "renderprog %s has no Vulkan program", decl->GetName() );
		currentDecl = nullptr;
		return;
	}
	RENDERLOG_PRINTF( "Binding SPIR-V Program %s\n", decl->GetName() );
	currentUsesJoints = program.usesJoints;
	currentOptionalSkinning = program.optionalSkinning;
}

/*
================================================================================================
idRenderProgManager::Unbind()
================================================================================================
*/
void idRenderProgManager::Unbind()
{
	currentRenderProgram = -1;
	currentVertexShader = -1;
	currentFragmentShader = -1;
	currentDecl = nullptr;
	currentUsesJoints = false;
	currentOptionalSkinning = false;
}

/*
================================================================================================
idRenderProgManager::BindProgram()
================================================================================================
*/
void idRenderProgManager::BindProgram( int progIndex )
{
	if( progIndex < 0 || progIndex >= glslPrograms.Num() )
	{
		return;
	}
	if( currentRenderProgram == progIndex && currentDecl != nullptr )
	{
		return;
	}
	currentRenderProgram = progIndex;
	const programEntry_t & program = glslPrograms[progIndex];
	currentVertexShader = program.vertexShaderIndex;
	currentFragmentShader = program.fragmentShaderIndex;
	BindDecl( program.decl );
}

/*
================================================================================================
idRenderProgManager::FindGLSLProgram()
================================================================================================
*/
int idRenderProgManager::FindGLSLProgram( const char* name, int vIndex, int fIndex )
{
	for( int i = 0; i < glslPrograms.Num(); ++i )
	{
		if( glslPrograms[i].vertexShaderIndex == vIndex && glslPrograms[i].fragmentShaderIndex == fIndex )
		{
			return i;
		}
	}
	programEntry_t program;
	program.name = name;
	program.decl = nullptr;
	if( vIndex >= 0 && vIndex < vertexShaders.Num() )
	{
		program.decl = vertexShaders[vIndex].decl;
	}
	if( program.decl == nullptr && fIndex >= 0 && fIndex < fragmentShaders.Num() )
	{
		program.decl = fragmentShaders[fIndex].decl;
	}
	program.vertexShaderIndex = vIndex;
	program.fragmentShaderIndex = fIndex;
	return glslPrograms.Append( program );
}

/*
========================
RpPrintState
========================
*/
void RpPrintState( uint64 stateBits )
{

	// culling
	idLib::Printf( "Culling: " );
	switch( stateBits & GLS_CULL_BITS )
	{
		case GLS_CULL_FRONTSIDED:
			idLib::Printf( "FRONTSIDED -> BACK" );
			break;
		case GLS_CULL_BACKSIDED:
			idLib::Printf( "BACKSIDED -> FRONT" );
			break;
		case GLS_CULL_TWOSIDED:
			idLib::Printf( "TWOSIDED" );
			break;
		default:
			idLib::Printf( "NA" );
			break;
	}
	idLib::Printf( "\n" );

	// polygon mode
	idLib::Printf( "PolygonMode: %s\n", ( stateBits & GLS_POLYMODE_LINE ) ? "LINE" : "FILL" );

	// color mask
	idLib::Printf( "ColorMask: " );
	idLib::Printf( ( stateBits & GLS_REDMASK ) ? "_" : "R" );
	idLib::Printf( ( stateBits & GLS_GREENMASK ) ? "_" : "G" );
	idLib::Printf( ( stateBits & GLS_BLUEMASK ) ? "_" : "B" );
	idLib::Printf( ( stateBits & GLS_ALPHAMASK ) ? "_" : "A" );
	idLib::Printf( "\n" );

	// blend
	idLib::Printf( "Blend: src=" );
	switch( stateBits & GLS_SRCBLEND_BITS )
	{
		case GLS_SRCBLEND_ZERO:
			idLib::Printf( "ZERO" );
			break;
		case GLS_SRCBLEND_ONE:
			idLib::Printf( "ONE" );
			break;
		case GLS_SRCBLEND_DST_COLOR:
			idLib::Printf( "DST_COLOR" );
			break;
		case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
			idLib::Printf( "ONE_MINUS_DST_COLOR" );
			break;
		case GLS_SRCBLEND_SRC_ALPHA:
			idLib::Printf( "SRC_ALPHA" );
			break;
		case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
			idLib::Printf( "ONE_MINUS_SRC_ALPHA" );
			break;
		case GLS_SRCBLEND_DST_ALPHA:
			idLib::Printf( "DST_ALPHA" );
			break;
		case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
			idLib::Printf( "ONE_MINUS_DST_ALPHA" );
			break;
		default:
			idLib::Printf( "NA" );
			break;
	}
	idLib::Printf( ", dst=" );
	switch( stateBits & GLS_DSTBLEND_BITS )
	{
		case GLS_DSTBLEND_ZERO:
			idLib::Printf( "ZERO" );
			break;
		case GLS_DSTBLEND_ONE:
			idLib::Printf( "ONE" );
			break;
		case GLS_DSTBLEND_SRC_COLOR:
			idLib::Printf( "SRC_COLOR" );
			break;
		case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
			idLib::Printf( "ONE_MINUS_SRC_COLOR" );
			break;
		case GLS_DSTBLEND_SRC_ALPHA:
			idLib::Printf( "SRC_ALPHA" );
			break;
		case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
			idLib::Printf( "ONE_MINUS_SRC_ALPHA" );
			break;
		case GLS_DSTBLEND_DST_ALPHA:
			idLib::Printf( "DST_ALPHA" );
			break;
		case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
			idLib::Printf( "ONE_MINUS_DST_ALPHA" );
			break;
		default:
			idLib::Printf( "NA" );
	}
	idLib::Printf( "\n" );

	// depth func
	idLib::Printf( "DepthFunc: " );
	switch( stateBits & GLS_DEPTHFUNC_BITS )
	{
		case GLS_DEPTHFUNC_EQUAL:
			idLib::Printf( "EQUAL" );
			break;
		case GLS_DEPTHFUNC_ALWAYS:
			idLib::Printf( "ALWAYS" );
			break;
		case GLS_DEPTHFUNC_LESS:
			idLib::Printf( "LEQUAL" );
			break;
		case GLS_DEPTHFUNC_GREATER:
			idLib::Printf( "GEQUAL" );
			break;
		default:
			idLib::Printf( "NA" );
			break;
	}
	idLib::Printf( "\n" );

	// depth mask
	idLib::Printf( "DepthWrite: %s\n", ( stateBits & GLS_DEPTHMASK ) ? "FALSE" : "TRUE" );

	// depth bounds
	idLib::Printf( "DepthBounds: %s\n", ( stateBits & GLS_DEPTH_TEST_MASK ) ? "TRUE" : "FALSE" );

	// depth bias
	idLib::Printf( "DepthBias: %s\n", ( stateBits & GLS_POLYGON_OFFSET ) ? "TRUE" : "FALSE" );

	// stencil
	auto printStencil = [&]( stencilFace_t face, uint64 bits, uint64 mask, uint64 ref )
	{
		idLib::Printf( "Stencil: %s, ", ( bits & ( GLS_STENCIL_FUNC_BITS | GLS_STENCIL_OP_BITS ) ) ? "ON" : "OFF" );
		idLib::Printf( "Face=" );
		switch( face )
		{
			case STENCIL_FACE_FRONT:
				idLib::Printf( "FRONT" );
				break;
			case STENCIL_FACE_BACK:
				idLib::Printf( "BACK" );
				break;
			default:
				idLib::Printf( "BOTH" );
				break;
		}
		idLib::Printf( ", Func=" );
		switch( bits & GLS_STENCIL_FUNC_BITS )
		{
			case GLS_STENCIL_FUNC_NEVER:
				idLib::Printf( "NEVER" );
				break;
			case GLS_STENCIL_FUNC_LESS:
				idLib::Printf( "LESS" );
				break;
			case GLS_STENCIL_FUNC_EQUAL:
				idLib::Printf( "EQUAL" );
				break;
			case GLS_STENCIL_FUNC_LEQUAL:
				idLib::Printf( "LEQUAL" );
				break;
			case GLS_STENCIL_FUNC_GREATER:
				idLib::Printf( "GREATER" );
				break;
			case GLS_STENCIL_FUNC_NOTEQUAL:
				idLib::Printf( "NOTEQUAL" );
				break;
			case GLS_STENCIL_FUNC_GEQUAL:
				idLib::Printf( "GEQUAL" );
				break;
			case GLS_STENCIL_FUNC_ALWAYS:
				idLib::Printf( "ALWAYS" );
				break;
			default:
				idLib::Printf( "NA" );
				break;
		}
		idLib::Printf( ", OpFail=" );
		switch( bits & GLS_STENCIL_OP_FAIL_BITS )
		{
			case GLS_STENCIL_OP_FAIL_KEEP:
				idLib::Printf( "KEEP" );
				break;
			case GLS_STENCIL_OP_FAIL_ZERO:
				idLib::Printf( "ZERO" );
				break;
			case GLS_STENCIL_OP_FAIL_REPLACE:
				idLib::Printf( "REPLACE" );
				break;
			case GLS_STENCIL_OP_FAIL_INCR:
				idLib::Printf( "INCR" );
				break;
			case GLS_STENCIL_OP_FAIL_DECR:
				idLib::Printf( "DECR" );
				break;
			case GLS_STENCIL_OP_FAIL_INVERT:
				idLib::Printf( "INVERT" );
				break;
			case GLS_STENCIL_OP_FAIL_INCR_WRAP:
				idLib::Printf( "INCR_WRAP" );
				break;
			case GLS_STENCIL_OP_FAIL_DECR_WRAP:
				idLib::Printf( "DECR_WRAP" );
				break;
			default:
				idLib::Printf( "NA" );
				break;
		}
		idLib::Printf( ", ZFail=" );
		switch( bits & GLS_STENCIL_OP_ZFAIL_BITS )
		{
			case GLS_STENCIL_OP_ZFAIL_KEEP:
				idLib::Printf( "KEEP" );
				break;
			case GLS_STENCIL_OP_ZFAIL_ZERO:
				idLib::Printf( "ZERO" );
				break;
			case GLS_STENCIL_OP_ZFAIL_REPLACE:
				idLib::Printf( "REPLACE" );
				break;
			case GLS_STENCIL_OP_ZFAIL_INCR:
				idLib::Printf( "INCR" );
				break;
			case GLS_STENCIL_OP_ZFAIL_DECR:
				idLib::Printf( "DECR" );
				break;
			case GLS_STENCIL_OP_ZFAIL_INVERT:
				idLib::Printf( "INVERT" );
				break;
			case GLS_STENCIL_OP_ZFAIL_INCR_WRAP:
				idLib::Printf( "INCR_WRAP" );
				break;
			case GLS_STENCIL_OP_ZFAIL_DECR_WRAP:
				idLib::Printf( "DECR_WRAP" );
				break;
			default:
				idLib::Printf( "NA" );
				break;
		}
		idLib::Printf( ", OpPass=" );
		switch( bits & GLS_STENCIL_OP_PASS_BITS )
		{
			case GLS_STENCIL_OP_PASS_KEEP:
				idLib::Printf( "KEEP" );
				break;
			case GLS_STENCIL_OP_PASS_ZERO:
				idLib::Printf( "ZERO" );
				break;
			case GLS_STENCIL_OP_PASS_REPLACE:
				idLib::Printf( "REPLACE" );
				break;
			case GLS_STENCIL_OP_PASS_INCR:
				idLib::Printf( "INCR" );
				break;
			case GLS_STENCIL_OP_PASS_DECR:
				idLib::Printf( "DECR" );
				break;
			case GLS_STENCIL_OP_PASS_INVERT:
				idLib::Printf( "INVERT" );
				break;
			case GLS_STENCIL_OP_PASS_INCR_WRAP:
				idLib::Printf( "INCR_WRAP" );
				break;
			case GLS_STENCIL_OP_PASS_DECR_WRAP:
				idLib::Printf( "DECR_WRAP" );
				break;
			default:
				idLib::Printf( "NA" );
				break;
		}
		idLib::Printf( ", mask=%llu, ref=%llu\n", mask, ref );
	};

	uint32 mask = uint32( ( stateBits & GLS_STENCIL_FUNC_MASK_BITS ) >> GLS_STENCIL_FUNC_MASK_SHIFT );
	uint32 ref = uint32( ( stateBits & GLS_STENCIL_FUNC_REF_BITS ) >> GLS_STENCIL_FUNC_REF_SHIFT );
	if( stateBits & GLS_SEPARATE_STENCIL )
	{
		printStencil( STENCIL_FACE_FRONT, ( stateBits & GLS_STENCIL_FRONT_OPS ), mask, ref );
		printStencil( STENCIL_FACE_BACK, ( ( stateBits & GLS_STENCIL_BACK_OPS ) >> 12 ), mask, ref );
	}
	else
	{
		printStencil( STENCIL_FACE_NUM, stateBits, mask, ref );
	}
}

#else

idRenderProgManager renderProgManager;

/*
================================================================================================
idRenderProgManager::idRenderProgManager()
================================================================================================
*/
idRenderProgManager::idRenderProgManager()
{
}

/*
================================================================================================
idRenderProgManager::~idRenderProgManager()
================================================================================================
*/
idRenderProgManager::~idRenderProgManager()
{
}

/*
================================================================================================
idRenderProgManager::Init()

Every builtin shader resolves to the renderprog decl named after the legacy
builtin table entry; the GL programs are compiled lazily on first bind, so a
decl that fails to compile reports at the first draw that uses it instead of
at startup. The decl manager registers the renderprogs folder during its own
Init(), before the renderer initializes, so the lookups below find every
decl.
================================================================================================
*/
void idRenderProgManager::Init()
{
	common->Printf( "----- Initializing Render Shaders -----\n" );

	for( int i = 0; i < MAX_BUILTINS; i++ )
	{
		builtinShaders[i] = -1;
	}

	// RB: added checks for GPU skinning
	struct builtinShaders_t
	{
		int					index;
		const char*			name;
		const char*			nameOutSuffix;
		uint32				shaderFeatures;
		bool				requireGPUSkinningSupport;
		rpStage_t			stages;
		vertexLayoutType_t	layout;
	} builtins[] =
	{
		{ BUILTIN_GUI, "builtin/gui", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_COLOR, "builtin/color", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		// RB begin
		{ BUILTIN_COLOR_SKINNED, "builtin/color", "_skinned", BIT( USE_GPU_SKINNING ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_VERTEX_COLOR, "builtin/vertex_color", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_LIGHTING, "builtin/lighting/ambient_lighting", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_LIGHTING_SKINNED, "builtin/lighting/ambient_lighting", "_skinned", BIT( USE_GPU_SKINNING ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_AMBIENT_LIGHTING_IBL, "builtin/lighting/ambient_lighting_IBL", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_LIGHTING_IBL_SKINNED, "builtin/lighting/ambient_lighting_IBL", "_skinned", BIT( USE_GPU_SKINNING ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_LIGHTING_IBL_PBR, "builtin/lighting/ambient_lighting_IBL", "_PBR", BIT( USE_PBR ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_LIGHTING_IBL_PBR_SKINNED, "builtin/lighting/ambient_lighting_IBL", "_PBR_skinned", BIT( USE_GPU_SKINNING | USE_PBR ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_AMBIENT_LIGHTGRID_IBL, "builtin/lighting/ambient_lightgrid_IBL", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_LIGHTGRID_IBL_SKINNED, "builtin/lighting/ambient_lightgrid_IBL", "_skinned", BIT( USE_GPU_SKINNING ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_LIGHTGRID_IBL_PBR, "builtin/lighting/ambient_lightgrid_IBL", "_PBR", BIT( USE_PBR ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_LIGHTGRID_IBL_PBR_SKINNED, "builtin/lighting/ambient_lightgrid_IBL", "_PBR_skinned", BIT( USE_GPU_SKINNING | USE_PBR ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_SMALL_GEOMETRY_BUFFER, "builtin/gbuffer", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_SMALL_GEOMETRY_BUFFER_SKINNED, "builtin/gbuffer", "_skinned", BIT( USE_GPU_SKINNING ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		// RB end
		{ BUILTIN_TEXTURED, "builtin/texture", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_TEXTURE_VERTEXCOLOR, "builtin/texture_color", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_TEXTURE_VERTEXCOLOR_SRGB, "builtin/texture_color", "_sRGB", BIT( USE_SRGB ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_TEXTURE_VERTEXCOLOR_SKINNED, "builtin/texture_color_skinned", "", 0, true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_TEXTURE_VERTEXCOLOR_SOFT, "builtin/texture_color", "_soft", BIT( USE_SOFT_PARTICLE ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_TEXTURE_VERTEXCOLOR_SKINNED_SOFT, "builtin/texture_color_skinned", "_soft", BIT( USE_SOFT_PARTICLE ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_TEXTURE_TEXGEN_VERTEXCOLOR, "builtin/texture_color_texgen", "",  0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		// RB begin
		{ BUILTIN_INTERACTION, "builtin/lighting/interaction", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_INTERACTION_SKINNED, "builtin/lighting/interaction", "_skinned", BIT( USE_GPU_SKINNING ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_INTERACTION_AMBIENT, "builtin/lighting/interactionAmbient", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_INTERACTION_AMBIENT_SKINNED, "builtin/lighting/interactionAmbient_skinned", "", 0, true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_INTERACTION_SHADOW_MAPPING_SPOT, "builtin/lighting/interactionSM", "_spot", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_INTERACTION_SHADOW_MAPPING_SPOT_SKINNED, "builtin/lighting/interactionSM", "_spot_skinned", BIT( USE_GPU_SKINNING ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_INTERACTION_SHADOW_MAPPING_POINT, "builtin/lighting/interactionSM", "_point", BIT( LIGHT_POINT ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_INTERACTION_SHADOW_MAPPING_POINT_SKINNED, "builtin/lighting/interactionSM", "_point_skinned", BIT( USE_GPU_SKINNING ) | BIT( LIGHT_POINT ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_INTERACTION_SHADOW_MAPPING_PARALLEL, "builtin/lighting/interactionSM", "_parallel", BIT( LIGHT_PARALLEL ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_INTERACTION_SHADOW_MAPPING_PARALLEL_SKINNED, "builtin/lighting/interactionSM", "_parallel_skinned", BIT( USE_GPU_SKINNING ) | BIT( LIGHT_PARALLEL ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		// PBR variants
		{ BUILTIN_PBR_INTERACTION, "builtin/lighting/interaction", "_PBR", BIT( USE_PBR ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_PBR_INTERACTION_SKINNED, "builtin/lighting/interaction", "_skinned_PBR", BIT( USE_GPU_SKINNING ) | BIT( USE_PBR ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_PBR_INTERACTION_AMBIENT, "builtin/lighting/interactionAmbient", "_PBR", BIT( USE_PBR ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_PBR_INTERACTION_AMBIENT_SKINNED, "builtin/lighting/interactionAmbient_skinned", "_PBR", BIT( USE_PBR ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_PBR_INTERACTION_SHADOW_MAPPING_SPOT, "builtin/lighting/interactionSM", "_spot_PBR", BIT( USE_PBR ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_PBR_INTERACTION_SHADOW_MAPPING_SPOT_SKINNED, "builtin/lighting/interactionSM", "_spot_skinned_PBR", BIT( USE_GPU_SKINNING ) | BIT( USE_PBR ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_PBR_INTERACTION_SHADOW_MAPPING_POINT, "builtin/lighting/interactionSM", "_point_PBR", BIT( LIGHT_POINT ) | BIT( USE_PBR ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_PBR_INTERACTION_SHADOW_MAPPING_POINT_SKINNED, "builtin/lighting/interactionSM", "_point_skinned_PBR", BIT( USE_GPU_SKINNING ) | BIT( LIGHT_POINT ) | BIT( USE_PBR ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_PBR_INTERACTION_SHADOW_MAPPING_PARALLEL, "builtin/lighting/interactionSM", "_parallel_PBR", BIT( LIGHT_PARALLEL ) | BIT( USE_PBR ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_PBR_INTERACTION_SHADOW_MAPPING_PARALLEL_SKINNED, "builtin/lighting/interactionSM", "_parallel_skinned_PBR", BIT( USE_GPU_SKINNING ) | BIT( LIGHT_PARALLEL ) | BIT( USE_PBR ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		// debug stuff
		{ BUILTIN_DEBUG_LIGHTGRID, "builtin/debug/lightgrid", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_DEBUG_LIGHTGRID_SKINNED, "builtin/debug/lightgrid", "_skinned", BIT( USE_GPU_SKINNING ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_DEBUG_OCTAHEDRON, "builtin/debug/octahedron", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_DEBUG_OCTAHEDRON_SKINNED, "builtin/debug/octahedron", "_skinned", BIT( USE_GPU_SKINNING ), true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		// RB end

		{ BUILTIN_ENVIRONMENT, "builtin/legacy/environment", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_ENVIRONMENT_SKINNED, "builtin/legacy/environment_skinned", "",  0, true , SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT},
		{ BUILTIN_BUMPY_ENVIRONMENT, "builtin/legacy/bumpyenvironment", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_BUMPY_ENVIRONMENT_SKINNED, "builtin/legacy/bumpyenvironment_skinned", "", 0, true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_DEPTH, "builtin/depth", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_DEPTH_SKINNED, "builtin/depth_skinned", "", 0, true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_BLENDLIGHT, "builtin/fog/blendlight", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_FOG, "builtin/fog/fog", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_FOG_SKINNED, "builtin/fog/fog_skinned", "", 0, true, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_DIFFUSE_CUBE, "builtin/legacy/diffuseCube", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_SKYBOX, "builtin/legacy/skybox", "",  0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_WOBBLESKY, "builtin/legacy/wobblesky", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_ATMOS_SKY, "builtin/atmos/atmos_sky", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_ATMOS_SKY_CUBE, "builtin/atmos/atmos_sky_cube", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_GLASSWARP, "builtin/legacy/glassWarp", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_POSTPROCESS, "builtin/post/postprocess", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		// RB begin
		{ BUILTIN_SCREEN, "builtin/post/screen", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_TONEMAP, "builtin/post/tonemap", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_BRIGHTPASS, "builtin/post/tonemap", "_brightpass", BIT( BRIGHTPASS ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_HDR_GLARE_CHROMATIC, "builtin/post/hdr_glare_chromatic", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_HDR_DEBUG, "builtin/post/tonemap", "_debug", BIT( HDR_DEBUG ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_SMAA_EDGE_DETECTION, "builtin/post/SMAA_edge_detection", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_SMAA_BLENDING_WEIGHT_CALCULATION, "builtin/post/SMAA_blending_weight_calc", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_SMAA_NEIGHBORHOOD_BLENDING, "builtin/post/SMAA_final", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		{ BUILTIN_AMBIENT_OCCLUSION, "builtin/SSAO/AmbientOcclusion_AO", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_OCCLUSION_AND_OUTPUT, "builtin/SSAO/AmbientOcclusion_AO", "_write", BIT( BRIGHTPASS ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_OCCLUSION_BLUR, "builtin/SSAO/AmbientOcclusion_blur", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_OCCLUSION_BLUR_AND_OUTPUT, "builtin/SSAO/AmbientOcclusion_blur", "_write", BIT( BRIGHTPASS ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_OCCLUSION_MINIFY, "builtin/SSAO/AmbientOcclusion_minify", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_AMBIENT_OCCLUSION_RECONSTRUCT_CSZ, "builtin/SSAO/AmbientOcclusion_minify", "_mip0", BIT( BRIGHTPASS ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_DEEP_GBUFFER_RADIOSITY_SSGI, "builtin/SSGI/DeepGBufferRadiosity_radiosity", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_DEEP_GBUFFER_RADIOSITY_BLUR, "builtin/SSGI/DeepGBufferRadiosity_blur", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_DEEP_GBUFFER_RADIOSITY_BLUR_AND_OUTPUT, "builtin/SSGI/DeepGBufferRadiosity_blur", "_write", BIT( BRIGHTPASS ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		// RB end
		{ BUILTIN_BINK, "builtin/video/bink", "",  0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		// SRS - Added Bink shader without sRGB to linear conversion for testVideo cmd
		{ BUILTIN_BINK_SRGB, "builtin/video/bink", "_sRGB", BIT( USE_SRGB ), false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		// SRS end
		{ BUILTIN_BINK_GUI, "builtin/video/bink_gui", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		{ BUILTIN_MOTION_BLUR, "builtin/post/motionBlur", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },

		// RB begin
		{ BUILTIN_DEBUG_SHADOWMAP, "builtin/debug/debug_shadowmap", "", 0, false, SHADER_STAGE_DEFAULT, LAYOUT_DRAW_VERT },
		// RB end
	};
	int numBuiltins = sizeof( builtins ) / sizeof( builtins[0] );

	for( int i = 0; i < numBuiltins; i++ )
	{
		if( builtins[i].requireGPUSkinningSupport && !glConfig.gpuSkinningAvailable )
		{
			// RB: don't try to load shaders that would break the GLSL compiler in the OpenGL driver
			continue;
		}

		// the decls carry the full builtin name plus the table suffix,
		// e.g. builtin/lighting/interactionSM + _spot
		idStr declName( builtins[i].name );
		declName.StripFileExtension();
		declName += builtins[i].nameOutSuffix;

		idDeclRenderProg * decl = FindRenderProgDecl( declName.c_str() );
		if( decl == nullptr )
		{
			common->Warning( "no renderprog decl for builtin shader %s", declName.c_str() );
		}

		shaderEntry_t shader;
		shader.name = declName;
		shader.decl = decl;
		const int vertexShaderIndex = vertexShaders.Append( shader );
		const int fragmentShaderIndex = fragmentShaders.Append( shader );

		programEntry_t program;
		program.name = declName;
		program.decl = decl;
		program.vertexShaderIndex = vertexShaderIndex;
		program.fragmentShaderIndex = fragmentShaderIndex;
		builtinShaders[builtins[i].index] = glslPrograms.Append( program );
	}

	glslUniforms.SetNum( RENDERPARM_TOTAL, vec4_zero );

	r_useHalfLambertLighting.ClearModified();
	r_useHDR.ClearModified();
	r_usePBR.ClearModified();
	r_pbrDebug.ClearModified();

	cmdSystem->AddCommand( "reloadShaders", R_ReloadShaders, CMD_FL_RENDERER, "reloads shaders" );
}

/*
================================================================================================
idRenderProgManager::LoadAllShaders()

The GL programs are compiled lazily on the next bind, so there is nothing
to load eagerly.
================================================================================================
*/
void idRenderProgManager::LoadAllShaders()
{
}

/*
================================================================================================
idRenderProgManager::KillAllShaders()
================================================================================================
*/
void idRenderProgManager::KillAllShaders()
{
	Unbind();
	for( int i = 0; i < glslPrograms.Num(); i++ )
	{
		if( glslPrograms[i].decl != nullptr )
		{
			glslPrograms[i].decl->FreeProgram();
		}
	}
	// the decl may also be referenced through a shader entry that has no
	// program entry yet
	for( int i = 0; i < vertexShaders.Num(); i++ )
	{
		if( vertexShaders[i].decl != nullptr )
		{
			vertexShaders[i].decl->FreeProgram();
		}
	}
	for( int i = 0; i < fragmentShaders.Num(); i++ )
	{
		if( fragmentShaders[i].decl != nullptr )
		{
			fragmentShaders[i].decl->FreeProgram();
		}
	}
}

/*
================================================================================================
idRenderProgManager::Shutdown()
================================================================================================
*/
void idRenderProgManager::Shutdown()
{
	KillAllShaders();
}

/*
================================================================================================
idRenderProgManager::FindGLSLProgram()
================================================================================================
*/
int idRenderProgManager::FindGLSLProgram( const char* name, int vIndex, int fIndex )
{
	for( int i = 0; i < glslPrograms.Num(); ++i )
	{
		if( glslPrograms[i].vertexShaderIndex == vIndex && glslPrograms[i].fragmentShaderIndex == fIndex )
		{
			return i;
		}
	}
	programEntry_t program;
	program.name = name;
	program.decl = nullptr;
	if( vIndex >= 0 && vIndex < vertexShaders.Num() )
	{
		program.decl = vertexShaders[vIndex].decl;
	}
	if( program.decl == nullptr && fIndex >= 0 && fIndex < fragmentShaders.Num() )
	{
		program.decl = fragmentShaders[fIndex].decl;
	}
	program.vertexShaderIndex = vIndex;
	program.fragmentShaderIndex = fIndex;
	return glslPrograms.Append( program );
}

/*
================================================================================================
idRenderProgManager::BindDecl()

Uploads the decl if needed, binds its GL program, and refreshes the cached
joint and skinning state of the current program.
================================================================================================
*/
void idRenderProgManager::BindDecl( idDeclRenderProg * decl )
{
	currentDecl = decl;
	currentUsesJoints = false;
	currentOptionalSkinning = false;
	if( decl == nullptr )
	{
		glUseProgram( 0 );
		return;
	}
	// a purged decl has its data stripped; reparse the real source so the
	// cached decl pointer heals after a level load
	if( decl->GetState() == DS_UNPARSED )
	{
		decl->EnsureNotPurged();
	}
	// a decl that failed to parse holds the green default program; try the
	// real source once and never draw the green stub
	if( decl->GetState() == DS_DEFAULTED )
	{
		decl->RecoverFromDefaulted();
	}
	if( decl->GetState() == DS_DEFAULTED )
	{
		glUseProgram( 0 );
		currentDecl = nullptr;
		return;
	}
	decl->EnsureUploaded();
	if( !decl->IsUploaded() )
	{
		// the decl has no generated code to upload; keep no program bound so
		// the draw is skipped instead of drawing with the wrong program
		common->Warning( "renderprog %s has no GL program", decl->GetName() );
		glUseProgram( 0 );
		currentDecl = nullptr;
		return;
	}
	RENDERLOG_PRINTF( "Binding GLSL Program %s\n", decl->GetName() );
	glUseProgram( decl->GetProgram() );
	currentUsesJoints = decl->UsesJoints();
	// optional skinning means the vertex program branches on the
	// rpEnableSkinning parm; only the heat haze shaders do
	for( int i = 0; i < decl->data.vertexParms.Num(); i++ )
	{
		if( decl->data.vertexParms[i] == RENDERPARM_ENABLE_SKINNING )
		{
			currentOptionalSkinning = true;
			break;
		}
	}
}

/*
================================================================================================
idRenderProgManager::BindProgram()
================================================================================================
*/
void idRenderProgManager::BindProgram( int progIndex )
{
	if( progIndex < 0 || progIndex >= glslPrograms.Num() )
	{
		return;
	}
	if( currentRenderProgram == progIndex && currentDecl != nullptr )
	{
		return;
	}
	currentRenderProgram = progIndex;
	const programEntry_t & program = glslPrograms[progIndex];
	currentVertexShader = program.vertexShaderIndex;
	currentFragmentShader = program.fragmentShaderIndex;
	BindDecl( program.decl );
}

/*
================================================================================================
idRenderProgManager::Unbind()
================================================================================================
*/
void idRenderProgManager::Unbind()
{
	currentRenderProgram = -1;
	currentVertexShader = -1;
	currentFragmentShader = -1;
	currentDecl = nullptr;
	currentUsesJoints = false;
	currentOptionalSkinning = false;

	glUseProgram( 0 );
}

/*
================================================================================================
idRenderProgManager::CommitUniforms()

The decl stores the GL uniform location + 1 for every parm of its vertex
and fragment programs; a location of 0 means the compiled program does not
use that parm. The render parm indices of the registry match the
renderParm_t values, so the uniform values are looked up directly.
================================================================================================
*/
void idRenderProgManager::CommitUniforms( uint64 stateBits )
{
	const idDeclRenderProg * decl = currentDecl;
	if( decl == nullptr )
	{
		return;
	}
	const renderProgData_t & data = decl->data;
	for( int index = 0; index < data.vertexParms.Num(); index++ )
	{
		const int location = decl->GetVertexUniformLocation( index );
		if( location != 0 )
		{
			const renderParm_t parm = ( renderParm_t )data.vertexParms[index];
			if( parm == RENDERPARM_SHADOW_MATRIX_0_X )
			{
				// RB: HACK rpShadowMatrices[6 * 4]; the 24 matrix rows sit
				// consecutively in the uniform values, so one call commits
				// the whole array
				glUniform4fv( location - 1, 6 * 4, glslUniforms[parm].ToFloatPtr() );
			}
			else
			{
				glUniform4fv( location - 1, 1, glslUniforms[parm].ToFloatPtr() );
			}
		}
	}
	for( int index = 0; index < data.fragmentParms.Num(); index++ )
	{
		const int location = decl->GetFragmentUniformLocation( index );
		if( location != 0 )
		{
			const renderParm_t parm = ( renderParm_t )data.fragmentParms[index];
			if( parm == RENDERPARM_SHADOW_MATRIX_0_X )
			{
				glUniform4fv( location - 1, 6 * 4, glslUniforms[parm].ToFloatPtr() );
			}
			else
			{
				glUniform4fv( location - 1, 1, glslUniforms[parm].ToFloatPtr() );
			}
		}
	}
}

#endif // !USE_VULKAN
