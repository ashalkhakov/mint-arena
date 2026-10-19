/*
===========================================================================
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.

This file is part of Spearmint Source Code.

Spearmint Source Code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 3 of the License,
or (at your option) any later version.

Spearmint Source Code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Spearmint Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, Spearmint Source Code is also subject to certain additional terms.
You should have received a copy of these additional terms immediately following
the terms and conditions of the GNU General Public License.  If not, please
request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional
terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc.,
Suite 120, Rockville, Maryland 20850 USA.
===========================================================================
*/
// g_world.c -- world query functions

#include "g_local.h"

clip_t      g_clip;

/*
=================
G_GetBrushBounds

gets mins and maxs for inline bmodels
=================
*/
void G_GetBrushBounds( int modelindex, vec3_t mins, vec3_t maxs ) {
	clipHandle_t	h;
	char			modelName[MAX_QPATH];
	vec3_t			bounds[2];

	if (!mins || !maxs) {
		Com_Error( ERR_DROP, "G_GetBrushBounds: NULL" );
	}

	snprintf( modelName, sizeof(modelName), "*%d", modelindex );

	h = trap_CM_LoadModel( modelName, qfalse );
	if ( !trap_CM_GetModelBounds( h, bounds ) ) {
		Com_Error( ERR_DROP, "G_GetBrushBounds: unable to get bounds for modelindex %d", modelindex );
	}
	VectorCopy( bounds[0], mins );
	VectorCopy( bounds[1], maxs );
}

/*
================
G_ClipHandleForEntity

Returns a headnode that can be used for testing or clipping to a
given entity.  If the entity is a bsp model, the headnode will
be returned, otherwise a custom box tree or capsule will be constructed.
================
*/
clipHandle_t G_ClipHandleForEntity( const sharedEntity_t *ent ) {
	char			modelName[MAX_QPATH];
	traceModel_t	trm;
	vec3_t			bounds[2];

	if ( ent->s.collisionType == CT_SUBMODEL ) {
		// explicit hulls in the BSP model
		snprintf( modelName, sizeof(modelName), "*%d", ent->s.modelindex );
		return trap_CM_LoadModel( modelName, qfalse );
	}

	// create a temp tree or capsule from bounding box sizes
    trm.type = TRM_INVALID;
	VectorCopy( ent->s.mins, bounds[0] );
	VectorCopy( ent->s.maxs, bounds[1] );
	switch ( ent->s.collisionType ) {
		case CT_AABB:
			TraceModelSetupBox( &trm, bounds );
			break;
		case CT_CAPSULE:
			TraceModelSetupDodecahedron( &trm, bounds );
			break;
	}

	// FIXME: need a dummy material with the given contents
	return trap_CM_SetupTrmModel( &trm, 0 );
}



/*
===============================================================================

ENTITY CHECKING

To avoid linearly searching through lists of entities during environment testing,
the world is carved up with an evenly spaced, axially aligned bsp tree.  Entities
are kept in chains either at the final leafs, or at the first node that splits
them, which prevents having to deal with multiple fragments of a single entity.

===============================================================================
*/

/*
===============
G_SectorList_f
===============
*/
void G_SectorList_f( void ) {
    ClipPrintSectorList( &g_clip );
}

/*
===============
G_UnlinkEntity

===============
*/
void G_UnlinkEntity( gentity_t *ent ) {
    ClipModelUnlink( &ent->clipModel );
    ClipModelSetOwner( &ent->clipModel, NULL );

    trap_UnlinkEntity( ent );
}

/*
===============
G_LinkEntity

===============
*/
void G_LinkEntity( gentity_t *ent ) {
    int         i;
	float		*origin, *angles;
    vec3_t      axis[3];

	// physics props link their rotated trace model at the simulated position
	if ( ent->body ) {
		G_PhysicsLinkProp( ent );
		return;
	}

	// get the position
	origin = ent->r.currentOrigin;
	angles = ent->r.currentAngles;

	// set the abs box
	if ( ent->s.collisionType == CT_SUBMODEL && (angles[0] || angles[1] || angles[2]) ) {
		// expand for rotation
		float		max;

		max = RadiusFromBounds( ent->s.mins, ent->s.maxs );
		for (i=0 ; i<3 ; i++) {
			ent->r.absmin[i] = origin[i] - max;
			ent->r.absmax[i] = origin[i] + max;
		}
	} else {
		// normal
		VectorAdd (origin, ent->s.mins, ent->r.absmin);
		VectorAdd (origin, ent->s.maxs, ent->r.absmax);
	}

	// drop the links at the old position, ClipModelInit forgets them without unlinking
	ClipModelUnlink( &ent->clipModel );

    int newId = ent->s.number;
	if ( ent->s.collisionType == CT_SUBMODEL ) {
        char name[MAX_QPATH];

        AnglesToAxis( angles, axis );
        sprintf( name, "*%d", ent->s.modelindex );
        ClipModelInitFromModel( &ent->clipModel, name );
    } else {
        traceModel_t trm;
        vec3_t bounds[2];

        // boxes don't rotate
        AxisClear( axis );

        VectorCopy( ent->s.mins, bounds[0] );
        VectorCopy( ent->s.maxs, bounds[1] );
        trm.type = TRM_INVALID;
        TraceModelSetupBox( &trm, bounds );
        ClipModelInitFromTraceModel( &ent->clipModel, &trm );
    }
	// entities are filtered by s.contents like Quake 3, not by the model's contents
	ent->clipModel.contents = ent->s.contents;
    ClipModelLink2( &ent->clipModel, &g_clip, ( sharedEntity_t * )ent, newId, origin, axis, -1 );
    if ( ent->r.ownerNum >= 0 && ent->r.ownerNum < ENTITYNUM_NONE ) {
        sharedEntity_t *owner = ( sharedEntity_t * )&g_entities[ ent->r.ownerNum ];
        ClipModelSetOwner( &ent->clipModel, owner );
    } else {
        ClipModelSetOwner( &ent->clipModel, NULL );
    }

	// because movement is clipped an epsilon away from an actual edge,
	// we must fully check even when bounding boxes don't quite touch
	ent->r.absmin[0] -= 1;
	ent->r.absmin[1] -= 1;
	ent->r.absmin[2] -= 1;
	ent->r.absmax[0] += 1;
	ent->r.absmax[1] += 1;
	ent->r.absmax[2] += 1;

    trap_LinkEntity( ent );
}

