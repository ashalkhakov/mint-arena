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

#include "idlib_local.h"
#include "q_extramath.h"
#include "surface.h"
#include "polylib.h"

/*
================
VertexListClear

Frees up the memory allocated by the list.  Assumes that type automatically handles freeing up memory.
================
*/
static ID_INLINE void VertexListClear( surface_t *self ) {
	if ( self->verts ) {
		ii.FreeMemory( self->verts );
	}

	self->verts = NULL;
	self->numVerts = 0;
	self->sizeVerts = 0;
}

/*
================
VertexListResize

Allocates memory for the amount of elements requested while keeping the contents intact.
Contents are copied using their = operator so that data is correnctly instantiated.
================
*/
static ID_INLINE void VertexListResize( surface_t *self, int newsize, int newgranularity ) {
	surfVert_t *temp;
	int		i;

	assert( newsize >= 0 );

	assert( newgranularity > 0 );
	//granularity = newgranularity;

	// free up the list if no data is being reserved
	if ( newsize <= 0 ) {
		VertexListClear( self );
		return;
	}

	temp = self->verts;
	self->sizeVerts = newsize;
	if ( self->sizeVerts < self->numVerts ) {
		self->numVerts = self->sizeVerts;
	}

	// copy the old list into our new one
	self->verts = ii.GetMemory( sizeof(temp[0]) * self->sizeVerts );
	for (i = 0; i < self->numVerts; i++) {
		self->verts[i] = temp[i];
	}

	// delete the old list if it exists
	if ( temp ) {
		ii.FreeMemory(temp);
	}
}

/*
================
VertexListAppend

Increases the size of the list by one element and copies the supplied data into it.

Returns the index of the new element.
================
*/
static ID_INLINE int VertexListAppend( surface_t *self, surfVert_t *obj ) {
	if ( !self->verts ) {
		VertexListResize( self, 16, 16 );
	}

	if ( self->numVerts == self->sizeVerts ) {
		int newsize;
		int granularity = 16;

		newsize = self->sizeVerts + granularity;
		VertexListResize( self, newsize - newsize % granularity, granularity );
	}

	assert( self->verts != NULL && self->numVerts < self->sizeVerts );
	self->verts[self->numVerts] = *obj;
	self->numVerts++;

	return self->numVerts - 1;
}

/*
=================
SurfaceResizeVerts
=================
*/
void SurfaceResizeVerts( surface_t *self, int sizeVerts ) {
    VertexListResize( self, sizeVerts, 16 );
}

/*
=================
SurfaceResizeIndexes
=================
*/
void SurfaceResizeIndexes( surface_t *self, int sizeIndexes ) {
	int *temp = self->indexes;
    int i;

	self->sizeIndexes = sizeIndexes;
	if ( self->sizeIndexes < self->numIndexes ) {
		self->numIndexes = self->sizeIndexes;
	}

	// copy the old list into our new one
	self->indexes = ii.GetMemory( sizeof(temp[0]) * self->sizeIndexes );
	for (i = 0; i < self->numIndexes; i++) {
		self->indexes[i] = temp[i];
	}

	// delete the old list if it exists
	if ( temp ) {
		ii.FreeMemory(temp);
	}    
}

/*
=================
UpdateVertexIndex
=================
*/
static ID_INLINE int UpdateVertexIndex( int vertexIndexNum[2], int *vertexRemap, int *vertexCopyIndex, int vertNum ) {
	int s = INTSIGNBITSET( vertexRemap[vertNum] );
	vertexIndexNum[0] = vertexRemap[vertNum];
	vertexRemap[vertNum] = vertexIndexNum[s];
	vertexIndexNum[1] += s;
	vertexCopyIndex[vertexRemap[vertNum]] = vertNum;
	return vertexRemap[vertNum];
}

