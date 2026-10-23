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

/*
===============================================================================

	idBitMsgDelta

===============================================================================
*/

void BitMsgDelta_Init( bitMsgDelta_t *delta, bitMsg_t *base, bitMsg_t *newBase, bitMsg_t *msg ) {
	delta->base = base;
	delta->newBase = newBase;
	delta->msg = msg;
	delta->changed = qfalse;
}

/*
================
BitMsgDelta_WriteBits

the value into the new base, and into the message: as a bit where it is the
base's, else a bit and the value
================
*/
void BitMsgDelta_WriteBits( bitMsgDelta_t *delta, int value, int numBits ) {
	int baseValue, n = numBits < 0 ? -numBits : numBits;
	unsigned int mask = n >= 32 ? 0xffffffffu : ( 1u << n ) - 1;

	if ( delta->newBase ) {
		BitMsg_WriteBits( delta->newBase, value, numBits );
	}
	if ( !delta->base ) {
		BitMsg_WriteBits( delta->msg, value, numBits );
		delta->changed = qtrue;
		return;
	}
	baseValue = BitMsg_ReadBits( delta->base, numBits );
	if ( !delta->base->overflowed && ( (unsigned int)baseValue & mask ) == ( (unsigned int)value & mask ) ) {
		BitMsg_WriteBits( delta->msg, 0, 1 );
		return;
	}
	BitMsg_WriteBits( delta->msg, 1, 1 );
	BitMsg_WriteBits( delta->msg, value, numBits );
	delta->changed = qtrue;
}

/*
================
BitMsgDelta_ReadBits

the value, the base's where the message says it's unchanged; into the new
base too
================
*/
int BitMsgDelta_ReadBits( bitMsgDelta_t *delta, int numBits ) {
	int value, baseValue;

	if ( !delta->base ) {
		value = BitMsg_ReadBits( delta->msg, numBits );
		delta->changed = qtrue;
	} else {
		baseValue = BitMsg_ReadBits( delta->base, numBits );
		if ( BitMsg_ReadBits( delta->msg, 1 ) ) {
			value = BitMsg_ReadBits( delta->msg, numBits );
			delta->changed = qtrue;
		} else {
			value = baseValue;
		}
	}
	if ( delta->newBase ) {
		BitMsg_WriteBits( delta->newBase, value, numBits );
	}
	return value;
}
