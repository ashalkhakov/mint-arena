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

// entityDefs.c -- entity definitions, Doom 3's entityDef declarations
//
// def/*.def hold blocks of
//
//	entityDef <name> {
//		"inherit"	"<other def>"	// its keys, unless this one sets them
//		"<key>"		"<value>"
//		...
//	}
//
// with // and /* */ comments. Other kinds of blocks (Doom 3's "model" and
// the like) are skipped. A later def of a name replaces an earlier one,
// files load in name order. The game and the cgame load the same files, so
// they know the same defs. Code adds defs of its own (EntityDef_Add) for things
// built into the game; a file's def of such a name changes the keys it sets.

#include "idlib_local.h"
#include "dict.h"
#include "decl_entitydef.h"

#define MAX_ENTITYDEFS			2048
#define MAX_DEF_NAME		64
#define DEF_HASH_SIZE		1024
#define MAX_DEF_FILES		4096		// bytes of def/ file names

typedef struct entityDef_s {
	char			name[MAX_DEF_NAME];
	dict_t			dict;
	qboolean		resolved;		// "inherit" merged in
	qboolean		resolving;		// to catch loops
	qboolean		builtin;		// added by code: a file's def of the name only changes its keys
	struct entityDef_s	*hashNext;
} entityDef_t;

static entityDef_t	*entityDefs[MAX_ENTITYDEFS];
static int		numEntityDefs;
static entityDef_t	*entityDefHash[DEF_HASH_SIZE];

static int EntityDef_Hash( const char *name ) {
	int hash = 0;

	for ( ; *name; name++ ) {
		int c = *name;
		hash = hash * 31 + ( ( c >= 'A' && c <= 'Z' ) ? c - 'A' + 'a' : c );
	}
	return hash & ( DEF_HASH_SIZE - 1 );
}

static entityDef_t *EntityDef_FindRaw( const char *name ) {
	entityDef_t *def;

	for ( def = entityDefHash[EntityDef_Hash( name )]; def; def = def->hashNext ) {
		if ( !Q_stricmp( def->name, name ) ) {
			return def;
		}
	}
	return NULL;
}

/*
================
EntityDef_Clear
================
*/
void EntityDef_Clear( void ) {
	int i;

	for ( i = 0; i < numEntityDefs; i++ ) {
		DictFree( &entityDefs[i]->dict );
		ii.FreeMemory( entityDefs[i] );
	}
	numEntityDefs = 0;
	memset( entityDefHash, 0, sizeof( entityDefHash ) );
}

/*
================
EntityDef_Add

A def with the name, for things built into the game; it starts empty. A
def a file gives for the name later only changes the keys it sets, where
a file's def replaces any other def of its name.
================
*/
static dict_t *EntityDef_AddFrom( const char *name, qboolean builtin ) {
	entityDef_t *def;
	int hash;

	def = EntityDef_FindRaw( name );
	if ( def ) {
		if ( !def->builtin || builtin ) {
			DictClear( &def->dict );
		}
		def->builtin = builtin;
		def->resolved = qfalse;
		return &def->dict;
	}
	if ( numEntityDefs == MAX_ENTITYDEFS ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: more than %d entity defs, %s left out\n", MAX_ENTITYDEFS, name );
		return NULL;
	}

	def = ii.GetMemory( sizeof( *def ) );
	memset( def, 0, sizeof( *def ) );
	def->builtin = builtin;
	Q_strncpyz( def->name, name, sizeof( def->name ) );
	DictInit( &def->dict );

	hash = EntityDef_Hash( name );
	def->hashNext = entityDefHash[hash];
	entityDefHash[hash] = def;
	entityDefs[numEntityDefs++] = def;
	return &def->dict;
}

dict_t *EntityDef_Add( const char *name ) {
	return EntityDef_AddFrom( name, qtrue );
}

/*
================
EntityDef_Resolve

Merges in what the def inherits, once.
================
*/
static void EntityDef_Resolve( entityDef_t *def ) {
	const char *parentName;
	entityDef_t *parent;

	if ( def->resolved ) {
		return;
	}
	if ( def->resolving ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: entity def %s inherits from itself\n", def->name );
		return;
	}
	def->resolving = qtrue;

	// the parent's own parents first, then its keys under this one's
	while ( DictGetString( &def->dict, "inherit", "", &parentName ) && parentName[0] ) {
		char name[MAX_DEF_NAME];

		Q_strncpyz( name, parentName, sizeof( name ) );
		DictDelete( &def->dict, "inherit" );
		parent = EntityDef_FindRaw( name );
		if ( !parent ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: entity def %s inherits from unknown def %s\n", def->name, name );
			continue;
		}
		EntityDef_Resolve( parent );
		DictSetDefaults( &def->dict, &parent->dict );
	}

	// a def's classname is its name, whatever it inherits
	DictSet( &def->dict, "classname", def->name );

	def->resolving = qfalse;
	def->resolved = qtrue;
}

