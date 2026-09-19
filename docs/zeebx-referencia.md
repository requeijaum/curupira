# Zeebx como referencia de contrato

Zeebx (`/home/rafaelfrequiao/projects/zeebx-emu`, rev. `2aee6a9`) e um irmao HLE BREW do Curupira. Ambos reimplementam contratos do runtime BREW; Curupira nao e um emulador firmware/BIOS LLE.

## Limite de licenca

Zeebx declara `GPL-2.0-only` em `Cargo.toml`; Curupira e MIT. Nenhum codigo, texto de implementacao ou traducao linha-a-linha do Zeebx pode entrar aqui. Zeebx serve para estudar tecnicas, algoritmos, estados, ordem de chamadas e contratos. Cada mudanca Curupira deve ser reimplementada independentemente em C++ com TDD e evidencia propria.

## Estado das faltas diretas do corpus

| Frente | Estado Curupira | Referencia Zeebx | Regra / proximo experimento |
|---|---|---|---|
| `a3d` escrita `udata/a3d.sav` | Resolvida por COW efemero (`d1157a6`): CREATE/RW/APPEND nunca escrevem host/NAND. | `src/brew/vfs.rs`, `src/machine/file.rs` escrevem diretamente no cache host. | Nao copiar escrita host. Proximo experimento: trace real a3d post-COW para confirmar chamadas Remove/RmDir se surgirem. |
| `FONT_STANDARD11/15/36` | Resolvido (`e1be2da`): CLSIDs, objetos/vtable IFont, `GetInfo`/`MeasureText`. `DrawText` recusa com nome. | Zeebx tem fallback de display/TTF, nao classes FONT_STANDARD. | Nao substituir fontes nomeadas por fallback unico. Implementar raster somente quando titulo medir `IFont::DrawText`. |
| `MMD_BUFFER` | Resolvido por classe/conteudo; WAV, MIDI/MP3 timeline e QCP QCELP/EVRC real (`9cdea7a`). Sem sink host. | `src/machine/media.rs` e `src/audio` misturam PCM/CPAL. | Nao tratar blob MP3 como PCM. Proximo experimento para audibilidade: sink PCM de frontend sem dirigir relogio virtual. |
| ATC/ATITC | Resolvido para formatos medidos; entrada truncada recusa explicitamente. | `src/video/atc.rs` e `src/machine/gl.rs` sao mais permissivos. | Manter validacao Curupira ate haver recurso real malformado que exija compatibilidade permissiva. |
| loader/MIF/extensoes | Curupira mede extensoes e executa `AEEMod_Load` na faixa guest adequada; a3d/IMicro3D ja destravou pixels. | Zeebx carrega extensoes em endereco HLE fixo e posterga invocacao. | Nao copiar endereco fixo Zeebx; guardar proveniencia MIF como evento, nao pressuposto (`cbc0ee8`). |
| `quake2brew` PAKZ/EnumInit | Bloqueado explicitamente. `autoexec.cfg` nao existe nas 3330 entradas reais. Raw PAKZ e PAK logico foram medidos e causaram alocacoes invalidas. | VFS Zeebx resolve arquivos host; nao prova formato que `FS_LoadPakFile` do guest espera. | Proximo experimento: trace ABI do loader interno (Read/Seek/GetInfo e diretorio PAK) antes de nova VFS. Nunca fabricar `autoexec.cfg`/EnumInit vazio. |
| `rocketweb` splash/Prompt/SIDHASH | Splash nao existe na pasta do titulo; Prompt(NULL) e ShowCopyright visual; SIDHASH nao possui valor medido. | Sem evidencia de contrato aplicavel. | Manter recusas nomeadas. Proximo experimento: localizar assets/firmware ou medir dispositivo. |

## Tecnicas HLE a aproveitar sem copiar

Branches de correcao Zeebx devem ser lidos como relatorio de tecnica: `fix-chessbots`, `fix-jetboard`, `fix-zwheel-roller`, `fix-fp-memoria`, `fix-fp-display`, `fix-fp-threading`, `fix-guardas-imagem`, `fix-botao-segfault` e `perf-apresentacao`. O aprendizado valido e identificar o tipo de estado/ordem/guarda que um titulo exige; a implementacao Curupira precisa de desenho e testes independentes.

