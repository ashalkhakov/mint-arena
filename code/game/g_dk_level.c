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
// g_dk_level.c -- Daikatana's level entities that make things happen:
// func_event_generator (its func_event_generator.cpp), target_earthquake,
// target_attractor and effect_lightning (target.cpp, effects.cpp),
// func_monitor and info_camera (func_monitor.cpp). Their looks are the
// cgame's: the earthquake's shake, the lightning's bolts (EV_DK_EARTHQUAKE,
// EV_DK_BOLT, cg_event.c).

#include "g_local.h"

void InitTrigger( gentity_t *self );		// g_trigger.c

/*
===============================================================================

func_event_generator: a timed relay. Each key that isn't one of its own is
a targetname and the seconds after it's set off that it's used: "agg5" "4"

===============================================================================
*/

#define EVENTGEN_ONCE			1
#define EVENTGEN_TOUCHABLE		2
#define EVENTGEN_MONSTERS		4		// monsters set it off too

static const char *eventGenKeys[] = { "classname", "model", "modelname", "origin", "spawnflags", "targetname",
									  "target", "sound", "gametype", "!gametype", "angle", "angles", NULL };

// one event's time come: what it names, used
void Think_EventGeneratorFire( gentity_t *self ) {
	gentity_t *t = NULL;

	while ( ( t = G_Find( t, FOFS( targetname ), self->target ) ) != NULL ) {
		if ( t->use ) {
			t->use( t, self, self->activator );
		}
	}
	G_FreeEntity( self );
}

/*
================
Use_EventGenerator

each of its events, after its seconds; not again until they've all gone
================
*/
void Use_EventGenerator( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	const dict_t			*args = G_ScriptSpawnArgs( self );
	const dictKeyValue_t	*kv;
	const char				*key, *value;
	gentity_t				*ev;
	float					delay, last = 0;
	int						i, k;

	if ( !args || level.time < self->pain_debounce_time ) {
		return;
	}
	if ( self->noise_index ) {
		G_Sound( self, CHAN_AUTO, self->noise_index );
	}
	for ( i = 0; i < DictGetNumKeyVals( args ); i++ ) {
		kv = DictGetKeyVal( args, i );
		key = PoolStrGetString( kv->key );
		value = PoolStrGetString( kv->value );
		for ( k = 0; eventGenKeys[k] && Q_stricmp( key, eventGenKeys[k] ); k++ ) {
		}
		if ( eventGenKeys[k] || ( !isdigit( value[0] ) && value[0] != '.' ) ) {
			continue;		// one of its own
		}
		delay = atof( value );
		last = MAX( last, delay );
		ev = G_Spawn();
		ev->classname = "event_generator_event";
		ev->target = G_NewString( key );
		ev->activator = activator;
		ev->think = Think_EventGeneratorFire;
		ev->nextthink = level.time + MAX( FRAMETIME, delay * 1000 );
		ev->r.svFlags |= SVF_NOCLIENT;
	}
	self->pain_debounce_time = level.time + last * 1000 + FRAMETIME;

	if ( self->spawnflags & EVENTGEN_ONCE ) {
		self->use = NULL;
		self->touch = NULL;
	}
}

void Touch_EventGenerator( gentity_t *self, gentity_t *other, trace_t *trace ) {
	if ( other->health <= 0 || !( other->player || ( ( self->spawnflags & EVENTGEN_MONSTERS ) && G_IsMonster( other ) ) ) ) {
		return;
	}
	Use_EventGenerator( self, other, other );
}

/*QUAKED func_event_generator (.5 .5 .5) ? ONCE TOUCHABLE MONSTERS
Daikatana's timed relay: used (or, TOUCHABLE, touched), it uses each
targetname given as a key, that key's seconds later ("door1" "2.5").
"sound" played as it goes.
*/
void SP_func_event_generator( gentity_t *ent ) {
	char *sound;

	if ( G_SpawnString( "sound", "", &sound ) && sound[0] ) {
		ent->noise_index = G_SoundIndex( sound );
	}
	if ( ent->model && ent->model[0] == '*' ) {
		InitTrigger( ent );
	}
	ent->use = Use_EventGenerator;
	if ( ent->spawnflags & EVENTGEN_TOUCHABLE ) {
		ent->touch = Touch_EventGenerator;
	} else {
		ent->s.contents = 0;
	}
	ent->r.svFlags |= SVF_NOCLIENT;
	trap_LinkEntity( ent );
}
