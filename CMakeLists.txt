cmake_minimum_required(VERSION 3.7)

project(noah VERSION 0.5.1)

configure_file(bin/noah.in ${PROJECT_SOURCE_DIR}/bin/noah @ONLY)
configure_file(include/version.h.in ${PROJECT_SOURCE_DIR}/include/version.h)

if (CMAKE_HOST_SYSTEM_VERSION VERSION_LESS 16.0.0)
add_definitions("-DMACOS_PRE_16")
endif()

find_library(HYPERVISOR_FRAMEWORK Hypervisor)
find_library(PTHREAD_LIBRARY pthread)

set(CMAKE_C_FLAGS "-Wall -Wextra -Wno-unused-parameter -std=gnu11")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g -fsanitize=address -fno-omit-frame-pointer")
include_directories(include)

add_executable(noah
  lib/vmm.c
  src/main.c
  src/meta_strace.c
  src/base.c
  src/conv.c
  src/debug.c
  src/proc/exec.c
  src/proc/fork.c
  src/proc/process.c
  src/net/net.c
  src/ipc/futex.c
  src/ipc/signal.c
  src/fs/fs.c
  src/sys/sys.c
  src/sys/time.c
  src/mm/mm.c
  src/mm/mmap.c
  src/mm/malloc.c
  src/proc/ptrace.c
  src/mm/shm.c
  src/ipc/sem.c
  )
target_link_libraries(noah ${PTHREAD_LIBRARY} ${HYPERVISOR_FRAMEWORK})

install(PROGRAMS bin/noah DESTINATION bin)
install(TARGETS noah DESTINATION libexec)
install(FILES man/noah.1 DESTINATION ${CMAKE_INSTALL_PREFIX}/man/man1)

enable_testing()
add_subdirectory(test)
