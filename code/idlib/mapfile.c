/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

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
#include "q_containers.h"
#include "mapfile.h"
#include "../idlib/l_script.h"
#include "../idlib/l_precomp.h"

static mapBrushSide_t *ReverseBrushSides(mapBrushSide_t *sides)
{
    mapBrushSide_t *prev = NULL;
    mapBrushSide_t *current = sides;
    mapBrushSide_t *next = NULL;

    while ( current != NULL ) {
        next = current->next;

        current->next = prev;

        prev = current;
        current = next;
    }

    return prev;
}

static mapPrimitive_t *ReversePrimitives(mapPrimitive_t *primitives)
{
    mapPrimitive_t *prev = NULL;
    mapPrimitive_t *current = primitives;
    mapPrimitive_t *next = NULL;

    while ( current != NULL ) {
        next = current->next;

        current->next = prev;

        prev = current;
        current = next;
    }

    return prev;
}

static mapEntity_t *ReverseEntities(mapEntity_t *entities)
{
    mapEntity_t *prev = NULL;
    mapEntity_t *current = entities;
    mapEntity_t *next = NULL;

    while ( current != NULL ) {
        next = current->next;

        current->next = prev;

        prev = current;
        current = next;
    }

    return prev;
}

static mapPrimitive_t *PrependPrimitives( mapPrimitive_t *l1, mapPrimitive_t *l2 ) {
    mapPrimitive_t *p, *next;

    p = l2;
    while ( p ) {
        next = p->next;

        p->next = l1;
        l1 = p;

        p = next;
    }

    return l1;
}

/*
===============
FloatCRC
===============
*/
static ID_INLINE unsigned int FloatCRC( float f ) {
	return *(unsigned int *)&f;
}

/*
===============
StringCRC
===============
*/
static ID_INLINE unsigned int StringCRC( const char *str ) {
	unsigned int i, crc;
	const unsigned char *ptr;

	crc = 0;
	ptr = ( const unsigned char *)str;
	for ( i = 0; ptr[i]; i++ ) {
		crc ^= ptr[i] << (i & 3);
	}
	return crc;
}


/*
=================
ParseMapPatch
=================
*/
mapPrimitive_t *ParseMapPatch( source_t *src, const vec3_t origin, qboolean patchDef3, float version ) {
    char        key[MAX_TOKEN];
	float		info[7];
	surfVert_t  *vert;
	token_t		token;
	int			i, j;
	mapPrimitive_t *patch;

	if ( !PC_ExpectTokenString( src, "{" ) ) {
		return NULL;
	}

	// read the material (we had an implicit 'textures/' in the old format...)
	if ( !PC_ReadToken( src, &token ) ) {
		SourceError( src, "ParseMapPatch: unexpected EOF" );
		return NULL;
	}

	// Parse it
	if ( patchDef3 ) {
		if ( !PC_Parse1DMatrix( src, 7, info ) ) {
			SourceError( src, "ParseMapPatch: unable to Parse patchDef3 info" );
			return NULL;
		}
	} else {
		if ( !PC_Parse1DMatrix( src, 5, info ) ) {
			SourceError( src, "ParseMapPatch: unable to parse patchDef2 info" );
			return NULL;
		}
	}

    patch = ( mapPrimitive_t * )ii.GetMemory( sizeof( *patch ) );

    MapPatchInitWithSize( patch, info[0], info[1] );
    SurfacePatchSetSize( &patch->patch, info[0], info[1] );
    // we had an implicit 'textures/' in the old format...
    if ( version < 2.0f ) {
        Q_strncpyz( patch->material, "textures/", sizeof( patch->material ) );
        Q_strcat( patch->material, sizeof( patch->material ), token.string );
    } else {
        Q_strncpyz( patch->material, token.string, sizeof( patch->material ) );
    }

	if ( patchDef3 ) {
		patch->horzSubdivisions = info[2];
		patch->vertSubdivisions = info[3];
		patch->explicitSubdivisions = qtrue;
	}

	if ( SurfacePatchGetWidth( &patch->patch ) < 0 || SurfacePatchGetHeight( &patch->patch ) < 0 ) {
		SourceError( src, "ParseMapPatch: bad size" );
        FreeMapPatch( patch );
        ii.FreeMemory( patch );
		return NULL;
	}

	// these were written out in the wrong order, IMHO
	if ( !PC_ExpectTokenString( src, "(" ) ) {
		SourceError( src, "ParseMapPatch: bad patch vertex data" );
        FreeMapPatch( patch );
		ii.FreeMemory( patch );
		return NULL;
	}
	for ( j = 0; j < SurfacePatchGetWidth( &patch->patch ); j++ ) {
		if ( !PC_ExpectTokenString( src, "(" ) ) {
			SourceError( src, "ParseMapPatch: bad vertex row data" );
            FreeMapPatch( patch );
			ii.FreeMemory( patch );
			return NULL;
		}
		for ( i = 0; i < SurfacePatchGetHeight( &patch->patch ); i++ ) {
			float v[5];

			if ( !PC_Parse1DMatrix( src, 5, v ) ) {
				SourceError( src, "ParseMapPatch: bad vertex column data" );
				FreeMapPatch( patch );
                ii.FreeMemory( patch );
				return NULL;
			}

			vert = SurfaceGetVertex( &patch->patch.surf, i * SurfacePatchGetWidth( &patch->patch ) + j );
			vert->xyz[0] = v[0] - origin[0];
			vert->xyz[1] = v[1] - origin[1];
			vert->xyz[2] = v[2] - origin[2];
			vert->st[0] = v[3];
			vert->st[1] = v[4];
		}
		if ( !PC_ExpectTokenString( src, ")" ) ) {
            FreeMapPatch( patch );
			ii.FreeMemory( patch );
			SourceError( src, "ParseMapPatch: unable to parse patch control points" );
			return NULL;
		}
	}
	if ( !PC_ExpectTokenString( src, ")" ) ) {
		SourceError( src, "ParseMapPatch: unable to parse patch control points, no closure" );
        FreeMapPatch( patch );
		ii.FreeMemory( patch );
		return NULL;
	}

	// read any key/value pairs
	while( PC_ReadToken( src, &token ) ) {
		if ( !strcmp( token.string, "}" ) ) {
			PC_ExpectTokenString( src, "}" );
			break;
		}
		if ( token.type == TT_STRING ) {
            Q_strncpyz( key, token.string, sizeof( key ) );
			PC_ExpectTokenType( src, TT_STRING, 0, &token );
			DictSet( &patch->epairs, key, token.string );
		}
	}

	return patch;
}

