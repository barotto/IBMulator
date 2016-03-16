/*
  FDDTEST.C - Direct access to floppy disk drives.
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
#include <conio.h>
#include <stdio.h>
#include <string.h>
#include <bios.h>

typedef unsigned char      uint8_t;
typedef unsigned int       uint16_t;
typedef unsigned long int  uint32_t;

#define WARNING 1

enum FloppyRegisters
{
	STATUS_REGISTER_A                = 0x3F0, // read-only
	STATUS_REGISTER_B                = 0x3F1, // read-only
	DIGITAL_OUTPUT_REGISTER          = 0x3F2,
	TAPE_DRIVE_REGISTER              = 0x3F3,
	MAIN_STATUS_REGISTER             = 0x3F4, // read-only
	DATARATE_SELECT_REGISTER         = 0x3F4, // write-only
	DATA_FIFO                        = 0x3F5,
	DIGITAL_INPUT_REGISTER           = 0x3F7, // read-only
	CONFIGURATION_CONTROL_REGISTER   = 0x3F7  // write-only
};

enum FloppyCommands
{
	READ_TRACK         = 2,
	SPECIFY            = 3,
	SENSE_DRIVE_STATUS = 4,
	WRITE_DATA         = 5,
	READ_DATA          = 6,
	RECALIBRATE        = 7,
	SENSE_INTERRUPT    = 8,
	WRITE_DELETED_DATA = 9,
	READ_ID            = 10,
	READ_DELETED_DATA  = 12,
	FORMAT_TRACK       = 13,
	DUMPREG            = 14,
	SEEK               = 15,
	VERSION            = 16,
	SCAN_EQUAL         = 17,
	PERPENDICULAR_MODE = 18,
	CONFIGURE          = 19,
	LOCK               = 20,
	VERIFY             = 22,
	SCAN_LOW_OR_EQUAL  = 25,
	SCAN_HIGH_OR_EQUAL = 29
};

typedef struct DriveParameterTable_s {
	uint8_t rSrtHdUnld; /* bits 4-7: SRT step rate time in ms
	                       bits 0-3: HUT head unload time in ms */
	uint8_t rDmaHdLd;   /* bits 1-7: HLT head load time in ms
	                       bit    0: ND 0=use DMA */
	uint8_t bMotorOff;  /* 55-ms increments before turning disk motor off */
	uint8_t bSectSize;  /* sector size (0=128, 1=256, 2=512, 3=1024) */
	uint8_t bLastTrack; /* EOT (last sector on a track) */
	uint8_t bGapLen;    /* gap length for read/write operations */
	uint8_t bDTL;       /* DTL (Data Transfer Length) max transfer when
	                       length not set */
	uint8_t bGapFmt;    /* gap length for format operation */
	uint8_t bFillChar;  /* fill character for format (normally 0f6H) */
	uint8_t bHdSettle;  /* head-settle time (in milliseconds) */
	uint8_t bMotorOn;   /* motor-startup time (in 1/8th-second intervals) */
} DriveParameterTable;

enum BIOSDataArea {
	INSTALLED_HARDWARE             = 0x10,
	DISKETTE_RECALIBRATE_STATUS    = 0x3E,
	DISKETTE_MOTOR_STATUS          = 0x3F,
	DISKETTE_MOTOR_TURNOFF_TIMEOUT = 0x40,
	DISKETTE_LAST_OPERATION_STATUS = 0x41,
	DISKETTE_DRIVE_MEDIA_STATE     = 0x90,
	DISKETTE_DRIVE0_CURRENT_TRACK  = 0x94,
	DISKETTE_DRIVE1_CURRENT_TRACK  = 0x95
};

#define BIOS_DATA_SEG 0x40

#define DMA_CHANNEL 0x02

enum ErrorConditions {
	SUCCESSFUL = 0,
	INVALID_COMMAND,
	INVALID_ARGUMENTS,
	BIOS_INT_ERROR,
	FIFO_READ_TIMEOUT,
	FIFO_WRITE_TIMEOUT,
	DRIVE_NOT_READY,
	ABNORMAL_TERMINATION,
	MOTOR_IS_ON,
	SEEK_ERROR,
	OUT_OF_MEMORY,

	ERRORS_COUNT
};

const char *g_error_str[ERRORS_COUNT] = {
	"successful",
	"invalid command",
	"invalid arguments",
	"BIOS INT error",
	"FIFO read timeout",
	"FIFO write timeout",
	"drive not ready",
	"abnormal termination",
	"motor is already on",
	"seek error",
	"out of memory"
};


/*******************************************************************************
 * Utility
 */

/* some defines to keep my 21st century IDE quiet */
#ifndef __MSDOS__
	#error
	#define far
	#define DISABLE_INTERRUPTS
	#define ENABLE_INTERRUPTS
#else
	#define DISABLE_INTERRUPTS asm{ cli; }
	#define ENABLE_INTERRUPTS  asm{ sti; }
#endif


