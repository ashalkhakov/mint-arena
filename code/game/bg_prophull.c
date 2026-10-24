/*
===========================================================================
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.

This file is part of Spearmint Source Code.

Spearmint Source Code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 3 of the License,
or (at your option) any later version.

Spearmint Source Code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Spearmint Source Code.  If not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/
// bg_prophull.c -- convex trace models that wrap a model's mesh, for props
//
// A trace model holds at most MAX_TRACEMODEL_POLYS faces and
// MAX_TRACEMODEL_EDGES edges, too few for a real convex hull of a mesh.
// Instead the mesh is wrapped in a k-DOP: the planes touching it in 26
// directions (the axes, the edge and the corner diagonals of a cube). While
// the polytope has too many faces, edges or vertices, the plane whose
// removal adds the least volume goes, never one of the six axis planes, so
// the shape stays closed. The game and the cgame both build it from the
// same file, so the cgame predicts against what the game collides with.

#ifdef CGAME
#include "../cgame/cg_local.h"
#else
#include "g_local.h"
#endif
#include "../idlib/qfiles.h"

#define HULL_MAX_PLANES		26
#define HULL_MAX_VERTS		128
#define HULL_MAX_FACEVERTS	32
#define HULL_EPSILON		0.01f		// also the merge distance of vertices, times ten
#define HULL_MIN_SIZE		2.0f		// flat meshes get some thickness

typedef struct {
	vec3_t	normal;
	float	dist;
} hullPlane_t;

typedef struct {
	int		numVerts;
	vec3_t	verts[HULL_MAX_VERTS];

	int		numFaces;
	int		facePlane[HULL_MAX_PLANES];
	int		faceNumVerts[HULL_MAX_PLANES];
	int		faceVerts[HULL_MAX_PLANES][HULL_MAX_FACEVERTS];	// counter-clockwise seen from outside

	int		numEdges;
	float	volume;
} hull_t;

/*
================
Hull_Directions

The 26 directions of the k-DOP, the axes first.
================
*/
static int Hull_Directions( vec3_t dirs[HULL_MAX_PLANES] ) {
	int x, y, z, n = 0, pass;

	// pass 0: axes, 1: edge diagonals, 2: corners
	for ( pass = 0; pass < 3; pass++ ) {
		for ( x = -1; x <= 1; x++ ) {
			for ( y = -1; y <= 1; y++ ) {
				for ( z = -1; z <= 1; z++ ) {
					int nonZero = ( x != 0 ) + ( y != 0 ) + ( z != 0 );
					if ( nonZero != pass + 1 ) {
						continue;
					}
					VectorSet( dirs[n], x, y, z );
					VectorNormalize( dirs[n] );
					n++;
				}
			}
		}
	}
	return n;
}

/*
================
Hull_IsClosed

Every edge of a face runs the other way on exactly one other face, and
V - E + F = 2. Planes nearly on top of each other can make slivers that
aren't.
================
*/
static qboolean Hull_IsClosed( const hull_t *hull ) {
	int f, g, j, k, a, b, found;

	for ( f = 0; f < hull->numFaces; f++ ) {
		for ( j = 0; j < hull->faceNumVerts[f]; j++ ) {
			a = hull->faceVerts[f][j];
			b = hull->faceVerts[f][( j + 1 ) % hull->faceNumVerts[f]];
			found = 0;
			for ( g = 0; g < hull->numFaces; g++ ) {
				for ( k = 0; k < hull->faceNumVerts[g]; k++ ) {
					if ( hull->faceVerts[g][k] == b && hull->faceVerts[g][( k + 1 ) % hull->faceNumVerts[g]] == a ) {
						found++;
					}
				}
			}
			if ( found != 1 ) {
				return qfalse;
			}
		}
	}
	return hull->numVerts - hull->numEdges + hull->numFaces == 2;
}

