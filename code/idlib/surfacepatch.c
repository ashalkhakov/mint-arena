/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

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

#include "surfacepatch.h"

#define Sqr(x) ((x) * (x))

/*
=================
SurfacePatchSetSize
=================
*/
void SurfacePatchSetSize( surfacePatch_t *self, int patchWidth, int patchHeight ) {
	if (patchWidth < 1 || patchWidth > self->maxWidth) {
		ii.Com_Error( ERR_FATAL, "SurfacePatchSetSize: invalid patchWidth" );
	}
	if (patchHeight < 1 || patchHeight > self->maxHeight) {
		ii.Com_Error( ERR_FATAL, "SurfacePatchSetSize: invalid patchHeight");
	}
	self->width = patchWidth;
	self->height = patchHeight;
	self->surf.numVerts = self->width * self->height;
}

/*
============
LerpVert
============
*/
static void LerpVert( const surfVert_t *a, const surfVert_t *b, surfVert_t *out) {
	// DG: TODO: what about out.tangent and out.color ?
	out->xyz[0] = 0.5f * (a->xyz[0] + b->xyz[0]);
	out->xyz[1] = 0.5f * (a->xyz[1] + b->xyz[1]);
	out->xyz[2] = 0.5f * (a->xyz[2] + b->xyz[2]);
	out->normal[0] = 0.5f * (a->normal[0] + b->normal[0]);
	out->normal[1] = 0.5f * (a->normal[1] + b->normal[1]);
	out->normal[2] = 0.5f * (a->normal[2] + b->normal[2]);
	out->st[0] = 0.5f * (a->st[0] + b->st[0]);
	out->st[1] = 0.5f * (a->st[1] + b->st[1]);
}

/*
=================
PutOnCurve

Expects an expanded patch.
=================
*/
void PutOnCurve( surfacePatch_t *self ) {
	int i, j;
	surfVert_t prev, next;

	assert( self->expanded == qtrue );
	// put all the approximating points on the curve
	for (i = 0; i < self->width; i++) {
		for (j = 1; j < self->height; j += 2) {
			LerpVert( &self->surf.verts[j * self->maxWidth + i], &self->surf.verts[(j + 1) * self->maxWidth + i], &prev );
			LerpVert( &self->surf.verts[j * self->maxWidth + i], &self->surf.verts[(j - 1) * self->maxWidth + i], &next );
			LerpVert( &prev, &next, &self->surf.verts[j * self->maxWidth + i] );
		}
	}

	for ( j = 0; j < self->height; j++ ) {
		for ( i = 1; i < self->width; i += 2 ) {
			LerpVert( &self->surf.verts[j * self->maxWidth + i], &self->surf.verts[j * self->maxWidth + i + 1], &prev );
			LerpVert( &self->surf.verts[j * self->maxWidth + i], &self->surf.verts[j * self->maxWidth + i - 1], &next );
			LerpVert( &prev, &next, &self->surf.verts[j * self->maxWidth + i] );
		}
	}
}

/*
================
ProjectPointOntoVector
================
*/
static void ProjectPointOntoVector(const vec3_t point, const vec3_t vStart, const vec3_t vEnd, vec3_t vProj) {
	vec3_t pVec, vec;

	VectorSubtract( point, vStart, pVec );
	VectorSubtract( vEnd, vStart, vec );
	VectorNormalize( vec );
	// project onto the directional vector for this segment
	VectorMA( vStart, DotProduct( pVec, vec ), vec, vProj );
}

