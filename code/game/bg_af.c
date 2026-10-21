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
// bg_af.c -- articulated figures, a port of Doom 3's idPhysics_AF
// (game/physics/Physics_AF.cpp)
//
// The constraints a ragdoll is made of: fixed, ball and socket, universal
// and hinge joints with their friction, cone and pyramid limits, contacts
// with contact friction. Vectors and matrices keep Doom 3's meaning: "v * M"
// is VectorMat (v[0]*M[0] + v[1]*M[1] + v[2]*M[2], a body's axes' rows), "M * v"
// is MatVector (each row dotted with v).

#include "../idlib/q_shared.h"
#include "bg_public.h"
#include "bg_af.h"
#include "../idlib/lcp.h"

#define ERROR_REDUCTION					0.5f
#define ERROR_REDUCTION_MAX				256.0f
#define LIMIT_ERROR_REDUCTION			0.3f
#define LCP_EPSILON						1e-7f
#define LIMIT_LCP_EPSILON				1e-4f
#define CONTACT_LCP_EPSILON				1e-6f
#define CENTER_OF_MASS_EPSILON			1e-4f
#define NO_MOVE_TIME					1.0f
#define NO_MOVE_TRANSLATION_TOLERANCE	10.0f
#define NO_MOVE_ROTATION_TOLERANCE		10.0f
#define MIN_MOVE_TIME					-1.0f
#define MAX_MOVE_TIME					-1.0f
#define IMPULSE_THRESHOLD				500.0f
#define SUSPEND_LINEAR_VELOCITY			10.0f
#define SUSPEND_ANGULAR_VELOCITY		15.0f
#define SUSPEND_LINEAR_ACCELERATION		20.0f
#define SUSPEND_ANGULAR_ACCELERATION	30.0f

// Doom 3's cvars, as constants
#define AF_MAX_LINEAR_VELOCITY			128.0f		// af_maxLinearVelocity, per step
#define AF_MAX_ANGULAR_VELOCITY			1.57f		// af_maxAngularVelocity, per step
#define AF_USE_SYMMETRY					1			// af_useSymmetry
#define AF_INFINITY						1e30f
#define MS2SEC( t )						( (t) * 0.001f )

#define AF_MAX_FRAME_CONSTRAINTS		256
#define AF_MAX_AUX_ROWS					384

// the frame constraints of the figure being evaluated: contacts, their
// friction, joint friction and limits; made anew each step
static afConstraint_t	af_framePool[AF_MAX_FRAME_CONSTRAINTS];
static int				af_numFramePool;
static afConstraint_t	*af_frameConstraints[AF_MAX_FRAME_CONSTRAINTS];
static int				af_numFrameConstraints;
static afConstraint_t	*af_contactConstraints[AF_MAX_CONTACTS];
static int				af_numContactConstraints;
// the auxiliary constraints with the frame constraints added
static afConstraint_t	*af_aux[AF_MAX_CONSTRAINTS + AF_MAX_FRAME_CONSTRAINTS];
static int				af_numAux;

// the auxiliary forces' work space
static float			af_responseBuf[AF_MAX_BODIES * AF_MAX_AUX_ROWS * 8];
static int				af_responseIndexBuf[AF_MAX_BODIES * AF_MAX_AUX_ROWS];
static float			af_jmk[AF_MAX_AUX_ROWS * AF_MAX_AUX_ROWS];
static float			af_rhs[AF_MAX_AUX_ROWS], af_lo[AF_MAX_AUX_ROWS], af_hi[AF_MAX_AUX_ROWS], af_lm[AF_MAX_AUX_ROWS];
static int				af_boxIndex[AF_MAX_AUX_ROWS];

typedef struct {
	cm_trace_t	trace;
	afBody_t	*body;
} afCollision_t;

static afCollision_t	af_collisions[AF_MAX_BODIES];
static int				af_numCollisions;

/*
===============================================================================

	Doom 3's vector and matrix operations

===============================================================================
*/

// v * M
static ID_INLINE void VectorMat( const vec3_t v, const vec3_t m[3], vec3_t out ) {
	vec3_t t;

	t[0] = m[0][0] * v[0] + m[1][0] * v[1] + m[2][0] * v[2];
	t[1] = m[0][1] * v[0] + m[1][1] * v[1] + m[2][1] * v[2];
	t[2] = m[0][2] * v[0] + m[1][2] * v[1] + m[2][2] * v[2];
	VectorCopy( t, out );
}

// v * M'
static ID_INLINE void VectorMatT( const vec3_t v, const vec3_t m[3], vec3_t out ) {
	vec3_t t;

	t[0] = DotProduct( m[0], v );
	t[1] = DotProduct( m[1], v );
	t[2] = DotProduct( m[2], v );
	VectorCopy( t, out );
}

// M * v
#define MatVector( m, v, out )		VectorMatT( v, m, out )

static ID_INLINE void Mat3Mul( const vec3_t a[3], const vec3_t b[3], vec3_t out[3] ) {
	vec3_t t[3];
	int i, j;

	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			t[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
		}
	}
	AxisCopy( t, out );
}

// a' * b
static ID_INLINE void Mat3TransposeMul( const vec3_t a[3], const vec3_t b[3], vec3_t out[3] ) {
	vec3_t t[3];

	TransposeAxis( a, t );
	Mat3Mul( (const vec3_t *)t, b, out );
}

// a * b'
static ID_INLINE void Mat3MulTranspose( const vec3_t a[3], const vec3_t b[3], vec3_t out[3] ) {
	vec3_t t[3];

	TransposeAxis( b, t );
	Mat3Mul( a, (const vec3_t *)t, out );
}

static ID_INLINE void Mat3Identity( vec3_t m[3] ) {
	VectorSet( m[0], 1, 0, 0 );
	VectorSet( m[1], 0, 1, 0 );
	VectorSet( m[2], 0, 0, 1 );
}

static ID_INLINE void SkewSymmetric( const vec3_t v, vec3_t out[3] ) {
	VectorSet( out[0], 0.0f, -v[2], v[1] );
	VectorSet( out[1], v[2], 0.0f, -v[0] );
	VectorSet( out[2], -v[1], v[0], 0.0f );
}

// axis' * inertia * axis: a body's tensor in world space
static void WorldTensor( const vec3_t axis[3], const vec3_t tensor[3], vec3_t out[3] ) {
	vec3_t t[3];

	Mat3TransposeMul( axis, tensor, t );
	Mat3Mul( (const vec3_t *)t, axis, out );
}

// idQuat::ToMat3 of ( v * s, c )
static void QuatToMat3( const vec3_t v, float s, float c, vec3_t mat[3] ) {
	float x = v[0] * s, y = v[1] * s, z = v[2] * s, w = c;
	float x2 = x + x, y2 = y + y, z2 = z + z;
	float xx = x * x2, xy = x * y2, xz = x * z2;
	float yy = y * y2, yz = y * z2, zz = z * z2;
	float wx = w * x2, wy = w * y2, wz = w * z2;

	mat[0][0] = 1.0f - ( yy + zz );
	mat[0][1] = xy - wz;
	mat[0][2] = xz + wy;

	mat[1][0] = xy + wz;
	mat[1][1] = 1.0f - ( xx + zz );
	mat[1][2] = yz - wx;

	mat[2][0] = xz - wy;
	mat[2][1] = yz + wx;
	mat[2][2] = 1.0f - ( xx + yy );
}

static ID_INLINE qboolean Mat3IsDiagonal( const vec3_t m[3], float epsilon ) {
	return fabs( m[0][1] ) <= epsilon && fabs( m[0][2] ) <= epsilon && fabs( m[1][0] ) <= epsilon
		&& fabs( m[1][2] ) <= epsilon && fabs( m[2][0] ) <= epsilon && fabs( m[2][1] ) <= epsilon;
}

static ID_INLINE float Vec6LengthSqr3( const float *v ) {
	return v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
}

static ID_INLINE void VecXClamp( afVec_t *v, float lo, float hi ) {
	int i;

	for ( i = 0; i < v->size; i++ ) {
		if ( v->v[i] < lo ) {
			v->v[i] = lo;
		} else if ( v->v[i] > hi ) {
			v->v[i] = hi;
		}
	}
}

static ID_INLINE void VecXZero( afVec_t *v, int size ) {
	v->size = size;
	memset( v->v, 0, sizeof( v->v ) );
}

static ID_INLINE float VecXLength( const afVec_t *v ) {
	float sum = 0.0f;
	int i;

	for ( i = 0; i < v->size; i++ ) {
		sum += v->v[i] * v->v[i];
	}
	return sqrt( sum );
}

static ID_INLINE void MatXZero( afMat_t *m, int rows, int cols ) {
	m->rows = rows;
	m->cols = cols;
	memset( m->m, 0, sizeof( m->m ) );
}

// idMatX::Set( m1, m2 ): 3x6
static void MatXSet2( afMat_t *m, const vec3_t m1[3], const vec3_t m2[3] ) {
	int i, j;

	m->rows = 3;
	m->cols = 6;
	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			m->m[i][j] = m1[i][j];
			m->m[i][j + 3] = m2[i][j];
		}
	}
}

// idMatX::Set( m1, m2, m3, m4 ): 6x6
static void MatXSet4( afMat_t *m, const vec3_t m1[3], const vec3_t m2[3], const vec3_t m3[3], const vec3_t m4[3] ) {
	int i, j;

	m->rows = 6;
	m->cols = 6;
	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			m->m[i][j] = m1[i][j];
			m->m[i][j + 3] = m2[i][j];
			m->m[i + 3][j] = m3[i][j];
			m->m[i + 3][j + 3] = m4[i][j];
		}
	}
}

// a row of 6: linear then angular
static ID_INLINE void MatXSetRow( afMat_t *m, int row, const vec3_t linear, const vec3_t angular ) {
	m->m[row][0] = linear[0]; m->m[row][1] = linear[1]; m->m[row][2] = linear[2];
	m->m[row][3] = angular[0]; m->m[row][4] = angular[1]; m->m[row][5] = angular[2];
}

// out = a * b
static void MatXMul( const afMat_t *a, const afMat_t *b, afMat_t *out ) {
	afMat_t t;
	int i, j, k;
	double sum;

	t.rows = a->rows;
	t.cols = b->cols;
	for ( i = 0; i < a->rows; i++ ) {
		for ( j = 0; j < b->cols; j++ ) {
			sum = 0.0;
			for ( k = 0; k < a->cols; k++ ) {
				sum += a->m[i][k] * b->m[k][j];
			}
			t.m[i][j] = sum;
		}
	}
	*out = t;
}

// out = a' * b
static void MatXTransposeMul( const afMat_t *a, const afMat_t *b, afMat_t *out ) {
	afMat_t t;
	int i, j, k;
	double sum;

	t.rows = a->cols;
	t.cols = b->cols;
	for ( i = 0; i < a->cols; i++ ) {
		for ( j = 0; j < b->cols; j++ ) {
			sum = 0.0;
			for ( k = 0; k < a->rows; k++ ) {
				sum += a->m[k][i] * b->m[k][j];
			}
			t.m[i][j] = sum;
		}
	}
	*out = t;
}

static void MatXTranspose( const afMat_t *a, afMat_t *out ) {
	afMat_t t;
	int i, j;

	t.rows = a->cols;
	t.cols = a->rows;
	for ( i = 0; i < a->rows; i++ ) {
		for ( j = 0; j < a->cols; j++ ) {
			t.m[j][i] = a->m[i][j];
		}
	}
	*out = t;
}

// dst = m * v, dst += m * v, dst -= m * v
static void MatXMulVec( const afMat_t *m, const float *v, float *dst, int op ) {
	float t[6];
	int i, j;
	double sum;

	for ( i = 0; i < m->rows; i++ ) {
		sum = 0.0;
		for ( j = 0; j < m->cols; j++ ) {
			sum += m->m[i][j] * v[j];
		}
		t[i] = sum;
	}
	for ( i = 0; i < m->rows; i++ ) {
		if ( op == 0 ) {
			dst[i] = t[i];
		} else if ( op > 0 ) {
			dst[i] += t[i];
		} else {
			dst[i] -= t[i];
		}
	}
}

// dst = m' * v, dst += m' * v, dst -= m' * v
static void MatXTransposeMulVec( const afMat_t *m, const float *v, float *dst, int op ) {
	float t[6];
	int i, j;
	double sum;

	for ( i = 0; i < m->cols; i++ ) {
		sum = 0.0;
		for ( j = 0; j < m->rows; j++ ) {
			sum += m->m[j][i] * v[j];
		}
		t[i] = sum;
	}
	for ( i = 0; i < m->cols; i++ ) {
		if ( op == 0 ) {
			dst[i] = t[i];
		} else if ( op > 0 ) {
			dst[i] += t[i];
		} else {
			dst[i] -= t[i];
		}
	}
}

#define MATX_SET	0
#define MATX_ADD	1
#define MATX_SUB	-1

// Gauss-Jordan with partial pivoting, idMatX::InverseFastSelf's job
static qboolean MatXInverseSelf( afMat_t *m ) {
	double a[6][12];
	int n = m->rows, i, j, k, pivot;
	double d, f;

	if ( n != m->cols ) {
		return qfalse;
	}
	for ( i = 0; i < n; i++ ) {
		for ( j = 0; j < n; j++ ) {
			a[i][j] = m->m[i][j];
			a[i][j + n] = ( i == j ) ? 1.0 : 0.0;
		}
	}
	for ( i = 0; i < n; i++ ) {
		pivot = i;
		for ( j = i + 1; j < n; j++ ) {
			if ( fabs( a[j][i] ) > fabs( a[pivot][i] ) ) {
				pivot = j;
			}
		}
		if ( fabs( a[pivot][i] ) < 1e-30 ) {
			return qfalse;
		}
		if ( pivot != i ) {
			for ( k = 0; k < 2 * n; k++ ) {
				d = a[i][k]; a[i][k] = a[pivot][k]; a[pivot][k] = d;
			}
		}
		d = 1.0 / a[i][i];
		for ( k = 0; k < 2 * n; k++ ) {
			a[i][k] *= d;
		}
		for ( j = 0; j < n; j++ ) {
			if ( j == i ) {
				continue;
			}
			f = a[j][i];
			if ( f == 0.0 ) {
				continue;
			}
			for ( k = 0; k < 2 * n; k++ ) {
				a[j][k] -= f * a[i][k];
			}
		}
	}
	for ( i = 0; i < n; i++ ) {
		for ( j = 0; j < n; j++ ) {
			m->m[i][j] = a[i][j + n];
		}
	}
	return qtrue;
}

// v = this->inverseWorldSpatialInertia * v
static void AF_InverseWorldSpatialInertiaMultiply( const afBody_t *body, float *dst, const float *v ) {
	const afMat_t *m = &body->inverseWorldSpatialInertia;
	float t[6];

	if ( body->spatialInertiaSparse ) {
		t[0] = m->m[0][0] * v[0];
		t[1] = m->m[1][1] * v[1];
		t[2] = m->m[2][2] * v[2];
		t[3] = m->m[3][3] * v[3] + m->m[3][4] * v[4] + m->m[3][5] * v[5];
		t[4] = m->m[4][3] * v[3] + m->m[4][4] * v[4] + m->m[4][5] * v[5];
		t[5] = m->m[5][3] * v[3] + m->m[5][4] * v[4] + m->m[5][5] * v[5];
	} else {
		MatXMulVec( m, v, t, MATX_SET );
	}
	memcpy( dst, t, sizeof( t ) );
}

/*
===============================================================================

	Constraints

===============================================================================
*/

static afBody_t *AF_MasterBody( const afConstraint_t *c ) {
	return c->body2;	// no master: a figure isn't bound to another entity
}

static void AF_ConstraintInit( afConstraint_t *c, afConstraintType_t type, const char *name, int size ) {
	int i;

	memset( c, 0, sizeof( *c ) );
	c->type = type;
	Q_strncpyz( c->name, name, sizeof( c->name ) );

	VecXZero( &c->lo, 6 );
	VecXZero( &c->hi, 6 );
	VecXZero( &c->e, 6 );
	for ( i = 0; i < 6; i++ ) {
		c->lo.v[i] = -AF_INFINITY;
		c->hi.v[i] = AF_INFINITY;
		c->e.v[i] = LCP_EPSILON;
		c->boxIndex[i] = -1;
	}
	c->boxConstraint = NULL;

	// InitSize
	MatXZero( &c->J1, size, 6 );
	MatXZero( &c->J2, size, 6 );
	VecXZero( &c->c1, size );
	VecXZero( &c->c2, size );
	VecXZero( &c->s, size );
	VecXZero( &c->lm, size );
	c->limit.epsilon = -1.0f;
}