/*
============
MapPatchWrite
============
*/
qboolean MapPatchWrite( const mapPrimitive_t *p, fileHandle_t fp, int primitiveNum, const vec3_t origin ) {
	int i, j;
	const surfVert_t *v;

	if ( p->explicitSubdivisions ) {
		FS_WriteFloatString( fp, "// primitive %d\n{\n patchDef3\n {\n", primitiveNum );
		FS_WriteFloatString( fp, "  \"%s\"\n  ( %d %d %d %d 0 0 0 )\n", p->material, p->patch.width, p->patch.height, p->horzSubdivisions, p->vertSubdivisions );
	} else {
		FS_WriteFloatString( fp, "// primitive %d\n{\n patchDef2\n {\n", primitiveNum );
		FS_WriteFloatString( fp, "  \"%s\"\n  ( %d %d 0 0 0 )\n", p->material, p->patch.width, p->patch.height );
	}

	FS_WriteFloatString( fp, "  (\n" );
	for ( i = 0; i < p->patch.width; i++ ) {
		FS_WriteFloatString( fp, "   ( " );
		for ( j = 0; j < p->patch.height; j++ ) {
            v = SurfaceGetVertex( &p->patch.surf, j * p->patch.width + i );

			FS_WriteFloatString( fp, " ( %f %f %f %f %f )", v->xyz[0] + origin[0],
								v->xyz[1] + origin[1], v->xyz[2] + origin[2], v->st[0], v->st[1] );
		}
		FS_WriteFloatString( fp, " )\n" );
	}
	FS_WriteFloatString( fp, "  )\n }\n}\n" );

	return qtrue;
}

/*
===============
GetMapPatchGeometryCRC
===============
*/
unsigned int GetMapPatchGeometryCRC( const mapPrimitive_t *p ) {
	int i, j;
	unsigned int crc;
    surfVert_t *v;

	crc = p->horzSubdivisions ^ p->vertSubdivisions;
	for ( i = 0; i < p->patch.width; i++ ) {
		for ( j = 0; j < p->patch.height; j++ ) {
            v = SurfaceGetVertex( &p->patch.surf, j * p->patch.width + i );

			crc ^= FloatCRC( v->xyz[0] );
			crc ^= FloatCRC( v->xyz[1] );
			crc ^= FloatCRC( v->xyz[2] );
		}
	}

	crc ^= StringCRC( p->material );

	return crc;
}


