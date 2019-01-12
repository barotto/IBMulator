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

#ifndef _TEXTSCREEN_H_
#define _TEXTSCREEN_H_

#include "common.h"

#define DEFAULT_FG_COL  c_lgray
#define DEFAULT_BG_COL  c_blue


class TextScreen
{
private:
    int m_curRow;
    int m_curCol;
    int m_prevCol;
    int m_curFgColor;
    int m_curBgColor;
    bool m_moved;
    int m_cols;
    int m_rows;
    char* m_textPage;

    uint8_t m_error;
    char *m_modeName;           // printable name of the current mode

    void setMode(int bios, int cols, int rows, int boxw, int boxh);
    void setMode_b40x25_9x16_01h();
    void setMode_b80x25_9x16_03h();
    void setMode_b80x25_9x16_07h();
    void setMode_640x480(int boxh);

    void (TextScreen::*m_resetModeFn)();

public:
    TextScreen();
    ~TextScreen();

    inline int16_t error() const   { return m_error; }
    inline int cols() const      { return m_cols; }
    inline int rows() const      { return m_rows; }
    inline const char * modeName() const { return m_modeName; }

    void setMode(int16_t newMode);
    void resetMode();

    int setRow(int row);
    int setCol(int col);
    void setPos(int row, int col);
    inline int getRow() const { return m_curRow; }
    inline int getCol() const { return m_curCol; }
    void getPos(int &row, int &col);

    void setColor(uint8_t fg);
    void setColor(uint8_t fg, uint8_t bg);

    void erasePage();
    void erasePage(uint8_t fg, uint8_t bg);

    void moveCursor();
    void moveCursor(int row, int col);
    void moveCursor(int row, int col, uint8_t fg, uint8_t bg);

    void write(char ch);
    void write(const char *text);
    void write(const char *text, uint8_t fg);
    void write(const char *text, uint8_t fg, uint8_t bg);
    void write(int row, int col, const char *text, uint8_t fg, uint8_t bg);

    inline TextScreen& operator()(char ch)          { write(ch); return *this; }
    inline TextScreen& operator()(int row, int col) { setPos(row,col); return *this; }
    inline TextScreen& operator()(const char *text) { write(text); return *this; }
    inline TextScreen& operator()(const char *text, uint8_t fg) { write(text,fg); return *this; }

    TextScreen& drawBox(int16_t row, int16_t col, int16_t width, int16_t height);
    TextScreen& drawBox(int16_t row, int16_t col, int16_t width, int16_t height, uint8_t fg);
    TextScreen& drawBox(int16_t row, int16_t col, int16_t width, int16_t height, uint8_t fg, uint8_t bg);
};

#endif
