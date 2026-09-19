# CMake generated Testfile for 
# Source directory: /home/gem/project/terminal-graphic-system
# Build directory: /home/gem/project/terminal-graphic-system/build-skia
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(tgs_tests "/home/gem/project/terminal-graphic-system/build-skia/tgs_tests")
set_tests_properties(tgs_tests PROPERTIES  ENVIRONMENT "CHAR_AND_GRAPHICS=/home/gem/project/terminal-graphic-system/build-skia/char_and_graphics" _BACKTRACE_TRIPLES "/home/gem/project/terminal-graphic-system/CMakeLists.txt;285;add_test;/home/gem/project/terminal-graphic-system/CMakeLists.txt;0;")
subdirs("deps/googletest")
