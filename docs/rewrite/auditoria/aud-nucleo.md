# Auditoria do NUCLEO -- Zeebulator 2, branch `auditoria` (worktree `/tmp/wt-auditoria/curupira`)

Auditor: sub-agente `aud-nucleo`. Ficheiros auditados (e SO estes):
`core/carga/mod.{h,cpp}`, `core/carga/bar.{h,cpp}`, `core/cpu/arm_interpreter.{h,cpp}`,
`core/cpu/cpu.h`, `core/memoria/memoria.{h,cpp}`, `core/traco/traco.{h,cpp}`,
`core/tempo/tempo.h`, `core/audio/misturador.{h,cpp}`, `core/brew/arquivo.{h,cpp}`,
`core/brew/vfs.{h,cpp}`, `core/brew/formato.{h,cpp}`, `core/brew/sha256.{h,cpp}`,
`core/brew/tela.{h,cpp}`.

Lido primeiro, como pedido: `docs/rewrite/DESIGN.md` (P2, P7, as tres decisoes
estruturais) e `docs/rewrite/PLAN.md`. Nada foi modificado; nao corri `cmake` nem
o build. O unico executavel que corri foi um programa de 8 linhas meu
(`/tmp/verif_vfs.cpp`) para confirmar o comportamento exacto do constructo de
`std::filesystem` usado em `vfs.cpp`.

## Regra de gravidade que usei (declarada, para poder ser contestada)

- `SILENCIOSO` -- **devolve sucesso (ou um valor) sem fazer o trabalho e sem
  deixar evento no traco**. E o caso proibido pelo P2.
- `REGISTADO` -- recusa ou no-op **que deixa rasto** (evento `INSTRUCAO_RECUSADA`,
  `RegistarFalta`, contador declarado). O defeito, quando existe, e de
  correccao/completude, nao de silencio.
- `CANDIDATO` -- **nao confirmado** (contrato ambiguo, ou demanda por medir).

O grau `SILENCIOSO`/`REGISTADO` descreve o MECANISMO; a DEMANDA vai na 5.a coluna
da tabela, porque um stub que ninguem chama e diferente de um que 3 titulos pedem.

Ferramentas de medicao usadas (para o pai poder repetir):

```
grep -o '"mod":"[a-z0-9_]*"' /tmp/wt-auditoria/curupira/tools/baseline/bateria.json
# demanda: somar o campo "faltas" e ler "recusadas" por titulo, em tools/baseline/bateria.json
arm-none-eabi-objdump -D -b binary -m arm -M force-thumb --start-address=0x20d0 --stop-address=0x2100 \
  "/media/rafaelfrequiao/8C5F-19E51/zeebo/ROMs/debug_nand/mod/274804/brainchallenge.mod"
grep -o 'nome' /tmp/wt-auditoria/curupira/tools/brew_slots.inc
```

Resumo: **35 achados -- 12 SILENCIOSO, 7 REGISTADO, 16 CANDIDATO**. O pior achado e um
**descodificador Thumb que executa a instrucao errada em silencio** (F1), medido
em 2 dos 62 titulos do `bateria.json`.

---

## a) Tabela de achados (ordenada por gravidade)

