# Auditoria -- `imedia`, `ihiddevice`, `ihid_entrada`, `widget` (fragmento aud-media-hid-widget)

Auditoria de STUBS na arvore NOVA (`/tmp/wt-auditoria/curupira/`, branch `auditoria`, base `full-rewrite`).
Principio aplicado: **P2** (stub silencioso e proibido; o estado por omissao de um caminho nao
implementado e RECUSAR e REGISTAR, com o NOME). Regra 1 de `core/traco/traco.h`: nao ha emissao
sem nome. Modelo de comparacao: `core/brew/imedia.cpp` (aceita + guarda + **deixa evento**).

## 0. Estado exacto do que foi lido (o repo esta vivo: foi editado DURANTE esta auditoria)

| ficheiro | sha256[0:12] | linhas | ultima alteracao |
|---|---|---|---|
| `core/brew/widget.cpp` | `c28dea9cce97` | 656 | 11:22:13 (**mudou durante a auditoria**) |
| `core/brew/widget.h` | `20fb8eeb9a77` | 254 | 11:13:58 |
| `core/brew/imedia.cpp` | `1bc73172787f` | 927 | 11:13:58 |
| `core/brew/imedia.h` | `caf4cc0179d3` | 480 | 11:13:58 |
| `core/brew/ihiddevice.cpp` | `484af266d601` | 642 | 11:13:58 |
| `core/brew/ihiddevice.h` | `90ed3570e24e` | 436 | 11:13:58 |
| `core/brew/ihid_entrada.cpp` | `7650f92f0abb` | 407 | 11:13:58 |
| `core/brew/ihid_entrada.h` | `80f82eee17cb` | 284 | 11:13:58 |

**AVISO DE REPO VIVO.** O `core/brew/widget.cpp` foi editado as **11:22:13**, a meio desta auditoria
(`git status`: `M curupira/core/brew/widget.cpp`, `M curupira/tests/widget_test.cpp`, HEAD `0286921`).
A edicao foi a correccao de UM dos achados desta auditoria -- o `FID_ACTIVE`/`FID_VISIBLE` que
devolvia `TRUE` sem rasto -- que passou a `RegistarFalta("IRootForm FID_ACTIVE nao aplicado")` com
teste proprio. **Todos os numeros de linha abaixo sao contra os hashes desta tabela.** Se um
ficheiro mudar outra vez, os numeros dos outros ficheiros continuam validos (hashes iguais).

**Convencao de gravidade usada** (a do pedido, com o criterio tornado explicito):

- `SILENCIOSO` -- o pior: o pedido devolve sem deixar rasto NENHUM no traco. Inclui as tres formas
  do mesmo mal: (i) sucesso e nada; (ii) sucesso com resposta errada; (iii) recusa MUDA (categoria 2
  do pedido: recusa mas nao registra).
- `REGISTADO` -- recusa/no-op que deixa rasto nomeado. O comportamento pode estar errado, mas o
  instrumento nao mente.
- `CANDIDATO` -- achado confirmado a ler codigo + cabecalho do SDK, mas SEM sintoma medido num
  titulo. Nao e inventado: e o que nao tem demanda medida.
## a) Achados, por gravidade

