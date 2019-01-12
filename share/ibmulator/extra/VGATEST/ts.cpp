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

#include <stdio.h>
#include <string.h>
#include <i86.h>
#include <conio.h>

#include "common.h"
#include "utils.h"
#include "ts.h"

#define mkTextColor(_FG_,_BG_) (_FG_ | (_BG_ << 4))

TextScreen::TextScreen()
{
    m_curRow = 0;
    m_curCol = 0;
    m_prevCol = 0;
    m_curFgColor = DEFAULT_FG_COL;
    m_curBgColor = DEFAULT_BG_COL;
    m_moved = false;

    m_error = e_none;
    m_modeName = "";

    uint8_t mode = getBIOSMode();
    switch(mode) {
        case 0:
        case 1:
            m_resetModeFn = setMode_b40x25_9x16_01h;
            m_cols = 40;
            m_rows = 25;
            m_textPage = ((char *)0xb8000);
            break;
        case 2:
        case 3:
            m_resetModeFn = setMode_b80x25_9x16_03h;
            m_cols = 80;
            m_rows = 25;
            m_textPage = ((char *)0xb8000);
            break;
        case 7:
            m_resetModeFn = setMode_b80x25_9x16_07h;
            m_cols = 80;
            m_rows = 25;
            m_textPage = ((char *)0xb0000);
            break;
        default:
            m_resetModeFn = NULL;
            m_error = e_modeNotSupported;
            break;
    }
}

TextScreen::~TextScreen()
{
}

void TextScreen::setMode(int16_t mode)
{
    m_error = e_none;
    m_modeName = "";

    switch (mode)
    {
    case t_b40x25_8x8_00h  : setMode(0x00, 40, 25, 8,  8); break; //0  320x200 16gray B800
    case t_b40x25_8x14_00h : setMode(0x00, 40, 25, 8, 14); break; //0* 320x350 16gray B800
    case t_b40x25_8x16_00h : setMode(0x00, 40, 25, 8, 16); break; //   320x400  16    B800
    case t_b40x25_9x16_00h : setMode(0x00, 40, 25, 9, 16); break; //0+ 360x400  16    B800
    case t_b40x25_8x8_01h  : setMode(0x01, 40, 25, 8,  8); break; //1  320x200  16    B800
    case t_b40x25_8x14_01h : setMode(0x01, 40, 25, 8, 14); break; //1* 320x350  16    B800
    case t_b40x25_8x16_01h : setMode(0x01, 40, 25, 8, 16); break; //   320x400  16    B800
    case t_b40x25_9x16_01h : setMode(0x01, 40, 25, 9, 16); break; //1+ 360x400  16    B800
    case t_b80x25_8x8_02h  : setMode(0x02, 80, 25, 8,  8); break; //2  640x200 16gray B800
    case t_b80x25_8x14_02h : setMode(0x02, 80, 25, 8, 14); break; //2* 640x350 16gray B800
    case t_b80x25_8x16_02h : setMode(0x02, 80, 25, 8, 16); break; //   640x400  16    B800
    case t_b80x25_9x16_02h : setMode(0x02, 80, 25, 9, 16); break; //2+ 720x400  16    B800
    case t_b80x25_8x8_03h  : setMode(0x03, 80, 25, 8,  8); break; //3  640x200  16    B800
    case t_b80x25_8x14_03h : setMode(0x03, 80, 25, 8, 14); break; //3* 640x350  16/64 B800
    case t_b80x25_8x16_03h : setMode(0x03, 80, 25, 8, 16); break; //   640x400  16    B800
    case t_b80x25_9x16_03h : setMode(0x03, 80, 25, 9, 16); break; //3+ 720x400  16    B800
    case t_b80x25_9x14_07h : setMode(0x07, 80, 25, 9, 14); break; //7  720x350 mono   B000
    case t_b80x25_9x16_07h : setMode(0x07, 80, 25, 9, 16); break; //7+ 720x400 mono   B000
    case t_t80x43_8x8  : setMode(0x03, 80, 43, 8,  8); break; // 640x350
    case t_t80x50_9x8  : setMode(0x03, 80, 50, 9,  8); break; // 720x400
    case t_t80x28_9x14 : setMode(0x03, 80, 28, 9, 14); break; // 720x400
    case t_t80x30_8x16 : setMode_640x480(16); break; // 640x480
    case t_t80x34_8x14 : setMode_640x480(14); break; // 640x480
    case t_t80x60_8x8  : setMode_640x480(8); break;  // 640x480
    default:
        m_error = e_modeNotSupported;
        break;
    }

    if (m_error != e_none) {
        return;
    }

    erasePage(c_black, c_black);
}