/*
================
Hull_Build

The polytope the planes bound: its vertices from every three planes that
meet inside all the others, its faces the planes with three or more of
them. Fails when it's too big to hold.
================
*/
static qboolean Hull_Build( const hullPlane_t *planes, int numPlanes, hull_t *hull ) {
	int i, j, k, m, n;
	vec3_t center, u, v, d;
	float angles[HULL_MAX_FACEVERTS];

	hull->numVerts = 0;
	hull->numFaces = 0;
	hull->numEdges = 0;
	hull->volume = 0.0f;

	for ( i = 0; i < numPlanes; i++ ) {
		for ( j = i + 1; j < numPlanes; j++ ) {
			for ( k = j + 1; k < numPlanes; k++ ) {
				const float *a = planes[i].normal, *b = planes[j].normal, *c = planes[k].normal;
				vec3_t bc, ca, ab, p;
				float det;

				CrossProduct( b, c, bc );
				det = DotProduct( a, bc );
				if ( fabs( det ) < 1e-6f ) {
					continue;
				}
				CrossProduct( c, a, ca );
				CrossProduct( a, b, ab );
				VectorScale( bc, planes[i].dist, p );
				VectorMA( p, planes[j].dist, ca, p );
				VectorMA( p, planes[k].dist, ab, p );
				VectorScale( p, 1.0f / det, p );

				// inside all the planes
				for ( m = 0; m < numPlanes; m++ ) {
					if ( DotProduct( planes[m].normal, p ) > planes[m].dist + HULL_EPSILON ) {
						break;
					}
				}
				if ( m < numPlanes ) {
					continue;
				}
				// not one already found
				for ( m = 0; m < hull->numVerts; m++ ) {
					if ( Distance( hull->verts[m], p ) < HULL_EPSILON * 10.0f ) {
						break;
					}
				}
				if ( m < hull->numVerts ) {
					continue;
				}
				if ( hull->numVerts == HULL_MAX_VERTS ) {
					return qfalse;
				}
				VectorCopy( p, hull->verts[hull->numVerts] );
				hull->numVerts++;
			}
		}
	}
	if ( hull->numVerts < 4 ) {
		return qfalse;
	}

	VectorClear( center );
	for ( i = 0; i < hull->numVerts; i++ ) {
		VectorAdd( center, hull->verts[i], center );
	}
	VectorScale( center, 1.0f / hull->numVerts, center );

	for ( i = 0; i < numPlanes; i++ ) {
		int f = hull->numFaces;
		float area = 0.0f;

		n = 0;
		for ( j = 0; j < hull->numVerts; j++ ) {
			if ( fabs( DotProduct( planes[i].normal, hull->verts[j] ) - planes[i].dist ) < HULL_EPSILON * 10.0f ) {
				if ( n == HULL_MAX_FACEVERTS ) {
					return qfalse;
				}
				hull->faceVerts[f][n++] = j;
			}
		}
		if ( n < 3 ) {
			continue;	// doesn't touch the polytope
		}

		// sort counter-clockwise around the face's middle, seen from outside
		PerpendicularVector( u, planes[i].normal );
		CrossProduct( planes[i].normal, u, v );
		VectorClear( d );
		for ( j = 0; j < n; j++ ) {
			VectorAdd( d, hull->verts[hull->faceVerts[f][j]], d );
		}
		VectorScale( d, 1.0f / n, d );
		for ( j = 0; j < n; j++ ) {
			vec3_t r;
			VectorSubtract( hull->verts[hull->faceVerts[f][j]], d, r );
			angles[j] = atan2( DotProduct( r, v ), DotProduct( r, u ) );
		}
		for ( j = 1; j < n; j++ ) {
			for ( k = j; k > 0 && angles[k - 1] > angles[k]; k-- ) {
				float ta = angles[k]; int tv = hull->faceVerts[f][k];
				angles[k] = angles[k - 1]; hull->faceVerts[f][k] = hull->faceVerts[f][k - 1];
				angles[k - 1] = ta; hull->faceVerts[f][k - 1] = tv;
			}
		}

		// vertices along the face's edges are no corners: a plane that only
		// grazes the polytope finds some, and the faces next to it then too
		for ( j = 0; j < n && n >= 3; ) {
			const float *prev = hull->verts[hull->faceVerts[f][( j + n - 1 ) % n]];
			const float *cur = hull->verts[hull->faceVerts[f][j]];
			const float *next = hull->verts[hull->faceVerts[f][( j + 1 ) % n]];
			vec3_t e1, e2, cross;

			VectorSubtract( cur, prev, e1 );
			VectorSubtract( next, cur, e2 );
			CrossProduct( e1, e2, cross );
			if ( VectorLength( cross ) < HULL_EPSILON * ( VectorLength( e1 ) + VectorLength( e2 ) ) ) {
				memmove( &hull->faceVerts[f][j], &hull->faceVerts[f][j + 1], ( n - j - 1 ) * sizeof( int ) );
				n--;
			} else {
				j++;
			}
		}
		if ( n < 3 ) {
			continue;
		}

		// volume: the pyramid from the middle of the polytope to the face
		for ( j = 1; j + 1 < n; j++ ) {
			vec3_t e1, e2, cross;
			VectorSubtract( hull->verts[hull->faceVerts[f][j]], hull->verts[hull->faceVerts[f][0]], e1 );
			VectorSubtract( hull->verts[hull->faceVerts[f][j + 1]], hull->verts[hull->faceVerts[f][0]], e2 );
			CrossProduct( e1, e2, cross );
			area += 0.5f * VectorLength( cross );
		}
		if ( area < HULL_EPSILON ) {
			continue;
		}
		hull->volume += area * ( planes[i].dist - DotProduct( planes[i].normal, center ) ) / 3.0f;

		hull->facePlane[f] = i;
		hull->faceNumVerts[f] = n;
		hull->numFaces++;
		hull->numEdges += n;
	}
	hull->numEdges /= 2;	// every edge is on two faces

	// keep the vertices the faces use, the corners
	{
		int remap[HULL_MAX_VERTS], used = 0;

		for ( i = 0; i < hull->numVerts; i++ ) {
			remap[i] = -1;
		}
		for ( i = 0; i < hull->numFaces; i++ ) {
			for ( j = 0; j < hull->faceNumVerts[i]; j++ ) {
				remap[hull->faceVerts[i][j]] = 0;
			}
		}
		for ( i = 0; i < hull->numVerts; i++ ) {
			if ( remap[i] == 0 ) {
				VectorCopy( hull->verts[i], hull->verts[used] );
				remap[i] = used++;
			}
		}
		hull->numVerts = used;
		for ( i = 0; i < hull->numFaces; i++ ) {
			for ( j = 0; j < hull->faceNumVerts[i]; j++ ) {
				hull->faceVerts[i][j] = remap[hull->faceVerts[i][j]];
			}
		}
	}
	return hull->numFaces >= 4 && Hull_IsClosed( hull );
}

static qboolean Hull_IsAxis( const hullPlane_t *plane ) {
	return fabs( plane->normal[0] ) + fabs( plane->normal[1] ) + fabs( plane->normal[2] ) < 1.01f;
}

