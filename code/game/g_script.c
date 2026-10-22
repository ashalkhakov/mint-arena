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
// g_script.c -- the game's use of the scripts, as Doom 3's game uses them:
// the game's script (script/dk_main.script and what it includes) compiled as
// the game starts, dk_main() run as each map starts, the map's own
// (maps/<map>.script) compiled by worldspawn and its main() run; $name for an
// entity of that targetname; an entity's "scriptobject"

#include "g_local.h"

// the entities' spawn args (Doom 3's idEntity::spawnArgs), for getKey()...
static dict_t	*entitySpawnArgs[ MAX_GENTITIES ];

static int		consoleFunctionCount;
static qboolean	shuttingDown;			// no destroy() as the game shuts down

/*
================
G_ScriptFindEntity

$name: the entity of that targetname; player1... the players; world
================
*/
gentity_t *G_ScriptFindEntity( const char *name ) {
	gentity_t	*ent;
	int			i;

	if ( !Q_stricmp( name, "world" ) ) {
		return &g_entities[ ENTITYNUM_WORLD ];
	}

	if ( !Q_stricmpn( name, "player", 6 ) && name[ 6 ] >= '1' && name[ 6 ] <= '9' ) {
		i = atoi( name + 6 ) - 1;
		if ( i >= 0 && i < level.maxplayers && g_entities[ i ].inuse && g_entities[ i ].player ) {
			return &g_entities[ i ];
		}
		return NULL;
	}

	for ( i = 0, ent = g_entities; i < level.num_entities; i++, ent++ ) {
		if ( ent->inuse && ent->targetname && !Q_stricmp( ent->targetname, name ) ) {
			return ent;
		}
	}
	return NULL;
}

/*
================
G_ScriptPlayer

the player the scripts' events act for (Doom 3's local player)
================
*/
gentity_t *G_ScriptPlayer( void ) {
	int i;

	for ( i = 0; i < level.maxplayers; i++ ) {
		if ( g_entities[ i ].inuse && g_entities[ i ].player && level.players[ i ].pers.connected == CON_CONNECTED ) {
			return &g_entities[ i ];
		}
	}
	return NULL;
}

/*
================
G_ScriptActivate

EV_Activate: its use, as a trigger of it would
================
*/
void G_ScriptActivate( gentity_t *ent, gentity_t *activator ) {
	if ( ent->use ) {
		ent->use( ent, activator, activator );
	}
}

/*
================
G_ScriptSetCallback

EV_Thread_SetCallback: whether sys.waitFor( ent ) waits for it: a mover on
its way (Reached_BinaryMover lets the thread go on), or a camera playing
(G_CameraStop)
================
*/
qboolean G_ScriptSetCallback( gentity_t *ent ) {
	if ( ent->cameraPlaying ) {
		return qtrue;
	}
	if ( ent->s.eType == ET_MOVER && ( ent->moverState == MOVER_1TO2 || ent->moverState == MOVER_2TO1 ) ) {
		return qtrue;
	}
	return qfalse;
}

/*
================
G_ScriptGetSpawnArg / SetSpawnArg
================
*/
const char *G_ScriptGetSpawnArg( gentity_t *ent, const char *key, const char *defaultValue ) {
	const char	*value;
	dict_t		*args;

	args = entitySpawnArgs[ ent - g_entities ];
	if ( args && DictGetString( args, key, NULL, &value ) && value ) {
		return value;
	}
	if ( ent->item && BG_ItemDef( ent->item ) && DictGetString( BG_ItemDef( ent->item ), key, NULL, &value ) && value ) {
		return value;
	}
	// its def's, as Doom 3's spawn args hold them
	if ( ent->classname && EntityDef_Find( ent->classname ) && DictGetString( EntityDef_Find( ent->classname ), key, NULL, &value ) && value ) {
		return value;
	}
	return defaultValue;
}

// all of them (the map's keys), NULL for none
const dict_t *G_ScriptSpawnArgs( gentity_t *ent ) {
	return entitySpawnArgs[ ent - g_entities ];
}