/*
=================
SurfaceSplit
=================
*/
int SurfaceSplit( const surface_t *self, const plane_t plane, const float epsilon, surface_t **front, surface_t **back, int *frontOnPlaneEdges, int *backOnPlaneEdges ) {
	float *			dists;
	float			f;
	byte *			sides;
	int				counts[3];
	int *			edgeSplitVertex;
	int				numEdgeSplitVertexes;
	int *			vertexRemap[2];
	int				vertexIndexNum[2][2];
	int *			vertexCopyIndex[2];
	int *			indexPtr[2];
	int				indexNum[2];
	int *			index;
	int *			onPlaneEdges[2];
	int				numOnPlaneEdges[2];
	int				maxOnPlaneEdges;
	int				i;
	surface_t *		surface[2];
	surfVert_t		v;
	surfVert_t *	verts;
	int *			indexes;
	vec3_t			dir1, dir2, cross;

	verts = self->verts;
	indexes = self->indexes;

	dists = ( float * )ii.GetMemory( self->numVerts * sizeof( float ) );
	sides = ( byte * )ii.GetMemory( self->numVerts * sizeof( byte ) );

	counts[0] = counts[1] = counts[2] = 0;

	// determine side for each vertex
	for ( i = 0; i < self->numVerts; i++ ) {
		dists[i] = f = PlaneDistance( plane, verts[i].xyz );
		if ( f > epsilon ) {
			sides[i] = SIDE_FRONT;
		} else if ( f < -epsilon ) {
			sides[i] = SIDE_BACK;
		} else {
			sides[i] = SIDE_ON;
		}
		counts[sides[i]]++;
	}

	*front = *back = NULL;

	// if coplanar, put on the front side if the normals match
	if ( !counts[SIDE_FRONT] && !counts[SIDE_BACK] ) {

		VectorSubtract( verts[indexes[1]].xyz, verts[indexes[0]].xyz, dir1 );
		VectorSubtract( verts[indexes[0]].xyz, verts[indexes[2]].xyz, dir2 );
		CrossProduct( dir1, dir2, cross );
		f = DotProduct( cross, plane );
		if ( FLOATSIGNBITSET( f ) ) {
			*back = ii.GetMemory( sizeof( surface_t ) );
			SurfaceInitFromSurface( *back, self );
			ii.FreeMemory( sides );
			ii.FreeMemory( dists );
			return SIDE_BACK;
		} else {
			*front = ii.GetMemory( sizeof( surface_t ) );
			SurfaceInitFromSurface( *front, self );
			ii.FreeMemory( sides );
			ii.FreeMemory( dists );
			return SIDE_FRONT;
		}
	}
	// if nothing at the front of the clipping plane
	if ( !counts[SIDE_FRONT] ) {
		*back = ii.GetMemory( sizeof( surface_t ) );
		SurfaceInitFromSurface( *back, self );
		ii.FreeMemory( sides );
		ii.FreeMemory( dists );
		return SIDE_BACK;
	}
	// if nothing at the back of the clipping plane
	if ( !counts[SIDE_BACK] ) {
		*front = ii.GetMemory( sizeof( surface_t ) );
		SurfaceInitFromSurface( *back, self );
		ii.FreeMemory( sides );
		ii.FreeMemory( dists );
		return SIDE_FRONT;
	}

	// allocate front and back surface
	*front = surface[0] = ii.GetMemory( sizeof( surface_t ) );
	SurfaceInit( *front );
	*back = surface[1] = ii.GetMemory( sizeof( surface_t ) );
	SurfaceInit( *back );

	edgeSplitVertex = ( int * )ii.GetMemory( self->numEdges * sizeof( int ) );
	numEdgeSplitVertexes = 0;

	maxOnPlaneEdges = 4 * counts[SIDE_ON];
	counts[SIDE_FRONT] = counts[SIDE_BACK] = counts[SIDE_ON] = 0;

	// split edges
	for ( i = 0; i < self->numEdges; i++ ) {
		int v0 = self->edges[i].verts[0];
		int v1 = self->edges[i].verts[1];
		int sidesOr = ( sides[v0] | sides[v1] );

		// if both vertexes are on the same side or one is on the clipping plane
		if ( !( sides[v0] ^ sides[v1] ) || ( sidesOr & SIDE_ON ) ) {
			edgeSplitVertex[i] = -1;
			counts[sidesOr & SIDE_BACK]++;
			counts[SIDE_ON] += ( sidesOr & SIDE_ON ) >> 1;
		} else {
			f = dists[v0] / ( dists[v0] - dists[v1] );
			DrawVertLerpAll( &v, &verts[v0], &verts[v1], f );
			edgeSplitVertex[i] = numEdgeSplitVertexes++;
			VertexListAppend( surface[0], &v );
			VertexListAppend( surface[1], &v );
		}
	}

	// each edge is shared by at most two triangles, as such there can never be more indexes than twice the number of edges
	surface[0]->sizeIndexes = ( ( counts[SIDE_FRONT] + counts[SIDE_ON] ) * 2 ) + ( numEdgeSplitVertexes * 4 );
	surface[0]->numIndexes = 0;
	surface[0]->indexes = ii.GetMemory( sizeof( *surface[0]->indexes ) * surface[0]->sizeIndexes );
	surface[1]->sizeIndexes = ( ( counts[SIDE_BACK] + counts[SIDE_ON] ) * 2 ) + ( numEdgeSplitVertexes * 4 );
	surface[1]->numIndexes = 0;
	surface[1]->indexes = ii.GetMemory( sizeof( *surface[1]->indexes ) * surface[1]->sizeIndexes );

	// allocate indexes to construct the triangle indexes for the front and back surface
	vertexRemap[0] = (int *) ii.GetMemory( self->numVerts * sizeof( int ) );
	memset( vertexRemap[0], -1, self->numVerts * sizeof( int ) );
	vertexRemap[1] = (int *) ii.GetMemory( self->numVerts * sizeof( int ) );
	memset( vertexRemap[1], -1, self->numVerts * sizeof( int ) );

	vertexCopyIndex[0] = (int *) ii.GetMemory( ( numEdgeSplitVertexes + self->numVerts ) * sizeof( int ) );
	vertexCopyIndex[1] = (int *) ii.GetMemory( ( numEdgeSplitVertexes + self->numVerts ) * sizeof( int ) );

	vertexIndexNum[0][0] = vertexIndexNum[1][0] = 0;
	vertexIndexNum[0][1] = vertexIndexNum[1][1] = numEdgeSplitVertexes;

	indexPtr[0] = surface[0]->indexes;
	indexPtr[1] = surface[1]->indexes;
	indexNum[0] = surface[0]->numIndexes;
	indexNum[1] = surface[1]->numIndexes;

	maxOnPlaneEdges += 4 * numEdgeSplitVertexes;
	// allocate one more in case no triangles are actually split which may happen for a disconnected surface
	onPlaneEdges[0] = (int *) ii.GetMemory( ( maxOnPlaneEdges + 1 ) * sizeof( int ) );
	onPlaneEdges[1] = (int *) ii.GetMemory( ( maxOnPlaneEdges + 1 ) * sizeof( int ) );
	numOnPlaneEdges[0] = numOnPlaneEdges[1] = 0;

	// split surface triangles
	for ( i = 0; i < self->numEdgeIndexes; i += 3 ) {
		int e0, e1, e2, v0, v1, v2, s, n;

		e0 = abs( self->edgeIndexes[i+0] );
		e1 = abs( self->edgeIndexes[i+1] );
		e2 = abs( self->edgeIndexes[i+2] );

		v0 = indexes[i+0];
		v1 = indexes[i+1];
		v2 = indexes[i+2];

		switch( ( INTSIGNBITSET( edgeSplitVertex[e0] ) | ( INTSIGNBITSET( edgeSplitVertex[e1] ) << 1 ) | ( INTSIGNBITSET( edgeSplitVertex[e2] ) << 2 ) ) ^ 7 ) {
			case 0: {	// no edges split
				if ( ( sides[v0] & sides[v1] & sides[v2] ) & SIDE_ON ) {
					// coplanar
					VectorSubtract( verts[v1].xyz, verts[v0].xyz, dir1 );
					VectorSubtract( verts[v0].xyz, verts[v2].xyz, dir2 );
					CrossProduct( dir1, dir2, cross );
					f = DotProduct( cross, plane );
					s = FLOATSIGNBITSET( f );
				} else {
					s = ( sides[v0] | sides[v1] | sides[v2] ) & SIDE_BACK;
				}
				n = indexNum[s];
				onPlaneEdges[s][numOnPlaneEdges[s]] = n;
				numOnPlaneEdges[s] += ( sides[v0] & sides[v1] ) >> 1;
				onPlaneEdges[s][numOnPlaneEdges[s]] = n+1;
				numOnPlaneEdges[s] += ( sides[v1] & sides[v2] ) >> 1;
				onPlaneEdges[s][numOnPlaneEdges[s]] = n+2;
				numOnPlaneEdges[s] += ( sides[v2] & sides[v0] ) >> 1;
				index = indexPtr[s];
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v0 );
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v1 );
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v2 );
				indexNum[s] = n;
				break;
			}
			case 1: {	// first edge split
				s = sides[v0] & SIDE_BACK;
				n = indexNum[s];
				onPlaneEdges[s][numOnPlaneEdges[s]++] = n;
				index = indexPtr[s];
				index[n++] = edgeSplitVertex[e0];
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v2 );
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v0 );
				indexNum[s] = n;
				s ^= 1;
				n = indexNum[s];
				onPlaneEdges[s][numOnPlaneEdges[s]++] = n;
				index = indexPtr[s];
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v2 );
				index[n++] = edgeSplitVertex[e0];
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v1 );
				indexNum[s] = n;
				break;
			}
			case 2: {	// second edge split
				s = sides[v1] & SIDE_BACK;
				n = indexNum[s];
				onPlaneEdges[s][numOnPlaneEdges[s]++] = n;
				index = indexPtr[s];
				index[n++] = edgeSplitVertex[e1];
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v0 );
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v1 );
				indexNum[s] = n;
				s ^= 1;
				n = indexNum[s];
				onPlaneEdges[s][numOnPlaneEdges[s]++] = n;
				index = indexPtr[s];
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v0 );
				index[n++] = edgeSplitVertex[e1];
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v2 );
				indexNum[s] = n;
				break;
			}
			case 3: {	// first and second edge split
				s = sides[v1] & SIDE_BACK;
				n = indexNum[s];
				onPlaneEdges[s][numOnPlaneEdges[s]++] = n;
				index = indexPtr[s];
				index[n++] = edgeSplitVertex[e1];
				index[n++] = edgeSplitVertex[e0];
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v1 );
				indexNum[s] = n;
				s ^= 1;
				n = indexNum[s];
				onPlaneEdges[s][numOnPlaneEdges[s]++] = n;
				index = indexPtr[s];
				index[n++] = edgeSplitVertex[e0];
				index[n++] = edgeSplitVertex[e1];
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v0 );
				index[n++] = edgeSplitVertex[e1];
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v2 );
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v0 );
				indexNum[s] = n;
				break;
			}
			case 4: {	// third edge split
				s = sides[v2] & SIDE_BACK;
				n = indexNum[s];
				onPlaneEdges[s][numOnPlaneEdges[s]++] = n;
				index = indexPtr[s];
				index[n++] = edgeSplitVertex[e2];
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v1 );
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v2 );
				indexNum[s] = n;
				s ^= 1;
				n = indexNum[s];
				onPlaneEdges[s][numOnPlaneEdges[s]++] = n;
				index = indexPtr[s];
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v1 );
				index[n++] = edgeSplitVertex[e2];
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v0 );
				indexNum[s] = n;
				break;
			}
			case 5: {	// first and third edge split
				s = sides[v0] & SIDE_BACK;
				n = indexNum[s];
				onPlaneEdges[s][numOnPlaneEdges[s]++] = n;
				index = indexPtr[s];
				index[n++] = edgeSplitVertex[e0];
				index[n++] = edgeSplitVertex[e2];
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v0 );
				indexNum[s] = n;
				s ^= 1;
				n = indexNum[s];
				onPlaneEdges[s][numOnPlaneEdges[s]++] = n;
				index = indexPtr[s];
				index[n++] = edgeSplitVertex[e2];
				index[n++] = edgeSplitVertex[e0];
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v1 );
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v1 );
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v2 );
				index[n++] = edgeSplitVertex[e2];
				indexNum[s] = n;
				break;
			}
			case 6: {	// second and third edge split
				s = sides[v2] & SIDE_BACK;
				n = indexNum[s];
				onPlaneEdges[s][numOnPlaneEdges[s]++] = n;
				index = indexPtr[s];
				index[n++] = edgeSplitVertex[e2];
				index[n++] = edgeSplitVertex[e1];
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v2 );
				indexNum[s] = n;
				s ^= 1;
				n = indexNum[s];
				onPlaneEdges[s][numOnPlaneEdges[s]++] = n;
				index = indexPtr[s];
				index[n++] = edgeSplitVertex[e1];
				index[n++] = edgeSplitVertex[e2];
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v1 );
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v0 );
				index[n++] = UpdateVertexIndex( vertexIndexNum[s], vertexRemap[s], vertexCopyIndex[s], v1 );
				index[n++] = edgeSplitVertex[e2];
				indexNum[s] = n;
				break;
			}
		}
	}

	surface[0]->numIndexes = indexNum[0];
	surface[1]->numIndexes = indexNum[1];

	// copy vertexes
	// FIXME: only reallocate if more than sizeVerts...
	surface[0]->numVerts = vertexIndexNum[0][1];
	surface[0]->sizeVerts = surface[0]->numVerts;
	surface[0]->verts = ii.GetMemory( sizeof( *surface[0]->verts ) * surface[0]->sizeVerts );
	index = vertexCopyIndex[0];
	for ( i = numEdgeSplitVertexes; i < surface[0]->numVerts; i++ ) {
		surface[0]->verts[i] = verts[index[i]];
	}
	// FIXME: only reallocate if more than sizeVerts...
	surface[1]->numVerts = vertexIndexNum[1][1];
	surface[1]->sizeVerts = surface[1]->numVerts;
	surface[1]->verts = ii.GetMemory( sizeof( *surface[1]->verts ) * surface[1]->sizeVerts );
	index = vertexCopyIndex[1];
	for ( i = numEdgeSplitVertexes; i < surface[1]->numVerts; i++ ) {
		surface[1]->verts[i] = verts[index[i]];
	}

	// generate edge indexes
	SurfaceGenerateEdgeIndexes( surface[0] );
	SurfaceGenerateEdgeIndexes( surface[1] );

	if ( frontOnPlaneEdges ) {
		// FIXME: fishy!
		memcpy( frontOnPlaneEdges, onPlaneEdges[0], numOnPlaneEdges[0] * sizeof( int ) );
		frontOnPlaneEdges[numOnPlaneEdges[0]] = -1;
	}

	if ( backOnPlaneEdges ) {
		// FIXME: fishy!
		memcpy( backOnPlaneEdges, onPlaneEdges[1], numOnPlaneEdges[1] * sizeof( int ) );
		backOnPlaneEdges[numOnPlaneEdges[1]] = -1;
	}

	ii.FreeMemory( vertexRemap[0] );
	ii.FreeMemory( vertexRemap[1] );
	ii.FreeMemory( vertexCopyIndex[0] );
	ii.FreeMemory( vertexCopyIndex[1] );
	ii.FreeMemory( onPlaneEdges[0] );
	ii.FreeMemory( onPlaneEdges[1] );
	ii.FreeMemory( sides );
	ii.FreeMemory( dists );

	return SIDE_CROSS;
}

