/*
 * Copyright (C) 2015-2019  Marco Bortolin
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

#ifndef IBMULATOR_GUI_FILE_INTERFACE_H
#define IBMULATOR_GUI_FILE_INTERFACE_H

#include <Rocket/Core/String.h>
#include <Rocket/Core/FileInterface.h>


class RocketFileInterface : public Rocket::Core::FileInterface
{
public:

	RocketFileInterface(const Rocket::Core::String & _root);

	/// Opens a file.
	virtual Rocket::Core::FileHandle Open(const Rocket::Core::String& path);

	/// Closes a previously opened file.
	virtual void Close(Rocket::Core::FileHandle file);

	/// Reads data from a previously opened file.
	virtual size_t Read(void* buffer, size_t size, Rocket::Core::FileHandle file);

	/// Seeks to a point in a previously opened file.
	virtual bool Seek(Rocket::Core::FileHandle file, long offset, int origin);

	/// Returns the current position of the file pointer.
	virtual size_t Tell(Rocket::Core::FileHandle file);

private:
	Rocket::Core::String root;
};

#endif