static afConstraint_t *AF_NewFrameConstraint( afConstraintType_t type, const char *name, int size ) {
	afConstraint_t *c;

	if ( af_numFramePool >= AF_MAX_FRAME_CONSTRAINTS ) {
		return NULL;
	}
	c = &af_framePool[af_numFramePool++];
	AF_ConstraintInit( c, type, name, size );
	c->frameConstraint = qtrue;
	return c;
}

static void AF_AddFrameConstraint( afPhysics_t *af, afConstraint_t *c ) {
	if ( af_numFrameConstraints >= AF_MAX_FRAME_CONSTRAINTS ) {
		return;
	}
	af_frameConstraints[af_numFrameConstraints++] = c;
	c->physics = af;
}

static float AF_GetJointFrictionScale( const afPhysics_t *af ) {
	if ( af->jointFrictionScale > 0.0f ) {
		return af->jointFrictionScale;
	}
	return 1.0f;
}

static float AF_GetContactFrictionScale( const afPhysics_t *af ) {
	if ( af->contactFrictionScale > 0.0f ) {
		return af->contactFrictionScale;
	}
	return 1.0f;
}

static float AF_JointFriction( const afConstraint_t *c ) {
	return c->friction * AF_GetJointFrictionScale( c->physics );
}

/*
================
AF_SetAnchor

The joints' anchor, relative to each body.
================
*/
void AF_SetAnchor( afConstraint_t *c, const vec3_t worldPosition ) {
	vec3_t d;

	// get anchor relative to center of mass of body1
	VectorSubtract( worldPosition, c->body1->current->worldOrigin, d );
	VectorMatT( d, (const vec3_t *)c->body1->current->worldAxis, c->anchor1 );
	if ( c->body2 ) {
		// get anchor relative to center of mass of body2
		VectorSubtract( worldPosition, c->body2->current->worldOrigin, d );
		VectorMatT( d, (const vec3_t *)c->body2->current->worldAxis, c->anchor2 );
	} else {
		VectorCopy( worldPosition, c->anchor2 );
	}

	if ( c->limit.type != AF_LIMIT_NONE ) {
		VectorCopy( c->anchor2, c->limit.anchor );
	}
}

/*
================
AF_SetShafts

idAFConstraint_UniversalJoint::SetShafts
================
*/
void AF_SetShafts( afConstraint_t *c, const vec3_t cardanShaft1, const vec3_t cardanShaft2 ) {
	vec3_t cardanAxis, vecY;

	VectorCopy( cardanShaft1, c->shaft1 );
	VectorNormalize( c->shaft1 );
	VectorCopy( cardanShaft2, c->shaft2 );
	VectorNormalize( c->shaft2 );

	// the cardan axis is a vector orthogonal to both cardan shafts
	CrossProduct( c->shaft1, c->shaft2, cardanAxis );
	if ( VectorNormalize( cardanAxis ) == 0.0f ) {
		VectorOrthogonalBasis( c->shaft1, cardanAxis, vecY );
		VectorNormalize( cardanAxis );
	}

	VectorMatT( c->shaft1, (const vec3_t *)c->body1->current->worldAxis, c->shaft1 );
	VectorMatT( cardanAxis, (const vec3_t *)c->body1->current->worldAxis, c->axis1 );
	if ( c->body2 ) {
		VectorMatT( c->shaft2, (const vec3_t *)c->body2->current->worldAxis, c->shaft2 );
		VectorMatT( cardanAxis, (const vec3_t *)c->body2->current->worldAxis, c->axis2 );
	} else {
		VectorCopy( cardanAxis, c->axis2 );
	}

	if ( c->limit.type != AF_LIMIT_NONE ) {
		VectorCopy( c->shaft1, c->limit.body1Axis );
	}
}

/*
================
AF_SetHingeAxis
================
*/
void AF_SetHingeAxis( afConstraint_t *c, const vec3_t axis ) {
	vec3_t normAxis;

	VectorCopy( axis, normAxis );
	VectorNormalize( normAxis );

	// get axis relative to body1
	VectorMatT( normAxis, (const vec3_t *)c->body1->current->worldAxis, c->axis1 );
	if ( c->body2 ) {
		// get axis relative to body2
		VectorMatT( normAxis, (const vec3_t *)c->body2->current->worldAxis, c->axis2 );
	} else {
		VectorCopy( normAxis, c->axis2 );
	}
}

/*
================
AF_InitFixedOffset
================
*/
void AF_InitFixedOffset( afConstraint_t *c ) {
	vec3_t d;

	if ( c->body2 ) {
		VectorSubtract( c->body1->current->worldOrigin, c->body2->current->worldOrigin, d );
		VectorMatT( d, (const vec3_t *)c->body2->current->worldAxis, c->offset );
		Mat3MulTranspose( (const vec3_t *)c->body1->current->worldAxis, (const vec3_t *)c->body2->current->worldAxis, c->relAxis );
	} else {
		VectorCopy( c->body1->current->worldOrigin, c->offset );
		AxisCopy( c->body1->current->worldAxis, c->relAxis );
	}
}

/*
================
AF_SetConeLimit

The coneAxis (world space) is the axis of the cone, coneAngle the angle the
cone hull makes at the top, body1Axis (world space) the axis of body1 kept
within the cone. A hinge's limit is a cone too.
================
*/
void AF_SetConeLimit( afConstraint_t *c, const vec3_t coneAxis, float coneAngle, const vec3_t body1Axis ) {
	afLimit_t *l = &c->limit;

	l->type = AF_LIMIT_CONE;
	VectorCopy( c->anchor2, l->anchor );
	if ( c->body2 ) {
		VectorMatT( coneAxis, (const vec3_t *)c->body2->current->worldAxis, l->axis );
	} else {
		VectorCopy( coneAxis, l->axis );
	}
	VectorNormalize( l->axis );
	VectorMatT( body1Axis, (const vec3_t *)c->body1->current->worldAxis, l->body1Axis );
	VectorNormalize( l->body1Axis );
	l->cosAngle = cos( DEG2RAD( coneAngle * 0.5f ) );
	l->sinHalfAngle = sin( DEG2RAD( coneAngle * 0.25f ) );
	l->cosHalfAngle = cos( DEG2RAD( coneAngle * 0.25f ) );
}

/*
================
AF_SetPyramidLimit
================
*/
void AF_SetPyramidLimit( afConstraint_t *c, const vec3_t pyramidAxis, const vec3_t baseAxis,
						float angle1, float angle2, const vec3_t body1Axis ) {
	afLimit_t *l = &c->limit;
	vec3_t axis, base;
	float d;

	l->type = AF_LIMIT_PYRAMID;
	VectorCopy( c->anchor2, l->anchor );
	if ( c->body2 ) {
		VectorMatT( pyramidAxis, (const vec3_t *)c->body2->current->worldAxis, axis );
		VectorMatT( baseAxis, (const vec3_t *)c->body2->current->worldAxis, base );
	} else {
		VectorCopy( pyramidAxis, axis );
		VectorCopy( baseAxis, base );
	}

	// setup the base and make sure the basis is orthonormal
	VectorCopy( axis, l->basis[2] );
	VectorNormalize( l->basis[2] );
	d = DotProduct( l->basis[2], base );
	VectorMA( base, -d, l->basis[2], l->basis[0] );
	VectorNormalize( l->basis[0] );
	CrossProduct( l->basis[0], l->basis[2], l->basis[1] );

	l->cosAngles[0] = cos( DEG2RAD( angle1 * 0.5f ) );
	l->cosAngles[1] = cos( DEG2RAD( angle2 * 0.5f ) );
	l->sinHalfAngles[0] = sin( DEG2RAD( angle1 * 0.25f ) );
	l->sinHalfAngles[1] = sin( DEG2RAD( angle2 * 0.25f ) );
	l->cosHalfAngles[0] = cos( DEG2RAD( angle1 * 0.25f ) );
	l->cosHalfAngles[1] = cos( DEG2RAD( angle2 * 0.25f ) );

	VectorMatT( body1Axis, (const vec3_t *)c->body1->current->worldAxis, l->body1Axis );
}

void AF_SetNoLimit( afConstraint_t *c ) {
	c->limit.type = AF_LIMIT_NONE;
}

/*
================
AF_ConeLimitAdd

idAFConstraint_ConeLimit::Add
================
*/
static void AF_ConeLimitAdd( afPhysics_t *af, afConstraint_t *joint, float invTimeStep ) {
	const afLimit_t *l = &joint->limit;
	afConstraint_t *c;
	afBody_t *body1 = joint->body1, *body2 = joint->body2, *master;
	vec3_t ax, anchor, body1ax, normal, coneVector, p1, p2, t, q[3];
	float a;

	master = body2;

	if ( master ) {
		VectorMat( l->axis, (const vec3_t *)master->current->worldAxis, ax );
		VectorMat( l->anchor, (const vec3_t *)master->current->worldAxis, t );
		VectorAdd( master->current->worldOrigin, t, anchor );
	} else {
		VectorCopy( l->axis, ax );
		VectorCopy( l->anchor, anchor );
	}

	VectorMat( l->body1Axis, (const vec3_t *)body1->current->worldAxis, body1ax );

	a = DotProduct( ax, body1ax );

	// if the body1 axis is inside the cone
	if ( a > l->cosAngle ) {
		return;		// constraint exerts no force
	}

	c = AF_NewFrameConstraint( CONSTRAINT_CONELIMIT, "coneLimit", 1 );
	if ( !c ) {
		return;
	}
	c->body1 = body1;
	c->body2 = body2;
	c->joint = joint;

	// calculate the inward cone normal for the position the body1 axis went outside the cone
	CrossProduct( body1ax, ax, normal );
	VectorNormalize( normal );
	QuatToMat3( normal, l->sinHalfAngle, l->cosHalfAngle, q );
	VectorMat( ax, (const vec3_t *)q, coneVector );
	CrossProduct( coneVector, ax, t );
	CrossProduct( t, coneVector, normal );
	VectorNormalize( normal );

	VectorMA( anchor, 32.0f, coneVector, p1 );
	VectorSubtract( p1, body1->current->worldOrigin, p1 );

	c->J1.rows = 1;
	CrossProduct( p1, normal, t );
	MatXSetRow( &c->J1, 0, normal, t );
	VectorScale( body1ax, 32.0f, t );
	c->c1.v[0] = ( invTimeStep * LIMIT_ERROR_REDUCTION ) * DotProduct( normal, t );

	if ( body2 ) {
		vec3_t n;

		VectorMA( anchor, 32.0f, coneVector, p2 );
		VectorSubtract( p2, master->current->worldOrigin, p2 );
		VectorNegate( normal, n );
		c->J2.rows = 1;
		CrossProduct( p2, n, t );
		MatXSetRow( &c->J2, 0, n, t );
		c->c2.v[0] = 0.0f;
	}

	c->lo.v[0] = 0.0f;
	c->e.v[0] = l->epsilon >= 0.0f ? l->epsilon : LIMIT_LCP_EPSILON;

	AF_AddFrameConstraint( af, c );
}

/*
================
AF_PyramidLimitAdd

idAFConstraint_PyramidLimit::Add
================
*/
static void AF_PyramidLimitAdd( afPhysics_t *af, afConstraint_t *joint, float invTimeStep ) {
	const afLimit_t *l = &joint->limit;
	afConstraint_t *c;
	afBody_t *body1 = joint->body1, *body2 = joint->body2, *master;
	vec3_t worldBase[3], anchor, body1ax, ax[2], v, normal, pyramidVector, p1, p2, t, q[3];
	float a[2], d;
	int i;

	master = body2;

	if ( master ) {
		VectorMat( l->basis[0], (const vec3_t *)master->current->worldAxis, worldBase[0] );
		VectorMat( l->basis[1], (const vec3_t *)master->current->worldAxis, worldBase[1] );
		VectorMat( l->basis[2], (const vec3_t *)master->current->worldAxis, worldBase[2] );
		VectorMat( l->anchor, (const vec3_t *)master->current->worldAxis, t );
		VectorAdd( master->current->worldOrigin, t, anchor );
	} else {
		AxisCopy( l->basis, worldBase );
		VectorCopy( l->anchor, anchor );
	}

	VectorMat( l->body1Axis, (const vec3_t *)body1->current->worldAxis, body1ax );

	for ( i = 0; i < 2; i++ ) {
		d = DotProduct( worldBase[!i], body1ax );
		VectorMA( body1ax, -d, worldBase[!i], ax[i] );
		VectorNormalize( ax[i] );
		a[i] = DotProduct( worldBase[2], ax[i] );
	}

	// if the body1 axis is inside the pyramid
	if ( a[0] > l->cosAngles[0] && a[1] > l->cosAngles[1] ) {
		return;		// constraint exerts no force
	}

	c = AF_NewFrameConstraint( CONSTRAINT_PYRAMIDLIMIT, "pyramidLimit", 1 );
	if ( !c ) {
		return;
	}
	c->body1 = body1;
	c->body2 = body2;
	c->joint = joint;

	// calculate the inward pyramid normal for the position the body1 axis went outside the pyramid
	VectorCopy( worldBase[2], pyramidVector );
	for ( i = 0; i < 2; i++ ) {
		if ( a[i] <= l->cosAngles[i] ) {
			CrossProduct( ax[i], worldBase[2], v );
			VectorNormalize( v );
			QuatToMat3( v, l->sinHalfAngles[i], l->cosHalfAngles[i], q );
			VectorMat( pyramidVector, (const vec3_t *)q, pyramidVector );
		}
	}

	CrossProduct( pyramidVector, worldBase[2], t );
	CrossProduct( t, pyramidVector, normal );
	VectorNormalize( normal );

	VectorMA( anchor, 32.0f, pyramidVector, p1 );
	VectorSubtract( p1, body1->current->worldOrigin, p1 );

	c->J1.rows = 1;
	CrossProduct( p1, normal, t );
	MatXSetRow( &c->J1, 0, normal, t );
	VectorScale( body1ax, 32.0f, t );
	c->c1.v[0] = ( invTimeStep * LIMIT_ERROR_REDUCTION ) * DotProduct( normal, t );

	if ( body2 ) {
		vec3_t n;

		VectorMA( anchor, 32.0f, pyramidVector, p2 );
		VectorSubtract( p2, master->current->worldOrigin, p2 );
		VectorNegate( normal, n );
		c->J2.rows = 1;
		CrossProduct( p2, n, t );
		MatXSetRow( &c->J2, 0, n, t );
		c->c2.v[0] = 0.0f;
	}

	c->lo.v[0] = 0.0f;
	c->e.v[0] = l->epsilon >= 0.0f ? l->epsilon : LIMIT_LCP_EPSILON;

	AF_AddFrameConstraint( af, c );
}

static void AF_LimitAdd( afPhysics_t *af, afConstraint_t *joint, float invTimeStep ) {
	if ( joint->limit.type == AF_LIMIT_CONE ) {
		AF_ConeLimitAdd( af, joint, invTimeStep );
	} else if ( joint->limit.type == AF_LIMIT_PYRAMID ) {
		AF_PyramidLimitAdd( af, joint, invTimeStep );
	}
}

