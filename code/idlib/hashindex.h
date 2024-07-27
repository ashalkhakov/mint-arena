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
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.   See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.   If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.   If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#ifndef __HASHINDEX_H__
#define __HASHINDEX_H__

#include "q_shared.h"
#include "idlib_public.h"

extern idlib_import_t ii;

static ID_INLINE int StrHash(const char *string)
{
    int i, hash = 0;
    for (i = 0; *string != '\0'; i++)
    {
        hash += (*string++) * (i + 119);
    }
    return hash;
}

static ID_INLINE int StrIHash(const char *string)
{
    int i, c, hash = 0;
    for (i = 0; *string != '\0'; i++)
    {
        c = *string++;
        if ((c <= 'Z' && c >= 'A') || (c >= 0xC0 && c <= 0xDF))
        {
            c += ('a' - 'A');
        }
        hash += c * (i + 119);
    }
    return hash;
}

/*
===============================================================================

      Fast hash table for indexes and arrays.
      Does not allocate memory until the first key/index pair is added.

===============================================================================
*/

#define DEFAULT_HASH_SIZE 1024
#define DEFAULT_HASH_GRANULARITY 1024

extern int INVALID_INDEX[1];

typedef struct hashIndex_s
{
    int                        hashSize;
    int *                      hash;
    int                        indexSize;
    int *                      indexChain;
    int                        granularity;
    int                        hashMask;
    int                        lookupMask;
} hashIndex_t;

void                  HashIndexInitWithDefaults(hashIndex_t *h);
void                  HashIndexInit(hashIndex_t *h, const int initialHashSize, const int initialIndexSize);
void                  HashIndexAllocate(hashIndex_t *h, const int newHashSize, const int newIndexSize);

// free allocated memory
void                  HashIndexFree(hashIndex_t *h);
// force resizing the index, current hash table stays intact
void                  HashIndexResizeIndex(hashIndex_t *h, const int newIndexSize);
// returns number in the range [0-100] representing the spread over the hash table
int                        HashIndexGetSpread(const hashIndex_t *h);

/*
================
HashIndexAllocated

returns total size of allocated memory
================
*/
static ID_INLINE size_t HashIndexAllocated(const hashIndex_t *h)
{
    return h->hashSize * sizeof(int) + h->indexSize * sizeof(int);
}

/*
================
HashIndexSize

returns total size of allocated memory including size of hash index type
================
*/
static ID_INLINE size_t HashIndexSize(const hashIndex_t *h)
{
    return sizeof(*h) + HashIndexAllocated(h);
}

/*
================
HashIndexInitFrom
================
*/
static ID_INLINE void HashIndexInitFrom(hashIndex_t *self, const hashIndex_t *other)
{
    self->granularity = other->granularity;
    self->hashMask = other->hashMask;
    self->lookupMask = other->lookupMask;

    if (other->lookupMask == 0)
    {
              self->hashSize = other->hashSize;
              self->indexSize = other->indexSize;
              HashIndexFree(self);
             
    }
    else
    {
        if (other->hashSize != self->hashSize || self->hash == INVALID_INDEX)
        {
            if (self->hash != INVALID_INDEX)
            {
                                  ii.FreeMemory(self->hash);
                           
            }
                        self->hashSize = other->hashSize;
                        self->hash = (int *)ii.GetMemory(sizeof(int) * self->hashSize);
                 
        }
              if (other->indexSize != self->indexSize || self->indexChain == INVALID_INDEX)
        {
                        if (self->indexChain != INVALID_INDEX)
            {
                                  ii.FreeMemory(self->indexChain);
                           
            }
                        self->indexSize = other->indexSize;
                        self->indexChain = (int *)ii.GetMemory(sizeof(int) * self->indexSize);
                 
        }
              memcpy(self->hash, other->hash, self->hashSize * sizeof(self->hash[0]));
              memcpy(self->indexChain, other->indexChain, self->indexSize * sizeof(self->indexChain[0]));
             
    }
}

/*
================
HashIndexAdd

add an index to the hash, assumes the index has not yet been added to the hash
================
*/
static ID_INLINE void HashIndexAdd(hashIndex_t *self, const int key, const int index)
{
    int h;

    assert(index >= 0);
    if (self->hash == INVALID_INDEX)
    {
              HashIndexAllocate(self, self->hashSize, index >= self->indexSize ? index + 1 : self->indexSize);
             
    }
    else if (index >= self->indexSize)
    {
              HashIndexResizeIndex(self, index + 1);
             
    }
    h = key & self->hashMask;
    self->indexChain[index] = self->hash[h];
    self->hash[h] = index;
}

/*
================
HashIndexRemove

remove an index from the hash
================
*/
static ID_INLINE void HashIndexRemove(hashIndex_t *self, const int key, const int index)
{
    int k = key & self->hashMask;

    if (self->hash == INVALID_INDEX)
    {
              return;
             
    }
    if (self->hash[k] == index)
    {
              self->hash[k] = self->indexChain[index];
             
    }
    else
    {
              for (int i = self->hash[k]; i != -1; i = self->indexChain[i])
        {
                        if (self->indexChain[i] == index)
            {
                                  self->indexChain[i] = self->indexChain[index];
                                  break;
                           
            }
                 
        }
             
    }
    self->indexChain[index] = -1;
}

