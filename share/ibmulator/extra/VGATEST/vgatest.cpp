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

// Use wmake to compile with OpenWatcom.

#include <time.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "utils.h"
#include "gs.h"
#include "ts.h"

GfxScreen gfx;
TextScreen text;


//---------------------------------------------------
//
// Text Mode demo
//

void drawCharMap(int r, int c, int color)
{
    int ch = 0;
    int w = 16;
    int h = 16;
    char buf[2];
    for(int i=0; i<16; i++) {
        snprintf(buf, 2, "%x", i);
        text(r,c+2+i)(buf);
        text(r+1,c+2+i)("-");
        text(r+2+i,c)(buf);
        text(r+2+i,c+1)(":");
    }

    text.setColor(color);
    for(int y=r; (y<r+h && ch<256); y++) {
        for(int x=c; (x<c+w && ch<256); x++,ch++) {
            //text.setColor(((y-r)%8)+8);
            text(y+2,x+2)(ch);
        }
    }
}

void demoText()
{
    char buf[4];

    text.setColor(c_brown);
    // columns axis
    for(int i=0; i<text.cols(); i++) {
        snprintf(buf, 2, "%d", i%10);
        text(0,i)(buf);
    }
    // rows axis
    for(int i=0; i<text.rows(); i++) {
        snprintf(buf, 2, "%d", i%10);
        text(i,0)(buf);
    }

    int ro = 1;
    int co = 1;

    // write the mode name
    text(ro++,co)(text.modeName(),c_lgray);

    int w = 16;
    int h = 16;

    // print Map A characters
    text.setColor(c_lgray);
    text.drawBox(ro, co, w+1+2, h+1+2)("Map A");
    int mapselR = text.getRow();
    int mapselC = text.getCol()+1;
    drawCharMap(ro+1, co+1, c_white);

    // print Map B characters
    text.setColor(c_lgray);
    text.drawBox(ro, co+3+(text.cols()==40?0:1)+w, w+1+2, h+1+2)("Map B");
    drawCharMap(ro+1, co+4+(text.cols()==40?0:1)+w, c_cyan);

    // print Background and Foreground combinations
    text.setColor(c_lgray);
    text.drawBox(ro+h+4, co, 17, 2)("Background");
    text.drawBox(ro+h+4, co+17+1, 17, 2)("Foreground");
    for(int i=0; i<16; i++) {
        text.setColor(c_white,i);
        text(ro+h+5,co+i+1)(7);//176);
        text.setColor(i,c_black);
        text(ro+h+5,co+i+3+16)(0xdb);//8);//178);
    }

    text.setColor(c_lgray,c_black);

    bool exit = false;
    int mapA = 0;
    int mapAsel = 0;
    int mapCount = 8;
    snprintf(buf, 2, "%d", mapA);
    text(mapselR,mapselC)(buf);

    while(!exit) {
        char ch = getch();
        switch(ch) {
            case k_ESC:
                exit = true;
                break;
            case k_SPACE: {
                // switch between fonts
                mapA = (mapA+1) % mapCount;
                if(mapA > 3) {
                    mapAsel = ((mapA&3) << 2) | 0x20;
                } else {
                    mapAsel = (mapA << 2);
                }
                //see Int 10/AX=1103h
                //union REGS rg;
                //rg.h.ah = 0x11;
                //rg.h.al = 0x03;
                //rg.h.bl = mapAsel;
                //int386(0x10, &rg, &rg);
                SEQ_OUT(3, mapAsel);
                snprintf(buf, 2, "%d", mapA);
                text(mapselR,mapselC)(buf);
                break;
            }
            default:
                break;
        }
    }
}

//---------------------------------------------------
//
// A real can of worms.
//
// Draw a bunch of worms that crawl around the screen.
// Code by Robert C. Pendleton

#define worms (100)
#define segments (50)

typedef struct
{
    int x, y;
} point;

typedef struct
{
    int head;
    int color;
    int heading;
    point body[segments];
} worm;

