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
// cg_netevent.c -- Doom 3's entity network events on the client
// (idGameLocal::ClientProcessEntityNetworkEventQueue): the server's "eev"
// commands queued in time order, each run when the client's time reaches
// it (idEntity::ClientReceiveEvent)

#include "cg_local.h"

#define MAX_QUEUED_EVENTS		256

typedef struct {
	int		entityNum;
	int		event;
	int		time;
	int		params[MAX_EVENT_PARAM_SIZE / 4];
	int		size;
} queuedEvent_t;

static queuedEvent_t	cg_eventQueue[MAX_QUEUED_EVENTS];
static int				cg_numQueuedEvents;

void CG_ClearNetEvents( void ) {
	cg_numQueuedEvents = 0;
}

static int CG_HexDigit( int c ) {
	if ( c >= '0' && c <= '9' ) {
		return c - '0';
	}
	if ( c >= 'a' && c <= 'f' ) {
		return c - 'a' + 10;
	}
	return -1;
}

/*
================
CG_QueueNetEvent

"eev <entity> <event> <time> <params as hex>", argument start the entity's
================
*/
void CG_QueueNetEvent( int start ) {
	queuedEvent_t *ev;
	const char *hex;
	int i, hi, lo;

	if ( cg_numQueuedEvents == MAX_QUEUED_EVENTS ) {
		CG_Printf( S_COLOR_YELLOW "CG_QueueNetEvent: too many queued events\n" );
		return;
	}
	ev = &cg_eventQueue[cg_numQueuedEvents];
	memset( ev, 0, sizeof( *ev ) );
	ev->entityNum = atoi( CG_Argv( start ) );
	ev->event = atoi( CG_Argv( start + 1 ) );
	ev->time = atoi( CG_Argv( start + 2 ) );
	hex = CG_Argv( start + 3 );
	for ( i = 0; i < MAX_EVENT_PARAM_SIZE && hex[0] && hex[1]; i++, hex += 2 ) {
		hi = CG_HexDigit( hex[0] );
		lo = CG_HexDigit( hex[1] );
		if ( hi < 0 || lo < 0 ) {
			break;
		}
		ev->params[i >> 2] |= ( ( hi << 4 ) | lo ) << ( ( i & 3 ) * 8 );
	}
	ev->size = i;
	if ( ev->entityNum < 0 || ev->entityNum >= MAX_GENTITIES ) {
		return;
	}

	// in time order: Doom 3 drops what comes out of order (OUTOFORDER_DROP)
	if ( cg_numQueuedEvents && ev->time < cg_eventQueue[cg_numQueuedEvents - 1].time ) {
		return;
	}
	cg_numQueuedEvents++;
}

/*
================
CG_EntityReceiveEvent

idEntity::ClientReceiveEvent: what's common to entities; qfalse for an
event it doesn't know
================
*/
static qboolean CG_EntityReceiveEvent( centity_t *cent, int event, int time, bitMsg_t *msg ) {
	const bgSoundShader_t *shader;
	const char *file;
	int index, channel;
	float volume;

	switch ( event ) {
		case ENTEV_STARTSOUND:
			index = BitMsg_ReadBits( msg, 12 );
			channel = BitMsg_ReadBits( msg, 4 );
			volume = BitMsg_ReadBits( msg, 8 ) / 255.0f;
			if ( index < 0 || index >= bg_numSoundShaders ) {
				return qtrue;
			}
			shader = &bg_soundShaders[index];
			file = BG_SoundShaderFile( shader );
			if ( file ) {
				trap_S_StartSoundVolume( NULL, cent->currentState.number, channel, trap_S_RegisterSound( file, qfalse ),
										 shader->volume * volume );
			}
			return qtrue;
		default:
			return qfalse;
	}
}

/*
================
CG_ProcessNetEvents

the queue's events whose time has come, in order
================
*/
void CG_ProcessNetEvents( void ) {
	queuedEvent_t *ev;
	centity_t *cent;
	bitMsg_t msg;
	int i, n;

	for ( n = 0; n < cg_numQueuedEvents; n++ ) {
		ev = &cg_eventQueue[n];

		// only process forward, in order
		if ( ev->time > cg.time ) {
			break;
		}

		cent = &cg_entities[ev->entityNum];
		if ( !cent->currentValid ) {
			if ( cg_debugEvents.integer ) {
				CG_Printf( S_COLOR_YELLOW "event %d of entity %d: the entity isn't there\n", ev->event, ev->entityNum );
			}
			continue;
		}
		if ( cg_debugEvents.integer ) {
			CG_Printf( "%i: entity event %d of entity %d (%d bytes, sent at %d)\n", cg.time, ev->event, ev->entityNum, ev->size, ev->time );
		}
		BitMsg_Init( &msg, ev->params, ARRAY_LEN( ev->params ) );
		msg.maxBits = ev->size * 8;
		if ( !CG_EntityReceiveEvent( cent, ev->event, ev->time, &msg ) && cg_debugEvents.integer ) {
			CG_Printf( S_COLOR_YELLOW "event %d of entity %d: unknown event\n", ev->event, ev->entityNum );
		}
	}

	// the rest move up
	for ( i = n; i < cg_numQueuedEvents; i++ ) {
		cg_eventQueue[i - n] = cg_eventQueue[i];
	}
	cg_numQueuedEvents -= n;
}
