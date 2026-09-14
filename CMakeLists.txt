# Zeebulator 2 -- arvore nova, na branch `full-rewrite`.
#
# Separada da arvore antiga (`CMakeLists.txt` na raiz) DE PROPOSITO: o
# Zeebulator da branch `new_ez_ui` fica intacto, e as duas podem ser construidas
# e comparadas lado a lado. Ver docs/rewrite/DESIGN.md.
cmake_minimum_required(VERSION 3.16)
project(zeebulator2 CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(zb2_core STATIC
  core/traco/traco.cpp
  core/memoria/memoria.cpp
  core/cpu/arm_interpreter.cpp
  core/carga/mod.cpp
  core/brew/ajudantes.cpp
)
target_include_directories(zb2_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(zb2_core PRIVATE -Wall -Wextra)

# Testes. O googletest ja esta buscado pela arvore antiga; aqui usa-se o mesmo
# padrao para nao introduzir uma segunda dependencia.
include(FetchContent)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(zb2_tests
  tests/fundacao_test.cpp
  tests/cpu_test.cpp
  tests/carga_test.cpp
)
target_link_libraries(zb2_tests PRIVATE zb2_core GTest::gtest_main)
add_test(NAME fundacao COMMAND zb2_tests)

add_executable(zb2_sonda_mod tools/sonda_mod.cpp)
target_link_libraries(zb2_sonda_mod PRIVATE zb2_core)
