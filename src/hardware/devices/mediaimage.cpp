/*
 * 	Copyright (c) 2002-2014  The Bochs Project
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
#include "mediaimage.h"
#include "filesys.h"

#if HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#ifdef __linux__
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#include "wincompat.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>


// helper functions
int read_image(int fd, int64_t offset, void *buf, int count)
{
	if(lseek(fd, offset, SEEK_SET) == -1) {
		return -1;
	}
	return read(fd, buf, count);
}

int write_image(int fd, int64_t offset, void *buf, int count)
{
	if(lseek(fd, offset, SEEK_SET) == -1) {
		return -1;
	}
	return write(fd, buf, count);
}

int hdimage_open_file(const char *_pathname, int _flags, uint64_t *_fsize, FILETIME *_mtime)
{
	if(FileSys::get_file_stats(_pathname,_fsize,_mtime) < 0) {
		return -1;
	}

	int fd = ::open(_pathname, _flags
#ifdef O_BINARY
			| O_BINARY
#endif
	);

	if(fd < 0) {
		return fd;
	}

	return fd;
}

int hdimage_open_temp(const char *_pathname, char *_template, int _flags,
		uint64_t *_fsize, FILETIME *_mtime)
{
	if(FileSys::get_file_stats(_pathname,_fsize,_mtime) < 0) {
		return -1;
	}

	int tmpfd = ::mkostemp(_template, _flags
#ifdef O_BINARY
			| O_BINARY
#endif
	);
	int srcfd;
	if(tmpfd >= 0) {
		srcfd = hdimage_open_file(_pathname, O_RDONLY, _fsize, NULL);
		if(srcfd >= 0) {
			if(!hdimage_backup_file(srcfd, tmpfd)) {
				::close(tmpfd);
				tmpfd = -1;
			}
			::close(srcfd);
		} else {
			::close(tmpfd);
			tmpfd = -1;
		}
	}

	return tmpfd;
}

int hdimage_detect_image_mode(const char *pathname)
{
	int result = HDIMAGE_MODE_UNKNOWN;
	uint64_t image_size = 0;

	int fd = hdimage_open_file(pathname, O_RDONLY, &image_size, NULL);
	if(fd < 0) {
		return result;
	}

	if(FlatMediaImage::check_format(fd, image_size) == HDIMAGE_FORMAT_OK) {
		result = HDIMAGE_MODE_FLAT;
	}
	::close(fd);

	return result;
}

// if return_time==0, this returns the fat_date, else the fat_time
#ifndef _WIN32
uint16_t fat_datetime(time_t time, int return_time)
{
  struct tm* t;
  struct tm t1;

  t = &t1;
  localtime_r(&time, t);
  if(return_time)
    return htod16((t->tm_sec/2) | (t->tm_min<<5) | (t->tm_hour<<11));
  return htod16((t->tm_mday) | ((t->tm_mon+1)<<5) | ((t->tm_year-80)<<9));
}
#else
uint16_t fat_datetime(FILETIME time, int return_time)
{
  SYSTEMTIME gmtsystime, systime;
  TIME_ZONE_INFORMATION tzi;

  FileTimeToSystemTime(&time, &gmtsystime);
  GetTimeZoneInformation(&tzi);
  SystemTimeToTzSpecificLocalTime(&tzi, &gmtsystime, &systime);
  if(return_time)
    return htod16((systime.wSecond/2) | (systime.wMinute<<5) | (systime.wHour<<11));
  return htod16((systime.wDay) | (systime.wMonth<<5) | ((systime.wYear-1980)<<9));
}
#endif

bool hdimage_backup_file(int _from_fd, int _backup_fd)
{
	char *buf;
	off_t offset;
	int nread, size;
	bool ret = true;

	offset = 0;
	size = 0x20000;
	buf = (char*)malloc(size);
	if(buf == NULL) {
		return false;
	}
	while((nread = read_image(_from_fd, offset, buf, size)) > 0) {
		if(write_image(_backup_fd, offset, buf, nread) < 0) {
			ret = false;
			break;
		}
		if(nread < size) {
			break;
		}
		offset += size;
	};
	if(nread < 0) {
		ret = false;
	}
	free(buf);

	return ret;
}

bool hdimage_backup_file(int fd, const char *backup_fname)
{
  int backup_fd = ::open(backup_fname, O_RDWR | O_CREAT | O_TRUNC
#ifdef O_BINARY
    | O_BINARY
#endif
    , S_IWUSR | S_IRUSR
#ifdef S_IRGRP
	| S_IRGRP | S_IWGRP
#endif
	);
  if(backup_fd >= 0) {
	  bool ret = hdimage_backup_file(fd, backup_fd);
	  ::close(backup_fd);
	  return ret;
  }
  return 0;
}


bool hdimage_copy_file(const char *src, const char *dst)
{
#ifdef _WIN32
  return (bool)CopyFile(src, dst, FALSE);
#elif defined(__linux__)
  pid_t pid, ws;

  if((src == NULL) || (dst == NULL)) {
    return 0;
  }

  if(!(pid = fork())) {
    execl("/bin/cp", "/bin/cp", src, dst, (char *)0);
    return 0;
  }
  wait(&ws);
  if(!WIFEXITED(ws)) {
    return -1;
  }
  return (WEXITSTATUS(ws) == 0);
#else
  int fd1, fd2;
  char *buf;
  off_t offset;
  int nread, size;
  bool ret = 1;

  fd1 = ::open(src, O_RDONLY
#ifdef O_BINARY
    | O_BINARY
#endif
    );
  if(fd1 < 0) return 0;
  fd2 = ::open(dst, O_RDWR | O_CREAT | O_TRUNC
#ifdef O_BINARY
    | O_BINARY
#endif
    , S_IWUSR | S_IRUSR | S_IRGRP | S_IWGRP);
  if(fd2 < 0) return 0;
  offset = 0;
  size = 0x20000;
  buf = (char*)malloc(size);
  if(buf == NULL) {
    ::close(fd1);
    ::close(fd2);
    return 0;
  }
  while ((nread = read_image(fd1, offset, buf, size)) > 0) {
    if(write_image(fd2, offset, buf, nread) < 0) {
      ret = 0;
      break;
    }
    if(nread < size) {
      break;
    }
    offset += size;
  };
  if(nread < 0) {
    ret = 0;
  }
  free(buf);
  ::close(fd1);
  ::close(fd2);
  return ret;
#endif
}


/*******************************************************************************
 * base class MediaImage
 */