void G_ScriptSetSpawnArg( gentity_t *ent, const char *key, const char *value ) {
	dict_t	**args;

	args = &entitySpawnArgs[ ent - g_entities ];
	if ( !*args ) {
		*args = Script_Alloc( sizeof( dict_t ) );
		DictInit( *args );
	}
	DictSet( *args, key, value );
}

/*
================
G_ScriptConstructScriptObject

Doom 3's idEntity::ConstructScriptObject: a thread for its init(), run once
the entity is spawned
================
*/
static void G_ScriptConstructScriptObject( gentity_t *ent ) {
	const function_t	*constructor;
	scriptThread_t		*thread;

	// init the script object's data
	ScriptObject_ClearObject( &ent->scriptObject );

	// call script object's constructor
	constructor = ScriptObject_GetConstructor( &ent->scriptObject );
	if ( constructor ) {
		// start a thread that will initialize after Spawn is done being called
		thread = Thread_New();
		Thread_SetThreadName( thread, ent->targetname ? ent->targetname : ent->classname );
		Thread_CallObjectFunction( thread, ent, constructor, qtrue );
		Thread_DelayedStart( thread, 0 );
	}
}

/*
================
G_ScriptEntitySpawned

an entity the map or a script spawned: its spawn args, $name, its script object
================
*/
void G_ScriptEntitySpawned( gentity_t *ent ) {
	const char	*scriptObjectName;
	int			i;

	for ( i = 0; i < level.numSpawnVars; i++ ) {
		G_ScriptSetSpawnArg( ent, level.spawnVars[ i ][ 0 ], level.spawnVars[ i ][ 1 ] );
	}

	// Daikatana's "uniqueid" names it for its action scripts (g_aiscript.c)
	if ( !ent->targetname && G_ScriptGetSpawnArg( ent, "uniqueid", NULL ) ) {
		ent->targetname = G_NewString( G_ScriptGetSpawnArg( ent, "uniqueid", "" ) );
	}
	if ( ent->targetname ) {
		Program_SetEntity( ent->targetname, ent );
	}

	scriptObjectName = G_ScriptGetSpawnArg( ent, "scriptobject", NULL );
	if ( scriptObjectName && scriptObjectName[ 0 ] ) {
		if ( !ScriptObject_SetType( &ent->scriptObject, scriptObjectName ) ) {
			G_Error( "Script object '%s' not found on entity '%s'.", scriptObjectName, ent->targetname ? ent->targetname : ent->classname );
		}
		G_ScriptConstructScriptObject( ent );
	}
}

/*
================
G_ScriptPlayerSpawned

$player1...
================
*/
void G_ScriptPlayerSpawned( gentity_t *ent ) {
	Program_SetEntity( va( "player%d", (int)( ent - g_entities ) + 1 ), ent );
}

/*
================
G_ScriptEntityFreed

Doom 3's idEntity::DeconstructScriptObject: its destroy() runs at once
================
*/
void G_ScriptEntityFreed( gentity_t *ent ) {
	const function_t	*destructor;
	scriptThread_t		*thread;
	dict_t				**args;

	if ( ScriptObject_HasObject( &ent->scriptObject ) && !shuttingDown ) {
		destructor = ScriptObject_GetDestructor( &ent->scriptObject );
		if ( destructor ) {
			// start a thread that will run immediately and be destroyed
			thread = Thread_NewObjectFunction( ent, destructor );
			Thread_Execute( thread );
			thread->removed = qtrue;
		}
	}
	ScriptObject_Free( &ent->scriptObject );

	args = &entitySpawnArgs[ ent - g_entities ];
	if ( *args ) {
		DictFree( *args );
		Script_Free( *args );
		*args = NULL;
	}
}

/*
================
G_ScriptCallFunction

a function of the scripts, in a thread of its own from the next frame on
(Doom 3's idTrigger::CallScript); its name may be namespace::function
================
*/
void G_ScriptCallFunction( const char *funcname, gentity_t *activator ) {
	const function_t	*func;
	scriptThread_t		*thread;

	func = Program_FindFunction( funcname );
	if ( !func ) {
		G_Printf( S_COLOR_YELLOW "WARNING: no script function '%s'\n", funcname );
		return;
	}
	thread = Thread_NewFunction( func );
	Thread_DelayedStart( thread, 0 );
}

