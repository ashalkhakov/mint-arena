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

#include "../idlib/surfaceflags.h"
#include "g_local.h"

#define Sqr(x) ((x) * (x))
#define Max(x, y) ((x) > (y) ? (x) : (y))

static ID_INLINE int FloatHash( const float *array, const int numFloats ) {
	int i, hash = 0;
	const int *ptr;

	ptr = ( const int * )( array );
	for ( i = 0; i < numFloats; i++ ) {
		hash ^= ptr[i];
	}
	return hash;
}

#define	MAX_SECTOR_DEPTH				12
#define MAX_SECTORS						((1<<(MAX_SECTOR_DEPTH+1))-1)

typedef struct clipSector_s {
	int						axis;		// -1 = leaf node
	float					dist;
	struct clipSector_s *	children[2];
	struct clipLink_s *		clipLinks;
} clipSector_t;

typedef struct clipLink_s {
	clipModel_t *			clipModel;
	struct clipSector_s *	sector;
	struct clipLink_s *		prevInSector;
	struct clipLink_s *		nextInSector;
	struct clipLink_s *		nextLink;
} clipLink_t;

typedef struct trmCache_s {
	traceModel_t			trm;
	int						refCount;
	float					volume;
	vec3_t					centerOfMass;
	vec3_t					inertiaTensor[3];
} trmCache_t;

vec3_t vec3_boxEpsilon = { CM_BOX_EPSILON, CM_BOX_EPSILON, CM_BOX_EPSILON };

#define BLOCK_TYPE clipLink_t
#define BLOCK_SIZE 1024
#include "../idlib/heap.h"

blockAllocclipLink_t1024	clipLinkAllocator;

/*
===============================================================

	clipModel_t trace model cache

===============================================================
*/

static int                  numTraceModelCache;
static int                  sizeTraceModelCache;
static trmCache_t** 		traceModelCache;
static hashIndex_t			traceModelHash;
static qboolean             traceModelCacheInitialized;

/*
===============
ClipModelInitTraceModelCache
===============
*/
void ClipModelInitTraceModelCache( clip_t *self ) {
    HashIndexInit( &traceModelHash, 1024, 512 );
}

/*
===============
ClipModelClearTraceModelCache
===============
*/
void ClipModelClearTraceModelCache( clip_t *self ) {
    int i;

    for ( i = 0; i < numTraceModelCache; i++ ) {
        ii.FreeMemory( traceModelCache[i] );
    }
    if ( traceModelCache ) {
        ii.FreeMemory( traceModelCache );
        traceModelCache = NULL;
    }
    numTraceModelCache = 0;
    sizeTraceModelCache = 0;
	HashIndexFree( &traceModelHash );
}

/*
===============
ClipModelTraceModelCacheSize
===============
*/
int ClipModelTraceModelCacheSize( clip_t *self ) {
	return numTraceModelCache * sizeof( traceModel_t );
}

/*
===============
ClipModelAllocTraceModel
===============
*/
int ClipModelAllocTraceModel( const traceModel_t *trm ) {
	int i, hashKey, traceModelIndex;
	trmCache_t *entry;

	hashKey = ClipModelGetTraceModelHashKey( trm );
	for ( i = HashIndexFirst( &traceModelHash, hashKey ); i >= 0; i = HashIndexNext( &traceModelHash, i ) ) {
		if ( TraceModelCompare( &traceModelCache[i]->trm, trm ) ) {
			traceModelCache[i]->refCount++;
			return i;
		}
	}

	entry = ( trmCache_t * ) ii.GetMemory( sizeof( trmCache_t ) );
    memcpy( &entry->trm, trm, sizeof( entry->trm ) );
    TraceModelGetMassProperties( &entry->trm, 1.0f, &entry->volume, entry->centerOfMass, entry->inertiaTensor );
	entry->refCount = 1;

    if ( numTraceModelCache >= sizeTraceModelCache ) {
        sizeTraceModelCache = sizeTraceModelCache == 0 ? 64 : sizeTraceModelCache * 1.5;
        trmCache_t **newCache = ( trmCache_t ** )ii.GetMemory( sizeof( *traceModelCache ) * sizeTraceModelCache );
        memset( newCache, 0, sizeof( *traceModelCache ) * sizeTraceModelCache );
        if ( traceModelCache ) {
            memcpy( newCache, traceModelCache, sizeof( *traceModelCache ) * numTraceModelCache );
            ii.FreeMemory( traceModelCache );
        }
        traceModelCache = newCache;
    }
    traceModelCache[numTraceModelCache] = entry;
    traceModelIndex = numTraceModelCache;
    numTraceModelCache++;
	HashIndexAdd( &traceModelHash, hashKey, traceModelIndex );
	return traceModelIndex;
}

/*
===============
ClipModelFreeTraceModel
===============
*/
void ClipModelFreeTraceModel( int traceModelIndex ) {
	if ( traceModelIndex < 0 || traceModelIndex >= numTraceModelCache || traceModelCache[traceModelIndex]->refCount <= 0 ) {
		ii.Com_Printf( "ClipModelFreeTraceModel: tried to free uncached trace model" );
		return;
	}
	traceModelCache[traceModelIndex]->refCount--;
}

/*
===============
ClipModelGetCachedTraceModel
===============
*/
traceModel_t *ClipModelGetCachedTraceModel( int traceModelIndex ) {
	return &traceModelCache[traceModelIndex]->trm;
}

/*
===============
ClipModelGetTraceModelHashKey
===============
*/
int ClipModelGetTraceModelHashKey( const traceModel_t *trm ) {
	return ( trm->type << 8 ) ^ ( trm->numVerts << 4 ) ^ ( trm->numEdges << 2 ) ^ ( trm->numPolys << 0 ) ^ FloatHash( trm->bounds[0], 3 );
}


/*
===============================================================

	clipModel_t

===============================================================
*/

/*
================
ClipModelLoadModel
================
*/
qboolean ClipModelLoadModel( clipModel_t *self, const char *name ) {
	self->renderModelHandle = -1;
	if ( self->traceModelIndex != -1 ) {
		ClipModelFreeTraceModel( self->traceModelIndex );
		self->traceModelIndex = -1;
	}
	self->collisionModelHandle = trap_CM_LoadModel( name, qfalse );
	if ( self->collisionModelHandle ) {
		trap_CM_GetModelBounds( self->collisionModelHandle, self->bounds );
		trap_CM_GetModelContents( self->collisionModelHandle, &self->contents );
		return qtrue;
	} else {
		VectorClear( self->bounds[0] );
        VectorClear( self->bounds[1] );
		return qfalse;
	}
}

/*
================
ClipModelLoadTraceModel
================
*/
void ClipModelLoadTraceModel( clipModel_t *self, const traceModel_t *trm ) {
	self->collisionModelHandle = 0;
	self->renderModelHandle = -1;
	if ( self->traceModelIndex != -1 ) {
		ClipModelFreeTraceModel( self->traceModelIndex );
	}
	self->traceModelIndex = ClipModelAllocTraceModel( trm );
    VectorCopy( trm->bounds[0], self->bounds[0] );
    VectorCopy( trm->bounds[1], self->bounds[1] );
}

