/*
===========================================================================

PreyNX - the renderer's debug drawing and the tools it serves are not part of
this build. The rest of the engine still calls these.

===========================================================================
*/

#include "../../idlib/precompiled.h"
#include "../tr_local.h"

void RB_AddDebugText( const char *text, const idVec3 &origin, float scale, const idVec4 &color, const idMat3 &viewAxis, const int align, const int lifetime, const bool depthTest ) {
}

void RB_ClearDebugText( int time ) {
}

void RB_AddDebugLine( const idVec4 &color, const idVec3 &start, const idVec3 &end, const int lifeTime, const bool depthTest ) {
}

void RB_ClearDebugLines( int time ) {
}

void RB_AddDebugPolygon( const idVec4 &color, const idWinding &winding, const int lifeTime, const bool depthTest ) {
}

void RB_ClearDebugPolygons( int time ) {
}

float RB_DrawTextLength( const char *text, float scale, int len ) {
	return 0.0f;
}

void RB_ShutdownDebugTools( void ) {
}

void RB_ShowImages( void ) {
}

/*
================================================================================
Map compiler

Prey ships its compiled maps. The renderer only reaches these while compiling one.
================================================================================
*/
void Dmap_f( const idCmdArgs &args ) {
	common->Printf( "dmap is not part of this build\n" );
}

optimizedShadow_t SuperOptimizeOccluders( idVec4 *verts, glIndex_t *indexes, int numIndexes, idPlane projectionPlane, idVec3 projectionOrigin ) {
	optimizedShadow_t ret;
	memset( &ret, 0, sizeof( ret ) );
	return ret;
}

void CleanupOptimizedShadowTris( srfTriangles_t *tri ) {
}

/*
================================================================================
Render textures

Only the editors used them.
================================================================================
*/
idRenderTexture::idRenderTexture( idImage *colorImage, idImage *depthImage_ ) {
	if ( colorImage ) {
		colorImages.Append( colorImage );
	}
	depthImage = depthImage_;
	deviceHandle = 0;
}

idRenderTexture::~idRenderTexture() {
}

void idRenderTexture::Resize( int width, int height ) {
}

void idRenderTexture::EnsureDeviceHandle( void ) {
}

void idRenderTexture::MakeCurrent( void ) {
}

void idRenderTexture::MakeCurrent( int cubeFace ) {
}

void idRenderTexture::BindNull( void ) {
}

void idRenderTexture::AddRenderImage( idImage *image ) {
	colorImages.Append( image );
}

void idRenderTexture::InitRenderTexture( void ) {
}
