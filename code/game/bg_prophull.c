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
Hull_ModelPoints

The vertices of a frame of every surface of an MD3 (the first frame for one
it doesn't have), scaled.
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
