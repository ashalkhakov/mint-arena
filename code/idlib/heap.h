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

#include "idlib_local.h"

/*
===============================================================================

	Block based allocator for fixed size objects.

	All objects of the 'type' are properly constructed.
	However, the constructor is not called for re-used objects.

===============================================================================
*/

// NOTE: this is a template module

#ifndef BLOCK_TYPE
#error "Define BLOCK_TYPE"
#endif
#ifndef BLOCK_SIZE
#error "Define BLOCK_SIZE"
#endif

#define PASTE_HELPER2(x, y) x##y
#define PASTE2(x, y) PASTE_HELPER2(x, y)
#define PASTE_HELPER(x, y, z) x##y##z
#define PASTE(x, y, z) PASTE_HELPER(x, y, z)

#define QUALIFIED(f) PASTE(f, BLOCK_TYPE, BLOCK_SIZE)
#define METHOD(f) PASTE2(BlockAlloc, QUALIFIED(f))

typedef struct QUALIFIED(element_s) {
    struct QUALIFIED(element_s) *	next;
    BLOCK_TYPE	        		t;
} QUALIFIED(element_t);
typedef struct QUALIFIED(block_s) {
    QUALIFIED(element_t)			elements[BLOCK_SIZE];
    struct QUALIFIED(block_s) *        	next;
} QUALIFIED(block_t);

typedef struct QUALIFIED(blockAlloc) {
	QUALIFIED(block_t) *		blocks;
	QUALIFIED(element_t) *		free;
	int						total;
	int						active;
} QUALIFIED(blockAlloc);

static ID_INLINE int METHOD(GetTotalCount)( const QUALIFIED(blockAlloc) *self ) {
    return self->total;
}
static ID_INLINE int METHOD(GetAllocCount)( const QUALIFIED(blockAlloc) *self ) {
    return self->active;
}
static ID_INLINE int METHOD(GetFreeCount)( const QUALIFIED(blockAlloc) *self ) {
    return self->total - self->active;
}

void METHOD(Init)( QUALIFIED(blockAlloc) *self ) {
	self->blocks = NULL;
	self->free = NULL;
	self->total = self->active = 0;
}
void METHOD( Shutdown )( QUALIFIED(blockAlloc) *self ) {
	while( self->blocks ) {
		QUALIFIED(block_t) *block = self->blocks;
		self->blocks = self->blocks->next;
		ii.FreeMemory( block );
	}
	self->blocks = NULL;
	self->free = NULL;
	self->total = self->active = 0;

}

BLOCK_TYPE *METHOD(Alloc)( QUALIFIED(blockAlloc) *self ) {
	if ( !self->free ) {
		QUALIFIED(block_t) *block = ii.GetMemory( sizeof(QUALIFIED(block_t)) );
		block->next = self->blocks;
		self->blocks = block;
		for ( int i = 0; i < BLOCK_SIZE; i++ ) {
			block->elements[i].next = self->free;
			self->free = &block->elements[i];
		}
		self->total += BLOCK_SIZE;
	}
	self->active++;
	QUALIFIED(element_t) *element = self->free;
	self->free = self->free->next;
	element->next = NULL;
	return &element->t;
}

void METHOD(Free)( QUALIFIED(blockAlloc) *self, BLOCK_TYPE *t ) {
    QUALIFIED(element_t) *element = (QUALIFIED(element_t) *)( ( (unsigned char *) t ) - ( (int) &((QUALIFIED(element_t) *)0)->t ) );
	element->next = self->free;
	self->free = element;
	self->active--;
}

#undef QUALIFIED
#undef PASTE_HELPER
#undef PASTE
