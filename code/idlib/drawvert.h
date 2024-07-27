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

#ifndef __DRAWVERT_H__
#define __DRAWVERT_H__

#include "idlib_local.h"

/*
===============================================================================

	Draw Vertex.

===============================================================================
*/

typedef struct surfVert_s {
	vec3_t			xyz;
	vec2_t			st;
	vec3_t			normal;
	vec3_t			tangents[2];
	byte			color[4];
} surfVert_t;

static ID_INLINE float DrawVertGetIndex( const surfVert_t *self, const int index) {
	assert(index >= 0 && index < 5);
	return ((float*)(&self->xyz))[index];
}
static ID_INLINE float *DrawVertGetIndexRef( const surfVert_t *self, const int index ) {
	assert(index >= 0 && index < 5);
	return &((float*)(&self->xyz))[index];
}

static ID_INLINE void DrawVertClear( surfVert_t *self ) {
	memset( self, 0, sizeof( *self ) );
}

static ID_INLINE void DrawVertLerp( surfVert_t *self, const surfVert_t *a, const surfVert_t *b, const float f ) {
	self->xyz[0] = a->xyz[0] + f * ( b->xyz[0] - a->xyz[0] );
	self->xyz[1] = a->xyz[1] + f * ( b->xyz[1] - a->xyz[1] );
	self->xyz[2] = a->xyz[2] + f * ( b->xyz[2] - a->xyz[2] );

	self->st[0] = a->st[0] + f * ( b->st[0] - a->st[0] );
	self->st[1] = a->st[1] + f * ( b->st[1] - a->st[1] );
}

static ID_INLINE void DrawVertLerpAll( surfVert_t *self, const surfVert_t *a, const surfVert_t *b, const float f ) {
	int i;

	DrawVertLerp( self, a, b, f );

	for ( i = 0; i < 3; i++ ) {
		self->normal[i] = a->normal[i] + f * ( b->normal[i] - a->normal[i] );
		self->tangents[0][i] = a->tangents[0][i] + f * ( b->tangents[0][i] - a->tangents[0][i] );
		self->tangents[1][i] = a->tangents[1][i] + f * ( b->tangents[1][i] - a->tangents[1][i] );
	}

	for ( i = 0; i < 4; i++ ) {
		self->color[i] = ( byte )( a->color[i] + f * (b->color[i] - a->color[0] ) );
	}
}

/*
=============
DrawVertNormalize
=============
*/
static ID_INLINE void DrawVertNormalize( surfVert_t *self ) {
	VectorNormalize( self->normal );
	CrossProduct( self->tangents[1], self->normal, self->tangents[0] );
	VectorNormalize( self->tangents[1] );
	CrossProduct( self->tangents[0], self->tangents[1], self->normal );
	VectorNormalize( self->tangents[0] );
}

static ID_INLINE void DrawVertSetColor( surfVert_t *self, unsigned int color ) {
	*( unsigned int * )( self->color ) = color;
}

static ID_INLINE unsigned long DrawVertGetColor( const surfVert_t *self ) {
	return *( const unsigned int * )( self->color );
}

#endif /* !__DRAWVERT_H__ */
