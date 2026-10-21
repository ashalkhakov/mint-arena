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
// g_script_debug.c -- the script debugger's server: dhewm3's
// tools/debugger/DebuggerServer.cpp with its script-debugger-protocol (2)
// changes, and the game's answers to it (Game_local.cpp's DebuggerRequest,
// GameEdit.cpp's), speaking the same messages to the same clients (dhewm3's
// debugger, UDQuakeTools' udscript-dap).
//
// The server listens on UDP 27980 and talks to the client at
// com_dbgClientAdr:27981, when com_enableDebuggerServer is set. dhewm3 runs
// it in a thread of its own; here the game reads its messages each frame and,
// while a thread is stopped, waits for them in Break() as dhewm3's game thread
// waits for the debugger's.

#if !defined( Q3_VM ) && !defined( _WIN32 )
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#define SCRIPT_DEBUGGER
#endif

#include "g_local.h"

/*
================
ScriptDebug_Update / ScriptError, without a server
================
*/
#ifndef SCRIPT_DEBUGGER

qboolean ScriptDebug_Update( interpreter_t *interpreter, int instructionPointer ) {
	return qfalse;
}

void ScriptDebug_ScriptError( interpreter_t *interpreter, int instructionPointer, const char *message, qboolean isError ) {
}

void ScriptDebug_Init( void ) {
}

void ScriptDebug_Frame( void ) {
}

void ScriptDebug_Shutdown( void ) {
}

#else

#define DEBUGGER_SERVER_PORT		27980
#define DEBUGGER_CLIENT_PORT		27981
#define MAX_MSGLEN					8600	// a lot, to list all the threads of a map
#define MAX_BREAKPOINTS				256

// tools/debugger/DebuggerMessages.h
enum {
	DBMSG_UNKNOWN,
	DBMSG_CONNECT,
	DBMSG_CONNECTED,
	DBMSG_DISCONNECT,
	DBMSG_ADDBREAKPOINT,
	DBMSG_REMOVEBREAKPOINT,
	DBMSG_HITBREAKPOINT,
	DBMSG_RESUME,
	DBMSG_RESUMED,
	DBMSG_BREAK,
	DBMSG_PRINT,
	DBMSG_INSPECTVARIABLE,
	DBMSG_INSPECTCALLSTACK,
	DBMSG_INSPECTTHREADS,
	DBMSG_STEPOVER,
	DBMSG_STEPINTO,
	DBMSG_INSPECTSCRIPTS,
	DBMSG_EXECCOMMAND,
	// protocol 2
	DBMSG_VERSION,
	DBMSG_STEPOUT,
	DBMSG_BREAKONERRORS,
	DBMSG_SCRIPTERROR,
	DBMSG_INSPECTTHREADCALLSTACK,
	DBMSG_INSPECTVARIABLES,
	DBMSG_SETVARIABLE,
	DBMSG_INSPECTENTITY,
	DBMSG_SETNEXTSTATEMENT
};

#define DEBUGGER_PROTOCOL_VERSION	2

#define DBCAP_STEPOUT			1
#define DBCAP_INSPECT			2
#define DBCAP_BREAKONERRORS		4

#define DBERR_ERRORS			1
#define DBERR_WARNINGS			2

#define DBVAR_LOCAL				0
#define DBVAR_FIELD				1
#define DBVAR_SPAWNARG			2

/*
===============================================================================

idBitMsg: the messages are Doom 3's, bit packed, little endian; strings
byte aligned

===============================================================================
*/
typedef struct {
	byte		*data;
	int			maxSize;
	int			curSize;
	int			writeBit;
	int			readCount;
	int			readBit;
	qboolean	overflowed;
} dbMsg_t;

static void Msg_Init( dbMsg_t *msg, byte *data, int size ) {
	memset( msg, 0, sizeof( *msg ) );
	msg->data = data;
	msg->maxSize = size;
}

static int Msg_RemainingSpace( const dbMsg_t *msg ) {
	return msg->maxSize - msg->curSize;
}

static void Msg_WriteBits( dbMsg_t *msg, int value, int numBits ) {
	int put, fraction;

	if ( numBits < 0 ) {
		numBits = -numBits;
	}
	while ( numBits ) {
		if ( msg->writeBit == 0 ) {
			if ( msg->curSize >= msg->maxSize ) {
				msg->overflowed = qtrue;
				return;
			}
			msg->data[ msg->curSize ] = 0;
			msg->curSize++;
		}
		put = 8 - msg->writeBit;
		if ( put > numBits ) {
			put = numBits;
		}
		fraction = value & ( ( 1 << put ) - 1 );
		msg->data[ msg->curSize - 1 ] |= fraction << msg->writeBit;
		numBits -= put;
		value = (int)( (unsigned)value >> put );
		msg->writeBit = ( msg->writeBit + put ) & 7;
	}
}

static void Msg_WriteByte( dbMsg_t *msg, int c ) {
	Msg_WriteBits( msg, c, 8 );
}

static void Msg_WriteShort( dbMsg_t *msg, int c ) {
	Msg_WriteBits( msg, c, -16 );
}

static void Msg_WriteInt( dbMsg_t *msg, int c ) {
	Msg_WriteBits( msg, c, 32 );
}

static void Msg_WriteString( dbMsg_t *msg, const char *s ) {
	int len;

	if ( !s ) {
		s = "";
	}
	len = strlen( s ) + 1;
	msg->writeBit = 0;		// byte aligned
	if ( msg->curSize + len > msg->maxSize ) {
		msg->overflowed = qtrue;
		return;
	}
	memcpy( msg->data + msg->curSize, s, len );
	msg->curSize += len;
}

