# AUDITORIA — classes/CLSID, os 117 ajudantes e a cablagem

Sub-agente: `aud-classes-ajudantes`.
Arvore auditada: `/tmp/wt-auditoria/curupira` (branch `auditoria`, base `full-rewrite`),
revisao `0286921` (`git rev-parse --short HEAD`).
Ficheiros auditados (so estes): `core/brew/classes.cpp`+`.h`, `core/brew/clsids.cpp`+`.h`,
`core/brew/ajudantes.cpp`+`.h`, `core/brew/ajudantes_extra.cpp`+`.h`,
`core/brew/interface.cpp`+`.h`, `tools/clsids.inc`, `tools/ajudantes_slots.inc`,
`tools/brew_slots.inc`.

Estado da arvore no momento da auditoria: `git status --short` mostrou
` M tests/widget_test.cpp` (38 linhas a mais, frente dos widgets). **NAO e meu**: nao
modifiquei ficheiro nenhum. Nao corri `cmake` nem build.

---

## 0. A PERGUNTA CENTRAL — os 117 slots e as classes CLSID

### Os 117 slots de `AEEHelperFuncs`

Contagem respondida por MEDICAO, nao por leitura do `.inc`:

| pergunta | resposta | como foi medido |
|---|---|---|
| quantos slots existem | **117** | parse do `struct AEEHelperFuncs` de `AEEStdLib.h` do SDK: 117 campos `(*nome)(...)`; o `.inc` tem 117 `kDeclaracoes` |
| os nomes e os offsets conferem? | **zero divergencias** | comparei campo a campo (nome e `4*posicao`) os 117: 117/117 iguais |
| o cabecalho e o mesmo que gerou o `.inc`? | **sim** | sha256 do `AEEStdLib.h` = `b9492fe63407fdcfd49473f790328f7a84b73e968012debf5b24dcac7dced1e7`, igual ao que o `.inc` declara na linha 4 |
| a guarda corre? | **passa** | `sh tools/verificar_ajudantes.sh` -> `OK: ajudantes_slots.inc corresponde a AEEStdLib.h (117 campos).` (exit 0) |
| **quantos RECUSAM com o nome?** | **96** | ver a conta abaixo |
| **quantos devolvem zero sem registar?** | **0 no caminho da demanda; 1 sitio na cablagem** (`TabelaDeAjudantes::Instalar`, achado S1) | ver abaixo |

A conta dos 117 no motor (`core/brew/despacho.cpp`, fora do meu lote mas necessario para
responder):

- **16** offsets tem implementacao propria do despacho e estao ligados na tabela `kLigados`
  (`despacho.cpp:271-290`): 0x000 memmove, 0x004 memset, 0x008 strcpy, 0x010 strcmp,
  0x014 strlen, 0x018 strchr, 0x020 sprintf, 0x040 strtowstr, 0x068 malloc, 0x06c free,
  0x08c GetAEEVersion, 0x09c dbgprintf, 0x0a8 aee_GetRand, 0x0b0 aee_GetUpTimeMS,
  0x0c0 GetAppInstance, 0x13c vsprintf. Conferi que cada um dos 16 tem o ramo
  `idx == <constante>` no despacho.
- **5** tem implementacao em `core/brew/ajudantes_extra.cpp:431-437`: 0x044 wstrtostr,
  0x050 utf8towstr, 0x0d8 strstr, 0x0e8 stristr, 0x138 GetRAMFree.
- **96** = 117 - 16 - 5 nao tem implementacao e **RECUSAM COM NOME**:
  `ajudantes_extra.cpp:502-503` emite `RegistarFalta` com
  `AEEHelperFuncs[0xNNN] <nome do SDK>` + `| assinatura: <texto do cabecalho>` e escreve
  `R0 = kAeeUnsupported` = 20 (`AEEStdErr.h:36`). **Nenhum destes 96 devolve zero em silencio.**
- Verifiquei o modelo no registo real: a demanda da bateria
  (`tools/baseline/bateria.json`) nao tem um unico nome de forma `AEEHelperFuncs[0x7xx]`
  (o que aconteceria se um offset ligado caisse no ramo generico). So aparecem
  `AEEHelperFuncs[0x140] vsnprintf` (116 chamadas, 4 titulos) e
  `AEEHelperFuncs[0x074] realloc` (9 chamadas, 3 titulos) -- os dois com NOME do SDK.

