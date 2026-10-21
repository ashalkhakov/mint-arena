/*
===========================================================================
Copyright (C) 2026 Spearmint Source Code contributors.

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
===========================================================================
*/

// g_frob.c -- looking at things and using them, The Dark Mod's frob
// (idPlayer::PerformFrobCheck, PerformFrob): every frame the player's eye
// picks what it looks at among what can be used, within that thing's reach
// ("frob_distance", 80 by default, Daikatana's), favouring what's nearest the
// middle of the view ("frob_bias" scales it). The client is told
// (STAT_FROB_ENTITY) to show it and say what use would do. Use, pressed,
// takes an item into the inventory, or uses an entity made "frobable".

#include "g_local.h"

#define FROB_DISTANCE		80		// Daikatana's use reach (Client_Use)
#define FROB_MIN_DOT		0.94f	// Doom 3's "in view" for items (idItem::UpdateRenderEntity)

static float G_FrobDistance( const gentity_t *ent ) {
	const dict_t *def = ent->item ? BG_ItemDef( ent->item ) : NULL;

	if ( ent->frobDistance > 0 ) {
		return ent->frobDistance;
	}
	return def ? DictGetFloat( def, "frob_distance", va( "%d", FROB_DISTANCE ) ) : FROB_DISTANCE;
}

static float G_FrobBias( const gentity_t *ent ) {
	const dict_t *def = ent->item ? BG_ItemDef( ent->item ) : NULL;

	return def ? DictGetFloat( def, "frob_bias", "1" ) : 1.0f;
}

// whether an entity is something a player can use now
static qboolean G_Frobable( const gentity_t *ent ) {
	if ( !ent->inuse ) {
		return qfalse;
	}
	if ( ent->s.eType == ET_ITEM ) {
		return ent->item && ent->s.contents && !( ent->s.eFlags & EF_NODRAW ) && !( ent->r.svFlags & SVF_NOCLIENT );
	}
	return ent->frobable && !( ent->r.svFlags & SVF_NOCLIENT );
}

// the middle of an entity, to look at
static void G_FrobCenter( const gentity_t *ent, vec3_t center ) {
	VectorAdd( ent->r.absmin, ent->r.absmax, center );
	VectorScale( center, 0.5f, center );
}

/*
================
G_FrobCheck

What the player looks at and can use: of what can be used within reach and
nearly in the middle of the view, unblocked, the one most in the middle
(times its bias). ENTITYNUM_NONE for nothing.
================
*/
static int G_FrobCheck( gentity_t *ent ) {
	playerState_t *ps = &ent->player->ps;
	vec3_t eye, forward, mins, maxs, center, dir;
	int touch[MAX_GENTITIES], num, i, best = ENTITYNUM_NONE;
	float bestScore = 0, dist, score, reach;
	gentity_t *hit;
	trace_t tr;

	if ( ps->stats[STAT_HEALTH] <= 0 || ps->pm_type != PM_NORMAL ) {
		return ENTITYNUM_NONE;
	}

	VectorCopy( ps->origin, eye );
	eye[2] += ps->viewheight;
	AngleVectors( ps->viewangles, forward, NULL, NULL );

	VectorSet( mins, eye[0] - FROB_DISTANCE * 2, eye[1] - FROB_DISTANCE * 2, eye[2] - FROB_DISTANCE * 2 );
	VectorSet( maxs, eye[0] + FROB_DISTANCE * 2, eye[1] + FROB_DISTANCE * 2, eye[2] + FROB_DISTANCE * 2 );
	num = G_EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );

	for ( i = 0; i < num; i++ ) {
		hit = &g_entities[touch[i]];
		if ( hit == ent || !G_Frobable( hit ) ) {
			continue;
		}
		G_FrobCenter( hit, center );
		VectorSubtract( center, eye, dir );
		dist = VectorNormalize( dir );
		reach = G_FrobDistance( hit );
		// within reach of the eye, measured to its nearest side
		if ( dist - RadiusFromBounds( hit->s.mins, hit->s.maxs ) > reach ) {
			continue;
		}
		score = DotProduct( dir, forward );
		if ( score < FROB_MIN_DOT && dist > 24 ) {
			continue;
		}
		score *= G_FrobBias( hit );
		if ( best != ENTITYNUM_NONE && score <= bestScore ) {
			continue;
		}
		// nothing solid in the way
		G_Trace( &tr, eye, NULL, NULL, center, ent->s.number, MASK_SOLID, TT_AABB );
		if ( tr.fraction < 1.0f && tr.entityNum != hit->s.number ) {
			continue;
		}
		best = hit->s.number;
		bestScore = score;
	}
	return best;
}

/*
================
G_Frob

Uses what the player looks at, The Dark Mod's order: an entity's own use
(its script's frob( player ), its targets), else taking an item.
================
*/
static void G_Frob( gentity_t *ent, gentity_t *target ) {
	trace_t trace;

	if ( target->s.eType == ET_ITEM ) {
		memset( &trace, 0, sizeof( trace ) );
		g_itemFrobbed = qtrue;
		Touch_Item( target, ent, &trace );
		g_itemFrobbed = qfalse;
		return;
	}

	if ( ScriptObject_HasObject( &target->scriptObject ) &&
		G_ScriptCallObjectFunction( target, "frob", ent, NULL ) ) {
		return;
	}
	if ( target->use ) {
		target->use( target, ent, ent );
	} else {
		G_UseTargets( target, ent );
	}
}

/*
================
G_FrobThink

Each player think, after its buttons: what it looks at, and use pressed on
it.
================
*/
void G_FrobThink( gentity_t *ent ) {
	gplayer_t *player = ent->player;
	int target;

	target = G_FrobCheck( ent );
	if ( g_debugFrob.integer && target != player->ps.stats[STAT_FROB_ENTITY] ) {
		G_Printf( "frob: %s looks at %d %s\n", player->pers.netname, target,
			target != ENTITYNUM_NONE ? g_entities[target].classname : "" );
	}
	player->ps.stats[STAT_FROB_ENTITY] = target;
	if ( g_debugFrob.integer && ( player->buttons & BUTTON_ACTIVATE ) && !( player->oldbuttons & BUTTON_ACTIVATE ) ) {
		G_Printf( "frob: %s uses %d\n", player->pers.netname, target );
	}

	if ( target != ENTITYNUM_NONE && ( player->buttons & BUTTON_ACTIVATE ) && !( player->oldbuttons & BUTTON_ACTIVATE ) ) {
		G_Frob( ent, &g_entities[target] );
	}
}
