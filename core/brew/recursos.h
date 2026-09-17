#ifndef ZB2_CORE_BREW_RECURSOS_H
#define ZB2_CORE_BREW_RECURSOS_H

// OS RECURSOS DO BREW: `IShell::LoadResDataEx` (slot 41) e o `.bar`.
//
// Este modulo responde a UMA pergunta: o que se devolve ao guest quando ele pede
// um recurso. Nao conhece o IShell, nao le a pilha do guest e nao toca no
// despacho -- quem liga isto a faixa de saida e o `core/brew/despacho.cpp`, com
// oito linhas (o remendo esta no relatorio).
//
// ===========================================================================
// 1) A ASSINATURA, do cabecalho do SDK
// ===========================================================================
//
// `platform/system/inc/AEEIShell.h`, linha 305 (a macro), e linha 2417 (a
// documentacao):
//
//   void* (*LoadResDataEx)(iname *po, const char *pszResFile, uint16 nResID,
//                          ResType nType, void *pBuf, uint32 *pnBufSize);
//
// Em AAPCS isso sao os registradores `r0=po, r1=pszResFile, r2=nResID, r3=nType`
// e os dois ultimos na PILHA: `[sp+0]=pBuf` e `[sp+4]=pnBufSize`. Slot 41, gerado
// de `AEEIShell.h` por `tools/gerar_slots.py` -> `tools/brew_slots.inc:41`.
//
// `pszResFile` e um DESCRITOR: o `pacmania` passa a cadeia "pacmania.bar"
// (medida: a cadeia esta em 0x00101a64 do `pacmania.mod` e o campo +0x120 do
// applet guarda esse ponteiro).
//
// ===========================================================================
// 2) AS TRES FORMAS QUE O `pBuf` SELECCIONA -- do cabecalho do SDK
// ===========================================================================
//
// `AEEIShell.h:2440-2465`, citado palavra a palavra:
//
//   pBuf :  Buffer into which to load the resource data.
//           If pBuf is NULL, a buffer is allocated to hold the loaded resource,
//           and must be freed with ISHELL_FreeResData().
//           If pBuf is -1, no buffer is allocated, but size information is
//           calculated and returned in pnBufSize.
//   pnBufSize :  Cannot be NULL.
//              [in] : if pBuf is not NULL or -1. INPUT points to the sizeof(pBuf)
//                     in bytes.
//              [out]: Contains the size of the resource data read.
//                     If pBuf was not NULL on INPUT and *pnBufSize was too small
//                     to hold the resource, the function returns NULL.
//                     If pBuf was -1, pnBufSize is filled with the actual
//                     resource size, and the function returns -1 (0xffffffff).
//   Return Value: Void pointer to the resource data, if successful.
//                 NULL if loading the resource fails or if the buffer size is
//                 too small to hold the resource.
//
// Logo, as tres formas e os seus valores de retorno:
//
//   pBuf == (void*)-1  ->  escreve SO o tamanho em *pnBufSize, devolve -1
//   pBuf == NULL       ->  ALOCA o recurso inteiro, devolve o ponteiro
//   pBuf != 0          ->  copia para o buffer do chamador, devolve `pBuf`
//
// O `-1` de retorno e a parte que ninguem adivinha: devolver o tamanho no r0
// (que seria o "obvio") esta ERRADO, e o cabecalho diz as duas coisas que se
// devolvem nessa forma.
//
// ===========================================================================
// 3) O TAMANHO E O DO RECURSO INTEIRO, e nao o do dado -- medido no SDK
// ===========================================================================
//
// `Deprecated/pvs/platform/ui/widgets/src/utwidgets/UTResFile.c:140-177` e o
// PROPRIO TESTE DO VENDEDOR, e compara as duas formas:
//
//   nErr = IRESFILE_Get(MYRESFILE(me), IDB_BINARY_9001, RESTYPE_BINARY,
//                       (void *)(-1), &nLen1);          // so o tamanho
//   pRes1 = (char*)MALLOC(nLen1);
//   nErr = IRESFILE_Get(MYRESFILE(me), ..., pRes1, &nLen1);   // com buffer
//   ...
//   pRes2 = ISHELL_LoadResDataEx(GETSHELL(me), UTWIDGETS_RES_FILE,
//                                IDB_BINARY_9001, RESTYPE_BINARY, NULL, &nLen2);
//   TEST_ASSERT(me, "Compare resouce size", (nLen1 == nLen2));
//   nErr = MEMCMP(pRes1, pRes2, nLen1);
//
// Ou seja: o tamanho da forma "so o tamanho" E o numero de bytes que a forma com
// buffer escreve E o numero de bytes que a alocacao devolve. E um so numero, e e
// o RECURSO INTEIRO -- que, para um recurso de imagem, e o `AEEResBlob`
// (`bDataOffset`, zero, mime, dado), como o proprio cabecalho diz:
//
//   "If loading an image resource, the contents of pBuf is an AEEResBlob
//    structure, whose raw data lies at RESBLOB_DATA(pBuf)."
//   (`RESBLOB_DATA(b) ((void *)((byte *)(b) + (b)->bDataOffset))`,
//    `platform/deprecated/inc/AEEShell.h:173`)
//
// ===========================================================================
// 4) O QUE O `pacmania` FAZ, medido no proprio modulo (desmonte)
// ===========================================================================
//
// Tres sitios, e cada um usa uma das tres formas. As instrucoes (capstone,
// `pacmania.mod` com a base a ZERO -- ver `tests/mod_base_test.cpp`):
//
// a) 0x16864, chamada a 0x168a0 (`blx ip`, `lr=0x168a4`), o pedido de TAMANHO:
//      0011687c  add  r3, sp, #8
//      00116880  mvn  r2, #0          ; r2 = 0xFFFFFFFF
//      00116884  strd r2, r3, [sp]    ; [sp]=-1 (pBuf), [sp+4]=sp+8 (pnBufSize)
//      0011688c  uxth r2, r8          ; r2 = o id, que vem do CHAMADOR
//      00116890  ldr  r1, [r5, #0x120]; r1 = "pacmania.bar"
//      00116898  ldr  ip, [r3, #0xa4] ; 0xa4 = 41*4 -> o slot 41
//      0011689c  mov  r3, #6          ; RESTYPE_IMAGE
//      001168a0  blx  ip
//    e logo a seguir, a 0x168a4, o segundo passo da MESMA rotina:
//      001168a4  ldr  r0, [sp, #8]    ; le *pnBufSize
//      001168e0  ldr  r0, [sp, #8]
//      001168e4  add  r0, r0, #1
//      001168e8  bl   #0x1b26c        ; malloc(len+1) -- wrapper do 0x68 da tabela
//      001168f4  add  r3, sp, #8
//      001168f8  str  r3, [sp, #4]    ; pnBufSize = sp+8
//      001168fc  str  r4, [sp]        ; pBuf = o buffer alocado  (forma COPIAR)
//      00116918  blx  ip
//    -> a forma "so o tamanho" e a forma "copiar" na mesma rotina, com o mesmo
//       `sp+8` como `pnBufSize` nas duas.
//
// b) 0x10d8c, chamada a 0x10db8 (`lr=0x10dbc`), o pedido de ALOCACAO:
//      00110d90  add  r3, sp, #8
//      00110d94  mov  r2, #0          ; r2 = o ID cai a zero antes de ser reposto
//      00110d98  strd r2, r3, [sp]    ; [sp]=0 (pBuf NULL!), [sp+4]=sp+8
//      00110db4  ldr  r2, [pc, #0xd0] ; o id literal: mem[0x10e8c] = 0x13e3 = 5091
//      00110db8  blx  ip
//      00110dc4  streq r0, [r4, #0xe8]
//      00110dcc  ldrb r0, [r5]        ; r5 = o ponteiro DEVOLVIDO
//      00110dd4  add  r1, r5, r0      ; r1 = blob + bDataOffset
//      00110dd8  ldrb r0, [r1]        ; e daqui le os campos do dado
//      00110df0  ldrb r0, [r1, #5]    ; o maior deslocamento lido: 5
//    -> o jogo LE o `AEEResBlob` no valor de retorno, e o recurso 90 do
//       `pacmania.bar` tem EXACTAMENTE 34 bytes com `bDataOffset` = 0x1c = 28 e
//       mime "application/octet-stream", logo dados de 6 bytes (medido em
//       `core/carga/bar.h`, seccao 4).
//
// c) a chamada a 0x16918 (`lr=0x1691c`), com `pBuf` no `[sp]` e `*pnBufSize` no
//    `[sp+4]`, e a forma COPIAR: o buffer veio do `malloc` da alinea (a).
//
// E o pedido de demanda medido pela bateria, antes deste modulo:
//
//   IShell::slot41   3x
//     r2=0x00000000 ... lr=0x000168a4     (o id DINAMICO, que neste passo e ZERO)
//     r2=0x00000000 ... lr=0x0001691c     (idem)
//     r2=0x000013e3 ... lr=0x00010dbc     (5091, o id que EXISTE no `.bar`)
//
// ===========================================================================
// 5) O QUE ESTE MODULO NAO FAZ
// ===========================================================================
//
//  - Nao le a pilha do guest, nao conhece registradores, e nao decide politica
//    nenhuma do despacho: recebe um `PedidoDeRecurso` ja lido.
//  - Nao abre ficheiros por si: recebe um `LeitorDeRecursos`. E o que permite
//    testar as tres formas com um `.bar` SINTETICO, sem ROM e sem disco (P4), e
//    ao mesmo tempo o motor poder servir-se da VFS e da pasta do titulo.
//    A UNICA excepcao e o `.mif` do proprio modulo (`LoadResString` com base
//    NULA): ele mora FORA da pasta do titulo (na pasta irma `mif/`, medida), e
//    a VFS RETIRA o `..` em vez de o resolver -- logo o caminho posto pelo
//    despacho (`rec-despacho.patch`) le-se directamente, e o motivo de recusa
//    diz que caminho se tentou.
//  - Nao sabe o que e um recurso de tipo 7 (`RESTYPE_STRING`) para alem de
//    devolver os bytes: o `LoadResString` (slot 17) e outro metodo.
//  - Nao comprime nem descodifica: o `.bar` do corpus nao tem compressao
//    (medido nos 143 recursos do `pacmania.bar`), e a descodificacao de imagem
//    e de outro modulo.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/brew/ajudantes.h"
#include "core/brew/vfs.h"
#include "core/carga/bar.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

