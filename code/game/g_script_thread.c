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
// g_script_thread.c -- Doom 3's Script_Thread.cpp: the scripts' threads, and
// the events of sys (sys.wait( 1 )...)
//
// Doom 3 runs a thread by posting it an EV_Thread_Execute event; here a thread
// has the time it runs next, and Thread_RunThreads runs those whose time has
// come at the start of each frame, as idEvent::ServiceEvents does.

#include "g_local.h"

static scriptThread_t	*currentThread = NULL;
static int				threadIndex = 0;
static scriptThread_t	**threadList;
static int				numThreads, maxThreads;
static trace_t			trace;
static int				executeSequence;		// the order threads were posted in, for the same time

/*
================
Thread_CurrentThread / CurrentThreadNum
================
*/
scriptThread_t *Thread_CurrentThread( void ) {
	return currentThread;
}

int Thread_CurrentThreadNum( void ) {
	if ( currentThread ) {
		return currentThread->threadNum;
	} else {
		return 0;
	}
}

/*
================
Thread_BeginMultiFrameEvent / EndMultiFrameEvent
================
*/
qboolean Thread_BeginMultiFrameEvent( gentity_t *ent, const eventDef_t *event ) {
	if ( !currentThread ) {
		G_Error( "Thread_BeginMultiFrameEvent called without a current thread" );
	}
	return Interp_BeginMultiFrameEvent( &currentThread->interpreter, ent, event );
}

void Thread_EndMultiFrameEvent( gentity_t *ent, const eventDef_t *event ) {
	if ( !currentThread ) {
		G_Error( "Thread_EndMultiFrameEvent called without a current thread" );
	}
	Interp_EndMultiFrameEvent( &currentThread->interpreter, ent, event );
}

/*
================
Thread_Init
================
*/
static void Thread_Init( scriptThread_t *thread ) {
	// create a unique threadNum
	do {
		threadIndex++;
		if ( threadIndex == 0 ) {
			threadIndex = 1;
		}
	} while( Thread_GetThread( threadIndex ) );

	thread->threadNum = threadIndex;
	if ( numThreads >= maxThreads ) {
		scriptThread_t **newList;

		maxThreads = maxThreads ? maxThreads * 2 : 64;
		newList = Script_Alloc( maxThreads * sizeof( newList[0] ) );
		if ( threadList ) {
			memcpy( newList, threadList, numThreads * sizeof( newList[0] ) );
			Script_Free( threadList );
		}
		threadList = newList;
	}
	threadList[ numThreads++ ] = thread;

	thread->creationTime = level.time;
	thread->lastExecuteTime = 0;
	thread->manualControl = qfalse;
	thread->executeTime = -1;
	thread->removed = qfalse;

	Thread_ClearWaitFor( thread );

	Interp_Init( &thread->interpreter );
	thread->interpreter.thread = thread;
}

/*
================
Thread_New
================
*/
scriptThread_t *Thread_New( void ) {
	scriptThread_t *thread;

	thread = Script_Alloc( sizeof( *thread ) );
	Thread_Init( thread );
	Thread_SetThreadName( thread, va( "thread_%d", threadIndex ) );
	if ( g_debugScript.integer ) {
		G_Printf( "%d: create thread (%d) '%s'\n", level.time, thread->threadNum, thread->threadName );
	}
	return thread;
}

/*
================
Thread_NewObjectFunction
================
*/
scriptThread_t *Thread_NewObjectFunction( gentity_t *self, const function_t *func ) {
	scriptThread_t *thread;

	assert( self );

	thread = Script_Alloc( sizeof( *thread ) );
	Thread_Init( thread );
	Thread_SetThreadName( thread, self->targetname ? self->targetname : self->classname );
	Interp_EnterObjectFunction( &thread->interpreter, self, func, qfalse );
	if ( g_debugScript.integer ) {
		G_Printf( "%d: create thread (%d) '%s'\n", level.time, thread->threadNum, thread->threadName );
	}
	return thread;
}

/*
================
Thread_NewFunction
================
*/
scriptThread_t *Thread_NewFunction( const function_t *func ) {
	scriptThread_t *thread;

	assert( func );

	thread = Script_Alloc( sizeof( *thread ) );
	Thread_Init( thread );
	Thread_SetThreadName( thread, func->name );
	Interp_EnterFunction( &thread->interpreter, func, qfalse );
	if ( g_debugScript.integer ) {
		G_Printf( "%d: create thread (%d) '%s'\n", level.time, thread->threadNum, thread->threadName );
	}
	return thread;
}

/*
================
Thread_ThreadCallback
================
*/
static void Thread_ThreadCallback( scriptThread_t *thread, scriptThread_t *ended ) {
	if ( thread->interpreter.threadDying ) {
		return;
	}

	if ( ended == thread->waitingForThread ) {
		Thread_ClearWaitFor( thread );
		Thread_DelayedStart( thread, 0 );
	}
}

/*
================
Thread_Delete

idThread's destructor
================
*/
void Thread_Delete( scriptThread_t *thread ) {
	int i;

	if ( g_debugScript.integer ) {
		G_Printf( "%d: end thread (%d) '%s'\n", level.time, thread->threadNum, thread->threadName );
	}

	for ( i = 0; i < numThreads; i++ ) {
		if ( threadList[ i ] == thread ) {
			memmove( &threadList[ i ], &threadList[ i + 1 ], ( numThreads - i - 1 ) * sizeof( threadList[0] ) );
			numThreads--;
			break;
		}
	}

	for( i = 0; i < numThreads; i++ ) {
		if ( threadList[ i ]->waitingForThread == thread ) {
			Thread_ThreadCallback( threadList[ i ], thread );
		}
	}

	if ( currentThread == thread ) {
		currentThread = NULL;
	}

	Script_Free( thread );
}

/*
================
Thread_ManualDelete

tells the thread manager not to delete this thread when it ends
================
*/
void Thread_ManualDelete( scriptThread_t *thread ) {
	thread->interpreter.terminateOnExit = qfalse;
}

