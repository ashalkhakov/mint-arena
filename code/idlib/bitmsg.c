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
// bitmsg.c -- Doom 3's idBitMsg (idlib/BitMsg): values packed bit by bit
// into an array of words

#include "q_shared.h"
#include "bitmsg.h"

void BitMsg_Init( bitMsg_t *msg, int *words, int numWords ) {
	msg->data = words;
	msg->maxBits = numWords * 32;
	msg->bit = 0;
	msg->overflowed = qfalse;
}

/*
================
BitMsg_WriteBits

the low numBits of value; numBits < 0 writes -numBits of a signed value
================
*/
void BitMsg_WriteBits( bitMsg_t *msg, int value, int numBits ) {
	unsigned int v;
	int i, word, bit;

	if ( numBits < 0 ) {
		numBits = -numBits;
	}
	if ( numBits <= 0 || numBits > 32 ) {
		return;
	}
	if ( msg->bit + numBits > msg->maxBits ) {
		msg->overflowed = qtrue;
		return;
	}
	v = (unsigned int)value;
	for ( i = 0; i < numBits; i++, msg->bit++ ) {
		word = msg->bit >> 5;
		bit = msg->bit & 31;
		if ( v & ( 1u << i ) ) {
			msg->data[word] |= ( 1u << bit );
		} else {
			msg->data[word] &= ~( 1u << bit );
		}
	}
}

int BitMsg_ReadBits( bitMsg_t *msg, int numBits ) {
	unsigned int v = 0;
	qboolean sign = qfalse;
	int i;

	if ( numBits < 0 ) {
		numBits = -numBits;
		sign = qtrue;
	}
	if ( numBits <= 0 || numBits > 32 || msg->bit + numBits > msg->maxBits ) {
		msg->overflowed = qtrue;
		return 0;
	}
	for ( i = 0; i < numBits; i++, msg->bit++ ) {
		if ( (unsigned int)msg->data[msg->bit >> 5] & ( 1u << ( msg->bit & 31 ) ) ) {
			v |= ( 1u << i );
		}
	}
	if ( sign && numBits < 32 && ( v & ( 1u << ( numBits - 1 ) ) ) ) {
		v |= ~( ( 1u << numBits ) - 1 );		// sign extend
	}
	return (int)v;
}

void BitMsg_WriteFloat( bitMsg_t *msg, float f ) {
	floatint_t fi;

	fi.f = f;
	BitMsg_WriteBits( msg, fi.i, 32 );
}

float BitMsg_ReadFloat( bitMsg_t *msg ) {
	floatint_t fi;

	fi.i = BitMsg_ReadBits( msg, 32 );
	return fi.f;
}