/*
================
RemoveLinearColumnsRows

Expects an expanded patch.
================
*/
void RemoveLinearColumnsRows( surfacePatch_t *self ) {
	int i, j, k;
	float len, maxLength;
	vec3_t proj, dir;

	assert( self->expanded == qtrue );
	for ( j = 1; j < self->width - 1; j++ ) {
		maxLength = 0;
		for ( i = 0; i < self->height; i++ ) {
			ProjectPointOntoVector(
				self->surf.verts[i * self->maxWidth + j].xyz,
				self->surf.verts[i * self->maxWidth + j - 1].xyz,
				self->surf.verts[i * self->maxWidth + j + 1].xyz,
				proj
			);
			VectorSubtract( self->surf.verts[i * self->maxWidth + j].xyz, proj, dir );
			len = VectorLengthSquared( dir );
			if ( len > maxLength ) {
				maxLength = len;
			}
		}
		if ( maxLength < Sqr(0.2f) ) {
			self->width--;
			for ( i = 0; i < self->height; i++ ) {
				for (k = j; k < self->width; k++) {
					self->surf.verts[i * self->maxWidth + k] = self->surf.verts[i * self->maxWidth + k + 1];
				}
			}
			j--;
		}
	}
	for ( j = 1; j < self->height - 1; j++ ) {
		maxLength = 0;
		for ( i = 0; i < self->width; i++ ) {
			ProjectPointOntoVector(
				&self->surf.verts[j * self->maxWidth + i].xyz,
				&self->surf.verts[(j - 1) * self->maxWidth + i].xyz,
				&self->surf.verts[(j + 1) * self->maxWidth + i].xyz,
				proj
			);
			VectorSubtract( self->surf.verts[j * self->maxWidth + i].xyz, proj, dir );
			len = VectorLengthSquared( dir );
			if ( len > maxLength ) {
				maxLength = len;
			}
		}
		if ( maxLength < Sqr( 0.2f ) ) {
			self->height--;
			for ( i = 0; i < self->width; i++ ) {
				for ( k = j; k < self->height; k++ ) {
					self->surf.verts[k * self->maxWidth + i] = self->surf.verts[(k + 1) * self->maxWidth + i];
				}
			}
			j--;
		}
	}
}

/*
================
ResizeExpanded
================
*/
void ResizeExpanded( surfacePatch_t *self, int newHeight, int newWidth) {
	int i, j;
	int newNumVerts;
	surfVert_t *newVerts;

	assert( self->expanded == qtrue );
	if (newHeight <= self->maxHeight && newWidth <= self->maxWidth ) {
		return;
	}
	if ( newHeight * newWidth > self->maxHeight * self->maxWidth ) {
		newNumVerts = newHeight * newWidth;
		newVerts = ( surfVert_t * )ii.GetMemory( sizeof( *newVerts ) * newNumVerts );
		memcpy( newVerts, self->surf.verts, sizeof( *newVerts ) * self->surf.numVerts );
		ii.FreeMemory( self->surf.verts );
		self->surf.verts = newVerts;
		self->surf.sizeVerts = newNumVerts;
		self->surf.numVerts = newNumVerts;
	}
	// space out verts for new height and width
	for ( j = self->maxHeight - 1; j >= 0; j-- ) {
		for (i = self->maxWidth - 1; i >= 0; i-- ) {
			self->surf.verts[j * newWidth + i] = self->surf.verts[j * self->maxWidth + i];
		}
	}
	self->maxHeight = newHeight;
	self->maxWidth = newWidth;
}

/*
================
Collapse
================
*/
void Collapse( surfacePatch_t *self ) {
	int i, j;

	if ( !self->expanded ) {
		//idLib::common->FatalError("idSurface_Patch::Collapse: patch not expanded");
		// FIXME: !!
	}
	self->expanded = qfalse;
	if ( self->width != self->maxWidth ) {
		for ( j = 0; j < self->height; j++ ) {
			for ( i = 0; i < self->width; i++ ) {
				self->surf.verts[j * self->width + i] = self->surf.verts[j * self->maxWidth + i];
			}
		}
	}
	self->surf.numVerts = self->width * self->height;
}

/*
================
Expand
================
*/
void Expand( surfacePatch_t *self ) {
	int i, j;

	if ( self->expanded ) {
		// FIXME!
		//idLib::common->FatalError("idSurface_Patch::Expand: patch alread expanded");
	}
	self->expanded = qtrue;
	self->surf.numVerts = self->maxWidth * self->maxHeight;
	if ( self->width != self->maxWidth ) {
		for ( j = self->height - 1; j >= 0; j-- ) {
			for ( i = self->width - 1; i >= 0; i-- ) {
				self->surf.verts[j * self->maxWidth + i] = self->surf.verts[j * self->width + i];
			}
		}
	}
}

/*
=================
GenerateNormals

Handles all the complicated wrapping and degenerate cases
Expects a Not expanded patch.
=================
*/
#define	COPLANAR_EPSILON	0.1f

