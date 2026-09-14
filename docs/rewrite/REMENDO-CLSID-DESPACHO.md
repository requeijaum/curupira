# REMENDO: as tres classes do arranque, no `despacho.cpp`

## O que este remendo faz, e o numero que o obriga

A bateria, commit `ecc97b0` (comando: `./build/zb2_bateria "$corpus" "$mods" /tmp/antes.json`):

    IShell::CreateInstance CLSID desconhecido pedido 3x
        iid=0x0100104f ppo=0x8007ff74                  1x   tectoy
        iid=0x01003109 ppo=0x80200380                  1x   zenonia
        iid=0x01028e3c ppo=0x8020339c                  1x   tectoy

Tres numeros, e nenhum nome. O motor ja sabia responder por nome a pergunta "que
METODO e este?" (o `despacho` nomeia cada slot); nao sabia responder a pergunta
irma, "que CLASSE e este numero?".

Este remendo liga o `core/brew/classes.{h,cpp}` ao despacho. Sao **30 linhas
acrescentadas e 2 mudadas**, em `core/brew/despacho.cpp` e em mais nenhum
ficheiro -- nao ha alteracao ao `despacho.h`, porque o modulo novo nao guarda
estado dentro do `Despacho` (os enderecos das tres classes sao funcoes puras da
faixa de saida, como os dos objectos genericos).

## As quatro ligacoes

1. `#include "core/brew/classes.h"` e `#include "core/brew/clsids.h"`.
2. `ConstruirClasses(mem_, saidas, traco_);` dentro do `InstalarAjudantes`, ao lado
   do `InstalarGl`: e o mesmo passo de construcao do sistema, e **nao um sitio que
   a bateria tenha de chamar** -- `tools/bateria.cpp` e partilhado.
3. O ramo `AtenderClasse(cpu, idx, traco_)` no `Correr`, **antes** do
   `idx == kBaseDoShell + 2` e do ramo generico `idx >= kBaseDoShell`. Os indices
   desta faixa sao 40000+, e o ramo generico (2000) apanhava-os e dava-lhes o nome
   de um metodo do IShell. **E o erro de ORDEM, que ja apareceu oito vezes nesta
   arvore**; o teste `Classes.AChamadaDeOutraFaixaNaoEApanhada` e a guarda dele.
   O `++saidas` mantem o limite de 200 recusas que os outros ramos ja tem.
4. `if (devolver == 0) devolver = zb2::brew::ObjetoDoClsid(iid);` no fim da cadeia
   do `CreateInstance`, e o **nome** no detalhe da recusa
   (`zb2::brew::DescreverClsid(iid)`).

A CHAVE da recusa (`"IShell::CreateInstance CLSID desconhecido"`) **nao muda**: e
por ela que se procura o defeito antes e depois, e muda-la partia o comando que
fica vermelho primeiro.

## O que o remendo NAO faz

- Nao muda o desfecho de nenhum CLSID fora destes tres. Os que continuam
  desconhecidos continuam a recusar, agora com o nome do SDK a frente **quando o
  SDK o declara**.
- Nao inventa um nome para o que o SDK nao declara: o `0x01011810`, que o `tectoy`
  pede a seguir, sai como `desconhecido (0x01011810)`. Ver o relatorio.

## Como aplicar

    cd <raiz do repositorio>
    git apply tools/remendo_clsid_despacho.patch
    cd curupira && cmake --build build -j4 && ./build/zb2_tests

O `git apply` recusa se o `despacho.cpp` tiver mudado entretanto, que e o
comportamento certo: o remendo foi medido contra o `ecc97b0`.

## O numero, com o remendo aplicado

    ./build/zb2_bateria "$corpus" "$mods" /tmp/depois.json
    ./build/zb2_comparar /tmp/antes.json /tmp/depois.json

    == degraus medidos (referencia -> corrida) ==
      carga: 62 -> 62 | modulo: 62 -> 62 | vtable: 48 -> 48 | applet: 41 -> 41
    resultado: 0 regressao(oes), 0 melhoria(s) em 62 titulos -- SEM REGRESSOES

    IShell::CreateInstance CLSID desconhecido   pedido 3x -> 1x
        (o que sobra e `iid=0x01011810 desconhecido (0x01011810)`, que o SDK nao
         declara em cabecalho nenhum -- ver o relatorio da frente)

E a demanda passa a dizer os METODOS, com o nome:

    ITextCtl::HandleEvent  ITextCtl::IsActive  ITextCtl::SetActive
    ITextCtl::SetInputMode ITextCtl::SetProperties ITextCtl::SetRect    (zenonia)
    IAppHistory::Back      IAppHistory::GetClass                        (tectoy)

Os seis do `ITextCtl` sao exactamente os seis que o DESMONTE do `zenonia` pede
(0x45998, 0x459b0, 0x459c8, 0x459e0, 0x66330, 0x6635c), com os argumentos do
cabecalho: `0x80010000` = `TP_FRAME|TP_FIXSETRECT`, `3` = `AEE_TM_LETTERS`, `1` =
`setActive(TRUE)`.

## O que se pede ao dono do repositorio

1. Aplicar o remendo (e a unica coisa que nao esta na branch `clsid`).
2. Uma linha no mapa de enderecos de objecto de `core/brew/igl.h` (o comentario
   "o mapa medido dos enderecos de objecto deste emulador"): depois de
   `0x800B0000 IGL | 0x800B1000 IEGL`, entra `0x8F000000 AppHistory`,
   `0x8F001000 ValueModel` e `0x8F002000 TextCtl`. **Nao alterei o `igl.h`** --
   e ficheiro da frente do GL, e a alteracao e um comentario.
3. `tools/gerar_slots.py` e `tools/brew_slots.inc` estao alterados nesta branch, e
   sao ficheiros de TODAS as frentes: tres interfaces novas (`AppHistory`,
   `ValueModel`, `TextCtl`), o `k<Interface>Slots` e o `NomeDe<Interface>(slot)`
   para cada uma das 25. O acrescento e ADITIVO -- o `brew_slots.inc` deste commit
   e byte a byte igual ao anterior nas interfaces que ja existiam (verificado com
   `diff` antes de escrever o ficheiro).
