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
// g_airnav.c -- routes through the air for flying monsters, as Doom 3's: on
// the map's AAS
//
// Doom 3's flyers route through its AAS (idAASLocal::FlyPathToGoal): its
// areas cover all the empty space, not only the floors, and a fly
// reachability goes through every face between two of them. The map's AAS
// here is Quake 3's (bspc, ./dk aas), whose areas cover the air too; the game
// reads it (maps/<map>.aas) and links its areas through their faces as
// Doom 3's fly reachabilities do. A route is A* from area to area (no liquid),
// through the middles of the faces it crosses, pulled straight: each waypoint
// the farthest one the flyer's box flies to in a line.
//
// The AAS is made for the player's box: the space it covers is where a
// player fits, so a flyer's routes keep that far from the walls.

#include "g_local.h"
#include "../botlib/aasfile.h"

#define AIR_SEARCHES		4				// a frame at most: more wait for the next frames

typedef struct {
	int			planenum;
	int			children[2];		// > 0 a node, < 0 an area, 0 solid
} airNode_t;

typedef struct {
	int			other;				// the area on the other side
	vec3_t		point;				// the middle of the face between them
} airLink_t;

static struct {
	qboolean		loaded;			// tried
	int				numAreas;
	vec3_t			*areaCenter;
	byte			*areaUsable;	// not liquid nor disabled
	int				*firstLink;		// [numAreas + 1]
	airLink_t		*links;
	int				numNodes;
	airNode_t		*nodes;
	aas_plane_t		*planes;
	int				numPlanes;

	// the search's
	float			*g;
	int				*parent;		// area, 0 none
	int				*parentLink;
	byte			*closed;
	int				*heap;
	float			*heapF;
	int				heapSize;

	int				searchFrame;
	int				searches;
} air;

/*
================
AirNav_Free
================
*/
void G_AirNavShutdown( void ) {
	void *blocks[] = { air.areaCenter, air.areaUsable, air.firstLink, air.links, air.nodes, air.planes,
					   air.g, air.parent, air.parentLink, air.closed, air.heap, air.heapF };
	int i;

	for ( i = 0; i < (int)ARRAY_LEN( blocks ); i++ ) {
		if ( blocks[i] ) {
			trap_HeapFree( blocks[i] );
		}
	}
	memset( &air, 0, sizeof( air ) );
}