/*
================
Thread_GetThread
================
*/
scriptThread_t *Thread_GetThread( int num ) {
	int i;

	for( i = 0; i < numThreads; i++ ) {
		if ( threadList[ i ]->threadNum == num ) {
			return threadList[ i ];
		}
	}

	return NULL;
}

int Thread_NumThreads( void ) {
	return numThreads;
}

scriptThread_t *Thread_GetThreadByIndex( int index ) {
	return threadList[ index ];
}

/*
================
Thread_DisplayInfo
================
*/
void Thread_DisplayInfo( scriptThread_t *thread ) {
	G_Printf(
		"%12i: '%s'\n"
		"        File: %s(%d)\n"
		"     Created: %d (%d ms ago)\n"
		"      Status: ",
		thread->threadNum, thread->threadName,
		Interp_CurrentFile( &thread->interpreter ), Interp_CurrentLine( &thread->interpreter ),
		thread->creationTime, level.time - thread->creationTime );

	if ( thread->interpreter.threadDying ) {
		G_Printf( "Dying\n" );
	} else if ( thread->interpreter.doneProcessing ) {
		G_Printf(
			"Paused since %d (%d ms)\n"
			"      Reason: ",  thread->lastExecuteTime, level.time - thread->lastExecuteTime );
		if ( thread->waitingForThread ) {
			G_Printf( "Waiting for thread #%3i '%s'\n", thread->waitingForThread->threadNum, thread->waitingForThread->threadName );
		} else if ( ( thread->waitingFor != ENTITYNUM_NONE ) && ( g_entities[ thread->waitingFor ].inuse ) ) {
			G_Printf( "Waiting for entity #%3i '%s'\n", thread->waitingFor, g_entities[ thread->waitingFor ].targetname ? g_entities[ thread->waitingFor ].targetname : g_entities[ thread->waitingFor ].classname );
		} else if ( thread->waitingUntil ) {
			G_Printf( "Waiting until %d (%d ms total wait time)\n", thread->waitingUntil, thread->waitingUntil - thread->lastExecuteTime );
		} else {
			G_Printf( "None\n" );
		}
	} else {
		G_Printf( "Processing\n" );
	}

	Interp_DisplayInfo( &thread->interpreter );

	G_Printf( "\n" );
}

/*
================
Thread_ListThreads_f
================
*/
void Thread_ListThreads_f( void ) {
	int	i;

	for( i = 0; i < numThreads; i++ ) {
		G_Printf( "%3i: %-20s : %s(%d)\n", threadList[ i ]->threadNum, threadList[ i ]->threadName, Interp_CurrentFile( &threadList[ i ]->interpreter ), Interp_CurrentLine( &threadList[ i ]->interpreter ) );
	}
	G_Printf( "%d active threads\n\n", numThreads );
}

/*
================
Thread_Restart
================
*/
void Thread_Restart( void ) {
	int	i;

	// reset the threadIndex
	threadIndex = 0;

	currentThread = NULL;
	for( i = numThreads - 1; i >= 0; i-- ) {
		Script_Free( threadList[ i ] );
	}
	Script_Free( threadList );
	threadList = NULL;
	numThreads = maxThreads = 0;

	memset( &trace, 0, sizeof( trace ) );
	trace.entityNum = ENTITYNUM_NONE;
}

/*
================
Thread_DelayedStart
================
*/
void Thread_DelayedStart( scriptThread_t *thread, int delay ) {
	if ( level.time <= 0 ) {
		delay++;
	}
	thread->executeTime = level.time + delay;
	thread->executeTime = MAX( thread->executeTime, 0 );
	executeSequence++;
}

/*
================
Thread_Start
================
*/
qboolean Thread_Start( scriptThread_t *thread ) {
	thread->executeTime = -1;
	return Thread_Execute( thread );
}

/*
================
Thread_SetThreadName
================
*/
void Thread_SetThreadName( scriptThread_t *thread, const char *name ) {
	Q_strncpyz( thread->threadName, name, sizeof( thread->threadName ) );
}

/*
================
Thread_ObjectMoveDone
================
*/
void Thread_ObjectMoveDone( int threadnum, gentity_t *obj ) {
	scriptThread_t *thread;

	if ( !threadnum ) {
		return;
	}

	thread = Thread_GetThread( threadnum );
	if ( thread && thread->waitingFor == obj - g_entities ) {
		Thread_ClearWaitFor( thread );
		Thread_DelayedStart( thread, 0 );
	}
}

/*
================
Thread_EntityMoveDone

any thread waiting for the entity
================
*/
void Thread_EntityMoveDone( gentity_t *obj ) {
	int i;

	for ( i = 0; i < numThreads; i++ ) {
		if ( threadList[ i ]->waitingFor == obj - g_entities ) {
			Thread_ClearWaitFor( threadList[ i ] );
			Thread_DelayedStart( threadList[ i ], 0 );
		}
	}
}

/*
================
Thread_End
================
*/
void Thread_End( scriptThread_t *thread ) {
	// Tell thread to die.  It will exit on its own.
	Thread_Pause( thread );
	thread->interpreter.threadDying	= qtrue;
}

/*
================
Thread_KillThreadByName
================
*/
void Thread_KillThreadByName( const char *name ) {
	int			i;
	int			len;
	const char	*ptr;

	// see if the name uses a wild card
	ptr = strchr( name, '*' );
	if ( ptr ) {
		len = ptr - name;
	} else {
		len = strlen( name );
	}

	// kill only those threads whose name matches name
	for( i = 0; i < numThreads; i++ ) {
		if ( !Q_strncmp( threadList[ i ]->threadName, name, len ) ) {
			Thread_End( threadList[ i ] );
		}
	}
}

/*
================
Thread_KillThread
================
*/
void Thread_KillThread( int num ) {
	scriptThread_t *thread;

	thread = Thread_GetThread( num );
	if ( thread ) {
		// Tell thread to die.  It will delete itself on it's own.
		Thread_End( thread );
	}
}

