/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#ifndef __Q_EXTRAMATH_H__
#define __Q_EXTRAMATH_H__

#include "q_shared.h"

typedef vec_t			vec5_t[5]; // xyz, st

typedef vec_t           plane_t[4]; // normal, d

typedef struct rotation_s rotation_t;

#define PLANESIDE_FRONT				0
#define PLANESIDE_BACK				1
#define PLANESIDE_ON				2
#define PLANESIDE_CROSS				3

#define Vector5Copy(a,b)		((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2],(b)[3]=(a)[3],(b)[4]=(a)[4])

#ifdef Q_INFINITY
#undef Q_INFINITY
#endif
#define	Q_INFINITY		1e30f

#define FLOATSIGNBITSET(f)		(((*(const unsigned long *)&(f)) >> 31) & 1)
#define FLOATSIGNBITNOTSET(f)	(((~(*(const unsigned long *)&(f))) >> 31) & 1)
#define INTSIGNBITSET(i)		((((const unsigned long)(i)) >> 31) & 1)
#define INTSIGNBITNOTSET(i)		(((~((const unsigned long)(i))) >> 31) & 1)

// vector must be normalized
void VectorToAxis( const vec3_t v, vec3_t axis[3] );

static ID_INLINE void VectorOrthogonalBasis( const vec3_t v, vec3_t left, vec3_t up ) {
	float l, s;
    float x, y, z;

    x = v[0];
    y = v[1];
    z = v[2];

	if ( fabs( z ) > 0.7f ) {
		l = y * y + z * z;
		s = 1.0f / sqrt( l );
		up[0] = 0;
		up[1] = z * s;
		up[2] = -y * s;
		left[0] = l * s;
		left[1] = -x * up[2];
		left[2] = x * up[1];
	}
	else {
		l = x * x + y * y;
		s = 1.0f / sqrt( l );
		left[0] = -y * s;
		left[1] = x * s;
		left[2] = 0;
		up[0] = -z * left[1];
		up[1] = z * left[0];
		up[2] = l * s;
	}
}

float BoundsGetRadius( const vec3_t bounds[2] );
float BoundsDistanceToPlane( vec3_t bounds[2], const plane_t plane );
qboolean AddBoundsToBounds( const vec3_t abounds[2], vec3_t bounds[2] );
void BoundsFromTransformedBounds( vec3_t self[2], const vec3_t bounds[2], const vec3_t origin, const vec3_t axis[3] );
// most tight bounds for a translation
void BoundsFromPointTranslation( vec3_t bounds[2], const vec3_t point, const vec3_t translation );
void BoundsFromBoundsTranslation( vec3_t self[2], const vec3_t bounds[2], const vec3_t origin, const vec3_t axis[3], const vec3_t translation );
// most tight bounds for a rotation
void BoundsFromPointRotation( vec3_t bounds[2], const vec3_t point, const rotation_t *rotation );
void BoundsFromBoundsRotation( vec3_t self[2], const vec3_t bounds[2], const vec3_t origin, const vec3_t axis[3], const rotation_t *rotation );

int BoxOnPlaneSideSlow( vec3_t emins, vec3_t emaxs, const plane_t p, const float epsilon );

static ID_INLINE qboolean AxisCompare( const vec3_t axis1[3], const vec3_t axis2[3] ) {
	if ( VectorCompare( axis1[0], axis2[0] )
		 && VectorCompare( axis1[1], axis2[1] )
		 && VectorCompare( axis1[2], axis2[2] ) ) {
		return qtrue;
	}
	return qfalse;
}

static ID_INLINE qboolean AxisIsRotated( const vec3_t axis[3] ) {
	return !AxisCompare( axis, axisDefault );
}

static ID_INLINE void TransposeAxis( const vec3_t matrix[3], vec3_t transpose[3] ) {
	int i, j;
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			transpose[i][j] = matrix[j][i];
		}
	}
}

static ID_INLINE void NormalVectors( const vec3_t v, vec3_t left, vec3_t down ) {
	float d;

	d = v[0] * v[0] + v[1] * v[1];
	if ( !d ) {
		left[0] = 1;
		left[1] = 0;
		left[2] = 0;
	} else {
		d = 1.0f / sqrtf( d );
		left[0] = -v[1] * d;
		left[1] = v[0] * d;
		left[2] = 0;
	}
    CrossProduct( left, v, down );
}

