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
	G_LinkEntity( ent );
}

/*
===============================================================================

target_earthquake: used, the view of whoever's within its radius shakes,
the more the nearer, for its duration, with a rumble

===============================================================================
*/

// over: the rumble stops
void Think_EarthquakeStop( gentity_t *self ) {
	self->s.loopSound = 0;
	self->r.svFlags |= SVF_NOCLIENT;
}

void Use_Earthquake( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	gentity_t	*ev;
	float		severity, duration;

	severity = atof( G_ScriptGetSpawnArg( self, "severity", G_ScriptGetSpawnArg( self, "speed", "200" ) ) );
	duration = atof( G_ScriptGetSpawnArg( self, "duration", G_ScriptGetSpawnArg( self, "count", "5" ) ) );

	ev = G_TempEntity( self->s.origin, EV_DK_EARTHQUAKE );
	ev->r.svFlags |= SVF_BROADCAST;		// its radius goes past what's seen
	ev->s.angles2[0] = self->random;
	ev->s.angles2[1] = severity;
	ev->s.angles2[2] = duration * 1000;

	self->s.loopSound = self->noise_index;
	self->r.svFlags &= ~SVF_NOCLIENT;
	self->think = Think_EarthquakeStop;
	self->nextthink = level.time + duration * 1000;
}

/*QUAKED target_earthquake (1 0 0) (-8 -8 -8) (8 8 8)
Daikatana's: used, the views within "radius" (200) shake, by "severity" (or
"speed", 200), for "duration" (or "count", 5) seconds, rumbling.
*/
void SP_target_earthquake( gentity_t *ent ) {
	G_SpawnFloat( "radius", "200", &ent->random );
	ent->noise_index = G_SoundIndex( va( "sounds/global/earthquake_%c.wav", 'a' + rand() % 4 ) );
	ent->use = Use_Earthquake;
	ent->r.svFlags |= SVF_NOCLIENT;
	G_SetOrigin( ent, ent->s.origin );
	G_LinkEntity( ent );
}

/*
===============================================================================

effect_lightning (effects.cpp): bolts from it to its target_attractors (its
"target"s), now and then (or a player near, or the ground); used, it's
switched on or off, or strikes once

===============================================================================
*/

#define LIGHTNING_ON			1
#define LIGHTNING_CYCLE			2		// the attractors in turn, not at random
#define LIGHTNING_GROUND		4		// some strike the ground ("gndchance")
#define LIGHTNING_RANDOM_DELAY	8		// up to "delay" between strikes
#define LIGHTNING_NO_CLIENTS	32		// never at a player
#define LIGHTNING_TRACE_DMG		64		// hurts what's in the bolt's way
#define LIGHTNING_STRIKE_ONCE	128		// strikes once when used
#define LIGHTNING_CONSTANT		512		// a bolt that's always there

/*QUAKED target_attractor (0 .5 .8) (-8 -8 -8) (8 8 8)
Daikatana's: where an effect_lightning's bolts strike; its targets are used
when one does.
*/
void SP_target_attractor( gentity_t *ent ) {
	ent->r.svFlags |= SVF_NOCLIENT;
	G_SetOrigin( ent, ent->s.origin );
}

/*
================
G_LightningBolt

one bolt from it to end: drawn by the clients, heard, hurting what it
strikes ("dmg") and, with TRACE_DMG, what's in its way
================
*/
static void G_LightningBolt( gentity_t *self, const vec3_t end, gentity_t *struck ) {
	gentity_t	*ev;
	trace_t		tr;
	vec3_t		dir;
	const char	*sound;
	char		key[16];
	int			n;

	G_DPrintf( "effect_lightning %d strikes %s\n", self->s.number, vtos( end ) );
	ev = G_TempEntity( self->s.origin, EV_DK_BOLT );
	VectorCopy( end, ev->s.origin2 );
	ev->r.svFlags |= SVF_BROADCAST;		// seen from as far as its end

	// one of its sounds ("sound", "sound1" to "sound3")
	n = rand() % 3;
	Com_sprintf( key, sizeof( key ), "sound%d", n + 1 );
	sound = G_ScriptGetSpawnArg( self, key, G_ScriptGetSpawnArg( self, "sound", "" ) );
	if ( sound[0] ) {
		G_Sound( self, CHAN_AUTO, G_SoundIndex( sound ) );
	}

	if ( self->damage <= 0 ) {
		return;
	}
	VectorSubtract( end, self->s.origin, dir );
	VectorNormalize( dir );
	if ( struck && struck->takedamage ) {
		G_Damage( struck, self, self, dir, (float *)end, self->damage, DAMAGE_NO_KNOCKBACK, MOD_UNKNOWN );
	}
	if ( self->spawnflags & LIGHTNING_TRACE_DMG ) {
		G_TraceBox( &tr, self->s.origin, NULL, NULL, end, self->s.number, MASK_SHOT );
		if ( tr.entityNum < ENTITYNUM_WORLD && g_entities[tr.entityNum].takedamage && &g_entities[tr.entityNum] != struck ) {
			G_Damage( &g_entities[tr.entityNum], self, self, dir, tr.endpos, self->damage, DAMAGE_NO_KNOCKBACK, MOD_UNKNOWN );
		}
	}
}