### As classes CLSID

| pergunta | resposta | medicao |
|---|---|---|
| quantos valores no `.inc` | 2477 constantes de nome, **1764 valores distintos**, tabela por valor ordenada e sem repetidos | parse do `tools/clsids.inc` + `static_assert(Ordenada())` em `clsids.cpp:14-20` |
| a guarda corre? | **passa** | `sh tools/verificar_clsids.sh` -> `OK: clsids.inc corresponde aos cabecalhos (2477 constantes, os 3 do corpus conferidos).` (exit 0, 3.9 s) |
| os 3 CLSIDs do corpus existem no cabecalho? | **sim, conferidos por expressao** | 0x0100104f = `AEEAppHistory.bid:9` (`#define AEECLSID_AppHistory 0x0100104f`); 0x01003109 = `AEEClassIDs.h:209` (`AEECLSID_TEXTCTL_10 + 0x100`, com `AEECLSID_CONTROL = QVERSION + 0x3000` e `_10 = CONTROL+9` -> 0x01003009 + 0x100); 0x01028e3c = `AEECLSID_VALUEMODEL_1.bid:31` |
| os slots das 3 interfaces conferem com o SDK? | **sim** | `AEEIAppHistory.h`: `INHERIT_IQI`(3) + Forward, Back, Top -> **Top = 5** (= `kAppHistory_Top`, `brew_slots.inc:405`), 16 slots; `AEEText.h:199-224`: `QINTERFACE` + `DECLARE_IBASE`(2) + `DECLARE_ICONTROL`(9, `AEE.h:297-306`, com `Reset` no fim) + 17 metodos -> 28 slots, `SetRect=6`, `SetProperties=8`, `SetInputMode=18` (= `brew_slots.inc:473`), `SetActive=4` -- todos iguais aos do `.inc` |
| as classes recusam com nome? | **sim, 8 recusas com nome na bateria** | demanda: `ITextCtl::SetRect`, `ITextCtl::SetProperties`, `ITextCtl::SetInputMode`, `ITextCtl::SetActive`, `ITextCtl::IsActive`, `ITextCtl::HandleEvent` (1x cada) e `IAppHistory::Back`, `IAppHistory::GetClass` (1x cada). Nao ha nenhum `ITextCtl::slot28` nem numero cru |
| **e o que devolve sucesso sem fazer nada?** | **`IAppHistory::Top`** (achado S4) | `classes.cpp:152-155` -> `cpu.Set(kR0, kAeeSuccess)` sem trabalho e **sem evento nenhum** |

Nota de contagem honesta: `IShell::CreateInstance CLSID desconhecido` continua a aparecer 16 vezes em 13
titulos (`IShell::CreateInstance CLSID desconhecido`), mas sao OUTROS IIDs -- os tres
deste lote deixaram de aparecer como numeros e passaram a aparecer como nomes de metodo.

---

## 1. CONVENCAO DESTA TABELA

- `SILENCIOSO` — sucesso (ou resposta que se le como resposta) e nada feito, **sem rasto**.
- `REGISTADO` — recusa ou no-op que deixa rasto com nome. Nao e defeito: e o modelo. Fica na
  tabela para a conta da pergunta central ficar verificavel.
- `CANDIDATO` — achado confirmado no codigo, mas sem sintoma medido hoje (falta chamador,
  falta titulo, ou so aparece numa condicao que o corpus nao exercita). Cada um diz por que.

Ordenada por gravidade.

