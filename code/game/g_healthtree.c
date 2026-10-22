/*
===========================================================================
Copyright (C) 2026 the Daikatana restoration project

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
//
// g_healthtree.c -- Daikatana's health tree (healthtree.cpp): a plant with
// up to five fruit, each healing whoever uses it (frobs it, g_frob.c) by 10,
// up to their health's limit, one a second. Its model's frame shows the
// fruit left. Picked bare it stays bare in the campaign; elsewhere it grows
// a fruit back every 30 / max_fruit seconds.
//
// Keys: "max_fruit" (0 to 5, default 5), "fruit_value" (default 10),
// "model" (default models/e1/healthtree.md3).

#include "g_local.h"

#define HT_MAXFRUIT		5
#define HT_USE_DELAY	1000	// msec between fruit

static const char *ht_useSounds[] = { "sounds/e1/t_use1.wav", "sounds/e1/t_use2.wav" };

// its frame: 0 with all five fruit, 5 with none
static void HealthTree_SetFrame( gentity_t *self ) {
	self->s.frame = HT_MAXFRUIT - self->count;
}

/*
================
HealthTree_Regrow

a fruit back, outside the campaign
================
*/
void HealthTree_Regrow( gentity_t *self ) {
	if ( self->count < self->health ) {
		self->count++;
		HealthTree_SetFrame( self );
		G_Sound( self, CHAN_AUTO, G_SoundIndex( "sounds/e1/t_regen.wav" ) );
	}
	if ( self->count < self->health ) {
		self->nextthink = level.time + self->wait;
	}
}

/*
================
HealthTree_Use

a fruit eaten: 10 health, up to the user's limit, if they need it
================
*/
void HealthTree_Use( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	gplayer_t *player;
	int max;

	if ( !activator || !activator->player || activator->health <= 0 ) {
		return;
	}
	player = activator->player;
	max = player->ps.stats[STAT_MAX_HEALTH];
	if ( self->count <= 0 || level.time < self->pain_debounce_time || activator->health >= max ) {
		return;
	}

	activator->health = MIN( activator->health + self->damage, max );
	player->ps.stats[STAT_HEALTH] = activator->health;
	self->count--;
	HealthTree_SetFrame( self );
	self->pain_debounce_time = level.time + HT_USE_DELAY;
	G_Sound( self, CHAN_AUTO, G_SoundIndex( ht_useSounds[rand() & 1] ) );

	if ( g_gametype.integer != GT_CAMPAIGN && self->wait > 0 && self->nextthink <= level.time ) {
		self->nextthink = level.time + self->wait;
	}
}

/*
QUAKED misc_healthtree (0 .5 0) (-8 -8 -24) (8 8 8)
Daikatana's health tree: use it for 10 health a fruit, up to five fruit.
"max_fruit" fruit it has (0 to 5), "fruit_value" health a fruit gives.
*/
void SP_misc_healthtree( gentity_t *ent ) {
	char	*model;
	trace_t	tr;
	vec3_t	end;
	int		fruit;

	if ( g_gametype.integer != GT_CAMPAIGN && g_instagib.integer ) {
		G_FreeEntity( ent );
		return;
	}

	G_SpawnString( "model", "models/e1/healthtree.md3", &model );
	if ( !strstr( model, ".md3" ) ) {
		model = "models/e1/healthtree.md3";		// a converted map's Daikatana .dkm name
	}
	ent->s.modelindex = G_ModelIndex( model );
	ent->s.eType = ET_GENERAL;

	G_SpawnInt( "max_fruit", "5", &fruit );
	ent->health = Com_Clamp( 0, HT_MAXFRUIT, fruit );		// how many it grows
	ent->count = ent->health;							// how many it has
	G_SpawnInt( "fruit_value", "10", &ent->damage );
	ent->wait = ent->health ? 30000 / ent->health : 0;	// msec to grow one back
	HealthTree_SetFrame( ent );

	VectorSet( ent->s.mins, -8, -8, -24 );
	VectorSet( ent->s.maxs, 8, 8, 8 );
	ent->s.contents = CONTENTS_SOLID;
	ent->clipmask = MASK_SOLID;

	// on the floor under it, as Daikatana's falls to it
	VectorSet( end, ent->s.origin[0], ent->s.origin[1], ent->s.origin[2] - 256 );
	G_TraceBox( &tr, ent->s.origin, ent->s.mins, ent->s.maxs, end, ENTITYNUM_NONE, MASK_SOLID );
	if ( !tr.startsolid && tr.fraction < 1.0f ) {
		VectorCopy( tr.endpos, ent->s.origin );
	}
	G_SetOrigin( ent, ent->s.origin );
	VectorCopy( ent->s.angles, ent->s.apos.trBase );

	ent->use = HealthTree_Use;
	ent->think = HealthTree_Regrow;
	ent->frobable = qtrue;

	G_SoundIndex( ht_useSounds[0] );
	G_SoundIndex( ht_useSounds[1] );
	G_SoundIndex( "sounds/e1/t_regen.wav" );

	G_LinkEntity( ent );
}
