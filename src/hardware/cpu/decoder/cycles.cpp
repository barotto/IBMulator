/*
 * Copyright (C) 2016  Marco Bortolin
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

#include "hardware/cpu/decoder.h"

                                       /* base    memop   ex.  rep    base_rep pmode   noj    bu  */
#define cBase(_base_)                  { _base_ , _base_ , 0 ,   0   , _base_ ,  0   ,  0   ,  0   }
#define cBaseMem(_base_,_mem_)         { _base_ , _mem_  , 0 ,   0   , _base_ ,  0   ,  0   ,  0   }
#define cBaseRep(_base_,_brep_,_rep_)  { _base_ , _base_ , 0 , _rep_ , _brep_ ,  0   ,  0   ,  0   }
#define cBasePM(_base_,_pm_)           { _base_ , _base_ , 0 ,   0   , _base_ , _pm_ ,  0   ,  0   }
#define cBaseBU(_base_,_bu_)           { _base_ , _base_ , 0 ,   0   , _base_ ,  0   ,  0   , _bu_ }
#define cBaseNoJ(_base_,_nj_)          { _base_ , _base_ , 0 ,   0   , _base_ ,  0   , _nj_ ,  0   }
#define cBaseMemPM(_base_,_mem_,_pm_)  { _base_ , _mem_  , 0 ,   0   , _base_ , _pm_ ,  0   ,  0   }

#define cNull { 0,0,0,0,0,0,0,0 }
#define cNullBlock3 cNull,cNull, cNull,cNull, cNull,cNull, cNull,cNull
#define cNullBlock7 cNullBlock3,cNullBlock3
#define cNullBlockF cNullBlock7,cNullBlock7

//should be 7, but i try to compensate the 2 decode cycles when the pq is invalid
#define JMPC 6
#define LOOPC 7

/* Cycles are reported in the comments as in the Intel's docs, and then adjusted
 * for the program. Memory I/O related cycles are subtracted and incorrect
 * values are corrected through direct HW measurements. Some are just guesswork.
 *
 * Jumps/calls to gates and special segments are not taken into consideration.
 * In those cases cycles are Real Mode + PM penalty, so it can be faster than
 * real hw. Memory I/O is always counted tho, so the difference should be
 * minimal. The same for RETs and INTs.
 *
 * Note: when the destination operand is memory, the cycles reported by the docs
 * are discounted by a couple ticks. That's because when the EU sends the data
 * to the BU, the EU is free to execute the next instruction, even if the
 * current is not actually completed (the data is still in transit). This is the
 * effect of the quasi-pipelined nature of the 286.
 *
 * The 286 needs 2 cycles to access the memory (excluding any other additional
 * cost like wait-states.) The 386 seems to generally need the same amount, but
 * just 1 cycle in some cases. More details in Abrash's Black Book, CH11.
 */

