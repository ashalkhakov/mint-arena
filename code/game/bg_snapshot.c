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
// bg_snapshot.c -- Doom 3's entity snapshots (WriteToSnapshot and
// ReadFromSnapshot): what an entity's class sends of itself, written as a
// bit message (idBitMsg) into its entityState_t's snapshot words. The engine
// delta compresses the words; an entity at rest sends nothing.

#include "../idlib/q_shared.h"
#include "bg_public.h"
#include "bg_af.h"

/*
===============================================================================

	idBitMsg

===============================================================================
*/

void BG_BitMsgInit( bitMsg_t *msg, int *words, int numWords ) {
	msg->data = words;
	msg->maxBits = numWords * 32;
	msg->bit = 0;
	msg->overflowed = qfalse;
}

/*
================
BG_WriteBits

the low numBits of value; numBits < 0 writes -numBits of a signed value
================
*/
void BG_WriteBits( bitMsg_t *msg, int value, int numBits ) {
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

int BG_ReadBits( bitMsg_t *msg, int numBits ) {
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

void BG_WriteFloat( bitMsg_t *msg, float f ) {
	floatint_t fi;

	fi.f = f;
	BG_WriteBits( msg, fi.i, 32 );
}

float BG_ReadFloat( bitMsg_t *msg ) {
	floatint_t fi;

	fi.i = BG_ReadBits( msg, 32 );
	return fi.f;
}

/*
===============================================================================

	Articulated figures

===============================================================================
*/

#define AF_QUAT_BITS		10		// a quaternion's x, y and z each; w from them

// rows: the axes; x y z w with w >= 0
static void AF_AxisToQuat( const vec3_t axis[3], float q[4] ) {
	vec3_t m[3];
	float t, s;
	int i, j, k;
	static const int next[3] = { 1, 2, 0 };

	// the rotation matrix with the axes as columns
	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			m[i][j] = axis[j][i];
		}
	}
	t = m[0][0] + m[1][1] + m[2][2];
	if ( t > 0.0f ) {
		s = 0.5f / sqrt( t + 1.0f );
		q[3] = 0.25f / s;
		q[0] = ( m[2][1] - m[1][2] ) * s;
		q[1] = ( m[0][2] - m[2][0] ) * s;
		q[2] = ( m[1][0] - m[0][1] ) * s;
	} else {
		i = 0;
		if ( m[1][1] > m[0][0] ) {
			i = 1;
		}
		if ( m[2][2] > m[i][i] ) {
			i = 2;
		}
		j = next[i];
		k = next[j];
		s = sqrt( ( m[i][i] - ( m[j][j] + m[k][k] ) ) + 1.0f );
		q[i] = s * 0.5f;
		s = 0.5f / s;
		q[3] = ( m[k][j] - m[j][k] ) * s;
		q[j] = ( m[j][i] + m[i][j] ) * s;
		q[k] = ( m[k][i] + m[i][k] ) * s;
	}
	if ( q[3] < 0.0f ) {
		q[0] = -q[0]; q[1] = -q[1]; q[2] = -q[2]; q[3] = -q[3];
	}
}

static void AF_QuatToAxis( const float q[4], vec3_t axis[3] ) {
	float x = q[0], y = q[1], z = q[2], w = q[3];

	// columns of the rotation matrix are the axes
	axis[0][0] = 1 - 2 * ( y * y + z * z );	axis[1][0] = 2 * ( x * y - w * z );		axis[2][0] = 2 * ( x * z + w * y );
	axis[0][1] = 2 * ( x * y + w * z );		axis[1][1] = 1 - 2 * ( x * x + z * z );	axis[2][1] = 2 * ( y * z - w * x );
	axis[0][2] = 2 * ( x * z - w * y );		axis[1][2] = 2 * ( y * z + w * x );		axis[2][2] = 1 - 2 * ( x * x + y * y );
}

