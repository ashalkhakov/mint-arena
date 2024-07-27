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

#include "idlib_local.h"
#include "q_extramath.h"

vec6_t pluecker_origin = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
plane_t plane_origin = { 0.0f, 0.0f, 0.0f, 0.0f };

/*
=============
VectorToAxis
=============
*/
void VectorToAxis( const vec3_t v, vec3_t mat[3] ) {
	float	d;

    VectorCopy( v, mat[0] );
	d = v[0] * v[0] + v[1] * v[1];
	if ( !d ) {
		mat[1][0] = 1.0f;
		mat[1][1] = 0.0f;
		mat[1][2] = 0.0f;
	} else {
		d = 1.0f / sqrt( d );
		mat[1][0] = -v[1] * d;
		mat[1][1] = v[0] * d;
		mat[1][2] = 0.0f;
	}
    CrossProduct( v, mat[1], mat[2] );
}

/*
================
PlaneIntersection
================
*/
qboolean PlaneIntersection( const plane_t self, const plane_t plane, vec3_t start, vec3_t dir ) {
	double n00, n01, n11, det, invDet, f0, f1;
    int i;

	n00 = VectorLengthSquared( self );
	n01 = DotProduct( self, plane );
	n11 = VectorLengthSquared( plane );
	det = n00 * n11 - n01 * n01;

	if ( fabs(det) < 1e-6f ) {
		return qfalse;
	}

	invDet = 1.0f / det;
	f0 = ( n01 * plane[3] - n11 * self[3] ) * invDet;
	f1 = ( n01 * self[3] - n00 * plane[3] ) * invDet;

    CrossProduct( self, plane, dir );
    for ( i = 0; i < 3; i++ ) {
    	start[i] = f0 * self[i] + f1 * plane[i];
    }
	return qtrue;
}

/*
============
BoundsGetRadius
============
*/
float BoundsGetRadius( const vec3_t bounds[2] ) {
	int		i;
	float	total, b0, b1;

	total = 0.0f;
	for ( i = 0; i < 3; i++ ) {
		b0 = (float)fabs( bounds[0][i] );
		b1 = (float)fabs( bounds[1][i] );
		if ( b0 > b1 ) {
			total += b0 * b0;
		} else {
			total += b1 * b1;
		}
	}
	return sqrt( total );
}

/*
================
BoundsDistanceToPlane
================
*/
float BoundsDistanceToPlane( vec3_t bounds[2], const plane_t plane ) {
	vec3_t center;
	float d1, d2;

    VectorAdd( bounds[0], bounds[1], center );
    VectorScale( center, 0.5f, center );

	d1 = PlaneDistance( plane, center );
	d2 = fabs( ( bounds[1][0] - center[0] ) * plane[0] ) +
			fabs( ( bounds[1][1] - center[1] ) * plane[1] ) +
				fabs( ( bounds[1][2] - center[2] ) * plane[2] );

	if ( d1 - d2 > 0.0f ) {
		return d1 - d2;
	}
	if ( d1 + d2 < 0.0f ) {
		return d1 + d2;
	}
	return 0.0f;
}

qboolean AddBoundsToBounds( const vec3_t abounds[2], vec3_t bounds[2] ) {
	qboolean expanded = qfalse;
	if ( abounds[0][0] < bounds[0][0] ) {
		bounds[0][0] = abounds[0][0];
		expanded = qtrue;
	}
	if ( abounds[0][1] < bounds[0][1] ) {
		bounds[0][1] = abounds[0][1];
		expanded = qtrue;
	}
	if ( abounds[0][2] < bounds[0][2] ) {
		bounds[0][2] = abounds[0][2];
		expanded = qtrue;
	}
	if ( abounds[1][0] > bounds[1][0] ) {
		bounds[1][0] = abounds[1][0];
		expanded = qtrue;
	}
	if ( abounds[1][1] > bounds[1][1] ) {
		bounds[1][1] = abounds[1][1];
		expanded = qtrue;
	}
	if ( abounds[1][2] > bounds[1][2] ) {
		bounds[1][2] = abounds[1][2];
		expanded = qtrue;
	}
	return expanded;
}

/*
============
BoundsFromTransformedBounds
============
*/
void BoundsFromTransformedBounds( vec3_t self[2], const vec3_t bounds[2], const vec3_t origin, const vec3_t axis[3] ) {
	int i;
	vec3_t center, extents, rotatedExtents;
    vec3_t tmp;

    for ( i = 0; i < 3; i++ ) {
        center[i] = (bounds[0][i] + bounds[1][i]) * 0.5f;
        extents[i] = bounds[1][i] - center[i];
    }

	for ( i = 0; i < 3; i++ ) {
		rotatedExtents[i] = fabs( extents[0] * axis[0][i] ) +
							fabs( extents[1] * axis[1][i] ) +
							fabs( extents[2] * axis[2][i] );
	}

    MatrixRotateVector( center, axis, tmp );
    VectorAdd( origin, tmp, center );
	VectorSubtract( center, rotatedExtents, self[0] );
	VectorAdd( center, rotatedExtents, self[1] );
}

/*
============
BoundsFromPointTranslation

  Most tight bounds for the translational movement of the given point.
============
*/
void BoundsFromPointTranslation( vec3_t self[2], const vec3_t point, const vec3_t translation ) {
	int i;

	for ( i = 0; i < 3; i++ ) {
		if ( translation[i] < 0.0f ) {
			self[0][i] = point[i] + translation[i];
			self[1][i] = point[i];
		}
		else {
			self[0][i] = point[i];
			self[1][i] = point[i] + translation[i];
		}
	}
}

/*
============
BoundsFromBoundsTranslation2

  Most tight bounds for the translational movement of the given bounds.
============
*/
void BoundsFromBoundsTranslation( vec3_t self[2], const vec3_t bounds[2], const vec3_t origin, const vec3_t axis[3], const vec3_t translation ) {
	int i;

	if ( AxisIsRotated( axis ) ) {
		BoundsFromTransformedBounds( self, bounds, origin, axis );
	}
	else {
		VectorAdd( bounds[0], origin, self[0] );
		VectorAdd( bounds[1], origin, self[1] );
	}
	for ( i = 0; i < 3; i++ ) {
		if ( translation[i] < 0.0f ) {
			self[0][i] += translation[i];
		}
		else {
    		self[1][i] += translation[i];
		}
	}
}

/*
================
BoundsForPointRotation

  only for rotations < 180 degrees
================
*/
void BoundsForPointRotation( const vec3_t start, const rotation_t *rotation, vec3_t bounds[2] ) {
	int i;
	float radiusSqr;
	vec3_t v1, v2;
	vec3_t origin, axis, end;
    vec3_t tmp;

    RotationRotatePoint2( rotation, start, end );
    VectorCopy( rotation->vec, axis );
    VectorSubtract( start, rotation->origin, tmp );
    VectorMA( rotation->origin, DotProduct( axis, tmp ), axis, origin );
    VectorSubtract( start, origin, v1 );
    VectorSubtract( end, origin, v2 );
	radiusSqr = VectorLengthSquared( v1 );
    CrossProduct( v1, axis, tmp );
    VectorCopy( tmp, v1 );
    CrossProduct( v2, axis, tmp );
    VectorCopy( tmp, v2 );

	for ( i = 0; i < 3; i++ ) {
		// if the derivative changes sign along this axis during the rotation from start to end
		if ( ( v1[i] > 0.0f && v2[i] < 0.0f ) || ( v1[i] < 0.0f && v2[i] > 0.0f ) ) {
			if ( ( 0.5f * (start[i] + end[i]) - origin[i] ) > 0.0f ) {
				bounds[0][i] = ( start[i] < end[i] ) ? start[i] : end[i];
				bounds[1][i] = origin[i] + sqrt( radiusSqr * ( 1.0f - axis[i] * axis[i] ) );
			}
			else {
				bounds[0][i] = origin[i] - sqrt( radiusSqr * ( 1.0f - axis[i] * axis[i] ) );
				bounds[1][i] = ( start[i] > end[i] ) ? start[i] : end[i];
			}
		}
		else if ( start[i] > end[i] ) {
			bounds[0][i] = end[i];
			bounds[1][i] = start[i];
		}
		else {
			bounds[0][i] = start[i];
			bounds[1][i] = end[i];
		}
	}
}

/*
============
BoundsFromPointRotation

  Most tight bounds for the rotational movement of the given point.
============
*/
void BoundsFromPointRotation( vec3_t self[2], const vec3_t point, const rotation_t *rotation ) {
	float radius;
    vec3_t dir;

	if ( fabs( rotation->angle ) < 180.0f ) {
        BoundsForPointRotation( point, rotation, self );
	}
	else {
        VectorSubtract( point, rotation->origin, dir );

		radius = VectorLength( dir );

		// FIXME: these bounds are usually way larger
		VectorSet( self[0], -radius, -radius, -radius );
		VectorSet( self[1], radius, radius, radius );
	}
}

/*
============
BoundsFromBoundsRotation

  Most tight bounds for the rotational movement of the given bounds.
============
*/
void BoundsFromBoundsRotation( vec3_t self[2], const vec3_t bounds[2], const vec3_t origin, const vec3_t axis[3], const rotation_t *rotation ) {
	int i;
	float radius;
	vec3_t point, tmp, tmp2;
	vec3_t rBounds[2];
    vec3_t tmpBounds[2];

	if ( fabs( rotation->angle ) < 180.0f ) {
        MatrixRotateVector( bounds[0], axis, tmp );
        VectorAdd( tmp, origin, point );

        BoundsForPointRotation( self, point, rotation );
		for ( i = 1; i < 8; i++ ) {
			point[0] = bounds[(i^(i>>1))&1][0];
			point[1] = bounds[(i>>1)&1][1];
			point[2] = bounds[(i>>2)&1][2];
            MatrixRotateVector( point, axis, tmp );
            VectorAdd( tmp, origin, tmp );
            BoundsForPointRotation( tmp, rotation, tmpBounds );
            AddBoundsToBounds( tmpBounds, self );
		}
	}
	else {

        VectorSubtract( bounds[1], bounds[0], point );
        VectorScale( point, 0.5f, point );
        VectorSubtract( bounds[1], point, tmp );
        VectorSubtract( point, rotation->origin, tmp2 );

		radius = VectorLength( tmp ) + VectorLength( tmp2 );

		// FIXME: these bounds are usually way larger
		VectorSet( self[0], -radius, -radius, -radius );
		VectorSet( self[1], radius, radius, radius );
	}
}

/*
==================
BoxOnPlaneSideSlow

Returns 1, 2, or 1 + 2
==================
*/
int BoxOnPlaneSideSlow( vec3_t mins, vec3_t maxs, const plane_t plane, const float epsilon ) {
	vec3_t center;
	float d1, d2;

    VectorAdd( mins, maxs, center );
    VectorScale( center, 0.5f, center );

	d1 = PlaneDistance( plane, center );
	d2 = fabs( ( maxs[0] - center[0] ) * plane[0] ) +
			fabs( ( maxs[1] - center[1] ) * plane[1] ) +
				fabs( ( maxs[2] - center[2] ) * plane[2] );

	if ( d1 - d2 > epsilon ) {
		return PLANESIDE_FRONT; // front
	}
	if ( d1 + d2 < -epsilon ) {
		return PLANESIDE_BACK; // back
	}
	return PLANESIDE_CROSS; // front + back
}

