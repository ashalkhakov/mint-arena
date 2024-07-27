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
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code. If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code. If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "dict.h"
#include "l_script.h"
#include "l_precomp.h"

#define DICT_GRANULARITY 16

static strPool_t    globalKeys;
static strPool_t    globalValues;

/*
================
DictListClear

Frees up the memory allocated by the list. Assumes that type automatically handles freeing up memory.
================
*/
static ID_INLINE void DictListClear(dict_t* self) {
    if (self->args) {
        ii.FreeMemory(self->args);
    }

    self->args = NULL;
    self->num = 0;
    self->size = 0;
}

/*
================
DictListResize

Allocates memory for the amount of elements requested while keeping the contents intact.
Contents are copied using their = operator so that data is correnctly instantiated.
================
*/
static ID_INLINE void DictListResize(dict_t* self, int newsize, int newgranularity) {
    dictKeyValue_t *temp;
    int    i;

    assert( newsize >= 0 );

    assert( newgranularity > 0 );
    //self->granularity = newgranularity;

    // free up the list if no data is being reserved
    if ( newsize <= 0 ) {
        DictListClear( self );
        return;
    }

    temp = self->args;
    self->size = newsize;
    if ( self->size < self->num ) {
        self->num = self->size;
    }

    // copy the old list into our new one
    self->args = ii.GetMemory(sizeof(temp[0]) * self->size);
    for (i = 0; i < self->num; i++) {
        self->args[i] = temp[i];
    }

    // delete the old list if it exists
    if (temp) {
        ii.FreeMemory(temp);
    }
}

/*
================
DictListAppend

Increases the size of the list by one element and copies the supplied data into it.

Returns the index of the new element.
================
*/
static ID_INLINE int DictListAppend( dict_t* self, dictKeyValue_t* obj ) {
    if ( !self->args ) {
        DictListResize( self, 16, DICT_GRANULARITY );
    }

    if ( self->num == self->size ) {
        int newsize;
        int granularity = DICT_GRANULARITY;

        newsize = self->size + granularity;
        DictListResize( self, newsize - newsize % granularity, granularity );
    }

    assert( self->args != NULL && self->num < self->size );
    self->args[self->num] = *obj;
    self->num++;

    return self->num - 1;
}

/*
================
DictClearAndCopy

 clear existing key/value pairs and copy all key/value pairs from other
================
*/
void DictClearAndCopy( dict_t *self, const dict_t *other ) {
    int i;

    // check for assignment to self
    if ( self == other ) {
        return;
    }

    DictClear( self );

    DictListResize( self, other->num, DICT_GRANULARITY );

    self->num = other->num;
    for ( i = 0; i < self->num; i++ ) {
        self->args[i].key = StrPoolCopyString( &globalKeys, other->args[i].key );
        self->args[i].value = StrPoolCopyString( &globalValues, other->args[i].value );
    }

    HashIndexInitFrom( &self->argHash, &other->argHash );
}

/*
================
DictCopy

 copy all key value pairs without removing existing key/value pairs not present in the other dict
================
*/
void DictCopy( dict_t *self, const dict_t *other ) {
    int i, n, *found;
    dictKeyValue_t kv;

    // check for assignment to self
    if (self == other) {
        return;
    }

    kv.key = NULL;
    kv.value = NULL;

    n = other->num;

    if ( self->num ) {
        found = ( int * )ii.GetMemory( other->num * sizeof(int) );
        for (i = 0; i < n; i++) {
            found[i] = DictFindKeyIndex( self, PoolStrGetString( other->args[i].key ) );
        }
    } else {
        found = NULL;
    }

    for ( i = 0; i < n; i++ ) {
        if ( found && found[i] != -1 ) {
            // first set the new value and then free the old value to allow proper self copying
            poolStr_t* oldValue = self->args[found[i]].value;
            self->args[found[i]].value = StrPoolCopyString( &globalValues, other->args[i].value );
            StrPoolFreeString( &globalValues, oldValue );
        } else {
            kv.key = StrPoolCopyString( &globalKeys, other->args[i].key );
            kv.value = StrPoolCopyString( &globalValues, other->args[i].value );
            int k = HashIndexGenerateKeyForString( &self->argHash, PoolStrGetString( kv.key ), qfalse );
            int i = DictListAppend( self, &kv );
            HashIndexAdd( &self->argHash, k, i );
        }
    }

    if ( found ) {
        ii.FreeMemory( found );
        found = NULL;
    }
}

