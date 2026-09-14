# AUDITORIA DE STUBS E DE SUCESSO FALSO -- a base do `curupira/`

Branch `auditoria`, worktree `/tmp/wt-auditoria`, base `full-rewrite` (`0286921`).
Auditoria feita a 15 de setembro de 2026, com quatro auditores independentes
(`aud-modulos-3d`, `aud-media-hid-widget`, `aud-classes-ajudantes`, `aud-nucleo`) e
com a verificacao, pelo auditor principal, dos achados de maior gravidade. Os
relatorios com as 106 citacoes literais estao em `docs/rewrite/auditoria/`.

O P2 diz: *"stub silencioso e proibido; o caminho nao implementado RECUSA e
REGISTA; nunca devolve sucesso e nao faz nada"*. Esta auditoria e a lista do que
o viola, ordenada por gravidade, e nao por ficheiro.

## 0. O que foi medido, e com que comando

```
./build/zb2_bateria "$corpus" "$mods" /tmp/aud-bateria-head.json      # 59 s
  corpus=/home/rafaelfrequiao/projects/zeebo-emulator/research/sources/scripts/corpus62.json
  mods='/media/rafaelfrequiao/8C5F-19E51/zeebo/ROMs/debug_nand/mod'
```

Estado medido no HEAD (`0286921`), e nao o que estava no pedido:

```
== 62 titulos | carga 62 | ponteiro de modulo 62 | applet 37 ==
   PIXELS > 0 em 0 titulos | CORES maxima 1 | 21 titulos pedem algo
```

| demanda medida (15 nomes, 174 pedidos) | quantos |
|---|---|
| `AEEHelperFuncs[0x140] vsnprintf` | 116 |
| `IShell::slot41` (= `kShell_LoadResDataEx`, `brew_slots.inc:...`) | 21 |
| `IShell::CreateInstance CLSID desconhecido` | 16 |
| `AEEHelperFuncs[0x074] realloc` | 9 |
| `IShell::slot44` (= `kShell_GetDeviceInfoEx`) | 2 |
| `IAppHistory::Back`, `IAppHistory::GetClass`, `IRootForm HandleEvent nao atendido`, `ITextCtl::HandleEvent`, `ITextCtl::IsActive`, `ITextCtl::SetActive`, `ITextCtl::SetInputMode`, `ITextCtl::SetProperties`, `ITextCtl::SetRect`, `IFileMgr::slot5` (= `kFileMgr_MkDir`) | 1 cada |

**A demanda NAO ve um stub silencioso, por construcao.** Ela e o mapa de
`Traco::RegistarFalta`. Um caminho que devolve sucesso e nao registra nao aparece
la, nem com zero. Por isso os piores achados desta auditoria foram encontrados a
ler o contrato no cabecalho do SDK, e nao a contar a demanda -- e a coluna
"demanda" de cada achado diz `0 = nao aparece` quando e esse o caso.

Ha DUAS respostas falsas que se podem ler na demanda por causa disto: ver
"CONTRADIZ O QUE SE PENSAVA", ponto 1.

**O NUMERO, antes -> depois, e o comando:**

```
./build/zb2_bateria "$corpus" "$mods" /tmp/aud-bateria-head.json     # HEAD 0286921
./build/zb2_bateria "$corpus" "$mods" /tmp/aud-bateria-depois.json   # com as correccoes
./build/zb2_comparar /tmp/aud-bateria-head.json /tmp/aud-bateria-depois.json

  carga 62 -> 62 | modulo 62 -> 62 | vtable 48 -> 48 | applet 37 -> 37
  PIXELS > 0: 0 em 62, nos dois lados | CORES maxima 1, nos dois lados
  demanda: 15 nomes e 174 pedidos, IGUAL nos dois
  resultado: 0 regressoes, 0 melhorias -- SEM REGRESSOES
```

**A auditoria NAO move o numero, e isso e o resultado honesto**: os tres defeitos
corrigidos sao de contrato e de instrumento, e nenhum deles e exercitado pelos 62
titulos nas fases que a bateria corre. O que ela move e o que se pode AFIRMAR: as
duas structs do guest tem agora o tamanho do cabecalho, duas aceitacoes mudas
deixaram rasto, e 17 numeros de linha que nao levavam a sitio nenhum passaram a
levar (com uma guarda que os confere contra o cabecalho).

## 1. A LISTA

Gravidade:
- **SILENCIOSO** -- devolve sucesso (ou um valor que se le como resposta) sem
  fazer o trabalho e sem deixar rasto. E o proibido pelo P2.
- **REGISTADO** -- recusa ou no-op COM rasto. Nao e defeito; fica na lista para a
  conta das 117/80/32 entradas ficar verificavel.
- **CANDIDATO** -- defeito confirmado no codigo, sem sintoma medido hoje (falta
  chamador, falta titulo, ou so aparece numa condicao que o corpus nao exercita).

Coluna "verif.": `S` = o auditor principal leu a linha, o cabecalho do SDK e o
contrato e confirma; `frag` = achado de um auditor, que eu NAO reverifiquei linha
a linha (o relatorio dele esta em `docs/rewrite/auditoria/`).

### 1.1 SILENCIOSO

