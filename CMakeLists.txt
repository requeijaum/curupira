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
  core/carga/bar.cpp
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
  tests/bar_test.cpp
)
# A RAIZ DO REPOSITORIO, para os testes que abrem ficheiros reais (o
# `pacmania.bar` e o SDK) poderem dizer ONDE procuraram -- e SALTAR em vez de
# passarem em silencio quando nao os encontram.
#
# Numa worktree do git, `.git` e um ficheiro ponteiro e o repositorio principal
# e outro: `--git-common-dir` da o caminho do `.git` principal, e a raiz e a
# pasta acima dele. E onde vivem `research/docs` (o SDK) e o corpus.
# O `git` local ja usa este truque na arvore antiga; aqui fica explicito.
find_package(Git QUIET)
set(ZB2_RAIZ_DO_REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")
if(GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --path-format=absolute --git-common-dir
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                  OUTPUT_VARIABLE ZB2_GIT_COMUM OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(ZB2_GIT_COMUM)
    get_filename_component(ZB2_GIT_COMUM "${ZB2_GIT_COMUM}" DIRECTORY)
    set(ZB2_RAIZ_DO_REPO "${ZB2_GIT_COMUM}")
  endif()
endif()
target_compile_definitions(zb2_tests PRIVATE ZB2_RAIZ_DO_REPO="${ZB2_RAIZ_DO_REPO}")
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