static int Msg_ReadBits( dbMsg_t *msg, int numBits ) {
	int value, valueBits, get, fraction;
	qboolean sgn;

	value = 0;
	valueBits = 0;
	sgn = numBits < 0;
	if ( sgn ) {
		numBits = -numBits;
	}
	while ( valueBits < numBits ) {
		if ( msg->readBit == 0 ) {
			msg->readCount++;
		}
		if ( msg->readCount > msg->curSize ) {
			return -1;
		}
		get = 8 - msg->readBit;
		if ( get > ( numBits - valueBits ) ) {
			get = numBits - valueBits;
		}
		fraction = msg->data[ msg->readCount - 1 ];
		fraction >>= msg->readBit;
		fraction &= ( 1 << get ) - 1;
		value |= fraction << valueBits;
		valueBits += get;
		msg->readBit = ( msg->readBit + get ) & 7;
	}
	if ( sgn && numBits < 32 && ( value & ( 1 << ( numBits - 1 ) ) ) ) {
		value |= -1 ^ ( ( 1 << numBits ) - 1 );
	}
	return value;
}

static int Msg_ReadShort( dbMsg_t *msg ) {
	return Msg_ReadBits( msg, -16 );
}

static int Msg_ReadInt( dbMsg_t *msg ) {
	return Msg_ReadBits( msg, 32 );
}

static void Msg_ReadString( dbMsg_t *msg, char *buffer, int bufferSize ) {
	int l, c;

	msg->readBit = 0;		// byte aligned
	l = 0;
	while ( 1 ) {
		if ( msg->readCount >= msg->curSize ) {
			break;
		}
		c = msg->data[ msg->readCount++ ];
		if ( c <= 0 ) {
			break;
		}
		if ( l < bufferSize - 1 ) {
			buffer[ l++ ] = c;
		}
	}
	buffer[ l ] = '\0';
}

/*
===============================================================================

the server

===============================================================================
*/
typedef struct {
	char		filename[ MAX_QPATH ];
	int			lineNumber;
	int			id;
	qboolean	onceOnly;
} breakpoint_t;

static vmCvar_t			com_enableDebuggerServer;
static vmCvar_t			com_dbgClientAdr;

static int				sock = -1;
static struct sockaddr_in	clientAdr;
static qboolean			connected;

static breakpoint_t		breakpoints[ MAX_BREAKPOINTS ];
static int				numBreakpoints;

static qboolean			breakNow;			// mBreak: a thread is stopped
static qboolean			breakNext;
static int				breakOnErrors;		// DBERR_*
static qboolean			breakStepOver;
static qboolean			breakStepInto;
static qboolean			breakStepOut;

// the thread a step began in, and where: only it ends the step
static interpreter_t	*stepInterpreter;
static char				stepFile[ MAX_QPATH ];
static int				stepLine;
static int				stepDepth;

static int				breakStepOverDepth;
static const function_t	*breakStepOverFunc1;
static const function_t	*breakStepOverFunc2;
static int				breakInstructionPointer;
static interpreter_t	*breakInterpreter;

static char				lastStatementFile[ MAX_QPATH ];
static int				lastStatementLine = -1;

// the thread of the last statement, and its call depth then
static interpreter_t	*lastInterpreter;
static int				lastDepth;

static void Break( interpreter_t *interpreter, int instructionPointer );

/*
================
SendPacket
================
*/
static void SendPacket( const dbMsg_t *msg ) {
	if ( sock < 0 ) {
		return;
	}
	sendto( sock, (const char *)msg->data, msg->curSize, 0, (const struct sockaddr *)&clientAdr, sizeof( clientAdr ) );
}

static void SendMessage( int dbmsg ) {
	dbMsg_t	msg;
	byte		buffer[ 16 ];

	Msg_Init( &msg, buffer, sizeof( buffer ) );
	Msg_WriteShort( &msg, dbmsg );
	SendPacket( &msg );
}

/*
================
FileNameForStatement

as the client names files: relative, with slashes
================
*/
static const char *FileNameForStatement( int instructionPointer ) {
	static char	name[ MAX_QPATH ];
	char		*s;

	Q_strncpyz( name, Program_GetFilenameForStatement( instructionPointer ), sizeof( name ) );
	for ( s = name; *s; s++ ) {
		if ( *s == '\\' ) {
			*s = '/';
		}
	}
	return name;
}

/*
================
Debugger_FindThreadInterpreter

the interpreter of the thread a request names (the stopped one too)
================
*/
static interpreter_t *Debugger_FindThreadInterpreter( int number ) {
	scriptThread_t *thread;

	if ( breakInterpreter && breakInterpreter->thread && breakInterpreter->thread->threadNum == number ) {
		return breakInterpreter;
	}
	thread = Thread_GetThread( number );
	if ( !thread || !thread->interpreter.currentFunction ) {
		return NULL;
	}
	return &thread->interpreter;
}

/*
================
WriteCallstackFunc

idGameEditExt::MSG_WriteCallstackFunc
================
*/
static void WriteCallstackFunc( dbMsg_t *msg, const prstack_t *stack, int instructionPtr ) {
	const statement_t	*st;
	const function_t	*func;
	char				file[ MAX_QPATH ];
	char				*s;

	func = stack->f;

	// If the function is unknown then just fill in with default data.
	if ( !func ) {
		Msg_WriteString( msg, "<UNKNOWN>" );
		Msg_WriteString( msg, "<UNKNOWN>" );
		Msg_WriteInt( msg, 0 );
		return;
	}
	Msg_WriteString( msg, va( "%s(  )", func->name ) );

	if ( stack->s == -1 ) {
		// this is a fake stack created by debugger, use intruction pointer for retrieval.
		st = Program_GetStatement( instructionPtr );
	} else {
		// the calling statement: the call returns to the one after it
		st = Program_GetStatement( stack->s > 0 ? stack->s - 1 : stack->s );
	}

	Q_strncpyz( file, Program_GetFilename( st->file ), sizeof( file ) );
	for ( s = file; *s; s++ ) {
		if ( *s == '\\' ) {
			*s = '/';
		}
	}
	Msg_WriteString( msg, file );
	Msg_WriteInt( msg, st->linenumber );
}