static point headings[] =
{
    { 0, 1},
    { 1, 1},
    { 1, 0},
    { 1,-1},
    { 0,-1},
    {-1,-1},
    {-1, 0},
    {-1, 1}
};

void demoWorm()
{
    worm can[worms];
    int head;
    int tail;
    int i, j, k;
    int x, y;
    int next;
    uint8_t bg = gfx.color(c_black);

    gfx.setVisiblePage(0);
    gfx.setActivePage(0);
    gfx.clear(bg);

    fsrand((unsigned) time(NULL));

    for (i = 0; i < worms; i++) {
        can[i].head = 0;
        can[i].heading = i & 0x7;
        can[i].color = gfx.color((i & 0x7f) + 32);
        for (j = 0; j < segments; j++) {
            can[i].body[j].x = gfx.maxx() / 2;
            can[i].body[j].y = gfx.maxy() / 2;
        }
    }

    while (!kbhit()) {
        for (i = 0; i < worms; i++) {
            tail = head = can[i].head;
            head++;
            if (head >= segments) {
                head = 0;
            }
            can[i].head = head;

            gfx.putPixel(can[i].body[head].x,
                         can[i].body[head].y,
                         bg);

            if (frand() < (FRAND_MAX / 10)) {
                if (frand() < (FRAND_MAX / 2)) {
                    can[i].heading = (can[i].heading + 1) & 0x7;
                } else {
                    can[i].heading = (can[i].heading - 1) & 0x7;
                }
            }

            x = can[i].body[tail].x;
            y = can[i].body[tail].y;

            x = x + headings[can[i].heading].x;
            y = y + headings[can[i].heading].y;

            next = gfx.getPixel(x, y);
            k = 0;
            while (next != -1 &&
                   next != bg &&
                   next != can[i].color &&
                   k < 2)
            {
                can[i].heading = frand() & 0x7;

                x = can[i].body[tail].x;
                y = can[i].body[tail].y;

                x = x + headings[can[i].heading].x;
                y = y + headings[can[i].heading].y;

                next = gfx.getPixel(x, y);
                k++;
            }

            if (x < 0) {
                x = gfx.maxx();
            }

            if (x > gfx.maxx()) {
                x = 0;
            }

            if (y < 0) {
                y = gfx.maxy();
            }

            if (y > gfx.maxy()) {
                y = 0;
            }

            can[i].body[head].x = x;
            can[i].body[head].y = y;

            gfx.putPixel(x, y, can[i].color);
        }

        gfx.drawText(8, 8, gfx.color(c_white), gfx.modeName());
    }
    getch();
}


//---------------------------------------------------
//
// Demo of line drawing. Gives a feel for the effects
// of different pixel sizes.
// Code by Robert C. Pendleton

void demoLine()
{
    int activePage = 1;
    int x, y;
    int red = gfx.color(c_red);
    int green = gfx.color(c_green);

    gfx.setVisiblePage(0);

    while (!kbhit()) {
        x = 0;
        while (!kbhit() && x < gfx.maxx()) {
            gfx.setActivePage(activePage);
            gfx.clear(0);

            gfx.drawLine(x, 0, gfx.maxx() - x, gfx.maxy(), red);
            gfx.drawLine(gfx.maxx() - x, 0, x, gfx.maxy(), green);

            gfx.drawText(8, 8, gfx.color(c_white), gfx.modeName());
            gfx.setVisiblePage(activePage);

            activePage++;

            x++;
        }

        y = 0;
        while (!kbhit() && y < gfx.maxy()) {
            gfx.setActivePage(activePage);
            gfx.clear(0);

            gfx.drawLine(gfx.maxx(), y, 0, gfx.maxy() - y, red);
            gfx.drawLine(gfx.maxx(), gfx.maxy() - y, 0, y, green);

            gfx.drawText(8, 8, gfx.color(c_white), gfx.modeName());
            gfx.setVisiblePage(activePage);

            activePage++;

            y++;
        }
    }

    getch();
}

