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
// lcp.h -- Doom 3's box LCP solver for symmetric matrices (idLCP_Symmetric,
// idlib/math/Lcp)

#ifndef __LCP_H__
#define __LCP_H__

// solves for x: (m*x - b) complementary to the bounds lo <= x <= hi; a
// variable with boxIndex >= 0 has its bounds scaled by x[boxIndex]
qboolean	LCP_SolveSymmetric( float *m, int n, int stride, float *x, const float *b, const float *lo, const float *hi,
							const int *boxIndex );

#endif /* !__LCP_H__ */
