/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2013-2018 Robert Beckebans
Copyright (C) 2016-2017 Dustin Land

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

#include "../RenderCommon.h"
#include "../RenderProgs.h"


void RpPrintState( uint64 stateBits );

struct vertexLayout_t
{
	VkPipelineVertexInputStateCreateInfo inputState;
	idList< VkVertexInputBindingDescription > bindingDesc;
	idList< VkVertexInputAttributeDescription > attributeDesc;
};

idUniformBuffer emptyUBO;

static vertexLayout_t vertexLayouts[ NUM_VERTEX_LAYOUTS ];

/*
=============
CreateVertexDescriptions
=============
*/
void CreateVertexDescriptions()
{
	VkPipelineVertexInputStateCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	createInfo.pNext = NULL;
	createInfo.flags = 0;

	VkVertexInputBindingDescription binding = {};
	VkVertexInputAttributeDescription attribute = {};

	{
		vertexLayout_t& layout = vertexLayouts[ LAYOUT_DRAW_VERT ];
		layout.inputState = createInfo;

		uint32 locationNo = 0;
		uint32 offset = 0;

		binding.stride = sizeof( idDrawVert );
		binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		layout.bindingDesc.Append( binding );

		// Position
		attribute.format = VK_FORMAT_R32G32B32_SFLOAT;
		attribute.location = locationNo++;
		attribute.offset = offset;
		layout.attributeDesc.Append( attribute );
		offset += sizeof( idDrawVert::xyz );

		// TexCoord
		attribute.format = VK_FORMAT_R16G16_SFLOAT;
		attribute.location = locationNo++;
		attribute.offset = offset;
		layout.attributeDesc.Append( attribute );
		offset += sizeof( idDrawVert::st );

		// Normal
		attribute.format = VK_FORMAT_R8G8B8A8_UNORM;
		attribute.location = locationNo++;
		attribute.offset = offset;
		layout.attributeDesc.Append( attribute );
		offset += sizeof( idDrawVert::normal );

		// Tangent
		attribute.format = VK_FORMAT_R8G8B8A8_UNORM;
		attribute.location = locationNo++;
		attribute.offset = offset;
		layout.attributeDesc.Append( attribute );
		offset += sizeof( idDrawVert::tangent );

		// Color1
		attribute.format = VK_FORMAT_R8G8B8A8_UNORM;
		attribute.location = locationNo++;
		attribute.offset = offset;
		layout.attributeDesc.Append( attribute );
		offset += sizeof( idDrawVert::color );

		// Color2
		attribute.format = VK_FORMAT_R8G8B8A8_UNORM;
		attribute.location = locationNo++;
		attribute.offset = offset;
		layout.attributeDesc.Append( attribute );
	}
}

/*
========================
CreateDescriptorPools
========================
*/
void CreateDescriptorPools( VkDescriptorPool( &pools )[ NUM_FRAME_DATA ] )
{
	const int numPools = 2;
	VkDescriptorPoolSize poolSizes[ numPools ];
	poolSizes[ 0 ].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[ 0 ].descriptorCount = MAX_DESC_UNIFORM_BUFFERS;

	poolSizes[ 1 ].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[ 1 ].descriptorCount = MAX_DESC_IMAGE_SAMPLERS;

	VkDescriptorPoolCreateInfo poolCreateInfo = {};
	poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolCreateInfo.pNext = NULL;
	poolCreateInfo.maxSets = MAX_DESC_SETS;
	poolCreateInfo.poolSizeCount = numPools;
	poolCreateInfo.pPoolSizes = poolSizes;

	for( int i = 0; i < NUM_FRAME_DATA; ++i )
	{
		ID_VK_CHECK( vkCreateDescriptorPool( vkcontext.device, &poolCreateInfo, NULL, &pools[ i ] ) );
	}
}

/*
========================
GetDescriptorType
========================
*/
static VkDescriptorType GetDescriptorType( rpBinding_t type )
{
	switch( type )
	{
		case BINDING_TYPE_UNIFORM_BUFFER:
			return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

		case BINDING_TYPE_SAMPLER:
			return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

		default:
			idLib::Error( "Unknown rpBinding_t %d", static_cast< int >( type ) );
			return VK_DESCRIPTOR_TYPE_MAX_ENUM;
	}
}


/*
========================
ScanForParmUses

Appends every render parm the line references that is not tracked yet;
global.inc helper functions read parms the shader body never names directly.
========================
*/
static void ScanForParmUses( const char * p, idList<int>& parmIndices )
{
	while( *p != '\0' )
	{
		if( p[0] == 'r' && p[1] == 'p' && p[2] >= 'A' && p[2] <= 'Z' )
		{
			const char * start = p;
			p += 3;
			while( ( *p >= 'a' && *p <= 'z' ) || ( *p >= 'A' && *p <= 'Z' ) || ( *p >= '0' && *p <= '9' ) || *p == '_' )
			{
				p++;
			}
			idStr name( start );
			name.CapLength( ( int )( p - start ) );
			const idRenderProgParm * parm = renderProgParms.FindParmByName( name.c_str() );
			if( parm != nullptr )
			{
				const int parmIndex = parm->GetIndex();
				bool found = false;
				for( int i = 0; i < parmIndices.Num(); ++i )
				{
					if( parmIndices[i] == parmIndex )
					{
						found = true;
						break;
					}
				}
				if( !found )
				{
					parmIndices.Append( parmIndex );
				}
			}
		}
		else
		{
			p++;
		}
	}
}

