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
  core/brew/sha256.cpp
  core/brew/despacho.cpp
  core/brew/ihid_entrada.cpp
  core/brew/ihiddevice.cpp
  core/brew/igl.cpp
  core/brew/imedia.cpp
  core/audio/misturador.cpp
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
  tests/comparar_test.cpp
  tests/hid_test.cpp
  tests/bar_test.cpp
  tests/igl_test.cpp
  tests/mod_base_test.cpp
  tests/media_test.cpp
  tests/audio_test.cpp
)
# A RAIZ DO REPOSITORIO, para os testes que abrem ficheiros reais (o
# `pacmania.bar` e o SDK) poderem dizer ONDE procuraram -- e SALTAR em vez de
# passarem em silencio quando nao os encontram.
#
# Numa worktree do git, `.git` e um ficheiro ponteiro e o repositorio principal
# e outro: `--git-common-dir` da o caminho do `.git` principal, e a raiz e a
# pasta acima dele.
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
target_link_libraries(zb2_tests PRIVATE zb2_core zb2_comparar_lib GTest::gtest_main)
add_test(NAME fundacao COMMAND zb2_tests)

# A GUARDA DOS SLOTS. Corre o gerador e compara com o ficheiro versionado.
# `SKIP_RETURN_CODE 77` e o que distingue "nao correu por falta do SDK" de
# "passou": um teste que passa sem ter corrido e pior do que um teste vermelho.
add_test(NAME slots_do_sdk COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/verificar_slots.sh)
set_tests_properties(slots_do_sdk PROPERTIES SKIP_RETURN_CODE 77)

# A GUARDA DA TABELA DO IGL. Gera `tools/gl_slots.inc` de `AEEGL.h` e compara
# com o ficheiro versionado; confere tambem os 102 thunks do `conftest.elf`.
# `SKIP_RETURN_CODE 77` porque sem o instalador da extensao OpenGL ES nao ha
# cabecalho para ler -- e "nao correu" NAO e "passou".
add_test(NAME slots_do_sdk_gl COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/verificar_slots_gl.sh)
set_tests_properties(slots_do_sdk_gl PROPERTIES SKIP_RETURN_CODE 77)

add_executable(zb2_sonda_mod tools/sonda_mod.cpp)
target_link_libraries(zb2_sonda_mod PRIVATE zb2_core)

add_executable(zb2_sonda_gl tools/sonda_gl.cpp)
target_link_libraries(zb2_sonda_gl PRIVATE zb2_core)

add_executable(zb2_bateria tools/bateria.cpp)
target_link_libraries(zb2_bateria PRIVATE zb2_core)

# ---------------------------------------------------------------------------
# O COMPARADOR (etapa 9 do PLANO): o juiz das regressoes.
#
# A biblioteca existe porque o `main` do comparador tem de ser o MESMO codigo que
# o teste prova. Um teste com uma copia da regra prova que o teste e consistente
# consigo mesmo, e nao que a ferramenta esta certa.
add_library(zb2_comparar_lib STATIC tools/comparar.cpp)
target_include_directories(zb2_comparar_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(zb2_comparar_lib PRIVATE -Wall -Wextra)

add_executable(zb2_comparar tools/comparar_main.cpp)
target_link_libraries(zb2_comparar PRIVATE zb2_comparar_lib)

# As guardas do comparador, chamadas pelo BINARIO e pelo codigo de saida (e nao
# so pela API): o que o `ctest` tem de provar e o que o CI vai ver -- um codigo de
# saida distinto por cada tipo de falha.
add_test(NAME comparar_guardas COMMAND zb2_tests --gtest_filter=Comparar.*)
add_test(NAME comparar_shell
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/comparar_guarda.sh $<TARGET_FILE:zb2_comparar>)

# A CORRIDA DE REGRESSAO. Precisa do corpus E da midia (o corpus62.json aponta
# para pastas de `debug_nand/mod`, que vive num disco externo). Sem a midia o
# teste e SKIP (codigo 77) e nao "passou": um teste que passa sem ter corrido e
# pior do que um teste vermelho -- e a mesma regra de `slots_do_sdk`.
set(ZB2_CORPUS "$ENV{ZB2_CORPUS}" CACHE STRING "caminho do corpus62.json")
if(NOT ZB2_CORPUS)
  set(ZB2_CORPUS "${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/corpus62.json")
endif()
set(ZB2_MODS "$ENV{ZB2_MODS}" CACHE STRING "directorio com as pastas de mods do corpus")
set(ZB2_BASELINE "$ENV{ZB2_BASELINE}" CACHE STRING "corrida de referencia guardada")
if(NOT ZB2_BASELINE)
  set(ZB2_BASELINE "${CMAKE_CURRENT_SOURCE_DIR}/tools/baseline/bateria.json")
endif()
add_test(NAME regressao
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/regressao.sh)
set_tests_properties(regressao PROPERTIES
  SKIP_RETURN_CODE 77
  # ZB2_DIR e a pasta de build. NAO se passa `ZB2_BUILD`: essa e o COMMIT que a
  # bateria grava no cabecalho, e o script vai busca-lo ao git na hora de correr.
  ENVIRONMENT "ZB2_DIR=${CMAKE_CURRENT_BINARY_DIR};ZB2_CORPUS=${ZB2_CORPUS};ZB2_MODS=${ZB2_MODS};ZB2_BASELINE=${ZB2_BASELINE}")
# A SONDA DA ETAPA 5 (midia). E o comando que fica VERMELHO enquanto o motor nao
# conhecer o AEECLSID_MEDIA: sai com 1, e imprime o numero medido.
add_executable(zb2_sonda_media tools/sonda_media.cpp)
target_link_libraries(zb2_sonda_media PRIVATE zb2_core)