#define CALL_INT13(CYL,SEC,HD) \
	/*low eight bits of cylinder number*/ \
	inregs.h.ch = (unsigned char)CYL; \
	/*sector number 1-63 (bits 0-5)*/ \
	inregs.h.cl = (unsigned char)(SEC & 0x3F); \
	/*high two bits of cylinder (bits 6-7)*/ \
	inregs.h.cl |= (unsigned char)((CYL & 0x300) >> 2); \
	/*head number*/ \
	inregs.h.dh = (unsigned char)HD; \
	int86x(0x13, &inregs, &outregs, &segregs);

#define INT13_ERROR (outregs.x.cflag != 0 || outregs.h.ah != 0)

#define CHECK(_FN_) if((result = _FN_) != SUCCESSFUL) return result

#define LOW_BYTE(_X_)  (_X_ & 0x00FF)
#define HI_BYTE(_X_)  ((_X_ & 0xFF00) >> 8)



void *far_memcpy(void *i_destv, const void far *i_srcv, size_t i_len)
{
	uint8_t *dest = i_destv;
	const uint8_t far *src = i_srcv;
	for(; i_len>0; i_len--) {
		*dest++ = *src++;
	}
	return i_destv;
}

/* Waits a specified amount of microseconds.
 * i_amount: number of usecs to wait expressed as multiple of 15.085 usecs.
 */
void wait(uint8_t i_amount)
{
	volatile register uint8_t p61, prev;

	do {
		do {
			p61 = inportb(0x61);
			p61 = p61 & 0x10;
		} while(p61 == prev);
		prev = p61;
	} while(i_amount--);
}

void wait_ms(uint16_t i_millisec)
{
	uint32_t micro;
	union REGS inregs, outregs;

	micro = (uint32_t)i_millisec * 1000;
	inregs.h.ah = 0x86;
	inregs.x.cx = micro>>16;
	inregs.x.dx = (uint16_t)micro;
	int86(0x15, &inregs, &outregs);
	if(outregs.x.cflag) {
		/* fallback */
		while(i_millisec--) {
			wait(66);
		}
	}
}


/*******************************************************************************
 * DMA transfers
 */

typedef struct {
	uint8_t  page;
	uint16_t offset;
	uint16_t length;
} DMA_block;

void DMA_block_load(DMA_block *o_block, void *i_data, size_t i_data_len)
{
	uint16_t temp, segment, offset;
	uint32_t offset32;

	segment = FP_SEG(i_data);
	offset  = FP_OFF(i_data);

	o_block->page = (segment & 0xF000) >> 12;
	temp = (segment & 0x0FFF) << 4;
	offset32 = offset + temp;
	if(offset32 > 0xFFFF) {
		o_block->page++;
	}
	o_block->offset = (uint16_t)offset32;
	o_block->length = (uint16_t)(i_data_len - 1);
}


/*******************************************************************************
 * Floppy functions
 */

int floppy_read_FIFO(uint8_t *i_data)
{
	uint16_t delay0 = 5;
	uint16_t delay1 = 65535;
	volatile register uint8_t msr;

	while(--delay0) {
		while(--delay1) {
			msr = inportb(MAIN_STATUS_REGISTER);
			if((msr&0xC0) == 0xC0) {
				*i_data = inportb(DATA_FIFO);
				wait(3);
				return SUCCESSFUL;
			}
		}
	}
	return FIFO_READ_TIMEOUT;
}

int floppy_write_FIFO(uint8_t i_data)
{
	uint16_t delay0 = 5;
	uint16_t delay1 = 65535;
	volatile register uint8_t msr;

	while(--delay0) {
		while(--delay1) {
			msr = inportb(MAIN_STATUS_REGISTER);
			if((msr&0xC0) == 0x80) {
				outportb(DATA_FIFO, i_data);
				wait(3);
				return SUCCESSFUL;
			}
		}
	}
	return FIFO_WRITE_TIMEOUT;
}

int floppy_wait_int()
{
	union REGS inregs, outregs;
	uint8_t drv_status, last_op;
	uint16_t delay0 = 6;
	uint16_t delay1 = 65535;
	int result = DRIVE_NOT_READY;

	/* wait for the drive to become ready */
	inregs.h.ah = 0x90;
	inregs.h.al = 0x01;
	int86(0x15, &inregs, &outregs);
	if(outregs.x.cflag == 0) {
		while(--delay0 && result!=SUCCESSFUL) {
			while(--delay1 && result!=SUCCESSFUL) {
				/* read BIOS data area 40:3E */
				drv_status = peekb(BIOS_DATA_SEG, DISKETTE_RECALIBRATE_STATUS);
				/* bit 7 is INT signal */
				if(drv_status & 0x80) {
					result = SUCCESSFUL;
				}
			}
		}
		if(result != SUCCESSFUL) {
			/* bit7 = drive not ready */
			last_op = peekb(BIOS_DATA_SEG, DISKETTE_LAST_OPERATION_STATUS);
			pokeb(BIOS_DATA_SEG, DISKETTE_LAST_OPERATION_STATUS, last_op|0x80);
		}
		/* reset INT flag */
		pokeb(BIOS_DATA_SEG, DISKETTE_RECALIBRATE_STATUS, drv_status&0x7F);
	}
	return result;
}

