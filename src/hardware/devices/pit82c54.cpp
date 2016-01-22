/*
 * Copyright (C) 2001-2013  The Bochs Project
 * Copyright (C) 2015, 2016  Marco Bortolin
 *
 * This file is part of IBMulator.
 *
 * IBMulator is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * IBMulator is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with IBMulator.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Emulator of an Intel 8254/82C54 Programmable Interval Timer.
 * Greg Alexander <yakovlev@usa.com>
 *
 * Comment of the original author:
 *
 * Things I am unclear on (greg):
 * 1.)What happens if both the status and count registers are latched,
 *  but the first of the two count registers has already been read?
 *  I.E.:
 *   latch count 0 (16-bit)
 *   Read count 0 (read LSByte)
 *   READ_BACK status of count 0
 *   Read count 0 - do you get MSByte or status?
 *  This will be flagged as an error.
 * 2.)What happens when we latch the output in the middle of a 2-part
 *  unlatched read?
 * 3.)I assumed that programming a counter removes a latched status.
 * 4.)I implemented the 8254 description of mode 0, not the 82C54 one.
 * 5.)clock() calls represent a rising clock edge followed by a falling
 *  clock edge.
 * 6.)What happens when we trigger mode 1 in the middle of a 2-part
 *  write?
 */

#include "ibmulator.h"
#include "machine.h"
#include "pit82c54.h"


void PIT_82C54::init()
{
	for(int i=0; i<3; i++) {
		counter[i].name = i;
		counter[i].out_handler = nullptr;
	}
}

void PIT_82C54::reset(unsigned)
{
	for(int i=0; i<3; i++) {
		counter[i].reset();
	}
}

void PIT_82C54::PIT_counter::reset()
{
	PDEBUGF(LOG_V2, LOG_PIT, "PIT: setting read_state %u to LSB\n", name);

	//Chip IOs;
	GATE   = true;
	OUTpin = true;

	//Architected state;
	count        = 0;
	outlatch     = 0;
	inlatch      = 0;
	status_latch = 0;

	//Status Register data;
	rw_mode    = 1;
	mode       = 4;
	bcd_mode   = false;
	null_count = false;

	//Latch status data;
	count_LSB_latched = false;
	count_MSB_latched = false;
	status_latched    = false;

	//Miscelaneous State;
	count_binary     = 0;
	triggerGATE      = false;
	write_state      = LSByte;
	read_state       = LSByte;
	count_written    = true;
	first_pass       = false;
	state_bit_1      = false;
	state_bit_2      = false;
	next_change_time = 0;

	seen_problems = 0;
}

void PIT_82C54::PIT_counter::dbg_print()
{
	PDEBUGF(LOG_V2, LOG_PIT, "count: %d\n", count);
	PDEBUGF(LOG_V2, LOG_PIT, "count_binary: 0x%04x\n", count_binary);
	PDEBUGF(LOG_V2, LOG_PIT, "counter GATE: %x\n", GATE);
	PDEBUGF(LOG_V2, LOG_PIT, "counter OUT: %x\n", OUTpin);
	PDEBUGF(LOG_V2, LOG_PIT, "next_change_time: %d\n", next_change_time);
}

void PIT_82C54::print_cnum(uint8_t cnum)
{
	ASSERT(cnum<=MAX_COUNTER);
	counter[cnum].dbg_print();
}