/*
================
EntityDef_Find

The def of the name, with what it inherits, or NULL.
================
*/
const dict_t *EntityDef_Find( const char *name ) {
	entityDef_t *def;

	if ( !name || !name[0] ) {
		return NULL;
	}
	def = EntityDef_FindRaw( name );
	if ( !def ) {
		return NULL;
	}
	EntityDef_Resolve( def );
	return &def->dict;
}

int EntityDef_Num( void ) {
	return numEntityDefs;
}

const char *EntityDef_Name( int num ) {
	if ( num < 0 || num >= numEntityDefs ) {
		return NULL;
	}
	return entityDefs[num]->name;
}

const dict_t *EntityDef_ForNum( int num ) {
	if ( num < 0 || num >= numEntityDefs ) {
		return NULL;
	}
	EntityDef_Resolve( entityDefs[num] );
	return &entityDefs[num]->dict;
}

/*
================
EntityDef_SkipBlock

Past the closing brace of a block whose opening brace was read.
================
*/
static void EntityDef_SkipBlock( char **p ) {
	int depth = 1;
	char *token;

	while ( depth > 0 ) {
		token = COM_ParseExt( p, qtrue );
		if ( !token[0] ) {
			return;
		}
		if ( !strcmp( token, "{" ) ) {
			depth++;
		} else if ( !strcmp( token, "}" ) ) {
			depth--;
		}
	}
}

/*
================
EntityDef_ParseFile
================
*/
static void EntityDef_ParseFile( const char *fileName, char *text ) {
	char *p = text, *token;
	char name[MAX_DEF_NAME], key[MAX_TOKEN_CHARS];
	dict_t *dict;
	int line;

	COM_BeginParseSession( fileName );

	while ( 1 ) {
		token = COM_ParseExt( &p, qtrue );
		if ( !token[0] ) {
			break;
		}

		// Doom 3 files hold other declarations too: skip to their block
		if ( Q_stricmp( token, "entityDef" ) ) {
			while ( token[0] && strcmp( token, "{" ) ) {
				token = COM_ParseExt( &p, qtrue );
			}
			if ( token[0] ) {
				EntityDef_SkipBlock( &p );
			}
			continue;
		}

		token = COM_ParseExt( &p, qtrue );
		Q_strncpyz( name, token, sizeof( name ) );
		token = COM_ParseExt( &p, qtrue );
		if ( strcmp( token, "{" ) ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: %s, line %d: no { after entityDef %s\n", fileName, COM_GetCurrentParseLine(), name );
			break;
		}

		dict = EntityDef_AddFrom( name, qfalse );
		while ( 1 ) {
			token = COM_ParseExt( &p, qtrue );
			if ( !token[0] ) {
				Com_Printf( S_COLOR_YELLOW "WARNING: %s: entityDef %s isn't closed\n", fileName, name );
				return;
			}
			if ( !strcmp( token, "}" ) ) {
				break;
			}
			Q_strncpyz( key, token, sizeof( key ) );
			line = COM_GetCurrentParseLine();

			// the value is on the key's line; "" is a value, nothing isn't
			while ( *p == ' ' || *p == '\t' ) {
				p++;
			}
			if ( !*p || *p == '\n' || *p == '\r' || ( p[0] == '/' && ( p[1] == '/' || p[1] == '*' ) ) ) {
				Com_Printf( S_COLOR_YELLOW "WARNING: %s, line %d: key %s of %s has no value\n", fileName, line, key, name );
				continue;
			}
			token = COM_ParseExt( &p, qfalse );
			if ( dict ) {
				DictSet( dict, key, token );
			}
		}
	}
}

/*
================
EntityDef_LoadAll

Every .def file in def/, in name order. The defs added by code before stay unless a
file replaces them.
================
*/
static int QDECL EntityDef_CompareFileNames( const void *a, const void *b ) {
	return Q_stricmp( *(const char **)a, *(const char **)b );
}

void EntityDef_LoadAll( void ) {
	char fileList[MAX_DEF_FILES], *fileName, *names[256];
	char path[MAX_QPATH];
	int numFiles, i;
	long length;
	void *text;

	numFiles = ii.FS_GetFileList( "def", ".def", fileList, sizeof( fileList ) );
	Com_DPrintf( "def/: %d files\n", numFiles );
	numFiles = MIN( numFiles, (int)ARRAY_LEN( names ) );
	fileName = fileList;
	for ( i = 0; i < numFiles; i++ ) {
		names[i] = fileName;
		fileName += strlen( fileName ) + 1;
	}
	qsort( names, numFiles, sizeof( names[0] ), EntityDef_CompareFileNames );

	for ( i = 0; i < numFiles; i++ ) {
		Com_sprintf( path, sizeof( path ), "def/%s", names[i] );
		length = ii.FS_ReadFile( path, &text );
		if ( length <= 0 || !text ) {
			ii.FS_FreeFile( text );
			continue;
		}
		( (char *)text )[length] = '\0';
		EntityDef_ParseFile( path, (char *)text );
		ii.FS_FreeFile( text );
	}
}
