/*
  ROMDUMP - Dumps the ROM of the IBM PS/1 model 2011
  Use at your own risk.

  Copyright (c) 2015  Marco Bortolin

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

#define READ_BUF_SIZE 1024 //power of 2 only!
#define GDT_SIZE 48
#define ROM1_NAME "F80000.BIN"
#define ROM2_NAME "FC0000.BIN"

unsigned char g_model;
unsigned char g_submodel;
unsigned char g_readbuf[READ_BUF_SIZE];
unsigned char g_gdt[GDT_SIZE];

int check_model()
{
	union REGS inregs, outregs;
	struct SREGS segregs;
	char far *table;

	inregs.h.ah = 0xC0;
	int86(0x15,&inregs,&outregs);
	if(outregs.x.cflag != 0) {
		return 0;
	}
	segread(&segregs);
	table = (char far *) MK_FP(segregs.es, outregs.x.bx);
	g_model = (unsigned char)(*(table + 2));
	g_submodel = (unsigned char)(*(table + 3));
	return 1;
}

int read_ext_memory(unsigned long int srcaddr, unsigned int bytes)
{
	int i;
	unsigned int seg,off;
	unsigned long int destaddr;
	char far *bufptr;
	char far *gdtptr;
	struct SREGS segregs;
	union REGS inregs, outregs;

	memset(g_gdt, 0, GDT_SIZE);

	//source
	*(unsigned int*)(&g_gdt[0x10]) = bytes;
	*(unsigned int*)(&g_gdt[0x12]) = (unsigned int)(srcaddr);
	g_gdt[0x14] = (unsigned char)(srcaddr>>16);
	g_gdt[0x15] = 0x93;

	//destination
	bufptr = (char far *)g_readbuf;
	seg = FP_SEG(bufptr);
	off = FP_OFF(bufptr);
	destaddr = (unsigned long int)(seg) << 4;
	destaddr += off;
	*(unsigned int*)(&g_gdt[0x18]) = bytes;
	*(unsigned int*)(&g_gdt[0x1A]) = (unsigned int)(destaddr);
	g_gdt[0x1C] = (unsigned char)(destaddr>>16);
	g_gdt[0x1D] = 0x93;

	gdtptr = (char far *)g_gdt;
	segregs.es = FP_SEG(gdtptr);
	inregs.x.si = FP_OFF(gdtptr);
	inregs.x.cx = bytes/2;
	inregs.h.ah = 0x87;

	int86x(0x15, &inregs, &outregs, &segregs);

	if(outregs.x.cflag != 0) {
		return 0;
	}
	return 1;
}

int dump_rom(FILE *dest, unsigned long int addr)
{
	int i;
	int iterations = 262144 / READ_BUF_SIZE;
	for(i=0; i<iterations; i++) {
		if(!read_ext_memory(addr, READ_BUF_SIZE)) {
			printf("error while reading %d bytes at %ld\n", READ_BUF_SIZE, addr);
			return 0;
		}
		if(fwrite(g_readbuf, 1, READ_BUF_SIZE, dest) != READ_BUF_SIZE) {
			printf("error trying to write %d bytes to file\n", READ_BUF_SIZE);
			return 0;
		}
		printf(".");
		addr += READ_BUF_SIZE;
	}
	return 1;
}

int main(int argc, char **argv)
{
	unsigned char is_intl;
	FILE *outf;
	int c;

	printf("ROMDUMP - Dumps the ROM of the IBM PS/1 model 2011\n");

	switch(argc) {
		case 1:
			printf("Usage: ROMDUMP us|intl\nUse 'us' if you have the US version of the PS/1, use 'intl' otherwise\n");
			return 1;
		case 2:
			if(strcmp(argv[1], "us") == 0) {
				is_intl = 0;
			} else if(strcmp(argv[1], "intl") == 0) {
				is_intl = 1;
			} else {
				printf("invalid argument: '%s'\n", argv[1]);
				return 1;
			}
			break;
	}

	if(!check_model()) {
		printf("ERROR: unable to derermine the machine model\n");
		return 1;
	}

	if(g_model!=0xFC || g_submodel!=0x0B) {
		printf("WARNING: this machine appears not to be a IBM PS/1 model 2011.\nContinue anyway? [y/N] ");
		c = getchar();
		if(c!='y' && c!='Y') {
			return 0;
		}
	}

	if(is_intl) {
		printf("International model: dumping 2 ROM files\n");
		printf(ROM1_NAME": ");
		outf = fopen(ROM1_NAME, "wb");
		if(outf == NULL) {
			printf("error opening the file\n");
			return 1;
		}
		if(!dump_rom(outf,0xF80000)) {
			fclose(outf);
			return 1;
		}
		fclose(outf);
		printf("\n");
	} else {
		printf("US model: dumping 1 ROM file\n");
	}

	printf(ROM2_NAME": ");
	outf = fopen(ROM2_NAME, "wb");
	if(outf == NULL) {
		printf("error opening the file\n");
		return 1;
	}
	if(!dump_rom(outf,0xFC0000)) {
		fclose(outf);
		return 1;
	}
	fclose(outf);

	return 0;
}
