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

#include "hashindex.h"

int INVALID_INDEX[1] = { -1 };

/*
================
HashIndexInitWithDefaults
================
*/
void HashIndexInitWithDefaults(hashIndex_t* self) {
  HashIndexInit(self, 1024, 1024);
}

/*
================
HashIndexInit
================
*/
void HashIndexInit( hashIndex_t *self, const int initialHashSize, const int initialIndexSize ) {
  // power of two check
  assert( initialHashSize > 0 );
  assert( (initialHashSize & (initialHashSize - 1)) == 0 );

  self->hashSize = initialHashSize;
  self->hash = INVALID_INDEX;
  self->indexSize = initialIndexSize;
  self->indexChain = INVALID_INDEX;
  self->granularity = DEFAULT_HASH_GRANULARITY;
  self->hashMask = self->hashSize - 1;
  self->lookupMask = 0;
}

/*
================
HashIndexAllocate
================
*/
void HashIndexAllocate( hashIndex_t *self, const int newHashSize, const int newIndexSize ) {
  // power of two check
  assert( newHashSize > 0 );
  assert( (newHashSize & (newHashSize - 1)) == 0 );

  HashIndexFree( self );
  self->hashSize = newHashSize;
  self->hash = ii.GetMemory( sizeof(int) * self->hashSize );
  memset( self->hash, 0xff, self->hashSize * sizeof( self->hash[0] ) );
  self->indexSize = newIndexSize;
  self->indexChain = ii.GetMemory( sizeof(int) * self->indexSize );
  memset( self->indexChain, 0xff, self->indexSize * sizeof( self->indexChain[0] ) );
  self->hashMask = self->hashSize - 1;
  self->lookupMask = -1;
}

/*
================
HashIndexFree
================
*/
void HashIndexFree( hashIndex_t *self ) {
  if ( self->hash != INVALID_INDEX ) {
    if ( self->hash ) {
        ii.FreeMemory( self->hash );
    }
    self->hash = INVALID_INDEX;
  }
  if ( self->indexChain != INVALID_INDEX ) {
    if ( self->indexChain ) {
        ii.FreeMemory( self->indexChain );
    }
    self->indexChain = INVALID_INDEX;
  }
  self->lookupMask = 0;
}

/*
================
HashIndexResizeIndex
================
*/
void HashIndexResizeIndex( hashIndex_t *self, const int newIndexSize ) {
  int *oldIndexChain, mod, newSize;

  if ( newIndexSize <= self->indexSize ) {
    return;
  }

  mod = newIndexSize % self->granularity;
  if ( !mod ) {
    newSize = newIndexSize;
  } else {
    newSize = newIndexSize + self->granularity - mod;
  }

  if ( self->indexChain == INVALID_INDEX ) {
    self->indexSize = newSize;
    return;
  }

  oldIndexChain = self->indexChain;
  self->indexChain = ii.GetMemory( sizeof(int) * newSize );
  memcpy( self->indexChain, oldIndexChain, self->indexSize * sizeof(int) );
  memset( self->indexChain + self->indexSize, 0xff, (newSize - self->indexSize) * sizeof(int) );
  ii.FreeMemory( oldIndexChain );
  self->indexSize = newSize;
}

/*
================
HashIndexGetSpread
================
*/
int HashIndexGetSpread( const hashIndex_t *self ) {
  int i, index, totalItems, *numHashItems, average, error, e;

  if ( self->hash == INVALID_INDEX ) {
    return 100;
  }

  totalItems = 0;
  numHashItems = ii.GetMemory( sizeof( int ) * self->hashSize );
  for ( i = 0; i < self->hashSize; i++ ) {
    numHashItems[i] = 0;
    for ( index = self->hash[i]; index >= 0; index = self->indexChain[index] ) {
      numHashItems[i]++;
    }
    totalItems += numHashItems[i];
  }
  // if no items in hash
  if ( totalItems <= 1 ) {
    ii.FreeMemory( numHashItems );
    return 100;
  }
  average = totalItems / self->hashSize;
  error = 0;
  for ( i = 0; i < self->hashSize; i++ ) {
    e = abs( numHashItems[i] - average );
    if ( e > 1 ) {
      error += e - 1;
    }
  }
  ii.FreeMemory( numHashItems );
  return 100 - (error * 100 / totalItems);
}