// prefix none
static const Cycles cycles_none[256*2] = {
//              docs:286         386        hw:286               386
/* 00 ADD eb,rb      2/7         2/7        */ cBaseMem(2,5),    cBaseMem(2,5),
/* 01 ADD ew,rw      2/7         2/7        */ cBaseMem(2,8),    cBaseMem(2,8),
/* 02 ADD rb,eb      2/7         2/6        */ cBaseMem(2,5),    cBaseMem(2,4),
/* 03 ADD rw,ew      2/7         2/6        */ cBaseMem(2,5),    cBaseMem(2,4),
/* 04 ADD AL,ib      3           2          */ cBase(3),         cBase(2),
/* 05 ADD AX,iw      3           2          */ cBase(3),         cBase(2),
/* 06 PUSH ES        3           2          */ cBase(3),         cBase(2),
// 8 descriptor fetch + 2 stack pop = 10 cycles for mem ops
// 20 by intel docs - 10 memory operations = 10 cycles for the instruction exec
// 10 pmode - 3 rmode  = 7 cycles of penalty
/* 07 POP ES         5,p20       7,p21      */ cBasePM(3,7),      cBasePM(5,8),
/* 08 OR eb,rb       2/7         2/6        */ cBaseMem(2,5),     cBaseMem(2,4), //<-- is this correct?
/* 09 OR ew,rw       2/7         2/6        */ cBaseMem(2,8),     cBaseMem(2,7),
/* 0A OR rb,eb       2/7         2/7        */ cBaseMem(2,5),     cBaseMem(2,5),
/* 0B OR rw,ew       2/7         2/7        */ cBaseMem(2,5),     cBaseMem(2,5),
/* 0C OR AL,ib       3           2          */ cBase(3),          cBase(2),
/* 0D OR AX,iw       3           2          */ cBase(3),          cBase(2),
/* 0E PUSH CS        3           2          */ cBase(3),          cBase(2),
/* 0F 2-byte opcode                         */ cNull,             cNull,
/* 10 ADC eb,rb      2/7         2/7        */ cBaseMem(2,5),     cBaseMem(2,5), //<-- is this correct?
/* 11 ADC ew,rw      2/7         2/7        */ cBaseMem(2,8),     cBaseMem(2,8),
/* 12 ADC rb,eb      2/7         2/6        */ cBaseMem(2,5),     cBaseMem(2,4),
/* 13 ADC rw,ew      2/7         2/6        */ cBaseMem(2,5),     cBaseMem(2,4),
/* 14 ADC AL,ib      3           2          */ cBase(3),          cBase(2),
/* 15 ADC AX,iw      3           2          */ cBase(3),          cBase(2),
/* 16 PUSH SS        3           2          */ cBase(3),          cBase(2),
/* 17 POP SS         5,p20       7,p21      */ cBasePM(3,7),      cBasePM(5,8),
/* 18 SBB eb,rb      2/7         2/6        */ cBaseMem(2,8),     cBaseMem(2,7),
/* 19 SBB ew,rw      2/7         2/6        */ cBaseMem(2,8),     cBaseMem(2,7),
/* 1A SBB rb,eb      2/7         2/7        */ cBaseMem(2,5),     cBaseMem(2,5),
/* 1B SBB rw,ew      2/7         2/7        */ cBaseMem(2,5),     cBaseMem(2,5),
/* 1C SBB AL,ib      3           2          */ cBase(3),          cBase(2),
/* 1D SBB AX,iw      3           2          */ cBase(3),          cBase(2),
/* 1E PUSH DS        3           2          */ cBase(3),          cBase(2),
/* 1F POP DS         5,p20       7,p21      */ cBasePM(3,7),      cBasePM(5,8),
/* 20 AND eb,rb      2/7         2/7        */ cBaseMem(2,8),     cBaseMem(2,8),
/* 21 AND ew,rw      2/7         2/7        */ cBaseMem(2,8),     cBaseMem(2,8),
/* 22 AND rb,eb      2/7         2/6        */ cBaseMem(2,5),     cBaseMem(2,4),
/* 23 AND rw,ew      2/7         2/6        */ cBaseMem(2,5),     cBaseMem(2,4),
/* 24 AND AL,ib      3           2          */ cBase(3),          cBase(2),
/* 25 AND AX,iw      3           2          */ cBase(3),          cBase(2),
/* 26 seg ovr (ES)                          */ cNull,             cNull,
/* 27 DAA            3           4          */ cBase(3),          cBase(4),
/* 28 SUB eb,rb      2/7         2/6        */ cBaseMem(2,8),     cBaseMem(2,7),
/* 29 SUB ew,rw      2/7         2/6        */ cBaseMem(2,8),     cBaseMem(2,7),
/* 2A SUB rb,eb      2/7         2/7        */ cBaseMem(2,5),     cBaseMem(2,5),
/* 2B SUB rw,ew      2/7         2/7        */ cBaseMem(2,5),     cBaseMem(2,5),
/* 2C SUB AL,ib      3           2          */ cBase(3),          cBase(2),
/* 2D SUB AX,iw      3           2          */ cBase(3),          cBase(2),
/* 2E seg ovr (CS)                          */ cNull,             cNull,
/* 2F DAS            3           4          */ cBase(3),          cBase(4),
/* 30 XOR eb,rb      2/7         2/6        */ cBaseMem(2,8),     cBaseMem(2,7),
/* 31 XOR ew,rw      2/7         2/6        */ cBaseMem(2,8),     cBaseMem(2,7),
/* 32 XOR rb,eb      2/7         2/7        */ cBaseMem(2,5),     cBaseMem(2,5),
/* 33 XOR rw,ew      2/7         2/7        */ cBaseMem(2,5),     cBaseMem(2,5),
/* 34 XOR AL,ib      3           2          */ cBase(3),          cBase(2),
/* 35 XOR AX,iw      3           2          */ cBase(3),          cBase(2),
/* 36 seg ovr (SS)                          */ cNull,             cNull,
/* 37 AAA            3           4          */ cBase(3),          cBase(4),
/* 38 CMP eb,rb      2/7         2/5        */ cBaseMem(2,5),     cBaseMem(2,3), //<-- is this correct?
/* 39 CMP ew,rw      2/7         2/5        */ cBaseMem(2,5),     cBaseMem(2,3), //<-- is this correct?
/* 3A CMP rb,eb      2/6         2/6        */ cBaseMem(2,4),     cBaseMem(2,4),
/* 3B CMP rw,ew      2/6         2/6        */ cBaseMem(2,4),     cBaseMem(2,4),
/* 3C CMP AL,ib      3           2          */ cBase(3),          cBase(2),
/* 3D CMP AX,iw      3           2          */ cBase(3),          cBase(2),
/* 3E seg ovr (DS)                          */ cNull,             cNull,
/* 3F AAS            3           4          */ cBase(3),          cBase(4),
/* 40 INC AX         2           2          */ cBase(2),          cBase(2),
/* 41 INC CX         2           2          */ cBase(2),          cBase(2),
/* 42 INC DX         2           2          */ cBase(2),          cBase(2),
/* 43 INC BX         2           2          */ cBase(2),          cBase(2),
/* 44 INC SP         2           2          */ cBase(2),          cBase(2),
/* 45 INC BP         2           2          */ cBase(2),          cBase(2),
/* 46 INC SI         2           2          */ cBase(2),          cBase(2),
/* 47 INC DI         2           2          */ cBase(2),          cBase(2),
/* 48 DEC AX         2           2          */ cBase(2),          cBase(2),
/* 49 DEC CX         2           2          */ cBase(2),          cBase(2),
/* 4A DEC DX         2           2          */ cBase(2),          cBase(2),
/* 4B DEC BX         2           2          */ cBase(2),          cBase(2),
/* 4C DEC SP         2           2          */ cBase(2),          cBase(2),
/* 4D DEC BP         2           2          */ cBase(2),          cBase(2),
/* 4E DEC SI         2           2          */ cBase(2),          cBase(2),
/* 4F DEC DI         2           2          */ cBase(2),          cBase(2),
/* 50 PUSH AX        3           2          */ cBase(3),          cBase(2),
/* 51 PUSH CX        3           2          */ cBase(3),          cBase(2),
/* 52 PUSH DX        3           2          */ cBase(3),          cBase(2),
/* 53 PUSH BX        3           2          */ cBase(3),          cBase(2),
/* 54 PUSH SP        3           2          */ cBase(3),          cBase(2),
/* 55 PUSH BP        3           2          */ cBase(3),          cBase(2),
/* 56 PUSH SI        3           2          */ cBase(3),          cBase(2),
/* 57 PUSH DI        3           2          */ cBase(3),          cBase(2),
/* 58 POP AX         5           4          */ cBaseBU(3,-3),     cBaseBU(2,-2),
/* 59 POP CX         5           4          */ cBaseBU(3,-3),     cBaseBU(2,-2),
/* 5A POP DX         5           4          */ cBaseBU(3,-3),     cBaseBU(2,-2),
/* 5B POP BX         5           4          */ cBaseBU(3,-3),     cBaseBU(2,-2),
/* 5C POP SP         5           4          */ cBaseBU(3,-3),     cBaseBU(2,-2),
/* 5D POP BP         5           4          */ cBaseBU(3,-3),     cBaseBU(2,-2),
/* 5E POP SI         5           4          */ cBaseBU(3,-3),     cBaseBU(2,-2),
/* 5F POP DI         5           4          */ cBaseBU(3,-3),     cBaseBU(2,-2),
/* 60 PUSHA          17          18         */ cBase(17),         cBase(18), //<-- is this correct?
/* 61 POPA           19          24         */ cBase(3),          cBase(3),
//normal cycles are the same as INT
/* 62 BOUND rw,md    noj13       noj10      */ cBaseNoJ(16,13),   cBaseNoJ(13,10),  //<-- is this correct?
/* 63 ARPL ew,rw     10/11       20/21      */ cBaseMem(10,9),    cBaseMem(20,19),
/* 64 seg ovr (FS)                          */ cNull,             cNull,
/* 65 seg ovr (GS)                          */ cNull,             cNull,
/* 66 op-size ovr                           */ cNull,             cNull,
/* 67 addr-size ovr                         */ cNull,             cNull,
/* 68 PUSH dw        3           2          */ cBase(3),          cBase(2),
/* 69 IMUL rw,ew,iw  21/24       9-22/12-25 */ cBaseMem(21,22),   cBaseMem(6,7),
/* 6A PUSH ib        3           2          */ cBase(3),          cBase(2),
/* 6B IMUL rw,ew,ib  21/24       9-14/12-17 */ cBaseMem(21,22),   cBaseMem(6,7),
/* 6C INSB           5           15,p9-29   */ cBaseRep(5,4,5),   cBaseRep(15,6,13), //TODO? 386 PM
/* 6D INSW           5           15,p9-29   */ cBaseRep(5,4,5),   cBaseRep(15,6,13), //TODO? 386 PM
/* 6E OUTSB          5           14,p8-28   */ cBaseRep(3,4,5),   cBaseRep(12,12,5), //TODO? 386 PM
/* 6F OUTSW          5           14,p8-28   */ cBaseRep(3,4,5),   cBaseRep(12,12,5), //TODO? 386 PM
/* 70 JO cb          7,noj3      7,noj3     */ cBaseNoJ(JMPC,3),  cBaseNoJ(JMPC,3),
/* 71 JNO cb         7,noj3      7,noj3     */ cBaseNoJ(JMPC,3),  cBaseNoJ(JMPC,3),
/* 72 JC cb          7,noj3      7,noj3     */ cBaseNoJ(JMPC,3),  cBaseNoJ(JMPC,3),
/* 73 JNC cb         7,noj3      7,noj3     */ cBaseNoJ(JMPC,3),  cBaseNoJ(JMPC,3),
/* 74 JE cb          7,noj3      7,noj3     */ cBaseNoJ(JMPC,3),  cBaseNoJ(JMPC,3),
/* 75 JNE cb         7,noj3      7,noj3     */ cBaseNoJ(JMPC,3),  cBaseNoJ(JMPC,3),
/* 76 JBE cb         7,noj3      7,noj3     */ cBaseNoJ(JMPC,3),  cBaseNoJ(JMPC,3),
/* 77 JA cb          7,noj3      7,noj3     */ cBaseNoJ(JMPC,3),  cBaseNoJ(JMPC,3),
/* 78 JS cb          7,noj3      7,noj3     */ cBaseNoJ(JMPC,3),  cBaseNoJ(JMPC,3),
/* 79 JNS cb         7,noj3      7,noj3     */ cBaseNoJ(JMPC,3),  cBaseNoJ(JMPC,3),
/* 7A JPE c          7,noj3      7,noj3     */ cBaseNoJ(JMPC,3),  cBaseNoJ(JMPC,3),
/* 7B JPO cb         7,noj3      7,noj3     */ cBaseNoJ(JMPC,3),  cBaseNoJ(JMPC,3),
/* 7C JL cb          7,noj3      7,noj3     */ cBaseNoJ(JMPC,3),  cBaseNoJ(JMPC,3),
/* 7D JNL cb         7,noj3      7,noj3     */ cBaseNoJ(JMPC,3),  cBaseNoJ(JMPC,3),
/* 7E JLE cb         7,noj3      7,noj3     */ cBaseNoJ(JMPC,3),  cBaseNoJ(JMPC,3),
/* 7F JNLE cb        7,noj3      7,noj3     */ cBaseNoJ(JMPC,3),  cBaseNoJ(JMPC,3),
/* 80 Group 1                               */ cNull,             cNull,
/* 81 Group 1                               */ cNull,             cNull,
/* 82 alias of 80                           */ cNull,             cNull,
/* 83 Group 1                               */ cNull,             cNull,
/* 84 TEST eb,rb     2/6         2/5        */ cBaseMem(2,4),     cBaseMem(2,3),
/* 85 TEST ew,rw     2/6         2/5        */ cBaseMem(2,4),     cBaseMem(2,3),
/* 86 XCHG eb,rb     3/5         3/5        */ cBaseMem(3,3),     cBaseMem(3,3),
/* 87 XCHG ew,rw     3/5         3/5        */ cBaseMem(3,3),     cBaseMem(3,3),
/* 88 MOV eb,rb      2/3         2/2        */ cBaseMem(2,3),     cBaseMem(2,2),
/* 89 MOV ew,rw      2/3         2/2        */ cBaseMem(2,3),     cBaseMem(2,2),
/* 8A MOV rb,eb      2/5         2/4        */ cBaseMem(2,3),     cBaseMem(2,2),
/* 8B MOV rw,ew      2/5         2/4        */ cBaseMem(2,3),     cBaseMem(2,2),
/* 8C MOV ew,SR      2/3         2/2        */ cBaseMem(2,3),     cBaseMem(2,2),
/* 8D LEA rw,m       3           2          */ cBase(3),          cBase(2),
/* 8E MOV SR,ew      2/5,p17/19  2/5,p18/19 */ cBaseMemPM(2,3,5), cBaseMemPM(2,3,6),
/* 8F POP mw         5           5          */ cBase(3),          cBase(3),
/* 90 NOP            3           3          */ cBase(3),          cBase(3),
/* 91 XCHG AX,CX     3           3          */ cBase(3),          cBase(3),
/* 92 XCHG AX,DX     3           3          */ cBase(3),          cBase(3),
/* 93 XCHG AX,BX     3           3          */ cBase(3),          cBase(3),
/* 94 XCHG AX,SP     3           3          */ cBase(3),          cBase(3),
/* 95 XCHG AX,BP     3           3          */ cBase(3),          cBase(3),
/* 96 XCHG AX,SI     3           3          */ cBase(3),          cBase(3),
/* 97 XCHG AX,DI     3           3          */ cBase(3),          cBase(3),
/* 98 CBW            2           3          */ cBase(2),          cBase(3),
/* 99 CWD            2           3          */ cBase(2),          cBase(3),
//for 286 PM mode penalty:
// 4 cycles for PQ flush and fill
// 4 cycles for 2 stack pushes
// 8 cycles for 4 mem reads (descriptor)
// 26 - 16 = 10 (5 of penalty)
//TODO never tested on real HW
/* 9A CALL cd        13,p26      17,p34     */ cBasePM(5,5),      cBasePM(9,5),
/* 9B WAIT           3           6          */ cBase(3),          cBase(6),
/* 9C PUSHF          3           4          */ cBase(3),          cBase(4),
/* 9D POPF           5           5          */ cBase(3),          cBase(3),
/* 9E SAHF           2           3          */ cBase(2),          cBase(3),
/* 9F LAHF           2           2          */ cBase(2),          cBase(2),
/* A0 MOV AL,xb      5           4          */ cBase(3),          cBase(2),
/* A1 MOV AX,xw      5           4          */ cBase(3),          cBase(2),
/* A2 MOV xb,AL      3           2          */ cBase(3),          cBase(2),
/* A3 MOV xw,AX      3           2          */ cBase(3),          cBase(2),
/* A4 MOVSB          5           7          */ cBaseRep(3,0,5),   cBaseRep(5,0,5),
/* A5 MOVSW          5           7          */ cBaseRep(3,0,5),   cBaseRep(5,0,5),
/* A6 CMPSB          8           10         */ cBaseRep(4,5,5),   cBaseRep(6,5,5),
/* A7 CMPSW          8           10         */ cBaseRep(4,5,5),   cBaseRep(6,5,5),
/* A8 TEST AL,ib     3           2          */ cBase(3),          cBase(2),
/* A9 TEST AX,iw     3           2          */ cBase(3),          cBase(2),
/* AA STOSB          3           4          */ cBaseRep(3,0,4),   cBaseRep(4,1,5),
/* AB STOSW          3           4          */ cBaseRep(3,0,4),   cBaseRep(4,1,5),
/* AC LODSB          5           5          */ cBaseRep(3,2,5),   cBaseRep(3,2,5),
/* AD LODSW          5           5          */ cBaseRep(3,2,5),   cBaseRep(3,2,5),
/* AE SCASB          7           7          */ cBaseRep(5,6,5),   cBaseRep(5,6,5),
/* AF SCASW          7           7          */ cBaseRep(5,6,5),   cBaseRep(5,6,5),
/* B0 MOV AL,ib      2           2          */ cBase(2),          cBase(2),
/* B1 MOV CL,ib      2           2          */ cBase(2),          cBase(2),
/* B2 MOV DL,ib      2           2          */ cBase(2),          cBase(2),
/* B3 MOV BL,ib      2           2          */ cBase(2),          cBase(2),
/* B4 MOV AH,ib      2           2          */ cBase(2),          cBase(2),
/* B5 MOV CH,ib      2           2          */ cBase(2),          cBase(2),
/* B6 MOV DH,ib      2           2          */ cBase(2),          cBase(2),
/* B7 MOV BH,ib      2           2          */ cBase(2),          cBase(2),
/* B8 MOV AX,iw      2           2          */ cBase(2),          cBase(2),
/* B9 MOV CX,iw      2           2          */ cBase(2),          cBase(2),
/* BA MOV DX,iw      2           2          */ cBase(2),          cBase(2),
/* BB MOV BX,iw      2           2          */ cBase(2),          cBase(2),
/* BC MOV SP,iw      2           2          */ cBase(2),          cBase(2),
/* BD MOV BP,iw      2           2          */ cBase(2),          cBase(2),
/* BE MOV SI,iw      2           2          */ cBase(2),          cBase(2),
/* BF MOV DI,iw      2           2          */ cBase(2),          cBase(2),
/* C0 Group 2                               */ cNull,             cNull,
/* C1 Group 2                               */ cNull,             cNull,
/* C2 RET iw         11          10         */ cBase(7),          cBase(6),
/* C3 RET            11          10         */ cBase(7),          cBase(6),
//for PM mode:
// 4 cycles for pointer load (2 mem reads)
// 8 cycles for decriptor load (4 mem reads)
/* C4 LES rw,ed      7,p21       7,p22      */ cBasePM(3,6),      cBasePM(3,7),
/* C5 LDS rw,ed      7,p21       7,p22      */ cBasePM(3,6),      cBasePM(3,7),
/* C6 MOV eb,ib      2/3         2/2        */ cBaseMem(2,3),     cBaseMem(2,2),
/* C7 MOV ew,iw      2/3         2/2        */ cBaseMem(2,3),     cBaseMem(2,2),
/* C8 ENTER iw,ib    11          10         */ cBase(12),         cBase(11),
/* C9 LEAVE          5           4          */ cBase(3),          cBase(2),
/* CA RET iw         15,p25      18,pm32    */ cBasePM(11,7),     cBasePM(14,12),
/* CB RET            15,p25      18,pm32    */ cBasePM(11,7),     cBasePM(14,12),
/* CC INT 3          23,p40      33,pm59    */ cBasePM(13,7),     cBasePM(23,10),
/* CD INT ib         23,p40      37,pm59    */ cBasePM(13,7),     cBasePM(23,10),
/* CE INTO           24,noj3     35,noj3    */ cBaseNoJ(14,3),    cBaseNoJ(21,3),
/* CF IRET           17,p31      22,p38     */ cBasePM(11,7),     cBasePM(16,10),
/* D0 Group 2                               */ cNull,             cNull,
/* D1 Group 2                               */ cNull,             cNull,
/* D2 Group 2                               */ cNull,             cNull,
/* D3 Group 2                               */ cNull,             cNull,
/* D4 AAM            16          17         */ cBase(16),         cBase(17),
/* D5 AAD            14          19         */ cBase(14),         cBase(19),
/* D6 SALC           ??          ??         */ cBase(1),          cBase(1),
/* D7 XLATB          5           5          */ cBase(3),          cBase(3),
/* D8 FPU ESC        ??          ??         */ cBase(1),          cBase(1),
/* D9 FPU ESC        ??          ??         */ cBase(1),          cBase(1),
/* DA FPU ESC        ??          ??         */ cBase(1),          cBase(1),
/* DB FPU ESC        ??          ??         */ cBase(1),          cBase(1),
/* DC FPU ESC        ??          ??         */ cBase(1),          cBase(1),
/* DD FPU ESC        ??          ??         */ cBase(1),          cBase(1),
/* DE FPU ESC        ??          ??         */ cBase(1),          cBase(1),
/* DF FPU ESC        ??          ??         */ cBase(1),          cBase(1),
/* E0 LOOPNZ cb      8,noj4      11,noj??   */ cBaseNoJ(LOOPC,4), cBaseNoJ(LOOPC,4),
/* E1 LOOPZ cb       8,noj4      11,noj??   */ cBaseNoJ(LOOPC,4), cBaseNoJ(LOOPC,4),
/* E2 LOOP cb        8,noj4      11,noj??   */ cBaseNoJ(LOOPC,4), cBaseNoJ(LOOPC,4),
/* E3 JCXZ cb        8,noj4      11,noj??   */ cBaseNoJ(LOOPC,4), cBaseNoJ(LOOPC,4),
/* E4 IN AL,ib       5           12,p26     */ cBase(3),          cBasePM(10,14),
/* E5 IN AX,ib       5           12,p26     */ cBase(3),          cBasePM(10,14),
/* E6 OUT ib,AL      3           10,p24     */ cBase(3),          cBasePM(10,14),
/* E7 OUT ib,AX      3           10,p24     */ cBase(3),          cBasePM(10,14),
//TODO are the cycles given in the intel docs inclusive of the time needed
//to refill the prefetch queue?
/* E8 CALL cw        7           7          */ cBase(1),          cBase(1),
/* E9 JMP cw         7           7          */ cBase(JMPC),       cBase(JMPC),
/* EA JMP cd         11,p23      12,p27     */ cBasePM(JMPC,6),   cBasePM(JMPC,6),
/* EB JMP cb         7           7          */ cBase(JMPC),       cBase(JMPC),
/* EC IN AL,DX       5           13,p27     */ cBase(5),          cBasePM(11,14),
/* ED IN AX,DX       5           13,p27     */ cBase(5),          cBasePM(11,14),
/* EE OUT DX,AL      3           11,p25     */ cBase(3),          cBasePM(11,14),
/* EF OUT DX,AX      3           11,p25     */ cBase(3),          cBasePM(11,14),
/* F0 LOCK                                  */ cNull,             cNull,
/* F1                                       */ cNull,             cNull,
/* F2 REP/REPE                              */ cNull,             cNull,
/* F3 REPNE                                 */ cNull,             cNull,
/* F4 HLT            2           5          */ cBase(2),          cBase(5),
/* F5 CMC            2           2          */ cBase(2),          cBase(2),
/* F6 Group 3                               */ cNull,             cNull,
/* F7 Group 3                               */ cNull,             cNull,
/* F8 CLC            2           2          */ cBase(2),          cBase(2),
/* F9 STC            2           2          */ cBase(2),          cBase(2),
/* FA CLI            3           3          */ cBase(3),          cBase(3),
/* FB STI            2           3          */ cBase(2),          cBase(2),
/* FC CLD            2           2          */ cBase(2),          cBase(2),
/* FD STD            2           2          */ cBase(2),          cBase(2),
/* FE Group 4                               */ cNull,             cNull,
/* FF Group 5                               */ cNull,             cNull
};