MediaImage::MediaImage()
:
cylinders(0),
heads(0),
spt(0),
hd_size(0)
{
}

int MediaImage::open(const char* _pathname)
{
	return open(_pathname, O_RDWR);
}

uint32_t MediaImage::get_capabilities()
{
	return (cylinders == 0) ? HDIMAGE_AUTO_GEOMETRY : 0;
}

uint32_t MediaImage::get_timestamp()
{
	return (fat_datetime(mtime, 1) | (fat_datetime(mtime, 0) << 16));
}


/*******************************************************************************
 * FlatMediaImage
 */

FlatMediaImage::FlatMediaImage()
:
fd(-1)
{

}

FlatMediaImage::~FlatMediaImage()
{
	close();
}

int FlatMediaImage::open(const char* _pathname, int _flags)
{
	if((fd = hdimage_open_file(_pathname, _flags, &hd_size, &mtime)) < 0) {
		return -1;
	}
	if(!is_valid()) {
		close();
		return -1;
	}
	pathname = _pathname;
	return fd;
}

int FlatMediaImage::open_temp(const char* _pathname, char *_template)
{
	if((fd = hdimage_open_temp(_pathname, _template, O_RDWR, &hd_size, &mtime)) < 0) {
		return -1;
	}
	if(!is_valid()) {
		remove(_template);
		close();
		return -1;
	}
	pathname = _template;
	return fd;
}

void FlatMediaImage::close()
{
	if(fd > -1) {
		::close(fd);
		fd = -1;
	}
	hd_size = 0;
}