/*
========================
ConvertDeclGLSLToVulkan

The decl generator emits plain GLSL with one individual uniform per render
parm (from global.inc) and unbound sampler declarations. Vulkan needs the
parms in uniform blocks and explicit bindings, so the individual uniform
declarations are stripped and one uniform block per stage is emitted right
after the #version line, the joint matrix block of skinned programs gets its
binding, and every sampler gets the next free binding. The binding
assignment is fixed: binding 0 carries the vertex parm block, binding 1 the
joint matrix block, the fragment parm block follows the vertex bindings, and
the samplers fill the rest.
========================
*/
static idStr ConvertDeclGLSLToVulkan( const idDeclRenderProg * decl, bool isVertexProgram,
									  int uboBinding, int jointBinding, int firstSamplerBinding,
									  idList<int>& parmIndices, idList<rpBinding_t>& samplerBindings,
									  bool& jointBlockFound )
{
	parmIndices.Clear();
	samplerBindings.Clear();
	jointBlockFound = false;

	// start with the decl's parm list; parms that only global.inc helpers
	// reference are appended while scanning the source
	const idStr & source = isVertexProgram ? decl->data.vertexCode : decl->data.fragmentCode;
	const idStaticList<short, 42> & declParms = isVertexProgram ? decl->data.vertexParms : decl->data.fragmentParms;
	for( int i = 0; i < declParms.Num(); ++i )
	{
		parmIndices.Append( declParms[i] );
	}

	idStr out;
	int samplerBinding = firstSamplerBinding;
	const char * p = source.c_str();
	while( *p != '\0' )
	{
		const char * lineEnd = strchr( p, '\n' );
		const int lineLength = ( lineEnd != NULL ) ? ( int )( lineEnd - p ) : ( int )strlen( p );

		idStr line;
		line.Append( p, lineLength );

		const char * s = line.c_str();
		while( *s == '\t' || *s == ' ' )
		{
			s++;
		}

		bool dropped = false;
		if( strncmp( s, "uniform ", 8 ) == 0 )
		{
			if( strncmp( s + 8, "vec4 ", 5 ) == 0 )
			{
				// an individual render parm uniform from global.inc or an
				// old-style body; it moves into the stage uniform block
				idStr name( s + 13 );
				int nameEnd = 0;
				while( ( name[nameEnd] >= 'a' && name[nameEnd] <= 'z' ) ||
					   ( name[nameEnd] >= 'A' && name[nameEnd] <= 'Z' ) ||
					   ( name[nameEnd] >= '0' && name[nameEnd] <= '9' ) ||
					   name[nameEnd] == '_' )
				{
					nameEnd++;
				}
				name.CapLength( nameEnd );
				const idRenderProgParm * parm = renderProgParms.FindParmByName( name.c_str() );
				if( parm != nullptr )
				{
					const int parmIndex = parm->GetIndex();
					bool found = false;
					for( int i = 0; i < parmIndices.Num(); ++i )
					{
						if( parmIndices[i] == parmIndex )
						{
							found = true;
							break;
						}
					}
					if( !found )
					{
						parmIndices.Append( parmIndex );
					}
					dropped = true; // the uniform block declares it
				}
			}
			else if( strstr( s, "matrices_ubo" ) != NULL )
			{
				// the joint matrix block of skinned programs; it binds like
				// the legacy UBO_MAT at binding 1
				line.Replace( "uniform matrices_ubo", va( "layout( binding = %i ) uniform matrices_ubo", jointBinding ) );
				jointBlockFound = true;
			}
			else if( strncmp( s + 8, "sampler", 7 ) == 0 )
			{
				line.Replace( "uniform", va( "layout( binding = %i ) uniform", samplerBinding ) );
				samplerBindings.Append( BINDING_TYPE_SAMPLER );
				samplerBinding++;
			}
		}
		if( !dropped )
		{
			ScanForParmUses( s, parmIndices );
			out += line;
			out += "\n";
		}

		if( lineEnd == NULL )
		{
			break;
		}
		p = lineEnd + 1;
	}

	// emit the stage uniform block right after the #version line so it
	// precedes every use
	if( parmIndices.Num() > 0 )
	{
		idStr uboBlock;
		uboBlock += "\n";
		uboBlock += va( "layout( binding = %i ) uniform UBO%c {\n", uboBinding, isVertexProgram ? 'V' : 'F' );
		for( int i = 0; i < parmIndices.Num(); ++i )
		{
			if( parmIndices[i] == RENDERPARM_SHADOW_MATRIX_0_X )
			{
				// the 24 matrix rows sit consecutively in the uniform values
				uboBlock += "\tvec4 rpShadowMatrices[24];\n";
			}
			else
			{
				const char * parmName = renderProgParms.GetParmNameForIndex( parmIndices[i] );
				uboBlock += "\tvec4 ";
				uboBlock += ( parmName != NULL ) ? parmName : "<badParmName>";
				uboBlock += ";\n";
			}
		}
		uboBlock += "};\n";

		const int versionPos = out.FindText( "#version" );
		if( versionPos != -1 )
		{
			const int lineEndPos = out.FindChar( '\n', versionPos );
			out.Insert( uboBlock.c_str(), ( lineEndPos != -1 ) ? lineEndPos + 1 : out.Length() );
		}
		else
		{
			out.Insert( uboBlock.c_str(), 0 );
		}
	}

	return out;
}

/*
========================
idRenderProgManager::StartFrame
========================
*/
void idRenderProgManager::StartFrame()
{
	counter++;
	currentData = counter % NUM_FRAME_DATA;
	currentDescSet = 0;
	currentParmBufferOffset = 0;

	vkResetDescriptorPool( vkcontext.device, descriptorPools[ currentData ], 0 );
}

/*
================================================================================================
CompileGLSLtoSPIRV
================================================================================================
*/

#include <shaderc/shaderc.hpp>