/*
============
RotationToMat3
============
*/
void RotationToMat3( rotation_t *r ) {
	float wx, wy, wz;
	float xx, yy, yz;
	float xy, xz, zz;
	float x2, y2, z2;
	float a, c, s, x, y, z;

	if ( r->axisValid ) {
        return;
	}

	a = r->angle * ( DEG2RAD( 1.0f ) * 0.5f );
    s = sin( a );
    c = cos( a );

	x = r->vec[0] * s;
	y = r->vec[1] * s;
	z = r->vec[2] * s;

	x2 = x + x;
	y2 = y + y;
	z2 = z + z;

	xx = x * x2;
	xy = x * y2;
	xz = x * z2;

	yy = y * y2;
	yz = y * z2;
	zz = z * z2;

	wx = c * x2;
	wy = c * y2;
	wz = c * z2;

	r->axis[ 0 ][ 0 ] = 1.0f - ( yy + zz );
	r->axis[ 0 ][ 1 ] = xy - wz;
	r->axis[ 0 ][ 2 ] = xz + wy;

	r->axis[ 1 ][ 0 ] = xy + wz;
	r->axis[ 1 ][ 1 ] = 1.0f - ( xx + zz );
	r->axis[ 1 ][ 2 ] = yz - wx;

	r->axis[ 2 ][ 0 ] = xz - wy;
	r->axis[ 2 ][ 1 ] = yz + wx;
	r->axis[ 2 ][ 2 ] = 1.0f - ( xx + yy );

	r->axisValid = qtrue;
}

/*
============
RotationNormalize180
============
*/
void RotationNormalize180( rotation_t *r ) {
	r->angle -= floor( r->angle / 360.0f ) * 360.0f;
	if ( r->angle > 180.0f ) {
		r->angle -= 360.0f;
	}
	else if ( r->angle < -180.0f ) {
		r->angle += 360.0f;
	}
}

/*
============
RotationNormalize360
============
*/
void RotationNormalize360( rotation_t *r ) {
	r->angle -= floor( r->angle / 360.0f ) * 360.0f;
	if ( r->angle > 360.0f ) {
		r->angle -= 360.0f;
	}
	else if ( r->angle < 0.0f ) {
		r->angle += 360.0f;
	}
}

//===============================================================================

/*
================
PlueckerFromPlanes

  pluecker coordinate for the intersection of two planes
================
*/
qboolean PlueckerFromPlanes( vec6_t p, const plane_t p1, const plane_t p2 ) {

	p[0] = -( p1[2] * -p2[3] - p2[2] * -p1[3] );
	p[1] = -( p2[1] * -p1[3] - p1[1] * -p2[3] );
	p[2] = p1[1] * p2[2] - p2[1] * p1[2];

	p[3] = -( p1[0] * -p2[3] - p2[0] * -p1[3] );
	p[4] = p1[0] * p2[1] - p2[0] * p1[1];
	p[5] = p1[0] * p2[2] - p2[0] * p1[2];

	return ( p[2] != 0.0f || p[5] != 0.0f || p[4] != 0.0f );
}

/*
================
PlueckerDistance3DSqr

  calculates square of shortest distance between the two
  3D lines represented by their pluecker coordinates
================
*/
float PlueckerDistance3DSqr( const vec6_t p, const vec6_t a ) {
	float d, s;
	vec3_t dir;

	dir[0] = -a[5] *  p[4] -  a[4] * -p[5];
	dir[1] =  a[4] *  p[2] -  a[2] *  p[4];
	dir[2] =  a[2] * -p[5] - -a[5] *  p[2];
	if ( dir[0] == 0.0f && dir[1] == 0.0f && dir[2] == 0.0f ) {
		return -1.0f;	// FIXME: implement for parallel lines
	}
	d = a[4] * ( p[2]*dir[1] - -p[5]*dir[0]) +
		a[5] * ( p[2]*dir[2] -  p[4]*dir[0]) +
		a[2] * (-p[5]*dir[2] -  p[4]*dir[1]);
	s = PlueckerPermutedInnerProduct( p, a ) / d;
	return DotProduct( dir, dir ) * ( s * s );
}

//===============================================================================

/*
============
TraceModelInitBox

  Initialize size independent box.
============
*/
static void TraceModelInitBox( traceModel_t *mod ) {
	int i;

	mod->type = TRM_BOX;
	mod->numVerts = 8;
	mod->numEdges = 12;
	mod->numPolys = 6;

	// set box edges
	for ( i = 0; i < 4; i++ ) {
		mod->edges[ i + 1 ].v[0] = i;
		mod->edges[ i + 1 ].v[1] = (i + 1) & 3;
		mod->edges[ i + 5 ].v[0] = 4 + i;
		mod->edges[ i + 5 ].v[1] = 4 + ((i + 1) & 3);
		mod->edges[ i + 9 ].v[0] = i;
		mod->edges[ i + 9 ].v[1] = 4 + i;
	}

	// all edges of a polygon go counter clockwise
	mod->polys[0].numEdges = 4;
	mod->polys[0].edges[0] = -4;
	mod->polys[0].edges[1] = -3;
	mod->polys[0].edges[2] = -2;
	mod->polys[0].edges[3] = -1;
	VectorSet( mod->polys[0].normal, 0.0f, 0.0f, -1.0f );

	mod->polys[1].numEdges = 4;
	mod->polys[1].edges[0] = 5;
	mod->polys[1].edges[1] = 6;
	mod->polys[1].edges[2] = 7;
	mod->polys[1].edges[3] = 8;
	VectorSet( mod->polys[1].normal, 0.0f, 0.0f, 1.0f );

	mod->polys[2].numEdges = 4;
	mod->polys[2].edges[0] = 1;
	mod->polys[2].edges[1] = 10;
	mod->polys[2].edges[2] = -5;
	mod->polys[2].edges[3] = -9;
	VectorSet( mod->polys[2].normal, 0.0f, -1.0f,  0.0f );

	mod->polys[3].numEdges = 4;
	mod->polys[3].edges[0] = 2;
	mod->polys[3].edges[1] = 11;
	mod->polys[3].edges[2] = -6;
	mod->polys[3].edges[3] = -10;
	VectorSet( mod->polys[3].normal, 1.0f,  0.0f,  0.0f );

	mod->polys[4].numEdges = 4;
	mod->polys[4].edges[0] = 3;
	mod->polys[4].edges[1] = 12;
	mod->polys[4].edges[2] = -7;
	mod->polys[4].edges[3] = -11;
	VectorSet( mod->polys[4].normal, 0.0f,  1.0f,  0.0f );

	mod->polys[5].numEdges = 4;
	mod->polys[5].edges[0] = 4;
	mod->polys[5].edges[1] = 9;
	mod->polys[5].edges[2] = -8;
	mod->polys[5].edges[3] = -12;
	VectorSet( mod->polys[5].normal, -1.0f,  0.0f,  0.0f );

	// convex model
	mod->isConvex = qtrue;

	TraceModelGenerateEdgeNormals( mod );
}

/*
============
TraceModelSetupBox
============
*/
void TraceModelSetupBox( traceModel_t *mod, const vec3_t boxBounds[2] ) {
	int i;

	if ( mod->type != TRM_BOX ) {
		TraceModelInitBox( mod );
	}
	// offset to center
    VectorAdd( boxBounds[0], boxBounds[1], mod->offset );
    VectorScale( mod->offset, 0.5f, mod->offset );
	// set box vertices
	for ( i = 0; i < 8; i++ ) {
		mod->verts[i][0] = boxBounds[(i^(i>>1))&1][0];
		mod->verts[i][1] = boxBounds[(i>>1)&1][1];
		mod->verts[i][2] = boxBounds[(i>>2)&1][2];
	}
	// set polygon plane distances
	mod->polys[0].dist = -boxBounds[0][2];
	mod->polys[1].dist = boxBounds[1][2];
	mod->polys[2].dist = -boxBounds[0][1];
	mod->polys[3].dist = boxBounds[1][0];
	mod->polys[4].dist = boxBounds[1][1];
	mod->polys[5].dist = -boxBounds[0][0];
	// set polygon bounds
	for ( i = 0; i < 6; i++ ) {
	    VectorCopy( boxBounds[0], mod->polys[i].bounds[0] );
        VectorCopy( boxBounds[1], mod->polys[i].bounds[1] );
	}
	mod->polys[0].bounds[1][2] = boxBounds[0][2];
	mod->polys[1].bounds[0][2] = boxBounds[1][2];
	mod->polys[2].bounds[1][1] = boxBounds[0][1];
	mod->polys[3].bounds[0][0] = boxBounds[1][0];
	mod->polys[4].bounds[0][1] = boxBounds[1][1];
	mod->polys[5].bounds[1][0] = boxBounds[0][0];

	VectorCopy( boxBounds[0], mod->bounds[0] );
    VectorCopy( boxBounds[1], mod->bounds[1] );
}

/*
============
TraceModelSetupBox2

  The origin is placed at the center of the cube.
============
*/
void TraceModelSetupBox2( traceModel_t *mod, const float size ) {
	vec3_t boxBounds[2];
	float halfSize;

	halfSize = size * 0.5f;
	VectorSet( boxBounds[0], -halfSize, -halfSize, -halfSize );
	VectorSet( boxBounds[1], halfSize, halfSize, halfSize );
	TraceModelSetupBox( mod, boxBounds );
}


/*
============
TraceModelInitOctahedron

  Initialize size independent octahedron.
============
*/
void TraceModelInitOctahedron( traceModel_t *mod ) {

	mod->type = TRM_OCTAHEDRON;
	mod->numVerts = 6;
	mod->numEdges = 12;
	mod->numPolys = 8;

	// set edges
	mod->edges[ 1].v[0] =  4; mod->edges[ 1].v[1] =  0;
	mod->edges[ 2].v[0] =  0; mod->edges[ 2].v[1] =  2;
	mod->edges[ 3].v[0] =  2; mod->edges[ 3].v[1] =  4;
	mod->edges[ 4].v[0] =  2; mod->edges[ 4].v[1] =  1;
	mod->edges[ 5].v[0] =  1; mod->edges[ 5].v[1] =  4;
	mod->edges[ 6].v[0] =  1; mod->edges[ 6].v[1] =  3;
	mod->edges[ 7].v[0] =  3; mod->edges[ 7].v[1] =  4;
	mod->edges[ 8].v[0] =  3; mod->edges[ 8].v[1] =  0;
	mod->edges[ 9].v[0] =  5; mod->edges[ 9].v[1] =  2;
	mod->edges[10].v[0] =  0; mod->edges[10].v[1] =  5;
	mod->edges[11].v[0] =  5; mod->edges[11].v[1] =  1;
	mod->edges[12].v[0] =  5; mod->edges[12].v[1] =  3;

	// all edges of a polygon go counter clockwise
	mod->polys[0].numEdges = 3;
	mod->polys[0].edges[0] = 1;
	mod->polys[0].edges[1] = 2;
	mod->polys[0].edges[2] = 3;

	mod->polys[1].numEdges = 3;
	mod->polys[1].edges[0] = -3;
	mod->polys[1].edges[1] = 4;
	mod->polys[1].edges[2] = 5;

	mod->polys[2].numEdges = 3;
	mod->polys[2].edges[0] = -5;
	mod->polys[2].edges[1] = 6;
	mod->polys[2].edges[2] = 7;

	mod->polys[3].numEdges = 3;
	mod->polys[3].edges[0] = -7;
	mod->polys[3].edges[1] = 8;
	mod->polys[3].edges[2] = -1;

	mod->polys[4].numEdges = 3;
	mod->polys[4].edges[0] = 9;
	mod->polys[4].edges[1] = -2;
	mod->polys[4].edges[2] = 10;

	mod->polys[5].numEdges = 3;
	mod->polys[5].edges[0] = 11;
	mod->polys[5].edges[1] = -4;
	mod->polys[5].edges[2] = -9;

	mod->polys[6].numEdges = 3;
	mod->polys[6].edges[0] = 12;
	mod->polys[6].edges[1] = -6;
	mod->polys[6].edges[2] = -11;

	mod->polys[7].numEdges = 3;
	mod->polys[7].edges[0] = -10;
	mod->polys[7].edges[1] = -8;
	mod->polys[7].edges[2] = -12;

	// convex model
	mod->isConvex = qtrue;
}