/*
================
AF_EvaluateFixed
================
*/
static void AF_EvaluateFixed( afConstraint_t *c, float invTimeStep ) {
	vec3_t ofs, a2, ax[3], m[3], identity[3], zero[3], negIdentity[3], skew[3], d;
	afBody_t *master = AF_MasterBody( c );
	rotation_t r;

	if ( master ) {
		VectorMat( c->offset, (const vec3_t *)master->current->worldAxis, a2 );
		VectorAdd( a2, master->current->worldOrigin, ofs );
		Mat3Mul( (const vec3_t *)c->relAxis, (const vec3_t *)master->current->worldAxis, ax );
	} else {
		VectorClear( a2 );
		VectorCopy( c->offset, ofs );
		AxisCopy( c->relAxis, ax );
	}

	Mat3Identity( identity );
	memset( zero, 0, sizeof( zero ) );
	MatXSet4( &c->J1, (const vec3_t *)identity, (const vec3_t *)zero, (const vec3_t *)zero, (const vec3_t *)identity );

	if ( c->body2 ) {
		Mat3Identity( negIdentity );
		VectorNegate( negIdentity[0], negIdentity[0] );
		VectorNegate( negIdentity[1], negIdentity[1] );
		VectorNegate( negIdentity[2], negIdentity[2] );
		SkewSymmetric( a2, skew );
		MatXSet4( &c->J2, (const vec3_t *)negIdentity, (const vec3_t *)skew, (const vec3_t *)zero, (const vec3_t *)negIdentity );
	} else {
		MatXZero( &c->J2, 6, 6 );
	}

	VectorSubtract( ofs, c->body1->current->worldOrigin, d );
	VectorScale( d, -( invTimeStep * ERROR_REDUCTION ), c->c1.v );
	Mat3TransposeMul( (const vec3_t *)c->body1->current->worldAxis, (const vec3_t *)ax, m );
	Mat3ToRotation( (const vec3_t *)m, &r );
	VectorScale( r.vec, -( invTimeStep * ERROR_REDUCTION ) * -DEG2RAD( r.angle ), c->c1.v + 3 );
	c->c1.size = 6;

	VecXClamp( &c->c1, -ERROR_REDUCTION_MAX, ERROR_REDUCTION_MAX );
}

/*
================
AF_EvaluateBallAndSocket
================
*/
static void AF_EvaluateBallAndSocket( afPhysics_t *af, afConstraint_t *c, float invTimeStep ) {
	vec3_t a1, a2, d, identity[3], skew[3];
	afBody_t *master = AF_MasterBody( c );

	VectorMat( c->anchor1, (const vec3_t *)c->body1->current->worldAxis, a1 );

	if ( master ) {
		VectorMat( c->anchor2, (const vec3_t *)master->current->worldAxis, a2 );
		VectorAdd( a2, master->current->worldOrigin, d );
	} else {
		VectorClear( a2 );
		VectorCopy( c->anchor2, d );
	}
	VectorSubtract( d, a1, d );
	VectorSubtract( d, c->body1->current->worldOrigin, d );
	VectorScale( d, -( invTimeStep * ERROR_REDUCTION ), c->c1.v );
	c->c1.size = 3;

	VecXClamp( &c->c1, -ERROR_REDUCTION_MAX, ERROR_REDUCTION_MAX );

	Mat3Identity( identity );
	SkewSymmetric( a1, skew );
	VectorNegate( skew[0], skew[0] ); VectorNegate( skew[1], skew[1] ); VectorNegate( skew[2], skew[2] );
	MatXSet2( &c->J1, (const vec3_t *)identity, (const vec3_t *)skew );

	if ( c->body2 ) {
		VectorNegate( identity[0], identity[0] ); VectorNegate( identity[1], identity[1] ); VectorNegate( identity[2], identity[2] );
		SkewSymmetric( a2, skew );
		MatXSet2( &c->J2, (const vec3_t *)identity, (const vec3_t *)skew );
	} else {
		MatXZero( &c->J2, 3, 6 );
	}

	AF_LimitAdd( af, c, invTimeStep );
}

/*
================
AF_EvaluateUniversal

NOTE: this joint is homokinetic
================
*/
static void AF_EvaluateUniversal( afPhysics_t *af, afConstraint_t *c, float invTimeStep ) {
	vec3_t a1, a2, s1, s2, d1, d2, v, t, identity[3], skew[3];
	afBody_t *master = AF_MasterBody( c );

	VectorMat( c->anchor1, (const vec3_t *)c->body1->current->worldAxis, a1 );
	VectorMat( c->shaft1, (const vec3_t *)c->body1->current->worldAxis, s1 );
	VectorMat( c->axis1, (const vec3_t *)c->body1->current->worldAxis, t );
	CrossProduct( s1, t, d1 );

	if ( master ) {
		VectorMat( c->anchor2, (const vec3_t *)master->current->worldAxis, a2 );
		VectorMat( c->shaft2, (const vec3_t *)master->current->worldAxis, s2 );
		VectorMat( c->axis2, (const vec3_t *)master->current->worldAxis, d2 );
		VectorAdd( a2, master->current->worldOrigin, t );
	} else {
		VectorCopy( c->anchor2, a2 );
		VectorCopy( c->shaft2, s2 );
		VectorCopy( c->axis2, d2 );
		VectorCopy( a2, t );
	}
	VectorSubtract( t, a1, t );
	VectorSubtract( t, c->body1->current->worldOrigin, t );
	VectorScale( t, -( invTimeStep * ERROR_REDUCTION ), c->c1.v );

	Mat3Identity( identity );
	SkewSymmetric( a1, skew );
	VectorNegate( skew[0], skew[0] ); VectorNegate( skew[1], skew[1] ); VectorNegate( skew[2], skew[2] );
	MatXSet2( &c->J1, (const vec3_t *)identity, (const vec3_t *)skew );
	c->J1.rows = 4;
	memset( c->J1.m[3], 0, sizeof( c->J1.m[3] ) );
	c->J1.m[3][3] = s1[0]; c->J1.m[3][4] = s1[1]; c->J1.m[3][5] = s1[2];

	if ( c->body2 ) {
		VectorNegate( identity[0], identity[0] ); VectorNegate( identity[1], identity[1] ); VectorNegate( identity[2], identity[2] );
		SkewSymmetric( a2, skew );
		MatXSet2( &c->J2, (const vec3_t *)identity, (const vec3_t *)skew );
		c->J2.rows = 4;
		memset( c->J2.m[3], 0, sizeof( c->J2.m[3] ) );
		c->J2.m[3][3] = s2[0]; c->J2.m[3][4] = s2[1]; c->J2.m[3][5] = s2[2];
	} else {
		MatXZero( &c->J2, 4, 6 );
	}

	CrossProduct( s1, s2, v );
	if ( VectorNormalize( v ) != 0.0f ) {
		vec3_t m1[3], m2[3], m[3];

		VectorCopy( s1, m1[0] );
		VectorCopy( v, m1[1] );
		CrossProduct( v, m1[0], m1[2] );

		VectorNegate( s2, m2[0] );
		VectorCopy( v, m2[1] );
		CrossProduct( v, m2[0], m2[2] );

		Mat3TransposeMul( (const vec3_t *)m2, (const vec3_t *)m1, m );
		VectorMat( d2, (const vec3_t *)m, d2 );
	}

	c->c1.v[3] = -( invTimeStep * ERROR_REDUCTION ) * DotProduct( d1, d2 );
	c->c1.size = 4;

	VecXClamp( &c->c1, -ERROR_REDUCTION_MAX, ERROR_REDUCTION_MAX );

	AF_LimitAdd( af, c, invTimeStep );
}

/*
================
AF_EvaluateHinge
================
*/
static void AF_EvaluateHinge( afPhysics_t *af, afConstraint_t *c, float invTimeStep ) {
	vec3_t a1, a2, x1, x2, cross, vecX, vecY, t, identity[3], skew[3];
	afBody_t *master = AF_MasterBody( c );
	int i;

	VectorMat( c->axis1, (const vec3_t *)c->body1->current->worldAxis, x1 );	// axis in body1 space
	VectorOrthogonalBasis( x1, vecX, vecY );									// basis for axis in body1 space
	VectorMat( c->anchor1, (const vec3_t *)c->body1->current->worldAxis, a1 );	// anchor in body1 space

	if ( master ) {
		VectorMat( c->anchor2, (const vec3_t *)master->current->worldAxis, a2 );	// anchor in master space
		VectorMat( c->axis2, (const vec3_t *)master->current->worldAxis, x2 );
		VectorAdd( a2, master->current->worldOrigin, t );
	} else {
		VectorCopy( c->anchor2, a2 );
		VectorCopy( c->axis2, x2 );
		VectorCopy( a2, t );
	}
	VectorSubtract( t, a1, t );
	VectorSubtract( t, c->body1->current->worldOrigin, t );
	VectorScale( t, -( invTimeStep * ERROR_REDUCTION ), c->c1.v );

	Mat3Identity( identity );
	SkewSymmetric( a1, skew );
	VectorNegate( skew[0], skew[0] ); VectorNegate( skew[1], skew[1] ); VectorNegate( skew[2], skew[2] );
	MatXSet2( &c->J1, (const vec3_t *)identity, (const vec3_t *)skew );
	c->J1.rows = 5;
	for ( i = 0; i < 3; i++ ) {
		c->J1.m[3][i] = 0.0f; c->J1.m[3][i + 3] = vecX[i];
		c->J1.m[4][i] = 0.0f; c->J1.m[4][i + 3] = vecY[i];
	}

	if ( c->body2 ) {
		VectorNegate( identity[0], identity[0] ); VectorNegate( identity[1], identity[1] ); VectorNegate( identity[2], identity[2] );
		SkewSymmetric( a2, skew );
		MatXSet2( &c->J2, (const vec3_t *)identity, (const vec3_t *)skew );
		c->J2.rows = 5;
		for ( i = 0; i < 3; i++ ) {
			c->J2.m[3][i] = 0.0f; c->J2.m[3][i + 3] = -vecX[i];
			c->J2.m[4][i] = 0.0f; c->J2.m[4][i + 3] = -vecY[i];
		}
	} else {
		MatXZero( &c->J2, 5, 6 );
	}

	CrossProduct( x1, x2, cross );
	c->c1.v[3] = -( invTimeStep * ERROR_REDUCTION ) * DotProduct( cross, vecX );
	c->c1.v[4] = -( invTimeStep * ERROR_REDUCTION ) * DotProduct( cross, vecY );
	c->c1.size = 5;

	VecXClamp( &c->c1, -ERROR_REDUCTION_MAX, ERROR_REDUCTION_MAX );

	AF_LimitAdd( af, c, invTimeStep );
}

static void AF_ConstraintEvaluate( afPhysics_t *af, afConstraint_t *c, float invTimeStep ) {
	switch ( c->type ) {
		case CONSTRAINT_FIXED:
			AF_EvaluateFixed( c, invTimeStep );
			break;
		case CONSTRAINT_BALLANDSOCKETJOINT:
			AF_EvaluateBallAndSocket( af, c, invTimeStep );
			break;
		case CONSTRAINT_UNIVERSALJOINT:
			AF_EvaluateUniversal( af, c, invTimeStep );
			break;
		case CONSTRAINT_HINGE:
			AF_EvaluateHinge( af, c, invTimeStep );
			break;
		default:
			// frame constraints are set up when they're added
			break;
	}
}

/*
================
AF_JointFrictionAdd

The ball and socket, universal and hinge joints' friction constraints
(idAFConstraint_*Friction::Add).
================
*/
static void AF_JointFrictionAdd( afPhysics_t *af, afConstraint_t *joint, float invTimeStep ) {
	afConstraint_t *c;
	vec3_t s1, dir1, dir2, a1, a2, zero = { 0, 0, 0 };
	float f;

	f = AF_JointFriction( joint ) * VecXLength( &joint->lm );
	if ( f == 0.0f ) {
		return;
	}

	switch ( joint->type ) {
		case CONSTRAINT_BALLANDSOCKETJOINT:
			c = AF_NewFrameConstraint( CONSTRAINT_FRICTION, "ballAndSocketJointFriction", 3 );
			if ( !c ) {
				return;
			}
			c->body1 = joint->body1;
			c->body2 = joint->body2;
			c->lo.v[0] = c->lo.v[1] = c->lo.v[2] = -f;
			c->hi.v[0] = c->hi.v[1] = c->hi.v[2] = f;
			c->J1.m[0][3] = c->J1.m[1][4] = c->J1.m[2][5] = 1.0f;
			if ( c->body2 ) {
				c->J2.m[0][3] = c->J2.m[1][4] = c->J2.m[2][5] = 1.0f;
			}
			break;

		case CONSTRAINT_UNIVERSALJOINT:
			c = AF_NewFrameConstraint( CONSTRAINT_FRICTION, "universalJointFriction", 2 );
			if ( !c ) {
				return;
			}
			c->body1 = joint->body1;
			c->body2 = joint->body2;
			c->lo.v[0] = c->lo.v[1] = -f;
			c->hi.v[0] = c->hi.v[1] = f;
			VectorMat( joint->shaft1, (const vec3_t *)c->body1->current->worldAxis, s1 );
			NormalVectors( s1, dir1, dir2 );
			MatXSetRow( &c->J1, 0, zero, dir1 );
			MatXSetRow( &c->J1, 1, zero, dir2 );
			if ( c->body2 ) {
				VectorNegate( dir1, dir1 );
				VectorNegate( dir2, dir2 );
				MatXSetRow( &c->J2, 0, zero, dir1 );
				MatXSetRow( &c->J2, 1, zero, dir2 );
			}
			break;

		case CONSTRAINT_HINGE:
			c = AF_NewFrameConstraint( CONSTRAINT_FRICTION, "hingeFriction", 1 );
			if ( !c ) {
				return;
			}
			c->body1 = joint->body1;
			c->body2 = joint->body2;
			c->lo.v[0] = -f;
			c->hi.v[0] = f;
			VectorMat( joint->axis1, (const vec3_t *)c->body1->current->worldAxis, a1 );
			MatXSetRow( &c->J1, 0, zero, a1 );
			if ( c->body2 ) {
				VectorMat( joint->axis2, (const vec3_t *)c->body2->current->worldAxis, a2 );
				VectorNegate( a2, a2 );
				MatXSetRow( &c->J2, 0, zero, a2 );
			}
			break;

		default:
			return;
	}

	c->joint = joint;
	AF_AddFrameConstraint( af, c );
}

/*
================
AF_ContactSetup

idAFConstraint_Contact::Setup
================
*/
static void AF_ContactSetup( afConstraint_t *c, afBody_t *b1, afBody_t *b2, const contactInfo_t *ci ) {
	vec3_t p, lin, ang;
	float vel;
	float minBounceVelocity = 2.0f;

	c->body1 = b1;
	c->body2 = b2;
	c->contact = *ci;

	VectorSubtract( ci->point, b1->current->worldOrigin, p );
	VectorCopy( ci->normal, lin );
	CrossProduct( p, ci->normal, ang );
	c->J1.rows = 1;
	MatXSetRow( &c->J1, 0, lin, ang );
	vel = DotProduct( lin, b1->current->spatialVelocity ) + DotProduct( ang, b1->current->spatialVelocity + 3 );

	if ( b2 ) {
		VectorSubtract( ci->point, b2->current->worldOrigin, p );
		VectorNegate( ci->normal, lin );
		CrossProduct( p, lin, ang );
		c->J2.rows = 1;
		MatXSetRow( &c->J2, 0, lin, ang );
		vel += DotProduct( lin, b2->current->spatialVelocity ) + DotProduct( ang, b2->current->spatialVelocity + 3 );
		c->c2.v[0] = 0.0f;
	}

	if ( b1->bouncyness > 0.0f && -vel > minBounceVelocity ) {
		c->c1.v[0] = b1->bouncyness * vel;
	} else {
		c->c1.v[0] = 0.0f;
	}

	c->e.v[0] = CONTACT_LCP_EPSILON;
	c->lo.v[0] = 0.0f;
	c->hi.v[0] = AF_INFINITY;
	c->boxConstraint = NULL;
	c->boxIndex[0] = -1;
}