static int CompileGLSLtoSPIRV( const char* filename, const idStr& dataGLSL, const rpStage_t stage, uint32** spirvBuffer )
{
	shaderc::Compiler compiler;
	shaderc::CompileOptions options;

	// Like -DMY_DEFINE=1
	//options.AddMacroDefinition("MY_DEFINE", "1");

	//if (optimize)
	{
		options.SetOptimizationLevel( shaderc_optimization_level_performance );
	}

	shaderc_shader_kind shaderKind;
	if( stage == SHADER_STAGE_VERTEX )
	{
		shaderKind = shaderc_glsl_vertex_shader;
	}
	else if( stage == SHADER_STAGE_COMPUTE )
	{
		shaderKind = shaderc_glsl_compute_shader;
	}
	else
	{
		shaderKind = shaderc_glsl_fragment_shader;
	}

	std::string source = dataGLSL.c_str();

	shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv( source, shaderKind, filename, options );

	if( module.GetCompilationStatus() != shaderc_compilation_status_success )
	{
		idLib::Printf( "Comping GLSL to SPIR-V using shaderc failed for: %s\n", filename );
		idLib::Printf( "%s\n", module.GetErrorMessage().c_str() );
		return 0;
	}

	std::vector<uint32_t> spirV = { module.cbegin(), module.cend() };

	// copy to spirvBuffer
	int32 spirvLen = spirV.size() * sizeof( uint32_t );

	void* buffer = Mem_Alloc( spirvLen, TAG_RENDERPROG );
	memcpy( buffer, spirV.data(), spirvLen );

	*spirvBuffer = ( uint32_t* ) buffer;
	return spirvLen;


}

/*
================================================================================================
idRenderProgManager::UploadDeclProgram

Converts the decl's generated GLSL to UBO based Vulkan GLSL, compiles it to
SPIR-V with shaderc, and creates the shader modules plus the descriptor set
and pipeline layouts. Programs upload lazily on the first bind and rebuild
whenever the decl data changes.
================================================================================================
*/
void idRenderProgManager::UploadDeclProgram( programEntry_t & program )
{
	idDeclRenderProg * decl = program.decl;
	if( decl == nullptr )
	{
		return;
	}
	if( program.vertexModule != VK_NULL_HANDLE && program.declCheckSum == decl->data.compoundCheckSum )
	{
		return; // already uploaded from the current decl data
	}

	FreeDeclProgram( program );

	if( decl->data.vertexCode.IsEmpty() || decl->data.fragmentCode.IsEmpty() )
	{
		common->Warning( "renderprog %s has no generated shader code to upload", decl->GetName() );
		return;
	}

	const bool usesJoints = decl->UsesJoints();

	idList<int> vertexParmIndices;
	idList<rpBinding_t> vertexSamplerBindings;
	bool vertexJointBlock = false;
	idStr vertexGLSL = ConvertDeclGLSLToVulkan( decl, true, 0, 1, usesJoints ? 2 : 1,
												vertexParmIndices, vertexSamplerBindings, vertexJointBlock );

	// the fragment bindings start after the vertex bindings
	const int vertexBindingCount = ( vertexParmIndices.Num() > 0 ? 1 : 0 ) + ( vertexJointBlock ? 1 : 0 ) + vertexSamplerBindings.Num();
	const int fragmentUBOBinding = vertexBindingCount;
	idList<int> fragmentParmIndices;
	idList<rpBinding_t> fragmentSamplerBindings;
	bool fragmentJointBlock = false;
	idStr fragmentGLSL = ConvertDeclGLSLToVulkan( decl, false, fragmentUBOBinding, 1, fragmentUBOBinding + 1,
												  fragmentParmIndices, fragmentSamplerBindings, fragmentJointBlock );

	// compile to SPIR-V and create the shader modules
	uint32* vertexSPIRV = NULL;
	const int vertexSPIRVLen = CompileGLSLtoSPIRV( va( "renderprog:%s_vertex", decl->GetName() ), vertexGLSL, SHADER_STAGE_VERTEX, &vertexSPIRV );
	uint32* fragmentSPIRV = NULL;
	const int fragmentSPIRVLen = CompileGLSLtoSPIRV( va( "renderprog:%s_fragment", decl->GetName() ), fragmentGLSL, SHADER_STAGE_FRAGMENT, &fragmentSPIRV );
	if( vertexSPIRVLen <= 0 || fragmentSPIRVLen <= 0 || vertexSPIRV == NULL || fragmentSPIRV == NULL )
	{
		if( vertexSPIRV != NULL )
		{
			Mem_Free( vertexSPIRV );
		}
		if( fragmentSPIRV != NULL )
		{
			Mem_Free( fragmentSPIRV );
		}
		common->Warning( "Failed to compile render prog %s from %s to SPIR-V", decl->GetName(), decl->GetFileName() );
		return;
	}

	VkShaderModuleCreateInfo moduleCreateInfo = {};
	moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	moduleCreateInfo.codeSize = vertexSPIRVLen;
	moduleCreateInfo.pCode = vertexSPIRV;
	ID_VK_CHECK( vkCreateShaderModule( vkcontext.device, &moduleCreateInfo, NULL, &program.vertexModule ) );
	moduleCreateInfo.codeSize = fragmentSPIRVLen;
	moduleCreateInfo.pCode = fragmentSPIRV;
	ID_VK_CHECK( vkCreateShaderModule( vkcontext.device, &moduleCreateInfo, NULL, &program.fragmentModule ) );
	Mem_Free( vertexSPIRV );
	Mem_Free( fragmentSPIRV );

	// descriptor set layout: the binding ids are assigned in list order and
	// match the layout qualifiers of the converted GLSL
	idList< VkDescriptorSetLayoutBinding > layoutBindings;
	VkDescriptorSetLayoutBinding binding = {};
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	if( vertexParmIndices.Num() > 0 )
	{
		binding.binding = 0;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		layoutBindings.Append( binding );
		program.bindings.Append( BINDING_TYPE_UNIFORM_BUFFER );
	}
	if( vertexJointBlock )
	{
		binding.binding = 1;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		layoutBindings.Append( binding );
		program.bindings.Append( BINDING_TYPE_UNIFORM_BUFFER );
	}
	for( int i = 0; i < vertexSamplerBindings.Num(); ++i )
	{
		binding.binding = ( usesJoints ? 2 : 1 ) + i;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		layoutBindings.Append( binding );
		program.bindings.Append( BINDING_TYPE_SAMPLER );
	}

	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	if( fragmentParmIndices.Num() > 0 )
	{
		binding.binding = fragmentUBOBinding;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		layoutBindings.Append( binding );
		program.bindings.Append( BINDING_TYPE_UNIFORM_BUFFER );
	}
	for( int i = 0; i < fragmentSamplerBindings.Num(); ++i )
	{
		binding.binding = fragmentUBOBinding + 1 + i;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		layoutBindings.Append( binding );
		program.bindings.Append( BINDING_TYPE_SAMPLER );
	}

	VkDescriptorSetLayoutCreateInfo layoutCreateInfo = {};
	layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutCreateInfo.bindingCount = layoutBindings.Num();
	layoutCreateInfo.pBindings = layoutBindings.Ptr();
	ID_VK_CHECK( vkCreateDescriptorSetLayout( vkcontext.device, &layoutCreateInfo, NULL, &program.descriptorSetLayout ) );

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
	pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCreateInfo.setLayoutCount = 1;
	pipelineLayoutCreateInfo.pSetLayouts = &program.descriptorSetLayout;
	ID_VK_CHECK( vkCreatePipelineLayout( vkcontext.device, &pipelineLayoutCreateInfo, NULL, &program.pipelineLayout ) );

	program.vertexParmIndices = vertexParmIndices;
	program.fragmentParmIndices = fragmentParmIndices;
	program.usesJoints = vertexJointBlock;
	program.optionalSkinning = false;
	for( int i = 0; i < program.vertexParmIndices.Num(); ++i )
	{
		if( program.vertexParmIndices[i] == RENDERPARM_ENABLE_SKINNING )
		{
			program.optionalSkinning = true;
			break;
		}
	}
	program.declCheckSum = decl->data.compoundCheckSum;
}

