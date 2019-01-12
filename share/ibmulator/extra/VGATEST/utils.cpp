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

#include <conio.h>
#include <string.h>

#include "common.h"
#include "utils.h"

bool isVga()
{
    // if function 1axxH is supported then we have a vga
    union REGS rg;
    memset(&rg, 0, sizeof(rg));
    rg.w.ax = 0x1a00;
    int386(0x10, &rg, &rg);
    return (rg.h.al == 0x1a);
}

uint8_t far * getFont()
{
    union REGPACK rg;
    memset(&rg, 0, sizeof(rg));
    rg.w.ax = 0x1130;
    rg.h.bh = 0x03;
    intr(0x10, &rg);
    uint32_t seg = rg.w.es;
    uint32_t off = rg.w.bp;

    return (uint8_t far *)MK_FP(seg, off);
}

uint8_t getBIOSMode()
{
    union REGS rg;
    memset(&rg, 0, sizeof(rg));
    rg.h.ah = 0x0f;
    int386(0x10, &rg, &rg);

    return rg.h.al;
}

void setBIOSMode(uint8_t mode)
{
    union REGS rg;
    memset(&rg, 0, sizeof(rg));
    rg.h.ah = 0x00;
    rg.h.al = mode;
    int386(0x10, &rg, &rg);
}

void setVGARegisters(uint16_t baseAddr, const int16_t *regs)
{
    do {
        outpw(baseAddr, *regs++);
    } while(*regs != -1);
}

void unlockCRTC(int16_t baseAddr)
{
    outp(baseAddr, 0x11);
    int crtc11 = inp(baseAddr+1) & 0x7f; // Protect Registers 0-7 = 0
    outp(baseAddr+1, crtc11);
}