//---------------------------------------------------
//
// Demo of circles drawing.
// Useful to check aspect ratio.
//
void demoCircle()
{
    gfx.setActivePage(0);
    gfx.clear(0);
    gfx.setVisiblePage(0);

    int radius = 0;
    if(gfx.width() < gfx.height()) {
        radius = gfx.width()/2;
    } else {
        radius = gfx.height()/2;
    }
    int cx = gfx.width()/2 - 1;
    int cy = gfx.height()/2 - 1;

    uint8_t color = 0;
    while(!kbhit()) {
        for(int i=1; i<10; i++) {
            gfx.drawCircle(cx, cy, radius/i, color+i);
        }
        color++;
        gfx.drawText(8, 8, gfx.color(c_white), gfx.modeName());
    }
    getch();
}

//---------------------------------------------------
//
// Draw the color palette.
//
void demoPalette()
{
    gfx.setActivePage(0);
    gfx.clear(0);
    gfx.setVisiblePage(0);

    int16_t c = gfx.colors();
    //char buf[4];
    //memset(buf, 0, 4);
    if(c <= 16) {
        for(int i=0; i<c; i++) {
            gfx.clear(gfx.height()/c * i, gfx.height()/c, gfx.palidx(i));
        }
        /*
        for(int i=0; i<c; i++) {
            int x = 0;
            int y = screen.height()/c * i;
            int c = screen.getPixel(x, y);
            sprintf(buf, "%d", c);
            screen.drawText(x, y, c_white, buf);
        }
        */
    } else {
        // 256 colors
        int w = gfx.width()/16;
        int h = gfx.height()/16;
        for(int y=0; y<16; y++) {
            for(int x=0; x<16; x++) {
                if(gfx.chained()) {
                    gfx.fillRect8chained(x*w, y*h, w, h, y*16+x);
                } else {
                    gfx.fillRect8(x*w, y*h, w, h, y*16+x);
                }
            }
        }
    }

    gfx.drawRectangle(0, 0, gfx.width(), gfx.height(), gfx.color(c_white));
    gfx.drawText(8, 8, gfx.color(c_white), gfx.modeName());

    while(!kbhit()) {}
    getch();
}


//---------------------------------------------------
// MAIN
//---------------------------------------------------
int getHexFromKeyb(int row, int col)
{
    int mode1 = -1;
    int mode2 = -1;
    int mode = -1;

    // first digit
    text.moveCursor(row, col);
    mode1 = getche();
    if(mode1 == k_ESC) {
        return 0xFFFF;
    }
    if(mode1 >= 'a') {
        mode1 = (mode1-'a')+10;
    } else {
        mode1 -= '0';
    }

    // second digit
    text.moveCursor(row, col+1);
    mode2 = getche();
    if(mode2 == k_ESC) {
        return 0xFFFF;
    }
    if(mode2 >= 'a') {
        mode2 = (mode2-'a')+10;
    } else {
        mode2 -= '0';
    }

    mode = mode1*16 + mode2;

    return mode;
}