| ficheiro:linha | o que devolve | o que devia fazer | gravidade | demanda medida |
|---|---|---|---|---|
| `core/cpu/arm_interpreter.cpp:832-833` | aceita `0x9000-0x9FFF` (LDR/STR Thumb SP-relativo e com registrador de deslocamento) no ramo de LDR/STR imediato e executa um acesso a `r0 + imm5*4` | descodificar a forma SP-relativa/registrador, ou RECUSAR com o nome (como faz o ramo generico da linha 882) | SILENCIOSO | 3 de 62 correm Thumb (`brainchallenge`, `reksio`, `rocketweb`); `brainchallenge` executa `9300`, `9202`, `9101` em `0x20e2/0x20e4/0x20ea` e `reksio` executa `9101` em `0x99e` (bateria.json, anel do despacho) |
| `core/cpu/arm_interpreter.cpp:839` | `meia` **nunca pode ser verdadeira** (a mascara `0xF000==0x8000` forca o bit 12 a zero) -> `STRH/LDRH` Thumb (`0x8000-0x8FFF`) sao executados como acesso de PALAVRA com offset `imm5*4` | usar `imm5*1` e `Escrever16/Ler16` | SILENCIOSO | ramo morto por construcao; nenhuma janela executada medida contem a forma (nao medido = nao medido) |
| `core/cpu/arm_interpreter.cpp:645` e `:653` | `MRS ..., SPSR` le sempre `spsr_[0]`; o `MSR` **ignora o bit 22 (R)** e escreve o CPSR -- muda modo/T e nao deixa evento | banco de SPSR por modo (o campo existe: `spsr_[7]`) e usar o bit 22 | SILENCIOSO | nao medido (nenhuma medicao de `msr spsr_*` no corpus) |
| `core/cpu/arm_interpreter.cpp:263` | `ROR #0` imediato (== `RRX` no ARM ARM) e convertido em "sem deslocamento": resultado e carry errados, sem evento | implementar `RRX` | SILENCIOSO | nao medido |
| `core/cpu/cpu.h:115` | `InstruscoesRecusadas()` devolve `0` por omissao -- um contador de recusas que nasce a zero | `= 0` pura, ou valor que diga "nao sei" | SILENCIOSO | latente: nao ha segunda `ICpu` (so `ArmInterpreter` deriva) |
| `core/cpu/arm_interpreter.h:54` + `arm_interpreter.cpp:139` | `modo_atual_valido_` **nunca e escrito nem lido**; `ModoAtual()` devolve `static_cast<Modo>(modo_atual_ & 0x1F)` -- o cabecalho (h:36-38) promete que se RECUSA a devolver modo invalido | guardar a validade e recusar | SILENCIOSO | so um teste usa `ModoAtual()`; nenhum caminho de titulo |
| `core/memoria/memoria.cpp:79-81` | escrita com `autor_` por declarar: incrementa um contador (cujo nome diz "outro autor") e **nao emite nada**; `vigiados_ja_vistos_` e escrito e **ninguem o le** | `RegistarFalta`/aviso com endereco e autor, como o cabecalho (h:14-15, h:40-43) promete | SILENCIOSO | n/a: a bateria declara `EscritorUnico("cpu")`; nenhum titulo medido produz "segundo escritor" |
| `core/brew/vfs.cpp:9-12` | `Registar` de pasta inexistente: o `error_code` nao e lido, o conjunto fica vazio, a funcao devolve `void` | devolver/registar o erro e a contagem | SILENCIOSO | medido: o constructo exacto devolve `entradas=0 ec=2 No such file`; unico chamador de producao `tools/bateria.cpp:485` |
| `core/brew/formato.cpp:105-108` | especificador desconhecido (`%f`, `%o`, `%.2f`, `%-5d`, `%+d`): escreve o texto LITERAL e **nao consome o argumento**, sem evento | consumir o argumento e registar a falta | SILENCIOSO | `%4.2f`, `%4.1f`, `%5.1f` reais em `quake.mod`; 24 de 62 `.mod` tem `%...f` dentro de cadeia imprimivel |
| `core/brew/formato.cpp:48-59` + `:72,75` | largura e `zero_a_esquerda` sao LIDOS e deitados fora para `d/i/u` e `s` (`%02d` sai sem padding), sem evento | aplicar a largura a todos os conversores | SILENCIOSO | `cnk2`: `"%02d; BLOC = %02d"`, `"%sbody%03d.pof"`, `"%02d:%02d:%02d"`; `tectoy` `%11s %8ld`; `quake` `%12s`; (largura em `x/X` esta tratada) |
| `core/brew/tela.cpp:18-20` + `:49` | `Clip` aceita qualquer rect sem validar nem registar; um clip degenerado faz `Ponto` recusar TUDO em silencio (`escritos_` nao conta) | validar e registar clip fora do ecra / largura 0 | SILENCIOSO | demanda confirmada por outro ficheiro: `despacho.cpp:679` le 4 `uint32` de um `AEERect` do SDK, que sao **4 `int16`** (`AEEIDisplay.h:240`, `AEERect.h:21-24`) |
| `core/audio/misturador.cpp:34-37` | `amostras_nao_nulas` conta a amostra CRUA `!= 0` mesmo com `mudo` (saida a zero) -- o cabecalho (h:54-56) diz o contrario | contar o que SAI (cabecalho) ou corrigir o cabecalho | SILENCIOSO | `imedia.cpp:112` aceita `MM_PARM_MUTE` ("Aplicado") e `imedia.cpp:437` passa `o.mudo`; o criterio da etapa 5 e "o misturador reporta amostras nao nulas" |
| `core/cpu/arm_interpreter.cpp:882` | as formas Thumb nao implementadas (registrador alto `0x4400/0x4700` sem BX, `LDM/STM` `0xC000`, `0xA000-0xBFFF`) RECUSAM com o opcode e o PC no traco, e avancam PC+2 | implementar (o evento existe, o trabalho nao) | REGISTADO | `rocketweb` (18 recusas) tem `0x447a`, `0x4479`, `0xc707` na janela executada |
| `core/cpu/arm_interpreter.cpp:571` | `LDM/STM com S` recusado com nome **depois** de ja ter escrito os 16 registradores: o estado fica a meio | recusar antes de mexer no estado | REGISTADO | nao medido |
| `core/cpu/arm_interpreter.cpp:553` | `LDM/STM` com lista vazia: `Recusar` com nome **e `return` sem avancar o PC** -> a mesma instrucao reexecuta ate ao orcamento | avancar ou parar declaradamente | REGISTADO | nao medido (instrucao UNPREDICTABLE) |
| `core/cpu/arm_interpreter.cpp:683` | `SWI` recusado com nome e a execucao CONTINUA (`Set(kPC, pc+4)`, linha 789): a chamada ao sistema vira no-op | parar no SWI ou marcar o passo | REGISTADO | nao medido (o `bateria` nao diz QUAL instrucao recusou -- `PAREDE-DA-PILHA.md:181-183`) |
| `core/cpu/arm_interpreter.cpp:489` | `escrita no PC com S` (restauro de SPSR) recusado com nome | implementar | REGISTADO | nao medido |
| `core/cpu/arm_interpreter.cpp:674` | `MRC p15` (qualquer registrador) devolve a constante declarada `0x410FB760` e regista a falta | ler o registrador PEDIDO, ou recusar os que nao sejam o tipo de cache | REGISTADO | nao medido por titulo |
| `core/cpu/arm_interpreter.cpp:346-379` | o grupo extra (formas `*T`, `Rt/Rn=PC`, `Rt` impar, desalinhamento, offset de registrador invalido) RECUSA com nome | implementar as formas validas que faltam | REGISTADO | a familia foi medida no `a3d` (`PAREDE-DA-PILHA.md:163-167`) |
| `core/cpu/arm_interpreter.cpp:154-163` | `Repor` nao limpa as bancadas sombreadas (`sombra_*`), nem `spsr_`, nem `modo_atual_valido_`, e ZERA os contadores de recusa | repor estado completo, ou dizer o que nao repoe | CANDIDATO | a bateria chama `Repor` 2x por titulo (`bateria.cpp:646,684`); o efeito depende de o guest ter mudado de modo na fase anterior |
| `core/brew/formato.cpp:32` | `argumento()` devolve `0` quando acabam os argumentos, sem evento (a format string pede mais que os que vieram) | registar | CANDIDATO | por medir: exige contar especificadores contra argumentos por titulo |
| `core/brew/formato.cpp:114-117` | escreve e termina a zero no buffer do guest **sem limite nenhum** | limitar/recusar | CANDIDATO | nao medido |
| `core/brew/arquivo.cpp:29` | ficheiro de 0 bytes e recusado com `return 0` (o mesmo "nao ha IFile" de "nao existe") | devolver um `IFile` valido de tamanho 0 | CANDIDATO | o `pm.bin` do `pacmania` tem 0 bytes no corpus e a cadeia `pm.bin` esta em `0x1306c` do `pacmania.mod` (que tem 7 faltas de `IShell::slot41`) |
| `core/brew/arquivo.cpp:79` | `Informacao` com `pInfo == 0` devolve `true` sem escrever nada e sem evento (o SDK diz `pInfo [out] placeholder`, nao admite nulo) | recusar | CANDIDATO | nenhuma medicao de `pInfo=0` |
| `core/brew/arquivo.cpp:89-96` | `Fechar` de id desconhecido e no-op silencioso (`void`) | registar | CANDIDATO | n/a |
| `core/memoria/memoria.cpp:117-129` (+`:132`) | `EscreverBloco` (e `EscreverBruto`, que e so um alias) escrevem `memcpy` directo: **nao passam pela vigia nem pelo contador de escritor** | passar pela vigia, ou declarar que a vigia so ve escritas de 1/2/4 bytes | CANDIDATO | `despacho.cpp:638` e `ajudantes.cpp:134` escrevem no guest por este caminho |
| `core/memoria/memoria.cpp:135` (e `:99`) | `Vigiar`/`PararDeVigiar` **nao tem chamador nenhum fora dos testes**: o evento `VIGIA:` nao pode nascer numa corrida real | ligar a vigia ao instrumento | CANDIDATO | `grep -rn Vigiar` : so `tests/fundacao_test.cpp:222,237` |
| `core/audio/misturador.cpp:26` e `:50` | `amostras == nullptr` (com `quantas > 0`) e aceito e ignorado sem evento nem contador | registar | CANDIDATO | n/a |
| `core/carga/mod.cpp:12` e `:24` | recusa (imagem vazia / nao cabe) com `motivo` mas **sem evento no `Traco` que lhe e passado** (o traco so serve o caminho de sucesso) | `RegistarFalta` | CANDIDATO | o chamador regista-o no motivo da bateria (`bateria.cpp:639`) |
| `core/tempo/tempo.h:41` e `:54` | avanco negativo: contado e devolvido (`AvancosInvalidos`), sem evento; os dois leitores estao so em teste. `Repor()` nao tem chamador nenhum | registar no traco; remover ou usar `Repor` | CANDIDATO | `grep`: `AvancosInvalidos` so em `tests/fundacao_test.cpp:47,56` |
| `core/traco/traco.cpp:120-121` | `DestinoFicheiro::Escrever` e no-op quando o ficheiro nao abriu: o `motivo` existe mas nada obriga a le-lo | propagar a falha de destino | CANDIDATO | so os testes constroem `DestinoFicheiro` |
| `core/traco/traco.h:157` | `Traco` construido sem `Tempo` poe `quando = 0` em TODOS os eventos, sem avisar | exigir tempo, ou marcar os eventos sem tempo | CANDIDATO | por medir |
| `core/carga/mod.h:44-48` | o comentario diz "Carrega `imagem` em `base` e aponta o PC para la"; a funcao nao recebe CPU e nao toca no PC | corrigir o comentario | CANDIDATO | n/a |
| `core/carga/mod.cpp:31` | escreve o ponteiro ROPI em `base-4` e **nao o rele para confirmar** | ler de volta e conferir | CANDIDATO | n/a (o projeto ja teve uma cablagem que passou na conferencia e nao servia -- `imedia.cpp:170-179`) |
| `core/carga/bar.cpp:57-58` | le o ficheiro inteiro com `istreambuf_iterator` e nao distingue "acabou" de "a leitura falhou": uma leitura truncada chega ao `Validar`, que a recusa acusando o FORMATO | ler com verificacao de erro e dizer que foi I/O | CANDIDATO | n/a |

---

## b) Citacao literal de cada linha acusada

As linhas sao copiadas tal como estao no ficheiro (o `|` e do formato da citacao).
Em cada achado: **linha**, **contrato que a julga**, **demanda**, **sintoma**.

### F1 -- Thumb `0x9000-0x9FFF` executado como outro acesso (`arm_interpreter.cpp:832-833`, `848`, `854`)

```
     832|   if ((instr & 0xE000u) == 0x6000u || (instr & 0xE000u) == 0x7000u ||
     833|       (instr & 0xE000u) == 0x8000u) {  // LDR/STR, LDRB/STRB, LDRH/STRH
     848|     const Reg end = base + (byte ? imm5 : imm5 << 2);
     854|       else Set(static_cast<int>(rd), mem_.Ler32(end));
```

