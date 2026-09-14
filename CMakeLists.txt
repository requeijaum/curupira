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
  core/brew/formato.cpp
  core/brew/tela.cpp
  core/brew/vfs.cpp
  core/brew/arquivo.cpp
  core/brew/interface.cpp
  core/brew/despacho.cpp
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
  tests/brew_test.cpp
)
target_link_libraries(zb2_tests PRIVATE zb2_core GTest::gtest_main)
add_test(NAME fundacao COMMAND zb2_tests)

# A GUARDA DOS SLOTS. Corre o gerador e compara com o ficheiro versionado.
# `SKIP_RETURN_CODE 77` e o que distingue "nao correu por falta do SDK" de
# "passou": um teste que passa sem ter corrido e pior do que um teste vermelho.
add_test(NAME slots_do_sdk COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/verificar_slots.sh)
set_tests_properties(slots_do_sdk PROPERTIES SKIP_RETURN_CODE 77)

add_executable(zb2_sonda_mod tools/sonda_mod.cpp)
target_link_libraries(zb2_sonda_mod PRIVATE zb2_core)

add_executable(zb2_bateria tools/bateria.cpp)
target_link_libraries(zb2_bateria PRIVATE zb2_core)
