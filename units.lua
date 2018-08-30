
require "tundra.syntax.glob"
require "tundra.syntax.files"
local native = require "tundra.native"

ExternalLibrary {
    Name = "defaultConfiguration",
    Pass = "CompileGenerator",
    Propagate = {
        Libs = {Config="win32-*-*"; "User32.lib", "Gdi32.lib", "Ws2_32.lib", "shell32.lib" },
    },
}  

--============================================================================================--  

Program {
   Name = "learnyouraytracing",
   Sources = {
      "main.cpp",
   },
   Depends = { 
      "defaultConfiguration"
   },
   Env = {
      CPPPATH = {       -- keep in sync with .clang_complete file
         "extlibs", "extlibs/cml",
      },
   },
}
Default "learnyouraytracing"
