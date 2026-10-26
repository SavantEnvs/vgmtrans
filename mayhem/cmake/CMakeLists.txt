# Out-of-tree wrapper project: pulls in upstream's CMake (core library only, no Qt UI / shell) and
# builds the fuzz harnesses + known-answer self-test against its `vgmtranscore` object library.
cmake_minimum_required(VERSION 3.25)
project(vgmtrans_mayhem C CXX)

get_filename_component(VGMTRANS_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
get_filename_component(MAYHEM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# upstream's top-level CMakeLists appends ${CMAKE_SOURCE_DIR}/cmake, which is this wrapper's dir
list(APPEND CMAKE_MODULE_PATH "${VGMTRANS_SRC}/cmake")
set(ENABLE_UI_QT OFF CACHE BOOL "" FORCE)
set(ENABLE_SHELL OFF CACHE BOOL "" FORCE)
add_subdirectory("${VGMTRANS_SRC}" vgmtrans EXCLUDE_FROM_ALL)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_CXX_STANDARD 20)

set(HARNESS_ENGINE "" CACHE STRING "link option for the fuzzing engine (e.g. -fsanitize=fuzzer)")
set(STANDALONE_MAIN "" CACHE FILEPATH "run-once driver (C source) for the -standalone binaries")
set(LSAN_OFF "" CACHE FILEPATH "TU defining __lsan_is_turned_off (empty = none)")

function(vgmtrans_exe name)
  add_executable(${name} ${ARGN})
  target_include_directories(${name} PRIVATE "${MAYHEM_DIR}")
  target_link_libraries(${name} PRIVATE vgmtranscore)
endfunction()

if(HARNESS_ENGINE)
  foreach(t scan convert)
    set(defs "")
    if(t STREQUAL "convert")
      set(defs FUZZ_CONVERT=1)
    endif()
    vgmtrans_exe(fuzz_${t} "${MAYHEM_DIR}/fuzz_scan.cpp" ${LSAN_OFF})
    target_compile_definitions(fuzz_${t} PRIVATE ${defs})
    target_link_options(fuzz_${t} PRIVATE ${HARNESS_ENGINE})

    vgmtrans_exe(fuzz_${t}-standalone "${MAYHEM_DIR}/fuzz_scan.cpp" ${LSAN_OFF} "${STANDALONE_MAIN}")
    target_compile_definitions(fuzz_${t}-standalone PRIVATE ${defs})
  endforeach()
  vgmtrans_exe(fuzz_open "${MAYHEM_DIR}/fuzz_open.cpp" ${LSAN_OFF})
endif()

vgmtrans_exe(selftest "${MAYHEM_DIR}/selftest.cpp")