/*
============
TraceModelInitDodecahedron

  Initialize size independent dodecahedron.
============
*/
void TraceModelInitDodecahedron( traceModel_t *mod ) {
	traceModelEdge_t *edges;
	traceModelPoly_t *polys;

	mod->type = TRM_DODECAHEDRON;
	mod->numVerts = 20;
	mod->numEdges = 30;
	mod->numPolys = 12;

	edges = mod->edges;
	polys = mod->polys;

	// set edges
	edges[ 1].v[0] =  0; edges[ 1].v[1] =  8;
	edges[ 2].v[0] =  8; edges[ 2].v[1] =  9;
	edges[ 3].v[0] =  9; edges[ 3].v[1] =  4;
	edges[ 4].v[0] =  4; edges[ 4].v[1] = 16;
	edges[ 5].v[0] = 16; edges[ 5].v[1] =  0;
	edges[ 6].v[0] = 16; edges[ 6].v[1] = 17;
	edges[ 7].v[0] = 17; edges[ 7].v[1] =  2;
	edges[ 8].v[0] =  2; edges[ 8].v[1] = 12;
	edges[ 9].v[0] = 12; edges[ 9].v[1] =  0;
	edges[10].v[0] =  2; edges[10].v[1] = 10;
	edges[11].v[0] = 10; edges[11].v[1] =  3;
	edges[12].v[0] =  3; edges[12].v[1] = 13;
	edges[13].v[0] = 13; edges[13].v[1] = 12;
	edges[14].v[0] =  9; edges[14].v[1] =  5;
	edges[15].v[0] =  5; edges[15].v[1] = 15;
	edges[16].v[0] = 15; edges[16].v[1] = 14;
	edges[17].v[0] = 14; edges[17].v[1] =  4;
	edges[18].v[0] =  3; edges[18].v[1] = 19;
	edges[19].v[0] = 19; edges[19].v[1] = 18;
	edges[20].v[0] = 18; edges[20].v[1] =  1;
	edges[21].v[0] =  1; edges[21].v[1] = 13;
	edges[22].v[0] =  7; edges[22].v[1] = 11;
	edges[23].v[0] = 11; edges[23].v[1] =  6;
	edges[24].v[0] =  6; edges[24].v[1] = 14;
	edges[25].v[0] = 15; edges[25].v[1] =  7;
	edges[26].v[0] =  1; edges[26].v[1] =  8;
	edges[27].v[0] = 18; edges[27].v[1] =  5;
	edges[28].v[0] =  6; edges[28].v[1] = 17;
	edges[29].v[0] = 11; edges[29].v[1] = 10;
	edges[30].v[0] = 19; edges[30].v[1] =  7;

	// all edges of a polygon go counter clockwise
	polys[0].numEdges = 5;
	polys[0].edges[0] = 1;
	polys[0].edges[1] = 2;
	polys[0].edges[2] = 3;
	polys[0].edges[3] = 4;
	polys[0].edges[4] = 5;

	polys[1].numEdges = 5;
	polys[1].edges[0] = -5;
	polys[1].edges[1] = 6;
	polys[1].edges[2] = 7;
	polys[1].edges[3] = 8;
	polys[1].edges[4] = 9;

	polys[2].numEdges = 5;
	polys[2].edges[0] = -8;
	polys[2].edges[1] = 10;
	polys[2].edges[2] = 11;
	polys[2].edges[3] = 12;
	polys[2].edges[4] = 13;

	polys[3].numEdges = 5;
	polys[3].edges[0] = 14;
	polys[3].edges[1] = 15;
	polys[3].edges[2] = 16;
	polys[3].edges[3] = 17;
	polys[3].edges[4] = -3;

	polys[4].numEdges = 5;
	polys[4].edges[0] = 18;
	polys[4].edges[1] = 19;
	polys[4].edges[2] = 20;
	polys[4].edges[3] = 21;
	polys[4].edges[4] = -12;

	polys[5].numEdges = 5;
	polys[5].edges[0] = 22;
	polys[5].edges[1] = 23;
	polys[5].edges[2] = 24;
	polys[5].edges[3] = -16;
	polys[5].edges[4] = 25;

	polys[6].numEdges = 5;
	polys[6].edges[0] = -9;
	polys[6].edges[1] = -13;
	polys[6].edges[2] = -21;
	polys[6].edges[3] = 26;
	polys[6].edges[4] = -1;

	polys[7].numEdges = 5;
	polys[7].edges[0] = -26;
	polys[7].edges[1] = -20;
	polys[7].edges[2] = 27;
	polys[7].edges[3] = -14;
	polys[7].edges[4] = -2;

	polys[8].numEdges = 5;
	polys[8].edges[0] = -4;
	polys[8].edges[1] = -17;
	polys[8].edges[2] = -24;
	polys[8].edges[3] = 28;
	polys[8].edges[4] = -6;

	polys[9].numEdges = 5;
	polys[9].edges[0] = -23;
	polys[9].edges[1] = 29;
	polys[9].edges[2] = -10;
	polys[9].edges[3] = -7;
	polys[9].edges[4] = -28;

	polys[10].numEdges = 5;
	polys[10].edges[0] = -25;
	polys[10].edges[1] = -15;
	polys[10].edges[2] = -27;
	polys[10].edges[3] = -19;
	polys[10].edges[4] = 30;

	polys[11].numEdges = 5;
	polys[11].edges[0] = -30;
	polys[11].edges[1] = -18;
	polys[11].edges[2] = -11;
	polys[11].edges[3] = -29;
	polys[11].edges[4] = -22;

	// convex model
	mod->isConvex = qtrue;
}

/*
============
TraceModelSetupOctahedron
============
*/
void TraceModelSetupOctahedron( traceModel_t *mod, const vec3_t octBounds[2] ) {
	int i, e0, e1, v0, v1, v2;
	vec3_t v, offset, tmp1, tmp2;

	if ( mod->type != TRM_OCTAHEDRON ) {
		TraceModelInitOctahedron( mod );
	}

	VectorAdd( octBounds[0], octBounds[1], offset );
	VectorScale( offset, 0.5f, offset );
	VectorCopy( offset, mod->offset );

	v[0] = octBounds[1][0] - offset[0];
	v[1] = octBounds[1][1] - offset[1];
	v[2] = octBounds[1][2] - offset[2];

	// set vertices
	VectorSet( mod->verts[0], offset[0] + v[0], offset[1], offset[2] );
	VectorSet( mod->verts[1], offset[0] - v[0], offset[1], offset[2] );
	VectorSet( mod->verts[2], offset[0], offset[1] + v[1], offset[2] );
	VectorSet( mod->verts[3], offset[0], offset[1] - v[1], offset[2] );
	VectorSet( mod->verts[4], offset[0], offset[1], offset[2] + v[2] );
	VectorSet( mod->verts[5], offset[0], offset[1], offset[2] - v[2] );

	// set polygons
	for ( i = 0; i < mod->numPolys; i++ ) {
		e0 = mod->polys[i].edges[0];
		e1 = mod->polys[i].edges[1];
		v0 = mod->edges[abs(e0)].v[INTSIGNBITSET(e0)];
		v1 = mod->edges[abs(e0)].v[INTSIGNBITNOTSET(e0)];
		v2 = mod->edges[abs(e1)].v[INTSIGNBITNOTSET(e1)];
		// polygon plane
		VectorSubtract( mod->verts[v1], mod->verts[v0], tmp1 );
		VectorSubtract( mod->verts[v2], mod->verts[v0], tmp2 );
		CrossProduct( tmp1, tmp2, mod->polys[i].normal );
		
		VectorNormalize( mod->polys[i].normal );
		mod->polys[i].dist = DotProduct( mod->polys[i].normal, mod->verts[v0] );
		// polygon bounds
		VectorCopy( mod->verts[v0], mod->polys[i].bounds[0] );
		VectorCopy( mod->verts[v0], mod->polys[i].bounds[1] );
		AddPointToBounds( mod->verts[v1], mod->polys[i].bounds[0], mod->polys[i].bounds[1] );
		AddPointToBounds( mod->verts[v2], mod->polys[i].bounds[0], mod->polys[i].bounds[1] );
	}

	// trm bounds
	VectorCopy( octBounds[0], mod->bounds[0] );
	VectorCopy( octBounds[1], mod->bounds[1] );

	TraceModelGenerateEdgeNormals( mod );
}

/*
============
TraceModelSetupOctahedron2

  The origin is placed at the center of the octahedron.
============
*/
void TraceModelSetupOctahedron2( traceModel_t *mod, const float size ) {
	vec3_t octBounds[2];
	float halfSize;

	halfSize = size * 0.5f;
	VectorSet( octBounds[0], -halfSize, -halfSize, -halfSize );
	VectorSet( octBounds[1], halfSize, halfSize, halfSize );
	TraceModelSetupOctahedron( mod, octBounds );
}

/*
============
TraceModelSetupDodecahedron
============
*/
void TraceModelSetupDodecahedron( traceModel_t *mod, const vec3_t dodBounds[2] ) {
	int i, e0, e1, e2, e3, v0, v1, v2, v3, v4;
	float s, d;
	vec3_t a, b, c, offset, tmp1, tmp2;

	if ( mod->type != TRM_DODECAHEDRON ) {
		TraceModelInitDodecahedron( mod );
	}

	a[0] = a[1] = a[2] = 0.5773502691896257f; // 1.0f / ( 3.0f ) ^ 0.5f;
	b[0] = b[1] = b[2] = 0.3568220897730899f; // ( ( 3.0f - ( 5.0f ) ^ 0.5f ) / 6.0f ) ^ 0.5f;
	c[0] = c[1] = c[2] = 0.9341723589627156f; // ( ( 3.0f + ( 5.0f ) ^ 0.5f ) / 6.0f ) ^ 0.5f;
	d = 0.5f / c[0];
	s = ( dodBounds[1][0] - dodBounds[0][0] ) * d;
	a[0] *= s;
	b[0] *= s;
	c[0] *= s;
	s = ( dodBounds[1][1] - dodBounds[0][1] ) * d;
	a[1] *= s;
	b[1] *= s;
	c[1] *= s;
	s = ( dodBounds[1][2] - dodBounds[0][2] ) * d;
	a[2] *= s;
	b[2] *= s;
	c[2] *= s;

	VectorAdd( dodBounds[0], dodBounds[1], offset );
	VectorScale( offset, 0.5f, offset );
	VectorCopy( offset, mod->offset );

	// set vertices
	VectorSet( mod->verts[ 0], offset[0] + a[0], offset[1] + a[1], offset[2] + a[2] );
	VectorSet( mod->verts[ 1], offset[0] + a[0], offset[1] + a[1], offset[2] - a[2] );
	VectorSet( mod->verts[ 2], offset[0] + a[0], offset[1] - a[1], offset[2] + a[2] );
	VectorSet( mod->verts[ 3], offset[0] + a[0], offset[1] - a[1], offset[2] - a[2] );
	VectorSet( mod->verts[ 4], offset[0] - a[0], offset[1] + a[1], offset[2] + a[2] );
	VectorSet( mod->verts[ 5], offset[0] - a[0], offset[1] + a[1], offset[2] - a[2] );
	VectorSet( mod->verts[ 6], offset[0] - a[0], offset[1] - a[1], offset[2] + a[2] );
	VectorSet( mod->verts[ 7], offset[0] - a[0], offset[1] - a[1], offset[2] - a[2] );
	VectorSet( mod->verts[ 8], offset[0] + b[0], offset[1] + c[1], offset[2]        );
	VectorSet( mod->verts[ 9], offset[0] - b[0], offset[1] + c[1], offset[2]        );
	VectorSet( mod->verts[10], offset[0] + b[0], offset[1] - c[1], offset[2]        );
	VectorSet( mod->verts[11], offset[0] - b[0], offset[1] - c[1], offset[2]        );
	VectorSet( mod->verts[12], offset[0] + c[0], offset[1]       , offset[2] + b[2] );
	VectorSet( mod->verts[13], offset[0] + c[0], offset[1]       , offset[2] - b[2] );
	VectorSet( mod->verts[14], offset[0] - c[0], offset[1]       , offset[2] + b[2] );
	VectorSet( mod->verts[15], offset[0] - c[0], offset[1]       , offset[2] - b[2] );
	VectorSet( mod->verts[16], offset[0]       , offset[1] + b[1], offset[2] + c[2] );
	VectorSet( mod->verts[17], offset[0]       , offset[1] - b[1], offset[2] + c[2] );
	VectorSet( mod->verts[18], offset[0]       , offset[1] + b[1], offset[2] - c[2] );
	VectorSet( mod->verts[19], offset[0]       , offset[1] - b[1], offset[2] - c[2] );

	// set polygons
	for ( i = 0; i < mod->numPolys; i++ ) {
		e0 = mod->polys[i].edges[0];
		e1 = mod->polys[i].edges[1];
		e2 = mod->polys[i].edges[2];
		e3 = mod->polys[i].edges[3];
		v0 = mod->edges[abs(e0)].v[INTSIGNBITSET(e0)];
		v1 = mod->edges[abs(e0)].v[INTSIGNBITNOTSET(e0)];
		v2 = mod->edges[abs(e1)].v[INTSIGNBITNOTSET(e1)];
		v3 = mod->edges[abs(e2)].v[INTSIGNBITNOTSET(e2)];
		v4 = mod->edges[abs(e3)].v[INTSIGNBITNOTSET(e3)];
		// polygon plane
		VectorSubtract( mod->verts[v1], mod->verts[v0], tmp1 );
		VectorSubtract( mod->verts[v2], mod->verts[v0], tmp2 );
		CrossProduct( tmp1, tmp2, mod->polys[i].normal );
		VectorNormalize( mod->polys[i].normal );
		mod->polys[i].dist = DotProduct( mod->polys[i].normal, mod->verts[v0] );
		// polygon bounds
		VectorCopy( mod->verts[v0], mod->polys[i].bounds[0] );
		VectorCopy( mod->verts[v0], mod->polys[i].bounds[1] );
		AddPointToBounds( mod->verts[v1], mod->polys[i].bounds[0], mod->polys[i].bounds[1] );
		AddPointToBounds( mod->verts[v2], mod->polys[i].bounds[0], mod->polys[i].bounds[1] );
		AddPointToBounds( mod->verts[v3], mod->polys[i].bounds[0], mod->polys[i].bounds[1] );
		AddPointToBounds( mod->verts[v4], mod->polys[i].bounds[0], mod->polys[i].bounds[1] );
	}

	// trm bounds
	VectorCopy( dodBounds[0], mod->bounds[0] );
	VectorCopy( dodBounds[1], mod->bounds[1] );

	TraceModelGenerateEdgeNormals( mod );
}