static qboolean Hull_Fits( const hull_t *hull ) {
	int i;

	if ( hull->numFaces > MAX_TRACEMODEL_POLYS || hull->numEdges > MAX_TRACEMODEL_EDGES
			|| hull->numVerts > MAX_TRACEMODEL_VERTS ) {
		return qfalse;
	}
	for ( i = 0; i < hull->numFaces; i++ ) {
		if ( hull->faceNumVerts[i] > MAX_TRACEMODEL_POLYEDGES ) {
			return qfalse;
		}
	}
	return qtrue;
}

/*
================
Hull_FromPoints

The k-DOP of the points, with as few planes taken out as fit a trace model.
================
*/
static qboolean Hull_FromDirections( const vec3_t *points, int numPoints, int numDirs, hull_t *hull, hullPlane_t planes[HULL_MAX_PLANES] ) {
	hullPlane_t trial[HULL_MAX_PLANES];
	vec3_t dirs[HULL_MAX_PLANES];
	hull_t test;
	int numPlanes, i, j, n, best;
	float bestVolume;

	Hull_Directions( dirs );
	numPlanes = numDirs;
	for ( i = 0; i < numPlanes; i++ ) {
		VectorCopy( dirs[i], planes[i].normal );
		planes[i].dist = -999999.0f;
		for ( j = 0; j < numPoints; j++ ) {
			planes[i].dist = MAX( planes[i].dist, DotProduct( dirs[i], points[j] ) );
		}
	}

	// flat meshes: keep the axis planes apart
	for ( i = 0; i < 3; i++ ) {
		float lo = -planes[i].dist, hi = planes[5 - i].dist;	// dirs 0..2 are -x,-y,-z, 3..5 +z,+y,+x
		if ( hi - lo < HULL_MIN_SIZE ) {
			float grow = ( HULL_MIN_SIZE - ( hi - lo ) ) * 0.5f;
			planes[i].dist += grow;
			planes[5 - i].dist += grow;
		}
	}

	if ( !Hull_Build( planes, numPlanes, hull ) ) {
		return qfalse;
	}

	while ( !Hull_Fits( hull ) ) {
		// planes that are no faces go first, they change nothing; the axis
		// planes stay, so it stays closed whatever goes later
		n = 0;
		for ( i = 0; i < numPlanes; i++ ) {
			for ( j = 0; j < hull->numFaces && hull->facePlane[j] != i; j++ ) {
			}
			if ( j < hull->numFaces || Hull_IsAxis( &planes[i] ) ) {
				trial[n++] = planes[i];
			}
		}
		if ( n < numPlanes ) {
			memcpy( planes, trial, n * sizeof( planes[0] ) );
			numPlanes = n;
			if ( !Hull_Build( planes, numPlanes, hull ) ) {
				return qfalse;
			}
			continue;
		}

		// take out the diagonal plane that adds the least volume
		best = -1;
		bestVolume = 0.0f;
		for ( i = 0; i < numPlanes; i++ ) {
			if ( Hull_IsAxis( &planes[i] ) ) {
				continue;
			}
			n = 0;
			for ( j = 0; j < numPlanes; j++ ) {
				if ( j != i ) {
					trial[n++] = planes[j];
				}
			}
			if ( !Hull_Build( trial, n, &test ) ) {
				continue;
			}
			if ( best < 0 || test.volume < bestVolume ) {
				best = i;
				bestVolume = test.volume;
			}
		}
		if ( best < 0 ) {
			return qfalse;
		}
		memmove( &planes[best], &planes[best + 1], ( numPlanes - best - 1 ) * sizeof( planes[0] ) );
		numPlanes--;
		if ( !Hull_Build( planes, numPlanes, hull ) ) {
			return qfalse;
		}
	}
	return qtrue;
}