/*
================
AF_ContactFrictionAdd

idAFConstraint_ContactFriction::Add
================
*/
static void AF_ContactFrictionAdd( afPhysics_t *af, afConstraint_t *cc ) {
	afConstraint_t *c;
	afBody_t *body1 = cc->body1, *body2 = cc->body2;
	vec3_t r, dir1, dir2, n, t;
	float friction;

	c = AF_NewFrameConstraint( CONSTRAINT_FRICTION, "contactFriction", 2 );
	if ( !c ) {
		return;
	}
	c->body1 = body1;
	c->body2 = body2;
	c->joint = cc;

	friction = body1->contactFriction * AF_GetContactFrictionScale( af );

	// if the body only has friction in one direction
	if ( body1->useFrictionDir ) {
		VectorMat( body1->frictionDir, (const vec3_t *)body1->current->worldAxis, dir1 );
		// project the friction direction into the contact plane
		VectorMA( dir1, -DotProduct( dir1, cc->contact.normal ), cc->contact.normal, dir1 );
		VectorNormalize( dir1 );

		VectorSubtract( cc->contact.point, body1->current->worldOrigin, r );

		c->J1.rows = 1;
		CrossProduct( r, dir1, t );
		MatXSetRow( &c->J1, 0, dir1, t );
		c->c1.size = 1;
		c->c1.v[0] = 0.0f;

		if ( body2 ) {
			VectorSubtract( cc->contact.point, body2->current->worldOrigin, r );
			VectorNegate( dir1, n );
			c->J2.rows = 1;
			CrossProduct( r, n, t );
			MatXSetRow( &c->J2, 0, n, t );
			c->c2.size = 1;
			c->c2.v[0] = 0.0f;
		}

		c->lo.v[0] = -friction;
		c->hi.v[0] = friction;
		c->boxConstraint = cc;
		c->boxIndex[0] = 0;
	} else {
		// get two friction directions orthogonal to contact normal
		NormalVectors( cc->contact.normal, dir1, dir2 );

		VectorSubtract( cc->contact.point, body1->current->worldOrigin, r );

		c->J1.rows = 2;
		CrossProduct( r, dir1, t );
		MatXSetRow( &c->J1, 0, dir1, t );
		CrossProduct( r, dir2, t );
		MatXSetRow( &c->J1, 1, dir2, t );
		c->c1.size = 2;
		c->c1.v[0] = c->c1.v[1] = 0.0f;

		if ( body2 ) {
			VectorSubtract( cc->contact.point, body2->current->worldOrigin, r );
			c->J2.rows = 2;
			VectorNegate( dir1, n );
			CrossProduct( r, n, t );
			MatXSetRow( &c->J2, 0, n, t );
			VectorNegate( dir2, n );
			CrossProduct( r, n, t );
			MatXSetRow( &c->J2, 1, n, t );
			c->c2.size = 2;
			c->c2.v[0] = c->c2.v[1] = 0.0f;

			if ( body2->contactFriction < friction ) {
				friction = body2->contactFriction;
			}
		}

		c->lo.v[0] = -friction;
		c->hi.v[0] = friction;
		c->boxConstraint = cc;
		c->boxIndex[0] = 0;
		c->lo.v[1] = -friction;
		c->hi.v[1] = friction;
		c->boxIndex[1] = 0;
	}

	if ( body1->useContactMotorDir && body1->contactMotorForce > 0.0f ) {
		int newRow = c->J1.rows;

		VectorMat( body1->contactMotorDir, (const vec3_t *)body1->current->worldAxis, dir1 );
		// project the motor force direction into the contact plane
		VectorMA( dir1, -DotProduct( dir1, cc->contact.normal ), cc->contact.normal, dir1 );
		VectorNormalize( dir1 );

		VectorSubtract( cc->contact.point, body1->current->worldOrigin, r );
		VectorNegate( dir1, n );
		c->J1.rows = newRow + 1;
		CrossProduct( r, n, t );
		MatXSetRow( &c->J1, newRow, n, t );
		c->c1.size = newRow + 1;
		c->c1.v[newRow] = body1->contactMotorVelocity;

		if ( body2 ) {
			VectorSubtract( cc->contact.point, body2->current->worldOrigin, r );
			c->J2.rows = newRow + 1;
			CrossProduct( r, n, t );
			MatXSetRow( &c->J2, newRow, n, t );
			c->c2.size = newRow + 1;
			c->c2.v[newRow] = 0.0f;
		}

		c->lo.v[newRow] = -body1->contactMotorForce;
		c->hi.v[newRow] = body1->contactMotorForce;
		c->boxIndex[newRow] = -1;
	}

	AF_AddFrameConstraint( af, c );
}

static void AF_ConstraintApplyFriction( afPhysics_t *af, afConstraint_t *c, float invTimeStep ) {
	float friction;

	switch ( c->type ) {
		case CONSTRAINT_BALLANDSOCKETJOINT:
		case CONSTRAINT_UNIVERSALJOINT:
		case CONSTRAINT_HINGE:
			if ( AF_JointFriction( c ) <= 0.0f ) {
				return;
			}
			AF_JointFrictionAdd( af, c, invTimeStep );
			break;

		case CONSTRAINT_CONTACT:
			friction = c->body1->contactFriction;
			if ( c->body2 && c->body2->contactFriction < friction ) {
				friction = c->body2->contactFriction;
			}
			friction *= AF_GetContactFrictionScale( af );
			if ( friction <= 0.0f ) {
				return;
			}
			AF_ContactFrictionAdd( af, c );
			break;

		default:
			break;
	}
}

/*
===============================================================================

	Bodies

===============================================================================
*/

static void AF_BodyInit( afBody_t *body ) {
	memset( body, 0, sizeof( *body ) );
	body->linearFriction = -1.0f;
	body->angularFriction = -1.0f;
	body->contactFriction = -1.0f;
	body->bouncyness = -1.0f;
	body->mass = 1.0f;
	body->invMass = 1.0f;
	Mat3Identity( body->inertiaTensor );
	Mat3Identity( body->inverseInertiaTensor );
	body->current = &body->state[0];
	body->next = &body->state[1];
	Mat3Identity( body->current->worldAxis );
	*body->next = *body->current;
	body->saved = *body->current;
	Mat3Identity( body->atRestAxis );
	VecXZero( &body->s, 6 );
	VecXZero( &body->totalForce, 6 );
	VecXZero( &body->auxForce, 6 );
	VecXZero( &body->acceleration, 6 );
	body->selfCollision = qtrue;
	body->isZero = qtrue;
}

/*
================
AF_SetDensity
================
*/
static void AF_SetDensity( afBody_t *body, float density, const vec3_t inertiaScale[3] ) {
	// get the body mass properties
	TraceModelGetMassProperties( &body->trm, density, &body->mass, body->centerOfMass, body->inertiaTensor );

	// make sure we have a valid mass
	if ( body->mass <= 0.0f || IS_NAN( body->mass ) ) {
		Com_Printf( S_COLOR_YELLOW "AF_SetDensity: invalid mass for body '%s'\n", body->name );
		body->mass = 1.0f;
		VectorClear( body->centerOfMass );
		Mat3Identity( body->inertiaTensor );
	}

	// make sure the center of mass is at the body origin
	if ( fabs( body->centerOfMass[0] ) > CENTER_OF_MASS_EPSILON || fabs( body->centerOfMass[1] ) > CENTER_OF_MASS_EPSILON
		|| fabs( body->centerOfMass[2] ) > CENTER_OF_MASS_EPSILON ) {
		Com_DPrintf( S_COLOR_YELLOW "AF_SetDensity: center of mass not at origin for body '%s'\n", body->name );
	}
	VectorClear( body->centerOfMass );

	// calculate the inverse mass and inverse inertia tensor
	body->invMass = 1.0f / body->mass;
	if ( inertiaScale ) {
		Mat3Mul( (const vec3_t *)body->inertiaTensor, inertiaScale, body->inertiaTensor );
	}
	if ( Mat3IsDiagonal( (const vec3_t *)body->inertiaTensor, 1e-3f ) ) {
		body->inertiaTensor[0][1] = body->inertiaTensor[0][2] = 0.0f;
		body->inertiaTensor[1][0] = body->inertiaTensor[1][2] = 0.0f;
		body->inertiaTensor[2][0] = body->inertiaTensor[2][1] = 0.0f;
		Mat3Identity( body->inverseInertiaTensor );
		body->inverseInertiaTensor[0][0] = 1.0f / body->inertiaTensor[0][0];
		body->inverseInertiaTensor[1][1] = 1.0f / body->inertiaTensor[1][1];
		body->inverseInertiaTensor[2][2] = 1.0f / body->inertiaTensor[2][2];
	} else {
		Mat3Inverse( (const vec3_t *)body->inertiaTensor, body->inverseInertiaTensor );
	}
}

void AF_SetBodyFriction( afBody_t *body, float linear, float angular, float contact ) {
	if ( linear < 0.0f || linear > 1.0f || angular < 0.0f || angular > 1.0f || contact < 0.0f ) {
		Com_Printf( S_COLOR_YELLOW "AF_SetBodyFriction: friction out of range, linear = %.1f, angular = %.1f, contact = %.1f\n",
					linear, angular, contact );
		return;
	}
	body->linearFriction = linear;
	body->angularFriction = angular;
	body->contactFriction = contact;
}

/*
===============================================================================

	idAFTree

===============================================================================
*/

/*
================
AF_TreeFactor

factor matrix for the primary constraints in the tree
================
*/
static void AF_TreeFactor( const afTree_t *tree ) {
	int i, j;
	afBody_t *body;
	afConstraint_t *child = NULL;
	afMat_t childI, t;

	// from the leaves up towards the root
	for ( i = tree->numBodies - 1; i >= 0; i-- ) {
		body = tree->sortedBodies[i];

		if ( body->numChildren ) {

			for ( j = 0; j < body->numChildren; j++ ) {

				child = body->children[j]->primaryConstraint;

				// child->I = - child->body1->J.Transpose() * child->body1->I * child->body1->J;
				MatXTransposeMul( &child->body1->J, &child->body1->I, &t );
				MatXMul( &t, &child->body1->J, &childI );
				{
					int r, cc;
					for ( r = 0; r < childI.rows; r++ ) {
						for ( cc = 0; cc < childI.cols; cc++ ) {
							childI.m[r][cc] = -childI.m[r][cc];
						}
					}
				}

				child->invI = childI;
				if ( !MatXInverseSelf( &child->invI ) ) {
					Com_DPrintf( S_COLOR_YELLOW "AF_TreeFactor: couldn't invert %dx%d matrix for constraint '%s'\n",
								child->invI.rows, child->invI.cols, child->name );
				}
				MatXMul( &child->invI, &child->J, &child->J );

				// body->I -= child->J.TransposeMultiply( childI ) * child->J;
				MatXTransposeMul( &child->J, &childI, &t );
				MatXMul( &t, &child->J, &t );
				{
					int r, cc;
					for ( r = 0; r < 6; r++ ) {
						for ( cc = 0; cc < 6; cc++ ) {
							body->I.m[r][cc] -= t.m[r][cc];
						}
					}
				}
			}

			body->invI = body->I;
			if ( !MatXInverseSelf( &body->invI ) ) {
				Com_DPrintf( S_COLOR_YELLOW "AF_TreeFactor: couldn't invert matrix for body %s\n", body->name );
			}
			if ( body->primaryConstraint ) {
				MatXMul( &body->invI, &body->J, &body->J );
			}
		} else if ( body->primaryConstraint ) {
			MatXMul( &body->inverseWorldSpatialInertia, &body->J, &body->J );
		}
	}
}

/*
================
AF_TreeSolve

solve for primary constraints in the tree
================
*/
static void AF_TreeSolve( const afTree_t *tree, int auxiliaryIndex ) {
	int i, j;
	afBody_t *body, *child;
	afConstraint_t *primaryConstraint;

	// from the leaves up towards the root
	for ( i = tree->numBodies - 1; i >= 0; i-- ) {
		body = tree->sortedBodies[i];

		for ( j = 0; j < body->numChildren; j++ ) {
			child = body->children[j];
			primaryConstraint = child->primaryConstraint;

			if ( !child->isZero ) {
				MatXTransposeMulVec( &child->J, child->s.v, primaryConstraint->s.v, MATX_SUB );
				primaryConstraint->isZero = qfalse;
			}
			if ( !primaryConstraint->isZero ) {
				MatXTransposeMulVec( &primaryConstraint->J, primaryConstraint->s.v, body->s.v, MATX_SUB );
				body->isZero = qfalse;
			}
		}
	}

	// from the root down towards the leaves
	for ( i = 0; i < tree->numBodies; i++ ) {
		body = tree->sortedBodies[i];
		primaryConstraint = body->primaryConstraint;

		if ( primaryConstraint ) {

			if ( AF_USE_SYMMETRY && body->parent->maxSubTreeAuxiliaryIndex < auxiliaryIndex ) {
				continue;
			}

			if ( !primaryConstraint->isZero ) {
				MatXMulVec( &primaryConstraint->invI, primaryConstraint->s.v, primaryConstraint->s.v, MATX_SET );
			}
			MatXMulVec( &primaryConstraint->J, primaryConstraint->body2->s.v, primaryConstraint->s.v, MATX_SUB );

			primaryConstraint->lm = primaryConstraint->s;

			if ( AF_USE_SYMMETRY && body->maxSubTreeAuxiliaryIndex < auxiliaryIndex ) {
				continue;
			}

			if ( body->numChildren ) {
				if ( !body->isZero ) {
					MatXMulVec( &body->invI, body->s.v, body->s.v, MATX_SET );
				}
				MatXMulVec( &body->J, primaryConstraint->s.v, body->s.v, MATX_SUB );
			}
		} else if ( body->numChildren ) {
			MatXMulVec( &body->invI, body->s.v, body->s.v, MATX_SET );
		}
	}
}

#define AF_RESPONSE( body, index )		( (body)->response + (index) * 8 )

/*
================
AF_TreeResponse

calculate body forces in the tree in response to a constraint force
================
*/
static void AF_TreeResponse( const afTree_t *tree, const afConstraint_t *constraint, int row, int auxiliaryIndex ) {
	int i, j;
	afBody_t *body;
	afConstraint_t *child, *primaryConstraint;
	float v[6], *force;

	// if a single body don't waste time because there aren't any primary constraints
	if ( tree->numBodies == 1 ) {
		body = constraint->body1;
		if ( body->tree == tree ) {
			memcpy( AF_RESPONSE( body, body->numResponses ), constraint->J1.m[row], 6 * sizeof( float ) );
			body->responseIndex[body->numResponses++] = auxiliaryIndex;
		} else {
			body = constraint->body2;
			memcpy( AF_RESPONSE( body, body->numResponses ), constraint->J2.m[row], 6 * sizeof( float ) );
			body->responseIndex[body->numResponses++] = auxiliaryIndex;
		}
		return;
	}

	// initialize right hand side to zero
	for ( i = 0; i < tree->numBodies; i++ ) {
		body = tree->sortedBodies[i];
		primaryConstraint = body->primaryConstraint;
		if ( primaryConstraint ) {
			memset( primaryConstraint->s.v, 0, sizeof( primaryConstraint->s.v ) );
			primaryConstraint->isZero = qtrue;
		}
		memset( body->s.v, 0, sizeof( body->s.v ) );
		body->isZero = qtrue;
		memset( AF_RESPONSE( body, body->numResponses ), 0, 6 * sizeof( float ) );
	}

	// set right hand side for first constrained body
	body = constraint->body1;
	if ( body->tree == tree ) {
		AF_InverseWorldSpatialInertiaMultiply( body, v, constraint->J1.m[row] );
		primaryConstraint = body->primaryConstraint;
		if ( primaryConstraint ) {
			MatXMulVec( &primaryConstraint->J1, v, primaryConstraint->s.v, MATX_SET );
			primaryConstraint->isZero = qfalse;
		}
		for ( i = 0; i < body->numChildren; i++ ) {
			child = body->children[i]->primaryConstraint;
			MatXMulVec( &child->J2, v, child->s.v, MATX_SET );
			child->isZero = qfalse;
		}
		memcpy( AF_RESPONSE( body, body->numResponses ), constraint->J1.m[row], 6 * sizeof( float ) );
	}

	// set right hand side for second constrained body
	body = constraint->body2;
	if ( body && body->tree == tree ) {
		AF_InverseWorldSpatialInertiaMultiply( body, v, constraint->J2.m[row] );
		primaryConstraint = body->primaryConstraint;
		if ( primaryConstraint ) {
			MatXMulVec( &primaryConstraint->J1, v, primaryConstraint->s.v, MATX_ADD );
			primaryConstraint->isZero = qfalse;
		}
		for ( i = 0; i < body->numChildren; i++ ) {
			child = body->children[i]->primaryConstraint;
			MatXMulVec( &child->J2, v, child->s.v, MATX_ADD );
			child->isZero = qfalse;
		}
		memcpy( AF_RESPONSE( body, body->numResponses ), constraint->J2.m[row], 6 * sizeof( float ) );
	}

	// solve for primary constraints
	AF_TreeSolve( tree, auxiliaryIndex );

	// store body forces in response to the constraint force
	for ( i = 0; i < tree->numBodies; i++ ) {
		body = tree->sortedBodies[i];

		if ( AF_USE_SYMMETRY && body->maxAuxiliaryIndex < auxiliaryIndex ) {
			continue;
		}

		force = AF_RESPONSE( body, body->numResponses );

		// add forces of all primary constraints acting on this body
		primaryConstraint = body->primaryConstraint;
		if ( primaryConstraint ) {
			MatXTransposeMulVec( &primaryConstraint->J1, primaryConstraint->lm.v, force, MATX_ADD );
		}
		for ( j = 0; j < body->numChildren; j++ ) {
			child = body->children[j]->primaryConstraint;
			MatXTransposeMulVec( &child->J2, child->lm.v, force, MATX_ADD );
		}

		body->responseIndex[body->numResponses++] = auxiliaryIndex;
	}
}