int floppy_wait_int_forever()
{
	union REGS inregs, outregs;
	uint8_t drv_status = 0;

	/* wait for the drive to become ready */
	inregs.h.ah = 0x90;
	inregs.h.al = 0x01;
	int86(0x15, &inregs, &outregs);
	if(outregs.x.cflag != 0) {
		return DRIVE_NOT_READY;
	}

	do {
		/* read BIOS data area 40:3E */
		drv_status = peekb(BIOS_DATA_SEG, DISKETTE_RECALIBRATE_STATUS);
		/* bit 7 is INT signal */
	} while(!(drv_status & 0x80));

	/* reset INT flag */
	pokeb(BIOS_DATA_SEG, DISKETTE_RECALIBRATE_STATUS, drv_status&0x7F);

	return SUCCESSFUL;
}

int floppy_read_result(uint8_t *i_res)
{
	volatile register uint8_t msr;
	uint8_t di;
	int result;

	for(di=0; di<10; ++di) {
		CHECK( floppy_read_FIFO(&i_res[di]) );
		msr = inportb(MAIN_STATUS_REGISTER);
		if(!(msr & 0x10)) {
			/* finish: bit 4 cleared at end of the Result Phase */
			return SUCCESSFUL;
		}
	}
	return DRIVE_NOT_READY;
}

int floppy_version(uint8_t *_version)
{
	uint8_t cmdres[10], result;

	CHECK( floppy_write_FIFO(VERSION) );
	CHECK( floppy_read_result(cmdres) );

	*_version = cmdres[0];

	return SUCCESSFUL;
}

int floppy_motor_on(uint8_t i_drive, uint8_t i_timeout)
{
	uint8_t drvbit = (1 << i_drive);
	uint8_t dor;
	uint8_t motor;
	int result = MOTOR_IS_ON;

	DISABLE_INTERRUPTS

	/* reset MOTOR TURN-OFF TIMEOUT COUNT */
	pokeb(BIOS_DATA_SEG, DISKETTE_MOTOR_TURNOFF_TIMEOUT, i_timeout);

	/* check if already on */
	motor = peekb(BIOS_DATA_SEG, DISKETTE_MOTOR_STATUS);
	if(!(motor & drvbit)) {
		/* set motor-on for i_drive */
		motor = (motor&0xE0) | drvbit;
		pokeb(BIOS_DATA_SEG, DISKETTE_MOTOR_STATUS, motor);

		/* re-enable interrupts */
		ENABLE_INTERRUPTS

		/* set drive selected bit */
		motor |= i_drive<<4;
		pokeb(BIOS_DATA_SEG, DISKETTE_MOTOR_STATUS, motor);

		/* turn on motor on FDD */
		dor = 0x10 << i_drive; /* bit5 (drive 1)
		                          bit4 (drive 0)
		                          drive motor select */
		dor |= 0x0C;           /* bit3 (!DMAGATE, so enable DMA)
		                          bit2 (!RESET, so disable reset) */
		dor |= i_drive;        /* bit0-1 drive select */

		outportb(DIGITAL_OUTPUT_REGISTER, dor);
		wait(500);

		result = SUCCESSFUL;
	}

	ENABLE_INTERRUPTS

	return result;
}

int floppy_sense(uint8_t *o_PCN)
{
	uint8_t cmdres[10], last_op;
	int result;

	CHECK( floppy_write_FIFO(SENSE_INTERRUPT) );
	CHECK( floppy_read_result(cmdres) );

	if(o_PCN != NULL) {
		*o_PCN = cmdres[1];
	}

	if((cmdres[0]&0x60) == 0x60) {
		/* IC bit6 == 1 (Abnormal termination) */
		/* SE bit5 == 1 (Seek End) */
		/* set seek error */
		last_op = peekb(BIOS_DATA_SEG, DISKETTE_LAST_OPERATION_STATUS);
		pokeb(BIOS_DATA_SEG, DISKETTE_LAST_OPERATION_STATUS, last_op|0x40);
		return SEEK_ERROR;
	}
	return SUCCESSFUL;
}

int floppy_chk_int()
{
	int result;

	CHECK( floppy_wait_int() );

	return floppy_sense(NULL);
}

void floppy_DMA_init(DMA_block *i_block, uint8_t i_dir)
{
	uint8_t mode;

	if(i_dir == 0) {
		/* read from floppy */
		/* mode = write transfer, autoinitialized, single mode */
		mode = 0x44;
	} else {
		/* write to floppy */
		/* mode = read transfer, autoinitialized, single mode */
		mode = 0x48;
	}

	DISABLE_INTERRUPTS

	/* mask channel */
	outportb(0x0A, DMA_CHANNEL | 0x04);

	/* send the offset address */
	outportb(0x0C, 0xFF); /* reset flip-flop */
	outportb(0x04, LOW_BYTE(i_block->offset));
	outportb(0x04, HI_BYTE(i_block->offset));

	/* send the page */
	outportb(0x81, i_block->page);

	/* send the count */
	outportb(0x0C, 0xFF); /* reset flip-flop */
	outportb(0x05, LOW_BYTE(i_block->length));
	outportb(0x05, HI_BYTE(i_block->length));

	/* set mode on channel */
	outportb(0x0B, mode | DMA_CHANNEL);

	/* Enable the DMA channel (clear the mask) */
	outportb(0x0A, DMA_CHANNEL);

	ENABLE_INTERRUPTS
}

