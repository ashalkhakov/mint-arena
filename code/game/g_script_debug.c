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
// g_script_debug.c -- the script debugger's side of the interpreter: each
// statement and each script error goes by here (dhewm3's updateGameDebugger
// and gameDebuggerScriptError)

#include "g_local.h"

/*
================
ScriptDebug_Update

before each statement; qtrue when a debugger is connected
================
*/
qboolean ScriptDebug_Update( interpreter_t *interpreter, int instructionPointer ) {
	return qfalse;
}

/*
================
ScriptDebug_ScriptError

a script's error or warning, before the game handles it
================
*/
void ScriptDebug_ScriptError( interpreter_t *interpreter, int instructionPointer, const char *message, qboolean isError ) {
}
