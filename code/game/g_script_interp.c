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
// g_script_interp.c -- Doom 3's Script_Interpreter.cpp: a thread's stack, and
// the statements it executes. With the script debugger's changes of dhewm3's
// script-debugger-protocol: a caller's variables at its own frame, objects as
// their entities, setting a variable, going on from another line.

#include "g_local.h"

/*
================
the stack
================
*/
static ID_INLINE void PopParms( interpreter_t *self, int numParms ) {
	// pop our parms off the stack
	if ( self->localstackUsed < numParms ) {
		Interp_Error( self, "locals stack underflow\n" );
	}

	self->localstackUsed -= numParms;
}

static ID_INLINE void Push( interpreter_t *self, int value ) {
	if ( self->localstackUsed + (int)sizeof( intptr_t ) > LOCALSTACK_SIZE ) {
		Interp_Error( self, "Push: locals stack overflow\n" );
	}
	// an int at the start of a slot of a pointer's size, as dhewm3 does
	*( int * )&self->localstack[ self->localstackUsed ] = value;
	self->localstackUsed += sizeof( intptr_t );
}

static ID_INLINE void PushVector( interpreter_t *self, const float *vector ) {
	if ( self->localstackUsed + (int)E_EVENT_SIZEOF_VEC > LOCALSTACK_SIZE ) {
		Interp_Error( self, "Push: locals stack overflow\n" );
	}
	VectorCopy( vector, ( float * )&self->localstack[ self->localstackUsed ] );
	self->localstackUsed += E_EVENT_SIZEOF_VEC;
}

static ID_INLINE void PushString( interpreter_t *self, const char *string ) {
	if ( self->localstackUsed + MAX_STRING_LEN > LOCALSTACK_SIZE ) {
		Interp_Error( self, "PushString: locals stack overflow\n" );
	}
	Q_strncpyz( ( char * )&self->localstack[ self->localstackUsed ], string, MAX_STRING_LEN );
	self->localstackUsed += MAX_STRING_LEN;
}

static ID_INLINE const char *FloatToString( float value ) {
	static char	text[ 32 ];

	if ( value == ( float )( int )value ) {
		Com_sprintf( text, sizeof( text ), "%d", ( int )value );
	} else {
		Com_sprintf( text, sizeof( text ), "%f", value );
	}
	return text;
}

static ID_INLINE const char *VectorToString( const float *v ) {
	static char	text[ 64 ];

	// idVec3::ToString()
	Com_sprintf( text, sizeof( text ), "%.2f %.2f %.2f", v[ 0 ], v[ 1 ], v[ 2 ] );
	return text;
}

static ID_INLINE void AppendString( interpreter_t *self, varDef_t *def, const char *from ) {
	if ( def->initialized == DEF_STACKVARIABLE ) {
		Q_strcat( ( char * )&self->localstack[ self->localstackBase + def->value.stackOffset ], MAX_STRING_LEN, from );
	} else {
		Q_strcat( def->value.stringPtr, MAX_STRING_LEN, from );
	}
}

static ID_INLINE void SetString( interpreter_t *self, varDef_t *def, const char *from ) {
	if ( def->initialized == DEF_STACKVARIABLE ) {
		Q_strncpyz( ( char * )&self->localstack[ self->localstackBase + def->value.stackOffset ], from, MAX_STRING_LEN );
	} else {
		Q_strncpyz( def->value.stringPtr, from, MAX_STRING_LEN );
	}
}

static ID_INLINE const char *GetString( interpreter_t *self, varDef_t *def ) {
	if ( def->initialized == DEF_STACKVARIABLE ) {
		return ( char * )&self->localstack[ self->localstackBase + def->value.stackOffset ];
	} else {
		return def->value.stringPtr;
	}
}

static ID_INLINE varEval_t GetVariable( interpreter_t *self, varDef_t *def ) {
	if ( def->initialized == DEF_STACKVARIABLE ) {
		varEval_t val;
		val.intPtr = ( int * )&self->localstack[ self->localstackBase + def->value.stackOffset ];
		return val;
	} else {
		return def->value;
	}
}

/*
================
Interp_GetEntity

the entity of a number as the scripts hold it: its number + 1, 0 none
================
*/
gentity_t *Interp_GetEntity( int entnum ) {
	assert( entnum <= MAX_GENTITIES );
	if ( ( entnum > 0 ) && ( entnum <= MAX_GENTITIES ) && ( g_entities[ entnum - 1 ].inuse || entnum - 1 == ENTITYNUM_WORLD ) ) {
		return &g_entities[ entnum - 1 ];
	}
	return NULL;
}

int Interp_EntityNumber( const gentity_t *ent ) {
	if ( !ent ) {
		return 0;
	}
	return ent - g_entities + 1;
}

/*
================
Interp_PushEntity
================
*/
void Interp_PushEntity( interpreter_t *self, gentity_t *ent ) {
	Push( self, Interp_EntityNumber( ent ) );
}

/*
================
Interp_GetScriptObject
================
*/
scriptObject_t *Interp_GetScriptObject( int entnum ) {
	gentity_t *ent;

	ent = Interp_GetEntity( entnum );
	if ( ent && ent->scriptObject.data ) {
		return &ent->scriptObject;
	}
	return NULL;
}

static ID_INLINE void NextInstruction( interpreter_t *self, int position ) {
	// Before we execute an instruction, we increment instructionPointer,
	// therefore we need to compensate for that here.
	self->instructionPointer = position - 1;
}

/*
============
Interp_Init
============
*/
void Interp_Init( interpreter_t *self ) {
	memset( self, 0, sizeof( *self ) );
	self->terminateOnExit = qtrue;
	Interp_Reset( self );
}

/*
============
Interp_Reset
============
*/
void Interp_Reset( interpreter_t *self ) {
	self->callStackDepth = 0;
	self->localstackUsed = 0;
	self->localstackBase = 0;

	self->maxLocalstackUsed = 0;
	self->maxStackDepth = 0;

	self->popParms = 0;
	self->multiFrameEvent = NULL;
	self->eventEntity = NULL;

	self->currentFunction = 0;
	NextInstruction( self, 0 );

	self->threadDying 	= qfalse;
	self->doneProcessing	= qtrue;
}

/*
============
Interp_FindFrameDef

the variable of a name a frame of the function sees: its parameter or
local, or a field of its object or its superclasses'
============
*/
const varDef_t *Interp_FindFrameDef( const function_t *func, const char *name, qboolean *inNamespace ) {
	varDef_t		*d;
	char			funcObject[ 1024 ];
	char			*funcName;
	const varDef_t	*scope = NULL;
	const varDef_t	*scopeObj;

	Q_strncpyz( funcObject, func->name, sizeof( funcObject ) );
	funcName = strstr( funcObject, "::" );
	if ( funcName ) {
		*funcName = '\0';
		scopeObj = Program_GetDef( NULL, funcObject, &def_namespace );
		funcName += 2;
		if ( scopeObj ) {
			scope = Program_GetDef( NULL, funcName, scopeObj );
		}
	} else {
		funcName = funcObject;
		scope = Program_GetDef( NULL, func->name, &def_namespace );
		scopeObj = NULL;
	}

	if ( !scope ) {
		return NULL;
	}

	d = Program_GetDef( NULL, name, scope );

	// Check the objects for it if it wasnt local to the function
	if ( !d ) {
		for ( ; scopeObj && VarDef_Type( scopeObj ) == ev_object && Type_SuperClass( scopeObj->typeDef ); scopeObj = Type_SuperClass( scopeObj->typeDef )->def ) {
			d = Program_GetDef( NULL, name, scopeObj );
			if ( d ) {
				break;
			}
		}
	}

	*inNamespace = ( scope == &def_namespace );
	return d;
}