| # | ficheiro:linha | o que devolve | o que devia fazer | verif. |
|---|---|---|---|---|
| S1 | `core/cpu/arm_interpreter.cpp:832-833`, `:848` | o ramo aceita `(instr & 0xE000) == 0x8000`, que cobre `0x8000-0x8FFF` (STRH/LDRH) **e** `0x9000-0x9FFF` (`STR/LDR [sp,#imm8]`); decodifica as duas como acesso de PALAVRA a `r0 + imm5*4`, com `op = (instr>>11)&3` que nestas duas formas vale 2 ou 3, logo **um STORE e executado como LOAD** | descodificar as formas 8 e 9, ou RECUSAR com o nome (como faz o ramo generico da linha 882) | S |
| S2 | `core/cpu/arm_interpreter.cpp:839` | `meia` e condicao MORTA (`(instr & 0xF000) == 0x8000` obriga o bit 12 a zero e a condicao seguinte pede-o a um); o ramo das linhas 841-847 nunca corre, e `STRH/LDRH` Thumb cai no acesso de palavra | usar `imm5*1` e `Ler16/Escrever16` | S |
| S3 | `core/cpu/arm_interpreter.cpp:645`, `:653` | `MRS ..., SPSR` le sempre `spsr_[0]`; o `MSR` ignora o bit 22 (R) e escreve o CPSR -- muda modo e T | banco de SPSR por modo (`spsr_[7]` existe e so o indice 0 e usado) e usar o bit 22 | frag |
| S4 | `core/cpu/arm_interpreter.cpp:263` | `ROR #0` imediato (== `RRX`) e tratado como "sem deslocamento": resultado e carry errados | implementar `RRX` | frag |
| S5 | `core/brew/despacho.cpp:1090-1091` | `IFileMgr::GetLastError` devolve **0 = AEE_SUCCESS** sempre, sem rasto e sem estado de erro | guardar o erro da ultima operacao e devolve-lo; `AEEFile.h:1342` diz "SUCCESS : If last file operation was successful" | S |
| S6 | `core/brew/despacho.cpp:860-870` | `IDisplay::SetDestination` aceita um bitmap nosso e guarda-o em `destino_` -- campo que **ninguem le** (`grep destino_` em `despacho.cpp` da 858 e 866, as duas ESCRITAS); e `pDst = NULL`, que o cabecalho diz ser legal ("or NULL to set to device (screen) bitmap", `AEEIDisplay.h:1899`), e RECUSADO com `kAeeUnsupported` e sem rasto | usar o destino no desenho, ou recusar com nome; e aceitar o NULL do contrato | S |
| S7 | `core/brew/despacho.cpp:847-859` | `IDisplay::GetDestination` devolve um bitmap FIXO em `kObjDibBase+0x300` ignorando `destino_`, e **nao incrementa a contagem de referencias** (`AEEIDisplay.h:1945`: "The reference count of the bitmap will be incremented") | devolver o destino actual, com AddRef | S |
| S8 | `core/brew/despacho.cpp:894-902` | `IShell::CancelTimer` compara so o `pfn` (o cabecalho passa `pfn` **e** `pUser`, `AEEIShell.h:300`), **ignora o r2**, e devolve `kAeeSuccess` mesmo quando nao desarmou nada; nao deixa evento nenhum (`SetTimer` deixa) | comparar o par e registar a recusa de cancelar o que nao e nosso | S |
| S9 | `core/brew/despacho.cpp:992-1000` | `ISQLMgr::Open` devolve `kAeeUnsupported` **sem `RegistarFalta`** -- o comentario diz "Recusa DECLARADA" e a declaracao nao existe em lado nenhum | `RegistarFalta(Area::Brew,"ISQLMgr::Open","nao ha SQLite neste emulador")` | S |
| S10 | `core/brew/despacho.cpp:1037-1043` | `IFileMgr::RmDir` devolve `kAeeUnsupported` sem rasto, com o comentario "Recusa-se em voz alta (principio P2)" -- **o comentario afirma um P2 que o codigo nao cumpre** | registar | S |
| S11 | `core/brew/despacho.cpp:988-991` | `IFile::Write` devolve 0 bytes sem rasto (a VFS e so de leitura por decisao) | registar a recusa com o nome | S |
| S12 | `core/brew/despacho.cpp:983-987`, `:871-881` | `IFile::GetInfo` com id invalido e `IDisplay::GetDeviceBitmap` com `ppIBitmap` nulo devolvem `kAeeUnsupported` **sem rasto** (idem `SetDestination` com ponteiro alheio, `:868-870`) | registar | S |
| S13 | `core/brew/despacho.cpp:1044-1049` + `core/brew/despacho.h:217` | `aee_GetAppInstance` (offset `0x0c0`) devolve `applet_`, e `applet_` **nunca e escrito**: `DefinirApplet` tem ZERO chamadores no repositorio inteiro | ligar o setter (a bateria guarda o applet em `g_applet`, e nao no motor) | S |
| S14 | `core/brew/despacho.cpp:810-812` + `despacho.h` | `IDisplay::Update` devolve 0 sem evento, e `updates_` **nunca e lido** (nao tem acessor, ao contrario de `Textos()/Blits()/Backlights()`). O no-op e o contrato (`Update` copia o bitmap do dispositivo para o ecra, e a `Tela` ja E o ecra); o contador morto e o defeito | tirar `updates_` ou expos-lo; deixar o evento da aceitacao | S |
| S15 | `core/brew/despacho.h` (`faltas_`, `Faltas()`, `LimparFaltas()`) | mapa escrito UMA vez em lado nenhum: `Faltas()` devolve sempre vazio. `tools/sonda_gl.cpp:606` documenta que deixou de o usar, "o mapa do despacho NUNCA e escrito" | apagar a API, ou escreve-la | S |
| S16 | `core/brew/despacho.cpp:903` | `idx == kSlotIdHeapLock \|\| idx == kSlotIdHeapLock + 0` -- o segundo termo e identico ao primeiro (disjuncao morta). O `IHeap1::Unlock` (slot 8) nao esta ligado em lado nenhum | decidir sobre o `Unlock` e tirar o `+ 0` | S |
| S17 | `core/brew/despacho.cpp:1172` | `(void)pp_saida;` -- o parametro `pp_saida` de `Correr` nunca e usado | tirar o parametro | S |
| S18 | `core/brew/ajudantes.cpp:172` + `:174` | `TabelaDeAjudantes::Instalar` escreve **0** no slot do guest de TODAS as funcoes declaradas e implementadas e devolve `ok = true`; o objecto tem `traco_` e nao o le. `tools/sonda_mod.cpp:99-100` remenda o `malloc` e o `free` a mao, e `tests/carga_test.cpp:403-405` afirma `ok == true` | escrever o endereco de saida de cada implementacao (precisa de `Saidas`) ou RECUSAR e registar | S |
| S19 | `core/brew/ajudantes.cpp:75-76` e `:129` | `Alocador::Malloc` sem espaco e `Realloc` sem espaco: `++falhas_; return 0;` -- zero no rasto, com o `Traco` disponivel (a bateria passa `&traco` em `bateria.cpp:469`) | `RegistarFalta` com o tamanho pedido | S |
| S20 | `core/brew/classes.cpp:152-155` | `IAppHistory::Top` devolve `kAeeSuccess` sem trabalho e **sem evento** -- a justificacao esta escrita, a aceitacao nao deixa rasto (contraste com o modelo `imedia`, que aceita, nao aplica e regista) | deixar o evento da aceitacao | S |
| S21 | `core/brew/widget.cpp:571-572` + `:584-587` + `:440` + `:23-30` | o modulo escreve **16 bytes** num `WidgetExtent` (`AEEIWidget.h:38-43`: `{int width; int height;}` = 8 B) e num `AEERect` (`AEERect.h:23-26`: 4 x `int16` = 8 B); o `SetExtent` LE 16 bytes. Sobrescreve 8 bytes da memoria do guest em cada chamada, e os valores sao os pares empacotados | usar as structs do cabecalho | S -- **CORRIGIDO nesta auditoria (C2)** |
| S22 | `core/brew/widget.cpp:259-263` | `FID_ACTIVE`/`FID_VISIBLE` devolviam TRUE sem aplicar e sem rasto, com o `FID_THEME` seis linhas abaixo a registar o mesmo caso | registar com o nome | S -- **CORRIGIDO nesta auditoria (C1)** |
| S23 | `core/brew/widget.cpp:593-594`, `:603-604` | `GetParent`/`GetModel` poem a resposta no r0 e **nunca escrevem o `IContainer**`/`IModel**` de saida**; o `GetModel` ignora tambem o `AEECLSID` | escrever a saida do contrato | frag |
| S24 | `core/brew/widget.cpp:379-391` | `RemoveForm(FORM_LAST)` -- que e o que `IRootForm::PopForm` usa -- recusa com `AEE_EBADPARM` e sem registo | registar com o nome | frag |
| S25 | `core/brew/ihiddevice.cpp:341-354` e `:206-217` | `RegisterForStatusChange`/`RegisterForConnectEvents` aceitam, guardam num campo nunca lido e devolvem SUCCESS **sem evento**, enquanto os irmaos `RegisterForButtonEvent`/`RegisterForPositionChange` no mesmo ficheiro emitem `REGISTA_BOTAO`/`REGISTA_POSICAO` | emitir o evento de aceitacao | frag |
| S26 | `core/brew/ihiddevice.cpp:225-243` | `GetConnectedDevices(nDeviceType = 0)` devolve 0 dispositivos; o cabecalho diz que 0 e "all attached devices", e ha um joystick | devolver o joystick | frag |
| S27 | `core/brew/ihid_entrada.cpp:398-402` | `ISignalCtl::Enable` e um no-op com sucesso e sem rasto (o contrato tem o estado "desactivado") | guardar o estado e registar | frag |
| S28 | `core/brew/imedia.cpp:675-678` | o `p2` do `MM_PARM_AUDIO_PATH` (SilenceTimerMS) e o unico argumento do modelo descartado sem linha | registar | frag |
| S29 | `core/brew/imedia.cpp:721-726` | `GetMediaParm` devolve -1 para um parametro nunca escrito, contra os por-omissao do proprio objecto (volume 100, pan 64, repetir 1), e o caso 5 nao emite evento | devolver o por-omissao e registar | frag |
| S30 | `core/brew/egl.cpp:612` | `eglCreateWindowSurface` diz no motivo "janela 0x... guardada ... N par(es) guardado(s)" e **nao guarda nada** (`janela` nao vai a membro nenhum; os atributos vao para `ultimos_atributos_`, um vector unico que a chamada seguinte apaga) | guardar o que o texto diz, ou dizer o que faz | frag |
| S31 | `core/brew/egl.cpp:778` (e `:644`, `:650`, `:728`) | `eglSwapBuffers` diz "nenhum rasterizador escreveu pixels" -- falso na propria corrida da sonda (`glClear` 307200 + `glDrawArrays` 38400) | dizer o numero real da tela | S (verifiquei o texto; a contagem e da sonda do auditor) |
| S32 | `core/brew/igl.cpp:736` | `glTexParameterx` aceita qualquer `pname` e so `MIN/MAG_FILTER` tem consequencia; um `pname` de wrap e aceite, nunca aplicado (o rasterizador prende na borda) e sem evento, apesar de `rasterizador.h:48-49` prometer que "fica registado no detalhe do desenho" | registar o `pname` nao aplicado | frag |
| S33 | `core/brew/igl.cpp:975` | `glGetError` devolve `GL_NO_ERROR` sempre, e nenhuma recusa do IGL escreve codigo de erro (`GL_INVALID_*` existem em `gl_slots.inc` e nao sao usados) | servir um codigo de erro real (o `egl.cpp` faz o contrario e esta certo) | frag |
| S34 | `core/brew/vfs.cpp:9-12` | `Registar` engole o `error_code` do `directory_iterator` e devolve `void`: um caminho errado da 62 titulos sem ficheiros, sem uma linha. Medido pelo auditor com o constructo exacto: `entradas=0 ec=2` | devolver o erro, e o chamador (`bateria.cpp:485`) registar | frag |
| S35 | `core/brew/formato.cpp:105-108` | especificador desconhecido (`%f`) e copiado LITERAL e **nao consome o argumento**: todos os conversores seguintes leem o argumento anterior | consumir o argumento e registar | frag |
| S36 | `core/brew/formato.cpp:48-59`, `:72`, `:75` | largura e `zero_a_esquerda` sao calculados e usados SO em `x/X`: `%02d` sai sem padding (e `cnk2` constroi `"%sbody%03d.pof"`) | aplicar a largura em todos os conversores | frag |
| S37 | `core/brew/tela.cpp:49-54` + `:16-23` | `Clip` aceita qualquer rect sem validar nem registar; um clip degenerado faz `Ponto` recusar tudo em silencio (`escritos_` nao conta) | validar e registar | frag |
| S38 | `core/audio/misturador.cpp:34-37` | com `mudo` (saida a zero) o contador `amostras_nao_nulas` conta a amostra CRUA: o criterio da etapa 5 passa com a saida muda. O cabecalho diz o contrario | contar o que SAI | frag |
| S39 | `core/audio/misturador.cpp:26`, `:50` | `amostras == nullptr` com `quantas > 0`: aceito e ignorado, sem evento nem contador | travar e CONTAR, como o volume acima do maximo ja faz | frag |
| S40 | `core/memoria/memoria.cpp:79-81` | escrita com `autor_` por declarar: incrementa um contador cujo nome diz "outro autor" e **nao emite nada**; `vigiados_ja_vistos_` e escrito e ninguem o le; o cabecalho (`memoria.h:40-43`) promete "registadas como falta" | registar | frag |
| S41 | `core/memoria/memoria.cpp:117-133` | `EscreverBloco` (e `EscreverBruto`, que e um alias) escrevem `memcpy` directo: **nao passam pela vigia nem pelo contador de escritor** | passar pela vigia, ou declarar que a vigia so ve escritas escalares | frag |
| S42 | `core/carga/mod.cpp:11-14`, `:23-26` | a recusa de carga sai em `motivo`, e o `Traco` (que e passado a funcao) so serve o caminho de sucesso | `RegistarFalta` | frag |
| S43 | `core/tempo/tempo.h:39-45` | avanco negativo: contado e nunca lido fora dos testes; `Repor()` sem chamador | registar no traco | frag |
| S44 | `core/traco/traco.cpp:119-122` | `DestinoFicheiro::Escrever` e no-op quando o ficheiro nao abriu: o `motivo` existe e nada obriga a le-lo. Um log vazio e indistinguivel de uma corrida calada | propagar a falha | S |