/*
================
Thread_Execute
================
*/
qboolean Thread_Execute( scriptThread_t *thread ) {
	scriptThread_t	*oldThread;
	qboolean		done;

	if ( thread->manualControl && ( thread->waitingUntil > level.time ) ) {
		return qfalse;
	}

	oldThread = currentThread;
	currentThread = thread;

	thread->lastExecuteTime = level.time;
	Thread_ClearWaitFor( thread );
	done = Interp_Execute( &thread->interpreter );
	if ( done ) {
		Thread_End( thread );
		if ( thread->interpreter.terminateOnExit ) {
			// PostEventMS( &EV_Remove, 0 )
			thread->removed = qtrue;
		}
	} else if ( !thread->manualControl ) {
		if ( thread->waitingUntil > thread->lastExecuteTime ) {
			thread->executeTime = thread->waitingUntil;
			executeSequence++;
		} else if ( Interp_MultiFrameEventInProgress( &thread->interpreter ) ) {
			thread->executeTime = level.time + MAX( level.time - level.previousTime, 1 );
			executeSequence++;
		}
	}

	currentThread = oldThread;

	return done;
}

/*
================
Thread_RunThreads

the threads whose time has come, those they start and those that wait for
them, then those that ended are deleted
================
*/
void Thread_RunThreads( void ) {
	scriptThread_t	*thread;
	int				i, best;
	int				runs;

	for ( runs = 0; ; runs++ ) {
		// the first due
		best = -1;
		for ( i = 0; i < numThreads; i++ ) {
			thread = threadList[ i ];
			if ( thread->removed || thread->executeTime < 0 || thread->executeTime > level.time ) {
				continue;
			}
			if ( best < 0 || thread->executeTime < threadList[ best ]->executeTime ) {
				best = i;
			}
		}

		// those that ended
		for ( i = numThreads - 1; i >= 0; i-- ) {
			if ( threadList[ i ]->removed ) {
				Thread_Delete( threadList[ i ] );
				if ( best > i ) {
					best--;
				} else if ( best == i ) {
					best = -1;
				}
				i = MIN( i, numThreads );
			}
		}

		if ( best < 0 ) {
			// may have been deleted, or a waiting thread started: look again
			for ( i = 0; i < numThreads; i++ ) {
				thread = threadList[ i ];
				if ( !thread->removed && thread->executeTime >= 0 && thread->executeTime <= level.time ) {
					break;
				}
			}
			if ( i == numThreads ) {
				break;
			}
			continue;
		}

		if ( runs > 100000 ) {
			G_Error( "Thread_RunThreads: the threads keep starting each other" );
		}

		thread = threadList[ best ];
		thread->executeTime = -1;
		Thread_Execute( thread );
	}
}

/*
================
Thread_IsWaiting
================
*/
qboolean Thread_IsWaiting( scriptThread_t *thread ) {
	if ( thread->waitingForThread || ( thread->waitingFor != ENTITYNUM_NONE ) ) {
		return qtrue;
	}

	if ( thread->waitingUntil && ( thread->waitingUntil > level.time ) ) {
		return qtrue;
	}

	return qfalse;
}

/*
================
Thread_CallFunction

NOTE: If this is called from within a event called by this thread, the function arguments will be invalid after calling this function.
================
*/
void Thread_CallFunction( scriptThread_t *thread, const function_t *func, qboolean clearStack ) {
	Thread_ClearWaitFor( thread );
	Interp_EnterFunction( &thread->interpreter, func, clearStack );
}

void Thread_CallObjectFunction( scriptThread_t *thread, gentity_t *self, const function_t *func, qboolean clearStack ) {
	assert( self );
	Thread_ClearWaitFor( thread );
	Interp_EnterObjectFunction( &thread->interpreter, self, func, clearStack );
}

/*
================
Thread_ClearWaitFor
================
*/
void Thread_ClearWaitFor( scriptThread_t *thread ) {
	thread->waitingFor			= ENTITYNUM_NONE;
	thread->waitingForThread	= NULL;
	thread->waitingUntil		= 0;
}

/*
================
Thread_Error / Warning
================
*/
void QDECL Thread_Error( scriptThread_t *thread, const char *fmt, ... ) {
	va_list	argptr;
	char	text[ 1024 ];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	Interp_Error( &thread->interpreter, "%s", text );
}

void QDECL Thread_Warning( scriptThread_t *thread, const char *fmt, ... ) {
	va_list	argptr;
	char	text[ 1024 ];

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	Interp_Warning( &thread->interpreter, "%s", text );
}

/*
================
Thread_Pause
================
*/
void Thread_Pause( scriptThread_t *thread ) {
	Thread_ClearWaitFor( thread );
	thread->interpreter.doneProcessing = qtrue;
}

/*
================
Thread_WaitMS
================
*/
void Thread_WaitMS( scriptThread_t *thread, int time ) {
	Thread_Pause( thread );
	if ( level.skipCinematic && !thread->manualControl ) {
		// a cinematic is skipped: on at once, this frame
		thread->waitingUntil = level.time;
		Thread_DelayedStart( thread, 0 );
		return;
	}
	thread->waitingUntil = level.time + time;
}

/*
================
Thread_WaitSec
================
*/
void Thread_WaitSec( scriptThread_t *thread, float time ) {
	Thread_WaitMS( thread, ( int )( time * 1000.0f ) );
}

/*
================
Thread_WaitFrame
================
*/
void Thread_WaitFrame( scriptThread_t *thread ) {
	Thread_Pause( thread );

	// manual control threads don't set waitingUntil so that they can be run again
	// that frame if necessary.
	if ( !thread->manualControl ) {
		thread->waitingUntil = level.time + MAX( level.time - level.previousTime, 1 );
	}
}

/***********************************************************************

  Script callable events

***********************************************************************/

static void Event_TerminateThread( scriptThread_t *thread, const intptr_t *args ) {
	Thread_KillThread( EVARG_INT( 0 ) );
}

static void Event_Pause( scriptThread_t *thread, const intptr_t *args ) {
	Thread_Pause( thread );
}

