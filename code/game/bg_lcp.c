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
// bg_lcp.c -- Doom 3's box LCP solver for symmetric matrices (idLCP_Symmetric)
//
// A Dantzig-style pivoting solver: the clamped variables' sub matrix is kept
// LDL' factored and updated as variables are added and removed. The SIMD
// routines it used are plain loops here.

#include "../idlib/q_shared.h"
#include "bg_public.h"
#include "bg_af.h"

#define LCP_BOUND_EPSILON			1e-5f
#define LCP_ACCEL_EPSILON			1e-5f
#define LCP_DELTA_ACCEL_EPSILON		1e-9f
#define LCP_DELTA_FORCE_EPSILON		1e-9f
#define LCP_MAX_ITERATIONS			32
#define LCP_INFINITY				1e30f

#define LCP_MAX_SIZE				384

// the solver's state; a solve is never re-entered
static float	*lcp_m;
static int		lcp_n, lcp_stride;
static float	lcp_b[LCP_MAX_SIZE], lcp_lo[LCP_MAX_SIZE], lcp_hi[LCP_MAX_SIZE];
static float	lcp_f[LCP_MAX_SIZE], lcp_a[LCP_MAX_SIZE];
static float	lcp_deltaF[LCP_MAX_SIZE], lcp_deltaA[LCP_MAX_SIZE];
static float	lcp_clamped[LCP_MAX_SIZE * LCP_MAX_SIZE];		// LDL' factored, lcp_n columns
static float	lcp_diagonal[LCP_MAX_SIZE];					// reciprocals of D
static float	lcp_solveCache1[LCP_MAX_SIZE], lcp_solveCache2[LCP_MAX_SIZE];
static float	lcp_tmp[LCP_MAX_SIZE], lcp_v1[LCP_MAX_SIZE], lcp_v2[LCP_MAX_SIZE];
static float	*lcp_rowPtrs[LCP_MAX_SIZE];
static int		lcp_boxIndexBuf[LCP_MAX_SIZE], *lcp_boxIndex;
static int		lcp_side[LCP_MAX_SIZE], lcp_permuted[LCP_MAX_SIZE];
static int		lcp_numUnbounded, lcp_numClamped, lcp_clampedChangeStart;

#define CLAMPED( r, c )		lcp_clamped[(r) * lcp_n + (c)]

static ID_INLINE float LCP_Dot( const float *a, const float *b, int n ) {
	float sum = 0.0f;
	int i;

	for ( i = 0; i < n; i++ ) {
		sum += a[i] * b[i];
	}
	return sum;
}

/*
============
LCP_LowerTriangularSolve

Solves x in L * x = b for the first n rows, L lower triangular with ones on
the diagonal; the first skip elements of x are already solved.
============
*/
static void LCP_LowerTriangularSolve( float *x, const float *b, int n, int skip ) {
	int i, j;
	double sum;

	for ( i = skip; i < n; i++ ) {
		const float *row = &lcp_clamped[i * lcp_n];

		sum = b[i];
		for ( j = 0; j < i; j++ ) {
			sum -= row[j] * x[j];
		}
		x[i] = sum;
	}
}

/*
============
LCP_LowerTriangularSolveTranspose

Solves x in L' * x = b, L lower triangular with ones on the diagonal.
============
*/
static void LCP_LowerTriangularSolveTranspose( float *x, const float *b, int n ) {
	int i, j;
	double sum;

	for ( i = n - 1; i >= 0; i-- ) {
		sum = b[i];
		for ( j = i + 1; j < n; j++ ) {
			sum -= CLAMPED( j, i ) * x[j];
		}
		x[i] = sum;
	}
}

/*
============
LCP_LDLTFactor

In-place LDL' of the n x n clamped sub matrix, the reciprocals of D stored.
============
*/
static qboolean LCP_LDLTFactor( int n ) {
	int i, j, k;
	double sum, d;

	for ( i = 0; i < n; i++ ) {
		float *row = &lcp_clamped[i * lcp_n];

		for ( k = 0; k < i; k++ ) {
			lcp_tmp[k] = CLAMPED( k, k ) * row[k];
		}
		sum = row[i];
		for ( k = 0; k < i; k++ ) {
			sum -= lcp_tmp[k] * row[k];
		}
		if ( sum == 0.0 ) {
			return qfalse;
		}
		row[i] = sum;
		lcp_diagonal[i] = d = 1.0 / sum;

		for ( j = i + 1; j < n; j++ ) {
			float *other = &lcp_clamped[j * lcp_n];

			sum = other[i];
			for ( k = 0; k < i; k++ ) {
				sum -= lcp_tmp[k] * other[k];
			}
			other[i] = sum * d;
		}
	}
	return qtrue;
}