// the player in plain sight of it: from 128 out, as the emitter may be in
// a brush (effect_lightning_think)
static qboolean G_LightningSees( gentity_t *self, gentity_t *player ) {
	vec3_t	dir, start;
	trace_t	tr;

	VectorSubtract( player->r.currentOrigin, self->s.origin, dir );
	VectorNormalize( dir );
	VectorMA( self->s.origin, 128, dir, start );
	G_TraceBox( &tr, start, NULL, NULL, player->r.currentOrigin, self->s.number, MASK_SHOT );
	return tr.fraction == 1.0f || tr.entityNum == player->s.number;
}

/*
================
G_LightningStrike

effect_lightning_think: a bolt at a player in sight within 2000 now and
then ("chance"), or at the ground ("gndchance"), else at an attractor
================
*/
static void G_LightningStrike( gentity_t *self ) {
	gentity_t	*attractors[32], *t = NULL, *player, *best = NULL;
	trace_t		tr;
	vec3_t		end, dir;
	float		roll = random(), dist, bestDist = 2000;
	int			i, num = 0;

	if ( !( self->spawnflags & ( LIGHTNING_NO_CLIENTS | LIGHTNING_CONSTANT ) ) && roll < self->random ) {
		for ( i = 0; i < level.maxplayers; i++ ) {
			player = &g_entities[i];
			if ( !player->inuse || !player->player || player->health <= 0 ) {
				continue;
			}
			dist = Distance( self->s.origin, player->r.currentOrigin );
			if ( dist < bestDist && G_LightningSees( self, player ) ) {
				best = player;
				bestDist = dist;
			}
		}
		if ( best ) {
			G_LightningBolt( self, best->r.currentOrigin, best );
			return;
		}
	} else if ( ( self->spawnflags & LIGHTNING_GROUND ) && roll < self->speed ) {
		for ( i = 0; i < 6; i++ ) {
			VectorSet( dir, crandom(), crandom(), crandom() );
			VectorNormalize( dir );
			VectorMA( self->s.origin, 2000, dir, end );
			G_TraceBox( &tr, self->s.origin, NULL, NULL, end, self->s.number, MASK_SHOT );
			if ( tr.fraction < 1 && tr.entityNum == ENTITYNUM_WORLD ) {
				G_LightningBolt( self, tr.endpos, NULL );
				return;
			}
		}
	}

	// an attractor: in turn, or any
	while ( self->target && num < (int)ARRAY_LEN( attractors ) && ( t = G_Find( t, FOFS( targetname ), self->target ) ) != NULL ) {
		attractors[num++] = t;
	}
	if ( !num ) {
		return;
	}
	if ( self->spawnflags & LIGHTNING_CYCLE ) {
		t = attractors[self->count++ % num];
	} else {
		t = attractors[rand() % num];
	}
	G_UseTargets( t, self );
	G_LightningBolt( self, t->s.origin, t );
}

void Think_Lightning( gentity_t *self ) {
	G_LightningStrike( self );
	if ( self->spawnflags & LIGHTNING_STRIKE_ONCE ) {
		return;
	}
	if ( self->spawnflags & LIGHTNING_CONSTANT ) {
		self->nextthink = level.time + self->wait * 1000;		// the bolt, again as it fades
	} else if ( self->spawnflags & LIGHTNING_RANDOM_DELAY ) {
		self->nextthink = level.time + random() * self->delay * 1000 + FRAMETIME;
	} else {
		self->nextthink = level.time + self->delay * 1000;
	}
}