| ficheiro:linha | o que devolve | o que devia fazer | gravidade |
|---|---|---|---|
| `core/brew/ajudantes.cpp:172` (com `:174`) | escreve **0** no slot do guest para TODA a funcao declarada e implementada, e devolve `ok=true`, sem evento | escrever o endereco de saida da implementacao (precisa de `Saidas`) e **registar** cada slot que nao consiga ligar | SILENCIOSO |
| `core/brew/ajudantes.cpp:76` | `Malloc` sem espaco: `++falhas_; return 0;` — zero no rasto; `Falhas()` so e lido pelo ctest e pela sonda | `RegistarFalta`/`Emitir` com o tamanho pedido (`traco_` existe no objecto e nao e usado aqui) | SILENCIOSO |
| `core/brew/ajudantes.cpp:129` | `Realloc` sem espaco: `if (novo == 0) return 0;` — zero no rasto | idem: registar a falha com o tamanho | SILENCIOSO |
| `core/brew/classes.cpp:154` | `cpu.Set(kR0, kAeeSuccess)` para `IAppHistory::Top`: sucesso, nenhum trabalho, **nenhum evento** | deixar o evento da aceitacao (o `imedia` faz isso: aceita, nao aplica, e o evento diz o nome) | SILENCIOSO |
| `core/brew/ajudantes_extra.cpp:376-377` | ignora o valor devolvido por `LerCadeia`: um palheiro sem NUL nos primeiros 65536 bytes fica TRUNCADO e a busca devolve 0 ("nao encontrado"), sem rasto | comparar o resultado com `kLimiteDeCadeia` e registar a truncagem | SILENCIOSO |
| `core/brew/classes.cpp:174-175` | recusa com `ITextCtl::SetInputMode` + r0=20 no traco | (e o que faz: modelo a seguir) | REGISTADO |
| `core/brew/ajudantes_extra.cpp:502-503` | recusa os 96 slots com `AEEHelperFuncs[0xNNN] nome` + assinatura + r0=20 | (e o que faz: modelo a seguir) | REGISTADO |
| `core/brew/ajudantes.cpp:86-90` | `free` de endereco fora do heap: conta e emite `FREE_FORA_DO_HEAP` | (e o que faz: modelo a seguir) | REGISTADO |
| `core/brew/interface.cpp:61` (+ `:72`) | `Cablar` escreve a cablagem e devolve `{ok,motivo}` — mas **nao tem um unico chamador no motor**: os 5 sitios sao `tests/brew_test.cpp:253,267,280,289,304` | ou o motor passa a usar o `Cablar` (uma cablagem so), ou a funcao sai; as guardas dela nao protegem nada hoje | CANDIDATO |
| `core/brew/interface.cpp:45-48` | o limite da vtable e `l.vt + kSlotsPorVtable` (64) para qualquer `vt >= 9000`: numa vtable de CLASSE (32 slots, `classes.h:115`) aceita os slots 32..63 e escreve na vtable da classe SEGUINTE | usar o numero de slots REAL da vtable que esta a ser cablada | CANDIDATO |
| `core/brew/interface.cpp:14-26` | `ConstruirObjeto` escreve `saidas.Endereco(base+i)` sem conferir `saidas.quantos`/`ativa`: um indice fora da faixa da um endereco que o laco nunca reconhece | recusar e registar, como faz `core/brew/widget.cpp:52-60` ("em SILENCIO, que e a pior especie") e `ihiddevice.cpp:85` | CANDIDATO |
| `core/brew/interface.cpp:75-91` | `NomeDoAjudante` devolve nomes de 14 offsets; o unico chamador e `despacho.cpp:1113`, e ai **nunca e alcancado**: `AtenderAjudanteExtra` toma primeiro TODOS os 117 offsets do catalogo | apagar e usar sempre `NomeDoAjudanteDoSdk` (o comentario do `ajudantes_extra.h:53-57` diz que estes 14 nomes sairam de `interface.cpp`: nao sairam) | CANDIDATO |
| `core/brew/ajudantes_extra.h:83` (+ `.cpp:504,510`) | `recusas_` conta por instancia, mas `AtenderAjudanteExtra` constroi um `AjudantesExtra` temporario por chamada: no motor o contador e sempre 0 | contador estatico, ou objecto persistente; o ctest ve-o porque usa um objecto que vive | CANDIDATO |
| `core/brew/ajudantes.cpp:119-129` | `Realloc` nao valida que `endereco` seja um bloco NOSSO (o `Free` valida): com lixo, `capacidade >= tamanho` devolve o proprio endereco como sucesso, sem rasto | validar como o `Free` faz e registar; nota: o slot 0x74 **nao esta ligado** ao `Alocador::Realloc` (a demanda pede `AEEHelperFuncs[0x074] realloc` 9x em 3 titulos) | CANDIDATO |
| `core/brew/ajudantes.cpp:93-96` | `Free` de um bloco ja livre nao e detetado: volta a somar `livre=1` e subtrai outra vez a `alocado_` (o `min` evita o underflow) | detetar e registar o `free` duplo (o `free` fora do heap ja e registado) | CANDIDATO |
| `core/brew/ajudantes_extra.cpp:425-428` | `Implementacao::nome` e escrito nas 5 linhas de `kImplementados` e **nunca lido** (o nome vem do catalogo) | tirar o campo ou usa-lo no registo | CANDIDATO |
| `core/brew/ajudantes.h:78` | `Blocos()` nunca e lido em lado nenhum do repositorio | tirar | CANDIDATO |
| `core/brew/ajudantes.h:82-84` | o comentario diz que os 8 bytes de reserva do cabecalho existem "para o `realloc` poder crescer sem mover dados quando o vizinho esta livre"; o `Realloc` **nunca** cresce no lugar (aloca outro e copia) | tirar o campo ou corrigir o comentario | CANDIDATO |
| `core/brew/classes.h:125` | comentario diz `0x800C0000 + k*0x1000`; o codigo usa `0x8F000000`, e o MESMO cabecalho (linhas 56-83) diz que o `0x800C0000` NAO estava livre | corrigir o comentario | CANDIDATO |
| `core/brew/interface.cpp:8-14` | `kGenericos` (7 entradas) esta copiado em `tools/bateria.cpp:147-152` com os mesmos valores | uma lista so: a segunda copia de um numero medido e o defeito que o projeto ja pagou duas vezes | CANDIDATO |
| `tools/ajudantes_slots.inc` (`linha` dos registos) | **17 dos 117** registos apontam a linha do COMENTARIO da seccao, 1 a 3 linhas acima da declaracao (ex.: 0x008 diz 52; a declaracao esta na 53) | corrigir o `tools/nomear_ajudantes.py` | CANDIDATO |
| `core/brew/ajudantes_extra.cpp:374` | `strstr`/`stristr` com ponteiro nulo devolve 0 ("nao encontrado") sem evento | registar; o cabecalho (`AEEStdLib.h:152,156`) nao define este caso, logo o valor nao e uma medicao | CANDIDATO |
| `core/brew/classes.cpp:110-112` e `:133,141` | chaves de falta escritas `classes_da_brewm_sem_slots` / `classes_da_brewm_cablagem_perdida` ("brewm") | `..._brew_...` (cosmetico: a chave e o que a demanda mostra) | CANDIDATO |