static qboolean LCP_FactorClamped( void ) {
	int i;

	lcp_clampedChangeStart = 0;

	for ( i = 0; i < lcp_numClamped; i++ ) {
		memcpy( &lcp_clamped[i * lcp_n], lcp_rowPtrs[i], lcp_numClamped * sizeof( float ) );
	}
	return LCP_LDLTFactor( lcp_numClamped );
}

static void LCP_SolveClamped( float *x, const float *b ) {
	int i;

	// solve L
	LCP_LowerTriangularSolve( lcp_solveCache1, b, lcp_numClamped, lcp_clampedChangeStart );

	// solve D
	for ( i = 0; i < lcp_numClamped; i++ ) {
		lcp_solveCache2[i] = lcp_solveCache1[i] * lcp_diagonal[i];
	}

	// solve Lt
	LCP_LowerTriangularSolveTranspose( x, lcp_solveCache2, lcp_numClamped );

	lcp_clampedChangeStart = lcp_numClamped;
}

#define LCP_SWAPF( a, b )	{ float t = (a); (a) = (b); (b) = t; }
#define LCP_SWAPI( a, b )	{ int t = (a); (a) = (b); (b) = t; }

static void LCP_SwapColumns( int i, int j ) {
	int r;

	for ( r = 0; r < lcp_n; r++ ) {
		float *row = lcp_m + r * lcp_stride;
		LCP_SWAPF( row[i], row[j] );
	}
}

static void LCP_Swap( int i, int j ) {
	float *p;

	if ( i == j ) {
		return;
	}

	p = lcp_rowPtrs[i]; lcp_rowPtrs[i] = lcp_rowPtrs[j]; lcp_rowPtrs[j] = p;
	LCP_SwapColumns( i, j );
	LCP_SWAPF( lcp_b[i], lcp_b[j] );
	LCP_SWAPF( lcp_lo[i], lcp_lo[j] );
	LCP_SWAPF( lcp_hi[i], lcp_hi[j] );
	LCP_SWAPF( lcp_a[i], lcp_a[j] );
	LCP_SWAPF( lcp_f[i], lcp_f[j] );
	if ( lcp_boxIndex ) {
		LCP_SWAPI( lcp_boxIndex[i], lcp_boxIndex[j] );
	}
	LCP_SWAPI( lcp_side[i], lcp_side[j] );
	LCP_SWAPI( lcp_permuted[i], lcp_permuted[j] );
}

static void LCP_AddClamped( int r, qboolean useSolveCache ) {
	float d, dot;
	int i, nc = lcp_numClamped;

	if ( nc < lcp_clampedChangeStart ) {
		lcp_clampedChangeStart = nc;
	}

	// add a row at the bottom and a column at the right of the factored
	// matrix for the clamped variables
	LCP_Swap( nc, r );

	// solve for v in L * v = rowPtr[numClamped]
	if ( useSolveCache ) {
		// the lower triangular solve was cached in SolveClamped called by CalcForceDelta
		memcpy( &lcp_clamped[nc * lcp_n], lcp_solveCache2, nc * sizeof( float ) );
		dot = LCP_Dot( lcp_solveCache2, lcp_solveCache1, nc );
	} else {
		LCP_LowerTriangularSolve( lcp_tmp, lcp_rowPtrs[nc], nc, 0 );
		// add bottom row to L
		for ( i = 0; i < nc; i++ ) {
			CLAMPED( nc, i ) = lcp_tmp[i] * lcp_diagonal[i];
		}
		dot = LCP_Dot( &lcp_clamped[nc * lcp_n], lcp_tmp, nc );
	}

	// update diagonal[numClamped]
	d = lcp_rowPtrs[nc][nc] - dot;

	if ( d == 0.0f ) {
		Com_DPrintf( "LCP_AddClamped: updating factorization failed\n" );
		lcp_numClamped++;
		return;
	}

	CLAMPED( nc, nc ) = d;
	lcp_diagonal[nc] = 1.0f / d;

	lcp_numClamped++;
}