/*
================
G_ScriptCallObjectFunction

a method of the entity's script object, as a thread of its own that runs at
once: funcname( self ), or funcname( self, other ) when it takes an entity
too. What it returns is in result, if it returned it without waiting
(else result is left as it is). qfalse if it has none.
================
*/
qboolean G_ScriptCallObjectFunction( gentity_t *ent, const char *funcname, gentity_t *other, float *result ) {
	const function_t	*func;
	scriptThread_t		*thread;
	etype_t				returnType;

	func = ScriptObject_GetFunction( &ent->scriptObject, funcname );
	if ( !func ) {
		return qfalse;
	}

	thread = Thread_New();
	Thread_SetThreadName( thread, va( "%s::%s", ScriptObject_GetTypeName( &ent->scriptObject ), funcname ) );
	if ( func->type->numParms == 2 && func->type->parmTypes[ 1 ] == &type_entity ) {
		Interp_Reset( &thread->interpreter );
		Interp_PushEntity( &thread->interpreter, ent );
		Interp_PushEntity( &thread->interpreter, other );
		Interp_EnterFunction( &thread->interpreter, func, qfalse );
	} else if ( func->type->numParms == 1 ) {
		Thread_CallObjectFunction( thread, ent, func, qtrue );
	} else {
		G_Printf( S_COLOR_YELLOW "WARNING: %s's %s() takes what the game can't give it\n", ScriptObject_GetTypeName( &ent->scriptObject ), funcname );
		Thread_Delete( thread );
		return qfalse;
	}

	if ( Thread_Start( thread ) ) {
		thread->removed = qtrue;
		if ( result ) {
			returnType = Type_ReturnType( func->type )->type;
			if ( returnType == ev_float ) {
				*result = *program_returnDef->value.floatPtr;
			} else if ( returnType == ev_boolean ) {
				*result = *program_returnDef->value.intPtr;
			}
		}
	}
	return qtrue;
}

/*
================
G_ScriptInit

as the game starts: the game's script, and its dk_main() (Doom 3's
idProgram::Startup and idGameLocal::InitScriptForMap)
================
*/
void G_ScriptInit( const char *mapname ) {
	const function_t	*func;
	scriptThread_t		*thread;
	fileHandle_t		f;

	Event_Init();
	consoleFunctionCount = 0;
	shuttingDown = qfalse;
	ScriptDebug_Init();

	if ( trap_FS_FOpenFile( SCRIPT_DEFAULT, &f, FS_READ ) >= 0 && f ) {
		trap_FS_FCloseFile( f );
		Program_Startup( SCRIPT_DEFAULT );
	} else {
		G_DPrintf( "no %s\n", SCRIPT_DEFAULT );
		Program_Startup( NULL );
	}

	// run the main game script function (not the level specific main)
	func = Program_FindFunction( SCRIPT_DEFAULTFUNC );
	if ( func != NULL ) {
		thread = Thread_NewFunction( func );
		if ( Thread_Start( thread ) ) {
			// thread has finished executing, so delete it
			thread->removed = qtrue;
		}
	}
}