/*
================
Hull_FromPoints

The k-DOP of all 26 directions, or failing that of the axes and the edge
diagonals, or a box.
================
*/
static qboolean Hull_FromPoints( const vec3_t *points, int numPoints, hull_t *hull, hullPlane_t planes[HULL_MAX_PLANES] ) {
	static const int numDirs[] = { 26, 18, 6 };
	int i;

	for ( i = 0; i < ARRAY_LEN( numDirs ); i++ ) {
		if ( Hull_FromDirections( points, numPoints, numDirs[i], hull, planes ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
================
Hull_TraceModel
================
*/
static void Hull_TraceModel( const hull_t *hull, const hullPlane_t *planes, traceModel_t *trm ) {
	int i, j, k, a, b, edge;

	memset( trm, 0, sizeof( *trm ) );
	trm->type = TRM_CUSTOM;
	trm->isConvex = qtrue;

	trm->numVerts = hull->numVerts;
	ClearBounds( trm->bounds[0], trm->bounds[1] );
	for ( i = 0; i < hull->numVerts; i++ ) {
		VectorCopy( hull->verts[i], trm->verts[i] );
		AddPointToBounds( hull->verts[i], trm->bounds[0], trm->bounds[1] );
	}
	VectorAdd( trm->bounds[0], trm->bounds[1], trm->offset );
	VectorScale( trm->offset, 0.5f, trm->offset );

	// edges from 1, negative when a face runs along one backwards
	trm->numEdges = 0;
	trm->numPolys = hull->numFaces;
	for ( i = 0; i < hull->numFaces; i++ ) {
		traceModelPoly_t *poly = &trm->polys[i];
		int n = hull->faceNumVerts[i];

		poly->numEdges = n;
		for ( j = 0; j < n; j++ ) {
			a = hull->faceVerts[i][j];
			b = hull->faceVerts[i][( j + 1 ) % n];
			edge = 0;
			for ( k = 1; k <= trm->numEdges; k++ ) {
				if ( trm->edges[k].v[0] == a && trm->edges[k].v[1] == b ) {
					edge = k;
					break;
				}
				if ( trm->edges[k].v[0] == b && trm->edges[k].v[1] == a ) {
					edge = -k;
					break;
				}
			}
			if ( !edge ) {
				edge = ++trm->numEdges;
				trm->edges[edge].v[0] = a;
				trm->edges[edge].v[1] = b;
			}
			poly->edges[j] = edge;
		}

		VectorCopy( planes[hull->facePlane[i]].normal, poly->normal );
		poly->dist = planes[hull->facePlane[i]].dist;
		ClearBounds( poly->bounds[0], poly->bounds[1] );
		for ( j = 0; j < n; j++ ) {
			AddPointToBounds( hull->verts[hull->faceVerts[i][j]], poly->bounds[0], poly->bounds[1] );
		}
	}

	TraceModelGenerateEdgeNormals( trm );
}

/*
================
Hull_IQMPoints

An IQM's vertices in its bind pose (a prop autorig made a model of its own,
Hiro's sword, is a still mesh about its joint), scaled.
================
*/
static int Hull_IQMPoints( const void *buffer, long length, float scale, vec3_t **pointsOut ) {
	const iqmHeader_t *header = (const iqmHeader_t *)buffer;
	const iqmVertexArray_t *va;
	const float *pos;
	vec3_t *points;
	int numVerts, i, k;

	if ( length < (long)sizeof( iqmHeader_t ) || LittleLong( header->version ) != IQM_VERSION
		|| LittleLong( header->ofs_vertexarrays ) + LittleLong( header->num_vertexarrays ) * sizeof( iqmVertexArray_t ) > (unsigned)length ) {
		return 0;
	}
	numVerts = LittleLong( header->num_vertexes );
	va = (const iqmVertexArray_t *)( (const byte *)buffer + LittleLong( header->ofs_vertexarrays ) );
	for ( i = 0; i < (int)LittleLong( header->num_vertexarrays ); i++, va++ ) {
		if ( LittleLong( va->type ) != IQM_POSITION || LittleLong( va->format ) != IQM_FLOAT || LittleLong( va->size ) != 3
			|| LittleLong( va->offset ) + numVerts * 3 * sizeof( float ) > (unsigned)length || numVerts <= 0 ) {
			continue;
		}
		pos = (const float *)( (const byte *)buffer + LittleLong( va->offset ) );
		points = trap_HeapMalloc( numVerts * sizeof( vec3_t ) );
		for ( k = 0; k < numVerts; k++ ) {
			points[k][0] = LittleFloat( pos[k * 3 + 0] ) * scale;
			points[k][1] = LittleFloat( pos[k * 3 + 1] ) * scale;
			points[k][2] = LittleFloat( pos[k * 3 + 2] ) * scale;
		}
		*pointsOut = points;
		return numVerts;
	}
	return 0;
}

/*
================
Hull_ModelPoints

The vertices of a frame of every surface of an MD3 (the first frame for one
it doesn't have), or an IQM's, scaled.
================
*/
static int Hull_ModelPoints( const char *modelName, float scale, int frame, vec3_t **pointsOut ) {
	void *buffer;
	long length;
	const md3Header_t *header;
	const md3Surface_t *surf;
	vec3_t *points;
	int numPoints = 0, i, j;

	*pointsOut = NULL;
	length = BG_FS_ReadFile( modelName, &buffer );
	if ( length < (long)sizeof( md3Header_t ) || !buffer ) {
		BG_FS_FreeFile( buffer );
		return 0;
	}
	if ( length >= (long)sizeof( iqmHeader_t ) && !memcmp( buffer, IQM_MAGIC, sizeof( IQM_MAGIC ) ) ) {
		numPoints = Hull_IQMPoints( buffer, length, scale, pointsOut );
		BG_FS_FreeFile( buffer );
		return numPoints;
	}
	header = (const md3Header_t *)buffer;
	if ( LittleLong( header->ident ) != MD3_IDENT || LittleLong( header->version ) != MD3_VERSION ) {
		BG_FS_FreeFile( buffer );
		return 0;
	}

	// count, then copy
	surf = (const md3Surface_t *)( (const byte *)buffer + LittleLong( header->ofsSurfaces ) );
	for ( i = 0; i < LittleLong( header->numSurfaces ); i++ ) {
		numPoints += LittleLong( surf->numVerts );
		surf = (const md3Surface_t *)( (const byte *)surf + LittleLong( surf->ofsEnd ) );
	}
	if ( !numPoints ) {
		BG_FS_FreeFile( buffer );
		return 0;
	}
	points = trap_HeapMalloc( numPoints * sizeof( vec3_t ) );

	numPoints = 0;
	surf = (const md3Surface_t *)( (const byte *)buffer + LittleLong( header->ofsSurfaces ) );
	if ( frame < 0 || frame >= LittleLong( header->numFrames ) ) {
		frame = 0;
	}
	for ( i = 0; i < LittleLong( header->numSurfaces ); i++ ) {
		const md3XyzNormal_t *xyz = (const md3XyzNormal_t *)( (const byte *)surf + LittleLong( surf->ofsXyzNormals ) )
									+ frame * LittleLong( surf->numVerts );
		for ( j = 0; j < LittleLong( surf->numVerts ); j++ ) {
			points[numPoints][0] = LittleShort( xyz[j].xyz[0] ) * MD3_XYZ_SCALE * scale;
			points[numPoints][1] = LittleShort( xyz[j].xyz[1] ) * MD3_XYZ_SCALE * scale;
			points[numPoints][2] = LittleShort( xyz[j].xyz[2] ) * MD3_XYZ_SCALE * scale;
			numPoints++;
		}
		surf = (const md3Surface_t *)( (const byte *)surf + LittleLong( surf->ofsEnd ) );
	}

	BG_FS_FreeFile( buffer );
	*pointsOut = points;
	return numPoints;
}

/*
================
BG_PropModelTraceModel

A convex trace model wrapping the model's mesh in the frame at the scale,
made once per model, scale and frame. Fails for a model that can't be read.
================
*/
#define MAX_HULL_CACHE	64

typedef struct {
	char			model[MAX_QPATH];
	float			scale;
	int				frame;
	qboolean		valid;
	traceModel_t	trm;
} hullCache_t;

static hullCache_t	hullCache[MAX_HULL_CACHE];
static int			numHullCache;

qboolean BG_PropModelTraceModel( const char *modelName, float scale, int frame, traceModel_t *trm ) {
	hullCache_t *cache;
	vec3_t *points;
	hull_t *hull;
	int i, numPoints;

	if ( scale <= 0.0f ) {
		scale = 1.0f;
	}

	for ( i = 0; i < numHullCache; i++ ) {
		if ( hullCache[i].scale == scale && hullCache[i].frame == frame && !Q_stricmp( hullCache[i].model, modelName ) ) {
			if ( hullCache[i].valid ) {
				*trm = hullCache[i].trm;
			}
			return hullCache[i].valid;
		}
	}

	cache = &hullCache[numHullCache < MAX_HULL_CACHE ? numHullCache++ : MAX_HULL_CACHE - 1];
	Q_strncpyz( cache->model, modelName, sizeof( cache->model ) );
	cache->scale = scale;
	cache->frame = frame;
	cache->valid = qfalse;

	numPoints = Hull_ModelPoints( modelName, scale, frame, &points );
	if ( numPoints ) {
		hullPlane_t planes[HULL_MAX_PLANES];

		hull = trap_HeapMalloc( sizeof( *hull ) );
		if ( Hull_FromPoints( (const vec3_t *)points, numPoints, hull, planes ) ) {
			Hull_TraceModel( hull, planes, &cache->trm );
			cache->valid = qtrue;
		}
		trap_HeapFree( hull );
		trap_HeapFree( points );
	}
	if ( !cache->valid ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: no collision hull for %s\n", modelName );
		return qfalse;
	}

	*trm = cache->trm;
	return qtrue;
}

/*
===============================================================================

COMPOUND HULLS

A concave model, a tree say, is badly wrapped by one convex hull: the hull
of the crown reaches down to the ground around the trunk. So, as Half-Life
does with its sets of hit boxes, the model collides as several convex pieces
instead. The mesh is cut in two with a plane, again and again: each time the
piece whose best cut shrinks its bounding volume the most is cut, while that
saves a good share of it and there are pieces to spare. The triangles are
clipped at the planes so the pieces meet without gaps. Each piece is then
wrapped as a single model is.

===============================================================================
*/

#define PART_CANDIDATE_CUTS		7		// cuts tried along each axis
#define PART_MIN_SAVING			0.3f	// a cut must save this share of the piece's box
#define PART_MIN_SHARE			0.02f	// pieces with less of the whole box aren't cut

typedef struct {
	vec3_t	*tris;			// three points a triangle
	int		numTris;
	vec3_t	bounds[2];
} hullPiece_t;

/*
================
Hull_ModelTriangles

The triangles of a frame of every surface of an MD3, scaled.
================
*/
static int Hull_ModelTriangles( const char *modelName, float scale, int frame, vec3_t **trisOut ) {
	void *buffer;
	long length;
	const md3Header_t *header;
	const md3Surface_t *surf;
	vec3_t *tris;
	int numTris = 0, i, j, k, f;

	*trisOut = NULL;
	length = BG_FS_ReadFile( modelName, &buffer );
	if ( length < (long)sizeof( md3Header_t ) || !buffer ) {
		BG_FS_FreeFile( buffer );
		return 0;
	}
	header = (const md3Header_t *)buffer;
	if ( LittleLong( header->ident ) != MD3_IDENT || LittleLong( header->version ) != MD3_VERSION ) {
		BG_FS_FreeFile( buffer );
		return 0;
	}

	surf = (const md3Surface_t *)( (const byte *)buffer + LittleLong( header->ofsSurfaces ) );
	for ( i = 0; i < LittleLong( header->numSurfaces ); i++ ) {
		numTris += LittleLong( surf->numTriangles );
		surf = (const md3Surface_t *)( (const byte *)surf + LittleLong( surf->ofsEnd ) );
	}
	if ( !numTris ) {
		BG_FS_FreeFile( buffer );
		return 0;
	}
	tris = trap_HeapMalloc( numTris * 3 * sizeof( vec3_t ) );

	f = ( frame < 0 || frame >= LittleLong( header->numFrames ) ) ? 0 : frame;
	numTris = 0;
	surf = (const md3Surface_t *)( (const byte *)buffer + LittleLong( header->ofsSurfaces ) );
	for ( i = 0; i < LittleLong( header->numSurfaces ); i++ ) {
		const md3XyzNormal_t *xyz = (const md3XyzNormal_t *)( (const byte *)surf + LittleLong( surf->ofsXyzNormals ) )
									+ f * LittleLong( surf->numVerts );
		const md3Triangle_t *tri = (const md3Triangle_t *)( (const byte *)surf + LittleLong( surf->ofsTriangles ) );
		int numVerts = LittleLong( surf->numVerts );

		for ( j = 0; j < LittleLong( surf->numTriangles ); j++, tri++ ) {
			for ( k = 0; k < 3; k++ ) {
				int v = LittleLong( tri->indexes[k] );

				if ( v < 0 || v >= numVerts ) {
					v = 0;
				}
				tris[numTris * 3 + k][0] = LittleShort( xyz[v].xyz[0] ) * MD3_XYZ_SCALE * scale;
				tris[numTris * 3 + k][1] = LittleShort( xyz[v].xyz[1] ) * MD3_XYZ_SCALE * scale;
				tris[numTris * 3 + k][2] = LittleShort( xyz[v].xyz[2] ) * MD3_XYZ_SCALE * scale;
			}
			numTris++;
		}
		surf = (const md3Surface_t *)( (const byte *)surf + LittleLong( surf->ofsEnd ) );
	}

	BG_FS_FreeFile( buffer );
	*trisOut = tris;
	return numTris;
}

static float Hull_BoxVolume( const vec3_t bounds[2] ) {
	vec3_t size;
	int i;

	for ( i = 0; i < 3; i++ ) {
		// flat pieces still have some volume
		size[i] = bounds[1][i] - bounds[0][i];
		if ( size[i] < HULL_MIN_SIZE ) {
			size[i] = HULL_MIN_SIZE;
		}
	}
	return size[0] * size[1] * size[2];
}

/*
================
Hull_ClipTriangle

The parts of a triangle below (side 0) or above (side 1) dist on the axis,
as up to two triangles. Returns their count.
================
*/
static int Hull_ClipTriangle( const vec3_t tri[3], int axis, float dist, int side, vec3_t out[6] ) {
	vec3_t poly[4];
	float d[3], frac;
	int i, n = 0, numTris;

	for ( i = 0; i < 3; i++ ) {
		d[i] = side ? tri[i][axis] - dist : dist - tri[i][axis];
	}
	for ( i = 0; i < 3; i++ ) {
		const float *a = tri[i], *b = tri[( i + 1 ) % 3];
		float da = d[i], db = d[( i + 1 ) % 3];

		if ( da >= 0 ) {
			VectorCopy( a, poly[n] );
			n++;
		}
		if ( ( da >= 0 ) != ( db >= 0 ) && n < 4 ) {
			frac = da / ( da - db );
			poly[n][0] = a[0] + frac * ( b[0] - a[0] );
			poly[n][1] = a[1] + frac * ( b[1] - a[1] );
			poly[n][2] = a[2] + frac * ( b[2] - a[2] );
			n++;
		}
	}
	if ( n < 3 ) {
		return 0;
	}
	numTris = 0;
	for ( i = 1; i + 1 < n; i++ ) {
		VectorCopy( poly[0], out[numTris * 3 + 0] );
		VectorCopy( poly[i], out[numTris * 3 + 1] );
		VectorCopy( poly[i + 1], out[numTris * 3 + 2] );
		numTris++;
	}
	return numTris;
}

static void Hull_PieceBounds( hullPiece_t *piece ) {
	int i;

	ClearBounds( piece->bounds[0], piece->bounds[1] );
	for ( i = 0; i < piece->numTris * 3; i++ ) {
		AddPointToBounds( piece->tris[i], piece->bounds[0], piece->bounds[1] );
	}
}

/*
================
Hull_SplitPiece

Cuts the piece where it saves the most bounding volume, or keeps it whole.
================
*/
static qboolean Hull_SplitPiece( const hullPiece_t *piece, hullPiece_t *below, hullPiece_t *above, float *saving ) {
	vec3_t sideBounds[2][2], clipped[6];
	float volume, best, dist, bestDist = 0, v;
	int axis, cut, side, i, n, j, bestAxis = -1;

	*saving = 0;
	volume = Hull_BoxVolume( (const vec3_t *)piece->bounds );
	best = volume;
	for ( axis = 0; axis < 3; axis++ ) {
		for ( cut = 1; cut <= PART_CANDIDATE_CUTS; cut++ ) {
			dist = piece->bounds[0][axis] + ( piece->bounds[1][axis] - piece->bounds[0][axis] ) * cut / ( PART_CANDIDATE_CUTS + 1 );
			for ( side = 0; side < 2; side++ ) {
				ClearBounds( sideBounds[side][0], sideBounds[side][1] );
			}
			for ( i = 0; i < piece->numTris; i++ ) {
				for ( side = 0; side < 2; side++ ) {
					n = Hull_ClipTriangle( (const vec3_t *)&piece->tris[i * 3], axis, dist, side, clipped );
					for ( j = 0; j < n * 3; j++ ) {
						AddPointToBounds( clipped[j], sideBounds[side][0], sideBounds[side][1] );
					}
				}
			}
			if ( sideBounds[0][0][0] > sideBounds[0][1][0] || sideBounds[1][0][0] > sideBounds[1][1][0] ) {
				continue;	// all on one side
			}
			v = Hull_BoxVolume( (const vec3_t *)sideBounds[0] ) + Hull_BoxVolume( (const vec3_t *)sideBounds[1] );
			if ( v < best ) {
				best = v;
				bestAxis = axis;
				bestDist = dist;
			}
		}
	}
	if ( bestAxis < 0 ) {
		return qfalse;
	}
	*saving = volume - best;
	if ( !below ) {
		return qtrue;	// just asking
	}

	// cut it
	below->tris = trap_HeapMalloc( piece->numTris * 6 * sizeof( vec3_t ) );
	above->tris = trap_HeapMalloc( piece->numTris * 6 * sizeof( vec3_t ) );
	below->numTris = above->numTris = 0;
	for ( i = 0; i < piece->numTris; i++ ) {
		n = Hull_ClipTriangle( (const vec3_t *)&piece->tris[i * 3], bestAxis, bestDist, 0, clipped );
		memcpy( below->tris[below->numTris * 3], clipped, n * 3 * sizeof( vec3_t ) );
		below->numTris += n;
		n = Hull_ClipTriangle( (const vec3_t *)&piece->tris[i * 3], bestAxis, bestDist, 1, clipped );
		memcpy( above->tris[above->numTris * 3], clipped, n * 3 * sizeof( vec3_t ) );
		above->numTris += n;
	}
	Hull_PieceBounds( below );
	Hull_PieceBounds( above );
	return qtrue;
}

/*
================
Hull_SplitByPiece

The model's separate pieces of mesh (triangles sharing a corner, a coffin's
boards each its own box): each its own hull, the smallest merged into the
nearest while there are more than maxPieces. 0 for a model too big to look
through, or of one piece (cut it instead).
================
*/
#define PIECE_MAX_TRIS			1024
#define PIECE_WELD				0.01f	// corners this close are one

static int Hull_PieceRoot( int *up, int t ) {
	while ( up[t] != t ) {
		up[t] = up[up[t]];
		t = up[t];
	}
	return t;
}

static int Hull_SplitByPiece( const hullPiece_t *whole, hullPiece_t *pieces, int maxPieces ) {
	int *up, *root, numRoots, i, j, a, b, k, l, best;
	float d, bestDist;
	vec3_t ci, cj;

	if ( whole->numTris > PIECE_MAX_TRIS ) {
		return 0;
	}
	up = trap_HeapMalloc( whole->numTris * sizeof( int ) );
	for ( i = 0; i < whole->numTris; i++ ) {
		up[i] = i;
	}
	for ( a = 0; a < whole->numTris; a++ ) {
		for ( b = a + 1; b < whole->numTris; b++ ) {
			for ( k = 0; k < 9; k++ ) {
				if ( Distance( whole->tris[a * 3 + k / 3], whole->tris[b * 3 + k % 3] ) < PIECE_WELD ) {
					up[Hull_PieceRoot( up, a )] = Hull_PieceRoot( up, b );
					break;
				}
			}
		}
	}

	// the pieces: a triangle's root each
	root = trap_HeapMalloc( whole->numTris * sizeof( int ) );
	numRoots = 0;
	for ( i = 0; i < whole->numTris; i++ ) {
		if ( Hull_PieceRoot( up, i ) == i ) {
			root[numRoots++] = i;
		}
	}
	if ( numRoots < 2 ) {
		trap_HeapFree( root );
		trap_HeapFree( up );
		return 0;
	}
	for ( i = 0; i < numRoots; i++ ) {
		pieces[i].tris = NULL;
		pieces[i].numTris = 0;
	}
	// (more than there's room for: each in a list of its own first)
	{
		hullPiece_t *all = trap_HeapMalloc( numRoots * sizeof( hullPiece_t ) );

		for ( i = 0; i < numRoots; i++ ) {
			all[i].tris = trap_HeapMalloc( whole->numTris * 3 * sizeof( vec3_t ) );
			all[i].numTris = 0;
		}
		for ( i = 0; i < whole->numTris; i++ ) {
			for ( j = 0; root[j] != Hull_PieceRoot( up, i ); j++ ) {
			}
			memcpy( all[j].tris[all[j].numTris * 3], whole->tris[i * 3], 3 * sizeof( vec3_t ) );
			all[j].numTris++;
		}
		for ( i = 0; i < numRoots; i++ ) {
			Hull_PieceBounds( &all[i] );
		}
		// the smallest into the nearest, till they fit
		while ( numRoots > maxPieces ) {
			l = 0;
			for ( i = 1; i < numRoots; i++ ) {
				if ( all[i].numTris < all[l].numTris ) {
					l = i;
				}
			}
			VectorAdd( all[l].bounds[0], all[l].bounds[1], ci );
			best = -1;
			bestDist = 0.0f;
			for ( j = 0; j < numRoots; j++ ) {
				if ( j == l ) {
					continue;
				}
				VectorAdd( all[j].bounds[0], all[j].bounds[1], cj );
				d = Distance( ci, cj );
				if ( best < 0 || d < bestDist ) {
					best = j;
					bestDist = d;
				}
			}
			memcpy( all[best].tris[all[best].numTris * 3], all[l].tris, all[l].numTris * 3 * sizeof( vec3_t ) );
			all[best].numTris += all[l].numTris;
			Hull_PieceBounds( &all[best] );
			trap_HeapFree( all[l].tris );
			all[l] = all[--numRoots];
		}
		for ( i = 0; i < numRoots; i++ ) {
			pieces[i] = all[i];
		}
		trap_HeapFree( all );
	}
	trap_HeapFree( root );
	trap_HeapFree( up );
	return numRoots;
}

#define MAX_PARTS_CACHE		64

typedef struct {
	char			model[MAX_QPATH];
	float			scale;
	int				frame;
	int				maxParts;
	int				numParts;
	traceModel_t	*trms;
} partsCache_t;

static partsCache_t	partsCache[MAX_PARTS_CACHE];
static int			numPartsCache;

/*
================
BG_PropModelTraceModels

Up to maxParts convex trace models that together wrap the model's mesh in
the frame at the scale (see above), made once per model, scale, frame and
maxParts. maxParts negative: a hull for each separate piece of the mesh
(Hull_SplitByPiece: a coffin's boards), as many at most. Returns how many,
0 for a model that can't be read.
================
*/
int BG_PropModelTraceModels( const char *modelName, float scale, int frame, int maxParts, traceModel_t *trms ) {
	partsCache_t *cache;
	hullPiece_t pieces[MAX_PROP_PARTS], below, above;
	hullPlane_t planes[HULL_MAX_PLANES];
	hull_t *hull;
	float saving, bestSaving, total;
	int i, numPieces, best;
	qboolean byPiece;

	if ( scale <= 0.0f ) {
		scale = 1.0f;
	}
	// (negative: a hull for each piece of mesh, as many at most)
	byPiece = maxParts < 0;
	maxParts = Com_Clamp( 1, MAX_PROP_PARTS, byPiece ? -maxParts : maxParts );
	if ( maxParts == 1 ) {
		return BG_PropModelTraceModel( modelName, scale, frame, trms ) ? 1 : 0;
	}

	for ( i = 0; i < numPartsCache; i++ ) {
		cache = &partsCache[i];
		if ( cache->scale == scale && cache->frame == frame && cache->maxParts == ( byPiece ? -maxParts : maxParts )
			&& !Q_stricmp( cache->model, modelName ) ) {
			memcpy( trms, cache->trms, cache->numParts * sizeof( trms[0] ) );
			return cache->numParts;
		}
	}
	if ( numPartsCache == MAX_PARTS_CACHE ) {
		// out of room: one hull, uncached
		return BG_PropModelTraceModel( modelName, scale, frame, trms ) ? 1 : 0;
	}
	cache = &partsCache[numPartsCache++];
	Q_strncpyz( cache->model, modelName, sizeof( cache->model ) );
	cache->scale = scale;
	cache->frame = frame;
	cache->maxParts = byPiece ? -maxParts : maxParts;
	cache->numParts = 0;
	cache->trms = NULL;

	numPieces = 0;
	pieces[0].numTris = Hull_ModelTriangles( modelName, scale, frame, &pieces[0].tris );
	if ( !pieces[0].numTris ) {
		// a model without triangles: one hull, if that works
		if ( BG_PropModelTraceModel( modelName, scale, frame, trms ) ) {
			cache->trms = trap_HeapMalloc( sizeof( traceModel_t ) );
			cache->trms[0] = trms[0];
			cache->numParts = 1;
		}
		return cache->numParts;
	}
	Hull_PieceBounds( &pieces[0] );
	numPieces = 1;
	total = Hull_BoxVolume( (const vec3_t *)pieces[0].bounds );
	if ( byPiece ) {
		hullPiece_t whole = pieces[0];

		numPieces = Hull_SplitByPiece( &whole, pieces, maxParts );
		if ( numPieces ) {
			trap_HeapFree( whole.tris );
			maxParts = numPieces;		// (no cutting after)
		} else {
			pieces[0] = whole;			// one piece, or too big: cut it as any other
			numPieces = 1;
		}
	}

	// cut the piece that saves the most until it isn't worth it
	while ( numPieces < maxParts ) {
		best = -1;
		bestSaving = 0;
		for ( i = 0; i < numPieces; i++ ) {
			if ( Hull_BoxVolume( (const vec3_t *)pieces[i].bounds ) < total * PART_MIN_SHARE ) {
				continue;
			}
			if ( !Hull_SplitPiece( &pieces[i], NULL, NULL, &saving ) ) {
				continue;
			}
			if ( saving < Hull_BoxVolume( (const vec3_t *)pieces[i].bounds ) * PART_MIN_SAVING ) {
				continue;
			}
			if ( saving > bestSaving ) {
				bestSaving = saving;
				best = i;
			}
		}
		if ( best < 0 ) {
			break;
		}
		Hull_SplitPiece( &pieces[best], &below, &above, &saving );
		trap_HeapFree( pieces[best].tris );
		pieces[best] = below;
		pieces[numPieces++] = above;
	}

	// wrap each
	cache->trms = trap_HeapMalloc( numPieces * sizeof( traceModel_t ) );
	hull = trap_HeapMalloc( sizeof( *hull ) );
	for ( i = 0; i < numPieces; i++ ) {
		if ( pieces[i].numTris && Hull_FromPoints( (const vec3_t *)pieces[i].tris, pieces[i].numTris * 3, hull, planes ) ) {
			Hull_TraceModel( hull, planes, &cache->trms[cache->numParts++] );
		}
		trap_HeapFree( pieces[i].tris );
	}
	trap_HeapFree( hull );

	if ( !cache->numParts ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: no collision hull for %s\n", modelName );
		return 0;
	}
	memcpy( trms, cache->trms, cache->numParts * sizeof( trms[0] ) );
	return cache->numParts;
}