/*
=================
ParseMapBrush
=================
*/
mapPrimitive_t *ParseMapBrush( source_t *src, const vec3_t origin, qboolean newFormat, float version ) {
	vec3_t planepts[3];
	token_t token;
	mapBrushSide_t *side;
    mapPrimitive_t brush;
    char key[MAX_STRING_CHARS];
    vec4_t tmp4, pln;

	if ( !PC_ExpectTokenString( src, "{" ) ) {
		return NULL;
	}

    DictInit( &brush.epairs );
    brush.sides = NULL;
    brush.numSides = 0;
    side = NULL;

	do {
		if ( !PC_ReadToken( src, &token ) ) {
			PC_SourceError( src, "ParseMapBrush: unexpected EOF" );
			goto cleanup;
		}
		if ( !strcmp( token.string, "}" ) ) {
			break;
		}

		// here we may have to jump over brush epairs ( only used in editor )
		do {
			// if token is a brace
			if ( !strcmp( token.string, "(" ) ) {
				break;
			}
			// the token should be a key string for a key/value pair
			if ( token.type != TT_STRING ) {
				PC_SourceError( src, "ParseMapBrush: unexpected %s, expected ( or epair key string", token.string );
				goto cleanup;
			}

			Q_strncpyz( key, token.string, sizeof( key ) );

			if ( !PC_ReadTokenOnLine( src, &token ) || token.type != TT_STRING ) {
				PC_SourceError( src, "ParseMapBrush: expected epair value string not found" );
				goto cleanup;
			}

            DictSet( &brush.epairs, key, token.string );

			// try to read the next key
			if ( !PC_ReadToken( src, &token ) ) {
				PC_SourceError( src, "ParseMapBrush: unexpected EOF" );
				goto cleanup;
			}
		} while (1);

		PC_UnreadToken( src, &token );

		side = ii.GetMemory( sizeof( *side ) );
        side->next = brush.sides;
        brush.sides = side;
        brush.numSides++;

		if ( newFormat ) {
			if ( !PC_Parse1DMatrix( src, 4, tmp4 ) ) {
				PC_SourceError( src, "ParseMapBrush: unable to read brush side plane definition" );
                goto cleanup;
			}
            Vector4Copy( tmp4, side->plane );
		} else {
			// read the three point plane definition
			if (!PC_Parse1DMatrix( src, 3, planepts[0] ) ||
				!PC_Parse1DMatrix( src, 3, planepts[1] ) ||
				!PC_Parse1DMatrix( src, 3, planepts[2] ) ) {
				PC_SourceError( src, "ParseMapBrush: unable to read brush side plane definition" );
				goto cleanup;
			}

			VectorSubtract( planepts[0], origin, planepts[0] );
			VectorSubtract( planepts[1], origin, planepts[1] );
			VectorSubtract( planepts[2], origin, planepts[2] );

			PlaneInitWithPoints( pln, planepts[0], planepts[1], planepts[2], qtrue );
			PlaneCopy( pln, side->plane );
		}

		// read the texture matrix
		// this is odd, because the texmat is 2D relative to default planar texture axis
		if ( !PC_Parse2DMatrix( src, 2, 3, side->texMat[0] ) ) {
			PC_SourceError( src, "ParseMapBrush: unable to read brush side texture matrix" );
			goto cleanup;
		}
        VectorCopy( origin, side->origin );
		
		// read the material
		if ( !PC_ReadTokenOnLine( src, &token ) ) {
			PC_SourceError( src, "ParseMapBrush: unable to read brush side material" );
			goto cleanup;
		}

		// we had an implicit 'textures/' in the old format...
		if ( version < 2.0f ) {
            Q_strncpyz( side->material, "textures/", sizeof( side->material ) );
            Q_strcat( side->material, sizeof( side->material ), token.string );
		} else {
            Q_strncpyz( side->material, token.string, sizeof( side->material ) );
		}

		// Q2 allowed override of default flags and values, but we don't any more
		if ( PC_ReadTokenOnLine( src, &token ) ) {
			if ( PC_ReadTokenOnLine( src, &token ) ) {
				if ( PC_ReadTokenOnLine( src, &token ) ) {
				}
			}
		}
	} while( 1 );

	if ( !PC_ExpectTokenString( src, "}" ) ) {
		goto cleanup;
	}

	mapPrimitive_t *result = ii.GetMemory( sizeof( *result ) );;
    memcpy( result, &brush, sizeof( *result ) );
    result->sides = ReverseBrushSides( result->sides );

	return result;

cleanup:
    FreeMapBrush( &brush );
    return NULL;
}

