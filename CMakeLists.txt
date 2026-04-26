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
endif()

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
set(REFS_DATA_S_IN   "${CMAKE_SOURCE_DIR}/src/common/refs_data.S.in")
set(REFS_DATA_S_OUT  "${CMAKE_BINARY_DIR}/refs_data.S")

add_custom_command(
  OUTPUT  ${REFS_BIN} ${LABELS_BIN}
  COMMAND $<TARGET_FILE:prepare_refs> ${REFS_GZ} ${REFS_BIN} ${LABELS_BIN}
  DEPENDS prepare_refs ${REFS_GZ}
  COMMENT "[prepare_refs] decoding and quantising 100k references"
  VERBATIM
)

configure_file(${REFS_DATA_S_IN} ${REFS_DATA_S_OUT} @ONLY)
add_custom_target(refs_data_assets DEPENDS ${REFS_BIN} ${LABELS_BIN})
add_library(rinha_refs OBJECT ${REFS_DATA_S_OUT})
add_dependencies(rinha_refs refs_data_assets)
set_source_files_properties(${REFS_DATA_S_OUT} PROPERTIES LANGUAGE ASM)

add_executable(api
  src/api/main.cpp
  src/api/handlers.cpp
  src/api/json_parser.cpp
  src/api/vectorize.cpp
  src/api/knn_avx2.cpp
  src/api/responses.cpp
  $<TARGET_OBJECTS:rinha_refs>
)
add_dependencies(api refs_data_assets)
target_include_directories(api PRIVATE src)
target_link_libraries(api PRIVATE Drogon::Drogon simdjson::simdjson)
set_source_files_properties(src/api/knn_avx2.cpp PROPERTIES
  COMPILE_OPTIONS "-mavx2;-mfma;-mbmi2;-mpopcnt"
)

add_executable(lb src/lb/main.cpp)
target_include_directories(lb PRIVATE src)
target_link_libraries(lb PRIVATE Drogon::Drogon)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_custom_command(TARGET api  POST_BUILD COMMAND ${CMAKE_STRIP} $<TARGET_FILE:api>  || true)
  add_custom_command(TARGET lb   POST_BUILD COMMAND ${CMAKE_STRIP} $<TARGET_FILE:lb>   || true)
endif()
