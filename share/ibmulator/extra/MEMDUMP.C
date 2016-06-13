/*
  MEMDUMP - Dumps various low RAM regions
  Use at your own risk.

  Copyright (C) 2016  Marco Bortolin

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <dos.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* some defines to keep my 21st century IDE quiet */
#ifndef __MSDOS__
	#error
	#define far
#endif

int dump_mem(const char *name, unsigned seg, unsigned off, unsigned long len)
{
	FILE *outf;
	char far *pos = (char far *)MK_FP(seg,off);
	char byte;
	size_t res;

	printf("%s", name);
	outf = fopen(name, "wb");
	if(outf == NULL) {
		printf(" error opening the file for writing. Is disk full?\n");
		return 0;
	}
	while(len--) {
		byte = *pos++;
		res = fwrite(&byte,1,1,outf);
		if(res != 1) {
			printf("\nerror writing to the file. Is disk full?\n");
			fclose(outf);
			return 0;
		}
		if((len % 512) == 0) {
			printf(".");
		}
	}
	printf("\n");
	fclose(outf);

	return 1;
}

int main(int argc, char **argv)
{
	int i;

	printf("MEMDUMP - Dumps various low RAM regions\n");

	dump_mem("00000.BIN", 0x0000, 0x0000, 0x400l);
	dump_mem("00400.BIN", 0x0040, 0x0000, 0x100l);
	dump_mem("9FC00.BIN", 0x9000, 0xFC00, 0x400l);
	dump_mem("C0000.BIN", 0xC000, 0x0000, 0x10000l);
	dump_mem("D0000.BIN", 0xD000, 0x0000, 0x10000l);
	dump_mem("E0000.BIN", 0xE000, 0x0000, 0x10000l);
	dump_mem("F0000.BIN", 0xF000, 0x0000, 0x10000l);

	printf("Done!\n");

	return 0;
}
