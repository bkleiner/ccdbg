#pragma once

#include <string>

#include "dbgsession.h"
#include "line_parser.h"
#include "symtab.h"
#include "symtypetree.h"

namespace debug::core {

  class cdb_file : public line_parser {
  public:
    cdb_file(dbg_session *session);
    ~cdb_file();
    bool open(std::string filename);
    bool parse_record();

  protected:
    dbg_session *mSession;

    std::string base_dir;

    std::string cur_module;
    std::string cur_file;

    symbol_scope parse_scope();
    symbol_identifier parse_identifier();

    bool parse_linker();
    bool parse_type_chain_record(symbol *sym);

    bool parse_type();
    bool parse_type_member(sym_type_struct *t);
  };

} // namespace debug::core