static ID_INLINE void VectorRotateSelf( vec3_t vec, const vec3_t mat[3] ) {
	float x = mat[ 0 ][0] * vec[0] + mat[ 1 ][0] * vec[1] + mat[ 2 ][0] * vec[2];
	float y = mat[ 0 ][1] * vec[0] + mat[ 1 ][1] * vec[1] + mat[ 2 ][1] * vec[2];
	vec[2] = mat[ 0 ][2] * vec[0] + mat[ 1 ][2] * vec[1] + mat[ 2 ][2] * vec[2];
	vec[0] = x;
	vec[1] = y;
}

static ID_INLINE void MatrixRotateVector( const vec3_t vec, const vec3_t axis[3], vec3_t out ) {
    out[ 0 ] = axis[0][0] * vec[0] + axis[1][0] * vec[1] + axis[2][0] * vec[2];
    out[ 1 ] = axis[0][1] * vec[0] + axis[1][1] * vec[1] + axis[2][1] * vec[2];
    out[ 2 ] = axis[0][2] * vec[0] + axis[1][2] * vec[1] + axis[2][2] * vec[2];
}

static ID_INLINE qboolean FixDegenerateVectorNormal( vec3_t v ) {
	if ( v[0] == 0.0f ) {
		if ( v[1] == 0.0f ) {
			if ( v[2] > 0.0f ) {
				if ( v[2] != 1.0f ) {
					v[2] = 1.0f;
					return qtrue;
				}
			} else {
				if ( v[2] != -1.0f ) {
					v[2] = -1.0f;
					return qtrue;
				}
			}
			return qfalse;
		} else if ( v[2] == 0.0f ) {
			if ( v[1] > 0.0f ) {
				if ( v[1] != 1.0f ) {
					v[1] = 1.0f;
					return qtrue;
				}
			} else {
				if ( v[1] != -1.0f ) {
					v[1] = -1.0f;
					return qtrue;
				}
			}
			return qfalse;
		}
	} else if ( v[1] == 0.0f ) {
		if ( v[2] == 0.0f ) {
			if ( v[0] > 0.0f ) {
				if ( v[0] != 1.0f ) {
					v[0] = 1.0f;
					return qtrue;
				}
			} else {
				if ( v[0] != -1.0f ) {
					v[0] = -1.0f;
					return qtrue;
				}
			}
			return qfalse;
		}
	}
	if ( fabs( v[0] ) == 1.0f ) {
		if ( v[1] != 0.0f || v[2] != 0.0f ) {
			v[1] = v[2] = 0.0f;
			return qtrue;
		}
		return qfalse;
	} else if ( fabs( v[1] ) == 1.0f ) {
		if ( v[0] != 0.0f || v[2] != 0.0f ) {
			v[0] = v[2] = 0.0f;
			return qtrue;
		}
		return qfalse;
	} else if ( fabs( v[2] ) == 1.0f ) {
		if ( v[0] != 0.0f || v[1] != 0.0f ) {
			v[0] = v[1] = 0.0f;
			return qtrue;
		}
		return qfalse;
	}
	return qfalse;
}

//=====================================================

extern plane_t plane_origin;
#define plane_zero plane_origin

static ID_INLINE void PlaneInit( plane_t self ) {
}

static ID_INLINE void PlaneCopy( const plane_t self, plane_t out ) {
    out[0] = self[0];
    out[1] = self[1];
    out[2] = self[2];
    out[3] = self[3];
}

static ID_INLINE void PlaneFromCoefficients( plane_t self, float a, float b, float c, float d ) {
    VectorSet( self, a, b, c );
	self[3] = d;
}

static ID_INLINE void PlaneFromNormalAndDist( plane_t self, const vec3_t normal, const float dist ) {
    VectorCopy( normal, self );
	self[3] = -dist;
}

static ID_INLINE float PlaneGetByIndex( const plane_t self, int index ) {
	return ( self )[ index ];
}

static ID_INLINE float *PlaneGetRefByIndex( plane_t self, int index ) {
	return &( self[ index ] );
}

