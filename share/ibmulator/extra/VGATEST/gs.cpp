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
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "common.h"
#include "utils.h"
#include "gs.h"

#define VGA_ADDR ((uint8_t *) 0xa0000)

int32_t SIN_ACOS[1024];


GfxScreen::GfxScreen()
{
    m_error = e_none;

    m_origMode = getBIOSMode();
    if(!isVga()) {
        m_error = e_notVgaDisplay;
        return;
    }

    m_fontAddr = NULL;
    m_activeOffset = 0;
    m_maxx = 0;
    m_maxy = 0;
    m_width = 0;
    m_height = 0;
    m_pages = 0;
    m_lineSize = 0;
    m_pageSize = 0;
    m_chained = 0;
    m_modeName = NULL;
    m_colors = 0;
    m_crtc_addr = 0;
    m_isr1_addr = 0;

    m_putPixelFn = NULL;
    m_getPixelFn = NULL;
    m_clearFn = NULL;
    m_drawTextFn = NULL;

    // precompute the sin(arccos(x)) table for circles
    for(int i=0;i<1024;i++) {
        SIN_ACOS[i] = sin(acos((float)i/1024))*0x10000L;
    }
}

GfxScreen::~GfxScreen()
{
}

void GfxScreen::setMode(int16_t mode)
{
    m_error = e_none;
    m_modeName = NULL;

    switch (mode)
    {
    case v_b320x200_0Dh: mode_b320x200_0Dh(); break;
    case v_b640x200_0Eh: mode_b640x200_0Eh(); break;
    case v_b640x350_0Fh: mode_b640x350_0Fh(); break;
    case v_b640x350_10h: mode_b640x350_10h(); break;
    case v_b640x480_12h: mode_b640x480_12h(); break;
    case v_b320x200_13h: mode_b320x200_13h(); break;
    case v_t160x120:   mode_t160x120(); break;
    case v_t296x220:   mode_t296x220(); break;
    case v_t256x256_Q: mode_t256x256_Q(); break;
    case v_t320x200_Y: mode_t320x200_Y(); break;
    case v_t320x240_X: mode_t320x240_X(); break;
    case v_t320x400:   mode_t320x400(); break;
    case v_t360x270:   mode_t360x270(); break;
    case v_t360x360:   mode_t360x360(); break;
    case v_t360x480:   mode_t360x480(); break;
    case v_t400x300:   mode_t400x300(); break;
    default:
        m_error = e_modeNotSupported;
        break;
    }

    if (m_error != e_none) {
        return;
    }

    m_fontAddr = getFont();
    m_activeOffset = VGA_ADDR;
    if(inp(MOR_READ) & 1) {
        m_crtc_addr = 0x3d4;
        m_isr1_addr = 0x3da;
    } else {
        m_crtc_addr = 0x3b4;
        m_isr1_addr = 0x3ba;
    }
    m_maxx = m_width - 1;
    m_maxy = m_height - 1;

    clear(color(c_black));
}

void GfxScreen::resetMode()
{
    setBIOSMode(m_origMode);

    m_width = 0;
    m_height = 0;
    m_maxx = 0;
    m_maxy = 0;
    m_pages = 0;
    m_lineSize = 0;
    m_pageSize = 0;
    m_chained = 0;
    m_modeName = NULL;
    m_crtc_addr = 0;
    m_isr1_addr = 0;

    m_putPixelFn = NULL;
    m_getPixelFn = NULL;
    m_clearFn = NULL;
    m_drawTextFn = NULL;
}

void GfxScreen::clear4(int row, int lines, uint8_t color)
{
    setPlanarRWMode(0,0);

    // enable all planes
    SEQ_OUT(SEQ_MAPMASK, 0x0F);

    // CPU data to all planes will be
    // replaced by set/reset value
    GCR_OUT(GCR_EN_SETRESET, 0x0F);

    // set/reset value
    GCR_OUT(GCR_SETRESET, color);

    // enable all bits
    GCR_OUT(GCR_BITMASK, 0xff);

    // since set/reset is enabled for all planes, the written value is ignored
    fillLong(m_activeOffset + (m_lineSize * row), 0, (m_lineSize*lines)/4);

    // disable set/reset
    GCR_OUT(GCR_EN_SETRESET, 0x00);

    setPlanarRWMode(0,2);
}

void GfxScreen::clear8(int row, int lines, uint8_t color)
{
    uint32_t c = color;
    c = (c << 8) | color;
    c = (c << 8) | color;
    c = (c << 8) | color;

    outp(SEQ_ADDR, 0x02);  // enable all planes
    outp(SEQ_DATA, 0x0F);

    fillLong(m_activeOffset + (m_lineSize * row), c, (m_width*lines)/16);
}

void GfxScreen::clear8chained(int row, int lines, uint8_t color)
{
    uint32_t c = color;
    c = (c << 8) | color;
    c = (c << 8) | color;
    c = (c << 8) | color;

    fillLong(m_activeOffset + (m_lineSize * row), c, (m_width*lines)/4);
}

