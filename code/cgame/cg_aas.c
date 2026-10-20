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
// cg_aas.c -- the bots' navigation (AAS) drawn in the world, to see what
// they see: after RaZeR-RBI's aas-playground viewer (MIT), its colours and
// what it shows
//
// The cgame reads the map's .aas itself, so any client can look, for any
// of the AAS worlds (the creature sizes, botlib/be_aas.h):
//
//	cg_drawAAS 1|2|3		the standard (player), large or xxl world, 0 off
//	cg_drawAASRange			how far from the view (1024)
//	cg_drawAASShow			what, added up: 1 ground, 2 face edges, 4 liquids,
//							8 cluster portals, 16 reachabilities, 32 the grapple
//							ones too (31)
//
// It needs a bigger cgame heap than the default (vm_cgameHeapMegs), and
// polygons (r_maxpolys, r_maxpolyverts) for a busy view.

#include "cg_local.h"
#include "../botlib/aasfile.h"

#define AAS_MAX_DRAWN_POLYS		6000
#define AAS_MAX_FACE_POINTS		32

static const char *aasWorldSuffixes[] = { "", "_large", "_xxl" };

static struct {
	int					world;			// loaded: cg_drawAAS's, 0 none
	char				mapname[MAX_QPATH];
	qboolean			failed;			// don't try again until it changes

	int					numVertexes;
	aas_vertex_t		*vertexes;
	int					numEdges;
	aas_edge_t			*edges;
	int					numEdgeIndex;
	aas_edgeindex_t		*edgeIndex;
	int					numFaces;
	aas_face_t			*faces;
	int					numFaceIndex;
	aas_faceindex_t		*faceIndex;
	int					numAreas;
	aas_area_t			*areas;
	int					numAreaSettings;
	aas_areasettings_t	*areaSettings;
	int					numReachability;
	aas_reachability_t	*reachability;

	qhandle_t			shader;
	int					numPolys;		// drawn this frame
} cg_aas;

static void CG_AAS_Free( void ) {
	void **lumps[] = { (void **)&cg_aas.vertexes, (void **)&cg_aas.edges, (void **)&cg_aas.edgeIndex,
					   (void **)&cg_aas.faces, (void **)&cg_aas.faceIndex, (void **)&cg_aas.areas,
					   (void **)&cg_aas.areaSettings, (void **)&cg_aas.reachability };
	int i;

	for ( i = 0; i < ARRAY_LEN( lumps ); i++ ) {
		if ( *lumps[i] ) {
			trap_HeapFree( *lumps[i] );
			*lumps[i] = NULL;
		}
	}
	cg_aas.world = 0;
}

/*
================
CG_AAS_ReadLump

A lump of the file into the heap, as count items of size.
================
*/
static void *CG_AAS_ReadLump( fileHandle_t f, const aas_lump_t *lump, int size, int *count ) {
	void *data;

	*count = lump->filelen / size;
	if ( *count <= 0 ) {
		return NULL;
	}
	if ( lump->filelen > trap_HeapAvailable() ) {
		CG_Printf( S_COLOR_YELLOW "AAS: %d bytes don't fit the cgame heap (vm_cgameHeapMegs)\n", lump->filelen );
		*count = 0;
		return NULL;
	}
	data = trap_HeapMalloc( lump->filelen );
	trap_FS_Seek( f, lump->fileofs, FS_SEEK_SET );
	trap_FS_Read( data, lump->filelen, f );
	return data;
}

