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
// g_script.h -- Doom 3's script system, in C: the language (g_script_program.c,
// g_script_compiler.c), its interpreter (g_script_interp.c), threads and the
// events scripts call (g_script_thread.c, g_script_events.c)
//
// A script is straight-line code in a thread: sys.wait(), sys.waitFor()... pause
// it until the game has done what it waits for, and the game goes on meanwhile.
// Doom 3's scripts compile as they are, with the events we have.

#ifndef __G_SCRIPT_H__
#define __G_SCRIPT_H__

#define MAX_STRING_LEN		128
#define MAX_GLOBALS			296608			// in bytes
#define MAX_FUNCS			3072
#define MAX_STATEMENTS		81920

// a vector's size on the stack and in the globals: a whole number of intptr_t
#define E_EVENT_SIZEOF_VEC	( ( sizeof( vec3_t ) + ( sizeof( intptr_t ) - 1 ) ) & ~( sizeof( intptr_t ) - 1 ) )

#define SCRIPT_DEFAULT		"script/dk_main.script"
#define SCRIPT_DEFAULTDEFS	"script/dk_defs.script"
#define SCRIPT_DEFAULTFUNC	"dk_main"		// the game script's main, run as each map starts

typedef enum {
	ev_error = -1, ev_void, ev_scriptevent, ev_namespace, ev_string, ev_float, ev_vector, ev_entity, ev_field,
	ev_function, ev_virtualfunction, ev_pointer, ev_object, ev_jumpoffset, ev_argsize, ev_boolean
} etype_t;

typedef struct typeDef_s		typeDef_t;
typedef struct varDef_s			varDef_t;
typedef struct eventDef_s		eventDef_t;
typedef struct scriptThread_s	scriptThread_t;

typedef struct function_s {
	char				*name;
	const eventDef_t	*eventdef;
	varDef_t			*def;
	const typeDef_t		*type;
	int					firstStatement;
	int					numStatements;
	int					parmTotal;
	int					locals;			// total ints of parms + locals
	int					filenum;		// source file defined in
	int					numParmSize;	// 0 until the function's body is parsed
	int					*parmSize;
} function_t;

typedef union eval_s {
	const char			*stringPtr;
	float				_float;
	float				vector[ 3 ];
	function_t			*function;
	int					_int;
	int					entity;
} eval_t;

/*
===============================================================================

typeDef_t

Type information for variables and functions.

===============================================================================
*/
struct typeDef_s {
	etype_t				type;
	char				*name;
	int					size;

	// function types are more complex
	typeDef_t			*auxType;					// return type
	int					numParms;
	int					maxParms;
	typeDef_t			**parmTypes;
	char				**parmNames;
	int					numFunctions;
	int					maxFunctions;
	const function_t	**functions;

	varDef_t			*def;						// a def that points to this type
};

void			Type_Init( typeDef_t *self, etype_t etype, varDef_t *edef, const char *ename, int esize, typeDef_t *aux );
void			Type_Copy( typeDef_t *self, const typeDef_t *other );
void			Type_Free( typeDef_t *self );
qboolean		Type_Inherits( const typeDef_t *self, const typeDef_t *basetype );
qboolean		Type_MatchesType( const typeDef_t *self, const typeDef_t *matchtype );
qboolean		Type_MatchesVirtualFunction( const typeDef_t *self, const typeDef_t *matchfunc );
void			Type_AddFunctionParm( typeDef_t *self, typeDef_t *parmtype, const char *name );
void			Type_AddField( typeDef_t *self, typeDef_t *fieldtype, const char *name );
void			Type_SetName( typeDef_t *self, const char *newname );
typeDef_t		*Type_SuperClass( const typeDef_t *self );
typeDef_t		*Type_ReturnType( const typeDef_t *self );
void			Type_SetReturnType( typeDef_t *self, typeDef_t *type );
typeDef_t		*Type_FieldType( const typeDef_t *self );
void			Type_SetFieldType( typeDef_t *self, typeDef_t *type );
typeDef_t		*Type_PointerType( const typeDef_t *self );
void			Type_SetPointerType( typeDef_t *self, typeDef_t *type );
int				Type_GetFunctionNumber( const typeDef_t *self, const function_t *func );
const function_t *Type_GetFunction( const typeDef_t *self, int funcNumber );
void			Type_AddFunction( typeDef_t *self, const function_t *func );