int floppy_rw_track(uint8_t i_drive, DriveParameterTable *i_params,
		uint8_t _cyl, DMA_block *i_dma, uint8_t i_dir, uint8_t *i_cmd_result)
{
	uint8_t cmd, pcn, result;
	uint8_t cmd_result[10];

	if(i_dir == 0) {
		/* read from floppy */
		/* MT:MFM:SK:0:0:1:1:0 */
		cmd = READ_DATA;
	} else {
		/* write to floppy */
		/* MT:MFM:0:0:0:1:0:1 */
		cmd = WRITE_DATA;
	}
	/* Specify MT (multitrack) and MFM (double density) mode */
	cmd |= 0xC0;

	floppy_DMA_init(i_dma, i_dir);

	CHECK( floppy_write_FIFO(cmd) );
	CHECK( floppy_write_FIFO(i_drive&0x03) ); // head (0) and drive
	CHECK( floppy_write_FIFO(_cyl) );         // C (cylinder)
	CHECK( floppy_write_FIFO(0) );            // H (first head)
	CHECK( floppy_write_FIFO(1) );            // R (first sector, 1-based)
	CHECK( floppy_write_FIFO(i_params->bSectSize) ); // N (Sector size code)
	CHECK( floppy_write_FIFO(i_params->bLastTrack) );// EOT (end-of-tracks)
	CHECK( floppy_write_FIFO(i_params->bGapLen) );   // GPL (gap length)
	CHECK( floppy_write_FIFO(i_params->bDTL) );      // DTL (data length)

	CHECK( floppy_wait_int_forever() );
	CHECK( floppy_read_result(i_cmd_result) );

	if(i_cmd_result[0] & 0xC0) {
		return ABNORMAL_TERMINATION;
	}
	return SUCCESSFUL;
}

int cmd_INT13_reset()
{
	struct SREGS segregs;
	union REGS inregs, outregs;

	inregs.h.ah = 0x00;
	inregs.h.dl = 0x00;
	CALL_INT13(0,0,0);
	return INT13_ERROR;
}

int get_drive_paramenters(uint8_t i_drive, uint8_t *i_type, uint8_t *i_trks, uint8_t *i_spt,
                          DriveParameterTable *o_tbl)
{
	struct SREGS segregs;
	union REGS inregs, outregs;
	uint8_t far *table;

	inregs.h.ah = 0x08;
	inregs.h.dl = 0x00 + i_drive;
	segregs.es = 0;
	inregs.x.di = 0;

	int86x(0x13, &inregs, &outregs, &segregs);
	if(outregs.x.cflag) {
		return BIOS_INT_ERROR;
	}
	if(outregs.x.bx==0 && outregs.x.cx==0) {
		return BIOS_INT_ERROR;
	}
	*i_type = outregs.h.bl;
	*i_trks = outregs.h.ch;
	*i_spt  = outregs.h.cl;
	table  = MK_FP(segregs.es,outregs.x.di);
	far_memcpy(o_tbl, table, sizeof(DriveParameterTable));

	return SUCCESSFUL;
}


/*******************************************************************************
 * MAIN program
 */

uint8_t g_drive;
uint8_t g_max_trk;
uint8_t g_spt;
DriveParameterTable g_param_tbl;

typedef int(*command_func_t)(const char*);
typedef struct command_s {
	const char    *cmd;
	const char    *shcmd;
	const char    *args;
	command_func_t func;
	const char    *help;
} command_t;

int cmd_quit(const char*);
int cmd_help(const char*);
int cmd_drive(const char *);
int cmd_motor(const char*);
int cmd_seek(const char*);
int cmd_recalibrate(const char *);
int cmd_read(const char *);
int cmd_reset(const char *);
int cmd_rate(const char *);
int cmd_dump(const char *);
int cmd_specify(const char *);
int cmd_bench(const char *);

command_t g_commands_table[] = {
  { "quit",        "q", "",  &cmd_quit,        "quit the program" },
  { "help",        "h", "",  &cmd_help,        "print this help" },
  { "drive",       "d", "N", &cmd_drive,       "select the current drive, N=0,1" },
  { "motor",       "m", "N", &cmd_motor,       "turn motor on, auto turn off after N*55ms (max.255)" },
  { "seek",        "s", "N", &cmd_seek,        "seek to track N" },
  { "recalibrate", "c", "",  &cmd_recalibrate, "recalibrate the drive (seek to trk 0)" },
  { "read",        "r", "N", &cmd_read,        "read sectors 1 to N on the current cylinder" },
  { "reset",       "R", "",  &cmd_reset,       "reset the controller" },
  { "rate",        "t", "N", &cmd_rate,        "set data rate, N=0,1,2" },
  { "dump",        "D", "",  &cmd_dump,        "dump controller registers" },
  { "specify",     "S", "S,U,L", &cmd_specify, "specify SRT,HUL,HLT (ND always 0)" },
  { "bench",       "b", "N", &cmd_bench,       "timed data read of N sectors using BIOS funcs" },
  { NULL, NULL, NULL, NULL }
};

