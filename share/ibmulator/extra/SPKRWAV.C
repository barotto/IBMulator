/*
  SPKRWAV - Plays a 8bit PCM mono WAV or VOC file through the PC speaker.
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
#include <stdlib.h>
#include <dos.h>
#include <string.h>

#define BUFFERSIZE 8*1024*sizeof(char)
#define DEFAULTFREQ 16000

void interrupt (*g_original_INT8)();
unsigned short g_current_buffer = 0;
unsigned short g_switch_buffer = 0;
unsigned char *g_double_buffer[2];
unsigned short g_buffer_pos = 0;
unsigned char byte;
unsigned char g_amplitudes[256];
unsigned short g_counter;

void interrupt NewISR (__CPPARGS)
{
	if(g_buffer_pos == BUFFERSIZE) {
		g_current_buffer = 1-g_current_buffer;
		g_buffer_pos = 0;
		g_switch_buffer = 1;
	}
	byte = g_double_buffer[g_current_buffer][g_buffer_pos++];
	outportb(0x42, (int)g_amplitudes[byte]);

	//end of int
	outportb(0x20, 0x20);
}

int main(int argc, char **argv)
{
	FILE * audioFile;
	unsigned short PCM;
	unsigned short channels;
	short int bits;
	long int frequency = 0;
	unsigned short int finish = 0;
	unsigned short int is_WAV = 0;
	unsigned short int i;

	g_double_buffer[0] = (unsigned char *) malloc(BUFFERSIZE);
	g_double_buffer[1] = (unsigned char *) malloc(BUFFERSIZE);

	switch(argc) {
		case 1:
			printf("Usage: SPKRWAV audiofile [frequency]\n");
			return 1;
		case 2:
			frequency = DEFAULTFREQ;
			break;
		case 3:
			frequency = atoi(argv[2]);
			break;
	}
	if('.' == argv[1][strlen(argv[1])-4] &&
		'w' == argv[1][strlen(argv[1])-3] &&
		'a' == argv[1][strlen(argv[1])-2] &&
		'v' == argv[1][strlen(argv[1])-1]) {
		is_WAV = 1;
	}

	audioFile = fopen(argv[1],"rb");
	if(audioFile == NULL) {
		printf("error opening the audio file\n");
		return 1;
	}

	if(is_WAV) {
		fseek(audioFile, 20, SEEK_SET);
		fread(&PCM, 1, sizeof(short int), audioFile);
		fread(&channels, 1, sizeof(short int), audioFile);
		fseek(audioFile, 34, SEEK_SET);
		fread(&bits, 1, sizeof(short int), audioFile);
		if(PCM == 1 && channels == 1 && bits == 8) {
			fseek(audioFile , 24, SEEK_SET);
			fread(&frequency, 1, sizeof(long int), audioFile);
			fseek(audioFile, 44, SEEK_SET);
		} else {
			printf("ERROR: only 8 bit PCM mono WAV files are suported\n");
			return 1;
		}
	}
	printf("frequency: %d Hz\n", frequency);
	if(frequency==0) {
		printf("incorrect frequency\n");
		return 1;
	}
	g_counter = 1193180/frequency;

	//double buffering
	fread(g_double_buffer[0], 1, BUFFERSIZE, audioFile);
	fread(g_double_buffer[1], 1, BUFFERSIZE, audioFile);

	for(i = 0; i < 256; i++) {
		g_amplitudes[i] = (i*g_counter/256) + 1;
	}

	//INT 8
	g_original_INT8 = getvect(8);

	printf("installing ISR\n");
	setvect(8, NewISR);

	printf("counter 2 mode 0\n");
	outportb(0x43, 0x90);

	printf("counter 0 mode 3\n");
	outportb(0x43, 0x16);

	printf("counter 0 count: %d\n",g_counter);
	outportb(0x40, g_counter);

	//enable the speaker
	byte = inportb(0x61);
	printf("speaker activation\n");
	outportb(0x61, byte|0x03);

	//wait until finish or the user press a key
	printf("playing... (press a key to stop)\n");
	while(peekb(0x40,0x1A) == peekb(0x40,0x1C) && !finish) {
		if(g_switch_buffer) {
			if(!fread(g_double_buffer[1-g_current_buffer], 1, BUFFERSIZE, audioFile)) {
				finish = 1;
			}
			g_switch_buffer = 0;
		}
	}

	//disable the speaker
	byte = inportb(0x61);
	outportb(0x61, byte&0xFC);

	//restore the original value of counter 0
	outportb(0x40, 0x00);

	//restore the original INT8 handler
	setvect(8, g_original_INT8);

	free(g_double_buffer[0]);
	free(g_double_buffer[1]);
	fclose(audioFile);

	printf("bye\n");
	return 0;
}
