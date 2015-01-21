/*
  SPKRTONE - Plays a continuous tone through the PC speaker
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

int main(int argc, char **argv)
{
	unsigned short counter;
	unsigned char byte;
	int frequency = 0;

	switch(argc) {
		case 1:
			printf("Usage: SPKRTONE frequency\n");
			return 1;
		case 2:
			frequency = atoi(argv[1]);
			break;
	}
	printf("frequency: %d Hz\n", frequency);
	if(frequency == 0) {
		printf("invalid frequency\n");
		return 1;
	}
	counter = 1193180/frequency;

	printf("counter 2 mode 3\n");
	outportb(0x43, 0xb6);

	printf("counter 2 count: %d\n", counter);
	outportb(0x42, (unsigned char)(counter));
	outportb(0x42, (unsigned char)(counter>>8));

	byte = inportb(0x61);
	printf("speaker activation\n");
	outportb(0x61, byte|0x03);

	printf("playing... (press a key to stop)\n");
	while(peekb(0x40,0x1A) == peekb(0x40,0x1C)) {}

	//disable the speaker
	byte = inportb(0x61);
	outportb(0x61, byte&0xFC);

	printf("bye\n");
	return 0;
}