/*
================================================================================================
idRenderProgManager::FreeDeclProgram

Releases the uploaded Vulkan state of one program; the entry and its decl
mapping stay alive so the program uploads again on the next bind.
================================================================================================
*/
void idRenderProgManager::FreeDeclProgram( programEntry_t & program )
{
	for( int i = 0; i < program.pipelines.Num(); ++i )
	{
		vkDestroyPipeline( vkcontext.device, program.pipelines[i].pipeline, NULL );
	}
	program.pipelines.Clear();
	if( program.pipelineLayout != VK_NULL_HANDLE )
	{
		vkDestroyPipelineLayout( vkcontext.device, program.pipelineLayout, NULL );
		program.pipelineLayout = VK_NULL_HANDLE;
	}
	if( program.descriptorSetLayout != VK_NULL_HANDLE )
	{
		vkDestroyDescriptorSetLayout( vkcontext.device, program.descriptorSetLayout, NULL );
		program.descriptorSetLayout = VK_NULL_HANDLE;
	}
	if( program.vertexModule != VK_NULL_HANDLE )
	{
		vkDestroyShaderModule( vkcontext.device, program.vertexModule, NULL );
		program.vertexModule = VK_NULL_HANDLE;
	}
	if( program.fragmentModule != VK_NULL_HANDLE )
	{
		vkDestroyShaderModule( vkcontext.device, program.fragmentModule, NULL );
		program.fragmentModule = VK_NULL_HANDLE;
	}
	program.bindings.Clear();
	program.vertexParmIndices.Clear();
	program.fragmentParmIndices.Clear();
	program.usesJoints = false;
	program.optionalSkinning = false;
	program.declCheckSum = 0;
}

/*
================================================================================================
idRenderProgManager::AllocParmBlockBuffer

Suballocates the uniform block values of one stage from the frame parm
buffer in the order of the parm list; the shadow matrix parm covers the 24
consecutive rows of rpShadowMatrices, which the legacy parm block buffer
never supported.
================================================================================================
*/
void idRenderProgManager::AllocParmBlockBuffer( const idList<int>& parmIndices, idUniformBuffer& ubo )
{
	int numVec4s = 0;
	for( int i = 0; i < parmIndices.Num(); ++i )
	{
		numVec4s += ( parmIndices[i] == RENDERPARM_SHADOW_MATRIX_0_X ) ? 6 * 4 : 1;
	}

	const int bytes = ALIGN( numVec4s * sizeof( idVec4 ), vkcontext.gpu->props.limits.minUniformBufferOffsetAlignment );

	ubo.Reference( *parmBuffers[ currentData ], currentParmBufferOffset, bytes );

	idVec4* gpuUniforms = ( idVec4* )ubo.MapBuffer( BM_WRITE );

	int slot = 0;
	for( int i = 0; i < parmIndices.Num(); ++i )
	{
		if( parmIndices[i] == RENDERPARM_SHADOW_MATRIX_0_X )
		{
			for( int j = 0; j < 6 * 4; j++ )
			{
				gpuUniforms[ slot++ ] = glslUniforms[ RENDERPARM_SHADOW_MATRIX_0_X + j ];
			}
		}
		else
		{
			gpuUniforms[ slot++ ] = glslUniforms[ parmIndices[i] ];
		}
	}

	ubo.UnmapBuffer();

	currentParmBufferOffset += bytes;
}