static ID_INLINE void PlaneNegate( const plane_t self, plane_t p ) {
    PlaneFromCoefficients( p, -self[0], -self[1], -self[2], -self[3] );
}

static ID_INLINE void PlaneFromPoint( plane_t self, const vec3_t v ) {
    VectorCopy( v, self );
    self[3] = 0.0f;
}

static ID_INLINE void PlaneAdd( const plane_t self, const plane_t p, plane_t out ) {
    VectorAdd( self, p, out );
    out[3] = self[3] + p[3];
}

static ID_INLINE void PlaneSubtract( const plane_t self, const plane_t p, plane_t out ) {
    VectorSubtract( self, p, out );
    out[3] = self[3] - p[3];
}

static ID_INLINE void PlaneRotateSelf( plane_t self, const vec3_t m[3] ) {
    VectorRotateSelf( self, m );
}

static ID_INLINE qboolean PlaneCompare( const plane_t self, const plane_t p ) {
    if ( self[0] == p[0]
            && self[1] == p[1]
            && self[2] == p[2]
            && self[3] == p[3] ) {
        return qtrue;
    }
    return qfalse;
}

static ID_INLINE qboolean PlaneCompareWithEpsilon( const plane_t self, const plane_t p, const float epsilon ) {
	if ( fabs( self[0] - p[0] ) > epsilon ) {
		return qfalse;
	}
			
	if ( fabs( self[1] - p[1] ) > epsilon ) {
		return qfalse;
	}

	if ( fabs( self[2] - p[2] ) > epsilon ) {
		return qfalse;
	}

	if ( fabs( self[3] - p[3] ) > epsilon ) {
		return qfalse;
	}

	return qtrue;
}

static ID_INLINE qboolean PlaneCompareWithSeparateEpsilons( const plane_t self, const plane_t p, const float normalEps, const float distEps ) {
	if ( fabs( self[3] - p[3] ) > distEps ) {
		return qfalse;
	}
	if ( !VectorCompareEpsilon( self, p, normalEps ) ) {
		return qfalse;
	}
	return qtrue;
}

static ID_INLINE void PlaneZero( plane_t self ) {
    VectorClear( self );
    self[3] = 0.0f;
}

static ID_INLINE void PlaneSetNormal( plane_t self, const vec3_t normal ) {
    VectorCopy( normal, self );
}

static ID_INLINE qboolean PlaneFixDegenerateNormal( plane_t self ) {
	return FixDegenerateVectorNormal( self );
}

static ID_INLINE qboolean PlaneFixDegeneracies( plane_t self, float distEpsilon ) {
	qboolean fixedNormal = PlaneFixDegenerateNormal( self );
	// only fix dist if the normal was degenerate
	if ( fixedNormal ) {
		if ( fabs( self[3] - rint( self[3] ) ) < distEpsilon ) {
			self[3] =  rint( self[3] );
		}
	}
	return fixedNormal;
}

static ID_INLINE float PlaneNormalize( plane_t self, qboolean fixDegenerate ) {
	float length = VectorNormalize( self );

	if ( fixDegenerate ) {
		FixDegenerateVectorNormal( self );
	}
	return length;
}

static ID_INLINE float PlaneGetDist( const plane_t self ) {
	return -self[3];
}

static ID_INLINE void PlaneSetDist( plane_t self, const float dist ) {
	self[3] = -dist;
}

static ID_INLINE qboolean PlaneInitWithPoints( plane_t self, const vec3_t p1, const vec3_t p2, const vec3_t p3, qboolean fixDegenerate ) {
    vec3_t dir1, dir2;

    VectorSubtract( p1, p2, dir1 );
    VectorSubtract( p3, p2, dir2 );

    CrossProduct( dir1, dir2, self );

	if ( PlaneNormalize( self, fixDegenerate ) == 0.0f ) {
		return qfalse;
	}
	self[3] = -DotProduct( self, p2 );
	return qtrue;
}

static ID_INLINE qboolean PlaneFromVecs( plane_t self, const vec3_t dir1, const vec3_t dir2, const vec3_t p, qboolean fixDegenerate ) {
    CrossProduct( dir1, dir2, self );
	if ( PlaneNormalize( self, fixDegenerate ) == 0.0f ) {
		return qfalse;
	}
	self[3] = -DotProduct( self, p );
	return qtrue;
}