static void Event_Wait( scriptThread_t *thread, const intptr_t *args ) {
	Thread_WaitSec( thread, EVARG_FLOAT( 0 ) );
}

static void Event_WaitFrame( scriptThread_t *thread, const intptr_t *args ) {
	Thread_WaitFrame( thread );
}

static void Event_SetCamera( scriptThread_t *thread, const intptr_t *args ) {
	gentity_t *cam = EVARG_ENTITY( 0 );

	if ( !cam || ( Q_stricmp( cam->classname, "func_cameraspline" ) && Q_stricmp( cam->classname, "func_cameraview" ) ) ) {
		Thread_Error( thread, "Entity is not a camera" );
		return;
	}
	G_SetCamera( cam );
}

static void Event_FirstPerson( scriptThread_t *thread, const intptr_t *args ) {
	G_SetCamera( NULL );
}

static void Event_FadeIn( scriptThread_t *thread, const intptr_t *args ) {
	G_SetFade( EVARG_VECTOR( 0 ), 0, EVARG_FLOAT( 1 ) * 1000 );
}

static void Event_FadeOut( scriptThread_t *thread, const intptr_t *args ) {
	G_SetFade( EVARG_VECTOR( 0 ), 1, EVARG_FLOAT( 1 ) * 1000 );
}

static void Event_FadeTo( scriptThread_t *thread, const intptr_t *args ) {
	G_SetFade( EVARG_VECTOR( 0 ), EVARG_FLOAT( 1 ), EVARG_FLOAT( 2 ) * 1000 );
}

static void Event_StartGlobalSound( scriptThread_t *thread, const intptr_t *args ) {
	if ( !level.skipCinematic ) {
		G_GlobalSound( EVARG_STRING( 0 ) );
	}
}

static void Event_StartGlobalLoop( scriptThread_t *thread, const intptr_t *args ) {
	if ( !level.skipCinematic ) {
		G_GlobalLoop( EVARG_STRING( 0 ) );
	}
}

static void Event_StopGlobalLoops( scriptThread_t *thread, const intptr_t *args ) {
	G_StopGlobalLoops();
}

static void Event_InCinematic( scriptThread_t *thread, const intptr_t *args ) {
	Thread_ReturnInt( level.inCinematic );
}

static void Event_SkippingCinematic( scriptThread_t *thread, const intptr_t *args ) {
	Thread_ReturnInt( level.skipCinematic );
}

static void Event_WaitFor( scriptThread_t *thread, const intptr_t *args ) {
	gentity_t *ent = EVARG_ENTITY( 0 );

	if ( ent && G_ScriptSetCallback( ent ) ) {
		Thread_Pause( thread );
		thread->waitingFor = ent - g_entities;
	}
}

static void Event_WaitForThread( scriptThread_t *thread, const intptr_t *args ) {
	scriptThread_t *waitFor;

	waitFor = Thread_GetThread( EVARG_INT( 0 ) );
	if ( !waitFor ) {
		if ( g_debugScript.integer ) {
			// just print a warning and continue executing
			Thread_Warning( thread, "Thread %d not running", EVARG_INT( 0 ) );
		}
	} else {
		Thread_Pause( thread );
		thread->waitingForThread = waitFor;
	}
}

static void Event_Print( scriptThread_t *thread, const intptr_t *args ) {
	G_Printf( "%s", EVARG_STRING( 0 ) );
}

static void Event_PrintLn( scriptThread_t *thread, const intptr_t *args ) {
	G_Printf( "%s\n", EVARG_STRING( 0 ) );
}

static void Event_Say( scriptThread_t *thread, const intptr_t *args ) {
	trap_SendServerCommand( -1, va( "chat \"%s\"", EVARG_STRING( 0 ) ) );
}

static void Event_Assert( scriptThread_t *thread, const intptr_t *args ) {
	assert( EVARG_FLOAT( 0 ) );
}

static void Event_Trigger( scriptThread_t *thread, const intptr_t *args ) {
	gentity_t *ent = EVARG_ENTITY( 0 );

	if ( ent ) {
		G_ScriptActivate( ent, G_ScriptPlayer() );
	}
}

static void Event_SetCvar( scriptThread_t *thread, const intptr_t *args ) {
	trap_Cvar_Set( EVARG_STRING( 0 ), EVARG_STRING( 1 ) );
}

static void Event_GetCvar( scriptThread_t *thread, const intptr_t *args ) {
	char value[ MAX_STRING_LEN ];

	trap_Cvar_VariableStringBuffer( EVARG_STRING( 0 ), value, sizeof( value ) );
	Thread_ReturnString( value );
}

static void Event_Random( scriptThread_t *thread, const intptr_t *args ) {
	Thread_ReturnFloat( EVARG_FLOAT( 0 ) * random() );
}

static void Event_GetTime( scriptThread_t *thread, const intptr_t *args ) {
	Thread_ReturnFloat( level.time * 0.001f );
}

static void Event_KillThread( scriptThread_t *thread, const intptr_t *args ) {
	Thread_KillThreadByName( EVARG_STRING( 0 ) );
}

static void Event_SetThreadName( scriptThread_t *thread, const intptr_t *args ) {
	Thread_SetThreadName( thread, EVARG_STRING( 0 ) );
}

static void Event_GetEntity( scriptThread_t *thread, const intptr_t *args ) {
	const char	*name = EVARG_STRING( 0 );
	int			entnum;

	assert( name );

	if ( name[ 0 ] == '*' ) {
		entnum = atoi( &name[ 1 ] );
		if ( ( entnum < 0 ) || ( entnum >= MAX_GENTITIES ) ) {
			Thread_Error( thread, "Entity number in string out of range." );
		}
		Thread_ReturnEntity( Interp_GetEntity( entnum + 1 ) );
	} else {
		Thread_ReturnEntity( G_ScriptFindEntity( name ) );
	}
}

// the spawn args sys.spawn() spawns the next entity with (Doom 3's are the thread's)
static char		spawnArgKeys[ MAX_SPAWN_VARS ][ MAX_STRING_LEN ];
static char		spawnArgValues[ MAX_SPAWN_VARS ][ MAX_STRING_LEN ];
static int		numSpawnArgs;