/*
============================================================================

AREA QUERY

Fills in a list of all entities who's absmin / absmax intersects the given
bounds.  This does NOT mean that they actually touch in the case of bmodels.
============================================================================
*/

/*
================
G_EntitiesInBox
================
*/
int G_EntitiesInBox( const vec3_t mins, const vec3_t maxs, int *entityList, int maxcount ) {
	static sharedEntity_t *list[MAX_GENTITIES];
	vec3_t bounds[2];
	int i, num;

	VectorCopy( mins, bounds[0] );
	VectorCopy( maxs, bounds[1] );

	if ( maxcount > MAX_GENTITIES ) {
		maxcount = MAX_GENTITIES;
	}

	// Quake 3 returns every linked entity, even ones without contents
	num = ClipEntitiesTouchingBounds( &g_clip, bounds, CLIP_ANY_CONTENTS, list, maxcount );
	for ( i = 0; i < num; i++ ) {
		entityList[i] = list[i]->s.number;
	}

	return num;
}

//===========================================================================

/*
================
G_PassEntity

Quake 3 uses ENTITYNUM_NONE to pass nothing. The world isn't in the clip
world, so passing it doesn't skip world collision like it does in Doom 3.
================
*/
static const sharedEntity_t *G_PassEntity( int passEntityNum ) {
	if ( passEntityNum < 0 || passEntityNum >= ENTITYNUM_MAX_NORMAL ) {
		return NULL;
	}
	return ( const sharedEntity_t * )&g_entities[ passEntityNum ];
}

/*
================
G_TraceClipModel

Returns a clip model for tracing the given bounds, NULL for a point.
================
*/
static const clipModel_t *G_TraceClipModel( const vec3_t mins, const vec3_t maxs ) {
	traceModel_t trm;
	vec3_t bounds[2];

	if ( VectorCompare( mins, vec3_origin ) && VectorCompare( maxs, vec3_origin ) ) {
		return NULL;
	}

	VectorCopy( mins, bounds[0] );
	VectorCopy( maxs, bounds[1] );
	trm.type = TRM_INVALID;
	TraceModelSetupBox( &trm, bounds );
	ClipModelLoadTraceModel( &g_clip.temporaryClipModel, &trm );

	return &g_clip.temporaryClipModel;
}

/*
================
G_FinishTrace

The collision model doesn't detect starting in solid. A zero length trace is
a position test though, it returns fraction 0 when stuck.
================
*/
static void G_FinishTrace( trace_t *results, const cm_trace_t *trace, const vec3_t start, const vec3_t end,
							qboolean startSolid, qboolean allSolid ) {
	if ( VectorCompare( start, end ) ) {
		startSolid = allSolid = ( trace->fraction == 0.0f );
	}

	CM_TraceToQuakeTrace( results, trace, start, trap_CM_GetMaterialSurfaceFlags( trace->c.material ), startSolid, allSolid );
	if ( results->fraction < 1.0f && trace->c.entityNum != ENTITYNUM_NONE ) {
		results->entityNum = trace->c.entityNum;
	}
}