### 1.2 REGISTADO -- o modelo, para a conta ficar verificavel

| ficheiro:linha | o que faz | porque esta certo |
|---|---|---|
| `core/brew/igl.cpp:446-480` + `:391-419` | os 80 slots do IGL passam todos por um `Registar` unico; `feito`/`feito_com`/`recusa`/`sem` sao as unicas saidas | nenhuma das 133 saidas de `Igl::Executar` contorna o `Registar` (contado pelo auditor) |
| `core/brew/egl.cpp:373-786` | idem para os 28 slots do IEGL, com `erro_` mantido e servido por `eglGetError` | `egl.cpp:398-403` le e LIMPA o erro, como o EGL define |
| `core/brew/ajudantes_extra.cpp:496-505` | os 96 offsets do catalogo sem implementacao recusam com o NOME do SDK e a ASSINATURA do cabecalho, e `r0 = 20` (`AEEStdErr.h:36`) | e o caminho que faz a demanda ter nomes |
| `core/brew/classes.cpp:166-175` | `ITextCtl::SetInputMode` e os outros recusam com o nome do metodo | a demanda mostra `ITextCtl::SetRect`, e nao `slot28` |
| `core/brew/imedia.cpp` (`Recusar`/`AceitarComEvento`) | aceita, guarda e deixa evento com o nome | e o caso MODELO citado no pedido |
| `core/brew/despacho.cpp:903-911` (`IHeap1::Lock`) | devolve sucesso sem bloquear nada | P6: um escritor so, nao ha nada a bloquear -- o contrato E o no-op |
| `core/brew/despacho.cpp:912-917` (`FreeResData`) | nao faz nada | nao ha recursos alocados (`LoadResDataEx` recusa) |
| `core/brew/ajudantes_extra.cpp:102-109` (`wstrtostr` com `nSize == 0`) | no-op | o cabecalho define-o assim |
| `core/brew/igl.cpp:963-967` (`glFinish`/`glFlush`), `egl.cpp:761-768` (`eglWaitGL`/`eglWaitNative`) | no-op | o rasterizador desenha no proprio `glDrawArrays`; nao ha fila a esvaziar |
| `core/video/rasterizador.cpp` (15 `return false`, todos com motivo) | nenhuma primitiva devolve sucesso sem escrever | contado pelo auditor dos 3D |
| `core/brew/sha256.cpp` | nenhum caminho de recusa | transliterado e igual ao `hashlib` em 8 tamanhos |
| `core/carga/bar.cpp` | todas as recusas levam o campo e o valor no `motivo` | 320 ficheiros do SDK medidos |

