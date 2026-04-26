cmake_minimum_required(VERSION 3.16)
project(rinha_cpp_drogon LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(RINHA_PGO_GENERATE "Instrument hot TUs with -fprofile-generate"  OFF)
option(RINHA_PGO_USE      "Consume PGO profile via -fprofile-use"        OFF)
set(RINHA_PGO_DIR "/tmp/rinha-profraw" CACHE PATH "PGO profile-data directory")
option(RINHA_KNN_ASM "Use the hand-written inline-asm dist_one"          OFF)

if(RINHA_PGO_GENERATE AND RINHA_PGO_USE)
  message(FATAL_ERROR "RINHA_PGO_GENERATE and RINHA_PGO_USE are mutually exclusive")
endif()

add_compile_options(
  -Wall
  -Wextra
  -Wno-unused-parameter
  -fvisibility=hidden
  -fno-semantic-interposition
)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(
    -O3
    -DNDEBUG
    -march=haswell
    -mtune=haswell
    -mavx2
    -mfma
    -mbmi2
    -mpopcnt
    -mf16c
    -fno-plt
    -ffast-math
    -fno-math-errno
    -funroll-loops
  )
  add_link_options(-flto=auto)
  add_compile_options(-flto=auto)

  if(RINHA_PGO_GENERATE)
    add_compile_options(-fprofile-generate=${RINHA_PGO_DIR})
    add_link_options(-fprofile-generate=${RINHA_PGO_DIR})
  elseif(RINHA_PGO_USE)
    add_compile_options(-fprofile-use=${RINHA_PGO_DIR} -fprofile-correction)
    add_link_options(-fprofile-use=${RINHA_PGO_DIR} -fprofile-correction)
  endif()
endif()

set(RINHA_HOTFLAGS
  -fno-exceptions
  -fno-rtti
  -fno-stack-protector
  -fno-asynchronous-unwind-tables
  -falign-functions=64
)

find_package(Drogon CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

find_package(simdjson CONFIG QUIET)
if(NOT simdjson_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG        v3.10.1
    GIT_SHALLOW    TRUE
  )
  set(SIMDJSON_BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
  set(SIMDJSON_DEVELOPER_MODE OFF CACHE BOOL "" FORCE)
  set(SIMDJSON_JUST_LIBRARY ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(simdjson)
endif()

add_executable(prepare_refs tools/prepare_refs.cpp)
target_link_libraries(prepare_refs PRIVATE simdjson::simdjson ZLIB::ZLIB)
target_compile_options(prepare_refs PRIVATE -O2)

set(REFS_GZ          "${CMAKE_SOURCE_DIR}/resources/references.json.gz")
set(REFS_BIN         "${CMAKE_BINARY_DIR}/refs.bin")
set(LABELS_BIN       "${CMAKE_BINARY_DIR}/labels.bin")
set(NORMS_BIN        "${CMAKE_BINARY_DIR}/norms.bin")
set(REFS_DATA_S_IN   "${CMAKE_SOURCE_DIR}/src/common/refs_data.S.in")
set(REFS_DATA_S_OUT  "${CMAKE_BINARY_DIR}/refs_data.S")

add_custom_command(
  OUTPUT  ${REFS_BIN} ${LABELS_BIN} ${NORMS_BIN}
  COMMAND $<TARGET_FILE:prepare_refs> ${REFS_GZ} ${REFS_BIN} ${LABELS_BIN} ${NORMS_BIN}
  DEPENDS prepare_refs ${REFS_GZ}
  COMMENT "[prepare_refs] decoding refs"
  VERBATIM
)

configure_file(${REFS_DATA_S_IN} ${REFS_DATA_S_OUT} @ONLY)
add_custom_target(refs_data_assets DEPENDS ${REFS_BIN} ${LABELS_BIN} ${NORMS_BIN})
add_library(rinha_refs OBJECT ${REFS_DATA_S_OUT})
add_dependencies(rinha_refs refs_data_assets)
set_source_files_properties(${REFS_DATA_S_OUT} PROPERTIES
  LANGUAGE ASM
  OBJECT_DEPENDS "${REFS_BIN};${LABELS_BIN};${NORMS_BIN}"
)

add_executable(api
  src/api/main.cpp
  src/api/handlers.cpp
  src/api/json_fast.cpp
  src/api/vectorize.cpp
  src/api/knn_avx2.cpp
  src/api/responses.cpp
  src/common/refs_residency.cpp
  $<TARGET_OBJECTS:rinha_refs>
)
add_dependencies(api refs_data_assets)
target_include_directories(api PRIVATE src)
target_link_libraries(api PRIVATE Drogon::Drogon)

set(RINHA_HOTFLAGS_STR "")
foreach(_f IN LISTS RINHA_HOTFLAGS)
  if(RINHA_HOTFLAGS_STR STREQUAL "")
    set(RINHA_HOTFLAGS_STR "${_f}")
  else()
    set(RINHA_HOTFLAGS_STR "${RINHA_HOTFLAGS_STR};${_f}")
  endif()
endforeach()

set_source_files_properties(
  src/api/json_fast.cpp
  src/api/vectorize.cpp
  src/api/knn_avx2.cpp
  src/common/refs_residency.cpp
  PROPERTIES
  COMPILE_OPTIONS "${RINHA_HOTFLAGS_STR}"
)

set(RINHA_KNN_FLAGS "${RINHA_HOTFLAGS_STR};-mavx2;-mfma;-mbmi2;-mpopcnt;-funroll-all-loops;-fpeel-loops;-ftree-vectorize")
if(RINHA_KNN_ASM)
  set(RINHA_KNN_FLAGS "${RINHA_KNN_FLAGS};-DRINHA_KNN_ASM=1")
endif()
set_source_files_properties(src/api/knn_avx2.cpp PROPERTIES
  COMPILE_OPTIONS "${RINHA_KNN_FLAGS}"
)

add_executable(lb src/lb/main.cpp)
target_include_directories(lb PRIVATE src)
target_link_libraries(lb PRIVATE Drogon::Drogon)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_custom_command(TARGET api  POST_BUILD COMMAND ${CMAKE_STRIP} $<TARGET_FILE:api>  || true)
  add_custom_command(TARGET lb   POST_BUILD COMMAND ${CMAKE_STRIP} $<TARGET_FILE:lb>   || true)
endif()