`(instr & 0xE000) == 0x8000` e verdade para `0x8000-0x9FFF`, que contem DUAS
formas que este ramo nao trata: `0x8000-0x8FFF` (STRH/LDRH imediato) e
`0x9000-0x9FFF` (STR/LDR `[sp, #imm8]`, e a forma SP-relativa). O ramo calcula
`rn = (instr>>3)&7` e `imm5 = (instr>>6)&0x1F`, o que para essas duas formas nao
da nem a base nem o offset: da um acesso de PALAVRA a `r0 + imm5*4`.

Contrato: Thumb-1, ARM ARM A6.5.3 (formato 9: `1001 L imm8`, registo base = SP,
offset = `imm8*4`). O oraculo que este projeto usa (`arm-none-eabi-objdump`) diz
que `0x9300` e `str r3, [sp, #0]`.

Demanda (medida no `tools/baseline/bateria.json`): 3 dos 62 titulos correm Thumb.
O anel de 16 instrucoes do `despacho` (`despacho.cpp:1223-1226` guarda `pc` e
`mem_.Ler32(pc)`) prova-o pelo PASSO entre PCs consecutivos:

```
brainchallenge  recusadas=7        passos entre PCs = [2,2,2,2,2,2,2,2,2,2,40,2,-8,2,2]
reksio          recusadas=9        passos entre PCs = [2,2,2,2,2,2,2,2,2,2,2,2,2,2,2]
rocketweb       recusadas=18       passos entre PCs = [2,2,2,2,2,2,2,2,2,2,56,2,-92,2,2]
gof             recusadas=28166    passos entre PCs = [4,4,4,4,...]
zeebovolley     recusadas=1        passos entre PCs = [4,4,4,4,24,...]
rmp             recusadas=56173    passos entre PCs = [4,-152,4,9076,...]
```

Um PC nao multiplo de 4 em modo ARM nao existe (todo o `Set(kPC, ...)` do
interpretador em ARM e `pc+4` ou um alvo alinhado), logo passo=2 == execucao
Thumb. E a palavra que o anel reporta e exactamente `mem32(ficheiro, endereco)`
(conferido byte a byte para `brainchallenge 0x20e2 = 0x92029300`, `reksio
0x099c = 0x91011c2b`, `rocketweb 0x0788 = 0x466f003b`), o que fixa QUAL
instrucao estava no PC:

```
brainchallenge.mod @0x20e2 (executada) -> 0x9300 = str  r3, [sp, #0]
brainchallenge.mod @0x20e4 (executada) -> 0x9202 = str  r2, [sp, #8]
brainchallenge.mod @0x20ea (executada) -> 0x9101 = str  r1, [sp, #4]
reksio.mod         @0x099e (executada) -> 0x9101 = str  r1, [sp, #4]
```

(objdump force-thumb, e as tres guardam os argumentos 5, 6 e 7 na pilha antes de
`bl 0x1a08` / `bl 0x760` -- a convencao AAPCS.)

O que o interpretador faz com cada uma (modelo exacto do ramo, aplicado aos
opcodes medidos):

```
0x9300 -> r0 <- mem32[r0+48]      (o "str r3,[sp,#0]" desaparece e r0 e lido)
0x9202 -> r2 <- mem32[r0+32]      (destroi o argumento r2, posto segundos antes)
0x9101 -> r1 <- mem32[r0+16]      (destroi o argumento r1)
```

Sintoma num titulo: o `bl` seguinte recebe os argumentos 5-7 por escrever e r1/r2
estragados; nenhum evento, nenhum contador. `brainchallenge` termina a fase com
`saiu_do_modulo_para_0x80200010` e 7 recusas; `reksio` com
`saiu_do_modulo_para_0x80200010`. A instrucao errada NAO conta para `recusadas`
(nao ha recusa) -- o numero que a bateria mostra nao a ve.

### F2 -- `meia` e uma condicao morta (`arm_interpreter.cpp:839`)

```
     839|     const bool meia = (instr & 0xF000u) == 0x8000u && (instr & 0x1000u) != 0;
```

`(instr & 0xF000) == 0x8000` obriga o bit 12 a ZERO, e a segunda condicao pede o
bit 12 a UM: `meia` e sempre `false`. O ramo `if (meia)` das linhas 841-847 e
codigo morto, e a familia Thumb `STRH/LDRH` (`0x8000-0x8FFF`) cai no acesso de
palavra da linha 848 -- offset `imm5*4` em vez de `imm5*2`, e `Ler32/Escrever32`
em vez de `Ler16/Escrever16`. Sem evento.

Contrato: Thumb-1 formato 8 (`1000 L imm5`), meia-palavra, offset `imm5*1`.
Demanda: nao medido em execucao; a familia foi medida em ARM
(`docs/rewrite/PAREDE-DA-PILHA.md:163-167`: 60 `strh` e 21 `ldrh` no corpus).
Nao medi um titulo que execute a forma Thumb: **nao medido = nao medido**.

### F3 -- SPSR por modo nao existe (`arm_interpreter.cpp:645`, `653`, `659-660`)

```
     645|     Set(static_cast<int>(rd), spsr ? spsr_[0] : Cpsr());
     653|     valor = rot == 0 ? imm : ((imm >> rot) | (imm << (32 - rot)));
     659|   if ((mascara_campos & 1) != 0) novo = (novo & 0xFFFFFF00u) | (valor & 0xFFu);
     660|   if ((mascara_campos & 8) != 0) novo = (novo & 0x00FFFFFFu) | (valor & 0xFF000000u);
```

O campo `spsr_[7]` (`arm_interpreter.h:52`) tem sete entradas -- uma por modo --
e so o indice 0 e escrito (`arm_interpreter.cpp:80`) e lido. `spsr_[1..6]` nunca
sao escritos. E o `MSR` nunca olha para o bit 22 (R), que e o que separa
"escreve o CPSR" de "escreve o SPSR": um `msr spsr_cxsf, rX` escreve o CPSR,
incluindo os bits de modo e o T. Os bits 17 (x) e 18 (s) da mascara de campos
sao aceitos e ignorados.

Contrato: ARM ARM A4.2.4/A5.3.12 (MRS/MSR; R=1 -> SPSR do modo corrente) e os dois
registadores de estado sombreados por modo de excepcao.
Demanda: nao medido. Sintoma candidato: um retorno de excepcao que reponha o
estado por SPSR acerta no CPSR e pode mudar o modo ou entrar em Thumb sem aviso.

### F4 -- `RRX` tratado como "sem deslocamento" (`arm_interpreter.cpp:263`)

```
     263|   if (quantidade == 0 && tipo != 0) quantidade = 32;
```

Esta regra esta CERTA para `LSR`(01) e `ASR`(10), e errada para `ROR`(11): no
ARM, `ROR #0` no campo de deslocamento imediato significa `RRX` (rodar 1 pelo
carry), nao `ROR #32`. `Deslocar(valor, 3, 32, ...)` cai no `q = quantidade & 31
= 0`, que devolve o valor intacto com `carry = bit 31`. Resultado e carry
errados, sem evento.

Contrato: ARM ARM A5.2.1 (tabela do deslocamento imediato).
Demanda: nao medido.

### F5 -- contador de recusas com valor por omissao (`cpu.h:115`)

```
     115|   virtual std::uint64_t InstruscoesRecusadas() const { return 0; }
```

O comentario das linhas 111-114 explica porque o contador esta na INTERFACE. Mas
um valor por omissao de `0` numa funcao cujo contrato e "quantas instrucoes o
nucleo nao soube executar" e a versao em codigo do defeito que o proprio
`Despacho::Faltas()` ja cometeu nesta arvore: um contador que nunca e escrito
responde "nenhuma" com toda a confianca do mundo.
Contrato: o proprio comentario + P2. Demanda: latente (so `ArmInterpreter`
deriva de `ICpu`, e ele faz `override`).

### F6 -- campo morto e promessa nao cumprida (`arm_interpreter.h:54`, `h:36-38`, `cpp:139-141`)

```
      54|   mutable bool modo_atual_valido_ = true;     (arm_interpreter.h)
     139| Modo ArmInterpreter::ModoAtual() const {
     141| }
```

```
      36|   // Modo actual. Recusa-se a devolver um modo invalido: se o CPSR tiver lixo,
      37|   // isto denuncia.
      38|   Modo ModoAtual() const;
```

