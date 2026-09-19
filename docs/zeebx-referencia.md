# Zeebx como referencia de contrato

Zeebx (`/home/rafaelfrequiao/projects/zeebx-emu`, rev. `2aee6a9`) e um HLE BREW. Curupira executa codigo guest LLE. Este documento usa Zeebx apenas como evidencia de assinatura, ordem e retorno observavel. Nao e fonte de stubs para copiar.

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
