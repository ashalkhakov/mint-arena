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

#ifndef __CLIP_H__
#define __CLIP_H__

#include "../idlib/q_extramath.h"

/*
===============================================================================

  Handles collision detection with the world and between physics objects.

===============================================================================
*/

#define CLIPMODEL_ID_TO_JOINT_HANDLE( id )	( ( id ) >= 0 ? INVALID_JOINT : ((jointHandle_t) ( -1 - id )) )
#define JOINT_HANDLE_TO_CLIPMODEL_ID( id )	( -1 - id )

typedef struct clip_s clip_t;
typedef struct clipModel_s clipModel_t;


//===============================================================
//
//	clipModel_t
//
//===============================================================

typedef struct clipModel_s {
	qboolean				enabled;				// true if this clip model is used for clipping
	sharedEntity_t *		entity;					// entity using this clip model
	int						id;						// id for entities that use multiple clip models
	sharedEntity_t *		owner;					// owner of the entity that owns this clip model
	vec3_t					origin;					// origin of clip model
	vec3_t  				axis[3];				// orientation of clip model
	vec3_t  				bounds[2];				// bounds
	vec3_t  				absBounds[2];			// absolute bounds
	qhandle_t       		material;				// material for trace models
	int						contents;				// all contents ored together
	cmHandle_t				collisionModelHandle;	// handle to collision model
	int						traceModelIndex;		// trace model used for collision detection
	int						renderModelHandle;		// render model def handle

	struct clipLink_s *		clipLinks;				// links into sectors
	int						touchCount;
} clipModel_t;

void ClipModelInit( clipModel_t *self );
void ClipModelInitFromModel( clipModel_t *self, const char *name );
void ClipModelInitFromTraceModel( clipModel_t *self, const traceModel_t *trm );
void ClipModelInitFromRenderHandle( clipModel_t *self, const int renderModelHandle );
void ClipModelCopy( const clipModel_t *self, clipModel_t *out );
void ClipModelFree( clipModel_t *self );

qboolean ClipModelLoadModel( clipModel_t *self, const char *name );
void ClipModelLoadTraceModel( clipModel_t *self, const traceModel_t *trm );
void ClipModelLoadRenderModel( clipModel_t *self, const int renderModelHandle );

void ClipModelLink( clipModel_t *self, clip_t *clp );				// must have been linked with an entity and id before
void ClipModelLink2( clipModel_t *self, clip_t *clp, sharedEntity_t *ent, int newId, const vec3_t newOrigin, const vec3_t newAxis[3], int renderModelHandle );
void ClipModelUnlink( clipModel_t *self );						// unlink from sectors

// unlinks the clip model
void ClipModelSetPosition( clipModel_t *self, const vec3_t newOrigin, const vec3_t newAxis[3] );

// returns handle used to collide vs this model
cmHandle_t ClipModelHandle( const clipModel_t *self );

void ClipModelGetMassProperties( const clipModel_t *self, const float density, float *mass, vec3_t centerOfMass, vec3_t inertiaTensor[3] );

traceModel_t *ClipModelGetCachedTraceModel( int traceModelIndex );
int ClipModelGetTraceModelHashKey( const traceModel_t *trm );

// unlinks the clip model
static ID_INLINE void ClipModelTranslate( clipModel_t *self, const vec3_t translation ) {
	ClipModelUnlink( self );
	VectorAdd( self->origin, translation, self->origin );
}

// unlinks the clip model
static ID_INLINE void ClipModelRotate( clipModel_t *self, rotation_t *rotation ) {
    vec3_t axis[3];

	ClipModelUnlink( self );
	RotationRotatePoint( rotation, self->origin );
    RotationToMat3( rotation );
    MatrixMultiply( self->axis, rotation->axis, axis );
    AxisCopy( axis, self->axis );
}

// enable for clipping
static ID_INLINE void ClipModelEnable( clipModel_t *self ) {
	self->enabled = qtrue;
}

// keep linked but disable for clipping
static ID_INLINE void ClipModelDisable( clipModel_t *self ) {
	self->enabled = qfalse;
}

static ID_INLINE void ClipModelSetMaterial( clipModel_t *self, qhandle_t m ) {
	self->material = m;
}

static ID_INLINE qhandle_t ClipModelGetMaterial( const clipModel_t *self ) {
	return self->material;
}

