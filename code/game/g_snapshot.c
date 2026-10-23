/*
===========================================================================
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

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
// g_snapshot.c -- Doom 3's idGameLocal::ServerWriteSnapshot: the game's own
// state in each client's snapshot, after the entityState_ts every entity
// has. Each entity in the snapshot writes what it needs (WriteToSnapshot),
// delta coded (bitMsgDelta_t) against what it wrote for the snapshot the
// client last acknowledged, so an entity that hasn't changed sends nothing.
// What was written is kept a snapshot at a time, a client at a time, as
// Doom 3's server keeps its clients' entity states. cgame reads it back
// (cg_snapshot.c).

#include "g_local.h"

#define MAX_STATE_ENTITIES		256			// entities with a state a snapshot
#define STATE_RECORD_WORDS		4096		// their states, whole

typedef struct {
	int			entityNum;
	int			start;		// word
	int			numBits;
} stateEntity_t;

// what was written for a client's snapshot: each entity's whole state
typedef struct {
	int				sequence;
	int				numEntities;
	stateEntity_t	entities[MAX_STATE_ENTITIES];
	int				numWords;
	int				words[STATE_RECORD_WORDS];
} stateRecord_t;

static stateRecord_t	g_stateRecords[MAX_CLIENTS][SNAPSHOT_BACKUP];

/*
================
G_WriteEntitySnapshot

an entity's WriteToSnapshot: qfalse if it has no state of its own
================
*/
static qboolean G_WriteEntitySnapshot( gentity_t *ent, bitMsgDelta_t *delta ) {
	if ( ent->ragdoll ) {
		G_WriteRagdollSnapshot( ent, delta );
		return qtrue;
	}
	return qfalse;
}

/*
================
G_FindStateEntity

the entity's state in a record, or NULL
================
*/
static const stateEntity_t *G_FindStateEntity( const stateRecord_t *rec, int entityNum ) {
	int i;

	for ( i = 0; i < rec->numEntities; i++ ) {
		if ( rec->entities[i].entityNum == entityNum ) {
			return &rec->entities[i];
		}
	}
	return NULL;
}

/*
================
G_KeepBaseState

an entity of the snapshot not in its message: its state stays its base's,
as the client takes it (CG_ReadSnapshotState)
================
*/
static void G_KeepBaseState( stateRecord_t *rec, const stateRecord_t *base, int entityNum ) {
	const stateEntity_t *baseEnt = base ? G_FindStateEntity( base, entityNum ) : NULL;
	stateEntity_t *se;
	int numWords;

	if ( !baseEnt || rec->numEntities >= MAX_STATE_ENTITIES ) {
		return;
	}
	numWords = ( baseEnt->numBits + 31 ) / 32;
	if ( rec->numWords + numWords > STATE_RECORD_WORDS ) {
		return;
	}
	se = &rec->entities[rec->numEntities++];
	se->entityNum = entityNum;
	se->start = rec->numWords;
	se->numBits = baseEnt->numBits;
	memcpy( rec->words + se->start, base->words + baseEnt->start, numWords * sizeof( int ) );
	rec->numWords += numWords;
}

/*
================
G_WriteSnapshot

GAME_WRITE_SNAPSHOT: the state of the snapshot sequence for client
clientNum, against what was written for baseSequence (-1: nothing): a
list of entity numbers and their states, ended by MAX_GENTITIES-1
================
*/
void G_WriteSnapshot( int clientNum, int sequence, int baseSequence ) {
	static int entityNums[MAX_GENTITIES];
	static int out[SNAPSHOT_STATE_WORDS];
	stateRecord_t *rec, *base;
	const stateEntity_t *baseEnt;
	stateEntity_t *se;
	bitMsg_t msg, baseMsg, newBase;
	bitMsgDelta_t delta;
	gentity_t *ent;
	int i, n, start;

	if ( clientNum < 0 || clientNum >= MAX_CLIENTS ) {
		return;
	}
	rec = &g_stateRecords[clientNum][sequence & ( SNAPSHOT_BACKUP - 1 )];
	base = NULL;
	if ( baseSequence >= 0 ) {
		base = &g_stateRecords[clientNum][baseSequence & ( SNAPSHOT_BACKUP - 1 )];
		if ( base->sequence != baseSequence || base == rec ) {
			base = NULL;		// (can't be: the engine keeps as many)
		}
	}
	rec->sequence = sequence;
	rec->numEntities = 0;
	rec->numWords = 0;

	memset( out, 0, sizeof( out ) );
	BitMsg_Init( &msg, out, SNAPSHOT_STATE_WORDS );
	msg.maxBits -= GENTITYNUM_BITS;		// room for the end

	n = trap_GetSnapshotEntities( clientNum, entityNums, MAX_GENTITIES );
	for ( i = 0; i < n; i++ ) {
		ent = &g_entities[entityNums[i]];
		if ( !ent->inuse || rec->numEntities >= MAX_STATE_ENTITIES ) {
			G_KeepBaseState( rec, base, entityNums[i] );
			continue;
		}
		se = &rec->entities[rec->numEntities];
		se->entityNum = entityNums[i];
		se->start = rec->numWords;
		memset( rec->words + se->start, 0, ( STATE_RECORD_WORDS - se->start ) * sizeof( int ) );
		BitMsg_Init( &newBase, rec->words + se->start, STATE_RECORD_WORDS - se->start );

		baseEnt = base ? G_FindStateEntity( base, se->entityNum ) : NULL;
		if ( baseEnt ) {
			BitMsg_Init( &baseMsg, base->words + baseEnt->start, ( baseEnt->numBits + 31 ) / 32 );
			baseMsg.maxBits = baseEnt->numBits;
		}

		// the entity's number, then its state; nothing if it hasn't changed
		start = msg.bit;
		BitMsg_WriteBits( &msg, se->entityNum, GENTITYNUM_BITS );
		BitMsgDelta_Init( &delta, baseEnt ? &baseMsg : NULL, &newBase, &msg );
		if ( !G_WriteEntitySnapshot( ent, &delta ) ) {
			msg.bit = start;
			G_KeepBaseState( rec, base, entityNums[i] );
			continue;
		}
		if ( newBase.overflowed || msg.overflowed ) {
			G_Printf( S_COLOR_YELLOW "G_WriteSnapshot: entity %d's state doesn't fit\n", se->entityNum );
			msg.bit = start;
			msg.overflowed = qfalse;
			G_KeepBaseState( rec, base, entityNums[i] );
			continue;
		}
		if ( !delta.changed ) {
			msg.bit = start;
		}
		se->numBits = newBase.bit;
		rec->numWords += ( newBase.bit + 31 ) / 32;
		rec->numEntities++;
	}
	// (room is kept for the end: an entity that doesn't fit stays out)
	msg.maxBits += GENTITYNUM_BITS;
	BitMsg_WriteBits( &msg, MAX_GENTITIES - 1, GENTITYNUM_BITS );
	trap_SetSnapshotState( out, ( msg.bit + 7 ) / 8 );
}
