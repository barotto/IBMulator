/*
  SPKRWAV - Plays a 8bit PCM mono WAV file through the PC Speaker.
  Use at your own risk.

  Copyright (C) 2015, 2016  Marco Bortolin

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
unsigned char g_amplitudes[256];


void interrupt NewISR (__CPPARGS)
{
	int byte, amp;
	if(g_buffer_pos == BUFFERSIZE) {
		g_current_buffer = 1-g_current_buffer;
		g_buffer_pos = 0;
		g_switch_buffer = 1;
	}
	byte = g_double_buffer[g_current_buffer][g_buffer_pos++];
	amp = g_amplitudes[byte];
	outportb(0x42, amp); /* counter 2 count */
	outportb(0x20, 0x20); /* end of int */
}

int main(int argc, char **argv)
{
	FILE * audioFile = NULL;
	unsigned short PCM = 0;
	unsigned short channels = 0;
	short int bits = 0;
	long int frequency = 0;
	unsigned short counter = 0;
	unsigned short finish = 0;
	unsigned short i = 0;
	unsigned char byte = 0;
	long int chunk_name = 0x20746d66;
	long int chunk_size = 0;

	g_double_buffer[0] = (unsigned char *) malloc(BUFFERSIZE);
	g_double_buffer[1] = (unsigned char *) malloc(BUFFERSIZE);

	switch(argc) {
		case 1:
			printf("Usage: SPKRWAV audiofile [frequency]\n");
			return 1;
		case 3:
			frequency = atoi(argv[2]);
			break;
	}

	audioFile = fopen(argv[1],"rb");
	if(audioFile == NULL) {
		printf("ERROR: unable to open the audio file\n");
		return 1;
	}
	if(fread(&chunk_name, 4, 1, audioFile) != 1) {
		printf("ERROR: unable to read from file\n");
		return 1;
	}
	if(chunk_name == 0x46464952) {
		fseek(audioFile, 20, SEEK_SET);
		fread(&PCM, 1, sizeof(short int), audioFile);
		fread(&channels, 1, sizeof(short int), audioFile);
		fseek(audioFile, 34, SEEK_SET);
		fread(&bits, 1, sizeof(short int), audioFile);
		if(PCM == 1 && channels == 1 && bits == 8) {
			if(frequency == 0) {
				fseek(audioFile , 24, SEEK_SET);
				fread(&frequency, 1, sizeof(long int), audioFile);
			}
			fseek(audioFile, 16, SEEK_SET);
			while(chunk_name != 0x61746164) {
				fread(&chunk_size, 4, 1, audioFile);
				if(fseek(audioFile, chunk_size, SEEK_CUR) != 0) {
					printf("unable to find the data chunk\n");
					return 1;
				}
				if(fread(&chunk_name, 4, 1, audioFile) != 1) {
					printf("unable to find the data chunk\n");
					return 1;
				}
			}
			fseek(audioFile, 4, SEEK_CUR);
		} else {
			printf("ERROR: only 8 bit PCM mono WAV files are suported\n");
			return 1;
		}
	} else {
		// raw PCM data
		fseek(audioFile, 0, SEEK_SET);
		if(frequency == 0) {
			frequency = DEFAULTFREQ;
		}
	}

	printf("frequency: %d Hz\n", frequency);
	counter = 1193180/frequency;

	//double buffering
	fread(g_double_buffer[0], 1, BUFFERSIZE, audioFile);
	fread(g_double_buffer[1], 1, BUFFERSIZE, audioFile);

	for(i = 0; i < 256; i++) {
		g_amplitudes[i] = ((i*counter)/256) + 1;
	}

	//INT 8
	g_original_INT8 = getvect(8);

	printf("counter 2 mode 0\n");
	outportb(0x43, 0x90);

	byte = inportb(0x61);
	printf("speaker activation\n");
	outportb(0x61, byte|0x03);

	printf("counter 0 mode 3\n");
	outportb(0x43, 0x16);

	printf("counter 0 count: %d\n", counter);
	outportb(0x40, counter);

	printf("installing ISR\n");
	setvect(8, NewISR);

	//wait until finish or the user press a key
	printf("playing... (press any key to stop)\n");
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
