;
;   test386.asm
;   Copyright (C) 2012-2015 Jeff Parsons <Jeff@pcjs.org>
;   Copyright (C) 2016 Marco Bortolin <barotto@gmail.com>
;
;   This file is part of IBMulator (http://barotto.github.io/IBMulator/) and is
;   a derivative work of PCjs (http://pcjs.org/) tests/pcx86/80386/test386.asm
;
;   IBMulator is free software: you can redistribute it and/or modify it under
;   the terms of the GNU General Public License as published by the Free
;   Software Foundation, either version 3 of the License, or (at your option)
;   any later version.
;
;   IBMulator is distributed in the hope that it will be useful, but WITHOUT ANY
;   WARRANTY without even the implied warranty of MERCHANTABILITY or FITNESS
;   FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
;   details.
;
;   You should have received a copy of the GNU General Public License along with
;   IBMulator.  If not see <http://www.gnu.org/licenses/gpl.html>.
;
;   Overview
;   --------
;   This file is designed to run as a test ROM, loaded in place of the BIOS.
;   Its pourpose is to test the CPU, reporting its status to the POST port and
;   to the printer/serial port.
;   A 80386 or later CPU is required. This ROM is designed to test a emulator's
;   CPU implementation (never tested on a real hardware.)
;
;   It must be installed at physical address 0xf0000 and aliased at physical
;   address 0xffff0000.  The jump at resetVector should align with the CPU reset
;   address 0xfffffff0, which will transfer control to f000:0045.  From that
;   point on, all memory accesses should remain within the first 1MB.
;
%define COPYRIGHT 'test386.asm (C) 2012-2015 Jeff Parsons, (C) 2016 Marco Bortolin      '
%define RELEASE   '10/29/16'

	cpu	386
	section .text

	%include "x86.inc"

	bits	16

PAGING    equ 1
POST_PORT equ 0x190
LPT_PORT  equ 1
COM_PORT  equ 0
IBM_PS1   equ 1

CSEG_REAL	equ	0xf000
CSEG_PROT16	equ	0x0008
CSEG_PROT32	equ	0x0010
DSEG_PROT16	equ	0x0018
DSEG_PROT32	equ	0x0020
SSEG_PROT32	equ	0x0028

OFF_ERROR        equ 0xa000

;
;   We set our exception handlers at fixed addresses to simplify interrupt gate descriptor initialization.
;
OFF_INTDEFAULT   equ OFF_ERROR
OFF_INTDIVERR    equ 0xa200
OFF_INTPAGEFAULT equ 0xa400

;
;   Output to the POST and LPT port a code, destroys al and dx
;
%macro	POST 1
	mov	al, 0x%1
	mov	dx, POST_PORT
	out	dx, al
%endmacro

;
;   The "defGate" macro defines an interrupt gate, given a selector (%1) and an offset (%2)
;
%macro	defGate	2
	dw    (%2 & 0xffff)
	dw    %1
	dw    ACC_TYPE_GATE386_INT | ACC_PRESENT
	dw    (%2 >> 16) & 0xffff
%endmacro

;
;   The "defDesc" macro defines a descriptor, given a name (%1), base (%2), limit (%3), type (%4), and ext (%5)
;
%assign	selDesc	0

%macro	defDesc	1-5 0,0,0,0
	%assign %1 selDesc
	dw	(%3 & 0x0000ffff)
	dw	(%2 & 0x0000ffff)
	%if selDesc = 0
	dw	((%2 & 0x00ff0000) >> 16) | %4 | (0 << 13)
	%else
	dw	((%2 & 0x00ff0000) >> 16) | %4 | (0 << 13) | ACC_PRESENT
	%endif
	dw	((%3 & 0x000f0000) >> 16) | %5 | ((%2 & 0xff000000) >> 16)
	%assign selDesc selDesc+8
%endmacro

;
; The "set" macro initializes a register to the specified value (eg, "set eax,0")
;
%macro	set	2
	%ifnum %2
		%if %2 = 0
			xor %1,%1
		%else
			mov %1,%2
		%endif
	%else
		mov %1,%2
	%endif
%endmacro

;
;   Test store, move, scan, and compare string data
;   DS:ESI test buffer 1
;   ES:EDI test buffer 2
;   ECX: buffer size in dwords
;	this function is a macro because it needs to be compiled in 16-bit and 32-bit modes
;
%macro testStringOps 0
	mov    ebp, ecx   ; EBP <- buffers dword size (can't use stack to save)
	mov    ebx, ecx
	shl    ebx, 2     ; EBX <- buffers byte size

	mov    eax, 0x12345678
	cld

	; STORE buffers with pattern in EAX
	rep stosd           ; store ECX dwords at ES:EDI from EAX
	cmp    ecx, 0
	jnz    error        ; ECX must be 0
	sub    edi, ebx     ; rewind EDI
	; now switch ES:EDI with DS:ESI
	mov    dx, es
	mov    cx, ds
	xchg   dx, cx
	mov    es, dx
	mov    ds, cx
	xchg   edi, esi
	; store again ES:EDI with pattern in EAX
	mov    ecx, ebp     ; reset ECX
	rep stosd
	sub    edi, ebx     ; rewind EDI

	; COMPARE two buffers
	mov    ecx, ebp     ; reset ECX
	repe cmpsd          ; find nonmatching dwords in ES:EDI and DS:ESI
	cmp    ecx, 0
	jnz    error        ; ECX must be 0
	sub    edi, ebx     ; rewind EDI
	sub    esi, ebx     ; rewind ESI

	; SCAN buffer for pattern
	mov    ecx, ebp     ; reset ECX
	repe scasd          ; SCAN first dword not equal to EAX
	cmp    ecx, 0
	jne    error        ; ECX must be 0
	sub    edi, ebx     ; rewind EDI

	; MOVE and COMPARE data between buffers
	; first zero-fill ES:EDI so that we can compare the moved data later
	mov    eax, 0
	mov    ecx, ebp     ; reset ECX
	rep stosd           ; zero fill ES:EDI
	sub    edi, ebx     ; rewind EDI
	mov    ecx, ebp     ; reset ECX
	rep movsd           ; MOVE data from DS:ESI to ES:EDI
	sub    edi, ebx     ; rewind EDI
	sub    esi, ebx     ; rewind ESI
	repe cmpsd          ; COMPARE moved data in ES:EDI with DS:ESI
	cmp    ecx, 0
	jne    error        ; ECX must be 0
%endmacro


header:
	db COPYRIGHT

cpuTest:
;
;   Basic 16-bit flags, jumps, and shifts tests
;
	cli
	mov    ax, 0D58Dh     ; AH bits 7,6,4,2,0=1
	sahf                  ; Store AH into FLAGS: AH bits 7,6,4,2,0->SF,ZF,AF,PF,CF
	jnb    cpuTestError   ; if not below (CF=0)
	jnz    cpuTestError   ; if not zero (ZF=0)
	jnp    cpuTestError   ; if not parity (PF=0)
	jns    cpuTestError   ; if not sign (SF=0)
	lahf                  ; Load AH from Flags: SF,ZF,AF,PF,CF->AH bits 7,6,4,2,0
	mov    cl, 5          ; CL <- 5
	shr    ah, cl         ; Unsigned divide AH by 2, CL (5) times: AH=00000110. CF = low-order bit of AH
	jnb    cpuTestError   ; if not below (CF=0)
	mov    al, 1000000b
	shl    al, 1          ; OF = high-order bit of AL <> (CF);
	jno    cpuTestError   ; if not overflow (OF=0)
	xor    ah, ah         ; AH = 0
	sahf
	jbe    cpuTestError   ; if below or equal (CF=1 or ZF=1)
	js     cpuTestError   ; if sign (SF=1)
	jp     cpuTestError   ; if parity (PF=1)
	lahf                  ; AH = 0
	shr    ah, cl         ; shift AH right CL times, CF = low-order bit of AH
	jb     cpuTestError   ; if below (CF=1)
	shl    ah, 1          ; OF = high-order bit of AH <> (CF)
	jo     cpuTestError   ; if overflow (OF=1)
	jz     start          ; if 0 (ZF=1)
cpuTestError:
	hlt
	jmp cpuTestError

start:
;
;   Quick tests of unsigned 32-bit multiplication and division
;   Thorough arithmetical and logical tests are done later
;
	POST 1
	mov    eax, 0x80000001
	imul   eax
	mov    eax, 0x44332211
	mov    ebx, eax
	mov    ecx, 0x88776655
	mul    ecx
	div    ecx
	cmp    eax, ebx
	jne    error

;
;   Test of moving a segment register to a 32-bit register
;
	POST 2
	xor    edx, edx   ; dx <- 0
	mov    ds, dx     ; DS <- 0x0000
	mov    eax, ds
	test   eax, eax
	jnz    error

;
;   Test store, move, scan, and compare string data in real mode
;
	POST 3
	xor    dx, dx
	mov    ecx, 0x1000  ; ECX <- 4K double words
	mov    ds, dx
	mov    esi, 0       ; DS:ESI <- 0000h:0000h
	mov    es, dx
	mov    edi, 0x8000  ; ES:EDI <- 0000h:8000h
	testStringOps

	jmp initPages

	times  32768 nop ; lots of NOPs to test generation of 16-bit conditional jumps

addrGDT:
	dw myGDTEnd - myGDT - 1 ; 16-bit limit of myGDT
	dw myGDT, 0x000f        ; 32-bit base address of myGDT

myGDT:
	defDesc NULL ; the first descriptor in any descriptor table is always a dud (it corresponds to the null selector)
	defDesc CSEG_PROT16,0x000f0000,0x0000ffff,ACC_TYPE_CODE_READABLE,EXT_NONE
	defDesc CSEG_PROT32,0x000f0000,0x0000ffff,ACC_TYPE_CODE_READABLE,EXT_BIG
	defDesc DSEG_PROT16,0x00000000,0x000fffff,ACC_TYPE_DATA_WRITABLE,EXT_NONE
	defDesc DSEG_PROT32,0x00000000,0x000fffff,ACC_TYPE_DATA_WRITABLE,EXT_BIG
	defDesc SSEG_PROT32,0x00010000,0x000effff,ACC_TYPE_DATA_WRITABLE,EXT_BIG
myGDTEnd:

addrIDT:
	dw myIDTEnd - myIDT - 1  ; 16-bit limit of myIDT
	dw myIDT, 0x000f         ; 32-bit base address of myIDT

myIDT:
	defGate CSEG_PROT32, OFF_INTDIVERR
	defGate CSEG_PROT32, OFF_INTDEFAULT
	defGate CSEG_PROT32, OFF_INTDEFAULT
	defGate CSEG_PROT32, OFF_INTDEFAULT
	defGate CSEG_PROT32, OFF_INTDEFAULT
	defGate CSEG_PROT32, OFF_INTDEFAULT
	defGate CSEG_PROT32, OFF_INTDEFAULT
	defGate CSEG_PROT32, OFF_INTDEFAULT
	defGate CSEG_PROT32, OFF_INTDEFAULT
	defGate CSEG_PROT32, OFF_INTDEFAULT
	defGate CSEG_PROT32, OFF_INTDEFAULT
	defGate CSEG_PROT32, OFF_INTDEFAULT
	defGate CSEG_PROT32, OFF_INTDEFAULT
	defGate CSEG_PROT32, OFF_INTDEFAULT
	defGate CSEG_PROT32, OFF_INTPAGEFAULT
	defGate CSEG_PROT32, OFF_INTDEFAULT
	defGate CSEG_PROT32, OFF_INTDEFAULT
myIDTEnd:

addrIDTReal:
	dw 0x3FF      ; 16-bit limit of real-mode IDT
	dd 0x00000000 ; 32-bit base address of real-mode IDT

initPages:
;
;   ESI (PDBR) = 1000h
;   0000-1000    1000 (4K)  free
;   1000-3000    1000 (4K)  page directory
;   2000-3000    1000 (4K)  page table
;   3000-11000   e000 (56K) stack tests
;   12000-13000  1000 (4K)  non present page (PTE 12h)
;   13000-a0000 8d000       free
;

PAGE_DIR_ADDR equ 0x1000
PAGE_TBL_ADDR equ 0x2000
NOT_PRESENT_PTE equ 0x12
NOT_PRESENT_LIN equ 0x12000
PF_HANDLER_SIG equ 0x50465046

;   Now we want to build a page directory and a page table. We need two pages of
;   4K-aligned physical memory.  We use a hard-coded address, segment 0x100,
;   corresponding to physical address 0x1000.
;
	POST 4
	mov   esi, PAGE_DIR_ADDR
	mov	  eax, esi
	shr   eax, 4
	mov   es,  eax
;
;   Build a page directory at ES:EDI (0100:0000) with only 1 valid PDE (the first one),
;   because we're not going to access any memory outside the first 1MB.
;
	cld
	mov   eax, PAGE_TBL_ADDR | PTE_USER | PTE_READWRITE | PTE_PRESENT
	xor   edi, edi
	stosd
	mov   ecx, 1024-1 ; ECX == number of (remaining) PDEs to write
	xor   eax, eax    ; fill remaining PDEs with 0
	rep   stosd
;
;   Build a page table at EDI with 256 (out of 1024) valid PTEs, mapping the first 1MB
;   as linear == physical.
;
	mov   eax, PTE_USER | PTE_READWRITE | PTE_PRESENT
	mov   ecx, 256 ; ECX == number of PTEs to write
initPT:
	stosd
	add   eax, 0x1000
	loop  initPT
	mov   ecx, 1024-256 ; ECX == number of (remaining) PTEs to write
	xor   eax, eax
	rep   stosd
	mov   edi, NOT_PRESENT_PTE ; mark PTE 12h (page at phy 12000h) as not present
	shl   edi, 2
	add   edi, PAGE_DIR_ADDR ; edi <- PAGE_DIR_ADDR + (NOT_PRESENT_PTE * 4)
	mov   eax, NOT_PRESENT_LIN | PTE_USER | PTE_READWRITE
	stosd

goProt:
	POST 5
	cli ; make sure interrupts are off now, since we've not initialized the IDT yet
	o32 lidt [cs:addrIDT]
	o32 lgdt [cs:addrGDT]
	mov    cr3, esi
	mov    eax, cr0
	%if PAGING
	or     eax, CR0_MSW_PE | CR0_PG
	%else
	or     eax, CR0_MSW_PE
	%endif
	mov    cr0, eax
	jmp    CSEG_PROT32:toProt32 ; jump to flush the prefetch queue

toProt32:
	bits 32

;
;   Test stack
;
	POST 6
	mov    ax, DSEG_PROT16
	mov    ds, ax
	mov    es, ax
;
;   We'll set the top of our stack to ESI+0x2000+0xe000. This guarantees an ESP greater
;   than 0xffff, and so for the next few tests, with a 16-bit data segment in SS, we
;   expect all pushes/pops will occur at SP rather than ESP.
;
	add    esi, 0x2000       ; ESI <- PDBR + 0x2000, bottom of scratch memory
	mov    ss,  ax           ; SS <- DSEG_PROT16 (0x00000000 - 0x000fffff)
	lea    esp, [esi+0xe000] ; set ESP to bottom of scratch + 56K
	lea    ebp, [esp-4]
	and    ebp, 0xffff       ; EBP now mirrors SP instead of ESP
	mov    ebx, [ebp]        ; save dword about to be trashed by pushes
	mov    eax, 0x11223344
	push   eax
	cmp    [ebp], eax        ; did the push use SP instead of ESP?
	jne    error             ; no, error

	POST 7
	pop    eax
	push   ax
	cmp    [ebp+2], ax
	jne    error

	POST 8
	pop    ax
	mov    [ebp], ebx      ; restore dword trashed by the above pushes
	mov    ax,  DSEG_PROT32
	mov    ss,  ax
	lea    esp, [esi+0xe000] ; SS:ESP should now be a valid 32-bit pointer
	lea    ebp, [esp-4]
	mov    edx, [ebp]
	mov    eax, 0x11223344
	push   eax
	cmp    [ebp], eax  ; did the push use ESP instead of SP?
	jne    error       ; no, error

	POST 9
	pop    eax
	push   ax
	cmp    [ebp+2], ax
	jne    error
	pop    ax

;
;   Test moving a segment register to a 32-bit memory location
;
	POST A
	mov    ebx, [0x0000] ; save the DWORD at 0x0000:0x0000 in EBX
	or     eax, -1
	mov    [0x0000], eax
	mov    [0x0000], ds
	mov    ax, ds
	cmp    eax, [0x0000]
	jne    error

	POST B
	mov    eax, ds
	xor    eax, 0xffff0000
	cmp    eax, [0x0000]
	jne    error

	mov    [0x0000], ebx ; restore the DWORD at 0x0000:0x0000 from EBX

;
;   Test moving a byte to a 32-bit register with sign-extension
;
	POST C
	movsx  eax, byte [cs:signedByte]
	cmp    eax, 0xffffff80
	jne    error
;
;   Test moving a word to a 32-bit register with sign-extension
;
	POST D
	movsx  eax, word [cs:signedWord]
	cmp    eax, 0xffff8080
	jne    error
;
;   Test moving a byte to a 32-bit register with zero-extension
;
	POST E
	movzx  eax, byte [cs:signedByte]
	cmp    eax, 0x00000080
	jne    error
;
;   Test moving a word to a 32-bit register with zero-extension
;
	POST F
	movzx  eax, word [cs:signedWord]
	cmp    eax, 0x00008080
	jne    error
;
;   More assorted zero and sign-extension tests
;
	POST 10
	mov    esp, 0x40000
	mov    edx, [esp]
	push   edx      ; save word at scratch address 0x40000
	add    esp, 4
	push   byte -128       ; NASM will not use opcode 0x6A ("PUSH imm8") unless we specify "byte"
	pop    ebx             ; verify EBX == 0xFFFFFF80
	cmp    ebx, 0xFFFFFF80
	jne    error

	POST 11
	and    ebx, 0xff       ; verify EBX == 0x00000080
	cmp    ebx, 0x00000080
	jne    error

	POST 12
	movsx  bx, bl          ; verify EBX == 0x0000FF80
	cmp    ebx, 0x0000FF80
	jne    error

	POST 13
	movsx  ebx, bx         ; verify EBX == 0xFFFFFF80
	cmp    ebx, 0xFFFFFF80
	jne    error

	POST 14
	movzx  bx,  bl         ; verify EBX == 0xFFFF0080
	cmp    ebx, 0xFFFF0080
	jne    error

	POST 15
	movzx  ebx, bl         ; verify EBX == 0x00000080
	cmp    ebx, 0x00000080
	jne    error

	POST 16
	not    ebx             ; verify EBX == 0xFFFFFF7F
	cmp    ebx,0xFFFFFF7F
	jne    error

	POST 17
	movsx  bx, bl          ; verify EBX == 0xFFFF007F
	cmp    ebx, 0xFFFF007F
	jne    error

	POST 18
	movsx  ebx, bl         ; verify EBX == 0x0000007F
	cmp    ebx, 0x0000007F
	jne    error

	POST 19
	not    ebx             ; verify EBX == 0xFFFFFF80
	cmp    ebx, 0xFFFFFF80
	jne    error

	POST 1A
	movzx  ebx, bx         ; verify EBX == 0x0000FF80
	cmp    ebx, 0x0000FF80
	jne    error

	POST 1B
	movzx  bx, bl          ; verify EBX == 0x00000080
	cmp    ebx,0x00000080
	jne    error

	POST 1C
	movsx  bx, bl
	neg    bx
	neg    bx
	cmp    ebx, 0x0000FF80
	jne    error

	POST 1D
	movsx  ebx, bx
	neg    ebx
	neg    ebx
	cmp    ebx, 0xFFFFFF80
	jne    error

;
;   Test assorted 32-bit addressing modes
;
	mov    ax, SSEG_PROT32  ; we want SS != DS for the next tests
	mov    ss, ax

	mov    ebx, 0x11223344
	mov    [0x40000], ebx  ; store a known word at the scratch address

	; now access that scratch address using various addressing modes
	POST 1E
	mov    ecx, 0x40000
	cmp    [ecx], ebx
	jne    error

	POST 1F
	add    ecx, 64
	cmp    [ecx-64], ebx
	jne    error

	POST 20
	sub    ecx, 64
	shr    ecx, 1
	cmp    [ecx+0x20000], ebx
	jne    error

	POST 21
	cmp    [ecx+ecx], ebx
	jne    error

	POST 22
	shr    ecx, 1
	cmp    [ecx+ecx*2+0x10000], ebx
	jne    error

	POST 23
	cmp    [ecx*4], ebx
	jne    error

	POST 24
	mov    ebp, ecx
	cmp    [ebp+ecx*2+0x10000], ebx
	je     error ; since SS != DS, this better be a mismatch

	pop    edx
	mov    [0x40000], edx ; restore word at scratch address 0x40000

;
;   Verify string operations
;
	POST 25
	pushad
	pushfd
	xor    dx, dx
	mov    ecx, 0x800   ; ECX <- 2K double words
	mov    esi, 0x13000
	mov    edi, 0x15000
	testStringOps
	popfd
	popad

;
;	Verify Page faults
;
	POST 26
	mov eax, [NOT_PRESENT_LIN] ; generate a page fault
	cmp eax, PF_HANDLER_SIG    ; the page fault handler should have put its signature in memory
	jne error

;
;   Verify Bit Scan operations
;
%macro	bitscan	1
	mov edx, 1
	shl edx, 31
	mov ecx, 31
.%1loop:
	%1  ebx, edx
	shr edx, 1
	lahf
	cmp ebx, ecx
	jne error
	sahf
	loopne .%1loop ; if CX>0 ZF must be 0
	cmp ecx, 0
	jne error ; CX must be 0
%endmacro

	POST 27
	bitscan bsf

	POST 28
	bitscan bsr

;
;   Verify Bit Test operations
;

%macro bittest 1
	mov edx, 0xaaaaaaaa
	mov ecx, 31
.%1loop:
	%1 edx, ecx
	lahf ; save CF
	test ecx, 1
	jz .%1zero
.%1one:
	sahf ; bit in CF must be 1
	jnb error
	jmp .%1next
.%1zero:
	sahf ; bit in CF must be 0
	jb error
.%1next:
	dec ecx
	jns .%1loop
%endmacro

	POST 29
	bittest bt

	POST 30
	bittest btc
	cmp edx, 0x55555555
	jne error

	POST 31
	bittest btr
	cmp edx, 0
	jne error

	POST 32
	bittest bts
	cmp edx, 0xffffffff
	jne error

;
;   Double precision shifts
;

	POST 33
	mov dword [0x40000], 0x0000a5a5
	mov ebx, 0x5a5a0000
	shld [0x40000], ebx, 16
	cmp dword [0x40000], 0xa5a55a5a
	jne error

	POST 34
	mov dword [0x40000], 0xa5a50000
	mov ebx, 0x00005a5a
	shrd [0x40000], ebx, 16
	cmp dword [0x40000], 0x5a5aa5a5
	jne error

;
;   Now run a series of unverified tests for arithmetical and logical opcodes
;   Manually verify by comparing the tests output with a reference file
;
	POST AA

	%if LPT_PORT && IBM_PS1
	; Enable output to the configured LPT port
	mov    ax, 0xff7f  ; bit 7 = 0  setup functions
	out    94h, al     ; system board enable/setup register
	mov    dx, 102h
	in     al, dx      ; al = p[102h] POS register 2
	or     al, 0x91    ; enable LPT1 on port 3BCh, normal mode
	out    dx, al
	mov    al, ah
	out    94h, al     ; bit 7 = 1  enable functions
	%endif

	cld
	mov    esi, tableOps   ; ESI -> tableOps entry

testOps:
	movzx  ecx, byte [cs:esi]           ; ECX == length of instruction sequence
	test   ecx, ecx                     ; (must use JZ since there's no long version of JECXZ)
	jz     near testDone                ; zero means we've reached the end of the table
	movzx  ebx, byte [cs:esi+1]         ; EBX == TYPE
	shl    ebx, 6                       ; EBX == TYPE * 64
	movzx  edx, byte [cs:esi+2]         ; EDX == SIZE
	shl    edx, 4                       ; EDX == SIZE * 16
	lea    ebx, [cs:typeValues+ebx+edx] ; EBX -> values for type
	add    esi, 3                       ; ESI -> instruction mnemonic
.skip:
	cs lodsb
	test   al,al
	jnz    .skip
	push   ecx
	mov    ecx, [cs:ebx]    ; ECX == count of values for dst
	mov    eax, [cs:ebx+4]  ; EAX -> values for dst
	mov    ebp, [cs:ebx+8]  ; EBP == count of values for src
	mov    edi, [cs:ebx+12] ; EDI -> values for src
	xchg   ebx, eax         ; EBX -> values for dst
	sub    eax, eax         ; set all ARITH flags to known values prior to tests
testDst:
	push   ebp
	push   edi
	pushfd
testSrc:
	mov   eax, [cs:ebx]    ; EAX == dst
	mov   edx, [cs:edi]    ; EDX == src
	popfd
	call  printOp
	call  printEAX
	call  printEDX
	call  printPS
	call  esi       ; execute the instruction sequence
	call  printEAX
	call  printEDX
	call  printPS
	call  printEOL
	pushfd
	add   edi,4    ; EDI -> next src
	dec   ebp      ; decrement src count
	jnz   testSrc
	popfd
	pop   edi         ; ESI -> restored values for src
	pop   ebp         ; EBP == restored count of values for src
	lea   ebx,[ebx+4] ; EBX -> next dst (without modifying flags)
	loop  testDst

	pop  ecx
	add  esi, ecx     ; ESI -> next tableOps entry
	jmp  testOps

testDone:
	jmp testsDone

;
;   printOp(ESI -> instruction sequence)
;
;   Rewinds ESI to the start of the mnemonic preceding the instruction sequence and prints the mnemonic
;
;   Uses: None
;
printOp:
	pushfd
	pushad
.findSize:
	dec    esi
	mov    al, [cs:esi-1]
	cmp    al, 32
	jae    .findSize
	call   printStr
	movzx  eax, al
	mov    al, [cs:achSize+eax]
	call   printChar
	mov    al, ' '
	call   printChar
	popad
	popfd
	ret

;
;   printEAX()
;
;   Uses: None
;
printEAX:
	pushfd
	pushad
	mov     esi, strEAX
	call    printStr
	mov     cl, 8
	call    printVal
	popad
	popfd
	ret

;
;   printEDX()
;
;   Uses: None
;
printEDX:
	pushfd
	pushad
	mov    esi, strEDX
	call   printStr
	mov    cl, 8
	mov    eax, edx
	call   printVal
	popad
	popfd
	ret

;
;   printPS(ESI -> instruction sequence)
;
;   Uses: None
;
printPS:
	pushfd
	pushad
	pushfd
	pop    edx
.findType:
	dec    esi
	mov    al, [cs:esi-1]
	cmp    al, 32
	jae    .findType
	movzx  eax, byte [cs:esi-2]
	and    edx, [cs:typeMasks+eax*4]
	mov    esi, strPS
	call   printStr
	mov    cl, 4
	mov    eax, edx
	call   printVal
	popad
	popfd
	ret

;
;   printEOL()
;
;   Uses: None
;
printEOL:
	push    eax
;	mov     al,0x0d
;	call    printChar
	mov     al,0x0a
	call    printChar
	pop     eax
	ret

;
;   printChar(AL)
;
;   Uses: None
;
printChar:
	pushfd
	push   edx
	%if COM_PORT
	push   eax
	mov    dx, [cs:COMLSRports+(COM_PORT-1)*2]   ; EDX == COM LSR (Line Status Register)
.loop:
	in     al, dx
	test   al, 0x20    ; THR (Transmitter Holding Register) empty?
	jz     .loop       ; no
	pop    eax
	mov    dx, [cs:COMTHRports+(COM_PORT-1)*2]   ; EDX -> COM2 THR (Transmitter Holding Register)
	out    dx, al
	jmp    $+2
	%endif
	%if LPT_PORT
	mov    dx, [cs:LPTports+(LPT_PORT-1)*2]
	out    dx, al
	jmp    $+2
	%endif
	pop    edx
	popfd
	ret

;
;   printStr(ESI -> zero-terminated string)
;
;   Uses: ESI, Flags
;
printStr:
	push    eax
.loop:
	cs lodsb
	test    al, al
	jz      .done
	call    printChar
	jmp     .loop
.done:
	pop     eax
	ret

;
;   printVal(EAX == value, CL == number of hex digits)
;
;   Uses: EAX, ECX, Flags
;
printVal:
	shl    cl, 2  ; CL == number of bits (4 times the number of hex digits)
	jz     .done
.loop:
	sub    cl, 4
	push   eax
	shr    eax, cl
	and    al, 0x0f
	add    al, '0'
	cmp    al, '9'
	jbe    .digit
	add    al, 'A'-'0'-10
.digit:
	call   printChar
	pop    eax
	test   cl, cl
	jnz    .loop
.done:
	mov    al, ' '
	call   printChar
	ret

TYPE_ARITH    equ  0
TYPE_ARITH1   equ  1
TYPE_LOGIC    equ  2
TYPE_MULTIPLY equ  3
TYPE_DIVIDE   equ  4

SIZE_BYTE     equ  0
SIZE_SHORT    equ  1
SIZE_LONG     equ  2

%macro	defOp	6
    %ifidni %3,al
	%assign size SIZE_BYTE
    %elifidni %3,dl
	%assign size SIZE_BYTE
    %elifidni %3,ax
	%assign size SIZE_SHORT
    %elifidni %3,dx
	%assign size SIZE_SHORT
    %else
	%assign size SIZE_LONG
    %endif
	db	%%end-%%beg,%6,size
%%name:
	db	%1,0
%%beg:
    %ifidni %4,none
	%2	%3
    %elifidni %5,none
	%2	%3,%4
    %else
	%2	%3,%4,%5
    %endif
	ret
%%end:
%endmacro

strEAX: db  "EAX=",0
strEDX: db  "EDX=",0
strPS:  db  "PS=",0
strDE:  db  "#DE ",0 ; when this is displayed, it indicates a Divide Error exception
achSize db  "BWD"

ALLOPS equ 1

tableOps:
	defOp    "ADD",add,al,dl,none,TYPE_ARITH
	defOp    "ADD",add,ax,dx,none,TYPE_ARITH
	defOp    "ADD",add,eax,edx,none,TYPE_ARITH
	defOp    "OR",or,al,dl,none,TYPE_LOGIC
	defOp    "OR",or,ax,dx,none,TYPE_LOGIC
	defOp    "OR",or,eax,edx,none,TYPE_LOGIC
	defOp    "ADC",adc,al,dl,none,TYPE_ARITH
	defOp    "ADC",adc,ax,dx,none,TYPE_ARITH
	defOp    "ADC",adc,eax,edx,none,TYPE_ARITH
	defOp    "SBB",sbb,al,dl,none,TYPE_ARITH
	defOp    "SBB",sbb,ax,dx,none,TYPE_ARITH
	defOp    "SBB",sbb,eax,edx,none,TYPE_ARITH
	defOp    "AND",and,al,dl,none,TYPE_LOGIC
	defOp    "AND",and,ax,dx,none,TYPE_LOGIC
	defOp    "AND",and,eax,edx,none,TYPE_LOGIC
	defOp    "SUB",sub,al,dl,none,TYPE_ARITH
	defOp    "SUB",sub,ax,dx,none,TYPE_ARITH
	defOp    "SUB",sub,eax,edx,none,TYPE_ARITH
	defOp    "XOR",xor,al,dl,none,TYPE_LOGIC
	defOp    "XOR",xor,ax,dx,none,TYPE_LOGIC
	defOp    "XOR",xor,eax,edx,none,TYPE_LOGIC
	defOp    "CMP",cmp,al,dl,none,TYPE_ARITH
	defOp    "CMP",cmp,ax,dx,none,TYPE_ARITH
	defOp    "CMP",cmp,eax,edx,none,TYPE_ARITH
	defOp    "INC",inc,al,none,none,TYPE_ARITH1
	defOp    "INC",inc,ax,none,none,TYPE_ARITH1
	defOp    "INC",inc,eax,none,none,TYPE_ARITH1
	defOp    "DEC",dec,al,none,none,TYPE_ARITH1
	defOp    "DEC",dec,ax,none,none,TYPE_ARITH1
	defOp    "DEC",dec,eax,none,none,TYPE_ARITH1
	defOp    "MULA",mul,dl,none,none,TYPE_MULTIPLY
	defOp    "MULA",mul,dx,none,none,TYPE_MULTIPLY
	defOp    "MULA",mul,edx,none,none,TYPE_MULTIPLY
	defOp    "IMULA",imul,dl,none,none,TYPE_MULTIPLY
	defOp    "IMULA",imul,dx,none,none,TYPE_MULTIPLY
	defOp    "IMULA",imul,edx,none,none,TYPE_MULTIPLY
	defOp    "IMUL",imul,ax,dx,none,TYPE_MULTIPLY
	defOp    "IMUL",imul,eax,edx,none,TYPE_MULTIPLY
	defOp    "IMUL8",imul,ax,dx,0x77,TYPE_MULTIPLY
	defOp    "IMUL8",imul,ax,dx,-0x77,TYPE_MULTIPLY
	defOp    "IMUL8",imul,eax,edx,0x77,TYPE_MULTIPLY
	defOp    "IMUL8",imul,eax,edx,-0x77,TYPE_MULTIPLY
	defOp    "IMUL16",imul,ax,0x777,none,TYPE_MULTIPLY
	defOp    "IMUL32",imul,eax,0x777777,none,TYPE_MULTIPLY
	defOp    "DIVDL",div,dl,none,none,TYPE_DIVIDE
	defOp    "DIVDX",div,dx,none,none,TYPE_DIVIDE
	defOp    "DIVEDX",div,edx,none,none,TYPE_DIVIDE
	defOp    "DIVAL",div,al,none,none,TYPE_DIVIDE
	defOp    "DIVAX",div,ax,none,none,TYPE_DIVIDE
	defOp    "DIVEAX",div,eax,none,none,TYPE_DIVIDE
	defOp    "IDIVDL",idiv,dl,none,none,TYPE_DIVIDE
	defOp    "IDIVDX",idiv,dx,none,none,TYPE_DIVIDE
	defOp    "IDIVEDX",idiv,edx,none,none,TYPE_DIVIDE
	defOp    "IDIVAL",idiv,al,none,none,TYPE_DIVIDE
	defOp    "IDIVAX",idiv,ax,none,none,TYPE_DIVIDE
	defOp    "IDIVEAX",idiv,eax,none,none,TYPE_DIVIDE
	db	0

	align	4

typeMasks:
	dd	PS_ARITH
	dd	PS_ARITH
	dd	PS_LOGIC
	dd	PS_MULTIPLY
	dd	PS_DIVIDE

arithValues:
.bvals:	dd	0x00,0x01,0x02,0x7E,0x7F,0x80,0x81,0xFE,0xFF
	ARITH_BYTES equ ($-.bvals)/4

.wvals:	dd	0x0000,0x0001,0x0002,0x7FFE,0x7FFF,0x8000,0x8001,0xFFFE,0xFFFF
	ARITH_WORDS equ ($-.wvals)/4

.dvals:	dd	0x00000000,0x00000001,0x00000002,0x7FFFFFFE,0x7FFFFFFF,0x80000000,0x80000001,0xFFFFFFFE,0xFFFFFFFF
	ARITH_DWORDS equ ($-.dvals)/4

muldivValues:
.bvals:	dd	0x00,0x01,0x02,0x3F,0x40,0x41,0x7E,0x7F,0x80,0x81,0xFE,0xFF
	MULDIV_BYTES equ ($-.bvals)/4

.wvals:	dd	0x0000,0x0001,0x0002,0x3FFF,0x4000,0x4001,0x7FFE,0x7FFF,0x8000,0x8001,0xFFFE,0xFFFF
	MULDIV_WORDS equ ($-.wvals)/4

.dvals:	dd	0x00000000,0x00000001,0x00000002,0x3FFFFFFF,0x40000000,0x40000001,0x7FFFFFFE,0x7FFFFFFF,0x80000000,0x80000001,0xFFFFFFFE,0xFFFFFFFF
	MULDIV_DWORDS equ ($-.dvals)/4

typeValues:
	;
	; Values for TYPE_ARITH
	;
	dd	ARITH_BYTES,arithValues,ARITH_BYTES,arithValues
	dd	ARITH_BYTES+ARITH_WORDS,arithValues,ARITH_BYTES+ARITH_WORDS,arithValues
	dd	ARITH_BYTES+ARITH_WORDS+ARITH_DWORDS,arithValues,ARITH_BYTES+ARITH_WORDS+ARITH_DWORDS,arithValues
	dd	0,0,0,0
	;
	; Values for TYPE_ARITH1
	;
	dd	ARITH_BYTES,arithValues,1,arithValues
	dd	ARITH_BYTES+ARITH_WORDS,arithValues,1,arithValues
	dd	ARITH_BYTES+ARITH_WORDS+ARITH_DWORDS,arithValues,1,arithValues
	dd	0,0,0,0
	;
	; Values for TYPE_LOGIC (using ARITH values for now)
	;
	dd	ARITH_BYTES,arithValues,ARITH_BYTES,arithValues
	dd	ARITH_BYTES+ARITH_WORDS,arithValues,ARITH_BYTES+ARITH_WORDS,arithValues
	dd	ARITH_BYTES+ARITH_WORDS+ARITH_DWORDS,arithValues,ARITH_BYTES+ARITH_WORDS+ARITH_DWORDS,arithValues
	dd	0,0,0,0
	;
	; Values for TYPE_MULTIPLY (a superset of ARITH values)
	;
	dd	MULDIV_BYTES,muldivValues,MULDIV_BYTES,muldivValues
	dd	MULDIV_BYTES+MULDIV_WORDS,muldivValues,MULDIV_BYTES+MULDIV_WORDS,muldivValues
	dd	MULDIV_BYTES+MULDIV_WORDS+MULDIV_DWORDS,muldivValues,MULDIV_BYTES+MULDIV_WORDS+MULDIV_DWORDS,muldivValues
	dd	0,0,0,0
	;
	; Values for TYPE_DIVIDE
	;
	dd	MULDIV_BYTES,muldivValues,MULDIV_BYTES,muldivValues
	dd	MULDIV_BYTES+MULDIV_WORDS,muldivValues,MULDIV_BYTES+MULDIV_WORDS,muldivValues
	dd	MULDIV_BYTES+MULDIV_WORDS+MULDIV_DWORDS,muldivValues,MULDIV_BYTES+MULDIV_WORDS+MULDIV_DWORDS,muldivValues
	dd	0,0,0,0

	times	OFF_ERROR-($-$$) nop

error:
	cli
	hlt
	jmp	error

	times	OFF_INTDIVERR-($-$$) nop

intDivErr:
	push esi
	mov  esi,strDE
	call printStr
	pop  esi
;
;   It's rather annoying that the 80386 treats #DE as a fault rather than a trap, leaving CS:EIP pointing to the
;   faulting instruction instead of the RET we conveniently placed after it.  So, instead of trying to calculate where
;   that RET is, we simply set EIP on the stack to point to our own RET.
;
	mov  dword [esp], intDivRet
	iretd
intDivRet:
	ret

	times	OFF_INTPAGEFAULT-($-$$) nop

intPageFault:
 	; check the error code, it must be 0
	pop   eax
	cmp   eax, 0
	jnz error
	; check CR2 register, it must contain the linear address NOT_PRESENT_LIN
	mov   eax, cr2
	cmp   eax, NOT_PRESENT_LIN
	jne   error
	; mark the PTE as present
	mov   bx, ds ; save DS
	mov   ax, DSEG_PROT16
	mov   ds, ax
	mov   eax, NOT_PRESENT_PTE ; mark PTE as present
	shl   eax, 2
	add   eax, PAGE_TBL_ADDR ; eax <- PAGE_DIR_ADDR + (NOT_PRESENT_PTE * 4)
	mov   edx, [eax]
	or    edx, PTE_PRESENT
	mov   [eax], edx
	mov   eax, PAGE_DIR_ADDR
	mov   cr3, eax ; flush the page translation cache
	; mark the memory location at NOT_PRESENT_LIN with the handler signature
	mov   eax, PF_HANDLER_SIG
	mov   [NOT_PRESENT_LIN], eax
	mov   ds, bx ; restore DS
	xor   eax, eax
	iretd

LPTports:
	dw   0x3BC
	dw   0x378
	dw   0x278
COMTHRports:
	dw   0x3F8
	dw   0x2F8
COMLSRports:
	dw   0x3FD
	dw   0x2FD
signedWord:
	db   0x80
signedByte:
	db   0x80

testsDone:
;
; Testing finished, back to real mode and prepare to restart
;
	POST FE
	mov  ax,  DSEG_PROT16
	mov  ss,  ax
	sub  esp, esp
;
;   Return to real-mode, after first resetting the IDTR and loading CS with a 16-bit code segment
;
	o32 lidt [cs:addrIDTReal]
	jmp  CSEG_PROT16:toProt16
toProt16:
	bits 16
goReal:
	mov    eax, cr0
	and    eax, ~(CR0_MSW_PE | CR0_PG) & 0xffffffff
	mov    cr0, eax
jmpReal:
	jmp    CSEG_REAL:toReal
toReal:
	mov    ax, cs
	mov    ds, ax
	mov    es, ax
	mov    ss, ax
	mov    sp, 0xfffe

	POST FF
finish:	hlt
	jmp finish
;
;   Fill the remaining space with NOPs until we get to target offset 0xFFF0.
;
	times 0xfff0-($-$$) nop

resetVector:
	jmp   CSEG_REAL:cpuTest ; 0000FFF0

release:
	db    RELEASE,0       ; 0000FFF5  release date
	db    0xFC            ; 0000FFFE  FC (Model ID byte)
	db    0x00            ; 0000FFFF  00 (checksum byte, unused)