| ficheiro:linha | o que devolve | o que devia fazer | gravidade |
|---|---|---|---|
| **A1** `core/brew/widget.cpp:23-30`, `:571-572` | Escreve 4 palavras de 32 bits (16 B) no `WidgetExtent*` do guest -- que e `{int width; int height;}` = 8 B -- e devolve SUCCESS (`r0=1`). O titulo le width=0/height=0 e recebe 8 B escritos POR CIMA do que esta a seguir ao struct. Sem evento. **Sintoma:** um titulo que se dimensiona pela extensao do widget calcula com 0x0, e 8 B da memoria DELE mudam por baixo. | Escrever 2 `int32` (width,height) a partir de `kW_Rect+8/+12` (dx,dy). `EscreverRect` so se aplica a `AEERect` nosso. | `SILENCIOSO` |
| **A2** `core/brew/widget.cpp:23-30`, `:440` | `GetClientRect` escreve 4 palavras de 32 bits no `AEERect*` do guest -- que e `int16 x,y,dx,dy` = 8 B -- e o resultado sao DUAS palavras de zeros seguidas de 320/240 FORA do struct: o rect de cliente sai `{0,0,0,0}` em vez de `{0,0,320,240}`. Sem evento. **Sintoma:** um titulo que posiciona/recorta pelo rect de cliente ve uma area de zero. | Escrever 4 `int16` (`x,y,dx,dy`). | `SILENCIOSO` |
| **A3** `core/brew/widget.cpp:23-30`, `:584-587` | `SetExtent` LE 4 palavras (16 B) de um `WidgetExtent` do guest de 8 B: as duas ultimas vem de fora do struct e ficam guardadas como dx/dy do widget. `r0=1`. Sem evento. **Sintoma:** o rect do widget passa a ser (width,height,lixo,lixo). | Ler 2 `int32`. | `SILENCIOSO` |
| **A4** `core/brew/widget.cpp:593-594` | `GetParent` devolve o pai no `r0` e **nunca escreve o `IContainer **ppwc` (r1)**. O jogo que segue o cabecalho (`AEEGETPVTBL(pif,IWidget)->GetParent(pif,&ppwc)`) fica com o que estava na variavel dele. **Sintoma:** a variavel do jogo nunca e preenchida (fica com o valor anterior). | Escrever `*ppwc` (e nao o r0, que o contrato nao le). | `SILENCIOSO` |
| **A5** `core/brew/widget.cpp:603-604` | `GetModel` devolve o modelo no `r0`, ignora o `AEECLSID id` (r1) **e** o `IModel **ppm` (r2), e nao devolve codigo de erro nenhum. Sem evento. **Sintoma:** idem, e o jogo nao recebe codigo de erro nenhum para reagir. | Escrever `*ppm` e devolver `AEE_SUCCESS`/`AEE_EFAILED`. | `SILENCIOSO` |
| **A6** `core/brew/widget.cpp:366-373` | `InsertForm` IGNORA o `pr` (`IForm *pfBefore`, r2) em silencio: `FORM_DEFAULT`(0), `FORM_LAST`(1), `FORM_FIRST`(2), `FORM_ALL`(5) e "inserir antes desta forma" dao todos o mesmo `push_back`. Sem evento. **Sintoma:** a ordem de insercao pedida nao e cumprida (um popup pode ficar por cima de tudo em vez de logo abaixo). | Usar o `pr` (os cinco sentinelas estao no proprio comentario do codigo) ou registar que nao se cumpre. | `SILENCIOSO` |
| **A7** `core/brew/widget.cpp:379-391` | `RemoveForm` procura o VALOR do argumento na pilha: com o sentinela documentado `FORM_LAST` -- que e o que `IRootForm_PopForm()` usa -- nunca encontra nada e devolve `AEE_EBADPARM`. O comentario diz "Recusa DECLARADA" e **nao ha registo nenhum**. A forma fica na pilha. **Sintoma:** `IRootForm_PopForm()` -- a funcao que o cabecalho chama "conveniente" -- falha SEMPRE com EBADPARM e um rasto nenhum; a forma fica na pilha e a navegacao nao volta a forma anterior. | Tratar `FORM_DEFAULT/LAST/FIRST/ALL` e registar a recusa. | `SILENCIOSO` |
| **A8** `core/brew/widget.cpp:394-396`, `:403-416` | `GetForm` ignora o `boolean bWrap` (r3) em silencio (o 4.o argumento do contrato). **Sintoma:** uma iteracao que conta com o `bWrap` (o proprio `IRootForm_IsFormOnStack` usa `TRUE, TRUE`) para antes do fim. | Cumprir o `bWrap` ou deixar rasto. | `SILENCIOSO` |
| **A9** `core/brew/ihiddevice.cpp:341-354` | `RegisterForStatusChange` aceita o `ISignal`, guarda-o em `sinal_de_estado_` e devolve `AEE_SUCCESS` **sem deixar evento nenhum** -- e os dois irmaos do MESMO ficheiro emitem (`REGISTA_BOTAO` na linha 423, `REGISTA_POSICAO` na 437). O campo guardado nunca e lido. **Sintoma:** um jogo que registra para mudanca de estado deixa o mesmo rasto que um que nao registra nada. | Emitir um evento nomeado, como os irmaos. | `SILENCIOSO` |
| **A10** `core/brew/ihiddevice.cpp:206-217` | `IHID::RegisterForConnectEvents`: aceita, guarda em `sinal_de_conexao_` (nunca lido) e devolve SUCCESS, sem evento. O registo do jogo fica invisivel no traco. **Sintoma:** idem para os eventos de conexao. | Emitir um evento nomeado. | `SILENCIOSO` |
| **A11** `core/brew/ihid_entrada.cpp:398-402` | `ISignalCtl_Enable` (slot 5): no-op com `r0=AEE_SUCCESS` e **sem evento**. O contrato do SDK nao e um no-op: um sinal DESPACHADO fica desactivado, e enquanto desactivado os `ISignal_Set` sao registados e so saem no `Enable` seguinte. Aqui nunca ha estado desactivado, e o pedido do jogo nao deixa linha. **Sintoma:** um jogo que conta com o "desactivado ate ao Enable" recebe TODOS os `Set` despachados, e o numero/ordem de callbacks difere do aparelho. | Ou implementar o estado desactivado, ou registar que o `Enable` nao faz nada. | `SILENCIOSO` |
| **A12** `core/brew/ihid_entrada.cpp:384-387` | `ISignal_Set` devolve `AEE_SUCCESS` sempre, mesmo quando o `Marcar(endereco)` de dentro recusou (sinal sem `pfn`). E um `Set` num sinal DESANEXADO e um caminho LEGAL (`AEEISignal.h`: "If the signal has been detached, this call has no effect") que cai em `RegistarFalta("ISignal_Set", "... nao tem funcao (pfn=0)")` -- uma falta FALSA no relatorio, com um sucesso por cima. **Sintoma:** uma falta que nao existe entra no relatorio da bateria (e o `Set` devolve sucesso). | Separar "desanexado" (no-op legal) de "falta", e devolver o codigo da recusa. | `SILENCIOSO` |
| **A13** `core/brew/imedia.cpp:675-678` | `MM_PARM_AUDIO_PATH`: o `p2` -- que o cabecalho define como `SilenceTimerMS` (1000 ms por omissao, o silencio entre repeticoes) -- e aceite e ignorado SEM evento. E o UNICO `p2` excluido a mao do evento `IMEDIA_PARM_P2`, logo o unico argumento descartado sem rasto no ficheiro que e o MODELO. **Sintoma:** o temporizador de silencio do ringer nao existe e nao se sabe; a cadencia de repeticao difere. | Aplicar ou emitir (a linha que ja existe para os outros `p2`). | `SILENCIOSO` |
| **A14** `core/brew/imedia.cpp:682-730` | `GetMediaParm` devolve `-1` (`0xFFFFFFFF`) para um parametro que o jogo nunca escreveu, quando o proprio objecto tem o por-omissao declarado (`volume = kVolumeMaximo` = 100, `pan = 64` = "Balanced - default", `repetir = 1`, `mudo = false`): o `Get` so consulta o mapa `guardados` (`it->second : -1`), e nunca os campos. Um `Get` de volume antes de qualquer `Set` devolve -1. E o caso 5 NAO emite evento nenhum, em nenhum dos ramos. **Sintoma:** um jogo que le um parametro antes de o escrever recebe `0xFFFFFFFF` (e pode trava-lo ou escreve-lo de volta). | Ler o campo do objecto (o `Get` do `kMmParmRate` ja tem um por-omissao; os outros nao). | `SILENCIOSO` |
| **A15** `core/brew/imedia.cpp:789-827`, `:829-846`, `:865-871` | `Seek`, `Pause`, `Resume` (e `GetState`) MUDAM o estado do objecto e devolvem SUCCESS **sem uma linha no traco** -- `Play` (linha 760), `Stop` (785) e `SetMediaParm` (669) emitem. Um `Pause`/`Resume` a mais ou a menos nao se ve no traco. **Sintoma:** um `Pause`/`Resume`/`Seek` a mais nao se ve no traco: nao ha como comparar duas corridas neste ponto. | Emitir um evento nomeado por mudanca de estado. | `SILENCIOSO` |
| **A16** `core/brew/widget.cpp:580-583` | `SetExtent(po, 0)`: devolve 0 (falha) sem registo nenhum (recusa MUDA). **Sintoma:** nenhum identificado; e a inconsistencia (o resto do ficheiro registra). | Registar a recusa (o resto do ficheiro regista as que nomeia). | `SILENCIOSO` |
| **B1** `core/brew/widget.cpp:259-284` | **CORRIGIDO AS 11:22:13 DURANTE ESTA AUDITORIA.** Era `TRUE` + `++escritas_` + nada (SILENCIOSO, e o contador `escritas_` nao diz QUAL propriedade). Agora e `TRUE` + `RegistarFalta("IRootForm FID_ACTIVE nao aplicado")` + recusa local + teste proprio. | ja feito | `REGISTADO (era SILENCIOSO)` |
| **B2** `core/brew/ihiddevice.cpp:225-243` | `GetConnectedDevices` com `nDeviceType == 0`: SUCCESS com ZERO dispositivos. O cabecalho diz ``nDeviceType`` "a UID for the type of device ... **or 0 to return all attached devices**" -- e este emulador TEM um joystick (`kHandleDoDispositivo` = 1). Deixa rasto (`HID_SEM_DISPOSITIVO`, nivel Depuracao). | 0 = todos -> devolver o handle 1. | `REGISTADO (resposta errada)` |
| **B3** `core/brew/ihiddevice.cpp:245-246` | O `IHID` nao tem `QueryInterface`: o slot 2 declarado no `INHERIT_IQI` cai no default e devolve `AEE_EBADPARM` (14) com o nome `IHID::slot`. O `IHIDDevice` do mesmo ficheiro TEM o slot 2 implementado (linha 257). | Implementar (devolve o objecto ou ECLASSNOTSUPPORT) ou devolver 20. | `REGISTADO` |
| **B4** `core/brew/ihiddevice.cpp:69-77` | `Ihid::Recusar` poe sempre `AEE_EBADPARM` (14), inclusive para "slot sem implementacao" -- onde o resto da arvore (`widget.cpp:636`, `imedia.cpp:874`) usa `AEE_EUNSUPPORTED` (20) e o `Rumble` do mesmo ficheiro usa 20. | Escolher o codigo por caso. | `REGISTADO` |
| **B5** `core/brew/widget.cpp:231-241` | Aceitar so widgets NOSSOS em `WID_TITLE`/`WID_SOFTKEYS`/`WID_BACKGROUND`/`WID_FORM`: um widget do proprio jogo da `FALSE` (registado). O `AEEIRootForm.h` manda o contrario (`WID_TITLE` --  "Sets the title widget ... replacing it with the widget pointed to by 'dwParam'"). | Aceitar o widget do jogo, ou dizer no registo porque nao serve. | `REGISTADO` |
| **B6** `core/brew/widget.cpp:631-638` | O default poe `AEE_EUNSUPPORTED` (20) no r0 de QUALQUER slot, incluindo o slot 11 (`IntersectOpaque`), cujo contrato e `boolean`: 20 le-se como TRUE ("o widget e opaco"). | Devolver `FALSE` nos slots de contrato `boolean`. | `REGISTADO` |
| **C1** `core/brew/widget.cpp:249-257` | `FID_DISPLAY`: aceita, guarda em `display_`, SUCCESS, sem evento. O cabecalho manda redimensionar fundo/titulo/softkeys ao tamanho do display -- guardar e nao redimensionar nao deixa rasto. | Emitir (o `FID_THEME`, ao lado, ja registra). | `CANDIDATO` |
| **C2** `core/brew/widget.cpp:308-333` | `GETPROPERTY` de `WID_FORM`/`WID_TITLE`/`WID_SOFTKEYS`/`WID_BACKGROUND`: escreve o ponteiro e NAO incrementa a contagem de referencias, que o cabecalho promete ("incrementing the reference count of the title widget"). Sem evento. Aqui nada e libertado, logo nao ha sintoma visivel. | Incrementar as referencias. | `CANDIDATO` |
| **C3** `core/brew/ihiddevice.cpp:96-121` | A leitura de volta da cablagem (que existe por causa do `SetTimer` perdido) comeca no slot 3: o slot 2 (`QueryInterface`) dos DOIS objectos nunca e conferido. | Comecar a conferencia no slot 2. | `CANDIDATO` |
| **C4** `core/brew/ihiddevice.h:431` | `bool primeira_injecao_ = true;` nunca e lido nem escrito: campo morto. O nome diz o que faltava -- uma notificacao INICIAL de posicao -- e a ninguem e enviada. | Usar (marcar o sinal na primeira injecao) ou remover. | `CANDIDATO` |
| **C5** `core/brew/widget.h:165`, `:167` | `ResumoDeWidget::tem_handler` e `handler_pcxt` sao escritos em `Resumo()` e nunca lidos por ninguem (instrumento morto, contra P7). | Ler num teste ou remover. | `CANDIDATO` |
| **C6** `core/brew/imedia.h:434` | `std::uint32_t pan = kMmMaxPan / 2;  // "Balanced - default"` nunca e lido nem escrito: `MM_PARM_PAN` vai para `guardados` e o misturador nunca ve pan. Um campo que parece estado aplicado e nao e. | Aplicar no misturador ou remover o campo. | `CANDIDATO` |
| **C7** `core/brew/imedia.cpp:347` | `fila_.push_back(a)` sem limite: a fila de avisos pode crescer sem tecto (um `Play` num objecto ja a tocar emite um aviso de ABORT por chamada). Cada aviso deixa evento, logo e visivel. | Limite declarado, como o dos objectos (16) e o do buffer. | `CANDIDATO` |
| **C8** `core/brew/ihiddevice.cpp:487-540` | `GetPositionState` escreve `0` nos 20 eixos nao mapeados (nao o repouso medido 128); `GetMinPositionInfo` escreve 0/0 para eles (o "nao suportado" do cabecalho) -- as duas coisas nao se contradizem, mas o valor de um eixo nao suportado vai como 0 e nao como "ausente". | - | `CANDIDATO` |
| **C9** `core/brew/widget.cpp:144-148` | A faixa de `Atender` dos widgets inclui os slots 0 e 1, que nenhuma vtable referencia (`ConstruirObjeto` aponta-os para as saidas 3/4 do despacho): sub-faixa morta. O ramo da raiz exclui-os (linha 141), o do widget nao -- e o `default` responderia `IWidget slot nao implementado` para um pedido que ja nao pode acontecer. | Comecar em 2, como o ramo da raiz. | `CANDIDATO` |