void GfxScreen::clear(uint8_t color)
{
    clear(0, m_height, color);
}

void GfxScreen::setActivePage(uint8_t page)
{
    int32_t offset = page % m_pages;
    offset *= m_pageSize / 4;
    m_activeOffset = VGA_ADDR + offset;
}

void GfxScreen::setVisiblePage(uint8_t page)
{
    if(m_pages == 1) {
        vsync();
        return;
    }

    int32_t offset = page % m_pages;
    offset *= m_pageSize / 4;

    // wait for display disable
    while(inp(m_isr1_addr) & 0x01);

    // set start address
    outp(m_crtc_addr, 0x0c);
    outp(m_crtc_addr+1, (offset & 0xff00) >> 8);
    outp(m_crtc_addr, 0x0d);
    outp(m_crtc_addr+1, offset &0x00ff);

    // wait for vertical retrace
    while(!(inp(m_isr1_addr) & 0x08));
}

void GfxScreen::putPixel4(int16_t x, int16_t y, uint8_t color)
{
    // clip to the mode width and height
    if(x < 0 || x > m_maxx || y < 0 || y > m_maxy) {
        return;
    }

    uint8_t bitmask = 1 << ((x & 0x07) ^ 0x07);
    outp(GCR_ADDR, 0x08);
    outp(GCR_DATA, bitmask);

    int offset = y*m_lineSize + x/8;
    volatile uint8_t dummy = *(m_activeOffset + offset); // load latches
    *(m_activeOffset + offset) = color % 16;
}

void GfxScreen::putPixel8(int16_t x, int16_t y, uint8_t color)
{
    // clip to the mode width and height
    if (x < 0 || x > m_maxx || y < 0 || y > m_maxy) {
        return;
    }

    // set the mask so that only one pixel gets written
    outp(SEQ_ADDR, 0x02);
    outp(SEQ_DATA, 1 << (x & 0x3));

    // put pixel in memory
    *(m_activeOffset + (m_lineSize * y) + (x >> 2)) = color;
}

void GfxScreen::putPixel8chained(int16_t x, int16_t y, uint8_t color)
{
    // clip to the mode width and height
    if (x < 0 || x > m_maxx || y < 0 || y > m_maxy) {
        return;
    }

    // put pixel in memory
    *(m_activeOffset + (m_lineSize * y) + x) = color;
}

int16_t GfxScreen::getPixel4(int16_t x, int16_t y)
{
    if(x < 0 || x > m_maxx || y < 0 || y > m_maxy) {
        return -1;
    }

    uint8_t *cell = (m_activeOffset + (m_lineSize * y) + x/8);
    int16_t value = 0;
    uint8_t bit = x & 7;

    GCR_OUT(GCR_READMAP_SEL, 0);
    value |= ((*cell)>>bit)&1;
    GCR_OUT(GCR_READMAP_SEL, 1);
    value |= (((*cell)>>bit)&1) << 1;
    GCR_OUT(GCR_READMAP_SEL, 2);
    value |= (((*cell)>>bit)&1) << 2;
    GCR_OUT(GCR_READMAP_SEL, 3);
    value |= (((*cell)>>bit)&1) << 3;

    return value;
}

int16_t GfxScreen::getPixel8(int16_t x, int16_t y)
{
    // clip to the mode width and height
    if (x < 0 || x > m_maxx || y < 0 || y > m_maxy) {
        return -1;
    }

    // set the mask so that only one pixel gets read
    outp(GCR_ADDR, 0x04);
    outp(GCR_DATA, x & 0x3);

    return *(m_activeOffset + (m_lineSize * y) + (x >> 2));
}

int16_t GfxScreen::getPixel8chained(int16_t x, int16_t y)
{
    // clip to the mode width and height
    if (x < 0 || x > m_maxx || y < 0 || y > m_maxy) {
        return -1;
    }

    return *(m_activeOffset + (m_lineSize * y) + x);
}

void GfxScreen::drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t color)
{
    // Lines are NOT clipped.
    //
    // Algorithm derived from:
    //  Digital Line Drawing by Paul Heckbert
    //  from "Graphics Gems", Academic Press, 1990
    //
    int16_t x = x1;
    int16_t y = y1;

    int16_t dx = x2 - x1;
    int16_t ax = abs(dx) << 1;
    int16_t sx = sign(dx);

    int16_t dy = y2 - y1;
    int16_t ay = abs(dy) << 1;
    int16_t sy = sign(dy);

    int16_t d;

    if(ax > ay) {
        // x dominant
        d = ay - (ax >> 1);
        while(true) {
            putPixel(x, y, color);
            if(x == x2) {
                return;
            }
            if(d >= 0) {
                y += sy;
                d -= ax;
            }
            x += sx;
            d += ay;
        }
    } else {
        // y dominant
        d = ax - (ay >> 1);
        while(true) {
            putPixel(x, y, color);
            if(y == y2) {
                return;
            }
            if(d >= 0) {
                x += sx;
                d -= ay;
            }
            y += sy;
            d += ax;
        }
    }
}