void PIT_82C54::PIT_counter::latch()
{
	if(count_LSB_latched || count_MSB_latched) {
		//Do nothing because previous latch has not been read.;
	} else {
		switch(read_state) {
			case MSByte:
				outlatch = count & 0xFFFF;
				count_MSB_latched = true;
				break;
			case LSByte:
				outlatch = count & 0xFFFF;
				count_LSB_latched = true;
				break;
			case LSByte_multiple:
				outlatch = count & 0xFFFF;
				count_LSB_latched = true;
				count_MSB_latched = true;
				break;
			case MSByte_multiple:
				if(!(seen_problems & UNL_2P_READ)) {
					// seen_problems|=UNL_2P_READ;
					PDEBUGF(LOG_V2, LOG_PIT, "Unknown behavior when latching during 2-part read.\n");
					PDEBUGF(LOG_V2, LOG_PIT, "  This message will not be repeated.\n");
				}
				//I guess latching and resetting to LSB first makes sense;
				PDEBUGF(LOG_V2, LOG_PIT, "Setting read_state to LSB_mult\n");
				read_state = LSByte_multiple;
				outlatch = count & 0xFFFF;
				count_LSB_latched = true;
				count_MSB_latched = true;
				break;
			default:
				PDEBUGF(LOG_V2, LOG_PIT, "Unknown read mode found during latch command.\n");
				break;
		}
	}
}

void PIT_82C54::PIT_counter::set_OUT(bool value, uint32_t _cycles)
{
	if(OUTpin != value) {
		OUTpin = value;
		if(out_handler != nullptr) {
			out_handler(value, _cycles);
		}
	}
}

void PIT_82C54::PIT_counter::set_count(uint32_t data)
{
	count = data & 0xFFFF;
	set_binary_to_count();
}

void PIT_82C54::PIT_counter::set_count_to_binary()
{
	if(bcd_mode) {
		count=
			(((count_binary/1)%10)<<0) |
			(((count_binary/10)%10)<<4) |
			(((count_binary/100)%10)<<8) |
			(((count_binary/1000)%10)<<12);
	} else {
		count = count_binary;
	}
}

void PIT_82C54::PIT_counter::set_binary_to_count()
{
	if(bcd_mode) {
		count_binary=
				(1*((count>>0)&0xF)) +
				(10*((count>>4)&0xF)) +
				(100*((count>>8)&0xF)) +
				(1000*((count>>12)&0xF));
	} else {
		count_binary = count;
	}
}

bool PIT_82C54::PIT_counter::decrement()
{
	if(count == 0) {
		if(bcd_mode) {
			count = 0x9999;
			count_binary = 9999;
		} else {
			count = 0xFFFF;
			count_binary = 0xFFFF;
		}
		return true;
	}
	count_binary--;
	set_count_to_binary();
	return false;
}

bool PIT_82C54::PIT_counter::decrement_multiple(uint32_t cycles)
{
	bool wraparound = false;
	while(cycles>0) {
		if(cycles <= count_binary) {
			count_binary -= cycles;
			cycles = 0;
			set_count_to_binary();
		} else { //cycles > count_binary
			cycles -= (count_binary+1);
			count_binary = 0;
			set_count_to_binary();
			decrement();
			//the counter has reached zero!
			wraparound = true;
		}
	}
	return wraparound;
}

void PIT_82C54::PIT_counter::clock_multiple(uint32_t cycles)
{
	while(cycles>0) {
		if(next_change_time==0) {
			if(count_written) {
				switch(mode) {
					case 0:
						if(GATE && (write_state!=MSByte_multiple)) {
							decrement_multiple(cycles);
						}
						break;
					case 1:
						decrement_multiple(cycles);
						break;
					case 2:
						if(!first_pass && GATE) {
							decrement_multiple(cycles);
						}
						break;
					case 3:
						if(!first_pass && GATE) {
							//i think
							//the program can't get here because next_change_time is 0
							//only when (count_written==0) || (count_written==1 && GATE==0)
							decrement_multiple(2*cycles);
						}
						break;
					case 4:
						if(GATE) {
							decrement_multiple(cycles);
						}
						break;
					case 5:
						decrement_multiple(cycles);
						break;
					default:
						break;
				}
			}
			cycles = 0;
		} else { //next_change_time!=0
			switch(mode) {
				case 0:
				case 1:
				case 2:
				case 4:
				case 5:
					if(next_change_time > cycles) {
						decrement_multiple(cycles);
						next_change_time -= cycles;
						cycles = 0;
					} else {
						decrement_multiple((next_change_time-1));
						uint32_t c = cycles;
						cycles -= next_change_time;
						clock(c);

					}
					break;
				case 3:
					if(next_change_time > (cycles)) {
						decrement_multiple(cycles*2);
						next_change_time -= cycles;
						cycles = 0;
					} else {
						decrement_multiple((next_change_time-1)*2);
						uint32_t c = cycles;
						cycles -= next_change_time;
						clock(c);
					}
					break;
				default:
					cycles = 0;
					break;
			}
		}
	}
}

