/*
  VGATEST
  Use at your own risk.

  Copyright (C) 2019  Marco Bortolin

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

#ifndef _UTILS_H_
#define _UTILS_H_

#include "common.h"

#define max(a,b) (((a) > (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))
#define abs(a) (((a)<0) ? -(a) : (a))
#define sign(a) (((a)<0) ? -1 : (a)>0 ? 1 : 0)
#define randr(lo,hi) (rand() % (hi + 1 - lo) + lo)

bool isVga();
uint8_t far * getFont();
uint8_t getBIOSMode();
void setBIOSMode(uint8_t mode);
void setVGARegisters(uint16_t baseAddr, const int16_t *regs);
void unlockCRTC(int16_t baseAddr);


void fillLong(void *addr, uint32_t value, int32_t count);
#pragma aux fillLong = \
    "cld" \
    "rep stosd" \
    parm [edi] [eax] [ecx];


//---------------------------------------------------
//  Inline replacement for srand() and rand().
//  It returns a number between 0 and 0xffff.
//  Original code: "Graphic Gems II"
static long _FRSEED_ = 987654321l;

#define FRAND_MAX (0xffffl)
#define fsrand(seed)  (_FRSEED_ = seed)
#define frand()      ((_FRSEED_ = (((25173l * _FRSEED_) + 13849l))) & 0xffffl)


#endif