// prefix 0F
static const Cycles cycles_0F[256*2] = {
//                    docs:286    386    hw:286        386
/* 00 Group 6                            */ cNull,     cNull,
/* 01 Group 7                            */ cNull,     cNull,
/* 02 LAR rw,ew            14/16  15/16  */ cBase(14), cBase(15),
/* 03 LSL rw,ew            14/16  20/21  */ cBase(14), cBase(20),
/* 04 illegal op.                        */ cNull,     cNull,
/* 05 LOADALL              195    ??     */ cBase(93), cBase(93),
/* 06 CLTS                 2      5      */ cBase(2),  cBase(5),
/* 07 illegal op.                        */ cNull,     cNull,
/* 08-0F illegal op.                     */ cNullBlock7,
/* 10-1F illegal op.                     */ cNullBlockF,
/* 20 MOV r32,CR0/CR2/CR3  -      6      */ cNull,     cBase(6),
/* 21 MOV r32,DR0 -- 3     -      22     */ cNull,     cBase(22),
/* 22 MOV CR0/CR2/CR3,r32  -      10/4/5 */ cNull,     cBase(10),
/* 23 MOV DR0 -- 3,r32     -      22     */ cNull,     cBase(22),
/* 24 MOV r32,TR6/TR7      -      12     */ cNull,     cBase(12),
/* 25 illegal op.                        */ cNull,     cNull,
/* 26 MOV TR6/TR7,r32      -      12     */ cNull,     cBase(12),
/* 27 illegal op.                        */ cNull,     cNull,
/* 28-2F illegal op.                     */ cNullBlock7,
/* 30-3F illegal op.                     */ cNullBlockF,
/* 40-4F illegal op.                     */ cNullBlockF,
/* 50-5F illegal op.                     */ cNullBlockF,
/* 60-6F illegal op.                     */ cNullBlockF,
/* 70-7F illegal op.                     */ cNullBlockF,
/* 80 JO rel16             -      7+m,3  */ cNull,     cBaseNoJ(JMPC,3),
/* 81 JNO rel16            -      7+m,3  */ cNull,     cBaseNoJ(JMPC,3),
/* 82 JB rel16             -      7+m,3  */ cNull,     cBaseNoJ(JMPC,3),
/* 83 JNB rel16            -      7+m,3  */ cNull,     cBaseNoJ(JMPC,3),
/* 84 JZ rel16             -      7+m,3  */ cNull,     cBaseNoJ(JMPC,3),
/* 85 JNZ rel16            -      7+m,3  */ cNull,     cBaseNoJ(JMPC,3),
/* 86 JNA rel16            -      7+m,3  */ cNull,     cBaseNoJ(JMPC,3),
/* 87 JA rel16             -      7+m,3  */ cNull,     cBaseNoJ(JMPC,3),
/* 88 JS rel16             -      7+m,3  */ cNull,     cBaseNoJ(JMPC,3),
/* 89 JNS rel16            -      7+m,3  */ cNull,     cBaseNoJ(JMPC,3),
/* 8A JP rel16             -      7+m,3  */ cNull,     cBaseNoJ(JMPC,3),
/* 8B JPO rel16            -      7+m,3  */ cNull,     cBaseNoJ(JMPC,3),
/* 8C JL rel16             -      7+m,3  */ cNull,     cBaseNoJ(JMPC,3),
/* 8D JNL rel16            -      7+m,3  */ cNull,     cBaseNoJ(JMPC,3),
/* 8E JLE rel16            -      7+m,3  */ cNull,     cBaseNoJ(JMPC,3),
/* 8F JG rel16             -      7+m,3  */ cNull,     cBaseNoJ(JMPC,3),
/* 90 SETO r/m8            -      4/5    */ cNull,     cBaseMem(4,5),
/* 91 SETNO r/m8           -      4/5    */ cNull,     cBaseMem(4,5),
/* 92 SETC r/m8            -      4/5    */ cNull,     cBaseMem(4,5),
/* 93 SETAE r/m8           -      4/5    */ cNull,     cBaseMem(4,5),
/* 94 SETE r/m8            -      4/5    */ cNull,     cBaseMem(4,5),
/* 95 SETNE r/m8           -      4/5    */ cNull,     cBaseMem(4,5),
/* 96 SETBE r/m8           -      4/5    */ cNull,     cBaseMem(4,5),
/* 97 SETA r/m8            -      4/5    */ cNull,     cBaseMem(4,5),
/* 98 SETS r/m8            -      4/5    */ cNull,     cBaseMem(4,5),
/* 99 SETNS r/m8           -      4/5    */ cNull,     cBaseMem(4,5),
/* 9A SETP r/m8            -      4/5    */ cNull,     cBaseMem(4,5),
/* 9B SETNP r/m8           -      4/5    */ cNull,     cBaseMem(4,5),
/* 9C SETNGE r/m8          -      4/5    */ cNull,     cBaseMem(4,5),
/* 9D SETNL r/m8           -      4/5    */ cNull,     cBaseMem(4,5),
/* 9E SETLE r/m8           -      4/5    */ cNull,     cBaseMem(4,5),
/* 9F SETG r/m8            -      4/5    */ cNull,     cBaseMem(4,5),
/* A0 PUSH FS              -      2      */ cNull,     cBase(2),
/* A1 POP FS               -      7,p21  */ cNull,     cBasePM(5,8),
/* A2 illegal op.                        */ cNull,     cNull,
/* A3 BT r/m16,r16         -      3/12   */ cNull,     cBaseMem(3,10),
/* A4 SHLD r/m16,r16,imm8  -      3/7    */ cNull,     cBaseMem(3,5),
/* A5 SHLD r/m16,r16,CL    -      3/7    */ cNull,     cBaseMem(3,5),
/* A6 illegal op.                        */ cNull,     cNull,
/* A7 illegal op.                        */ cNull,     cNull,
/* A8 PUSH GS              -      2      */ cNull,     cBase(2),
/* A9 POP GS               -      7,p21  */ cNull,     cBasePM(5,8),
/* AA illegal op.                        */ cNull,     cNull,
/* AB BTS r/m16,r16        -      6/13   */ cNull,     cBaseMem(6,11),
/* AC SHRD r/m16,r16,imm8  -      3/7    */ cNull,     cBaseMem(3,5),
/* AD SHRD r/m16,r16,CL    -      3/7    */ cNull,     cBaseMem(3,5),
/* AE illegal op.                        */ cNull,     cNull,
/* AF IMUL r16,r/m16       -      9/12   */ cNull,     cBaseMem(6,7),
/* B0-B7                                 */ cNullBlock7,
/* B8 illegal op.                        */ cNull,     cNull,
/* B9 illegal op.                        */ cNull,     cNull,
/* BA Group 8                            */ cNull,     cNull,
/* BB BTC r/m16,r16        -      6/13   */ cNull,     cBaseMem(6,13),
/* BC BSF r16,r/m16        -      10+3n  */ cNull,     cBase(10),
/* BD BSR r16,r/m16        -      10+3n  */ cNull,     cBase(10),
/* BE MOVSX r16,r/m8       -      3/6    */ cNull,     cBaseMem(3,4),
/* BF illegal op.                        */ cNull,     cNull,
/* C0-CF                                 */ cNullBlockF,
/* D0-DF                                 */ cNullBlockF,
/* E0-EF                                 */ cNullBlockF,
/* F0-FF                                 */ cNullBlockF
};