/*
============
TraceModelSetupDodecahedron

  The origin is placed at the center of the octahedron.
============
*/
void TraceModelSetupDodecahedron2( traceModel_t *mod, const float size ) {
	vec3_t dodBounds[2];
	float halfSize;

	halfSize = size * 0.5f;
	VectorSet( dodBounds[0], -halfSize, -halfSize, -halfSize );
	VectorSet( dodBounds[1], halfSize, halfSize, halfSize );
	TraceModelSetupDodecahedron( mod, dodBounds );
}

/*
============
TraceModelSetupCylinder
============
*/
void TraceModelSetupCylinder( traceModel_t *mod, const vec3_t cylBounds[2], const int numSides ) {
	int i, n, i1, n2;
	float angle;
	vec3_t halfSize, offset, tmp1, tmp2;
	traceModelVert_t *verts;
	traceModelEdge_t *edges;
	traceModelPoly_t *polys;

	n = numSides;
	if ( n < 3 ) {
		n = 3;
	}
	if ( n * 2 > MAX_TRACEMODEL_VERTS ) {
		ii.Com_Printf( "WARNING: TraceModelSetupCylinder: too many vertices\n" );
		n = MAX_TRACEMODEL_VERTS / 2;
	}
	if ( n * 3 > MAX_TRACEMODEL_EDGES ) {
		ii.Com_Printf( "WARNING: TraceModelSetupCylinder: too many sides\n" );
		n = MAX_TRACEMODEL_EDGES / 3;
	}
	if ( n + 2 > MAX_TRACEMODEL_POLYS ) {
		ii.Com_Printf( "WARNING: TraceModelSetupCylinder: too many polygons\n" );
		n = MAX_TRACEMODEL_POLYS - 2;
	}

	verts = mod->verts;
	edges = mod->edges;
	polys = mod->polys;

	mod->type = TRM_CYLINDER;
	mod->numVerts = n * 2;
	mod->numEdges = n * 3;
	mod->numPolys = n + 2;
	VectorAdd( cylBounds[0], cylBounds[1], offset );
	VectorScale( offset, 0.5f, offset );
	VectorCopy( offset, mod->offset );
	VectorSubtract( cylBounds[1], offset, halfSize );
	for ( i = 0; i < n; i++ ) {
		// verts
		angle = 2.0f * M_PI * i / n;
		VectorSet( verts[i], cos( angle ) * halfSize[0] + offset[0],
							sin( angle ) * halfSize[1] + offset[1],
							-halfSize[2] + offset[2] );
		VectorSet( verts[n+i], verts[i][0], verts[i][1], halfSize[2] + offset[2] );
		// edges
		i1 = i + 1;
		n2 = n << 1;
		edges[i1].v[0] = i;
		edges[i1].v[1] = i1 % n;
		edges[n+i1].v[0] = edges[i1].v[0] + n;
		edges[n+i1].v[1] = edges[i1].v[1] + n;
		edges[n2+i1].v[0] = i;
		edges[n2+i1].v[1] = n + i;
		// vertical polygon edges
		polys[i].numEdges = 4;
		polys[i].edges[0] = i1;
		polys[i].edges[1] = n2 + (i1 % n) + 1;
		polys[i].edges[2] = -(n + i1);
		polys[i].edges[3] = -(n2 + i1);
		// bottom and top polygon edges
		polys[n].edges[i] = -(n - i);
		polys[n+1].edges[i] = n + i1;
	}
	// bottom and top polygon numEdges
	polys[n].numEdges = n;
	polys[n+1].numEdges = n;
	// polygons
	for ( i = 0; i < n; i++ ) {
		// vertical polygon plane
		VectorSubtract( verts[(i+1)%n], verts[i], tmp1 );
		VectorSubtract( verts[n+i], verts[i], tmp2 );
		CrossProduct( tmp1, tmp2, polys[i].normal );
		VectorNormalize( polys[i].normal );
		polys[i].dist = DotProduct( polys[i].normal, verts[i] );
		// vertical polygon bounds
		ClearBounds( polys[i].bounds[0], polys[i].bounds[1] );
		AddPointToBounds( verts[i], polys[i].bounds[0], polys[i].bounds[1] );
		AddPointToBounds( verts[(i+1)%n], polys[i].bounds[0], polys[i].bounds[1] );
		polys[i].bounds[0][2] = -halfSize[2] + offset[2];
		polys[i].bounds[1][2] = halfSize[2] + offset[2];
	}
	// bottom and top polygon plane
	VectorSet( polys[n].normal, 0.0f, 0.0f, -1.0f );
	polys[n].dist = -cylBounds[0][2];
	VectorSet( polys[n+1].normal, 0.0f, 0.0f, 1.0f );
	polys[n+1].dist = cylBounds[1][2];
	// trm bounds
	VectorCopy( cylBounds[0], mod->bounds[0] );
	VectorCopy( cylBounds[1], mod->bounds[1] );
	// bottom and top polygon bounds
	VectorCopy( mod->bounds[0], polys[n].bounds[0] );
	VectorCopy( mod->bounds[1], polys[n].bounds[1] );
	polys[n].bounds[1][2] = mod->bounds[0][2];
	VectorCopy( mod->bounds[0], polys[n+1].bounds[0] );
	VectorCopy( mod->bounds[1], polys[n+1].bounds[1] );
	polys[n+1].bounds[0][2] = mod->bounds[1][2];
	// convex model
	mod->isConvex = qtrue;

	TraceModelGenerateEdgeNormals( mod );
}

/*
============
TraceModelSetupCylinder2

  The origin is placed at the center of the cylinder.
============
*/
void TraceModelSetupCylinder2( traceModel_t *mod, const float height, const float width, const int numSides ) {
	vec3_t cylBounds[2];
	float halfHeight, halfWidth;

	halfHeight = height * 0.5f;
	halfWidth = width * 0.5f;
	VectorSet( cylBounds[0], -halfWidth, -halfWidth, -halfHeight );
	VectorSet( cylBounds[1], halfWidth, halfWidth, halfHeight );
	TraceModelSetupCylinder( mod, cylBounds, numSides );
}

/*
============
TraceModelSetupCone
============
*/
void TraceModelSetupCone( traceModel_t *mod, const vec3_t coneBounds[2], const int numSides ) {
	int i, n, i1;
	float angle;
	vec3_t halfSize, offset, tmp1, tmp2;
	traceModelEdge_t *edges;
	traceModelVert_t *verts;
	traceModelPoly_t *polys;

	n = numSides;
	if ( n < 2 ) {
		n = 3;
	}
	if ( n + 1 > MAX_TRACEMODEL_VERTS ) {
		ii.Com_Printf( "WARNING: TraceModelSetupCone: too many vertices\n" );
		n = MAX_TRACEMODEL_VERTS - 1;
	}
	if ( n * 2 > MAX_TRACEMODEL_EDGES ) {
		ii.Com_Printf( "WARNING: TraceModelSetupCone: too many edges\n" );
		n = MAX_TRACEMODEL_EDGES / 2;
	}
	if ( n + 1 > MAX_TRACEMODEL_POLYS ) {
		ii.Com_Printf( "WARNING: TraceModelSetupCone: too many polygons\n" );
		n = MAX_TRACEMODEL_POLYS - 1;
	}

	verts = mod->verts;
	edges = mod->edges;
	polys = mod->polys;

	mod->type = TRM_CONE;
	mod->numVerts = n + 1;
	mod->numEdges = n * 2;
	mod->numPolys = n + 1;
	VectorAdd( coneBounds[0], coneBounds[1], offset );
	VectorScale( offset, 0.5f, offset );
	VectorCopy( offset, mod->offset );
	VectorSubtract( coneBounds[1], offset, halfSize );
	VectorSet( verts[n], 0.0f, 0.0f, halfSize[2] + offset[2] );
	for ( i = 0; i < n; i++ ) {
		// verts
		angle = 2.0f * M_PI * i / n;
		VectorSet( mod->verts[i],
			cos( angle ) * halfSize[0] + offset[0],
			sin( angle ) * halfSize[1] + offset[1],
			-halfSize[2] + offset[2] );
		// edges
		i1 = i + 1;
		edges[i1].v[0] = i;
		edges[i1].v[1] = i1 % n;
		edges[n+i1].v[0] = i;
		edges[n+i1].v[1] = n;
		// vertical polygon edges
		polys[i].numEdges = 3;
		polys[i].edges[0] = i1;
		polys[i].edges[1] = n + (i1 % n) + 1;
		polys[i].edges[2] = -(n + i1);
		// bottom polygon edges
		polys[n].edges[i] = -(n - i);
	}
	// bottom polygon numEdges
	polys[n].numEdges = n;

	// polygons
	for ( i = 0; i < n; i++ ) {
		// polygon plane
		VectorSubtract( verts[(i+1)%n], verts[i], tmp1 );
		VectorSubtract( verts[n], verts[i], tmp2 );
		CrossProduct( tmp1, tmp2, polys[i].normal );
		VectorNormalize( polys[i].normal );
		polys[i].dist = DotProduct( polys[i].normal, verts[i] );
		// polygon bounds
		ClearBounds( polys[i].bounds[0], polys[i].bounds[1] );
		AddPointToBounds( verts[i], polys[i].bounds[0], polys[i].bounds[1] );
		AddPointToBounds( verts[(i+1)%n], polys[i].bounds[0], polys[i].bounds[1] );
		AddPointToBounds( verts[n], polys[i].bounds[0], polys[i].bounds[1] );
	}
	// bottom polygon plane
	VectorSet( polys[n].normal, 0.0f, 0.0f, -1.0f );
	polys[n].dist = -coneBounds[0][2];
	// trm bounds
	VectorCopy( coneBounds[0], mod->bounds[0] );
	VectorCopy( coneBounds[1], mod->bounds[1] );
	// bottom polygon bounds
	VectorCopy( mod->bounds[0], polys[n].bounds[0] );
	VectorCopy( mod->bounds[1], polys[n].bounds[1] );
	polys[n].bounds[1][2] = mod->bounds[0][2];
	// convex model
	mod->isConvex = qtrue;

	TraceModelGenerateEdgeNormals( mod );
}