/*
=================
SurfaceClipInPlace
=================
*/
qboolean SurfaceClipInPlace( surface_t *self, const plane_t plane, const float epsilon, const qboolean keepOn ) {
	float *			dists;
	float			f;
	byte *			sides;
	int				counts[3];
	int				i;
	int *			edgeSplitVertex;
	int *			vertexRemap;
	int				vertexIndexNum[2];
	int *			vertexCopyIndex;
	int *			indexPtr;
	int				indexNum;
	int				numEdgeSplitVertexes;
	surfVert_t		v;
	surfVert_t		*verts;
	int				*indexes;
	surfaceEdge_t	*edges;
	surfVert_t		*newVerts;
	int				sizeNewVerts, numNewVerts;
	int				*newIndexes;
	int				sizeNewIndexes, numNewIndexes;
	vec3_t			dir1, dir2, tmp;

	verts = self->verts;
	indexes = self->indexes;
	edges = self->edges;
	dists = (float *) ii.GetMemory( self->numVerts * sizeof( float ) );
	sides = (byte *) ii.GetMemory( self->numVerts * sizeof( byte ) );

	counts[0] = counts[1] = counts[2] = 0;

	// determine side for each vertex
	for ( i = 0; i < self->numVerts; i++ ) {
		dists[i] = f = PlaneDistance( plane, verts[i].xyz );
		if ( f > epsilon ) {
			sides[i] = SIDE_FRONT;
		} else if ( f < -epsilon ) {
			sides[i] = SIDE_BACK;
		} else {
			sides[i] = SIDE_ON;
		}
		counts[sides[i]]++;
	}

	// if coplanar, put on the front side if the normals match
	if ( !counts[SIDE_FRONT] && !counts[SIDE_BACK] ) {
		
		VectorSubtract( verts[indexes[1]].xyz, verts[indexes[0]].xyz, dir1 );
		VectorSubtract( verts[indexes[0]].xyz, verts[indexes[2]].xyz, dir2 );
		CrossProduct( dir1, dir2, tmp );
		f = DotProduct( tmp, plane );
		if ( FLOATSIGNBITSET( f ) ) {
			SurfaceClear( self );
			ii.FreeMemory( dists );
			ii.FreeMemory( sides );
			return qfalse;
		} else {
			return qtrue;
		}
	}
	// if nothing at the front of the clipping plane
	if ( !counts[SIDE_FRONT] ) {
		SurfaceClear( self );
		ii.FreeMemory( dists );
		ii.FreeMemory( sides );
		return qfalse;
	}
	// if nothing at the back of the clipping plane
	if ( !counts[SIDE_BACK] ) {
		ii.FreeMemory(dists);
		ii.FreeMemory(sides);
		return qtrue;
	}

	edgeSplitVertex = (int *) ii.GetMemory( self->numEdges * sizeof( int ) );
	numEdgeSplitVertexes = 0;

	counts[SIDE_FRONT] = counts[SIDE_BACK] = 0;

	sizeNewVerts = self->numVerts;
	numNewVerts = 0;
	newVerts = ( surfVert_t * )ii.GetMemory( sizeNewVerts * sizeof( *newVerts ) );

	// split edges
	for ( i = 0; i < self->numEdges; i++ ) {
		int v0 = edges[i].verts[0];
		int v1 = edges[i].verts[1];

		// if both vertexes are on the same side or one is on the clipping plane
		if ( !( sides[v0] ^ sides[v1] ) || ( ( sides[v0] | sides[v1] ) & SIDE_ON ) ) {
			edgeSplitVertex[i] = -1;
			counts[(sides[v0]|sides[v1]) & SIDE_BACK]++;
		} else {
			f = dists[v0] / ( dists[v0] - dists[v1] );
			DrawVertLerpAll( &v, &verts[v0], &verts[v1], f );
			edgeSplitVertex[i] = numEdgeSplitVertexes++;
			newVerts[numNewVerts++] = v;
		}
	}

	// each edge is shared by at most two triangles, as such there can never be
	// more indexes than twice the number of edges
	sizeNewIndexes = (counts[SIDE_FRONT] << 1) + (numEdgeSplitVertexes << 2);
	newIndexes = ( int * )ii.GetMemory( sizeNewIndexes );

	// allocate indexes to construct the triangle indexes for the front and back surface
	vertexRemap = (int *)ii.GetMemory( self->numVerts * sizeof( int ) );
	memset( vertexRemap, -1, self->numVerts * sizeof( int ) );

	vertexCopyIndex = (int *)ii.GetMemory( ( numEdgeSplitVertexes + self->numVerts ) * sizeof( int ) );

	vertexIndexNum[0] = 0;
	vertexIndexNum[1] = numEdgeSplitVertexes;

	indexPtr = newIndexes;
	indexNum = numNewIndexes;

	// split surface triangles
	for ( i = 0; i < self->numEdgeIndexes; i += 3 ) {
		int e0, e1, e2, v0, v1, v2;

		e0 = abs( self->edgeIndexes[i+0] );
		e1 = abs( self->edgeIndexes[i+1] );
		e2 = abs( self->edgeIndexes[i+2] );

		v0 = indexes[i+0];
		v1 = indexes[i+1];
		v2 = indexes[i+2];

		switch( ( INTSIGNBITSET( edgeSplitVertex[e0] ) | ( INTSIGNBITSET( edgeSplitVertex[e1] ) << 1 ) | ( INTSIGNBITSET( edgeSplitVertex[e2] ) << 2 ) ) ^ 7 ) {
			case 0: {	// no edges split
				if ( ( sides[v0] | sides[v1] | sides[v2] ) & SIDE_BACK ) {
					break;
				}
				if ( ( sides[v0] & sides[v1] & sides[v2] ) & SIDE_ON ) {
					// coplanar
					if ( !keepOn ) {
						break;
					}

					VectorSubtract( verts[v1].xyz, verts[v0].xyz, dir1 );
					VectorSubtract( verts[v0].xyz, verts[v2].xyz, dir2 );
					CrossProduct( dir1, dir2, tmp );
					f = DotProduct( tmp, plane );
					if ( FLOATSIGNBITSET( f ) ) {
						break;
					}
				}
				indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v0 );
				indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v1 );
				indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v2 );
				break;
			}
			case 1: {	// first edge split
				if ( !( sides[v0] & SIDE_BACK ) ) {
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v0 );
					indexPtr[indexNum++] = edgeSplitVertex[e0];
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v2 );
				} else {
					indexPtr[indexNum++] = edgeSplitVertex[e0];
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v1 );
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v2 );
				}
				break;
			}
			case 2: {	// second edge split
				if ( !( sides[v1] & SIDE_BACK ) ) {
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v1 );
					indexPtr[indexNum++] = edgeSplitVertex[e1];
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v0 );
				} else {
					indexPtr[indexNum++] = edgeSplitVertex[e1];
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v2 );
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v0 );
				}
				break;
			}
			case 3: {	// first and second edge split
				if ( !( sides[v1] & SIDE_BACK ) ) {
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v1 );
					indexPtr[indexNum++] = edgeSplitVertex[e1];
					indexPtr[indexNum++] = edgeSplitVertex[e0];
				} else {
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v0 );
					indexPtr[indexNum++] = edgeSplitVertex[e0];
					indexPtr[indexNum++] = edgeSplitVertex[e1];
					indexPtr[indexNum++] = edgeSplitVertex[e1];
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v2 );
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v0 );
				}
				break;
			}
			case 4: {	// third edge split
				if ( !( sides[v2] & SIDE_BACK ) ) {
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v2 );
					indexPtr[indexNum++] = edgeSplitVertex[e2];
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v1 );
				} else {
					indexPtr[indexNum++] = edgeSplitVertex[e2];
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v0 );
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v1 );
				}
				break;
			}
			case 5: {	// first and third edge split
				if ( !( sides[v0] & SIDE_BACK ) ) {
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v0 );
					indexPtr[indexNum++] = edgeSplitVertex[e0];
					indexPtr[indexNum++] = edgeSplitVertex[e2];
				} else {
					indexPtr[indexNum++] = edgeSplitVertex[e0];
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v1 );
					indexPtr[indexNum++] = edgeSplitVertex[e2];
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v1 );
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v2 );
					indexPtr[indexNum++] = edgeSplitVertex[e2];
				}
				break;
			}
			case 6: {	// second and third edge split
				if ( !( sides[v2] & SIDE_BACK ) ) {
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v2 );
					indexPtr[indexNum++] = edgeSplitVertex[e2];
					indexPtr[indexNum++] = edgeSplitVertex[e1];
				} else {
					indexPtr[indexNum++] = edgeSplitVertex[e2];
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v1 );
					indexPtr[indexNum++] = edgeSplitVertex[e1];
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v0 );
					indexPtr[indexNum++] = UpdateVertexIndex( vertexIndexNum, vertexRemap, vertexCopyIndex, v1 );
					indexPtr[indexNum++] = edgeSplitVertex[e2];
				}
				break;
			}
		}
	}

	numNewIndexes = indexNum;

	// copy vertexes
	numNewVerts = vertexIndexNum[1];
	for ( i = numEdgeSplitVertexes; i < numNewVerts; i++ ) {
		newVerts[i] = verts[vertexCopyIndex[i]];
	}

	// copy back to this surface
	ii.FreeMemory( self->indexes );
	self->indexes = newIndexes;
	self->numIndexes = numNewIndexes;
	self->sizeIndexes = sizeNewIndexes;
	ii.FreeMemory( self->verts );
	self->verts = newVerts;
	self->numVerts = numNewVerts;
	self->sizeVerts = sizeNewVerts;
	ii.FreeMemory( dists );
	ii.FreeMemory( sides );

	SurfaceGenerateEdgeIndexes( self );

	return qtrue;
}