/*
============
Interp_FormatValue

a value as the script debugger shows it
============
*/
qboolean Interp_FormatValue( const typeDef_t *type, const byte *ptr, char *out, int outSize ) {
	const float	*v;
	gentity_t	*ent;

	switch ( type->type ) {
		case ev_boolean:
			Com_sprintf( out, outSize, "%d", *( const int * )ptr );
			return qtrue;
		case ev_float:
			Com_sprintf( out, outSize, "%g", *( const float * )ptr );
			return qtrue;
		case ev_vector:
			v = ( const float * )ptr;
			Com_sprintf( out, outSize, "%g,%g,%g", v[ 0 ], v[ 1 ], v[ 2 ] );
			return qtrue;
		case ev_object:		// an object: the entity it is the script object of
		case ev_entity:
			ent = Interp_GetEntity( *( const int * )ptr );
			if ( ent ) {
				Com_sprintf( out, outSize, "$%s", ent->targetname ? ent->targetname : ent->classname );
			} else {
				Q_strncpyz( out, "$null_entity", outSize );
			}
			return qtrue;
		case ev_string:
			Com_sprintf( out, outSize, "\"%s\"", ( const char * )ptr );
			return qtrue;
		default:
			return qfalse;
	}
}

/*
============
Interp_ParseValue

a value from how Interp_FormatValue writes it
============
*/
qboolean Interp_ParseValue( const typeDef_t *type, const char *text, byte *ptr, char *why, int whySize ) {
	char		value[ MAX_STRING_LEN * 2 ];
	char		*s, *e;
	float		x, y, z;
	char		rest[ 2 ];
	gentity_t	*ent;
	int			len;

	// trimmed
	while ( *text == ' ' ) {
		text++;
	}
	Q_strncpyz( value, text, sizeof( value ) );
	for ( e = value + strlen( value ) - 1; e >= value && *e == ' '; e-- ) {
		*e = '\0';
	}

	switch ( type->type ) {
		case ev_boolean:
			if ( !strcmp( value, "true" ) || !strcmp( value, "1" ) ) {
				*( int * )ptr = 1;
			} else if ( !strcmp( value, "false" ) || !strcmp( value, "0" ) ) {
				*( int * )ptr = 0;
			} else {
				Q_strncpyz( why, "not a boolean (1, 0, true or false)", whySize );
				return qfalse;
			}
			return qtrue;
		case ev_float:
			if ( sscanf( value, "%f %1s", &x, rest ) != 1 ) {
				Q_strncpyz( why, "not a number", whySize );
				return qfalse;
			}
			*( float * )ptr = x;
			return qtrue;
		case ev_vector:
			for ( s = value; *s; s++ ) {
				if ( *s == ',' ) {
					*s = ' ';
				}
			}
			if ( sscanf( value, "%f %f %f %1s", &x, &y, &z, rest ) != 3 ) {
				Q_strncpyz( why, "not a vector (x,y,z)", whySize );
				return qfalse;
			}
			VectorSet( ( float * )ptr, x, y, z );
			return qtrue;
		case ev_entity:
			s = value;
			if ( *s == '$' ) {
				s++;
			}
			if ( !*s || !strcmp( s, "null_entity" ) || !strcmp( s, "null" ) ) {
				*( int * )ptr = 0;
				return qtrue;
			}
			ent = G_ScriptFindEntity( s );
			if ( !ent ) {
				Com_sprintf( why, whySize, "no entity %s", s );
				return qfalse;
			}
			*( int * )ptr = Interp_EntityNumber( ent );
			return qtrue;
		case ev_string:
			len = strlen( value );
			if ( len >= 2 && value[ 0 ] == '"' && value[ len - 1 ] == '"' ) {
				value[ len - 1 ] = '\0';
				Q_strncpyz( ( char * )ptr, value + 1, MAX_STRING_LEN );
			} else {
				Q_strncpyz( ( char * )ptr, value, MAX_STRING_LEN );
			}
			return qtrue;
		default:
			Q_strncpyz( why, "its type can't be set", whySize );
			return qfalse;
	}
}

/*
============
GetFrameRegisterValue

a variable of the frame whose stack base localstackBase is
============
*/
static qboolean GetFrameRegisterValue( interpreter_t *self, const function_t *func, const char *name, char *out, int outSize ) {
	varEval_t		reg;
	const varDef_t	*d;
	qboolean		inNamespace;
	const typeDef_t	*field;
	gentity_t		*entity;

	out[ 0 ] = '\0';

	d = Interp_FindFrameDef( func, name, &inNamespace );
	if ( !d ) {
		Q_strncpyz( out, "???", outSize );
		return qfalse;
	}

	reg = GetVariable( self, ( varDef_t * )d );
	switch( VarDef_Type( d ) ) {
	case ev_float:
		if ( reg.floatPtr ) {
			Com_sprintf( out, outSize, "%g", *reg.floatPtr );
		} else {
			Q_strncpyz( out, "0", outSize );
		}
		return qtrue;

	case ev_vector:
		if ( reg.vectorPtr ) {
			Com_sprintf( out, outSize, "%g,%g,%g", reg.vectorPtr[ 0 ], reg.vectorPtr[ 1 ], reg.vectorPtr[ 2 ] );
		} else {
			Q_strncpyz( out, "0,0,0", outSize );
		}
		return qtrue;

	case ev_boolean:
		if ( reg.intPtr ) {
			Com_sprintf( out, outSize, "%d", *reg.intPtr );
		} else {
			Q_strncpyz( out, "0", outSize );
		}
		return qtrue;

	case ev_field:
		if ( inNamespace ) {
			// should never happen, but handle it safely anyway
			return qfalse;
		}

		field  = Type_FieldType( d->typeDef );
		entity = Interp_GetEntity( *( ( int * )&self->localstack[ self->localstackBase ] ) );
		if ( !entity || !field || !entity->scriptObject.data ) {
			return qfalse;
		}
		return Interp_FormatValue( field, &entity->scriptObject.data[ reg.ptrOffset ], out, outSize );

	case ev_object:		// an object (self): the entity it is the script object of
	case ev_entity:
		if ( reg.entityNumberPtr ) {
			return Interp_FormatValue( &type_entity, ( byte * )reg.entityNumberPtr, out, outSize );
		}
		Q_strncpyz( out, "$null_entity", outSize );
		return qtrue;

	case ev_string:
		if ( reg.stringPtr ) {
			Com_sprintf( out, outSize, "\"%s\"", reg.stringPtr );
		} else {
			Q_strncpyz( out, "\"\"", outSize );
		}
		return qtrue;

	default:
		return qfalse;
	}
}

/*
============
Interp_GetRegisterValue

Returns a string representation of the value of the register with the
specified name, at the frame scopeDepth of the call stack (-1 the function
executing)
============
*/
qboolean Interp_GetRegisterValue( interpreter_t *self, const char *name, char *out, int outSize, int scopeDepth ) {
	const function_t	*func;
	int					savedStackBase;
	qboolean			found;

	out[ 0 ] = '\0';

	if ( scopeDepth == -1 ) {
		scopeDepth = self->callStackDepth;
	}

	if ( scopeDepth == self->callStackDepth ) {
		func = self->currentFunction;
	} else if ( scopeDepth >= 0 && scopeDepth < self->callStackDepth ) {
		func = self->callStack[ scopeDepth ].f;
	} else {
		func = NULL;
	}
	if ( !func ) {
		return qfalse;
	}

	// a caller's locals (and its self) are on its own part of the stack
	savedStackBase = self->localstackBase;
	if ( scopeDepth != self->callStackDepth ) {
		self->localstackBase = self->callStack[ scopeDepth ].stackbase;
	}
	found = GetFrameRegisterValue( self, func, name, out, outSize );
	self->localstackBase = savedStackBase;
	return found;
}