int64_t FlatMediaImage::lseek(int64_t offset, int whence)
{
	return (int64_t)::lseek(fd, (off_t)offset, whence);
}

ssize_t FlatMediaImage::read(void* buf, size_t count)
{
	return ::read(fd, (char*) buf, count);
}

ssize_t FlatMediaImage::write(const void* buf, size_t count)
{
	return ::write(fd, (char*) buf, count);
}

int FlatMediaImage::check_format(int fd, uint64_t imgsize)
{
	char buffer[512];

	if((imgsize <= 0) || ((imgsize % 512) != 0)) {
		return HDIMAGE_SIZE_ERROR;
	} else if(read_image(fd, 0, buffer, 512) < 0) {
		return HDIMAGE_READ_ERROR;
	} else {
		return HDIMAGE_FORMAT_OK;
	}
}

bool FlatMediaImage::save_state(const char *backup_fname)
{
	return hdimage_backup_file(fd, backup_fname);
}

void FlatMediaImage::restore_state(const char *backup_fname)
{
	close();
	if(!hdimage_copy_file(backup_fname, pathname.c_str())) {
		PERRF(LOG_HDD, "Failed to restore image '%s'\n", pathname.c_str());
		throw std::exception();
	}
	if(MediaImage::open(pathname.c_str()) < 0) {
		PERRF(LOG_HDD, "Failed to open restored image '%s'\n", pathname.c_str());
		throw std::exception();
	}
}

void FlatMediaImage::create(const char* _pathname, unsigned _sectors)
{
    std::ofstream ofs(_pathname, std::ios::binary | std::ios::out);
    if(!ofs.is_open()) {
    	throw std::exception();
    }
    ofs.seekp((_sectors*512) - 1);
    ofs.write("", 1);
    if(!ofs.good()) {
    	throw std::exception();
    }
}

bool FlatMediaImage::is_valid()
{
	PINFOF(LOG_V2, LOG_HDD, "image size: %llu\n", hd_size);

	if(hd_size <= 0) {
		PERRF(LOG_HDD, "size of disk image not detected / invalid\n");
		return false;
	}

	unsigned sectors = cylinders * heads * spt;
	if(hd_size != 512 * sectors) {
		PERRF(LOG_HDD, "size of disk image is wrong, %d bytes instead of %d bytes\n",
				hd_size, (512 * sectors));
		return false;
	}

	return true;
}


/*******************************************************************************
 * RedoLog
 */

RedoLog::RedoLog()
{
  fd = -1;
  catalog = NULL;
  bitmap = NULL;
  extent_index = (uint32_t)0;
  extent_offset = (uint32_t)0;
  extent_next = (uint32_t)0;
}

void RedoLog::print_header()
{
  PDEBUGF(LOG_V0, LOG_HDD, "redolog : Standard Header : magic='%s', type='%s', subtype='%s', version = %d.%d\n",
           header.standard.magic, header.standard.type, header.standard.subtype,
           dtoh32(header.standard.version)/0x10000,
           dtoh32(header.standard.version)%0x10000);
  if (dtoh32(header.standard.version) == STANDARD_HEADER_VERSION) {
    PDEBUGF(LOG_V0, LOG_HDD, "redolog : Specific Header : #entries=%d, bitmap size=%d, exent size = %d disk size = %lld\n",
             dtoh32(header.specific.catalog),
             dtoh32(header.specific.bitmap),
             dtoh32(header.specific.extent),
             dtoh64(header.specific.disk));
  } else if (dtoh32(header.standard.version) == STANDARD_HEADER_V1) {
    redolog_header_v1_t header_v1;
    memcpy(&header_v1, &header, STANDARD_HEADER_SIZE);
    PDEBUGF(LOG_V0, LOG_HDD, "redolog : Specific Header : #entries=%d, bitmap size=%d, exent size = %d disk size = %lld\n",
             dtoh32(header_v1.specific.catalog),
             dtoh32(header_v1.specific.bitmap),
             dtoh32(header_v1.specific.extent),
             dtoh64(header_v1.specific.disk));
  }
}

