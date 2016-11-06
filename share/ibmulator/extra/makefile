all: test386

test386: test386.asm inc/x86.inc
	nasm -i./inc/ -f bin test386.asm -l test386.lst -o test386.bin

