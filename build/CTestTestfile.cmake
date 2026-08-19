# CMake generated Testfile for 
# Source directory: G:/ftp/hybrid-ftp
# Build directory: G:/ftp/hybrid-ftp/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[protocol_tests]=] "G:/ftp/hybrid-ftp/build/protocol_tests.exe")
set_tests_properties([=[protocol_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "G:/ftp/hybrid-ftp/CMakeLists.txt;120;add_test;G:/ftp/hybrid-ftp/CMakeLists.txt;0;")
add_test([=[concurrency_tests]=] "G:/ftp/hybrid-ftp/build/concurrency_tests.exe")
set_tests_properties([=[concurrency_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "G:/ftp/hybrid-ftp/CMakeLists.txt;127;add_test;G:/ftp/hybrid-ftp/CMakeLists.txt;0;")
