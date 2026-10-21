/*
===========================================================================
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.

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
// decl_entitydef.h -- entity definitions, Doom 3's entityDef declarations
// (framework/DeclEntityDef): def/*.def, and defs code adds

#ifndef __DECL_ENTITYDEF_H__
#define __DECL_ENTITYDEF_H__

struct dict_s;

void					EntityDef_Clear( void );
struct dict_s			*EntityDef_Add( const char *name );		// built into the game
const struct dict_s		*EntityDef_Find( const char *name );	// with what it inherits, or NULL
int						EntityDef_Num( void );
const char				*EntityDef_Name( int num );
const struct dict_s		*EntityDef_ForNum( int num );
void					EntityDef_LoadAll( void );				// every def/*.def, in name order

#endif /* !__DECL_ENTITYDEF_H__ */
