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

#ifndef __DICT_H__
#define __DICT_H__

#include "hashindex.h"
#include "strpool.h"

typedef struct source_s source_t;

/*
===============================================================================

Key/value dictionary

This is a dictionary class that tracks an arbitrary number of key / value
pair combinations. It is used for map entity spawning, GUI state management,
and other things.

Keys are compared case-insensitive.

Does not allocate memory until the first key/value pair is added.

===============================================================================
*/

typedef struct dictKeyValue_s {
  poolStr_t* key;
  poolStr_t* value;
} dictKeyValue_t;

static ID_INLINE size_t DictKeyValueAllocated( const dictKeyValue_t *self ) {
  return PoolStrAllocated( self->key ) + PoolStrAllocated( self->value );
}
static ID_INLINE size_t DictKeyValueSize( const dictKeyValue_t *self ) {
  return sizeof(*self) + PoolStrSize( self->key ) + PoolStrSize( self->value );
}

static ID_INLINE qboolean DictKeyValuesEqual( const dictKeyValue_t *self, const dictKeyValue_t *kv ) {
  return (self->key == kv->key && self->value == kv->value) ? qtrue : qfalse;
}

typedef struct dict_s {
  int          num, size;
  dictKeyValue_t    *args;
  hashIndex_t      argHash;
} dict_t;

void DictClear( dict_t *self );

static ID_INLINE void DictInit( dict_t *self ) {
  self->num = self->size = 0;
  self->args = NULL;
  HashIndexInit( &self->argHash, 128, 16 );

  HashIndexSetGranularity( &self->argHash, 16 );
  HashIndexClearAndResize( &self->argHash, 128, 16 );
}

// clear existing key/value pairs and copy all key/value pairs from other
void DictClearAndCopy( dict_t *self, const dict_t *other );

static ID_INLINE void DictInitFromDict(dict_t* self, const dict_t* other) {
  DictInit( self );
  DictClearAndCopy( self, other );
}

static ID_INLINE void DictFree( dict_t *self ) {
  DictClear( self );
}

static ID_INLINE void DictSetGranularity( dict_t *self, int granularity) {
  //args.SetGranularity(granularity);
  HashIndexSetGranularity( &self->argHash, granularity);
}

static ID_INLINE void DictSetHashSize( dict_t *self, int hashSize ) {
  if (self->num == 0 ) {
    HashIndexClearAndResize( &self->argHash, hashSize, 16 );
  }
}

size_t  DictAllocated( const dict_t *self );

static ID_INLINE size_t  DictSize( const dict_t *self ) {
  return sizeof( *self ) + DictAllocated( self );
}

static ID_INLINE int DictGetNumKeyVals( const dict_t *self ) {
  return self->num;
}

static ID_INLINE const dictKeyValue_t *DictGetKeyVal( const dict_t *self, int index ) {
  if ( index >= 0 && index < self->num ) {
    return &self->args[index];
  }
  return NULL;
}

// copy from other while leaving existing key/value pairs in place
void DictCopy( dict_t *self, const dict_t *other );
// clear existing key/value pairs and transfer key/value pairs from other
void DictTransferKeyValues( dict_t *self, dict_t *other );
// copy key/value pairs from other dict not present in this dict
void DictSetDefaults( dict_t *self, const dict_t *dict );
// clear dict freeing up memory
void DictClear( dict_t *self );

const dictKeyValue_t *DictFindKey( const dict_t *self, const char* key );
int DictFindKeyIndex( const dict_t *self, const char * key );

void DictSet( dict_t *self, const char* key, const char* value );

static ID_INLINE qboolean DictGetString( const dict_t *self, const char* key, const char* defaultString, const char** out) {
  const dictKeyValue_t* kv = DictFindKey( self, key );
  if (kv) {
    *out = kv->value->str.data;
    return qtrue;
  }
  *out = defaultString;
  return qfalse;
}

void DictDelete( dict_t *self, const char *key );

// print the dict
void DictPrint( const dict_t *d );

void DictGetVector( const dict_t *d, const char *key, const char *defaultString, vec3_t out );
void DictGetVec2( const dict_t *d, const char *key, const char *defaultString, vec2_t v );
void DictGetVec4( const dict_t *d, const char *key, const char *defaultString, vec4_t v );
void DictGetAngles( const dict_t *d, const char *key, const char *defaultString, vec3_t angles );
void DictGetMatrix( const dict_t *d, const char *key, const char *defaultString, vec3_t axis[3] );