/*
==================
G_Trace

Moves the given mins/maxs volume through the world from start to end.
passEntityNum and entities owned by passEntityNum are explicitly not checked.
==================
*/
void G_Trace( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask, traceType_t type ) {
	cm_trace_t				trace;
	const sharedEntity_t	*passEntity;
	const clipModel_t		*mdl;
	qboolean				startSolid, allSolid;

	if ( !mins ) {
		mins = vec3_origin;
	}
	if ( !maxs ) {
		maxs = vec3_origin;
	}

	passEntity = G_PassEntity( passEntityNum );
	mdl = G_TraceClipModel( mins, maxs );

	ClipTranslation( &g_clip, &trace, start, end, mdl, axisDefault, contentmask, passEntity );

	startSolid = allSolid = qfalse;
	if ( !VectorCompare( start, end ) ) {
		startSolid = ( ClipContents( &g_clip, start, mdl, axisDefault, contentmask, passEntity ) != 0 );
		if ( startSolid ) {
			allSolid = ( ClipContents( &g_clip, trace.endpos, mdl, axisDefault, contentmask, passEntity ) != 0 );
		}
	}

	G_FinishTrace( results, &trace, start, end, startSolid, allSolid );
}

/*
=============
G_TraceBox
=============
*/
void G_TraceBox( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask ) {
    G_Trace( results, start, mins, maxs, end, passEntityNum, contentmask, TT_AABB);
}

/*
=============
G_TraceCapsule
=============
*/
void G_TraceCapsule( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask ) {
    G_Trace( results, start, mins, maxs, end, passEntityNum, contentmask, TT_CAPSULE);
}

/*
=============
G_ClipToEntities

G_Trace that doesn't clip to world. Starting in solid is only reported for
zero length traces.
=============
*/
void G_ClipToEntities( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask, traceType_t type ) {
	cm_trace_t		trace;
	const clipModel_t *mdl;

	if ( !mins ) {
		mins = vec3_origin;
	}
	if ( !maxs ) {
		maxs = vec3_origin;
	}

	mdl = G_TraceClipModel( mins, maxs );

	ClipTranslationEntities( &g_clip, &trace, start, end, mdl, axisDefault, contentmask, G_PassEntity( passEntityNum ) );
	if ( trace.fraction == 1.0f ) {
		trace.c.entityNum = ENTITYNUM_NONE;
	}

	G_FinishTrace( results, &trace, start, end, qfalse, qfalse );
}

/*
=============
G_ClipToEntity

Trace against a single entity. Starting in solid is only reported for zero
length traces.
=============
*/
void G_ClipToEntity( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int entityNum, int contentmask, traceType_t type ) {
	cm_trace_t		trace;
	const clipModel_t *mdl;
	gentity_t		*touch;

	if ( !mins ) {
		mins = vec3_origin;
	}
	if ( !maxs ) {
		maxs = vec3_origin;
	}

	memset( &trace, 0, sizeof( trace ) );
	trace.fraction = 1.0f;
	VectorCopy( end, trace.endpos );
	trace.c.entityNum = ENTITYNUM_NONE;

	touch = &g_entities[ entityNum ];
	if ( entityNum < 0 || entityNum >= ENTITYNUM_MAX_NORMAL || !touch->r.linked || !( contentmask & touch->s.contents )
			|| !ClipModelIsLinked( &touch->clipModel ) ) {
		G_FinishTrace( results, &trace, start, end, qfalse, qfalse );
		return;
	}

	mdl = G_TraceClipModel( mins, maxs );

	ClipTranslationModel( &g_clip, &trace, start, end, mdl, axisDefault, contentmask,
							ClipModelHandle( &touch->clipModel ), touch->clipModel.origin, ( const vec3_t * )touch->clipModel.axis );
	if ( trace.fraction < 1.0f ) {
		trace.c.entityNum = touch->s.number;
		if ( ClipModelIsTraceModel( &touch->clipModel ) ) {
			trace.c.contents = touch->s.contents;
		}
	} else {
		trace.c.entityNum = ENTITYNUM_NONE;
	}

	G_FinishTrace( results, &trace, start, end, qfalse, qfalse );
}

/*
=============
G_PointContents
=============
*/
int G_PointContents( const vec3_t p, int passEntityNum ) {
	return ClipContents( &g_clip, p, NULL, axisDefault, -1, G_PassEntity( passEntityNum ) );
}

/*
==================
G_EntityContact

Tests if the absolute box mins/maxs touches the entity.
==================
*/
qboolean	G_EntityContact( const vec3_t mins, const vec3_t maxs, const sharedEntity_t *gEnt, traceType_t type ) {
	traceModel_t	trm;
	vec3_t			bounds[2], axis[3];
	clipHandle_t	ch;
	const float		*angles;

	VectorCopy( mins, bounds[0] );
	VectorCopy( maxs, bounds[1] );
	trm.type = TRM_INVALID;
	TraceModelSetupBox( &trm, bounds );

	angles = gEnt->r.currentAngles;
	if ( VectorCompare( angles, vec3_origin ) ) {
		AxisCopy( axisDefault, axis );
	} else {
		AnglesToAxis( angles, axis );
	}

	// a zero length trace would be a position test too, but Contents says so directly
	ch = G_ClipHandleForEntity( gEnt );
	return ( trap_CM_Contents( vec3_origin, &trm, axisDefault, -1, ch, gEnt->r.currentOrigin, ( const vec3_t * )axis ) != 0 );
}