Contagem: **16 SILENCIOSO**, **6 REGISTADO**, **9 CANDIDATO** (31 achados).


## b) Citacao literal de cada linha acusada

**A1** (SILENCIOSO) -- `core/brew/widget.cpp:23-30`, `:571-572`

```c++
widget.cpp:22: // O `AEERect` tem quatro `int32` seguidos: x, y, dx, dy (`AEEStdDef.h`).
widget.cpp:23: void EscreverRect(Memoria& mem, std::uint32_t onde, std::uint32_t x, std::uint32_t y,
widget.cpp:24:                   std::uint32_t dx, std::uint32_t dy) {
widget.cpp:25:   if (onde == 0) return;
widget.cpp:26:   mem.Escrever32(onde + 0, x);
widget.cpp:27:   mem.Escrever32(onde + 4, y);
widget.cpp:28:   mem.Escrever32(onde + 8, dx);
widget.cpp:29:   mem.Escrever32(onde + 12, dy);
widget.cpp:30: }
widget.cpp:571:       EscreverRect(mem_, pr, mem_.Ler32(objeto + kW_Rect + 0), mem_.Ler32(objeto + kW_Rect + 4),
widget.cpp:572:                    mem_.Ler32(objeto + kW_Rect + 8), mem_.Ler32(objeto + kW_Rect + 12));
```

