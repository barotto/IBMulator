/*
 * 	Copyright (c) 2015  Marco Bortolin
 *
 *	This file is part of IBMulator
 *
 *  IBMulator is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 3 of the License, or
 *	(at your option) any later version.
 *
 *	IBMulator is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ibmulator.h"
#include "file_interface.h"
#include <stdio.h>

using namespace Rocket::Core;

RocketFileInterface::RocketFileInterface(const String & _root)
:
root(_root)
{

}

// Opens a file.
FileHandle RocketFileInterface::Open(const String& path)
{
	if(path == "") return NULL;

	// Attempt to open the file relative to the application's root.
	FILE* fp = fopen((root + path).CString(), "rb");
	if (fp != NULL) {
		return (FileHandle) fp;
	}
	// Attempt to open the file relative to the current working directory.
	fp = fopen(path.CString(), "rb");
	return (FileHandle) fp;
}

// Closes a previously opened file.
void RocketFileInterface::Close(FileHandle file)
{
	fclose((FILE*) file);
}

// Reads data from a previously opened file.
size_t RocketFileInterface::Read(void* buffer, size_t size, FileHandle file)
{
	return fread(buffer, 1, size, (FILE*) file);
}

// Seeks to a point in a previously opened file.
bool RocketFileInterface::Seek(FileHandle file, long offset, int origin)
{
	return fseek((FILE*) file, offset, origin) == 0;
}

// Returns the current position of the file pointer.
size_t RocketFileInterface::Tell(FileHandle file)
{
	return ftell((FILE*) file);
}