/*
================
G_ScriptWorldSpawn

Doom 3's idWorldspawn::Spawn: the map's script and its main(), and the
functions its "call" keys name
================
*/
void G_ScriptWorldSpawn( void ) {
	char				mapname[ MAX_QPATH ];
	const char			*scriptname;
	const function_t	*func;
	scriptThread_t		*thread;
	fileHandle_t		f;
	int					i;

	trap_Cvar_VariableStringBuffer( "mapname", mapname, sizeof( mapname ) );
	scriptname = va( "maps/%s.script", mapname );

	// load script
	if ( trap_FS_FOpenFile( scriptname, &f, FS_READ ) > 0 && f ) {
		trap_FS_FCloseFile( f );
		Program_CompileFile( scriptname );
	} else if ( f ) {
		trap_FS_FCloseFile( f );
	}

	// and the cinematics it plays (./dk assembles cine/maps/<map>.script: all
	// of them at once would be more than the scripts' memory)
	scriptname = va( "cine/maps/%s.script", mapname );
	f = 0;
	if ( trap_FS_FOpenFile( scriptname, &f, FS_READ ) > 0 && f ) {
		trap_FS_FCloseFile( f );
		Program_CompileFile( scriptname );
	} else if ( f ) {
		trap_FS_FCloseFile( f );
	}

	// and Daikatana's action scripts (./dk converts its cin/aiscripts/<map>.sca)
	scriptname = va( "ai/maps/%s.script", mapname );
	f = 0;
	if ( trap_FS_FOpenFile( scriptname, &f, FS_READ ) > 0 && f ) {
		trap_FS_FCloseFile( f );
		Program_CompileFile( scriptname );
	} else if ( f ) {
		trap_FS_FCloseFile( f );
	}

	// call the main function by default
	func = Program_FindFunction( "main" );
	if ( func != NULL ) {
		thread = Thread_NewFunction( func );
		Thread_DelayedStart( thread, 0 );
	}

	// Daikatana's map intro: its cinematic (cine/<name>.script's <name>::play),
	// in single player
	if ( g_gametype.integer == GT_CAMPAIGN ) {
		char *intro;

		G_SpawnString( "cinematic_intro", "", &intro );
		if ( intro[0] ) {
			func = Program_FindFunction( va( "%s::play", intro ) );
			if ( func ) {
				thread = Thread_NewFunction( func );
				Thread_DelayedStart( thread, 0 );
			} else {
				G_Printf( S_COLOR_YELLOW "no cinematic %s (./dk cine converts them)\n", intro );
			}
		}
	}

	// call any functions specified in worldspawn
	for ( i = 0; i < level.numSpawnVars; i++ ) {
		if ( Q_stricmpn( level.spawnVars[ i ][ 0 ], "call", 4 ) ) {
			continue;
		}
		func = Program_FindFunction( level.spawnVars[ i ][ 1 ] );
		if ( func == NULL ) {
			G_Error( "Function '%s' not found in script for '%s' key on worldspawn", level.spawnVars[ i ][ 1 ], level.spawnVars[ i ][ 0 ] );
		}
		thread = Thread_NewFunction( func );
		Thread_DelayedStart( thread, 0 );
	}
}

/*
================
G_ScriptFrame

the threads whose time has come, as the frame starts
================
*/
void G_ScriptFrame( void ) {
	ScriptDebug_Frame();
	Thread_RunThreads();
}

/*
================
G_ScriptShutdown
================
*/
void G_ScriptShutdown( void ) {
	int i;

	shuttingDown = qtrue;
	ScriptDebug_Shutdown();
	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		ScriptObject_Free( &g_entities[ i ].scriptObject );
		if ( entitySpawnArgs[ i ] ) {
			DictFree( entitySpawnArgs[ i ] );
			Script_Free( entitySpawnArgs[ i ] );
			entitySpawnArgs[ i ] = NULL;
		}
	}
	Thread_Restart();
	Program_FreeData();
}

/*
================
G_Script_f

the console's "script <statements>": compiled into a function of its own
and run at once (Doom 3's Cmd_Script_f)
================
*/
void G_Script_f( void ) {
	char				script[ MAX_STRING_CHARS ];
	char				text[ MAX_STRING_CHARS + 64 ];
	char				funcname[ 64 ];
	const char			*s;
	const function_t	*func;
	scriptThread_t		*thread;

	// what follows the command, quotes and all
	trap_LiteralArgs( script, sizeof( script ) );
	for ( s = script; *s && *s != ' ' && *s != '\t'; s++ ) {
	}
	while ( *s == ' ' || *s == '\t' ) {
		s++;
	}

	Com_sprintf( funcname, sizeof( funcname ), "ConsoleFunction_%d", consoleFunctionCount++ );

	Com_sprintf( text, sizeof( text ), "void %s() {%s;}\n", funcname, s );
	if ( Program_CompileText( "console", text, qtrue ) ) {
		func = Program_FindFunction( funcname );
		if ( func ) {
			// set all the entity names in case the user named one in the script that wasn't referenced in the default script
			G_ScriptSetEntityNames();

			thread = Thread_NewFunction( func );
			Thread_SetThreadName( thread, funcname );
			if ( Thread_Start( thread ) ) {
				thread->removed = qtrue;
			}
		}
	}
}