/*
=================
ParseMapBrushQ3
=================
*/
mapPrimitive_t *ParseMapBrushQ3( source_t *src, const vec3_t origin ) {
	int shift[2], rotate;
	float scale[2];
	vec4_t pln;
	vec3_t planepts[3];
	token_t token;
    mapPrimitive_t brush;
    mapBrushSide_t *side;

    MapBrushInit( &brush );

	do {
		if ( !PC_CheckTokenString( src, "}" ) ) {
			break;
		}

		side = ii.GetMemory( sizeof( *side ) );
        side->next = brush.sides;
        brush.sides = side;
        brush.numSides++;

		// read the three point plane definition
		if (!PC_Parse1DMatrix( src, 3, planepts[0] ) ||
			!PC_Parse1DMatrix( src, 3, planepts[1] ) ||
			!PC_Parse1DMatrix( src, 3, planepts[2] ) ) {
			PC_SourceError( src, "ParseMapBrushQ3: unable to read brush side plane definition" );
			goto cleanup;
		}

        VectorSubtract( planepts[0], origin, planepts[0] );
        VectorSubtract( planepts[1], origin, planepts[1] );
        VectorSubtract( planepts[2], origin, planepts[2] );

		PlaneInitWithPoints( pln, planepts[0], planepts[1], planepts[2], qtrue );
		PlaneCopy( pln, side->plane );

		// read the material
		if ( !PC_ReadTokenOnLine( src, &token ) ) {
			PC_SourceError( src, "ParseMapBrushQ3: unable to read brush side material" );
			goto cleanup;
		}

		// we have an implicit 'textures/' in the old format
        Q_strncpyz( side->material, "textures/", sizeof( side->material ) );
        Q_strcat( side->material, sizeof( side->material ), token.string );

		// read the texture shift, rotate and scale
		shift[0] = PC_ParseInt( src );
		shift[1] = PC_ParseInt( src );
		rotate = PC_ParseInt( src );
		scale[0] = PC_ParseFloat( src, NULL );
		scale[1] = PC_ParseFloat( src, NULL );
		VectorSet( side->texMat[0], 0.03125f, 0.0f, 0.0f );
		VectorSet( side->texMat[1], 0.0f, 0.03125f, 0.0f );
		VectorCopy( origin, side->origin );
		
		// Q2 allowed override of default flags and values, but we don't any more
		if ( PC_ReadTokenOnLine( src, &token ) ) {
			if ( PC_ReadTokenOnLine( src, &token ) ) {
				if ( PC_ReadTokenOnLine( src, &token ) ) {
				}
			}
		}
	} while( 1 );

	mapPrimitive_t *result = ii.GetMemory( sizeof( *result ) );
    memcpy( result, &brush, sizeof( *result ) );
    result->sides = ReverseBrushSides( result->sides );

	return result;

cleanup:
    FreeMapBrush( &brush );
    return NULL;
}

/*
============
MapBrushWrite
============
*/
qboolean MapBrushWrite( const mapPrimitive_t *p, fileHandle_t fp, int primitiveNum, const vec3_t origin ) {
	int i;
	mapBrushSide_t *side;
    const dictKeyValue_t *kvp;

	FS_WriteFloatString( fp, "// primitive %d\n{\n brushDef3\n {\n", primitiveNum );

	// write brush epairs
	for ( i = 0; i < DictGetNumKeyVals( &p->epairs ); i++) {
        kvp = DictGetKeyVal( &p->epairs, i );
		FS_WriteFloatString( fp, "  \"%s\" \"%s\"\n", PoolStrGetString( kvp->key ), PoolStrGetString( kvp->value ) );
	}

	// write brush sides
    side = p->sides;
	while ( side ) {
		FS_WriteFloatString( fp, "  ( %f %f %f %f ) ", side->plane[0], side->plane[1], side->plane[2], side->plane[3] );
		FS_WriteFloatString( fp, "( ( %f %f %f ) ( %f %f %f ) ) \"%s\" 0 0 0\n",
							side->texMat[0][0], side->texMat[0][1], side->texMat[0][2],
							side->texMat[1][0], side->texMat[1][1], side->texMat[1][2],
							side->material );
		side = side->next;
	}

	FS_WriteFloatString( fp, " }\n}\n" );

	return qtrue;
}

