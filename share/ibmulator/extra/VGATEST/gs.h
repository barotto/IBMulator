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

#ifndef _GFXSCREEN_H_
#define _GFXSCREEN_H_

#include "common.h"


typedef struct {
    uint16_t red;
    uint16_t green;
    uint16_t blue;
} s_color;


class GfxScreen
{
private:
    uint8_t m_error;          // the last error
    uint8_t m_origMode;       // the original video mode
    uint8_t far *m_fontAddr;  // address of the current font
    uint8_t *m_activeOffset;  // address of the active page
    int16_t m_maxx;           // maximum x coord
    int16_t m_maxy;           // maximum y coord
    int16_t m_width;          // the width in pixels
    int16_t m_height;         // the height in pixels
    int16_t m_pages;          // the number of graphic pages
    int32_t m_lineSize;       // offset to next scan line
    int32_t m_pageSize;       // offset to a page
    bool  m_chained;          // 1=planes are chained (linear buffer)
    char *m_modeName;         // printable name of the current mode
    int16_t m_colors;
    int16_t m_crtc_addr;
    int16_t m_isr1_addr;
    uint8_t m_cval[256];
    uint8_t m_cmap[256];

    void (GfxScreen::*m_clearFn)(int row, int lines, uint8_t color);
    void (GfxScreen::*m_putPixelFn)(int16_t x, int16_t y, uint8_t color);
    int16_t (GfxScreen::*m_getPixelFn)(int16_t x, int16_t y);
    void (GfxScreen::*m_drawTextFn)(int16_t x, int16_t y, uint8_t color, const char *string);

public:
    GfxScreen();
    ~GfxScreen();

    inline int16_t error()    { return m_error; }
    inline int16_t maxx()     { return m_maxx; }
    inline int16_t maxy()     { return m_maxy; }
    inline int16_t width()    { return m_width; }
    inline int16_t height()   { return m_height; }
    inline int16_t pages()    { return m_pages; }
    inline int32_t lineSize() { return m_lineSize; }
    inline int32_t pageSize() { return m_pageSize; }
    inline bool  chained()    { return m_chained; }
    inline char *modeName()   { return m_modeName; }
    inline int16_t colors()   { return m_colors; }

    inline uint8_t color(uint8_t cname) const { return m_cval[m_cmap[cname]]; }
    inline uint8_t palidx(uint8_t color) const { return m_cval[color]; }

    void setMode(int16_t newMode);
    void resetMode();

    void clear(uint8_t color);
    inline void clear(int row, int lines, uint8_t color) { (*this.*m_clearFn)(row, lines, color); }

    void setActivePage(uint8_t page);
    void setVisiblePage(uint8_t page);

    inline void putPixel(int16_t x, int16_t y, uint8_t color) { (*this.*m_putPixelFn)(x,y,color); }
    inline int16_t getPixel(int16_t x, int16_t y) { return (*this.*m_getPixelFn)(x,y); }

    void drawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t color);
    void drawCircle(int16_t cx, int16_t cy, int16_t r, uint8_t color);
    void drawRectangle(int16_t x, int16_t y, int16_t width, int16_t height, uint8_t color);
    void fillRect8(int16_t x, int16_t y, int16_t width, int16_t height, uint8_t color);
    void fillRect8chained(int16_t x, int16_t y, int16_t width, int16_t height, uint8_t color);

    inline void drawText(int16_t x, int16_t y, uint8_t color, const char *string) {
        (*this.*m_drawTextFn)(x,y,color,string);
    }

    void setColor256(int16_t index, uint16_t r, uint16_t g, uint16_t b);
    void setPalette256(int16_t start, int16_t count, s_color *p);

    void vsync();

private:
    // routines to set the modes
    // BIOS
    void mode_b320x200_0Dh();
    void mode_b640x200_0Eh();
    void mode_b640x350_0Fh();
    void mode_b640x350_10h();
    void mode_b640x480_12h();
    void mode_b320x200_13h();
    // Tweaked
    void mode_t160x120();
    void mode_t256x256_Q();
    void mode_t296x220();
    void mode_t320x200_Y();
    void mode_t320x240_X();
    void mode_t320x400();
    void mode_t360x270();
    void mode_t360x360();
    void mode_t360x480();
    void mode_t400x300();

    void clear4(int row, int lines, uint8_t color);
    void clear8(int row, int lines, uint8_t color);
    void clear8chained(int row, int lines, uint8_t color);

    void putPixel4(int16_t x, int16_t y, uint8_t color);
    void putPixel8(int16_t x, int16_t y, uint8_t color);
    void putPixel8chained(int16_t x, int16_t y, uint8_t color);

    int16_t getPixel4(int16_t x, int16_t y);
    int16_t getPixel8(int16_t x, int16_t y);
    int16_t getPixel8chained(int16_t x, int16_t y);

    void setPlanarRWMode(int rmode, int wmode);

    void drawChar8(int16_t x, int16_t y, uint8_t color, char c);
    void drawChar4(int16_t x, int16_t y, uint8_t color, char c);
    void drawText8(int16_t x, int16_t y, uint8_t color, const char *string);
    void drawText4(int16_t x, int16_t y, uint8_t color, const char *string);

    void setStdVGAColorMap();
};

#endif