void TextScreen::resetMode()
{
    if(m_resetModeFn) {
        (*this.*m_resetModeFn)();
    }
}

int TextScreen::setRow(int row)
{
    setPos(row, m_curCol);
    return row;
}

int TextScreen::setCol(int col)
{
    setPos(m_curRow, col);
    return col;
}

void TextScreen::setPos(int row, int col)
{
    m_curRow = row % m_rows;
    m_curCol = col % m_cols;
    m_prevCol = m_curCol;
    m_moved = true;
}

void TextScreen::getPos(int &row, int &col)
{
    row = m_curRow;
    col = m_curCol;
}

void TextScreen::setColor(uint8_t fg)
{
    m_curFgColor = fg;
}

void TextScreen::setColor(uint8_t fg, uint8_t bg)
{
    m_curFgColor = fg;
    m_curBgColor = bg;
}

void TextScreen::moveCursor()
{
    moveCursor(m_curRow, m_curCol, m_curFgColor, m_curBgColor);
}

void TextScreen::moveCursor(int row, int col)
{
    moveCursor(row, col, m_curFgColor, m_curBgColor);
}

void TextScreen::moveCursor(int row, int col, uint8_t fg, uint8_t bg)
{
    union REGS rg;

    rg.h.ah = 0x02;
    rg.h.dl = col;
    rg.h.dh = row;
    rg.h.bh = 0;

    int386(0x10, &rg, &rg);

    *(m_textPage + ((row * m_cols) << 1) + (col << 1) + 1) = mkTextColor(fg, bg);

    m_curFgColor = fg;
    m_curBgColor = bg;
}

void TextScreen::erasePage()
{
    erasePage(m_curFgColor, m_curBgColor);
}

void TextScreen::erasePage(uint8_t fg, uint8_t bg)
{
    uint8_t color = mkTextColor(fg, bg);
    char *ch = m_textPage;

    for (int i = 0; i < (m_rows * m_cols); i++) {
        *ch++ = ' ';
        *ch++ = color;
    }

    m_curFgColor = fg;
    m_curBgColor = bg;
}

void TextScreen::write(const char *text)
{
    write(text, m_curFgColor, m_curBgColor);
}

void TextScreen::write(char ch)
{
    char buf[2];
    buf[0] = ch;
    buf[1] = '\0';
    write(buf, m_curFgColor, m_curBgColor);
}

void TextScreen::write(const char *text, uint8_t fg)
{
    write(text, fg, m_curBgColor);
}

void TextScreen::write(const char *text, uint8_t fg, uint8_t bg)
{
    int col = m_curCol;
    if(!m_moved) {
        //col = m_prevCol;
    }

    write(m_curRow, col, text, fg, bg);
}

void TextScreen::write(int row, int col, const char *text, uint8_t fg, uint8_t bg)
{
    m_curRow = row % m_rows;
    m_curCol = col % m_cols;
    uint8_t color = mkTextColor(fg, bg);
    char t = *text++;
    while(t) {
        char *ch = m_textPage + ((m_curRow * m_cols) << 1) + (m_curCol << 1);
        if(t == '\n') {
            m_curRow = (m_curRow + 1) % m_rows;
            m_curCol = m_prevCol;
        } else {
            *ch++ = t;
            *ch++ = color;
            m_curCol = (m_curCol + 1) % m_cols;
        }
        t = *text++;
    }

    m_curFgColor = fg;
    m_curBgColor = bg;
    m_moved = false;
}

TextScreen& TextScreen::drawBox(int16_t row, int16_t col, int16_t width, int16_t height)
{
    return drawBox(row, col, width, height, m_curFgColor, m_curBgColor);
}