// Group 1
static const Cycles cycles_80[8*2] = {
/* 80 /0 ADD eb,ib  3/7  2/7  */ cBaseMem(3,5), cBaseMem(2,5),
/* 80 /1 OR  eb,ib  3/7  2/7  */ cBaseMem(3,5), cBaseMem(2,5),
/* 80 /2 ADC eb,ib  3/7  2/7  */ cBaseMem(3,5), cBaseMem(2,5),
/* 80 /3 SBB eb,ib  3/7  2/7  */ cBaseMem(3,5), cBaseMem(2,5),
/* 80 /4 AND eb,ib  3/7  2/7  */ cBaseMem(3,5), cBaseMem(2,5),
/* 80 /5 SUB eb,ib  3/7  2/7  */ cBaseMem(3,5), cBaseMem(2,5),
/* 80 /6 XOR eb,ib  3/7  2/7  */ cBaseMem(3,5), cBaseMem(2,5),
/* 80 /7 CMP eb,ib  3/6  2/5  */ cBaseMem(3,4), cBaseMem(2,3)
};
static const Cycles cycles_81[8*2] = {
/* 81 /0 ADD ew,iw  3/7  2/7  */ cBaseMem(3,8), cBaseMem(2,8),
/* 81 /1 OR  ew,iw  3/7  2/7  */ cBaseMem(3,8), cBaseMem(2,8),
/* 81 /2 ADC ew,iw  3/7  2/7  */ cBaseMem(3,8), cBaseMem(2,8),
/* 81 /3 SBB ew,iw  3/7  2/7  */ cBaseMem(3,8), cBaseMem(2,8),
/* 81 /4 AND ew,iw  3/7  2/7  */ cBaseMem(3,8), cBaseMem(2,8),
/* 81 /5 SUB ew,iw  3/7  2/7  */ cBaseMem(3,8), cBaseMem(2,8),
/* 81 /6 XOR ew,iw  3/7  2/7  */ cBaseMem(3,8), cBaseMem(2,8),
/* 81 /7 CMP ew,iw  3/6  2/5  */ cBaseMem(3,4), cBaseMem(2,3)
};
static const Cycles cycles_83[8*2] = {
/* 83 /0 ADD ew,ib  3/7  2/7  */ cBaseMem(3,8), cBaseMem(2,8),
/* 83 /1 OR  ew,ib  3/7  2/7  */ cBaseMem(3,8), cBaseMem(2,8),
/* 83 /2 ADC ew,ib  3/7  2/7  */ cBaseMem(3,8), cBaseMem(2,8),
/* 83 /3 SBB ew,ib  3/7  2/7  */ cBaseMem(3,8), cBaseMem(2,8),
/* 83 /4 AND ew,ib  3/7  2/7  */ cBaseMem(3,8), cBaseMem(2,8),
/* 83 /5 SUB ew,ib  3/7  2/7  */ cBaseMem(3,8), cBaseMem(2,8),
/* 83 /6 XOR ew,ib  3/7  2/7  */ cBaseMem(3,8), cBaseMem(2,8),
/* 83 /7 CMP ew,ib  3/6  2/5  */ cBaseMem(3,4), cBaseMem(2,3),
};