namespace zb2::brew {

// Os `ResType` do SDK (`platform/deprecated/inc/AEEShell.h:558-563`; os mesmos
// valores em `platform/system/inc/AEEIShell.h:25-30`). O `kTipoImagem` e o que o
// `pacmania` passa nas tres chamadas medidas (r3 = 6), e o unico em que o dado e
// precedido do `AEEResBlob`.
constexpr std::uint16_t kTipoTexto = 1;         // RESTYPE_STRING
constexpr std::uint16_t kTipoImagem = 6;        // RESTYPE_IMAGE
constexpr std::uint16_t kTipoDialogo = 0x2000;  // RESTYPE_DIALOG
constexpr std::uint16_t kTipoControlo = 0x2001;  // RESTYPE_CONTROL
constexpr std::uint16_t kTipoBinario = 0x5000;   // RESTYPE_BINARY

// A marca que abre um recurso de TEXTO de 8 bits, medido em 8 dos 13
// `aeecontrols.bar` do SDK (`en`, `es`, `it`, `da`, `fr`, ...): o recurso e
// `[0x03][caracteres]`, sem terminador. Os outros cinco (`he`, `ja`, `ko`) comecam
// por `0xFF 0xFE`, `0xFD 0xFE` e `0xFE 0xFE` -- que NAO estao descodificados aqui,
// e por isso sao RECUSADOS pelo nome da marca.
constexpr std::uint8_t kMarcaDoTextoDe8Bits = 0x03;

// O `(void*)-1` do `pBuf`, com o nome que o SDK lhe da na macro
// `ISHELL_GetResSize`.
constexpr std::uint32_t kSoOTamanho = 0xFFFFFFFFu;

// `bar_campos::kCabecalho` do leitor do `.bar` (`core/carga/bar.h`) e 32; o
// `bDataOffset` do recurso medido e 28. Ficam citados aqui so para quem le o
// registo de depuracao poder conferir os numeros sem abrir outro ficheiro.

enum class FormaDoRecurso {
  Tamanho,    // pBuf == (void*)-1
  Copia,      // pBuf != 0
  Alocacao,   // pBuf == 0
  Recusado,   // nada foi escrito
};

// O nome da forma, para o registo e para os testes nao compararem inteiros.
const char* Nome(FormaDoRecurso f);

// Um pedido, ja lido da ABI pelo despacho. Os nomes sao os do cabecalho do SDK.
struct PedidoDeRecurso {
  std::string ficheiro;             // pszResFile: o descritor, ex. "pacmania.bar"
  std::uint16_t id = 0;             // nResID
  std::uint16_t tipo = kTipoImagem; // nType
  std::uint32_t buffer = 0;         // pBuf, tal como o guest o passou
  std::uint32_t pn_tamanho = 0;     // pnBufSize; o cabecalho diz "Cannot be NULL"
  // LoadResDataEx tem pnBufSize; a API legacy LoadResData (slot 18) nao tem.
  bool tem_pn_tamanho = true;
  const char* nome_da_api = "IShell::LoadResDataEx";
};

struct ResultadoDoRecurso {
  bool ok = false;
  FormaDoRecurso forma = FormaDoRecurso::Recusado;
  std::string motivo;        // vazio quando ok
  std::uint32_t ponteiro = 0;  // o valor a devolver no r0
  std::uint32_t bytes = 0;     // bytes do recurso (o BLOB inteiro)
  std::string mime;            // so quando o recurso e do tipo 6 e o blob foi lido
};

// De onde vem os bytes de um `.bar`. Devolve `false` e o motivo quando o
// ficheiro nao existe, esta vazio ou nao se consegue ler.
using LeitorDeRecursos = std::function<bool(const std::string& ficheiro,
                                            std::vector<std::uint8_t>* bytes,
                                            std::string* motivo)>;

// O leitor do motor: a pasta do titulo, com a MESMA normalizacao de caminhos da
// VFS (`core/brew/vfs.cpp`) -- uma so regra, e nao duas verdades sobre que
// ficheiro existe.
LeitorDeRecursos LeitorDaPasta(const std::string& pasta_do_titulo, const Vfs* vfs);

// O pedido do `LoadResString` (IShell slot 17), ja lido da ABI pelo despacho:
// `int LoadResString(IShell *po, const char *pszBaseFile, uint16 nResID,
//                   AECHAR *pBuff, int nSize)` -> r1 = ficheiro, r2 = id,
// r3 = destino, `[sp+0]` = nSize.
struct PedidoDeTexto {
  std::string ficheiro;
  bool base_nula = false;  // r1 == 0: o SDK passa NULL para ler do proprio modulo (MIF)
  std::uint16_t id = 0;
  std::uint32_t destino = 0;  // AECHAR *pBuff: UTF-16
  std::uint32_t n_bytes = 0;  // `int nSize` -- o cabecalho diz "tamanho em bytes"
};

struct ResultadoDoTexto {
  bool ok = false;
  std::string motivo;
  std::uint32_t caracteres = 0;  // o valor de retorno do SDK
  std::uint32_t bytes_do_recurso = 0;
  std::uint8_t marca = 0;
};

// A tabela de recursos: os `.bar` abertos (por nome) e as alocacoes que ESTE
// servico fez (para o `FreeResData` poder saber o que e dele).
class Recursos {
 public:
  Recursos(Memoria& mem, Alocador& alocador, LeitorDeRecursos leitor, Traco* traco = nullptr);