static ID_INLINE void PlaneFitThroughPoint( plane_t self, const vec3_t p ) {
    self[3] = -DotProduct( self, p );
}

static ID_INLINE void PlaneTranslate( const plane_t self, const vec3_t translation, plane_t out ) {
    PlaneFromNormalAndDist( out, self, self[3] - DotProduct( translation, self ) );
}

static ID_INLINE void PlaneTranslateSelf( plane_t self, const vec3_t translation ) {
	self[3] -= DotProduct( translation, self );
}

static ID_INLINE void PlaneRotateAndTranslate( const plane_t self, const vec3_t origin, const vec3_t axis[3], plane_t out ) {
    MatrixRotateVector( self, axis, out );
	out[3] = self[3] + DotProduct( origin, self ) - DotProduct( origin, out );
}

static ID_INLINE void PlaneRotateAndTranslateSelf( plane_t self, const vec3_t origin, const vec3_t axis[3] ) {
	self[3] += DotProduct( origin, self );
    VectorRotateSelf( self, axis );
	self[3] -= DotProduct( origin, self );
}

static ID_INLINE float PlaneDistance( const plane_t self, const vec3_t v ) {
    return DotProduct( self, v ) + self[3];
}

static ID_INLINE int PlaneSide( const plane_t self, const vec3_t v, const float epsilon ) {
	float dist = PlaneDistance( self, v );
	if ( dist > epsilon ) {
		return PLANESIDE_FRONT;
	}
	else if ( dist < -epsilon ) {
		return PLANESIDE_BACK;
	}
	else {
		return PLANESIDE_ON;
	}
}

static ID_INLINE qboolean PlaneLineIntersection( const plane_t self, const vec3_t start, const vec3_t end ) {
	float d1, d2, fraction;

	d1 = DotProduct( self, start ) + self[3];
	d2 = DotProduct( self, end ) + self[3];
	if ( d1 == d2 ) {
		return qfalse;
	}
	if ( d1 > 0.0f && d2 > 0.0f ) {
		return qfalse;
	}
	if ( d1 < 0.0f && d2 < 0.0f ) {
		return qfalse;
	}
	fraction = ( d1 / ( d1 - d2 ) );
	return ( fraction >= 0.0f && fraction <= 1.0f );
}

static ID_INLINE qboolean PlaneRayIntersection( const plane_t self, const vec3_t start, const vec3_t dir, float *scale ) {
	float d1, d2;

	d1 = DotProduct( self, start ) + self[3];
	d2 = DotProduct( self, dir );
	if ( d2 == 0.0f ) {
		return qfalse;
	}
	*scale = -( d1 / d2 );
	return qtrue;
}

qboolean PlaneIntersection( const plane_t self, const plane_t plane, vec3_t start, vec3_t dir );

//=====================================================

typedef struct rotation_s {
	vec3_t			origin;			// origin of rotation
	vec3_t			vec;			// normalized vector to rotate around
	vec_t			angle;			// angle of rotation in degrees
	vec3_t			axis[3];			// rotation axis
	qboolean		axisValid;		// true if rotation axis is valid	
} rotation_t;

void RotationToMat3( rotation_t *r );
void RotationNormalize180( rotation_t *r );
void RotationNormalize360( rotation_t *r );

static ID_INLINE void RotationFromOriginAndVector( rotation_t *r, const vec3_t rotationOrigin, const vec3_t rotationVec, const float rotationAngle ) {
	VectorCopy( rotationOrigin, r->origin );
	VectorCopy( rotationVec, r->vec );
	r->angle = rotationAngle;
	r->axisValid = qfalse;
}

static ID_INLINE void RotationSetAngle( rotation_t *r, const float rotationAngle ) {
	r->angle = rotationAngle;
	r->axisValid = qfalse;
}

static ID_INLINE void RotationSetVec( rotation_t *r, const vec3_t vec ) {
	VectorCopy( vec, r->vec );
	r->axisValid = qfalse;
}

static ID_INLINE void RotationNegate( const rotation_t *r, rotation_t *r2 ) {
	RotationFromOriginAndVector( r2, r->origin, r->vec, -r->angle );
}

static ID_INLINE void RotationScale( rotation_t *r, const float s ) {
	r->angle *= s;
	r->axisValid = qfalse;
}

