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

#ifndef __STRPOOL_H__
#define __STRPOOL_H__

#include "hashindex.h"
#include "str.h"

/*
===============================================================================

  strPool_t

===============================================================================
*/

typedef struct strPool_s strPool_t;
typedef struct poolStr_s poolStr_t;

#define POOLSTR_USE_STR 1

struct poolStr_s {
#if POOLSTR_USE_STR
    str_t        str;
#endif
    strPool_t   *pool;
    int         numUsers;
#if !POOLSTR_USE_STR
    char        str[1];
#endif
};

static ID_INLINE char *PoolStrGetString( poolStr_t* self ) {
    return self->str.data;
}

static ID_INLINE void PoolStrInit( poolStr_t *self ) {
    self->pool = NULL;
    self->numUsers = 0;
#if POOLSTR_USE_STR
    StrInit( &self->str );
#else
    self->str[0] = 0;
#endif
}

static ID_INLINE void PoolStrFree( poolStr_t *self ) {
    assert( self->numUsers == 0 );
#if POOLSTR_USE_STR
    StrFree( &self->str );
#endif
}

// returns total size of allocated memory
static ID_INLINE size_t PoolStrAllocated( const poolStr_t *self ) {
#if POOLSTR_USE_STR
    return StrAllocated( &self->str );
#else
    return strlen( self->str );
#endif
}

// returns total size of allocated memory including size of string pool type
static ID_INLINE size_t PoolStrSize( const poolStr_t *self ) {
    return sizeof( *self ) + PoolStrAllocated( self );
}

// returns a pointer to the pool this string was allocated from
static ID_INLINE const strPool_t *PoolStrGetPool( const poolStr_t *self ) {
    return self->pool;
}

struct strPool_s {
    qboolean        caseSensitive;
    int             num, size;
    poolStr_t       **pool;
    hashIndex_t     poolHash;
};

static ID_INLINE void StrPoolInit( strPool_t *self ) {
    self->caseSensitive = qtrue;
    self->num = self->size = 0;
    self->pool = NULL;
    HashIndexInitWithDefaults( &self->poolHash );
}

static ID_INLINE void StrPoolSetCaseSensitive( strPool_t *self, qboolean caseSensitive ) {
    self->caseSensitive = caseSensitive;
}

static ID_INLINE int StrPoolNum( const strPool_t *self ) {
    return self->num;
}

static ID_INLINE const poolStr_t *StrPoolGetByIndex( const strPool_t *self, int index ) {
    return self->pool[index];
}

/*
================
StrPoolListClear

Frees up the memory allocated by the list. Assumes that type automatically handles freeing up memory.
================
*/
static ID_INLINE void StrPoolListClear( strPool_t *self ) {
    if ( self->pool ) {
        ii.FreeMemory( self->pool );
    }

    self->pool = NULL;
    self->num = 0;
    self->size = 0;
}

/*
================
StrPoolListResize

Allocates memory for the amount of elements requested while keeping the contents intact.
Contents are copied using their = operator so that data is correnctly instantiated.
================
*/
static ID_INLINE void StrPoolListResize( strPool_t *self, int newsize, int newgranularity) {
    poolStr_t** temp;
    int    i;

    assert( newsize >= 0 );

    assert( newgranularity > 0 );
    //granularity = newgranularity;

    // free up the list if no data is being reserved
    if ( newsize <= 0 ) {
        StrPoolListClear( self );
        return;
    }

    temp = self->pool;
    self->size = newsize;
    if ( self->size < self->num ) {
        self->num = self->size;
    }

    // copy the old list into our new one
    self->pool = ( poolStr_t ** )ii.GetMemory( sizeof( temp[0] ) * self->size );
    for (i = 0; i < self->num; i++) {
        self->pool[i] = temp[i];
    }

    // delete the old list if it exists
    if ( temp ) {
        ii.FreeMemory( temp );
    }
}

/*
================
StrPoolListAppend

Increases the size of the list by one element and copies the supplied data into it.

Returns the index of the new element.
================
*/
static ID_INLINE int StrPoolListAppend(strPool_t *self, poolStr_t *obj) {
    if ( !self->pool ) {
        StrPoolListResize( self, 16, 16 );
    }

    if (self->num == self->size) {
        int newsize;
        int granularity = 16;

        newsize = self->size + granularity;
        StrPoolListResize( self, newsize - newsize % granularity, granularity );
    }

    assert(self->pool != NULL && self->num < self->size);
    self->pool[self->num] = obj;
    self->num++;

    return self->num - 1;
}