static void LCP_RemoveClamped( int r ) {
	int i, j, n;
	float *addSub, *original, *v, *ptr;
	double sum, diag, newDiag, invNewDiag, p1, p2, alpha1, alpha2, beta1, beta2;
	float addSubBuf[LCP_MAX_SIZE];

	if ( r < lcp_clampedChangeStart ) {
		lcp_clampedChangeStart = r;
	}

	lcp_numClamped--;

	// no need to swap and update the factored matrix when the last row and column are removed
	if ( r == lcp_numClamped ) {
		return;
	}

	// swap the to be removed row/column with the last row/column
	LCP_Swap( r, lcp_numClamped );

	// update the factored matrix
	addSub = addSubBuf;

	if ( r == 0 ) {

		if ( lcp_numClamped == 1 ) {
			diag = lcp_rowPtrs[0][0];
			if ( diag == 0.0f ) {
				Com_DPrintf( "LCP_RemoveClamped: updating factorization failed\n" );
				return;
			}
			CLAMPED( 0, 0 ) = diag;
			lcp_diagonal[0] = 1.0f / diag;
			return;
		}

		// calculate the row/column to be added to the lower right sub matrix starting at (r, r)
		original = lcp_rowPtrs[lcp_numClamped];
		ptr = lcp_rowPtrs[r];
		addSub[0] = ptr[0] - original[lcp_numClamped];
		for ( i = 1; i < lcp_numClamped; i++ ) {
			addSub[i] = ptr[i] - original[i];
		}

	} else {

		v = lcp_tmp;

		// solve for v in L * v = rowPtr[r]
		LCP_LowerTriangularSolve( v, lcp_rowPtrs[r], r, 0 );

		// update removed row
		for ( i = 0; i < r; i++ ) {
			CLAMPED( r, i ) = v[i] * lcp_diagonal[i];
		}

		// if the last row/column of the matrix is updated
		if ( r == lcp_numClamped - 1 ) {
			// only calculate new diagonal
			diag = lcp_rowPtrs[r][r] - LCP_Dot( &lcp_clamped[r * lcp_n], v, r );
			if ( diag == 0.0f ) {
				Com_DPrintf( "LCP_RemoveClamped: updating factorization failed\n" );
				return;
			}
			CLAMPED( r, r ) = diag;
			lcp_diagonal[r] = 1.0f / diag;
			return;
		}

		// calculate the row/column to be added to the lower right sub matrix starting at (r, r)
		for ( i = 0; i < r; i++ ) {
			v[i] = CLAMPED( r, i ) * CLAMPED( i, i );
		}
		for ( i = r; i < lcp_numClamped; i++ ) {
			if ( i == r ) {
				sum = CLAMPED( r, r );
			} else {
				sum = CLAMPED( r, r ) * CLAMPED( i, r );
			}
			ptr = &lcp_clamped[i * lcp_n];
			for ( j = 0; j < r; j++ ) {
				sum += ptr[j] * v[j];
			}
			addSub[i] = lcp_rowPtrs[r][i] - sum;
		}
	}

	// add row/column to the lower right sub matrix starting at (r, r)
	diag = 0.70710678118654752440;		// sqrt( 1/2 )
	lcp_v1[r] = ( 0.5f * addSub[r] + 1.0f ) * diag;
	lcp_v2[r] = ( 0.5f * addSub[r] - 1.0f ) * diag;
	for ( i = r + 1; i < lcp_numClamped; i++ ) {
		lcp_v1[i] = lcp_v2[i] = addSub[i] * diag;
	}

	alpha1 = 1.0f;
	alpha2 = -1.0f;

	// simultaneous update/downdate of the sub matrix starting at (r, r)
	n = lcp_n;
	for ( i = r; i < lcp_numClamped; i++ ) {

		diag = CLAMPED( i, i );
		p1 = lcp_v1[i];
		newDiag = diag + alpha1 * p1 * p1;

		if ( newDiag == 0.0f ) {
			Com_DPrintf( "LCP_RemoveClamped: updating factorization failed\n" );
			return;
		}

		alpha1 /= newDiag;
		beta1 = p1 * alpha1;
		alpha1 *= diag;

		diag = newDiag;
		p2 = lcp_v2[i];
		newDiag = diag + alpha2 * p2 * p2;

		if ( newDiag == 0.0f ) {
			Com_DPrintf( "LCP_RemoveClamped: updating factorization failed\n" );
			return;
		}

		CLAMPED( i, i ) = newDiag;
		lcp_diagonal[i] = invNewDiag = 1.0f / newDiag;

		alpha2 *= invNewDiag;
		beta2 = p2 * alpha2;
		alpha2 *= diag;

		// update column below diagonal (i,i)
		ptr = lcp_clamped + i;
		for ( j = i + 1; j < lcp_numClamped; j++ ) {
			sum = ptr[j * n];

			lcp_v1[j] -= p1 * sum;
			sum += beta1 * lcp_v1[j];

			lcp_v2[j] -= p2 * sum;
			sum += beta2 * lcp_v2[j];

			ptr[j * n] = sum;
		}
	}
}