/*
===============================================================================

scriptObject_t

An entity's script object ("scriptobject" key): its type's fields, the
entity being its self.

===============================================================================
*/
typedef struct scriptObject_s {
	typeDef_t			*type;			// NULL: none
	byte				*data;
} scriptObject_t;

void			ScriptObject_Free( scriptObject_t *self );
qboolean		ScriptObject_SetType( scriptObject_t *self, const char *typeName );
void			ScriptObject_ClearObject( scriptObject_t *self );
qboolean		ScriptObject_HasObject( const scriptObject_t *self );
const char		*ScriptObject_GetTypeName( const scriptObject_t *self );
const function_t *ScriptObject_GetConstructor( const scriptObject_t *self );
const function_t *ScriptObject_GetDestructor( const scriptObject_t *self );
const function_t *ScriptObject_GetFunction( const scriptObject_t *self, const char *name );
byte			*ScriptObject_GetVariable( const scriptObject_t *self, const char *name, etype_t etype );

/*
===============================================================================

varDef_t

The name, type and place of a variable, function or object of the scripts.

===============================================================================
*/
typedef union varEval_s {
	void					**objectPtrPtr;
	char					*stringPtr;
	float					*floatPtr;
	float					*vectorPtr;
	function_t				*functionPtr;
	int						*intPtr;
	byte					*bytePtr;
	int						*entityNumberPtr;
	int						virtualFunction;
	int						jumpOffset;
	int						stackOffset;		// offset in stack for local variables
	int						argSize;
	union varEval_s			*evalPtr;
	int						ptrOffset;
} varEval_t;

typedef enum {
	DEF_UNINITIALIZED, DEF_INITIALIZEDVARIABLE, DEF_INITIALIZEDCONSTANT, DEF_STACKVARIABLE
} defInitialized_t;

typedef struct varDefName_s {
	char					*name;
	varDef_t				*defs;
	struct varDefName_s		*hashNext;		// next name of the same hash
} varDefName_t;

struct varDef_s {
	int						num;			// global index/ID of variable
	varEval_t				value;
	varDef_t				*scope;			// function, namespace, or object the var was defined in
	int						numUsers;		// number of users if this is a constant
	defInitialized_t		initialized;

	typeDef_t				*typeDef;
	varDefName_t			*name;			// name of this var
	varDef_t				*next;			// next var with the same name
};

const char		*VarDef_Name( const varDef_t *self );
const char		*VarDef_GlobalName( const varDef_t *self );
#define			VarDef_Type( d )	( ( d )->typeDef ? ( d )->typeDef->type : ev_void )
int				VarDef_DepthOfScope( const varDef_t *self, const varDef_t *otherScope );
void			VarDef_SetFunction( varDef_t *self, function_t *func );
void			VarDef_SetObject( varDef_t *self, void *object );
void			VarDef_SetValue( varDef_t *self, const eval_t *value, qboolean constant );
void			VarDef_SetString( varDef_t *self, const char *string, qboolean constant );

// the basic types; function types are allocated
extern typeDef_t	type_void;
extern typeDef_t	type_scriptevent;
extern typeDef_t	type_namespace;
extern typeDef_t	type_string;
extern typeDef_t	type_float;
extern typeDef_t	type_vector;
extern typeDef_t	type_entity;
extern typeDef_t	type_field;
extern typeDef_t	type_function;
extern typeDef_t	type_virtualfunction;
extern typeDef_t	type_pointer;
extern typeDef_t	type_object;
extern typeDef_t	type_jumpoffset;	// only used for jump opcodes
extern typeDef_t	type_argsize;		// only used for function call and thread opcodes
extern typeDef_t	type_boolean;

extern varDef_t		def_void;
extern varDef_t		def_scriptevent;
extern varDef_t		def_namespace;
extern varDef_t		def_string;
extern varDef_t		def_float;
extern varDef_t		def_vector;
extern varDef_t		def_entity;
extern varDef_t		def_field;
extern varDef_t		def_function;
extern varDef_t		def_virtualfunction;
extern varDef_t		def_pointer;
extern varDef_t		def_object;
extern varDef_t		def_jumpoffset;
extern varDef_t		def_argsize;
extern varDef_t		def_boolean;