int cmd_quit(const char *i_args)
{
	exit(0);
	return 0;
}

int cmd_help(const char *i_args)
{
	command_t *p;

	printf("%-13s%-7s%-10s\n","Commands", "Short", "Args");
	for(p=g_commands_table; p->cmd!=NULL; ++p) {
		printf(" %-13s%-7s%-7s%s\n", p->cmd, p->shcmd, p->args, p->help);
	}
	printf("You can concatenate multiple commands with ; like so: m64;c;s79;s0\n");
	return SUCCESSFUL;
}

int cmd_reset(const char *i_args)
{
	int result, i;
	uint8_t dor;

	DISABLE_INTERRUPTS

	dor = inportb(DIGITAL_OUTPUT_REGISTER);
	outportb(DIGITAL_OUTPUT_REGISTER, 0x00);
	wait(2);
	outportb(DIGITAL_OUTPUT_REGISTER, dor);

	ENABLE_INTERRUPTS

	/* data rate: 250kb/s */
	outportb(CONFIGURATION_CONTROL_REGISTER, 0x02);

	CHECK( floppy_wait_int_forever() );

	/* repeat for all the possible drives connected */
	for(i=0; i<4; i++) {
		CHECK( floppy_sense(NULL) );
	}

	CHECK( floppy_write_FIFO(SPECIFY) );
	/* SRT=13,HUT=1,HLT=1,ND */
	CHECK( floppy_write_FIFO(0xD1) );
	CHECK( floppy_write_FIFO(0x02) );

	/* do a manual recalibrate for every drive
	CHECK( floppy_motor_on(g_drive, 64) );
	CHECK( cmd_recalibrate(NULL) );
	*/

	return SUCCESSFUL;
}

int cmd_motor(const char* i_args)
{
	unsigned timeout;

	if((!i_args) || (sscanf(i_args, "%u", &timeout)!=1)) {
		timeout = 0xFF;
	}
	if(timeout < 1 || timeout > 0xFF) {
		timeout = 0xFF;
	}

	return floppy_motor_on(g_drive, timeout);
}

int cmd_drive(const char *i_args)
{
	unsigned drive;
	uint8_t type, hw, dor;

	if((!i_args) || (sscanf(i_args, "%u", &drive)!=1)) {
		return INVALID_ARGUMENTS;
	}
	if(drive > 1) {
		return INVALID_ARGUMENTS;
	}

	hw = peekb(BIOS_DATA_SEG, INSTALLED_HARDWARE);
	/* bit 0 = floppy disk drives are installed */
	if((hw & 1) == 0) {
		printf("ERROR: no floppy drives installed on the system\n");
		return INVALID_ARGUMENTS;
	}
	/* bit 7-6 = number of floppy disk drives minus 1 */
	if( drive > ((hw & 0xC0) >> 6) ) {
		printf("drive not installed\n");
		return INVALID_ARGUMENTS;
	}
	if(get_drive_paramenters(drive,&type,&g_max_trk,&g_spt,&g_param_tbl)) {
		return BIOS_INT_ERROR;
	}
	g_drive = drive;

	/* turn off motors */
	dor = 0x0C;     /* bit3 (enable DMA)
	                   bit2 (disable reset) */
	dor |= drive;   /* bit0-1 drive select */
	outportb(DIGITAL_OUTPUT_REGISTER, dor);

	printf("current drive:%u, type:", drive);
	switch(type) {
		case 0x01: printf("360K"); break;
		case 0x02: printf("1.2M"); break;
		case 0x03: printf("720K"); break;
		case 0x04: printf("1.44M"); break;
		case 0x05: printf("2.88M(?)"); break;
		case 0x06: printf("2.88M"); break;
		case 0x10: printf("ATAPI Removable Media Device"); break;
		default:   printf("%d",type); break;
	}
	printf(", tracks:%u, sect. per track:%u\n", g_max_trk+1, g_spt);
	printf("ParamTbl = SRT:%u, HLT:%u, HUT:%u, HdStl:%u, Gap:%u, MotOn:%u, MotOff:%u\n",
			g_param_tbl.rSrtHdUnld>>4,
			g_param_tbl.rDmaHdLd>>1,
			g_param_tbl.rSrtHdUnld&0x0f,
			g_param_tbl.bHdSettle,
			g_param_tbl.bGapLen,
			g_param_tbl.bMotorOn,
			g_param_tbl.bMotorOff
			);

	return SUCCESSFUL;
}