static const char *SpawnArg( const char *key ) {
	int i;

	for ( i = 0; i < numSpawnArgs; i++ ) {
		if ( !Q_stricmp( spawnArgKeys[ i ], key ) ) {
			return spawnArgValues[ i ];
		}
	}
	return NULL;
}

static void SetSpawnArg( const char *key, const char *value ) {
	int i;

	for ( i = 0; i < numSpawnArgs; i++ ) {
		if ( !Q_stricmp( spawnArgKeys[ i ], key ) ) {
			break;
		}
	}
	if ( i == numSpawnArgs ) {
		if ( numSpawnArgs >= MAX_SPAWN_VARS ) {
			return;
		}
		numSpawnArgs++;
	}
	Q_strncpyz( spawnArgKeys[ i ], key, sizeof( spawnArgKeys[ i ] ) );
	Q_strncpyz( spawnArgValues[ i ], value, sizeof( spawnArgValues[ i ] ) );
}

static void Event_Spawn( scriptThread_t *thread, const intptr_t *args ) {
	const char	*keys[ MAX_SPAWN_VARS ];
	const char	*values[ MAX_SPAWN_VARS ];
	gentity_t	*ent;
	int			i;

	SetSpawnArg( "classname", EVARG_STRING( 0 ) );
	for ( i = 0; i < numSpawnArgs; i++ ) {
		keys[ i ] = spawnArgKeys[ i ];
		values[ i ] = spawnArgValues[ i ];
	}
	ent = G_SpawnEntityFromKeys( numSpawnArgs, keys, values );
	Thread_ReturnEntity( ent );
	numSpawnArgs = 0;
}

static void Event_SetSpawnArg( scriptThread_t *thread, const intptr_t *args ) {
	SetSpawnArg( EVARG_STRING( 0 ), EVARG_STRING( 1 ) );
}

static void Event_SpawnString( scriptThread_t *thread, const intptr_t *args ) {
	const char *result;

	result = SpawnArg( EVARG_STRING( 0 ) );
	Thread_ReturnString( result ? result : EVARG_STRING( 1 ) );
}

static void Event_SpawnFloat( scriptThread_t *thread, const intptr_t *args ) {
	const char *result;

	result = SpawnArg( EVARG_STRING( 0 ) );
	if ( result ) {
		Thread_ReturnFloat( atof( result ) );
	} else {
		Thread_ReturnFloat( EVARG_FLOAT( 1 ) );
	}
}

static void Event_SpawnVector( scriptThread_t *thread, const intptr_t *args ) {
	const char	*result;
	vec3_t		v;

	result = SpawnArg( EVARG_STRING( 0 ) );
	if ( result ) {
		VectorClear( v );
		sscanf( result, "%f %f %f", &v[0], &v[1], &v[2] );
		Thread_ReturnVector( v );
	} else {
		Thread_ReturnVector( EVARG_VECTOR( 1 ) );
	}
}

static void Event_AngToForward( scriptThread_t *thread, const intptr_t *args ) {
	vec3_t forward;

	AngleVectors( EVARG_VECTOR( 0 ), forward, NULL, NULL );
	Thread_ReturnVector( forward );
}

static void Event_AngToRight( scriptThread_t *thread, const intptr_t *args ) {
	vec3_t right;

	AngleVectors( EVARG_VECTOR( 0 ), NULL, right, NULL );
	Thread_ReturnVector( right );
}

static void Event_AngToUp( scriptThread_t *thread, const intptr_t *args ) {
	vec3_t up;

	AngleVectors( EVARG_VECTOR( 0 ), NULL, NULL, up );
	Thread_ReturnVector( up );
}

static void Event_GetSine( scriptThread_t *thread, const intptr_t *args ) {
	Thread_ReturnFloat( sin( DEG2RAD( EVARG_FLOAT( 0 ) ) ) );
}

static void Event_GetCosine( scriptThread_t *thread, const intptr_t *args ) {
	Thread_ReturnFloat( cos( DEG2RAD( EVARG_FLOAT( 0 ) ) ) );
}

static void Event_GetSquareRoot( scriptThread_t *thread, const intptr_t *args ) {
	Thread_ReturnFloat( sqrt( EVARG_FLOAT( 0 ) ) );
}

static void Event_VecNormalize( scriptThread_t *thread, const intptr_t *args ) {
	vec3_t n;

	VectorCopy( EVARG_VECTOR( 0 ), n );
	VectorNormalize( n );
	Thread_ReturnVector( n );
}

static void Event_VecLength( scriptThread_t *thread, const intptr_t *args ) {
	Thread_ReturnFloat( VectorLength( EVARG_VECTOR( 0 ) ) );
}

static void Event_VecDotProduct( scriptThread_t *thread, const intptr_t *args ) {
	Thread_ReturnFloat( DotProduct( EVARG_VECTOR( 0 ), EVARG_VECTOR( 1 ) ) );
}

static void Event_VecCrossProduct( scriptThread_t *thread, const intptr_t *args ) {
	vec3_t c;

	CrossProduct( EVARG_VECTOR( 0 ), EVARG_VECTOR( 1 ), c );
	Thread_ReturnVector( c );
}

static void Event_VecToAngles( scriptThread_t *thread, const intptr_t *args ) {
	vec3_t ang;

	vectoangles( EVARG_VECTOR( 0 ), ang );
	Thread_ReturnVector( ang );
}