/*
============
Interp_SetRegisterValue

the script debugger's: sets a variable of a frame from its value as
Interp_GetRegisterValue writes it, and writes it back in out (else why not)
============
*/
qboolean Interp_SetRegisterValue( interpreter_t *self, const char *name, const char *value, char *out, int outSize, int scopeDepth ) {
	const function_t	*func;
	const varDef_t		*d;
	qboolean			inNamespace;
	qboolean			set = qfalse;
	int					depth = scopeDepth;
	int					savedStackBase;

	if ( depth == -1 ) {
		depth = self->callStackDepth;
	}
	if ( depth < 0 || depth > self->callStackDepth ) {
		Q_strncpyz( out, "no such frame", outSize );
		return qfalse;
	}
	func = ( depth == self->callStackDepth ) ? self->currentFunction : self->callStack[ depth ].f;
	if ( !func ) {
		Q_strncpyz( out, "no such frame", outSize );
		return qfalse;
	}

	// a caller's locals (and its self) are on its own part of the stack
	savedStackBase = self->localstackBase;
	if ( depth != self->callStackDepth ) {
		self->localstackBase = self->callStack[ depth ].stackbase;
	}

	d = Interp_FindFrameDef( func, name, &inNamespace );
	if ( !d ) {
		Com_sprintf( out, outSize, "%s is not a variable here", name );
	} else {
		varEval_t reg = GetVariable( self, ( varDef_t * )d );
		if ( VarDef_Type( d ) == ev_field ) {
			gentity_t *entity = inNamespace ? NULL : Interp_GetEntity( *( ( int * )&self->localstack[ self->localstackBase ] ) );
			const typeDef_t *field = Type_FieldType( d->typeDef );
			if ( !entity || !field || !entity->scriptObject.data ) {
				Q_strncpyz( out, "no object", outSize );
			} else {
				set = Interp_ParseValue( field, value, &entity->scriptObject.data[ reg.ptrOffset ], out, outSize );
			}
		} else if ( reg.bytePtr ) {
			set = Interp_ParseValue( d->typeDef, value, reg.bytePtr, out, outSize );
		} else {
			Q_strncpyz( out, "it has no storage", outSize );
		}
	}
	self->localstackBase = savedStackBase;

	if ( set ) {
		Interp_GetRegisterValue( self, name, out, outSize, scopeDepth );
	}
	return set;
}

/*
============
Interp_SetNextStatementLine

the script debugger's: the stopped thread goes on from the first statement
of that line of its function (of its file)
============
*/
qboolean Interp_SetNextStatementLine( interpreter_t *self, int line ) {
	const statement_t	*here;
	const statement_t	*st;
	int					i;

	if ( !self->currentFunction || self->instructionPointer < 0 || self->instructionPointer >= Program_NumStatements() ) {
		return qfalse;
	}

	here = Program_GetStatement( self->instructionPointer );
	for ( i = self->currentFunction->firstStatement; i < self->currentFunction->firstStatement + self->currentFunction->numStatements; i++ ) {
		st = Program_GetStatement( i );
		if ( st->linenumber == line && st->file == here->file ) {
			self->instructionPointer = i;
			return qtrue;
		}
	}
	return qfalse;
}

/*
============
Interp_CurrentLine / CurrentFile
============
*/
int Interp_CurrentLine( const interpreter_t *self ) {
	if ( self->instructionPointer < 0 ) {
		return 0;
	}
	return Program_GetLineNumberForStatement( self->instructionPointer );
}

const char *Interp_CurrentFile( const interpreter_t *self ) {
	if ( self->instructionPointer < 0 ) {
		return "";
	}
	return Program_GetFilenameForStatement( self->instructionPointer );
}

/*
============
Interp_StackTrace
============
*/
void Interp_StackTrace( const interpreter_t *self ) {
	const function_t	*f;
	int 				i;
	int					top;

	if ( self->callStackDepth == 0 ) {
		G_Printf( "<NO STACK>\n" );
		return;
	}

	top = self->callStackDepth;
	if ( top >= MAX_STACK_DEPTH ) {
		top = MAX_STACK_DEPTH - 1;
	}

	if ( !self->currentFunction ) {
		G_Printf( "<NO FUNCTION>\n" );
	} else {
		G_Printf( "%12s : %s\n", Program_GetFilename( self->currentFunction->filenum ), self->currentFunction->name );
	}

	for( i = top; i >= 0; i-- ) {
		f = self->callStack[ i ].f;
		if ( !f ) {
			G_Printf( "<NO FUNCTION>\n" );
		} else {
			G_Printf( "%12s : %s\n", Program_GetFilename( f->filenum ), f->name );
		}
	}
}

/*
============
Interp_Error

Aborts the currently executing function
============
*/
void QDECL Interp_Error( interpreter_t *self, const char *fmt, ... ) {
	va_list argptr;
	char	text[ 1024 ];
	const statement_t *line;

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	Interp_StackTrace( self );

	if ( ( self->instructionPointer >= 0 ) && ( self->instructionPointer < Program_NumStatements() ) ) {
		ScriptDebug_ScriptError( self, self->instructionPointer, text, qtrue );
		line = Program_GetStatement( self->instructionPointer );
		G_Error( "%s(%d): Thread '%s': %s\n", Program_GetFilename( line->file ), line->linenumber, self->thread->threadName, text );
	} else {
		G_Error( "Thread '%s': %s\n", self->thread->threadName, text );
	}
}

/*
============
Interp_Warning

Prints file and line number information with warning.
============
*/
void QDECL Interp_Warning( interpreter_t *self, const char *fmt, ... ) {
	va_list argptr;
	char	text[ 1024 ];
	const statement_t *line;

	va_start( argptr, fmt );
	Q_vsnprintf( text, sizeof( text ), fmt, argptr );
	va_end( argptr );

	if ( ( self->instructionPointer >= 0 ) && ( self->instructionPointer < Program_NumStatements() ) ) {
		ScriptDebug_ScriptError( self, self->instructionPointer, text, qfalse );
		line = Program_GetStatement( self->instructionPointer );
		G_Printf( S_COLOR_YELLOW "WARNING: %s(%d): Thread '%s': %s\n", Program_GetFilename( line->file ), line->linenumber, self->thread->threadName, text );
	} else {
		G_Printf( S_COLOR_YELLOW "WARNING: Thread '%s' : %s\n", self->thread->threadName, text );
	}
}

/*
================
Interp_DisplayInfo
================
*/
void Interp_DisplayInfo( const interpreter_t *self ) {
	const function_t *f;
	int i;

	G_Printf( " Stack depth: %d bytes, %d max\n", self->localstackUsed, self->maxLocalstackUsed );
	G_Printf( "  Call depth: %d, %d max\n", self->callStackDepth, self->maxStackDepth );
	G_Printf( "  Call Stack: " );

	if ( self->callStackDepth == 0 ) {
		G_Printf( "<NO STACK>\n" );
	} else {
		if ( !self->currentFunction ) {
			G_Printf( "<NO FUNCTION>\n" );
		} else {
			G_Printf( "%12s : %s\n", Program_GetFilename( self->currentFunction->filenum ), self->currentFunction->name );
		}

		for( i = self->callStackDepth; i > 0; i-- ) {
			G_Printf( "              " );
			f = self->callStack[ i ].f;
			if ( !f ) {
				G_Printf( "<NO FUNCTION>\n" );
			} else {
				G_Printf( "%12s : %s\n", Program_GetFilename( f->filenum ), f->name );
			}
		}
	}
}

