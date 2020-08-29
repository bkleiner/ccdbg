#pragma once

#include <assert.h>
#include <iostream>
#include <map>
#include <memory>
#include <stdint.h>
#include <string>
#include <vector>

namespace debug {

  namespace core {
    class target;
    class sym_tab;
    class sym_type_tree;
    class context_mgr;
    class breakpoint_mgr;
    class module_mgr;
  } // namespace core

  class dbg_session {
  public:
    dbg_session();
    ~dbg_session();

    core::target *target();
    core::sym_tab *symtab();
    core::sym_type_tree *symtree();
    core::context_mgr *contextmgr();
    core::breakpoint_mgr *bpmgr();
    core::module_mgr *modulemgr();

    bool select_target(std::string name);

  private:
    std::unique_ptr<core::sym_tab> sym_tab;
    std::unique_ptr<core::sym_type_tree> sym_type_tree;
    std::unique_ptr<core::context_mgr> context_mgr;
    std::unique_ptr<core::breakpoint_mgr> breakpoint_mgr;
    std::unique_ptr<core::module_mgr> module_mgr;

    std::string current_target;
    std::map<std::string, std::unique_ptr<core::target>> targets;

    core::target *add_target(core::target *t);
  };

} // namespace debug