/*
============
TraceModelSetupCone

  The origin is placed at the apex of the cone.
============
*/
void TraceModelSetupCone2( traceModel_t *mod, const float height, const float width, const int numSides ) {
	vec3_t coneBounds[2];
	float halfWidth;

	halfWidth = width * 0.5f;
	VectorSet( coneBounds[0], -halfWidth, -halfWidth, -height );
	VectorSet( coneBounds[1], halfWidth, halfWidth, 0.0f );
	TraceModelSetupCone( mod, coneBounds, numSides );
}

/*
============
TraceModelInitBone

  Initialize size independent bone.
============
*/
void TraceModelInitBone( traceModel_t *mod ) {
	int i;
	traceModelEdge_t *edges;
	traceModelPoly_t *polys;

	mod->type = TRM_BONE;
	mod->numVerts = 5;
	mod->numEdges = 9;
	mod->numPolys = 6;

	edges = mod->edges;
	polys = mod->polys;

	// set bone edges
	for ( i = 0; i < 3; i++ ) {
		edges[ i + 1 ].v[0] = 0;
		edges[ i + 1 ].v[1] = i + 1;
		edges[ i + 4 ].v[0] = 1 + i;
		edges[ i + 4 ].v[1] = 1 + ((i + 1) % 3);
		edges[ i + 7 ].v[0] = i + 1;
		edges[ i + 7 ].v[1] = 4;
	}

	// all edges of a polygon go counter clockwise
	polys[0].numEdges = 3;
	polys[0].edges[0] = 2;
	polys[0].edges[1] = -4;
	polys[0].edges[2] = -1;

	polys[1].numEdges = 3;
	polys[1].edges[0] = 3;
	polys[1].edges[1] = -5;
	polys[1].edges[2] = -2;

	polys[2].numEdges = 3;
	polys[2].edges[0] = 1;
	polys[2].edges[1] = -6;
	polys[2].edges[2] = -3;

	polys[3].numEdges = 3;
	polys[3].edges[0] = 4;
	polys[3].edges[1] = 8;
	polys[3].edges[2] = -7;

	polys[4].numEdges = 3;
	polys[4].edges[0] = 5;
	polys[4].edges[1] = 9;
	polys[4].edges[2] = -8;

	polys[5].numEdges = 3;
	polys[5].edges[0] = 6;
	polys[5].edges[1] = 7;
	polys[5].edges[2] = -9;

	// convex model
	mod->isConvex = qtrue;
}

/*
============
TraceModelSetupBone

  The origin is placed at the center of the bone.
============
*/
void TraceModelSetupBone( traceModel_t *mod, const float length, const float width ) {
	int i, j, edgeNum;
	float halfLength = length * 0.5f;
	traceModelVert_t *verts;
	traceModelEdge_t *edges;
	traceModelPoly_t *polys;
	vec3_t tmp1, tmp2;

	if ( mod->type != TRM_BONE ) {
		TraceModelInitBone( mod );
	}

	verts = mod->verts;
	edges = mod->edges;
	polys = mod->polys;

	// offset to center
	VectorSet( mod->offset, 0.0f, 0.0f, 0.0f );
	// set vertices
	VectorSet( verts[0], 0.0f, 0.0f, -halfLength );
	VectorSet( verts[1], 0.0f, width * -0.5f, 0.0f );
	VectorSet( verts[2], width * 0.5f, width * 0.25f, 0.0f );
	VectorSet( verts[3], width * -0.5f, width * 0.25f, 0.0f );
	VectorSet( verts[4], 0.0f, 0.0f, halfLength );
	// set bounds
	VectorSet( mod->bounds[0], width * -0.5f, width * -0.5f, -halfLength );
	VectorSet( mod->bounds[1], width * 0.5f, width * 0.25f, halfLength );
	// poly plane normals
	VectorSubtract( verts[2], verts[0], tmp1 );
	VectorSubtract( verts[1], verts[0], tmp2 );
	CrossProduct( tmp1, tmp2, polys[0].normal );
	VectorNormalize( polys[0].normal );
	VectorSet( polys[2].normal, -polys[0].normal[0], polys[0].normal[1], polys[0].normal[2] );
	VectorSet( polys[3].normal, polys[0].normal[0], polys[0].normal[1], -polys[0].normal[2] );
	VectorSet( polys[5].normal, -polys[0].normal[0], polys[0].normal[1], -polys[0].normal[2] );
	VectorSubtract( verts[3], verts[0], tmp1 );
	VectorSubtract( verts[2], verts[0], tmp2 );
	CrossProduct( tmp1, tmp2, polys[1].normal );
	VectorNormalize( polys[1].normal );
	VectorSet( polys[4].normal, polys[1].normal[0], polys[1].normal[1], -polys[1].normal[2] );
	// poly plane distances
	for ( i = 0; i < 6; i++ ) {
		polys[i].dist = DotProduct( polys[i].normal, verts[ edges[ abs(polys[i].edges[0]) ].v[0] ] );
		ClearBounds( polys[i].bounds[0], polys[i].bounds[1] );
		for ( j = 0; j < 3; j++ ) {
			edgeNum = polys[i].edges[ j ];
			AddPointToBounds( verts[ edges[abs(edgeNum)].v[edgeNum < 0] ], polys[i].bounds[0], polys[i].bounds[1] );
		}
	}

	TraceModelGenerateEdgeNormals( mod );
}

/*
============
TraceModelSetupPolygon
============
*/
void TraceModelSetupPolygon( traceModel_t *mod, const vec3_t *v, const int count ) {
	int i, j;
	vec3_t mid, tmp1, tmp2;
	traceModelVert_t *verts;
	traceModelEdge_t *edges;
	traceModelPoly_t *polys;

	mod->type = TRM_POLYGON;
	mod->numVerts = count;
	// times three because we need to be able to turn the polygon into a volume
	if ( mod->numVerts * 3 > MAX_TRACEMODEL_EDGES ) {
		ii.Com_Printf( "WARNING: idTraceModel::SetupPolygon: too many vertices\n" );
		mod->numVerts = MAX_TRACEMODEL_EDGES / 3;
	}

	verts = mod->verts;
	edges = mod->edges;
	polys = mod->polys;
	mod->numEdges = mod->numVerts;
	mod->numPolys = 2;
	// set polygon planes
	polys[0].numEdges = mod->numEdges;
	VectorSubtract( v[1], v[0], tmp1 );
	VectorSubtract( v[2], v[0], tmp2 );
	CrossProduct( tmp1, tmp2, polys[0].normal );
	VectorNormalize( polys[0].normal );
	polys[0].dist = DotProduct( polys[0].normal, v[0] );
	polys[1].numEdges = mod->numEdges;
	VectorNegate( polys[0].normal, polys[1].normal );
	polys[1].dist = -polys[0].dist;
	// setup verts, edges and polygons
	ClearBounds( polys[0].bounds[0], polys[0].bounds[1] );
	VectorClear( mid );
	for ( i = 0, j = 1; i < mod->numVerts; i++, j++ ) {
		if ( j >= mod->numVerts ) {
			j = 0;
		}
		VectorCopy( v[i], verts[i] );
		edges[i+1].v[0] = i;
		edges[i+1].v[1] = j;
		VectorSubtract( v[i], v[j], tmp1 );
		CrossProduct( polys[0].normal, tmp1, edges[i+1].normal );
		VectorNormalize( edges[i+1].normal );
		polys[0].edges[i] = i + 1;
		polys[1].edges[i] = -(mod->numVerts - i);
		AddPointToBounds( verts[i], polys[0].bounds[0], polys[0].bounds[1] );
		VectorAdd( mid, v[i], mid );
	}
	VectorCopy( polys[0].bounds[0], polys[1].bounds[0] );
	VectorCopy( polys[0].bounds[1], polys[1].bounds[1] );
	// offset to center
	VectorScale( mid, 1.0f / mod->numVerts, mod->offset );
	// total bounds
	VectorCopy( polys[0].bounds[0], mod->bounds[0] );
	VectorCopy( polys[0].bounds[1], mod->bounds[1] );
	// considered non convex because the model has no volume
	mod->isConvex = qfalse;
}

/*
============
TraceModelVolumeFromPolygon
============
*/
void TraceModelVolumeFromPolygon( const traceModel_t *mod, traceModel_t *trm, float thickness ) {
	int i;
	vec3_t	tmp1;

	memcpy( trm, mod, sizeof( *trm ) );
	trm->type = TRM_POLYGONVOLUME;
	trm->numVerts = mod->numVerts * 2;
	trm->numEdges = mod->numEdges * 3;
	trm->numPolys = mod->numEdges + 2;
	for ( i = 0; i < mod->numEdges; i++ ) {
		VectorMA( mod->verts[i], -thickness, mod->polys[0].normal, trm->verts[ mod->numVerts + i ] );
		trm->edges[ mod->numEdges + i + 1 ].v[0] = mod->numVerts + i;
		trm->edges[ mod->numEdges + i + 1 ].v[1] = mod->numVerts + (i+1) % mod->numVerts;
		trm->edges[ mod->numEdges * 2 + i + 1 ].v[0] = i;
		trm->edges[ mod->numEdges * 2 + i + 1 ].v[1] = mod->numVerts + i;
		trm->polys[1].edges[i] = -(mod->numEdges + i + 1);
		trm->polys[2+i].numEdges = 4;
		trm->polys[2+i].edges[0] = -(i + 1);
		trm->polys[2+i].edges[1] = mod->numEdges*2 + i + 1;
		trm->polys[2+i].edges[2] = mod->numEdges + i + 1;
		trm->polys[2+i].edges[3] = -(mod->numEdges*2 + (i+1) % mod->numEdges + 1);
		VectorSubtract( mod->verts[(i + 1) % mod->numVerts], mod->verts[i], tmp1 );
		CrossProduct( tmp1, mod->polys[0].normal, trm->polys[2+i].normal );
		VectorNormalize( trm->polys[2+i].normal );
		trm->polys[2+i].dist = DotProduct( trm->polys[2+i].normal, mod->verts[i] );
	}
	trm->polys[1].dist = DotProduct( trm->polys[1].normal, trm->verts[ mod->numEdges ] );

	TraceModelGenerateEdgeNormals( trm );
}

/*
============
TraceModelGenerateEdgeNormals
============
*/
#define SHARP_EDGE_DOT	-0.7f