// override contents
static ID_INLINE void ClipModelSetContents( clipModel_t *self, int newContents ) {
	self->contents = newContents;
}

static ID_INLINE int ClipModelGetContents( const clipModel_t *self ) {
	return self->contents;
}

static ID_INLINE void ClipModelSetEntity( clipModel_t *self, sharedEntity_t *newEntity ) {
	self->entity = newEntity;
}

static ID_INLINE sharedEntity_t *ClipModelGetEntity( const clipModel_t *self ) {
	return self->entity;
}

static ID_INLINE void ClipModelSetId( clipModel_t *self, int newId ) {
	self->id = newId;
}

static ID_INLINE int ClipModelGetId( const clipModel_t *self ) {
	return self->id;
}

static ID_INLINE void ClipModelSetOwner( clipModel_t *self, sharedEntity_t *newOwner ) {
	self->owner = newOwner;
}

static ID_INLINE sharedEntity_t *ClipModelGetOwner( const clipModel_t *self ) {
	return self->owner;
}

static ID_INLINE void ClipModelGetBounds( const clipModel_t *self, vec3_t bounds[2] ) {
	VectorCopy( self->bounds[0], bounds[0] );
    VectorCopy( self->bounds[1], bounds[1] );
}

static ID_INLINE void ClipModelGetAbsBounds( const clipModel_t *self, vec3_t absBounds[2] ) {
    VectorCopy( self->absBounds[0], absBounds[0] );
    VectorCopy( self->absBounds[1], absBounds[1] );
}

static ID_INLINE void ClipModelGetOrigin( const clipModel_t *self, vec3_t origin ) {
	VectorCopy( self->origin, origin );
}

static ID_INLINE void ClipModelGetAxis( const clipModel_t *self, vec3_t axis[3] ) {
	AxisCopy( self->axis, axis );
}

// returns true if this is a render model
static ID_INLINE qboolean ClipModelIsRenderModel( const clipModel_t *self ) {
	if ( self->renderModelHandle != -1 ) {
        return qtrue;
    }
    return qfalse;
}

// returns true if this is a trace model
static ID_INLINE qboolean ClipModelIsTraceModel( const clipModel_t *self ) {
	if ( self->traceModelIndex != -1 ) {
        return qtrue;
    }
    return qfalse;
}

// returns true if the clip model is linked
static ID_INLINE qboolean ClipModelIsLinked( const clipModel_t *self ) {
	if ( self->clipLinks != NULL ) {
        return qtrue;
    }
    return qfalse;
}

// returns true if enabled for collision detection
static ID_INLINE qboolean ClipModelIsEnabled( const clipModel_t *self ) {
	return self->enabled;
}

static ID_INLINE qboolean ClipModelIsEqual( const clipModel_t *self, const traceModel_t *trm ) {
	if ( self->traceModelIndex != -1 ) {
        traceModel_t *mod = ClipModelGetCachedTraceModel( self->traceModelIndex );
        if ( TraceModelCompare( mod, trm ) ) {
            return qtrue;
        }
    }
    return qfalse;
}

static ID_INLINE const traceModel_t *ClipModelGetTraceModel( const clipModel_t *self ) {
	if ( !ClipModelIsTraceModel( self ) ) {
		return NULL;
	}
	return ClipModelGetCachedTraceModel( self->traceModelIndex );
}

//===============================================================
//
//	clip_t
//
//===============================================================

typedef struct clip_s {
	int						numClipSectors;
	struct clipSector_s *	clipSectors;
	vec3_t  				worldBounds[2];
	clipModel_t				temporaryClipModel;
	clipModel_t				defaultClipModel;
	int     				touchCount;
							// statistics
	int						numTranslations;
	int						numRotations;
	int						numMotions;
	int						numRenderModelTraces;
	int						numContents;
	int						numContacts;
} clip_t;

void ClipInit( clip_t *self );
void ClipFree( clip_t *self );

// clip versus the rest of the world
qboolean ClipTranslation( clip_t *self, cm_trace_t *results, const vec3_t start, const vec3_t end,
        const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask, const sharedEntity_t *passEntity );
qboolean ClipRotation( clip_t *self, cm_trace_t *results, const vec3_t start, const rotation_t *rotation,
        const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask, const sharedEntity_t *passEntity );