int RedoLog::make_header(const char* type, uint64_t size)
{
  uint32_t entries, extent_size, bitmap_size;
  uint64_t maxsize;
  uint32_t flip=0;

  // Set standard header values
  memset(&header, 0, sizeof(redolog_header_t));
  strcpy((char*)header.standard.magic, STANDARD_HEADER_MAGIC);
  strcpy((char*)header.standard.type, REDOLOG_TYPE);
  strcpy((char*)header.standard.subtype, type);
  header.standard.version = htod32(STANDARD_HEADER_VERSION);
  header.standard.header = htod32(STANDARD_HEADER_SIZE);

  entries = 512;
  bitmap_size = 1;

  // Compute #entries and extent size values
  do {
    extent_size = 8 * bitmap_size * 512;

    header.specific.catalog = htod32(entries);
    header.specific.bitmap = htod32(bitmap_size);
    header.specific.extent = htod32(extent_size);

    maxsize = (uint64_t)entries * (uint64_t)extent_size;

    flip++;

    if(flip&0x01) bitmap_size *= 2;
    else entries *= 2;
  } while (maxsize < size);

  header.specific.timestamp = 0;
  header.specific.disk = htod64(size);

  print_header();

  catalog = (uint32_t*)malloc(dtoh32(header.specific.catalog) * sizeof(uint32_t));
  bitmap = (uint8_t*)malloc(dtoh32(header.specific.bitmap));

  if ((catalog == NULL) || (bitmap==NULL))
    PERRF_ABORT(LOG_HDD, "redolog : could not malloc catalog or bitmap\n");

  for (uint32_t i=0; i<dtoh32(header.specific.catalog); i++)
    catalog[i] = htod32(REDOLOG_PAGE_NOT_ALLOCATED);

  bitmap_blocks = 1 + (dtoh32(header.specific.bitmap) - 1) / 512;
  extent_blocks = 1 + (dtoh32(header.specific.extent) - 1) / 512;

  PDEBUGF(LOG_V2, LOG_HDD, "redolog : each bitmap is %d blocks\n", bitmap_blocks);
  PDEBUGF(LOG_V2, LOG_HDD, "redolog : each extent is %d blocks\n", extent_blocks);

  return 0;
}

int RedoLog::create(const char* filename, const char* type, uint64_t size)
{
  PDEBUGF(LOG_V0, LOG_HDD, "redolog : creating redolog %s\n", filename);

  int filedes = ::open(filename, O_RDWR | O_CREAT | O_TRUNC
#ifdef O_BINARY
            | O_BINARY
#endif
            , S_IWUSR | S_IRUSR
#ifdef S_IRGRP
			| S_IRGRP | S_IWGRP
#endif
  );

  return create(filedes, type, size);
}

int RedoLog::create(int filedes, const char* type, uint64_t size)
{
  fd = filedes;

  if (fd < 0)
  {
    return -1; // open failed
  }

  if (make_header(type, size) < 0)
  {
    return -1;
  }

  ssize_t res;
  // Write header
  res = ::write(fd, &header, dtoh32(header.standard.header));
  ASSERT(unsigned(res)==dtoh32(header.standard.header));

  // Write catalog
  // FIXME could mmap
  res = ::write(fd, catalog, dtoh32(header.specific.catalog) * sizeof (uint32_t));
  ASSERT(unsigned(res)==dtoh32(header.specific.catalog) * sizeof (uint32_t));

  return 0;
}

int RedoLog::open(const char* filename, const char *type)
{
  return open(filename, type, O_RDWR);
}