void PIT_82C54::PIT_counter::clock(uint32_t _cycles)
{
	switch(mode) {
		case 0:
			if(count_written) {
				if(null_count) {
					set_count(inlatch);
					if(GATE) {
						if(count_binary == 0) {
							next_change_time = 1;
						} else {
							next_change_time = count_binary & 0xFFFF;
						}
					} else {
						next_change_time = 0;
					}
					null_count=0;
				} else {
					if(GATE && (write_state!=MSByte_multiple)) {
						decrement();
						if(!OUTpin) {
							next_change_time = count_binary & 0xFFFF;
							if(!count) {
								set_OUT(true, _cycles);
							}
						} else {
							next_change_time = 0;
						}
					} else {
						next_change_time = 0; //if the clock isn't moving.
					}
				}
			} else {
				next_change_time = 0; //default to 0.
			}
			triggerGATE = false;
			break;
		case 1:
			if(count_written) {
				if(triggerGATE) {
					set_count(inlatch);
					if(count_binary == 0) {
						next_change_time = 1;
					} else {
						next_change_time = count_binary & 0xFFFF;
					}
					null_count=0;
					set_OUT(false, _cycles);
					if(write_state == MSByte_multiple) {
						PDEBUGF(LOG_V1, LOG_PIT,
								"Undefined behavior when loading a half loaded count.\n");
					}
				} else {
					decrement();
					if(!OUTpin) {
						if(count_binary == 0) {
							next_change_time = 1;
						} else {
							next_change_time = count_binary & 0xFFFF;
						}
						if(count==0) {
							set_OUT(true, _cycles);
						}
					} else {
						next_change_time = 0;
					}
				}
			} else {
				next_change_time = 0; //default to 0.
			}
			triggerGATE = false;
			break;
		case 2:
			if(count_written) {
				if(triggerGATE || first_pass) {
					set_count(inlatch);
					next_change_time = (count_binary-1) & 0xFFFF;
					null_count=0;
					if(inlatch == 1) {
						PDEBUGF(LOG_V1, LOG_PIT,
								"ERROR: count of 1 is invalid in pit mode 2.\n");
					}
					if(!OUTpin) {
						set_OUT(true, _cycles);
					}
					if(write_state == MSByte_multiple) {
						PDEBUGF(LOG_V1, LOG_PIT,
								"Undefined behavior when loading a half loaded count.\n");
					}
					first_pass = false;
				} else {
					if(GATE) {
						decrement();
						next_change_time = (count_binary-1) & 0xFFFF;
						if(count==1) {
							next_change_time = 1;
							set_OUT(false, _cycles);
							first_pass = true;
						}
					} else {
						next_change_time = 0;
					}
				}
			} else {
				next_change_time = 0;
			}
			triggerGATE = false;
			break;
		case 3:
			if(count_written) {
				if((triggerGATE || first_pass || state_bit_2) && GATE)
				{
					set_count(inlatch & 0xFFFE);
					state_bit_1 = inlatch & 0x1;
					uint32_t real_count = count_binary==0?65536:count_binary;
					if(!OUTpin || !state_bit_1) {
						if(((real_count/2)-1)==0) {
							next_change_time = 1;
						} else {
							//Bochs code: this is plain wrong. if the inlatch is
							//0 (eq to 65536) then count_binary is 0 and
							//next_change_time will be 65535 which is not
							//correct, it should be 32767
							// next_change_time = ((count_binary/2)-1) & 0xFFFF;
							next_change_time = ((real_count/2)-1) & 0xFFFF;
						}
					} else {
						if((real_count/2)==0) {
							next_change_time = 1;
						} else {
							next_change_time = (real_count/2) & 0xFFFF;
						}
					}
					null_count = 0;
					if(inlatch == 1) {
						PDEBUGF(LOG_V2, LOG_PIT, "Count of 1 is invalid in pit mode 3.\n");
					}
					if(!OUTpin) {
						set_OUT(true, _cycles);
					} else if(OUTpin && !first_pass) {
						set_OUT(false, _cycles);
					}
					if(write_state == MSByte_multiple) {
						PDEBUGF(LOG_V0, LOG_PIT,
								"Undefined behavior when loading a half loaded count.\n");
					}
					state_bit_2 = false;
					first_pass = false;
				} else {
					if(GATE) {
						decrement();
						decrement();
						//see above
						uint32_t real_count = count_binary==0?65536:count_binary;
						if(!OUTpin || !state_bit_1) {
							next_change_time = ((real_count/2)-1) & 0xFFFF;
						} else {
							next_change_time = (real_count/2) & 0xFFFF;
						}
						if(count == 0) {
							state_bit_2 = true;
							next_change_time = 1;
						}
						if((count == 2) && (!OUTpin || !state_bit_1)) {
							state_bit_2 = true;
							next_change_time = 1;
						}
					} else {
						next_change_time = 0;
					}
				}
			} else {
				next_change_time = 0;
			}
			triggerGATE = false;
			break;
		case 4:
			if(count_written) {
				if(!OUTpin) {
					set_OUT(true,_cycles);
				}
				if(null_count) {
					set_count(inlatch);
					if(GATE) {
						if(count_binary == 0) {
							next_change_time = 1;
						} else {
							next_change_time = count_binary & 0xFFFF;
						}
					} else {
						next_change_time = 0;
					}
					null_count=0;
					if(write_state == MSByte_multiple) {
						PDEBUGF(LOG_V2, LOG_PIT,
								"Undefined behavior when loading a half loaded count.\n");
					}
					first_pass = true;
				} else {
					if(GATE) {
						decrement();
						if(first_pass) {
							next_change_time = count_binary & 0xFFFF;
							if(!count) {
								set_OUT(false,_cycles);
								next_change_time = 1;
								first_pass = false;
							}
						} else {
							next_change_time = 0;
						}
					} else {
						next_change_time = 0;
					}
				}
			} else {
				next_change_time = 0;
			}
			triggerGATE = false;
			break;
		case 5:
			if(count_written) {
				if(!OUTpin) {
					set_OUT(true,_cycles);
				}
				if(triggerGATE) {
					set_count(inlatch);
					if(count_binary == 0) {
						next_change_time = 1;
					} else {
						next_change_time = count_binary & 0xFFFF;
					}
					null_count=0;
					if(write_state == MSByte_multiple) {
						PDEBUGF(LOG_V2, LOG_PIT,
								"Undefined behavior when loading a half loaded count.\n");
					}
					first_pass = true;
				} else {
					decrement();
					if(first_pass) {
						next_change_time = count_binary & 0xFFFF;
						if(!count) {
							set_OUT(false,_cycles);
							next_change_time = 1;
							first_pass = false;
						}
					} else {
						next_change_time = 0;
					}
				}
			} else {
				next_change_time = 0;
			}
			triggerGATE = false;
			break;
		default:
			PDEBUGF(LOG_V2, LOG_PIT, "Mode not implemented.\n");
			next_change_time = 0;
			triggerGATE = false;
			break;
	}

}

