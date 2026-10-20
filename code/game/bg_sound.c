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
// bg_sound.c -- sound shaders, Doom 3's: sound/*.sndshd hold blocks of
//
//	<name> {						(or "sound <name> {")
//		minDistance 1				parameters (Doom 3's names)
//		maxDistance 15
//		volume 0.85
//		no_dups						don't play the same file twice in a row
//		sounds/hiro/pain1.wav		the files, one played at random
//		sounds/hiro/pain2.wav
//	}
//
// and a def's "snd_*" key names a shader, as in Doom 3 ("snd_footstep"
// "dk_footstep"). The parameters are kept for when the sound system takes
// them; it plays the files at its own volume and distances for now.

#ifdef CGAME
#include "../cgame/cg_local.h"
#else
#include "g_local.h"
#endif

#define MAX_SOUND_SHADER_FILES_LIST		8192		// bytes of sound/ file names

bgSoundShader_t	bg_soundShaders[MAX_SOUND_SHADERS];
int				bg_numSoundShaders;

/*
================
BG_FindSoundShader

NULL for none.
================
*/
const bgSoundShader_t *BG_FindSoundShader( const char *name ) {
	int i;

	if ( !name || !name[0] ) {
		return NULL;
	}
	for ( i = 0; i < bg_numSoundShaders; i++ ) {
		if ( !Q_stricmp( bg_soundShaders[i].name, name ) ) {
			return &bg_soundShaders[i];
		}
	}
	return NULL;
}

/*
================
BG_ParseSoundShaderFile
================
*/
static void BG_ParseSoundShaderFile( const char *fileName, char *text ) {
	char *p = text, *token;
	bgSoundShader_t *shader;
	char name[MAX_QPATH];

	COM_BeginParseSession( fileName );

	while ( 1 ) {
		token = COM_ParseExt( &p, qtrue );
		if ( !token[0] ) {
			break;
		}
		if ( !Q_stricmp( token, "sound" ) ) {
			token = COM_ParseExt( &p, qtrue );
		}
		Q_strncpyz( name, token, sizeof( name ) );
		token = COM_ParseExt( &p, qtrue );
		if ( strcmp( token, "{" ) ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: %s, line %d: no { after sound shader %s\n", fileName, COM_GetCurrentParseLine(), name );
			return;
		}

		// a later one of the name replaces the earlier
		shader = (bgSoundShader_t *)BG_FindSoundShader( name );
		if ( !shader ) {
			if ( bg_numSoundShaders == MAX_SOUND_SHADERS ) {
				Com_Printf( S_COLOR_YELLOW "WARNING: more than %d sound shaders, %s left out\n", MAX_SOUND_SHADERS, name );
				shader = NULL;
			} else {
				shader = &bg_soundShaders[bg_numSoundShaders++];
			}
		}
		if ( shader ) {
			memset( shader, 0, sizeof( *shader ) );
			Q_strncpyz( shader->name, name, sizeof( shader->name ) );
			// Doom 3's defaults
			shader->minDistance = 1;
			shader->maxDistance = 10;
			shader->volume = 1;
		}

		while ( 1 ) {
			token = COM_ParseExt( &p, qtrue );
			if ( !token[0] ) {
				Com_Printf( S_COLOR_YELLOW "WARNING: %s: sound shader %s isn't closed\n", fileName, name );
				return;
			}
			if ( !strcmp( token, "}" ) ) {
				break;
			}
			if ( !shader ) {
				continue;
			}
			if ( !Q_stricmp( token, "minDistance" ) ) {
				shader->minDistance = atof( COM_ParseExt( &p, qfalse ) );
			} else if ( !Q_stricmp( token, "maxDistance" ) ) {
				shader->maxDistance = atof( COM_ParseExt( &p, qfalse ) );
			} else if ( !Q_stricmp( token, "volume" ) ) {
				shader->volume = atof( COM_ParseExt( &p, qfalse ) );
			} else if ( !Q_stricmp( token, "no_dups" ) ) {
				shader->flags |= SSF_NO_DUPS;
			} else if ( !Q_stricmp( token, "looping" ) ) {
				shader->flags |= SSF_LOOPING;
			} else if ( !Q_stricmp( token, "global" ) ) {
				shader->flags |= SSF_GLOBAL;
			} else if ( !Q_stricmp( token, "private" ) ) {
				shader->flags |= SSF_PRIVATE;
			} else if ( !Q_stricmp( token, "shakes" ) || !Q_stricmp( token, "leadinVolume" )
						|| !Q_stricmp( token, "description" ) ) {
				COM_ParseExt( &p, qfalse );		// not used
			} else if ( strchr( token, '/' ) || strchr( token, '.' ) ) {
				if ( shader->numFiles < MAX_SOUND_SHADER_FILES ) {
					Q_strncpyz( shader->files[shader->numFiles++], token, MAX_QPATH );
				}
			}
			// anything else is a parameter Doom 3 has and we don't need
		}
	}
}

/*
================
BG_InitSoundShaders

Every sound/*.sndshd, in name order.
================
*/
static int QDECL BG_CompareSoundFileNames( const void *a, const void *b ) {
	return Q_stricmp( *(const char **)a, *(const char **)b );
}

void BG_InitSoundShaders( void ) {
	char fileList[MAX_SOUND_SHADER_FILES_LIST], *fileName, *names[256];
	char path[MAX_QPATH];
	int numFiles, i;
	long length;
	void *text;

	bg_numSoundShaders = 0;
	numFiles = trap_FS_GetFileList( "sound", ".sndshd", fileList, sizeof( fileList ) );
	numFiles = MIN( numFiles, (int)ARRAY_LEN( names ) );
	fileName = fileList;
	for ( i = 0; i < numFiles; i++ ) {
		names[i] = fileName;
		fileName += strlen( fileName ) + 1;
	}
	qsort( names, numFiles, sizeof( names[0] ), BG_CompareSoundFileNames );

	for ( i = 0; i < numFiles; i++ ) {
		Com_sprintf( path, sizeof( path ), "sound/%s", names[i] );
		length = BG_FS_ReadFile( path, &text );
		if ( length <= 0 || !text ) {
			BG_FS_FreeFile( text );
			continue;
		}
		( (char *)text )[length] = '\0';
		BG_ParseSoundShaderFile( path, (char *)text );
		BG_FS_FreeFile( text );
	}
}

/*
================
BG_SoundShaderFile

A file of the shader to play, at random; with no_dups never the one played
last. NULL for none.
================
*/
const char *BG_SoundShaderFile( const bgSoundShader_t *shader ) {
	bgSoundShader_t *s = (bgSoundShader_t *)shader;
	int pick;

	if ( !s || s->numFiles <= 0 ) {
		return NULL;
	}
	pick = rand() % s->numFiles;
	if ( ( s->flags & SSF_NO_DUPS ) && s->numFiles > 1 && pick == s->lastFile - 1 ) {
		pick = ( pick + 1 + rand() % ( s->numFiles - 1 ) ) % s->numFiles;
	}
	s->lastFile = pick + 1;
	return s->files[pick];
}