/*
================
AF_TreeCalculateForces

calculate forces on the bodies in the tree
================
*/
static void AF_TreeCalculateForces( const afTree_t *tree, float timeStep ) {
	int i, j, k;
	float invStep;
	afBody_t *body;
	afConstraint_t *child, *c, *primaryConstraint;

	// forces on bodies
	for ( i = 0; i < tree->numBodies; i++ ) {
		body = tree->sortedBodies[i];

		for ( k = 0; k < 6; k++ ) {
			body->totalForce.v[k] = body->current->externalForce[k] + body->auxForce.v[k];
		}
	}

	// if a single body don't waste time because there aren't any primary constraints
	if ( tree->numBodies == 1 ) {
		return;
	}

	invStep = 1.0f / timeStep;

	// initialize right hand side
	for ( i = 0; i < tree->numBodies; i++ ) {
		body = tree->sortedBodies[i];

		AF_InverseWorldSpatialInertiaMultiply( body, body->acceleration.v, body->totalForce.v );
		for ( k = 0; k < 6; k++ ) {
			body->acceleration.v[k] += body->current->spatialVelocity[k] * invStep;
		}
		primaryConstraint = body->primaryConstraint;
		if ( primaryConstraint ) {
			// b = ( J * acc + c )
			c = primaryConstraint;
			MatXMulVec( &c->J1, c->body1->acceleration.v, c->s.v, MATX_SET );
			MatXMulVec( &c->J2, c->body2->acceleration.v, c->s.v, MATX_ADD );
			for ( k = 0; k < c->J1.rows; k++ ) {
				c->s.v[k] += invStep * ( c->c1.v[k] + c->c2.v[k] );
			}
			c->s.size = c->J1.rows;
			c->isZero = qfalse;
		}
		memset( body->s.v, 0, sizeof( body->s.v ) );
		body->isZero = qtrue;
	}

	// solve for primary constraints
	AF_TreeSolve( tree, 0 );

	// calculate forces on bodies after applying primary constraints
	for ( i = 0; i < tree->numBodies; i++ ) {
		body = tree->sortedBodies[i];

		// add forces of all primary constraints acting on this body
		primaryConstraint = body->primaryConstraint;
		if ( primaryConstraint ) {
			MatXTransposeMulVec( &primaryConstraint->J1, primaryConstraint->lm.v, body->totalForce.v, MATX_ADD );
		}
		for ( j = 0; j < body->numChildren; j++ ) {
			child = body->children[j]->primaryConstraint;
			MatXTransposeMulVec( &child->J2, child->lm.v, body->totalForce.v, MATX_ADD );
		}
	}
}

static void AF_TreeSetMaxSubTreeAuxiliaryIndex( afTree_t *tree ) {
	int i, j;
	afBody_t *body, *child;

	// from the leaves up towards the root
	for ( i = tree->numBodies - 1; i >= 0; i-- ) {
		body = tree->sortedBodies[i];

		body->maxSubTreeAuxiliaryIndex = body->maxAuxiliaryIndex;
		for ( j = 0; j < body->numChildren; j++ ) {
			child = body->children[j];
			if ( child->maxSubTreeAuxiliaryIndex > body->maxSubTreeAuxiliaryIndex ) {
				body->maxSubTreeAuxiliaryIndex = child->maxSubTreeAuxiliaryIndex;
			}
		}
	}
}

static void AF_TreeSortBodies_r( afTree_t *tree, afBody_t *body ) {
	int i;

	for ( i = 0; i < body->numChildren; i++ ) {
		tree->sortedBodies[tree->numBodies++] = body->children[i];
	}
	for ( i = 0; i < body->numChildren; i++ ) {
		AF_TreeSortBodies_r( tree, body->children[i] );
	}
}

/*
================
AF_TreeSortBodies

sort body list to make sure parents come first
================
*/
static void AF_TreeSortBodies( afTree_t *tree ) {
	int i;
	afBody_t *body;

	// find the root
	for ( i = 0; i < tree->numBodies; i++ ) {
		if ( !tree->sortedBodies[i]->parent ) {
			break;
		}
	}

	if ( i >= tree->numBodies ) {
		Com_Printf( S_COLOR_RED "Articulated figure tree has no root.\n" );
		return;
	}

	body = tree->sortedBodies[i];
	tree->numBodies = 0;
	tree->sortedBodies[tree->numBodies++] = body;
	AF_TreeSortBodies_r( tree, body );
}

/*
===============================================================================

	idPhysics_AF

===============================================================================
*/

/*
================
AF_Init
================
*/
void AF_Init( afPhysics_t *af ) {
	memset( af, 0, sizeof( *af ) );
	af->changedAF = qtrue;

	af->current.atRest = -1;
	af->current.lastTimeStep = 0.05f;

	af->linearFriction = 0.005f;
	af->angularFriction = 0.005f;
	af->contactFriction = 0.8f;
	af->bouncyness = 0.4f;
	af->totalMass = 0.0f;
	af->forceTotalMass = -1.0f;
	af->clipMask = MASK_SOLID;

	af->suspendVelocity[0] = SUSPEND_LINEAR_VELOCITY;
	af->suspendVelocity[1] = SUSPEND_ANGULAR_VELOCITY;
	af->suspendAcceleration[0] = SUSPEND_LINEAR_ACCELERATION;
	af->suspendAcceleration[1] = SUSPEND_LINEAR_ACCELERATION;
	af->noMoveTime = NO_MOVE_TIME;
	af->noMoveTranslation = NO_MOVE_TRANSLATION_TOLERANCE;
	af->noMoveRotation = NO_MOVE_ROTATION_TOLERANCE;
	af->minMoveTime = MIN_MOVE_TIME;
	af->maxMoveTime = MAX_MOVE_TIME;
	af->impulseThreshold = IMPULSE_THRESHOLD;

	af->timeScale = 1.0f;

	af->enableCollision = qtrue;
	af->selfCollision = qtrue;
	af->comeToRest = qtrue;
	af->noImpact = qfalse;

	VectorSet( af->gravityVector, 0, 0, -800 );
}

afBody_t *AF_GetBody( const afPhysics_t *af, const char *name ) {
	int i;

	for ( i = 0; i < af->numBodies; i++ ) {
		if ( !Q_stricmp( af->bodies[i].name, name ) ) {
			return (afBody_t *)&af->bodies[i];
		}
	}
	return NULL;
}

int AF_GetBodyId( const afPhysics_t *af, const char *name ) {
	afBody_t *body = AF_GetBody( af, name );

	return body ? body->id : -1;
}

/*
================
AF_AddBody

bodies get an id in the order they are added starting at zero; the trace
model is moved to be about its center of mass, origin with it
================
*/
afBody_t *AF_AddBody( afPhysics_t *af, const char *name, const traceModel_t *trm, float density,
					const vec3_t origin, const vec3_t axis[3], const vec3_t inertiaScale[3] ) {
	afBody_t *body;

	if ( af->numBodies >= AF_MAX_BODIES ) {
		Com_Printf( S_COLOR_RED "AF_AddBody: more than %d bodies\n", AF_MAX_BODIES );
		return NULL;
	}
	if ( AF_GetBody( af, name ) ) {
		Com_Printf( S_COLOR_RED "AF_AddBody: a body with the name '%s' already exists.\n", name );
		return NULL;
	}

	body = &af->bodies[af->numBodies];
	AF_BodyInit( body );
	Q_strncpyz( body->name, name, sizeof( body->name ) );
	body->id = af->numBodies;
	body->trm = *trm;
	VectorCopy( origin, body->current->worldOrigin );
	AxisCopy( axis, body->current->worldAxis );
	AF_SetDensity( body, density, inertiaScale );
	*body->next = *body->current;

	if ( body->linearFriction < 0.0f ) {
		body->linearFriction = af->linearFriction;
		body->angularFriction = af->angularFriction;
		body->contactFriction = af->contactFriction;
	}
	if ( body->bouncyness < 0.0f ) {
		body->bouncyness = af->bouncyness;
	}
	if ( !body->clipMaskSet ) {
		body->clipMask = af->clipMask;
	}

	af->numBodies++;
	af->changedAF = qtrue;
	return body;
}

/*
================
AF_AddConstraint

the joints' anchors and axes are set after this, with the bodies where they are
================
*/
afConstraint_t *AF_AddConstraint( afPhysics_t *af, afConstraintType_t type, const char *name, afBody_t *body1, afBody_t *body2 ) {
	afConstraint_t *c;
	int size;

	if ( af->numConstraints >= AF_MAX_CONSTRAINTS ) {
		Com_Printf( S_COLOR_RED "AF_AddConstraint: more than %d constraints\n", AF_MAX_CONSTRAINTS );
		return NULL;
	}
	if ( !body1 ) {
		Com_Printf( S_COLOR_RED "AF_AddConstraint: body1 == NULL on constraint '%s'.\n", name );
		return NULL;
	}
	if ( body1 == body2 ) {
		Com_Printf( S_COLOR_RED "AF_AddConstraint: body1 and body2 of constraint '%s' are the same.\n", name );
		return NULL;
	}

	switch ( type ) {
		case CONSTRAINT_FIXED:					size = 6; break;
		case CONSTRAINT_BALLANDSOCKETJOINT:		size = 3; break;
		case CONSTRAINT_UNIVERSALJOINT:			size = 4; break;
		case CONSTRAINT_HINGE:					size = 5; break;
		default:
			Com_Printf( S_COLOR_RED "AF_AddConstraint: constraint '%s' of an unsupported type\n", name );
			return NULL;
	}

	c = &af->constraints[af->numConstraints++];
	AF_ConstraintInit( c, type, name, size );
	c->body1 = body1;
	c->body2 = body2;
	c->physics = af;
	c->allowPrimary = qtrue;
	c->noCollision = qtrue;

	if ( type == CONSTRAINT_FIXED ) {
		AF_InitFixedOffset( c );
	} else if ( type == CONSTRAINT_HINGE ) {
		AxisCopy( body1->current->worldAxis, c->initialAxis );
		if ( body2 ) {
			Mat3MulTranspose( (const vec3_t *)c->initialAxis, (const vec3_t *)body2->current->worldAxis, c->initialAxis );
		}
	}

	af->changedAF = qtrue;
	return c;
}

static qboolean AF_IsClosedLoop( const afBody_t *body1, const afBody_t *body2 ) {
	const afBody_t *b1, *b2;

	for ( b1 = body1; b1->parent; b1 = b1->parent ) {
	}
	for ( b2 = body2; b2->parent; b2 = b2->parent ) {
	}
	return ( b1 == b2 );
}

/*
================
AF_BuildTrees

af_useLinearTime's: bilateral constraints that make no loops are primary
================
*/
static void AF_BuildTrees( afPhysics_t *af ) {
	int i;
	float scale;
	afBody_t *b;
	afConstraint_t *c;
	afTree_t *tree;

	af->numPrimaryConstraints = 0;
	af->numAuxiliaryConstraints = 0;
	af->numTrees = 0;
	af->totalMass = 0.0f;

	for ( i = 0; i < af->numBodies; i++ ) {
		b = &af->bodies[i];
		b->parent = NULL;
		b->primaryConstraint = NULL;
		b->numConstraints = 0;
		b->numChildren = 0;
		b->tree = NULL;
		af->totalMass += b->mass;
	}

	if ( af->forceTotalMass > 0.0f ) {
		scale = af->forceTotalMass / af->totalMass;
		for ( i = 0; i < af->numBodies; i++ ) {
			b = &af->bodies[i];
			b->mass *= scale;
			b->invMass = 1.0f / b->mass;
			VectorScale( b->inertiaTensor[0], scale, b->inertiaTensor[0] );
			VectorScale( b->inertiaTensor[1], scale, b->inertiaTensor[1] );
			VectorScale( b->inertiaTensor[2], scale, b->inertiaTensor[2] );
			Mat3Inverse( (const vec3_t *)b->inertiaTensor, b->inverseInertiaTensor );
		}
		af->totalMass = af->forceTotalMass;
		af->forceTotalMass = -1.0f;		// scaled once
	}

	for ( i = 0; i < af->numConstraints; i++ ) {
		c = &af->constraints[i];

		c->body1->constraints[c->body1->numConstraints++] = c;
		if ( c->body2 ) {
			c->body2->constraints[c->body2->numConstraints++] = c;
		}

		// only bilateral constraints between two non-world bodies that do not
		// create loops can be used as primary constraints
		if ( !c->body1->primaryConstraint && c->allowPrimary && c->body2 != NULL && !AF_IsClosedLoop( c->body1, c->body2 ) ) {
			c->body1->primaryConstraint = c;
			c->body1->parent = c->body2;
			c->body2->children[c->body2->numChildren++] = c->body1;
			c->isPrimary = qtrue;
			c->firstIndex = 0;
			af->primaryConstraints[af->numPrimaryConstraints++] = c;
		} else {
			c->isPrimary = qfalse;
			af->auxiliaryConstraints[af->numAuxiliaryConstraints++] = c;
		}
	}

	// create trees for all parent bodies
	for ( i = 0; i < af->numBodies; i++ ) {
		if ( !af->bodies[i].parent ) {
			tree = &af->trees[af->numTrees++];
			tree->numBodies = 0;
			tree->sortedBodies[tree->numBodies++] = &af->bodies[i];
			af->bodies[i].tree = tree;
		}
	}

	// add each child body to the appropriate tree
	for ( i = 0; i < af->numBodies; i++ ) {
		if ( af->bodies[i].parent ) {
			for ( b = af->bodies[i].parent; !b->tree; b = b->parent ) {
			}
			b->tree->sortedBodies[b->tree->numBodies++] = &af->bodies[i];
			af->bodies[i].tree = b->tree;
		}
	}

	if ( af->numTrees > 1 ) {
		Com_DPrintf( S_COLOR_YELLOW "Articulated figure has multiple separate tree structures.\n" );
	}

	// sort bodies in each tree to make sure parents come first
	for ( i = 0; i < af->numTrees; i++ ) {
		AF_TreeSortBodies( &af->trees[i] );
	}
}

static void AF_EvaluateConstraints( afPhysics_t *af, float timeStep ) {
	int i;
	float invTimeStep;
	afBody_t *body;
	afConstraint_t *c;

	invTimeStep = 1.0f / timeStep;

	// setup the constraint equations for the current position and orientation of the bodies
	for ( i = 0; i < af->numPrimaryConstraints; i++ ) {
		c = af->primaryConstraints[i];
		AF_ConstraintEvaluate( af, c, invTimeStep );
		c->J = c->J2;
	}
	for ( i = 0; i < af->numAuxiliaryConstraints; i++ ) {
		AF_ConstraintEvaluate( af, af->auxiliaryConstraints[i], invTimeStep );
	}

	// add contact constraints to the list with frame constraints
	for ( i = 0; i < af_numContactConstraints; i++ ) {
		AF_AddFrameConstraint( af, af_contactConstraints[i] );
	}

	// setup body primary constraint matrix
	for ( i = 0; i < af->numBodies; i++ ) {
		body = &af->bodies[i];

		if ( body->primaryConstraint ) {
			MatXTranspose( &body->primaryConstraint->J1, &body->J );
		}
	}
}