static ID_INLINE void RotationReCalculateMatrix( rotation_t *r ) {
	r->axisValid = qfalse;
	RotationToMat3( r );
}

static ID_INLINE void RotationRotatePoint( rotation_t *r, vec3_t point ) {
	vec3_t		tmp;

	if ( !r->axisValid ) {
		RotationToMat3( r );
	}

	VectorSubtract( point, r->origin, tmp );
	VectorRotate( tmp, r->axis, point );
	VectorAdd( point, r->origin, point );
}

static ID_INLINE void RotationRotatePoint2( rotation_t *r, vec3_t point, vec3_t dest ) {
	vec3_t		tmp;

	if ( !r->axisValid ) {
		RotationToMat3( r );
	}

	VectorSubtract( point, r->origin, tmp );
	VectorRotate( tmp, r->axis, point );
	VectorAdd( point, r->origin, point );
}

static ID_INLINE void RotationToAngularVelocity( rotation_t *r, vec3_t angularVelocity ) {
	VectorScale( r->vec, DEG2RAD( r->angle ), angularVelocity );
}

/*
===============================================================================

	Pluecker coordinate

===============================================================================
*/

typedef vec_t	vec6_t[6];

extern vec6_t pluecker_origin;

static ID_INLINE void PlueckerCopy( const vec6_t p, vec6_t out ) {
	out[0] = p[0];
	out[1] = p[1];
	out[2] = p[2];
	out[3] = p[3];
	out[4] = p[4];
	out[5] = p[5];
}

static ID_INLINE void PlueckerSet( vec6_t p, const float a1, const float a2, const float a3, const float a4, const float a5, const float a6 ) {
	p[0] = a1;
	p[1] = a2;
	p[2] = a3;
	p[3] = a4;
	p[4] = a5;
	p[5] = a6;
}

static ID_INLINE void PlueckerZero( vec6_t p ) {
	p[0] = p[1] = p[2] = p[3] = p[4] = p[5] = 0.0f;
}

static ID_INLINE void PlueckerFromLine( vec6_t p, const vec3_t start, const vec3_t end ) {
	p[0] = start[0] * end[1] - end[0] * start[1];
	p[1] = start[0] * end[2] - end[0] * start[2];
	p[2] = start[0] - end[0];
	p[3] = start[1] * end[2] - end[1] * start[2];
	p[4] = start[2] - end[2];
	p[5] = end[1] - start[1];
}

static ID_INLINE void PlueckerFromRay( vec6_t p, const vec3_t start, const vec3_t dir ) {
	p[0] = start[0] * dir[1] - dir[0] * start[1];
	p[1] = start[0] * dir[2] - dir[0] * start[2];
	p[2] = -dir[0];
	p[3] = start[1] * dir[2] - dir[1] * start[2];
	p[4] = -dir[2];
	p[5] = dir[1];
}

qboolean PlueckerFromPlanes( vec6_t p, const plane_t p1, const plane_t p2 );

static ID_INLINE qboolean PlueckerToLine( const vec6_t p, vec3_t start, vec3_t end ) {
	vec3_t dir1, dir2, tmp;
	float d;

	dir1[0] = p[3];
	dir1[1] = -p[1];
	dir1[2] = p[0];

	dir2[0] = -p[2];
	dir2[1] = p[5];
	dir2[2] = -p[4];

	d = DotProduct( dir2, dir2 );
	if ( d == 0.0f ) {
		return qfalse; // pluecker coordinate does not represent a line
	}

	CrossProduct( dir2, dir1, tmp );
	VectorScale( tmp, 1.0f / d, start );
	VectorAdd( start, dir2, end );
	return qtrue;
}

static ID_INLINE qboolean PlueckerToRay( const vec6_t p, vec3_t start, vec3_t dir ) {
	vec3_t dir1, tmp;
	float d;

	dir1[0] = p[3];
	dir1[1] = -p[1];
	dir1[2] = p[0];

	dir[0] = -p[2];
	dir[1] = p[5];
	dir[2] = -p[4];

	d = DotProduct( dir, dir );
	if ( d == 0.0f ) {
		return qfalse; // pluecker coordinate does not represent a line
	}

	CrossProduct( dir, dir1, tmp );
	VectorScale( tmp, 1.0f / d, start );
	return qtrue;
}