void GenerateNormals( surfacePatch_t *self ) {
	surfVert_t	*verts;
	int			width, height;
	int			i, j, k, dist;
	vec3_t		norm;
	vec3_t		sum;
	int			count;
	vec3_t		base;
	vec3_t		delta;
	int			x, y;
	vec3_t		around[8], temp;
	qboolean	good[8];
	qboolean	wrapWidth, wrapHeight;
	static int	neighbors[8][2] = {
		{0,1}, {1,1}, {1,0}, {1,-1}, {0,-1}, {-1,-1}, {-1,0}, {-1,1}
	};

	assert( self->expanded == qfalse );

	width = self->width;
	height = self->height;
	verts = self->surf.verts;

	//
	// if all points are coplanar, set all normals to that plane
	//
	vec3_t		extent[3];
	float		offset;

	VectorSubtract( verts[width - 1].xyz, verts[0].xyz, extent[0] );
	VectorSubtract( verts[(height - 1) * width + width - 1].xyz, verts[0].xyz, extent[1] );
	VectorSubtract( verts[(height - 1) * width].xyz, verts[0].xyz, extent[2] );

	CrossProduct( extent[0], extent[1], norm );
	if ( VectorLengthSquared( norm ) == 0.0f) {
		CrossProduct( extent[0], extent[2], norm );
		if ( VectorLengthSquared( norm ) == 0.0f ) {
			CrossProduct( extent[1], extent[2], norm );
		}
	}

	// wrapped patched may not get a valid normal here
	if ( VectorNormalize( norm ) != 0.0f) {

		offset = DotProduct( verts[0].xyz, norm );
		for (i = 1; i < width * height; i++) {
			float d = DotProduct( verts[i].xyz, norm );
			if ( fabs(d - offset) > COPLANAR_EPSILON ) {
				break;
			}
		}

		if (i == width * height) {
			// all are coplanar
			for (i = 0; i < width * height; i++) {
				VectorCopy( norm, verts[i].normal );
			}
			return;
		}
	}

	// check for wrapped edge cases, which should smooth across themselves
	wrapWidth = qfalse;
	for ( i = 0; i < height; i++ ) {
		VectorSubtract( verts[i * width].xyz, verts[i * width + width - 1].xyz, delta );
		if ( VectorLengthSquared( delta ) > Sqr( 1.0f ) ) {
			break;
		}
	}
	if (i == height) {
		wrapWidth = qtrue;
	}

	wrapHeight = qfalse;
	for ( i = 0; i < width; i++ ) {
		VectorSubtract( verts[i].xyz, verts[(height - 1) * width + i].xyz, delta );
		if ( VectorLengthSquared( delta ) > Sqr(1.0f) ) {
			break;
		}
	}
	if (i == width) {
		wrapHeight = qtrue;
	}

	for (i = 0; i < width; i++) {
		for (j = 0; j < height; j++) {
			count = 0;
			VectorCopy( verts[j * width + i].xyz, base );
			for (k = 0; k < 8; k++) {
				VectorClear( around[k] );
				good[k] = qfalse;

				for ( dist = 1; dist <= 3; dist++ ) {
					x = i + neighbors[k][0] * dist;
					y = j + neighbors[k][1] * dist;
					if ( wrapWidth ) {
						if ( x < 0 ) {
							x = width - 1 + x;
						}
						else if ( x >= width ) {
							x = 1 + x - width;
						}
					}
					if ( wrapHeight ) {
						if ( y < 0 ) {
							y = height - 1 + y;
						}
						else if ( y >= height ) {
							y = 1 + y - height;
						}
					}

					if ( x < 0 || x >= width || y < 0 || y >= height ) {
						break;					// edge of patch
					}
					VectorSubtract( verts[y * width + x].xyz, base, temp );
					if ( VectorNormalize( temp ) == 0.0f) {
						continue;				// degenerate edge, get more dist
					}
					else {
						good[k] = qtrue;
						VectorCopy( temp, around[k] );
						break;					// good edge
					}
				}
			}

			VectorClear( sum );
			for ( k = 0; k < 8; k++ ) {
				if ( !good[k] || !good[(k + 1) & 7] ) {
					continue;	// didn't get two points
				}
				CrossProduct( around[(k + 1) & 7], around[k], norm );
				if ( VectorNormalize( norm ) == 0.0f ) {
					continue;
				}
				VectorAdd( sum, norm, sum );
				count++;
			}
			if ( count == 0 ) {
				//idLib::common->Printf("bad normal\n");
				count = 1;
			}
			VectorCopy( sum, verts[j * width + i].normal );
			VectorNormalize( verts[j * width + i].normal );
		}
	}
}