`grep -rn modo_atual_valido_` no repositorio inteiro da UMA linha: a declaracao
com inicializador. Nunca e escrita, nunca e lida. E `ModoAtual()` faz
`static_cast<Modo>(modo_atual_ & 0x1F)` sem qualquer verificacao -- devolve
`static_cast<Modo>(0)` (que nao e modo nenhum) com toda a naturalidade, ao
contrario do que o cabecalho promete. Quem detecta o modo invalido e
`SetCpsr` (linha 132). O `ModoValido` e usado pelos testes e pela `sonda_mod`.
Demanda: nenhum caminho de titulo chama `ModoAtual()`.

### F7 -- "escritas de outro autor": contador com o nome trocado e sem evento (`memoria.cpp:74-82`, `:79`)

```
      74| void Memoria::Escrever8(Endereco a, std::uint8_t v) {
      75|   if (!autor_.empty()) {
      79|     ++vigiados_ja_vistos_;  // contador de escritas, usado pelos testes
      80|   } else {
      81|     ++escritas_outro_autor_;
```

```
      44|   void EscritorUnico(std::string nome);            (memoria.h)
      46|   std::uint64_t EscritasDeOutroAutor() const { return escritas_outro_autor_; }
```

```
      14| // ... O escritor identifica-se uma vez, e escrever sem se ter identificado e
      15| // um erro registado.                                      (memoria.h)
```

```
      40|   // Identifica quem escreve. A partir daqui, escritas de outro autor sao
      41|   // registadas como falta. ...                            (memoria.h)
```

Tres defeitos num bloco:
1. `escritas_outro_autor_` conta as escritas feitas **antes de existir autor**,
   nao as de "outro autor" (na ausencia de `EscritorUnico` ha um so autor: o
   anonimo). O nome do acessor mente.
2. Nenhum dos dois ramos emite evento: "escritas de outro autor sao registadas
   como falta" (h:40-41) nao esta implementado, e P2 pede registo.
3. `vigiados_ja_vistos_` (h:103) e escrito com o comentario "usado pelos testes";
   `grep -rn vigiados_ja_vistos_` devolve a declaracao e esta linha. Nenhum teste
   e nenhum chamador o le. E um contador que existe para agradar a um comentario.
Demanda: a bateria faz `mem.EscritorUnico("cpu")` (bateria.cpp:467), portanto o
ramo do contador nem corre nas corridas medidas; o que corre e o `++` da linha 79.

### F8 -- VFS que nao registou nada nao diz nada (`vfs.cpp:7-14`)

```
       7| void Vfs::Registar(const std::string& pasta) {
       8|   nomes_.clear();
       9|   std::error_code ec;
      10|   for (const auto& entrada : std::filesystem::directory_iterator(pasta, ec)) {
      11|     if (!entrada.is_regular_file(ec)) continue;
      12|     nomes_.insert(entrada.path().filename().string());
      13|   }
      14| }
```

`Registar` devolve `void`, apaga o conjunto ANTES de tentar (linha 8) e nao olha
para `ec` nem uma vez. O overload com `error_code` de `directory_iterator` nao
lanca: devolve um intervalo vazio.
Medido com um programa meu de 8 linhas que usa o constructo EXACTO (ficheiro
`/tmp/verif_vfs.cpp`, e nao toca no repositorio):

```
entradas=0 ec=2 msg=No such file or directory
iter vazio=1 ec2=2
```

Demanda: `tools/bateria.cpp:485` e o unico chamador de producao
(`vfs_do_titulo.Registar(dir + "/" + t.pasta)`).
Sintoma: um `dir` errado (ou uma pasta em falta) deixa a VFS vazia; todos os
`Existe()` devolvem falso, todos os `OpenFile` devolvem NULL, e a bateria regista
62 titulos "sem recursos" sem uma linha que explique por que. O instrumento
acredita no que mediu e o que mediu foi o caminho errado.

### F9 -- especificador desconhecido passa ao lado, sem evento (`formato.cpp:105-108`)

```
     105|       default:
     106|         tmp[0] = '%';
     107|         tmp[1] = espec;
     108|         tmp[2] = 0;
```

O `default` copia o `%` e a letra para a saida e **nao chama `argumento()`**: o
argumento nao e consumido, logo todos os conversores seguintes leem o argumento
ANTERIOR (desalinhamento). Nada e registado.
O comentario do topo do ficheiro (linhas 27-28) diz que "uma formatacao em falta
e registada pelo DESPACHO, com o nome do slot" -- o despacho regista a CHAMADA ao
`sprintf`; nao ha nada, em lado nenhum, que diga que um `%f` foi deitado fora.

Contrato: `int sprintf(char *pBuf, const char *pFmt, ...)` -- AEEHelperFuncs
0x020 -- e o C99 para os conversores.
Demanda (medida nos 62 `.mod` do corpus, cadeia imprimivel que contem `%`):
`%f` aparece em 24 titulos; em `quake.mod` as formas sao reais e com precisao:
`"%4.1f megabytes"`, `"version: %4.2f"`, `"%5.1f"`, `"'%5.1f %5.1f %5.1f'"`.
(NOTA: a mesma varredura da `%e`, `%o`, `%g`, `%Q`, `%B` como ruido de dados
binarios -- a amostragem de contexto mostrou `%Q\xa0\xe1`, `%ea`, `anal%gico`.
So a `%f` foi confirmada como format string real.)
Sintoma: um titulo que formate um numero com `%4.2f` escreve no buffer a cadeia
LITERAL `%4.2f`; se a mesma format string tiver mais conversores, esses leem
argumentos trocados -- silencio total.

### F10 -- largura lida e deitada fora (`formato.cpp:48-59`, `:72`, `:75`)

```
      48|     int largura = 0;
      49|     bool zero_a_esquerda = false;
      ...
      55|     while (espec >= '0' && espec <= '9') {
      56|       largura = largura * 10 + (espec - '0');
      ...
      72|         std::snprintf(tmp, sizeof(tmp), "%d", static_cast<std::int32_t>(argumento()));
      75|         std::snprintf(tmp, sizeof(tmp), "%u", argumento());
```