---

## 2. CITACAO LITERAL DAS LINHAS ACUSADAS

Todas as citacoes foram extraidas do ficheiro na revisao `0286921`, com o texto exacto
(posso confirma-las por `grep -n`). Prefixo `ficheiro:linha|`.

```
core/brew/ajudantes.cpp:76|  return 0;
core/brew/ajudantes.cpp:89|      traco_->Emitir(Area::Brew, Nivel::Aviso, "FREE_FORA_DO_HEAP", buf);
core/brew/ajudantes.cpp:127|  if (capacidade >= tamanho) return endereco;  // ja cabe
core/brew/ajudantes.cpp:129|  if (novo == 0) return 0;
core/brew/ajudantes.cpp:94|  c.livre = 1;
core/brew/ajudantes.cpp:96|  alocado_ -= std::min(alocado_, c.tamanho);
core/brew/ajudantes.cpp:143|    : mem_(mem), alocador_(alocador), traco_(traco) {}
core/brew/ajudantes.cpp:170|  for (const SlotDeAjudante& s : slots_) {
core/brew/ajudantes.cpp:171|    if (!s.implementado()) continue;
core/brew/ajudantes.cpp:172|    mem_.Escrever32(endereco + s.offset, 0);
core/brew/ajudantes.cpp:174|  r.ok = true;
core/brew/ajudantes.cpp:175|  r.endereco = endereco;
core/brew/ajudantes.h:78|  std::uint32_t Blocos() const { return blocos_; }
core/brew/ajudantes.h:82|  // Cabecalho de bloco: 16 bytes, alinhado a 8. Guarda o tamanho do bloco
core/brew/ajudantes.h:83|  // (cabecalho incluido) e se esta livre. Os outros 8 bytes ficam de reserva
core/brew/ajudantes.h:84|  // para o `realloc` poder crescer sem mover dados quando o vizinho esta livre.
core/brew/ajudantes.h:124|  // Escreve a tabela no espaco do guest e devolve o endereco.
core/brew/ajudantes.h:126|  // RECUSA a instalar quando algum slot declarado esta por preencher, e diz
```

