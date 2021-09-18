/*
 * Copyright (c) 2005-2014  The Bochs Project
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

#ifndef IBMULATOR_HW_HDIMAGE_H
#define IBMULATOR_HW_HDIMAGE_H

#include "filesys.h"

#ifdef _WIN32
#include "wincompat.h"
#endif

struct MediaGeometry
{
	unsigned cylinders;
	unsigned heads;
	unsigned spt;
	int      wpcomp;
	unsigned lzone;

	bool operator==(const MediaGeometry &_geom) const {
		return cylinders==_geom.cylinders && heads==_geom.heads && spt==_geom.spt;
	}
};

enum {
	HDIMAGE_MODE_FLAT,

	HDIMAGE_MODE_LAST,
	HDIMAGE_MODE_UNKNOWN = -1
};

// required for access() checks
#ifndef F_OK
#define F_OK 0
#endif

// hdimage capabilities
#define HDIMAGE_READONLY      1
#define HDIMAGE_HAS_GEOMETRY  2
#define HDIMAGE_AUTO_GEOMETRY 4

// hdimage format check return values
#define HDIMAGE_FORMAT_OK      0
#define HDIMAGE_SIZE_ERROR    -1
#define HDIMAGE_READ_ERROR    -2
#define HDIMAGE_NO_SIGNATURE  -3
#define HDIMAGE_TYPE_ERROR    -4
#define HDIMAGE_VERSION_ERROR -5

// htod : convert host to disk (little) endianness
// dtoh : convert disk (little) to host endianness
#ifndef WORDS_BIGENDIAN
#define htod16(val) (val)
#define dtoh16(val) (val)
#define htod32(val) (val)
#define dtoh32(val) (val)
#define htod64(val) (val)
#define dtoh64(val) (val)
#else
#error BIG endianness not supported
#endif

class MediaImage;

int read_image(int fd, int64_t offset, void *buf, int count);
int write_image(int fd, int64_t offset, void *buf, int count);
int hdimage_open_file(const char *pathname, int flags, uint64_t *fsize, FILETIME *mtime);
int hdimage_detect_image_mode(const char *pathname);
bool hdimage_backup_file(int fd, const char *backup_fname);
bool hdimage_backup_file(int _from_fd, int _backup_fd);
bool hdimage_copy_file(const char *src, const char *dst);
uint16_t fat_datetime(FILETIME time, bool return_time);


class MediaImage
{
protected:

	MediaGeometry m_geometry;
	uint64_t m_size;
	FILETIME m_mtime;

public:
	// Default constructor
	MediaImage();
	virtual ~MediaImage() {}

	virtual MediaGeometry & geometry() { return m_geometry; }
	virtual uint64_t size() const { return m_size; }

	// Open a image. Returns non-negative if successful.
	virtual int open(const char *_pathname);

	// Open an image with specific flags. Returns non-negative if successful.
	virtual int open(const char *_pathname, int _flags) = 0;

	// Close the image.
	virtual void close() = 0;

	// Position ourselves. Return the resulting offset from the
	// beginning of the file.
	virtual int64_t lseek(int64_t _offset, int _whence) = 0;

	// Read count bytes to the buffer buf. Return the number of
	// bytes read (count).
	virtual ssize_t read(void *_buf, size_t _count) = 0;

	// Write count bytes from buf. Return the number of bytes
	// written (count).
	virtual ssize_t write(const void *_buf, size_t _count) = 0;

	// Get image capabilities
	virtual uint32_t get_capabilities();

	// Get modification time in FAT format
	virtual uint32_t get_timestamp();

	// Check image format
	static int check_format(int, uint64_t) { return HDIMAGE_NO_SIGNATURE; }

	// Save/restore support
	virtual bool save_state(const char *) { return 0; }
	virtual void restore_state(const char *) {}

	// Create a new image; doesn't open it
	virtual void create(const char*, unsigned) {}

	// Get the image name
	virtual std::string get_name() { return ""; }

	// Tells if the image is open
	virtual bool is_open() { return false; }
};


/*******************************************************************************
 * only FLAT MODE currently implemented
 */
class FlatMediaImage : public MediaImage
{
private:

	int m_fd;
	std::string m_pathname;

	bool is_valid();

public:

	FlatMediaImage();
	~FlatMediaImage();

	// Open an image with specific flags. Returns non-negative if successful.
	int open(const char* _pathname, int _flags);

	// Open a temporary read-write copy of _pathname image file.
	int open_temp(const char *_pathname, char *_template);

	// Close the image.
	void close();

	// Position ourselves. Return the resulting offset from the
	// beginning of the file.
	int64_t lseek(int64_t _offset, int _whence);

	// Read count bytes to the buffer buf. Return the number of
	// bytes read (count).
	ssize_t read(void* _buf, size_t _count);

	// Write count bytes from buf. Return the number of bytes
	// written (count).
	ssize_t write(const void* _buf, size_t _count);

	// Check image format
	static int check_format(int _fd, uint64_t _imgsize);

	// Save/restore support
	bool save_state(const char *_backup_fname);
	void restore_state(const char *_backup_fname);

	//Create a new flat image; doesn't open it
	void create(const char *_pathname, unsigned _sectors);

	//Get the image name
	std::string get_name() { return m_pathname; }

	bool is_open() { return (m_fd > -1); }
};

#endif