/*
========================
GetStencilOpState
========================
*/
static VkStencilOpState GetStencilOpState( uint64 stencilBits )
{
	VkStencilOpState state = {};

	switch( stencilBits & GLS_STENCIL_OP_FAIL_BITS )
	{
		case GLS_STENCIL_OP_FAIL_KEEP:
			state.failOp = VK_STENCIL_OP_KEEP;
			break;
		case GLS_STENCIL_OP_FAIL_ZERO:
			state.failOp = VK_STENCIL_OP_ZERO;
			break;
		case GLS_STENCIL_OP_FAIL_REPLACE:
			state.failOp = VK_STENCIL_OP_REPLACE;
			break;
		case GLS_STENCIL_OP_FAIL_INCR:
			state.failOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
			break;
		case GLS_STENCIL_OP_FAIL_DECR:
			state.failOp = VK_STENCIL_OP_DECREMENT_AND_CLAMP;
			break;
		case GLS_STENCIL_OP_FAIL_INVERT:
			state.failOp = VK_STENCIL_OP_INVERT;
			break;
		case GLS_STENCIL_OP_FAIL_INCR_WRAP:
			state.failOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;
			break;
		case GLS_STENCIL_OP_FAIL_DECR_WRAP:
			state.failOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;
			break;
	}
	switch( stencilBits & GLS_STENCIL_OP_ZFAIL_BITS )
	{
		case GLS_STENCIL_OP_ZFAIL_KEEP:
			state.depthFailOp = VK_STENCIL_OP_KEEP;
			break;
		case GLS_STENCIL_OP_ZFAIL_ZERO:
			state.depthFailOp = VK_STENCIL_OP_ZERO;
			break;
		case GLS_STENCIL_OP_ZFAIL_REPLACE:
			state.depthFailOp = VK_STENCIL_OP_REPLACE;
			break;
		case GLS_STENCIL_OP_ZFAIL_INCR:
			state.depthFailOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
			break;
		case GLS_STENCIL_OP_ZFAIL_DECR:
			state.depthFailOp = VK_STENCIL_OP_DECREMENT_AND_CLAMP;
			break;
		case GLS_STENCIL_OP_ZFAIL_INVERT:
			state.depthFailOp = VK_STENCIL_OP_INVERT;
			break;
		case GLS_STENCIL_OP_ZFAIL_INCR_WRAP:
			state.depthFailOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;
			break;
		case GLS_STENCIL_OP_ZFAIL_DECR_WRAP:
			state.depthFailOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;
			break;
	}
	switch( stencilBits & GLS_STENCIL_OP_PASS_BITS )
	{
		case GLS_STENCIL_OP_PASS_KEEP:
			state.passOp = VK_STENCIL_OP_KEEP;
			break;
		case GLS_STENCIL_OP_PASS_ZERO:
			state.passOp = VK_STENCIL_OP_ZERO;
			break;
		case GLS_STENCIL_OP_PASS_REPLACE:
			state.passOp = VK_STENCIL_OP_REPLACE;
			break;
		case GLS_STENCIL_OP_PASS_INCR:
			state.passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
			break;
		case GLS_STENCIL_OP_PASS_DECR:
			state.passOp = VK_STENCIL_OP_DECREMENT_AND_CLAMP;
			break;
		case GLS_STENCIL_OP_PASS_INVERT:
			state.passOp = VK_STENCIL_OP_INVERT;
			break;
		case GLS_STENCIL_OP_PASS_INCR_WRAP:
			state.passOp = VK_STENCIL_OP_INCREMENT_AND_WRAP;
			break;
		case GLS_STENCIL_OP_PASS_DECR_WRAP:
			state.passOp = VK_STENCIL_OP_DECREMENT_AND_WRAP;
			break;
	}

	return state;
}