// Group 2
static const Cycles cycles_C0[8*2] = {
/* C0 /0 ROL eb,ib  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* C0 /1 ROR eb,ib  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* C0 /2 RCL eb,ib  5/8  9/10 */ cBaseMem(5,6), cBaseMem(9,8),
/* C0 /3 RCR eb,ib  5/8  9/10 */ cBaseMem(5,6), cBaseMem(9,8),
/* C0 /4 SHL eb,ib  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* C0 /5 SHR eb,ib  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* C0 /6 SAL eb,ib  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* C0 /7 SAR eb,ib  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5)
};
static const Cycles cycles_C1[8*2] = {
/* C1 /0 ROL ew,ib  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* C1 /1 ROR ew,ib  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* C1 /2 RCL ew,ib  5/8  9/10 */ cBaseMem(5,6), cBaseMem(9,8),
/* C1 /3 RCR ew,ib  5/8  9/10 */ cBaseMem(5,6), cBaseMem(9,8),
/* C1 /4 SHL ew,ib  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* C1 /5 SHR ew,ib  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* C1 /6 SAL ew,ib  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* C1 /7 SAR ew,ib  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5)
};
static const Cycles cycles_D0[8*2] = {
//286: compensate the m_instr->cycles.extra = 1 in CPUExecutor
/* D0 /0 ROL eb,1  2/7  3/7  */ cBaseMem(1,7), cBaseMem(3,5),
/* D0 /1 ROR eb,1  2/7  3/7  */ cBaseMem(1,7), cBaseMem(3,5),
/* D0 /2 RCL eb,1  2/7  9/10 */ cBaseMem(1,7), cBaseMem(9,8),
/* D0 /3 RCR eb,1  2/7  9/10 */ cBaseMem(1,7), cBaseMem(9,8),
/* D0 /4 SHL eb,1  2/7  3/7  */ cBaseMem(1,7), cBaseMem(3,5),
/* D0 /5 SHR eb,1  2/7  3/7  */ cBaseMem(1,7), cBaseMem(3,5),
/* D0 /6 SAL eb,1  2/7  3/7  */ cBaseMem(1,7), cBaseMem(3,5),
/* D0 /7 SAR eb,1  2/7  3/7  */ cBaseMem(1,7), cBaseMem(3,5)
};
static const Cycles cycles_D1[8*2] = {
//286: compensate the m_instr->cycles.extra = 1 in CPUExecutor
/* D1 /0 ROL ew,1  2/7  3/7  */ cBaseMem(1,7), cBaseMem(3,5),
/* D1 /1 ROR ew,1  2/7  3/7  */ cBaseMem(1,7), cBaseMem(3,5),
/* D1 /2 RCL ew,1  2/7  9/10 */ cBaseMem(1,7), cBaseMem(9,8),
/* D1 /3 RCR ew,1  2/7  9/10 */ cBaseMem(1,7), cBaseMem(9,8),
/* D1 /4 SHL ew,1  2/7  3/7  */ cBaseMem(1,7), cBaseMem(3,5),
/* D1 /5 SHR ew,1  2/7  3/7  */ cBaseMem(1,7), cBaseMem(3,5),
/* D1 /6 SAL ew,1  2/7  3/7  */ cBaseMem(1,7), cBaseMem(3,5),
/* D1 /7 SAR ew,1  2/7  3/7  */ cBaseMem(1,7), cBaseMem(3,5)
};
static const Cycles cycles_D2[8*2] = {
/* D2 /0 ROL eb,CL  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* D2 /1 ROR eb,CL  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* D2 /2 RCL eb,CL  5/8  9/10 */ cBaseMem(5,6), cBaseMem(9,8),
/* D2 /3 RCR eb,CL  5/8  9/10 */ cBaseMem(5,6), cBaseMem(9,8),
/* D2 /4 SHL eb,CL  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* D2 /5 SHR eb,CL  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* D2 /6 SAL eb,CL  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* D2 /7 SAR eb,CL  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5)
};
static const Cycles cycles_D3[8*2] = {
/* D3 /0 ROL ew,CL  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* D3 /1 ROR ew,CL  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* D3 /2 RCL ew,CL  5/8  9/10 */ cBaseMem(5,6), cBaseMem(9,8),
/* D3 /3 RCR ew,CL  5/8  9/10 */ cBaseMem(5,6), cBaseMem(9,8),
/* D3 /4 SHL ew,CL  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* D3 /5 SHR ew,CL  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* D3 /6 SAL ew,CL  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5),
/* D3 /7 SAR ew,CL  5/8  3/7  */ cBaseMem(5,6), cBaseMem(3,5)
};