### 1.3 CANDIDATO

| ficheiro:linha | o que devolve | o que devia fazer | verif. |
|---|---|---|---|
| `core/brew/despacho.cpp:532-552` | os slots do `IShell`/`IDisplay`/`IFileMgr` e dos genericos recusam com o nome `%s::slot%u` -- **um numero** -- embora `tools/brew_slots.inc` tenha `NomeDeShell`/`NomeDeDisplay`/`NomeDeFileMgr` GERADOS e **sem um unico chamador** no motor. Medido na demanda: `IShell::slot41` 21x (`kShell_LoadResDataEx`), `IShell::slot44` 2x, `IFileMgr::slot5` 1x | chamar o gerador de nomes no ramo generico | S |
| `core/brew/despacho.cpp:679-680`, `:887-891`, `:693-694`, `:724-725` | le e escreve um `AEERect` como **4 `uint32`** (16 bytes) quando o SDK declara 4 `int16` (8 bytes, `AEERect.h:23-26`): `SetClipRect` da lixo ao `Tela::Clip`, `GetClipRect` escreve 8 bytes fora da struct do chamador | usar `Ler16/Escrever16` | S |
| `core/brew/interface.cpp:75-93` | `NomeDoAjudante` devolve nomes de 14 offsets e e **inalcancavel**: o `AtenderAjudanteExtra` apanha os 117 offsets antes, logo o ramo generico que o chama nunca corre. E o `ajudantes_extra.h:54` diz que "tira os 14 nomes escritos a mao de `interface.cpp`" -- nao tirou | apagar a switch e o comentario | S |
| `core/brew/interface.cpp:29-73` (`Cablar`) | escreve a cablagem e confere-a por leitura de volta, com duas guardas -- e **nao tem um unico chamador no motor** (os 5 sitios sao `tests/brew_test.cpp`) | usar o `Cablar` como caminho unico, ou apagar | S |
| `core/brew/interface.cpp:45-48` | o limite da vtable e `l.vt + 64` para qualquer `vt` < 9000, e as vtables das CLASSES tem 32 slots: aceita os slots 32-63 e escreve na vtable da classe SEGUINTE | usar o numero real de slots | S |
| `core/brew/interface.cpp:14-26` (`ConstruirObjeto`) | escreve `saidas.Endereco(base+i)` sem conferir `saidas.quantos`/`ativa`; um indice fora da faixa da um ponteiro que o laco nunca reconhece (o `widget.cpp:52-60` faz a conta e o `ihiddevice.cpp:85` tambem) | recusar e registar | S |
| `core/cpu/cpu.h:115` | `InstruscoesRecusadas()` devolve 0 por omissao na INTERFACE | recusar a pergunta, ou nao ter valor por omissao | frag |
| `core/cpu/arm_interpreter.h:54` | `modo_atual_valido_` declarado e **nunca escrito nem lido**; `ModoAtual()` devolve `static_cast<Modo>(modo_atual_ & 0x1F)` sem verificar, contra o que o cabecalho (h:36-38) promete | usar o campo ou tirar o campo e a promessa | S |
| `core/cpu/arm_interpreter.cpp:154-163` (`Repor`) | nao limpa as bancadas sombreadas nem o `spsr_`, e ZERA `recusadas_` (a bateria chama `Repor` 2x por titulo) | repor o estado todo, ou dizer o que nao repoe | frag |
| `core/cpu/arm_interpreter.cpp:345-381` | `rn == 15` e recusado como "UNPREDICTABLE" embora `LDRH r0,[pc,#imm]` seja forma VALIDA (so o `Rt = PC` e UNPREDICTABLE): o motivo mente sobre a forma | separar os dois casos | frag |
| `core/cpu/arm_interpreter.cpp:553` | `LDM/STM` com lista vazia: `Recusar` e `return` **sem avancar o PC** -- a instrucao reexecuta ate ao orcamento; e o unico caminho com esse comportamento | avancar, ou parar declaradamente | frag |
| `core/cpu/arm_interpreter.cpp:682-683` + `:789` | `SWI` recusado com nome e a execucao CONTINUA (`Set(kPC, pc+4)`) | parar no SWI | frag |
| `core/cpu/arm_interpreter.cpp:667-677` | `MRC p15` devolve `0x410FB760` para QUALQUER registrador pedido (registado como falta) | ler o registrador pedido, ou recusar os outros | frag |
| `core/brew/ihiddevice.h:431` | `primeira_injecao_` declarado e **nunca usado** (a deteccao de mudanca usa `ultimo_eixo_`, inicializado ao centro em `ihiddevice.cpp:66`) | tirar | S |
| `core/brew/widget.cpp:245` | `guardado` e `(void)guardado;` -- variavel local morta, e o `(void)` cala o aviso que a apanhava | tirar as duas linhas | S |
| `core/brew/ajudantes.h:78` | `Blocos()` sem leitor no repositorio | tirar | frag |
| `core/brew/ajudantes.h:82-84` | o comentario diz que os 8 bytes de reserva servem para o `realloc` crescer no lugar -- e o `Realloc` nunca cresce no lugar | tirar o campo ou corrigir o comentario | S |
| `core/brew/ajudantes.cpp:118-129` (`Realloc`) | nao valida que `endereco` seja um bloco NOSSO (o `Free` valida); o slot `0x074` **nao esta ligado** e a demanda pede-o 9x em 3 titulos | validar, e ligar o slot | S |
| `core/brew/ajudantes.cpp:93-96` (`Free`) | `free` duplo nao e detetado | detetar e registar | frag |
| `core/brew/ajudantes_extra.cpp:425-428` | `Implementacao::nome` escrito nas 5 linhas e nunca lido | tirar | frag |
| `core/brew/ajudantes_extra.h:83` + `.cpp:510` | `RecusasRegistadas()` conta por instancia, e o motor constroi um `AjudantesExtra` TEMPORARIO por chamada: o contador e sempre 0 | contador estatico ou objecto persistente | S |
| `core/brew/ajudantes_extra.cpp:376-377` | ignora o retorno de `LerCadeia`: um palheiro sem NUL em 65536 bytes fica truncado e a busca devolve 0 ("nao encontrado") | comparar com `kLimiteDeCadeia` e registar | frag |
| `core/brew/classes.h:125` | o comentario diz `0x800C0000 + k*0x1000` e o codigo usa `0x8F000000`; o MESMO cabecalho diz que o `0x800C0000` nao estava livre | corrigir o comentario | S |
| `core/brew/arquivo.h:34` | `ModoMudaOFicheiro` existe "para o despacho poder dizer PORQUE recusou", e o despacho **nunca o chama**: o motivo da recusa nunca chega ao traco | usar o motivo em `despacho.cpp:960` | S |
| `core/brew/arquivo.cpp:29` | um ficheiro de 0 bytes e recusado com o MESMO 0 de "nao existe" (o corpus tem `mod/276212/pm.bin` com 0 bytes) | devolver um `IFile` de tamanho 0 | S |
| `core/brew/arquivo.cpp:79` | `Informacao` com `pInfo == 0` devolve `true` sem escrever nada (o SDK nao admite nulo) | recusar | S |
| `core/brew/arquivo.cpp:89-96` | `Fechar` de id desconhecido: no-op mudo | registar | S |
| `core/brew/memoria` (`Vigiar`/`PararDeVigiar`) | a vigia de escrita **nao tem chamador fora dos testes**: o evento `VIGIA:` nao pode nascer numa corrida real | ligar a vigia ao instrumento | frag |
| `core/brew/formato.cpp:32` | argumento a mais devolve 0 sem evento | registar | frag |
| `core/brew/formato.cpp:114-117` | escreve no buffer do guest **sem limite nenhum** | limitar ou recusar | frag |
| `core/medio` `traco.cpp:135-140` (`Comparar`) | compara um nome que existe dos DOIS lados a zero e devolve `comparavel = true, diferenca = 0`: um nome mal escrito le-se como "sem diferenca" | recusar quando o nome nao existe em nenhum dos lados | S |
| `core/traco/traco.h:157` | `Traco` sem `Tempo` poe `quando = 0` em todos os eventos, sem avisar | exigir tempo, ou marcar | frag |
| `core/brew/igl.cpp:508` | `QueryInterface` do IGL devolve `0xE0000001` como ECLASSNOTSUPPORT; o cabecalho diz **3** (`AEEStdErr.h:19`), e o `despacho.cpp:43-47` chama a esse valor "um valor que nao existe em cabecalho nenhum"; o IEGL responde **0** a mesma pergunta -- tres respostas | servir 3 nos dois | S |
| `core/brew/egl.cpp:296` | os handles de superficie caem na faixa dos contextos a 16 criacoes e na das strings a 32 (`superficies_criadas_` so cresce) | reservar faixas distintas | frag |
| `core/video/rasterizador.cpp:515` | reserva `count` do guest sem limite (`0x40000000 * 40 B` -> `bad_alloc`) | limitar e recusar | frag |
| `core/carga/mod.cpp:31` | escreve o ponteiro ROPI em `base-4` e nao o rele para confirmar | ler de volta | frag |
| `core/carga/mod.h:44-48` | o comentario diz que a funcao "aponta o PC" -- nao recebe CPU nenhuma | corrigir o comentario | frag |
| `tools/ajudantes_slots.inc` (as linhas citadas) | 17 dos 117 registos citavam a linha do COMENTARIO de seccao acima da declaracao (o `malloc` citava `// Memory allocation routines`) | apontar para a declaracao | S -- **CORRIGIDO nesta auditoria (C3)** |
| `tools/bateria.cpp:224` | `kSlotIdGetFontMetricsAlias = 1548` sem uso | tirar | S |
| `tools/bateria.cpp:310` | `g_updates` escrito (reset) e nunca lido | tirar | S |
| `tools/bateria.cpp:196-240` vs `despacho.cpp:57-76` | as 45 constantes `kSlotId*` existem em DOIS ficheiros; os valores CONFEREM hoje (fiz o diff), e nada os compara | uma fonte so | S |
| `tools/bateria.cpp:147-152` vs `interface.cpp:8-14` | `kGenericos` copiado | uma fonte so | S |