static void AF_EvaluateBodies( afPhysics_t *af, float timeStep ) {
	int i;
	afBody_t *body;
	vec3_t mass[3], zero[3], worldInertia[3];

	memset( zero, 0, sizeof( zero ) );

	for ( i = 0; i < af->numBodies; i++ ) {
		body = &af->bodies[i];

		// the center of mass is at the body point of reference (AF_SetDensity)
		Mat3Identity( mass );
		VectorScale( mass[0], body->mass, mass[0] );
		VectorScale( mass[1], body->mass, mass[1] );
		VectorScale( mass[2], body->mass, mass[2] );

		// spatial inertia in world space
		WorldTensor( (const vec3_t *)body->current->worldAxis, (const vec3_t *)body->inertiaTensor, worldInertia );
		MatXSet4( &body->I, (const vec3_t *)mass, (const vec3_t *)zero, (const vec3_t *)zero, (const vec3_t *)worldInertia );

		// inverse spatial inertia in world space
		Mat3Identity( mass );
		VectorScale( mass[0], body->invMass, mass[0] );
		VectorScale( mass[1], body->invMass, mass[1] );
		VectorScale( mass[2], body->invMass, mass[2] );
		WorldTensor( (const vec3_t *)body->current->worldAxis, (const vec3_t *)body->inverseInertiaTensor, worldInertia );
		MatXSet4( &body->inverseWorldSpatialInertia, (const vec3_t *)mass, (const vec3_t *)zero, (const vec3_t *)zero, (const vec3_t *)worldInertia );

		body->spatialInertiaSparse = qtrue;

		// initialize auxiliary constraint force to zero
		VecXZero( &body->auxForce, 6 );
	}
}

static void AF_AddFrameConstraints( afPhysics_t *af ) {
	int i;

	// the auxiliary constraints with the frame constraints after them
	af_numAux = 0;
	for ( i = 0; i < af->numAuxiliaryConstraints; i++ ) {
		af_aux[af_numAux++] = af->auxiliaryConstraints[i];
	}
	for ( i = 0; i < af_numFrameConstraints; i++ ) {
		af_aux[af_numAux++] = af_frameConstraints[i];
	}
}

static void AF_ApplyFriction( afPhysics_t *af, float timeStep ) {
	int i;
	float invTimeStep;

	invTimeStep = 1.0f / timeStep;

	for ( i = 0; i < af->numPrimaryConstraints; i++ ) {
		AF_ConstraintApplyFriction( af, af->primaryConstraints[i], invTimeStep );
	}
	for ( i = 0; i < af->numAuxiliaryConstraints; i++ ) {
		AF_ConstraintApplyFriction( af, af->auxiliaryConstraints[i], invTimeStep );
	}
	for ( i = 0; i < af_numFrameConstraints; i++ ) {
		AF_ConstraintApplyFriction( af, af_frameConstraints[i], invTimeStep );
	}
}

static void AF_PrimaryFactor( afPhysics_t *af ) {
	int i;

	for ( i = 0; i < af->numTrees; i++ ) {
		AF_TreeFactor( &af->trees[i] );
	}
}

static void AF_PrimaryForces( afPhysics_t *af, float timeStep ) {
	int i;

	for ( i = 0; i < af->numTrees; i++ ) {
		AF_TreeCalculateForces( &af->trees[i], timeStep );
	}
}

/*
================
AF_AuxiliaryForces
================
*/
static void AF_AuxiliaryForces( afPhysics_t *af, float timeStep ) {
	int i, j, k, l, n, m, s, numAuxConstraints, *index;
	float *ptr, *j1, *j2, *dstPtr, *forcePtr, tmp[6];
	float invStep, u;
	afBody_t *body;
	afConstraint_t *constraint;

	// get the number of one dimensional auxiliary constraints
	for ( numAuxConstraints = 0, i = 0; i < af_numAux; i++ ) {
		numAuxConstraints += af_aux[i]->J1.rows;
	}

	if ( numAuxConstraints == 0 ) {
		return;
	}

	if ( numAuxConstraints > AF_MAX_AUX_ROWS ) {
		// drop the last frame constraints
		for ( k = 0, i = 0; i < af_numAux; i++ ) {
			if ( k + af_aux[i]->J1.rows > AF_MAX_AUX_ROWS ) {
				break;
			}
			k += af_aux[i]->J1.rows;
		}
		af_numAux = i;
		numAuxConstraints = k;
		Com_DPrintf( "AF_AuxiliaryForces: more than %d auxiliary constraint rows\n", AF_MAX_AUX_ROWS );
	}

	// memory to store the body response to auxiliary constraint forces
	forcePtr = af_responseBuf;
	index = af_responseIndexBuf;
	for ( i = 0; i < af->numBodies; i++ ) {
		body = &af->bodies[i];
		body->response = forcePtr;
		body->responseIndex = index;
		body->numResponses = 0;
		body->maxAuxiliaryIndex = 0;
		forcePtr += numAuxConstraints * 8;
		index += numAuxConstraints;
	}

	// set on each body the largest index of an auxiliary constraint constraining the body
	if ( AF_USE_SYMMETRY ) {
		for ( k = 0, i = 0; i < af_numAux; i++ ) {
			constraint = af_aux[i];
			for ( j = 0; j < constraint->J1.rows; j++, k++ ) {
				if ( k > constraint->body1->maxAuxiliaryIndex ) {
					constraint->body1->maxAuxiliaryIndex = k;
				}
				if ( constraint->body2 && k > constraint->body2->maxAuxiliaryIndex ) {
					constraint->body2->maxAuxiliaryIndex = k;
				}
			}
		}
		for ( i = 0; i < af->numTrees; i++ ) {
			AF_TreeSetMaxSubTreeAuxiliaryIndex( &af->trees[i] );
		}
	}

	// calculate forces of primary constraints in response to the auxiliary constraint forces
	for ( k = 0, i = 0; i < af_numAux; i++ ) {
		constraint = af_aux[i];

		for ( j = 0; j < constraint->J1.rows; j++, k++ ) {

			// calculate body forces in the tree in response to the constraint force
			AF_TreeResponse( constraint->body1->tree, constraint, j, k );
			// if there is a second body which is part of a different tree
			if ( constraint->body2 && constraint->body2->tree != constraint->body1->tree ) {
				// calculate body forces in the second tree in response to the constraint force
				AF_TreeResponse( constraint->body2->tree, constraint, j, k );
			}
		}
	}

	// create constraint matrix for auxiliary constraints using a mass matrix adjusted for the primary constraints
	for ( k = 0, i = 0; i < af_numAux; i++ ) {
		constraint = af_aux[i];

		for ( j = 0; j < constraint->J1.rows; j++, k++ ) {
			AF_InverseWorldSpatialInertiaMultiply( constraint->body1, tmp, constraint->J1.m[j] );
			j1 = tmp;
			ptr = constraint->body1->response;
			index = constraint->body1->responseIndex;
			dstPtr = &af_jmk[k * numAuxConstraints];
			s = AF_USE_SYMMETRY ? k + 1 : numAuxConstraints;

			for ( l = n = 0; n < constraint->body1->numResponses; n++ ) {
				m = index[n];
				if ( m >= s ) {
					break;
				}
				while ( l < m ) {
					dstPtr[l++] = 0.0f;
				}
				dstPtr[l++] = j1[0] * ptr[0] + j1[1] * ptr[1] + j1[2] * ptr[2] +
								j1[3] * ptr[3] + j1[4] * ptr[4] + j1[5] * ptr[5];
				ptr += 8;
			}

			while ( l < s ) {
				dstPtr[l++] = 0.0f;
			}

			if ( constraint->body2 ) {
				AF_InverseWorldSpatialInertiaMultiply( constraint->body2, tmp, constraint->J2.m[j] );
				j2 = tmp;
				ptr = constraint->body2->response;
				index = constraint->body2->responseIndex;
				for ( n = 0; n < constraint->body2->numResponses; n++ ) {
					m = index[n];
					if ( m >= s ) {
						break;
					}
					dstPtr[m] += j2[0] * ptr[0] + j2[1] * ptr[1] + j2[2] * ptr[2] +
									j2[3] * ptr[3] + j2[4] * ptr[4] + j2[5] * ptr[5];
					ptr += 8;
				}
			}
		}
	}

	if ( AF_USE_SYMMETRY ) {
		n = numAuxConstraints;
		for ( i = 0; i < numAuxConstraints; i++ ) {
			ptr = af_jmk + ( i + 1 ) * n + i;
			dstPtr = af_jmk + i * n + i + 1;
			for ( j = i + 1; j < numAuxConstraints; j++ ) {
				*dstPtr++ = *ptr;
				ptr += n;
			}
		}
	}

	invStep = 1.0f / timeStep;

	// calculate body acceleration
	for ( i = 0; i < af->numBodies; i++ ) {
		body = &af->bodies[i];
		AF_InverseWorldSpatialInertiaMultiply( body, body->acceleration.v, body->totalForce.v );
		for ( k = 0; k < 6; k++ ) {
			body->acceleration.v[k] += body->current->spatialVelocity[k] * invStep;
		}
	}

	// set first index for special box constrained variables
	for ( k = 0, i = 0; i < af_numAux; i++ ) {
		af_aux[i]->firstIndex = k;
		k += af_aux[i]->J1.rows;
	}

	// initialize right hand side and low and high bounds for auxiliary constraints
	for ( k = 0, i = 0; i < af_numAux; i++ ) {
		constraint = af_aux[i];

		for ( j = 0; j < constraint->J1.rows; j++, k++ ) {

			j1 = constraint->J1.m[j];
			ptr = constraint->body1->acceleration.v;
			af_rhs[k] = j1[0] * ptr[0] + j1[1] * ptr[1] + j1[2] * ptr[2] + j1[3] * ptr[3] + j1[4] * ptr[4] + j1[5] * ptr[5];
			af_rhs[k] += constraint->c1.v[j] * invStep;

			if ( constraint->body2 ) {
				j2 = constraint->J2.m[j];
				ptr = constraint->body2->acceleration.v;
				af_rhs[k] += j2[0] * ptr[0] + j2[1] * ptr[1] + j2[2] * ptr[2] + j2[3] * ptr[3] + j2[4] * ptr[4] + j2[5] * ptr[5];
				af_rhs[k] += constraint->c2.v[j] * invStep;
			}

			af_rhs[k] = -af_rhs[k];
			af_lo[k] = constraint->lo.v[j];
			af_hi[k] = constraint->hi.v[j];

			if ( constraint->boxIndex[j] >= 0 && constraint->boxConstraint ) {
				af_boxIndex[k] = constraint->boxConstraint->firstIndex + constraint->boxIndex[j];
			} else {
				af_boxIndex[k] = -1;
			}
			af_jmk[k * numAuxConstraints + k] += constraint->e.v[j] * invStep;
		}
	}

	// calculate lagrange multipliers for auxiliary constraints
	if ( !LCP_SolveSymmetric( af_jmk, numAuxConstraints, numAuxConstraints, af_lm, af_rhs, af_lo, af_hi, af_boxIndex ) ) {
		return;		// bad monkey!
	}

	// calculate auxiliary constraint forces
	for ( k = 0, i = 0; i < af_numAux; i++ ) {
		constraint = af_aux[i];

		for ( j = 0; j < constraint->J1.rows; j++, k++ ) {
			constraint->lm.v[j] = u = af_lm[k];
			constraint->lm.size = constraint->J1.rows;

			j1 = constraint->J1.m[j];
			ptr = constraint->body1->auxForce.v;
			ptr[0] += j1[0] * u; ptr[1] += j1[1] * u; ptr[2] += j1[2] * u;
			ptr[3] += j1[3] * u; ptr[4] += j1[4] * u; ptr[5] += j1[5] * u;

			if ( constraint->body2 ) {
				j2 = constraint->J2.m[j];
				ptr = constraint->body2->auxForce.v;
				ptr[0] += j2[0] * u; ptr[1] += j2[1] * u; ptr[2] += j2[2] * u;
				ptr[3] += j2[3] * u; ptr[4] += j2[4] * u; ptr[5] += j2[5] * u;
			}
		}
	}

	// recalculate primary constraint forces in response to auxiliary constraint forces
	AF_PrimaryForces( af, timeStep );

	// clear pointers pointing to the work space
	for ( i = 0; i < af->numBodies; i++ ) {
		body = &af->bodies[i];
		body->response = NULL;
		body->responseIndex = NULL;
	}
}

static void AF_VerifyContactConstraints( afPhysics_t *af ) {
	int i;
	afBody_t *body;
	vec3_t normal;
	float d;

	for ( i = 0; i < af_numContactConstraints; i++ ) {
		body = af_contactConstraints[i]->body1;
		VectorCopy( af_contactConstraints[i]->contact.normal, normal );
		d = DotProduct( normal, body->next->spatialVelocity );
		if ( d <= 0.0f ) {
			VectorMA( body->next->spatialVelocity, -1.0001f * d, normal, body->next->spatialVelocity );
		}
		body = af_contactConstraints[i]->body2;
		if ( !body ) {
			continue;
		}
		VectorNegate( normal, normal );
		d = DotProduct( normal, body->next->spatialVelocity );
		if ( d <= 0.0f ) {
			VectorMA( body->next->spatialVelocity, -1.0001f * d, normal, body->next->spatialVelocity );
		}
	}
}

static void AF_Evolve( afPhysics_t *af, float timeStep ) {
	int i, k;
	float angle, vSqr, maxLinearVelocity, maxAngularVelocity;
	vec3_t vec;
	afBody_t *body;
	rotation_t rotation;
	vec3_t axis[3];

	maxLinearVelocity = AF_MAX_LINEAR_VELOCITY / timeStep;
	maxAngularVelocity = AF_MAX_ANGULAR_VELOCITY / timeStep;

	for ( i = 0; i < af->numBodies; i++ ) {
		body = &af->bodies[i];

		// calculate the spatial velocity for the next physics state
		AF_InverseWorldSpatialInertiaMultiply( body, body->acceleration.v, body->totalForce.v );
		for ( k = 0; k < 6; k++ ) {
			body->next->spatialVelocity[k] = body->current->spatialVelocity[k] + timeStep * body->acceleration.v[k];
		}

		if ( maxLinearVelocity > 0.0f ) {
			// cap the linear velocity
			vSqr = Vec6LengthSqr3( body->next->spatialVelocity );
			if ( vSqr > Square( maxLinearVelocity ) ) {
				VectorScale( body->next->spatialVelocity, maxLinearVelocity / sqrt( vSqr ), body->next->spatialVelocity );
			}
		}

		if ( maxAngularVelocity > 0.0f ) {
			// cap the angular velocity
			vSqr = Vec6LengthSqr3( body->next->spatialVelocity + 3 );
			if ( vSqr > Square( maxAngularVelocity ) ) {
				VectorScale( body->next->spatialVelocity + 3, maxAngularVelocity / sqrt( vSqr ), body->next->spatialVelocity + 3 );
			}
		}
	}

	// make absolutely sure all contact constraints are satisfied
	AF_VerifyContactConstraints( af );

	// calculate the position of the bodies for the next physics state
	for ( i = 0; i < af->numBodies; i++ ) {
		body = &af->bodies[i];

		// translate world origin
		VectorMA( body->current->worldOrigin, timeStep, body->next->spatialVelocity, body->next->worldOrigin );

		// convert angular velocity to a rotation matrix
		VectorCopy( body->next->spatialVelocity + 3, vec );
		angle = -timeStep * (float)RAD2DEG( VectorNormalize( vec ) );
		if ( angle != 0.0f ) {
			memset( &rotation, 0, sizeof( rotation ) );
			RotationFromOriginAndVector( &rotation, vec3_origin, vec, angle );
			RotationNormalize180( &rotation );
			RotationToMat3( &rotation );

			// rotate world axis
			Mat3Mul( (const vec3_t *)body->current->worldAxis, (const vec3_t *)rotation.axis, axis );
			AxisCopy( axis, body->next->worldAxis );
			Mat3OrthoNormalizeSelf( body->next->worldAxis );
		} else {
			AxisCopy( body->current->worldAxis, body->next->worldAxis );
		}

		// linear and angular friction
		VectorMA( body->next->spatialVelocity, -body->linearFriction, body->next->spatialVelocity, body->next->spatialVelocity );
		VectorMA( body->next->spatialVelocity + 3, -body->angularFriction, body->next->spatialVelocity + 3, body->next->spatialVelocity + 3 );
	}
}

