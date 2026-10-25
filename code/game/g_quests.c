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

// g_quests.c -- quests and their objectives, Fallout 4's: what the player
// is set to do. The Dark Mod's objectives (MissionData.cpp: an objective's
// state, incomplete, complete or failed, whether it's shown, what its
// completion or failure fires) grouped into quests, each a def of its own
// (def/dk_quests.def: "title", "desc", "obj<n>" its objectives' text,
// "obj<n>_target_complete", "obj<n>_target_failed"). A quest is given and
// its objectives set by a target_setobjective a map fires (a spawn point
// the player arrives at, a monster seeing or killing...) or by a script
// ($player.setObjectiveState...). Every player has them alike (the
// campaign's coop), carried with the inventory from map to map and in
// saves (g_save.c); each is told of them by a "quests" server command,
// OpenJK's datapad objectives' way to the client (cg_quests.c), which
// announces what changed and the screen's quests tab shows.

#include "g_local.h"

/*
================
G_QuestFind

the player's quest of the def, or NULL
================
*/
static quest_t *G_QuestFind( gplayer_t *player, const char *name ) {
	int i;

	for ( i = 0; i < MAX_QUESTS; i++ ) {
		if ( player->pers.quests[i].name[0] && !Q_stricmp( player->pers.quests[i].name, name ) ) {
			return &player->pers.quests[i];
		}
	}
	return NULL;
}

/*
================
G_QuestGive

the quest, given if it isn't yet: active, its objectives hidden till set
================
*/
static quest_t *G_QuestGive( gplayer_t *player, const char *name ) {
	quest_t *q = G_QuestFind( player, name );
	int i;

	if ( q ) {
		return q;
	}
	if ( !EntityDef_Find( name ) ) {
		G_Printf( S_COLOR_YELLOW "quests: no quest def %s\n", name );
		return NULL;
	}
	for ( i = 0; i < MAX_QUESTS; i++ ) {
		if ( !player->pers.quests[i].name[0] ) {
			q = &player->pers.quests[i];
			memset( q, 0, sizeof( *q ) );
			Q_strncpyz( q->name, name, sizeof( q->name ) );
			q->state = QUEST_ACTIVE;
			memset( q->obj, QUEST_HIDDEN, MAX_QUEST_OBJS );
			q->order = ++player->pers.questOrder;
			return q;
		}
	}
	G_Printf( S_COLOR_YELLOW "quests: more than %d\n", MAX_QUESTS );
	return NULL;
}

/*
================
G_QuestStateChar

a state's name: incomplete (active, for a quest), complete, failed; 0 for
none
================
*/
static char G_QuestStateChar( const char *s, qboolean quest ) {
	if ( !s || !s[0] ) {
		return 0;
	}
	if ( !Q_stricmp( s, "complete" ) || !Q_stricmp( s, "completed" ) ) {
		return QUEST_COMPLETE;
	}
	if ( !Q_stricmp( s, "failed" ) ) {
		return QUEST_FAILED;
	}
	if ( !Q_stricmp( s, "incomplete" ) || !Q_stricmp( s, "active" ) ) {
		return quest ? QUEST_ACTIVE : QUEST_INCOMPLETE;
	}
	G_Printf( S_COLOR_YELLOW "quests: what state is %s?\n", s );
	return 0;
}

/*
================
G_QuestsSend

the player's quests, to its client: "quests <n>" then each quest's def,
state and objectives, a character each (QUEST_*); "questsload" for what a
saved game had, nothing new to announce
================
*/
void G_QuestsSend( gentity_t *ent, qboolean loaded ) {
	gplayer_t *player = ent->player;
	char cmd[MAX_STRING_CHARS];
	int i, n = 0;

	if ( !player ) {
		return;
	}
	for ( i = 0; i < MAX_QUESTS; i++ ) {
		if ( player->pers.quests[i].name[0] ) {
			n++;
		}
	}
	Com_sprintf( cmd, sizeof( cmd ), "%s %d", loaded ? "questsload" : "quests", n );
	for ( i = 0; i < MAX_QUESTS; i++ ) {
		quest_t *q = &player->pers.quests[i];

		if ( q->name[0] ) {
			Q_strcat( cmd, sizeof( cmd ), va( " %s %c %d %.*s", q->name, q->state, q->order, MAX_QUEST_OBJS, q->obj ) );
		}
	}
	trap_SendServerCommand( ent - g_entities, cmd );
}