void GfxScreen::drawCircle(int16_t cx, int16_t cy, int16_t r, uint8_t color)
{
    int32_t n=0, invradius=(1/(float)r)*0x10000L;
    int16_t dx=0, dy=r-1;

    putPixel(cx, cy, color);

    while(dx<=dy)
    {
        putPixel(cx+dy, cy-dx, color); // octant 0
        putPixel(cx+dx, cy-dy, color); // octant 1
        putPixel(cx-dx, cy-dy, color); // octant 2
        putPixel(cx-dy, cy-dx, color); // octant 3
        putPixel(cx-dy, cy+dx, color); // octant 4
        putPixel(cx-dx, cy+dy, color); // octant 5
        putPixel(cx+dx, cy+dy, color); // octant 6
        putPixel(cx+dy, cy+dx, color); // octant 7
        dx++;
        n += invradius;
        dy = (int16_t)((r * SIN_ACOS[(int16_t)(n>>6)]) >> 16);
    }
}

void GfxScreen::drawRectangle(int16_t x, int16_t y, int16_t width, int16_t height, uint8_t color)
{
    int x2 = x+width-1;
    int y2 = y+height-1;
    drawLine(x, y, x2, y, color);
    drawLine(x, y, x, y2, color);
    drawLine(x2, y, x2, y2, color);
    drawLine(x, y2, x2, y2, color);

}

void GfxScreen::drawChar8(int16_t x, int16_t y, uint8_t color, char c)
{
    // draws a character in 8-bit / 256-color modes
    uint8_t far *font = m_fontAddr + (c * 8);

    for (int i = 0; i < 8; i++) {
        uint8_t mask = *font;
        for (int j = 0; j < 8; j++) {
            if (mask & 0x80) {
                putPixel(x + j, y + i, color);
            }
            mask <<= 1;
        }
        font++;
    }
}

void GfxScreen::drawChar4(int16_t x, int16_t y, uint8_t color, char c)
{
    // draws a character in 4-bit / 16-color modes

    // C++ version of LISTING 26.1,
    // Graphics Programming Black Book by Michael Abrash.
    // This function needs write mode 3

    uint8_t* vga_offset = m_activeOffset + (y * m_lineSize) + x / 8;
    uint8_t xbit = x & 7;
    uint8_t far *font = m_fontAddr + c * 8;

    outp(GCR_ADDR, GCR_ROTATE);
    uint8_t rot = inp(GCR_DATA);
    rot &= 0xe0;
    rot |= xbit;
    outp(GCR_DATA, rot);

    uint8_t leftmask = 0xff >> xbit;
    uint8_t rightmask = 0xff << (-xbit + 8);
    volatile uint8_t dummy;
    for(uint8_t i=0; i<8; i++) {
        GCR_OUT(GCR_BITMASK, leftmask);
        dummy = *vga_offset;
        *vga_offset = *font;

        GCR_OUT(GCR_BITMASK, rightmask);
        dummy = *vga_offset;
        *vga_offset = *font;

        font++;
        vga_offset += m_lineSize;
    }
}

void GfxScreen::drawText8(int16_t x, int16_t y, uint8_t color, const char *string)
{
    while(*string) {
        drawChar8(x, y, color, *string++);
        x += 8;
    }
}

void GfxScreen::drawText4(int16_t x, int16_t y, uint8_t color, const char *string)
{
    setPlanarRWMode(0,3);

    outp(GCR_ADDR, GCR_SETRESET);
    uint8_t setrst = inp(GCR_DATA);
    setrst &= 0xf0;
    setrst |= color & 0x0f;
    outp(GCR_DATA, setrst);

    while(*string) {
        drawChar4(x, y, color, *string++);
        x += 8;
    }

    setPlanarRWMode(0,2);
}

int32_t middleMask[4][4] =
{
    {0x1, 0x3, 0x7, 0xf},
    {0x0, 0x2, 0x6, 0xe},
    {0x0, 0x0, 0x4, 0xc},
    {0x0, 0x0, 0x0, 0x8},
};
int32_t leftMask[4] = {0xf, 0xe, 0xc, 0x8};
int32_t rightMask[4] = {0x1, 0x3, 0x7, 0xf};

