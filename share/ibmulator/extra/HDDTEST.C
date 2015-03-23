/*
  HDDTEST - Tests the HDD's sectors by writing and reading a data pattern.
  It wipes the content of the drive. Use at your own risk.

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
#include <conio.h>
#include <stdio.h>
#include <string.h>

int g_max_cyl;
int g_max_head;
int g_spt;
int g_cur_sec;
int g_cur_cyl;
int g_cur_head;
int g_eoc;
int g_traks;
long int g_sectors;
unsigned char g_sec_buf[512];

#define CMD_WRITE 0
#define CMD_READ 1

#define CALL_INT13 \
	/*low eight bits of cylinder number*/ \
	inregs.h.ch = (unsigned char)g_cur_cyl; \
	/*sector number 1-63 (bits 0-5)*/ \
	inregs.h.cl = (unsigned char)(g_cur_sec & 0x3F); \
	/*high two bits of cylinder (bits 6-7)*/ \
	inregs.h.cl |= (unsigned char)((g_cur_cyl & 0x300) >> 2); \
	/*head number*/ \
	inregs.h.dh = (unsigned char)g_cur_head; \
	int86x(0x13, &inregs, &outregs, &segregs);

int determine_hdd_props()
{
	union REGS inregs, outregs;
	struct SREGS segregs;

	inregs.h.ah = 0x08;
	inregs.h.dl = 0x80; //HD0
	int86(0x13,&inregs,&outregs);
	//On PS/1s with IBM ROM DOS 4, nonexistent drives return CF clear, BX=CX=0000h
	if(outregs.x.cflag != 0 || outregs.h.ah != 0
	   || (outregs.x.bx==0 && outregs.x.cx==0))
	{
		return 0;
	}
	g_max_cyl = ((int)(outregs.h.cl & 0xC0)) << 2;
	g_max_cyl += outregs.h.ch;
	g_spt = outregs.h.cl & 0x3F;
	g_max_head = outregs.h.dh;

	g_traks = (g_max_cyl+1) * (g_max_head+1);
	g_sectors = (long int)g_traks * (long int)g_spt;

	return 1;
}

void increment_sector()
{
	g_cur_sec++;
	//warning: sectors are 1-based
	if(g_cur_sec > g_spt) {
		g_cur_sec = 1;
		g_cur_head++;
		if(g_cur_head > g_max_head) {
			g_cur_head = 0;
			g_cur_cyl++;
		}

		if(g_cur_cyl > g_max_cyl) {
			g_cur_cyl = g_max_cyl;
			g_eoc = 1;
		}
	}
}

int cmd_write()
{
	long int i;
	int s,x,y,j;
	char far *bufptr;
	struct SREGS segregs;
	union REGS inregs, outregs;

	g_eoc = 0;
	g_cur_cyl = 0;
	g_cur_head = 0;
	g_cur_sec = 1;

	printf("writing %ld sectors; press any key to interrupt.\n", g_sectors);
	printf("sector: ");
	x = wherex();
	y = wherey();

	/*DISK - WRITE DISK SECTOR(S)*/
	inregs.h.ah = 0x03;
	/*number of sectors to read (must be nonzero)*/
	inregs.h.al = 1;
	/*drive number (bit 7 set for hard disk)*/
	inregs.h.dl = 0x80;
	/*ES:BX -> data buffer*/
	bufptr = (char far *)g_sec_buf;
	segregs.es = FP_SEG(bufptr);
	inregs.x.bx = FP_OFF(bufptr);

	for(i=1; i<=g_sectors; i++) {
		if((i & 0xFF == 0xFF) || i==g_sectors) {
			if(kbhit()) {
				printf("\n");
				getch();
				return 0;
			}
			gotoxy(x,y);
			printf("%ld", i);
		}

		for(j=0; j<128; j++) {
			*(long int*)&g_sec_buf[j*4] = i;
		}

		CALL_INT13

		if(outregs.x.cflag != 0 || outregs.h.ah != 0) {
			return 0;
		}

		increment_sector();
	}
	printf("\n");
	return 1;
}

int cmd_read()
{
	long int i;
	int s,x,y,j;
	char far *bufptr;
	struct SREGS segregs;
	union REGS inregs, outregs;

	g_eoc = 0;
	g_cur_cyl = 0;
	g_cur_head = 0;
	g_cur_sec = 1;

	printf("reading %ld sectors; press any key to interrupt.\n", g_sectors);
	printf("sector: ");
	x = wherex();
	y = wherey();

	/*DISK - READ SECTOR(S) INTO MEMORY*/
	inregs.h.ah = 0x02;
	/*number of sectors to read (must be nonzero)*/
	inregs.h.al = 1;
	/*drive number (bit 7 set for hard disk)*/
	inregs.h.dl = 0x80;
	/*ES:BX -> data buffer*/
	bufptr = (char far *)g_sec_buf;
	segregs.es = FP_SEG(bufptr);
	inregs.x.bx = FP_OFF(bufptr);

	for(i=1; i<=g_sectors; i++) {
		if((i & 0xFF == 0xFF) || i==g_sectors) {
			if(kbhit()) {
				printf("\n");
				getch();
				return 0;
			}
			gotoxy(x,y);
			printf("%ld", i);
		}

		CALL_INT13

		if(outregs.x.cflag != 0 || outregs.h.ah != 0) {
			return 0;
		}

		for(j=0; j<128; j++) {
			if ( *(long int*)&g_sec_buf[j*4] != i ) {
				printf("\n");
				return 0;
			}
		}
		increment_sector();
	}
	printf("\n");
	return 1;
}

int main(int argc, char **argv)
{
	int cmd,c,result;

	printf("HDDTEST - Tests the HDD's sectors by writing and reading a data pattern.\n");
	printf("This program has been created to aid the development of IBMulator.\n"
			"It is NOT a proper HDD tester! Don't use it on real hardware!\n");

	switch(argc) {
		case 1:
			printf("Usage: HDDTEST w|r\n\n");
			return 1;
		case 2:
			if(strcmp(argv[1], "w") == 0) {
				cmd = CMD_WRITE;
			} else if(strcmp(argv[1], "r") == 0) {
				cmd = CMD_READ;
			} else {
				printf("invalid argument: '%s'\n", argv[1]);
				return 1;
			}
			break;
	}

	if(!determine_hdd_props()) {
		printf("ERROR: unable to determine the HDD properties\n");
		return 1;
	}

	printf("cylinders: %d, heads: %d, sectors per track: %d\n",
			g_max_cyl+1, g_max_head+1, g_spt);

	if(cmd == CMD_WRITE) {
		printf("WARNING: you are about to WIPE the entire content of the HDD.\n");
	}
	printf("Continue? [y/N] ");
	c = getche();
	if(c!='y' && c!='Y') {
		return 0;
	}

	if(cmd == CMD_WRITE) {
		printf("ARE YOU SURE? [y/N] \n");
		c = getche();
		printf("\n");
		if(c!='y' && c!='Y') {
			return 0;
		}
		result = cmd_write();
	} else {
		printf("\n");
		result = cmd_read();
	}
	if(!result) {
		printf("ERROR at C:%d,H:%d,S:%d\n",g_cur_cyl,g_cur_head,g_cur_sec);
	}
	return result;
}