/*
=================
GenerateIndexes
=================
*/
void GenerateIndexes( surfacePatch_t *self ) {
	int i, j, v1, v2, v3, v4, index;
	int newNumIndexes;
		
	newNumIndexes = (self->width - 1) * (self->height - 1) * 2 * 3;
	if ( newNumIndexes > self->surf.sizeIndexes ) {
        if ( self->surf.indexes ) {
		    ii.FreeMemory( self->surf.indexes );
        }
		self->surf.indexes = ( int * )ii.GetMemory( sizeof( *self->surf.indexes ) * newNumIndexes );
		self->surf.sizeIndexes = newNumIndexes;
	}
	self->surf.numIndexes = newNumIndexes;
	index = 0;
	for (i = 0; i < self->width - 1; i++) {
		for (j = 0; j < self->height - 1; j++) {
			v1 = j * self->width + i;
			v2 = v1 + 1;
			v3 = v1 + self->width + 1;
			v4 = v1 + self->width;
			self->surf.indexes[index++] = v1;
			self->surf.indexes[index++] = v3;
			self->surf.indexes[index++] = v2;
			self->surf.indexes[index++] = v1;
			self->surf.indexes[index++] = v4;
			self->surf.indexes[index++] = v3;
		}
	}

	SurfaceGenerateEdgeIndexes( &self->surf );
}

/*
===============
SampleSinglePatchPoint
===============
*/
void SampleSinglePatchPoint( const surfVert_t ctrl[3][3], float u, float v, surfVert_t *out ) {
	float	vCtrl[3][8];
	int		vPoint;
	int		axis;

	// find the control points for the v coordinate
	for (vPoint = 0; vPoint < 3; vPoint++) {
		for (axis = 0; axis < 8; axis++) {
			float a, b, c;
			float qA, qB, qC;
			if (axis < 3) {
				a = ctrl[0][vPoint].xyz[axis];
				b = ctrl[1][vPoint].xyz[axis];
				c = ctrl[2][vPoint].xyz[axis];
			}
			else if (axis < 6) {
				a = ctrl[0][vPoint].normal[axis - 3];
				b = ctrl[1][vPoint].normal[axis - 3];
				c = ctrl[2][vPoint].normal[axis - 3];
			}
			else {
				a = ctrl[0][vPoint].st[axis - 6];
				b = ctrl[1][vPoint].st[axis - 6];
				c = ctrl[2][vPoint].st[axis - 6];
			}
			qA = a - 2.0f * b + c;
			qB = 2.0f * b - 2.0f * a;
			qC = a;
			vCtrl[vPoint][axis] = qA * u * u + qB * u + qC;
		}
	}

	// interpolate the v value
	for ( axis = 0; axis < 8; axis++ ) {
		float a, b, c;
		float qA, qB, qC;

		a = vCtrl[0][axis];
		b = vCtrl[1][axis];
		c = vCtrl[2][axis];
		qA = a - 2.0f * b + c;
		qB = 2.0f * b - 2.0f * a;
		qC = a;

		if (axis < 3) {
			out->xyz[axis] = qA * v * v + qB * v + qC;
		}
		else if (axis < 6) {
			out->normal[axis - 3] = qA * v * v + qB * v + qC;
		}
		else {
			out->st[axis - 6] = qA * v * v + qB * v + qC;
		}
	}
}

/*
===================
SampleSinglePatch
===================
*/
void SampleSinglePatch( const surfVert_t *ctrl[3][3], int baseCol, int baseRow, int width, int horzSub, int vertSub, surfVert_t *outVerts ) {
	int		i, j;
	float	u, v;

	horzSub++;
	vertSub++;
	for ( i = 0; i < horzSub; i++ ) {
		for ( j = 0; j < vertSub; j++ ) {
			u = (float)i / (horzSub - 1);
			v = (float)j / (vertSub - 1);
			SampleSinglePatchPoint( ctrl, u, v, &outVerts[((baseRow + j) * width) + i + baseCol] );
		}
	}
}