/*
================
WriteInterpreterInfo

idGameEditExt::MSG_WriteInterpreterInfo: a thread's call stack, the function
it is in first
================
*/
static void WriteInterpreterInfo( dbMsg_t *msg, const interpreter_t *interpreter, int instructionPtr ) {
	prstack_t	temp;
	int			i;

	Msg_WriteShort( msg, interpreter->callStackDepth );

	// write out the current function
	temp.f = interpreter->currentFunction;
	temp.s = -1;
	temp.stackbase = 0;
	WriteCallstackFunc( msg, &temp, instructionPtr );

	// Run through all of the callstack and write each to the msg
	for ( i = interpreter->callStackDepth - 1; i > 0; i-- ) {
		WriteCallstackFunc( msg, &interpreter->callStack[ i ], instructionPtr );
	}
}

/*
================
WriteScriptList

idGameEditExt::MSG_WriteScriptList
================
*/
static void WriteScriptList( dbMsg_t *msg ) {
	char	file[ MAX_QPATH ];
	char	*s;
	int		i;

	Msg_WriteInt( msg, Program_NumFilenames() );
	for ( i = 0; i < Program_NumFilenames(); i++ ) {
		Q_strncpyz( file, Program_GetFilename( i ), sizeof( file ) );
		for ( s = file; *s; s++ ) {
			if ( *s == '\\' ) {
				*s = '/';
			}
		}
		Msg_WriteString( msg, file );
	}
}