/*
================
DictTransferKeyValues

 clear existing key/value pairs and transfer key/value pairs from other
================
*/
void DictTransferKeyValues( dict_t *self, dict_t *other ) {
    int i, n;

    if ( self == other ) {
        return;
    }
    if (other->num && PoolStrGetPool( other->args[0].key ) != &globalKeys) {
        ii.Com_Error( ERR_FATAL, "DictTransferKeyValues: can't transfer values across a DLL boundary");
        return;
    }
    DictClear( self );

    n = other->num;
    DictListResize( self, n, DICT_GRANULARITY );
    for (i = 0; i < n; i++) {
        self->args[i].key = other->args[i].key;
        self->args[i].value = other->args[i].value;
    }
    self->num = n;
    HashIndexInitFrom( &self->argHash, &other->argHash );

    DictListClear( other );
    HashIndexFree( &other->argHash );
}

/*
================
DictSetDefaults
================
*/
void DictSetDefaults( dict_t *self, const dict_t *dict ) {
    int i, j, k, n;
    const dictKeyValue_t *kv, *def;
    dictKeyValue_t newkv;

    newkv.key = newkv.value = NULL;

    n = dict->num;
    for ( i = 0; i < n; i++ ) {
        def = &dict->args[i];
        kv = DictFindKey( self, PoolStrGetString( def->key ) );
        if ( !kv ) {
            newkv.key = StrPoolCopyString( &globalKeys, def->key );
            newkv.value = StrPoolCopyString( &globalValues, def->value );
            k = HashIndexGenerateKeyForString( &self->argHash, PoolStrGetString( newkv.key ), qfalse );
            j = DictListAppend( self, &newkv );
            HashIndexAdd( &self->argHash, k, j );
        }
    }
}

/*
================
DictClear
================
*/
void DictClear( dict_t *self ) {
    int i;

    for (i = 0; i < self->num; i++) {
        StrPoolFreeString( &globalKeys, self->args[i].key );
        StrPoolFreeString( &globalValues, self->args[i].value );
    }

    DictListClear( self );
    HashIndexClear( &self->argHash );
}

/*
================
DictFindKey
================
*/
const dictKeyValue_t *DictFindKey( const dict_t *self, const char *key ) {
    int i, hash;

    if ( key == NULL || key[0] == '\0' ) {
        ii.Com_DPrintf( S_COLOR_YELLOW "DictFindKey: empty key" );
        return NULL;
    }

    hash = HashIndexGenerateKeyForString( &self->argHash, key, qfalse );
    for (i = HashIndexFirst( &self->argHash, hash ); i != -1; i = HashIndexNext( &self->argHash, i ) ) {
        if ( Q_stricmp( PoolStrGetString( self->args[i].key ), key ) == 0) {
            return &self->args[i];
        }
    }

    return NULL;
}

/*
================
DictFindKeyIndex
================
*/
int DictFindKeyIndex( const dict_t *self, const char *key ) {
    int i, hash;

    if ( key == NULL || key[0] == '\0' ) {
        ii.Com_DPrintf( S_COLOR_YELLOW "DictFindKeyIndex: empty key" );
        return 0;
    }

    hash = HashIndexGenerateKeyForString( &self->argHash, key, qfalse );
    for ( i = HashIndexFirst( &self->argHash, hash ); i != -1; i = HashIndexNext( &self->argHash, i ) ) {
        if ( Q_stricmp( PoolStrGetString( self->args[i].key ), key ) == 0 ) {
            return i;
        }
    }

    return -1;
}

/*
================
DictAllocated
================
*/
size_t DictAllocated( const dict_t *self ) {
    int    i;
    size_t  size;

    size = self->size * sizeof( self->args[0] ) + HashIndexAllocated( &self->argHash );
    for (i = 0; i < self->num; i++) {
        size += DictKeyValueSize( &self->args[i] );
    }

    return size;
}