/*
=================
SurfacePatchSubdivideExplicit
=================
*/
void SurfacePatchSubdivideExplicit( surfacePatch_t *self, int horzSubdivisions, int vertSubdivisions, qboolean genNormals, qboolean removeLinear ) {
	int i, j, k, l;
	surfVert_t sample[3][3];
	int outWidth = ( ( self->width - 1 ) / 2 * horzSubdivisions ) + 1;
	int outHeight = ( ( self->height - 1 ) / 2 * vertSubdivisions ) + 1;
	surfVert_t *dv = ( surfVert_t * )ii.GetMemory( sizeof( surfVert_t ) * outWidth * outHeight );

	// generate normals for the control mesh
	if ( genNormals ) {
		GenerateNormals( self );
	}

	int baseCol = 0;
	for (i = 0; i + 2 < self->width; i += 2) {
		int baseRow = 0;
		for (j = 0; j + 2 < self->height; j += 2) {
			for (k = 0; k < 3; k++) {
				for (l = 0; l < 3; l++) {
					sample[k][l] = self->surf.verts[((j + l) * self->width) + i + k];
				}
			}
			SampleSinglePatch(sample, baseCol, baseRow, outWidth, horzSubdivisions, vertSubdivisions, dv);
			baseRow += vertSubdivisions;
		}
		baseCol += horzSubdivisions;
	}
	if ( outWidth * outHeight > self->surf.sizeVerts ) {
		ii.FreeMemory( self->surf.verts );
		self->surf.verts = ( surfVert_t * )ii.GetMemory( sizeof( *self->surf.verts ) * outWidth * outHeight );
		self->surf.sizeVerts = outWidth * outHeight;
	}
	self->surf.numVerts = outWidth * outHeight;
	for (i = 0; i < outWidth * outHeight; i++) {
		self->surf.verts[i] = dv[i];
	}

	ii.FreeMemory( dv );

	self->width = self->maxWidth = outWidth;
	self->height = self->maxHeight = outHeight;
	self->expanded = qfalse;

	if ( removeLinear ) {
		Expand( self );
		RemoveLinearColumnsRows( self );
		Collapse( self );
	}

	// normalize all the lerped normals
	if ( genNormals ) {
		for ( i = 0; i < self->width * self->height; i++ ) {
			VectorNormalize( self->surf.verts[i].normal );
		}
	}

	GenerateIndexes( self );
}