## 2. As CORRECCOES feitas nesta auditoria, com o teste

Regra do pedido: cada correcao tem de ter o teste que fica VERMELHO sem ela.
Todas as correcoes estao em ficheiros que nao sao dos partilhados.

### C1 -- `FID_ACTIVE`/`FID_VISIBLE`: TRUE sem rasto (S22)

`core/brew/widget.cpp:259-263`. O defeito e de INCONSISTENCIA DENTRO DA MESMA
FUNCAO: o `FID_THEME` (seis linhas abaixo, no mesmo `if`) regista que o tema
ficou por aplicar -- "um TRUE com o tema por aplicar e uma meia-verdade que fica
escrita" -- e o `FID_ACTIVE`/`FID_VISIBLE` devolviam TRUE com um `++escritas_` e
mais nada. O `escritas_` e um contador AGREGADO: quem le a bateria sabe que
"alguma propriedade foi escrita" e nao QUAL.

Teste (novo, `tests/widget_test.cpp`):

```
Widget.OSetPropertyDeFidActiveDeixaRastoComONome
Widget.OSetPropertyDeFidVisibleDeixaRastoComONome
```

ANTES (vermelho, e este e o registo da violacao):

```
widget_test.cpp:461: Failure
Expected: (b.Faltas("IRootForm FID_ACTIVE nao aplicado")) >= (1u), actual: 0 vs 1
[  FAILED  ] Widget.OSetPropertyDeFidActiveDeixaRastoComONome
[  FAILED  ] Widget.OSetPropertyDeFidVisibleDeixaRastoComONome
```