/*
================
ScriptContents

the contents a script names, as Doom 3's are numbered (script/dk_defs.script):
low bits, which a script's float holds exactly (Quake 3's body and corpse
bits it wouldn't)
================
*/
static int ScriptContents( int contents ) {
	static const int map[][2] = {
		{ 1 << 0,	CONTENTS_SOLID },
		{ 1 << 2,	CONTENTS_WATER },
		{ 1 << 3,	CONTENTS_PLAYERCLIP },
		{ 1 << 4,	CONTENTS_MONSTERCLIP },
		{ 1 << 8,	CONTENTS_BODY },
		{ 1 << 10,	CONTENTS_CORPSE },
		{ 1 << 11,	CONTENTS_BODY },		// Doom 3's render model: our hit boxes are bodies
		{ 1 << 12,	CONTENTS_TRIGGER },
		{ 1 << 16,	CONTENTS_LAVA },		// Quake 3's own, past Doom 3's
		{ 1 << 17,	CONTENTS_SLIME },
		{ 1 << 18,	CONTENTS_MOVER },
	};
	int i, out;

	out = 0;
	for ( i = 0; i < ARRAY_LEN( map ); i++ ) {
		if ( contents & map[ i ][ 0 ] ) {
			out |= map[ i ][ 1 ];
		}
	}
	return out;
}

static void Event_Trace( scriptThread_t *thread, const intptr_t *args ) {
	gentity_t *pass = EVARG_ENTITY( 5 );

	G_TraceBox( &trace, EVARG_VECTOR( 0 ), EVARG_VECTOR( 2 ), EVARG_VECTOR( 3 ), EVARG_VECTOR( 1 ), pass ? pass->s.number : ENTITYNUM_NONE, ScriptContents( EVARG_INT( 4 ) ) );
	Thread_ReturnFloat( trace.fraction );
}

static void Event_TracePoint( scriptThread_t *thread, const intptr_t *args ) {
	gentity_t *pass = EVARG_ENTITY( 3 );

	if ( EVARG_INT( 2 ) & ( 1 << 11 ) ) {
		// Doom 3's CONTENTS_RENDERMODEL: what a shot meets, characters' hit boxes
		G_TraceShot( &trace, EVARG_VECTOR( 0 ), NULL, NULL, EVARG_VECTOR( 1 ), pass ? pass->s.number : ENTITYNUM_NONE, ScriptContents( EVARG_INT( 2 ) ), NULL );
	} else {
		G_TraceBox( &trace, EVARG_VECTOR( 0 ), NULL, NULL, EVARG_VECTOR( 1 ), pass ? pass->s.number : ENTITYNUM_NONE, ScriptContents( EVARG_INT( 2 ) ) );
	}
	Thread_ReturnFloat( trace.fraction );
}

static void Event_GetTraceFraction( scriptThread_t *thread, const intptr_t *args ) {
	Thread_ReturnFloat( trace.fraction );
}

static void Event_GetTraceEndPos( scriptThread_t *thread, const intptr_t *args ) {
	Thread_ReturnVector( trace.endpos );
}

static void Event_GetTraceNormal( scriptThread_t *thread, const intptr_t *args ) {
	if ( trace.fraction < 1.0f ) {
		Thread_ReturnVector( trace.plane.normal );
	} else {
		Thread_ReturnVector( vec3_origin );
	}
}

static void Event_GetTraceEntity( scriptThread_t *thread, const intptr_t *args ) {
	if ( trace.fraction < 1.0f && trace.entityNum >= 0 && trace.entityNum < MAX_GENTITIES ) {
		Thread_ReturnEntity( Interp_GetEntity( trace.entityNum + 1 ) );
	} else {
		Thread_ReturnEntity( NULL );
	}
}

static void Event_GetTraceJoint( scriptThread_t *thread, const intptr_t *args ) {
	Thread_ReturnString( "" );
}

static void Event_GetTraceBody( scriptThread_t *thread, const intptr_t *args ) {
	Thread_ReturnString( "" );
}

static void Event_StartMusic( scriptThread_t *thread, const intptr_t *args ) {
	trap_SetConfigstring( CS_MUSIC, EVARG_STRING( 0 ) );
}

static void Event_Error( scriptThread_t *thread, const intptr_t *args ) {
	Thread_Error( thread, "%s", EVARG_STRING( 0 ) );
}

static void Event_Warning( scriptThread_t *thread, const intptr_t *args ) {
	Thread_Warning( thread, "%s", EVARG_STRING( 0 ) );
}

static void Event_StrLen( scriptThread_t *thread, const intptr_t *args ) {
	Thread_ReturnInt( strlen( EVARG_STRING( 0 ) ) );
}

static void Event_StrLeft( scriptThread_t *thread, const intptr_t *args ) {
	const char	*string = EVARG_STRING( 0 );
	int			num = EVARG_INT( 1 );
	char		result[ MAX_STRING_LEN ];

	if ( num < 0 ) {
		Thread_ReturnString( "" );
		return;
	}

	if ( (int)strlen( string ) < num ) {
		Thread_ReturnString( string );
		return;
	}

	Q_strncpyz( result, string, MIN( num + 1, (int)sizeof( result ) ) );
	Thread_ReturnString( result );
}

static void Event_StrRight( scriptThread_t *thread, const intptr_t *args ) {
	const char	*string = EVARG_STRING( 0 );
	int			num = EVARG_INT( 1 );
	int			len;

	if ( num < 0 ) {
		Thread_ReturnString( "" );
		return;
	}

	len = strlen( string );
	if ( len < num ) {
		Thread_ReturnString( string );
		return;
	}

	Thread_ReturnString( string + len - num );
}

static void Event_StrSkip( scriptThread_t *thread, const intptr_t *args ) {
	const char	*string = EVARG_STRING( 0 );
	int			num = EVARG_INT( 1 );

	if ( num < 0 ) {
		Thread_ReturnString( string );
		return;
	}

	if ( (int)strlen( string ) < num ) {
		Thread_ReturnString( "" );
		return;
	}

	Thread_ReturnString( string + num );
}

static void Event_StrMid( scriptThread_t *thread, const intptr_t *args ) {
	const char	*string = EVARG_STRING( 0 );
	int			start = EVARG_INT( 1 );
	int			num = EVARG_INT( 2 );
	int			len;
	char		result[ MAX_STRING_LEN ];

	if ( start < 0 ) {
		start = 0;
	}
	len = strlen( string );
	if ( start > len ) {
		start = len;
	}

	if ( num < 0 ) {
		num = 0;
	} else if ( start + num > len ) {
		num = len - start;
	}

	Q_strncpyz( result, string + start, MIN( num + 1, (int)sizeof( result ) ) );
	Thread_ReturnString( result );
}