void GfxScreen::fillRect8(int16_t x, int16_t y, int16_t width, int16_t height, uint8_t color)
{
    int32_t x1 = x;
    int32_t x2 = x + width - 1;
    int32_t y1 = y;
    int32_t y2 = y + height - 1;

    // clip to the screen.
    if(x1 < 0) {
        x1 = 0;
    }
    if(x2 > m_maxx) {
        x2 = m_maxx;
    }
    if(y1 < 0) {
        y1 = 0;
    }
    if (y2 > m_maxy) {
        y2 = m_maxy;
    }
    if(y2 < y1 || x2 < x1) {
        return;
    }

    // in unchained 8bit modes we want to paint from the top down
    // and make use of the 4 pixel wide vertical bands
    int32_t leftBand = x1 >> 2;
    int32_t rightBand = x2 >> 2;
    int32_t leftBit = x1 & 3;
    int32_t rightBit = x2 & 3;
    uint8_t *top;
    uint8_t *where;
    int32_t mask;

    if(leftBand == rightBand) {
        // the whole rectangle is in one band
        mask = middleMask[leftBit][rightBit];
        outp(SEQ_ADDR, 0x02);
        outp(SEQ_DATA, mask);

        top = m_activeOffset + (m_lineSize * y1) + leftBand;
        for(int32_t i = y1; i <= y2; i++) {
            *top = color;
            top += m_lineSize;
        }
    } else {
        // spans 2 or more bands
        mask = leftMask[leftBit];
        outp(SEQ_ADDR, 0x02);
        outp(SEQ_DATA, mask);

        top = m_activeOffset + (m_lineSize * y1) + leftBand;
        where = top;
        // fill the left edge
        for(int32_t i = y1; i <= y2; i++) {
            *where = color;
            where += m_lineSize;
        }
        top++;

        outp(SEQ_ADDR, 0x02);
        outp(SEQ_DATA, 0x0f);

        int32_t bands = rightBand - (leftBand + 1);
        if(bands > 0) {
            where = top;
            // fill the middle
            for(int32_t i = y1; i <= y2; i++) {
                memset(where, color, bands);
                where += m_lineSize;
            }
            top += bands;
        }

        mask = rightMask[rightBit];

        outp(SEQ_ADDR, 0x02);
        outp(SEQ_DATA, mask);

        where = top;
        // fill the right edge
        for(int32_t i = y1; i <= y2; i++) {
            *where = color;
            where += m_lineSize;
        }
    }
}

void GfxScreen::fillRect8chained(int16_t x, int16_t y, int16_t width, int16_t height, uint8_t color)
{
    if(x<0 || y<0 || x>m_maxx || y>m_maxy || width==0 || height==0) {
        return;
    }
    if(x + width > m_width) {
        width = m_width - x;
    }
    if(y + height > m_height) {
        height = m_height - y;
    }

    uint32_t c = color;
    c = (c << 8) | color;
    c = (c << 8) | color;
    c = (c << 8) | color;

    uint8_t *lineptr = m_activeOffset + (m_lineSize * y) + x;
    int dwords = width / 4;
    int bytes = width - dwords * 4;
    int linestep = dwords * 4 + (m_width - width);
    for(int l=0; l<height; l++) {
        for(int b=0; b<bytes; b++) {
            *lineptr++ = color;
        }
        fillLong(lineptr, c, dwords);
        lineptr += linestep;
    }
}

//---------------------------------------------------
//
// Set one entry in the DAC color palette (256 color mode).
//

void GfxScreen::setColor256(int16_t index, uint16_t r, uint16_t g, uint16_t b)
{
    if (index < 0 || index > 255) {
        return;
    }

    while(!(inp(m_isr1_addr) & 0x01));    // wait for blanking

    outp(PAL_WRITE_ADDR, index);
    outp(PAL_DATA, r);
    outp(PAL_DATA, g);
    outp(PAL_DATA, b);
}

void GfxScreen::setPalette256(int16_t start, int16_t count, s_color *p)
{
    int16_t i;

    if(start < 0 || (start + count - 1) > 255) {
        return;
    }

    vsync();

    outp(PAL_WRITE_ADDR, start);
    for (i = 0; i < count; i++) {
        outp(PAL_DATA, p->red);
        outp(PAL_DATA, p->green);
        outp(PAL_DATA, p->blue);
        p++;
    }
}

void GfxScreen::vsync()
{
    // wait until any previous retrace has ended
    while(inp(m_isr1_addr) & 0x08);
    // wait until a new retrace has just begun
    while(!(inp(m_isr1_addr) & 0x08));
}

void GfxScreen::setPlanarRWMode(int rmode, int wmode)
{
    outp(GCR_ADDR, 0x05); // R/W mode
    uint8_t mode = inp(GCR_DATA) & 0xF4;
    mode |= ((rmode&1)<<3) | (wmode&3);
    outp(GCR_DATA, mode);
}

void GfxScreen::setStdVGAColorMap()
{
    for(int i=0; i<256; i++) m_cval[i] = i;
    for(int i=0; i<256; i++) m_cmap[i] = i;
}


//****************************************************************************//
// MODE SETTING
//****************************************************************************//

void selectVGAFreq(int hpels, int lines)
{
    uint8_t reg = 0x23; // bit 0,1,5
    switch(hpels) {
        case 640:
        case 320:
            break;
        case 720:
        case 360:
            reg |= 0x04;
            break;
    }
    switch(lines) {
        default:
        case 400: reg |= 0x40; break;
        case 350: reg |= 0x80; break;
        case 480: reg |= 0xc0; break;
    }

    SEQ_OUT(0x00,0x01);   // synchronous reset while setting Misc Output
    outp(MOR_ADDR, reg);
    SEQ_OUT(0x00,0x03);   // undo reset (restart sequencer)
}