/*
================
ClipModelLoadRenderModel
================
*/
void ClipModelLoadRenderModel( clipModel_t *self, const int renderModelHandle ) {
	self->collisionModelHandle = 0;
	self->renderModelHandle = renderModelHandle;
	/*
    // TODO: make this work
    if ( renderModelHandle != -1 ) {
		const renderEntity_t *renderEntity = gameRenderWorld->GetRenderEntity( renderModelHandle );
		if ( renderEntity ) {
			bounds = renderEntity->bounds;
		}
	}
    */
	if ( self->traceModelIndex != -1 ) {
		ClipModelFreeTraceModel( self->traceModelIndex );
		self->traceModelIndex = -1;
	}
}

/*
================
ClipModelInit
================
*/
void ClipModelInit( clipModel_t *self ) {
	self->enabled = qtrue;
	self->entity = NULL;
	self->id = 0;
	self->owner = NULL;
	VectorClear( self->origin );
	AxisClear( self->axis );
    VectorClear( self->bounds[0] );
    VectorClear( self->bounds[1] );
    VectorClear( self->absBounds[0] );
    VectorClear( self->absBounds[1] );
	self->material = 0;
	self->contents = CONTENTS_BODY;
	self->collisionModelHandle = 0;
	self->renderModelHandle = -1;
	self->traceModelIndex = -1;
	self->clipLinks = NULL;
	self->touchCount = -1;
}

/*
================
ClipModelInitFromModel
================
*/
void ClipModelInitFromModel( clipModel_t *self, const char *name ) {
	ClipModelInit( self );
	ClipModelLoadModel( self, name );
}

/*
================
ClipModelInitFromTraceModel
================
*/
void ClipModelInitFromTraceModel( clipModel_t *self, const traceModel_t *trm ) {
	ClipModelInit( self );
	ClipModelLoadTraceModel( self, trm );
}

/*
================
ClipModelInitFromRenderModel
================
*/
void ClipModelInitFromRenderModel( clipModel_t *self, const int renderModelHandle ) {
	ClipModelInit( self );
    // TODO: make this work
	//self->contents = CONTENTS_RENDERMODEL;
	ClipModelLoadRenderModel( self, renderModelHandle );
}

/*
================
ClipModelCopy
================
*/
void ClipModelCopy( const clipModel_t *model, clipModel_t *out ) {
	out->enabled = model->enabled;
	out->entity = model->entity;
	out->id = model->id;
	out->owner = model->owner;
    VectorCopy( model->origin, out->origin );
    AxisCopy( model->axis, out->axis );
    VectorCopy( model->bounds[0], out->bounds[0] );
    VectorCopy( model->bounds[1], out->bounds[1] );
    VectorCopy( model->absBounds[0], out->absBounds[0] );
    VectorCopy( model->absBounds[1], out->absBounds[1] );
	out->material = model->material;
	out->contents = model->contents;
	out->collisionModelHandle = model->collisionModelHandle;
	out->traceModelIndex = -1;
	if ( model->traceModelIndex != -1 ) {
		ClipModelLoadTraceModel( out, ClipModelGetCachedTraceModel( model->traceModelIndex ) );
	}
	out->renderModelHandle = model->renderModelHandle;
	out->clipLinks = NULL;
	out->touchCount = -1;
}

/*
================
ClipModelFree
================
*/
void ClipModelFree( clipModel_t *self ) {
	// make sure the clip model is no longer linked
	ClipModelUnlink( self );
	if ( self->traceModelIndex != -1 ) {
		ClipModelFreeTraceModel( self->traceModelIndex );
	}
}

/*
================
ClipModelSetPosition
================
*/
void ClipModelSetPosition( clipModel_t *self, const vec3_t newOrigin, const vec3_t newAxis[3] ) {
	if ( self->clipLinks ) {
		ClipModelUnlink( self );	// unlink from old position
	}
	VectorCopy( newOrigin, self->origin );
	AxisCopy( newAxis, self->axis );
}

/*
================
ClipModelHandle
================
*/
cmHandle_t ClipModelHandle( const clipModel_t *self ) {
	assert( self->renderModelHandle == -1 );
	if ( self->collisionModelHandle ) {
		return self->collisionModelHandle;
	} else if ( self->traceModelIndex != -1 ) {
		return trap_CM_SetupTrmModel( ClipModelGetCachedTraceModel( self->traceModelIndex ), self->material );
	} else {
		// this happens in multiplayer on the combat models
		ii.Com_Printf( "ClipModelHandle: clip model %d (%x) is not a collision or trace model", self->id, self->entity->s.number );
		return 0;
	}
}

/*
================
ClipModelGetMassProperties
================
*/
void ClipModelGetMassProperties( const clipModel_t *self, const float density, float *mass, vec3_t centerOfMass, vec3_t inertiaTensor[3] ) {
	if ( self->traceModelIndex == -1 ) {
		ii.Com_Error( ERR_DROP, "ClipModelGetMassProperties: clip model %d ('%x') is not a trace model\n", self->id, self->entity->s.number );
	}

	trmCache_t *entry = traceModelCache[self->traceModelIndex];
	*mass = entry->volume * density;
	VectorCopy( entry->centerOfMass, centerOfMass );
    VectorScale( entry->inertiaTensor[0], density, inertiaTensor[0] );
    VectorScale( entry->inertiaTensor[1], density, inertiaTensor[1] );
    VectorScale( entry->inertiaTensor[2], density, inertiaTensor[2] );
}

/*
===============
ClipModelUnlink
===============
*/
void ClipModelUnlink( clipModel_t *self ) {
	clipLink_t *link;

	for ( link = self->clipLinks; link; link = self->clipLinks ) {
		self->clipLinks = link->nextLink;
		if ( link->prevInSector ) {
			link->prevInSector->nextInSector = link->nextInSector;
		} else {
			link->sector->clipLinks = link->nextInSector;
		}
		if ( link->nextInSector ) {
			link->nextInSector->prevInSector = link->prevInSector;
		}
        BlockAllocFreeclipLink_t1024( &clipLinkAllocator, link );
	}
}

/*
===============
ClipModelLink_r
===============
*/
void ClipModelLink_r( clipModel_t *self, struct clipSector_s *node ) {
	clipLink_t *link;

	while( node->axis != -1 ) {
		if ( self->absBounds[0][node->axis] > node->dist ) {
			node = node->children[0];
		} else if ( self->absBounds[1][node->axis] < node->dist ) {
			node = node->children[1];
		} else {
			ClipModelLink_r( self, node->children[0] );
			node = node->children[1];
		}
	}

	link = BlockAllocAllocclipLink_t1024( &clipLinkAllocator );
	link->clipModel = self;
	link->sector = node;
	link->nextInSector = node->clipLinks;
	link->prevInSector = NULL;
	if ( node->clipLinks ) {
		node->clipLinks->prevInSector = link;
	}
	node->clipLinks = link;
	link->nextLink = self->clipLinks;
	self->clipLinks = link;
}