static ID_INLINE void DictSetFloat( dict_t *d, const char *key, float val ) {
	DictSet( d, key, va( "%f", val ) );
}

static ID_INLINE void DictSetInt( dict_t *d, const char *key, int val ) {
	DictSet( d, key, va( "%i", val ) );
}

static ID_INLINE void DictSetBool( dict_t *d, const char *key, qboolean val ) {
	DictSet( d, key, va( "%i", val ) );
}

static ID_INLINE void DictSetVector( dict_t *d, const char *key, const vec3_t val ) {
	DictSet( d, key, va( "%f %f %f", val[0], val[1], val[2] ) );
}

static ID_INLINE void DictSetVec4( dict_t *d, const char *key, const vec4_t val ) {
	DictSet( d, key, va( "%f %f %f %f", val[0], val[1], val[2], val[3] ) );
}

static ID_INLINE void DictSetVec2( dict_t *d, const char *key, const vec2_t val ) {
	DictSet( d, key, va( "%f %f", val[0], val[1] ) );
}

static ID_INLINE void DictSetAngles( dict_t *d, const char *key, const vec3_t val ) {
	DictSet( d, key, va( "%f %f %f", val[0], val[1], val[2] ) );
}

static ID_INLINE void DictSetMatrix( dict_t *d, const char *key, const vec3_t val[3] ) {
	const char *s = va(
		"%f %f %f %f %f %f %f %f %f",
		val[0][0], val[0][1], val[0][2],
		val[1][0], val[1][1], val[2][2],
		val[2][0], val[2][1], val[2][2] );
	DictSet( d, key, s );
}

static ID_INLINE qboolean DictGetStringBuffer( const dict_t *d, const char *key, const char *defaultString, char *out, int outSize ) {
	const dictKeyValue_t *kv = DictFindKey( d, key );
	const char *s;
	if ( kv ) {
		s = PoolStrGetString( kv->value );
		Q_strncpyz( out, s, outSize );
		return qtrue;
	}
	Q_strncpyz( out, defaultString, outSize );
	return qfalse;
}

static ID_INLINE const char *DictGetStringReference( const dict_t *d, const char *key, const char *defaultString ) {
	const dictKeyValue_t *kv = DictFindKey( d, key );
	if ( kv ) {
		return PoolStrGetString( kv->value );
	}
	return defaultString;
}

static ID_INLINE float DictGetFloat( const dict_t *d, const char *key, const char *defaultString ) {
	return atof( DictGetStringReference( d, key, defaultString ) );
}

static ID_INLINE int DictGetInt( const dict_t *d, const char *key, const char *defaultString ) {
	return atoi( DictGetStringReference( d, key, defaultString ) );
}

static ID_INLINE qboolean DictGetBool( const dict_t *d, const char *key, const char *defaultString ) {
	return ( atoi( DictGetStringReference( d, key, defaultString ) ) != 0 ? qtrue : qfalse );
}

qboolean DictGetFloat2( const dict_t *d, const char *key, const char *defaultString, float *out );
qboolean DictGetInt2( const dict_t *d, const char *key, const char *defaultString, int *out );
qboolean DictGetBool2( const dict_t *d, const char *key, const char *defaultString, qboolean *out );
qboolean DictGetAngles2( const dict_t *d, const char *key, const char *defaultString, vec3_t out );
qboolean DictGetVector2( const dict_t *d, const char *key, const char *defaultString, vec3_t out );
qboolean DictGetVec2_2( const dict_t *d, const char *key, const char *defaultString, vec2_t out );
qboolean DictGetVec4_2( const dict_t *d, const char *key, const char *defaultString, vec4_t out );
qboolean DictGetMatrix2( const dict_t *d, const char *key, const char *defaultString, vec3_t out[3] );

#if 0
  // returns a unique checksum for this dictionary's content
int DictChecksum( const dict_t *self );
#endif

// finds the next key/value pair with the given key prefix.
// lastMatch can be used to do additional searches past the first match.
const dictKeyValue_t *DictMatchPrefix( const dict_t *d, const char *prefix, const dictKeyValue_t *lastMatch );
// parse dict from parser
qboolean DictParse( dict_t *d, source_t *parser );
void DictWriteToFileHandle( const dict_t *d, fileHandle_t f );
void DictReadFromFileHandle( dict_t *d, fileHandle_t f );

void Dict_Init( void );
void Dict_Shutdown( void );

void Dict_ShowMemoryUsage_f( void );
void Dict_ListKeys_f( void );
void Dict_ListValues_f( void );

#endif /* !__DICT_H__ */