/*
================
HashIndexFirst

get the first index from the hash, returns -1 if empty hash entry
================
*/
static ID_INLINE int HashIndexFirst(const hashIndex_t *self, const int key)
{
    return self->hash[key & self->hashMask & self->lookupMask];
}

/*
================
HashIndexNext

get the next index from the hash, returns -1 if at the end of the hash chain
================
*/
static ID_INLINE int HashIndexNext(const hashIndex_t *self, const int index)
{
    assert(index >= 0 && index < self->indexSize);
    return self->indexChain[index & self->lookupMask];
}

/*
================
HashIndexInsertIndex
================
*/
static ID_INLINE void HashIndexInsertIndex(hashIndex_t *self, const int key, const int index)
{
    int i, max;

    if (self->hash != INVALID_INDEX)
    {
              max = index;
              for (i = 0; i < self->hashSize; i++)
        {
                        if (self->hash[i] >= index)
            {
                                  self->hash[i]++;
                                  if (self->hash[i] > max)
                {
                                            max = self->hash[i];
                                     
                }
                           
            }
                 
        }
              for (i = 0; i < self->indexSize; i++)
        {
                        if (self->indexChain[i] >= index)
            {
                                  self->indexChain[i]++;
                                  if (self->indexChain[i] > max)
                {
                                            max = self->indexChain[i];
                                     
                }
                           
            }
                 
        }
              if (max >= self->indexSize)
        {
                        HashIndexResizeIndex(self, max + 1);
                 
        }
              for (i = max; i > index; i--)
        {
                        self->indexChain[i] = self->indexChain[i - 1];
                 
        }
              self->indexChain[index] = -1;
             
    }
    HashIndexAdd(self, key, index);
}

/*
================
HashIndexRemoveIndex

remove an entry from the index and remove it from the hash, decreasing all indexes >= index
================
*/
static ID_INLINE void HashIndexRemoveIndex(hashIndex_t *self, const int key, const int index)
{
    int i, max;

    HashIndexRemove(self, key, index);
    if (self->hash != INVALID_INDEX)
    {
              max = index;
              for (i = 0; i < self->hashSize; i++)
        {
                        if (self->hash[i] >= index)
            {
                                  if (self->hash[i] > max)
                {
                                            max = self->hash[i];
                                     
                }
                                  self->hash[i]--;
                           
            }
                 
        }
              for (i = 0; i < self->indexSize; i++)
        {
                        if (self->indexChain[i] >= index)
            {
                                  if (self->indexChain[i] > max)
                {
                                            max = self->indexChain[i];
                                     
                }
                                  self->indexChain[i]--;
                           
            }
                 
        }
              for (i = index; i < max; i++)
        {
                        self->indexChain[i] = self->indexChain[i + 1];
                 
        }
              self->indexChain[max] = -1;
             
    }
}

/*
================
HashIndexClear

clear the hash
================
*/
static ID_INLINE void HashIndexClear(hashIndex_t *self)
{
    // only clear the hash table because clearing the indexChain is not really needed
          if (self->hash != INVALID_INDEX)
    {
              memset(self->hash, 0xff, self->hashSize * sizeof(self->hash[0]));
             
    }
}

/*
================
HashIndexClearAndResize
================
*/
static ID_INLINE void HashIndexClearAndResize(hashIndex_t *self, const int newHashSize, const int newIndexSize)
{
    HashIndexFree(self);
    self->hashSize = newHashSize;
    self->indexSize = newIndexSize;
}

/*
================
HashIndexGetHashSize
================
*/
static ID_INLINE int HashIndexGetHashSize(const hashIndex_t *self)
{
    return self->hashSize;
}

/*
================
HashIndexGetIndexSize
================
*/
static ID_INLINE int HashIndexGetIndexSize(const hashIndex_t *self)
{
    return self->indexSize;
}

/*
================
HashIndexSetGranularity
================
*/
static ID_INLINE void HashIndexSetGranularity(hashIndex_t *self, const int newGranularity)
{
    assert(newGranularity > 0);
    self->granularity = newGranularity;
}

/*
================
HashIndexGenerateKeyForString

returns a key for a string
================
*/
static ID_INLINE int HashIndexGenerateKeyForString(const hashIndex_t *self, const char *string, qboolean caseSensitive)
{
    if (caseSensitive)
    {
              return (StrHash(string) & self->hashMask);
             
    }
    else
    {
              return (StrIHash(string) & self->hashMask);
             
    }
}

/*
================
HashIndexGenerateKeyForVector

returns a key for a vector
================
*/
static ID_INLINE int HashIndexGenerateKeyForVector(const hashIndex_t *self, const vec3_t v)
{
    return ((((int)v[0]) + ((int)v[1]) + ((int)v[2])) & self->hashMask);
}

/*
================
HashIndexGenerateKeyForEdge

returns a key for two integers
================
*/
static ID_INLINE int HashIndexGenerateKeyForEdge(const hashIndex_t *self, const int n1, const int n2)
{
    return ((n1 + n2) & self->hashMask);
}

#endif /* !__HASHINDEX_H__ */