DEPOIS: 22/22 verdes na suite `Widget.*`.

### C2 -- as structs do guest: 16 bytes numa struct de 8 (S21)

`core/brew/widget.cpp:440`, `:571-572`, `:584-587` e o helper `:23-30`. O modulo
escrevia QUATRO `uint32` num `WidgetExtent` (`AEEIWidget.h:38-43`: `{int width;
int height;}`) e numa `AEERect` (`AEERect.h:23-26`: quatro `int16`).

O que mudou:
- `EscreverRectDoGuest` escreve 4 x `int16` (a `AEERect` do guest);
- `EscreverExtent`/`LerExtent` escrevem/leem 2 x `uint32` (a `WidgetExtent`);
- `SetExtent` guarda largura e altura e poe a origem a zero (o cabecalho diz que
  a `WidgetExtent` "does not define the bounds or placement");
- `EscreverRectNoObjeto` ficou para o rect DENTRO do nosso objecto -- **duas
  coisas com o mesmo nome foram a causa do defeito, e agora sao duas funcoes com
  nomes diferentes**.

Testes (novos; o antigo `OExtentDoWidgetEEscritoELido` foi substituido, porque
**encodava a mesma suposicao errada**):

```
Widget.OGetExtentEscreveUmaWidgetExtentDeOitoBytes
Widget.OSetExtentLeUmaWidgetExtentDeOitoBytes
Widget.OGetClientRectEscreveUmaAEERectDeOitoBytes
```

Os tres poem uma SENTINELA (`0x11111111`, `0x22222222`) nos 8 bytes a seguir a
struct. ANTES (vermelho):

```
Widget.OGetExtentEscreveUmaWidgetExtentDeOitoBytes ... FAILED
  b.Mem().Ler32(kArg0 + 8)  Which is: 0x11111111  vs  kSentinelaA   (o codigo antigo
  escrevia 320 em [prc+8])
Widget.OSetExtentLeUmaWidgetExtentDeOitoBytes ... FAILED   (largura guardada vinha de [prc+8])
Widget.OGetClientRectEscreveUmaAEERectDeOitoBytes ... FAILED
  b.Mem().Ler32(kArg0 + 8)  Which is: 320  vs  kSentinelaA
  b.Mem().Ler32(kArg0 + 12) Which is: 240  vs  kSentinelaB
```

DEPOIS: `22 tests from 1 test suite ran. [ PASSED ] 22`.

### C3 -- `tools/ajudantes_slots.inc`: 17 dos 117 numeros de linha mentiam (CANDIDATO)

Cada registo do `.inc` cita a linha do cabecalho onde o campo esta, e o campo
existe para a medicao ser LOCALIZAVEL sem contar campos a mao (P1). MEDIDO:

```
python3 /tmp/checar_linhas.py "$SDK" tools/ajudantes_slots.inc
campos lidos: 117 | referencia de linha MA: 17
  0x008 strcpy                   linha 52 -> '// Standard String Functions...'
  0x068 malloc                   linha 93 -> '// Memory allocation routines'
  ... (17 no total)
EXIT=1
```

A causa esta no gerador (`tools/nomear_ajudantes.py`): a linha era a do primeiro
caracter nao branco do BLOCO do membro, e o bloco comeca no COMENTARIO de seccao
acima da declaracao. A correccao usa a posicao do proprio `(*nome)`, que esta
sempre na linha da declaracao.

Guarda nova em `tools/verificar_ajudantes.sh` (corre no `ctest`, teste
`slots_do_sdk_ajudantes`), e a prova por violacao deliberada: com o gerador ANTIGO
o diff do ficheiro gerado continua a PASSAR (o `.inc` concorda consigo proprio) e
so a guarda nova acusa --

```
sh tools/verificar_ajudantes.sh
OK: ajudantes_slots.inc corresponde a AEEStdLib.h (117 campos).
FALHA: a linha citada NAO e a da declaracao, em 17 de 117 campos:
   0x008 strcpy: linha 52 -> '// Standard String Functions...'
EXIT=1
```

Depois da correccao:

```
OK: ajudantes_slots.inc corresponde a AEEStdLib.h (117 campos).
OK: as 117 referencias de linha levam a declaracao no cabecalho.
```

**A licao**: uma guarda de igualdade contra o ficheiro gerado NAO ve um erro do
proprio gerador. Era preciso uma guarda que confronte o ficheiro com o CABECALHO.

## 3. NAO E DEFEITO, MAS PARECE

1. **`IHeap1::Lock` a devolver 0** (`despacho.cpp:903-911`): P6, um escritor so;
   nao ha nada a bloquear. O contrato E o no-op, e esta escrito.