static void LCP_CalcForceDelta( int d, float dir ) {
	int i;

	lcp_deltaF[d] = dir;

	if ( lcp_numClamped == 0 ) {
		return;
	}

	// solve force delta
	LCP_SolveClamped( lcp_deltaF, lcp_rowPtrs[d] );

	// flip force delta based on direction
	if ( dir > 0.0f ) {
		for ( i = 0; i < lcp_numClamped; i++ ) {
			lcp_deltaF[i] = -lcp_deltaF[i];
		}
	}
}

static void LCP_CalcAccelDelta( int d ) {
	int j;

	// only the not clamped variables, including the current variable, can have a change in acceleration
	for ( j = lcp_numClamped; j <= d; j++ ) {
		// only the clamped variables and the current variable have a force delta unequal zero
		lcp_deltaA[j] = LCP_Dot( lcp_rowPtrs[j], lcp_deltaF, lcp_numClamped ) + lcp_rowPtrs[j][d] * lcp_deltaF[d];
	}
}

static void LCP_ChangeForce( int d, float step ) {
	int i;

	// only the clamped variables and current variable have a force delta unequal zero
	for ( i = 0; i < lcp_numClamped; i++ ) {
		lcp_f[i] += step * lcp_deltaF[i];
	}
	lcp_f[d] += step * lcp_deltaF[d];
}

static void LCP_ChangeAccel( int d, float step ) {
	int i;

	// only the not clamped variables, including the current variable, can have an acceleration unequal zero
	for ( i = lcp_numClamped; i <= d; i++ ) {
		lcp_a[i] += step * lcp_deltaA[i];
	}
}

static void LCP_GetMaxStep( int d, float dir, float *maxStep, int *limit, int *limitSide ) {
	int i;
	float s;

	// default to a full step for the current variable
	if ( fabs( lcp_deltaA[d] ) > LCP_DELTA_ACCEL_EPSILON ) {
		*maxStep = -lcp_a[d] / lcp_deltaA[d];
	} else {
		*maxStep = 0.0f;
	}
	*limit = d;
	*limitSide = 0;

	// test the current variable
	if ( dir < 0.0f ) {
		if ( lcp_lo[d] != -LCP_INFINITY ) {
			s = ( lcp_lo[d] - lcp_f[d] ) / dir;
			if ( s < *maxStep ) {
				*maxStep = s;
				*limitSide = -1;
			}
		}
	} else {
		if ( lcp_hi[d] != LCP_INFINITY ) {
			s = ( lcp_hi[d] - lcp_f[d] ) / dir;
			if ( s < *maxStep ) {
				*maxStep = s;
				*limitSide = 1;
			}
		}
	}

	// test the clamped bounded variables
	for ( i = lcp_numUnbounded; i < lcp_numClamped; i++ ) {
		if ( lcp_deltaF[i] < -LCP_DELTA_FORCE_EPSILON ) {
			// if there is a low boundary
			if ( lcp_lo[i] != -LCP_INFINITY ) {
				s = ( lcp_lo[i] - lcp_f[i] ) / lcp_deltaF[i];
				if ( s < *maxStep ) {
					*maxStep = s;
					*limit = i;
					*limitSide = -1;
				}
			}
		} else if ( lcp_deltaF[i] > LCP_DELTA_FORCE_EPSILON ) {
			// if there is a high boundary
			if ( lcp_hi[i] != LCP_INFINITY ) {
				s = ( lcp_hi[i] - lcp_f[i] ) / lcp_deltaF[i];
				if ( s < *maxStep ) {
					*maxStep = s;
					*limit = i;
					*limitSide = 1;
				}
			}
		}
	}

	// test the not clamped bounded variables
	for ( i = lcp_numClamped; i < d; i++ ) {
		if ( lcp_side[i] == -1 ) {
			if ( lcp_deltaA[i] >= -LCP_DELTA_ACCEL_EPSILON ) {
				continue;
			}
		} else if ( lcp_side[i] == 1 ) {
			if ( lcp_deltaA[i] <= LCP_DELTA_ACCEL_EPSILON ) {
				continue;
			}
		} else {
			continue;
		}
		// ignore variables for which the force is not allowed to take any substantial value
		if ( lcp_lo[i] >= -LCP_BOUND_EPSILON && lcp_hi[i] <= LCP_BOUND_EPSILON ) {
			continue;
		}
		s = -lcp_a[i] / lcp_deltaA[i];
		if ( s < *maxStep ) {
			*maxStep = s;
			*limit = i;
			*limitSide = 0;
		}
	}
}

