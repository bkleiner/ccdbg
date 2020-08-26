/***************************************************************************
 *   Copyright (C) 2005 by Ricky White   *
 *   rickyw@neatstuff.co.nz   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#ifndef MEMREMAP_H
#define MEMREMAP_H
#include "types.h"

#include <ctype.h>
#include <stdint.h>

struct target_addr {
  enum target_addr_space {
    AS_XSTACK,      ///< External stack
    AS_ISTACK,      ///< Internal stack
    AS_CODE,        ///< Code memory
    AS_CODE_STATIC, ///< Code memory, static segment
    AS_IRAM_LOW,    ///< Internal RAM (lower 128 bytes)
    AS_EXT_RAM,     ///< External data RAM
    AS_INT_RAM,     ///< Internal data RAM
    AS_BIT,         ///< Bit addressable area
    AS_SFR,         ///< SFR space
    AS_SBIT,        ///< SBIT space
    AS_REGISTER,    ///< Register space
    AS_UNDEF        ///< Used for function records, or any undefined space code
  };
  static constexpr const char addr_space_map[] = {'A', 'B', 'C', 'D', 'E', 'F', 'H', 'I', 'J', 'R', 'Z'};

  target_addr();
  target_addr(target_addr_space space, ADDR addr);

  static target_addr from_name(char name, ADDR addr);

  char space_name() {
    return addr_space_map[space];
  }

  operator ADDR() const {
    return addr;
  }

  target_addr_space space;
  ADDR addr;
};

/**
	convert to / from flat memory architecture as used by GDB / newcdb 
	and the mcs51 multiple memory areas.

	This allows tools written for debugging programs written for other processors witha flat memory map to work with newcdb.

	this is used by the x command when addresses are entered directly,  it will map these flat addresses to the correct memory areas

	@author Ricky White <rickyw@neatstuff.co.nz>
*/
class MemRemap {
public:
  /*
	Memory remapping

	Special addresses to allow easy debugging with interfaces designed for gdb

	0x00000000 - 0x00000000	Code memory ( support for processor bank switch and possible sw bank switch)
	0x20000000 - 0x2FFFFFFF	xdata + bank switch
	0x40000000 - 0x400000FF	data ram
	0x40000100 - 0x400001FF	i data ram
	0x80000080 - 0x800000FF	sfr
	0xFFFFFFFF				Invalid address
	
	@TODO add other areas for 8051 regs etc...
	
	@TODO consider how this can be applied to other processors.
*/
  static ADDR target(uint32_t flat_addr, char &area) {
    if (flat_addr < 0x20000000) {
      area = 'c';
      return flat_addr;
    } else if (flat_addr < 0x2FFFFFFF) {
      area = 'x';
      return flat_addr & 0x0FFFFFFF;
    } else if (flat_addr >= 0x40000000 && flat_addr <= 0x400000FF) {
      area = 'd';
      return flat_addr & 0xFF;
    } else if (flat_addr >= 0x40000100 && flat_addr <= 0x400001FF) {
      area = 'i';
      return flat_addr & 0xFF;
    } else if (flat_addr >= 0x80000080 && flat_addr <= 0x80000FFF) {
      area = 's';
      return flat_addr & 0xFF;
    } else
      return INVALID_ADDR;
  }

  static uint32_t flat(ADDR target_addr, char area) {
    switch (tolower(area)) {
    case 'c':
      return target_addr;
    case 'x':
      return target_addr | 0x20000000;
    case 'd':
      return target_addr | 0x40000000;
    case 'i':
      return target_addr | 0x40000100;
    case 's':
      return target_addr | 0x80000000;
    default:
      return INVALID_FLAT_ADDR;
    }
  }
};

#endif
