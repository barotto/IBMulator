/*
 * 	Copyright (c) 2005-2014  The Bochs Project
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

#ifndef IBMULATOR_HW_HDIMAGE_H
#define IBMULATOR_HW_HDIMAGE_H

#ifdef _WIN32
#include "wincompat.h"
#endif

enum {
  HDIMAGE_MODE_FLAT,
  HDIMAGE_MODE_VVFAT,

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


#define STANDARD_HEADER_MAGIC     "Bochs Virtual HD Image"
#define STANDARD_HEADER_V1        (0x00010000)
#define STANDARD_HEADER_VERSION   (0x00020000)
#define STANDARD_HEADER_SIZE      (512)


// WARNING : headers are kept in x86 (little) endianness
typedef struct
{
	uint8_t   magic[32];
	uint8_t   type[16];
	uint8_t   subtype[16];
	uint32_t  version;
	uint32_t  header;
} standard_header_t;


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
#define htod16(val) ((((val)&0xff00)>>8) | (((val)&0xff)<<8))
#define dtoh16(val) htod16(val)
#define htod32(val) bx_bswap32(val)
#define dtoh32(val) htod32(val)
#define htod64(val) bx_bswap64(val)
#define dtoh64(val) htod64(val)
#endif

class MediaImage;

int read_image(int fd, int64_t offset, void *buf, int count);
int write_image(int fd, int64_t offset, void *buf, int count);
#ifndef _WIN32
int hdimage_open_file(const char *pathname, int flags, uint64_t *fsize, time_t *mtime);
#else
int hdimage_open_file(const char *pathname, int flags, uint64_t *fsize, FILETIME *mtime);
#endif
int hdimage_detect_image_mode(const char *pathname);
bool hdimage_backup_file(int fd, const char *backup_fname);
bool hdimage_copy_file(const char *src, const char *dst);
#ifndef _WIN32
uint16_t fat_datetime(time_t time, int return_time);
#else
uint16_t fat_datetime(FILETIME time, int return_time);
#endif


class MediaImage
{
protected:

#ifndef _WIN32
	time_t mtime;
#else
	FILETIME mtime;
#endif

public:

	unsigned cylinders;
	unsigned heads;
	unsigned spt;
	uint64_t hd_size;

public:
	// Default constructor
	MediaImage();
	virtual ~MediaImage() {}

	// Open a image. Returns non-negative if successful.
	virtual int open(const char* pathname);

	// Open an image with specific flags. Returns non-negative if successful.
	virtual int open(const char* pathname, int flags) = 0;

	// Close the image.
	virtual void close() = 0;

	// Position ourselves. Return the resulting offset from the
	// beginning of the file.
	virtual int64_t lseek(int64_t offset, int whence) = 0;

	// Read count bytes to the buffer buf. Return the number of
	// bytes read (count).
	virtual ssize_t read(void* buf, size_t count) = 0;

	// Write count bytes from buf. Return the number of bytes
	// written (count).
	virtual ssize_t write(const void* buf, size_t count) = 0;

	// Get image capabilities
	virtual uint32_t get_capabilities();

	// Get modification time in FAT format
	virtual uint32_t get_timestamp();

	// Check image format
	static int check_format(int, uint64_t) {return HDIMAGE_NO_SIGNATURE;}

	// Save/restore support
	virtual bool save_state(const char *) {return 0;}
	virtual void restore_state(const char *) {}
};


/*******************************************************************************
 * only FLAT MODE currently implemented
 */
class FlatMediaImage : public MediaImage
{
private:

	int fd;
	const char *pathname;

public:

	// Open an image with specific flags. Returns non-negative if successful.
	int open(const char* pathname, int flags);

	// Close the image.
	void close();

	// Position ourselves. Return the resulting offset from the
	// beginning of the file.
	int64_t lseek(int64_t offset, int whence);

	// Read count bytes to the buffer buf. Return the number of
	// bytes read (count).
	ssize_t read(void* buf, size_t count);

	// Write count bytes from buf. Return the number of bytes
	// written (count).
	ssize_t write(const void* buf, size_t count);

	// Check image format
	static int check_format(int fd, uint64_t imgsize);

	// Save/restore support
	bool save_state(const char *backup_fname);
	void restore_state(const char *backup_fname);

};


/*******************************************************************************
 * REDOLOG class (currently used for vvfat)
 */

#define REDOLOG_TYPE "Redolog"
#define REDOLOG_SUBTYPE_UNDOABLE "Undoable"
#define REDOLOG_SUBTYPE_VOLATILE "Volatile"
#define REDOLOG_SUBTYPE_GROWING  "Growing"

#define REDOLOG_PAGE_NOT_ALLOCATED (0xffffffff)

#define UNDOABLE_REDOLOG_EXTENSION ".redolog"
#define UNDOABLE_REDOLOG_EXTENSION_LENGTH (strlen(UNDOABLE_REDOLOG_EXTENSION))
#define VOLATILE_REDOLOG_EXTENSION ".XXXXXX"
#define VOLATILE_REDOLOG_EXTENSION_LENGTH (strlen(VOLATILE_REDOLOG_EXTENSION))

typedef struct
{
	// the fields in the header are kept in little endian
	uint32_t  catalog;    // #entries in the catalog
	uint32_t  bitmap;     // bitmap size in bytes
	uint32_t  extent;     // extent size in bytes
	uint32_t  timestamp;  // modification time in FAT format (subtype 'undoable' only)
	uint64_t  disk;       // disk size in bytes
} redolog_specific_header_t;

typedef struct
{
	// the fields in the header are kept in little endian
	uint32_t  catalog;    // #entries in the catalog
	uint32_t  bitmap;     // bitmap size in bytes
	uint32_t  extent;     // extent size in bytes
	uint64_t  disk;       // disk size in bytes
} redolog_specific_header_v1_t;

typedef struct
{
	standard_header_t standard;
	redolog_specific_header_t specific;

	uint8_t padding[STANDARD_HEADER_SIZE - (sizeof (standard_header_t) + sizeof (redolog_specific_header_t))];
} redolog_header_t;

typedef struct
{
	standard_header_t standard;
	redolog_specific_header_v1_t specific;

	uint8_t padding[STANDARD_HEADER_SIZE - (sizeof (standard_header_t) + sizeof (redolog_specific_header_v1_t))];
} redolog_header_v1_t;


class RedoLog
{

private:

	void print_header();
	int fd;
	redolog_header_t header;     // Header is kept in x86 (little) endianness
	uint32_t *catalog;
	uint8_t *bitmap;
	bool bitmap_update;
	uint32_t extent_index;
	uint32_t extent_offset;
	uint32_t extent_next;

	uint32_t bitmap_blocks;
	uint32_t extent_blocks;

	int64_t imagepos;

public:

	RedoLog();

	int make_header(const char* type, uint64_t size);
	int create(const char* filename, const char* type, uint64_t size);
	int create(int filedes, const char* type, uint64_t size);
	int open(const char* filename, const char* type);
	int open(const char* filename, const char* type, int flags);
	void close();
	uint64_t get_size();
	uint32_t get_timestamp();
	bool set_timestamp(uint32_t timestamp);

	int64_t lseek(int64_t offset, int whence);
	ssize_t read(void* buf, size_t count);
	ssize_t write(const void* buf, size_t count);

	static int check_format(int fd, const char *subtype);

	bool save_state(const char *backup_fname);
};

#endif