/*
================
DictSet
================
*/
void DictSet( dict_t *self, const char* key, const char* value) {
    int             i, k, v;
    dictKeyValue_t  kv;

    if (key == NULL || key[0] == '\0') {
        return;
    }

    i = DictFindKeyIndex( self, key );
    if ( i != -1 ) {
        // first set the new value and then free the old value to allow proper self copying
        poolStr_t *oldValue = self->args[i].value;
        self->args[i].value = StrPoolAllocString( &globalValues, value );
        StrPoolFreeString( &globalValues, oldValue );
    } else {
        kv.key = StrPoolAllocString( &globalKeys, key );
        kv.value = StrPoolAllocString( &globalValues, value );
        k = HashIndexGenerateKeyForString( &self->argHash, PoolStrGetString( kv.key ), qfalse );
        v = DictListAppend( self, &kv );
        HashIndexAdd( &self->argHash, k, v );
    }
}

/*
================
DictListRemoveIndex

Removes the element at the specified index and moves all data following the element down to fill in the gap.
The number of elements in the list is reduced by one. Returns false if the index is outside the bounds of the list.
Note that the element is not destroyed, so any memory used by it may not be freed until the destruction of the list.
================
*/
static ID_INLINE qboolean DictListRemoveIndex( dict_t *self, int index ) {
    int i;

    assert( self->args != NULL);
    assert( index >= 0 );
    assert( index < self->num );

    if ( ( index < 0 ) || ( index >= self->num ) ) {
        return qfalse;
    }

    self->num--;
    for ( i = index; i < self->num; i++ ) {
        self->args[i] = self->args[i + 1];
    }

    return qtrue;
}

/*
================
DictDelete
================
*/
void DictDelete( dict_t *self, const char *key ) {
    int hash, i;

    hash = HashIndexGenerateKeyForString( &self->argHash, key, qfalse );
    for ( i = HashIndexFirst( &self->argHash, hash ); i != -1; i = HashIndexNext( &self->argHash, i ) ) {
        if ( Q_stricmp( PoolStrGetString( self->args[i].key ), key ) == 0 ) {
            StrPoolFreeString( &globalKeys, self->args[i].key );
            StrPoolFreeString( &globalValues, self->args[i].value );
            DictListRemoveIndex( self, i );
            HashIndexRemoveIndex( &self->argHash, hash, i );
            break;
        }
    }

#if 0
    // make sure all keys can still be found in the hash index
    for (i = 0; i < self->num; i++) {
        assert( DictFindKey( self, args[i].key->str.data ) != NULL );
    }
#endif
}

/*
================
DictPrint
================
*/
void DictPrint( const dict_t *d ) {
	int i;
	int n;

	n = d->num;
	for( i = 0; i < n; i++ ) {
		ii.Com_Printf( "%s = %s\n", d->args[i].key->str, d->args[i].value->str );
	}
}

/*
================
DictGetFloat2
================
*/
qboolean DictGetFloat2( const dict_t *d, const char *key, const char *defaultString, float *out ) {
	const char	*s;
	qboolean	found;

	found = DictGetString( d, key, defaultString, &s );
	*out = atof( s );
	return found;
}

/*
================
DictGetInt2
================
*/
qboolean DictGetInt2( const dict_t *d, const char *key, const char *defaultString, int *out ) {
	const char	*s;
	qboolean	found;

	found = DictGetString( d, key, defaultString, &s );
	*out = atoi( s );
	return found;
}

/*
================
DictGetBool2
================
*/
qboolean DictGetBool2( const dict_t *d, const char *key, const char *defaultString, qboolean *out ) {
	const char	*s;
	qboolean	found;

	found = DictGetString( d, key, defaultString, &s );
	*out = ( atoi( s ) != 0 );
	return found;
}

/*
================
DictGetAngles2
================
*/
qboolean DictGetAngles2( const dict_t *d, const char *key, const char *defaultString, vec3_t out ) {
	qboolean	found;
	const char	*s;
	
	if ( !defaultString ) {
		defaultString = "0 0 0";
	}

	found = DictGetString( d, key, defaultString, &s );
    VectorClear( out );
	sscanf( s, "%f %f %f", &out[PITCH], &out[YAW], &out[ROLL] );
	return found;
}

/*
================
DictGetVector2
================
*/
qboolean DictGetVector2( const dict_t *d, const char *key, const char *defaultString, vec3_t out ) {
	qboolean	found;
	const char	*s;
	
	if ( !defaultString ) {
		defaultString = "0 0 0";
	}

	found = DictGetString( d, key, defaultString, &s );
	VectorClear( out );
	sscanf( s, "%f %f %f", &out[0], &out[1], &out[2] );
	return found;
}