/*
========================
CreateGraphicsPipeline
========================
*/
static VkPipeline CreateGraphicsPipeline(
	vertexLayoutType_t vertexLayoutType,
	VkShaderModule vertexShader,
	VkShaderModule fragmentShader,
	VkPipelineLayout pipelineLayout,
	uint64 stateBits )
{

	// Pipeline
	vertexLayout_t& vertexLayout = vertexLayouts[ vertexLayoutType ];

	// Vertex Input
	VkPipelineVertexInputStateCreateInfo vertexInputState = vertexLayout.inputState;
	vertexInputState.vertexBindingDescriptionCount = vertexLayout.bindingDesc.Num();
	vertexInputState.pVertexBindingDescriptions = vertexLayout.bindingDesc.Ptr();
	vertexInputState.vertexAttributeDescriptionCount = vertexLayout.attributeDesc.Num();
	vertexInputState.pVertexAttributeDescriptions = vertexLayout.attributeDesc.Ptr();

	// Input Assembly
	VkPipelineInputAssemblyStateCreateInfo assemblyInputState = {};
	assemblyInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assemblyInputState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	// Rasterization
	VkPipelineRasterizationStateCreateInfo rasterizationState = {};
	rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizationState.rasterizerDiscardEnable = VK_FALSE;
	rasterizationState.depthBiasEnable = ( stateBits & GLS_POLYGON_OFFSET ) != 0;
	rasterizationState.depthClampEnable = VK_FALSE;
	rasterizationState.frontFace = ( stateBits & GLS_CLOCKWISE ) ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizationState.lineWidth = 1.0f;
	rasterizationState.polygonMode = ( stateBits & GLS_POLYMODE_LINE ) ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;

	switch( stateBits & GLS_CULL_BITS )
	{
		case GLS_CULL_TWOSIDED:
			rasterizationState.cullMode = VK_CULL_MODE_NONE;
			break;

		case GLS_CULL_BACKSIDED:
			if( stateBits & GLS_MIRROR_VIEW )
			{
				rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
			}
			else
			{
				rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
			}
			break;

		case GLS_CULL_FRONTSIDED:
		default:
			if( stateBits & GLS_MIRROR_VIEW )
			{
				rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
			}
			else
			{
				rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
			}
			break;
	}

	// Color Blend Attachment
	VkPipelineColorBlendAttachmentState attachmentState = {};
	{
		VkBlendFactor srcFactor = VK_BLEND_FACTOR_ONE;
		switch( stateBits & GLS_SRCBLEND_BITS )
		{
			case GLS_SRCBLEND_ZERO:
				srcFactor = VK_BLEND_FACTOR_ZERO;
				break;
			case GLS_SRCBLEND_ONE:
				srcFactor = VK_BLEND_FACTOR_ONE;
				break;
			case GLS_SRCBLEND_DST_COLOR:
				srcFactor = VK_BLEND_FACTOR_DST_COLOR;
				break;
			case GLS_SRCBLEND_ONE_MINUS_DST_COLOR:
				srcFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
				break;
			case GLS_SRCBLEND_SRC_ALPHA:
				srcFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				break;
			case GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA:
				srcFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				break;
			case GLS_SRCBLEND_DST_ALPHA:
				srcFactor = VK_BLEND_FACTOR_DST_ALPHA;
				break;
			case GLS_SRCBLEND_ONE_MINUS_DST_ALPHA:
				srcFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
				break;
		}

		VkBlendFactor dstFactor = VK_BLEND_FACTOR_ZERO;
		switch( stateBits & GLS_DSTBLEND_BITS )
		{
			case GLS_DSTBLEND_ZERO:
				dstFactor = VK_BLEND_FACTOR_ZERO;
				break;
			case GLS_DSTBLEND_ONE:
				dstFactor = VK_BLEND_FACTOR_ONE;
				break;
			case GLS_DSTBLEND_SRC_COLOR:
				dstFactor = VK_BLEND_FACTOR_SRC_COLOR;
				break;
			case GLS_DSTBLEND_ONE_MINUS_SRC_COLOR:
				dstFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
				break;
			case GLS_DSTBLEND_SRC_ALPHA:
				dstFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				break;
			case GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA:
				dstFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				break;
			case GLS_DSTBLEND_DST_ALPHA:
				dstFactor = VK_BLEND_FACTOR_DST_ALPHA;
				break;
			case GLS_DSTBLEND_ONE_MINUS_DST_ALPHA:
				dstFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
				break;
		}

		VkBlendOp blendOp = VK_BLEND_OP_ADD;
		switch( stateBits & GLS_BLENDOP_BITS )
		{
			case GLS_BLENDOP_MIN:
				blendOp = VK_BLEND_OP_MIN;
				break;
			case GLS_BLENDOP_MAX:
				blendOp = VK_BLEND_OP_MAX;
				break;
			case GLS_BLENDOP_ADD:
				blendOp = VK_BLEND_OP_ADD;
				break;
			case GLS_BLENDOP_SUB:
				blendOp = VK_BLEND_OP_SUBTRACT;
				break;
		}

		attachmentState.blendEnable = ( srcFactor != VK_BLEND_FACTOR_ONE || dstFactor != VK_BLEND_FACTOR_ZERO );
		attachmentState.colorBlendOp = blendOp;
		attachmentState.srcColorBlendFactor = srcFactor;
		attachmentState.dstColorBlendFactor = dstFactor;
		attachmentState.alphaBlendOp = blendOp;
		attachmentState.srcAlphaBlendFactor = srcFactor;
		attachmentState.dstAlphaBlendFactor = dstFactor;

		// Color Mask
		attachmentState.colorWriteMask = 0;
		attachmentState.colorWriteMask |= ( stateBits & GLS_REDMASK ) ?	0 : VK_COLOR_COMPONENT_R_BIT;
		attachmentState.colorWriteMask |= ( stateBits & GLS_GREENMASK ) ? 0 : VK_COLOR_COMPONENT_G_BIT;
		attachmentState.colorWriteMask |= ( stateBits & GLS_BLUEMASK ) ? 0 : VK_COLOR_COMPONENT_B_BIT;
		attachmentState.colorWriteMask |= ( stateBits & GLS_ALPHAMASK ) ? 0 : VK_COLOR_COMPONENT_A_BIT;
	}

	// Color Blend
	VkPipelineColorBlendStateCreateInfo colorBlendState = {};
	colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendState.attachmentCount = 1;
	colorBlendState.pAttachments = &attachmentState;

	// Depth / Stencil
	VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
	{
		VkCompareOp depthCompareOp = VK_COMPARE_OP_ALWAYS;
		switch( stateBits & GLS_DEPTHFUNC_BITS )
		{
			case GLS_DEPTHFUNC_EQUAL:
				depthCompareOp = VK_COMPARE_OP_EQUAL;
				break;
			case GLS_DEPTHFUNC_ALWAYS:
				depthCompareOp = VK_COMPARE_OP_ALWAYS;
				break;
			case GLS_DEPTHFUNC_LESS:
				depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
				break;
			case GLS_DEPTHFUNC_GREATER:
				depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
				break;
		}

		VkCompareOp stencilCompareOp = VK_COMPARE_OP_ALWAYS;
		switch( stateBits & GLS_STENCIL_FUNC_BITS )
		{
			case GLS_STENCIL_FUNC_NEVER:
				stencilCompareOp = VK_COMPARE_OP_NEVER;
				break;
			case GLS_STENCIL_FUNC_LESS:
				stencilCompareOp = VK_COMPARE_OP_LESS;
				break;
			case GLS_STENCIL_FUNC_EQUAL:
				stencilCompareOp = VK_COMPARE_OP_EQUAL;
				break;
			case GLS_STENCIL_FUNC_LEQUAL:
				stencilCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
				break;
			case GLS_STENCIL_FUNC_GREATER:
				stencilCompareOp = VK_COMPARE_OP_GREATER;
				break;
			case GLS_STENCIL_FUNC_NOTEQUAL:
				stencilCompareOp = VK_COMPARE_OP_NOT_EQUAL;
				break;
			case GLS_STENCIL_FUNC_GEQUAL:
				stencilCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
				break;
			case GLS_STENCIL_FUNC_ALWAYS:
				stencilCompareOp = VK_COMPARE_OP_ALWAYS;
				break;
		}

		depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencilState.depthTestEnable = VK_TRUE;
		depthStencilState.depthWriteEnable = ( stateBits & GLS_DEPTHMASK ) == 0;
		depthStencilState.depthCompareOp = depthCompareOp;
		depthStencilState.depthBoundsTestEnable = vkcontext.physicalDeviceFeatures.depthBounds && ( stateBits & GLS_DEPTH_TEST_MASK ) != 0;
		depthStencilState.minDepthBounds = 0.0f;
		depthStencilState.maxDepthBounds = 1.0f;
		depthStencilState.stencilTestEnable = ( stateBits & ( GLS_STENCIL_FUNC_BITS | GLS_STENCIL_OP_BITS | GLS_SEPARATE_STENCIL ) ) != 0;

		uint32 ref = uint32( ( stateBits & GLS_STENCIL_FUNC_REF_BITS ) >> GLS_STENCIL_FUNC_REF_SHIFT );
		uint32 mask = uint32( ( stateBits & GLS_STENCIL_FUNC_MASK_BITS ) >> GLS_STENCIL_FUNC_MASK_SHIFT );

		if( stateBits & GLS_SEPARATE_STENCIL )
		{
			depthStencilState.front = GetStencilOpState( stateBits & GLS_STENCIL_FRONT_OPS );
			depthStencilState.front.writeMask = 0xFFFFFFFF;
			depthStencilState.front.compareOp = stencilCompareOp;
			depthStencilState.front.compareMask = mask;
			depthStencilState.front.reference = ref;

			depthStencilState.back = GetStencilOpState( ( stateBits & GLS_STENCIL_BACK_OPS ) >> 12 );
			depthStencilState.back.writeMask = 0xFFFFFFFF;
			depthStencilState.back.compareOp = stencilCompareOp;
			depthStencilState.back.compareMask = mask;
			depthStencilState.back.reference = ref;
		}
		else
		{
			depthStencilState.front = GetStencilOpState( stateBits );
			depthStencilState.front.writeMask = 0xFFFFFFFF;
			depthStencilState.front.compareOp = stencilCompareOp;
			depthStencilState.front.compareMask = mask;
			depthStencilState.front.reference = ref;
			depthStencilState.back = depthStencilState.front;
		}
	}

	// Multisample
	VkPipelineMultisampleStateCreateInfo multisampleState = {};
	multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleState.rasterizationSamples = vkcontext.sampleCount;
	if( vkcontext.supersampling )
	{
		multisampleState.sampleShadingEnable = VK_TRUE;
		multisampleState.minSampleShading = 1.0f;
	}

	// Shader Stages
	idList< VkPipelineShaderStageCreateInfo > stages;
	VkPipelineShaderStageCreateInfo stage = {};
	stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stage.pName = "main";

	{
		stage.module = vertexShader;
		stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages.Append( stage );
	}

	if( fragmentShader != VK_NULL_HANDLE )
	{
		stage.module = fragmentShader;
		stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages.Append( stage );
	}

	// Dynamic
	idList< VkDynamicState > dynamic;
	dynamic.Append( VK_DYNAMIC_STATE_SCISSOR );
	dynamic.Append( VK_DYNAMIC_STATE_VIEWPORT );

	//if( stateBits & GLS_POLYGON_OFFSET )
	{
		dynamic.Append( VK_DYNAMIC_STATE_DEPTH_BIAS );
	}

	//if( stateBits & GLS_DEPTH_TEST_MASK )
	{
		dynamic.Append( VK_DYNAMIC_STATE_DEPTH_BOUNDS );
	}

	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = dynamic.Num();
	dynamicState.pDynamicStates = dynamic.Ptr();

	// Viewport / Scissor
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	// Pipeline Create
	VkGraphicsPipelineCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	createInfo.layout = pipelineLayout;
	createInfo.renderPass = vkcontext.renderPass;
	createInfo.pVertexInputState = &vertexInputState;
	createInfo.pInputAssemblyState = &assemblyInputState;
	createInfo.pRasterizationState = &rasterizationState;
	createInfo.pColorBlendState = &colorBlendState;
	createInfo.pDepthStencilState = &depthStencilState;
	createInfo.pMultisampleState = &multisampleState;
	createInfo.pDynamicState = &dynamicState;
	createInfo.pViewportState = &viewportState;
	createInfo.stageCount = stages.Num();
	createInfo.pStages = stages.Ptr();

	VkPipeline pipeline = VK_NULL_HANDLE;

	ID_VK_CHECK( vkCreateGraphicsPipelines( vkcontext.device, vkcontext.pipelineCache, 1, &createInfo, NULL, &pipeline ) );

	return pipeline;
}

