// Definitions that xrCore expects the engine executable to provide. The tests link
// xrCore on its own, so they get inert versions here. Declarations match the externs
// in xrCore.cpp, log.cpp and xrDebugNew.cpp.
#include "../../xrCore/xrCore.h"

XRCORE_API u32 build_id = 0;
XRCORE_API LPCSTR build_date = "xrCore_tests";

xr_string get_modded_exes_version_string() { return xr_string("tests"); }
LPCSTR get_modded_exes_name() { return "xrCore_tests"; }

enum Console_mark;
bool is_console_mark(Console_mark) { return false; }

void printLuaStack() {}