int RedoLog::open(const char* filename, const char *type, int flags)
{
  uint64_t imgsize = 0;
#ifndef _WIN32
  time_t mtime;
#else
  FILETIME mtime;
#endif

  fd = hdimage_open_file(filename, flags, &imgsize, &mtime);
  if (fd < 0) {
    PDEBUGF(LOG_V0, LOG_HDD, "redolog : could not open image %s\n", filename);
    // open failed.
    return -1;
  }
  PDEBUGF(LOG_V0, LOG_HDD, "redolog : open image %s\n", filename);

  int res = check_format(fd, type);
  if (res != HDIMAGE_FORMAT_OK) {
    switch (res) {
      case HDIMAGE_READ_ERROR:
        PERRF_ABORT(LOG_HDD, "redolog : could not read header\n");
        break;
      case HDIMAGE_NO_SIGNATURE:
        PERRF_ABORT(LOG_HDD, "redolog : Bad header magic\n");
        break;
      case HDIMAGE_TYPE_ERROR:
        PERRF_ABORT(LOG_HDD, "redolog : Bad header type or subtype\n");
        break;
      case HDIMAGE_VERSION_ERROR:
        PERRF_ABORT(LOG_HDD, "redolog : Bad header version\n");
        break;
    }
    return -1;
  }

  if (read_image(fd, 0, &header, sizeof(header)) < 0) {
    return -1;
  }
  print_header();

  if (dtoh32(header.standard.version) == STANDARD_HEADER_V1) {
    redolog_header_v1_t header_v1;

    memcpy(&header_v1, &header, STANDARD_HEADER_SIZE);
    header.specific.disk = header_v1.specific.disk;
  }
  if (!strcmp(type, REDOLOG_SUBTYPE_GROWING)) {
    set_timestamp(fat_datetime(mtime, 1) | (fat_datetime(mtime, 0) << 16));
  }

  catalog = (uint32_t*)malloc(dtoh32(header.specific.catalog) * sizeof(uint32_t));

  // FIXME could mmap
  res = read_image(fd, dtoh32(header.standard.header), catalog, dtoh32(header.specific.catalog) * sizeof(uint32_t));

  if (res !=  (ssize_t)(dtoh32(header.specific.catalog) * sizeof(uint32_t)))
  {
    PERRF_ABORT(LOG_HDD, "redolog : could not read catalog %d=%d\n",res, dtoh32(header.specific.catalog));
    return -1;
  }

  // check last used extent
  extent_next = 0;
  for (uint32_t i=0; i < dtoh32(header.specific.catalog); i++)
  {
    if (dtoh32(catalog[i]) != REDOLOG_PAGE_NOT_ALLOCATED)
    {
      if (dtoh32(catalog[i]) >= extent_next)
        extent_next = dtoh32(catalog[i]) + 1;
    }
  }
  PDEBUGF(LOG_V0, LOG_HDD, "redolog : next extent will be at index %d\n",extent_next);

  // memory used for storing bitmaps
  bitmap = (uint8_t *)malloc(dtoh32(header.specific.bitmap));

  bitmap_blocks = 1 + (dtoh32(header.specific.bitmap) - 1) / 512;
  extent_blocks = 1 + (dtoh32(header.specific.extent) - 1) / 512;

  PDEBUGF(LOG_V2, LOG_HDD, "redolog : each bitmap is %d blocks\n", bitmap_blocks);
  PDEBUGF(LOG_V2, LOG_HDD, "redolog : each extent is %d blocks\n", extent_blocks);

  imagepos = 0;
  bitmap_update = 1;

  return 0;
}

void RedoLog::close()
{
  if (fd >= 0)
    ::close(fd);

  if (catalog != NULL)
    free(catalog);

  if (bitmap != NULL)
    free(bitmap);
}

uint64_t RedoLog::get_size()
{
  return dtoh64(header.specific.disk);
}

uint32_t RedoLog::get_timestamp()
{
  return dtoh32(header.specific.timestamp);
}

bool RedoLog::set_timestamp(uint32_t timestamp)
{
  header.specific.timestamp = htod32(timestamp);
  // Update header
  write_image(fd, 0, &header, dtoh32(header.standard.header));
  return 1;
}