/*
================
G_ScriptSetEntityNames

every entity's $name
================
*/
void G_ScriptSetEntityNames( void ) {
	gentity_t	*ent;
	int			i;

	for ( i = 0, ent = g_entities; i < level.num_entities; i++, ent++ ) {
		if ( !ent->inuse ) {
			continue;
		}
		if ( ent->targetname ) {
			Program_SetEntity( ent->targetname, ent );
		}
		if ( ent->player ) {
			G_ScriptPlayerSpawned( ent );
		}
	}
}

/*
================
G_ScriptCallTargets

as an entity uses its targets: the script function its "call" key names,
in a thread of its own (Doom 3's idTrigger::CallScript)
================
*/
void G_ScriptCallTargets( gentity_t *ent, gentity_t *activator ) {
	const char *call;

	call = G_ScriptGetSpawnArg( ent, "call", NULL );
	if ( call && call[ 0 ] ) {
		G_ScriptCallFunction( call, activator );
	}
}

/*
================
Use_CallObjectFunction
================
*/
void Use_CallObjectFunction( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	const function_t	*func;
	const char			*funcName;
	scriptThread_t		*thread;
	gentity_t			*t;

	funcName = G_ScriptGetSpawnArg( self, "call", "" );
	if ( !self->target ) {
		return;
	}
	t = NULL;
	while ( ( t = G_Find( t, FOFS( targetname ), self->target ) ) != NULL ) {
		if ( !ScriptObject_HasObject( &t->scriptObject ) ) {
			continue;
		}
		func = ScriptObject_GetFunction( &t->scriptObject, funcName );
		if ( !func ) {
			G_Error( "Function '%s' not found on entity '%s' for function call from '%s'", funcName,
				t->targetname ? t->targetname : t->classname, self->targetname ? self->targetname : self->classname );
		}
		if ( func->type->numParms != 1 ) {
			G_Error( "Function '%s' on entity '%s' has the wrong number of parameters for function call from '%s'", funcName,
				t->targetname ? t->targetname : t->classname, self->targetname ? self->targetname : self->classname );
		}
		if ( !Type_Inherits( t->scriptObject.type, func->type->parmTypes[ 0 ] ) ) {
			G_Error( "Function '%s' on entity '%s' is the wrong type for function call from '%s'", funcName,
				t->targetname ? t->targetname : t->classname, self->targetname ? self->targetname : self->classname );
		}
		// create a thread and call the function
		thread = Thread_New();
		Thread_CallObjectFunction( thread, t, func, qtrue );
		Thread_Start( thread );
		if ( thread->interpreter.threadDying ) {
			thread->removed = qtrue;
		}
	}
}

/*QUAKED target_callobjectfunction (1 0 0) (-8 -8 -8) (8 8 8)
Doom 3's: when used, the method its "call" key names of each of its targets'
script objects.
*/
void SP_target_callobjectfunction( gentity_t *self ) {
	self->use = Use_CallObjectFunction;
}

/*
================
G_ScriptLoading

as a saved game is loaded: the map's script objects it replaces get no destroy()
================
*/
void G_ScriptLoading( qboolean loading ) {
	shuttingDown = loading;
}