A largura e o flag `0` sao calculados com cuidado (linhas 48-59) e usados SO nos
casos `x` e `X` (linhas 78-87). Em `d`, `i`, `u`, `s`, `c` o valor calculado e
ignorado: `%02d` imprime sem padding. Sem evento. O comentario das linhas 46-47
explica porque a largura importa ("`%04x` e comum e sem isto o jogo le um numero
errado") -- o raciocinio vale igual para `%02d`, e `%02d` nao tem tratamento.

Demanda (mesma varredura): `cnk2` traz `"%02d; BLOC = %02d"`, `"%sbody%03d.pof"`
(nome de FICHEIRO construido com largura), `"%02d:%02d:%02d"`; `tectoy`
`"%11s %8ld bytes"`; `quake` `"%12s : %s"`. Largura em `d` aparece em 34 titulos
(com ruido; as amostras de contexto que vi sao format strings reais).
Sintoma: `cnk2` constroi um nome de ficheiro e recebe `body7.pof` em vez de
`body007.pof` -> o `OpenFile` falha e o titulo segue sem o recurso.

### F11 -- clip aceito sem validar (`tela.cpp:16-23`, `:49-54`)

```
      17|   if (x < static_cast<int>(clip_[0]) || y < static_cast<int>(clip_[1])) return;
      18|   if (x >= static_cast<int>(clip_[0] + clip_[2])) return;
      19|   if (y >= static_cast<int>(clip_[1] + clip_[3])) return;
      49| void Tela::Clip(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) {
      50|   clip_[0] = x;
      51|   clip_[1] = y;
      52|   clip_[2] = w;
      53|   clip_[3] = h;
      54| }
```

`Clip` aceita tudo e nao registra nada; `Ponto` recusa em silencio (o `return`
sem evento) e `escritos_` deixa de contar. Um clip com `w = 0xFFFFFFFF` (vindo de
um `w` negativo do guest) faz `clip_[0]+clip_[2]` dar `0xFFFFFFFF`, o
`static_cast<int>` dar `-1`, e `x >= -1` ser verdade para todo o `x`: a tela fica
preta sem uma linha que o explique. A medida da `Tela` e o censo de cores, portanto
o sintoma aparece como "o titulo nao desenha".

Demanda: confirmada por OUTRO ficheiro (nao e meu): `despacho.cpp:679` le
`mem_.Ler32(prc + 0/4/8/12)`, ou seja 4 `uint32` = 16 bytes, de um `AEERect` que
o SDK define como **`int16 x,y; int16 dx,dy;`** -- 8 bytes
(`platform/ui/inc/AEERect.h:21-24`; `platform/ui/inc/AEEIDisplay.h:240` declara
`void (*SetClipRect)(iname *po, const AEERect * pRect)`). O `GetClipRect`
correspondente (`despacho.cpp:884-892`) escreve 16 bytes por cima de uma struct
de 8, no espaco do guest. Isto pertence ao auditor do `despacho`, mas a `Tela` e
quem paga: recebe lixo onde devia validar.

### F12 -- `mudo` conta como amostra nao nula (`misturador.cpp:34-37`)

```
      34|       const std::int32_t bruto = amostras[ja + k];
      35|       acumulador_.push_back(Escalar(amostras[ja + k], volume));
      36|       ++medida_.amostras_recebidas;
      37|       if (bruto != 0) ++medida_.amostras_nao_nulas;
```

`bruto` e a amostra CRUA. Com `mudo` (linha 17: `if (mudo) volume = 0;`), o que
vai para o acumulador e zero, mas o contador de "nao nulas" sobe.
O cabecalho diz o contrario:

```
      54|   // `mudo` aplica MM_PARM_MUTE (1 = mudo, 0 = normal): as amostras ZERAM, e
      55|   // portanto nao contam para `amostras_nao_nulas` -- que e o que o criterio
      56|   // da etapa mede.                                    (misturador.h)
```

e o TESTE diz o contrario do cabecalho:

```
      56| TEST(Misturador, MudoZeraOPicoSemDescartarAContagem) {
      57|   // MUTE (MM_PARM_MUTE) e um pedido ACEITO: as amostras continuam a passar e a
      58|   // ser contadas, mas o que sai e zero. As duas contagens dizem coisas
      59|   // diferentes de proposito.
      65|   EXPECT_EQ(m.MedidaAcumulada().amostras_nao_nulas, 32u);   (tests/audio_test.cpp)
```

A contradicao esta CONFIRMADA; qual dos dois lados e o defeito e decisao do dono.
O que e facto: com `MM_PARM_MUTE` aceito (`imedia.cpp:112`) e passado adiante
(`imedia.cpp:437`: `misturador_.MisturarNoBloco(..., o.volume, o.mudo, 0)`), o
numero que o criterio da etapa 5 le sobe com a SAIDA a zero. Um titulo que so
toque com mudo satisfaz "o misturador reporta amostras nao nulas".

### F13 -- as formas Thumb que faltam RECUSAM com nome (`arm_interpreter.cpp:882`)

```
     882|   Recusar(instr, pc, "forma Thumb NAO implementada");
```

Correto segundo P2 (o `Recusar` poe opcode e PC no traco e conta em
`recusadas_`). O que se pode medir da janela executada do `rocketweb` (PCs com
passo 2, portanto Thumb):

```
     782:	447a      	add	r2, pc          <- registrador alto, cai aqui
     786:	4479      	add	r1, pc          <- registrador alto, cai aqui
     788:	003b      	movs	r3, r7
     78a:	466f      	mov	r7, sp          <- registrador alto, cai aqui
     78c:	c707      	stmia	r7!, {r0, r1, r2}   <- 0xC000, cai aqui
```

`rocketweb` tem `recusadas = 18`. O `bateria` nao diz QUAL instrucao recusou
(`PAREDE-DA-PILHA.md:181-183` diz-o explicitamente), portanto isto e indicio
forte, nao prova de que as 18 sejam estas.
Sintoma: o `add r2, pc` nao soma nada (o resultado fica por calcular) e o
`stmia` nao guarda -- mas as duas ficam no traco com o opcode.

### F14 -- recusa depois de ter mexido no estado (`arm_interpreter.cpp:545-571`)

```
     560|   for (int i = 0; i < 16; ++i) {
     561|     if ((lista & (1u << i)) == 0) continue;
     562|     if (l) {
     563|       Set(i, mem_.Ler32(endereco));
     564|     } else {
     565|       mem_.Escrever32(endereco, Get(i));
     566|     }
     569|   if (w) Set(static_cast<int>(rn), u ? base + static_cast<Reg>(quantos) * passo
     571|   if (s) Recusar(instr, pc, "LDM/STM com S (banco de usuario) nao implementado");
```

O `s` (banco de usuario) e conferido no FIM: os 16 registradores ja foram
carregados/guardados e a base ja andou quando a recusa sai. Ao contrario do
`TransferenciaExtra`, que valida tudo antes de escrever (linhas 345-381), aqui a
recusa deixa o estado a meio. REGISTADO (ha evento), mas o estado nao devia ter
sido tocado.

### F15 -- lista vazia: recusa e nao avanca o PC (`arm_interpreter.cpp:553`)

```
     553|   if (lista == 0) { Recusar(instr, pc, "LDM/STM com lista de registradores vazia"); return; }
```

Todas as outras recusas do interpretador saem por um caminho que avanca o PC (o
`ExecutarArm` faz `Set(kPC, pc + 4)` a seguir). Esta faz `return` sem tocar no PC,
e o chamador e `if (g == 4) { Bloco(instr, pc); return; }` (linha 784), que
tambem nao avanca. A mesma instrucao reexecuta: `recusadas_` sobe uma vez por
passo e a corrida so para no orcamento. Nao e um laco infinito silencioso (ha
evento e ha contador), mas e um caminho com um comportamento diferente de todos
os outros, sem nada a dizer que e de proposito.

### F16 -- `SWI` recusada e a execucao continua (`arm_interpreter.cpp:682-684`, `:787-789`)

```
     682| void ArmInterpreter::SWI(std::uint32_t instr, std::uint32_t pc) {
     683|   Recusar(instr, pc, "SWI sem tratador registado");
     684| }
     787|   // 111: SWI
     788|   SWI(instr, pc);
     789|   Set(kPC, pc + 4);
```

A recusa fica registada (bom) e a execucao segue como se o `SWI` tivesse
acontecido. Quem despacha pelo PC dentro da faixa de saida nao ve nada disto.

### F17 -- `escrita no PC com S` recusada com nome (`arm_interpreter.cpp:487-491`)

```
     487|   if (rd == kPC) {
     488|     if (s) {
     489|       Recusar(instr, pc, "escrita no PC com S (restauro de SPSR) nao implementada");
     490|       return;
     491|     }
```

REGISTADO: recusa com o opcode e o PC no traco, e o registrador destino nao e
escrito. E a forma que o retorno de excepcao usa (`movs pc, lr` /
`ldmfd sp!, {..., pc}^`). Demanda: nao medido (nenhuma recusa da bateria diz QUAL
instrucao, cf. D4).

### F18 -- `MRC p15` devolve uma constante declarada (`arm_interpreter.cpp:667-677`)

```
     672|     Set(static_cast<int>(rd), 0x410FB760u);
     674|       traco_->RegistarFalta(Area::Cpu, "coprocessador p15 leitura real",
     675|                             "devolvido um valor declarado de cache type");
```

REGISTADO, e o exemplo que o pedido manda contar como certo... com uma ressalva:
o valor e o mesmo para QUALQUER registrador p15 pedido (o `instr` nao decide
nada), portanto uma leitura que nao seja o tipo de cache recebe uma resposta de
tipo de cache, com evento a dizer "falta". O contrato do ARM diz que CRn/opcode2
seleccionam o registrador.

### F19 -- as recusas do grupo extra (`arm_interpreter.cpp:345-381`)

```
     346|     Recusar(instr, pc, "extra load/store com Rn ou Rt = PC e UNPREDICTABLE no ARM");
     350|     Recusar(instr, pc, "forma nao privilegiada do extra load/store (LDRHT/STRHT/LDRSBT/LDRSHT) nao implementada");
     354|     Recusar(instr, pc, "LDRD/STRD com Rt impar e UNPREDICTABLE no ARM");
     358|     Recusar(instr, pc, "extra load/store com escrita na base e Rd = Rn e UNPREDICTABLE no ARM");
     367|     Recusar(instr, pc, "offset de registrador com os bits 11-8 diferentes de zero nao implementado");
     372|     Recusar(instr, pc, "LDRD/STRD com offset de registrador, P=0 e U=0 nao e forma valida");
     379|     Recusar(instr, pc, "offset de registrador nesta forma de extra load/store nao e valido no ARM");
```

Todas com nome e opcode. Os dois lados que faltam: `rn == 15`/`rt == 15` sao
recusados como "UNPREDICTABLE" embora `LDRH r0,[pc,#imm]` seja forma VALIDA no
ARM (o PC como base e legal no extra load/store; so o `Rt = PC` e
UNPREDICTABLE) -- ou seja, uma instrucao valida esta a ser recusada com um motivo
que diz que ela e invalida. REGISTADO, mas o motivo mente sobre a forma.

### F20 -- `Repor` nao repoe tudo (`arm_interpreter.cpp:154-163`)

```
     154| void ArmInterpreter::Repor(Reg pc, Reg sp) {
     155|   for (int i = 0; i < 16; ++i) banco_[i] = 0;
     158|   n_ = z_ = c_ = v_ = false;
     159|   modo_atual_ = static_cast<std::uint32_t>(Modo::Usuario) | Cpsr::kI | Cpsr::kF;
     160|   recusadas_ = 0;
     161|   ultima_recusada_ = 0;
     162|   pc_da_recusada_ = 0;
     163| }
```

Nao limpa `sombra_fiq_r8_r12_`, `sombra_irq_r13_r14_`, `sombra_svc_abt_und_`,
`spsr_` nem `modo_atual_valido_`. A bateria chama `Repor` duas vezes por titulo
(`bateria.cpp:646` antes da fase `create`, `:684` antes da fase `start`): o estado
com que a segunda fase comeca depende do que a primeira fez. Em P4
(determinismo por construcao) isso e o que se quer evitar. E `recusadas_ = 0`
apaga as recusas da fase anterior: quem le o contador so no fim perde metade da
historia (a bateria le as duas).

### F21 -- argumento a mais devolve zero (`formato.cpp:31-33`)

```
      31|   const auto argumento = [&](void) -> std::uint32_t {
      32|     return proximo < nArgumentos ? argumentos[proximo++] : 0u;
      33|   };
```

Uma format string com mais conversores que argumentos produz zeros e nenhuma
linha que o diga. Quem resolve os argumentos e o `despacho.cpp:932-945`, com um
teto de 8.

### F22 -- escrita no buffer do guest sem limite (`formato.cpp:114-118`)

```
     114|   for (std::size_t k = 0; k < saida.size(); ++k) {
     115|     mem.Escrever8(pBuf + static_cast<Endereco>(k), static_cast<std::uint8_t>(saida[k]));
     116|   }
     117|   mem.Escrever8(pBuf + static_cast<Endereco>(saida.size()), 0);
```

`Formatar` nao conhece o tamanho do buffer do chamador (o `sprintf` tambem nao),
mas nao tem limite NENHUM: um `%s` de uma cadeia longa mais um prefixo escreve o
que quiser na memoria do guest. Silencioso por natureza (nao ha evento nem
contador de bytes escritos).

### F23 -- ficheiro de 0 bytes recusado (`arquivo.cpp:25-32`)

```
      25|   std::ifstream f(pasta_do_titulo + "/" + caminho, std::ios::binary);
      26|   if (!f) return 0;
      27|   Aberto a;
      28|   a.dados.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
      29|   if (a.dados.empty()) return 0;
      30|   a.id = proximo_id_++;
      31|   abertos_.push_back(a);
      32|   return a.id;
```

Seis recusas devolvem o MESMO `0`, e o unico motivo que o chamador pode
distinguir e "nao ha IFile". A linha 29 transforma "o ficheiro existe e esta
vazio" em "nao existe": o SDK (`AEEFile.h`, `_OFM_READ`) distingue os dois.
Demanda: o corpus tem UM ficheiro de 0 bytes, `mod/276212/pm.bin` do
`pacmania` (medido: `os.path.getsize == 0`), e a cadeia `pm.bin` esta em
`0x1306c` do `pacmania.mod`. O `pacmania` tem 7 faltas de `IShell::slot41`;
nao medi se ele abre o `pm.bin` pelo `OpenFile` -- **nao medido = nao medido**.

### F24 -- `Informacao` devolve sucesso sem escrever (`arquivo.cpp:76-79`)

```
      76| bool Arquivos::Informacao(std::uint32_t id, Memoria& mem, Endereco pInfo) const {
      77|   const Aberto* a = Procurar(id);
      78|   if (a == nullptr) return false;
      79|   if (pInfo == 0) return true;
```

Contrato do SDK (`AEEFile.h`, `IFILE_GetInfo`): `pInfo [out] : placeholder for
file information` -- `AEE_SUCCESS : If successful`. Nao ha nada que autorize
`pInfo` nulo, e menos ainda que autorize devolver sucesso com o `[out]` por
preencher. E "devolve sucesso e nao faz nada", a definicao do pedido.

### F25 -- `Fechar` de id desconhecido (`arquivo.cpp:89-96`)

```
      89| void Arquivos::Fechar(std::uint32_t id) {
      90|   for (auto it = abertos_.begin(); it != abertos_.end(); ++it) {
      91|     if (it->id == id) {
      92|       abertos_.erase(it);
      93|       return;
      94|     }
      95|   }
      96| }
```

Fechar o que nunca foi aberto (ou duas vezes) nao deixa rasto. Baixo, mas e
exactamente a familia "aceita e ignora".

### F26 -- escrita em bloco fora da vigia (`memoria.cpp:117-133`)

```
     117| void Memoria::EscreverBloco(Endereco a, const void* origem, std::uint32_t quantos) {
     125|     std::uint8_t* p = Pagina(n << kPaginaBits, true);
     126|     std::memcpy(p + dentro, o + feito, neste);
     131| void Memoria::EscreverBruto(Endereco a, const void* origem, std::uint32_t quantos) {
     132|   EscreverBloco(a, origem, quantos);
     133| }
```

A vigia e o contador de escritor vivem em `Escrever8` (linhas 74-102). Como
`Escrever16/32` passam por `Escrever8`, as escritas escalares sao vistas; as
escritas de BLoco (e o `EscreverBruto`, que e literalmente um alias) nao passam
por nada disso. O cabecalho diz que `EscreverBruto` e "reservado a carregar o
modulo" -- mas `EscreverBloco` e publico e faz exactamente o mesmo, portanto a
distincao nao existe no codigo. `despacho.cpp:638` e `ajudantes.cpp:134` usam-na
para escrever memoria do guest.
Sintoma: uma faixa vigiada escrita por um `memcpy` do guest nao gera evento
`VIGIA:` -- e na arvore antiga "sem linha no log" era lido como "nao aconteceu".

### F27 -- a vigia nao tem chamador (`memoria.cpp:135-140`)

```
     135| void Memoria::Vigiar(const Vigia& v) { vigias_.push_back(v); }
     137| void Memoria::PararDeVigiar() {
     138|   vigias_.clear();
     139|   escritas_vigiadas_.clear();
     140| }
```

`grep -rn Vigiar` no repositorio inteiro: a declaracao, a definicao e
`tests/fundacao_test.cpp:222,237`. Nenhum `sonda_*`, nenhum `bateria`, nenhum
`despacho`. O evento `VIGIA: ` (`memoria.cpp:99`, o unico sítio que o emite) nao
pode nascer numa corrida real. O cabecalho apresenta a vigia como o instrumento
que descobriu que a pilha do `Boiaz` era escrita pelo proprio jogo -- hoje e um
instrumento so ao alcance dos testes.

### F28 -- `nullptr` engolido no misturador (`misturador.cpp:26`, `:50`)

```
      26|   if (amostras == nullptr || quantas == 0) return;
      50|   if (amostras == nullptr) return;
```

`quantas` amostras pedidas e nao misturadas, sem evento e sem contador. O
bloco do volume acima do maximo (linhas 18-25) e o exemplo a imitar: trava e
CONTA.

### F29 -- recusa do carregador sem evento (`mod.cpp:11-14`, `:23-26`)

```
      11|   if (imagem.empty()) {
      12|     r.motivo = "imagem vazia";
      13|     return r;
      14|   }
      23|   if (static_cast<std::uint64_t>(base) + imagem.size() > 0x100000000ull) {
      24|     r.motivo = "imagem nao cabe no espaco de enderecos a partir desta base";
      25|     return r;
      26|   }
```

O `traco` e passado a funcao e so serve o caminho de sucesso (linha 36). A
recusa sai em `motivo` (o chamador `bateria.cpp:639` poe-a no registo da
bateria), mas o `Traco` -- o ponto unico que o P7 quer -- nao sabe nada.
`ok = false` impede o uso descuidado, portanto e baixo.

### F30 -- contadores do tempo sem leitor (`tempo.h:39-45`, `:54-58`)

```
      39|     if (quanto < 0) {
      40|       ultimo_avanco_negativo_ = quanto;
      41|       ++avancos_invalidos_;
      42|       return;
      43|     }
      54|   void Repor() {
      55|     agora_ = 0;
      56|     avancos_invalidos_ = 0;
      57|     ultimo_avanco_negativo_ = 0;
      58|   }
```

O comentario diz que "engoli-lo em silencio esconderia uma contagem de tempo
invertida -- o oposto do principio P2". Mas o registo e um contador cujo unico
leitor e `tests/fundacao_test.cpp:47,56`; numa corrida real um avanco negativo e
engolido em silencio com um numero que ninguem le (a familia do
`Despacho::Faltas()`). E `Repor()` nao tem chamador nenhum.

### F31 -- destino de log que nao abriu (`traco.cpp:119-122`)

```
     119| void DestinoFicheiro::Escrever(const Evento& e) {
     120|   if (f_ == nullptr) return;
     121|   std::fprintf(static_cast<std::FILE*>(f_), "%12lld %-7s %s %-28s | %s\n",
```

O `motivo` e guardado (h:112) mas `Escrever` nao o propaga: um registador com um
destino que nao abriu emite para o vazio. Hoje so os testes constroem
`DestinoFicheiro` (e o teste le `Abriu()`/`Motivo()`), portanto e latente.

### F32 -- `Traco` sem relogio (`traco.cpp:96`, `:106`)

```
      96|     e.quando = tempo_ != nullptr ? tempo_->Agora() : 0;
     106|   e.quando = tempo_ != nullptr ? tempo_->Agora() : 0;
```

Um `Traco` construido sem `Tempo` (o valor por omissao de `tempo` e `nullptr`,
`traco.h:125`) poe `quando = 0` em todos os eventos e nao avisa. A regra 4 do cabecalho
existe para os contadores serem comparaveis; um carimbo de tempo constante e o
mesmo defeito uma camada abaixo.

### F33 -- documento do carregador promete mais do que faz (`mod.h:44-48`)

```
      44| // Carrega `imagem` em `base` e aponta o PC para la.
      45| //
      46| // `tabela_de_ajudantes` e o endereco da `AEEHelperFuncs`, e vai para `base - 4`
```

A funcao nao recebe CPU nenhuma e nao toca no PC: quem o aponta e o chamador
(`bateria.cpp:646` `cpu.Repor(kBase, kPilha)`). Documentacao, baixo.

### F34 -- ROPI escrito e nao conferido (`mod.cpp:28-31`)

```
      28|   // A convencao ROPI. Escreve-se mesmo quando o valor e zero: deixar o sitio por
      29|   // escrever faria o modulo ler o que la estivesse, e um zero acidental e
      30|   // indistinguivel de um zero propositado no log.
      31|   mem.Escrever32(endereco_ropi, tabela_de_ajudantes);
```

O raciocinio esta certo e a escrita nunca e conferida. Nesta arvore ja se perdeu
uma cablagem que "passou na conferencia e nao servia"
(`imedia.cpp:170-179`) -- ler de volta o `[base-4]` e uma linha. Baixo.

### F35 -- leitura do `.bar` sem verificacao de erro (`bar.cpp:57-58`)

```
      57|   std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)),
      58|                                   std::istreambuf_iterator<char>());
```

O `if (!f)` da linha 49 cobre "nao abriu"; nao ha nada a cobrir "abriu e a
leitura morreu a meio". O vector fica com o que veio e o `Validar` recusa (o
ultimo deslocamento deixa de ser igual ao tamanho do ficheiro) -- com um motivo
que acusa o FORMATO, nao o I/O. Nao e um sucesso silencioso (ha recusa com
motivo); o que esta errado e o diagnostico apontar para o lado errado. Por isso e
CANDIDATO e nao SILENCIOSO.

