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

In addition, Spearmint Source Code is also subject to certain additional terms.
You should have received a copy of these additional terms immediately following
the terms and conditions of the GNU General Public License.  If not, please
request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional
terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc.,
Suite 120, Rockville, Maryland 20850 USA.
===========================================================================
*/

/*****************************************************************************
 * name:		l_log.c
 *
 * desc:		log file
 *
 * $Archive: /MissionPack/CODE/botlib/l_log.c $
 *
 *****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "idlib_local.h"
#include "l_libvar.h"
#include "l_log.h"

#define MAX_LOGFILENAMESIZE		1024

typedef struct logfile_s
{
	char filename[MAX_LOGFILENAMESIZE];
	fileHandle_t fp;
	int numwrites;
} logfile_t;

static logfile_t logfile;

//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void Log_Open(char *filename)
{
	if (!LibVarValue("log", "0")) return;
	if (!filename || !strlen(filename))
	{
		ii.Com_Printf("openlog <filename>\n");
		return;
	} //end if
	if (logfile.fp)
	{
		ii.Com_Printf("log file %s is already opened\n", logfile.filename);
		return;
	} //end if
	if ( ii.FS_FOpenFile( filename, &logfile.fp, FS_WRITE ) ) {
		ii.Com_Error( ERR_DROP, "can't open the log file %s\n", filename );
		return;
	}
	Q_strncpyz(logfile.filename, filename, MAX_LOGFILENAMESIZE);
	ii.Com_Printf("Opened log %s\n", logfile.filename);
} //end of the function Log_Create
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void Log_Close(void)
{
	if (!logfile.fp) return;
	ii.FS_FCloseFile( logfile.fp );
	logfile.fp = 0;
	ii.Com_Printf("Closed log %s\n", logfile.filename);
} //end of the function Log_Close
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void Log_Shutdown(void)
{
	if (logfile.fp) Log_Close();
} //end of the function Log_Shutdown
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
extern int FS_WriteFloatStringImpl( char *buf, const char *fmt, va_list argPtr );
void QDECL Log_Write(char *fmt, ...)
{
	char buf[MAX_STRING_CHARS];
	int len;
	va_list argPtr;

	if (!logfile.fp) {
		return;
	}

	va_start( argPtr, fmt );
	len = FS_WriteFloatStringImpl( buf, fmt, argPtr );
	va_end( argPtr );

	ii.FS_Write( buf, len, logfile.fp );
	// ii.FS_Flush( logfile.fp ); // TODO: make it flush
} //end of the function Log_Write
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void QDECL Log_WriteTimeStamped(char *fmt, ...)
{
	char buf[MAX_STRING_CHARS];
	int len;
	va_list argPtr;
	int time;

	if (!logfile.fp) return;

	time = ii.MilliSeconds();

	Log_Write("%d   %02d:%02d:%02d:%02d   ",
					logfile.numwrites,
					(int) (time / 60 / 60),
					(int) (time / 60),
					(int) (time),
					(int) ((int) (time * 100)) -
							((int) time) * 100);

	va_start( argPtr, fmt );
	len = FS_WriteFloatStringImpl( buf, fmt, argPtr );
	va_end( argPtr );

	ii.FS_Write( buf, len, logfile.fp );
	logfile.numwrites++;
	// ii.FS_Flush( logfile.fp ); // TODO: make it flush
} //end of the function Log_Write
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
fileHandle_t Log_FilePointer(void)
{
	return logfile.fp;
} //end of the function Log_FilePointer
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void Log_Flush(void)
{
    // TODO: make this work
	//if (logfile.fp) fflush(logfile.fp);
} //end of the function Log_Flush