/*
================
G_ScriptWriteEntity

an entity's script object (its type's name and its fields) and spawn args
================
*/
void G_ScriptWriteEntity( const gentity_t *ent ) {
	const dict_t			*args;
	const dictKeyValue_t	*kv;
	int						i, n;

	if ( ScriptObject_HasObject( &ent->scriptObject ) ) {
		G_SaveString( ent->scriptObject.type->name );
		n = ent->scriptObject.type->size;
		G_SaveBytes( &n, sizeof( n ) );
		G_SaveBytes( ent->scriptObject.data, n );
	} else {
		G_SaveString( NULL );
	}

	args = entitySpawnArgs[ ent - g_entities ];
	n = args ? DictGetNumKeyVals( args ) : 0;
	G_SaveBytes( &n, sizeof( n ) );
	for ( i = 0; i < n; i++ ) {
		kv = DictGetKeyVal( args, i );
		G_SaveString( PoolStrGetString( kv->key ) );
		G_SaveString( PoolStrGetString( kv->value ) );
	}
}

/*
================
G_ScriptReadEntity

what G_ScriptWriteEntity wrote, for the entity of that number
================
*/
void G_ScriptReadEntity( gentity_t *ent, int entityNum ) {
	char	typeName[ MAX_STRING_CHARS ];
	char	key[ MAX_STRING_CHARS ], value[ MAX_STRING_CHARS ];
	dict_t	**args;
	byte	*data;
	int		i, n;

	memset( &ent->scriptObject, 0, sizeof( ent->scriptObject ) );
	if ( G_LoadStringBuf( typeName, sizeof( typeName ) ) ) {
		G_LoadBytes( &n, sizeof( n ) );
		if ( n < 0 || n > MAX_GLOBALS ) {
			G_SaveFailed();
			return;
		}
		data = Script_Alloc( MAX( n, 1 ) );
		G_LoadBytes( data, n );
		if ( !ScriptObject_SetType( &ent->scriptObject, typeName ) ) {
			G_Printf( S_COLOR_YELLOW "WARNING: no script object '%s' for entity %d\n", typeName, entityNum );
		} else if ( ent->scriptObject.type->size != n ) {
			G_Printf( S_COLOR_YELLOW "WARNING: script object '%s' of entity %d isn't the size it was saved with\n", typeName, entityNum );
		} else {
			memcpy( ent->scriptObject.data, data, n );
		}
		Script_Free( data );
	}

	args = &entitySpawnArgs[ entityNum ];
	if ( *args ) {
		DictFree( *args );
		Script_Free( *args );
		*args = NULL;
	}
	G_LoadBytes( &n, sizeof( n ) );
	if ( n < 0 || n > MAX_SPAWN_VARS * 4 ) {
		G_SaveFailed();
		return;
	}
	for ( i = 0; i < n; i++ ) {
		G_LoadStringBuf( key, sizeof( key ) );
		G_LoadStringBuf( value, sizeof( value ) );
		if ( !*args ) {
			*args = Script_Alloc( sizeof( dict_t ) );
			DictInit( *args );
		}
		DictSet( *args, key, value );
	}
}

/*
================
G_ScriptWriteGame

the scripts' variables and threads
================
*/
void G_ScriptWriteGame( void ) {
	Program_Save();
	Thread_SaveAll();
}

/*
================
G_ScriptReadGame

the scripts' variables and threads as they were saved, instead of those the
map started with; when the scripts changed since, those the map started with
================
*/
void G_ScriptReadGame( void ) {
	if ( !Program_Restore() ) {
		return;
	}
	Thread_RestoreAll();
}

/*
================
G_ScriptListSpawnArgs

an entity's spawn args, for the script debugger (their keys stay the dict's)
================
*/
int G_ScriptListSpawnArgs( gentity_t *ent, const char **keys, char values[][ MAX_STRING_LEN + 16 ], int max, int valueSize ) {
	const dict_t			*args;
	const dictKeyValue_t	*kv;
	int						i, n;

	args = entitySpawnArgs[ ent - g_entities ];
	n = args ? DictGetNumKeyVals( args ) : 0;
	for ( i = 0; i < n && i < max; i++ ) {
		kv = DictGetKeyVal( args, i );
		keys[ i ] = PoolStrGetString( kv->key );
		Q_strncpyz( values[ i ], PoolStrGetString( kv->value ), valueSize );
	}
	return i;
}
