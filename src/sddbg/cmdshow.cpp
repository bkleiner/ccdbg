
#include "cmdshow.h"

#include <iostream>

namespace debug {

  CmdShow::CmdShow() {
    // build show/set cmd list
    cmdlist.push_back(new CmdShowVersion());
    cmdlist.push_back(new CmdShowCopying());
    cmdlist.push_back(new CmdShowWarranty());
  }

  CmdShow::~CmdShow() {
  }

  bool CmdShow::parse(std::string cmd) {
    if (cmd.find("show ") == 0) {
      ParseCmd::List::iterator it;
      for (it = cmdlist.begin(); it != cmdlist.end(); ++it) {
        if ((*it)->parse(cmd.substr(5, cmd.length() - 5)))
          break;
      }
      return true;
    }
    return false;
  }

  bool CmdShowVersion::parse(std::string cmd) {
    if (cmd.find("version") == 0) {
      std::cout << std::endl
                << "mcs51cdb version 0.1" << std::endl
                << " compiled on " << __DATE__ << " at " << __TIME__
                << std::endl
                << std::endl;
      return true;
    }
    return false;
  }

  bool CmdShowCopying::parse(std::string cmd) {
    return false;
  }

  bool CmdShowWarranty::parse(std::string cmd) {
    if (cmd.find("waranty") == 0) {

      std::cout << "                            NO WARRANTY\n"
                   "\n"
                   "  11. BECAUSE THE PROGRAM IS LICENSED FREE OF CHARGE, THERE IS NO WARRANTY\n"
                   "FOR THE PROGRAM, TO THE EXTENT PERMITTED BY APPLICABLE LAW.  EXCEPT WHEN\n"
                   "OTHERWISE STATED IN WRITING THE COPYRIGHT HOLDERS AND/OR OTHER PARTIES\n"
                   "PROVIDE THE PROGRAM \"AS IS\" WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED\n"
                   "OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF\n"
                   "MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  THE ENTIRE RISK AS\n"
                   "TO THE QUALITY AND PERFORMANCE OF THE PROGRAM IS WITH YOU.  SHOULD THE\n"
                   "PROGRAM PROVE DEFECTIVE, YOU ASSUME THE COST OF ALL NECESSARY SERVICING,\n"
                   "REPAIR OR CORRECTION.\n"
                   "\n"
                   "  12. IN NO EVENT UNLESS REQUIRED BY APPLICABLE LAW OR AGREED TO IN WRITING\n"
                   "WILL ANY COPYRIGHT HOLDER, OR ANY OTHER PARTY WHO MAY MODIFY AND/OR\n"
                   "REDISTRIBUTE THE PROGRAM AS PERMITTED ABOVE, BE LIABLE TO YOU FOR DAMAGES,\n"
                   "INCLUDING ANY GENERAL, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES ARISING\n"
                   "OUT OF THE USE OR INABILITY TO USE THE PROGRAM (INCLUDING BUT NOT LIMITED\n"
                   "TO LOSS OF DATA OR DATA BEING RENDERED INACCURATE OR LOSSES SUSTAINED BY\n"
                   "YOU OR THIRD PARTIES OR A FAILURE OF THE PROGRAM TO OPERATE WITH ANY OTHER\n"
                   "PROGRAMS), EVEN IF SUCH HOLDER OR OTHER PARTY HAS BEEN ADVISED OF THE\n"
                   "POSSIBILITY OF SUCH DAMAGES.\n"
                   "\n";
      return true;
    }
    return false;
  }
} // namespace debug