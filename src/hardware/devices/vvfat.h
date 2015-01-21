/////////////////////////////////////////////////////////////////////////
//
// Virtual VFAT image support (shadows a local directory)
// ported from QEMU block driver with some additions (see vvfat.cc)
//
// Copyright (c) 2004,2005  Johannes E. Schindelin
// Copyright (C) 2010-2012  The Bochs Project
// Copyright (c) 2015  Marco Bortolin
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
/////////////////////////////////////////////////////////////////////////

#ifndef IBMULATOR_HW_VVFAT_H
#define IBMULATOR_HW_VVFAT_H

#include "mediaimage.h"

typedef struct array_t {
	char *pointer;
	unsigned int size, next, item_size;
} array_t;

typedef struct {
	uint8_t head;
	uint8_t sector;
	uint8_t cylinder;
} mbr_chs_t;

#if defined(_MSC_VER) && (_MSC_VER<1300)
	#pragma pack(push, 1)
#elif defined(__MWERKS__) && defined(__MACH__)
	#pragma options align=packed
#endif

typedef MSC_ALIGN(1) struct direntry_t {
    uint8_t name[8];
    uint8_t extension[3];
    uint8_t attributes;
    uint8_t reserved[2];
    uint16_t ctime;
    uint16_t cdate;
    uint16_t adate;
    uint16_t begin_hi;
    uint16_t mtime;
    uint16_t mdate;
    uint16_t begin;
    uint32_t size;
} GCC_ATTRIBUTE(packed) direntry_t;

#if defined(_MSC_VER) && (_MSC_VER<1300)
	#pragma pack(pop)
#elif defined(__MWERKS__) && defined(__MACH__)
	#pragma options align=reset
#endif

// this structure are used to transparently access the files

enum {
	MODE_UNDEFINED = 0,
	MODE_NORMAL    = 1,
	MODE_MODIFIED  = 2,
	MODE_DIRECTORY = 4,
	MODE_FAKED     = 8,
	MODE_DELETED   = 16,
	MODE_RENAMED   = 32
};

typedef struct mapping_t {
	// begin is the first cluster, end is the last+1
	uint32_t begin, end;
	// as this->directory is growable, no pointer may be used here
	unsigned int dir_index;
	// the clusters of a file may be in any order; this points to the first
	int first_mapping_index;
	union {
		/* offset is
		 * - the offset in the file (in clusters) for a file, or
		 * - the next cluster of the directory for a directory, and
		 * - the address of the buffer for a faked entry
		 */
		struct {
			uint32_t offset;
		} file;
		struct {
			int parent_mapping_index;
			int first_dir_index;
		} dir;
	} info;
	// path contains the full path, i.e. it always starts with vvfat_path
	char *path;

	uint8_t mode;

	int read_only;
} mapping_t;

class VVFATMediaImage : public MediaImage
{
private:
	uint8_t  *first_sectors;
	uint32_t offset_to_bootsector;
	uint32_t offset_to_fat;
	uint32_t offset_to_root_dir;
	uint32_t offset_to_data;

	uint16_t cluster_size;
	uint8_t  sectors_per_cluster;
	uint32_t sectors_per_fat;
	uint32_t sector_count;
	uint32_t cluster_count; // total number of clusters of this partition
	uint32_t max_fat_value;
	uint32_t first_cluster_of_root_dir;
	uint16_t root_entries;
	uint16_t reserved_sectors;

	uint8_t fat_type;
	array_t fat, directory, mapping;

	int       current_fd;
	mapping_t *current_mapping;
	uint8_t   *cluster; // points to current cluster
	uint8_t   *cluster_buffer; // points to a buffer to hold temp data
	uint16_t  current_cluster;

	const char *vvfat_path;
	uint32_t sector_num;

	bool use_mbr_file;
	bool use_boot_file;
	FILE *vvfat_attr_fd;

	bool    vvfat_modified;
	void    *fat2;
	RedoLog *redolog;       // Redolog instance
	char    *redolog_name;  // Redolog name
	char    *redolog_temp;  // Redolog temporary file name

	bool m_commit;

	bool sector2CHS(uint32_t spos, mbr_chs_t *chs);
	void init_mbr();
	direntry_t* create_long_filename(const char* filename);
	void fat_set(unsigned int cluster, uint32_t value);
	void init_fat();
	direntry_t* create_short_and_long_name(unsigned int directory_start,
			const char* filename, int is_dot);
	int read_directory(int mapping_index);
	uint32_t sector2cluster(off_t sector_num);
	off_t cluster2sector(uint32_t cluster_num);
	int init_directories(const char* dirname);
	bool read_sector_from_file(const char *path, uint8_t *buffer, uint32_t sector);
	void set_file_attributes(void);
	uint32_t fat_get_next(uint32_t current);
	bool write_file(const char *path, direntry_t *entry, bool create);
	direntry_t* read_direntry(uint8_t *buffer, char *filename);
	void parse_directory(const char *path, uint32_t start_cluster);
	void commit_changes(void);
	void close_current_file(void);
	int open_file(mapping_t* mapping);
	int find_mapping_for_cluster_aux(int cluster_num, int index1, int index2);
	mapping_t* find_mapping_for_cluster(int cluster_num);
	mapping_t* find_mapping_for_path(const char* path);
	int read_cluster(int cluster_num);

	static int mkdir(const char *path);
	static int rmdir(const char *path);

public:
	VVFATMediaImage(uint64_t size, const char* redolog_name, bool commit);
	virtual ~VVFATMediaImage();

	int open(const char* pathname, int flags);
	void close();
	int64_t lseek(int64_t offset, int whence);
	ssize_t read(void* buf, size_t count);
	ssize_t write(const void* buf, size_t count);
	uint32_t get_capabilities();

};

#endif
