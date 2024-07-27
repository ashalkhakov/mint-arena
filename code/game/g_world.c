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

    // TODO: what should the lifetime of models be?
    ClipModelInit( &ent->clipModel );

    // TODO: what should be put here?
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
    ClipModelLink2( &ent->clipModel, &g_clip, ( sharedEntity_t * )ent, newId, origin, axis, -1 );
    if ( ent->r.ownerNum != -1 ) {
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
	vec3_t bounds[2];
    sharedEntity_t **list;
    int i, num;

    VectorCopy( mins, bounds[0] );
    VectorCopy( maxs, bounds[1] );

    // TODO: get rid of memory allocation
    list = ii.GetMemory( maxcount * sizeof( *list ) );

    num = ClipEntitiesTouchingBounds( &g_clip, bounds, -1, list, maxcount );
    for ( i = 0; i < num; i++ ) {
        entityList[i] = list[i]->s.number;
    }

    ii.FreeMemory( list );

	return num;
}

//===========================================================================

static void CmTraceToTrace( cm_trace_t *t, const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, trace_t *trace, traceModel_t *trm, cmHandle_t model, vec3_t origin, vec3_t axis[3], int brushmask ) {
    vec3_t dir;

    memset( trace, 0, sizeof( *trace ) );

	trace->fraction = t->fraction;
    VectorSubtract( end, start, dir );
    VectorMA( start, trace->fraction, dir, trace->endpos );
	//VectorCopy( t->endpos, trace->endpos ); // TODO: worldspace or not?

    if ( t->c.type != CONTACT_NONE ) {
        VectorCopy( t->c.point, trace->endpos );
        VectorCopy( t->c.normal, trace->plane.normal );
        trace->plane.dist = -t->c.dist;
		trace->plane.type = PlaneTypeForNormal( trace->plane.normal );
		SetPlaneSignbits( &trace->plane );
        trace->surfaceNum = t->c.modelFeature + 1;
        trace->surfaceFlags = trap_CM_GetMaterialSurfaceFlags( t->c.material );
        trace->contents = t->c.contents;
        trace->entityNum = t->c.entityNum;
    }
	trace->lateralFraction = 0.0f;
/*
    if ( cme.Contents( start, trm, axisDefault, -1, model, origin, axis ) & brushmask ) {
        trace->startsolid = qtrue;
    }
    if ( trace->startsolid ) {
        int c = cme.Contents( trace->endpos, trm, axisDefault, -1, model, origin, axis ) & brushmask;
        if ( c ) {
            trace->allsolid = qtrue;
            //trace->fraction = 0;
            //trace->contents = c;
        }
    }*/
}

/*
==================
G_Trace

Moves the given mins/maxs volume through the world from start to end.
passEntityNum and entities owned by passEntityNum are explicitly not checked.
==================
*/
void G_Trace( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask, traceType_t type ) {
    cm_trace_t      trace;
    sharedEntity_t  *passEntity;
    vec3_t          bounds[2];

    passEntity = ( sharedEntity_t * )&g_entities[ passEntityNum ];

	if ( !mins ) {
		mins = vec3_origin;
	}
	if ( !maxs ) {
		maxs = vec3_origin;
	}

    VectorCopy( mins, bounds[0] );
    VectorCopy( maxs, bounds[1] );

    ClipTraceBounds( &g_clip, &trace, start, end, bounds, contentmask, passEntity );
    CmTraceToTrace( &trace, start, end, mins, maxs, results, NULL, 0, vec3_origin, axisDefault, contentmask );
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

G_Trace that doesn't clip to world
=============
*/
void G_ClipToEntities( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask, traceType_t type ) {
    cm_trace_t      trace;
    clipModel_t     *model;
    sharedEntity_t  *passEntity;
    vec3_t          bounds[2];
    traceModel_t    trm;

    passEntity = ( sharedEntity_t * )&g_entities[ passEntityNum ];

	if ( !mins ) {
		mins = vec3_origin;
	}
	if ( !maxs ) {
		maxs = vec3_origin;
	}

    VectorCopy( mins, bounds[0] );
    VectorCopy( maxs, bounds[1] );
    trm.type = TRM_INVALID;
    TraceModelSetupBox( &trm, bounds );
    model = ClipDefaultClipModel( &g_clip );
    ClipModelLoadTraceModel( model, &trm );

    ClipTranslationEntities( &g_clip, &trace, start, end, model, axisDefault, contentmask, passEntity );
    CmTraceToTrace( &trace, start, end, mins, maxs, results, NULL, 0, vec3_origin, axisDefault, contentmask );
}

/*
=============
G_PointContents
=============
*/
int G_PointContents( const vec3_t p, int passEntityNum ) {
    clipModel_t *model;
    sharedEntity_t *passEntity;
    traceModel_t trm;

    passEntity = ( sharedEntity_t * )&g_entities[ passEntityNum ];

    model = ClipDefaultClipModel( &g_clip );

    trm.type = TRM_INVALID;
    TraceModelSetupBox2( &trm, 0 );
    ClipModelLoadTraceModel( model, &trm );

    return ClipContents( &g_clip, p, model, axisDefault, -1, passEntity );
}

void		CM_TransformedBoxTrace( trace_t *results, const vec3_t start, const vec3_t end,
						  const vec3_t mins, const vec3_t maxs,
						  clipHandle_t model, int brushmask,
						  const vec3_t origin, const vec3_t angles, traceType_t type ) {
	cm_trace_t tr;
	traceModel_t trm;
	vec3_t		trmBounds[2];
	vec3_t		axis[3];

    trm.type = TRM_INVALID;

    if ( VectorCompare( angles, vec3_origin ) ) {
        AxisCopy( axisDefault, axis );
    } else {
	    AnglesToAxis( angles, axis );
    }

	// allow NULL to be passed in for 0,0,0
	if ( !mins ) {
		mins = vec3_origin;
	}
	if ( !maxs ) {
		maxs = vec3_origin;
	}

	VectorCopy( mins, trmBounds[0] );
	VectorCopy( maxs, trmBounds[1] );

	switch ( type ) {
		case TT_AABB:
			TraceModelSetupBox( &trm, trmBounds );
			break;
		case TT_CAPSULE:
		case TT_BISPHERE:
			TraceModelSetupDodecahedron( &trm, trmBounds );
			break;
	}

	trap_CM_Translation( &tr, start, end, &trm, axisDefault, brushmask, model, origin, axis );

	CmTraceToTrace( &tr, start, end, mins, maxs, results, &trm, model, origin, axis, brushmask );
}

/*
==================
G_EntityContact
==================
*/
qboolean	G_EntityContact( const vec3_t mins, const vec3_t maxs, const sharedEntity_t *gEnt, traceType_t type ) {
	const float	*origin, *angles;
	clipHandle_t	ch;
	trace_t			trace;

	// check for exact collision
	origin = gEnt->r.currentOrigin;
	angles = gEnt->r.currentAngles;

	ch = G_ClipHandleForEntity( gEnt );
	CM_TransformedBoxTrace ( &trace, vec3_origin, vec3_origin, mins, maxs,
		ch, -1, origin, angles, type );

	return trace.startsolid;
}
