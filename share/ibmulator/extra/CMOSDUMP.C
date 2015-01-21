/*
  CMOSDUMP - Dumps the content of the CMOS memory to the file CMOS.BIN
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

#include <stdio.h>

#define CMOS_SIZE 64

/*
This function reads a value from the CMOS, preserving the passed NMI bit value.
To read the CMOS this function disables NMIs. It's responsibility of the caller
to inform this function about the current state of the NMI bit.
So to read CMOS byte 0Fh with NMI restored to enabled call read_cmos(0x0F).
Call read_cmos(0x8F) otherwise.
*/
unsigned char read_cmos(unsigned char _addr)
{
	unsigned char value;
	asm {
	mov   al, _addr
	pushf             /* save the CPU flags */
	rol   al, 1       /* rotate 8-bit AL left once (AL[0] = AL[7]) */
	stc               /* CF = 1 */
	rcr   al, 1       /* save the original value of _addr[7] (the NMI bit) in CF.
	                     rotate 9-bits (CF, AL) right once. now AL[7]=1 (NMI
	                     disabled) and CF=AL[0] */
	cli               /* IF = 0 (disable interrupts) */
	out   70h, al     /* inform the CMOS about the memory register we want to
	                     read */
	jmp   short $+2   /* delay */
	in    al, 71h     /* read the CMOS register value and put it in AL */
	push  ax          /* save AX */
	mov   al, 1Eh     /* AL = 11110b (0Fh shifted left by 1) */
	rcr   al, 1       /* reset the NMI bit to its original value. rotate 9-bits
	                     (CF, AL) right once */
	out   70h, al     /* CMOS reg = AL (it can be 8Fh or 0Fh) */
	jmp   short $+2   /* delay */
	in    al, 71h     /* bogus CMOS read to keep the chip happy */
	pop   ax          /* restore AX */
	popf              /* restore CPU flags */
	mov   value, al   /* return the read CMOS value for index _addr */
	}
	/*
	The "mov value, al" is redundant, because to translate "return value;" the
	assembler will add "mov al, [bp+var_1]" at the end anyway (AL is the
	register where the function return value should be).
	But I will leave the instruction there, with the associated "return value;",
	just for clarity.
	*/
	return value;
}

int main()
{
	unsigned char p, cmos[CMOS_SIZE];
	FILE *outf;
	size_t bytes_written;

	/* read the CMOS in its entirety */
	for(p=0; p<CMOS_SIZE; p++) {
		cmos[p] = read_cmos(p);
	}

	/* write the CMOS data to screen and file */
	outf = fopen("cmos.bin","wb");
	if(outf == NULL) {
		printf("error opening file to write\n");
		return 1;
	}
	bytes_written = fwrite(cmos, 1, CMOS_SIZE, outf);
	if(bytes_written != CMOS_SIZE) {
		printf("WARNING: written %d bytes out of %d. Disk full?\n",
	           bytes_written, CMOS_SIZE);
	}
	/*
	for(p=0; p<CMOS_SIZE; p++) {
		printf("[%2X=%2X] ", p, cmos[p]);
		fprintf(outf, "%2X = %2X\n", p, cmos[p]);
	}
	*/
	fclose(outf);

	return 0;
}