void PIT_82C54::clock_all(uint32_t cycles)
{
	//PDEBUGF(LOG_V2, LOG_PIT, "clock_all: cycles=%d\n",cycles);
	counter[0].clock_multiple(cycles);
	counter[1].clock_multiple(cycles);
	counter[2].clock_multiple(cycles);
}

uint8_t PIT_82C54::read(uint8_t address)
{
    if(address>MAX_ADDRESS) {
    	PDEBUGF(LOG_V2, LOG_PIT, "Counter address incorrect in data read\n");
    	return 0;
    }

    if(address==CONTROL_ADDRESS) {
    	PDEBUGF(LOG_V2, LOG_PIT, "PIT Read: Control Word Register\n");
		//Read from control word register;
		/* This might be okay.  If so, 0 seems the most logical
		 *  return value from looking at the docs.
		 */
		PDEBUGF(LOG_V2, LOG_PIT, "Read from control word register not defined\n");
		return 0;
    }

    //Read from a counter;
	PDEBUGF(LOG_V2, LOG_PIT, "PIT Read: Counter %d.",address);
	return counter[address].read();
}

uint8_t PIT_82C54::PIT_counter::read()
{
	if(status_latched) {
		//Latched Status Read;
		if(count_MSB_latched && (read_state==MSByte_multiple)) {
			PDEBUGF(LOG_V2, LOG_PIT, "Undefined output when status latched and count half read\n");
			return 0;
		} else {
			status_latched=0;
			return status_latch;
		}
	}

	//Latched Count Read;
	if(count_LSB_latched) {
		//Read Least Significant Byte;
		if(read_state==LSByte_multiple) {
			PDEBUGF(LOG_V2, LOG_PIT, "Setting read_state to MSB_mult\n");
			read_state=MSByte_multiple;
		}
		count_LSB_latched = false;
		return (outlatch & 0xFF);
	} else if(count_MSB_latched) {
		//Read Most Significant Byte;
		if(read_state==MSByte_multiple) {
			PDEBUGF(LOG_V2, LOG_PIT, "Setting read_state to LSB_mult\n");
			read_state=LSByte_multiple;
		}
		count_MSB_latched = false;
		return ((outlatch>>8) & 0xFF);
	} else {
		//Unlatched Count Read;
		if(!(read_state & 0x1)) {
			//Read Least Significant Byte;
			if(read_state==LSByte_multiple) {
				read_state=MSByte_multiple;
				PDEBUGF(LOG_V2, LOG_PIT, "Setting read_state to MSB_mult\n");
			}
			return (count & 0xFF);
		} else {
			//Read Most Significant Byte;
			if(read_state==MSByte_multiple) {
				PDEBUGF(LOG_V2, LOG_PIT, "Setting read_state to LSB_mult\n");
				read_state=LSByte_multiple;
			}
			return ((count>>8) & 0xFF);
		}
	}

    //Should only get here on errors;
    return 0;
}