/*
====================
Interp_ThreadCall

Copys the args from the calling thread's stack
====================
*/
void Interp_ThreadCall( interpreter_t *self, interpreter_t *source, const function_t *func, int args ) {
	Interp_Reset( self );

	memcpy( self->localstack, &source->localstack[ source->localstackUsed - args ], args );

	self->localstackUsed = args;
	self->localstackBase = 0;

	self->maxLocalstackUsed = self->localstackUsed;
	Interp_EnterFunction( self, func, qfalse );

	Thread_SetThreadName( self->thread, self->currentFunction->name );
}

/*
================
Interp_EnterObjectFunction

Calls a function on a script object.

NOTE: If this is called from within a event called by this interpreter, the function arguments will be invalid after calling this function.
================
*/
void Interp_EnterObjectFunction( interpreter_t *self, gentity_t *ent, const function_t *func, qboolean clearStack ) {
	if ( clearStack ) {
		Interp_Reset( self );
	}
	if ( self->popParms ) {
		PopParms( self, self->popParms );
		self->popParms = 0;
	}
	Push( self, Interp_EntityNumber( ent ) );
	Interp_EnterFunction( self, func, qfalse );
}

/*
====================
Interp_EnterFunction

Returns the new program statement counter

NOTE: If this is called from within a event called by this interpreter, the function arguments will be invalid after calling this function.
====================
*/
void Interp_EnterFunction( interpreter_t *self, const function_t *func, qboolean clearStack ) {
	int 		c;
	prstack_t	*stack;

	if ( clearStack ) {
		Interp_Reset( self );
	}
	if ( self->popParms ) {
		PopParms( self, self->popParms );
		self->popParms = 0;
	}

	if ( self->callStackDepth >= MAX_STACK_DEPTH ) {
		Interp_Error( self, "call stack overflow" );
	}

	stack = &self->callStack[ self->callStackDepth ];

	stack->s			= self->instructionPointer + 1;	// point to the next instruction to execute
	stack->f			= self->currentFunction;
	stack->stackbase	= self->localstackBase;

	self->callStackDepth++;
	if ( self->callStackDepth > self->maxStackDepth ) {
		self->maxStackDepth = self->callStackDepth;
	}

	if ( !func ) {
		Interp_Error( self, "NULL function" );
	}

	if ( self->debug ) {
		if ( self->currentFunction ) {
			G_Printf( "%d: call '%s' from '%s'(line %d)%s\n", level.time, func->name, self->currentFunction->name,
				Program_GetStatement( self->instructionPointer )->linenumber, clearStack ? " clear stack" : "" );
		} else {
			G_Printf( "%d: call '%s'%s\n", level.time, func->name, clearStack ? " clear stack" : "" );
		}
	}

	self->currentFunction = func;
	assert( !func->eventdef );
	NextInstruction( self, func->firstStatement );

	// allocate space on the stack for locals
	// parms are already on stack
	c = func->locals - func->parmTotal;
	assert( c >= 0 );

	if ( self->localstackUsed + c > LOCALSTACK_SIZE ) {
		Interp_Error( self, "EnterFuncton: locals stack overflow\n" );
	}

	// initialize local stack variables to zero
	memset( &self->localstack[ self->localstackUsed ], 0, c );

	self->localstackUsed += c;
	self->localstackBase = self->localstackUsed - func->locals;

	if ( self->localstackUsed > self->maxLocalstackUsed ) {
		self->maxLocalstackUsed = self->localstackUsed ;
	}
}

/*
====================
LeaveFunction
====================
*/
static void LeaveFunction( interpreter_t *self, varDef_t *returnDef ) {
	prstack_t *stack;
	varEval_t ret;

	if ( self->callStackDepth <= 0 ) {
		Interp_Error( self, "prog stack underflow" );
	}

	// return value
	if ( returnDef ) {
		switch( VarDef_Type( returnDef ) ) {
		case ev_string :
			Program_ReturnString( GetString( self, returnDef ) );
			break;

		case ev_vector :
			ret = GetVariable( self, returnDef );
			Program_ReturnVector( ret.vectorPtr );
			break;

		default :
			ret = GetVariable( self, returnDef );
			Program_ReturnInteger( *ret.intPtr );
		}
	}

	// remove locals from the stack
	PopParms( self, self->currentFunction->locals );
	assert( self->localstackUsed == self->localstackBase );

	if ( self->debug ) {
		statement_t *line = Program_GetStatement( self->instructionPointer );
		G_Printf( "%d: %s(%d): exit %s", level.time, Program_GetFilename( line->file ), line->linenumber, self->currentFunction->name );
		if ( self->callStackDepth > 1 ) {
			G_Printf( " return to %s(line %d)\n", self->callStack[ self->callStackDepth - 1 ].f->name, Program_GetStatement( self->callStack[ self->callStackDepth - 1 ].s )->linenumber );
		} else {
			G_Printf( " done\n" );
		}
	}

	// up stack
	self->callStackDepth--;
	stack = &self->callStack[ self->callStackDepth ];
	self->currentFunction = stack->f;
	self->localstackBase = stack->stackbase;
	NextInstruction( self, stack->s );

	if ( !self->callStackDepth ) {
		// all done
		self->doneProcessing = qtrue;
		self->threadDying = qtrue;
		self->currentFunction = 0;
	}
}

/*
================
ReturnSafeValue

what an event an entity doesn't respond to returns
================
*/
static void ReturnSafeValue( char returnType ) {
	switch( returnType ) {
	case D_EVENT_INTEGER :
		Program_ReturnInteger( 0 );
		break;

	case D_EVENT_FLOAT :
		Program_ReturnFloat( 0 );
		break;

	case D_EVENT_VECTOR :
		Program_ReturnVector( vec3_origin );
		break;

	case D_EVENT_STRING :
		Program_ReturnString( "" );
		break;

	case D_EVENT_ENTITY :
	case D_EVENT_ENTITY_NULL :
		Program_ReturnEntity( NULL );
		break;

	case D_EVENT_TRACE :
	default:
		// unsupported data type
		break;
	}
}

/*
================
GetEventArgs

an event's arguments from the stack, as its handler takes them (idEventDef's
format); qfalse when an entity it needs isn't there
================
*/
static qboolean GetEventArgs( interpreter_t *self, const function_t *func, int start, int pos, int argsize, intptr_t *data ) {
	const eventDef_t	*evdef = func->eventdef;
	const char			*format = evdef->formatspec ? evdef->formatspec : "";
	varEval_t			var;
	int					i, j;

	for( j = 0, i = 0; ( pos < argsize ) || ( format[ i ] != 0 ); i++ ) {
		if ( i >= D_EVENT_MAXARGS ) {
			Interp_Error( self, "too many arguments for '%s' event.", evdef->name );
		}
		switch( format[ i ] ) {
		case D_EVENT_INTEGER :
			var.intPtr = ( int * )&self->localstack[ start + pos ];
			data[ i ] = ( int )( *var.floatPtr );
			break;

		case D_EVENT_FLOAT :
			var.intPtr = ( int * )&self->localstack[ start + pos ];
			data[ i ] = 0;
			( *( float * )&data[ i ] ) = *var.floatPtr;
			break;

		case D_EVENT_VECTOR :
			var.intPtr = ( int * )&self->localstack[ start + pos ];
			data[ i ] = ( intptr_t )var.vectorPtr;
			break;

		case D_EVENT_STRING :
			data[ i ] = ( intptr_t )&self->localstack[ start + pos ];
			break;

		case D_EVENT_ENTITY :
			var.intPtr = ( int * )&self->localstack[ start + pos ];
			data[ i ] = ( intptr_t )Interp_GetEntity( *var.entityNumberPtr );
			if ( !data[ i ] ) {
				Interp_Warning( self, "Entity not found for event '%s'. Terminating thread.", evdef->name );
				self->threadDying = qtrue;
				PopParms( self, argsize );
				return qfalse;
			}
			break;

		case D_EVENT_ENTITY_NULL :
			var.intPtr = ( int * )&self->localstack[ start + pos ];
			data[ i ] = ( intptr_t )Interp_GetEntity( *var.entityNumberPtr );
			break;

		case D_EVENT_TRACE :
			Interp_Error( self, "trace type not supported from script for '%s' event.", evdef->name );
			break;

		default :
			Interp_Error( self, "Invalid arg format string for '%s' event.", evdef->name );
			break;
		}

		pos += func->parmSize[ j++ ];
	}
	return qtrue;
}