/*
========================
idRenderProgManager::GetPipeline

Returns the cached pipeline for the state bits, creating it from the
program's modules and layout on first use.
========================
*/
VkPipeline idRenderProgManager::GetPipeline( programEntry_t & program, uint64 stateBits )
{
	for( int i = 0; i < program.pipelines.Num(); ++i )
	{
		if( stateBits == program.pipelines[ i ].stateBits )
		{
			return program.pipelines[ i ].pipeline;
		}
	}

	VkPipeline pipeline = CreateGraphicsPipeline( program.vertexLayout, program.vertexModule, program.fragmentModule, program.pipelineLayout, stateBits );

	programEntry_t::pipelineState_t pipelineState;
	pipelineState.pipeline = pipeline;
	pipelineState.stateBits = stateBits;
	program.pipelines.Append( pipelineState );

	return pipeline;
}

/*
================================================================================================
idRenderProgManager::CommitUniforms

Allocates a descriptor set for the current decl program, fills the per stage
uniform blocks from the current uniform values, binds the images in sampler
binding order, and binds the pipeline for the state bits. Every draw gets a
fresh descriptor set from the frame pool; the pools reset in StartFrame.
================================================================================================
*/
void idRenderProgManager::CommitUniforms( uint64 stateBits )
{
	if( currentDecl == nullptr || currentRenderProgram < 0 || currentRenderProgram >= glslPrograms.Num() )
	{
		return;
	}
	programEntry_t& prog = glslPrograms[ currentRenderProgram ];
	if( prog.vertexModule == VK_NULL_HANDLE || prog.descriptorSetLayout == VK_NULL_HANDLE )
	{
		// the upload failed; skip the draw instead of binding garbage
		return;
	}

	VkPipeline pipeline = GetPipeline( prog, stateBits );

	VkDescriptorSetAllocateInfo setAllocInfo = {};
	setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	setAllocInfo.pNext = NULL;
	setAllocInfo.descriptorPool = descriptorPools[ currentData ];
	setAllocInfo.descriptorSetCount = 1;
	setAllocInfo.pSetLayouts = &prog.descriptorSetLayout;

	ID_VK_CHECK( vkAllocateDescriptorSets( vkcontext.device, &setAllocInfo, &descriptorSets[ currentData ][ currentDescSet ] ) );

	VkDescriptorSet descSet = descriptorSets[ currentData ][ currentDescSet ];
	currentDescSet++;

	int writeIndex = 0;
	int bufferIndex = 0;
	int imageIndex = 0;
	int bindingIndex = 0;

	VkWriteDescriptorSet writes[ MAX_DESC_SET_WRITES ];
	VkDescriptorBufferInfo bufferInfos[ MAX_DESC_SET_WRITES ];
	VkDescriptorImageInfo imageInfos[ MAX_DESC_SET_WRITES ];

	int uboIndex = 0;
	idUniformBuffer* ubos[ 3 ] = { NULL, NULL, NULL };

	idUniformBuffer vertParms;
	if( prog.vertexParmIndices.Num() > 0 )
	{
		AllocParmBlockBuffer( prog.vertexParmIndices, vertParms );
		ubos[ uboIndex++ ] = &vertParms;
	}

	idUniformBuffer jointBuffer;
	if( prog.usesJoints && vkcontext.jointCacheHandle > 0 )
	{
		if( !vertexCache.GetJointBuffer( vkcontext.jointCacheHandle, &jointBuffer ) )
		{
			idLib::Error( "idRenderProgManager::CommitCurrent: jointBuffer == NULL" );
			return;
		}
		assert( ( jointBuffer.GetOffset() & ( vkcontext.gpu->props.limits.minUniformBufferOffsetAlignment - 1 ) ) == 0 );
		ubos[ uboIndex++ ] = &jointBuffer;
	}
	else if( prog.usesJoints )
	{
		// no joint buffer is bound for this draw; the block still needs a
		// buffer behind it
		ubos[ uboIndex++ ] = &emptyUBO;
	}

	idUniformBuffer fragParms;
	if( prog.fragmentParmIndices.Num() > 0 )
	{
		AllocParmBlockBuffer( prog.fragmentParmIndices, fragParms );
		ubos[ uboIndex++ ] = &fragParms;
	}

	for( int i = 0; i < prog.bindings.Num(); ++i )
	{
		switch( prog.bindings[ i ] )
		{
			case BINDING_TYPE_UNIFORM_BUFFER:
			{
				idUniformBuffer* ubo = ubos[ bufferIndex ];
				ubo->DebugCheck();

				VkDescriptorBufferInfo& bufferInfo = bufferInfos[ bufferIndex++ ];
				memset( &bufferInfo, 0, sizeof( VkDescriptorBufferInfo ) );
				bufferInfo.buffer = ubo->GetBuffer();
				bufferInfo.offset = ubo->GetOffset();
				bufferInfo.range = ubo->GetSize();

				VkWriteDescriptorSet& write = writes[ writeIndex++ ];
				memset( &write, 0, sizeof( VkWriteDescriptorSet ) );
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.dstSet = descSet;
				write.dstBinding = bindingIndex++;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				write.pBufferInfo = &bufferInfo;

				break;
			}
			case BINDING_TYPE_SAMPLER:
			{
				idImage* image = vkcontext.imageParms[ imageIndex ];

				VkDescriptorImageInfo& imageInfo = imageInfos[ imageIndex++ ];
				memset( &imageInfo, 0, sizeof( VkDescriptorImageInfo ) );
				imageInfo.imageLayout = image->GetLayout();
				imageInfo.imageView = image->GetView();
				imageInfo.sampler = image->GetSampler();

				assert( image->GetView() != VK_NULL_HANDLE );

				VkWriteDescriptorSet& write = writes[ writeIndex++ ];
				memset( &write, 0, sizeof( VkWriteDescriptorSet ) );
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.dstSet = descSet;
				write.dstBinding = bindingIndex++;
				write.descriptorCount = 1;
				write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				write.pImageInfo = &imageInfo;

				break;
			}
		}
	}

	vkUpdateDescriptorSets( vkcontext.device, writeIndex, writes, 0, NULL );

	vkCmdBindDescriptorSets(
		vkcontext.commandBuffer[ vkcontext.frameParity ],
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		prog.pipelineLayout, 0, 1, &descSet,
		0, NULL );

	vkCmdBindPipeline(
		vkcontext.commandBuffer[ vkcontext.frameParity ],
		VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipeline );
}