/*
=============
SurfaceIsConnected
=============
*/
qboolean SurfaceIsConnected( const surface_t *self ) {
	int i, j, numIslands, numTris;
	int queueStart, queueEnd;
	int *queue, *islandNum;
	int curTri, nextTri, edgeNum;
	const int *index;

	numIslands = 0;
	numTris = self->numIndexes / 3;
	islandNum = (int *) ii.GetMemory( numTris * sizeof( int ) );
	memset( islandNum, -1, numTris * sizeof( int ) );
	queue = (int *) ii.GetMemory( numTris * sizeof( int ) );

	for ( i = 0; i < numTris; i++ ) {

		if ( islandNum[i] != -1 ) {
			continue;
		}

		queueStart = 0;
		queueEnd = 1;
		queue[0] = i;
		islandNum[i] = numIslands;

		for ( curTri = queue[queueStart]; queueStart < queueEnd; curTri = queue[++queueStart] ) {

			index = &self->edgeIndexes[curTri * 3];

			for ( j = 0; j < 3; j++ ) {

				edgeNum = index[j];
				nextTri = self->edges[abs(edgeNum)].tris[INTSIGNBITNOTSET(edgeNum)];

				if ( nextTri == -1 ) {
					continue;
				}

				nextTri /= 3;

				if ( islandNum[nextTri] != -1 ) {
					continue;
				}

				queue[queueEnd++] = nextTri;
				islandNum[nextTri] = numIslands;
			}
		}
		numIslands++;
	}

	return ( numIslands == 1 );
}

