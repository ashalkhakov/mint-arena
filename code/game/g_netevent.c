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
// g_netevent.c -- Doom 3's entity network events (idEntity::ServerSendEvent)
//
// An entity's event with parameters (a bit message of up to
// MAX_EVENT_PARAM_SIZE bytes), sent reliably with the time it happened; the
// clients run it when their time gets there (cg_netevent.c). Saved events
// (state a late joiner needs, as a door's) go again to whoever begins, until
// their entity is freed. Quake 3's EV_ events stay for what may be lost.

#include "g_local.h"

#define MAX_SAVED_EVENTS		256

typedef struct {
	int		entityNum;
	int		event;
	int		time;
	int		size;
	byte	params[MAX_EVENT_PARAM_SIZE];
} savedEvent_t;

static savedEvent_t		g_savedEvents[MAX_SAVED_EVENTS];
static int				g_numSavedEvents;

/*
================
G_EventCommand

"eev <entity> <event> <time> <params as hex>"
================
*/
static char *G_EventCommand( int entityNum, int event, int time, const byte *params, int size ) {
	static char cmd[64 + MAX_EVENT_PARAM_SIZE * 2];
	static const char hex[] = "0123456789abcdef";
	char *p;
	int i;

	Com_sprintf( cmd, sizeof( cmd ), "eev %d %d %d ", entityNum, event, time );
	p = cmd + strlen( cmd );
	for ( i = 0; i < size; i++ ) {
		*p++ = hex[params[i] >> 4];
		*p++ = hex[params[i] & 15];
	}
	if ( !size ) {
		*p++ = '-';
	}
	*p = '\0';
	return cmd;
}

/*
================
G_ServerSendEvent

idEntity::ServerSendEvent: msg may be NULL; excludePlayer -1 for none
================
*/
void G_ServerSendEvent( const gentity_t *ent, int event, const bitMsg_t *msg, qboolean saveEvent, int excludePlayer ) {
	byte params[MAX_EVENT_PARAM_SIZE];
	char *cmd;
	int size = 0, i;

	if ( msg ) {
		size = ( msg->bit + 7 ) >> 3;
		if ( size > MAX_EVENT_PARAM_SIZE || msg->overflowed ) {
			G_Printf( S_COLOR_YELLOW "G_ServerSendEvent: event %d of entity %d has too many parameters\n", event, ent->s.number );
			return;
		}
		for ( i = 0; i < size; i++ ) {
			params[i] = ( (unsigned int)msg->data[i >> 2] >> ( ( i & 3 ) * 8 ) ) & 0xff;
		}
	}

	cmd = G_EventCommand( ent->s.number, event, level.time, params, size );
	if ( excludePlayer < 0 ) {
		trap_SendServerCommand( -1, cmd );
	} else {
		for ( i = 0; i < level.maxplayers; i++ ) {
			if ( i != excludePlayer && level.players[i].pers.connected == CON_CONNECTED ) {
				trap_SendServerCommand( i, cmd );
			}
		}
	}

	if ( saveEvent ) {
		savedEvent_t *saved;

		if ( g_numSavedEvents == MAX_SAVED_EVENTS ) {
			// the oldest goes
			memmove( g_savedEvents, g_savedEvents + 1, ( MAX_SAVED_EVENTS - 1 ) * sizeof( g_savedEvents[0] ) );
			g_numSavedEvents--;
		}
		saved = &g_savedEvents[g_numSavedEvents++];
		saved->entityNum = ent->s.number;
		saved->event = event;
		saved->time = level.time;
		saved->size = size;
		memcpy( saved->params, params, size );
	}
}

/*
================
G_SendSavedEvents

idGameLocal::ServerWriteInitialReliableMessages's events: to a player beginning
================
*/
void G_SendSavedEvents( int playerNum ) {
	int i;

	for ( i = 0; i < g_numSavedEvents; i++ ) {
		trap_SendServerCommand( playerNum, G_EventCommand( g_savedEvents[i].entityNum, g_savedEvents[i].event,
														  g_savedEvents[i].time, g_savedEvents[i].params, g_savedEvents[i].size ) );
	}
}

/*
================
G_RemoveEntityEvents

an entity freed: its saved events go
================
*/
void G_RemoveEntityEvents( const gentity_t *ent ) {
	int i, n;

	for ( i = n = 0; i < g_numSavedEvents; i++ ) {
		if ( g_savedEvents[i].entityNum != ent->s.number ) {
			g_savedEvents[n++] = g_savedEvents[i];
		}
	}
	g_numSavedEvents = n;
}

void G_ClearSavedEvents( void ) {
	g_numSavedEvents = 0;
}

/*
================
G_SendSoundEvent

Doom 3's EVENT_STARTSOUNDSHADER: a sound shader (bg_sound.c) played on an
entity's channel for everyone, at volume (0 to 1) of its own
================
*/
void G_SendSoundEvent( const gentity_t *ent, int channel, const char *soundShader, float volume ) {
	const bgSoundShader_t *shader = BG_FindSoundShader( soundShader );
	int params[MAX_EVENT_PARAM_SIZE / 4];
	bitMsg_t msg;

	if ( !shader ) {
		return;
	}
	memset( params, 0, sizeof( params ) );
	BG_BitMsgInit( &msg, params, ARRAY_LEN( params ) );
	BG_WriteBits( &msg, shader - bg_soundShaders, 12 );
	BG_WriteBits( &msg, channel, 4 );
	BG_WriteBits( &msg, (int)( Com_Clamp( 0, 1, volume ) * 255 ), 8 );
	G_ServerSendEvent( ent, ENTEV_STARTSOUND, &msg, qfalse, -1 );
}