```
core/brew/classes.cpp:100|  return k == kClasseDoAppHistory && slot == brew_slots::kAppHistory_Top;
core/brew/classes.cpp:110|      traco.RegistarFalta(Area::Brew, "classes_da_brewm_sem_slots",
core/brew/classes.cpp:133|      traco.RegistarFalta(Area::Brew, "classes_da_brewm_cablagem_perdida",
core/brew/classes.cpp:141|        traco.RegistarFalta(Area::Brew, "classes_da_brewm_cablagem_perdida", det);
core/brew/classes.cpp:152|  if (SlotDaClasseImplementado(k, slot)) {
core/brew/classes.cpp:153|    // `int Top(po)` -- ver a justificacao em `SlotDaClasseImplementado`.
core/brew/classes.cpp:154|    cpu.Set(kR0, kAeeSuccess);
core/brew/classes.cpp:155|    return true;
core/brew/classes.cpp:174|  traco.RegistarFalta(Area::Brew, nome, det);
core/brew/classes.cpp:175|  cpu.Set(kR0, kAeeUnsupported);
core/brew/classes.h:125|constexpr std::uint32_t ObjetoDaClasse(std::uint32_t k) { return 0x8F000000u + k * 0x1000u; }
```

```
core/brew/interface.cpp:8|const Generico kGenericos[7] = {
core/brew/interface.cpp:25|    mem.Escrever32(vtable + i * 4, saidas.Endereco(base_dos_slots + i));
core/brew/interface.cpp:45|    const std::uint32_t fim = (l.vt >= kVtableGenericoBase)
core/brew/interface.cpp:46|                                  ? VtGenerico((l.vt - kVtableGenericoBase) / kPassoGenerico + 1u)
core/brew/interface.cpp:47|                                  : l.vt + kSlotsPorVtable;
core/brew/interface.cpp:48|    if (l.vt + l.slot >= fim) {
core/brew/interface.cpp:61|    mem.Escrever32(saidas.Endereco(l.vt) + l.slot * 4, saidas.Endereco(l.saida));
core/brew/interface.cpp:68|      return {false, "cablagem perdida: " + Hex(l.vt) + " slot " +
core/brew/interface.cpp:72|  return {true, ""};
core/brew/interface.cpp:75|const char* NomeDoAjudante(std::uint32_t offset) {
core/brew/interface.cpp:77|    case 0x000: return "memmove";
core/brew/interface.cpp:91|    default: return nullptr;
```

```
core/brew/ajudantes_extra.cpp:374|  if (p_palheiro == 0 || p_agulha == 0) return 0;
core/brew/ajudantes_extra.cpp:376|  mem.LerCadeia(p_palheiro, &palheiro, kLimiteDeCadeia);
core/brew/ajudantes_extra.cpp:377|  mem.LerCadeia(p_agulha, &agulha, kLimiteDeCadeia);
core/brew/ajudantes_extra.cpp:427|  const char* nome;
core/brew/ajudantes_extra.cpp:502|  RegistarRecusa(traco_, offset, "por implementar nesta etapa", DetalheDosRegistos(cpu));
core/brew/ajudantes_extra.cpp:503|  cpu.Set(kR0, kAeeUnsupported);
core/brew/ajudantes_extra.cpp:510|  AjudantesExtra extra(mem, alocador, traco);
core/brew/ajudantes_extra.h:83|  std::uint64_t RecusasRegistadas() const { return recusas_; }
```

A linha que sustenta o achado S1 no proprio teste (nao e meu ficheiro, mas e a prova de que
o teste passa com 0 na memoria):

