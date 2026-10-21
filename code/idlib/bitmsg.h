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
// bitmsg.h -- Doom 3's idBitMsg (idlib/BitMsg)

#ifndef __BITMSG_H__
#define __BITMSG_H__

typedef struct {
	int			*data;
	int			maxBits;
	int			bit;
	qboolean	overflowed;
} bitMsg_t;

void	BitMsg_Init( bitMsg_t *msg, int *words, int numWords );
void	BitMsg_WriteBits( bitMsg_t *msg, int value, int numBits );		// numBits < 0: signed
int		BitMsg_ReadBits( bitMsg_t *msg, int numBits );
void	BitMsg_WriteFloat( bitMsg_t *msg, float f );
float	BitMsg_ReadFloat( bitMsg_t *msg );

#endif /* !__BITMSG_H__ */