**A2** (SILENCIOSO) -- `core/brew/widget.cpp:23-30`, `:440`

```c++
widget.cpp:440:       EscreverRect(mem_, pr, 0, 0, kLarguraDoEcra, kAlturaDoEcra);
```

**A3** (SILENCIOSO) -- `core/brew/widget.cpp:23-30`, `:584-587`

```c++
widget.cpp:584:       mem_.Escrever32(objeto + kW_Rect + 0, mem_.Ler32(pr + 0));
widget.cpp:585:       mem_.Escrever32(objeto + kW_Rect + 4, mem_.Ler32(pr + 4));
widget.cpp:586:       mem_.Escrever32(objeto + kW_Rect + 8, mem_.Ler32(pr + 8));
widget.cpp:587:       mem_.Escrever32(objeto + kW_Rect + 12, mem_.Ler32(pr + 12));
```

**A4** (SILENCIOSO) -- `core/brew/widget.cpp:593-594`

```c++
widget.cpp:593:     case kIWidget_GetParent:
widget.cpp:594:       cpu.Set(kR0, mem_.Ler32(objeto + kW_Pai));
```

**A5** (SILENCIOSO) -- `core/brew/widget.cpp:603-604`

```c++
widget.cpp:603:     case kIWidget_GetModel:
widget.cpp:604:       cpu.Set(kR0, mem_.Ler32(objeto + kW_Modelo));
```

**A6** (SILENCIOSO) -- `core/brew/widget.cpp:366-373`

```c++
widget.cpp:367:       // `int InsertForm(po, IForm *pf, IForm *pfBefore)`. `FORM_FIRST`=2,
widget.cpp:368:       // `FORM_LAST`=1, `FORM_DEFAULT`=0 (`AEEIRootForm.h:32..38`).
widget.cpp:369:       const std::uint32_t pf = cpu.Get(kR1);
widget.cpp:370:       if (pf == 0) {
widget.cpp:371:         cpu.Set(kR0, kAeeBadParm);
widget.cpp:372:         return Atendido::Feito;
```

**A7** (SILENCIOSO) -- `core/brew/widget.cpp:379-391`

```c++
widget.cpp:379:     case kIRootForm_RemoveForm: {
widget.cpp:380:       const std::uint32_t pf = cpu.Get(kR1);
widget.cpp:381:       for (std::size_t i = pilha_.size(); i-- > 0;) {
widget.cpp:382:         if (pilha_[i] == pf) {
widget.cpp:388:       // Recusa DECLARADA: tirar da pilha uma forma que la nao esta e um erro de
widget.cpp:389:       // quem chama (AEE_EBADPARM), e nao um sucesso silencioso.
widget.cpp:390:       cpu.Set(kR0, kAeeBadParm);
```

**A8** (SILENCIOSO) -- `core/brew/widget.cpp:394-396`, `:403-416`

```c++
widget.cpp:394:     case kIRootForm_GetForm: {
widget.cpp:395:       const std::uint32_t pr = cpu.Get(kR1);
widget.cpp:396:       const bool proximo = cpu.Get(kR2) != 0;
widget.cpp:401:       // Sem forma de referencia: `TRUE` quer a primeira, `FALSE` a ultima. A
widget.cpp:402:       // regra esta escrita no cabecalho, e nao foi inventada aqui.
widget.cpp:403:       if (pr == 0) {
```

**A9** (SILENCIOSO) -- `core/brew/ihiddevice.cpp:341-354`

