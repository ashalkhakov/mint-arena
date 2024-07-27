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

#ifndef __SURFACE_H__
#define __SURFACE_H__

#include "drawvert.h"
#include "q_extramath.h"

/*
===============================================================================

	Surface base class.

	A surface is tesselated to a triangle mesh with each edge shared by
	at most two triangles.

===============================================================================
*/

typedef struct surfaceEdge_s {
	int						verts[2];	// edge vertices always with ( verts[0] < verts[1] )
	int						tris[2];	// edge triangles
} surfaceEdge_t;

typedef struct surface_s {
	// vertices
	int						numVerts, sizeVerts;
	surfVert_t				*verts;
	// 3 references to vertices for each triangle
	int						numIndexes, sizeIndexes;
	int						*indexes;
	// edges
	int						numEdges, sizeEdges;
	surfaceEdge_t			*edges;
	// 3 references to edges for each triangle, may be negative for reversed edge
	int						numEdgeIndexes, sizeEdgeIndexes;
	int						*edgeIndexes;
} surface_t;

// splits the surface into a front and back surface, the surface itself stays unchanged
// frontOnPlaneEdges and backOnPlaneEdges optionally store the indexes to the edges that lay on the split plane
// returns a SIDE_?
int						SurfaceSplit( const surface_t *self, const plane_t plane, const float epsilon, surface_t **front, surface_t **back, int *frontOnPlaneEdges, int *backOnPlaneEdges );
// cuts off the part at the back side of the plane, returns true if some part was at the front
// if there is nothing at the front the number of points is set to zero
qboolean				SurfaceClipInPlace( surface_t *self, const plane_t plane, const float epsilon, const qboolean keepOn );

// returns true if each triangle can be reached from any other triangle by a traversal
qboolean				SurfaceIsConnected( const surface_t *self );
// returns true if the surface is closed
qboolean				SurfaceIsClosed( const surface_t *self );
// returns true if the surface is a convex hull
qboolean				SurfaceIsPolytope( const surface_t *self, const float epsilon );

float					SurfacePlaneDistance( const surface_t *self, const plane_t plane );
int						SurfacePlaneSide( const surface_t *self, const plane_t plane, const float epsilon );

// returns true if the line intersects one of the surface triangles
qboolean				SurfaceLineIntersection( const surface_t *self, const vec3_t start, const vec3_t end, qboolean backFaceCull );
// intersection point is start + dir * scale
qboolean				SurfaceRayIntersection( const surface_t *self, const vec3_t start, const vec3_t dir, float *scale, qboolean backFaceCull );

void					SurfaceGenerateEdgeIndexes( surface_t *self );
int						SurfaceFindEdge( const surface_t *self, int v1, int v2 );

void                    SurfaceResizeVerts( surface_t *self, int sizeVerts );
void                    SurfaceResizeIndexes( surface_t *self, int sizeIndexes );

/*
====================
SurfaceInit
====================
*/
static ID_INLINE void SurfaceInit( surface_t *self ) {
	memset( self, 0, sizeof( *self ) );
}

/*
=================
SurfaceInitFromVertexesAndIndexes
=================
*/
static ID_INLINE void SurfaceInitFromVertsAndIndexes( surface_t *self, const surfVert_t *verts, const int numVerts, const int *indexes, const int numIndexes ) {
	assert( verts != NULL && indexes != NULL && numVerts > 0 && numIndexes > 0 );

	SurfaceInit( self );

	self->verts = ( surfVert_t * )ii.GetMemory( sizeof( *self->verts ) * numVerts );
	self->numVerts = self->sizeVerts = numVerts;
	for ( int i = 0; i < self->numVerts; i++ ) {
		self->verts[i] = verts[i];
	}

	self->indexes = ( int * )ii.GetMemory( sizeof( *self->indexes ) * numIndexes );
	self->numIndexes = self->sizeIndexes = numIndexes;
	memcpy( self->indexes, indexes, self->numIndexes * sizeof( self->indexes[0] ) );

	SurfaceGenerateEdgeIndexes( self );
}