// Group 3
static const Cycles cycles_F6[8*2] = {
/* F6 /0 TEST eb,ib  3/6    2/5    */ cBaseMem(3,4),   cBaseMem(2,3),
/* F6 /1 TEST eb,ib  3/6    2/5    */ cBaseMem(3,4),   cBaseMem(2,3),
/* F6 /2 NOT eb      2/7    2/6    */ cBaseMem(2,5),   cBaseMem(2,4),
/* F6 /3 NEG eb      2/7    2/6    */ cBaseMem(2,5),   cBaseMem(2,4),
/* F6 /4 MUL eb      13/16  9/12   */ cBaseMem(13,14), cBaseMem(6,7),
/* F6 /5 IMUL eb     13/16  9/12   */ cBaseMem(13,14), cBaseMem(6,7),
/* F6 /6 DIV eb      14/17  14/17  */ cBaseMem(14,15), cBaseMem(14,15),
/* F6 /7 IDIV eb     17/20  19/??  */ cBaseMem(17,18), cBaseMem(19,20)
};
static const Cycles cycles_F7[8 *2] = {
/* F7 /0 TEST ew,iw  3/6    2/5    */ cBaseMem(3,4),   cBaseMem(2,3),
/* F7 /1 TEST ew,iw  3/6    2/5    */ cBaseMem(3,4),   cBaseMem(2,3),
/* F7 /2 NOT ew      2/7    2/6    */ cBaseMem(2,8),   cBaseMem(2,4),
/* F7 /3 NEG ew      2/7    2/6    */ cBaseMem(2,8),   cBaseMem(2,4),
/* F7 /4 MUL ew      21/24  9/12   */ cBaseMem(21,22), cBaseMem(6,7),
/* F7 /5 IMUL ew     21/24  9/12   */ cBaseMem(21,22), cBaseMem(6,7),
/* F7 /6 DIV ew      22/25  22/25  */ cBaseMem(22,23), cBaseMem(22,23),
/* F7 /7 IDIV ew     25/28  27/??  */ cBaseMem(25,26), cBaseMem(27,28)
};