void Use_Lightning( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	if ( self->spawnflags & LIGHTNING_STRIKE_ONCE ) {
		G_LightningStrike( self );
		return;
	}
	self->spawnflags ^= LIGHTNING_ON;
	if ( self->spawnflags & LIGHTNING_ON ) {
		Think_Lightning( self );
	} else {
		self->nextthink = 0;
	}
}

/*QUAKED effect_lightning (0 .5 .8) (-8 -8 -8) (8 8 8) ON CYCLE GROUND_STRIKES RANDOM_DELAY - NO_CLIENTS TRACE_DMG STRIKE_ONCE - CONSTANT
Daikatana's lightning: bolts to its target_attractors every "delay" (2)
seconds, lasting "duration" (0.3), hurting by "dmg"; now and then at a
player near ("chance", 0.1) or the ground ("gndchance", 0.2). "sound",
"sound1" to "sound3" a strike's. Used, on or off, or STRIKE_ONCE a strike.
*/
void SP_effect_lightning( gentity_t *ent ) {
	char *sound;
	int i;

	G_SpawnFloat( "delay", "2", &ent->delay );
	G_SpawnFloat( "duration", "0.3", &ent->wait );
	G_SpawnInt( "dmg", "0", &ent->damage );
	G_SpawnFloat( "chance", "0.1", &ent->random );
	G_SpawnFloat( "gndchance", "0.2", &ent->speed );
	for ( i = 0; i < 4; i++ ) {
		if ( G_SpawnString( i ? va( "sound%d", i ) : "sound", "", &sound ) && sound[0] ) {
			G_SoundIndex( sound );
		}
	}
	ent->use = Use_Lightning;
	ent->think = Think_Lightning;
	ent->r.svFlags |= SVF_NOCLIENT;
	G_SetOrigin( ent, ent->s.origin );
	// as Daikatana's, once everything else is there (a constant bolt later still)
	if ( ent->spawnflags & LIGHTNING_ON ) {
		ent->nextthink = level.time + ( ( ent->spawnflags & LIGHTNING_CONSTANT ) ? 3750 : 100 );
	}
}

/*
===============================================================================

func_monitor and info_camera (func_monitor.cpp): used, the monitor shows
its camera (its "target", an info_camera looking at its own "target") on
the player's screen, a cinematic's view, for "wait" seconds

===============================================================================
*/

/*QUAKED info_camera (0 .5 .8) (-8 -8 -8) (8 8 8)
Daikatana's: a func_monitor's camera, looking at its "target".
*/
void SP_info_camera( gentity_t *ent ) {
	if ( ent->target ) {
		ent->cameraTarget = G_NewString( ent->target );		// Doom 3's func_cameraview, aimed
	}
	ent->cameraFov = 90;
	ent->r.svFlags |= SVF_NOCLIENT;
	G_SetOrigin( ent, ent->s.origin );
	VectorCopy( ent->s.angles, ent->r.currentAngles );
}

// its time is up: the players' own eyes
void Think_MonitorOff( gentity_t *self ) {
	if ( level.camera == self->enemy ) {
		G_SetCamera( NULL );
	}
	self->enemy = NULL;
}

void Use_Monitor( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	gentity_t *cam;

	if ( self->enemy || level.inCinematic ) {
		return;		// on already
	}
	cam = G_Find( NULL, FOFS( targetname ), self->target );
	if ( !cam ) {
		G_Printf( S_COLOR_YELLOW "func_monitor at %s: no camera %s\n", vtos( self->s.origin ), self->target );
		return;
	}
	self->enemy = cam;
	G_SetCamera( cam );
	self->think = Think_MonitorOff;
	self->nextthink = level.time + self->wait * 1000;
}

/*QUAKED func_monitor (0 .5 .8) ?
Daikatana's: used, its "target" camera (an info_camera) is the view for
"wait" seconds (5).
*/
void SP_func_monitor( gentity_t *ent ) {
	G_SpawnFloat( "wait", "5", &ent->wait );
	if ( ent->model && ent->model[0] == '*' ) {
		G_SetBrushModel( ent, ent->model );		// the screen
	}
	ent->use = Use_Monitor;
	G_LinkEntity( ent );
}