/*
===============================================================================

statements and opcodes

===============================================================================
*/
#define FLAG_OBJECTCALL_IMPL_NOT_PARSED_YET	1		// dhewm3's: an OP_OBJECTCALL to a method not parsed yet

typedef struct statement_s {
	unsigned short	op;
	unsigned short	flags;
	unsigned short	linenumber;
	unsigned short	file;
	varDef_t		*a;
	varDef_t		*b;
	varDef_t		*c;
} statement_t;

typedef struct opcode_s {
	const char	*name;
	const char	*opname;
	int			priority;
	qboolean	rightAssociative;
	varDef_t	*type_a;
	varDef_t	*type_b;
	varDef_t	*type_c;
} opcode_t;

enum {
	OP_RETURN,

	OP_UINC_F,
	OP_UINCP_F,
	OP_UDEC_F,
	OP_UDECP_F,
	OP_COMP_F,

	OP_MUL_F,
	OP_MUL_V,
	OP_MUL_FV,
	OP_MUL_VF,
	OP_DIV_F,
	OP_MOD_F,
	OP_ADD_F,
	OP_ADD_V,
	OP_ADD_S,
	OP_ADD_FS,
	OP_ADD_SF,
	OP_ADD_VS,
	OP_ADD_SV,
	OP_SUB_F,
	OP_SUB_V,

	OP_EQ_F,
	OP_EQ_V,
	OP_EQ_S,
	OP_EQ_E,
	OP_EQ_EO,
	OP_EQ_OE,
	OP_EQ_OO,

	OP_NE_F,
	OP_NE_V,
	OP_NE_S,
	OP_NE_E,
	OP_NE_EO,
	OP_NE_OE,
	OP_NE_OO,

	OP_LE,
	OP_GE,
	OP_LT,
	OP_GT,

	OP_INDIRECT_F,
	OP_INDIRECT_V,
	OP_INDIRECT_S,
	OP_INDIRECT_ENT,
	OP_INDIRECT_BOOL,
	OP_INDIRECT_OBJ,

	OP_ADDRESS,

	OP_EVENTCALL,
	OP_OBJECTCALL,
	OP_SYSCALL,

	OP_STORE_F,
	OP_STORE_V,
	OP_STORE_S,
	OP_STORE_ENT,
	OP_STORE_BOOL,
	OP_STORE_OBJENT,
	OP_STORE_OBJ,
	OP_STORE_ENTOBJ,

	OP_STORE_FTOS,
	OP_STORE_BTOS,
	OP_STORE_VTOS,
	OP_STORE_FTOBOOL,
	OP_STORE_BOOLTOF,

	OP_STOREP_F,
	OP_STOREP_V,
	OP_STOREP_S,
	OP_STOREP_ENT,
	OP_STOREP_FLD,
	OP_STOREP_BOOL,
	OP_STOREP_OBJ,
	OP_STOREP_OBJENT,

	OP_STOREP_FTOS,
	OP_STOREP_BTOS,
	OP_STOREP_VTOS,
	OP_STOREP_FTOBOOL,
	OP_STOREP_BOOLTOF,

	OP_UMUL_F,
	OP_UMUL_V,
	OP_UDIV_F,
	OP_UDIV_V,
	OP_UMOD_F,
	OP_UADD_F,
	OP_UADD_V,
	OP_USUB_F,
	OP_USUB_V,
	OP_UAND_F,
	OP_UOR_F,

	OP_NOT_BOOL,
	OP_NOT_F,
	OP_NOT_V,
	OP_NOT_S,
	OP_NOT_ENT,

	OP_NEG_F,
	OP_NEG_V,

	OP_INT_F,
	OP_IF,
	OP_IFNOT,

	OP_CALL,
	OP_THREAD,
	OP_OBJTHREAD,