void GfxScreen::mode_b320x200_0Dh()
{
    setBIOSMode(0x0d);

    m_width    = 320;
    m_height   = 200;
    m_pages    = 8;
    m_lineSize = 40;
    m_chained  = 0;
    m_pageSize = 32000;
    m_modeName = "Mode 0Dh 320x200x16";
    m_colors   = 16;
    m_putPixelFn = putPixel4;
    m_getPixelFn = getPixel4;
    m_clearFn    = clear4;
    m_drawTextFn = drawText4;

    // put pixel needs write mode 2
    setPlanarRWMode(0,2);

    setStdVGAColorMap();
}

void GfxScreen::mode_b640x200_0Eh()
{
    setBIOSMode(0x0e);

    m_width    = 640;
    m_height   = 200;
    m_pages    = 4;
    m_lineSize = 80;
    m_chained  = 0;
    m_pageSize = 64000;
    m_modeName = "Mode 0Eh 640x200x16";
    m_colors   = 16;
    m_putPixelFn = putPixel4;
    m_getPixelFn = getPixel4;
    m_clearFn    = clear4;
    m_drawTextFn = drawText4;

    // put pixel needs write mode 2
    setPlanarRWMode(0,2);

    setStdVGAColorMap();
}

void GfxScreen::mode_b640x350_0Fh()
{
    setBIOSMode(0x0f);

    m_width    = 640;
    m_height   = 350;
    m_pages    = 2;
    m_lineSize = 80;
    m_chained  = 0;
    m_pageSize = 112000;
    m_modeName = "Mode 0Fh 640x350 monochrome";
    m_colors   = 4;
    m_putPixelFn = putPixel4;
    m_getPixelFn = getPixel4;
    m_clearFn    = clear4;
    m_drawTextFn = drawText4;

    // put pixel needs write mode 2
    setPlanarRWMode(0,2);

    memset(m_cval, 0, 256);
    m_cval[0] = 0;
    m_cval[1] = 1;
    m_cval[2] = 4;
    m_cval[3] = 5;

    m_cmap[c_black  ] = 0;
    m_cmap[c_blue   ] = 1;
    m_cmap[c_green  ] = 1;
    m_cmap[c_cyan   ] = 1;
    m_cmap[c_red    ] = 1;
    m_cmap[c_magenta] = 1;
    m_cmap[c_brown  ] = 1;
    m_cmap[c_lgray  ] = 3;
    m_cmap[c_dgray  ] = 1;
    m_cmap[c_lblue  ] = 3;
    m_cmap[c_lgreen ] = 3;
    m_cmap[c_lcyan  ] = 3;
    m_cmap[c_lred   ] = 3;
    m_cmap[c_pink   ] = 3;
    m_cmap[c_yellow ] = 3;
    m_cmap[c_white  ] = 3;
    for(int i=16; i<256; i++) {
        m_cmap[i] = i%4;
    }
}

void GfxScreen::mode_b640x350_10h()
{
    setBIOSMode(0x10);

    m_width    = 640;
    m_height   = 350;
    m_pages    = 2;
    m_lineSize = 80;
    m_chained  = 0;
    m_pageSize = 112000;
    m_modeName = "Mode 10h 640x350x16";
    m_colors   = 16;
    m_putPixelFn = putPixel4;
    m_getPixelFn = getPixel4;
    m_clearFn = clear4;
    m_drawTextFn = drawText4;

    // put pixel needs write mode 2
    setPlanarRWMode(0,2);

    setStdVGAColorMap();
}

void GfxScreen::mode_b640x480_12h()
{
    setBIOSMode(0x12);

    m_width    = 640;
    m_height   = 480;
    m_pages    = 1;
    m_lineSize = 80;
    m_chained  = 0;
    m_pageSize = 153600;
    m_modeName = "Mode 12h 640x480x16";
    m_colors   = 16;
    m_putPixelFn = putPixel4;
    m_getPixelFn = getPixel4;
    m_clearFn = clear4;
    m_drawTextFn = drawText4;

    // put pixel needs write mode 2
    setPlanarRWMode(0,2);

    setStdVGAColorMap();
}

void GfxScreen::mode_b320x200_13h()
{
    setBIOSMode(0x13);

    m_width    = 320;
    m_height   = 200;
    m_pages    = 1;
    m_lineSize = 320;
    m_chained  = 1;
    m_pageSize = 64000;
    m_modeName = "Mode 13h 320x200x256 chain4";
    m_colors   = 256;
    m_putPixelFn = putPixel8chained;
    m_getPixelFn = getPixel8chained;
    m_clearFn   = clear8chained;
    m_drawTextFn = drawText8;

    setStdVGAColorMap();
}