/*
===============
ClipModelLink
===============
*/
void ClipModelLink( clipModel_t *self, clip_t *clp ) {

	assert( self->entity );
	if ( !self->entity ) {
		return;
	}

	if ( self->clipLinks ) {
		ClipModelUnlink( self );	// unlink from old position
	}

	if ( self->bounds[0][0] > self->bounds[1][0] ) {
		return;
	}

	// set the abs box
	if ( AxisIsRotated( self->axis ) ) {
		// expand for rotation
        BoundsFromTransformedBounds( self->absBounds, self->bounds, self->origin, self->axis );
	} else {
		// normal
        VectorAdd( self->bounds[0], self->origin, self->absBounds[0] );
        VectorAdd( self->bounds[1], self->origin, self->absBounds[1] );
	}

	// because movement is clipped an epsilon away from an actual edge,
	// we must fully check even when bounding boxes don't quite touch
    VectorSubtract( self->absBounds[0], vec3_boxEpsilon, self->absBounds[0] );
	VectorAdd( self->absBounds[1], vec3_boxEpsilon, self->absBounds[1] );

	ClipModelLink_r( self, clp->clipSectors );
}

/*
===============
ClipModelLink2
===============
*/
void ClipModelLink2( clipModel_t *self, clip_t *clp, sharedEntity_t *ent, int newId, const vec3_t newOrigin, const vec3_t newAxis[3], int renderModelHandle ) {

	self->entity = ent;
    self->id = newId;
	VectorCopy( newOrigin, self->origin );
	AxisCopy( newAxis, self->axis );
	if ( renderModelHandle != -1 ) {
		self->renderModelHandle = renderModelHandle;
        /*
        // TODO: make this work
		const renderEntity_t *renderEntity = gameRenderWorld->GetRenderEntity( renderModelHandle );
		if ( renderEntity ) {
			this->bounds = renderEntity->bounds;
		}
        */
	}
	ClipModelLink( self, clp );
}

/*
============
ClipModelCheckModel
============
*/
cmHandle_t ClipModelCheckModel( const char *name ) {
	return trap_CM_LoadModel( name, qfalse );
}

/*
===============================================================

	clip_t

===============================================================
*/

clipSector_t *ClipCreateClipSectors_r( clip_t *self, const int depth, const vec3_t bounds[2], vec3_t maxSector );

/*
===============
ClipInit
===============
*/
void ClipInit( clip_t *self ) {
	cmHandle_t h;
	vec3_t size, maxSector;
    traceModel_t trm;

    VectorClear( maxSector );

    if ( !traceModelCacheInitialized ) {
        ClipModelInitTraceModelCache( self );
        traceModelCacheInitialized = qtrue;
    }
    ClipModelClearTraceModelCache( self );

	// clear clip sectors
	self->clipSectors = ii.GetMemory( sizeof( clipSector_t ) * MAX_SECTORS );
	memset( self->clipSectors, 0, MAX_SECTORS * sizeof( clipSector_t ) );
	self->numClipSectors = 0;
	self->touchCount = -1;
	// get world map bounds
	h = trap_CM_LoadModel( "worldMap", qfalse );
	trap_CM_GetModelBounds( h, self->worldBounds );
	// create world sectors
	ClipCreateClipSectors_r( self, 0, self->worldBounds, maxSector );

	VectorSubtract( self->worldBounds[1], self->worldBounds[0], size );
	ii.Com_Printf( "map bounds are (%1.1f, %1.1f, %1.1f)\n", size[0], size[1], size[2] );
	ii.Com_Printf( "max clip sector is (%1.1f, %1.1f, %1.1f)\n", maxSector[0], maxSector[1], maxSector[2] );

	// initialize a default clip model
    trm.type = TRM_INVALID;
    TraceModelSetupBox2( &trm, 8 );
	ClipModelInitFromTraceModel( &self->defaultClipModel, &trm );

	// set counters to zero
	self->numRotations = self->numTranslations = self->numMotions = self->numRenderModelTraces = self->numContents = self->numContacts = 0;
}

/*
===============
ClipFree
===============
*/
void ClipFree( clip_t *self ) {
    if ( self->clipSectors ) {
	    ii.FreeMemory( self->clipSectors );
    }
	self->clipSectors = NULL;

	// free the trace model used for the temporaryClipModel
	if ( self->temporaryClipModel.traceModelIndex != -1 ) {
		ClipModelFreeTraceModel( self->temporaryClipModel.traceModelIndex );
		self->temporaryClipModel.traceModelIndex = -1;
	}

	// free the trace model used for the defaultClipModel
	if ( self->defaultClipModel.traceModelIndex != -1 ) {
		ClipModelFreeTraceModel( self->defaultClipModel.traceModelIndex );
		self->defaultClipModel.traceModelIndex = -1;
	}

    BlockAllocShutdownclipLink_t1024( &clipLinkAllocator );

    ClipModelClearTraceModelCache( self );
}

/*
===============
ClipCreateClipSectors_r

Builds a uniformly subdivided tree for the given world size
===============
*/
clipSector_t *ClipCreateClipSectors_r( clip_t *self, const int depth, const vec3_t bounds[2], vec3_t maxSector ) {
	int				i;
	clipSector_t	*anode;
	vec3_t			size;
	vec3_t		    front[2], back[2];

	anode = &self->clipSectors[self->numClipSectors];
	self->numClipSectors++;

	if ( depth == MAX_SECTOR_DEPTH ) {
		anode->axis = -1;
		anode->children[0] = anode->children[1] = NULL;

		for ( i = 0; i < 3; i++ ) {
			if ( bounds[1][i] - bounds[0][i] > maxSector[i] ) {
				maxSector[i] = bounds[1][i] - bounds[0][i];
			}
		}
		return anode;
	}

	VectorSubtract( bounds[1], bounds[0], size );
	if ( size[0] >= size[1] && size[0] >= size[2] ) {
		anode->axis = 0;
	} else if ( size[1] >= size[0] && size[1] >= size[2] ) {
		anode->axis = 1;
	} else {
		anode->axis = 2;
	}

	anode->dist = 0.5f * ( bounds[1][anode->axis] + bounds[0][anode->axis] );

	VectorCopy( bounds[0], front[0] );
    VectorCopy( bounds[1], front[1] );
    VectorCopy( bounds[0], back[0] );
    VectorCopy( bounds[1], back[1] );
	
	front[0][anode->axis] = back[1][anode->axis] = anode->dist;
	
	anode->children[0] = ClipCreateClipSectors_r( self, depth+1, front, maxSector );
	anode->children[1] = ClipCreateClipSectors_r( self, depth+1, back, maxSector );

	return anode;
}