// Group 4
static const Cycles cycles_FE[8*2] = {
/* FE /0 INC eb  2/7  2/6  */ cBaseMem(2,5), cBaseMem(2,4),
/* FE /1 DEC eb  2/7  2/6  */ cBaseMem(2,5), cBaseMem(2,4),
/* FE /2 illegal op.       */ cNull,         cNull,
/* FE /3 illegal op.       */ cNull,         cNull,
/* FE /4 illegal op.       */ cNull,         cNull,
/* FE /5 illegal op.       */ cNull,         cNull,
/* FE /6 illegal op.       */ cNull,         cNull,
/* FE /7 illegal op.       */ cNull,         cNull
};

// Group 5
static const Cycles cycles_FF[8*2] = {
/* FF /0 INC ew   2/7     2/6     */ cBaseMem(2,8),               cBaseMem(2,4),
/* FF /1 DEC ew   2/7     2/6     */ cBaseMem(2,8),               cBaseMem(2,4),
/* FF /2 CALL ew  7/11    7/10    */ cBaseMem(1,3),               cBaseMem(1,2),
/* FF /3 CALL ed  16/29   22,p38  */ cBaseMemPM(3,5,7),           cBaseMemPM(3,4,8),
/* FF /4 JMP ew   7/11    7/10    */ cBaseMem(JMPC,JMPC+2),       cBaseMem(JMPC,JMPC+1),
/* FF /5 JMP ed   15,p26  43,p31  */ cBaseMemPM(JMPC+4,JMPC+4,7), cBaseMemPM(JMPC+20,JMPC+20,-10),
/* FF /6 PUSH mw  5       5       */ cBase(5),                    cBase(5),
/* FF /7 illegal op.              */ cNull,                       cNull
};

