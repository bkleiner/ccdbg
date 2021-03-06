#include "dbg_session.h"

#include <stdint.h>

#include "breakpoint_mgr.h"
#include "cdb_file.h"
#include "disassembly.h"
#include "log.h"
#include "module.h"
#include "registers.h"
#include "sym_tab.h"
#include "sym_type_tree.h"

#include "target_cc.h"
#include "target_dummy.h"
#include "target_s51.h"
#include "target_silabs.h"

namespace debug {

  dbg_session::dbg_session()
      : current_target("")
      , sym_tab(std::make_unique<core::sym_tab>(this))
      , sym_type_tree(std::make_unique<core::sym_type_tree>(this))
      , context_mgr(std::make_unique<core::context_mgr>(this))
      , breakpoint_mgr(std::make_unique<core::breakpoint_mgr>(this))
      , module_mgr(std::make_unique<core::module_mgr>())
      , disassembly(std::make_unique<core::disassembly>())
      , cpu_registers(std::make_unique<core::cpu_registers>(this)) {

    current_target = add_target(new core::target_cc())->target_name();
    add_target(new core::target_s51());
    add_target(new core::target_dummy());
    add_target(new core::target_silabs());
  }

  dbg_session::~dbg_session() {}

  core::target *dbg_session::target() {
    return targets[current_target].get();
  }

  core::sym_tab *dbg_session::symtab() {
    return sym_tab.get();
  }

  core::sym_type_tree *dbg_session::symtree() {
    return sym_type_tree.get();
  }

  core::context_mgr *dbg_session::contextmgr() {
    return context_mgr.get();
  }

  core::breakpoint_mgr *dbg_session::bpmgr() {
    return breakpoint_mgr.get();
  }

  core::module_mgr *dbg_session::modulemgr() {
    return module_mgr.get();
  }

  core::disassembly *dbg_session::disasm() {
    return disassembly.get();
  }

  core::cpu_registers *dbg_session::regs() {
    return cpu_registers.get();
  }

  bool dbg_session::load(std::string path, std::string src_dir) {
    core::cdb_file cdbfile(this);
    if (!cdbfile.open(path + ".cdb", src_dir)) {
      return false;
    }

    disasm()->load_file(path + ".ihx");
    return true;
  }

  bool dbg_session::select_target(std::string name) {
    auto it = targets.find(name);
    if (it == targets.end())
      return false;

    if (target()) {
      core::log::print("current target {}\n", target()->target_name());

      if (target()->is_connected()) {
        bpmgr()->clear_all();

        target()->stop();
        target()->disconnect();
      }

      // clear out the data structures.
      symtab()->clear();
      symtree()->clear();
      //mcontext_mgr->clear()	@FIXME contextmgr needs a clear or reset
      modulemgr()->reset();
    }

    // select new target
    current_target = it->first;
    core::log::print("selecting target {}\n", target()->target_name());

    return true;
  }

  debug::core::target *dbg_session::add_target(debug::core::target *t) {
    targets[t->target_name()] = std::unique_ptr<core::target>(t);
    return t;
  }
} // namespace debug