/*
========================
idRenderProgManager::CachePipeline
========================
*/
void idRenderProgManager::CachePipeline( uint64 stateBits )
{
	if( currentDecl == nullptr || currentRenderProgram < 0 || currentRenderProgram >= glslPrograms.Num() )
	{
		return;
	}
	GetPipeline( glslPrograms[ currentRenderProgram ], stateBits );
}

/*
========================
idRenderProgManager::PrintPipelines
========================
*/
void idRenderProgManager::PrintPipelines()
{
	for( int i = 0; i < glslPrograms.Num(); ++i )
	{
		programEntry_t& prog = glslPrograms[ i ];
		for( int j = 0; j < prog.pipelines.Num(); ++j )
		{
			idLib::Printf( "%s: %llu\n", prog.name.c_str(), prog.pipelines[ j ].stateBits );
			idLib::Printf( "------------------------------------------\n" );
			RpPrintState( prog.pipelines[ j ].stateBits );
			idLib::Printf( "\n" );
		}
	}
}

/*
========================
idRenderProgManager::ClearPipelines
========================
*/
void idRenderProgManager::ClearPipelines()
{
	for( int i = 0; i < glslPrograms.Num(); ++i )
	{
		programEntry_t& prog = glslPrograms[ i ];
		for( int j = 0; j < prog.pipelines.Num(); ++j )
		{
			vkDestroyPipeline( vkcontext.device, prog.pipelines[ j ].pipeline, NULL );
		}
		prog.pipelines.Clear();
	}
}


CONSOLE_COMMAND( Vulkan_PrintPipelineStates, "Print the GLState bits associated with each pipeline.", 0 )
{
	renderProgManager.PrintPipelines();
}

CONSOLE_COMMAND( Vulkan_ClearPipelines, "Clear all existing pipelines, forcing them to be recreated.", 0 )
{
	renderProgManager.ClearPipelines();
}