// Group 6
static const Cycles cycles_0F00[8*2] = {
/* 00 /0 SLDT ew  2/3    2/2    */ cBaseMem(2,3), cBase(2),
/* 00 /1 STR ew   2/3    2/2    */ cBaseMem(2,3), cBase(2),
/* 00 /2 LLDT ew  17/19  20     */ cBase(17),     cBase(20),
/* 00 /3 LTR ew   17/19  23/27  */ cBase(17),     cBase(23),
/* 00 /4 VERR ew  14/16  10/11  */ cBase(14),     cBase(10),
/* 00 /5 VERW ew  14/16  15/16  */ cBase(14),     cBase(15),
/* 00 /6 illegal op.            */ cNull,         cNull,
/* 00 /7 illegal op.            */ cNull,         cNull
};

// Group 7
static const Cycles cycles_0F01[8*2] = {
/* 01 /0 SGDT m     11   9     */ cBase(7),      cBase(5),
/* 01 /1 SIDT m     12   9     */ cBase(8),      cBase(5),
/* 01 /2 LGDT m     11   11    */ cBase(7),      cBase(7),
/* 01 /3 LIDT m     12   11    */ cBase(8),      cBase(7),
/* 01 /4 SMSW ew    2/3  2/3   */ cBaseMem(2,3), cBaseMem(2,3),
/* 01 /5 illegal op.           */ cNull,         cNull,
/* 01 /6 LMSW ew    3/6  10/13 */ cBaseMem(3,4), cBaseMem(10,11),
/* 01 /7 illegal op.           */ cNull,         cNull
};

// Group 8
static const Cycles cycles_0FBA[8*2] = {
/* BA /0 illegal op.             */ cNull, cNull,
/* BA /1 illegal op.             */ cNull, cNull,
/* BA /2 illegal op.             */ cNull, cNull,
/* BA /3 illegal op.             */ cNull, cNull,
/* BA /4 BT r/m16,imm8   -  3/6  */ cNull, cBaseMem(3,6),
/* BA /5 BTS r/m16,imm8  -  6/8  */ cNull, cBaseMem(6,8),
/* BA /6 BTR r/m16,imm8  -  6/8  */ cNull, cBaseMem(6,8),
/* BA /7 BTC r/m16,imm8  -  6/8  */ cNull, cBaseMem(6,8)
};

const Cycles * CPUDecoder::ms_cycles[CTB_COUNT] = {
	// main tables
	[CTB_IDX_NONE] = cycles_none,
	[CTB_IDX_0F] = cycles_0F,

	// Group 1
	[CTB_IDX_80] = cycles_80,
	[CTB_IDX_81] = cycles_81,
	[CTB_IDX_83] = cycles_83,

	// Group 2
	[CTB_IDX_C0] = cycles_C0,
	[CTB_IDX_C1] = cycles_C1,
	[CTB_IDX_D0] = cycles_D0,
	[CTB_IDX_D1] = cycles_D1,
	[CTB_IDX_D2] = cycles_D2,
	[CTB_IDX_D3] = cycles_D3,

	// Group 3
	[CTB_IDX_F6] = cycles_F6,
	[CTB_IDX_F7] = cycles_F7,

	// Group 4
	[CTB_IDX_FE] = cycles_FE,

	// Group 5
	[CTB_IDX_FF] = cycles_FF,

	// Group 6
	[CTB_IDX_0F00] = cycles_0F00,

	// Group 7
	[CTB_IDX_0F01] = cycles_0F01,

	// Group 8
	[CTB_IDX_0FBA] = cycles_0FBA
};