static ID_INLINE void PlueckerToDir( const vec6_t p, vec3_t dir ) {
	dir[0] = -p[2];
	dir[1] = p[5];
	dir[2] = -p[4];	
}

static ID_INLINE float PlueckerPermutedInnerProduct( const vec6_t p, const vec6_t a ) {
	return p[0] * a[4] + p[1] * a[5] + p[2] * a[3] + p[4] * a[0] + p[5] * a[1] + p[3] * a[2];
}

static ID_INLINE float PlueckerLength( const vec6_t p ) {
	return ( float )sqrt( p[5] * p[5] + p[4] * p[4] + p[2] * p[2] );
}

static ID_INLINE float PlueckerLengthSqr( const vec6_t p ) {
	return ( p[5] * p[5] + p[4] * p[4] + p[2] * p[2] );
}

static ID_INLINE void PlueckerNormalize( const vec6_t p, vec6_t out ) {
	float d;

	d = PlueckerLengthSqr( p );
	if ( d == 0.0f ) {
		PlueckerCopy( p, out );
		return; // pluecker coordinate does not represent a line
	}
	d = 1.0f / ( float )sqrt( d );
	PlueckerSet( out, p[0]*d, p[1]*d, p[2]*d, p[3]*d, p[4]*d, p[5]*d );
}

static ID_INLINE float PlueckerNormalizeSelf( vec6_t p ) {
	float l, d;

	l = PlueckerLengthSqr( p );
	if ( l == 0.0f ) {
		return l; // pluecker coordinate does not represent a line
	}
	d = 1.0f / ( float )sqrt( l );
	p[0] *= d;
	p[1] *= d;
	p[2] *= d;
	p[3] *= d;
	p[4] *= d;
	p[5] *= d;
	return d * l;
}

float PlueckerDistance3DSqr( const vec6_t p, const vec6_t a );


/*
===============================================================================

	A trace model is an arbitrary polygonal model which is used by the
	collision detection system to find collisions, contacts or the contents
	of a volume. For collision detection speed reasons the number of vertices
	and edges are limited. The trace model can have any shape. However convex
	models are usually preferred.

===============================================================================
*/

// trace model type
typedef enum {
	TRM_INVALID,		// invalid trm
	TRM_BOX,			// box
	TRM_OCTAHEDRON,		// octahedron
	TRM_DODECAHEDRON,	// dodecahedron
	TRM_CYLINDER,		// cylinder approximation
	TRM_CONE,			// cone approximation
	TRM_BONE,			// two tetrahedrons attached to each other
	TRM_POLYGON,		// arbitrary convex polygon
	TRM_POLYGONVOLUME,	// volume for arbitrary convex polygon
	TRM_CUSTOM			// loaded from map model or ASE/LWO
} traceModelType_t;

// these are bit cache limits
#define MAX_TRACEMODEL_VERTS		32
#define MAX_TRACEMODEL_EDGES		32
#define MAX_TRACEMODEL_POLYS		16
#define MAX_TRACEMODEL_POLYEDGES	16

typedef vec3_t traceModelVert_t;

typedef struct {
	int					v[2];
	vec3_t				normal;
} traceModelEdge_t;

typedef struct {
	vec3_t				normal;
	float				dist;
	vec3_t				bounds[2];
	int					numEdges;
	int					edges[MAX_TRACEMODEL_POLYEDGES];
} traceModelPoly_t;

typedef struct {
	traceModelType_t	type;
	int					numVerts;
	traceModelVert_t	verts[MAX_TRACEMODEL_VERTS];
	int					numEdges;
	traceModelEdge_t	edges[MAX_TRACEMODEL_EDGES+1];
	int					numPolys;
	traceModelPoly_t	polys[MAX_TRACEMODEL_POLYS];
	vec3_t				offset;				// offset to center of model
	vec3_t				bounds[2];			// bounds of model
	qboolean			isConvex;			// true when model is convex
} traceModel_t;

					// axial box
void				TraceModelSetupBox( traceModel_t *mod, const vec3_t boxBounds[2] );
void				TraceModelSetupBox2( traceModel_t *mod, const float size );
					// octahedron