/*
====================
ClipModelsTouchingBounds_r
====================
*/
typedef struct listParms_s {
	vec3_t  		bounds[2];
	int				contentMask;
	clipModel_t	**	list;
	int				count;
	int				maxCount;
} listParms_t;

void ClipModelsTouchingBounds_r( const clip_t *self, const struct clipSector_s *node, listParms_t *parms ) {

	while( node->axis != -1 ) {
		if ( parms->bounds[0][node->axis] > node->dist ) {
			node = node->children[0];
		} else if ( parms->bounds[1][node->axis] < node->dist ) {
			node = node->children[1];
		} else {
			ClipModelsTouchingBounds_r( self, node->children[0], parms );
			node = node->children[1];
		}
	}

	for ( clipLink_t *link = node->clipLinks; link; link = link->nextInSector ) {
		clipModel_t	*check = link->clipModel;

		// if the clip model is enabled
		if ( !check->enabled ) {
			continue;
		}

		// avoid duplicates in the list
		if ( check->touchCount == self->touchCount ) {
			continue;
		}

		// if the clip model does not have any contents we are looking for
		if ( !( check->contents & parms->contentMask ) ) {
			continue;
		}

		// if the bounds really do overlap
		if (	check->absBounds[0][0] > parms->bounds[1][0] ||
				check->absBounds[1][0] < parms->bounds[0][0] ||
				check->absBounds[0][1] > parms->bounds[1][1] ||
				check->absBounds[1][1] < parms->bounds[0][1] ||
				check->absBounds[0][2] > parms->bounds[1][2] ||
				check->absBounds[1][2] < parms->bounds[0][2] ) {
			continue;
		}

		if ( parms->count >= parms->maxCount ) {
		    ii.Com_Printf( "ClipModelsTouchingBounds_r: max count" );
			return;
		}

		check->touchCount = self->touchCount;
		parms->list[parms->count] = check;
		parms->count++;
	}
}

/*
================
ClipModelsTouchingBounds
================
*/
int ClipModelsTouchingBounds( clip_t *self, const vec3_t bounds[2], int contentMask, clipModel_t **clipModelList, int maxCount ) {
	listParms_t parms;

	if (	bounds[0][0] > bounds[1][0] ||
			bounds[0][1] > bounds[1][1] ||
			bounds[0][2] > bounds[1][2] ) {
		// we should not go through the tree for degenerate or backwards bounds
		assert( qfalse );
		return 0;
	}

	VectorSubtract( bounds[0], vec3_boxEpsilon, parms.bounds[0] );
	VectorAdd( bounds[1], vec3_boxEpsilon, parms.bounds[1] );
	parms.contentMask = contentMask;
	parms.list = clipModelList;
	parms.count = 0;
	parms.maxCount = maxCount;

	self->touchCount++;
	ClipModelsTouchingBounds_r( self, self->clipSectors, &parms );

	return parms.count;
}

/*
================
ClipEntitiesTouchingBounds
================
*/
int ClipEntitiesTouchingBounds( clip_t *self, const vec3_t bounds[2], int contentMask, sharedEntity_t **entityList, int maxCount ) {
	clipModel_t *clipModelList[MAX_GENTITIES];
	int i, j, count, entCount;

	count = ClipModelsTouchingBounds( self, bounds, contentMask, clipModelList, MAX_GENTITIES );
	entCount = 0;
	for ( i = 0; i < count; i++ ) {
		// entity could already be in the list because an entity can use multiple clip models
		for ( j = 0; j < entCount; j++ ) {
			if ( entityList[j] == clipModelList[i]->entity ) {
				break;
			}
		}
		if ( j >= entCount ) {
			if ( entCount >= maxCount ) {
				ii.Com_Printf( "ClipEntitiesTouchingBounds: max count" );
				return entCount;
			}
			entityList[entCount] = clipModelList[i]->entity;
			entCount++;
		}
	}

	return entCount;
}

/*
====================
ClipGetTraceClipModels

  an ent will be excluded from testing if:
  cm->entity == passEntity ( don't clip against the pass entity )
  cm->entity == passOwner ( missiles don't clip with owner )
  cm->owner == passEntity ( don't interact with your own missiles )
  cm->owner == passOwner ( don't interact with other missiles from same owner )
====================
*/
int ClipGetTraceClipModels( clip_t *self, const vec3_t bounds[2], int contentMask, const sharedEntity_t *passEntity, clipModel_t **clipModelList ) {
	int i, num;
	clipModel_t	*cm;
	gentity_t *passOwner;
    const gentity_t *passGEntity;

	num = ClipModelsTouchingBounds( self, bounds, contentMask, clipModelList, MAX_GENTITIES );

	if ( !passEntity ) {
		return num;
	}

    // TODO: make this work for clipModels > 1?
    passGEntity = ( gentity_t * )( passEntity );
    //->GetPhysics()->GetNumClipModels() > 0
	if ( ClipModelIsEnabled( &passGEntity->clipModel ) ) {
		//passOwner = passEntity->GetPhysics()->GetClipModel()->GetOwner();
        passOwner = ( gentity_t * )ClipModelGetOwner( &passGEntity->clipModel );
	} else {
		passOwner = NULL;
	}

	for ( i = 0; i < num; i++ ) {

		cm = clipModelList[i];

		// check if we should ignore this entity
		if ( cm->entity == passEntity ) {
			clipModelList[i] = NULL;			// don't clip against the pass entity
		} else if ( cm->entity == ( sharedEntity_t * ) passOwner ) {
			clipModelList[i] = NULL;			// missiles don't clip with their owner
		} else if ( cm->owner ) {
			if ( cm->owner == passEntity ) {
				clipModelList[i] = NULL;		// don't clip against own missiles
			} else if ( cm->owner == ( sharedEntity_t * ) passOwner ) {
				clipModelList[i] = NULL;		// don't clip against other missiles from same owner
			}
		}
	}

	return num;
}

/*
============
ClipTraceRenderModel
============
*/
void ClipTraceRenderModel( const clip_t *self, cm_trace_t *trace, const vec3_t start, const vec3_t end, const float radius, const vec3_t axis[3], clipModel_t *touch ) {
    // TODO: make this work
	trace->fraction = 1.0f;
/*
	// if the trace is passing through the bounds
	if ( touch->absBounds.Expand( radius ).LineIntersection( start, end ) ) {
		modelcm_trace_t modelTrace;

		// test with exact render model and modify cm_trace_t structure accordingly
		if ( gameRenderWorld->ModelTrace( modelTrace, touch->renderModelHandle, start, end, radius ) ) {
			trace.fraction = modelTrace.fraction;
			trace.endAxis = axis;
			trace.endpos = modelTrace.point;
			trace.c.normal = modelTrace.normal;
			trace.c.dist = modelTrace.point * modelTrace.normal;
			trace.c.point = modelTrace.point;
			trace.c.type = CONTACT_TRMVERTEX;
			trace.c.modelFeature = 0;
			trace.c.trmFeature = 0;
			trace.c.contents = modelTrace.material->GetContentFlags();
			trace.c.material = modelTrace.material;
			// NOTE: trace.c.id will be the joint number
			touch->id = JOINT_HANDLE_TO_CLIPMODEL_ID( modelTrace.jointNumber );
		}
	}*/
}