void GfxScreen::mode_t160x120()
{
    m_width   = 160;
    m_height  = 120;
    m_maxx    = 159;
    m_maxy    = 119;
    m_pages   = 13;
    m_lineSize = 40;
    m_chained  = 0;
    m_pageSize = 19200;
    m_modeName = "160x120x256 planar";
    m_colors   = 256;
    m_putPixelFn = putPixel8;
    m_getPixelFn = getPixel8;
    m_clearFn = clear8;
    m_drawTextFn = drawText8;

    setStdVGAColorMap();

    setBIOSMode(0x13);

    selectVGAFreq(320, 480);

    unlockCRTC(CRTC_ADDR_COL);
    const int16_t crtc[] = {
        0x3200, // hor. total
        0x2701, // hor. display enable end
        0x2802, // blank start
        0x2003, // blank end
        0x2b04, // retrace start
        0x7005, // retrace end
        0x0d06, // vertical total
        0x3e07, // overflow register
        0x0008, // preset row scan
        0x4309, // max scan line/char heigth
        0xea10, // ver. retrace start
        0xac11, // ver. retrace end and lock
        0xdf12, // ver. display enable end
        0x1413, // offset/logical width
        0x0014, // underline location
        0xe715, // ver. blank start
        0x0616, // ver. blank end
        0xe317, // mode control
        -1
    };
    setVGARegisters(CRTC_ADDR_COL, &crtc[0]);

    SEQ_OUT(0x01, 0x01); // Clocking Mode
    SEQ_OUT(0x03, 0x00); // Character Map Select
    SEQ_OUT(0x04, 0x06); // Memory Mode

    GCR_OUT(0x05, 0x40);
    GCR_OUT(0x06, 0x05);

    ACR_OUT_COL(0x10, 0x41);
    ACR_OUT_COL(0x11, 0x00);
    ACR_OUT_COL(0x12, 0x0f);
    ACR_OUT_COL(0x13, 0x00);
    ACR_OUT_COL(0x14, 0x00);
}

void GfxScreen::mode_t296x220()
{
    m_width    = 296;
    m_height   = 220;
    m_pages    = 4;
    m_lineSize = 74;
    m_pageSize = 65120;
    m_modeName = "296x220x256 planar";
    m_colors   = 256;
    m_chained  = 0;
    m_putPixelFn = putPixel8;
    m_getPixelFn = getPixel8;
    m_clearFn    = clear8;
    m_drawTextFn = drawText8;

    setStdVGAColorMap();

    setBIOSMode(0x13);

    selectVGAFreq(320, 480);

    unlockCRTC(CRTC_ADDR_COL);
    const int16_t crtc[] = {
        0x5f00, // hor. total
        0x4901, // hor. display enable end
        0x5002, // blank start
        0x8203, // blank end
        0x5304, // retrace start
        0x8005, // retrace end
        0x0d06, // vertical total
        0x3e07, // overflow register
        0x0008, // preset row scan
        0x4109, // max scan line/char heigth
        0xd710, // ver. retrace start
        0xac11, // ver. retrace end and lock
        0xb712, // ver. display enable end
        0x2513, // offset/logical width
        0x0014, // underline location
        0xe715, // ver. blank start
        0x0616, // ver. blank end
        0xe317, // mode control
        -1
    };
    setVGARegisters(CRTC_ADDR_COL, &crtc[0]);

    SEQ_OUT(0x01, 0x01); // Clocking Mode
    SEQ_OUT(0x04, 0x06); // Memory Mode

    GCR_OUT(0x05, 0x40);
    GCR_OUT(0x06, 0x05);

    ACR_OUT_COL(0x10, 0x41);
    ACR_OUT_COL(0x13, 0x00);
}


void GfxScreen::mode_t256x256_Q()
{
    m_width    = 256;
    m_height   = 256;
    m_pages    = 1;
    m_lineSize = 256;
    m_chained  = 1;
    m_pageSize = 65536;
    m_modeName = "Mode Q 256x256x256 chain4";
    m_colors   = 256;
    m_putPixelFn = putPixel8chained;
    m_getPixelFn = getPixel8chained;
    m_clearFn    = clear8chained;
    m_drawTextFn = drawText8;

    setBIOSMode(0x13);

    unlockCRTC(CRTC_ADDR_COL);

    const int16_t crtc[] = {
        0x5f00, // hor. total
        0x3f01, // hor. display enable end
        0x4002, // blank start
        0x8203, // blank end
        0x4a04, // retrace start
        //0x4e04, // retrace start
        0x9a05, // retrace end
        0x2306, // vertical total
        0xb207, // overflow register
        0x0008, // preset row scan
        0x6109, // max scan line/char heigth
        0x0a10, // ver. retrace start
        0xac11, // ver. retrace end
        0xff12, // ver. display enable end
        0x2013, // offset/logical width
        0x4014, // underline location
        0x0715, // ver. blank start
        0x1a16, // ver. blank end
        //0x1716, // ver. blank end
        0xa317, // mode control
        -1
    };
    setVGARegisters(CRTC_ADDR_COL, &crtc[0]);

    SEQ_OUT(0x01,0x01); // clock mode register
    SEQ_OUT(0x04,0x0e); // memory mode register
    GCR_OUT(0x05,0x40); // mode register
    GCR_OUT(0x06,0x05); // misc. register
    ACR_OUT_COL(0x10,0x41); // mode control

    setStdVGAColorMap();
}

