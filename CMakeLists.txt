# Zeebulator 2 -- arvore nova, na branch `full-rewrite`.
#
# Separada da arvore antiga (`CMakeLists.txt` na raiz) DE PROPOSITO: o
# Zeebulator da branch `new_ez_ui` fica intacto, e as duas podem ser construidas
# e comparadas lado a lado. Ver docs/rewrite/DESIGN.md.
cmake_minimum_required(VERSION 3.16)
# O `C` ESTA AQUI POR CAUSA DO SQLITE, e nao por gosto: a amalgamacao e C
# (`third_party/sqlite3/sqlite3.c`) e um projecto so de C++ nao sabe compila-la --
# o sintoma, que nao diz nada sobre a causa, e um `Error required internal CMake
# variable not set: CMAKE_C_COMPILE_OBJECT` na geracao.
project(zeebulator2 CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# O SQLITE (a ponte do `ISQLDatabase` -- ver `core/brew/sql.h`).
#
# Vem do proprio projecto do SQLite (amalgamacao, dominio publico, 3.50.2) e esta
# versionado em `third_party/sqlite3/`, com a origem e a licenca num `LEIA-ME.md`:
# uma dependencia que se copia para dentro da arvore tem de dizer de onde veio, e
# uma que nao esta versionada faz a construcao depender da maquina de quem a
# corre. Ver `third_party/sqlite3/LEIA-ME.md`.
#
# AS FLAGS SAO AS MAGRAS, e cada uma e uma decisao:
#   `SQLITE_THREADSAFE=0`        -- o despacho corre no fio que o chamou; um
#                                   mutex por base de dados era peso sem uso.
#   `SQLITE_OMIT_LOAD_EXTENSION` -- nada carrega `.so` de fora.
#   `SQLITE_OMIT_DEPRECATED`     -- as APIs retiradas do SQLite ficam fora.
#   `SQLITE_DEFAULT_MEMSTATUS=0` -- o contador de memoria do proprio motor, fora.
add_library(zb2_sqlite3 STATIC third_party/sqlite3/sqlite3.c)
target_compile_definitions(zb2_sqlite3 PRIVATE
  SQLITE_THREADSAFE=0
  SQLITE_OMIT_LOAD_EXTENSION
  SQLITE_OMIT_DEPRECATED
  SQLITE_DEFAULT_MEMSTATUS=0
)
# O AMALGAMADO NAO SE EDITA, e nao se avisa sobre ele: 9 MB de C de terceiros que
# ninguem aqui mantem. Os avisos que interessam sao os da NOSSA arvore.
target_compile_options(zb2_sqlite3 PRIVATE -w)

add_library(zb2_core STATIC
  core/traco/traco.cpp
  core/memoria/memoria.cpp
  core/cpu/arm_interpreter.cpp
  core/carga/mod.cpp
  core/carga/bar.cpp
  core/carga/inflate.cpp
  core/carga/png.cpp
  core/carga/pack.cpp
  core/carga/pakz.cpp
  core/carga/aez.cpp
  core/brew/ajudantes.cpp
  core/brew/ajudantes_extra.cpp
  core/brew/formato.cpp
  core/brew/tela.cpp
  core/brew/vfs.cpp
  core/brew/arquivo.cpp
  core/brew/interface.cpp
  core/brew/widget.cpp
  core/brew/graficos.cpp
  core/brew/recursos.cpp
  core/brew/sha256.cpp
  core/brew/md5ctx.cpp
  core/brew/despacho.cpp
  core/brew/sql.cpp
  core/brew/ihid_entrada.cpp
  core/brew/ihiddevice.cpp
  core/brew/egl.cpp
  core/brew/igl.cpp
  core/brew/imedia.cpp
  core/brew/clsids.cpp
  core/brew/classes.cpp
  core/brew/cheats.cpp
  core/audio/misturador.cpp
  core/audio/wav.cpp
  core/video/rasterizador.cpp
  core/video/atitc.cpp
)
target_include_directories(zb2_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(zb2_core PRIVATE -Wall -Wextra)
# O SQLite e uma dependencia PRIVADA do motor: o cabecalho dele nao entra num
# `-I` publico (`core/brew/sql.cpp` inclui-o pelo caminho, `third_party/...`),
# logo nada fora do motor fica a saber que ha SQLite aqui dentro.
target_link_libraries(zb2_core PRIVATE zb2_sqlite3)
# O LZMA do sistema (liblzma / xz-utils): o decodificador LZMA_ALONE com que o
# censo em Python bateu byte a byte. Procurado e exigido, como o zlib do PACK.
find_library(ZB2_LZMA NAMES lzma)
if(NOT ZB2_LZMA)
  message(FATAL_ERROR "liblzma (xz-utils) nao encontrada -- a frente pakz precisa do lzma_alone_decoder")
endif()
target_link_libraries(zb2_core PUBLIC ${ZB2_LZMA})

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
  tests/entrada_despacho_test.cpp
  tests/sendevent_test.cpp
  tests/bar_test.cpp
  tests/egl_test.cpp
  tests/igl_test.cpp
  tests/mod_base_test.cpp
  tests/ajudantes_extra_test.cpp
  tests/media_test.cpp
  tests/widget_test.cpp
  tests/recursos_test.cpp
  tests/audio_test.cpp
  tests/rasterizador_test.cpp
  tests/atitc_test.cpp
  tests/classes_test.cpp
  tests/md5ctx_test.cpp
  tests/pack_test.cpp
  tests/pakz_test.cpp
  tests/aez_test.cpp
  tests/ecra_test.cpp
  tests/recusas_test.cpp
  tests/sql_test.cpp
  tests/ecra_guest_test.cpp
  tests/graficos_test.cpp
  tests/cheats_test.cpp
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

# A GUARDA DOS CLSIDs. Regenera `tools/clsids.inc` dos `*.bid`/`*.h` do SDK e
# compara com o ficheiro versionado. `SKIP_RETURN_CODE 77` porque sem o SDK nao
# ha cabecalho para ler -- e "nao correu" NAO e "passou".
add_test(NAME clsids_do_sdk COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/verificar_clsids.sh)
set_tests_properties(clsids_do_sdk PROPERTIES SKIP_RETURN_CODE 77)

# A GUARDA DA TABELA DO IGLES11. Regenera `tools/igles_slots.inc` de
# `AEEGLES10.h` + `AEEGLES11.h` (SDK 4.0.2 da consola) e compara com o ficheiro
# versionado. Os 148 nomes sao a diferenca entre uma lista de demanda que diz
# `IGLES11::slot67` e uma que diz `IGLES11::GetString`.
add_test(NAME slots_do_sdk_igles
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/verificar_slots_igles.sh)
set_tests_properties(slots_do_sdk_igles PROPERTIES SKIP_RETURN_CODE 77)

# A GUARDA DA TABELA DOS AJUDANTES (117 slots do `AEEHelperFuncs`). Regenera
# `tools/ajudantes_slots.inc` de `AEEStdLib.h` e compara com o versionado; o
# gerador tem ancoras internas, e `strtowstr`/`aee_GetRand` estao entre elas
# porque ja divergiram (0x0a0 em vez de 0x040, 0x090 em vez de 0x0a8).
add_test(NAME slots_do_sdk_ajudantes
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/verificar_ajudantes.sh)
set_tests_properties(slots_do_sdk_ajudantes PROPERTIES SKIP_RETURN_CODE 77)

# A SONDA DO WIDGET (a interface de formularios). E o comando que fica VERMELHO
# enquanto um pedido ao `IRootForm` nao for servido, e VERDE depois: a bateria
# nao ve esta fase, porque guarda a contagem de faltas ANTES do `EVT_APP_START`.
add_executable(zb2_sonda_widget tools/sonda_widget.cpp)
target_link_libraries(zb2_sonda_widget PRIVATE zb2_core)

add_executable(zb2_sonda_mod tools/sonda_mod.cpp)
target_link_libraries(zb2_sonda_mod PRIVATE zb2_core)

add_executable(zb2_sonda_gl tools/sonda_gl.cpp)
target_link_libraries(zb2_sonda_gl PRIVATE zb2_core)

# A SONDA DO DESCODIFICADOR (a metade C++ do auditor diferencial). Existe porque
# a comparacao com o `objdump` precisa do NOME que o NOSSO ramo de execucao deu a
# cada palavra -- e esse nome so o interpretador tem, sem uma segunda
# descodificacao que possa divergir da primeira em silencio.
add_executable(zb2_sonda_descodificador tools/sonda_descodificador.cpp)
target_link_libraries(zb2_sonda_descodificador PRIVATE zb2_core)

# A GUARDA DO AUDITOR: o espaco Thumb inteiro (65 536 meias-palavras) comparado
# com o `objdump`, e o criterio e ZERO palavras executadas como outra instrucao.
# `SKIP_RETURN_CODE 77` porque sem o binutils nao ha oraculo -- e "nao correu" NAO
# e "passou". O corpus inteiro (13 milhoes de palavras) fica fora do ctest pelo
# tempo que leva; corre-se a mao, e o comando esta no cabecalho da ferramenta.
add_test(NAME descodificador_espaco_thumb
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/guarda_descodificador.sh)
set_tests_properties(descodificador_espaco_thumb PROPERTIES
  SKIP_RETURN_CODE 77
  ENVIRONMENT "ZB2_DIR=${CMAKE_CURRENT_BINARY_DIR}")

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

# O PACK E O INFLATE (a familia Neo Geo). Os testes que abrem os `.pkg` de
# VERDADE (o `boot.pkg` do corpus e o caminho `karnovr/boot.rom`) SALTAM quando a
# midia nao esta nesta maquina -- e o `GTEST_SKIP` e o unico jeito honesto de
# dizer "nao correu", como nos `RecursosDeVerdade`.
add_test(NAME pack COMMAND zb2_tests --gtest_filter=Pack.*)
add_test(NAME pakz COMMAND zb2_tests --gtest_filter=Pakz.*)
# O AEZ (os recursos dos titulos BREW 3D). Os testes que abrem os `.aez` REAIS
# SALTAM quando a midia do corpus nao esta nesta maquina, como o `pakz`.
add_test(NAME aez COMMAND zb2_tests --gtest_filter=Aez.*)
add_test(NAME comparar_shell
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/comparar_guarda.sh $<TARGET_FILE:zb2_comparar>)

# A CORRIDA DE REGRESSAO. Precisa do corpus E da midia (o corpus62.json aponta
# para pastas de `debug_nand/mod`, que vive num disco externo). Sem a midia o
# teste e SKIP (codigo 77) e nao "passou": um teste que passa sem ter corrido e
# pior do que um teste vermelho -- e a mesma regra de `slots_do_sdk`.
# O CORPUS NAO TEM CAMINHO POR OMISSAO AQUI, e a razao e um defeito medido.
#
# Havia `"${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/corpus62.json"`, que estava certo
# enquanto esta arvore vivia em `research/sources/zeebulator/src2/`. Quando ela passou
# a `curupira/`, na raiz, o caminho deixou de existir -- e o efeito NAO foi um erro:
# foi o `regressao.sh` a sair 77 e o ctest a dizer **"SALTADO"**, com 7/7 verdes.
#
# **Um caminho invalido produz um teste que nao corre, e um teste que nao corre
# parece um teste que passou.** Deixar o `regressao.sh` procurar o corpus e o unico
# sitio onde a procura pode ser corrigida uma vez.
set(ZB2_CORPUS "$ENV{ZB2_CORPUS}" CACHE STRING "caminho do corpus62.json (vazio = procurar)")
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