	OP_PUSH_F,
	OP_PUSH_V,
	OP_PUSH_S,
	OP_PUSH_ENT,
	OP_PUSH_OBJ,
	OP_PUSH_OBJENT,
	OP_PUSH_FTOS,
	OP_PUSH_BTOF,
	OP_PUSH_FTOB,
	OP_PUSH_VTOS,
	OP_PUSH_BTOS,

	OP_GOTO,

	OP_AND,
	OP_AND_BOOLF,
	OP_AND_FBOOL,
	OP_AND_BOOLBOOL,
	OP_OR,
	OP_OR_BOOLF,
	OP_OR_FBOOL,
	OP_OR_BOOLBOOL,

	OP_BITAND,
	OP_BITOR,

	OP_BREAK,			// placeholder op.  not used in final code
	OP_CONTINUE,		// placeholder op.  not used in final code

	NUM_OPCODES
};

extern const opcode_t	opcodes[];

#define RESULT_STRING	"<RESULT>"

/*
===============================================================================

the program (idProgram): the scripts' types, variables, functions and code,
all compiled by one compiler into one program

===============================================================================
*/
extern varDef_t		*program_returnDef;
extern varDef_t		*program_returnStringDef;

void			Program_Startup( const char *defaultScript );
void			Program_Restart( void );
void			Program_FreeData( void );
qboolean		Program_CompileText( const char *source, const char *text, qboolean console );
const function_t *Program_CompileFunction( const char *functionName, const char *text );
void			Program_CompileFile( const char *filename );
void			Program_BeginCompilation( void );
void			Program_FinishCompilation( void );
void			Program_Disassemble( void );
int				Program_CalculateChecksum( void );

const char		*Program_GetFilename( int num );
int				Program_GetFilenum( const char *name );
int				Program_NumFilenames( void );
int				Program_GetLineNumberForStatement( int index );
const char		*Program_GetFilenameForStatement( int index );

typeDef_t		*Program_AllocType( const typeDef_t *type );
typeDef_t		*Program_AllocNewType( etype_t etype, varDef_t *edef, const char *ename, int esize, typeDef_t *aux );
typeDef_t		*Program_GetType( const typeDef_t *type, qboolean allocate );
typeDef_t		*Program_FindType( const char *name );

varDef_t		*Program_AllocDef( typeDef_t *type, const char *name, varDef_t *scope, qboolean constant );
varDef_t		*Program_GetDef( const typeDef_t *type, const char *name, const varDef_t *scope );
void			Program_FreeDef( varDef_t *d, const varDef_t *scope );
varDef_t		*Program_FindFreeResultDef( typeDef_t *type, const char *name, varDef_t *scope, const varDef_t *a, const varDef_t *b );
varDef_t		*Program_GetDefList( const char *name );
int				Program_NumVarDefs( void );
varDef_t		*Program_GetVarDef( int index );

function_t		*Program_FindFunction( const char *name );						// NULL if not found
function_t		*Program_FindObjectFunction( const char *name, const typeDef_t *type );	// NULL if not found
function_t		*Program_AllocFunction( varDef_t *def );
function_t		*Program_GetFunction( int index );
int				Program_GetFunctionIndex( const function_t *func );
int				Program_NumFunctions( void );

void			Program_SetEntity( const char *name, gentity_t *ent );

statement_t		*Program_AllocStatement( void );
statement_t		*Program_GetStatement( int index );
int				Program_NumStatements( void );

int				Program_GetReturnedInteger( void );
void			Program_ReturnFloat( float value );
void			Program_ReturnInteger( int value );
void			Program_ReturnVector( const vec3_t vec );
void			Program_ReturnString( const char *string );
void			Program_ReturnEntity( gentity_t *ent );

void			Program_Save( void );			// with g_save.c's G_SaveBytes
qboolean		Program_Restore( void );		// qfalse: the scripts aren't those of the save

// g_script_compiler.c
qboolean		Compiler_CompileFile( const char *text, const char *filename, qboolean console );
const char		*Compiler_LastError( void );	// what a failed Compiler_CompileFile says
void QDECL		Script_CompileError( const char *fmt, ... ) __attribute__ ((noreturn, format (printf, 1, 2)));