void PIT_82C54::write(uint8_t address, uint8_t data)
{
	if(address > MAX_ADDRESS) {
		PDEBUGF(LOG_V2, LOG_PIT, "Counter address incorrect in data write\n");
		return;
	}

	if(address == CONTROL_ADDRESS) {
		controlword = data;
		PDEBUGF(LOG_V2, LOG_PIT, "Control Word Write\n");
		uint8_t SC = (controlword>>6) & 0x3;
		uint8_t RW = (controlword>>4) & 0x3;
		uint8_t M  = (controlword>>1) & 0x7;
		uint8_t BCD = controlword & 0x1;
		if(SC == 3) {
			//READ_BACK command;
			int i;
			PDEBUGF(LOG_V2, LOG_PIT, "READ_BACK command\n");
			for(i=0; i<=MAX_COUNTER; i++) {
				if((M>>i) & 0x1) {
					PIT_counter *ctr = &counter[i];
					//If we are using this counter;
					if(!((controlword>>5) & 1)) {
						//Latch Count;
						ctr->latch();
					}
					if(!((controlword>>4) & 1)) {
						//Latch Status;
						if(ctr->status_latched) {
							//Do nothing because latched status has not been read.;
						} else {
							ctr->status_latch =
								((ctr->OUTpin & 0x1) << 7) |
								((ctr->null_count & 0x1) << 6) |
								((ctr->rw_mode & 0x3) << 4) |
								((ctr->mode & 0x7) << 1) |
								(ctr->bcd_mode&0x1);
							ctr->status_latched = true;
						}
					}
				}
			}
		} else {
			PIT_counter *ctr = &counter[SC];
			if(!RW) {
				//Counter Latch command;
				PDEBUGF(LOG_V2, LOG_PIT, "Counter Latch command.  SC=%d\n",SC);
				ctr->latch();
			} else {
				//Counter Program Command;
				PDEBUGF(LOG_V2, LOG_PIT, "Counter Program command.  SC=%d, RW=%d, M=%d, BCD=%d\n",SC,RW,M,BCD);
				ctr->null_count = 1;
				ctr->count_LSB_latched = false;
				ctr->count_MSB_latched = false;
				ctr->status_latched = false;
				ctr->inlatch = 0;
				ctr->count_written = false;
				ctr->first_pass = true;
				ctr->rw_mode = RW;
				ctr->bcd_mode = (BCD > 0);
				ctr->mode = M;
				switch(RW) {
					case 0x1:
						PDEBUGF(LOG_V2, LOG_PIT, "Setting read_state to LSB\n");
						ctr->read_state = LSByte;
						ctr->write_state = LSByte;
						break;
					case 0x2:
						PDEBUGF(LOG_V2, LOG_PIT, "Setting read_state to MSB\n");
						ctr->read_state = MSByte;
						ctr->write_state = MSByte;
						break;
					case 0x3:
						PDEBUGF(LOG_V2, LOG_PIT, "Setting read_state to LSB_mult\n");
						ctr->read_state = LSByte_multiple;
						ctr->write_state = LSByte_multiple;
						break;
					default:
						PDEBUGF(LOG_V2, LOG_PIT, "RW field invalid in control word write\n");
						break;
				}
				//All modes except mode 0 have initial output of 1.;
				if(M) {
					ctr->set_OUT(true,0);
				} else {
					ctr->set_OUT(false,0);
				}
				ctr->next_change_time = 0;
			}
		}
		return;
	}

	counter[address].write(data);
}