/*
================
AF_CollisionImpulse

apply impulse to the colliding bodies; the current state of the body should
be set to the moment of impact. This is silly as it doesn't take the AF
structure into account.
================
*/
static qboolean AF_CollisionImpulse( afPhysics_t *af, float timeStep, afBody_t *body, const cm_trace_t *collision ) {
	vec3_t r, velocity, impulse, t, rxn, invWorldInertia[3];
	float impulseNumerator, impulseDenominator;

	// collision point relative to the body center of mass
	VectorMat( body->centerOfMass, (const vec3_t *)body->current->worldAxis, t );
	VectorAdd( body->current->worldOrigin, t, t );
	VectorSubtract( collision->c.point, t, r );

	// the velocity at the collision point
	CrossProduct( body->current->spatialVelocity + 3, r, t );
	VectorAdd( body->current->spatialVelocity, t, velocity );

	// never stick
	if ( DotProduct( velocity, collision->c.normal ) > 0.0f ) {
		VectorCopy( collision->c.normal, velocity );
	}

	WorldTensor( (const vec3_t *)body->current->worldAxis, (const vec3_t *)body->inverseInertiaTensor, invWorldInertia );
	impulseNumerator = -( 1.0f + body->bouncyness ) * DotProduct( velocity, collision->c.normal );
	CrossProduct( r, collision->c.normal, rxn );
	MatVector( (const vec3_t *)invWorldInertia, rxn, t );
	CrossProduct( t, r, t );
	impulseDenominator = body->invMass + DotProduct( t, collision->c.normal );

	VectorScale( collision->c.normal, impulseNumerator / impulseDenominator, impulse );

	// apply impact to other entity
	if ( af->clip.impact ) {
		vec3_t negImpulse;

		VectorNegate( impulse, negImpulse );
		af->clip.impact( af->clip.ctx, af, body->id, collision, negImpulse );
	}

	// a ragdoll isn't stopped by its impacts (Doom 3's idAFEntity_Base::Collide does nothing)
	return qfalse;
}