  // Serve um pedido. Nunca finge: ou devolve `ok`, ou devolve `ok == false` com
  // o motivo (P2). O unico caminho que escreve no buffer do chamador e a forma
  // `Copia`; as outras duas escrevem SO em `*pn_tamanho`.
  ResultadoDoRecurso Atender(const PedidoDeRecurso& pedido);

  // O `LoadResString` (IShell slot 17). Le o recurso de tipo 1 e escreve-o no
  // destino como AECHAR (UTF-16). O QUE NAO ESTA MEDIDO, e fica DECLARADO: a
  // terminacao NUL no fim. Escreve-se quando cabe, porque um jogo que trate o
  // buffer como cadeia C fica correcto e um que conte os caracteres devolvidos
  // nao e afectado.
  // A BASE NULA (NULL) NAO E UM FICHEIRO: no SDK ela seleciona a cadeia do
  // proprio modulo (MIF -- `AEEShell.h:919-921` = `ISHELL_GetAppAuthor`,
  // `ISHELL_GetAppCopyright`, `ISHELL_GetAppVersion`; ids 6/7/8 em
  // `AEEShell.h:129-131`). O conteudo vem do `.mif`, que fica NA PASTA IRMA da
  // pasta do modulo -- `<pai de dir_>/mif/<pasta_>.mif`, medido em 62 ficheiros
  // do corpus --, e quem passa o caminho e o DESPACHO, uma linha que vive no
  // patch separado `rec-despacho.patch` (o ficheiro esta ocupado por outra
  // frente). Sem essa linha, recusa-se com o id no motivo. Representacao: 0x03
  // (8 bits) e o BOM UTF-16 0xFF 0xFE / 0xFE 0xFF; 12 de 62 `.mif` trazem 20
  // bytes depois do ultimo deslocamento, que nao sao recurso nenhum.
  // Demanda medida: chessbots, `alpineracerex` e `allstarcards` pedem o slot 17
  // 1x cada, base NULA, id 8 (IDS_MIF_VERSION; bateria de 2026-09-15, 62 titulos).
  ResultadoDoTexto ServirTexto(const PedidoDeTexto& pedido);

