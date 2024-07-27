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

#ifndef __IDLIB_PUBLIC_H__
#define __IDLIB_PUBLIC_H__

#define	IDLIB_API_VERSION		1

// id library exported functions

typedef struct idlib_import_s
{
	//get time for measuring time lapse
	int			(*MilliSeconds)(void);

	//print messages from the id library
	void		(QDECL *Com_Error)( int level, const char *error, ... ) __attribute__ ((noreturn, format(printf, 2, 3)));
	void		(QDECL *Com_Printf)( const char *msg, ... ) __attribute__ ((format (printf, 1, 2)));
	void		(QDECL *Com_DPrintf)( const char *msg, ... ) __attribute__ ((format (printf, 1, 2)));

	//memory allocation

#ifdef ZONE_DEBUG
#define GetMemory(size)					GetMemoryDebug(size, #size, __FILE__, __LINE__)
#define FreeMemory(ptr)					FreeMemoryDebug(ptr, #ptr, __FILE__, __LINE__)
	// dynamic memory allocator for things that need to be freed
	void	*(*GetMemoryDebug)( int bytes, char *label, char *file, int line );
	void	(*FreeMemoryDebug)( void *buf, char *label, char *file, int line );
#else
	// dynamic memory allocator for things that need to be freed
	void	*(*GetMemory)( int bytes );
	void	(*FreeMemory)( void *buf );
#endif
	int			(*AvailableMemory)(void);		// available Zone memory

	void	*(*HunkAlloc)( int size );

	//file system access
	int			(*FS_FOpenFile)( const char *qpath, fileHandle_t *file, fsMode_t mode );
	int			(*FS_Read)( void *buffer, int len, fileHandle_t f );
	int			(*FS_Write)( const void *buffer, int len, fileHandle_t f );
	void		(*FS_FCloseFile)( fileHandle_t f );
	int			(*FS_Seek)( fileHandle_t f, long offset, int origin );
	long		(*FS_ReadFile)( const char *qpath, void **buffer );
	void		(*FS_FreeFile)( void *buffer );
/*
	//debug visualisation stuff
	int			(*DebugLineCreate)(void);
	void		(*DebugLineDelete)(int line);
	void		(*DebugLineShow)(int line, vec3_t start, vec3_t end, int color);
	//
	int			(*DebugPolygonCreate)(int color, int numPoints, vec3_t *points);
	void		(*DebugPolygonDelete)(int id);
*/
} idlib_import_t;

unsigned	Com_BlockChecksum( const void *buffer, int length );
char		*Com_MD5File(const char *filename, int length, const char *prefix, int prefix_len);

typedef struct idlib_export_s
{
	void (*Init)( void );
	void (*Shutdown)( void );
} idlib_export_t;

//linking of id library
idlib_export_t *GetIdLibAPI( int apiVersion, idlib_import_t *import );

#endif /* !__IDLIB_PUBLIC_H_ */