static qboolean AF_ApplyCollisions( afPhysics_t *af, float timeStep ) {
	int i;

	for ( i = 0; i < af_numCollisions; i++ ) {
		if ( AF_CollisionImpulse( af, timeStep, af_collisions[i].body, &af_collisions[i].trace ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
================
AF_SetupCollisionForBody

which of the figure's bodies the moving one may collide with
================
*/
static void AF_SetupCollisionForBody( afPhysics_t *af, afBody_t *body ) {
	int i;
	afBody_t *b;

	if ( !af->selfCollision || !body->selfCollision ) {
		// disable all bodies
		for ( i = 0; i < af->numBodies; i++ ) {
			af->bodies[i].clipDisabled = qtrue;
		}
		return;
	}

	// enable all bodies that have self collision
	for ( i = 0; i < af->numBodies; i++ ) {
		af->bodies[i].clipDisabled = !af->bodies[i].selfCollision;
	}

	// don't let the body collide with itself
	body->clipDisabled = qtrue;

	// nor with those near it in the figure (AF_ExcludeNearBodies)
	for ( i = 0; i < af->numBodies; i++ ) {
		if ( body->noCollide & ( 1u << i ) ) {
			af->bodies[i].clipDisabled = qtrue;
		}
	}

	// disable any bodies attached with constraints
	for ( i = 0; i < body->numConstraints; i++ ) {
		if ( !body->constraints[i]->noCollision ) {
			continue;
		}
		if ( body->constraints[i]->body1 == body ) {
			b = body->constraints[i]->body2;
		} else if ( body->constraints[i]->body2 == body ) {
			b = body->constraints[i]->body1;
		} else {
			continue;
		}
		if ( b ) {
			// don't collide with this body
			b->clipDisabled = qtrue;
		}
	}
}

/*
===============================================================================

	Self collision: Doom 3 links each body's clip model and lets the others
	collide with it; here a body traces against the others' trace models

===============================================================================
*/

// the engine's collision queries, as the game and the cgame both have them
void	trap_CM_Translation( cm_trace_t *results, const vec3_t start, const vec3_t end,
							const traceModel_t *trm, const vec3_t trmAxis[3], int contentMask,
							cmHandle_t model, const vec3_t modelOrigin, const vec3_t modelAxis[3] );
void	trap_CM_Rotation( cm_trace_t *results, const vec3_t start, const rotation_t *rotation,
							const traceModel_t *trm, const vec3_t trmAxis[3], int contentMask,
							cmHandle_t model, const vec3_t modelOrigin, const vec3_t modelAxis[3] );
int		trap_CM_Contacts( contactInfo_t *contacts, const int maxContacts, const vec3_t start, const vec6_t dir, const float depth,
							const traceModel_t *trm, const vec3_t trmAxis[3], int contentMask,
							cmHandle_t model, const vec3_t modelOrigin, const vec3_t modelAxis[3] );

// whether body collides with other now (AF_SetupCollisionForBody's say)
static qboolean AF_SelfCollides( const afPhysics_t *af, const afBody_t *body, const afBody_t *other ) {
	return af->clip.trmModel && other != body && !other->clipDisabled && ( body->clipMask & other->contents );
}

/*
================
AF_SelfMotion

The body moved from where it is to end (origin and axis) against the
figure's other bodies, those before it where they're going (they've moved:
Doom 3 relinks each as it goes), the rest where they are; qtrue on a
collision, its entityNum -1 and its id the other body. A body already
touching another isn't held where it is (Doom 3 stopped it there each step
while its joint dragged it on, and small bodies twitched for ever): the
contacts between them keep it from going further in.
================
*/
static qboolean AF_SelfMotion( afPhysics_t *af, afBody_t *body, cm_trace_t *results, const vec3_t endOrigin, const vec3_t endAxis[3] ) {
	const afBodyPState_t *state;
	cm_trace_t trace, best;
	rotation_t rotation;
	vec3_t axis[3], reached;
	afBody_t *other;
	qboolean hit = qfalse, rotates;
	int i;

	memset( &best, 0, sizeof( best ) );
	best.fraction = 1.0f;
	VectorCopy( endOrigin, best.endpos );
	AxisCopy( body->current->worldAxis, best.endAxis );

	// translation
	if ( !VectorCompare( body->current->worldOrigin, endOrigin ) ) {
		for ( i = 0; i < af->numBodies; i++ ) {
			other = &af->bodies[i];
			if ( !AF_SelfCollides( af, body, other ) ) {
				continue;
			}
			state = other->id < body->id ? other->next : other->current;
			trap_CM_Translation( &trace, body->current->worldOrigin, endOrigin, &body->trm, (const vec3_t *)body->current->worldAxis,
								body->clipMask, af->clip.trmModel( af->clip.ctx, &other->trm ), state->worldOrigin,
								(const vec3_t *)state->worldAxis );
			if ( trace.fraction < best.fraction && trace.fraction > 0.0f ) {
				best = trace;
				best.c.entityNum = -1;
				best.c.id = other->id;
				hit = qtrue;
			}
		}
	}
	if ( hit ) {
		*results = best;
		return qtrue;
	}

	// then the rotation, where the translation got
	VectorCopy( best.endpos, reached );
	Mat3TransposeMul( (const vec3_t *)body->current->worldAxis, endAxis, axis );
	memset( &rotation, 0, sizeof( rotation ) );
	Mat3ToRotation( (const vec3_t *)axis, &rotation );
	VectorCopy( reached, rotation.origin );
	rotates = rotation.angle != 0.0f && !VectorCompare( rotation.vec, vec3_origin );
	if ( rotates ) {
		for ( i = 0; i < af->numBodies; i++ ) {
			other = &af->bodies[i];
			if ( !AF_SelfCollides( af, body, other ) ) {
				continue;
			}
			state = other->id < body->id ? other->next : other->current;
			trap_CM_Rotation( &trace, reached, &rotation, &body->trm, (const vec3_t *)body->current->worldAxis,
							  body->clipMask, af->clip.trmModel( af->clip.ctx, &other->trm ), state->worldOrigin,
							  (const vec3_t *)state->worldAxis );
			if ( trace.fraction < best.fraction && trace.fraction > 0.0f ) {
				best = trace;
				best.c.entityNum = -1;
				best.c.id = other->id;
				hit = qtrue;
			}
		}
	}
	if ( hit ) {
		*results = best;
	}
	return hit;
}

/*
================
AF_SelfContacts

the body's contacts with the figure's other bodies, after n of the world's
================
*/
static int AF_SelfContacts( afPhysics_t *af, afBody_t *body, contactInfo_t *contacts, int n, int maxContacts, const vec6_t dir ) {
	afBody_t *other;
	int i, j, num;

	for ( i = 0; i < af->numBodies && n < maxContacts; i++ ) {
		other = &af->bodies[i];
		if ( !AF_SelfCollides( af, body, other ) ) {
			continue;
		}
		num = trap_CM_Contacts( contacts + n, maxContacts - n, body->current->worldOrigin, dir, 2.0f, &body->trm,
								(const vec3_t *)body->current->worldAxis, body->clipMask,
								af->clip.trmModel( af->clip.ctx, &other->trm ), other->current->worldOrigin,
								(const vec3_t *)other->current->worldAxis );
		for ( j = 0; j < num; j++ ) {
			contacts[n + j].entityNum = -1;
			contacts[n + j].id = other->id;
		}
		n += num;
	}
	return n;
}

/*
================
AF_CheckForCollisions

check for collisions between the current and next state; if there is a
collision the next state is set to the state at the moment of impact
================
*/
static void AF_CheckForCollisions( afPhysics_t *af, float timeStep ) {
	int i;
	afBody_t *body;
	vec3_t axis[3];
	rotation_t rotation;
	cm_trace_t collision, selfCollision;
	qboolean hit;

	// clear list with collisions
	af_numCollisions = 0;

	if ( !af->enableCollision || !af->clip.motion ) {
		return;
	}

	for ( i = 0; i < af->numBodies; i++ ) {
		body = &af->bodies[i];

		if ( body->clipMask != 0 ) {

			AF_SetupCollisionForBody( af, body );

			Mat3TransposeMul( (const vec3_t *)body->current->worldAxis, (const vec3_t *)body->next->worldAxis, axis );
			memset( &rotation, 0, sizeof( rotation ) );
			Mat3ToRotation( (const vec3_t *)axis, &rotation );
			VectorCopy( body->current->worldOrigin, rotation.origin );

			// if there was a collision
			hit = af->clip.motion( af->clip.ctx, af, body->id, &collision, body->current->worldOrigin, body->next->worldOrigin,
								&rotation, &body->trm, (const vec3_t *)body->current->worldAxis, body->clipMask );
			if ( hit ) {
				// set the next state to the state at the moment of impact
				VectorCopy( collision.endpos, body->next->worldOrigin );
				AxisCopy( collision.endAxis, body->next->worldAxis );
			}
			// and the figure's other bodies on the way there
			if ( AF_SelfMotion( af, body, &selfCollision, body->next->worldOrigin, (const vec3_t *)body->next->worldAxis ) ) {
				VectorCopy( selfCollision.endpos, body->next->worldOrigin );
				AxisCopy( selfCollision.endAxis, body->next->worldAxis );
				collision = selfCollision;
				hit = qtrue;
			}
			if ( hit ) {
				// add collision to the list
				af_collisions[af_numCollisions].trace = collision;
				af_collisions[af_numCollisions].body = body;
				af_numCollisions++;
			}
		}
	}
}

/*
================
AF_EvaluateContacts
================
*/
static qboolean AF_EvaluateContacts( afPhysics_t *af ) {
	int i, j, k, numContacts, numBodyContacts;
	afBody_t *body;
	contactInfo_t contactInfo[10];
	vec6_t dir;

	// evaluate bodies
	AF_EvaluateBodies( af, af->current.lastTimeStep );

	// remove all existing contacts
	af->numContacts = 0;

	if ( !af->enableCollision || !af->clip.contacts ) {
		return qfalse;
	}

	// find all the contacts
	for ( i = 0; i < af->numBodies; i++ ) {
		body = &af->bodies[i];

		if ( body->clipMask == 0 ) {
			continue;
		}

		AF_SetupCollisionForBody( af, body );

		AF_InverseWorldSpatialInertiaMultiply( body, dir, body->current->externalForce );
		for ( k = 0; k < 6; k++ ) {
			dir[k] = body->current->spatialVelocity[k] + af->current.lastTimeStep * dir[k];
		}
		VectorNormalize( dir );
		VectorNormalize( dir + 3 );

		numContacts = af->clip.contacts( af->clip.ctx, af, i, contactInfo, 10, body->current->worldOrigin, dir, 2.0f,
										&body->trm, (const vec3_t *)body->current->worldAxis, body->clipMask );
		numContacts = AF_SelfContacts( af, body, contactInfo, numContacts, 10, dir );

		// merge nearby contacts between the same bodies
		// and assure there are at most three planar contacts between any pair of bodies
		for ( j = 0; j < numContacts; j++ ) {

			numBodyContacts = 0;
			for ( k = 0; k < af->numContacts; k++ ) {
				if ( af->contacts[k].entityNum == contactInfo[j].entityNum ) {
					if ( ( af->contacts[k].id == i && contactInfo[j].id == af->contactBodies[k] ) ||
							( af->contactBodies[k] == i && af->contacts[k].id == contactInfo[j].id ) ) {

						if ( DistanceSquared( af->contacts[k].point, contactInfo[j].point ) < Square( 2.0f ) ) {
							break;
						}
						if ( fabs( DotProduct( af->contacts[k].normal, contactInfo[j].normal ) ) > 0.9f ) {
							numBodyContacts++;
						}
					}
				}
			}

			if ( k >= af->numContacts && numBodyContacts < 3 && af->numContacts < AF_MAX_CONTACTS ) {
				af->contacts[af->numContacts] = contactInfo[j];
				af->contactBodies[af->numContacts] = i;
				af->numContacts++;
			}
		}
	}

	return ( af->numContacts != 0 );
}

/*
================
AF_SetupContactConstraints

a contact's entityNum is -1 when it's with another body of the figure (its id)
================
*/
static void AF_SetupContactConstraints( afPhysics_t *af ) {
	int i;
	afConstraint_t *c;

	af_numContactConstraints = 0;

	for ( i = 0; i < af->numContacts; i++ ) {
		c = AF_NewFrameConstraint( CONSTRAINT_CONTACT, "contact", 1 );
		if ( !c ) {
			break;
		}
		c->physics = af;
		if ( af->contacts[i].entityNum == -1 && af->contacts[i].id >= 0 && af->contacts[i].id < af->numBodies ) {
			AF_ContactSetup( c, &af->bodies[af->contactBodies[i]], &af->bodies[af->contacts[i].id], &af->contacts[i] );
		} else {
			AF_ContactSetup( c, &af->bodies[af->contactBodies[i]], NULL, &af->contacts[i] );
		}
		af_contactConstraints[af_numContactConstraints++] = c;
	}
}

static void AF_ClearExternalForce( afPhysics_t *af ) {
	int i;

	for ( i = 0; i < af->numBodies; i++ ) {
		memset( af->bodies[i].current->externalForce, 0, sizeof( af->bodies[i].current->externalForce ) );
		memset( af->bodies[i].next->externalForce, 0, sizeof( af->bodies[i].next->externalForce ) );
	}
}

static void AF_AddGravity( afPhysics_t *af ) {
	int i;
	afBody_t *body;

	for ( i = 0; i < af->numBodies; i++ ) {
		body = &af->bodies[i];
		// add gravitational force
		VectorMA( body->current->externalForce, body->mass, af->gravityVector, body->current->externalForce );
	}
}

static void AF_SwapStates( afPhysics_t *af ) {
	int i;
	afBody_t *body;
	afBodyPState_t *swap;

	for ( i = 0; i < af->numBodies; i++ ) {
		body = &af->bodies[i];

		// swap the current and next state for next simulation step
		swap = body->current;
		body->current = body->next;
		body->next = swap;
	}
}

/*
================
AF_ExcludeNearBodies

Bodies two joints apart or nearer don't collide with each other: a body's
grandparent and its siblings (two limbs off the same body, the throat and
the head) meet at the joints between them, always touching. Doom 3 only
leaves out the bodies a joint holds together (noCollision); colliding, the
others keep striking each other where they meet and a figure of small
bodies (the frog's) never comes to rest.
================
*/
void AF_ExcludeNearBodies( afPhysics_t *af ) {
	int dist[AF_MAX_BODIES][AF_MAX_BODIES];
	afConstraint_t *c;
	int i, j, k, a, b;

	for ( i = 0; i < af->numBodies; i++ ) {
		af->bodies[i].noCollide = 0;
		for ( j = 0; j < af->numBodies; j++ ) {
			dist[i][j] = i == j ? 0 : AF_MAX_BODIES;
		}
	}
	for ( i = 0; i < af->numConstraints; i++ ) {
		c = &af->constraints[i];
		if ( !c->body1 || !c->body2 ) {
			continue;
		}
		a = c->body1 - af->bodies;
		b = c->body2 - af->bodies;
		dist[a][b] = dist[b][a] = 1;
	}
	// how many joints apart (Floyd-Warshall, a figure is small)
	for ( k = 0; k < af->numBodies; k++ ) {
		for ( i = 0; i < af->numBodies; i++ ) {
			for ( j = 0; j < af->numBodies; j++ ) {
				if ( dist[i][k] + dist[k][j] < dist[i][j] ) {
					dist[i][j] = dist[i][k] + dist[k][j];
				}
			}
		}
	}
	for ( i = 0; i < af->numBodies; i++ ) {
		for ( j = 0; j < af->numBodies; j++ ) {
			if ( i != j && dist[i][j] <= 2 ) {
				af->bodies[i].noCollide |= 1u << j;
			}
		}
	}
}

/*
================
AF_ExcludeTouchingBodies

Bodies in each other or touching where the figure starts don't collide
with each other either (a compact figure's, the frog's tail against its
hind leg): colliding, they would strike each other where they meet.
================
*/
#define AF_TOUCH_MARGIN		2.0f		// as near as contacts are looked for (AF_SelfContacts)

void AF_ExcludeTouchingBodies( afPhysics_t *af ) {
	vec3_t dir, end;
	cm_trace_t tr;
	afBody_t *a, *b;
	int i, j;

	if ( !af->clip.trmModel ) {
		return;
	}
	for ( i = 0; i < af->numBodies; i++ ) {
		a = &af->bodies[i];
		for ( j = i + 1; j < af->numBodies; j++ ) {
			b = &af->bodies[j];
			if ( a->noCollide & ( 1u << j ) ) {
				continue;
			}
			// a moved toward b's middle by the margin meets it (in it, it stops at once)
			VectorSubtract( b->current->worldOrigin, a->current->worldOrigin, dir );
			VectorNormalize( dir );
			VectorMA( a->current->worldOrigin, AF_TOUCH_MARGIN, dir, end );
			trap_CM_Translation( &tr, a->current->worldOrigin, end, &a->trm,
								 (const vec3_t *)a->current->worldAxis, a->clipMask | a->contents,
								 af->clip.trmModel( af->clip.ctx, &b->trm ), b->current->worldOrigin,
								 (const vec3_t *)b->current->worldAxis );
			if ( tr.fraction < 1.0f ) {
				a->noCollide |= 1u << j;
				b->noCollide |= 1u << i;
			}
		}
	}
}

/*
================
AF_TestIfAtRest
================
*/
static qboolean AF_TestIfAtRest( afPhysics_t *af, float timeStep ) {
	int i;
	float translationSqr, maxTranslationSqr, rotation, maxRotation;
	afBody_t *body;
	vec3_t m[3];
	rotation_t r;

	if ( af->current.atRest >= 0 ) {
		return qtrue;
	}

	af->current.activateTime += timeStep;

	// if the simulation should never be suspended before a certaint amount of time passed
	if ( af->minMoveTime > 0.0f && af->current.activateTime < af->minMoveTime ) {
		return qfalse;
	}

	// if the simulation should always be suspended after a certain amount time passed
	if ( af->maxMoveTime > 0.0f && af->current.activateTime > af->maxMoveTime ) {
		return qtrue;
	}

	// test if all bodies hardly moved over a period of time
	if ( af->current.noMoveTime == 0.0f ) {
		for ( i = 0; i < af->numBodies; i++ ) {
			body = &af->bodies[i];
			VectorCopy( body->current->worldOrigin, body->atRestOrigin );
			AxisCopy( body->current->worldAxis, body->atRestAxis );
		}
		af->current.noMoveTime += timeStep;
	} else if ( af->current.noMoveTime > af->noMoveTime ) {
		af->current.noMoveTime = 0.0f;
		maxTranslationSqr = 0.0f;
		maxRotation = 0.0f;
		for ( i = 0; i < af->numBodies; i++ ) {
			body = &af->bodies[i];

			translationSqr = DistanceSquared( body->current->worldOrigin, body->atRestOrigin );
			if ( translationSqr > maxTranslationSqr ) {
				maxTranslationSqr = translationSqr;
			}
			Mat3TransposeMul( (const vec3_t *)body->atRestAxis, (const vec3_t *)body->current->worldAxis, m );
			Mat3ToRotation( (const vec3_t *)m, &r );
			rotation = r.angle;
			if ( rotation > maxRotation ) {
				maxRotation = rotation;
			}
		}

		if ( maxTranslationSqr < Square( af->noMoveTranslation ) && maxRotation < af->noMoveRotation ) {
			// hardly moved over a period of time so the articulated figure may come to rest
			return qtrue;
		}
	} else {
		af->current.noMoveTime += timeStep;
	}

	// test if the velocity or acceleration of any body is still too large to come to rest
	for ( i = 0; i < af->numBodies; i++ ) {
		body = &af->bodies[i];

		if ( Vec6LengthSqr3( body->current->spatialVelocity ) > Square( af->suspendVelocity[0] ) ) {
			return qfalse;
		}
		if ( Vec6LengthSqr3( body->current->spatialVelocity + 3 ) > Square( af->suspendVelocity[1] ) ) {
			return qfalse;
		}
		if ( Vec6LengthSqr3( body->acceleration.v ) > Square( af->suspendAcceleration[0] ) ) {
			return qfalse;
		}
		if ( Vec6LengthSqr3( body->acceleration.v + 3 ) > Square( af->suspendAcceleration[1] ) ) {
			return qfalse;
		}
	}

	// all bodies have a velocity and acceleration small enough to come to rest
	return qtrue;
}

void AF_Rest( afPhysics_t *af ) {
	int i;

	af->current.atRest = af->time;

	for ( i = 0; i < af->numBodies; i++ ) {
		memset( af->bodies[i].current->spatialVelocity, 0, sizeof( af->bodies[i].current->spatialVelocity ) );
		memset( af->bodies[i].current->externalForce, 0, sizeof( af->bodies[i].current->externalForce ) );
	}
}

void AF_Activate( afPhysics_t *af ) {
	// if the articulated figure was at rest
	if ( af->current.atRest >= 0 ) {
		// normally gravity is added at the end of a simulation frame
		// if the figure was at rest add gravity here so it is applied this simulation frame
		AF_AddGravity( af );
		// reset the active time for the max move time
		af->current.activateTime = 0.0f;
	}
	af->current.atRest = -1;
	af->current.noMoveTime = 0.0f;
}

qboolean AF_IsAtRest( const afPhysics_t *af ) {
	return af->current.atRest >= 0;
}

static void AF_AddPushVelocity( afPhysics_t *af, const float *pushVelocity ) {
	int i, k;

	for ( k = 0; k < 6; k++ ) {
		if ( pushVelocity[k] != 0.0f ) {
			break;
		}
	}
	if ( k == 6 ) {
		return;
	}
	for ( i = 0; i < af->numBodies; i++ ) {
		for ( k = 0; k < 6; k++ ) {
			af->bodies[i].current->spatialVelocity[k] += pushVelocity[k];
		}
	}
}

/*
================
AF_Evaluate

one step; endTimeMSec is the time after it
================
*/
qboolean AF_Evaluate( afPhysics_t *af, int timeStepMSec, int endTimeMSec ) {
	float timeStep, push[6];
	int k;

	timeStep = MS2SEC( timeStepMSec ) * af->timeScale;
	af->current.lastTimeStep = timeStep;
	af->time = endTimeMSec;

	// if the articulated figure changed
	if ( af->changedAF ) {
		AF_BuildTrees( af );
		af->changedAF = qfalse;
	}

	// if the simulation is suspended because the figure is at rest
	if ( af->current.atRest >= 0 || timeStep <= 0.0f ) {
		return qfalse;
	}

	af_numFramePool = 0;
	af_numFrameConstraints = 0;
	af_numContactConstraints = 0;

	// move the af velocity into the frame of a pusher
	for ( k = 0; k < 6; k++ ) {
		push[k] = -af->current.pushVelocity[k];
	}
	AF_AddPushVelocity( af, push );

	// evaluate contacts
	AF_EvaluateContacts( af );

	// setup contact constraints
	AF_SetupContactConstraints( af );

	// evaluate constraint equations
	AF_EvaluateConstraints( af, timeStep );

	// apply friction
	AF_ApplyFriction( af, timeStep );

	// add frame constraints
	AF_AddFrameConstraints( af );

	// factor matrices for primary constraints
	AF_PrimaryFactor( af );

	// calculate forces on bodies after applying primary constraints
	AF_PrimaryForces( af, timeStep );

	// calculate and apply auxiliary constraint forces
	AF_AuxiliaryForces( af, timeStep );

	// evolve current state to next state
	AF_Evolve( af, timeStep );

	// clear external forces on all bodies
	AF_ClearExternalForce( af );

	// remove all frame constraints
	af_numFrameConstraints = 0;
	af_numAux = 0;

	// check for collisions between current and next state
	AF_CheckForCollisions( af, timeStep );

	// swap the current and next state
	AF_SwapStates( af );

	// apply collision impulses
	if ( AF_ApplyCollisions( af, timeStep ) ) {
		af->current.atRest = af->time;
		af->comeToRest = qtrue;
	}

	// test if the simulation can be suspended because the whole figure is at rest
	if ( af->comeToRest && AF_TestIfAtRest( af, timeStep ) ) {
		AF_Rest( af );
	}

	// add gravitational force
	AF_AddGravity( af );

	// move the af velocity back into the world frame
	AF_AddPushVelocity( af, af->current.pushVelocity );
	memset( af->current.pushVelocity, 0, sizeof( af->current.pushVelocity ) );

	return qtrue;
}

void AF_GetImpactInfo( const afPhysics_t *af, int id, const vec3_t point, float *invMass,
						vec3_t invInertiaTensor[3], vec3_t position, vec3_t velocity ) {
	const afBody_t *body;
	vec3_t t;

	if ( id < 0 || id >= af->numBodies ) {
		*invMass = 0.0f;
		memset( invInertiaTensor, 0, sizeof( vec3_t ) * 3 );
		VectorClear( position );
		VectorClear( velocity );
		return;
	}
	body = &af->bodies[id];
	*invMass = 1.0f / body->mass;
	WorldTensor( (const vec3_t *)body->current->worldAxis, (const vec3_t *)body->inverseInertiaTensor, invInertiaTensor );
	VectorSubtract( point, body->current->worldOrigin, position );
	CrossProduct( body->current->spatialVelocity + 3, position, t );
	VectorAdd( body->current->spatialVelocity, t, velocity );
}

/*
================
AF_ApplyImpulse
================
*/
void AF_ApplyImpulse( afPhysics_t *af, int id, const vec3_t point, const vec3_t impulse ) {
	afBody_t *body;
	vec3_t invWorldInertia[3], r, t;

	if ( id < 0 || id >= af->numBodies ) {
		return;
	}
	if ( af->noImpact || VectorLengthSquared( impulse ) < Square( af->impulseThreshold ) ) {
		return;
	}
	body = &af->bodies[id];
	WorldTensor( (const vec3_t *)body->current->worldAxis, (const vec3_t *)body->inverseInertiaTensor, invWorldInertia );
	VectorMA( body->current->spatialVelocity, body->invMass, impulse, body->current->spatialVelocity );
	VectorSubtract( point, body->current->worldOrigin, r );
	CrossProduct( r, impulse, t );
	MatVector( (const vec3_t *)invWorldInertia, t, r );
	VectorAdd( body->current->spatialVelocity + 3, r, body->current->spatialVelocity + 3 );
	AF_Activate( af );
}

void AF_AddForce( afPhysics_t *af, int id, const vec3_t point, const vec3_t force ) {
	afBody_t *body;
	vec3_t r, t;

	if ( af->noImpact ) {
		return;
	}
	if ( id < 0 || id >= af->numBodies ) {
		return;
	}
	body = &af->bodies[id];
	VectorAdd( body->current->externalForce, force, body->current->externalForce );
	VectorSubtract( point, body->current->worldOrigin, r );
	CrossProduct( r, force, t );
	VectorAdd( body->current->externalForce + 3, t, body->current->externalForce + 3 );
	AF_Activate( af );
}

/*
================
AF_GetBounds

the bodies' bounds in the world
================
*/
void AF_GetBounds( const afPhysics_t *af, vec3_t absBounds[2] ) {
	vec3_t b[2];
	int i;

	ClearBounds( absBounds[0], absBounds[1] );
	for ( i = 0; i < af->numBodies; i++ ) {
		BoundsFromTransformedBounds( b, (const vec3_t *)af->bodies[i].trm.bounds, af->bodies[i].current->worldOrigin,
									(const vec3_t *)af->bodies[i].current->worldAxis );
		AddPointToBounds( b[0], absBounds[0], absBounds[1] );
		AddPointToBounds( b[1], absBounds[0], absBounds[1] );
	}
}

/*
================
AF_DebugDraw

the bodies' edges and the joints; color 0 a body, 1 a constraint, 2 a contact
================
*/
void AF_DebugDraw( const afPhysics_t *af, void (*line)( const vec3_t start, const vec3_t end, int color ) ) {
	const afBody_t *body;
	const afConstraint_t *c;
	vec3_t v1, v2, t;
	int i, j;

	for ( i = 0; i < af->numBodies; i++ ) {
		body = &af->bodies[i];
		for ( j = 1; j <= body->trm.numEdges; j++ ) {
			VectorMat( body->trm.verts[body->trm.edges[j].v[0]], (const vec3_t *)body->current->worldAxis, v1 );
			VectorMat( body->trm.verts[body->trm.edges[j].v[1]], (const vec3_t *)body->current->worldAxis, v2 );
			VectorAdd( v1, body->current->worldOrigin, v1 );
			VectorAdd( v2, body->current->worldOrigin, v2 );
			line( v1, v2, 0 );
		}
	}

	for ( i = 0; i < af->numConstraints; i++ ) {
		c = &af->constraints[i];
		VectorMat( c->anchor1, (const vec3_t *)c->body1->current->worldAxis, t );
		VectorAdd( c->body1->current->worldOrigin, t, t );
		for ( j = 0; j < 3; j++ ) {
			VectorCopy( t, v1 );
			VectorCopy( t, v2 );
			v1[j] -= 2;
			v2[j] += 2;
			line( v1, v2, 1 );
		}
	}

	for ( i = 0; i < af->numContacts; i++ ) {
		VectorMA( af->contacts[i].point, 6.0f, af->contacts[i].normal, v2 );
		line( af->contacts[i].point, v2, 2 );
	}
}

/*
================
AF_ImpactSpeed

how fast a body hit what an impact's impulse was for (the impact callback's)
================
*/
float AF_ImpactSpeed( const afPhysics_t *af, int body, const vec3_t impulse ) {
	if ( body < 0 || body >= af->numBodies ) {
		return 0.0f;
	}
	return VectorLength( impulse ) * af->bodies[body].invMass / ( 1.0f + af->bodies[body].bouncyness );
}