/*
====================
SurfaceInitFromSurface
====================
*/
static ID_INLINE void SurfaceInitFromSurface( surface_t *self, const surface_t *surf ) {
	SurfaceInit( self );
	
	self->verts = ( surfVert_t * )ii.GetMemory( sizeof( *self->verts ) * surf->numVerts );
	self->numVerts = self->sizeVerts = surf->numVerts;
	for ( int i = 0; i < self->numVerts; i++ ) {
		self->verts[i] = surf->verts[i];
	}

    if ( surf->indexes ) {
        self->indexes = ( int * )ii.GetMemory( sizeof( *self->indexes ) * surf->numIndexes );
        self->numIndexes = self->sizeIndexes = surf->numIndexes;
        memcpy( self->indexes, surf->indexes, self->numIndexes * sizeof( self->indexes[0] ) );
    }

    if ( surf->edges ) {
        self->edges = ( surfaceEdge_t * )ii.GetMemory( sizeof( *self->edges ) * surf->numEdges );
        self->numEdges = self->sizeEdges = surf->numEdges;
        memcpy( self->edges, surf->edges, self->numEdges * sizeof( self->edges[0] ) );
    }

    if ( surf->edgeIndexes ) {
        self->edgeIndexes = ( int * )ii.GetMemory( sizeof( *self->edgeIndexes ) * surf->numEdgeIndexes );
        self->numEdgeIndexes = self->sizeEdgeIndexes = surf->numEdgeIndexes;
        memcpy( self->edgeIndexes, surf->edgeIndexes, self->numEdgeIndexes * sizeof( self->edgeIndexes[0] ) );
    }
}

/*
====================
SurfaceFree
====================
*/
static ID_INLINE void SurfaceFree( surface_t *surf ) {
	if ( surf->verts ) {
		ii.FreeMemory( surf->verts );
		surf->verts = NULL;
	}
	surf->numVerts = surf->sizeVerts = 0;

	if ( surf->indexes ) {
		ii.FreeMemory( surf->indexes );
		surf->indexes = NULL;
	}
	surf->numIndexes = surf->sizeIndexes = 0;

	if ( surf->edges ) {
		ii.FreeMemory( surf->edges );
		surf->edges = NULL;
	}
	surf->numEdges = surf->sizeEdges = 0;

	if ( surf->edgeIndexes ) {
		ii.FreeMemory( surf->edgeIndexes );
		surf->edgeIndexes = NULL;
	}
	surf->numEdgeIndexes = surf->sizeEdgeIndexes = 0;
}

/*
=================
SurfaceGetVertex
=================
*/
static ID_INLINE surfVert_t *SurfaceGetVertex( surface_t *self, const int index ) {
	assert( index >= 0 && index < self->numVerts );
	return &self->verts[ index ];
}

/*
=================
SurfaceGetNumIndexes
=================
*/
static ID_INLINE int SurfaceGetNumIndexes( const surface_t *self ) {
	return self->numIndexes;
}

/*
=================
SurfaceGetIndexes
=================
*/
static ID_INLINE const int *SurfaceGetIndexes( const surface_t *self ) {
	return self->indexes;
}

/*
=================
SurfaceGetNumVertices
=================
*/
static ID_INLINE int SurfaceGetNumVertices( const surface_t *self ) {
	return self->numVerts;;
}

/*
=================
SurfaceGetVertices
=================
*/
static ID_INLINE const surfVert_t *SurfaceGetVertices( const surface_t *self ) {
	return self->verts;
}

/*
=================
SurfaceGetEdgeIndexes
=================
*/
static ID_INLINE const int *SurfaceGetEdgeIndexes( const surface_t *self ) {
	return self->edgeIndexes;
}

/*
=================
SurfaceGetEdges
=================
*/
static ID_INLINE const surfaceEdge_t *SurfaceGetEdges( const surface_t *self ) {
	return self->edges;
}