int main(int argc, char *argv[])
{
    if (gfx.error() != e_none) {
        if(gfx.error() == e_notVgaDisplay) {
            printf("This is not a VGA compatible display.\n");
        } else {
            printf("An error occurred.\n");
        }
        exit(1);
    }

    int mode = 0;
    char demo = 0;
    uint8_t a;

    while(true) {
        text.erasePage(DEFAULT_FG_COL, DEFAULT_BG_COL);

        text(2,28)("-", c_dgray)("-", c_lgray)("-", c_white)
                  (" VGA modes test ", c_white)
                  ("-", c_white)("-", c_lgray)("-", c_dgray);

        text(4,33);
        text("Quit    [q]\n", DEFAULT_FG_COL);
        text("Text    [t]\n");
        text("Circles [c]\n");
        text("Lines   [l]\n");
        text("Palette [p]\n");
        text("Worms   [w]\n");
        text(text.getRow()+1, 33);
        text("Which Test?");
        int p1row, p1col;
        text.getPos(p1row, p1col);

        demo = ' ';
        while(NULL == memchr("twclpqTWCLPQ", demo, 8)) {
            text.moveCursor(p1row, p1col);
            demo = getche();
            if(demo == k_ESC) {
                demo = 'q';
                break;
            }
        }

        if(demo == 'q' || demo == 'Q') {
            break;
        }

        if(demo == 't' || demo == 'T') {
            int modesrow = text.setRow(text.getRow()+2);
            int askrow = modesrow;

            text(modesrow, 16)("BIOS modes\n");
            text("0,1   40x25 8x8  [01]\n");
            text("0*,1* 40x25 8x14 [a1]\n");
            text("0+,1+ 40x25 9x16 [c1]\n");
            text("2,3   80x25 8x8  [03]\n");
            text("2*,3* 80x25 8x14 [a3]\n");
            text("2+,3+ 80x25 9x16 [c3]\n");
            text("7     80x25 9x14 [07]\n");
            text("7+    80x25 9x16 [a7]\n");
            askrow = (text.getRow()>askrow)?text.getRow():askrow;

            text(modesrow, 42)("Tweaked modes\n");
            text("80x43 8x8  [1a]\n");
            text("80x50 9x8  [1b]\n");
            text("80x28 9x14 [1c]\n");
            text("80x30 8x16 [1d]\n");
            text("80x34 8x14 [1e]\n");
            text("80x60 8x8  [1f]\n");
            askrow = (text.getRow()>askrow)?text.getRow():askrow;

            text(askrow+1, 33);
            text("Which Mode?");

            int p2row, p2col;
            text.getPos(p2row, p2col);

            do {
                int mode = getHexFromKeyb(p2row, p2col);
                if(mode == 0xFFFF) {
                    demo = 'q';
                    break;
                }
                text.setMode(mode);
            } while(text.error());

            switch(demo)
            {
            case 'q':
                break;
            case 't':
            case 'T':
                demoText();
                break;
            }

            text.resetMode();

        } else {
            int modesrow = text.setRow(text.getRow()+2);
            text(modesrow, 6)("BIOS modes\n");
            text(" Dh 320x200 [0d]\n");
            text(" Eh 640x200 [0e]\n");
            text(" Fh 640x350 [0f]\n");
            text("10h 640x350 [10]\n");
            text("12h 640x480 [12]\n");
            text("13h 320x200 [13]\n");

            text(modesrow, 28)("Tweaked 256-color modes\n");
            text("* 160x120 planar [14]\n");
            text("Q 256x256 chain4 [15]\n");
            text("  296x220 planar [16]\n");
            text("Y 320x200 planar [17]\n");
            text("X 320x240 planar [18]\n");
            text("  320x400 planar [19]\n");

            text(modesrow+1, 50);
            text("  360x270 planar [1a]\n");
            text("  360x360 planar [1b]\n");
            text("  360x480 planar [1c]\n");
            text("* 400x300 planar [1d]\n");

            text(text.getRow()+3, 33);
            text("Which Mode?");
            int p2row, p2col;
            text.getPos(p2row, p2col);

            text(23,50)("* = multisync monitor req.");

            int mode1 = -1;
            int mode2 = -1;
            int mode = -1;
            do {
                int mode = getHexFromKeyb(p2row, p2col);
                if(mode == 0xFFFF) {
                    demo = 'q';
                    break;
                }
                gfx.setMode(mode);
            } while(gfx.error());

            switch(demo)
            {
            case 'q':
                break;
            case 'w':
            case 'W':
                demoWorm();
                break;

            case 'c':
            case 'C':
                demoCircle();
                break;

            case 'l':
            case 'L':
                demoLine();
                break;

            case 'p':
            case 'P':
                demoPalette();
                break;
            }

            gfx.resetMode();
        }
    }

    text.erasePage(c_lgray, c_black);

    return 0;
}
