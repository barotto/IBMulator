/*
 * Copyright (C) 2015-2021  Marco Bortolin
 *
 * This file is part of IBMulator.
 *
 * IBMulator is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * IBMulator is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ibmulator.h"
#include "file_interface.h"
#include "filesys.h"
#include <stdio.h>

RmlFileInterface::RmlFileInterface(const std::string &_root)
:
root(_root)
{

}

// Opens a file.
Rml::FileHandle RmlFileInterface::Open(const std::string &_path)
{
	if(_path == "") return 0;

	// Attempt to open the file relative to the application's root.
	FILE* fp = FileSys::fopen((root + _path), "rb");
	if(fp != nullptr) {
		return reinterpret_cast<Rml::FileHandle>(fp);
	}
	// Attempt to open the file relative to the current working directory.
	fp = FileSys::fopen(_path, "rb");
	return reinterpret_cast<Rml::FileHandle>(fp);
}

// Closes a previously opened file.
void RmlFileInterface::Close(Rml::FileHandle _file)
{
	fclose(reinterpret_cast<FILE*>(_file));
}

// Reads data from a previously opened file.
size_t RmlFileInterface::Read(void* _buffer, size_t _size, Rml::FileHandle _file)
{
	return fread(_buffer, 1, _size, reinterpret_cast<FILE*>(_file));
}

// Seeks to a point in a previously opened file.
bool RmlFileInterface::Seek(Rml::FileHandle _file, long _offset, int _origin)
{
	return fseek(reinterpret_cast<FILE*>(_file), _offset, _origin) == 0;
}

// Returns the current position of the file pointer.
size_t RmlFileInterface::Tell(Rml::FileHandle _file)
{
	return ftell(reinterpret_cast<FILE*>(_file));
}
