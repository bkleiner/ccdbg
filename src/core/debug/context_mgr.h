#pragma once

#include <string>

#include "dbg_session.h"
#include "types.h"

namespace debug::core {

  struct context {
    std::string module;

    ADDR addr;

    LINE_NUM c_line;
    LINE_NUM asm_line;

    std::string function;
    BLOCK block;
    LEVEL level;
  };

  class context_mgr {
  public:
    context_mgr(dbg_session *session);

    void dump();
    context update_context();
    context set_context(ADDR addr);
    context get_current() { return ctx; }

  protected:
    dbg_session *session;
    context ctx;
  };

} // namespace debug::core