/*
============
ClipTraceModelForClipModel
============
*/
const traceModel_t *ClipTraceModelForClipModel( clip_t *self, const clipModel_t *mdl ) {
	if ( !mdl ) {
		return NULL;
	} else {
		if ( !ClipModelIsTraceModel( mdl ) ) {
			if ( ClipModelGetEntity( mdl ) ) {
				ii.Com_Error( ERR_DROP, "ClipTraceModelForClipModel: clip model %d on %x is not a trace model\n", ClipModelGetId( mdl ), ClipModelGetEntity( mdl )->s.number );
			} else {
				ii.Com_Error( ERR_DROP, "ClipTraceModelForClipModel: clip model %d is not a trace model\n", ClipModelGetId( mdl ) );
			}
		}
		return ClipModelGetCachedTraceModel( mdl->traceModelIndex );
	}
}

/*
============
ClipTestHugeTranslation
============
*/
static ID_INLINE qboolean ClipTestHugeTranslation( cm_trace_t *results, const clipModel_t *mdl, const vec3_t start, const vec3_t end, const vec3_t trmAxis[3] ) {
    vec3_t dir;

    VectorSubtract( end, start, dir );
	if ( mdl != NULL && VectorLengthSquared( dir ) > Sqr( CM_MAX_TRACE_DIST ) ) {
		//assert( 0 );

		results->fraction = 0.0f;
		VectorCopy( start, results->endpos );
		AxisCopy( trmAxis, results->endAxis );
		memset( &results->c, 0, sizeof( results->c ) );
		VectorCopy( start, results->c.point );
		results->c.entityNum = ENTITYNUM_WORLD;

		if ( ClipModelGetEntity( mdl ) ) {
			ii.Com_Printf( "huge translation for clip model %d on entity %d\n", ClipModelGetId( mdl ), ClipModelGetEntity( mdl )->s.number );
		} else {
			ii.Com_Printf( "huge translation for clip model %d\n", ClipModelGetId( mdl ) );
		}
		return qtrue;
	}
	return qfalse;
}

/*
============
ClipTranslationEntities
============
*/
void ClipTranslationEntities( clip_t *self, cm_trace_t *results, const vec3_t start, const vec3_t end,
						const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask, const sharedEntity_t *passEntity ) {
	int i, num;
	clipModel_t *touch, *clipModelList[MAX_GENTITIES];
	vec3_t traceBounds[2];
	float radius;
	cm_trace_t trace;
	const traceModel_t *trm;
    vec3_t dir;

	if ( ClipTestHugeTranslation( results, mdl, start, end, trmAxis ) ) {
		return;
	}

	trm = ClipTraceModelForClipModel( self, mdl );

	results->fraction = 1.0f;
	VectorCopy( end, results->endpos );
	AxisCopy( results->endAxis, trmAxis );

    VectorSubtract( end, start, dir );
	if ( !trm ) {
		BoundsFromPointTranslation( traceBounds, start, dir );
		radius = 0.0f;
	} else {
		BoundsFromBoundsTranslation( traceBounds, trm->bounds, start, trmAxis, dir );
		radius = BoundsGetRadius( trm->bounds );
	}

	num = ClipGetTraceClipModels( self, traceBounds, contentMask, passEntity, clipModelList );

	for ( i = 0; i < num; i++ ) {
		touch = clipModelList[i];

		if ( !touch ) {
			continue;
		}

		if ( touch->renderModelHandle != -1 ) {
			self->numRenderModelTraces++;
			ClipTraceRenderModel( self, &trace, start, end, radius, trmAxis, touch );
		} else {
			self->numTranslations++;
			trap_CM_Translation( &trace, start, end, trm, trmAxis, contentMask,
									ClipModelHandle( touch ), touch->origin, touch->axis );
		}

		if ( trace.fraction < results->fraction ) {
			*results = trace;
			results->c.entityNum = touch->entity->s.number;
			results->c.id = touch->id;
			if ( results->fraction == 0.0f ) {
				break;
			}
		}
	}
}

/*
============
ClipTranslation
============
*/
qboolean ClipTranslation( clip_t *self, cm_trace_t *results, const vec3_t start, const vec3_t end,
						const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask, const sharedEntity_t *passEntity ) {
	int i, num;
	clipModel_t *touch, *clipModelList[MAX_GENTITIES];
	vec3_t traceBounds[2];
	float radius;
	cm_trace_t trace;
	const traceModel_t *trm;
    vec3_t dir;

	if ( ClipTestHugeTranslation( results, mdl, start, end, trmAxis ) ) {
		return qtrue;
	}

	trm = ClipTraceModelForClipModel( self, mdl );

	if ( !passEntity || passEntity->s.number != ENTITYNUM_WORLD ) {
		// test world
		self->numTranslations++;
		trap_CM_Translation( results, start, end, trm, trmAxis, contentMask, 0, vec3_origin, axisDefault );
		results->c.entityNum = results->fraction != 1.0f ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
		if ( results->fraction == 0.0f ) {
			return qtrue;		// blocked immediately by the world
		}
	} else {
		memset( results, 0, sizeof( *results ) );
		results->fraction = 1.0f;
		VectorCopy( end, results->endpos );
		AxisCopy( trmAxis, results->endAxis );
	}

    VectorSubtract( results->endpos, start, dir );

	if ( !trm ) {
        BoundsFromPointTranslation( traceBounds, start, dir );
		radius = 0.0f;
	} else {
		BoundsFromBoundsTranslation( traceBounds, trm->bounds, start, trmAxis, dir );
		radius = BoundsGetRadius( trm->bounds );
	}

	num = ClipGetTraceClipModels( self, traceBounds, contentMask, passEntity, clipModelList );

	for ( i = 0; i < num; i++ ) {
		touch = clipModelList[i];

		if ( !touch ) {
			continue;
		}

		if ( touch->renderModelHandle != -1 ) {
			self->numRenderModelTraces++;
			ClipTraceRenderModel( self, &trace, start, end, radius, trmAxis, touch );
		} else {
			self->numTranslations++;
			trap_CM_Translation( &trace, start, end, trm, trmAxis, contentMask,
									ClipModelHandle( touch ), touch->origin, touch->axis );
		}

		if ( trace.fraction < results->fraction ) {
			*results = trace;
			results->c.entityNum = touch->entity->s.number;
			results->c.id = touch->id;
			if ( results->fraction == 0.0f ) {
				break;
			}
		}
	}

	return ( results->fraction < 1.0f );
}