/*
================
CG_AAS_Load

The map's AAS world (1 standard, 2 large, 3 xxl), what drawing it needs.
================
*/
static qboolean CG_AAS_Load( int world ) {
	char path[MAX_QPATH], base[MAX_QPATH];
	aas_header_t header;
	fileHandle_t f;
	int len, i;

	CG_AAS_Free();
	COM_StripExtension( cgs.mapname, base, sizeof( base ) );
	Com_sprintf( path, sizeof( path ), "%s%s.aas", base, aasWorldSuffixes[world - 1] );
	len = trap_FS_FOpenFile( path, &f, FS_READ );
	if ( len < (int)sizeof( header ) ) {
		CG_Printf( S_COLOR_YELLOW "AAS: no %s (./dk aas)\n", path );
		if ( f ) {
			trap_FS_FCloseFile( f );
		}
		return qfalse;
	}
	trap_FS_Read( &header, sizeof( header ), f );
	if ( LittleLong( header.ident ) != AASID
		|| ( LittleLong( header.version ) != AASVERSION && LittleLong( header.version ) != AASVERSION_OLD ) ) {
		CG_Printf( S_COLOR_YELLOW "AAS: %s isn't an AAS file\n", path );
		trap_FS_FCloseFile( f );
		return qfalse;
	}
	// version 5's lumps are scrambled (be_aas_file.c AAS_DData)
	if ( LittleLong( header.version ) == AASVERSION ) {
		byte *p = (byte *)&header + 8;

		for ( i = 0; i < (int)sizeof( header ) - 8; i++ ) {
			p[i] ^= (byte)( i * 119 );
		}
	}
	for ( i = 0; i < AAS_LUMPS; i++ ) {
		header.lumps[i].fileofs = LittleLong( header.lumps[i].fileofs );
		header.lumps[i].filelen = LittleLong( header.lumps[i].filelen );
	}

	cg_aas.vertexes = CG_AAS_ReadLump( f, &header.lumps[AASLUMP_VERTEXES], sizeof( aas_vertex_t ), &cg_aas.numVertexes );
	cg_aas.edges = CG_AAS_ReadLump( f, &header.lumps[AASLUMP_EDGES], sizeof( aas_edge_t ), &cg_aas.numEdges );
	cg_aas.edgeIndex = CG_AAS_ReadLump( f, &header.lumps[AASLUMP_EDGEINDEX], sizeof( aas_edgeindex_t ), &cg_aas.numEdgeIndex );
	cg_aas.faces = CG_AAS_ReadLump( f, &header.lumps[AASLUMP_FACES], sizeof( aas_face_t ), &cg_aas.numFaces );
	cg_aas.faceIndex = CG_AAS_ReadLump( f, &header.lumps[AASLUMP_FACEINDEX], sizeof( aas_faceindex_t ), &cg_aas.numFaceIndex );
	cg_aas.areas = CG_AAS_ReadLump( f, &header.lumps[AASLUMP_AREAS], sizeof( aas_area_t ), &cg_aas.numAreas );
	cg_aas.areaSettings = CG_AAS_ReadLump( f, &header.lumps[AASLUMP_AREASETTINGS], sizeof( aas_areasettings_t ), &cg_aas.numAreaSettings );
	cg_aas.reachability = CG_AAS_ReadLump( f, &header.lumps[AASLUMP_REACHABILITY], sizeof( aas_reachability_t ), &cg_aas.numReachability );
	trap_FS_FCloseFile( f );

	if ( !cg_aas.vertexes || !cg_aas.edges || !cg_aas.edgeIndex || !cg_aas.faces || !cg_aas.faceIndex
		|| !cg_aas.areas || !cg_aas.areaSettings || cg_aas.numAreaSettings < cg_aas.numAreas ) {
		CG_AAS_Free();
		return qfalse;
	}
	// Spearmint runs little endian; the file is too
	cg_aas.world = world;
	CG_Printf( "AAS: %s, %d areas, %d reachabilities\n", path, cg_aas.numAreas, cg_aas.numReachability );
	return qtrue;
}

/*
================
CG_AAS_FacePoints

A face's corners in order, as botlib's AAS_ShowFacePolygon gets them.
================
*/
static int CG_AAS_FacePoints( int faceNum, vec3_t *points ) {
	const aas_face_t *face;
	int i, n = 0, edgeNum, absEdge;

	face = &cg_aas.faces[abs( faceNum )];
	for ( i = 0; i < face->numedges && n < AAS_MAX_FACE_POINTS; i++ ) {
		if ( face->firstedge + i < 0 || face->firstedge + i >= cg_aas.numEdgeIndex ) {
			break;
		}
		edgeNum = cg_aas.edgeIndex[face->firstedge + i];
		absEdge = abs( edgeNum );
		if ( absEdge >= cg_aas.numEdges ) {
			break;
		}
		VectorCopy( cg_aas.vertexes[cg_aas.edges[absEdge].v[edgeNum < 0]], points[n] );
		n++;
	}
	return n;
}