/*
===============================================================================

events (idEventDef): the game's functions scripts call, on an entity
($door.open()) or the system (sys.wait( 1 ))

The format: d an integer, f a float, v a vector, s a string, e an entity,
E an entity or $null_entity, t a trace (not from scripts).

===============================================================================
*/
#define D_EVENT_MAXARGS			8

#define D_EVENT_VOID			( ( char )0 )
#define D_EVENT_INTEGER			'd'
#define D_EVENT_FLOAT			'f'
#define D_EVENT_VECTOR			'v'
#define D_EVENT_STRING			's'
#define D_EVENT_ENTITY			'e'
#define D_EVENT_ENTITY_NULL		'E'			// event can handle NULL entity pointers
#define D_EVENT_TRACE			't'

typedef void ( *sysEventFunc_t )( scriptThread_t *thread, const intptr_t *args );
typedef void ( *entityEventFunc_t )( gentity_t *self, const intptr_t *args );

struct eventDef_s {
	const char			*name;
	const char			*formatspec;
	char				returnType;
	sysEventFunc_t		sysFunc;			// sys.<name>(), on the thread
	entityEventFunc_t	entityFunc;			// <entity>.<name>()
};

// an event's arguments
#define EVARG_INT( i )		( ( int )args[ i ] )
#define EVARG_FLOAT( i )	( *( const float * )&args[ i ] )
#define EVARG_VECTOR( i )	( ( float * )args[ i ] )
#define EVARG_STRING( i )	( ( const char * )args[ i ] )
#define EVARG_ENTITY( i )	( ( gentity_t * )args[ i ] )

// the events a thread (sys) and the entities respond to, merged by name
// into the eventDef_ts
typedef struct {
	const char			*name;
	const char			*formatspec;
	char				returnType;
	sysEventFunc_t		func;
} sysEventCallback_t;

typedef struct {
	const char			*name;
	const char			*formatspec;
	char				returnType;
	entityEventFunc_t	func;
} entityEventCallback_t;

extern const sysEventCallback_t		threadEventCallbacks[];		// g_script_thread.c
extern const entityEventCallback_t	entityEventCallbacks[];		// g_script_events.c

const eventDef_t *Event_FindEvent( const char *name );
qboolean		Event_EntityRespondsTo( const gentity_t *ent, const eventDef_t *ev );
void			Event_Init( void );

/*
===============================================================================

the interpreter (idInterpreter): a thread's stack and where it is

===============================================================================
*/
#define MAX_STACK_DEPTH		64
#define LOCALSTACK_SIZE		( 6144 * 2 )

typedef struct prstack_s {
	int					s;
	const function_t	*f;
	int					stackbase;
} prstack_t;

typedef struct interpreter_s {
	byte				localstack[ LOCALSTACK_SIZE ];	// first, for its alignment
	prstack_t			callStack[ MAX_STACK_DEPTH ];
	int					callStackDepth;
	int					maxStackDepth;

	int					localstackUsed;
	int					localstackBase;
	int					maxLocalstackUsed;

	const function_t	*currentFunction;
	int					instructionPointer;

	int					popParms;
	const eventDef_t	*multiFrameEvent;
	gentity_t			*eventEntity;

	scriptThread_t		*thread;

	qboolean			doneProcessing;
	qboolean			threadDying;
	qboolean			terminateOnExit;
	qboolean			debug;
} interpreter_t;

