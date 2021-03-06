#include "line_spec.h"

#include <stdlib.h>
#include <string.h>

#include "breakpoint_mgr.h"
#include "line_parser.h"
#include "log.h"
#include "module.h"
#include "sym_tab.h"

namespace debug::core {

  line_spec::line_spec(line_spec_type spec_type, ADDR addr)
      : spec_type(spec_type)
      , addr(addr) {}

  line_spec line_spec::create(dbg_session *session, std::string linespec) {
    if (linespec.empty()) {
      return {INVALID};
    }

    line_parser p(linespec);
    if (p.peek() == '*') {
      p.consume();

      line_spec spec = {
          ADDRESS,
          std::stoi(p.consume(std::string::npos), 0, 16),
      };

      std::string mod;

      if (session->modulemgr()->get_c_addr(spec.addr, mod, spec.line)) {
        spec.file = session->modulemgr()->module(mod).get_c_file_name();
      }

      if (session->modulemgr()->get_asm_addr(spec.addr, mod, spec.line)) {
        spec.file = session->modulemgr()->module(mod).get_asm_file_name();
      }

      return spec;
    }

    /*
    if (p.peek() == '+') {
      const auto ctx = session->contextmgr()->get_current();
      int32_t offset = strtoul(linespec.substr(1).c_str(), 0, 0);
      spec_type = PLUS_OFFSET;
      addr = ctx.addr + offset;
      log::print("ERROR: offset suport not fully implemented...\n");
      return true;
    }

    if (p.peek() == '-') {
      const auto ctx = session->contextmgr()->get_current();
      int32_t offset = strtoul(linespec.substr(1).c_str(), 0, 0);
      spec_type = MINUS_OFFSET;
      addr = ctx.addr + offset;
      log::print("ERROR: offset suport not fully implemented...\n");
      return true;
    }
    */

    const auto file = p.consume_until(':');
    if (p.peek() == ':') {
      p.consume();

      // file:function or file:line number
      if (isdigit(p.peek())) {
        line_spec spec = {
            LINE_NUMBER,
        };

        spec.file = file;
        spec.line = std::stoul(p.consume(std::string::npos));
        spec.addr = session->symtab()->get_addr(spec.file, spec.line);
        return spec;
      }

      // file function
      line_spec spec = {
          FUNCTION,
      };

      spec.file = file;
      spec.function = p.consume(std::string::npos);
      if (session->symtab()->get_addr(spec.file, spec.function, spec.addr, spec.end_addr) &&
          session->symtab()->find_c_file_line(spec.addr, spec.file, spec.line))
        return spec;

      log::print("ERROR: linespec does not match a valid line.\n");
      return {INVALID, INVALID_ADDR};
    }

    line_spec spec = {
        FUNCTION,
    };
    spec.function = linespec;

    if (session->symtab()->get_addr(linespec, spec.addr, spec.end_addr) &&
        session->symtab()->find_c_file_line(spec.addr, spec.file, spec.line)) {
      return spec;
    }

    log::print("ERROR: linespec does not match a valid line.\n");
    return {INVALID, INVALID_ADDR};
  }
} // namespace debug::core