/*
===============
GetMapBrushGeometryCRC
===============
*/
unsigned int GetMapBrushGeometryCRC( const mapPrimitive_t *brush ) {
	int j;
	mapBrushSide_t *mapSide;
	unsigned int crc;

    assert( brush->type == PRIMTYPE_BRUSH );

	crc = 0;
    mapSide = brush->sides;
	while ( mapSide ) {
		for ( j = 0; j < 4; j++ ) {
			crc ^= FloatCRC( mapSide->plane[j] );
		}
		crc ^= StringCRC( mapSide->material );
        
        mapSide = mapSide->next;
	}

	return crc;
}

/*
============
StripTrailingWhitespace
============
*/
void StripTrailingWhitespace( char *str ) {
	int i;
	
	// cast to unsigned char to prevent stripping off high-ASCII characters
	for( i = strlen( str ); i > 0 && (unsigned char)(str[ i - 1 ]) <= ' '; i-- ) {
		str[ i - 1 ] = '\0';
	}
}

/*
================
ParseMapEntity
================
*/
mapEntity_t *ParseMapEntity( source_t *src, qboolean worldSpawn, float version ) {
	token_t	token;
	mapEntity_t *mapEnt;
	mapPrimitive_t *mapPatch;
	mapPrimitive_t *mapBrush;
	qboolean worldent;
	vec3_t origin;
	double v1, v2, v3;
    char key[MAX_STRING_CHARS], value[MAX_STRING_CHARS];

	if ( !PC_ReadToken( src, &token ) ) {
		return NULL;
	}

	if ( strcmp( token.string, "{" ) ) {
		PC_SourceError( src, "ParseMapEntity: { not found, found %s", token.string );
		return NULL;
	}

	mapEnt = ii.GetMemory( sizeof( *mapEnt ) );
    MapEntityInit( mapEnt );

	VectorClear( origin );
	worldent = qfalse;
	do {
		if ( !PC_ReadToken( src, &token ) ) {
			PC_SourceError( src, "ParseMapEntity: EOF without closing brace" );
			goto cleanup;
		}
		if ( !strcmp( token.string, "}" ) ) {
			break;
		}

		if ( !strcmp( token.string, "{" ) ) {
			// parse a brush or patch
			if ( !PC_ReadToken( src, &token ) ) {
				PC_SourceError( src, "ParseMapEntity: unexpected EOF" );
				goto cleanup;
			}

			if ( worldent ) {
				VectorClear( origin );
			}

			// if is it a brush: brush, brushDef, brushDef2, brushDef3
			if ( Q_stricmpn( token.string, "brush", 5 ) == 0 ) {
				mapBrush = ParseMapBrush( src, origin, ( Q_stricmp( token.string, "brushDef2" ) || Q_stricmp( token.string, "brushDef3" ) ), version );
				if ( !mapBrush ) {
					return NULL;
				}
                AddPrimitiveToMapEntity( mapEnt, mapBrush );
			}
			// if is it a patch: patchDef2, patchDef3
			else if ( Q_stricmpn( token.string, "patch", 5 ) == 0 ) {
				mapPatch = ParseMapPatch( src, origin, !( Q_stricmp( token.string, "patchDef3" ) ), version );
				if ( !mapPatch ) {
					return NULL;
				}
				AddPrimitiveToMapEntity( mapEnt, mapPatch );
			}
			// assume it's a brush in Q3 or older style
			else {
				PC_UnreadToken( src, &token );
				mapBrush = ParseMapBrushQ3( src, origin );
				if ( !mapBrush ) {
					return NULL;
				}
                AddPrimitiveToMapEntity( mapEnt, mapBrush );
			}
		} else {
			// parse a key / value pair
            Q_strncpyz( key, token.string, sizeof( key ) );
			PC_ReadTokenOnLine( src, &token );
            Q_strncpyz( value, token.string, sizeof( value ) );

			// strip trailing spaces that sometimes get accidentally
			// added in the editor
			StripTrailingWhitespace( value );
			StripTrailingWhitespace( key );

			DictSet( &mapEnt->epairs, key, value );

			if ( !Q_stricmp( key, "origin" ) ) {
				// scanf into doubles, then assign, so it is vec_t size independent
				v1 = v2 = v3 = 0;
				sscanf( value, "%lf %lf %lf", &v1, &v2, &v3 );
                VectorSet( origin, v1, v2, v3 );
			}
			else if ( !Q_stricmp( key, "classname" ) && !Q_stricmp( value, "worldspawn" ) ) {
				worldent = qtrue;
			}
		}
	} while( 1 );

    mapEnt->primitives = ReversePrimitives( mapEnt->primitives );
	return mapEnt;

cleanup:
    MapEntityFree( mapEnt );
    ii.FreeMemory( mapEnt );
    return NULL;
}