void			Interp_Init( interpreter_t *self );
void			Interp_Reset( interpreter_t *self );
void			Interp_StackTrace( const interpreter_t *self );
int				Interp_CurrentLine( const interpreter_t *self );
const char		*Interp_CurrentFile( const interpreter_t *self );
void QDECL		Interp_Error( interpreter_t *self, const char *fmt, ... ) __attribute__ ((noreturn, format (printf, 2, 3)));
void QDECL		Interp_Warning( interpreter_t *self, const char *fmt, ... ) __attribute__ ((format (printf, 2, 3)));
void			Interp_DisplayInfo( const interpreter_t *self );
qboolean		Interp_BeginMultiFrameEvent( interpreter_t *self, gentity_t *ent, const eventDef_t *event );
void			Interp_EndMultiFrameEvent( interpreter_t *self, gentity_t *ent, const eventDef_t *event );
qboolean		Interp_MultiFrameEventInProgress( const interpreter_t *self );
void			Interp_ThreadCall( interpreter_t *self, interpreter_t *source, const function_t *func, int args );
void			Interp_EnterFunction( interpreter_t *self, const function_t *func, qboolean clearStack );
void			Interp_EnterObjectFunction( interpreter_t *self, gentity_t *ent, const function_t *func, qboolean clearStack );
qboolean		Interp_Execute( interpreter_t *self );
gentity_t		*Interp_GetEntity( int entnum );
scriptObject_t	*Interp_GetScriptObject( int entnum );
int				Interp_EntityNumber( const gentity_t *ent );	// an entity as the scripts hold it (0 none)
void			Interp_PushEntity( interpreter_t *self, gentity_t *ent );	// an argument of the function entered next

// the script debugger's (dhewm3's script-debugger-protocol)
qboolean		Interp_GetRegisterValue( interpreter_t *self, const char *name, char *out, int outSize, int scopeDepth );
qboolean		Interp_SetRegisterValue( interpreter_t *self, const char *name, const char *value, char *out, int outSize, int scopeDepth );
qboolean		Interp_SetNextStatementLine( interpreter_t *self, int line );
const varDef_t	*Interp_FindFrameDef( const function_t *func, const char *name, qboolean *inNamespace );
qboolean		Interp_FormatValue( const typeDef_t *type, const byte *ptr, char *out, int outSize );
qboolean		Interp_ParseValue( const typeDef_t *type, const char *text, byte *ptr, char *why, int whySize );

/*
===============================================================================

the script debugger (g_script_debug.c): breakpoints and steps, as dhewm3's
DebuggerServer stops a thread

===============================================================================
*/
void			ScriptDebug_Init( void );
void			ScriptDebug_Frame( void );
void			ScriptDebug_Shutdown( void );
qboolean		ScriptDebug_Update( interpreter_t *interpreter, int instructionPointer );
void			ScriptDebug_ScriptError( interpreter_t *interpreter, int instructionPointer, const char *message, qboolean isError );

/*
===============================================================================

threads (idThread)

===============================================================================
*/
struct scriptThread_s {
	interpreter_t		interpreter;			// first, for its stack's alignment

	scriptThread_t		*waitingForThread;
	int					waitingFor;				// an entity's number, ENTITYNUM_NONE none
	int					waitingUntil;
	int					threadNum;
	char				threadName[ MAX_QPATH ];
	int					lastExecuteTime;
	int					creationTime;
	qboolean			manualControl;

	int					executeTime;			// its EV_Thread_Execute: when it runs next, -1 not
	qboolean			removed;				// ended: deleted when the threads are run next
};

scriptThread_t	*Thread_New( void );
scriptThread_t	*Thread_NewFunction( const function_t *func );
scriptThread_t	*Thread_NewObjectFunction( gentity_t *self, const function_t *func );
void			Thread_Delete( scriptThread_t *thread );
void			Thread_ManualDelete( scriptThread_t *thread );
void			Thread_WaitMS( scriptThread_t *thread, int time );
void			Thread_WaitSec( scriptThread_t *thread, float time );
void			Thread_WaitFrame( scriptThread_t *thread );
void			Thread_CallFunction( scriptThread_t *thread, const function_t *func, qboolean clearStack );
void			Thread_CallObjectFunction( scriptThread_t *thread, gentity_t *self, const function_t *func, qboolean clearStack );
void			Thread_DisplayInfo( scriptThread_t *thread );
scriptThread_t	*Thread_GetThread( int num );
int				Thread_NumThreads( void );
scriptThread_t	*Thread_GetThreadByIndex( int index );
void			Thread_Restart( void );
void			Thread_ObjectMoveDone( int threadnum, gentity_t *obj );
void			Thread_EntityMoveDone( gentity_t *obj );
void			Thread_End( scriptThread_t *thread );
void			Thread_KillThreadByName( const char *name );
void			Thread_KillThread( int num );
qboolean		Thread_Execute( scriptThread_t *thread );
void			Thread_DelayedStart( scriptThread_t *thread, int delay );
qboolean		Thread_Start( scriptThread_t *thread );
void			Thread_SetThreadName( scriptThread_t *thread, const char *name );
void			Thread_Pause( scriptThread_t *thread );
void			Thread_ClearWaitFor( scriptThread_t *thread );
qboolean		Thread_IsWaiting( scriptThread_t *thread );
void QDECL		Thread_Error( scriptThread_t *thread, const char *fmt, ... ) __attribute__ ((noreturn, format (printf, 2, 3)));
void QDECL		Thread_Warning( scriptThread_t *thread, const char *fmt, ... ) __attribute__ ((format (printf, 2, 3)));
scriptThread_t	*Thread_CurrentThread( void );
int				Thread_CurrentThreadNum( void );
qboolean		Thread_BeginMultiFrameEvent( gentity_t *ent, const eventDef_t *event );
void			Thread_EndMultiFrameEvent( gentity_t *ent, const eventDef_t *event );
void			Thread_RunThreads( void );			// each frame: the threads whose time has come
void			Thread_ListThreads_f( void );
void			Thread_SaveAll( void );				// saved games, with g_save.c's writers
void			Thread_RestoreAll( void );