```
tests/carga_test.cpp:403|  const auto r = t.Instalar(kTabela, /*permitir_por_implementar=*/true);
tests/carga_test.cpp:404|  EXPECT_TRUE(r.ok);
tests/carga_test.cpp:405|  EXPECT_EQ(r.faltam.size(), 1u) << "mesmo aceitando, o que falta fica dito";
```

e o chamador que remenda o que o `Instalar` deixou a zero:

```
tools/sonda_mod.cpp:99|  mem.Escrever32(kTabela + kSlotMalloc, cpu.GetSaidas().Endereco(0));
tools/sonda_mod.cpp:100|  mem.Escrever32(kTabela + kSlotFree, cpu.GetSaidas().Endereco(1));
```

---

## 3. NAO E DEFEITO MAS PARECE

1. **`classes.cpp:105-113`** — a guarda `quantos == 0 || quantos > kSlotsDaClasse` nunca
   dispara com os cabecalhos de hoje (16/9/28 contra 32) e o proprio comentario di-lo. Nao e
   defeito: se disparasse, **regista** e nao cria o objecto. (Codigo morto defensivo.)
2. **`classes.cpp:166-168`** — o ramo "slot fora da tabela do cabecalho" (`ITextCtl::slot28`)
   nunca apareceu na bateria. Nao e defeito: e exactamente a defesa que evita um `bx` para
   memoria que nao existe, e nomeia o que nao tem nome.
3. **`classes.cpp:148`** — `AtenderClasse` devolve `false` fora da faixa 40000..40095. Nao e
   "aceitar e ignorar": e a convencao que faz o despacho seguir a cadeia de ramos.