/*
================
G_SetObjective

for every player: the quest given if it isn't, and its objective n (1 to
MAX_QUEST_OBJS; 0 the quest itself) set to state (0 leaves it), shown or
hidden (visible -1 leaves it), only when it's in onlyIf now (0 any state).
What a change completes or fails fires the def's targets for it
================
*/
void G_SetObjective( const char *name, int n, char state, int visible, char onlyIf, gentity_t *activator ) {
	const dict_t *def = EntityDef_Find( name );
	qboolean fired = qfalse;
	int i;

	if ( n < 0 || n > MAX_QUEST_OBJS ) {
		G_Printf( S_COLOR_YELLOW "quests: %s has no objective %d\n", name, n );
		return;
	}
	for ( i = 0; i < level.maxplayers; i++ ) {
		gentity_t *ent = &g_entities[i];
		gplayer_t *player = ent->player;
		quest_t *q;
		char *slot, now, was;

		if ( !ent->inuse || !player || player->pers.connected != CON_CONNECTED ) {
			continue;
		}
		q = G_QuestGive( player, name );
		if ( !q ) {
			continue;
		}
		slot = n ? &q->obj[n - 1] : &q->state;
		was = *slot;
		now = *slot;
		if ( n && now == QUEST_HIDDEN && ( state || visible > 0 ) ) {
			now = QUEST_INCOMPLETE;		// (shown, as it was: incomplete)
		}
		if ( !onlyIf || ( now == onlyIf ) ) {
			if ( state ) {
				now = state;
			}
			if ( n && visible == 0 ) {
				now = QUEST_HIDDEN;
			}
		} else if ( was == QUEST_HIDDEN ) {
			now = was;
		}
		if ( now == was ) {
			continue;
		}
		*slot = now;
		q->order = ++player->pers.questOrder;
		G_QuestsSend( ent, qfalse );

		// what it fires (once, not per player)
		if ( !fired && def && ( now == QUEST_COMPLETE || now == QUEST_FAILED ) ) {
			const char *target;
			gentity_t *t = NULL;

			fired = qtrue;
			target = DictGetStringReference( def, n ? va( "obj%d_target_%s", n, now == QUEST_COMPLETE ? "complete" : "failed" )
											  : ( now == QUEST_COMPLETE ? "target_complete" : "target_failed" ), "" );
			while ( target[0] && ( t = G_Find( t, FOFS( targetname ), target ) ) != NULL ) {
				if ( t->use ) {
					t->use( t, activator ? activator : ent, activator ? activator : ent );
				}
			}
		}
	}
}

/*
================
G_GetObjective

the first player's state of the quest's objective n (0 the quest):
"incomplete" (or "active"), "complete", "failed", "hidden", or "" when it
hasn't the quest
================
*/
const char *G_GetObjective( const char *name, int n ) {
	quest_t *q = NULL;
	char c;
	int i;

	for ( i = 0; i < level.maxplayers && !q; i++ ) {
		if ( g_entities[i].inuse && level.players[i].pers.connected == CON_CONNECTED ) {
			q = G_QuestFind( &level.players[i], name );
		}
	}
	if ( !q || n < 0 || n > MAX_QUEST_OBJS ) {
		return "";
	}
	c = n ? q->obj[n - 1] : q->state;
	switch ( c ) {
	case QUEST_COMPLETE:	return "complete";
	case QUEST_FAILED:		return "failed";
	case QUEST_HIDDEN:		return "hidden";
	case QUEST_ACTIVE:		return "active";
	default:				return "incomplete";
	}
}

/*
================
Use_SetObjective
================
*/
void Use_SetObjective( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	G_SetObjective( self->map, self->count, (char)self->health, self->spawnflags, (char)self->damage, activator );
	G_UseTargets( self, activator );
}

/*QUAKED target_setobjective (.5 .5 .5) (-8 -8 -8) (8 8 8)
Used, sets a quest's objective (g_quests.c), for every player; the quest is
given if it isn't yet. Then fires its targets (another objective...).

"quest"		the quest's def (def/dk_quests.def)
"objective"	its objective's number (1 up); 0 the quest itself
"state"		incomplete, complete or failed (none: left as it is)
"visible"	1 shown, 0 hidden (none: left as it is; a state shows it)
"only_if"	only when it's in this state now (incomplete: not once
			complete or failed)
*/
void SP_target_setobjective( gentity_t *self ) {
	char *quest, *state, *onlyIf;
	int visible;

	G_SpawnString( "quest", "", &quest );
	G_SpawnInt( "objective", "0", &self->count );
	G_SpawnString( "state", "", &state );
	G_SpawnInt( "visible", "-1", &visible );
	G_SpawnString( "only_if", "", &onlyIf );
	if ( !quest[0] ) {
		G_Printf( S_COLOR_YELLOW "target_setobjective at %s without a quest\n", vtos( self->s.origin ) );
	}
	self->map = G_NewString( quest );		// (keys of no other use to a target: G_UseTargets prints its message)
	self->health = G_QuestStateChar( state, self->count == 0 );
	self->spawnflags = visible;			// (keys of no other use to a target)
	self->damage = G_QuestStateChar( onlyIf, self->count == 0 );
	self->use = Use_SetObjective;
}