TextScreen& TextScreen::drawBox(int16_t row, int16_t col, int16_t width, int16_t height, uint8_t fg)
{
    return drawBox(row, col, width, height, fg, m_curBgColor);
}

TextScreen& TextScreen::drawBox(int16_t row, int16_t col, int16_t width, int16_t height, uint8_t fg, uint8_t bg)
{
    setColor(fg,bg);

    for(int c=col; c<col+width; c++) {
        (*this)(row,c)(0xcd);
        (*this)(row+height,c)(0xcd);
    }

    for(int r=row; r<row+height; r++) {
        (*this)(r,col)(0xba);
        (*this)(r,col+width)(0xba);
    }

    (*this)(row,col)(0xc9);
    (*this)(row,col+width)(0xbb);
    (*this)(row+height, col)(0xc8);
    (*this)(row+height, col+width)(0xbc);

    m_curRow = row;
    m_curCol = col+1;

    return *this;
}

//****************************************************************************//
// MODE SETTING
//****************************************************************************//

void setScanlines(int scanlines)
{
    union REGS rg;
    rg.h.ah = 0x12;
    rg.h.bl = 0x30;
    switch(scanlines) {
        case 200:
            rg.h.al = 0x00;
            break;
        case 350:
            rg.h.al = 0x01;
            break;
        case 400:
            rg.h.al = 0x02;
            break;
        default:
            return;
    }
    int386(0x10, &rg, &rg);
}

void setBIOSFont(int map, int size, bool activate=false)
{
    union REGS rg;
    rg.h.ah = 0x11;
    rg.h.bl = map;
    switch(size) {
        case 8:
            rg.h.al = 0x02;
            break;
        case 14:
            rg.h.al = 0x01;
            break;
        case 16:
            rg.h.al = 0x04;
            break;
        default:
            return;
    }
    if(activate) {
        rg.h.al |= 0x10;
    }
    int386(0x10, &rg, &rg);
}

static uint8_t font8x16[8][16] = {
  {0x81,0x00,0x3C,0x42,0x42,0x42,0x42,0x00,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x81},
  {0x81,0x00,0x00,0x02,0x02,0x02,0x02,0x00,0x02,0x02,0x02,0x02,0x00,0x00,0x00,0x81},
  {0x81,0x00,0x3C,0x02,0x02,0x02,0x02,0x3C,0x40,0x40,0x40,0x40,0x3C,0x00,0x00,0x81},
  {0x81,0x00,0x3C,0x02,0x02,0x02,0x02,0x3C,0x02,0x02,0x02,0x02,0x3C,0x00,0x00,0x81},
  {0x81,0x00,0x00,0x42,0x42,0x42,0x42,0x3C,0x02,0x02,0x02,0x02,0x00,0x00,0x00,0x81},
  {0x81,0x00,0x3C,0x40,0x40,0x40,0x40,0x3C,0x02,0x02,0x02,0x02,0x3C,0x00,0x00,0x81},
  {0x81,0x00,0x3C,0x40,0x40,0x40,0x40,0x3C,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x81},
  {0x81,0x00,0x3C,0x02,0x02,0x02,0x02,0x00,0x02,0x02,0x02,0x02,0x00,0x00,0x00,0x81},
};

static uint8_t font8x14[8][14] = {
  {0x81,0x3C,0x42,0x42,0x42,0x42,0x00,0x42,0x42,0x42,0x42,0x3C,0x00,0x81},
  {0x81,0x00,0x02,0x02,0x02,0x02,0x00,0x02,0x02,0x02,0x02,0x00,0x00,0x81},
  {0x81,0x3C,0x02,0x02,0x02,0x02,0x3C,0x40,0x40,0x40,0x40,0x3C,0x00,0x81},
  {0x81,0x3C,0x02,0x02,0x02,0x02,0x3C,0x02,0x02,0x02,0x02,0x3C,0x00,0x81},
  {0x81,0x00,0x42,0x42,0x42,0x42,0x3C,0x02,0x02,0x02,0x02,0x00,0x00,0x81},
  {0x81,0x3C,0x40,0x40,0x40,0x40,0x3C,0x02,0x02,0x02,0x02,0x3C,0x00,0x81},
  {0x81,0x3C,0x40,0x40,0x40,0x40,0x3C,0x42,0x42,0x42,0x42,0x3C,0x00,0x81},
  {0x81,0x3C,0x02,0x02,0x02,0x02,0x00,0x02,0x02,0x02,0x02,0x00,0x00,0x81},
};