2. **`IFile::Write` a devolver 0 bytes e `RmDir` a recusar**: a VFS e so de
   leitura por DECISAO (a recusa e a resposta certa -- o que falta e o REGISTO,
   que e o achado S11/S10).
3. **`FreeResData` a nao fazer nada** (`despacho.cpp:912-917`): nao ha recursos
   alocados, porque o `LoadResDataEx` recusa. Certo.
4. **`kAeeUnsupported` a sair dos 96 offset do catalogo e dos slots do
   `ITextCtl`**: o cabecalho permite (sao metodos que o SDK declara), e a recusa
   LEVA O NOME. Modelo.
5. **`glFinish`/`glFlush`/`eglWaitGL`/`eglWaitNative` a devolver sucesso sem
   fazer nada**: o rasterizador desenha no proprio `glDrawArrays`; nao ha fila.
6. **`wstrtostr` com `nSize == 0`**: o cabecalho define-o como no-op.
7. **`Escalar` com divisao inteira, `sha256` inteiro, `Cpsr` com a armadilha do
   `cpsr = 0` corrigida**: deterministicos e medidos.
8. **`Tela::CorAtual` a truncar a 16 bits**: e o contrato do RGB565.
9. **`Memoria::Ler8` a devolver 0 para pagina nunca escrita**: e a memoria
   esparsa, documentada e com teste; o PC perdido em memoria nao mapeada e
   apanhado por fora (`saiu_do_modulo_para_...`).
10. **`Vfs::Normalizar` a colapsar `..`**: decisao escrita, e a VFS e uma lista
    plana de nomes -- nao ha travessia possivel.
11. **`MRC p15` com valor declarado E `RegistarFalta`**: o padrao certo; a
    ressalva e que o valor e o mesmo para qualquer registrador (CANDIDATO).
12. **Os slots de GL que o rasterizador nao faz** (blending, iluminacao, nevoa,
    ...): `igl.cpp:271-310` poem cada um no traco com o NOME, uma vez. E
    exatamente o contrario do `glCullFace` que descartou 86 377 chamadas.
13. **`glGetError` a devolver `GL_NO_ERROR`**: esta CERTO pela letra do P1
    ("inventar um codigo seria pior") e o motivo esta escrito; fica na lista como
    CANDIDATO porque o guest nao distingue "tudo bem" de "recusado".
14. **`classes.cpp:105-113` e `:166-168`**: codigo defensivo que hoje nao corre, e
    que se corresse REGISTAVA. Nao e defeito.
15. **O `e.tamanho` do instrumento**: ja foi corrigido (`bateria.cpp:441`), e a
    guarda `vtable` le agora um campo escrito. Confirmei-o a ler.

## 4. CONTRADIZ O QUE SE PENSAVA

1. **A LISTA DE DEMANDA DO PEDIDO NAO E A DO HEAD.** O pedido diz
   `rasterizador_de_GL 124x`, `LoadResDataEx 3x`, `CreateInstance 3x`. Medido no
   HEAD (`0286921`, e o comando esta no topo deste ficheiro): **nao existe
   nenhuma falta `rasterizador_de_GL`**, o `IShell::slot41` (= `LoadResDataEx`)
   aparece **21x** e o `CreateInstance CLSID desconhecido` **16x**. A lista do
   pedido descreve o estado de `ecc97b0`. **Quem planear pelo pedido planeia
   sobre um estado que ja mudou.**
2. **"os titulos nao chegam ao GL" tem agora outra causa medida**: `PIXELS 0` em
   62 de 62 e `CORES` maxima 1, e o rasterizador ja escreve na `Tela`. O que
   falta e o guest pedir `glDrawArrays` (o `ddragonz` pede-o a partir da bancada,
   nao do titulo). Nao me pronuncio sobre a causa: **nao a medi**.
3. **`IShell::slot41` e um numero, e o nome existe.** `tools/brew_slots.inc`
   gera `NomeDeShell(slot)`, `NomeDeDisplay(slot)`, `NomeDeFileMgr(slot)` e mais
   12, e **nenhum deles tem chamador no motor**: o ramo generico do despacho
   (`:532-552`) escreve `%s::slot%u`. 24 dos 174 pedidos da demanda sao numeros
   com nome disponivel.
4. **`AEEHelperFuncs[0x140] vsnprintf` e o pedido mais alto do corpus** (116x em
   4 titulos: `alice` 65, `zeeboids` 49, `dodgeball` 1, `zeebopeteca` 1) e o
   irmao directo, `vsprintf` (`0x13c`), esta implementado. E o `realloc`
   (`0x074`, 9x em 3 titulos: `gof`, `rmp`, `pbc`) tem o `Alocador::Realloc`
   ESCRITO e TESTADO -- e nao ligado ao slot. **Duas vitorias de demanda que ja
   estao a meio caminho**, e nenhuma delas precisa de engenharia reversa.
5. **Nenhum dos 117 ajudantes devolve zero em silencio no caminho da demanda**: a
   conta fecha (16 ligados no despacho + 5 em `AjudantesExtra` + 96 que recusam
   com nome e assinatura). O unico sitio que escreve zero sem rasto e o
   `TabelaDeAjudantes::Instalar` (S18), e o motor NAO o usa.
6. **O `modo_atual_valido_` e o `primeira_injecao_` sao campos mortos** (declarados
   e nunca usados, `grep` de uma linha so) e o `Faltas()` do despacho tambem. A
   varredura mecanica dos 153 campos de `core/**.h` deu exatamente estes dois.
7. **O motor nao usa `TabelaDeAjudantes` nem `Cablar`** -- so o `sonda_mod` e o
   ctest. As guardas do `Cablar` (slot da IBase, slot fora da vtable) nao
   protegem objecto nenhum a correr.
8. **O applet no HEAD e 37, e nao 41** (o numero do pedido), e o `rasterizador`
   ja esta cablado. Ver o comando no topo.
9. **O `docs/rewrite/PAREDE-DA-PILHA.md:174-175` diz "nenhum titulo do corpus
   corre Thumb nesta fase". E FALSO, e eu confirmei-o no HEAD**: no anel de 16
   instrucoes do despacho, os PCs andam de 2 em 2 em `brainchallenge`, `reksio` e
   `rocketweb` (ARM anda de 4 em 4). A confirmacao esta na seccao 5 deste
   relatorio, e e o achado S1/S2 -- o `STR/LDR [sp,#imm8]` do Thumb esta a ser
   executado como outra instrucao, em silencio, em titulos do corpus.