int TraceModelGenerateEdgeNormals( traceModel_t *mod ) {
	int i, j, edgeNum, numSharpEdges;
	float dot, len;
	vec3_t dir, tmp1, tmp2;
	traceModelPoly_t *poly;
	traceModelEdge_t *edge;

	for ( i = 0; i <= mod->numEdges; i++ ) {
		VectorClear( mod->edges[i].normal );
	}

	numSharpEdges = 0;
	for ( i = 0; i < mod->numPolys; i++ ) {
		poly = mod->polys + i;
		for ( j = 0; j < poly->numEdges; j++ ) {
			edgeNum = poly->edges[j];
			edge = mod->edges + abs( edgeNum );
			if ( edge->normal[0] == 0.0f && edge->normal[1] == 0.0f && edge->normal[2] == 0.0f ) {
                VectorCopy( poly->normal, edge->normal );
			}
			else {
				dot = DotProduct( edge->normal, poly->normal );
				// if the two planes make a very sharp edge
				if ( dot < SHARP_EDGE_DOT ) {
					// max length normal pointing outside both polygons
                    VectorSubtract( mod->verts[ edge->v[edgeNum > 0]], mod->verts[ edge->v[edgeNum < 0]], dir );
                    CrossProduct( edge->normal, dir, tmp1 );
                    VectorNegate( dir, dir );
                    CrossProduct( poly->normal, dir, tmp2 );
                    VectorAdd( tmp1, tmp2, edge->normal );
                    len = VectorLength( edge->normal );
                    VectorScale( edge->normal, ( 0.5f / ( 0.5f + 0.5f * SHARP_EDGE_DOT ) ) / len, edge->normal );
					numSharpEdges++;
				}
				else {
                    VectorAdd( edge->normal, poly->normal, tmp1 );
                    VectorScale( tmp1, 0.5f / ( 0.5f + 0.5f * dot ), edge->normal );
				}
			}
		}
	}
	return numSharpEdges;
}

/*
============
TraceModelTranslate
============
*/
void TraceModelTranslate( traceModel_t *mod, const vec3_t translation ) {
	int i;

	for ( i = 0; i < mod->numVerts; i++ ) {
		VectorAdd( mod->verts[i], translation, mod->verts[i] );
	}
	for ( i = 0; i < mod->numPolys; i++ ) {
		mod->polys[i].dist += DotProduct( mod->polys[i].normal, translation );
		VectorAdd( mod->polys[i].bounds[0], translation, mod->polys[i].bounds[0] );
		VectorAdd( mod->polys[i].bounds[1], translation, mod->polys[i].bounds[1] );
	}
	VectorAdd( mod->offset, translation, mod->offset );
	VectorAdd( mod->bounds[0], translation, mod->bounds[0] );
	VectorAdd( mod->bounds[1], translation, mod->bounds[1] );
}

/*
============
TraceModelRotate
============
*/
void TraceModelRotate( traceModel_t *mod, const vec3_t rotation[3] ) {
	int i, j, edgeNum;
    vec3_t tmp;

	for ( i = 0; i < mod->numVerts; i++ ) {
        VectorRotateSelf( mod->verts[i], rotation );
	}

	ClearBounds( mod->bounds[0], mod->bounds[1] );
	for ( i = 0; i < mod->numPolys; i++ ) {
        VectorCopy( mod->polys[i].normal, tmp );
        VectorRotate( tmp, rotation, mod->polys[i].normal );

		ClearBounds( mod->polys[i].bounds[0], mod->polys[i].bounds[1] );
		edgeNum = 0;
		for ( j = 0; j < mod->polys[i].numEdges; j++ ) {
			edgeNum = mod->polys[i].edges[j];
            AddPointToBounds( mod->verts[mod->edges[abs(edgeNum)].v[INTSIGNBITSET(edgeNum)]], mod->polys[i].bounds[0], mod->polys[i].bounds[1] );
		}
		mod->polys[i].dist = DotProduct( mod->polys[i].normal, mod->verts[mod->edges[abs(edgeNum)].v[INTSIGNBITSET(edgeNum)]] );

		AddBoundsToBounds( mod->polys[i].bounds, mod->bounds );
	}

	TraceModelGenerateEdgeNormals( mod );
}

/*
============
TraceModelShrink
============
*/
void TraceModelShrink( traceModel_t *mod, const float m ) {
	int i, j, edgeNum;
	traceModelEdge_t *edge;
	vec3_t dir, tmp;

	if ( mod->type == TRM_POLYGON ) {
		for ( i = 0; i < mod->numEdges; i++ ) {
			edgeNum = mod->polys[0].edges[i];
			edge = &mod->edges[abs(edgeNum)];
			VectorSubtract( mod->verts[ edge->v[ INTSIGNBITSET(edgeNum) ] ], mod->verts[ edge->v[ INTSIGNBITNOTSET(edgeNum) ] ], dir );
			if ( VectorNormalize( dir ) < 2.0f * m ) {
				continue;
			}
			VectorScale( dir, m, dir );
			VectorSubtract( mod->verts[ edge->v[ 0 ] ], dir, mod->verts[ edge->v[ 0 ] ] );
			VectorAdd( mod->verts[ edge->v[ 1 ] ], dir, mod->verts[ edge->v[ 1 ] ] );
		}
		return;
	}

	for ( i = 0; i < mod->numPolys; i++ ) {
		mod->polys[i].dist -= m;

		for ( j = 0; j < mod->polys[i].numEdges; j++ ) {
			edgeNum = mod->polys[i].edges[j];
			edge = &mod->edges[abs(edgeNum)];
			VectorScale( mod->polys[i].normal, m, tmp );
			VectorSubtract( mod->verts[ edge->v[ INTSIGNBITSET(edgeNum) ] ], tmp, mod->verts[ edge->v[ INTSIGNBITSET(edgeNum) ] ] );
		}
	}
}

/*
============
TraceModelCompare
============
*/
qboolean TraceModelCompare( const traceModel_t *mod, const traceModel_t *trm ) {
	int i;

	if ( mod->type != trm->type || mod->numVerts != trm->numVerts || 
			mod->numEdges != trm->numEdges || mod->numPolys != trm->numPolys ) {
		return qfalse;
	}
	if ( !VectorCompare( mod->bounds[0], trm->bounds[0] ) ||
			!VectorCompare( mod->bounds[1], trm->bounds[1] ) ||
			!VectorCompare( mod->offset, trm->offset ) ) {
		return qfalse;
	}

	switch( mod->type ) {
		case TRM_INVALID:
		case TRM_BOX:
		case TRM_OCTAHEDRON:
		case TRM_DODECAHEDRON:
		case TRM_CYLINDER:
		case TRM_CONE:
			break;
		case TRM_BONE:
		case TRM_POLYGON:
		case TRM_POLYGONVOLUME:
		case TRM_CUSTOM:
			for ( i = 0; i < trm->numVerts; i++ ) {
				if ( !VectorCompare( mod->verts[i], trm->verts[i] ) ) {
					return qfalse;
				}
			}
			break;
	}
	return qtrue;
}

/*
============
TraceModelGetPolygonArea
============
*/
float TraceModelGetPolygonArea( const traceModel_t *mod, int polyNum ) {
	int i;
	vec3_t base, v1, v2, cross;
	float total;
	const traceModelPoly_t *poly;

	if ( polyNum < 0 || polyNum >= mod->numPolys ) {
		return 0.0f;
	}
	poly = &mod->polys[polyNum];
	total = 0.0f;
	VectorCopy( mod->verts[ mod->edges[ abs(poly->edges[0]) ].v[ INTSIGNBITSET( poly->edges[0] ) ] ], base );
	for ( i = 0; i < poly->numEdges; i++ ) {
		VectorSubtract( mod->verts[ mod->edges[ abs(poly->edges[i]) ].v[ INTSIGNBITSET( poly->edges[i] ) ] ], base, v1 );
		VectorSubtract( mod->verts[ mod->edges[ abs(poly->edges[i]) ].v[ INTSIGNBITNOTSET( poly->edges[i] ) ] ], base, v2 );
		CrossProduct( v1, v2, cross );
		total += VectorLength( cross );
	}
	return total * 0.5f;
}

/*
============
TraceModelGetOrderedSilhouetteEdges
============
*/
int TraceModelGetOrderedSilhouetteEdges( const traceModel_t *mod, const int edgeIsSilEdge[MAX_TRACEMODEL_EDGES+1], int silEdges[MAX_TRACEMODEL_EDGES] ) {
	int i, j, edgeNum, numSilEdges, nextSilVert;
	int unsortedSilEdges[MAX_TRACEMODEL_EDGES];

	numSilEdges = 0;
	for ( i = 1; i <= mod->numEdges; i++ ) {
		if ( edgeIsSilEdge[i] ) {
			unsortedSilEdges[numSilEdges++] = i;
		}
	}

	silEdges[0] = unsortedSilEdges[0];
	unsortedSilEdges[0] = -1;
	nextSilVert = mod->edges[silEdges[0]].v[0];
	for ( i = 1; i < numSilEdges; i++ ) {
		for ( j = 1; j < numSilEdges; j++ ) {
			edgeNum = unsortedSilEdges[j];
			if ( edgeNum >= 0 ) {
				if ( mod->edges[edgeNum].v[0] == nextSilVert ) {
					nextSilVert = mod->edges[edgeNum].v[1];
					silEdges[i] = edgeNum;
					break;
				}
				if ( mod->edges[edgeNum].v[1] == nextSilVert ) {
					nextSilVert = mod->edges[edgeNum].v[0];
					silEdges[i] = -edgeNum;
					break;
				}
			}
		}
		if ( j >= numSilEdges ) {
			silEdges[i] = 1;	// shouldn't happen
		}
		unsortedSilEdges[j] = -1;
	}
	return numSilEdges;
}

/*
============
TraceModelGetProjectionSilhouetteEdges
============
*/
int TraceModelGetProjectionSilhouetteEdges( const traceModel_t *mod, const vec3_t projectionOrigin, int silEdges[MAX_TRACEMODEL_EDGES] ) {
	int i, j, edgeNum;
	int edgeIsSilEdge[MAX_TRACEMODEL_EDGES+1];
	const traceModelPoly_t *poly;
	vec3_t dir;

	memset( edgeIsSilEdge, 0, sizeof( edgeIsSilEdge ) );

	for ( i = 0; i < mod->numPolys; i++ ) {
		poly = &mod->polys[i];
		edgeNum = poly->edges[0];
		VectorSubtract( mod->verts[ mod->edges[abs(edgeNum)].v[ INTSIGNBITSET(edgeNum) ] ], projectionOrigin, dir );
		if ( DotProduct( dir, poly->normal ) < 0.0f ) {
			for ( j = 0; j < poly->numEdges; j++ ) {
				edgeNum = poly->edges[j];
				edgeIsSilEdge[abs(edgeNum)] ^= 1;
			}
		}
	}

	return TraceModelGetOrderedSilhouetteEdges( mod, edgeIsSilEdge, silEdges );
}

/*
============
TraceModelGetParallelProjectionSilhouetteEdges
============
*/
int TraceModelGetParallelProjectionSilhouetteEdges( const traceModel_t *mod, const vec3_t projectionDir, int silEdges[MAX_TRACEMODEL_EDGES] ) {
	int i, j, edgeNum;
	int edgeIsSilEdge[MAX_TRACEMODEL_EDGES+1];
	const traceModelPoly_t *poly;

	memset( edgeIsSilEdge, 0, sizeof( edgeIsSilEdge ) );

	for ( i = 0; i < mod->numPolys; i++ ) {
		poly = &mod->polys[i];
		if ( DotProduct( projectionDir, poly->normal ) < 0.0f ) {
			for ( j = 0; j < poly->numEdges; j++ ) {
				edgeNum = poly->edges[j];
				edgeIsSilEdge[abs(edgeNum)] ^= 1;
			}
		}
	}

	return TraceModelGetOrderedSilhouetteEdges( mod, edgeIsSilEdge, silEdges );
}


/*

  credits to Brian Mirtich for his paper "Fast and Accurate Computation of Polyhedral Mass Properties"

*/

