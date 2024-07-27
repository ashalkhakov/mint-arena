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

#ifndef __STR_H__
#define __STR_H__

#include "q_shared.h"
#include "idlib_public.h"

extern idlib_import_t ii;

// make str_t a multiple of 16 bytes long
// don't make too large to keep memory requirements to a minimum
#define STR_ALLOC_BASE 20
#define STR_ALLOC_GRAN 32

typedef struct str_s {
    int     len;
    char    *data;
    int     alloced;
    char    baseBuffer[STR_ALLOC_BASE];
} str_t;

static ID_INLINE void StrInit( str_t *self ) {
    self->len = 0;
    self->alloced = STR_ALLOC_BASE;
    self->data = self->baseBuffer;
    self->data[0] = '\0';
#ifdef ID_DEBUG_UNINITIALIZED_MEMORY
    memset( baseBuffer, 0, sizeof( baseBuffer ) );
#endif
}

static ID_INLINE char* StrGetString( str_t *self ) {
    return self->data;
}

/*
============
StrReAllocate
============
*/
static ID_INLINE void StrReAllocate( str_t *self, int amount, qboolean keepold ) {
    char* newbuffer;
    int    newsize;
    int    mod;

    //assert( self->data );
    assert( amount > 0 );

    mod = amount % STR_ALLOC_GRAN;
    if ( !mod ) {
        newsize = amount;
    } else {
        newsize = amount + STR_ALLOC_GRAN - mod;
    }
    self->alloced = newsize;

    if ( self->data && self->data != self->baseBuffer) {
        char *olddata = self->data;
        self->data = ( char * )ii.GetMemory( newsize );
        memcpy( self->data, olddata, self->len );
        ii.FreeMemory( olddata );
    } else {
        newbuffer = ( char * )ii.GetMemory( newsize );
        if ( self->data && keepold ) {
            memcpy( newbuffer, self->data, self->len );
            newbuffer[self->len] = '\0';
        } else {
            newbuffer[0] = '\0';
        }
        self->data = newbuffer;
    }
}

/*
============
StrFree
============
*/
static ID_INLINE void StrFree( str_t *self ) {
    if ( self->data && self->data != self->baseBuffer) {
        ii.FreeMemory( self->data );
        self->data = self->baseBuffer;
        self->data[0] = '\0';
    }
}


static ID_INLINE void StrEnsureAlloced( str_t *self, int amount, qboolean keepold) {
    if ( amount > self->alloced ) {
        StrReAllocate( self, amount, keepold );
    }
}

static ID_INLINE int StrAllocated( const str_t *self ) {
    if ( self->data == self->baseBuffer ) {
        return 0;
    }
    return self->alloced;
}


static ID_INLINE void StrAppend( str_t *self, const char *text ) {
    int newLen;
    int i;

    if ( !text ) {
        return;
    }

    newLen = self->len + strlen( text );
    StrEnsureAlloced( self, newLen + 1, qtrue );
    for ( i = 0; text[i]; i++ ) {
        self->data[self->len + i] = text[i];
    }
    self->len = newLen;
    self->data[self->len] = '\0';
}

#endif /* !__STR_H__ */