/*
================
DictGetVec2_2
================
*/
qboolean DictGetVec2_2( const dict_t *d, const char *key, const char *defaultString, vec2_t out ) {
	qboolean	found;
	const char	*s;
	
	if ( !defaultString ) {
		defaultString = "0 0";
	}

	found = DictGetString( d, key, defaultString, &s );
	out[0] = out[1] = 0.0f;
	sscanf( s, "%f %f", &out[0], &out[1] );
	return found;
}

/*
================
DictGetVec4_2
================
*/
qboolean DictGetVec4_2( const dict_t *d, const char *key, const char *defaultString, vec4_t out ) {
	qboolean	found;
	const char	*s;
	
	if ( !defaultString ) {
		defaultString = "0 0 0 0";
	}

	found = DictGetString( d, key, defaultString, &s );
	VectorClear( out );
	sscanf( s, "%f %f %f %f", &out[0], &out[1], &out[2], &out[3] );
	return found;
}

/*
================
DictGetMatrix2
================
*/
qboolean DictGetMatrix2( const dict_t *d, const char *key, const char *defaultString, vec3_t out[3] ) {
	const char	*s;
	qboolean	found;
		
	if ( !defaultString ) {
		defaultString = "1 0 0 0 1 0 0 0 1";
	}

	found = DictGetString( d, key, defaultString, &s );
	AxisClear( out );		// sccanf has a bug in it on Mac OS 9.  Sigh.
	sscanf( s, "%f %f %f %f %f %f %f %f %f", &out[0][0], &out[0][1], &out[0][2], &out[1][0], &out[1][1], &out[1][2], &out[2][0], &out[2][1], &out[2][2] );
	return found;
}

/*
================
WriteString
================
*/
static void WriteString( const char *s, fileHandle_t f ) {
	int	len = strlen( s );
	if ( len >= MAX_STRING_CHARS-1 ) {
		ii.Com_Error( ERR_DROP, "DictWriteToFileHandle: bad string" );
	}
	ii.FS_Write( s, len + 1, f );
}

/*
================
DictMatchPrefix
================
*/
const dictKeyValue_t *DictMatchPrefix( const dict_t *d, const char *prefix, const dictKeyValue_t *lastMatch ) {
	int	i;
	int len;
	int start;

	assert( prefix );
	len = strlen( prefix );

	start = -1;
	if ( lastMatch ) {
        for( i = 0; i < d->num; i++ ) {
            if (
                !Q_stricmp( PoolStrGetString( d->args[ i ].key ), PoolStrGetString( lastMatch->key ) )
                && !strcmp( PoolStrGetString( d->args[ i ].value ), PoolStrGetString( lastMatch->value ) )
            ) {
                start = i;
                break;
            }
        }

		assert( start >= 0 );
		if ( start < 1 ) {
			start = 0;
		}
	}

	for( i = start + 1; i < d->num; i++ ) {
		if ( !Q_stricmpn( PoolStrGetString( d->args[i].key ), prefix, len ) ) {
			return &d->args[i];
		}
	}
	return NULL;
}

/*
================
DictParse
================
*/
qboolean DictParse( dict_t *d, source_t *parser ) {
	token_t	token;
	token_t	token2;
	qboolean	errors;

	errors = qfalse;

    PC_ExpectTokenString( parser, "{" );
	PC_ReadToken( parser, &token );
	while( ( token.type != TT_PUNCTUATION ) || ( strcmp( token.string, "}" ) ) ) {
		if ( token.type != TT_STRING ) {
			SourceError( parser, "Expected quoted string, but found '%s'", token.string );
		}

		if ( !PC_ReadToken( parser, &token2 ) ) {
			SourceError( parser, "Unexpected end of file" );
		}

		if ( DictFindKey( d, token.string ) ) {
			SourceWarning( parser, "'%s' already defined", token.string );
			errors = qtrue;
		}
		DictSet( d, token.string, token2.string );

		if ( !PC_ReadToken( parser, &token ) ) {
			SourceError( parser, "Unexpected end of file" );
		}
	}

	return !errors;
}