typedef struct projectionIntegrals_s {
	float P1;
	float Pa, Pb;
	float Paa, Pab, Pbb;
	float Paaa, Paab, Pabb, Pbbb;
} projectionIntegrals_t;

/*
============
TraceModelProjectionIntegrals
============
*/
void TraceModelProjectionIntegrals( const traceModel_t *mod, int polyNum, int a, int b, struct projectionIntegrals_s *integrals ) {
	const traceModelPoly_t *poly;
	int i, edgeNum;
	vec3_t v1, v2;
	float a0, a1, da;
	float b0, b1, db;
	float a0_2, a0_3, a0_4, b0_2, b0_3, b0_4;
	float a1_2, a1_3, b1_2, b1_3;
	float C1, Ca, Caa, Caaa, Cb, Cbb, Cbbb;
	float Cab, Kab, Caab, Kaab, Cabb, Kabb;

	memset( integrals, 0, sizeof(projectionIntegrals_t) );
	poly = &mod->polys[polyNum];
	for ( i = 0; i < poly->numEdges; i++ ) {
		edgeNum = poly->edges[i];
		VectorCopy( mod->verts[ mod->edges[ abs(edgeNum) ].v[ edgeNum < 0 ] ], v1 );
		VectorCopy( mod->verts[ mod->edges[ abs(edgeNum) ].v[ edgeNum > 0 ] ], v2 );
		a0 = v1[a];
		b0 = v1[b];
		a1 = v2[a];
		b1 = v2[b];
		da = a1 - a0;
		db = b1 - b0;
		a0_2 = a0 * a0;
		a0_3 = a0_2 * a0;
		a0_4 = a0_3 * a0;
		b0_2 = b0 * b0;
		b0_3 = b0_2 * b0;
		b0_4 = b0_3 * b0;
		a1_2 = a1 * a1;
		a1_3 = a1_2 * a1; 
		b1_2 = b1 * b1;
		b1_3 = b1_2 * b1;

		C1 = a1 + a0;
		Ca = a1 * C1 + a0_2;
		Caa = a1 * Ca + a0_3;
		Caaa = a1 * Caa + a0_4;
		Cb = b1 * (b1 + b0) + b0_2;
		Cbb = b1 * Cb + b0_3;
		Cbbb = b1 * Cbb + b0_4;
		Cab = 3 * a1_2 + 2 * a1 * a0 + a0_2;
		Kab = a1_2 + 2 * a1 * a0 + 3 * a0_2;
		Caab = a0 * Cab + 4 * a1_3;
		Kaab = a1 * Kab + 4 * a0_3;
		Cabb = 4 * b1_3 + 3 * b1_2 * b0 + 2 * b1 * b0_2 + b0_3;
		Kabb = b1_3 + 2 * b1_2 * b0 + 3 * b1 * b0_2 + 4 * b0_3;

		integrals->P1 += db * C1;
		integrals->Pa += db * Ca;
		integrals->Paa += db * Caa;
		integrals->Paaa += db * Caaa;
		integrals->Pb += da * Cb;
		integrals->Pbb += da * Cbb;
		integrals->Pbbb += da * Cbbb;
		integrals->Pab += db * (b1 * Cab + b0 * Kab);
		integrals->Paab += db * (b1 * Caab + b0 * Kaab);
		integrals->Pabb += da * (a1 * Cabb + a0 * Kabb);
	}

	integrals->P1 *= (1.0f / 2.0f);
	integrals->Pa *= (1.0f / 6.0f);
	integrals->Paa *= (1.0f / 12.0f);
	integrals->Paaa *= (1.0f / 20.0f);
	integrals->Pb *= (1.0f / -6.0f);
	integrals->Pbb *= (1.0f / -12.0f);
	integrals->Pbbb *= (1.0f / -20.0f);
	integrals->Pab *= (1.0f / 24.0f);
	integrals->Paab *= (1.0f / 60.0f);
	integrals->Pabb *= (1.0f / -60.0f);
}

typedef struct polygonIntegrals_s {
	float Fa, Fb, Fc;
	float Faa, Fbb, Fcc;
	float Faaa, Fbbb, Fccc;
	float Faab, Fbbc, Fcca;
} polygonIntegrals_t;

/*
============
TraceModelPolygonIntegrals
============
*/
#define Cube(x) ((x) * (x) * (x))
void TraceModelPolygonIntegrals( const traceModel_t *mod, int polyNum, int a, int b, int c, struct polygonIntegrals_s *integrals ) {
	projectionIntegrals_t pi;
	vec3_t n;
	float w;
	float k1, k2, k3, k4;

	TraceModelProjectionIntegrals( mod, polyNum, a, b, &pi );

	VectorCopy( mod->polys[polyNum].normal, n );
	w = -mod->polys[polyNum].dist;
	k1 = 1 / n[c];
	k2 = k1 * k1;
	k3 = k2 * k1;
	k4 = k3 * k1;

	integrals->Fa = k1 * pi.Pa;
	integrals->Fb = k1 * pi.Pb;
	integrals->Fc = -k2 * (n[a] * pi.Pa + n[b] * pi.Pb + w * pi.P1);

	integrals->Faa = k1 * pi.Paa;
	integrals->Fbb = k1 * pi.Pbb;
	integrals->Fcc = k3 * (Square(n[a]) * pi.Paa + 2 * n[a] * n[b] * pi.Pab + Square(n[b]) * pi.Pbb
			+ w * (2 * (n[a] * pi.Pa + n[b] * pi.Pb) + w * pi.P1));

	integrals->Faaa = k1 * pi.Paaa;
	integrals->Fbbb = k1 * pi.Pbbb;
	integrals->Fccc = -k4 * (Cube(n[a]) * pi.Paaa + 3 * Square(n[a]) * n[b] * pi.Paab 
			+ 3 * n[a] * Square(n[b]) * pi.Pabb + Cube(n[b]) * pi.Pbbb
			+ 3 * w * (Square(n[a]) * pi.Paa + 2 * n[a] * n[b] * pi.Pab + Square(n[b]) * pi.Pbb)
			+ w * w * (3 * (n[a] * pi.Pa + n[b] * pi.Pb) + w * pi.P1));

	integrals->Faab = k1 * pi.Paab;
	integrals->Fbbc = -k2 * (n[a] * pi.Pabb + n[b] * pi.Pbbb + w * pi.Pbb);
	integrals->Fcca = k3 * (Square(n[a]) * pi.Paaa + 2 * n[a] * n[b] * pi.Paab + Square(n[b]) * pi.Pabb
			+ w * (2 * (n[a] * pi.Paa + n[b] * pi.Pab) + w * pi.Pa));
}
#undef Cube
typedef struct volumeIntegrals_s {
	float T0;
	vec3_t T1;
	vec3_t T2;
	vec3_t TP;
} volumeIntegrals_t;

/*
============
TraceModelVolumeIntegrals
============
*/
void TraceModelVolumeIntegrals( const traceModel_t *mod, volumeIntegrals_t *integrals ) {
	const traceModelPoly_t *poly;
	polygonIntegrals_t pi;
	int i, a, b, c;
	float nx, ny, nz;

	memset( integrals, 0, sizeof(volumeIntegrals_t) );
	for ( i = 0; i < mod->numPolys; i++ ) {
		poly = &mod->polys[i];

		nx = Q_fabs( poly->normal[0] );
		ny = Q_fabs( poly->normal[1] );
		nz = Q_fabs( poly->normal[2] );
		if ( nx > ny && nx > nz ) {
			c = 0;
		}
		else {
			c = (ny > nz) ? 1 : 2;
		}
		a = (c + 1) % 3;
		b = (a + 1) % 3;

		TraceModelPolygonIntegrals( mod, i, a, b, c, &pi );

		integrals->T0 += poly->normal[0] * ((a == 0) ? pi.Fa : ((b == 0) ? pi.Fb : pi.Fc));

		integrals->T1[a] += poly->normal[a] * pi.Faa;
		integrals->T1[b] += poly->normal[b] * pi.Fbb;
		integrals->T1[c] += poly->normal[c] * pi.Fcc;
		integrals->T2[a] += poly->normal[a] * pi.Faaa;
		integrals->T2[b] += poly->normal[b] * pi.Fbbb;
		integrals->T2[c] += poly->normal[c] * pi.Fccc;
		integrals->TP[a] += poly->normal[a] * pi.Faab;
		integrals->TP[b] += poly->normal[b] * pi.Fbbc;
		integrals->TP[c] += poly->normal[c] * pi.Fcca;
	}

	VectorScale( integrals->T1, 0.5f, integrals->T1 );
	VectorScale( integrals->T2, (1.0f / 3.0f), integrals->T2 );
	VectorScale( integrals->TP, 0.5f, integrals->TP );
}

/*
============
TraceModelGetMassProperties
============
*/
void TraceModelGetMassProperties( const traceModel_t *mod, const float density, float *mass, vec3_t centerOfMass, vec3_t inertiaTensor[3] ) {
	volumeIntegrals_t integrals;

	// if polygon trace model
	if ( mod->type == TRM_POLYGON ) {
		traceModel_t trm;

		TraceModelVolumeFromPolygon( mod, &trm, 1.0f );
		TraceModelGetMassProperties( &trm, density, mass, centerOfMass, inertiaTensor );
		return;
	}

	TraceModelVolumeIntegrals( mod, &integrals );

	// if no volume
	if ( integrals.T0 == 0.0f ) {
		*mass = 1.0f;
		VectorClear( centerOfMass );
		AxisClear( inertiaTensor );
		return;
	}

	// mass of model
	*mass = density * integrals.T0;
	// center of mass
	centerOfMass[0] = integrals.T1[0] / integrals.T0;
	centerOfMass[1] = integrals.T1[1] / integrals.T0;
	centerOfMass[2] = integrals.T1[2] / integrals.T0;
	// compute inertia tensor
	inertiaTensor[0][0] = density * (integrals.T2[1] + integrals.T2[2]);
	inertiaTensor[1][1] = density * (integrals.T2[2] + integrals.T2[0]);
	inertiaTensor[2][2] = density * (integrals.T2[0] + integrals.T2[1]);
	inertiaTensor[0][1] = inertiaTensor[1][0] = - density * integrals.TP[0];
	inertiaTensor[1][2] = inertiaTensor[2][1] = - density * integrals.TP[1];
	inertiaTensor[2][0] = inertiaTensor[0][2] = - density * integrals.TP[2];
	// translate inertia tensor to center of mass
	inertiaTensor[0][0] -= *mass * (centerOfMass[1]*centerOfMass[1] + centerOfMass[2]*centerOfMass[2]);
	inertiaTensor[1][1] -= *mass * (centerOfMass[2]*centerOfMass[2] + centerOfMass[0]*centerOfMass[0]);
	inertiaTensor[2][2] -= *mass * (centerOfMass[0]*centerOfMass[0] + centerOfMass[1]*centerOfMass[1]);
	inertiaTensor[0][1] = inertiaTensor[1][0] += *mass * centerOfMass[0] * centerOfMass[1];
	inertiaTensor[1][2] = inertiaTensor[2][1] += *mass * centerOfMass[1] * centerOfMass[2];
	inertiaTensor[2][0] = inertiaTensor[0][2] += *mass * centerOfMass[2] * centerOfMass[0];
}

/*
=================================================================

	fixedWinding_t

=================================================================
*/

void CopyFixedWinding( const fixedWinding_t *src, fixedWinding_t *dst ) {
	Com_Memcpy( dst, src, sizeof( *dst ) );
}

void ClearFixedWinding( fixedWinding_t *w ) {
	w->numPoints = 0;
}

qboolean WindingEnsureAlloced( fixedWinding_t *w, int n ) {
	if ( n > MAX_POINTS_ON_WINDING ) {
		Com_DPrintf("WARNING: fixedWinding_t -> MAX_POINTS_ON_WINDING overflowed\n");
		return qfalse;
	}
	assert( n <= MAX_POINTS_ON_WINDING );

	return qtrue;
}

