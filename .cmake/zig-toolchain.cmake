# Cross toolchain for Loongson 2K0300 (Hummingbird), LA64 v1.0, musl libc.
# Uses zig cc as compiler/linker/libc provider. LSX/LASX explicitly off.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR loongarch64)

set(_MGBA_ZIG_DIR "${CMAKE_CURRENT_LIST_DIR}")

set(CMAKE_C_COMPILER "${_MGBA_ZIG_DIR}/zigcc.sh")
set(CMAKE_C_COMPILER_TARGET loongarch64-linux-musl)
set(CMAKE_AR "${_MGBA_ZIG_DIR}/zigar.sh" CACHE FILEPATH "Archiver for zig cross build" FORCE)
set(CMAKE_RANLIB "${_MGBA_ZIG_DIR}/zigranlib.sh" CACHE FILEPATH "Ranlib for zig cross build" FORCE)
mark_as_advanced(CMAKE_AR CMAKE_RANLIB)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Search only in sysroot-ish places provided by zig
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