/*
================
CG_AAS_Line

A line about as wide on screen near as far (CG_DebugLine's is a fixed
width, which fills the view close up).
================
*/
static void CG_AAS_Line( const vec3_t start, const vec3_t end, const vec4_t color ) {
	polyVert_t verts[4];
	vec3_t dir, view, side, mid;
	float width;
	int i;

	if ( cg_aas.numPolys >= AAS_MAX_DRAWN_POLYS ) {
		return;
	}
	VectorSubtract( end, start, dir );
	VectorAdd( start, end, mid );
	VectorScale( mid, 0.5f, mid );
	VectorSubtract( mid, cg.refdef.vieworg, view );
	width = VectorLength( view ) * 0.0012f;
	if ( width < 0.05f ) {
		width = 0.05f;
	}
	CrossProduct( dir, view, side );
	if ( VectorNormalize( side ) == 0 ) {
		return;
	}
	VectorScale( side, width, side );
	VectorAdd( start, side, verts[0].xyz );
	VectorSubtract( start, side, verts[1].xyz );
	VectorSubtract( end, side, verts[2].xyz );
	VectorAdd( end, side, verts[3].xyz );
	for ( i = 0; i < 4; i++ ) {
		verts[i].st[0] = verts[i].st[1] = 0;
		verts[i].modulate[0] = color[0] * 255;
		verts[i].modulate[1] = color[1] * 255;
		verts[i].modulate[2] = color[2] * 255;
		verts[i].modulate[3] = color[3] * 255;
	}
	trap_R_AddPolyToScene( cg_aas.shader, 4, verts, 0, 0 );
	cg_aas.numPolys++;
}

/*
================
CG_AAS_DrawFace

A face, filled translucent and/or outlined.
================
*/
static void CG_AAS_DrawFace( int faceNum, const vec4_t color, qboolean fill, qboolean outline ) {
	vec3_t points[AAS_MAX_FACE_POINTS];
	polyVert_t verts[AAS_MAX_FACE_POINTS];
	static const vec4_t edgeColor = { 0, 0, 0, 1 };
	int i, n;

	n = CG_AAS_FacePoints( faceNum, points );
	if ( n < 3 ) {
		return;
	}
	if ( fill && cg_aas.numPolys < AAS_MAX_DRAWN_POLYS ) {
		for ( i = 0; i < n; i++ ) {
			// a little above, off the floor it lies on
			VectorCopy( points[i], verts[i].xyz );
			verts[i].xyz[2] += 0.5f;
			verts[i].st[0] = verts[i].st[1] = 0;
			verts[i].modulate[0] = color[0] * 255;
			verts[i].modulate[1] = color[1] * 255;
			verts[i].modulate[2] = color[2] * 255;
			verts[i].modulate[3] = color[3] * 255;
		}
		trap_R_AddPolyToScene( cg_aas.shader, n, verts, 0, 0 );
		cg_aas.numPolys++;
	}
	if ( outline ) {
		for ( i = 0; i < n; i++ ) {
			CG_AAS_Line( points[i], points[( i + 1 ) % n], edgeColor );
		}
	}
}

/*
================
CG_AAS_TravelColor

The viewer's colours for the travel types.
================
*/
static void CG_AAS_TravelColor( int travelType, vec4_t color ) {
	static const vec4_t colors[] = {
		{ 1, 1, 1, 1 },				// invalid
		{ 1, 1, 1, 1 },				// TRAVEL_INVALID
		{ 0.2f, 0.9f, 0.2f, 1 },	// TRAVEL_WALK
		{ 0.1f, 0.5f, 0.1f, 1 },	// TRAVEL_CROUCH
		{ 1, 0.9f, 0.1f, 1 },		// TRAVEL_BARRIERJUMP
		{ 1, 0.5f, 0, 1 },			// TRAVEL_JUMP
		{ 0.6f, 0.4f, 0.2f, 1 },	// TRAVEL_LADDER
		{ 0, 0.9f, 0.9f, 1 },		// TRAVEL_WALKOFFLEDGE
		{ 0.1f, 0.3f, 1, 1 },		// TRAVEL_SWIM
		{ 0.4f, 0.7f, 1, 1 },		// TRAVEL_WATERJUMP
		{ 1, 0, 1, 1 },				// TRAVEL_TELEPORT
		{ 0.6f, 0.2f, 0.9f, 1 },	// TRAVEL_ELEVATOR
		{ 1, 0.1f, 0.1f, 1 },		// TRAVEL_ROCKETJUMP
		{ 0.6f, 0, 0, 1 },			// TRAVEL_BFGJUMP
		{ 0.5f, 0.5f, 0.5f, 1 },	// TRAVEL_GRAPPLEHOOK
		{ 0.9f, 0.7f, 0.4f, 1 },	// TRAVEL_DOUBLEJUMP
		{ 0.9f, 0.9f, 0.6f, 1 },	// TRAVEL_RAMPJUMP
		{ 0.8f, 0.6f, 0.2f, 1 },	// TRAVEL_STRAFEJUMP
		{ 1, 0.5f, 0.8f, 1 },		// TRAVEL_JUMPPAD
		{ 0.7f, 0.5f, 1, 1 },		// TRAVEL_FUNCBOB
	};
	int t = travelType & TRAVELTYPE_MASK;

	Vector4Copy( colors[t >= 0 && t < ARRAY_LEN( colors ) ? t : 0], color );
}