```c++
ihiddevice.cpp:341: void Ihid::RegisterForStatusChange(ICpu& cpu) {
ihiddevice.cpp:342:   const std::uint32_t sinal = cpu.Get(kR1);
ihiddevice.cpp:343:   if (!sinais_.Conhece(sinal)) {
ihiddevice.cpp:347:   sinal_de_estado_ = sinal;
ihiddevice.cpp:348:   // REGISTADO, e o dispositivo NUNCA MUDA DE ESTADO neste emulador: um unico
ihiddevice.cpp:353:   cpu.Set(kR0, kAeeSuccess);
ihiddevice.cpp:423: }
ihiddevice.cpp:437:   traco_.Emitir(Area::Entrada, Nivel::Depuracao, "REGISTA_POSICAO",
ihiddevice.cpp:438:                 "slot 14, sinal=0x" + Hex(sinal));
```

**A10** (SILENCIOSO) -- `core/brew/ihiddevice.cpp:206-217`

```c++
ihiddevice.cpp:206:     case kIHID_RegisterForConnectEvents: {
ihiddevice.cpp:207:       const std::uint32_t sinal = cpu.Get(kR1);
ihiddevice.cpp:212:       sinal_de_conexao_ = sinal;
ihiddevice.cpp:213:       // Registado, e NAO marcado: nao houve mudanca de estado para avisar. Marcar
ihiddevice.cpp:215:       cpu.Set(kR0, kAeeSuccess);
```

**A11** (SILENCIOSO) -- `core/brew/ihid_entrada.cpp:398-402`

```c++
ihid_entrada.cpp:398:   if (slot == kISignalCtl_Enable) {
ihid_entrada.cpp:399:     // Nao ha fila de sinais desactivados: nao ha nada a fazer, e o sucesso e o
ihid_entrada.cpp:400:     // contrato. Fica dito aqui em vez de parecer um stub.
ihid_entrada.cpp:401:     cpu.Set(kR0, kAeeSuccess);
ihid_entrada.cpp:402:     return true;
```

**A12** (SILENCIOSO) -- `core/brew/ihid_entrada.cpp:384-387`

```c++
ihid_entrada.cpp:250:   const std::uint32_t puser = mem_.Ler32(endereco + kSinal_pUser);
ihid_entrada.cpp:251:   if (pfn == 0) {
ihid_entrada.cpp:252:     // Sem funcao nao ha callback. E uma RECUSA registada, e nao um sucesso mudo.
ihid_entrada.cpp:253:     traco_.RegistarFalta(Area::Entrada, "ISignal_Set",
ihid_entrada.cpp:254:                          "o sinal 0x" + Hex(endereco) + " nao tem funcao (pfn=0)");
ihid_entrada.cpp:384:   if (slot == kISignal_Set) {
ihid_entrada.cpp:385:     Marcar(endereco);
ihid_entrada.cpp:386:     cpu.Set(kR0, kAeeSuccess);
ihid_entrada.cpp:387:     return true;
```

**A13** (SILENCIOSO) -- `core/brew/imedia.cpp:675-678`

```c++
imedia.cpp:675:       if (p2 != 0 && id != kMmParmAudioPath) {
imedia.cpp:676:         traco_.Emitir(Area::Audio, Nivel::Depuracao, "IMEDIA_PARM_P2",
imedia.cpp:677:                       std::string(par->nome) + " p2=" + std::to_string(p2) + " (ignorado)");
imedia.cpp:678:       }
```

**A14** (SILENCIOSO) -- `core/brew/imedia.cpp:682-730`

```c++
imedia.cpp:722:         const auto it = o->guardados.find(id);
imedia.cpp:723:         if (pp1 != 0) {
imedia.cpp:724:           mem_.Escrever32(pp1, it != o->guardados.end() ? static_cast<std::uint32_t>(it->second)
imedia.cpp:725:                                                         : static_cast<std::uint32_t>(-1));
imedia.cpp:726:         }
imedia.h:434:     std::uint32_t pan = kMmMaxPan / 2;  // "Balanced - default" (AEEIMedia.h)
```

**A15** (SILENCIOSO) -- `core/brew/imedia.cpp:789-827`, `:829-846`, `:865-871`

```c++
imedia.cpp:826:       ++pedidos_aceitos_;
imedia.cpp:827:       return kAeeSucesso;
imedia.cpp:829:     case 10:  // Pause(po)
imedia.cpp:834:       o->estado = kMmEstadoPausado;
imedia.cpp:837:       return kAeeSucesso;
imedia.cpp:843:       o->estado = kMmEstadoTocando;
imedia.cpp:845:       ++pedidos_aceitos_;
imedia.cpp:869:       ++pedidos_aceitos_;
imedia.cpp:870:       return o->estado;
```

**A16** (SILENCIOSO) -- `core/brew/widget.cpp:580-583`

```c++
widget.cpp:578:     case kIWidget_SetExtent: {
widget.cpp:579:       const std::uint32_t pr = cpu.Get(kR1);
widget.cpp:580:       if (pr == 0) {
widget.cpp:581:         cpu.Set(kR0, 0);
widget.cpp:582:         return Atendido::Feito;
```

**B1** (REGISTADO (era SILENCIOSO)) -- `core/brew/widget.cpp:259-284`

```c++
widget.cpp:259:         if (w == FID_ACTIVE || w == FID_VISIBLE) {
widget.cpp:278:                                w == FID_ACTIVE ? "IRootForm FID_ACTIVE nao aplicado"
widget.cpp:279:                                                : "IRootForm FID_VISIBLE nao aplicado",
widget.cpp:280:                                det);
widget.cpp:281:           recusas_.push_back(w == FID_ACTIVE ? "FID_ACTIVE nao aplicado"
widget.cpp:282:                                              : "FID_VISIBLE nao aplicado");
widget.cpp:283:           ++escritas_;
widget.cpp:284:           cpu.Set(kR0, 1);
```

**B2** (REGISTADO (resposta errada)) -- `core/brew/ihiddevice.cpp:225-243`