  // `FreeResData(IShell*, void*)` -- IShell slot 20. So liberta o que o
  // `Atender` alocou: um ponteiro alheio, ou ja libertado, e RECUSADO em voz
  // alta, porque entregar ao alocador um endereco que ele nao deu e a forma
  // mais silenciosa de corromper o heap. Um `endereco == 0` e "nao ha nada para
  // libertar" (o contrato e `void`), e conta-se.
  bool Libertar(std::uint32_t endereco);

  // --- o que se mediu, para o relatorio e para os testes ---------------
  struct Medicao {
    std::uint64_t pedidos = 0;
    std::uint64_t servidos = 0;
    std::uint64_t recusados = 0;
    std::uint64_t de_tamanho = 0;
    std::uint64_t copiados = 0;
    std::uint64_t alocados = 0;
    std::uint64_t bytes_servidos = 0;
    std::uint64_t bytes_libertados = 0;
    std::uint64_t textos = 0;
    std::uint64_t libertacoes = 0;
    std::uint64_t libertacoes_de_nulo = 0;
  };
  const Medicao& Contagem() const { return medicao_; }

  // As recusas agrupadas por motivo -- e a lista que a bateria mostra, e a razao
  // de o motivo ser uma cadeia e nao um codigo.
  const std::map<std::string, std::uint64_t>& Recusas() const { return recusas_; }