int cmd_seek(const char *i_args)
{
	unsigned trk, hd=0, cur_trk, media_st;
	int result;

	if((!i_args) || (sscanf(i_args, "%u", &trk)!=1)) {
		return INVALID_ARGUMENTS;
	}
	if(trk > g_max_trk) {
		printf("max track number: %u\n", g_max_trk);
		return INVALID_ARGUMENTS;
	}
	if(hd>1) {
		hd = 1;
	}

	media_st = peekb(BIOS_DATA_SEG, DISKETTE_DRIVE_MEDIA_STATE+g_drive);
	if(media_st & 0x20) {
		/* double stepping required */
		trk *= 2;
	}

	CHECK( floppy_write_FIFO(SEEK) );
	CHECK( floppy_write_FIFO((g_drive&0x03) | (hd<<2)) );
	CHECK( floppy_write_FIFO(trk) );
	/* Seek command has no result phase.
	 * To verify successful completion of the command,
	 * it is necessary to check the head position immediately
	 * after completion of the command, using the check
	 * interrupt status command.
	 */
	CHECK( floppy_chk_int() );
	wait_ms(g_param_tbl.bHdSettle);

	pokeb(BIOS_DATA_SEG, DISKETTE_DRIVE0_CURRENT_TRACK+g_drive, trk);

	return SUCCESSFUL;
}

int cmd_recalibrate(const char *i_args)
{
	uint8_t drvbit = (1<<g_drive);
	uint8_t recal, result;

	CHECK( floppy_write_FIFO(RECALIBRATE) );
	CHECK( floppy_write_FIFO(g_drive) );

	/* set the recalibrate bit */
	recal = peekb(BIOS_DATA_SEG, DISKETTE_RECALIBRATE_STATUS);
	pokeb(BIOS_DATA_SEG, DISKETTE_RECALIBRATE_STATUS, recal|drvbit);

	/* Recalibrate command has no result phase. */
	CHECK( floppy_chk_int() );

	wait_ms(21);
	pokeb(BIOS_DATA_SEG, DISKETTE_DRIVE0_CURRENT_TRACK+g_drive, 0);
	wait_ms(g_param_tbl.bHdSettle);

	return result;
}

int cmd_read(const char * i_args)
{
	/* This function only works on high density floppies
	 */
	uint8_t *data, cyl, result;
	uint16_t sectors,sect_size;
	long t0, t1, ms;
	size_t data_len;
	DMA_block dma;
	uint8_t cmd_result[10];

	if(g_param_tbl.bSectSize > 7) {
		printf("invalid sector size = %d\n", g_param_tbl.bSectSize);
		return INVALID_ARGUMENTS;
	}

	if((!i_args) || (sscanf(i_args, "%u", &sectors)!=1)) {
		sectors = g_param_tbl.bLastTrack * 2;
	}

	sect_size = 128 << g_param_tbl.bSectSize;
	data_len = sectors * sect_size;
	/*
	printf("allocating %d*%d*2=%u bytes\n",
			sect_size,
			g_param_tbl.bLastTrack,
			data_len);
	*/
	data = (uint8_t*)malloc(data_len);
	if(data == NULL) {
		return OUT_OF_MEMORY;
	}

	DMA_block_load(&dma, data, data_len);
	floppy_motor_on(g_drive, 128);
	cyl = peekb(BIOS_DATA_SEG, DISKETTE_DRIVE0_CURRENT_TRACK+g_drive);

	printf("reading %u sectors on cylinder %u\n", sectors, cyl);

	t0 = biostime(0,0);
	result = floppy_rw_track(g_drive, &g_param_tbl, cyl, &dma, 0, cmd_result);
	t1 = biostime(0,0);

	free(data);

	printf("ST0: 0x%02X\n", cmd_result[0]);
	printf("ST1: 0x%02X\n", cmd_result[1]);
	printf("ST2: 0x%02X\n", cmd_result[2]);
	printf("C: %u\n", cmd_result[3]);
	printf("H: %u\n", cmd_result[4]);
	printf("R: %u\n", cmd_result[5]);
	printf("N: %u\n", cmd_result[6]);
	printf("---\n");

	if(result == SUCCESSFUL) {
		/* BIOS time uses IRQ0 which fires every 18.2 ms */
		ms = (1000.0/18.2)*(t1 - t0);
		printf("%u bytes read in %d ms, ", data_len, ms);
		printf("speed = %u KB/s\n", (data_len / ms));
	} else {
		if(cmd_result[1] & 0x80) {
			printf("EN: End of Cylinder\n");
		}
		if(cmd_result[1] & 0x20) {
			printf("DE: Data Error\n");
		}
		if(cmd_result[1] & 0x10) {
			printf("OR: Overrun/Underrun\n");
		}
		if(cmd_result[1] & 0x04) {
			printf("ND: No Data\n");
		}
		if((cmd_result[1]|cmd_result[2]) & 0x01) {
			printf("MA: Missing Address Mark\n");
		}
		if(cmd_result[2] & 0x40) {
			printf("CM: Control Mark\n");
		}
		if(cmd_result[2] & 0x20) {
			printf("DD: Data Error in Data Field\n");
		}
		if(cmd_result[2] & 0x10) {
			printf("WC: Wrong Cylinder\n");
		}
		if(cmd_result[2] & 0x02) {
			printf("BC: Bad Cylinder\n");
		}
		if(cmd_result[6] != g_param_tbl.bSectSize) {
			printf("wrong sector size %u\n", cmd_result[6]);
		}
	}
	return result;
}