---

## c) NAO E DEFEITO MAS PARECE

1. **`IHeap1::Lock` a devolver sucesso** (`despacho.cpp:903-911`, nao e meu
   ficheiro, mas e o exemplo que o pedido cita): num emulador de um escritor so
   (P6), "nao ha nada a bloquear" e o CONTRATO. Certo.
2. **`IFile::Write` a devolver 0 bytes** (`despacho.cpp:988-991`) e **`RmDir` a
   devolver `kAeeUnsupported`** (`:1037-1043`): a VFS e so de leitura por
   DECISAO, e a recusa e registada. Certo.
3. **`FreeResData` a nao fazer nada** (`despacho.cpp:912-917`): nao ha recursos
   alocados pelo `LoadResDataEx`; recusar e a resposta certa.
4. **`Memoria::Ler8` a devolver 0 para pagina nunca escrita** (`memoria.cpp:36`):
   desenhado e documentado ("ler nao aloca") e ha um teste que o prova
   (`fundacao_test.cpp:198-200`). Nao e stub; e o contrato da memoria esparsa.
   O efeito colateral -- um PC perdido em memoria nao mapeada executa zeros e o
   interpretador nao recusa -- e coberto por fora: o `despacho` confere a faixa do
   modulo e registra `saiu_do_modulo_para_0x...` (`despacho.cpp:1140-1171`), que e
   o que a bateria mostra em 21 dos 62 titulos.