  // Os `.bar` abertos. `Arquivo("pacmania.bar")` devolve `nullptr` quando nunca
  // foi aberto, e o motivo fica em `FalhaAoAbrir`.
  const ArquivoBar* Arquivo(const std::string& nome) const;

  // A tabela de recursos propriamente dita: (id, tipo) -> bytes, no `.bar` ja
  // aberto. Nao abre nada e nao aloca nada: e a CONSULTA, e existe para o servico
  // nao ser a unica coisa que sabe o que esta aberto. Um ficheiro que nunca foi
  // aberto, ou um par que nao existe, recusam com o motivo do leitor do `.bar`.
  RecursoDoBar Recurso(const std::string& ficheiro, std::uint16_t id, std::uint16_t tipo) const;
  const std::map<std::string, std::string>& FalhasAoAbrir() const { return falha_ao_abrir_; }
  std::size_t FicheirosAbertos() const { return abertos_.size(); }

  // As alocacoes vivas: endereco -> bytes. Existe para o teste poder afirmar que
  // o `Libertar` esvaziou o que o `Atender` encheu.
  const std::map<std::uint32_t, std::uint32_t>& Alocacoes() const { return alocados_; }
  std::size_t BytesNasAlocacoes() const;

 private:
  const ArquivoBar* Abrir(const std::string& nome, std::string* motivo);
  void Recusar(const char* nome_da_api, const std::string& motivo, ResultadoDoRecurso* r);

  Memoria& mem_;
  Alocador& al_;
  LeitorDeRecursos leitor_;
  Traco* traco_ = nullptr;

  std::map<std::string, ArquivoBar> abertos_;
  std::map<std::string, std::string> falha_ao_abrir_;
  std::map<std::uint32_t, std::uint32_t> alocados_;
  std::map<std::string, std::uint64_t> recusas_;
  Medicao medicao_;
};

}  // namespace zb2::brew

#endif  // ZB2_CORE_BREW_RECURSOS_H