## 5. A CONFIRMACAO DO S1/S2 (o que eu medi, e nao herdei)

O auditor `aud-nucleo` afirmou que 3 titulos correm Thumb. Verifiquei-o no MEU
JSON, contando os passos entre PCs do anel para os titulos que ele citou:

```
brainchallenge   recusadas=       7 steps=[2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 40, 2, -8, 2, 2]
reksio           recusadas=       9 steps=[2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2]
rocketweb        recusadas=      18 steps=[2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 56, 2, -92, 2, 2]
gof              recusadas=   28166 steps=[4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4]
rmp              recusadas=   56173 steps=[4, -152, 4, 9076, 4, 4, 4, -9474, 4, 4, 4, 4, 4, 4, 4]
```

E li as palavras executadas no `brainchallenge` (`pc=0x20e2` palavra
`0x92029300`; a instrucao em `0x20e2` e a metade BAIXA, `0x9300`), que o
`objdump -M force-thumb` desmonta como `str r3, [sp, #0]`. O interpretador, nessa
instrucao:

- rn = `(instr>>3)&7` = 0 e imm5 = `(instr>>6)&0x1F` = 12 -> `end = r0 + 48`;
- op = `(instr>>11)&3` = **2** (bits 12-11 de `0x9300` sao `10`), logo nao e 0 e
  cai no ramo de LOAD;
- resultado: `r0 = mem32[r0+48]`, o `str` nao acontece, e o `r0` do chamador fica
  destruido -- **sem evento e sem contar em `recusadas`**.

A mesma conta para o `STRH` (`0x8000-0x8FFF`) da sempre `op` 2 ou 3, logo tambem
LOAD: **nesta faixa, todo o STORE é executado como LOAD.**

## 6. FICHEIROS PARTILHADOS QUE EU PRECISO QUE SEJAM ALTERADOS

NAO os toquei (sao teus), e nenhum deles e preciso para as correccoes C1-C3.
Por ordem de valor:

| ficheiro | o que precisa | porque |
|---|---|---|
| `core/cpu/arm_interpreter.cpp` | o ramo `:832-833` (formas Thumb 8 e 9) e a condicao morta `:839` | S1/S2: execucao errada em silencio, medida em 3 titulos |
| `core/brew/despacho.cpp` | `:532-552` usar `NomeDeShell`/`NomeDeDisplay`/`NomeDeFileMgr` | 24 dos 174 pedidos da demanda sao numeros com nome disponivel |
| `core/brew/despacho.cpp` | `:679-680`, `:887-891`, `:693-694`, `:724-725`: `AEERect` = 4 x `int16` (`Ler16`/`Escrever16`) | le e escreve 16 bytes numa struct de 8 (o mesmo defeito que eu corrigi no widget) |
| `core/brew/despacho.cpp` | `RegistarFalta` em `:992`, `:1037`, `:988`, `:983`, `:871`, `:868` | recusas sem rasto (S9-S12) |
| `core/brew/despacho.cpp` | `SetDestination`/`GetDestination`/`CancelTimer`/`Update`/`applet_`/`faltas_`/`pp_saida`/o `+ 0` | S5-S17 |
| `core/brew/despacho.cpp` + `core/brew/ajudantes.cpp` + `tests/carga_test.cpp` | `TabelaDeAjudantes::Instalar` escrever 0 e devolver `ok` | S18: o teste afirma `ok == true` sobre uma tabela de zeros |
| `core/brew/ajudantes.cpp` | `Malloc`/`Realloc` sem espaco: registar | S19 (o `Traco` ja e passado em `bateria.cpp:469`) |
| `core/brew/despacho.cpp:150` | ligar o `realloc` (offset `0x074` -> `kSlotIdRealloc`) e o `vsnprintf` (`0x140`) | 9x + 116x de demanda, com o trabalho ja escrito no `Alocador::Realloc` |
| `core/brew/interface.cpp` | apagar `NomeDoAjudante` (inalcancavel) e decidir sobre o `Cablar` | dois modulos a menos a parecerem guardas |
| `core/brew/vfs.cpp` | `Registar` devolver o erro | 62 titulos sem ficheiros sem uma linha |
| `core/brew/formato.cpp` | largura em `d/u/s` e o `%` desconhecido consumir o argumento | `cnk2` constroi nomes de ficheiro com `%03d` |
| `core/traco/traco.cpp` | `DestinoFicheiro` propagar a falha; `Comparar` recusar nome desconhecido | S44 e um comparador que aceita nomes mal escritos |
| `core/memoria/memoria.cpp` | vigia/`EscreverBloco` (S40/S41) | a vigia nao ve as escritas de bloco e nao tem chamador |
| `tools/bateria.cpp:224`, `:310` | tirar `kSlotIdGetFontMetricsAlias` e `g_updates` | campos mortos no instrumento |

## 7. LIMITES DESTA AUDITORIA (o que eu NAO medi)

1. **Nao medi se algum titulo chama `IDisplay::SetClipRect`/`DrawRect`/`DrawText`
   /`BitBlt`.** A bateria regista `textos 0` e `blits 0` nos 62 titulos, e esses
   dois sao contadores; o `DrawRect` e o `SetClipRect` nao tem contador nenhum.
   A medicao que falta precisa do traco cru (`ZB2_TRACE=1`) ou de um contador.
2. **Nao medi a demanda de nenhum dos achados S3/S4/S23-S29/S32/S35-S43.** Onde o
   relatorio do auditor diz "nao medido", eu nao o promovi a medido.
3. **Nao corri a bancada de GL** (`tools/sonda_gl`): os numeros dos achados S30-S33
   sao da sonda que o auditor correu, e eu so verifiquei o TEXTO do codigo.
4. **A causa do `PIXELS 0` nao esta determinada por mim.** O rasterizador existe e
   escreve (provado pela bancada); eu nao medi o que falta ao guest.
5. **Os achados marcados `frag` nao foram reverificados linha a linha por mim.**
   O texto completo deles, com as citacoes, esta em `docs/rewrite/auditoria/`
   (4 ficheiros, 2400 linhas). Onde eu escrevi `S` no "verif.", a linha foi lida
   e o contrato conferido no cabecalho do SDK.
6. **A contagem da suite**: `zb2_tests` passou de 376 para 379 (+3 testes meus, e
   o `OExtentDoWidgetEEscritoELido` foi substituido por 3). O numero exacto esta
   na secao do relatorio ao pai.