int cmd_rate(const char *i_args)
{
	int rate;
	int result;

	if((!i_args) || (sscanf(i_args, "%u", &rate)!=1)) {
		return INVALID_ARGUMENTS;
	}
	if(rate > 2) {
		return INVALID_ARGUMENTS;
	}

	outportb(CONFIGURATION_CONTROL_REGISTER, rate);

	return SUCCESSFUL;
}

int cmd_dump(const char *i_args)
{
	uint8_t cmdres[10], reg;
	int result;

	CHECK( floppy_write_FIFO(DUMPREG) );
	CHECK( floppy_read_result(cmdres) );

	printf("PCN-Drive 0 .............. = 0x%02X\n", cmdres[0]);
	printf("PCN-Drive 1 .............. = 0x%02X\n", cmdres[1]);
	printf("PCN-Drive 2 .............. = 0x%02X\n", cmdres[2]);
	printf("PCN-Drive 3 .............. = 0x%02X\n", cmdres[3]);
	printf("SRT HUT .................. = 0x%02X\n", cmdres[4]);
	printf("HLT ND ................... = 0x%02X\n", cmdres[5]);
	printf("SC/EOT ................... = 0x%02X\n", cmdres[6]);
	printf("LOCK D3 D2 D1 D0 GAP WGATE = 0x%02X\n", cmdres[7]);
	printf("EIS EFIFO POLL FIFOTHR ... = 0x%02X\n", cmdres[8]);
	printf("PRETRK ................... = 0x%02X\n", cmdres[9]);
	printf("---------------------------------\n");
	reg = inportb(STATUS_REGISTER_A);
	printf("SRA=0x%02X, ", reg);
	reg = inportb(STATUS_REGISTER_B);
	printf("SRB=0x%02X\n", reg);
	reg = inportb(DIGITAL_OUTPUT_REGISTER);
	printf("DOR=0x%02X, ", reg);
	reg = inportb(MAIN_STATUS_REGISTER);
	printf("MSR=0x%02X\n", reg);
	reg = inportb(DIGITAL_INPUT_REGISTER);
	printf("DIR=0x%02X : ", reg);
	if(reg & 0x80) { printf("NDSKCHG "); }
	if(reg & 0x08) { printf("NDMAGATE "); }
	if(reg & 0x04) { printf("NOPREC "); }
	printf("DRATE=%02X ", reg & 0x03);
	switch(reg & 0x03) {
		case 0: printf("(500 Kbps)"); break;
		case 1: printf("(300 Kbps)"); break;
		case 2: printf("(250 Kbps)"); break;
		case 3: printf("(1 Mbps)"); break;
	}
	printf("\n");

	return SUCCESSFUL;
}

int cmd_specify(const char *i_args)
{
	unsigned srt, hut, hlt;
	int result;

	if((!i_args) || (sscanf(i_args, "%u,%u,%u", &srt,&hut,&hlt)!=3)) {
		return INVALID_ARGUMENTS;
	}

	if(srt>15 || hut>15 || hlt>127) {
		return INVALID_ARGUMENTS;
	}

	CHECK( floppy_write_FIFO(SPECIFY) );
	CHECK( floppy_write_FIFO(srt<<4|hut) );
	CHECK( floppy_write_FIFO(hlt<<1) );

	return SUCCESSFUL;
}

int cmd_bench(const char *i_args)
{
	uint8_t *data, cyl;
	long t0, t1, ms, benches[5];
	int result, sectors, sect_size, data_len, i;
	int max_sectors = g_param_tbl.bLastTrack*2;

	if((!i_args) || (sscanf(i_args, "%u", &sectors)!=1)) {
		sectors = max_sectors;
	}
	if(sectors > max_sectors) {
		sectors = max_sectors;
	}
	sect_size = 128 << g_param_tbl.bSectSize;
	data_len = sectors * sect_size;
	data = (uint8_t*)malloc(data_len);
	if(data == NULL) {
		return OUT_OF_MEMORY;
	}

	printf("resetting the disk system\n");
	biosdisk(0,g_drive,0,0,1,0,NULL);

	cyl = peekb(BIOS_DATA_SEG, DISKETTE_DRIVE0_CURRENT_TRACK+g_drive);
	printf("reading %d sectors on cylinder %u\n", sectors, cyl);

	for(i=0; i<5; i++) {
		t0 = biostime(0,0);
		result = biosdisk(2,g_drive,0,cyl,1,sectors,data);
		t1 = biostime(0,0);
		if(result != SUCCESSFUL) {
			switch(result) {
				case 0x01: printf("Bad command\n"); break;
				case 0x02: printf("Address mark not found\n"); break;
				case 0x04: printf("Record not found\n"); break;
				case 0x05: printf("Reset failed\n"); break;
				case 0x07: printf("Drive parameter activity failed\n"); break;
				case 0x09: printf("Attempt to DMA across 64K boundary\n"); break;
				case 0x0B: printf("Bad track flag detected\n"); break;
				case 0x10: printf("Bad ECC on disk readv"); break;
				case 0x11: printf("ECC corrected data error\n"); break;
				case 0x20: printf("Controller has failed\n"); break;
				case 0x40: printf("Seek operation failed\n"); break;
				case 0x80: printf("Attachment failed to respond\n"); break;
				case 0xBB: printf("Undefined error occurred\n"); break;
				case 0xFF: printf("Sense operation failed\n"); break;
				default: printf("Unknown error\n"); break;
			}
		} else {
			/* BIOS time uses IRQ0 which fires every 18.2 ms */
			ms = (1000.0/18.2)*(t1 - t0);
			printf("%d sectors read in %d ms, ", sectors, ms);
			printf("speed = %u KB/s\n", (data_len / ms));
		}
	}

	free(data);

	if(result != SUCCESSFUL) {
		return ABNORMAL_TERMINATION;
	}

	return SUCCESSFUL;
}