static uint8_t font8x8[8][8] = {
  {0x99,0x24,0x24,0x00,0x24,0x24,0x18,0x81},
  {0x81,0x04,0x04,0x00,0x04,0x04,0x00,0x81},
  {0x99,0x04,0x04,0x18,0x20,0x20,0x18,0x81},
  {0x99,0x04,0x04,0x18,0x04,0x04,0x18,0x81},
  {0x81,0x24,0x24,0x18,0x04,0x04,0x00,0x81},
  {0x99,0x20,0x20,0x18,0x04,0x04,0x18,0x81},
  {0x99,0x20,0x20,0x18,0x24,0x24,0x18,0x81},
  {0x99,0x04,0x04,0x00,0x04,0x04,0x00,0x81},
};

static int map_offset[8] = {
    0x0000, // map 0
    0x4000, // map 1
    0x8000, // map 2
    0xc000, // map 3
    0x2000, // map 4
    0x6000, // map 5
    0xa000, // map 6
    0xe000  // map 7
};

void setCustomFonts(int from_map, int size)
{
    uint8_t *font;
    switch(size) {
        case 8:
            font = &font8x8[0][0];
            break;
        case 14:
            font = &font8x14[0][0];
            break;
        case 16:
            font = &font8x16[0][0];
            break;
        default:
            return;
    }
    /*
     * Display memory plane 2 is divided up into eight 8K banks of characters,
     * each of which holds 256 character bitmaps. Each character is on a 32 byte
     * boundary and is 32 bytes long. The offset in plane 2 of a character within
     * a bank is determined by taking the character's value and multiplying it by 32.
     * The first byte at this offset contains the 8 pixels of the top scan line of
     * the characters. Each successive byte contains another scan line's worth of
     * pixels. The best way to read and write fonts to display memory
     * is to use standard (not Odd/Even) addressing and Read Mode 0 and Write Mode 0
     * with plane 2 selected for read or write.
     */
    // Save registers
    uint8_t seq_map_mask, seq_mem_mode, gcr_read_map_select, gcr_gfx_mode, gcr_misc;
    SEQ_IN(SEQ_MAPMASK, seq_map_mask);
    SEQ_IN(SEQ_MEMMODE, seq_mem_mode);
    GCR_IN(GCR_READMAP_SEL, gcr_read_map_select);
    GCR_IN(GCR_GFX_MODE, gcr_gfx_mode);
    GCR_IN(GCR_MISC, gcr_misc);

    // Put video adapter in planar mode
    SEQ_OUT(SEQ_MAPMASK, 0x04);     // SEQ: select plane 2 for writing
    SEQ_OUT(SEQ_MEMMODE, 0x06);     // SEQ: odd/even off
    GCR_OUT(GCR_READMAP_SEL, 0x02); // GCR: select plane 2 for reading
    GCR_OUT(GCR_GFX_MODE, 0x00);    // GCR: write mode 0, odd/even off
    GCR_OUT(GCR_MISC, 0x04);        // GCR: CPU memory window A0000-AFFFF

    // Copy fonts in VGA memory plane 2
    for(int map=from_map; map<8; map++) {
        for(int i=0; i<256; i++) {
            memcpy((uint8_t*)(0xa0000 + map_offset[map] + i*32), font+(size*map), size);
        }
    }

    // Restore registers
    SEQ_OUT(SEQ_MAPMASK, seq_map_mask);
    SEQ_OUT(SEQ_MEMMODE, seq_mem_mode);
    GCR_OUT(GCR_READMAP_SEL, gcr_read_map_select);
    GCR_OUT(GCR_GFX_MODE, gcr_gfx_mode);
    GCR_OUT(GCR_MISC, gcr_misc);
}