/*
================
AirNav_Load

the map's AAS: its BSP (to find a point's area), its areas, and their links
through their faces
================
*/
static qboolean AirNav_Load( void ) {
	char			mapname[MAX_QPATH];
	aas_header_t	header;
	fileHandle_t	f;
	byte			*data;
	int				len, i, k, a, b, n, numFaces;
	const aas_face_t	*faces;
	const aas_area_t	*areas;
	const aas_areasettings_t *settings;
	const aas_edge_t	*edges;
	const int		*edgeIndex;
	const float		*vertexes;
	int				*count;

	if ( air.loaded ) {
		return air.numAreas > 0;
	}
	air.loaded = qtrue;

	trap_Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
	len = trap_FS_FOpenFile( va( "maps/%s.aas", mapname ), &f, FS_READ );
	if ( len < (int)sizeof( header ) || !f ) {
		if ( f ) {
			trap_FS_FCloseFile( f );
		}
		G_DPrintf( "no maps/%s.aas: flyers fly straight\n", mapname );
		return qfalse;
	}
	data = trap_HeapMalloc( len );
	trap_FS_Read( data, len, f );
	trap_FS_FCloseFile( f );

	memcpy( &header, data, sizeof( header ) );
	if ( LittleLong( header.ident ) != AASID || LittleLong( header.version ) != AASVERSION ) {
		G_Printf( S_COLOR_YELLOW "WARNING: maps/%s.aas isn't an AAS of version %d\n", mapname, AASVERSION );
		trap_HeapFree( data );
		return qfalse;
	}
	// the header's lumps are scrambled (botlib's AAS_DData)
	for ( i = 0; i < (int)sizeof( header ) - 8; i++ ) {
		( (byte *)&header )[8 + i] ^= (byte)( i * 119 );
	}
	for ( i = 0; i < AAS_LUMPS; i++ ) {
		if ( header.lumps[i].fileofs < 0 || header.lumps[i].filelen < 0 || header.lumps[i].fileofs + header.lumps[i].filelen > len ) {
			G_Printf( S_COLOR_YELLOW "WARNING: maps/%s.aas is damaged\n", mapname );
			trap_HeapFree( data );
			return qfalse;
		}
	}
#define LUMP( t, n )	( (const t *)( data + header.lumps[n].fileofs ) )
#define LUMPCOUNT( t, n )	( header.lumps[n].filelen / (int)sizeof( t ) )

	vertexes = LUMP( float, AASLUMP_VERTEXES );
	edges = LUMP( aas_edge_t, AASLUMP_EDGES );
	edgeIndex = LUMP( int, AASLUMP_EDGEINDEX );
	faces = LUMP( aas_face_t, AASLUMP_FACES );
	numFaces = LUMPCOUNT( aas_face_t, AASLUMP_FACES );
	areas = LUMP( aas_area_t, AASLUMP_AREAS );
	settings = LUMP( aas_areasettings_t, AASLUMP_AREASETTINGS );
	air.numAreas = LUMPCOUNT( aas_area_t, AASLUMP_AREAS );

	air.numPlanes = LUMPCOUNT( aas_plane_t, AASLUMP_PLANES );
	air.planes = trap_HeapMalloc( MAX( air.numPlanes, 1 ) * sizeof( aas_plane_t ) );
	memcpy( air.planes, LUMP( aas_plane_t, AASLUMP_PLANES ), air.numPlanes * sizeof( aas_plane_t ) );
	air.numNodes = LUMPCOUNT( airNode_t, AASLUMP_NODES );
	air.nodes = trap_HeapMalloc( MAX( air.numNodes, 1 ) * sizeof( airNode_t ) );
	memcpy( air.nodes, LUMP( airNode_t, AASLUMP_NODES ), air.numNodes * sizeof( airNode_t ) );

	air.areaCenter = trap_HeapMalloc( air.numAreas * sizeof( vec3_t ) );
	air.areaUsable = trap_HeapMalloc( air.numAreas );
	for ( i = 1; i < air.numAreas; i++ ) {
		VectorCopy( areas[i].center, air.areaCenter[i] );
		air.areaUsable[i] = !( settings[i].areaflags & ( AREA_LIQUID | AREA_DISABLED ) )
			&& !( settings[i].contents & ( AREACONTENTS_WATER | AREACONTENTS_LAVA | AREACONTENTS_SLIME ) );
	}

	// Doom 3's fly reachabilities: through each face between two areas
	count = trap_HeapMalloc( ( air.numAreas + 1 ) * sizeof( int ) );
	for ( i = 0; i < numFaces; i++ ) {
		a = faces[i].frontarea;
		b = faces[i].backarea;
		if ( ( faces[i].faceflags & FACE_SOLID ) || a <= 0 || b <= 0 || a >= air.numAreas || b >= air.numAreas ) {
			continue;
		}
		count[a]++;
		count[b]++;
	}
	air.firstLink = trap_HeapMalloc( ( air.numAreas + 1 ) * sizeof( int ) );
	for ( n = 0, i = 0; i < air.numAreas; i++ ) {
		air.firstLink[i] = n;
		n += count[i];
		count[i] = 0;
	}
	air.firstLink[air.numAreas] = n;
	air.links = trap_HeapMalloc( MAX( n, 1 ) * sizeof( airLink_t ) );
	for ( i = 0; i < numFaces; i++ ) {
		vec3_t mid;

		a = faces[i].frontarea;
		b = faces[i].backarea;
		if ( ( faces[i].faceflags & FACE_SOLID ) || a <= 0 || b <= 0 || a >= air.numAreas || b >= air.numAreas ) {
			continue;
		}
		VectorClear( mid );
		for ( k = 0; k < faces[i].numedges; k++ ) {
			int e = edgeIndex[faces[i].firstedge + k];
			const float *v = &vertexes[ 3 * edges[abs( e )].v[e < 0 ? 1 : 0] ];

			VectorAdd( mid, v, mid );
		}
		if ( faces[i].numedges > 0 ) {
			VectorScale( mid, 1.0f / faces[i].numedges, mid );
		}
		air.links[air.firstLink[a] + count[a]].other = b;
		VectorCopy( mid, air.links[air.firstLink[a] + count[a]].point );
		count[a]++;
		air.links[air.firstLink[b] + count[b]].other = a;
		VectorCopy( mid, air.links[air.firstLink[b] + count[b]].point );
		count[b]++;
	}
	trap_HeapFree( count );
	trap_HeapFree( data );

	air.g = trap_HeapMalloc( air.numAreas * sizeof( float ) );
	air.parent = trap_HeapMalloc( air.numAreas * sizeof( int ) );
	air.parentLink = trap_HeapMalloc( air.numAreas * sizeof( int ) );
	air.closed = trap_HeapMalloc( air.numAreas );
	air.heap = trap_HeapMalloc( ( air.numAreas + 1 ) * 8 * sizeof( int ) );
	air.heapF = trap_HeapMalloc( ( air.numAreas + 1 ) * 8 * sizeof( float ) );

	G_DPrintf( "maps/%s.aas: %d areas, %d links through the air\n", mapname, air.numAreas, n / 2 );
	return qtrue;
}