/*
=================
SurfaceIsClosed
=================
*/
qboolean SurfaceIsClosed( const surface_t *self ) {
	for ( int i = 0; i < self->numEdges; i++ ) {
		if ( self->edges[i].tris[0] < 0 || self->edges[i].tris[1] < 0 ) {
			return qfalse;
		}
	}
	return qtrue;
}

/*
=============
SurfaceIsPolytope
=============
*/
qboolean SurfaceIsPolytope( const surface_t *self, const float epsilon ) {
	int i, j;
	plane_t plane;

	if ( !SurfaceIsClosed( self ) ) {
		return qfalse;
	}

	for ( i = 0; i < self->numIndexes; i += 3 ) {
		if ( !PlaneInitWithPoints( plane, self->verts[self->indexes[i+0]].xyz, self->verts[self->indexes[i+1]].xyz, self->verts[self->indexes[i+2]].xyz, qtrue ) )
			return qfalse;

		for ( j = 0; j < self->numVerts; j++ ) {
			if ( PlaneSide( plane, self->verts[j].xyz, epsilon ) == SIDE_FRONT ) {
				return qfalse;
			}
		}
	}
	return qtrue;
}

/*
=============
SurfacePlaneDistance
=============
*/
float SurfacePlaneDistance( const surface_t *self, const plane_t plane ) {
	int		i;
	float	d, min, max;

	min = Q_INFINITY;
	max = -min;
	for ( i = 0; i < self->numVerts; i++ ) {
		d = PlaneDistance( plane, self->verts[i].xyz );
		if ( d < min ) {
			min = d;
			if ( FLOATSIGNBITSET( min ) & FLOATSIGNBITNOTSET( max ) ) {
				return 0.0f;
			}
		}
		if ( d > max ) {
			max = d;
			if ( FLOATSIGNBITSET( min ) & FLOATSIGNBITNOTSET( max ) ) {
				return 0.0f;
			}
		}
	}
	if ( FLOATSIGNBITNOTSET( min ) ) {
		return min;
	}
	if ( FLOATSIGNBITSET( max ) ) {
		return max;
	}
	return 0.0f;
}