/*
============
MapEntityWrite
============
*/
qboolean MapEntityWrite( const mapEntity_t *e, fileHandle_t fp, int entityNum ) {
	int i;
	mapPrimitive_t *mapPrim;
    const dictKeyValue_t *kvp;
	vec3_t origin;

	FS_WriteFloatString( fp, "// entity %d\n{\n", entityNum );

	// write entity epairs
	for ( i = 0; i < DictGetNumKeyVals( &e->epairs ); i++) {
        kvp = DictGetKeyVal( &e->epairs, i );
		FS_WriteFloatString( fp, "\"%s\" \"%s\"\n", PoolStrGetString( kvp->key ), PoolStrGetString( kvp->value ) );
	}

    DictGetVector2( &e->epairs, "origin", "0 0 0", origin );

	// write pritimives
    mapPrim = e->primitives;
    i = 0;
	while ( mapPrim != NULL ) {
		switch( mapPrim->type ) {
			case PRIMTYPE_BRUSH:
                MapBrushWrite( mapPrim, fp, i, origin );
				break;
			case PRIMTYPE_PATCH:
                MapPatchWrite( mapPrim, fp, i, origin );
                break;
            case PRIMTYPE_MESH:
                // not in the source format
                // TODO: add?
                break;
			case PRIMTYPE_INVALID:
				break;
		}
        i++;
        mapPrim = mapPrim->next;
	}

	FS_WriteFloatString( fp, "}\n" );

	return qtrue;
}

/*
===============
RemoveMapEntityPrimitiveData
===============
*/
void RemoveMapEntityPrimitiveData( mapEntity_t *e ) {
    mapPrimitive_t *p, *next;

    p = e->primitives;
    while ( p ) {
        next = p->next;

        switch ( p->type ) {
            case PRIMTYPE_BRUSH:
                FreeMapBrush( p );
                break;
            case PRIMTYPE_PATCH:
				FreeMapPatch( p );
                break;
            case PRIMTYPE_MESH:
                FreeMapMesh( p );
                break;
			case PRIMTYPE_INVALID:
				break;
        }
        ii.FreeMemory( p );

        p = next;
    }
    e->primitives = NULL;
    e->numPrimitives = 0;
}

/*
===============
GetMapEntityGeometryCRC
===============
*/
unsigned int GetMapEntityGeometryCRC( const mapEntity_t *e ) {
	int i;
	unsigned int crc;
	mapPrimitive_t	*mapPrim;

	crc = 0;
    mapPrim = e->primitives;
	while ( mapPrim != NULL ) {

		switch( mapPrim->type ) {
			case PRIMTYPE_BRUSH:
				crc ^= GetMapBrushGeometryCRC( mapPrim );
				break;
			case PRIMTYPE_PATCH:
				crc ^= GetMapPatchGeometryCRC( mapPrim );
				break;
            case PRIMTYPE_MESH:
                // not in the source format
                // TODO: add?
                break;
			case PRIMTYPE_INVALID:
				break;
		}

        i++;
        mapPrim = mapPrim->next;
	}

	return crc;
}

void ClearMapEntities( mapFile_t *m ) {
    mapEntity_t *e, *n;

    e = m->entities;
    while ( e ) {
        n = e->next;
        MapEntityFree( e );
        e = n;
    }
    m->entities = NULL;
    m->numEntities = 0;
}