/*
================
AirPointArea

the AAS area a point is in, 0 solid
================
*/
static int AirPointArea( const vec3_t p ) {
	int		node = 1;
	float	d;

	while ( node > 0 && node < air.numNodes ) {
		const airNode_t		*n = &air.nodes[node];
		const aas_plane_t	*plane = &air.planes[n->planenum];

		d = DotProduct( p, plane->normal ) - plane->dist;
		node = n->children[ d > 0 ? 0 : 1 ];
	}
	return node < 0 ? -node : 0;
}

/*
================
AirNearArea

the area of a point, or of one near it the flyer gets to (a flyer by a wall
is where a player's box isn't)
================
*/
static int AirNearArea( const vec3_t p, const vec3_t mins, const vec3_t maxs, int passEntityNum ) {
	static const float	offsets[][3] = { { 0, 0, -24 }, { 0, 0, 24 }, { 24, 0, 0 }, { -24, 0, 0 }, { 0, 24, 0 }, { 0, -24, 0 }, { 0, 0, -48 } };
	vec3_t				q;
	float				best, d;
	int					area, i;

	area = AirPointArea( p );
	if ( area > 0 && air.areaUsable[area] ) {
		return area;
	}
	for ( i = 0; i < (int)ARRAY_LEN( offsets ); i++ ) {
		VectorAdd( p, offsets[i], q );
		area = AirPointArea( q );
		if ( area > 0 && air.areaUsable[area] && G_AirClear( p, q, mins, maxs, passEntityNum ) ) {
			return area;
		}
	}
	// the nearest area's middle it flies to
	area = 0;
	best = 512 * 512;
	for ( i = 1; i < air.numAreas; i++ ) {
		if ( !air.areaUsable[i] ) {
			continue;
		}
		d = DistanceSquared( p, air.areaCenter[i] );
		if ( d < best && G_AirClear( p, air.areaCenter[i], mins, maxs, passEntityNum ) ) {
			best = d;
			area = i;
		}
	}
	return area;
}

/*
================
the search's heap of areas by f
================
*/
static void HeapPush( int area, float f ) {
	int i = air.heapSize++;

	while ( i > 0 && air.heapF[( i - 1 ) / 2] > f ) {
		air.heap[i] = air.heap[( i - 1 ) / 2];
		air.heapF[i] = air.heapF[( i - 1 ) / 2];
		i = ( i - 1 ) / 2;
	}
	air.heap[i] = area;
	air.heapF[i] = f;
}

static int HeapPop( void ) {
	int		top = air.heap[0], i, c, area;
	float	f;

	air.heapSize--;
	if ( !air.heapSize ) {
		return top;
	}
	area = air.heap[air.heapSize];
	f = air.heapF[air.heapSize];
	i = 0;
	while ( ( c = 2 * i + 1 ) < air.heapSize ) {
		if ( c + 1 < air.heapSize && air.heapF[c + 1] < air.heapF[c] ) {
			c++;
		}
		if ( air.heapF[c] >= f ) {
			break;
		}
		air.heap[i] = air.heap[c];
		air.heapF[i] = air.heapF[c];
		i = c;
	}
	air.heap[i] = area;
	air.heapF[i] = f;
	return top;
}

/*
================
G_AirClear

a flyer's box goes from start to end in a line
================
*/
qboolean G_AirClear( const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, int passEntityNum ) {
	trace_t tr;

	G_TraceBox( &tr, start, mins, maxs, end, passEntityNum, CONTENTS_SOLID | CONTENTS_MONSTERCLIP );
	return tr.fraction == 1.0f && !tr.startsolid;
}