/*
================
IsLineCode

idGameEditExt::IsLineCode: a statement of the scripts on that line
================
*/
static qboolean IsLineCode( const char *filename, int linenumber ) {
	int i;

	for ( i = 0; i < Program_NumStatements(); i++ ) {
		if ( Program_GetStatement( i )->linenumber == linenumber && !strcmp( filename, FileNameForStatement( i ) ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
================
IsVectorComponent

a vector's components (the compiler's <name>_x, _y, _z) are no variables of their own
================
*/
static qboolean IsVectorComponent( const char *name, const char **names, int numNames ) {
	int		len, i;
	char	base[ MAX_STRING_CHARS ];

	len = strlen( name );
	if ( len <= 2 || name[ len - 2 ] != '_' || !strchr( "xyz", name[ len - 1 ] ) ) {
		return qfalse;
	}
	if ( !names ) {
		return qtrue;
	}
	Q_strncpyz( base, name, MIN( len - 1, (int)sizeof( base ) ) );
	for ( i = 0; i < numNames; i++ ) {
		if ( !strcmp( names[ i ], base ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

#define MAX_LISTED_VARIABLES	512

/*
================
ListVariables

idInterpreter::ListVariables: a frame's parameters and locals and, for a
method, its object's fields and its superclasses', with their values
================
*/
static void ListVariables( interpreter_t *interpreter, int scopeDepth, dbMsg_t *msg ) {
	static const char	*names[ MAX_LISTED_VARIABLES ];
	static int			kinds[ MAX_LISTED_VARIABLES ];
	static char			values[ MAX_LISTED_VARIABLES ][ MAX_STRING_LEN + 16 ];
	const function_t	*func;
	char				funcObject[ 1024 ];
	char				*funcName;
	const varDef_t		*scope = NULL;
	const varDef_t		*scopeObj = NULL;
	const varDef_t		*def;
	int					numNames, i, j, size, count;

	if ( scopeDepth == -1 || scopeDepth == interpreter->callStackDepth ) {
		func = interpreter->currentFunction;
		scopeDepth = interpreter->callStackDepth;
	} else if ( scopeDepth >= 0 && scopeDepth < interpreter->callStackDepth ) {
		func = interpreter->callStack[ scopeDepth ].f;
	} else {
		func = NULL;
	}

	if ( func ) {
		Q_strncpyz( funcObject, func->name, sizeof( funcObject ) );
		funcName = strstr( funcObject, "::" );
		if ( funcName ) {
			*funcName = '\0';
			scopeObj = Program_GetDef( NULL, funcObject, &def_namespace );
			if ( scopeObj ) {
				scope = Program_GetDef( NULL, funcName + 2, scopeObj );
			}
		} else {
			scope = Program_GetDef( NULL, func->name, &def_namespace );
		}
	}

	numNames = 0;
	if ( scope ) {
		for ( i = 0; i < Program_NumVarDefs() && numNames < MAX_LISTED_VARIABLES; i++ ) {
			def = Program_GetVarDef( i );
			if ( def->scope == scope && VarDef_Type( def ) != ev_function && Q_isalpha( VarDef_Name( def )[ 0 ] ) ) {
				for ( j = 0; j < numNames; j++ ) {
					if ( !strcmp( names[ j ], VarDef_Name( def ) ) ) {
						break;
					}
				}
				if ( j == numNames ) {
					names[ numNames ] = VarDef_Name( def );
					kinds[ numNames++ ] = DBVAR_LOCAL;
				}
			}
		}
	}
	for ( ; scopeObj && VarDef_Type( scopeObj ) == ev_object; scopeObj = Type_SuperClass( scopeObj->typeDef ) ? Type_SuperClass( scopeObj->typeDef )->def : NULL ) {
		for ( i = 0; i < Program_NumVarDefs() && numNames < MAX_LISTED_VARIABLES; i++ ) {
			def = Program_GetVarDef( i );
			if ( def->scope != scopeObj || VarDef_Type( def ) != ev_field || !Q_isalpha( VarDef_Name( def )[ 0 ] ) ) {
				continue;
			}
			for ( j = 0; j < numNames; j++ ) {
				if ( !strcmp( names[ j ], VarDef_Name( def ) ) ) {
					break;
				}
			}
			if ( j == numNames ) {
				names[ numNames ] = VarDef_Name( def );
				kinds[ numNames++ ] = DBVAR_FIELD;
			}
		}
	}

	// a vector's components are no variables of their own
	for ( i = numNames - 1; i >= 0; i-- ) {
		if ( IsVectorComponent( names[ i ], names, numNames ) ) {
			memmove( &names[ i ], &names[ i + 1 ], ( numNames - i - 1 ) * sizeof( names[0] ) );
			memmove( &kinds[ i ], &kinds[ i + 1 ], ( numNames - i - 1 ) * sizeof( kinds[0] ) );
			numNames--;
		}
	}

	// as many as the message holds, each with its value
	size = 2;
	count = 0;
	for ( i = 0; i < numNames; i++ ) {
		if ( !Interp_GetRegisterValue( interpreter, names[ i ], values[ i ], sizeof( values[ i ] ), scopeDepth ) ) {
			Q_strncpyz( values[ i ], "???", sizeof( values[ i ] ) );
		}
		size += strlen( names[ i ] ) + 1 + strlen( values[ i ] ) + 1 + 1;
		if ( size > Msg_RemainingSpace( msg ) ) {
			break;
		}
		count++;
	}

	Msg_WriteShort( msg, count );
	for ( i = 0; i < count; i++ ) {
		Msg_WriteString( msg, names[ i ] );
		Msg_WriteString( msg, values[ i ] );
		Msg_WriteByte( msg, kinds[ i ] );
	}
}

/*
================
ListEntity

idInterpreter::ListEntity: an entity's spawn args and its script object's fields
================
*/
static void ListEntity( gentity_t *entity, dbMsg_t *msg ) {
	static const char		*names[ MAX_LISTED_VARIABLES ];
	static char				values[ MAX_LISTED_VARIABLES ][ MAX_STRING_LEN + 16 ];
	static int				kinds[ MAX_LISTED_VARIABLES ];
	const scriptObject_t	*obj;
	const typeDef_t			*type;
	const varDef_t			*def;
	int						numNames, i, size, count;

	numNames = 0;
	if ( entity ) {
		numNames = G_ScriptListSpawnArgs( entity, names, values, MAX_LISTED_VARIABLES, sizeof( values[0] ) );
		for ( i = 0; i < numNames; i++ ) {
			kinds[ i ] = DBVAR_SPAWNARG;
		}

		obj = &entity->scriptObject;
		for ( type = ScriptObject_HasObject( obj ) ? obj->type : NULL; type && type->type == ev_object && type != &type_object; type = Type_SuperClass( type ) ) {
			for ( i = 0; i < Program_NumVarDefs() && numNames < MAX_LISTED_VARIABLES; i++ ) {
				def = Program_GetVarDef( i );
				if ( def->scope != type->def || VarDef_Type( def ) != ev_field || !Q_isalpha( VarDef_Name( def )[ 0 ] ) ) {
					continue;
				}
				if ( IsVectorComponent( VarDef_Name( def ), NULL, 0 ) ) {
					continue;
				}
				names[ numNames ] = VarDef_Name( def );
				if ( !Interp_FormatValue( Type_FieldType( def->typeDef ), &obj->data[ def->value.ptrOffset ], values[ numNames ], sizeof( values[0] ) ) ) {
					Q_strncpyz( values[ numNames ], "???", sizeof( values[0] ) );
				}
				kinds[ numNames++ ] = DBVAR_FIELD;
			}
		}
	}

	// as many as the message holds
	size = 2;
	count = 0;
	for ( i = 0; i < numNames; i++ ) {
		size += strlen( names[ i ] ) + 1 + strlen( values[ i ] ) + 1 + 1;
		if ( size > Msg_RemainingSpace( msg ) ) {
			break;
		}
		count++;
	}
	Msg_WriteShort( msg, count );
	for ( i = 0; i < count; i++ ) {
		Msg_WriteString( msg, names[ i ] );
		Msg_WriteString( msg, values[ i ] );
		Msg_WriteByte( msg, kinds[ i ] );
	}
}

/*
================
HandleInspectScripts
================
*/
static void HandleInspectScripts( void ) {
	dbMsg_t	msgOut;
	byte		buffer[ MAX_MSGLEN ];

	Msg_Init( &msgOut, buffer, sizeof( buffer ) );
	Msg_WriteShort( &msgOut, DBMSG_INSPECTSCRIPTS );
	WriteScriptList( &msgOut );
	SendPacket( &msgOut );
}

/*
================
SendVersion

the protocol and what this server does of it
================
*/
static void SendVersion( void ) {
	dbMsg_t	msgOut;
	byte		buffer[ 32 ];

	Msg_Init( &msgOut, buffer, sizeof( buffer ) );
	Msg_WriteShort( &msgOut, DBMSG_VERSION );
	Msg_WriteInt( &msgOut, DEBUGGER_PROTOCOL_VERSION );
	Msg_WriteInt( &msgOut, DBCAP_STEPOUT | DBCAP_INSPECT | DBCAP_BREAKONERRORS );
	SendPacket( &msgOut );
}

/*
================
HandleAddBreakpoint
================
*/
static void HandleAddBreakpoint( dbMsg_t *msg ) {
	dbMsg_t		msgOut;
	byte			buffer[ MAX_MSGLEN ];
	breakpoint_t	*bp;
	qboolean		onceOnly;
	int				lineNumber, id;
	char			filename[ 2048 ];

	// Read the breakpoint info
	onceOnly = Msg_ReadBits( msg, 1 ) ? qtrue : qfalse;
	lineNumber = Msg_ReadInt( msg );
	id = Msg_ReadInt( msg );
	Msg_ReadString( msg, filename, sizeof( filename ) );

	// check for statement on requested breakpoint location
	if ( !IsLineCode( filename, lineNumber ) || numBreakpoints >= MAX_BREAKPOINTS ) {
		Msg_Init( &msgOut, buffer, sizeof( buffer ) );
		Msg_WriteShort( &msgOut, DBMSG_REMOVEBREAKPOINT );
		Msg_WriteInt( &msgOut, lineNumber );
		Msg_WriteString( &msgOut, filename );
		SendPacket( &msgOut );
		return;
	}

	bp = &breakpoints[ numBreakpoints++ ];
	Q_strncpyz( bp->filename, filename, sizeof( bp->filename ) );
	bp->lineNumber = lineNumber;
	bp->id = id;
	bp->onceOnly = onceOnly;
}

/*
================
RemoveBreakpoint
================
*/
static void RemoveBreakpoint( int index ) {
	memmove( &breakpoints[ index ], &breakpoints[ index + 1 ], ( numBreakpoints - index - 1 ) * sizeof( breakpoints[0] ) );
	numBreakpoints--;
}

static void HandleRemoveBreakpoint( dbMsg_t *msg ) {
	int i, id;

	// ID that we are to remove
	id = Msg_ReadInt( msg );

	// Find the breakpoint that matches the given id and remove it from the list
	for ( i = 0; i < numBreakpoints; i++ ) {
		if ( breakpoints[ i ].id == id ) {
			RemoveBreakpoint( i );
			break;
		}
	}
}

/*
================
HandleInspectCallstack
================
*/
static void HandleInspectCallstack( void ) {
	dbMsg_t	msgOut;
	byte		buffer[ MAX_MSGLEN ];

	if ( !breakInterpreter ) {
		return;
	}
	Msg_Init( &msgOut, buffer, sizeof( buffer ) );
	Msg_WriteShort( &msgOut, DBMSG_INSPECTCALLSTACK );
	WriteInterpreterInfo( &msgOut, breakInterpreter, breakInstructionPointer );
	SendPacket( &msgOut );
}

/*
================
HandleInspectThreads
================
*/
static void HandleInspectThreads( void ) {
	dbMsg_t		msgOut;
	byte			buffer[ MAX_MSGLEN ];
	scriptThread_t	*thread;
	int				i;

	Msg_Init( &msgOut, buffer, sizeof( buffer ) );
	Msg_WriteShort( &msgOut, DBMSG_INSPECTTHREADS );

	// Write the number of threads to the message
	Msg_WriteShort( &msgOut, Thread_NumThreads() );

	// Loop through all of the threads and write their name and number to the message
	for ( i = 0; i < Thread_NumThreads(); i++ ) {
		thread = Thread_GetThreadByIndex( i );
		Msg_WriteString( &msgOut, thread->threadName );
		Msg_WriteInt( &msgOut, thread->threadNum );
		Msg_WriteBits( &msgOut, breakInterpreter && thread == breakInterpreter->thread, 1 );
		Msg_WriteBits( &msgOut, thread->interpreter.doneProcessing, 1 );
		Msg_WriteBits( &msgOut, Thread_IsWaiting( thread ), 1 );
		Msg_WriteBits( &msgOut, thread->interpreter.threadDying, 1 );
	}

	// Send off the inspect threads packet to the debugger client
	SendPacket( &msgOut );
}

/*
================
HandleInspectVariable
================
*/
static void HandleInspectVariable( dbMsg_t *msg ) {
	dbMsg_t	msgOut;
	byte		buffer[ MAX_MSGLEN ];
	char		varname[ 256 ];
	char		varvalue[ MAX_STRING_CHARS ];
	int			scopeDepth;

	if ( !breakNow ) {
		return;
	}

	scopeDepth = Msg_ReadShort( msg );
	Msg_ReadString( msg, varname, sizeof( varname ) );

	if ( !Interp_GetRegisterValue( breakInterpreter, varname, varvalue, sizeof( varvalue ), scopeDepth ) ) {
		Q_strncpyz( varvalue, "???", sizeof( varvalue ) );
	}

	Msg_Init( &msgOut, buffer, sizeof( buffer ) );
	Msg_WriteShort( &msgOut, DBMSG_INSPECTVARIABLE );
	Msg_WriteShort( &msgOut, scopeDepth );
	Msg_WriteString( &msgOut, varname );
	Msg_WriteString( &msgOut, varvalue );
	SendPacket( &msgOut );
}

/*
================
HandleExecCommand
================
*/
static void HandleExecCommand( dbMsg_t *msg ) {
	char cmdStr[ 2048 ];

	Msg_ReadString( msg, cmdStr, sizeof( cmdStr ) );
	trap_Cmd_ExecuteText( EXEC_APPEND, va( "%s\n", cmdStr ) );
}

/*
================
HandleGameRequest

what dhewm3's game answers (Game_local.cpp's DebuggerRequest), while stopped
================
*/
static void HandleGameRequest( int dbmsg, dbMsg_t *in ) {
	dbMsg_t		msgOut;
	byte			buffer[ MAX_MSGLEN ];
	interpreter_t	*interpreter;
	char			name[ MAX_STRING_CHARS ];
	char			value[ MAX_STRING_CHARS ];
	char			result[ MAX_STRING_CHARS ];
	int				number, scopeDepth, line;
	qboolean		set;

	if ( !breakNow ) {
		return;
	}

	Msg_Init( &msgOut, buffer, sizeof( buffer ) );
	Msg_WriteShort( &msgOut, dbmsg );

	switch ( dbmsg ) {
		case DBMSG_INSPECTVARIABLES:
			number = Msg_ReadInt( in );
			scopeDepth = Msg_ReadShort( in );
			Msg_WriteInt( &msgOut, number );
			Msg_WriteShort( &msgOut, scopeDepth );
			interpreter = Debugger_FindThreadInterpreter( number );
			if ( !interpreter ) {
				Msg_WriteShort( &msgOut, 0 );
			} else {
				ListVariables( interpreter, scopeDepth, &msgOut );
			}
			break;

		case DBMSG_INSPECTTHREADCALLSTACK:
			number = Msg_ReadInt( in );
			Msg_WriteInt( &msgOut, number );
			interpreter = Debugger_FindThreadInterpreter( number );
			if ( !interpreter ) {
				Msg_WriteShort( &msgOut, 0 );
			} else {
				WriteInterpreterInfo( &msgOut, interpreter, interpreter->instructionPointer );
			}
			break;

		case DBMSG_INSPECTENTITY:
			Msg_ReadString( in, name, sizeof( name ) );
			Msg_WriteString( &msgOut, name );
			ListEntity( G_ScriptFindEntity( name[ 0 ] == '$' ? name + 1 : name ), &msgOut );
			break;

		case DBMSG_SETVARIABLE:
			number = Msg_ReadInt( in );
			scopeDepth = Msg_ReadShort( in );
			Msg_ReadString( in, name, sizeof( name ) );
			Msg_ReadString( in, value, sizeof( value ) );
			interpreter = Debugger_FindThreadInterpreter( number );
			set = qfalse;
			if ( !interpreter ) {
				Q_strncpyz( result, "no such thread", sizeof( result ) );
			} else {
				set = Interp_SetRegisterValue( interpreter, name, value, result, sizeof( result ), scopeDepth );
			}
			Msg_WriteInt( &msgOut, number );
			Msg_WriteShort( &msgOut, scopeDepth );
			Msg_WriteString( &msgOut, name );
			Msg_WriteByte( &msgOut, set ? 1 : 0 );
			Msg_WriteString( &msgOut, result );
			break;

		case DBMSG_SETNEXTSTATEMENT:
			line = Msg_ReadInt( in );
			set = Interp_SetNextStatementLine( breakInterpreter, line );
			Msg_WriteInt( &msgOut, set ? line : 0 );
			Msg_WriteInt( &msgOut, breakInterpreter->instructionPointer );
			if ( set ) {
				// the stopped thread goes on from there: where a step begins
				breakInstructionPointer = breakInterpreter->instructionPointer;
				lastStatementLine = line;
				Q_strncpyz( lastStatementFile, FileNameForStatement( breakInstructionPointer ), sizeof( lastStatementFile ) );
			}
			break;

		default:
			return;
	}

	SendPacket( &msgOut );
}

/*
================
BeginStep
================
*/
static void BeginStep( void ) {
	stepInterpreter = breakInterpreter;
	Q_strncpyz( stepFile, FileNameForStatement( breakInstructionPointer ), sizeof( stepFile ) );
	stepLine = Program_GetLineNumberForStatement( breakInstructionPointer );
	stepDepth = breakInterpreter ? breakInterpreter->callStackDepth : 0;
}

/*
================
Resume
================
*/
static void Resume( void ) {
	breakNow = qfalse;
}

/*
================
ProcessMessages

the packets of the client; with wait, the first may take that many msec to come
================
*/
static void ProcessMessages( int wait ) {
	struct sockaddr_in	from;
	socklen_t			fromLen;
	struct timeval		tv;
	fd_set				fds;
	dbMsg_t			msg;
	byte				buffer[ MAX_MSGLEN ];
	int					size, command;

	if ( sock < 0 ) {
		return;
	}

	while ( 1 ) {
		FD_ZERO( &fds );
		FD_SET( sock, &fds );
		tv.tv_sec = 0;
		tv.tv_usec = wait * 1000;
		if ( select( sock + 1, &fds, NULL, NULL, &tv ) <= 0 ) {
			return;
		}
		wait = 0;

		fromLen = sizeof( from );
		size = recvfrom( sock, (char *)buffer, sizeof( buffer ), 0, (struct sockaddr *)&from, &fromLen );
		if ( size < 2 ) {
			continue;
		}

		// Only accept packets from the debugger client (or this machine) for security reasons
		if ( from.sin_addr.s_addr != clientAdr.sin_addr.s_addr && from.sin_addr.s_addr != htonl( INADDR_LOOPBACK ) ) {
			continue;
		}

		Msg_Init( &msg, buffer, sizeof( buffer ) );
		msg.curSize = size;
		command = Msg_ReadShort( &msg );

		switch ( command ) {
			case DBMSG_CONNECT:
				connected = qtrue;
				SendMessage( DBMSG_CONNECTED );
				SendVersion();
				HandleInspectScripts();
				break;

			case DBMSG_CONNECTED:
				connected = qtrue;
				SendVersion();
				HandleInspectScripts();
				break;

			case DBMSG_DISCONNECT:
				numBreakpoints = 0;
				breakOnErrors = 0;
				Resume();
				connected = qfalse;
				break;

			case DBMSG_ADDBREAKPOINT:
				HandleAddBreakpoint( &msg );
				break;

			case DBMSG_REMOVEBREAKPOINT:
				HandleRemoveBreakpoint( &msg );
				break;

			case DBMSG_RESUME:
				Resume();
				break;

			case DBMSG_BREAK:
				breakNext = qtrue;
				break;

			case DBMSG_STEPOVER:
				BeginStep();
				breakStepOver = qtrue;
				breakStepOverDepth = breakInterpreter ? breakInterpreter->callStackDepth : 0;
				breakStepOverFunc1 = breakInterpreter ? breakInterpreter->currentFunction : NULL;
				if ( breakStepOverDepth ) {
					breakStepOverFunc2 = breakInterpreter->callStack[ breakStepOverDepth - 1 ].f;
				} else {
					breakStepOverFunc2 = NULL;
				}
				Resume();
				break;

			case DBMSG_STEPINTO:
				BeginStep();
				breakStepInto = qtrue;
				Resume();
				break;

			case DBMSG_STEPOUT:
				BeginStep();
				breakStepOut = qtrue;
				Resume();
				break;

			case DBMSG_BREAKONERRORS:
				breakOnErrors = Msg_ReadInt( &msg );
				break;

			// what needs the scripts' internals
			case DBMSG_INSPECTVARIABLES:
			case DBMSG_INSPECTTHREADCALLSTACK:
			case DBMSG_INSPECTENTITY:
			case DBMSG_SETVARIABLE:
			case DBMSG_SETNEXTSTATEMENT:
				HandleGameRequest( command, &msg );
				break;

			case DBMSG_INSPECTVARIABLE:
				HandleInspectVariable( &msg );
				break;

			case DBMSG_INSPECTCALLSTACK:
				HandleInspectCallstack();
				break;

			case DBMSG_INSPECTTHREADS:
				HandleInspectThreads();
				break;

			case DBMSG_INSPECTSCRIPTS:
				HandleInspectScripts();
				break;

			case DBMSG_EXECCOMMAND:
				HandleExecCommand( &msg );
				break;
		}
	}
}

/*
================
Break

stops the thread there until the client resumes it: the game waits here,
answering the client
================
*/
static void Break( interpreter_t *interpreter, int instructionPointer ) {
	dbMsg_t	msg;
	byte		buffer[ MAX_MSGLEN ];

	// Clear all the break types
	breakStepOver = qfalse;
	breakStepInto = qfalse;
	breakStepOut = qfalse;
	breakNext = qfalse;

	// Set the break variable so we know the main thread is stopped
	breakNow = qtrue;
	breakInterpreter = interpreter;
	breakInstructionPointer = instructionPointer;

	// Inform the debugger of the breakpoint hit
	Msg_Init( &msg, buffer, sizeof( buffer ) );
	Msg_WriteShort( &msg, DBMSG_BREAK );
	Msg_WriteInt( &msg, Program_GetLineNumberForStatement( instructionPointer ) );
	Msg_WriteString( &msg, FileNameForStatement( instructionPointer ) );
	SendPacket( &msg );

	// the game waits for the client to resume it
	while ( breakNow && connected ) {
		ProcessMessages( 100 );
	}
	breakNow = qfalse;

	// Let the debugger client know that we have started back up again
	SendMessage( DBMSG_RESUMED );
}

/*
================
ScriptDebug_Update

dhewm3's rvDebuggerServer::CheckBreakpoints, before each statement; qtrue
when a debugger is connected
================
*/
qboolean ScriptDebug_Update( interpreter_t *interpreter, int instructionPointer ) {
	const char	*filename;
	int			linenumber, depth, i;

	if ( !connected ) {
		return qfalse;
	}

	// Grab the current statement and the filename that it came from
	filename = FileNameForStatement( instructionPointer );
	linenumber = Program_GetLineNumberForStatement( instructionPointer );

	// A step: only the thread it began in ends it, on another line than the
	// one it began on (other threads run meanwhile, and the thread may wait)
	if ( ( breakStepOver || breakStepInto || breakStepOut ) && interpreter == stepInterpreter ) {
		depth = interpreter->callStackDepth;
		if ( Program_GetStatement( instructionPointer )->op == OP_RETURN && depth <= 1 ) {
			// the thread returns from its last function: nothing left to step to
			breakStepOver = qfalse;
			breakStepInto = qfalse;
			breakStepOut = qfalse;
		} else if ( linenumber != stepLine || depth != stepDepth || strcmp( stepFile, filename ) ) {
			if ( breakStepInto
				|| ( breakStepOver && ( interpreter->currentFunction == breakStepOverFunc1 || interpreter->currentFunction == breakStepOverFunc2 ) && depth <= breakStepOverDepth )
				|| ( breakStepOut && depth < stepDepth ) ) {
				Q_strncpyz( lastStatementFile, filename, sizeof( lastStatementFile ) );
				lastStatementLine = linenumber;
				HandleInspectScripts();
				Break( interpreter, instructionPointer );
				return qtrue;
			}
		}
	}

	// back from a call into the rest of the line that called it: not a line
	// reached anew (its breakpoint would stop the thread twice)
	if ( interpreter == lastInterpreter && interpreter->callStackDepth < lastDepth && instructionPointer > 0
		&& Program_GetStatement( instructionPointer - 1 )->linenumber == linenumber
		&& Program_GetStatement( instructionPointer - 1 )->file == Program_GetStatement( instructionPointer )->file ) {
		Q_strncpyz( lastStatementFile, filename, sizeof( lastStatementFile ) );
		lastStatementLine = linenumber;
	}
	lastInterpreter = interpreter;
	lastDepth = interpreter->callStackDepth;

	// Operate on lines, not statements
	if ( lastStatementLine == linenumber && !strcmp( lastStatementFile, filename ) ) {
		return qtrue;
	}

	// Save the last visited line and file so we can prevent
	// double breaks on lines with more than one statement
	Q_strncpyz( lastStatementFile, filename, sizeof( lastStatementFile ) );
	lastStatementLine = linenumber;

	// See if we are supposed to break on the next script line
	if ( breakNext ) {
		HandleInspectScripts();
		Break( interpreter, instructionPointer );
		return qtrue;
	}

	// Check all the breakpoints
	for ( i = 0; i < numBreakpoints; i++ ) {
		breakpoint_t *bp = &breakpoints[ i ];

		if ( linenumber != bp->lineNumber || Q_stricmp( bp->filename, filename ) ) {
			continue;
		}

		if ( bp->onceOnly ) {
			dbMsg_t	msgOut;
			byte		buffer[ MAX_MSGLEN ];

			// also tell client to remove the breakpoint
			Msg_Init( &msgOut, buffer, sizeof( buffer ) );
			Msg_WriteShort( &msgOut, DBMSG_REMOVEBREAKPOINT );
			Msg_WriteInt( &msgOut, linenumber );
			Msg_WriteString( &msgOut, filename );
			SendPacket( &msgOut );
			RemoveBreakpoint( i );
		}

		HandleInspectScripts();

		// We hit a breakpoint, so break
		Break( interpreter, instructionPointer );
		break;
	}

	return qtrue;
}

/*
================
ScriptDebug_ScriptError

a script's error or warning: the client hears of it and, if it asked to,
the thread stops there before the game handles it
================
*/
void ScriptDebug_ScriptError( interpreter_t *interpreter, int instructionPointer, const char *message, qboolean isError ) {
	dbMsg_t	msgOut;
	byte		buffer[ MAX_MSGLEN ];

	if ( !connected || !( breakOnErrors & ( isError ? DBERR_ERRORS : DBERR_WARNINGS ) ) ) {
		return;
	}

	Msg_Init( &msgOut, buffer, sizeof( buffer ) );
	Msg_WriteShort( &msgOut, DBMSG_SCRIPTERROR );
	Msg_WriteByte( &msgOut, isError ? 1 : 0 );
	Msg_WriteString( &msgOut, message );
	SendPacket( &msgOut );

	HandleInspectScripts();
	Break( interpreter, instructionPointer );
}

#define DEBUGGER_STATE_FILE		"debugger.state"

/*
================
SaveState / LoadState

the connection and the breakpoints from one map to the next: the game (and
so this server) starts anew with each map, dhewm3's server lives on
================
*/
static void SaveState( void ) {
	fileHandle_t	f;
	const char		*s;
	int				i;

	if ( trap_FS_FOpenFile( DEBUGGER_STATE_FILE, &f, FS_WRITE ) < 0 || !f ) {
		return;
	}
	s = va( "connected %d %d\n", connected, breakOnErrors );
	trap_FS_Write( s, strlen( s ), f );
	for ( i = 0; connected && i < numBreakpoints; i++ ) {
		s = va( "bp %d %d %d %s\n", breakpoints[ i ].id, breakpoints[ i ].lineNumber, breakpoints[ i ].onceOnly, breakpoints[ i ].filename );
		trap_FS_Write( s, strlen( s ), f );
	}
	trap_FS_FCloseFile( f );
}

static void LoadState( void ) {
	fileHandle_t	f;
	char			text[ 32768 ];
	char			*line, *next;
	breakpoint_t	*bp;
	int				len, wasConnected, onceOnly;

	len = trap_FS_FOpenFile( DEBUGGER_STATE_FILE, &f, FS_READ );
	if ( len <= 0 || !f ) {
		return;
	}
	len = MIN( len, (int)sizeof( text ) - 1 );
	trap_FS_Read( text, len, f );
	trap_FS_FCloseFile( f );
	text[ len ] = '\0';

	for ( line = text; line && *line; line = next ) {
		next = strchr( line, '\n' );
		if ( next ) {
			*next++ = '\0';
		}
		if ( sscanf( line, "connected %d %d", &wasConnected, &breakOnErrors ) == 2 ) {
			connected = wasConnected;
		} else if ( !strncmp( line, "bp ", 3 ) && numBreakpoints < MAX_BREAKPOINTS ) {
			bp = &breakpoints[ numBreakpoints ];
			if ( sscanf( line, "bp %d %d %d %63s", &bp->id, &bp->lineNumber, &onceOnly, bp->filename ) == 4 ) {
				bp->onceOnly = onceOnly;
				numBreakpoints++;
			}
		}
	}
}

/*
================
ScriptDebug_Init

as the game starts: the server's port, and a word to the client, which
may be there already (dhewm3's rvDebuggerServer::Initialize)
================
*/
void ScriptDebug_Init( void ) {
	struct sockaddr_in	adr;
	struct addrinfo		hints, *res;
	int					one = 1;

	trap_Cvar_Register( &com_enableDebuggerServer, "com_enableDebuggerServer", "0", CVAR_ARCHIVE );
	trap_Cvar_Register( &com_dbgClientAdr, "com_dbgClientAdr", "localhost", CVAR_ARCHIVE );

	if ( !com_enableDebuggerServer.integer || sock >= 0 ) {
		return;
	}

	sock = socket( AF_INET, SOCK_DGRAM, 0 );
	if ( sock < 0 ) {
		G_Printf( S_COLOR_YELLOW "WARNING: the script debugger's server has no socket\n" );
		return;
	}
	setsockopt( sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof( one ) );
	fcntl( sock, F_SETFL, fcntl( sock, F_GETFL, 0 ) | O_NONBLOCK );

	memset( &adr, 0, sizeof( adr ) );
	adr.sin_family = AF_INET;
	adr.sin_addr.s_addr = htonl( INADDR_ANY );
	adr.sin_port = htons( DEBUGGER_SERVER_PORT );
	if ( bind( sock, (struct sockaddr *)&adr, sizeof( adr ) ) < 0 ) {
		G_Printf( S_COLOR_YELLOW "WARNING: the script debugger's server can't have port %d\n", DEBUGGER_SERVER_PORT );
		close( sock );
		sock = -1;
		return;
	}

	// the client, on its port
	memset( &clientAdr, 0, sizeof( clientAdr ) );
	clientAdr.sin_family = AF_INET;
	clientAdr.sin_port = htons( DEBUGGER_CLIENT_PORT );
	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if ( getaddrinfo( com_dbgClientAdr.string, NULL, &hints, &res ) == 0 && res ) {
		clientAdr.sin_addr = ( (struct sockaddr_in *)res->ai_addr )->sin_addr;
		freeaddrinfo( res );
	} else {
		clientAdr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
	}

	G_Printf( "Script debugger server on port %d, client %s:%d\n", DEBUGGER_SERVER_PORT, com_dbgClientAdr.string, DEBUGGER_CLIENT_PORT );

	// the last map's connection and breakpoints
	connected = qfalse;
	numBreakpoints = 0;
	LoadState();
	if ( connected ) {
		// the client is there: this map's scripts
		SendVersion();
		HandleInspectScripts();
	} else {
		// Attempt to let the client know we are here.  The client may not be running so this
		// message will just get ignored.
		SendMessage( DBMSG_CONNECT );
	}
}

/*
================
ScriptDebug_Frame

the client's packets, each frame (dhewm3 reads them in a thread of their own)
================
*/
void ScriptDebug_Frame( void ) {
	ProcessMessages( 0 );
}

/*
================
ScriptDebug_Shutdown

as the game shuts down (a map change too): the client stays connected, and
hears from the next map's server as it starts
================
*/
void ScriptDebug_Shutdown( void ) {
	if ( sock >= 0 ) {
		SaveState();
		close( sock );
		sock = -1;
	}
	connected = qfalse;
	breakNow = qfalse;
	breakInterpreter = NULL;
	stepInterpreter = NULL;
	lastInterpreter = NULL;
	numBreakpoints = 0;
}

#endif