void PIT_82C54::PIT_counter::write(uint8_t data)
{
	//Write to counter initial value.

	PDEBUGF(LOG_V2, LOG_PIT, "Write Initial Count: counter=%d, count=%d\n", name, data);
	switch(write_state) {
		case LSByte_multiple:
			inlatch = data;
			write_state = MSByte_multiple;
			break;
		case LSByte:
			inlatch = data;
			count_written = true;
			break;
		case MSByte_multiple:
			write_state = LSByte_multiple;
			inlatch |= (data << 8);
			count_written = true;
			break;
		case MSByte:
			inlatch = (data << 8);
			count_written = true;
			break;
		default:
			PDEBUGF(LOG_V2, LOG_PIT, "write counter %d in invalid write state\n", name);
			break;
	}
	if(count_written && write_state != MSByte_multiple) {
		null_count = true;
		/*
		MODE 1,2,3,5:
		The current counting sequence is not affected by a new count being
		written to the counter. If the counter receives a trigger after a new
		count is written, and before the end of the current count
		cycle/half-cycle, the new count is loaded on the next CLK pulse, and
		counting continues from the new count. If the trigger is not received
		by the counter, the new count is loaded following the current
		cycle/half-cycle.
		The original Bochs code doesn't take this into account.
		*/
		if(mode==0 || mode==4) {
			set_count(inlatch);
		}
	}
	switch(mode) {
		case 0:
			/*
			If a new count is written to a counter while counting, it is loaded on
			the next CLK pulse, and counting continues from the new count.
			If a 2-byte count is written to the counter, the following occurs:
			1. The first byte written to the counter disables the counting. OUT
			goes low immediately and there is no delay for the CLK pulse.
			2. When the second byte is written to the counter, the new count is
			loaded on the next CLK pulse. OUT goes high when the counter
			reaches 0.
			*/
			if(write_state != LSByte_multiple) {
				set_OUT(false,0);
			}
			next_change_time = 1;
			break;
		case 1:
			if(triggerGATE) { //for initial writes, if already saw trigger.
				next_change_time = 1;
			} //Otherwise, no change.
			break;
		case 6:
		case 2:
			next_change_time = 1; //FIXME: this could be loosened.
			break;
		case 7:
		case 3:
			next_change_time = 1; //FIXME: this could be loosened.
			break;
		case 4:
			next_change_time = 1;
			break;
		case 5:
			if(triggerGATE) { //for initial writes, if already saw trigger.
				next_change_time = 1;
			} //Otherwise, no change.
		break;
	}
}