/*
================
CG_AAS_DrawReachability

A reachability, an arrow from where it starts to where it ends.
================
*/
static void CG_AAS_DrawReachability( const aas_reachability_t *reach ) {
	vec3_t dir, side, head, up = { 0, 0, 1 };
	vec4_t color;
	float len;

	CG_AAS_TravelColor( reach->traveltype, color );
	CG_AAS_Line( reach->start, reach->end, color );
	VectorSubtract( reach->end, reach->start, dir );
	len = VectorNormalize( dir );
	if ( len < 1 ) {
		return;
	}
	CrossProduct( dir, up, side );
	if ( VectorNormalize( side ) == 0 ) {
		VectorSet( side, 1, 0, 0 );
	}
	VectorMA( reach->end, -6, dir, head );
	VectorMA( head, 3, side, head );
	CG_AAS_Line( reach->end, head, color );
	VectorMA( head, -6, side, head );
	CG_AAS_Line( reach->end, head, color );
}

typedef struct {
	int		area;
	float	dist;
} aasNearArea_t;

static int CG_AAS_CompareNear( const void *a, const void *b ) {
	float d = ( (const aasNearArea_t *)a )->dist - ( (const aasNearArea_t *)b )->dist;

	return d < 0 ? -1 : d > 0;
}

/*
================
CG_DrawAAS

The AAS world cg_drawAAS names, around the view.
================
*/
void CG_DrawAAS( void ) {
	static const vec4_t groundColor = { 0.25f, 0.25f, 0.25f, 0.45f };
	static const vec4_t doNotEnterColor = { 0.4f, 0.4f, 0, 0.5f };
	static const vec4_t waterColor = { 0, 0, 0.8f, 0.25f };
	static const vec4_t slimeColor = { 0, 0.55f, 0.15f, 0.25f };
	static const vec4_t lavaColor = { 1, 0.15f, 0, 0.25f };
	static const vec4_t portalColor = { 1, 0, 1, 0.33f };
	static const vec4_t walkablePortalColor = { 0, 1, 1, 0.33f };
	int world = cg_drawAAS.integer, show = cg_drawAASShow.integer;
	float range = cg_drawAASRange.value > 0 ? cg_drawAASRange.value : 1024;
	const float *view = cg.refdef.vieworg;
	static aasNearArea_t near[8192];
	int a, i, j, n, numNear, faceNum, contents;

	if ( world < 1 || world > ARRAY_LEN( aasWorldSuffixes ) ) {
		if ( cg_aas.world ) {
			CG_AAS_Free();
		}
		cg_aas.failed = qfalse;
		return;
	}
	if ( world != cg_aas.world || Q_stricmp( cg_aas.mapname, cgs.mapname ) ) {
		if ( cg_aas.failed && !Q_stricmp( cg_aas.mapname, cgs.mapname ) && world == -cg_aas.world ) {
			return;
		}
		Q_strncpyz( cg_aas.mapname, cgs.mapname, sizeof( cg_aas.mapname ) );
		cg_aas.failed = !CG_AAS_Load( world );
		if ( cg_aas.failed ) {
			cg_aas.world = -world;
			return;
		}
	}
	if ( !cg_aas.shader ) {
		// blended, both sides (scripts: debug/aas), or plain white
		cg_aas.shader = trap_R_RegisterShader( "debug/aas" );
		if ( !cg_aas.shader ) {
			cg_aas.shader = cgs.media.whiteShader;
		}
	}
	cg_aas.numPolys = 0;

	// the areas near the view, nearest first, so what's close is drawn when
	// there are too many
	numNear = 0;
	for ( a = 1; a < cg_aas.numAreas && numNear < ARRAY_LEN( near ); a++ ) {
		const aas_area_t *area = &cg_aas.areas[a];

		for ( j = 0; j < 3; j++ ) {
			if ( view[j] < area->mins[j] - range || view[j] > area->maxs[j] + range ) {
				break;
			}
		}
		if ( j < 3 ) {
			continue;
		}
		near[numNear].area = a;
		near[numNear].dist = Distance( view, area->center );
		numNear++;
	}
	qsort( near, numNear, sizeof( near[0] ), CG_AAS_CompareNear );

	for ( n = 0; n < numNear && cg_aas.numPolys < AAS_MAX_DRAWN_POLYS; n++ ) {
		const aas_area_t *area = &cg_aas.areas[near[n].area];
		const aas_areasettings_t *settings = &cg_aas.areaSettings[near[n].area];
		qboolean portal, walkablePortal = qfalse;

		contents = settings->contents;
		portal = ( contents & AREACONTENTS_CLUSTERPORTAL ) != 0;

		for ( i = 0; i < area->numfaces; i++ ) {
			const aas_face_t *face;
			int front, back;

			if ( area->firstface + i < 0 || area->firstface + i >= cg_aas.numFaceIndex ) {
				break;
			}
			faceNum = cg_aas.faceIndex[area->firstface + i];
			if ( abs( faceNum ) >= cg_aas.numFaces ) {
				continue;
			}
			face = &cg_aas.faces[abs( faceNum )];
			if ( face->faceflags & FACE_GROUND ) {
				walkablePortal = qtrue;
			}
			if ( portal ) {
				continue;
			}
			front = faceNum > 0 ? face->frontarea : face->backarea;
			back = faceNum > 0 ? face->backarea : face->frontarea;

			// liquid on one side: its surface
			if ( front > 0 && back > 0 && front < cg_aas.numAreas && back < cg_aas.numAreas
				&& !( cg_aas.areaSettings[front].areaflags & AREA_LIQUID ) != !( cg_aas.areaSettings[back].areaflags & AREA_LIQUID ) ) {
				if ( show & 4 ) {
					int c = cg_aas.areaSettings[front].contents | cg_aas.areaSettings[back].contents;

					CG_AAS_DrawFace( faceNum, ( c & AREACONTENTS_LAVA ) ? lavaColor
									 : ( c & AREACONTENTS_SLIME ) ? slimeColor : waterColor, qtrue, qfalse );
				}
			} else if ( face->faceflags & FACE_GROUND ) {
				CG_AAS_DrawFace( faceNum, ( contents & AREACONTENTS_DONOTENTER ) ? doNotEnterColor : groundColor,
								 ( show & 1 ) != 0, ( show & 2 ) != 0 );
			}
		}

		// a cluster portal: its faces, cyan where it can be walked through
		if ( portal && ( show & 8 ) ) {
			for ( i = 0; i < area->numfaces; i++ ) {
				if ( area->firstface + i >= cg_aas.numFaceIndex ) {
					break;
				}
				faceNum = cg_aas.faceIndex[area->firstface + i];
				if ( abs( faceNum ) < cg_aas.numFaces ) {
					CG_AAS_DrawFace( faceNum, walkablePortal ? walkablePortalColor : portalColor, qtrue, qfalse );
				}
			}
		}

		// where the bots can go from it
		if ( ( show & 16 ) && cg_aas.reachability ) {
			for ( i = 0; i < settings->numreachableareas && cg_aas.numPolys < AAS_MAX_DRAWN_POLYS; i++ ) {
				const aas_reachability_t *reach;

				if ( settings->firstreachablearea + i < 0 || settings->firstreachablearea + i >= cg_aas.numReachability ) {
					break;
				}
				reach = &cg_aas.reachability[settings->firstreachablearea + i];
				if ( ( reach->traveltype & TRAVELTYPE_MASK ) == TRAVEL_GRAPPLEHOOK && !( show & 32 ) ) {
					continue;
				}
				CG_AAS_DrawReachability( reach );
			}
		}
	}
}