/*
================
G_AirRoute

a route through the air from start to goal: up to max waypoints, the last the
goal (or as near as it gets); 0 none, -1 ask again next frame
================
*/
int G_AirRoute( const vec3_t start, const vec3_t goal, const vec3_t mins, const vec3_t maxs, int passEntityNum,
				vec3_t *route, int max ) {
	static vec3_t	points[ 4096 ];
	int				startArea, goalArea, area, best, i, k, link, numPoints, out;
	float			bestH, g, h;
	vec3_t			from;
	const float		*p;

	if ( !AirNav_Load() ) {
		return 0;
	}
	if ( air.searchFrame != level.framenum ) {
		air.searchFrame = level.framenum;
		air.searches = 0;
	}
	if ( air.searches >= AIR_SEARCHES ) {
		return -1;
	}
	air.searches++;

	startArea = AirNearArea( start, mins, maxs, passEntityNum );
	goalArea = AirNearArea( goal, mins, maxs, ENTITYNUM_NONE );
	if ( !startArea || !goalArea ) {
		return 0;
	}

	// A* from area to area, through their faces' middles
	for ( i = 0; i < air.numAreas; i++ ) {
		air.g[i] = 1e30f;
		air.parent[i] = 0;
		air.closed[i] = 0;
	}
	air.heapSize = 0;
	air.g[startArea] = 0;
	air.parentLink[startArea] = -1;
	HeapPush( startArea, Distance( start, goal ) );
	best = startArea;
	bestH = Distance( start, goal );
	while ( air.heapSize ) {
		area = HeapPop();
		if ( air.closed[area] ) {
			continue;
		}
		air.closed[area] = 1;
		if ( area == goalArea ) {
			best = area;
			break;
		}
		p = air.parentLink[area] >= 0 ? air.links[ air.parentLink[area] ].point : start;
		h = Distance( p, goal );
		if ( h < bestH ) {
			best = area;
			bestH = h;
		}
		for ( link = air.firstLink[area]; link < air.firstLink[area + 1]; link++ ) {
			int other = air.links[link].other;

			if ( air.closed[other] || !air.areaUsable[other] ) {
				continue;
			}
			g = air.g[area] + Distance( p, air.links[link].point );
			if ( g < air.g[other] ) {
				air.g[other] = g;
				air.parent[other] = area;
				air.parentLink[other] = link;
				if ( air.heapSize < ( air.numAreas + 1 ) * 8 ) {
					HeapPush( other, g + Distance( air.links[link].point, goal ) );
				}
			}
		}
	}

	if ( g_debugMove.integer ) {
		int closed = 0;

		for ( i = 0; i < air.numAreas; i++ ) {
			closed += air.closed[i];
		}
		G_Printf( "air route: area %d (%s) to %d (%s): %s, %d areas searched\n", startArea, vtos( air.areaCenter[startArea] ),
				  goalArea, vtos( air.areaCenter[goalArea] ), best == goalArea ? "there" : va( "short, at %d", best ), closed );
	}

	// the faces' middles back from the best area, then forward
	numPoints = 0;
	if ( best == goalArea ) {
		VectorCopy( goal, points[numPoints] );
		numPoints++;
	}
	for ( area = best; area != startArea && air.parentLink[area] >= 0 && numPoints < (int)ARRAY_LEN( points ) - 1; area = air.parent[area] ) {
		VectorCopy( air.links[ air.parentLink[area] ].point, points[numPoints] );
		numPoints++;
	}
	if ( !numPoints ) {
		return 0;
	}
	// from out of its area (by a wall, where a player's box isn't), by its
	// area's middle if the first face isn't in sight
	if ( !G_AirClear( start, points[numPoints - 1], mins, maxs, passEntityNum ) ) {
		VectorCopy( air.areaCenter[startArea], points[numPoints] );
		numPoints++;
	}

	// pulled straight: from where it is, the farthest it flies to in a line
	out = 0;
	VectorCopy( start, from );
	i = numPoints;		// past the last (points[numPoints - 1] is the first face)
	while ( i > 0 && out < max ) {
		for ( k = 0; k < i - 1; k++ ) {
			if ( G_AirClear( from, points[k], mins, maxs, passEntityNum ) ) {
				break;
			}
		}
		VectorCopy( points[k], route[out] );
		VectorCopy( points[k], from );
		out++;
		i = k;
	}
	return out;
}