/*
=============
SurfacePlaneSide
=============
*/
int SurfacePlaneSide( const surface_t *self, const plane_t plane, const float epsilon ) {
	qboolean	front, back;
	int			i;
	float		d;

	front = qfalse;
	back = qfalse;
	for ( i = 0; i < self->numVerts; i++ ) {
		d = PlaneDistance( plane, self->verts[i].xyz );
		if ( d < -epsilon ) {
			if ( front ) {
				return SIDE_CROSS;
			}
			back = qtrue;
			continue;
		}
		else if ( d > epsilon ) {
			if ( back ) {
				return SIDE_CROSS;
			}
			front = qtrue;
			continue;
		}
	}

	if ( back ) {
		return SIDE_BACK;
	}
	if ( front ) {
		return SIDE_FRONT;
	}
	return SIDE_ON;
}

/*
=================
SurfaceLineIntersection
=================
*/
qboolean SurfaceLineIntersection( const surface_t *self, const vec3_t start, const vec3_t end, qboolean backFaceCull ) {
	float scale;

	SurfaceRayIntersection( self, start, end - start, &scale, qfalse );
	return ( scale >= 0.0f && scale <= 1.0f );
}

/*
=================
SurfaceRayIntersection
=================
*/
qboolean SurfaceRayIntersection( const surface_t *self, const vec3_t start, const vec3_t dir, float *scale, qboolean backFaceCull ) {
	int i, i0, i1, i2, s0, s1, s2;
	float d, s = 0.0f;
	byte *sidedness;
	vec6_t rayPl, pl;
	plane_t plane;

	sidedness = (byte *)ii.GetMemory( self->numEdges * sizeof(byte) );
	*scale = Q_INFINITY;

	PlueckerFromRay( rayPl, start, dir );

	// ray sidedness for edges
	for ( i = 0; i < self->numEdges; i++ ) {
		PlueckerFromLine( pl, self->verts[ self->edges[i].verts[1] ].xyz, self->verts[ self->edges[i].verts[0] ].xyz );
		d = PlueckerPermutedInnerProduct( pl, rayPl );
		sidedness[ i ] = FLOATSIGNBITSET( d );
	}

	// test triangles
	for ( i = 0; i < self->numEdgeIndexes; i += 3 ) {
		i0 = self->edgeIndexes[i+0];
		i1 = self->edgeIndexes[i+1];
		i2 = self->edgeIndexes[i+2];
		s0 = sidedness[abs(i0)] ^ INTSIGNBITSET( i0 );
		s1 = sidedness[abs(i1)] ^ INTSIGNBITSET( i1 );
		s2 = sidedness[abs(i2)] ^ INTSIGNBITSET( i2 );

		if ( s0 & s1 & s2 ) {
			if ( !PlaneInitWithPoints( plane, self->verts[self->indexes[i+0]].xyz, self->verts[self->indexes[i+1]].xyz, self->verts[self->indexes[i+2]].xyz, qtrue ) ) {
				ii.FreeMemory( sidedness );
				return qfalse;
			}
			PlaneRayIntersection( plane, start, dir, &s );
			if ( fabs( s ) < fabs( *scale ) ) {
				*scale = s;
			}
		} else if ( !backFaceCull && !(s0 | s1 | s2) ) {
			if ( !PlaneInitWithPoints( plane, self->verts[self->indexes[i+0]].xyz, self->verts[self->indexes[i+1]].xyz, self->verts[self->indexes[i+2]].xyz, qtrue ) ) {
				ii.FreeMemory( sidedness );
				return qfalse;
			}
			PlaneRayIntersection( plane, start, dir, &s );
			if ( fabs( s ) < fabs( *scale ) ) {
				*scale = s;
			}
		}
	}

	ii.FreeMemory( sidedness );

	if ( fabs( *scale ) < Q_INFINITY ) {
		return qtrue;
	}
	return qfalse;
}