char *safe_gets(char *i_str, int i_maxlen)
{
	char *retval;
	int len;

	retval = fgets(i_str, i_maxlen, stdin);
	if(retval) {
		len = strlen(i_str);
		/* remove '\n' */
		if((len > 0) && (i_str[len-1] == '\n')) {
			i_str[len-1] = '\0';
		}
		return i_str;
	} else {
		return NULL;
	}
}


#define COMMAND_SIZE 15
#define COMMAND_LINE_SIZE 80

char linebuf[COMMAND_LINE_SIZE];
char cmd[COMMAND_SIZE+1];

int main(int argc, char **argv)
{
	int result = SUCCESSFUL;
	char *cmdline = NULL, *args, *cmdtok;
	int  cmdlen, i, sl, linelen = 0;
	command_t *p;
	uint8_t version;

	printf("FDDTEST - Direct access to floppy disk drives.\n");
	printf("This program has been created to aid the development of IBMulator.\n");

	if(WARNING) {
	printf("------------------------------- WARNING ---------------------------------------\n");
	printf("To correctly use this program you must have a deep understanding "
	       "of the inner workings of floppy disk drives and controllers.\n");
	printf("The commands are very low level and need to be executed in the "
	       "right order and with the right arguments, otherwise the controller "
	       "could hang or, worse, the drive could be damaged.\n");
	printf("If you are aware of the risks remove this warning.\n");
	exit(1);
	}

	if(floppy_version(&version)) {
		printf("ERROR: floppy disk controller not present or malfunctioning\n");
		exit(DRIVE_NOT_READY);
	}
	if(version == 0x90) {
		printf("Intel 82077AA or compatible controller detected\n");
	} else {
		printf("WARNING: 8272A/765A controller found. "
		"This program is untested on older controllers and some commands don't even work.\n");
	}

	sprintf(linebuf,"0");
	if(cmd_drive(linebuf)) {
		exit(DRIVE_NOT_READY);
	}

	if(argc>1) {
		linebuf[0] = '\0';
		for(i=1; i<argc; ++i) {
			sl = strlen(argv[i]);
			if(linelen+sl+1 < COMMAND_LINE_SIZE) {
				strcat(linebuf+linelen, argv[i]);
				strcat(linebuf+linelen+1, " ");
				linelen += sl+1;
			} else {
				printf("command string too big\n");
				exit(INVALID_ARGUMENTS);
			}
		}
		cmdline = linebuf;
		printf("executing: %s\n", cmdline);
	} else {
		printf("type help for usage info.\n");
	}

	while(1) {
		if(cmdline == NULL) {
			printf("> ");
			cmdline = safe_gets(linebuf, COMMAND_LINE_SIZE);
			if(cmdline == NULL) {
				printf("abnormal termination!\n");
				exit(ABNORMAL_TERMINATION);
			}
		}
		cmdtok = strtok(cmdline, ";");
		while(cmdtok) {
			if(sscanf(cmdtok, "%15[a-zA-Z]", cmd) != 1) {
				break;
			}
			for(p = g_commands_table; p->cmd != NULL; ++p) {
				if((strcmp(p->cmd, cmd)==0) || (strcmp(p->shcmd, cmd)==0)) {
					cmdlen = strlen(cmd);
					if(strlen(cmdtok) == cmdlen) {
						cmdtok = NULL;
						printf("%s\n", p->cmd);
					} else {
						cmdtok += cmdlen;
						printf("%s %s\n", p->cmd, cmdtok);
					}
					result = (*p->func)(cmdtok);
					if(result != SUCCESSFUL) {
						printf("error: %s (%d)\n",
								g_error_str[result], result);
						if(argc > 1) {
							exit(result);
						}
					}
					break;
				}
			}
			if((p->cmd == NULL)) {
				printf("%s\n", g_error_str[INVALID_COMMAND]);
				if(argc > 1) {
					exit(INVALID_COMMAND);
				}
			}
			cmdtok = strtok(NULL, ";");
		}
		cmdline = NULL;
	}

	return 0;
}