/*
===============
ParseMapFileFromString
===============
*/
qboolean ParseMapFileFromSource( mapFile_t *m, source_t *src ) {
	token_t token;
	mapEntity_t *mapEnt, *world;
    char *material;
	int i;

	// no string concatenation for epairs and allow path names for materials
	 //( LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS | LEXFL_ALLOWPATHNAMES );
	SetScriptFlags( src->scriptstack, SCFL_NOSTRINGWHITESPACES |
									SCFL_NOSTRINGESCAPECHARS |
									SCFL_HANDLE_COMPATIBILITY );

	m->version = OLD_MAP_VERSION;
	//m->fileTime = src.GetFileTime();
    ClearMapEntities( m );

	if ( PC_CheckTokenString( src, "Version" ) ) {
		PC_ReadTokenOnLine( src, &token );
		m->version = token.floatvalue;
	}

	while( 1 ) {
		mapEnt = ParseMapEntity( src, ( m->entities == NULL ), m->version );
		if ( !mapEnt ) {
			break;
		}
		AddEntityToMapFile( m, mapEnt );
	}

    m->entities = ReverseEntities( m->entities );
	world = m->entities;

	SetMapFileGeometryCRC( m );

	// if the map has a worldspawn
	if ( world != NULL ) {

		// "removeEntities" "classname" can be set in the worldspawn to remove all entities with the given classname
		const dictKeyValue_t *removeEntities = DictMatchPrefix( &world->epairs, "removeEntities", NULL );
		while ( removeEntities ) {
			RemoveEntitiesFromMapFile( m, PoolStrGetString( removeEntities->value ) );
			removeEntities = DictMatchPrefix( &world->epairs, "removeEntities", removeEntities );
		}

		// "overrideMaterial" "material" can be set in the worldspawn to reset all materials
		if ( DictGetString( &world->epairs, "overrideMaterial", "", &material ) ) {
            mapEnt = m->entities;
			while ( mapEnt ) {
                mapPrimitive_t *mapPrimitive = mapEnt->primitives;
                mapBrushSide_t *side;

				while ( mapPrimitive ) {
					switch( mapPrimitive->type ) {
						case PRIMTYPE_BRUSH: {
                            side = mapPrimitive->sides;
                            while ( side ) {
                                Q_strncpyz( side->material, material, sizeof( side->material ) );
                                side = side->next;
                            }
							break;
                        }
						case PRIMTYPE_PATCH:
                        case PRIMTYPE_MESH:
                            Q_strncpyz( mapPrimitive->material, material, sizeof( mapPrimitive->material ) );
							break;

						case PRIMTYPE_INVALID:
							break;
					}
                    mapPrimitive = mapPrimitive->next;
				}
                mapEnt = mapEnt->next;
			}
		}

		// force all entities to have a name key/value pair
		if ( DictGetBool( &world->epairs, "forceEntityNames", "0" ) ) {
			mapEnt = m->entities;
			i = 0;
            while ( mapEnt ) {
				if ( DictFindKey( &mapEnt->epairs, "name" ) == NULL ) {
                    const char *className;
                    const char *s;

                    DictGetString( &mapEnt->epairs, "classname", "forcedName", &className );
                    s = va( "%s%d", className, i );
                    DictSet( &mapEnt->epairs, "name", s );
				}
                mapEnt = mapEnt->next;
				i++;
			}
		}

		// move the primitives of any func_group entities to the worldspawn
		if ( DictGetBool( &world->epairs, "moveFuncGroups", "0" ) ) {
            mapEnt = world->next;
            while ( mapEnt ) {
                const char *className;
                
                DictGetString( &mapEnt->epairs, "classname", "", &className );
				
                if ( Q_stricmp( className, "func_group" ) == 0 ) {
                    world->primitives = PrependPrimitives( world->primitives, mapEnt->primitives );
                    world->numPrimitives += mapEnt->numPrimitives;

                    mapEnt->primitives = NULL;
                    mapEnt->numPrimitives = 0;
				}
			}
        }
	}

	m->hasPrimitiveData = qtrue;
	return qtrue;
}

/*
===============
ParseMapFile
===============
*/
qboolean ParseMapFile( mapFile_t *m, const char *filename, qboolean ignoreRegion, qboolean osPath ) {
	source_t *src = NULL;
	char fullName[MAX_QPATH];
	qboolean result;

    COM_StripExtension( filename, fullName, sizeof( fullName ) );
	m->hasPrimitiveData = qfalse;

	if ( !ignoreRegion ) {
		// try loading a .reg file first
		Q_strcat( fullName, sizeof( fullName ), ".reg" );
		src = PC_LoadSource( fullName, NULL, NULL );
	}

	if ( !src ) {
		// now try a .map file
        COM_StripExtension( filename, fullName, sizeof( fullName ) );
		Q_strcat( fullName, sizeof( fullName ), ".map" );
		src = PC_LoadSource( fullName, NULL, NULL );
		if ( !src ) {
			// didn't get anything at all
			return qfalse;
		}
	}

	result = ParseMapFileFromSource( m, src );
	FreeSource( src );

	return result;
}

