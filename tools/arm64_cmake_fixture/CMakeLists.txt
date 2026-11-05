# A CMake project against the SDK of NEO98 (tools/test_arm64_cmake_native.py):
# C and C++ in one program, a static library of its own, a resource, a
# definition per source, a system library by name and linker options in the
# form a project for MinGW writes them.
cmake_minimum_required(VERSION 3.16)
project(arm64_cmake_fixture C CXX RC)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
add_compile_options(-O2)
add_library(parts STATIC part.c)
add_executable(fixture main.cpp plain.c fixture.rc)
set_source_files_properties(plain.c PROPERTIES COMPILE_DEFINITIONS "PLAIN_VALUE=41")
target_link_libraries(fixture parts winmm)
target_link_options(fixture PRIVATE -Wl,--subsystem,console -Wl,--entry,mainCRTStartup)