/*
=================
SurfacePatchSubdivide
=================
*/
void SurfacePatchSubdivide( surfacePatch_t *self, float maxHorizontalError, float maxVerticalError, float maxLength, qboolean genNormals) {
	int			i, j, k, l;
	surfVert_t	*verts;
	surfVert_t	prev;
	surfVert_t	next, mid;
	vec3_t		prevxyz, nextxyz, midxyz;
	vec3_t		delta;
	float		maxHorizontalErrorSqr, maxVerticalErrorSqr, maxLengthSqr;

	DrawVertClear( &prev );
	next = prev;
	mid = prev;

	// generate normals for the control mesh
	if ( genNormals ) {
		GenerateNormals( self );
	}

	maxHorizontalErrorSqr = Sqr(maxHorizontalError);
	maxVerticalErrorSqr = Sqr(maxVerticalError);
	maxLengthSqr = Sqr(maxLength);

	Expand( self );

	// horizontal subdivisions
	verts = self->surf.verts;
	for (j = 0; j + 2 < self->width; j += 2) {
		// check subdivided midpoints against control points
		for (i = 0; i < self->height; i++) {
			for (l = 0; l < 3; l++) {
				prevxyz[l] = verts[i * self->maxWidth + j + 1].xyz[l] - verts[i * self->maxWidth + j].xyz[l];
				nextxyz[l] = verts[i * self->maxWidth + j + 2].xyz[l] - verts[i * self->maxWidth + j + 1].xyz[l];
				midxyz[l] = (verts[i * self->maxWidth + j].xyz[l] + verts[i * self->maxWidth + j + 1].xyz[l] * 2.0f +
					verts[i * self->maxWidth + j + 2].xyz[l]) * 0.25f;
			}

			if ( maxLength > 0.0f ) {
				// if the span length is too long, force a subdivision
				if ( VectorLengthSquared( prevxyz ) > maxLengthSqr || VectorLengthSquared( nextxyz ) > maxLengthSqr ) {
					break;
				}
			}
			// see if this midpoint is off far enough to subdivide
			VectorSubtract( verts[i * self->maxWidth + j + 1].xyz, midxyz, delta );
			if ( VectorLengthSquared( delta ) > maxHorizontalErrorSqr ) {
				break;
			}
		}

		if (i == self->height) {
			continue;	// didn't need subdivision
		}

		if ( self->width + 2 >= self->maxWidth ) {
			ResizeExpanded( self, self->maxHeight, self->maxWidth + 4 );
			verts = self->surf.verts;
		}

		// insert two columns and replace the peak
		self->width += 2;

		for (i = 0; i < self->height; i++) {
			LerpVert( &verts[i * self->maxWidth + j], &verts[i * self->maxWidth + j + 1], &prev );
			LerpVert( &verts[i * self->maxWidth + j + 1], &verts[i * self->maxWidth + j + 2], &next );
			LerpVert( &prev, &next, &mid );

			for ( k = self->width - 1; k > j + 3; k-- ) {
				verts[i * self->maxWidth + k] = verts[i * self->maxWidth + k - 2];
			}
			verts[i * self->maxWidth + j + 1] = prev;
			verts[i * self->maxWidth + j + 2] = mid;
			verts[i * self->maxWidth + j + 3] = next;
		}

		// back up and recheck this set again, it may need more subdivision
		j -= 2;
	}

	// vertical subdivisions
	verts = self->surf.verts;
	for (j = 0; j + 2 < self->height; j += 2) {
		// check subdivided midpoints against control points
		for (i = 0; i < self->width; i++) {
			for (l = 0; l < 3; l++) {
				prevxyz[l] = verts[(j + 1) * self->maxWidth + i].xyz[l] - verts[j * self->maxWidth + i].xyz[l];
				nextxyz[l] = verts[(j + 2) * self->maxWidth + i].xyz[l] - verts[(j + 1) * self->maxWidth + i].xyz[l];
				midxyz[l] = (verts[j * self->maxWidth + i].xyz[l] + verts[(j + 1) * self->maxWidth + i].xyz[l] * 2.0f +
					verts[(j + 2) * self->maxWidth + i].xyz[l]) * 0.25f;
			}

			if (maxLength > 0.0f) {
				// if the span length is too long, force a subdivision
				if ( VectorLengthSquared( prevxyz ) > maxLengthSqr || VectorLengthSquared( nextxyz ) > maxLengthSqr) {
					break;
				}
			}
			// see if this midpoint is off far enough to subdivide
			VectorSubtract( verts[(j + 1) * self->maxWidth + i].xyz, midxyz, delta );
			if ( VectorLengthSquared( delta ) > maxVerticalErrorSqr ) {
				break;
			}
		}

		if ( i == self->width ) {
			continue;	// didn't need subdivision
		}

		if ( self->height + 2 >= self->maxHeight ) {
			ResizeExpanded( self, self->maxHeight + 4, self->maxWidth );
			verts = self->surf.verts;
		}

		// insert two columns and replace the peak
		self->height += 2;

		for ( i = 0; i < self->width; i++ ) {
			LerpVert( &verts[j * self->maxWidth + i], &verts[(j + 1) * self->maxWidth + i], &prev );
			LerpVert( &verts[(j + 1) * self->maxWidth + i], &verts[(j + 2) * self->maxWidth + i], &next );
			LerpVert( &prev, &next, &mid);

			for ( k = self->height - 1; k > j + 3; k-- ) {
				verts[k * self->maxWidth + i] = verts[(k - 2) * self->maxWidth + i];
			}
			verts[(j + 1) * self->maxWidth + i] = prev;
			verts[(j + 2) * self->maxWidth + i] = mid;
			verts[(j + 3) * self->maxWidth + i] = next;
		}

		// back up and recheck this set again, it may need more subdivision
		j -= 2;
	}

	PutOnCurve( self );

	RemoveLinearColumnsRows( self );

	Collapse( self );

	// normalize all the lerped normals
	if ( genNormals ) {
		verts = self->surf.verts;
		for (i = 0; i < self->width * self->height; i++) {
			VectorNormalize( verts[i].normal );
		}
	}

	GenerateIndexes( self );
}
