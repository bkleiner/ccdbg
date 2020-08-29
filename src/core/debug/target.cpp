#include "target.h"

#include <cstdio>
#include <cstring>
#include <iostream>

#include "ihex.h"

namespace debug::core {

  target::target()
      : force_stop(false) {
  }

  target::~target() {
  }

  void target::print_buf_dump(char *buf, int len) {
    const int PerLine = 16;
    int i, addr;

    for (addr = 0; addr < len; addr += PerLine) {
      printf("%05x\t", (unsigned int)addr);
      // print each hex byte
      for (i = 0; i < PerLine; i++)
        printf("%02x ", (unsigned int)buf[addr + i] & 0xff);
      printf("\t");
      for (i = 0; i < PerLine; i++)
        putchar((buf[addr + i] >= '0' && buf[addr + i] <= 'z') ? buf[addr + i] : '.');
      putchar('\n');
    }
  }

  /** Default implementation, load an intel hex file and use write_code to place
	it in memory
*/
  bool target::load_file(std::string name) {
    // set all data to 0xff, since this is the default erased value for flash
    char buf[0x20000];
    memset(buf, 0xff, 0x20000);

    std::cout << "Loading file '" << name << "'" << std::endl;

    uint32_t start, end;
    if (!ihex_load_file(name.c_str(), buf, &start, &end)) {
      return false;
    }

    print_buf_dump(buf, end - start);
    printf("start %d %d\n", start, end);
    write_code(start, end - start + 1, (unsigned char *)&buf[start]);
    write_PC(start);
    return true;
  }

  void target::stop() {
    force_stop = true;
  }

  bool target::check_stop_forced() {
    if (force_stop) {
      force_stop = false;
      return true;
    }
    return false;
  }

  /** derived calsses must call this function to ensure the cache is updated.
*/
  void target::write_sfr(uint8_t addr,
                         uint8_t page,
                         uint8_t len,
                         unsigned char *buf) {
    SFR_PAGE_LIST::iterator it;
    it = cache_get_sfr_page(page);

    if (it != mCacheSfrPages.end()) {
      // update values in cache
      memcpy((*it).buf + (addr - 0x80), buf, len);
    }
  }

  void target::invalidate_cache() {
    mCacheSfrPages.clear();
  }

  void target::read_sfr_cache(uint8_t addr,
                              uint8_t page,
                              uint8_t len,
                              unsigned char *buf) {
    SFR_PAGE_LIST::iterator it;
    it = cache_get_sfr_page(page);

    if (it == mCacheSfrPages.end()) {
      // not in cache, read it and cache it.
      SFR_CACHE_PAGE page_entry;
      page_entry.page = page;
      read_sfr(0x80, page_entry.page, 128, page_entry.buf);
      mCacheSfrPages.push_back(page_entry);
      memcpy(buf, page_entry.buf + (addr - 0x80), len);
    } else {
      // in cache
      memcpy(buf, (*it).buf + (addr - 0x80), len);
    }
  }

  void target::read_memory(target_addr addr, int len, uint8_t *buf) {
    switch (addr.space) {
    case target_addr::AS_CODE:
    case target_addr::AS_CODE_STATIC:
      return read_code(addr.addr, len, buf);

    case target_addr::AS_ISTACK:
    case target_addr::AS_IRAM_LOW:
    case target_addr::AS_INT_RAM:
      return read_data(addr.addr, len, buf);

    case target_addr::AS_XSTACK:
    case target_addr::AS_EXT_RAM:
      return read_xdata(addr.addr, len, buf);

    case target_addr::AS_SFR:
      return read_sfr(addr.addr, len, buf);

    case target_addr::AS_REGISTER: {
      uint8_t offset = 0;
      read_sfr(0xd0, 1, &offset);
      return read_data(addr.addr + (offset & 0x18), len, buf);
    }

    default:
      break;
    }
  }
} // namespace debug::core