void				TraceModelSetupOctahedron( traceModel_t *mod, const vec3_t octBounds[2] );
void				TraceModelSetupOctahedron2( traceModel_t *mod, const float size );
					// dodecahedron
void				TraceModelSetupDodecahedron( traceModel_t *mod, const vec3_t dodBounds[2] );
void				TraceModelSetupDodecahedron2( traceModel_t *mod, const float size );
					// cylinder approximation
void				TraceModelSetupCylinder( traceModel_t *mod, const vec3_t cylBounds[2], const int numSides );
void				TraceModelSetupCylinder2( traceModel_t *mod, const float height, const float width, const int numSides );
					// cone approximation
void				TraceModelSetupCone( traceModel_t *mod, const vec3_t coneBounds[2], const int numSides );
void				TraceModelSetupCone2( traceModel_t *mod, const float height, const float width, const int numSides );
					// two tetrahedrons attached to each other
void				TraceModelSetupBone( traceModel_t *mod, const float length, const float width );
					// arbitrary convex polygon
void				TraceModelSetupPolygon( traceModel_t *mod, const vec3_t *v, const int count );

// generate edge normals
int					TraceModelGenerateEdgeNormals( traceModel_t *mod );

// translate the trm
void				TraceModelTranslate( traceModel_t *mod, const vec3_t translation );
// rotate the trm
void				TraceModelRotate( traceModel_t *mod, const vec3_t rotation[3] );
// shrink the model m units on all sides
void				TraceModelShrink( traceModel_t *mod, const float m );
// compare
qboolean			TraceModelCompare( const traceModel_t *mod, const traceModel_t *trm );
// get the area of one of the polygons
float				TraceModelGetPolygonArea( const traceModel_t *mod, int polyNum );
// get the silhouette edges
int					TraceModelGetProjectionSilhouetteEdges( const traceModel_t *mod, const vec3_t projectionOrigin, int silEdges[MAX_TRACEMODEL_EDGES] );
int					TraceModelGetParallelProjectionSilhouetteEdges( const traceModel_t *mod, const vec3_t projectionDir, int silEdges[MAX_TRACEMODEL_EDGES] );
// calculate mass properties assuming an uniform density
void				TraceModelGetMassProperties( const traceModel_t *mod, const float density, float *mass, vec3_t centerOfMass, vec3_t inertiaTensor[3] );

/*
===============================================================================

	A winding is an arbitrary convex polygon defined by an array of points.

	fixedWinding_t is a fixed buffer size winding not using
	memory allocations.

	When an operation would overflow the fixed buffer a warning
	is printed and the operation is safely cancelled.

===============================================================================
*/

#define	SIDE_FRONT					0
#define	SIDE_BACK					1
#define	SIDE_ON						2
#define	SIDE_CROSS					3

#define	MAX_POINTS_ON_WINDING	64

typedef struct
{
	int		numPoints;
	vec5_t	points[MAX_POINTS_ON_WINDING];		// variable sized
} fixedWinding_t;

void CopyFixedWinding( const fixedWinding_t *src, fixedWinding_t *dst );
void ClearFixedWinding( fixedWinding_t *w );

void FixedWindingBaseForPlane( fixedWinding_t *w, const plane_t plane );

// splits the winding in a back and front part, 'this' becomes the front part
// returns a SIDE_?
int SplitFixedWinding( fixedWinding_t *w, fixedWinding_t *back, const plane_t plane, const float epsilon );

void FixedWindingBounds( const fixedWinding_t *w, vec3_t bounds[2] );

qboolean FixedWindingIsHuge( const fixedWinding_t *w );

qboolean ClipFixedWindingInPlace( fixedWinding_t *w, const plane_t plane, const float epsilon, const qboolean keepOn );

static ID_INLINE void AddPointToFixedWinding( fixedWinding_t *w, const vec5_t v ) {
	if ( w->numPoints+1 >= MAX_POINTS_ON_WINDING ) {
		return;
	}
	VectorCopy( v, w->points[w->numPoints] );
	w->points[w->numPoints][3] = v[3];
	w->points[w->numPoints][4] = v[4];
	w->numPoints++;
}

void ReverseFixedWindingSelf( fixedWinding_t *w );

#endif /* !__Q_EXTRAMATH_H__ */