void GfxScreen::mode_t320x200_Y()
{
    m_width    = 320;
    m_height   = 200;
    m_pages    = 4;
    m_lineSize = 80;
    m_pageSize = 64000;
    m_chained  = 0;
    m_modeName = "Mode Y 320x200x256 planar";
    m_colors   = 256;
    m_putPixelFn = putPixel8;
    m_getPixelFn = getPixel8;
    m_clearFn    = clear8;
    m_drawTextFn = drawText8;

    setStdVGAColorMap();

    setBIOSMode(0x13);

    // Turn off Chain-4
    SEQ_OUT(0x04,0x06);

    // Turn off word mode
    CRTC_OUT_COL(0x17,0xe3);

    // Turn off doubleword mode
    CRTC_OUT_COL(0x14,0x00);
}

void GfxScreen::mode_t320x240_X()
{
    m_width    = 320;
    m_height   = 240;
    m_pages    = 3;
    m_lineSize = 80;
    m_pageSize = 76800;
    m_chained  = 0;
    m_modeName = "Mode X 320x240x256 planar";
    m_colors   = 256;
    m_putPixelFn = putPixel8;
    m_getPixelFn = getPixel8;
    m_clearFn    = clear8;
    m_drawTextFn = drawText8;

    setStdVGAColorMap();

    setBIOSMode(0x13);    // start from mode 13h

    SEQ_OUT(0x04,0x06);   // disable chain4

    selectVGAFreq(320, 480);

    unlockCRTC(CRTC_ADDR_COL);

    // reprogram the crtc
    const int16_t crtc[] = {
        0x0d06,  // vertical total
        0x3e07,  // overflow (bit 8 of vertical counts)
        0x4109,  // cell height (2 to double-scan)
        0xea10,  // v sync start
        0xac11,  // v sync end and protect cr0-cr7
        0xdf12,  // vertical displayed
        0x0014,  // turn off dword mode
        0xe715,  // v blank start
        0x0616,  // v blank end
        0xe317,  // turn on byte mode
        -1
    };
    setVGARegisters(CRTC_ADDR_COL, &crtc[0]);
}

void GfxScreen::mode_t320x400()
{
    m_width    = 320;
    m_height   = 400;
    m_pages    = 2;
    m_lineSize = 80;
    m_pageSize = 128000;
    m_chained  = 0;
    m_modeName = "320x400x256 planar";
    m_colors   = 256;
    m_putPixelFn = putPixel8;
    m_getPixelFn = getPixel8;
    m_clearFn    = clear8;
    m_drawTextFn = drawText8;

    setStdVGAColorMap();

    setBIOSMode(0x13);

    // Turn off the Chain-4 bit in SEQ #4
    SEQ_OUT(0x04,0x06);

    // Turn off word mode, by setting the Mode Control register
    // of the CRTC #17
    CRTC_OUT_COL(0x17,0xe3);

    // Turn off doubleword mode, by setting CRTC #14
    CRTC_OUT_COL(0x14,0x00);

    // Set MSL to 0 in the maximum scan line register.
    // This action turns 320x200 mode 13 into 320x400 mode 13+
    CRTC_OUT_COL(0x09,0x40);
}

void GfxScreen::mode_t360x270()
{
    m_width    = 360;
    m_height   = 270;
    m_pages    = 2;
    m_lineSize = 90;
    m_pageSize = 97200;
    m_chained  = 0;
    m_modeName = "360x270x256 planar";
    m_colors   = 256;
    m_putPixelFn = putPixel8;
    m_getPixelFn = getPixel8;
    m_clearFn    = clear8;
    m_drawTextFn = drawText8;

    setStdVGAColorMap();

    setBIOSMode(0x13);

    selectVGAFreq(360,480);

    unlockCRTC(CRTC_ADDR_COL);
    const int16_t crtc[] = {
        0x6b00, // hor. total
        0x5901, // hor. display enable end
        0x5a02, // blank start
        0x8e03, // blank end
        0x5e04, // retrace start
        0x8a05, // retrace end
        0x3006, // vertical total
        0xf007, // overflow register
        0x0008, // preset row scan
        0x6109, // max scan line/char heigth
        0x2010, // ver. retrace start
        0xa911, // ver. retrace end and lock
        0x1b12, // ver. display enable end
        0x2d13, // offset/logical width
        0x0014, // underline location
        0x1f15, // ver. blank start
        0x2f16, // ver. blank end
        0xe317, // mode control
        -1
    };
    setVGARegisters(CRTC_ADDR_COL, &crtc[0]);

    SEQ_OUT(0x01,0x01);
    SEQ_OUT(0x04,0x06);
    GCR_OUT(0x05,0x40);
    GCR_OUT(0x06,0x05);
    ACR_OUT_COL(0x10,0x41);
    ACR_OUT_COL(0x13,0x00);
}