int64_t RedoLog::lseek(int64_t offset, int whence)
{
  if ((offset % 512) != 0) {
    PERRF_ABORT(LOG_HDD, "redolog : lseek() offset not multiple of 512\n");
    return -1;
  }
  if (whence == SEEK_SET) {
    imagepos = offset;
  } else if (whence == SEEK_CUR) {
    imagepos += offset;
  } else {
    PERRF_ABORT(LOG_HDD, "redolog: lseek() mode not supported yet\n");
    return -1;
  }
  if (imagepos > (int64_t)dtoh64(header.specific.disk)) {
    PERRF_ABORT(LOG_HDD, "redolog : lseek() to byte %ld failed\n", (long)offset);
    return -1;
  }

  uint32_t old_extent_index = extent_index;
  extent_index = (uint32_t)(imagepos / dtoh32(header.specific.extent));
  if (extent_index != old_extent_index) {
    bitmap_update = 1;
  }
  extent_offset = (uint32_t)((imagepos % dtoh32(header.specific.extent)) / 512);

  PDEBUGF(LOG_V2, LOG_HDD, "redolog : lseeking extent index %d, offset %d\n",extent_index, extent_offset);

  return imagepos;
}

ssize_t RedoLog::read(void* buf, size_t count)
{
  int64_t block_offset, bitmap_offset;
  ssize_t ret;

  if (count != 512) {
    PERRF_ABORT(LOG_HDD, "redolog : read() with count not 512\n");
    return -1;
  }

  PDEBUGF(LOG_V2, LOG_HDD, "redolog : reading index %d, mapping to %d\n", extent_index, dtoh32(catalog[extent_index]));

  if (dtoh32(catalog[extent_index]) == REDOLOG_PAGE_NOT_ALLOCATED) {
    // page not allocated
    return 0;
  }

  bitmap_offset  = (int64_t)STANDARD_HEADER_SIZE + (dtoh32(header.specific.catalog) * sizeof(uint32_t));
  bitmap_offset += (int64_t)512 * dtoh32(catalog[extent_index]) * (extent_blocks + bitmap_blocks);
  block_offset    = bitmap_offset + ((int64_t)512 * (bitmap_blocks + extent_offset));

  PDEBUGF(LOG_V2, LOG_HDD, "redolog : bitmap offset is %x\n", (uint32_t)bitmap_offset);
  PDEBUGF(LOG_V2, LOG_HDD, "redolog : block offset is %x\n", (uint32_t)block_offset);

  if (bitmap_update) {
    if (read_image(fd, (off_t)bitmap_offset, bitmap,  dtoh32(header.specific.bitmap)) != (ssize_t)dtoh32(header.specific.bitmap)) {
      PERRF_ABORT(LOG_HDD, "redolog : failed to read bitmap for extent %d\n", extent_index);
      return -1;
    }
    bitmap_update = 0;
  }

  if (((bitmap[extent_offset/8] >> (extent_offset%8)) & 0x01) == 0x00) {
    PDEBUGF(LOG_V2, LOG_HDD, "read not in redolog\n");

    // bitmap says block not in redolog
    return 0;
  }

  ret = read_image(fd, (off_t)block_offset, buf, count);
  if (ret >= 0) lseek(512, SEEK_CUR);

  return ret;
}