/*
============
MapFileWrite
============
*/
qboolean MapFileWrite( const mapFile_t *m, const char *fileName, const char *ext, qboolean fromBasePath ) {
	int i;
	char qpath[MAX_QPATH];
	fileHandle_t fp;
    mapEntity_t *ent;

    COM_StripExtension( fileName, qpath, sizeof( qpath ) );
    Q_strcat( qpath, sizeof( qpath ), ext );

	ii.Com_Printf( "writing %s...\n", qpath );

    // TODO: fromBasePath?
	if ( ii.FS_FOpenFile( qpath, &fp, FS_WRITE ) ) {
		ii.Com_DPrintf( "Couldn't open %s\n", qpath );
		return qfalse;
	}

	FS_WriteFloatString( fp, "Version %f\n", (float) CURRENT_MAP_VERSION );

    ent = m->entities;
    i = 0;
    while ( ent ) {
        MapEntityWrite( ent, fp, i );
        i++;
        ent = ent->next;
    }

	ii.FS_FCloseFile( fp );

	return qtrue;
}

/*
===============
SetMapFileGeometryCRC
===============
*/
void SetMapFileGeometryCRC( mapFile_t *m ) {
    mapEntity_t *e;

	m->geometryCRC = 0;
    e = m->entities;
    while ( e ) {
		m->geometryCRC ^= GetMapEntityGeometryCRC( e );
        e = e->next;
    }
}

/*
===============
AddEntityToMapFile
===============
*/
int AddEntityToMapFile( mapFile_t *m, mapEntity_t *mapEnt ) {
    mapEnt->next = m->entities;
    m->entities = mapEnt;
    m->numEntities++;
	return m->numEntities - 1;
}

/*
===============
FindEntityInMapFile
===============
*/
mapEntity_t *FindEntityInMapFile( const mapFile_t *m, const char *name ) {
    mapEntity_t *e;
    const char *s;

    e = m->entities;
	while ( e ) {
        DictGetString( &e->epairs, "name", "", &s );
		if ( Q_stricmp( s, name ) == 0 ) {
			return e;
		}
        e = e->next;
	}
	return NULL;
}

/*
===============
RemoveEntityFromMapFile
===============
*/
void RemoveEntityFromMapFile( mapFile_t *m, mapEntity_t *mapEnt ) {
    mapEntity_t *e;

    e = m->entities;
    while ( e ) {
        if ( e->next == mapEnt ) {
            e->next = mapEnt->next;
            break;
        }
        e = e->next;
    }

    MapEntityFree( mapEnt );
    ii.FreeMemory( mapEnt );
}

/*
===============
RemoveEntitiesFromMapFile
===============
*/
void RemoveEntitiesFromMapFile( mapFile_t *m, const char *classname ) {
    mapEntity_t *e, *prev, *next;
    const char *name;

    prev = NULL;
    e = m->entities;
    while ( e ) {
        next = e->next;

        DictGetString( &e->epairs, "classname", "", &name );

		if ( Q_stricmp( name, classname ) == 0 ) {
            if ( prev ) {
                prev->next = next;
            }
			MapEntityFree( e );
			ii.FreeMemory( e );
			m->numEntities--;
		} else {
            prev = e;
        }

        e = next;
    }
}

/*
===============
RemoveAllEntitiesFromMapFile
===============
*/
void RemoveAllEntitiesFromMapFile( mapFile_t *m ) {
    mapEntity_t *e, *next;

    e = m->entities;
    while ( e ) {
        next = e->next;

        MapEntityFree( e );
        ii.FreeMemory( e );

        e = next;
    }

    m->numEntities = 0;
    m->entities = NULL;
	m->hasPrimitiveData = qfalse;
}

/*
===============
RemovePrimitiveDataFromMapFile
===============
*/
void RemovePrimitiveDataFromMapFile( mapFile_t *m ) {
    mapEntity_t *e;

    e = m->entities;
	while ( e ) {
        RemoveMapEntityPrimitiveData( e );
        e = e->next;
	}
	m->hasPrimitiveData = qfalse;
}

/*
===============
MapFileNeedsReload
===============
*/
qboolean MapFileNeedsReload( mapFile_t *m ) {
	/*if ( name.Length() ) {
		ID_TIME_T time = (ID_TIME_T)-1;
		if ( idLib::fileSystem->ReadFile( name, NULL, &time ) > 0 ) {
			return ( time > fileTime );
		}
	}*/
	return qtrue;
}