/*
=============
FixedWindingBaseForPlane
=============
*/
// TODO: reuse qfiles.h definition
#define MAX_WORLD_COORD		( 128*1024 )
#define MIN_WORLD_COORD		( -128*1024 )

void FixedWindingBaseForPlane( fixedWinding_t *w, const plane_t plane ) {
	vec3_t org, vright, vup;
	int i;

	VectorScale( plane, PlaneGetDist( plane ), org );

	NormalVectors( plane, vup, vright );
	VectorScale( vup, MAX_WORLD_COORD - MIN_WORLD_COORD, vup );
	VectorScale( vright, MAX_WORLD_COORD - MIN_WORLD_COORD, vright );

	assert( 4 < MAX_POINTS_ON_WINDING );
	w->numPoints = 4;
	for ( i = 0; i < 3; i++ ) {
		w->points[0][i] = org[i] - vright[i] + vup[i];
	}
	w->points[0][3] = w->points[0][4] = 0.0f;
	for ( i = 0; i < 3; i++ ) {
		w->points[1][i] = org[i] + vright[i] + vup[i];
	}
	w->points[1][3] = w->points[1][4] = 0.0f;
	for ( i = 0; i < 3; i++ ) {
		w->points[2][i] = org[i] + vright[i] - vup[i];
	}
	w->points[2][3] = w->points[2][4] = 0.0f;
	for ( i = 0; i < 3; i++ ) {
		w->points[3][i] = org[i] - vright[i] - vup[i];
	}
	w->points[3][3] = w->points[3][4] = 0.0f;
}

int SplitFixedWinding( fixedWinding_t *w, fixedWinding_t *back, const plane_t plane, const float epsilon ) {
	int		counts[3];
	float	dists[MAX_POINTS_ON_WINDING+4];
	byte	sides[MAX_POINTS_ON_WINDING+4];
	float	dot;
	int		i, j;
	vec5_t *p1, *p2;
	vec5_t	mid;
	fixedWinding_t out;

	counts[SIDE_FRONT] = counts[SIDE_BACK] = counts[SIDE_ON] = 0;

	// determine sides for each point
	for ( i = 0; i < w->numPoints; i++ ) {
		dists[i] = dot = PlaneDistance( plane, w->points[i] );
		if ( dot > epsilon ) {
			sides[i] = SIDE_FRONT;
		} else if ( dot < -epsilon ) {
			sides[i] = SIDE_BACK;
		} else {
			sides[i] = SIDE_ON;
		}
		counts[sides[i]]++;
	}

	if ( !counts[SIDE_BACK] ) {
		if ( !counts[SIDE_FRONT] ) {
			return SIDE_ON;
		}
		else {
			return SIDE_FRONT;
		}
	}
	
	if ( !counts[SIDE_FRONT] ) {
		return SIDE_BACK;
	}

	sides[i] = sides[0];
	dists[i] = dists[0];
	
	out.numPoints = 0;
	back->numPoints = 0;

	for ( i = 0; i < w->numPoints; i++ ) {
		p1 = &w->points[i];

		if ( !WindingEnsureAlloced( &out, out.numPoints+1 ) ) {
			return SIDE_FRONT;		// can't split -- fall back to original
		}
		if ( !WindingEnsureAlloced( back, back->numPoints+1 ) ) {
			return SIDE_FRONT;		// can't split -- fall back to original
		}

		if ( sides[i] == SIDE_ON ) {
			VectorCopy( *p1, out.points[out.numPoints] );
			out.points[out.numPoints][3] = *p1[3];
			out.points[out.numPoints][4] = *p1[4];
			out.numPoints++;
			
			VectorCopy( *p1, back->points[back->numPoints] );
			back->points[back->numPoints][3] = *p1[3];
			back->points[back->numPoints][4] = *p1[4];
			back->numPoints++;
			continue;
		}
	
		if ( sides[i] == SIDE_FRONT ) {
			VectorCopy( *p1, out.points[out.numPoints] );
			out.points[out.numPoints][3] = *p1[3];
			out.points[out.numPoints][4] = *p1[4];
			out.numPoints++;
		}
		if ( sides[i] == SIDE_BACK ) {
			VectorCopy( *p1, back->points[back->numPoints] );
			back->points[back->numPoints][3] = *p1[3];
			back->points[back->numPoints][4] = *p1[4];
			back->numPoints++;
		}
		
		if ( sides[i+1] == SIDE_ON || sides[i+1] == sides[i] ) {
			continue;
		}
			
		if ( !WindingEnsureAlloced( &out, out.numPoints+1 ) ) {
			return SIDE_FRONT;		// can't split -- fall back to original
		}

		if ( !WindingEnsureAlloced( back, back->numPoints+1 ) ) {
			return SIDE_FRONT;		// can't split -- fall back to original
		}

		// generate a split point
		j = i + 1;
		if ( j >= w->numPoints ) {
			p2 = &w->points[0];
		}
		else {
			p2 = &w->points[j];
		}

		dot = dists[i] / (dists[i] - dists[i+1]);
		for ( j = 0; j < 3; j++ ) {
			// avoid round off error when possible
			if ( plane[j] == 1.0f ) {
				mid[j] = PlaneGetDist( plane );
			} else if ( plane[j] == -1.0f ) {
				mid[j] = -PlaneGetDist( plane );
			} else {
				mid[j] = (*p1)[j] + dot * ( (*p2)[j] - (*p1)[j] );
			}
		}
		mid[3] = *p1[3] + dot * ( *p2[3] - *p1[3] );
		mid[4] = *p1[4] + dot * ( *p2[4] - *p1[4] );
			
		VectorCopy( mid, out.points[out.numPoints] );
		out.points[out.numPoints][3] = mid[3];
		out.points[out.numPoints][4] = mid[4];
		out.numPoints++;
		VectorCopy( mid, back->points[back->numPoints] );
		back->points[back->numPoints][3] = mid[3];
		back->points[back->numPoints][4] = mid[4];
		back->numPoints++;
	}
	for ( i = 0; i < out.numPoints; i++ ) {
		VectorCopy( out.points[i], w->points[i] );
		w->points[i][3] = out.points[i][3];
		w->points[i][4] = out.points[i][4];
	}
	w->numPoints = out.numPoints;

	return SIDE_CROSS;
}

void FixedWindingBounds( const fixedWinding_t *w, vec3_t bounds[2] ) {
	int i;

	if ( !w->numPoints ) {
		ClearBounds( bounds[0], bounds[1] );
		return;
	}

    VectorCopy( w->points[0], bounds[0] );
    VectorCopy( w->points[0], bounds[1] );
	for ( i = 1; i < w->numPoints; i++ ) {
		if ( w->points[i][0] < bounds[0][0] ) {
			bounds[0][0] = w->points[i][0];
		} else if ( w->points[i][0] > bounds[1][0] ) {
			bounds[1][0] = w->points[i][0];
		}
		if ( w->points[i][1] < bounds[0][1] ) {
			bounds[0][1] = w->points[i][1];
		} else if ( w->points[i][1] > bounds[1][1] ) {
			bounds[1][1] = w->points[i][1];
		}
		if ( w->points[i][2] < bounds[0][2] ) {
			bounds[0][2] = w->points[i][2];
		} else if ( w->points[i][2] > bounds[1][2] ) {
			bounds[1][2] = w->points[i][2];
		}
	}
}

/*
=============
FixedWindingIsHuge
=============
*/
qboolean FixedWindingIsHuge( const fixedWinding_t *w ) {
	int i, j;

	for ( i = 0; i < w->numPoints; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			if ( w->points[i][j] <= MIN_WORLD_COORD || w->points[i][j] >= MAX_WORLD_COORD ) {
				return qtrue;
			}
		}
	}
	return qfalse;
}

/*
=============
ClipFixedWindingInPlace
=============
*/
qboolean ClipFixedWindingInPlace( fixedWinding_t *w, const plane_t plane, const float epsilon, const qboolean keepOn ) {
	float		dists[MAX_POINTS_ON_WINDING];
	byte 		sides[MAX_POINTS_ON_WINDING];
	vec5_t 		newPoints[MAX_POINTS_ON_WINDING];
	int			newNumPoints;
	int			counts[3];
	float		dot;
	int			i, j;
	vec5_t *	p1, *p2;
	vec5_t		mid;
	int			maxpts;

	assert( w != NULL );

	counts[SIDE_FRONT] = counts[SIDE_BACK] = counts[SIDE_ON] = 0;

	// determine sides for each point
	for ( i = 0; i < w->numPoints; i++ ) {
		dists[i] = dot = PlaneDistance( plane, w->points[i] );
		if ( dot > epsilon ) {
			sides[i] = SIDE_FRONT;
		} else if ( dot < -epsilon ) {
			sides[i] = SIDE_BACK;
		} else {
			sides[i] = SIDE_ON;
		}
		counts[sides[i]]++;
	}
	sides[i] = sides[0];
	dists[i] = dists[0];
	
	// if the winding is on the plane and we should keep it
	if ( keepOn && !counts[SIDE_FRONT] && !counts[SIDE_BACK] ) {
		return qtrue;
	}
	// if nothing at the front of the clipping plane
	if ( !counts[SIDE_FRONT] ) {
		w->numPoints = 0;
		return qfalse;
	}
	// if nothing at the back of the clipping plane
	if ( !counts[SIDE_BACK] ) {
		return qtrue;
	}

	maxpts = w->numPoints + 4;		// cant use counts[0]+2 because of fp grouping errors
	if ( maxpts > MAX_POINTS_ON_WINDING ) {
		return qtrue;	// can't split -- fall back to original
	}

	newNumPoints = 0;

	for ( i = 0; i < w->numPoints; i++ ) {
		p1 = &w->points[i];

		if ( newNumPoints+1 > maxpts ) {
			return qtrue;		// can't split -- fall back to original
		}
		
		if ( sides[i] == SIDE_ON ) {
			Vector5Copy( *p1, newPoints[newNumPoints] );
			newNumPoints++;
			continue;
		}
	
		if ( sides[i] == SIDE_FRONT ) {
			Vector5Copy( *p1, newPoints[newNumPoints] );
			newNumPoints++;
		}

		if ( sides[i+1] == SIDE_ON || sides[i+1] == sides[i] ) {
			continue;
		}
			
		if ( newNumPoints+1 > maxpts ) {
			return qtrue;		// can't split -- fall back to original
		}

		// generate a split point
		p2 = &w->points[(i+1)%w->numPoints];
		
		dot = dists[i] / (dists[i] - dists[i+1]);
		for ( j = 0; j < 3; j++ ) {
			// avoid round off error when possible
			if ( plane[j] == 1.0f ) {
				mid[j] = PlaneGetDist( plane );
			} else if ( plane[j] == -1.0f ) {
				mid[j] = -PlaneGetDist( plane );
			} else {
				mid[j] = (*p1)[j] + dot * ( (*p2)[j] - (*p1)[j] );
			}
		}
		mid[3] = (*p1)[3] + dot * ( (*p2)[3] - (*p1)[3] );
		mid[4] = (*p1)[4] + dot * ( (*p2)[4] - (*p1)[4] );
			
		Vector5Copy( mid, newPoints[newNumPoints] );
		newNumPoints++;
	}

	w->numPoints = newNumPoints;
	memcpy( w->points, newPoints, newNumPoints * sizeof(vec5_t) );

	return qtrue;
}


/*
=============
ReverseFixedWindingSelf
=============
*/
void ReverseFixedWindingSelf( fixedWinding_t *w ) {
	vec5_t v;
	int i;

	for ( i = 0; i < (w->numPoints>>1); i++ ) {
		Vector5Copy( w->points[i], v );

		Vector5Copy( w->points[w->numPoints - i - 1], w->points[i] );
		Vector5Copy( v, w->points[w->numPoints - i - 1] );
	}
}
