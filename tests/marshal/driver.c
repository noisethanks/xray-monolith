/* mrun <script.lua> [args...] : LuaJIT + engine lmarshal, args in global arg[] */
#include <stdio.h>
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#include "luajit.h"
int luaopen_marshal(lua_State *L);
int main(int argc, char **argv) {
  lua_State *L = luaL_newstate();
  int i;
  luaL_openlibs(L);
  luaopen_marshal(L); lua_pop(L, 1);
  lua_pushstring(L, LUAJIT_VERSION); lua_setglobal(L, "VM_VERSION");
  lua_newtable(L);
  for (i = 2; i < argc; i++) { lua_pushstring(L, argv[i]); lua_rawseti(L, -2, i - 1); }
  lua_setglobal(L, "arg");
  if (luaL_dofile(L, argv[1])) { fprintf(stderr, "ERROR: %s\n", lua_tostring(L, -1)); return 1; }
  return 0;
}