/*
================
CallEvent
================
*/
static void CallEvent( interpreter_t *self, const function_t *func, int argsize ) {
	varEval_t			var;
	int 				start;
	intptr_t			data[ D_EVENT_MAXARGS ];
	const eventDef_t	*evdef;

	if ( !func ) {
		Interp_Error( self, "NULL function" );
	}

	assert( func->eventdef );
	evdef = func->eventdef;

	start = self->localstackUsed - argsize;
	var.intPtr = ( int * )&self->localstack[ start ];
	self->eventEntity = Interp_GetEntity( *var.entityNumberPtr );

	if ( !self->eventEntity || !Event_EntityRespondsTo( self->eventEntity, evdef ) ) {
		if ( self->eventEntity && trap_Cvar_VariableIntegerValue( "developer" ) ) {
			// give a warning in developer mode
			Interp_Warning( self, "Function '%s' not supported on entity '%s'", evdef->name, self->eventEntity->targetname ? self->eventEntity->targetname : self->eventEntity->classname );
		}
		// always return a safe value when an object doesn't exist
		ReturnSafeValue( evdef->returnType );
		PopParms( self, argsize );
		self->eventEntity = NULL;
		return;
	}

	memset( data, 0, sizeof( data ) );
	if ( !GetEventArgs( self, func, start, type_object.size, argsize, data ) ) {
		return;
	}

	self->popParms = argsize;
	evdef->entityFunc( self->eventEntity, data );

	if ( !self->multiFrameEvent ) {
		if ( self->popParms ) {
			PopParms( self, self->popParms );
		}
		self->eventEntity = NULL;
	} else {
		self->doneProcessing = qtrue;
	}
	self->popParms = 0;
}

/*
================
Interp_BeginMultiFrameEvent
================
*/
qboolean Interp_BeginMultiFrameEvent( interpreter_t *self, gentity_t *ent, const eventDef_t *event ) {
	if ( self->eventEntity != ent ) {
		Interp_Error( self, "Interp_BeginMultiFrameEvent called with wrong entity" );
	}
	if ( self->multiFrameEvent ) {
		if ( self->multiFrameEvent != event ) {
			Interp_Error( self, "Interp_BeginMultiFrameEvent called with wrong event" );
		}
		return qfalse;
	}

	self->multiFrameEvent = event;
	return qtrue;
}

/*
================
Interp_EndMultiFrameEvent
================
*/
void Interp_EndMultiFrameEvent( interpreter_t *self, gentity_t *ent, const eventDef_t *event ) {
	if ( self->multiFrameEvent != event ) {
		Interp_Error( self, "Interp_EndMultiFrameEvent called with wrong event" );
	}

	self->multiFrameEvent = NULL;
}

/*
================
Interp_MultiFrameEventInProgress
================
*/
qboolean Interp_MultiFrameEventInProgress( const interpreter_t *self ) {
	return self->multiFrameEvent != NULL;
}

/*
================
CallSysEvent
================
*/
static void CallSysEvent( interpreter_t *self, const function_t *func, int argsize ) {
	int 				start;
	intptr_t			data[ D_EVENT_MAXARGS ];
	const eventDef_t	*evdef;

	if ( !func ) {
		Interp_Error( self, "NULL function" );
	}

	assert( func->eventdef );
	evdef = func->eventdef;

	start = self->localstackUsed - argsize;

	memset( data, 0, sizeof( data ) );
	if ( !GetEventArgs( self, func, start, 0, argsize, data ) ) {
		return;
	}

	self->popParms = argsize;
	evdef->sysFunc( self->thread, data );
	if ( self->popParms ) {
		PopParms( self, self->popParms );
	}
	self->popParms = 0;
}

