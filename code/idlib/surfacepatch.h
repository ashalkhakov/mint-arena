/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#ifndef __SURFACE_PATCH_H__
#define __SURFACE_PATCH_H__

#include "surface.h"

/*
===============================================================================

	Bezier patch surface.

===============================================================================
*/

typedef struct surfacePatch_s {
	surface_t			surf;

	int					width;			// width of patch
	int					height;			// height of patch
	int					maxWidth;		// maximum width allocated for
	int					maxHeight;		// maximum height allocated for
	qboolean			expanded;		// true if vertices are spaced out
} surfacePatch_t;

void				SurfacePatchSetSize( surfacePatch_t *self, int patchWidth, int patchHeight );

// subdivide the patch mesh based on error
void				SurfacePatchSubdivide( surfacePatch_t *self, float maxHorizontalError, float maxVerticalError, float maxLength, qboolean genNormals );
// subdivide the patch up to an explicit number of horizontal and vertical subdivisions
void				SurfacePatchSubdivideExplicit( surfacePatch_t *self, int horzSubdivisions, int vertSubdivisions, qboolean genNormals, qboolean removeLinear );


/*
=================
SurfacePatchInit
=================
*/
static ID_INLINE void SurfacePatchInit( surfacePatch_t *self ) {
	SurfaceInit( &self->surf );
	self->height = self->width = self->maxHeight = self->maxWidth = 0;
	self->expanded = qfalse;
}

/*
=================
SurfacePatchInitWithSize
=================
*/
static ID_INLINE void SurfacePatchInitWithSize( surfacePatch_t *self, int maxPatchWidth, int maxPatchHeight ) {
	SurfaceInit( &self->surf );
	self->width = self->height = 0;
	self->maxWidth = maxPatchWidth;
	self->maxHeight = maxPatchHeight;
	self->surf.sizeVerts = self->surf.numVerts = self->maxWidth * self->maxHeight;
	self->surf.verts = ( surfVert_t * )ii.GetMemory( sizeof( surfVert_t ) * self->surf.sizeVerts );
	self->expanded = qfalse;
}

/*
=================
SurfacePatchInitFromPatch
=================
*/
static ID_INLINE void SurfacePatchInitFromPatch( surfacePatch_t *self, const surfacePatch_t *patch ) {

	SurfacePatchInit( self );
	SurfaceInitFromSurface( &self->surf, &patch->surf );

	self->height = patch->height;
	self->width = patch->width;
	self->maxHeight = patch->maxHeight;
	self->maxWidth = patch->maxWidth;
	self->expanded = patch->expanded;
}

/*
=================
SurfacePatchFree
=================
*/
static ID_INLINE void SurfacePatchFree( surfacePatch_t *self ) {
	SurfaceFree( &self->surf );
}

/*
=================
SurfacePatchGetWidth
=================
*/
static ID_INLINE int SurfacePatchGetWidth( const surfacePatch_t *self ) {
	return self->width;
}

/*
=================
SurfacePatchGetHeight
=================
*/
static ID_INLINE int SurfacePatchGetHeight( const surfacePatch_t *self ) {
	return self->height;
}

#endif /* !__SURFACE_PATCH_H__ */