#define			Thread_ReturnString( text )		Program_ReturnString( text )
#define			Thread_ReturnFloat( value )		Program_ReturnFloat( value )
#define			Thread_ReturnInt( value )		Program_ReturnFloat( ( float )( value ) )	// ints are floats in scripts
#define			Thread_ReturnVector( vec )		Program_ReturnVector( vec )
#define			Thread_ReturnEntity( ent )		Program_ReturnEntity( ent )

/*
===============================================================================

the game's use of them (g_script.c)

===============================================================================
*/
extern vmCvar_t	g_debugScript;
extern vmCvar_t	g_disasm;

gentity_t		*G_ScriptFindEntity( const char *name );		// $name: its targetname
gentity_t		*G_ScriptPlayer( void );						// $player1
void			G_ScriptActivate( gentity_t *ent, gentity_t *activator );	// EV_Activate: its use
qboolean		G_ScriptSetCallback( gentity_t *ent );		// EV_Thread_SetCallback: sys.waitFor() waits for it
gentity_t		*G_SpawnEntityFromKeys( int numKeys, const char **keys, const char **values );	// g_spawn.c
const char		*G_ScriptGetSpawnArg( gentity_t *ent, const char *key, const char *defaultValue );
void			G_ScriptSetSpawnArg( gentity_t *ent, const char *key, const char *value );
void			G_ScriptPlayerSpawned( gentity_t *ent );		// $player1...
void			G_ScriptWorldSpawn( void );						// the map's script
void			G_ScriptSetEntityNames( void );
void			G_ScriptCallTargets( gentity_t *ent, gentity_t *activator );	// its "call" key
void			Use_CallObjectFunction( gentity_t *self, gentity_t *other, gentity_t *activator );
void			SP_target_callobjectfunction( gentity_t *self );
void			G_ScriptWriteEntity( const gentity_t *ent );		// saved games (g_save.c)
void			G_ScriptReadEntity( gentity_t *ent, int entityNum );
void			G_ScriptWriteGame( void );
void			G_ScriptReadGame( void );
void			G_ScriptLoading( qboolean loading );
int				G_ScriptListSpawnArgs( gentity_t *ent, const char **keys, char values[][ MAX_STRING_LEN + 16 ], int max, int valueSize );
void			G_ScriptInit( const char *mapname );		// the default script, then the map's
void			G_ScriptShutdown( void );
void			G_ScriptFrame( void );
void			G_ScriptEntitySpawned( gentity_t *ent );	// $name, its script object
void			G_ScriptEntityFreed( gentity_t *ent );
void			G_ScriptCallFunction( const char *funcname, gentity_t *activator );
qboolean		G_ScriptCallObjectFunction( gentity_t *ent, const char *funcname, gentity_t *other, float *result );
void			G_Script_f( void );						// the console's "script <text>"
void			*Script_Alloc( int size );
void			Script_Free( void *ptr );
char			*Script_CopyString( const char *s );

#endif /* !__G_SCRIPT_H__ */