5. **`MRC p15` com valor declarado + `RegistarFalta`** (`arm_interpreter.cpp:667-677`):
   e o padrao certo do P2 (a ressalva sobre o registrador pedido esta em F18).
6. **`Coprocessador` a recusar com nome** (`:679`), **`SWI` a recusar com nome**
   (`:683`), **`condicao NV` a recusar** (`:689`), **as 7 familias do extra
   load/store a recusar** (`:345-381`), **`forma Thumb NAO implementada`** (`:882`):
   recusas registadas, que e o que o P2 pede. Sao buracos de implementacao
   VISIVEIS, nao stubs silenciosos.
7. **`TransferenciaExtra` a recusar `Rt = PC`** (`:346`): no ARM o `Rt = PC` no
   extra load/store e de facto UNPREDICTABLE.
8. **`Memoria::LerCadeia` a devolver `limite`** quando nao acha o NUL
   (`memoria.cpp:142-149`): documentado e limitado.
9. **`Tela::CorAtual` a truncar a 16 bits** (`tela.h:36`): e o contrato do RGB565.
10. **`Vfs::Normalizar` a colapsar `..` em vez de recusar** (`vfs.cpp:28-30`):
    e uma decisao escrita ("`..` NAO sai da pasta do modulo") e o resultado nunca
    sai da pasta (a VFS e uma lista PLANA de nomes de ficheiro, portanto nao ha
    travessia possivel). O lado estranho e `Existe("pasta/sub/x")` devolver
    `kAeeSuccess` quando `x` esta na pasta do titulo -- deliberado e documentado
    (linhas 36-39).
11. **`Escalar` com divisao inteira** (`misturador.cpp:9-12`) e **`FlagsDaSoma`/
    `FlagsDaSubtracao`** (`arm_interpreter.cpp:35-60`): inteiros deterministicos,
    com a inversao do carry do ARM bem explicada. Certo.
12. **`Bar::Validar` a recusar com o campo que falhou** (`bar.cpp:68-206`): le o
    formato medido, confere a assinatura, a coerencia dos deslocamentos, a
    monotonia, o ultimo valor == tamanho do ficheiro e o alcance dos registos, e
    devolve sempre o motivo. Nao achei um caminho silencioso neste ficheiro.
13. **`sha256.cpp`**: transcrevi o algoritmo para Python e comparei com
    `hashlib.sha256` em 8 tamanhos (0, 1, 3, 55, 56, 64, 1000, 100000 bytes):
    **identico nos 8**. (O padding do bloco de 64, o `% 64 != 56` e o
    comprimento em bits estao certos.)