static void AF_ReadQuat( bitMsg_t *msg, float q[4] ) {
	float max = (float)( ( 1 << ( AF_QUAT_BITS - 1 ) ) - 1 ), d;
	int i;

	for ( i = 0; i < 3; i++ ) {
		q[i] = BG_ReadBits( msg, -AF_QUAT_BITS ) / max;
	}
	d = 1.0f - q[0] * q[0] - q[1] * q[1] - q[2] * q[2];
	q[3] = d > 0.0f ? sqrt( d ) : 0.0f;
}

/*
================
BG_WriteAFSnapshot

The type, the number of bodies and each body's orientation; es->pos is the
first body's origin (the caller's TR_INTERPOLATE).
================
*/
void BG_WriteAFSnapshot( const afFigure_t *af, entityState_t *es ) {
	const afPhysics_t *phys = &af->physics;
	float max = (float)( ( 1 << ( AF_QUAT_BITS - 1 ) ) - 1 ), q[4];
	bitMsg_t msg;
	int i, k;

	memset( es->snapshot, 0, sizeof( es->snapshot ) );
	BG_BitMsgInit( &msg, es->snapshot, ES_SNAPSHOT_WORDS );
	BG_WriteBits( &msg, SNAPSHOT_AF, 8 );
	BG_WriteBits( &msg, phys->numBodies, 6 );
	for ( i = 0; i < phys->numBodies; i++ ) {
		AF_AxisToQuat( (const vec3_t *)phys->bodies[i].current->worldAxis, q );
		for ( k = 0; k < 3; k++ ) {
			BG_WriteBits( &msg, (int)floor( q[k] * max + 0.5f ), -AF_QUAT_BITS );
		}
	}
	VectorCopy( phys->bodies[0].current->worldOrigin, es->pos.trBase );
}

/*
================
BG_ReadAFSnapshot

The figure's bodies turned as the snapshot says (between es and next by
frac, next may be NULL), its first body at origin; qfalse when it isn't a
figure of as many bodies.
================
*/
qboolean BG_ReadAFSnapshot( afFigure_t *af, const entityState_t *es, const entityState_t *next, float frac, const vec3_t origin ) {
	afPhysics_t *phys = &af->physics;
	float q[4], q2[4], d;
	bitMsg_t msg, msg2;
	int i, k, n;

	BG_BitMsgInit( &msg, (int *)es->snapshot, ES_SNAPSHOT_WORDS );
	if ( BG_ReadBits( &msg, 8 ) != SNAPSHOT_AF ) {
		return qfalse;
	}
	n = BG_ReadBits( &msg, 6 );
	if ( n != phys->numBodies ) {
		return qfalse;
	}
	if ( next ) {
		BG_BitMsgInit( &msg2, (int *)next->snapshot, ES_SNAPSHOT_WORDS );
		if ( BG_ReadBits( &msg2, 8 ) != SNAPSHOT_AF || BG_ReadBits( &msg2, 6 ) != n ) {
			next = NULL;
		}
	}

	for ( i = 0; i < n; i++ ) {
		AF_ReadQuat( &msg, q );
		if ( next ) {
			// the shorter way, normalized
			AF_ReadQuat( &msg2, q2 );
			if ( q[0] * q2[0] + q[1] * q2[1] + q[2] * q2[2] + q[3] * q2[3] < 0.0f ) {
				for ( k = 0; k < 4; k++ ) {
					q2[k] = -q2[k];
				}
			}
			for ( k = 0; k < 4; k++ ) {
				q[k] += frac * ( q2[k] - q[k] );
			}
			d = sqrt( q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3] );
			if ( d > 0.0f ) {
				for ( k = 0; k < 4; k++ ) {
					q[k] /= d;
				}
			}
		}
		AF_QuatToAxis( q, phys->bodies[i].current->worldAxis );
	}
	VectorCopy( origin, phys->bodies[0].current->worldOrigin );
	return !msg.overflowed;
}