O VFS gravavel e fontes STANDARD ja exemplificam isso: Curupira COW efemero e objetos IFont nomeados foram medidos e testados, enquanto Zeebx usa escrita host e fonte de display/TTF diferentes. Nao sao candidatos a transplante.

## Ordem e slots

Zeebx usa tabelas ordenadas por cabecalho em `src/brew/aee_slots.rs` e trampoins em `src/brew/aee.rs`. Curupira usa tabelas geradas `tools/brew_slots.inc`/`tools/igles_slots.inc`, testes de slots e saidas distintas por metodo. A semelhanca e contratual; IDs internos Zeebx nao sao copiados.

## Diferencas deliberadas

- Zeebx persiste saves no cache extraido; Curupira usa COW por corrida para nao alterar NAND canonico.
- Zeebx pode desenhar TTF e tocar host audio; Curupira nao anuncia renderizacao/audio que nao possui backend medido.
- Zeebx aceita alguns dados ATC truncados; Curupira prefere recusar com motivo.

## Evidencia consultada

- Zeebx: `src/brew/{aee_slots.rs,aee.rs,vfs.rs}`, `src/machine/{file.rs,gl.rs,media.rs,display.rs,mod.rs}`, `src/{audio,video/atc.rs,loader}`.
- Curupira: `core/brew/{vfs,arquivo,imedia,igl,classes,despacho}.cpp`, `core/audio/misturador.cpp`, `core/video/atitc.*`, testes e `docs/LEDGER-CURUPIRA.md`.

`docs/LEDGER-CURUPIRA.md` contem secoes historicas que chamam VFS de somente-leitura. Elas antecedem o COW atual e nao descrevem a implementacao corrente.

## Zeebx v0.2.0/v0.2.1: tecnicas novas (leitura em worktree GPL)

A referencia atual e `/home/rafaelfrequiao/projects/zeebx-0.2.1` (`v0.2.1`). Os commits foram lidos para extrair tecnicas, nunca transcritos.

| Tecnica Zeebx | Evidencia | Licao Curupira independente |
|---|---|---|
| lote/ring de vertices | `7e845e8`, `src/machine/gl.rs`, `src/machine/buffer*.rs` | Produzir geometria por tick e enviar em lotes pode recuperar quadros sem mudar estado GL; Curupira deve primeiro medir demanda e manter rasterizador determinista. |
| unidade 1 + `GL_COMBINE` | `e2cb591` | Textura unit 1 isolada nao basta: combinacao deve participar da cor final. TDD deve separar estado por unidade e testar `GL_MODULATE`/combine antes de anunciar efeito visual. |
| leitura de pixels | `e054e24` | `ReadPixels` precisa ter origem de linha e pack layout que o jogo espera; nao contar pixels como se fosse leitura correta. |
| scissor/viewport largo | `e91580f`, `09080d1` | Converter y de scissor pela mesma convencao de viewport e preservar proporcao larga; testar coordenadas de borda. |
| nevoa | `89a445d`, `6d4f43e` | Cor/fator de nevoa sao estado completo RGBA, nao constante parcial. |
| memoria | `6588d68`, `1df4015` | Dono/teto e `realloc` que conserva bloco antigo no fracasso sao contratos verificaveis; Brainchallenge e candidato de medicao. |
| `fs:/mod` | `63d77d3` | Mount do modulo deve ser chave virtual controlada, nunca fallback host arbitrario. |
| input | `84f92a6`, `src/input/sensores.rs` | Ordem de UIDs e sensores precisam ser medidos por evento, nao inferidos por nomes. |

Prioridade Curupira apos v0.2.1: textura unit 1/`GL_COMBINE`, `ReadPixels`, scissor/viewport e memoria de Brainchallenge. Cada uma deve ter TDD, corpus verde e reimplementacao C++ independente.