/*
====================
Interp_Execute
====================
*/
qboolean Interp_Execute( interpreter_t *self ) {
	varEval_t	var_a;
	varEval_t	var_b;
	varEval_t	var_c;
	varEval_t	var;
	statement_t	*st;
	int 		runaway;
	scriptThread_t	*newThread;
	float		floatVal;
	scriptObject_t *obj;
	const function_t *func;

	if ( self->threadDying || !self->currentFunction ) {
		return qtrue;
	}

	if ( self->multiFrameEvent ) {
		// move to previous instruction and call it again
		self->instructionPointer--;
	}

	runaway = 5000000;

	self->doneProcessing = qfalse;
	while( !self->doneProcessing && !self->threadDying ) {
		self->instructionPointer++;

		if ( !--runaway ) {
			Interp_Error( self, "runaway loop error" );
		}

		// next statement
		if ( !ScriptDebug_Update( self, self->instructionPointer ) && g_debugScript.integer ) {
			static int lastLineNumber = -1;
			if ( lastLineNumber != Program_GetStatement( self->instructionPointer )->linenumber ) {
				G_Printf( "%s (%d)\n",
					Program_GetFilename( Program_GetStatement( self->instructionPointer )->file ),
					Program_GetStatement( self->instructionPointer )->linenumber );
				lastLineNumber = Program_GetStatement( self->instructionPointer )->linenumber;
			}
		}

		// the debugger may have moved the thread on (DBMSG_SETNEXTSTATEMENT)
		st = Program_GetStatement( self->instructionPointer );

		switch( st->op ) {
		case OP_RETURN:
			LeaveFunction( self, st->a );
			break;

		case OP_THREAD:
			newThread = Thread_New();
			Interp_ThreadCall( &newThread->interpreter, self, st->a->value.functionPtr, st->b->value.argSize );
			Thread_Start( newThread );

			// return the thread number to the script
			Program_ReturnFloat( newThread->threadNum );
			PopParms( self, st->b->value.argSize );
			break;

		case OP_OBJTHREAD:
			var_a = GetVariable( self, st->a );
			obj = Interp_GetScriptObject( *var_a.entityNumberPtr );
			if ( obj ) {
				gentity_t *ent = Interp_GetEntity( *var_a.entityNumberPtr );

				func = Type_GetFunction( obj->type, st->b->value.virtualFunction );
				assert( st->c->value.argSize == func->parmTotal );
				newThread = Thread_New();
				Thread_SetThreadName( newThread, ent->targetname ? ent->targetname : ent->classname );
				Interp_ThreadCall( &newThread->interpreter, self, func, func->parmTotal );
				Thread_Start( newThread );

				// return the thread number to the script
				Program_ReturnFloat( newThread->threadNum );
			} else {
				// return a null thread to the script
				Program_ReturnFloat( 0.0f );
			}
			PopParms( self, st->c->value.argSize );
			break;

		case OP_CALL:
			Interp_EnterFunction( self, st->a->value.functionPtr, qfalse );
			break;

		case OP_EVENTCALL:
			CallEvent( self, st->a->value.functionPtr, st->b->value.argSize );
			break;

		case OP_OBJECTCALL:
			var_a = GetVariable( self, st->a );
			obj = Interp_GetScriptObject( *var_a.entityNumberPtr );
			if ( obj ) {
				func = Type_GetFunction( obj->type, st->b->value.virtualFunction );
				Interp_EnterFunction( self, func, qfalse );
			} else {
				// return a 'safe' value
				Program_ReturnVector( vec3_origin );
				Program_ReturnString( "" );
				PopParms( self, st->c->value.argSize );
			}
			break;

		case OP_SYSCALL:
			CallSysEvent( self, st->a->value.functionPtr, st->b->value.argSize );
			break;

		case OP_IFNOT:
			var_a = GetVariable( self, st->a );
			if ( *var_a.intPtr == 0 ) {
				NextInstruction( self, self->instructionPointer + st->b->value.jumpOffset );
			}
			break;

		case OP_IF:
			var_a = GetVariable( self, st->a );
			if ( *var_a.intPtr != 0 ) {
				NextInstruction( self, self->instructionPointer + st->b->value.jumpOffset );
			}
			break;

		case OP_GOTO:
			NextInstruction( self, self->instructionPointer + st->a->value.jumpOffset );
			break;

		case OP_ADD_F:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = *var_a.floatPtr + *var_b.floatPtr;
			break;

		case OP_ADD_V:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			VectorAdd( var_a.vectorPtr, var_b.vectorPtr, var_c.vectorPtr );
			break;

		case OP_ADD_S:
			SetString( self, st->c, GetString( self, st->a ) );
			AppendString( self, st->c, GetString( self, st->b ) );
			break;

		case OP_ADD_FS:
			var_a = GetVariable( self, st->a );
			SetString( self, st->c, FloatToString( *var_a.floatPtr ) );
			AppendString( self, st->c, GetString( self, st->b ) );
			break;

		case OP_ADD_SF:
			var_b = GetVariable( self, st->b );
			SetString( self, st->c, GetString( self, st->a ) );
			AppendString( self, st->c, FloatToString( *var_b.floatPtr ) );
			break;

		case OP_ADD_VS:
			var_a = GetVariable( self, st->a );
			SetString( self, st->c, VectorToString( var_a.vectorPtr ) );
			AppendString( self, st->c, GetString( self, st->b ) );
			break;

		case OP_ADD_SV:
			var_b = GetVariable( self, st->b );
			SetString( self, st->c, GetString( self, st->a ) );
			AppendString( self, st->c, VectorToString( var_b.vectorPtr ) );
			break;

		case OP_SUB_F:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = *var_a.floatPtr - *var_b.floatPtr;
			break;

		case OP_SUB_V:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			VectorSubtract( var_a.vectorPtr, var_b.vectorPtr, var_c.vectorPtr );
			break;

		case OP_MUL_F:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = *var_a.floatPtr * *var_b.floatPtr;
			break;

		case OP_MUL_V:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = DotProduct( var_a.vectorPtr, var_b.vectorPtr );
			break;

		case OP_MUL_FV:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			VectorScale( var_b.vectorPtr, *var_a.floatPtr, var_c.vectorPtr );
			break;

		case OP_MUL_VF:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			VectorScale( var_a.vectorPtr, *var_b.floatPtr, var_c.vectorPtr );
			break;

		case OP_DIV_F:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );

			if ( *var_b.floatPtr == 0.0f ) {
				Interp_Warning( self, "Divide by zero" );
				*var_c.floatPtr = INFINITY;
			} else {
				*var_c.floatPtr = *var_a.floatPtr / *var_b.floatPtr;
			}
			break;

		case OP_MOD_F:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable ( self, st->c );

			if ( *var_b.floatPtr == 0.0f ) {
				Interp_Warning( self, "Divide by zero" );
				*var_c.floatPtr = *var_a.floatPtr;
			} else {
				*var_c.floatPtr = ( int )( *var_a.floatPtr ) % ( int )( *var_b.floatPtr );
			}
			break;

		case OP_BITAND:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( int )( *var_a.floatPtr ) & ( int )( *var_b.floatPtr );
			break;

		case OP_BITOR:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( int )( *var_a.floatPtr ) | ( int )( *var_b.floatPtr );
			break;

		case OP_GE:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( *var_a.floatPtr >= *var_b.floatPtr );
			break;

		case OP_LE:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( *var_a.floatPtr <= *var_b.floatPtr );
			break;

		case OP_GT:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( *var_a.floatPtr > *var_b.floatPtr );
			break;

		case OP_LT:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( *var_a.floatPtr < *var_b.floatPtr );
			break;

		case OP_AND:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( *var_a.floatPtr != 0.0f ) && ( *var_b.floatPtr != 0.0f );
			break;

		case OP_AND_BOOLF:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( *var_a.intPtr != 0 ) && ( *var_b.floatPtr != 0.0f );
			break;

		case OP_AND_FBOOL:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( *var_a.floatPtr != 0.0f ) && ( *var_b.intPtr != 0 );
			break;

		case OP_AND_BOOLBOOL:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( *var_a.intPtr != 0 ) && ( *var_b.intPtr != 0 );
			break;

		case OP_OR:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( *var_a.floatPtr != 0.0f ) || ( *var_b.floatPtr != 0.0f );
			break;

		case OP_OR_BOOLF:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( *var_a.intPtr != 0 ) || ( *var_b.floatPtr != 0.0f );
			break;

		case OP_OR_FBOOL:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( *var_a.floatPtr != 0.0f ) || ( *var_b.intPtr != 0 );
			break;

		case OP_OR_BOOLBOOL:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( *var_a.intPtr != 0 ) || ( *var_b.intPtr != 0 );
			break;

		case OP_NOT_BOOL:
			var_a = GetVariable( self, st->a );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( *var_a.intPtr == 0 );
			break;

		case OP_NOT_F:
			var_a = GetVariable( self, st->a );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( *var_a.floatPtr == 0.0f );
			break;

		case OP_NOT_V:
			var_a = GetVariable( self, st->a );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = VectorCompare( var_a.vectorPtr, vec3_origin );
			break;

		case OP_NOT_S:
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( strlen( GetString( self, st->a ) ) == 0 );
			break;

		case OP_NOT_ENT:
			var_a = GetVariable( self, st->a );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( Interp_GetEntity( *var_a.entityNumberPtr ) == NULL );
			break;

		case OP_NEG_F:
			var_a = GetVariable( self, st->a );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = -*var_a.floatPtr;
			break;

		case OP_NEG_V:
			var_a = GetVariable( self, st->a );
			var_c = GetVariable( self, st->c );
			VectorNegate( var_a.vectorPtr, var_c.vectorPtr );
			break;

		case OP_INT_F:
			var_a = GetVariable( self, st->a );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( int )( *var_a.floatPtr );
			break;

		case OP_EQ_F:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( *var_a.floatPtr == *var_b.floatPtr );
			break;

		case OP_EQ_V:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = VectorCompare( var_a.vectorPtr, var_b.vectorPtr );
			break;

		case OP_EQ_S:
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( strcmp( GetString( self, st->a ), GetString( self, st->b ) ) == 0 );
			break;

		case OP_EQ_E:
		case OP_EQ_EO:
		case OP_EQ_OE:
		case OP_EQ_OO:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( *var_a.entityNumberPtr == *var_b.entityNumberPtr );
			break;

		case OP_NE_F:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( *var_a.floatPtr != *var_b.floatPtr );
			break;

		case OP_NE_V:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = !VectorCompare( var_a.vectorPtr, var_b.vectorPtr );
			break;

		case OP_NE_S:
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( strcmp( GetString( self, st->a ), GetString( self, st->b ) ) != 0 );
			break;

		case OP_NE_E:
		case OP_NE_EO:
		case OP_NE_OE:
		case OP_NE_OO:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ( *var_a.entityNumberPtr != *var_b.entityNumberPtr );
			break;

		case OP_UADD_F:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			*var_b.floatPtr += *var_a.floatPtr;
			break;

		case OP_UADD_V:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			VectorAdd( var_b.vectorPtr, var_a.vectorPtr, var_b.vectorPtr );
			break;

		case OP_USUB_F:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			*var_b.floatPtr -= *var_a.floatPtr;
			break;

		case OP_USUB_V:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			VectorSubtract( var_b.vectorPtr, var_a.vectorPtr, var_b.vectorPtr );
			break;

		case OP_UMUL_F:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			*var_b.floatPtr *= *var_a.floatPtr;
			break;

		case OP_UMUL_V:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			VectorScale( var_b.vectorPtr, *var_a.floatPtr, var_b.vectorPtr );
			break;

		case OP_UDIV_F:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );

			if ( *var_a.floatPtr == 0.0f ) {
				Interp_Warning( self, "Divide by zero" );
				*var_b.floatPtr = INFINITY;
			} else {
				*var_b.floatPtr = *var_b.floatPtr / *var_a.floatPtr;
			}
			break;

		case OP_UDIV_V:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );

			if ( *var_a.floatPtr == 0.0f ) {
				Interp_Warning( self, "Divide by zero" );
				VectorSet( var_b.vectorPtr, INFINITY, INFINITY, INFINITY );
			} else {
				VectorScale( var_b.vectorPtr, 1.0f / *var_a.floatPtr, var_b.vectorPtr );
			}
			break;

		case OP_UMOD_F:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );

			if ( *var_a.floatPtr == 0.0f ) {
				Interp_Warning( self, "Divide by zero" );
				*var_b.floatPtr = *var_a.floatPtr;
			} else {
				*var_b.floatPtr = ( int )( *var_b.floatPtr ) % ( int )( *var_a.floatPtr );
			}
			break;

		case OP_UOR_F:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			*var_b.floatPtr = ( int )( *var_b.floatPtr ) | ( int )( *var_a.floatPtr );
			break;

		case OP_UAND_F:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			*var_b.floatPtr = ( int )( *var_b.floatPtr ) & ( int )( *var_a.floatPtr );
			break;

		case OP_UINC_F:
			var_a = GetVariable( self, st->a );
			( *var_a.floatPtr )++;
			break;

		case OP_UINCP_F:
			var_a = GetVariable( self, st->a );
			obj = Interp_GetScriptObject( *var_a.entityNumberPtr );
			if ( obj ) {
				var.bytePtr = &obj->data[ st->b->value.ptrOffset ];
				( *var.floatPtr )++;
			}
			break;

		case OP_UDEC_F:
			var_a = GetVariable( self, st->a );
			( *var_a.floatPtr )--;
			break;

		case OP_UDECP_F:
			var_a = GetVariable( self, st->a );
			obj = Interp_GetScriptObject( *var_a.entityNumberPtr );
			if ( obj ) {
				var.bytePtr = &obj->data[ st->b->value.ptrOffset ];
				( *var.floatPtr )--;
			}
			break;

		case OP_COMP_F:
			var_a = GetVariable( self, st->a );
			var_c = GetVariable( self, st->c );
			*var_c.floatPtr = ~( int )( *var_a.floatPtr );
			break;

		case OP_STORE_F:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			*var_b.floatPtr = *var_a.floatPtr;
			break;

		case OP_STORE_ENT:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			*var_b.entityNumberPtr = *var_a.entityNumberPtr;
			break;

		case OP_STORE_BOOL:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			*var_b.intPtr = *var_a.intPtr;
			break;

		case OP_STORE_OBJENT:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			obj = Interp_GetScriptObject( *var_a.entityNumberPtr );
			if ( !obj ) {
				*var_b.entityNumberPtr = 0;
			} else if ( !Type_Inherits( obj->type, st->b->typeDef ) ) {
				*var_b.entityNumberPtr = 0;
			} else {
				*var_b.entityNumberPtr = *var_a.entityNumberPtr;
			}
			break;

		case OP_STORE_OBJ:
		case OP_STORE_ENTOBJ:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			*var_b.entityNumberPtr = *var_a.entityNumberPtr;
			break;

		case OP_STORE_S:
			SetString( self, st->b, GetString( self, st->a ) );
			break;

		case OP_STORE_V:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			VectorCopy( var_a.vectorPtr, var_b.vectorPtr );
			break;

		case OP_STORE_FTOS:
			var_a = GetVariable( self, st->a );
			SetString( self, st->b, FloatToString( *var_a.floatPtr ) );
			break;

		case OP_STORE_BTOS:
			var_a = GetVariable( self, st->a );
			SetString( self, st->b, *var_a.intPtr ? "true" : "false" );
			break;

		case OP_STORE_VTOS:
			var_a = GetVariable( self, st->a );
			SetString( self, st->b, VectorToString( var_a.vectorPtr ) );
			break;

		case OP_STORE_FTOBOOL:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			if ( *var_a.floatPtr != 0.0f ) {
				*var_b.intPtr = 1;
			} else {
				*var_b.intPtr = 0;
			}
			break;

		case OP_STORE_BOOLTOF:
			var_a = GetVariable( self, st->a );
			var_b = GetVariable( self, st->b );
			*var_b.floatPtr = ( float )( *var_a.intPtr );
			break;

		case OP_STOREP_F:
			var_b = GetVariable( self, st->b );
			if ( var_b.evalPtr && var_b.evalPtr->floatPtr ) {
				var_a = GetVariable( self, st->a );
				*var_b.evalPtr->floatPtr = *var_a.floatPtr;
			}
			break;

		case OP_STOREP_ENT:
			var_b = GetVariable( self, st->b );
			if ( var_b.evalPtr && var_b.evalPtr->entityNumberPtr ) {
				var_a = GetVariable( self, st->a );
				*var_b.evalPtr->entityNumberPtr = *var_a.entityNumberPtr;
			}
			break;

		case OP_STOREP_FLD:
			var_b = GetVariable( self, st->b );
			if ( var_b.evalPtr && var_b.evalPtr->intPtr ) {
				var_a = GetVariable( self, st->a );
				*var_b.evalPtr->intPtr = *var_a.intPtr;
			}
			break;

		case OP_STOREP_BOOL:
			var_b = GetVariable( self, st->b );
			if ( var_b.evalPtr && var_b.evalPtr->intPtr ) {
				var_a = GetVariable( self, st->a );
				*var_b.evalPtr->intPtr = *var_a.intPtr;
			}
			break;

		case OP_STOREP_S:
			var_b = GetVariable( self, st->b );
			if ( var_b.evalPtr && var_b.evalPtr->stringPtr ) {
				Q_strncpyz( var_b.evalPtr->stringPtr, GetString( self, st->a ), MAX_STRING_LEN );
			}
			break;

		case OP_STOREP_V:
			var_b = GetVariable( self, st->b );
			if ( var_b.evalPtr && var_b.evalPtr->vectorPtr ) {
				var_a = GetVariable( self, st->a );
				VectorCopy( var_a.vectorPtr, var_b.evalPtr->vectorPtr );
			}
			break;

		case OP_STOREP_FTOS:
			var_b = GetVariable( self, st->b );
			if ( var_b.evalPtr && var_b.evalPtr->stringPtr ) {
				var_a = GetVariable( self, st->a );
				Q_strncpyz( var_b.evalPtr->stringPtr, FloatToString( *var_a.floatPtr ), MAX_STRING_LEN );
			}
			break;

		case OP_STOREP_BTOS:
			var_b = GetVariable( self, st->b );
			if ( var_b.evalPtr && var_b.evalPtr->stringPtr ) {
				var_a = GetVariable( self, st->a );
				if ( *var_a.floatPtr != 0.0f ) {
					Q_strncpyz( var_b.evalPtr->stringPtr, "true", MAX_STRING_LEN );
				} else {
					Q_strncpyz( var_b.evalPtr->stringPtr, "false", MAX_STRING_LEN );
				}
			}
			break;

		case OP_STOREP_VTOS:
			var_b = GetVariable( self, st->b );
			if ( var_b.evalPtr && var_b.evalPtr->stringPtr ) {
				var_a = GetVariable( self, st->a );
				Q_strncpyz( var_b.evalPtr->stringPtr, VectorToString( var_a.vectorPtr ), MAX_STRING_LEN );
			}
			break;

		case OP_STOREP_FTOBOOL:
			var_b = GetVariable( self, st->b );
			if ( var_b.evalPtr && var_b.evalPtr->intPtr ) {
				var_a = GetVariable( self, st->a );
				if ( *var_a.floatPtr != 0.0f ) {
					*var_b.evalPtr->intPtr = 1;
				} else {
					*var_b.evalPtr->intPtr = 0;
				}
			}
			break;

		case OP_STOREP_BOOLTOF:
			var_b = GetVariable( self, st->b );
			if ( var_b.evalPtr && var_b.evalPtr->floatPtr ) {
				var_a = GetVariable( self, st->a );
				*var_b.evalPtr->floatPtr = ( float )( *var_a.intPtr );
			}
			break;

		case OP_STOREP_OBJ:
			var_b = GetVariable( self, st->b );
			if ( var_b.evalPtr && var_b.evalPtr->entityNumberPtr ) {
				var_a = GetVariable( self, st->a );
				*var_b.evalPtr->entityNumberPtr = *var_a.entityNumberPtr;
			}
			break;

		case OP_STOREP_OBJENT:
			var_b = GetVariable( self, st->b );
			if ( var_b.evalPtr && var_b.evalPtr->entityNumberPtr ) {
				var_a = GetVariable( self, st->a );
				obj = Interp_GetScriptObject( *var_a.entityNumberPtr );
				if ( !obj ) {
					*var_b.evalPtr->entityNumberPtr = 0;

				// st->b points to type_pointer, which is just a temporary that gets its type reassigned, so we store the real type in st->c
				// so that we can do a type check during run time since we don't know what type the script object is at compile time because it
				// comes from an entity
				} else if ( !Type_Inherits( obj->type, st->c->typeDef ) ) {
					*var_b.evalPtr->entityNumberPtr = 0;
				} else {
					*var_b.evalPtr->entityNumberPtr = *var_a.entityNumberPtr;
				}
			}
			break;

		case OP_ADDRESS:
			var_a = GetVariable( self, st->a );
			var_c = GetVariable( self, st->c );
			obj = Interp_GetScriptObject( *var_a.entityNumberPtr );
			if ( obj ) {
				var_c.evalPtr->bytePtr = &obj->data[ st->b->value.ptrOffset ];
			} else {
				var_c.evalPtr->bytePtr = NULL;
			}
			break;

		case OP_INDIRECT_F:
			var_a = GetVariable( self, st->a );
			var_c = GetVariable( self, st->c );
			obj = Interp_GetScriptObject( *var_a.entityNumberPtr );
			if ( obj ) {
				var.bytePtr = &obj->data[ st->b->value.ptrOffset ];
				*var_c.floatPtr = *var.floatPtr;
			} else {
				*var_c.floatPtr = 0.0f;
			}
			break;

		case OP_INDIRECT_ENT:
			var_a = GetVariable( self, st->a );
			var_c = GetVariable( self, st->c );
			obj = Interp_GetScriptObject( *var_a.entityNumberPtr );
			if ( obj ) {
				var.bytePtr = &obj->data[ st->b->value.ptrOffset ];
				*var_c.entityNumberPtr = *var.entityNumberPtr;
			} else {
				*var_c.entityNumberPtr = 0;
			}
			break;

		case OP_INDIRECT_BOOL:
			var_a = GetVariable( self, st->a );
			var_c = GetVariable( self, st->c );
			obj = Interp_GetScriptObject( *var_a.entityNumberPtr );
			if ( obj ) {
				var.bytePtr = &obj->data[ st->b->value.ptrOffset ];
				*var_c.intPtr = *var.intPtr;
			} else {
				*var_c.intPtr = 0;
			}
			break;

		case OP_INDIRECT_S:
			var_a = GetVariable( self, st->a );
			obj = Interp_GetScriptObject( *var_a.entityNumberPtr );
			if ( obj ) {
				var.bytePtr = &obj->data[ st->b->value.ptrOffset ];
				SetString( self, st->c, var.stringPtr );
			} else {
				SetString( self, st->c, "" );
			}
			break;

		case OP_INDIRECT_V:
			var_a = GetVariable( self, st->a );
			var_c = GetVariable( self, st->c );
			obj = Interp_GetScriptObject( *var_a.entityNumberPtr );
			if ( obj ) {
				var.bytePtr = &obj->data[ st->b->value.ptrOffset ];
				VectorCopy( var.vectorPtr, var_c.vectorPtr );
			} else {
				VectorClear( var_c.vectorPtr );
			}
			break;

		case OP_INDIRECT_OBJ:
			var_a = GetVariable( self, st->a );
			var_c = GetVariable( self, st->c );
			obj = Interp_GetScriptObject( *var_a.entityNumberPtr );
			if ( !obj ) {
				*var_c.entityNumberPtr = 0;
			} else {
				var.bytePtr = &obj->data[ st->b->value.ptrOffset ];
				*var_c.entityNumberPtr = *var.entityNumberPtr;
			}
			break;

		case OP_PUSH_F:
			var_a = GetVariable( self, st->a );
			Push( self, *var_a.intPtr );
			break;

		case OP_PUSH_FTOS:
			var_a = GetVariable( self, st->a );
			PushString( self, FloatToString( *var_a.floatPtr ) );
			break;

		case OP_PUSH_BTOF:
			var_a = GetVariable( self, st->a );
			floatVal = *var_a.intPtr;
			Push( self, *( int * )&floatVal );
			break;

		case OP_PUSH_FTOB:
			var_a = GetVariable( self, st->a );
			if ( *var_a.floatPtr != 0.0f ) {
				Push( self, 1 );
			} else {
				Push( self, 0 );
			}
			break;

		case OP_PUSH_VTOS:
			var_a = GetVariable( self, st->a );
			PushString( self, VectorToString( var_a.vectorPtr ) );
			break;

		case OP_PUSH_BTOS:
			var_a = GetVariable( self, st->a );
			PushString( self, *var_a.intPtr ? "true" : "false" );
			break;

		case OP_PUSH_ENT:
			var_a = GetVariable( self, st->a );
			Push( self, *var_a.entityNumberPtr );
			break;

		case OP_PUSH_S:
			PushString( self, GetString( self, st->a ) );
			break;

		case OP_PUSH_V:
			var_a = GetVariable( self, st->a );
			PushVector( self, var_a.vectorPtr );
			break;

		case OP_PUSH_OBJ:
			var_a = GetVariable( self, st->a );
			Push( self, *var_a.entityNumberPtr );
			break;

		case OP_PUSH_OBJENT:
			var_a = GetVariable( self, st->a );
			Push( self, *var_a.entityNumberPtr );
			break;

		case OP_BREAK:
		case OP_CONTINUE:
		default:
			Interp_Error( self, "Bad opcode %i", st->op );
			break;
		}
	}

	return self->threadDying;
}
