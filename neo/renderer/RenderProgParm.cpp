/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2013-2018 Robert Beckebans

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

#include "RenderProgParm.h"

idRenderProgParmState renderProgParms;

// The built-in uniform list. The order and names must match the
// GLSLParmNames list in RenderProgs_GLSL.cpp exactly, because the renderprog
// system maps parm indices to uniform locations through this table.
static const char * builtinParmNames[] = {
	"rpScreenCorrectionFactor",
	"rpWindowCoord",
	"rpDiffuseModifier",
	"rpSpecularModifier",

	"rpLocalLightOrigin",
	"rpLocalViewOrigin",

	"rpLightProjectionS",
	"rpLightProjectionT",
	"rpLightProjectionQ",
	"rpLightFalloffS",

	"rpBumpMatrixS",
	"rpBumpMatrixT",

	"rpDiffuseMatrixS",
	"rpDiffuseMatrixT",

	"rpSpecularMatrixS",
	"rpSpecularMatrixT",

	"rpVertexColorModulate",
	"rpVertexColorAdd",

	"rpColor",
	"rpViewOrigin",
	"rpGlobalEyePos",

	"rpMVPmatrixX",
	"rpMVPmatrixY",
	"rpMVPmatrixZ",
	"rpMVPmatrixW",

	"rpModelMatrixX",
	"rpModelMatrixY",
	"rpModelMatrixZ",
	"rpModelMatrixW",

	"rpProjectionMatrixX",
	"rpProjectionMatrixY",
	"rpProjectionMatrixZ",
	"rpProjectionMatrixW",

	"rpModelViewMatrixX",
	"rpModelViewMatrixY",
	"rpModelViewMatrixZ",
	"rpModelViewMatrixW",

	"rpTextureMatrixS",
	"rpTextureMatrixT",

	"rpTexGen0S",
	"rpTexGen0T",
	"rpTexGen0Q",
	"rpTexGen0Enabled",

	"rpTexGen1S",
	"rpTexGen1T",
	"rpTexGen1Q",
	"rpTexGen1Enabled",

	"rpWobbleSkyX",
	"rpWobbleSkyY",
	"rpWobbleSkyZ",

	"rpOverbright",
	"rpEnableSkinning",
	"rpAlphaTest",

	"rpAmbientColor",

	"rpGlobalLightOrigin",
	"rpJitterTexScale",
	"rpJitterTexOffset",
	"rpCascadeDistances",

	"rpShadowMatrices",
	"rpShadowMatrix0Y",
	"rpShadowMatrix0Z",
	"rpShadowMatrix0W",

	"rpShadowMatrix1X",
	"rpShadowMatrix1Y",
	"rpShadowMatrix1Z",
	"rpShadowMatrix1W",

	"rpShadowMatrix2X",
	"rpShadowMatrix2Y",
	"rpShadowMatrix2Z",
	"rpShadowMatrix2W",

	"rpShadowMatrix3X",
	"rpShadowMatrix3Y",
	"rpShadowMatrix3Z",
	"rpShadowMatrix3W",

	"rpShadowMatrix4X",
	"rpShadowMatrix4Y",
	"rpShadowMatrix4Z",
	"rpShadowMatrix4W",

	"rpShadowMatrix5X",
	"rpShadowMatrix5Y",
	"rpShadowMatrix5Z",
	"rpShadowMatrix5W",

	"rpUser0",
	"rpUser1",
	"rpUser2",
	"rpUser3",
	"rpUser4",
	"rpUser5",
	"rpUser6",
	"rpUser7",
	"rpUser8",
	"rpUser9",
	"rpUser10",

	"rpSoftParticleViewport",
	"rpSoftParticleParms"
};

static_assert( sizeof( builtinParmNames ) / sizeof( builtinParmNames[0] ) == RENDERPROG_NUM_BUILTIN_PARMS, "builtinParmNames is out of sync with RENDERPROG_NUM_BUILTIN_PARMS" );

// Temporary expression slots are used by the parm block parser for
// intermediate values and never map to GPU uniforms.
static const char * temporaryParmNames[RENDERPROG_NUM_TEMPORARY_PARMS] = {
	"temp0", "temp1", "temp2", "temp3", "temp4", "temp5", "temp6", "temp7",
	"temp8", "temp9", "temp10", "temp11", "temp12", "temp13", "temp14", "temp15",
	"temp16", "temp17", "temp18", "temp19", "temp20", "temp21", "temp22", "temp23",
	"temp24", "temp25", "temp26", "temp27", "temp28", "temp29", "temp30", "temp31"
};

idRenderProgParm::idRenderProgParm( const char * name_, parmType_t type, int index ) {
	name = name_;
	parmType = type;
	parmIndex = index;
	memset( &declaredValue, 0, sizeof( declaredValue ) );
}

void idRenderProgParmState::EnsureBuilt() {
	if ( parms.Num() > 0 ) {
		return;
	}

	// The built-in block covers the whole renderParm_t range of this engine,
	// which already includes the user and soft particle parms.
	for ( int i = 0; i < RENDERPROG_NUM_BUILTIN_PARMS; i++ ) {
		parms.Append( idRenderProgParm( builtinParmNames[i], PT_VECTOR, i ) );
	}

	// Temporary expression slots are used by the parm block parser for
	// intermediate values and never map to GPU uniforms.
	for ( int i = 0; i < RENDERPROG_NUM_TEMPORARY_PARMS; i++ ) {
		parms.Append( idRenderProgParm( temporaryParmNames[i], PT_VECTOR, RENDERPROG_FIRST_TEMPORARY_PARM_INDEX + i ) );
	}
}

const idRenderProgParm * idRenderProgParmState::FindParmByName( const char * name ) {
	EnsureBuilt();
	for ( int i = 0; i < parms.Num(); i++ ) {
		if ( idStr::Icmp( parms[i].GetName(), name ) == 0 ) {
			return &parms[i];
		}
	}
	return nullptr;
}

const idRenderProgParm * idRenderProgParmState::GetRenderParmForIndex( int index ) {
	EnsureBuilt();
	if ( index < 0 || index >= parms.Num() ) {
		return nullptr;
	}
	return &parms[index];
}

const char * idRenderProgParmState::GetParmNameForIndex( int index ) {
	const idRenderProgParm * parm = GetRenderParmForIndex( index );
	if ( parm != nullptr ) {
		return parm->GetName();
	}
	return "";
}
