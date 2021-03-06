/* mc_sync.h - Memory Controller testbench SYNCdevices defines
	 Copyright (C) 2001 by Ivan Guzvinec, ivang@opencores.org

	 This file is part of OpenRISC 1000 Architectural Simulator.
	 
	 This program is free software; you can redistribute it and/or modify
	 it under the terms of the GNU General Public License as published by
	 the Free Software Foundation; either version 2 of the License, or
	 (at your option) any later version.
	 
	 This program is distributed in the hope that it will be useful,
	 but WITHOUT ANY WARRANTY; without even the implied warranty of
	 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
	 GNU General Public License for more details.

	 You should have received a copy of the GNU General Public License
	 along with this program; if not, write to the Free Software
	 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __MC_SYNC_H
#define __MC_SYNC_H

/* should configuration be read from MC? */
#define MC_READ_CONF

/* TEMPLATE SELECTION       */
/* change #undef to #define */
#define _MC_TEST_TEMPLATE1
#undef  _MC_TEST_TEMPLATE2
#undef  _MC_TEST_TEMPLATE3
/* ------------------------ */

/* memory configuration that must reflect mcmem.cfg */
#define MC_SYNC_CSMASK	0xFE	/* 8 bit mask for 8 chip selects. 1 ASYNC at CS, 0 something else at CS */

typedef struct MC_SYNC_CS
{
  unsigned long M;
} MC_SYNC_CS;

MC_SYNC_CS mc_sync_cs[8] = {
  { 0x02  /* SELect mask */
    },
  { 0x04 },
  { 0x06 },
  { 0x08 },
  { 0x0A },
  { 0x0C },
  { 0x0E },
  { 0x10 } };


#define MC_SYNC_TEST0	0x00000001LU /* no parity, bank after column, write enable */
#define MC_SYNC_TEST1	0x00000002LU /* parity */
#define MC_SYNC_TEST2	0x00000004LU /* bank after row */
#define MC_SYNC_TEST3	0x00000008LU /* RO */
#define MC_SYNC_TEST4	0x00000010LU /* - NOT DEFINED - */

#ifdef _MC_TEST_TEMPLATE1
  #define MC_SYNC_FLAGS	0x000002B4LU	/* MC_TEST_ flags... see mc_common.h */
  #define MC_SYNC_TESTS	0x00000005LU
#endif

#ifdef _MC_TEST_TEMPLATE2
  #define MC_SYNC_FLAGS	0x00000128LU	/* MC_TEST_ flags... see mc_common.h */
  #define MC_SYNC_TESTS	0x00000008LU
#endif

#ifdef _MC_TEST_TEMPLATE3
  #define MC_SYNC_FLAGS	0x000007FFLU	/* MC_TEST_ flags... see mc_common.h */
  #define MC_SYNC_TESTS	0x0000000FLU
#endif


#endif