```c++
ihiddevice.cpp:221:       const std::uint32_t tipo = cpu.Get(kR1);
ihiddevice.cpp:225:       if (tipo != 0x0106c3fdu) {  // AEEUID_HID_Joystick_Device
ihiddevice.cpp:226:         // ZERO DISPOSITIVOS, e dito: este emulador tem UM joystick e mais nada.
ihiddevice.cpp:230:         if (preq != 0) mem_.Escrever32(preq, 0);
ihiddevice.cpp:231:         cpu.Set(kR0, kAeeSuccess);
```

**B3** (REGISTADO) -- `core/brew/ihiddevice.cpp:245-246`

```c++
ihiddevice.cpp:245:     default:
ihiddevice.cpp:246:       Recusar(cpu, "IHID::slot", "slot sem implementacao");
ihiddevice.cpp:257:     case 2: QueryInterfaceIhidDevice(cpu); return true;
```

**B4** (REGISTADO) -- `core/brew/ihiddevice.cpp:69-77`

```c++
ihiddevice.cpp:74:   traco_.RegistarFalta(Area::Entrada, o_que, porque);
ihiddevice.cpp:75:   cpu.Set(kR0, kAeeBadParm);
ihiddevice.cpp:76:   return kAeeBadParm;
```

**B5** (REGISTADO) -- `core/brew/widget.cpp:231-241`

```c++
widget.cpp:230:         if (destino != nullptr) {
widget.cpp:231:           if (d != 0 && (d < kObjetoDosWidgets || d >= kFimDosObjetosDeWidget)) {
widget.cpp:237:             traco_.RegistarFalta(Area::Brew, "IRootForm_SetProperty widget alheio", det);
widget.cpp:238:             recusas_.push_back("SetProperty widget alheio");
widget.cpp:239:             cpu.Set(kR0, 0);
```

**B6** (REGISTADO) -- `core/brew/widget.cpp:631-638`

```c++
widget.cpp:631:     default: {
widget.cpp:632:       char det[64];
widget.cpp:633:       std::snprintf(det, sizeof(det), "k=%u slot=%u", k, slot);
widget.cpp:634:       traco_.RegistarFalta(Area::Brew, "IWidget slot nao implementado", det);
widget.cpp:635:       recusas_.push_back(std::string("IWidget slot ") + det);
widget.cpp:636:       cpu.Set(kR0, kAeeUnsupported);
```

**C1** (CANDIDATO) -- `core/brew/widget.cpp:249-257`

```c++
widget.cpp:249:         if (w == FID_DISPLAY) {
widget.cpp:254:           display_ = d;
widget.cpp:255:           ++escritas_;
widget.cpp:256:           cpu.Set(kR0, 1);
widget.cpp:257:           return Atendido::Feito;
```

**C2** (CANDIDATO) -- `core/brew/widget.cpp:308-333`

```c++
widget.cpp:312:           case WID_FORM: case WID_CONTAINER:
widget.cpp:313:             valor = Papel(papel_form_, kWidgetDoForm);
widget.cpp:315:           case WID_TITLE: valor = Papel(papel_titulo_, kWidgetDoTitulo); break;
widget.cpp:330:         if (d != 0) mem_.Escrever32(d, valor);
widget.cpp:331:         ++lidas_;
widget.cpp:332:         if (valor != 0) ++entregues_;
```

**C3** (CANDIDATO) -- `core/brew/ihiddevice.cpp:96-121`

```c++
ihiddevice.cpp:105:   const Checagem checagens[2] = {
ihiddevice.cpp:106:       {kVtableIhid, base_ + kBaseDoIhid, kIHID_CreateDevice, kIHID_GetConnectedDevices},
ihiddevice.cpp:107:       {kVtableIhidDevice, base_ + kBaseDoDispositivo, brew_slots::kHIDDevice_GetDeviceInfo,
ihiddevice.cpp:110:   for (const auto& c : checagens) {
ihiddevice.cpp:111:     for (std::uint32_t slot = c.primeiro; slot <= c.ultimo; ++slot) {
```

**C4** (CANDIDATO) -- `core/brew/ihiddevice.h:431`

```c++
ihiddevice.h:431:   bool primeira_injecao_ = true;
```

**C5** (CANDIDATO) -- `core/brew/widget.h:165`, `:167`

```c++
widget.h:165:   bool tem_handler = false;
widget.h:167:   std::uint32_t handler_pcxt = 0;
```

**C6** (CANDIDATO) -- `core/brew/imedia.h:434`

```c++
imedia.h:434:     std::uint32_t pan = kMmMaxPan / 2;  // "Balanced - default" (AEEIMedia.h)
```

**C7** (CANDIDATO) -- `core/brew/imedia.cpp:347`

```c++
imedia.cpp:347:   fila_.push_back(a);
imedia.h:467:   std::vector<Aviso> fila_;
```

**C8** (CANDIDATO) -- `core/brew/ihiddevice.cpp:487-540`

```c++
ihiddevice.cpp:507:         valor = (k == 0) ? 0u : (eixo != nullptr ? static_cast<std::uint32_t>(ValorDoEixo(eixo->uid)) : 0u);
ihiddevice.cpp:513:         valor = 0;
ihiddevice.cpp:516:         valor = (k != 0 && eixo != nullptr) ? static_cast<std::uint32_t>(kEixoMax) : 0u;
ihiddevice.cpp:522:         valor = (k == 0) ? 0u : (eixo != nullptr ? eixo->uid : kUidDesconhecido);
```

**C9** (CANDIDATO) -- `core/brew/widget.cpp:144-148`

```c++
widget.cpp:141:   if (indice >= base + 2 && indice < base + kSlotsPorObjetoDeWidget) {
widget.cpp:144:   if (indice >= kBaseDaFaixaDosWidgets &&
widget.cpp:145:       indice < kBaseDaFaixaDosWidgets + kQuantosWidgets * kSlotsPorObjetoDeWidget) {
widget.cpp:146:     const std::uint32_t delta = indice - kBaseDaFaixaDosWidgets;
widget.cpp:147:     return AtenderWidget(cpu, delta / kSlotsPorObjetoDeWidget,
widget.cpp:148:                          delta % kSlotsPorObjetoDeWidget);
```