/*
============
ClipRotation
============
*/
qboolean ClipRotation( clip_t *self, cm_trace_t *results, const vec3_t start, const rotation_t *rotation,
					const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask, const sharedEntity_t *passEntity ) {
	int i, num;
	clipModel_t *touch, *clipModelList[MAX_GENTITIES];
	vec3_t traceBounds[2];
	cm_trace_t trace;
	const traceModel_t *trm;

	trm = ClipTraceModelForClipModel( self, mdl );

	if ( !passEntity || passEntity->s.number != ENTITYNUM_WORLD ) {
		// test world
		self->numRotations++;
		trap_CM_Rotation( results, start, rotation, trm, trmAxis, contentMask, 0, vec3_origin, axisDefault );
		results->c.entityNum = results->fraction != 1.0f ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
		if ( results->fraction == 0.0f ) {
			return qtrue;		// blocked immediately by the world
		}
	} else {
		memset( results, 0, sizeof( *results ) );
		results->fraction = 1.0f;
        VectorCopy( start, results->endpos );
        RotationToMat3( rotation );
        MatrixMultiply( trmAxis, rotation->axis, results->endAxis );
	}

	if ( !trm ) {
		BoundsFromPointRotation( traceBounds, start, rotation );
	} else {
		BoundsFromBoundsRotation( traceBounds, trm->bounds, start, trmAxis, rotation );
	}

	num = ClipGetTraceClipModels( self, traceBounds, contentMask, passEntity, clipModelList );

	for ( i = 0; i < num; i++ ) {
		touch = clipModelList[i];

		if ( !touch ) {
			continue;
		}

		// no rotational collision with render models
		if ( touch->renderModelHandle != -1 ) {
			continue;
		}

		self->numRotations++;
		trap_CM_Rotation( &trace, start, rotation, trm, trmAxis, contentMask,
							ClipModelHandle( touch ), touch->origin, touch->axis );

		if ( trace.fraction < results->fraction ) {
			*results = trace;
			results->c.entityNum = touch->entity->s.number;
			results->c.id = touch->id;
			if ( results->fraction == 0.0f ) {
				break;
			}
		}
	}

	return ( results->fraction < 1.0f );
}

/*
============
ClipMotion
============
*/
qboolean ClipMotion( clip_t *self, cm_trace_t *results, const vec3_t start, const vec3_t end, const rotation_t *rotation,
					const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask, const sharedEntity_t *passEntity ) {
	int i, num;
	clipModel_t *touch, *clipModelList[MAX_GENTITIES];
	vec3_t dir, endPosition;
	vec3_t traceBounds[2];
	float radius;
	cm_trace_t translationalTrace, rotationalTrace, trace;
	rotation_t endRotation;
	const traceModel_t *trm;

	assert( VectorCompare( rotation->origin, start ) );

	if ( ClipTestHugeTranslation( results, mdl, start, end, trmAxis ) ) {
		return qtrue;
	}

	if ( mdl != NULL && rotation->angle != 0.0f && !VectorCompare( rotation->vec, vec3_origin ) ) {
		// if no translation
		if ( VectorCompare( start, end ) ) {
			// pure rotation
			return ClipRotation( self, results, start, rotation, mdl, trmAxis, contentMask, passEntity );
		}
	} else if ( !VectorCompare( start, end ) ) {
		// pure translation
		return ClipTranslation( self, results, start, end, mdl, trmAxis, contentMask, passEntity );
	} else {
		// no motion
		results->fraction = 1.0f;
		VectorCopy( start, results->endpos );
		AxisCopy( trmAxis, results->endAxis );
		return qfalse;
	}

	trm = ClipTraceModelForClipModel( self, mdl );

	radius = BoundsGetRadius( trm->bounds );

	if ( !passEntity || passEntity->s.number != ENTITYNUM_WORLD ) {
		// translational collision with world
		self->numTranslations++;
		trap_CM_Translation( &translationalTrace, start, end, trm, trmAxis, contentMask, 0, vec3_origin, axisDefault );
		translationalTrace.c.entityNum = translationalTrace.fraction != 1.0f ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
	} else {
		memset( &translationalTrace, 0, sizeof( translationalTrace ) );
		translationalTrace.fraction = 1.0f;
		VectorCopy( end, translationalTrace.endpos );
		AxisCopy( trmAxis, translationalTrace.endAxis );
	}

	if ( translationalTrace.fraction != 0.0f ) {

		BoundsFromBoundsRotation( traceBounds, trm->bounds, start, trmAxis, rotation );
		VectorSubtract( translationalTrace.endpos, start, dir );
		for ( i = 0; i < 3; i++ ) {
			if ( dir[i] < 0.0f ) {
				traceBounds[0][i] += dir[i];
			}
			else {
				traceBounds[1][i] += dir[i];
			}
		}

		num = ClipGetTraceClipModels( self, traceBounds, contentMask, passEntity, clipModelList );

		for ( i = 0; i < num; i++ ) {
			touch = clipModelList[i];

			if ( !touch ) {
				continue;
			}

			if ( touch->renderModelHandle != -1 ) {
				self->numRenderModelTraces++;
				ClipTraceRenderModel( self, &trace, start, end, radius, trmAxis, touch );
			} else {
				self->numTranslations++;
				trap_CM_Translation( &trace, start, end, trm, trmAxis, contentMask,
										ClipModelHandle( touch ), touch->origin, touch->axis );
			}

			if ( trace.fraction < translationalTrace.fraction ) {
				translationalTrace = trace;
				translationalTrace.c.entityNum = touch->entity->s.number;
				translationalTrace.c.id = touch->id;
				if ( translationalTrace.fraction == 0.0f ) {
					break;
				}
			}
		}
	} else {
		num = -1;
	}

	VectorCopy( translationalTrace.endpos, endPosition );
	endRotation = *rotation;
	VectorCopy( endPosition, endRotation.origin );

	if ( !passEntity || passEntity->s.number != ENTITYNUM_WORLD ) {
		// rotational collision with world
		self->numRotations++;
		trap_CM_Rotation( &rotationalTrace, endPosition, &endRotation, trm, trmAxis, contentMask, 0, vec3_origin, axisDefault );
		rotationalTrace.c.entityNum = rotationalTrace.fraction != 1.0f ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
	} else {
		memset( &rotationalTrace, 0, sizeof( rotationalTrace ) );
		rotationalTrace.fraction = 1.0f;
		VectorCopy( endPosition, rotationalTrace.endpos );
        RotationToMat3( rotation );
        MatrixMultiply( trmAxis, rotation->axis, rotationalTrace.endAxis );
	}

	if ( rotationalTrace.fraction != 0.0f ) {

		if ( num == -1 ) {
			BoundsFromBoundsRotation( traceBounds, trm->bounds, endPosition, trmAxis, &endRotation );
			num = ClipGetTraceClipModels( self, traceBounds, contentMask, passEntity, clipModelList );
		}

		for ( i = 0; i < num; i++ ) {
			touch = clipModelList[i];

			if ( !touch ) {
				continue;
			}

			// no rotational collision detection with render models
			if ( touch->renderModelHandle != -1 ) {
				continue;
			}

			self->numRotations++;
			trap_CM_Rotation( &trace, endPosition, &endRotation, trm, trmAxis, contentMask,
								ClipModelHandle( touch ), touch->origin, touch->axis );

			if ( trace.fraction < rotationalTrace.fraction ) {
				rotationalTrace = trace;
				rotationalTrace.c.entityNum = touch->entity->s.number;
				rotationalTrace.c.id = touch->id;
				if ( rotationalTrace.fraction == 0.0f ) {
					break;
				}
			}
		}
	}

	if ( rotationalTrace.fraction < 1.0f ) {
		*results = rotationalTrace;
	} else {
		*results = translationalTrace;
		AxisCopy( rotationalTrace.endAxis, results->endAxis );
	}

	results->fraction = Max( translationalTrace.fraction, rotationalTrace.fraction );

	return ( translationalTrace.fraction < 1.0f || rotationalTrace.fraction < 1.0f );
}

