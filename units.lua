
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
      "extlibs/TinyJob/Job.cpp",
      "extlibs/TinyJob/JobQueue.cpp",
      "extlibs/TinyJob/JobSystem.cpp",
      "extlibs/TinyJob/Worker.cpp",

   },
   Depends = { 
      "defaultConfiguration"
   },
   Libs = {
      { Config="linux_x86-*-*"; "pthread" },
   },
   Env = {
      CPPPATH = {       -- keep in sync with .clang_complete file
         "extlibs", "extlibs/cml",
      },
   },
}
Default "learnyouraytracing"