/*
================
DictWriteToFileHandle
================
*/
void DictWriteToFileHandle( const dict_t *d, fileHandle_t f ) {
	int c = LittleLong( d->num );
    ii.FS_Write( &c, sizeof( c ), f );
	for ( int i = 0; i < d->num; i++ ) {	// don't loop on the swapped count use the original
		WriteString( PoolStrGetString( d->args[i].key ), f );
		WriteString( PoolStrGetString( d->args[i].value ), f );
	}
}

/*
================
ReadString
================
*/
static void ReadString( fileHandle_t f, char *out, int outSize ) {
	char	str[MAX_STRING_CHARS];
	int		len;

	for ( len = 0; len < MAX_STRING_CHARS; len++ ) {
        ii.FS_Read( (void *)&str[len], 1, f );
		if ( str[len] == 0 ) {
			break;
		}
	}
	if ( len == MAX_STRING_CHARS ) {
		ii.Com_Error( ERR_DROP, "DictReadFromFileHandle: bad string" );
	}

    Q_strncpyz( out, str, outSize );
}

/*
================
DictReadFromFileHandle
================
*/
void DictReadFromFileHandle( dict_t *d, fileHandle_t f ) {
	int c;
	char key[MAX_STRING_CHARS], val[MAX_STRING_CHARS];

	DictClear( d );

    ii.FS_Read( &c, sizeof( c ), f );
	c = LittleLong( c );
	for ( int i = 0; i < c; i++ ) {
        ReadString( f, key, sizeof( key ) );
        ReadString( f, val, sizeof( val ) );
		DictSet( d, key, val );
	}
}

/*
================
Dict_Init
================
*/
void Dict_Init( void ) {
    StrPoolInit( &globalKeys );
    StrPoolSetCaseSensitive( &globalKeys, qfalse );
    StrPoolInit( &globalValues );
    StrPoolSetCaseSensitive( &globalValues, qtrue );
}

/*
================
Dict_Shutdown
================
*/
void Dict_Shutdown( void ) {
    StrPoolClear( &globalKeys );
    StrPoolClear( &globalValues );
}

/*
================
Dict_ShowMemoryUsage_f
================
*/
void Dict_ShowMemoryUsage_f( void ) {
	ii.Com_Printf( "%5d KB in %d keys\n", StrPoolSize( &globalKeys ) >> 10, globalKeys.num );
	ii.Com_Printf( "%5d KB in %d values\n", StrPoolSize( &globalValues ) >> 10, globalValues.num );
}

/*
================
DictStringSortCmp
================
*/
static int idListSortCompare( const void *x, const void *y ) {
	const poolStr_t **a = ( const poolStr_t ** )x;
	const poolStr_t **b = ( const poolStr_t ** )y;

	return Q_stricmp( PoolStrGetString( *a ), PoolStrGetString( *b ) );
}

/*
================
Dict_ListKeys_f
================
*/
void Dict_ListKeys_f( void ) {
	int i;
	poolStr_t **keyStrings;

    keyStrings = ii.GetMemory( sizeof( keyStrings[0] ) * globalKeys.num );
	for ( i = 0; i < globalKeys.num; i++ ) {
        keyStrings[i] = globalKeys.pool[i];
	}
    qsort( keyStrings, globalKeys.num, sizeof( keyStrings[0] ), idListSortCompare );
	for ( i = 0; i < globalKeys.num; i++ ) {
		ii.Com_Printf( "%s\n", keyStrings[i]->str );
	}
	ii.Com_Printf( "%5d keys\n", globalKeys.num );
    ii.FreeMemory( keyStrings );
}

/*
================
Dict_ListValues_f
================
*/
void Dict_ListValues_f( void ) {
	int i;
	poolStr_t **valueStrings;

    valueStrings = ii.GetMemory( sizeof( valueStrings[0] ) * globalValues.num );
	for ( i = 0; i < globalValues.num; i++ ) {
        valueStrings[i] = globalValues.pool[i];
	}
    qsort( valueStrings, globalValues.num, sizeof( valueStrings[0] ), idListSortCompare );
	for ( i = 0; i < globalValues.num; i++ ) {
		ii.Com_Printf( "%s\n", valueStrings[i]->str );
	}
	ii.Com_Printf( "%5d keys\n", globalValues.num );
    ii.FreeMemory( valueStrings );
}