/*
============
ClipContacts
============
*/
int ClipContacts( clip_t *self, contactInfo_t *contacts, const int maxContacts, const vec3_t start, const vec6_t dir, const float depth,
					 const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask, const sharedEntity_t *passEntity ) {
	int i, j, num, n, numContacts;
	clipModel_t *touch, *clipModelList[MAX_GENTITIES];
	vec3_t traceBounds[2];
	const traceModel_t *trm;

	trm = ClipTraceModelForClipModel( self, mdl );

	if ( !passEntity || passEntity->s.number != ENTITYNUM_WORLD ) {
		// test world
		self->numContacts++;
		numContacts = trap_CM_Contacts( contacts, maxContacts, start, dir, depth, trm, trmAxis, contentMask, 0, vec3_origin, axisDefault );
	} else {
		numContacts = 0;
	}

	for ( i = 0; i < numContacts; i++ ) {
		contacts[i].entityNum = ENTITYNUM_WORLD;
		contacts[i].id = 0;
	}

	if ( numContacts >= maxContacts ) {
		return numContacts;
	}

	if ( !trm ) {
        VectorCopy( start, traceBounds[0] );
        VectorCopy( start, traceBounds[1] );
	} else {
		BoundsFromTransformedBounds( traceBounds, trm->bounds, start, trmAxis );
	}
    traceBounds[0][0] -= depth;
    traceBounds[0][1] -= depth;
    traceBounds[0][2] -= depth;
    traceBounds[1][0] += depth;
    traceBounds[1][1] += depth;
    traceBounds[1][2] += depth;

	num = ClipGetTraceClipModels( self, traceBounds, contentMask, passEntity, clipModelList );

	for ( i = 0; i < num; i++ ) {
		touch = clipModelList[i];

		if ( !touch ) {
			continue;
		}

		// no contacts with render models
		if ( touch->renderModelHandle != -1 ) {
			continue;
		}

		self->numContacts++;
		n = trap_CM_Contacts( contacts + numContacts, maxContacts - numContacts,
								start, dir, depth, trm, trmAxis, contentMask,
									ClipModelHandle( touch ), touch->origin, touch->axis );

		for ( j = 0; j < n; j++ ) {
			contacts[numContacts].entityNum = touch->entity->s.number;
			contacts[numContacts].id = touch->id;
			numContacts++;
		}

		if ( numContacts >= maxContacts ) {
			break;
		}
	}

	return numContacts;
}

/*
============
ClipContents
============
*/
int ClipContents( clip_t *self, const vec3_t start, const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask, const sharedEntity_t *passEntity ) {
	int i, num, contents;
	clipModel_t *touch, *clipModelList[MAX_GENTITIES];
	vec3_t traceBounds[2];
	const traceModel_t *trm;

	trm = ClipTraceModelForClipModel( self, mdl );

	if ( !passEntity || passEntity->s.number != ENTITYNUM_WORLD ) {
		// test world
		self->numContents++;
		contents = trap_CM_Contents( start, trm, trmAxis, contentMask, 0, vec3_origin, axisDefault );
	} else {
		contents = 0;
	}

	if ( !trm ) {
		VectorCopy( start, traceBounds[0] );
		VectorCopy( start, traceBounds[1] );
	} else if ( AxisIsRotated( trmAxis ) ) {
		BoundsFromTransformedBounds( traceBounds, trm->bounds, start, trmAxis );
	} else {
		VectorAdd( trm->bounds[0], start, traceBounds[0] );
		VectorAdd( trm->bounds[1], start, traceBounds[1] );
	}

	num = ClipGetTraceClipModels( self, traceBounds, -1, passEntity, clipModelList );

	for ( i = 0; i < num; i++ ) {
		touch = clipModelList[i];

		if ( !touch ) {
			continue;
		}

		// no contents test with render models
		if ( touch->renderModelHandle != -1 ) {
			continue;
		}

		// if the entity does not have any contents we are looking for
		if ( ( touch->contents & contentMask ) == 0 ) {
			continue;
		}

		// if the entity has no new contents flags
		if ( ( touch->contents & contents ) == touch->contents ) {
			continue;
		}

		self->numContents++;
		if ( trap_CM_Contents( start, trm, trmAxis, contentMask, ClipModelHandle( touch ), touch->origin, touch->axis ) ) {
			contents |= ( touch->contents & contentMask );
		}
	}

	return contents;
}

/*
============
ClipTranslationModel
============
*/
void ClipTranslationModel( clip_t *self, cm_trace_t *results, const vec3_t start, const vec3_t end,
					const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask,
					cmHandle_t model, const vec3_t modelOrigin, const vec3_t modelAxis[3] ) {
	const traceModel_t *trm = ClipTraceModelForClipModel( self, mdl );
	self->numTranslations++;
	trap_CM_Translation( results, start, end, trm, trmAxis, contentMask, model, modelOrigin, modelAxis );
}

/*
============
ClipRotationModel
============
*/
void ClipRotationModel( clip_t *self, cm_trace_t *results, const vec3_t start, const rotation_t *rotation,
					const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask,
					cmHandle_t model, const vec3_t modelOrigin, const vec3_t modelAxis[3] ) {
	const traceModel_t *trm = ClipTraceModelForClipModel( self, mdl );
	self->numRotations++;
	trap_CM_Rotation( results, start, rotation, trm, trmAxis, contentMask, model, modelOrigin, modelAxis );
}

/*
============
ClipContactsModel
============
*/
int ClipContactsModel( clip_t *self, contactInfo_t *contacts, const int maxContacts, const vec3_t start, const vec6_t dir, const float depth,
					const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask,
					cmHandle_t model, const vec3_t modelOrigin, const vec3_t modelAxis[3] ) {
	const traceModel_t *trm = ClipTraceModelForClipModel( self, mdl );
	self->numContacts++;
	return trap_CM_Contacts( contacts, maxContacts, start, dir, depth, trm, trmAxis, contentMask, model, modelOrigin, modelAxis );
}