/*
================
StrPoolAllocString
================
*/
static ID_INLINE const poolStr_t *StrPoolAllocString( strPool_t *self, const char *string ) {
    int i, hash;
    poolStr_t *poolStr;

    hash = HashIndexGenerateKeyForString( &self->poolHash, string, self->caseSensitive );
    if ( self->caseSensitive ) {
        for ( i = HashIndexFirst( &self->poolHash, hash ); i != -1; i = HashIndexNext( &self->poolHash, i ) ) {
            if ( strcmp( PoolStrGetString( self->pool[i] ), string ) == 0 ) {
                self->pool[i]->numUsers++;
                return self->pool[i];
            }
        }
    } else {
        for ( i = HashIndexFirst( &self->poolHash, hash ); i != -1; i = HashIndexNext( &self->poolHash, i ) ) {
            if ( Q_stricmp( PoolStrGetString( self->pool[i] ), string ) == 0 ) {
                self->pool[i]->numUsers++;
                return self->pool[i];
            }
        }
    }

#if POOLSTR_USE_STR
    poolStr = ( poolStr_t * )ii.GetMemory( sizeof( *poolStr ) );
    StrInit( &poolStr->str );
    StrAppend( &poolStr->str, string );
#else
    int length = strlen(string);
    poolStr = ( poolStr_t * )ii.GetMemory( sizeof( *poolStr ) + length + 1 );
    Q_strncpyz( poolStr->str.data, string, length );
#endif

    poolStr->pool = self;
    poolStr->numUsers = 1;
    i = StrPoolListAppend( self, poolStr );
    HashIndexAdd( &self->poolHash, hash, i );
    return poolStr;
}

/*
================
StrPoolListRemoveIndex

Removes the element at the specified index and moves all data following the element down to fill in the gap.
The number of elements in the list is reduced by one. Returns false if the index is outside the bounds of the list.
Note that the element is not destroyed, so any memory used by it may not be freed until the destruction of the list.
================
*/
static ID_INLINE qboolean StrPoolListRemoveIndex( strPool_t *self, int index ) {
    int i;

    assert( self->pool != NULL );
    assert( index >= 0 );
    assert( index < self->num );

    if ( ( index < 0 ) || ( index >= self->num ) ) {
        return qfalse;
    }

    self->num--;
    for( i = index; i < self->num; i++ ) {
        self->pool[ i ] = self->pool[ i + 1 ];
    }

    return qtrue;
}

/*
================
StrPoolFreeString
================
*/
static ID_INLINE void StrPoolFreeString( strPool_t *self, poolStr_t *poolStr ) {
    int i, hash;

    assert( poolStr->numUsers >= 1 );
    assert( poolStr->pool == self );

    poolStr->numUsers--;
    if ( poolStr->numUsers <= 0 ) {
        hash = HashIndexGenerateKeyForString( &self->poolHash, PoolStrGetString( poolStr ), self->caseSensitive );
        if ( self->caseSensitive ) {
            for ( i = HashIndexFirst( &self->poolHash, hash ); i != -1; i = HashIndexNext( &self->poolHash, i ) ) {
                if ( strcmp( PoolStrGetString( self->pool[i] ), PoolStrGetString( poolStr ) ) == 0 ) {
                    break;
                }
            }
        } else {
            for ( i = HashIndexFirst( &self->poolHash, hash ); i != -1; i = HashIndexNext( &self->poolHash, i ) ) {
                if ( Q_stricmp( PoolStrGetString( self->pool[i] ), PoolStrGetString( poolStr ) ) == 0 ) {
                    break;
                }
            }
        }
        assert( i != -1 );
        assert( self->pool[i] == poolStr );
        PoolStrFree( self->pool[i] );
        ii.FreeMemory( self->pool[i] );
        self->pool[i] = NULL;
        StrPoolListRemoveIndex( self, i );
        HashIndexRemoveIndex( &self->poolHash, hash, i );
    }
}

/*
================
StrPoolCopyString
================
*/
static ID_INLINE const poolStr_t *StrPoolCopyString( strPool_t *self, poolStr_t *poolStr ) {

    assert( poolStr->numUsers >= 1 );

    if ( poolStr->pool == self ) {
        // the string is from this pool so just increase the user count
        poolStr->numUsers++;
        return poolStr;
    } else {
        // the string is from another pool so it needs to be re-allocated from this pool.
        return StrPoolAllocString( self, PoolStrGetString( poolStr ) );
    }
}

/*
================
StrPoolClear
================
*/
static ID_INLINE void StrPoolClear( strPool_t *self ) {
    int i;

    for ( i = 0; i < self->num; i++ ) {
        self->pool[i]->numUsers = 0;
        PoolStrFree( self->pool[i] );
        ii.FreeMemory( self->pool[i] );
        self->pool[i] = NULL;
    }
    self->num = self->size = 0;
    if ( self->pool ) {
        ii.FreeMemory( self->pool );
        self->pool = NULL;
    }
    HashIndexFree( &self->poolHash );
}

static ID_INLINE void StrPoolFree( strPool_t *self ) {
    StrPoolClear( self );
}


/*
================
StrPoolAllocated
================
*/
static ID_INLINE size_t StrPoolAllocated( const strPool_t *self ) {
    int i;
    size_t size;

    size = sizeof( self->pool[0] ) * self->size + HashIndexAllocated( &self->poolHash );
    for ( i = 0; i < self->num; i++ ) {
        size += PoolStrAllocated( self->pool[i] );
    }
    return size;
}

/*
================
StrPoolSize
================
*/
static ID_INLINE size_t StrPoolSize( const strPool_t *self ) {
    int i;
    size_t size;

    size = self->size + HashIndexSize( &self->poolHash );
    for ( i = 0; i < self->num; i++ ) {
        size += PoolStrSize( self->pool[i] );
    }
    return size;
}

#endif /* !__STRPOOL_H__ */