/*
============
LCP_SolveSymmetric

m is n x n with rows stride floats apart; it is changed while solving but
restored. Bounds of +-LCP_INFINITY (1e30) or more are unbounded.
============
*/
qboolean LCP_SolveSymmetric( float *m, int n, int stride, float *x, const float *b, const float *lo, const float *hi,
							const int *boxIndex ) {
	int i, j, k, limit, limitSide, boxStartIndex, numIgnored;
	float dir, maxStep, s;

	if ( n > LCP_MAX_SIZE ) {
		Com_DPrintf( "LCP_SolveSymmetric: %d variables, more than %d\n", n, LCP_MAX_SIZE );
		return qfalse;
	}

	lcp_m = m;
	lcp_n = n;
	lcp_stride = stride;

	for ( i = 0; i < n; i++ ) {
		lcp_f[i] = 0.0f;
		lcp_a[i] = 0.0f;
		lcp_b[i] = b[i];
		lcp_lo[i] = lo[i] <= -LCP_INFINITY ? -LCP_INFINITY : lo[i];
		lcp_hi[i] = hi[i] >= LCP_INFINITY ? LCP_INFINITY : hi[i];
		lcp_rowPtrs[i] = m + i * stride;
		lcp_permuted[i] = i;
		lcp_side[i] = 0;
	}
	if ( boxIndex ) {
		memcpy( lcp_boxIndexBuf, boxIndex, n * sizeof( int ) );
		lcp_boxIndex = lcp_boxIndexBuf;
	} else {
		lcp_boxIndex = NULL;
	}

	// permute input so all unbounded variables come first
	lcp_numUnbounded = 0;
	for ( i = 0; i < n; i++ ) {
		if ( lcp_lo[i] == -LCP_INFINITY && lcp_hi[i] == LCP_INFINITY ) {
			if ( lcp_numUnbounded != i ) {
				LCP_Swap( lcp_numUnbounded, i );
			}
			lcp_numUnbounded++;
		}
	}

	// permute input so all variables using the boxIndex come last
	boxStartIndex = n;
	if ( lcp_boxIndex ) {
		for ( i = n - 1; i >= lcp_numUnbounded; i-- ) {
			if ( lcp_boxIndex[i] >= 0 && ( lcp_lo[i] != -LCP_INFINITY || lcp_hi[i] != LCP_INFINITY ) ) {
				boxStartIndex--;
				if ( boxStartIndex != i ) {
					LCP_Swap( boxStartIndex, i );
				}
			}
		}
	}

	// all unbounded variables are clamped
	lcp_numClamped = lcp_numUnbounded;

	// if there are unbounded variables
	if ( lcp_numUnbounded ) {

		// factor and solve for unbounded variables
		if ( !LCP_FactorClamped() ) {
			Com_DPrintf( "LCP_SolveSymmetric: unbounded factorization failed\n" );
			return qfalse;
		}
		LCP_SolveClamped( lcp_f, lcp_b );

		// if there are no bounded variables we are done
		if ( lcp_numUnbounded == n ) {
			for ( i = 0; i < n; i++ ) {
				x[i] = lcp_f[i];	// the vector is not permuted
			}
			return qtrue;
		}
	}

	numIgnored = 0;

	// solve for bounded variables
	for ( i = lcp_numUnbounded; i < n; i++ ) {

		lcp_clampedChangeStart = 0;

		// once we hit the box start index we can initialize the low and high boundaries of the variables using the box index
		if ( i == boxStartIndex ) {
			for ( j = 0; j < boxStartIndex; j++ ) {
				x[lcp_permuted[j]] = lcp_f[j];
			}
			for ( j = boxStartIndex; j < n; j++ ) {
				s = x[lcp_boxIndex[j]];
				if ( lcp_lo[j] != -LCP_INFINITY ) {
					lcp_lo[j] = -fabs( lcp_lo[j] * s );
				}
				if ( lcp_hi[j] != LCP_INFINITY ) {
					lcp_hi[j] = fabs( lcp_hi[j] * s );
				}
			}
		}

		// calculate acceleration for current variable
		lcp_a[i] = LCP_Dot( lcp_rowPtrs[i], lcp_f, i ) - lcp_b[i];

		// if already at the low boundary
		if ( lcp_lo[i] >= -LCP_BOUND_EPSILON && lcp_a[i] >= -LCP_ACCEL_EPSILON ) {
			lcp_side[i] = -1;
			continue;
		}

		// if already at the high boundary
		if ( lcp_hi[i] <= LCP_BOUND_EPSILON && lcp_a[i] <= LCP_ACCEL_EPSILON ) {
			lcp_side[i] = 1;
			continue;
		}

		// if inside the clamped region
		if ( fabs( lcp_a[i] ) <= LCP_ACCEL_EPSILON ) {
			lcp_side[i] = 0;
			LCP_AddClamped( i, qfalse );
			continue;
		}

		// drive the current variable into a valid region
		for ( k = 0; k < LCP_MAX_ITERATIONS; k++ ) {

			// direction to move
			dir = lcp_a[i] <= 0.0f ? 1.0f : -1.0f;

			// calculate force delta
			LCP_CalcForceDelta( i, dir );

			// calculate acceleration delta: delta_a = m * delta_f;
			LCP_CalcAccelDelta( i );

			// maximum step we can take
			LCP_GetMaxStep( i, dir, &maxStep, &limit, &limitSide );

			if ( maxStep <= 0.0f ) {
				// ignore the current variable completely
				lcp_lo[i] = lcp_hi[i] = 0.0f;
				lcp_f[i] = 0.0f;
				lcp_side[i] = -1;
				numIgnored++;
				break;
			}

			// change force
			LCP_ChangeForce( i, maxStep );

			// change acceleration
			LCP_ChangeAccel( i, maxStep );

			// clamp/unclamp the variable that limited this step
			lcp_side[limit] = limitSide;
			switch ( limitSide ) {
				case 0:
					lcp_a[limit] = 0.0f;
					LCP_AddClamped( limit, ( limit == i ) );
					break;
				case -1:
					lcp_f[limit] = lcp_lo[limit];
					if ( limit != i ) {
						LCP_RemoveClamped( limit );
					}
					break;
				case 1:
					lcp_f[limit] = lcp_hi[limit];
					if ( limit != i ) {
						LCP_RemoveClamped( limit );
					}
					break;
			}

			// if the current variable limited the step we can continue with the next variable
			if ( limit == i ) {
				break;
			}
		}

		if ( k >= LCP_MAX_ITERATIONS ) {
			// clear remaining forces
			Com_DPrintf( "LCP_SolveSymmetric: max iterations (%d of %d bounded variables ignored)\n",
						n - i, n - lcp_numUnbounded );
			for ( j = i; j < n; j++ ) {
				lcp_f[j] = 0.0f;
			}
			break;
		}
	}

	if ( numIgnored ) {
		Com_DPrintf( "LCP_SolveSymmetric: %d of %d bounded variables ignored\n", numIgnored, n - lcp_numUnbounded );
	}

	// unpermute result
	for ( i = 0; i < n; i++ ) {
		x[lcp_permuted[i]] = lcp_f[i];
	}

	// unpermute original matrix
	for ( i = 0; i < n; i++ ) {
		for ( j = 0; j < n; j++ ) {
			if ( lcp_permuted[j] == i ) {
				break;
			}
		}
		if ( i != j ) {
			LCP_SwapColumns( i, j );
			LCP_SWAPI( lcp_permuted[i], lcp_permuted[j] );
		}
	}

	return qtrue;
}