/*
=================
SurfaceConcatenate
=================
*/
ID_INLINE void SurfaceConcatenate( surface_t *self, const surface_t *surf ) {
	int i, m, n;

	n = self->numVerts;
	m = self->numIndexes;

	// merge verts where possible ?
	if ( surf->numVerts + n > self->sizeVerts ) {
		int newSizeVerts = self->sizeVerts + surf->numVerts;
		surfVert_t *newVerts = ( surfVert_t * )ii.GetMemory( sizeof( *newVerts ) * newSizeVerts );

		memcpy( newVerts, self->verts, sizeof( *newVerts ) * self->numVerts );
		ii.FreeMemory( self->verts );
		self->verts = newVerts;
		self->sizeVerts = newSizeVerts;
	}
	memcpy( self->verts + self->numVerts, surf->verts, sizeof( *self->verts ) * surf->numVerts );
	self->numVerts += surf->numVerts;

	if ( surf->numIndexes + m > self->sizeIndexes ) {
		int newSizeIndexes = self->sizeIndexes + surf->numIndexes;
		int *newIndexes = ( int * )ii.GetMemory( sizeof( *newIndexes ) * newSizeIndexes );

		memcpy( newIndexes, self->indexes, sizeof( *newIndexes ) * self->numIndexes );
		ii.FreeMemory( self->indexes );
		self->indexes = newIndexes;
		self->sizeIndexes = newSizeIndexes;
	}
	memcpy( self->indexes + self->numIndexes, surf->indexes, sizeof( *self->indexes ) * surf->numIndexes );
	self->numIndexes += surf->numIndexes;
	for ( i = m; i < self->numIndexes; i++ ) {
		self->indexes[i] += n;
	}

	SurfaceGenerateEdgeIndexes( self );
}

/*
=================
SurfaceClear
=================
*/
static ID_INLINE void SurfaceClear( surface_t *self ) {
	SurfaceFree( self );
}

/*
=================
SurfaceSwapTriangles
=================
*/
#define SWAP(a, b, tmp) ((tmp) = (a), (a) = (b), (b) = (tmp))
static ID_INLINE void SurfaceSwapTriangles( surface_t *self, surface_t *surf ) {
	int		tmp;
	surfVert_t *tmpDrawVert;
	int* tmpIntPtr;
	surfaceEdge_t* tmpSurfEdge;

	SWAP( self->numVerts, surf->numVerts, tmp );
	SWAP( self->sizeVerts, surf->sizeVerts, tmp );
	SWAP( self->verts, surf->verts, tmpDrawVert );

	SWAP( self->numIndexes, surf->numIndexes, tmp );
	SWAP( self->sizeIndexes, surf->sizeIndexes, tmp );
	SWAP( self->indexes, surf->indexes, tmpIntPtr );

	SWAP( self->numEdges, surf->numEdges, tmp );
	SWAP( self->sizeEdges, surf->sizeEdges, tmp );
	SWAP( self->edges, surf->edges, tmpSurfEdge );

	SWAP( self->numEdgeIndexes, surf->numEdgeIndexes, tmp );
	SWAP( self->sizeEdgeIndexes, surf->sizeEdgeIndexes, tmp );
	SWAP( self->edgeIndexes, surf->edgeIndexes, tmpIntPtr );
}
#undef SWAP

/*
=================
SurfaceTranslateSelf
=================
*/
static ID_INLINE void SurfaceTranslateSelf( surface_t *self, const vec3_t translation ) {
	for (int i = 0; i < self->numVerts; i++) {
		VectorAdd( self->verts[i].xyz, translation, self->verts[i].xyz );
	}
}

/*
=================
SurfaceRotateSelf
=================
*/
static ID_INLINE void SurfaceRotateSelf( surface_t *self, const vec3_t rotation[3] ) {
	int i;

	for ( i = 0; i < self->numVerts; i++ ) {
		VectorRotateSelf( self->verts[i].xyz, rotation );
		VectorRotateSelf( self->verts[i].normal, rotation );
		VectorRotateSelf( self->verts[i].tangents[0], rotation );
		VectorRotateSelf( self->verts[i].tangents[1], rotation );
	}
}

#endif /* !__SURFACE_H__ */