void GfxScreen::mode_t360x360()
{
    m_width    = 360;
    m_height   = 360;
    m_pages    = 2;
    m_lineSize = 90;
    m_pageSize = 129600;
    m_chained  = 0;
    m_modeName = "360x360x256 planar";
    m_colors   = 256;
    m_putPixelFn = putPixel8;
    m_getPixelFn = getPixel8;
    m_clearFn    = clear8;
    m_drawTextFn = drawText8;

    setStdVGAColorMap();

    setBIOSMode(0x13);

    selectVGAFreq(360, 400);

    unlockCRTC(CRTC_ADDR_COL);
    const int16_t crtc[] = {
        0x6b00, // hor. total
        0x5901, // hor. display enable end
        0x5a02, // blank start
        0x8e03, // blank end
        0x5e04, // retrace start
        0x8a05, // retrace end
        0xbf06, // vertical total
        0x1f07, // overflow register
        0x0008, // preset row scan
        0x4009, // max scan line/char heigth
        0x8810, // ver. retrace start
        0x8511, // ver. retrace end and lock
        0x6712, // ver. display enable end
        0x2d13, // offset/logical width
        0x0014, // underline location
        0x6d15, // ver. blank start
        0xba16, // ver. blank end
        0xe317, // mode control
        -1
    };
    setVGARegisters(CRTC_ADDR_COL, &crtc[0]);

    SEQ_OUT(0x01,0x01);
    SEQ_OUT(0x04,0x06);
    GCR_OUT(0x05,0x40);
    GCR_OUT(0x06,0x05);
    ACR_OUT_COL(0x10,0x41);
    ACR_OUT_COL(0x13,0x00);
}

void GfxScreen::mode_t360x480()
{
    m_width    = 360;
    m_height   = 480;
    m_pages    = 1;
    m_lineSize = 90;
    m_pageSize = 172800;
    m_chained  = 0;
    m_modeName = "360x480x256 planar";
    m_colors   = 256;
    m_putPixelFn = putPixel8;
    m_getPixelFn = getPixel8;
    m_clearFn    = clear8;
    m_drawTextFn = drawText8;

    setStdVGAColorMap();

    //setBIOSMode(0x12);
    setBIOSMode(0x13);

    SEQ_OUT(0x04,0x06);   // disable chain4

    selectVGAFreq(360, 480);

    unlockCRTC(CRTC_ADDR_COL);

    // reprogram the crtc
    const int16_t crtc[] = {
        0x6b00,  // horz total
        0x5901,  // horz displayed
        0x5a02,  // start horz blanking
        0x8e03,  // end horz blanking
        0x5e04,  // start h sync
        0x8a05,  // end h sync
        0x0d06,  // vertical total
        0x3e07,  // overflow
        0x4009,  // cell height
        0xea10,  // v sync start
        0xac11,  // v sync end and protect cr0-cr7
        0xdf12,  // vertical displayed
        0x2d13,  // offset
        0x0014,  // turn off dword mode
        0xe715,  // v blank start
        0x0616,  // v blank end
        0xe317,  // turn on byte mode
        -1
    };
    setVGARegisters(CRTC_ADDR_COL, &crtc[0]);
}

void GfxScreen::mode_t400x300()
{
    m_width    = 400;
    m_height   = 300;
    m_pages    = 2;
    m_lineSize = 100;
    m_pageSize = 120000;
    m_chained  = 0;
    m_modeName = "400x300x256 planar";
    m_colors   = 256;
    m_putPixelFn = putPixel8;
    m_getPixelFn = getPixel8;
    m_clearFn    = clear8;
    m_drawTextFn = drawText8;

    setStdVGAColorMap();

    setBIOSMode(0x13);

    selectVGAFreq(720, 480);

    unlockCRTC(CRTC_ADDR_COL);
    const int16_t crtc[] = {
        0x7100, // hor. total
        0x6301, // hor. display enable end
        0x6402, // blank start
        0x9203, // blank end
        0x6704, // retrace start
        0x8205, // retrace end
        0x4606, // vertical total
        0x1f07, // overflow register
        0x0008, // preset row scan
        0x4009, // max scan line/char heigth
        0x3110, // ver. retrace start
        0x8011, // ver. retrace end and lock
        0x2b12, // ver. display enable end
        0x3213, // offset/logical width
        0x0014, // underline location
        0x2f15, // ver. blank start
        0x4416, // ver. blank end
        0xe317, // mode control
        -1
    };
    setVGARegisters(CRTC_ADDR_COL, &crtc[0]);

    SEQ_OUT(0x01,0x01);
    SEQ_OUT(0x02,0x0f);
    SEQ_OUT(0x04,0x06);
    GCR_OUT(0x05,0x40);
    GCR_OUT(0x06,0x05);
    ACR_OUT_COL(0x10,0x41);
    ACR_OUT_COL(0x13,0x00);
}
