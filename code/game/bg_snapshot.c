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
// ReadFromSnapshot): what an entity's class sends of itself in the game's
// state of a snapshot, written through a bit message delta (idBitMsgDelta)
// against the client's base (g_snapshot.c, cg_snapshot.c); an entity at rest
// sends nothing.

#include "../idlib/q_shared.h"
#include "bg_public.h"
#include "bg_af.h"

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
		q[i] = BitMsg_ReadBits( msg, -AF_QUAT_BITS ) / max;
	}
	d = 1.0f - q[0] * q[0] - q[1] * q[1] - q[2] * q[2];
	q[3] = d > 0.0f ? sqrt( d ) : 0.0f;
}

#define AF_ORIGIN_BITS		22		// an origin's x, y and z each, signed...
#define AF_ORIGIN_SCALE		16.0f	// ...in 16ths of a unit

/*
================
BG_WriteAFSnapshot

idPhysics_AF::WriteToSnapshot: what it is, the number of bodies, and each
body's origin and orientation, against the client's base (delta)
================
*/
void BG_WriteAFSnapshot( const afFigure_t *af, bitMsgDelta_t *delta ) {
	const afPhysics_t *phys = &af->physics;
	float max = (float)( ( 1 << ( AF_QUAT_BITS - 1 ) ) - 1 ), q[4];
	int i, k;

	BitMsgDelta_WriteBits( delta, SNAPSHOT_AF, 8 );
	BitMsgDelta_WriteBits( delta, phys->numBodies, 6 );
	for ( i = 0; i < phys->numBodies; i++ ) {
		for ( k = 0; k < 3; k++ ) {
			BitMsgDelta_WriteBits( delta, (int)floor( phys->bodies[i].current->worldOrigin[k] * AF_ORIGIN_SCALE + 0.5f ), -AF_ORIGIN_BITS );
		}
		AF_AxisToQuat( (const vec3_t *)phys->bodies[i].current->worldAxis, q );
		for ( k = 0; k < 3; k++ ) {
			BitMsgDelta_WriteBits( delta, (int)floor( q[k] * max + 0.5f ), -AF_QUAT_BITS );
		}
	}
}

/*
================
BG_DecodeEntitySnapshot

an entity's state read through the delta (the client's base), as its
class wrote it: its whole into the delta's new base; qfalse if it isn't
something known or the message ran out
================
*/
qboolean BG_DecodeEntitySnapshot( bitMsgDelta_t *delta ) {
	int i, k, n;

	switch ( BitMsgDelta_ReadBits( delta, 8 ) ) {
	case SNAPSHOT_AF:
		n = BitMsgDelta_ReadBits( delta, 6 );
		for ( i = 0; i < n; i++ ) {
			for ( k = 0; k < 3; k++ ) {
				BitMsgDelta_ReadBits( delta, -AF_ORIGIN_BITS );
			}
			for ( k = 0; k < 3; k++ ) {
				BitMsgDelta_ReadBits( delta, -AF_QUAT_BITS );
			}
		}
		break;
	default:
		return qfalse;
	}
	return !delta->msg->overflowed;
}

static void AF_ReadOrigin( bitMsg_t *msg, vec3_t origin ) {
	int k;

	for ( k = 0; k < 3; k++ ) {
		origin[k] = BitMsg_ReadBits( msg, -AF_ORIGIN_BITS ) / AF_ORIGIN_SCALE;
	}
}

/*
================
BG_ReadAFSnapshot

idPhysics_AF::ReadFromSnapshot: the figure's bodies where the state says,
between it and next (NULL: none) by frac; qfalse when it isn't a figure of
as many bodies
================
*/
qboolean BG_ReadAFSnapshot( afFigure_t *af, const entitySnapshot_t *state, const entitySnapshot_t *next, float frac ) {
	afPhysics_t *phys = &af->physics;
	float q[4], q2[4], d;
	vec3_t o, o2;
	bitMsg_t msg, msg2;
	int i, k, n;

	BitMsg_Init( &msg, (int *)state->words, ( state->numBits + 31 ) / 32 );
	if ( BitMsg_ReadBits( &msg, 8 ) != SNAPSHOT_AF ) {
		return qfalse;
	}
	n = BitMsg_ReadBits( &msg, 6 );
	if ( n != phys->numBodies ) {
		return qfalse;
	}
	if ( next ) {
		BitMsg_Init( &msg2, (int *)next->words, ( next->numBits + 31 ) / 32 );
		if ( BitMsg_ReadBits( &msg2, 8 ) != SNAPSHOT_AF || BitMsg_ReadBits( &msg2, 6 ) != n ) {
			next = NULL;
		}
	}

	for ( i = 0; i < n; i++ ) {
		AF_ReadOrigin( &msg, o );
		AF_ReadQuat( &msg, q );
		if ( next ) {
			AF_ReadOrigin( &msg2, o2 );
			for ( k = 0; k < 3; k++ ) {
				o[k] += frac * ( o2[k] - o[k] );
			}
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
		VectorCopy( o, phys->bodies[i].current->worldOrigin );
		AF_QuatToAxis( q, phys->bodies[i].current->worldAxis );
	}
	return !msg.overflowed;
}