static void Event_StrToFloat( scriptThread_t *thread, const intptr_t *args ) {
	Thread_ReturnFloat( atof( EVARG_STRING( 0 ) ) );
}

static void Event_IsClient( scriptThread_t *thread, const intptr_t *args ) {
	Thread_ReturnFloat( 0 );
}

static void Event_IsMultiplayer( scriptThread_t *thread, const intptr_t *args ) {
	Thread_ReturnFloat( g_gametype.integer != GT_CAMPAIGN );
}

static void Event_GetFrameTime( scriptThread_t *thread, const intptr_t *args ) {
	Thread_ReturnFloat( MAX( level.time - level.previousTime, 1 ) * 0.001f );
}

static void Event_GetTicsPerSecond( scriptThread_t *thread, const intptr_t *args ) {
	Thread_ReturnFloat( 1000.0f / MAX( level.time - level.previousTime, 1 ) );
}

// sys's events, as idThread responds to them
const sysEventCallback_t threadEventCallbacks[] = {
	{ "terminate",			"d",		0,		Event_TerminateThread },
	{ "pause",				NULL,		0,		Event_Pause },
	{ "wait",				"f",		0,		Event_Wait },
	{ "waitFrame",			NULL,		0,		Event_WaitFrame },
	{ "waitFor",			"e",		0,		Event_WaitFor },
	{ "setCamera",			"e",		0,		Event_SetCamera },
	{ "firstPerson",		NULL,		0,		Event_FirstPerson },
	{ "fadeIn",				"vf",		0,		Event_FadeIn },
	{ "fadeOut",			"vf",		0,		Event_FadeOut },
	{ "fadeTo",				"vff",		0,		Event_FadeTo },
	{ "inCinematic",		NULL,		'd',	Event_InCinematic },
	{ "startGlobalSound",	"s",		0,		Event_StartGlobalSound },
	{ "startGlobalLoop",	"s",		0,		Event_StartGlobalLoop },
	{ "stopGlobalLoops",	NULL,		0,		Event_StopGlobalLoops },
	{ "skippingCinematic",	NULL,		'd',	Event_SkippingCinematic },
	{ "waitForThread",		"d",		0,		Event_WaitForThread },
	{ "print",				"s",		0,		Event_Print },
	{ "println",			"s",		0,		Event_PrintLn },
	{ "say",				"s",		0,		Event_Say },
	{ "assert",				"f",		0,		Event_Assert },
	{ "trigger",			"e",		0,		Event_Trigger },
	{ "setcvar",			"ss",		0,		Event_SetCvar },
	{ "getcvar",			"s",		's',	Event_GetCvar },
	{ "random",				"f",		'f',	Event_Random },
	{ "getTime",			NULL,		'f',	Event_GetTime },
	{ "killthread",			"s",		0,		Event_KillThread },
	{ "threadname",			"s",		0,		Event_SetThreadName },
	{ "getEntity",			"s",		'e',	Event_GetEntity },
	{ "spawn",				"s",		'e',	Event_Spawn },
	{ "setSpawnArg",		"ss",		0,		Event_SetSpawnArg },
	{ "SpawnString",		"ss",		's',	Event_SpawnString },
	{ "SpawnFloat",			"sf",		'f',	Event_SpawnFloat },
	{ "SpawnVector",		"sv",		'v',	Event_SpawnVector },
	{ "angToForward",		"v",		'v',	Event_AngToForward },
	{ "angToRight",			"v",		'v',	Event_AngToRight },
	{ "angToUp",			"v",		'v',	Event_AngToUp },
	{ "sin",				"f",		'f',	Event_GetSine },
	{ "cos",				"f",		'f',	Event_GetCosine },
	{ "sqrt",				"f",		'f',	Event_GetSquareRoot },
	{ "vecNormalize",		"v",		'v',	Event_VecNormalize },
	{ "vecLength",			"v",		'f',	Event_VecLength },
	{ "DotProduct",			"vv",		'f',	Event_VecDotProduct },
	{ "CrossProduct",		"vv",		'v',	Event_VecCrossProduct },
	{ "VecToAngles",		"v",		'v',	Event_VecToAngles },
	{ "trace",				"vvvvde",	'f',	Event_Trace },
	{ "tracePoint",			"vvde",		'f',	Event_TracePoint },
	{ "getTraceFraction",	NULL,		'f',	Event_GetTraceFraction },
	{ "getTraceEndPos",		NULL,		'v',	Event_GetTraceEndPos },
	{ "getTraceNormal",		NULL,		'v',	Event_GetTraceNormal },
	{ "getTraceEntity",		NULL,		'e',	Event_GetTraceEntity },
	{ "getTraceJoint",		NULL,		's',	Event_GetTraceJoint },
	{ "getTraceBody",		NULL,		's',	Event_GetTraceBody },
	{ "music",				"s",		0,		Event_StartMusic },
	{ "error",				"s",		0,		Event_Error },
	{ "warning",			"s",		0,		Event_Warning },
	{ "strLength",			"s",		'd',	Event_StrLen },
	{ "strLeft",			"sd",		's',	Event_StrLeft },
	{ "strRight",			"sd",		's',	Event_StrRight },
	{ "strSkip",			"sd",		's',	Event_StrSkip },
	{ "strMid",				"sdd",		's',	Event_StrMid },
	{ "strToFloat",			"s",		'f',	Event_StrToFloat },
	{ "isClient",			NULL,		'f',	Event_IsClient },
	{ "isMultiplayer",		NULL,		'f',	Event_IsMultiplayer },
	{ "getFrameTime",		NULL,		'f',	Event_GetFrameTime },
	{ "getTicsPerSecond",	NULL,		'f',	Event_GetTicsPerSecond },
	{ NULL }
};

