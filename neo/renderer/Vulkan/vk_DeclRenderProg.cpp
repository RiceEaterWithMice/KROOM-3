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


/*
The Vulkan side of the renderprog decl system: converts a renderprog decl's
generated GLSL to UBO based Vulkan GLSL, compiles it to SPIR-V with shaderc,
and uploads the shader modules and layouts into the manager's program
entries. Mirrors gl_DeclRenderProg.cpp on the OpenGL side; the generic
Vulkan manager plumbing stays in RenderProgs_VK.cpp.

The decl upload path is adapted from the Wolfenstein: The New Order
render prog manager reconstructed under references/wolfnto-reversedengineered
(renderprogmanager.h/.cpp). That material is decompiled proprietary code and
is not GPL-compatible; this is a re-implementation on this engine's idlib.
*/

// implemented in RenderProgs_VK.cpp
int CompileGLSLtoSPIRV( const char* filename, const idStr& dataGLSL, const rpStage_t stage, uint32** spirvBuffer );

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