ssize_t RedoLog::write(const void* buf, size_t count)
{
  uint32_t i;
  int64_t block_offset, bitmap_offset, catalog_offset;
  ssize_t written;
  bool update_catalog = 0;

  if (count != 512) {
    PERRF_ABORT(LOG_HDD, "redolog : write() with count not 512\n");
    return -1;
  }

  PDEBUGF(LOG_V2, LOG_HDD, "redolog : writing index %d, mapping to %d\n", extent_index, dtoh32(catalog[extent_index]));

  if (dtoh32(catalog[extent_index]) == REDOLOG_PAGE_NOT_ALLOCATED) {
    if (extent_next >= dtoh32(header.specific.catalog)) {
      PERRF_ABORT(LOG_HDD, "redolog : can't allocate new extent... catalog is full\n");
      return -1;
    }

    PDEBUGF(LOG_V2, LOG_HDD, "redolog : allocating new extent at %d\n", extent_next);

    // Extent not allocated, allocate new
    catalog[extent_index] = htod32(extent_next);

    extent_next += 1;

    char *zerobuffer = (char*)malloc(512);
    memset(zerobuffer, 0, 512);

    // Write bitmap
    bitmap_offset  = (int64_t)STANDARD_HEADER_SIZE + (dtoh32(header.specific.catalog) * sizeof(uint32_t));
    bitmap_offset += (int64_t)512 * dtoh32(catalog[extent_index]) * (extent_blocks + bitmap_blocks);
    ::lseek(fd, (off_t)bitmap_offset, SEEK_SET);
    ssize_t res;
    for (i=0; i<bitmap_blocks; i++) {
      res = ::write(fd, zerobuffer, 512);
      ASSERT(res==512);
    }
    // Write extent
    for (i=0; i<extent_blocks; i++) {
      res = ::write(fd, zerobuffer, 512);
      ASSERT(res==512);
    }

    free(zerobuffer);

    update_catalog = 1;
  }

  bitmap_offset  = (int64_t)STANDARD_HEADER_SIZE + (dtoh32(header.specific.catalog) * sizeof(uint32_t));
  bitmap_offset += (int64_t)512 * dtoh32(catalog[extent_index]) * (extent_blocks + bitmap_blocks);
  block_offset    = bitmap_offset + ((int64_t)512 * (bitmap_blocks + extent_offset));

  PDEBUGF(LOG_V2, LOG_HDD, "redolog : bitmap offset is %x\n", (uint32_t)bitmap_offset);
  PDEBUGF(LOG_V2, LOG_HDD, "redolog : block offset is %x\n", (uint32_t)block_offset);

  // Write block
  written = write_image(fd, (off_t)block_offset, (void*)buf, count);

  // Write bitmap
  if (bitmap_update) {
    if (read_image(fd, (off_t)bitmap_offset, bitmap,  dtoh32(header.specific.bitmap)) != (ssize_t)dtoh32(header.specific.bitmap)) {
      PERRF_ABORT(LOG_HDD, "redolog : failed to read bitmap for extent %d\n", extent_index);
      return 0;
    }
    bitmap_update = 0;
  }

  // If bloc does not belong to extent yet
  if (((bitmap[extent_offset/8] >> (extent_offset%8)) & 0x01) == 0x00) {
    bitmap[extent_offset/8] |= 1 << (extent_offset%8);
    write_image(fd, (off_t)bitmap_offset, bitmap,  dtoh32(header.specific.bitmap));
  }

  // Write catalog
  if (update_catalog) {
    // FIXME if mmap
    catalog_offset  = (int64_t)STANDARD_HEADER_SIZE + (extent_index * sizeof(uint32_t));

    PDEBUGF(LOG_V2, LOG_HDD, "redolog : writing catalog at offset %x\n", (uint32_t)catalog_offset);

    write_image(fd, (off_t)catalog_offset, &catalog[extent_index], sizeof(uint32_t));
  }

  if (written >= 0) lseek(512, SEEK_CUR);

  return written;
}

int RedoLog::check_format(int fd, const char *subtype)
{
  redolog_header_t temp_header;

  int res = read_image(fd, 0, &temp_header, sizeof(redolog_header_t));
  if (res != STANDARD_HEADER_SIZE) {
    return HDIMAGE_READ_ERROR;
  }

  if (strcmp((char*)temp_header.standard.magic, STANDARD_HEADER_MAGIC) != 0) {
    return HDIMAGE_NO_SIGNATURE;
  }

  if (strcmp((char*)temp_header.standard.type, REDOLOG_TYPE) != 0) {
    return HDIMAGE_TYPE_ERROR;
  }
  if (strcmp((char*)temp_header.standard.subtype, subtype) != 0) {
    return HDIMAGE_TYPE_ERROR;
  }

  if ((dtoh32(temp_header.standard.version) != STANDARD_HEADER_VERSION) &&
      (dtoh32(temp_header.standard.version) != STANDARD_HEADER_V1)) {
    return HDIMAGE_VERSION_ERROR;
  }
  return HDIMAGE_FORMAT_OK;
}

bool RedoLog::save_state(const char *backup_fname)
{
  return hdimage_backup_file(fd, backup_fname);
}