static void AppendEdge( surface_t *self, surfaceEdge_t *e ) {
    if ( self->numEdges == self->sizeEdges ) {
        self->sizeEdges = self->sizeEdges > 0 ? self->sizeEdges * 1.5 : 16;
        surfaceEdge_t *newEdges = ii.GetMemory( self->sizeEdges * sizeof( *newEdges ) );
        if ( self->edges ) {
            memcpy( newEdges, self->edges, self->numEdges * sizeof( *newEdges ) );
            ii.FreeMemory( self->edges );
        }
        self->edges = newEdges;
    }

	self->edges[self->numEdges++] = *e;
}

/*
=================
SurfaceGenerateEdgeIndexes

  Assumes each edge is shared by at most two triangles.
=================
*/
void SurfaceGenerateEdgeIndexes( surface_t *self ) {
	int i, j, i0, i1, i2, s, v0, v1, edgeNum;
	int *index, *vertexEdges, *edgeChain;
	surfaceEdge_t e[3];
    int maxEdges;

	vertexEdges = (int *) ii.GetMemory( self->numVerts * sizeof( int ) );
	memset( vertexEdges, -1, self->numVerts * sizeof( int ) );
	edgeChain = (int *) ii.GetMemory( self->numIndexes * sizeof( int ) );

	self->numEdgeIndexes = self->numIndexes;
	if ( self->sizeEdgeIndexes < self->numIndexes ) {
        if ( self->edgeIndexes ) {
		    ii.FreeMemory( self->edgeIndexes );
        }
		self->edgeIndexes = ii.GetMemory( self->numEdgeIndexes * sizeof( int ) );
	}

	self->numEdges = 0;

	// the first edge is a dummy
	e[0].verts[0] = e[0].verts[1] = e[0].tris[0] = e[0].tris[1] = 0;
    AppendEdge( self, &e[0] );

	for ( i = 0; i < self->numIndexes; i += 3 ) {
		index = self->indexes + i;
		// vertex numbers
		i0 = index[0];
		i1 = index[1];
		i2 = index[2];
		// setup edges each with smallest vertex number first
		s = INTSIGNBITSET(i1 - i0);
		e[0].verts[0] = index[s];
		e[0].verts[1] = index[s^1];
		s = INTSIGNBITSET(i2 - i1) + 1;
		e[1].verts[0] = index[s];
		e[1].verts[1] = index[s^3];
		s = INTSIGNBITSET(i2 - i0) << 1;
		e[2].verts[0] = index[s];
		e[2].verts[1] = index[s^2];
		// get edges
		for ( j = 0; j < 3; j++ ) {
			v0 = e[j].verts[0];
			v1 = e[j].verts[1];
			for ( edgeNum = vertexEdges[v0]; edgeNum >= 0; edgeNum = edgeChain[edgeNum] ) {
				if ( self->edges[edgeNum].verts[1] == v1 ) {
					break;
				}
			}
			// if the edge does not yet exist
			if ( edgeNum < 0 ) {
				e[j].tris[0] = e[j].tris[1] = -1;
				edgeNum = self->numEdges;
                AppendEdge( self, &e[j] );
				edgeChain[edgeNum] = vertexEdges[v0];
				vertexEdges[v0] = edgeNum;
			}
			// update edge index and edge tri references
			if ( index[j] == v0 ) {
				assert( self->edges[edgeNum].tris[0] == -1 ); // edge may not be shared by more than two triangles
				self->edges[edgeNum].tris[0] = i;
				self->edgeIndexes[i+j] = edgeNum;
			} else {
				assert( self->edges[edgeNum].tris[1] == -1 ); // edge may not be shared by more than two triangles
				self->edges[edgeNum].tris[1] = i;
				self->edgeIndexes[i+j] = -edgeNum;
			}
		}
	}

	ii.FreeMemory( vertexEdges );
	ii.FreeMemory( edgeChain );
}

/*
=================
SurfaceFindEdge
=================
*/
int SurfaceFindEdge( const surface_t *self, int v1, int v2 ) {
	int i, firstVert, secondVert;

	if ( v1 < v2 ) {
		firstVert = v1;
		secondVert = v2;
	} else {
		firstVert = v2;
		secondVert = v1;
	}
	for ( i = 1; i < self->numEdges; i++ ) {
		if ( self->edges[i].verts[0] == firstVert ) {
			if ( self->edges[i].verts[1] == secondVert ) {
				break;
			}
		}
	}
	if ( i < self->numEdges ) {
		return v1 < v2 ? i : -i;
	}
	return 0;
}