qboolean ClipMotion( clip_t *self, cm_trace_t *results, const vec3_t start, const vec3_t end, const rotation_t *rotation,
        const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask, const sharedEntity_t *passEntity );
int ClipContacts( clip_t *self, contactInfo_t *contacts, const int maxContacts, const vec3_t start, const vec6_t dir, const float depth,
        const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask, const sharedEntity_t *passEntity );
int ClipContents( clip_t *self, const vec3_t start,
        const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask, const sharedEntity_t *passEntity );

// clip versus a specific model
void ClipTranslationModel( clip_t *self, cm_trace_t *results, const vec3_t start, const vec3_t end,
    const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask,
    cmHandle_t model, const vec3_t modelOrigin, const vec3_t modelAxis[3] );
void ClipRotationModel( clip_t *self, cm_trace_t *results, const vec3_t start, const rotation_t *rotation,
    const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask,
    cmHandle_t model, const vec3_t modelOrigin, const vec3_t modelAxis[3] );
int ClipContactsModel( clip_t *self, contactInfo_t *contacts, const int maxContacts, const vec3_t start, const vec6_t dir, const float depth,
    const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask,
    cmHandle_t model, const vec3_t modelOrigin, const vec3_t modelAxis[3] );
int ClipContentsModel( clip_t *self, const vec3_t start,
    const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask,
    cmHandle_t model, const vec3_t modelOrigin, const vec3_t modelAxis[3] );

// clip versus all entities but not the world
void ClipTranslationEntities( clip_t *self, cm_trace_t *results, const vec3_t start, const vec3_t end,
    const clipModel_t *mdl, const vec3_t trmAxis[3], int contentMask, const sharedEntity_t *passEntity );

// get a contact feature
qboolean ClipGetModelContactFeature( const clip_t *self, const contactInfo_t *contact, const clipModel_t *clipModel, fixedWinding_t *winding );

// get entities/clip models within or touching the given bounds
int ClipEntitiesTouchingBounds( clip_t *self, const vec3_t bounds[2], int contentMask, sharedEntity_t **entityList, int maxCount );
int	ClipModelsTouchingBounds( clip_t *self, const vec3_t bounds[2], int contentMask, clipModel_t **clipModelList, int maxCount );

// stats and debug drawing
void ClipPrintSectorList( clip_t *self );
void ClipPrintStatistics( clip_t *self );
void ClipDrawClipModels( clip_t *self, const vec3_t eye, const float radius, const sharedEntity_t *passEntity );
qboolean ClipDrawModelContactFeature( const clip_t *self, const contactInfo_t *contact, const clipModel_t *clipModel, int lifetime );

// special case translations versus the rest of the world

static ID_INLINE qboolean ClipTracePoint( clip_t *clip, cm_trace_t *results, const vec3_t start, const vec3_t end, int contentMask, const sharedEntity_t *passEntity ) {
	ClipTranslation( clip, results, start, end, NULL, axisDefault, contentMask, passEntity );
	if ( results->fraction < 1.0f ) {
        return qtrue;
    }
    return qfalse;
}

static ID_INLINE qboolean ClipTraceBounds( clip_t *clip, cm_trace_t *results, const vec3_t start, const vec3_t end, const vec3_t bounds[2], int contentMask, const sharedEntity_t *passEntity ) {
    traceModel_t trm;
    
    trm.type = TRM_INVALID;
    TraceModelSetupBox( &trm, bounds );

	ClipModelInitFromTraceModel( &clip->temporaryClipModel, &trm );
	ClipTranslation( clip, results, start, end, &clip->temporaryClipModel, axisDefault, contentMask, passEntity );
	if ( results->fraction < 1.0f ) {
        return qtrue;
    }
    return qfalse;
}

static ID_INLINE void ClipGetWorldBounds( const clip_t *clip, vec3_t bounds[2] ) {
    VectorCopy( clip->worldBounds[0], bounds[0] );
    VectorCopy( clip->worldBounds[1], bounds[1] );
}

static ID_INLINE const clipModel_t *ClipDefaultClipModel( const clip_t *clip ) {
	return &(clip->defaultClipModel);
}

#endif /* !__CLIP_H__ */