## c) NAO E DEFEITO MAS PARECE

Todos estes parecem stubs a um `grep` e estao CERTOS. Contados, como pedido.

1. `ihiddevice.cpp:463-472` (`Rumble`) e `:474-478` (`GetRumbleStatus`) -- devolvem `AEE_EUNSUPPORTED` (20) + `RegistarFalta`. O cabecalho prescreve exactamente esse codigo: *"AEE_EUNSUPPORTED : if the device does not support rumble"*. O emulador nao tem motor. **Certo, e registado.**
2. `ihiddevice.cpp:190-205` (`IHID_GetNextConnectEvent`) -- `AEE_ENOMORE` **sem evento**. O dispositivo esta ligado desde o inicio, logo nao ha evento pendente. O proprio codigo guarda a medicao: a arvore antiga devolvia `0` aqui e o `abd` girou *"20 mil milhoes de passos sem sair"*. Uma "linha por consulta" seria pior.
3. `ihiddevice.cpp:530-545` (`GetNextButtonEvent` com a fila vazia) -- `AEE_ENOMORE` sem evento. E a resposta NORMAL do contrato (`"AEE_ENOMORE : No more events are pending"`) e o sample do SDK consulta isto num laco: um evento por consulta afogaria o traco. (O `INJETA_BOTAO` da injecao e que deixa rasto, e deixa.)
4. `widget.cpp:619-629` (`IWidget::Draw`, o registo na linha 625) -- RECUSA + `RegistarFalta("IWidget::Draw sem rasterizador")` + `cpu.Set(kR0, 0)`. A decisao esta escrita e e defensavel: pintar um retangulo com a cor guardada seria O EMULADOR A DESENHAR, e a bateria passaria a contar pixels do emulador.
5. `widget.cpp:287-297` (o TEMA) -- `TRUE` + `RegistarFalta("IRootForm tema nao aplicado (sem IResFile)")`: e a forma MODELO (aceita, devolve o que o contrato promete, e diz que nao aplica).
6. `imedia.cpp:766-768` (`Record`) e a tabela de parametros `Recusado` (`MM_PARM_FRAME`, `MM_PARM_RESERVED_1/2`, `imedia.cpp:131-133`) -- `kAeeNaoSuportado` + `RegistarFalta`, com o nome do slot/parametro. Certo.
7. `widget.cpp:394-417` (`GetForm` com a pilha vazia ou com uma referencia desconhecida) -- devolve 0 sem evento. O contrato devolve `IForm *` e NULL e a resposta documentada (`IRootForm_GetTopForm()`, com a pilha vazia, devolve NULL no aparelho a serio tambem). Nao e uma recusa de um caminho que falta: e a resposta.
8. `ihid_entrada.cpp:389-396` (`Detach`) -- limpa o par `(pfn,pUser)` e devolve sucesso sem evento. E trabalho de verdade, nao um caminho em falta (o irmao `Enable`, esse, esta na tabela: A11).
9. `widget.cpp:120-136` (`EMeu` a devolver `false` quando o modulo nao se construiu) -- e o que impede uma construcao recusada de continuar a reclamar indices e a atender com objectos por construir.
10. `ihiddevice.cpp:251-255` e `ihid_entrada.cpp:305-310` (`po` que nao e a nossa fabrica) -- recusam com o endereco no detalhe. Certo.
11. `imedia.cpp:266-272` (`kAeeClasseNaoSuportada` para um CLSID fora da familia, sem escrever `*ppobj`) e `imedia.cpp:283-289` (`kAeeSemMemoria` no 17.o objecto) -- recusas com nome. Certo.
12. Os slots 0 e 1 (IBase) de todos estes modulos -- o motor intercepta-os antes (`despacho.cpp:395-406`, `idx == 3` / `idx == 4`): nao sao "slots nao implementados", sao de outro dono.


## d) CONTRADIZ O QUE SE PENSAVA

1. **"O `imedia` e o caso MODELO do que esta certo"** -- e verdade para o `SetMediaParm` (um evento por aceitacao, `IMEDIA_PARM_GUARDADO`/`IMEDIA_PARM_P2`), mas NAO para o resto do ficheiro: o `GetMediaParm` (caso 5) nao emite UM evento em nenhum ramo e devolve `-1` onde o proprio objecto tem por-omissao declarado; o `p2` do `MM_PARM_AUDIO_PATH` e o unico argumento do `SetMediaParm` descartado sem linha (`:675`); e cinco slots (5, 9, 10, 11, 13) nao deixam rasto no caminho de sucesso, enquanto o 3, o 6, o 8 e o `Criar` deixam. O modelo e bom; nao e uniforme.
2. **"A falta X nao e pedida por nenhum titulo"** (a forma que o pedido da como exemplo de assumpcao a derrubar) -- a resposta medida, e ela tem DOIS lados:
   - No baseline dos 62 (`tools/baseline/bateria.json`, build `eb62459`) a UNICA falta destes quatro modulos e **`IRootForm HandleEvent nao atendido` x1**, no `zeebo_app`. Nao ha **nenhuma** falta de `IHID*`, `ISignal*`, `IMedia*`, `IWidget*`, nem do tema, nem do `Draw` (ver a seccao e). Ou seja: `IHIDDevice::Rumble`, `IWidget::Draw`, `IWidget slot nao implementado`, o `IWidget::GetProperty` desconhecido e o `IMedia::Record` **nao aparecem na demanda medida (nao medido = nao medido)**.
   - **Mas a bateria so ve RECUSAS NOMEADAS.** Um defeito silencioso -- e sao 16 dos 31 achados daqui -- nao aparece la POR CONSTRUCAO. **Ausencia de falta nao e prova de que o caminho nao e pedido.** As unicas medidas de demanda que existem para o silencio sao as constantes do corpus (seccao e), e essas dizem apenas que o objecto foi PEDIDO, nao que metodo foi chamado.