4. **`ajudantes_extra.cpp:102-109`** — `wstrtostr` com `nSize == 0` devolve o destino e NAO
   emite evento. NAO e defeito: o cabecalho define-o assim ("If this is 0, this function does
   not do any conversion, but returns pszDest"), logo e um no-op do CONTRATO -- como o
   `IHeap1::Lock` do P6.
5. **`ajudantes_extra.cpp:114-128`** — `wstrtostr` trunca quando o destino nao cabe e escreve
   o NUL, sem registar. O comportamento e o do proprio SDK (as duas implementacoes que o
   comentario cita, `OEMBREWSettings.c:363` e `OEMPDPSettings_common.h:592`); a comparacao que
   fica e de INCONSISTENCIA: o `utf8towstr`, seis funcoes acima, REGISTA "destino pequeno de
   mais". O silencio aqui nao tem sintoma medido, mas o rasto e barato.
6. **`ajudantes_extra.cpp:300-325`** (`PercorrerHeap`) e **`ajudantes.cpp:82-91`** — recusas
   com rasto. Modelo.
7. **`clsids.cpp:36`** — `NomeDoClsid` devolve `nullptr` quando o SDK nao declara o valor, e
   `DescreverClsid` escreve `desconhecido (0x...)`. Nao e "zero sem registar": o nome do
   chamador e que decide, e o `despacho` regista a falta com o texto do `DescreverClsid`.
8. **`tools/brew_slots.inc`** — as 22 interfaces tem `k<Ifce>Slots` igual ao maior slot + 1
   (conferi as 22). O +1 que eu contei primeiro era a propria constante `k<Ifce>Slots`.
   Nenhum defeito.
9. **`tools/clsids.inc`** — a `kQuantosValores = 1764` bate com as 1764 entradas de
   `kPorValor`; 713 dos 2477 nomes nao chegam a `NomeDoClsid` (o gerador guarda UM nome por
   valor). Nao e defeito: e o desenho, e o valor de 0x0100104f guarda o nome
   `AEECLSID_AppHistory`, igual ao que o `classes.cpp` usa nas fichas.
10. **`ajudantes.h:52-65`** — os 7 codigos de erro conferem com `AEEStdErr.h` linha a linha
    (0/1/3/14/20/39/47 nas linhas 16/17/19/30/36/55/63). O enum e honesto.

---

## 4. CONTRADIZ O QUE SE PENSAVA

1. **"o `TabelaDeAjudantes` e a tabela do sistema"** — NAO E. O motor nao usa
   `TabelaDeAjudantes` em lado nenhum (`grep`): quem instala a `AEEHelperFuncs` e o
   `Despacho::InstalarAjudantes`, com a sua propria lista `kLigados` e um laco sobre os 117.
   O `TabelaDeAjudantes` so e usado por `tools/sonda_mod.cpp` e pelo ctest. Pior: o campo
   `SlotDeAjudante::funcao` (`std::function` com a implementacao) **nunca e invocado em
   ficheiro nenhum** -- as duas implementacoes dos testes e as da sonda sao codigo morto.
2. **"a cablagem passa pelo `Cablar`"** — NAO PASSA. `Cablar` tem 0 chamadores no motor
   (so `tests/brew_test.cpp`). As suas duas guardas (slot da IBase; slot fora da vtable) nao
   protegem objecto nenhum a correr. Cada componente fez a sua cablagem e a sua conferencia.
3. **"os 14 nomes escritos a mao foram tirados de `interface.cpp`"** — NAO FORAM. O
   `ajudantes_extra.h:53-57` afirma isso e a `switch` continua la (`interface.cpp:75-91`),
   agora como codigo morto, porque o `AtenderAjudanteExtra` apanha os 117 offsets antes.
4. **"`ConstruirObjeto` confere a faixa"** — nao confere. O `widget.cpp:48-60` faz a conta
   (`ultimo >= saidas.quantos`) e escreve no comentario que o defeito contrario "em SILENCIO,
   que e a pior especie" -- mas a conta esta DENTRO do widget, e nao na funcao que escreve os
   enderecos para todos os modulos que a chamam (classes, ihiddevice, ihid_entrada, igl, egl).
   Os testes chamam-no com a faixa por omissao (`Saidas s;` -> `quantos = 0`) e passam.
5. **"o que falta na tabela dos ajudantes e o que a bateria nao pede"** — o mais pedido de
   todos e um que falta: `AEEHelperFuncs[0x140] vsnprintf`, **116 chamadas em 4 titulos**
   (`alice` 65, `zeeboids` 49, `dodgeball` 1, `zeebopeteca` 1). E o `vsprintf` (0x13c) esta
   implementado -- o irmao directo. Depois dele, `AEEHelperFuncs[0x074] realloc` com 9
   chamadas em 3 titulos (`gof`, `rmp`, `pbc`), e o `Alocador::Realloc` escrito e testado no
   ctest mas **nao ligado ao slot**.
6. **"o `Top` e uma implementacao"** — e uma resposta. `IAppHistory::Top` recebe
   `AEE_SUCCESS` e nao faz trabalho nenhum (nao ha historia neste emulador). A justificacao do
   `classes.cpp:91-99` e o cabecalho fecham a conta, mas a aceitacao nao deixa evento: a
   bateria nao pode mostrar que um titulo pediu historia e recebeu "sim" (`grep -o 'Top'
   tools/baseline/bateria.json` = **0**). O `imedia` -- o caso MODELO citado no meu enunciado
   -- aceita, nao aplica e REGISTA. A diferenca entre os dois e exactamente a que o P2
   persegue.
7. **"nao medido = nao medido"** — o que NAO consegui medir de demanda: `strstr`, `stristr`,
   `wstrtostr`, `utf8towstr` e `GetRAMFree` nao aparecem em `tools/baseline/bateria.json`
   (0 ocorrencias de cada nome) porque ja estao implementados e a recusa deixou de existir; e
   `TabelaDeAjudantes`/`Instalar` tambem nao aparecem (0) porque o motor nao as usa. Nao ha,
   no corpus medido, um titulo a pedir os 14 nomes do `NomeDoAjudante`, nem `ITextCtl::slot28`.

---

## 5. O QUE NAO FIZ

- Nao corri `cmake` nem nenhum build (o agente pai constroi agora).
- Nao modifiquei ficheiro nenhum do repositorio. Escrevi so este relatorio.
- Corri duas guardas de leitura do proprio projeto (`tools/verificar_ajudantes.sh`,
  `tools/verificar_clsids.sh`); as duas escrevem so num `mktemp`.
- Nao conclui nada sobre `core/brew/despacho.cpp`, `tools/bateria.cpp`, `tools/sonda_mod.cpp`
  nem `tests/*`: sao de outros auditores, e uso-os apenas como CONTEXTO citado.