/*
================
Thread_SaveAll

every thread, its stack and where it is (Doom 3's idThread::Save and
idInterpreter::Save), with g_save.c's writers: functions by index, entities
by number
================
*/
static void SaveInt( int i ) {
	G_SaveBytes( &i, sizeof( i ) );
}

static int LoadInt( void ) {
	int i;

	G_LoadBytes( &i, sizeof( i ) );
	return i;
}

void Thread_SaveAll( void ) {
	const interpreter_t	*interp;
	scriptThread_t		*thread;
	int					i, j;

	SaveInt( threadIndex );
	SaveInt( numThreads );
	for ( i = 0; i < numThreads; i++ ) {
		thread = threadList[ i ];
		interp = &thread->interpreter;

		SaveInt( thread->threadNum );
		SaveInt( thread->waitingForThread ? thread->waitingForThread->threadNum : 0 );
		SaveInt( thread->waitingFor );
		SaveInt( thread->waitingUntil );
		G_SaveString( thread->threadName );
		SaveInt( thread->lastExecuteTime );
		SaveInt( thread->creationTime );
		SaveInt( thread->manualControl );
		SaveInt( thread->executeTime );
		SaveInt( thread->removed );

		SaveInt( interp->callStackDepth );
		for ( j = 0; j < interp->callStackDepth; j++ ) {
			SaveInt( interp->callStack[ j ].s );
			SaveInt( interp->callStack[ j ].f ? Program_GetFunctionIndex( interp->callStack[ j ].f ) : -1 );
			SaveInt( interp->callStack[ j ].stackbase );
		}
		SaveInt( interp->maxStackDepth );
		SaveInt( interp->localstackUsed );
		G_SaveBytes( interp->localstack, interp->localstackUsed );
		SaveInt( interp->localstackBase );
		SaveInt( interp->maxLocalstackUsed );
		SaveInt( interp->currentFunction ? Program_GetFunctionIndex( interp->currentFunction ) : -1 );
		SaveInt( interp->instructionPointer );
		SaveInt( interp->popParms );
		G_SaveString( interp->multiFrameEvent ? interp->multiFrameEvent->name : NULL );
		SaveInt( interp->eventEntity ? interp->eventEntity - g_entities : -1 );
		SaveInt( interp->doneProcessing );
		SaveInt( interp->threadDying );
		SaveInt( interp->terminateOnExit );
		SaveInt( interp->debug );
	}
}

/*
================
Thread_RestoreAll

the threads Thread_SaveAll wrote, instead of those running
================
*/
void Thread_RestoreAll( void ) {
	interpreter_t	*interp;
	scriptThread_t	*thread;
	char			name[ MAX_STRING_CHARS ];
	int				*waitingFor;
	int				i, j, n, count;

	Thread_Restart();

	threadIndex = LoadInt();
	count = LoadInt();
	if ( count < 0 || count > 65536 ) {
		G_SaveFailed();
		return;
	}
	waitingFor = Script_Alloc( MAX( count, 1 ) * sizeof( int ) );

	for ( i = 0; i < count; i++ ) {
		thread = Script_Alloc( sizeof( *thread ) );
		Interp_Init( &thread->interpreter );
		interp = &thread->interpreter;
		interp->thread = thread;

		thread->threadNum = LoadInt();
		waitingFor[ i ] = LoadInt();
		thread->waitingFor = LoadInt();
		thread->waitingUntil = LoadInt();
		G_LoadStringBuf( name, sizeof( name ) );
		Thread_SetThreadName( thread, name );
		thread->lastExecuteTime = LoadInt();
		thread->creationTime = LoadInt();
		thread->manualControl = LoadInt();
		thread->executeTime = LoadInt();
		thread->removed = LoadInt();

		interp->callStackDepth = LoadInt();
		if ( interp->callStackDepth < 0 || interp->callStackDepth > MAX_STACK_DEPTH ) {
			G_SaveFailed();
			interp->callStackDepth = 0;
		}
		for ( j = 0; j < interp->callStackDepth; j++ ) {
			interp->callStack[ j ].s = LoadInt();
			n = LoadInt();
			interp->callStack[ j ].f = ( n >= 0 && n < Program_NumFunctions() ) ? Program_GetFunction( n ) : NULL;
			interp->callStack[ j ].stackbase = LoadInt();
		}
		interp->maxStackDepth = LoadInt();
		interp->localstackUsed = LoadInt();
		if ( interp->localstackUsed < 0 || interp->localstackUsed > LOCALSTACK_SIZE ) {
			G_SaveFailed();
			interp->localstackUsed = 0;
		}
		G_LoadBytes( interp->localstack, interp->localstackUsed );
		interp->localstackBase = LoadInt();
		interp->maxLocalstackUsed = LoadInt();
		n = LoadInt();
		interp->currentFunction = ( n >= 0 && n < Program_NumFunctions() ) ? Program_GetFunction( n ) : NULL;
		interp->instructionPointer = LoadInt();
		interp->popParms = LoadInt();
		interp->multiFrameEvent = G_LoadStringBuf( name, sizeof( name ) ) ? Event_FindEvent( name ) : NULL;
		n = LoadInt();
		interp->eventEntity = ( n >= 0 && n < MAX_GENTITIES ) ? &g_entities[ n ] : NULL;
		interp->doneProcessing = LoadInt();
		interp->threadDying = LoadInt();
		interp->terminateOnExit = LoadInt();
		interp->debug = LoadInt();

		if ( numThreads >= maxThreads ) {
			scriptThread_t **newList;

			maxThreads = maxThreads ? maxThreads * 2 : 64;
			newList = Script_Alloc( maxThreads * sizeof( newList[0] ) );
			if ( threadList ) {
				memcpy( newList, threadList, numThreads * sizeof( newList[0] ) );
				Script_Free( threadList );
			}
			threadList = newList;
		}
		threadList[ numThreads++ ] = thread;
	}

	// whom they wait for
	for ( i = 0; i < numThreads; i++ ) {
		threadList[ i ]->waitingForThread = waitingFor[ i ] ? Thread_GetThread( waitingFor[ i ] ) : NULL;
	}
	Script_Free( waitingFor );
}