/*
============
ClipContentsModel
============
*/
int ClipContentsModel( clip_t *self, const vec3_t start,
					const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask,
					cmHandle_t model, const vec3_t modelOrigin, const vec3_t modelAxis[3] ) {
	const traceModel_t *trm = ClipTraceModelForClipModel( self, mdl );
	self->numContents++;
	return trap_CM_Contents( start, trm, trmAxis, contentMask, model, modelOrigin, modelAxis );
}

/*
============
ClipGetModelContactFeature
============
*/
qboolean ClipGetModelContactFeature( const clip_t *self, const contactInfo_t *contact, const clipModel_t *clipModel, fixedWinding_t *winding ) {
	int i;
	cmHandle_t handle;
	vec3_t start, end;
    vec5_t p;

    p[3] = p[4] = 0.0f;

	handle = -1;
	ClearFixedWinding( winding );

	if ( clipModel == NULL ) {
		handle = 0;
	} else {
		if ( clipModel->renderModelHandle != -1 ) {
            VectorCopy( contact->point, p );
            AddPointToFixedWinding( winding, p );
			return qtrue;
		} else if ( clipModel->traceModelIndex != -1 ) {
			handle = trap_CM_SetupTrmModel( ClipModelGetCachedTraceModel( clipModel->traceModelIndex ), clipModel->material );
		} else {
			handle = clipModel->collisionModelHandle;
		}
	}

	// if contact with a collision model
	if ( handle != -1 ) {
		switch( contact->type ) {
			case CONTACT_EDGE: {
				// the model contact feature is a collision model edge
				trap_CM_GetModelEdge( handle, contact->modelFeature, start, end );
                VectorCopy( start, p );
				AddPointToFixedWinding( winding, p );
                VectorCopy( end, p );
                AddPointToFixedWinding( winding, p );
				break;
			}
			case CONTACT_MODELVERTEX: {
				// the model contact feature is a collision model vertex
				trap_CM_GetModelVertex( handle, contact->modelFeature, start );
                VectorCopy( start, p );
                AddPointToFixedWinding( winding, p );
				break;
			}
			case CONTACT_TRMVERTEX: {
				// the model contact feature is a collision model polygon
				trap_CM_GetModelPolygon( handle, contact->modelFeature, winding );
				break;
			}
		}
	}

	// transform the winding to world space
	if ( clipModel ) {
		for ( i = 0; i < winding->numPoints; i++ ) {
            VectorRotateSelf( winding->points[i], clipModel->axis );
            VectorAdd( winding->points[i], clipModel->origin, winding->points[i] );
		}
	}

	return qtrue;
}

/*
============
ClipPrintSectorList
============
*/
void ClipPrintSectorList( clip_t *self ) {
	int				i, c;
	clipSector_t	*sec;
	clipLink_t  	*entLink;

	for ( i = 0 ; i < MAX_SECTORS ; i++ ) {
		sec = &self->clipSectors[i];

		c = 0;
		for ( entLink = sec->clipLinks ; entLink ; entLink = entLink->nextInSector ) {
			c++;
		}
		Com_Printf( "sector %i: %i entities\n", i, c );
	}
}

/*
============
ClipPrintStatistics
============
*/
void ClipPrintStatistics( clip_t *self ) {
	ii.Com_Printf( "t = %-3d, r = %-3d, m = %-3d, render = %-3d, contents = %-3d, contacts = %-3d\n",
					self->numTranslations, self->numRotations, self->numMotions, self->numRenderModelTraces, self->numContents, self->numContacts );
	self->numRotations = self->numTranslations = self->numMotions = self->numRenderModelTraces = self->numContents = self->numContacts = 0;
}

/*
============
ClipDrawClipModels
============
*/
void ClipDrawClipModels( clip_t *self, const vec3_t eye, const float radius, const sharedEntity_t *passEntity ) {
	int				i, num;
	vec3_t  		bounds[2];
	clipModel_t		*clipModelList[MAX_GENTITIES];
	clipModel_t		*clipModel;

    VectorCopy( eye, bounds[0] );
    VectorCopy( eye, bounds[1] );
    bounds[0][0] -= radius;
    bounds[0][1] -= radius;
    bounds[0][2] -= radius;
    bounds[1][0] += radius;
    bounds[1][1] += radius;
    bounds[1][2] += radius;

	num = ClipModelsTouchingBounds( self, bounds, -1, clipModelList, MAX_GENTITIES );

	for ( i = 0; i < num; i++ ) {
		clipModel = clipModelList[i];
		if ( ClipModelGetEntity( clipModel ) == passEntity ) {
			continue;
		}
		if ( clipModel->renderModelHandle != -1 ) {
            // TODO: make this work
			//gameRenderWorld->DebugBounds( colorCyan, clipModel->absBounds );
		} else {
			trap_CM_DrawModel( ClipModelHandle( clipModel ), clipModel->origin, clipModel->axis, eye, radius );
		}
	}
}

/*
============
ClipDrawModelContactFeature
============
*/
qboolean ClipDrawModelContactFeature( const clip_t *self, const contactInfo_t *contact, const clipModel_t *clipModel, int lifetime ) {
	int i;
	vec3_t axis[3];
	fixedWinding_t winding;

    ClearFixedWinding( &winding );
	if ( !ClipGetModelContactFeature( self, contact, clipModel, &winding ) ) {
		return qfalse;
	}

    VectorToAxis( contact->normal, axis );

	if ( winding.numPoints == 1 ) {
        // TODO: make this work
		//gameRenderWorld->DebugLine( colorCyan, winding[0].ToVec3(), winding[0].ToVec3() + 2.0f * axis[0], lifetime );
		//gameRenderWorld->DebugLine( colorWhite, winding[0].ToVec3() - 1.0f * axis[1], winding[0].ToVec3() + 1.0f * axis[1], lifetime );
		//gameRenderWorld->DebugLine( colorWhite, winding[0].ToVec3() - 1.0f * axis[2], winding[0].ToVec3() + 1.0f * axis[2], lifetime );
	} else {
		for ( i = 0; i < winding.numPoints; i++ ) {
            // TODO: make this work
			//gameRenderWorld->DebugLine( colorCyan, winding[i].ToVec3(), winding[(i+1)%winding.GetNumPoints()].ToVec3(), lifetime );
		}
	}

	VectorNegate( axis[0], axis[0] );
	VectorNegate( axis[2], axis[2] );
    // TODO: make this work
	//gameRenderWorld->DrawText( contact.material->GetName(), winding.GetCenter() - 4.0f * axis[2], 0.1f, colorWhite, axis, 1, 5000 );

	return qtrue;
}