void PIT_82C54::PIT_counter::set_GATE(bool value)
{
	if(!((GATE&&value) || (!(GATE||value)))) {
		PDEBUGF(LOG_V2, LOG_PIT, "Changing GATE %d to: %d\n",name,value);
		GATE = value;
		if(GATE) {
			triggerGATE = true;
		}
		switch(mode) {
			case 0:
				if(value && count_written) {
					if(null_count) {
						next_change_time = 1;
					} else {
						if((!OUTpin) && (write_state!=MSByte_multiple))
						{
							if(count_binary == 0) {
								next_change_time = 1;
							} else {
								next_change_time = count_binary & 0xFFFF;
							}
						} else {
							next_change_time = 0;
						}
					}
				} else {
					if(null_count) {
						next_change_time = 1;
					} else {
						next_change_time = 0;
					}
				}
				break;
			case 1:
				if(value && count_written) { //only triggers cause a change.
					next_change_time = 1;
				}
			break;
			case 2:
				if(!value) {
					set_OUT(true, 0);
					next_change_time = 0;
				} else {
					if(count_written) {
						next_change_time = 1;
					} else {
						next_change_time = 0;
					}
				}
				break;
			case 3:
				if(!value) {
					set_OUT(true, 0);
					first_pass = true;
					next_change_time = 0;
				} else {
					if(count_written) {
						next_change_time = 1;
					} else {
						next_change_time = 0;
					}
				}
				break;
			case 4:
				if(!OUTpin || null_count) {
					next_change_time = 1;
				} else {
					if(value && count_written) {
						if(first_pass) {
							if(count_binary == 0) {
								next_change_time = 1;
							} else {
								next_change_time = count_binary & 0xFFFF;
							}
						} else {
							next_change_time = 0;
						}
					} else {
						next_change_time = 0;
					}
				}
				break;
			case 5:
				if(value && count_written) { //only triggers cause a change.
					next_change_time = 1;
				}
				break;
			default:
				break;
		}
	}
}

uint32_t PIT_82C54::get_next_event_time()
{
	uint32_t time0 = counter[0].next_change_time;
	uint32_t time1 = counter[1].next_change_time;
	uint32_t time2 = counter[2].next_change_time;

	uint32_t out = time0;
	if(PIT_CNT1_AUTO_UPDATE && time1 && (time1<out)) {
		out = time1;
		//PDEBUGF(LOG_V2, LOG_PIT, "next counter=1 %u\n",time1);
	}
	if(time2 && (time2<out)) {
		out = time2;
		//PDEBUGF(LOG_V2, LOG_PIT, "next counter=2 %u\n",time2);
	}
	if(out == time0) {
		//PDEBUGF(LOG_V2, LOG_PIT, "next counter=0 %u\n",time0);
	}
	return out;
}