14. **`Cpsr`/`Modo`/`ModoValido`** (`cpu.h:32-58`): a armadilha do `cpsr = 0` esta
    fixada no construtor e no `SetCpsr`, com a medicao no comentario
    (`arm_interpreter.cpp:69-79`). Certo.

---

## d) CONTRADIZ O QUE SE PENSAVA

### D1 -- "nenhum titulo do corpus corre Thumb nesta fase" e FALSO

`docs/rewrite/PAREDE-DA-PILHA.md`, linhas 174-175:

```
   174| 4. **O Thumb continua sem `LDRH`/`STRH`/`LDRSB`/`LDRSH`** (`ExecutarThumb` tem 7
   175|    formas). Nao foi tocado: nenhum titulo do corpus corre Thumb nesta fase.
```

O `tools/baseline/bateria.json` do MESMO commit (`build eb62459`) mede o
contrario: `brainchallenge`, `reksio` e `rocketweb` executam instrucoes em
enderecos com PASSO 2 (logo Thumb), e as palavras que o anel regista decodificam
como Thumb coerente (`objdump -M force-thumb`):

```
reksio.mod     0x0994  6018   str r0, [r3, #0]
               0x0996  9300   str r3, [sp, #0]     <- executada (F1)
               0x0998  9202   str r2, [sp, #8]     <- executada (F1)
               0x099e  9101   str r1, [sp, #4]     <- executada (F1)
               0x09a4  f7ff fedc  bl 0x760
               0x09b6  bcfe   pop {r1-r7}
               0x09ba  4718   bx r3
```

Nao e so o comentario: o teste `tests/cpu_test.cpp:535-548`
(`BxParaEnderecoImparEntraEmThumb`) diz, no proprio comentario, "o modulo do Zeebo
tem codigo ARM e Thumb no mesmo ficheiro" -- e o unico teste de Thumb que existe.
O que falta nao e so o `LDRH/STRH` que a nota nomeia: falta o ramo `0x9000-0x9FFF`
inteiro, e esse e executado por dois titulos.

### D2 -- o criterio da etapa 5 pode passar com a saida muda

`PLAN.md` (etapa 5): "Criterio: um titulo toca som e o misturador reporta
amostras nao nulas". Medido: `amostras_nao_nulas` conta a amostra CRUA, portanto
um titulo que so toque com `MM_PARM_MUTE` (aceito por `imedia.cpp:112`) faz subir
o numero com a saida toda a zero. O cabecalho do misturador diz explicitamente o
contrario (h:54-56) e o teste diz o contrario do cabecalho
(`tests/audio_test.cpp:56-66`). O numero que serve de criterio nao distingue
"som" de "mudo".

### D3 -- a familia `LDRH/STRH` foi corrigida em ARM, mas o irmao Thumb ficou

`PAREDE-DA-PILHA.md:163-167` mede que `LDRH`/`STRH` de ARM "estavam a ser mal
executados em todo o corpus" (60 `strh`, 21 `ldrh` em 124 palavras distintas) e a
frente corrigiu isso com `TransferenciaExtra`. A MESMA frase, do lado Thumb, esta
por corrigir e agora e silenciosa (F2: `meia` morta; F1: o ramo `0x9000-0x9FFF`).
O padrao "escrever a familia ARM e deixar a Thumb" explica-se pelo ponto 4 acima
-- que a medicao do baseline desmente.

### D4 -- "a tabela de `recusadas` nao diz QUAL instrucao" esta certo e limita tudo

`PAREDE-DA-PILHA.md:181-183` diz: "com 4 milhoes de recusas num titulo, saber o
nome da forma recusada exigiu o `[DEBUG-pilha5]`". Confirmo: `nfs` (129026),
`allstarcards` (3999940), `rmp` (56173), `gof` (28166), `zeeboids` (48),
`rocketweb` (18), `reksio` (9), `brainchallenge` (7), `zeebovolley` (1). Nada no
`bateria.json` diz QUAL forma. Todas as minhas afirmacoes sobre recusas de
instrucoes sao por isso "indicio forte", nao prova -- e a medicao que falta e uma
palavra por forma no registo.

### D5 -- `AEE_MAX_FILE_NAME` e o `FileInfo` do `arquivo.cpp` estao CERTOS

Contra o que o pedido sugere ("campos que nunca sao escritos"), o `Informacao`
escreve a struct toda: `attrib` em `+0`, `dwCreationDate` em `+4`, `dwSize` em
`+8` e `AEE_MAX_FILE_NAME = 64` bytes de `szName` em `+12`
(`AEEFile.h:  typedef struct _FileInfo { char attrib; uint32 dwCreationDate;
uint32 dwSize; char szName[AEE_MAX_FILE_NAME]; }`). O unico campo que mente e o
`szName`, que vai a ZERO (o nome do ficheiro nao e escrito) -- e o `AEEFileInfo`
nao tem mais nada.

---

## e) Negativas honestas (ficheiros sem achados)

- `core/brew/sha256.{h,cpp}`: **nenhum achado**. Verifiquei o algoritmo contra o
  `hashlib` (8 tamanhos, tudo igual) e nao ha caminho de recusa nem de estado.
- `core/carga/bar.{h,cpp}`: **nenhum achado SILENCIOSO**. Todas as recusas levam o
  campo e o valor no `motivo`; as duas guardas "defensivas" (`Procurar`,
  `LerPorIndice`) estao declaradas como inalcancaveis em `violacoes.py` M23/M24, o
  que e honesto. O campo 28 do cabecalho nao ser lido e deliberado e explicado
  (`bar.h:32-37`). O unico ponto residual e F35 (leitura truncada diagnosticada
  como formato, e nao como I/O).
- `core/traco/traco.{h,cpp}`: nenhum achado alem de F31/F32; as regras 1 a 7 do
  cabecalho estao implementadas (emissao sem nome vira `EMISSAO_SEM_NOME`;
  `RegistarFalta` sem nome vira `FALTA_SEM_NOME`; caminho relativo e recusado com
  motivo; `Comparar` recusa etiquetas diferentes).
- `core/brew/tela.{h,cpp}`: so F11.
- `core/brew/arquivo.{h,cpp}`: F23, F24, F25 -- o resto (Ler/Posicionar/
  ModoMudaOFicheiro) segue o contrato do SDK que li.

## f) Limites desta auditoria (para o pai calibrar)

1. Nao corri `cmake` nem o build; nao executei o emulador. Tudo o que afirmo sobre
   execucao vem do `tools/baseline/bateria.json` (build `eb62459`) e do
   `arm-none-eabi-objdump`.
2. **A forma textual do `bateria.json` (`motivo`, `ultimas:`) NAO existe na
   arvore actual**: `grep -rn ultimas tools/ core/` nao encontra nada, logo o
   baseline foi produzido por uma revisao dos instrumentos anterior a esta. Usei
   os NUMEROS e a interpretacao `pc:mem32(pc)`, que confirmei byte a byte contra
   os 3 `.mod` do corpus. O campo `recusadas` e o resto do JSON nao dependem do
   texto.
3. O Thumb de `brainchallenge`/`reksio`/`rocketweb` esta provado pelo passo de 2
   bytes entre PCs consecutivos do anel e pela coerencia do desmontar Thumb; nao
   vi uma captura de ecra nem corri o titulo.
4. Nao medi a DEMANDA de F3, F4, F5, F14, F15, F16, F20, F22, F24, F34. Onde
   escrevi "nao medido", significa: nao sei quantos titulos o pedem e NAO o
   afirmo.
5. `grep -o 'nome' tools/brew_slots.inc`: procurei os nomes que me interessavam
   (`LoadResDataEx`/slot 41, `SetMediaParm`, os slots de IFile). O `slot41`
   aparece em 21 titulos (campo `faltas`), o que mostra que o caminho `.bar` esta
   em demanda e que o `bar.cpp` NAO esta morto; `AEEHelperFuncs[0x140] vsnprintf`
   e a falta mais pedida (116), mas `vsnprintf` NAO e servido pelo `formato.cpp`
   (os servidos sao `sprintf` 0x020 e `vsprintf` 0x13c) -- portanto essa falta
   nao e um defeito deste ficheiro, e um buraco declarado no `ajudantes`.