3. **O proprio `widget_test.cpp` encoda a convencao ERRADA** de A1/A2/A3: o teste `Widget.OExtentDoWidgetEEscritoELido` escreve 4 palavras em `rect` (`widget_test.cpp:304-307`) e espera 4 palavras de volta (`:312-315`). Teste e codigo partilham a mesma suposicao errada, logo a suite nao pode ver o defeito -- e a classe de defeito que o P7 existe para impedir ("um log so entra se puder ser verdadeiro").
4. **`widget.h` cita o cabecalho errado para o rect**: o comentario de `EscreverRect` diz *"O `AEERect` tem quatro `int32` seguidos: x, y, dx, dy (`AEEStdDef.h`)"*. O SDK define `AEERect` em **`platform/ui/inc/AEERect.h`** como `int16 x,y,dx,dy` (8 B), e o `AEEStdDef.h` **nao define `AEERect`**. A citacao aponta para o ficheiro errado E para o tipo errado -- uma afirmacao sem medicao que sustente (P1), e foi ela que produziu A1/A2/A3.
5. **O `widget.h` diz que a medicao que criou o modulo foi o `tectoy`** (tres pedidos ao slot 3). Medido agora, por varredura dos 62 `.mod` a procura do `AEECLSID_CRootForm` (0x01028e51, 4 bytes LE): **2 dos 62** o embutem -- `tectoy` e `zeebo_app`. E o `zeebo_app` e exactamente o titulo que produz a unica falta do widget **e** cujo `create` sai do modulo (`motivo`: `create:saiu_do_modulo_para_0xe3a06000 lr=0x00001cfc r0=0x80050300 ... sp=0x8007ffa0`, com as ultimas instrucoes a chamar `[vtable+0x40]` e depois `[vtable+0x34]` com `add r1, sp, #4`). O `tectoy` nao tem falta do widget nenhuma no baseline.
   **HIPOTESE (nao medida, marcada como hipotese):** um dos `[vtable+0x34]`/`[vtable+0x44]` pode ser um `GetExtent`/`GetClientRect`/`SetExtent` que escreve/le 16 B num struct de 8 na PILHA do titulo, e os 8 B a mais caem no quadro do chamador. Nao medi o evento nem o slot; fica para quem tiver o rasto com detalhe.
6. **Nenhum dos 62 titulos embute o IID do `IHIDDevice`** (0x0106c38e: 0 ocorrencias) **nem o do `IHID`** (0x0106c38d: 0). Isto relativiza B3 (o `QueryInterface` do `IHID` que falta): os jogos chegam ao dispositivo pelo `IShell::CreateInstance(AEECLSID_HID)` (0x0106c411, **53 dos 62**) e a seguir pelo `IHID_CreateDevice`, nunca por `QueryInterface` com aquele IID. Nao esta errado recusar; esta medido que ninguem pede.
7. **`primeira_injecao_`** (`ihiddevice.h:431`) e um campo com nome de comportamento que nao existe: a primeira injecao nunca marca o sinal de posicao. O cabecalho do modulo descreve o defeito da Z-Wheel ("`RegisterForPositionChange` respondia SUCESSO e nunca sinalizava nada, e o menu ficava a espera para sempre") e deixa o campo que serviria para o resolver por usar.


## e) Demanda MEDIDA (a gravidade nao se inventa)

### e.1 O baseline dos 62 titulos (`tools/baseline/bateria.json`, build `eb62459`)

Agregado de `faltas` (nome -> ocorrencias), por ordem:

| ocorrencias | falta |
|---|---|
| 116 | `AEEHelperFuncs[0x140] vsnprintf` |
| 21 | `IShell::slot41` |
| 16 | `IShell::CreateInstance CLSID desconhecido` |
| 9 | `AEEHelperFuncs[0x074] realloc` |
| 2 | `IShell::slot44` |
| 1 cada | `IAppHistory::Back`, `IAppHistory::GetClass`, **`IRootForm HandleEvent nao atendido`**, `ITextCtl::HandleEvent`, `ITextCtl::IsActive`, `ITextCtl::SetActive`, `ITextCtl::SetInputMode`, `ITextCtl::SetProperties`, `ITextCtl::SetRect`, `IFileMgr::slot5` |

**Dos meus quatro modulos aparece UMA linha: `IRootForm HandleEvent nao atendido`, 1 vez, no `zeebo_app`** (pasta 274791). Nada de `IHID*`, `ISignal*`, `IMedia*`, `IWidget*`, `IWidget::Draw`, tema ou `SetProperty`. Dois titulos do baseline (`zeebo_app` e nada mais) tocam no widget.

**LIMITE DA MEDIDA, dito por extenso:** a bateria regista apenas as RECUSAS COM NOME. Os 16 achados `SILENCIOSO` desta auditoria NAO PODEM aparecer la -- nao ha falta para um caminho que nao registra nada. `ausencia de falta != ausencia de chamada`. E nenhum titulo do baseline piorou por causa destes defeitos de forma VISIVEL na bateria, porque a bateria nao mede o rect de um widget nem o valor devolvido por um `Get`.

### e.2 A demanda do corpus (varredura de constantes de 32 bits LE nos 62 `.mod`)

| constante | titulos (de 62) |
|---|---|
| `AEECLSID_HID` = 0x0106c411 | **53** |
| `AEEUID_HID_Joystick_Device` = 0x0106c3fd | **60** |
| `AEECLSID_SignalCBFactory` = 0x01041207 | **37** |
| `AEEUID_HID_Keyboard_Device` = 0x0106c3fc | **11** (bate com o comentario do `ihiddevice.cpp:227`) |
| `AEECLSID_CRootForm` = 0x01028e51 | **2** (`tectoy`, `zeebo_app`) |
| `AEEIID_IHIDDevice` = 0x0106c38e | **0** |
| `AEEIID_IHID` = 0x0106c38d | **0** |

Isto mede que o OBJECTO foi pedido. Nao mede que metodo foi chamado -- e por isso os achados do `IWidget` (GetExtent/GetParent/GetModel/InsertForm/RemoveForm/GetForm) ficam sem demanda medida, embora dois titulos construam a interface inteira. **nao medido = nao medido.**