void TextScreen::setMode(int bios, int cols, int rows, int boxw, int boxh)
{
    int scanlines = boxh*rows;
    if(scanlines<200) {
        scanlines = 200;
    } else if(scanlines>200 && scanlines<350) {
        scanlines = 350;
    } else if(scanlines>350) {
        scanlines = 400;
    }
    setScanlines(scanlines);
    setBIOSMode(bios);
    uint8_t seq_clocking;
    //req'd for 8 pixel wide boxes?
    //SEQ_IN(SEQ_CLOCKING, seq_clocking);
    //seq_clocking &= ~0x01;
    //seq_clocking |= 0x01;
    //SEQ_OUT(SEQ_CLOCKING, seq_clocking);
    int resw = cols*boxw;
    static char buf[81];
    if(rows == 25) {
        setBIOSFont(1, boxh);
        snprintf(buf, 81, "Mode %d %dx%d %dx%d %dx%d", bios, cols, rows, boxw, boxh, resw, scanlines);
    } else {
        setBIOSFont(0, boxh, false);
        setBIOSFont(1, boxh, true);
        snprintf(buf, 81, "%dx%d %dx%d %dx%d", cols, rows, boxw, boxh, resw, scanlines);
    }
    setCustomFonts(2, boxh);
    m_cols = cols;
    m_rows = rows;
    m_modeName = buf;
    switch(bios) {
        case 7:
            m_textPage = ((char *)0xb0000);
            break;
        default:
            m_textPage = ((char *)0xb8000);
            break;
    }
}

void TextScreen::setMode_b40x25_9x16_01h()
{
    setMode(0x01, 40, 25, 9, 16);
}

void TextScreen::setMode_b80x25_9x16_03h()
{
    setMode(0x03, 80, 25, 9, 16);
}

void TextScreen::setMode_b80x25_9x16_07h()
{
    setMode(0x07, 80, 25, 9, 16);
}

void TextScreen::setMode_640x480(int boxh)
{
    int textlines = 480 / boxh;

    setScanlines(350);
    setBIOSMode(0x03);

    const int16_t crtc[] = {
        0x0c11, // Vertical Retrace End (unlock regs. 0-7)
        0x0d06, // Vertical Total
        0x3e07, // Overflow
        0xea10, // Vertical Retrace Start
        0x8c11, // Vertical Retrace End (lock regs. 0-7)
        0xdf12, // Vertical Display Enable End
        0xe715, // Start Vertical Blanking
        0x0616, // End Vertical Blanking
        -1
    };
    setVGARegisters(CRTC_ADDR_COL, &crtc[0]);

    outp(CRTC_ADDR_COL, 0x09);
    uint8_t max_scanline = inp(CRTC_DATA_COL);
    max_scanline &= ~0x1F;
    max_scanline |= (boxh-1);
    outp(CRTC_DATA_COL, max_scanline);

    uint8_t mor = inp(MOR_READ);
    mor &= 0x33;
    mor |= 0xC4;
    outp(MOR_ADDR, mor);

    *(uint16_t*)(0x400+0x4c) = 8192;        // Change page size in bytes
    *(uint8_t*)(0x400+0x84) = textlines-1;  // Change page number of lines (less 1)

    // Select Alternate Print Screen Handler
    /* On older PCs, XTs, and ATs, the default ROM-BIOS print-screen
          handler stops printing after 25 lines.  The EGA/VGA handler
          correctly prints the number of lines specified in the byte at
          0040:0084 in the BIOS Data Area.
     * Most EGAs and VGAs set this automatically.  However, it is good
          practice to use this fn whenever you change the number of video
          lines that are displayed in text mode.
     */
    union REGS rg;
    rg.h.ah = 0x12;
    rg.h.bl = 0x20;
    int386(0x10, &rg, &rg);

    setBIOSFont(0, boxh);
    setBIOSFont(1, boxh);
    setCustomFonts(2, boxh);

    m_textPage = ((char *)0xb8000);
    m_cols = 80;
    m_rows = textlines;

    static char buf[81];
    snprintf(buf, 81, "80x%d 8x%d 640x480", textlines, boxh);
    m_modeName = buf;
}

