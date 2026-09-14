#include "core/brew/despacho.h"

#include <cstdio>
#include <cstring>
#include <set>

#include "core/audio/misturador.h"
#include "core/brew/ajudantes_extra.h"
#include "core/brew/formato.h"
#include "core/brew/imedia.h"

namespace zb2::brew {

namespace {

// Os IIDs que o corpus MEDIU como pedidos.
//
// OS VALORES VEM DO CABECALHO, e nao da memoria: `AEECLSID_DISPLAY` e
// `AEECLSID_CORE+1` = 0x01001001, e `AEECLSID_FILEMGR` = 0x01001003
// (`platform/system/inc/AEEClassIDs.h`).
//
// **Na migracao eu escrevi um valor errado para o Display, de memoria, e a bateria caiu
// de 22 applets para 1.** Os que faltam ficaram com objecto generico, e a
// diferenca so apareceu no numero final. Dai o teste que compara estes valores com
// a medicao estar em `tests/brew_test.cpp`.
constexpr std::uint32_t kIidDisplay = 0x01001001u;
constexpr std::uint32_t kIidFileMgr = 0x01001003u;
constexpr std::uint32_t kIidHeap = 0x01001002u;
constexpr std::uint32_t kIidFile = 0x01001014u;
constexpr std::uint32_t kIidSound = 0x01001056u;
constexpr std::uint32_t kIidGraphics = 0x01002001u;
// `kIidRootForm` deixou de estar aqui: este ficheiro tinha uma copia local do
// mesmo numero que `core/brew/widget.h` declara, e **duas copias de um numero
// medido sao duas chances de ele divergir** -- foi assim que o `kAeeUnsupported`
// desta arvore passou a valer um codigo que nao existe em cabecalho nenhum. O
// valor passou a vir de um sitio so.
constexpr std::uint32_t kIidHid = 0x0106c411u;
constexpr std::uint32_t kIidSqlMgr = 0x0102c4e8u;
// As constantes que o despacho usa, todas derivadas dos cabecalhos gerados.
//
// OS CODIGOS DE ERRO JA NAO ESTAO AQUI. Estavam, com `kAeeUnsupported` a valer
// `0xE0000001` -- um valor que nao existe em cabecalho nenhum -- e a copia local
// ESCONDIA o enum de `ajudantes.h` (onde o `AEE_EUNSUPPORTED` e 20,
// AEEStdErr.h:36). Tirei a copia: agora o nome resolve para o enum, e ha um
// numero medido num sitio so.
constexpr std::uint32_t kSentinela = 0xFFFFFFF0u;
// A base do modulo. MEDIDA: ver `tests/mod_base_test.cpp` e o `bateria.cpp`.
constexpr std::uint32_t kBase = 0x00000000u;
constexpr int kOrcamentoSegundos = 25;
// Quanto a MIDIA anda por milissegundo do relogio virtual. O valor e o do modulo
// de midia (`Media::kAmostrasPorMs`, 22 = 22050/1000 truncado); escreve-se por
// extenso aqui para a constante do motor e a do modulo nao poderem divergir sem
// alguem ler isto.
constexpr std::uint32_t kAmostrasDeMidiaPorMs = zb2::brew::Media::kAmostrasPorMs;
constexpr std::uint32_t kSlotIdStrlen = 1503, kSlotIdMemset = 1504, kSlotIdStrcpy = 1505;
constexpr std::uint32_t kSlotIdMemmove = 1506, kSlotIdStrcmp = 1507, kSlotIdStrchr = 1508;
constexpr std::uint32_t kSlotIdStrtowstr = 1500, kSlotIdGetAeeVersion = 1501,
                       kSlotIdAeeGetRand = 1502;
constexpr std::uint32_t kSlotIdGetUpTime = 1540, kSlotIdQueryClass = 1541,
                       kSlotIdGetAppInstance = 1543;
constexpr std::uint32_t kSlotIdFmTest = 1510, kSlotIdFmFree = 1511, kSlotIdFmLastErr = 1512;
constexpr std::uint32_t kSlotIdSetTimer = 1520;
constexpr std::uint32_t kSlotIdGetFontMetrics = 1530, kSlotIdMeasureText = 1531,
                       kSlotIdDrawText = 1532, kSlotIdDrawRect = 1533, kSlotIdBitBlt = 1534,
                       kSlotIdSetColor = 1535, kSlotIdSetClipRect = 1536, kSlotIdUpdate = 1537,
                       kSlotIdCreateDIBitmap = 1538, kSlotIdBacklight = 1542;
constexpr std::uint32_t kSlotIdGetDest = 1545,
                       kSlotIdSetDest = 1546, kSlotIdRmDir = 1547, kSlotIdGetDeviceInfo = 1549,
                       kSlotIdGetDeviceBitmap = 1550, kSlotIdGetClipRect = 1551,
                       kSlotIdCancelTimer = 1552, kSlotIdSqlOpen = 1553, kSlotIdOpenFile = 1554,
                       kSlotIdFileRead = 1555, kSlotIdFileSeek = 1556, kSlotIdFileInfo = 1557,
                       kSlotIdFileRelease = 1558, kSlotIdFileWrite = 1559;
constexpr std::uint32_t kSlotIdSprintf = 1560, kSlotIdVsprintf = 1561, kSlotIdHeapLock = 1562,
                       kSlotIdFreeResData = 1563, kSlotIdCheckPriv = 1564;
constexpr std::uint32_t kBaseDoSlot = 1000;

// Uma linha da tabela de ajudantes: o offset no `AEEHelperFuncs` e o endereco de
// saida da implementacao.
// OS OFFSETS DESTA TABELA VEM DO CABECALHO, e nao da mao.
//
// DOIS DELES ESTAVAM NO SITIO ERRADO, e nada acusou: `kSlotStrtowstr` valia
// 0x0a0 -- que e o `wstrcompress`; o `strtowstr` verdadeiro e 0x040 -- e
// `kSlotAeeGetRand` valia 0x090, que e o `atoi`; o `aee_GetRand` e 0x0a8. O
// despacho servia um `strtowstr` no sitio do `wstrcompress` e bytes aleatorios
// no sitio do `atoi`. Os numeros certos JA estavam escritos em
// `tools/bateria.cpp` -- a SEGUNDA copia deles divergiu desta, e nao havia nada
// a comparar as duas. **Duas copias de um numero medido sao duas chances de ele
// divergir.** (Medido em `platform/system/inc/AEEStdLib.h`, campos 10, 16, 36 e
// 42; ver `tools/ajudantes_slots.inc`, gerado.)
constexpr std::uint32_t kSlotDbgPrintf = brew_ajudantes::kAjudante_dbgprintf;
constexpr std::uint32_t kSlotStrlen = brew_ajudantes::kAjudante_strlen;
constexpr std::uint32_t kSlotMemset = brew_ajudantes::kAjudante_memset;
constexpr std::uint32_t kSlotStrcpy = brew_ajudantes::kAjudante_strcpy;
constexpr std::uint32_t kSlotStrcmp = brew_ajudantes::kAjudante_strcmp;
constexpr std::uint32_t kSlotStrchr = brew_ajudantes::kAjudante_strchr;
constexpr std::uint32_t kSlotMemmove = brew_ajudantes::kAjudante_memmove;
constexpr std::uint32_t kSlotStrtowstr = brew_ajudantes::kAjudante_strtowstr;
constexpr std::uint32_t kSlotGetAeeVersion = brew_ajudantes::kAjudante_GetAEEVersion;
constexpr std::uint32_t kSlotAeeGetRand = brew_ajudantes::kAjudante_aee_GetRand;
static_assert(kSlotStrtowstr == 0x040, "0x040 e strtowstr; 0x0a0 e wstrcompress");
static_assert(kSlotAeeGetRand == 0x0a8, "0x0a8 e aee_GetRand; 0x090 e atoi");

struct LigacaoAjudante {
  std::uint32_t off;
  std::uint32_t saida;
};
}  // namespace

Despacho::Despacho(Memoria& mem, Traco& traco, Alocador& alocador, Vfs& vfs)
    : mem_(mem),
      traco_(traco),
      al_(alocador),
      vfs_(vfs),
      arquivos_(&vfs),
      sinais_(mem, traco),
      ihid_(mem, traco, sinais_, entrada_),
      widgets_(mem, traco) {}

bool Despacho::InstalarWidgets(const Saidas& saidas) {
  // IDEMPOTENTE: quem dirige o titulo pode chamar isto, e o `InstalarAjudantes`
  // ja o chamou. Uma segunda chamada nao e um erro -- mas tambem nao pode
  // REGISTAR uma falta, que seria um erro inventado pelo instrumento.
  if (widgets_prontos_) return true;
  std::string motivo;
  if (!widgets_.Construir(saidas, &motivo)) {
    // RECUSA RUIDOSA (P2): o `IRootForm` continua a existir, mas os slots dele
    // recusam com nome -- e nao devolvem sucesso sem fazer nada.
    traco_.RegistarFalta(Area::Brew, "os widgets nao foram construidos", motivo);
    return false;
  }
  widgets_prontos_ = true;
  return true;
}

bool Despacho::AtenderWidgets(ICpu& cpu, std::uint32_t indice) {
  if (!widgets_prontos_) return false;
  if (!widgets_.EMeu(indice)) return false;
  // Os DOIS resultados contam como "atendido": `Feito` nao regista nada, e
  // `NaoImplementado` ja registou o nome do que falta. O que NAO pode acontecer
  // e cair no ramo generico e ser nomeado outra vez -- o registo diria
  // `IRootForm::slot3` quando o que faltou foi, por exemplo, o `Draw` de um
  // widget.
  (void)widgets_.Atender(cpu, indice);
  return true;
}

namespace {
// Le uma cadeia do guest, com limite. Sem limite, um ponteiro errado percorre o
// espaco todo antes de parar.
std::string LerTextoDe(const Memoria& mem, std::uint32_t p, std::size_t maximo) {
  std::string s;
  if (p == 0) return s;
  for (std::size_t k = 0; k < maximo; ++k) {
    const char ch = static_cast<char>(mem.Ler8(p + static_cast<std::uint32_t>(k)));
    if (ch == 0) break;
    s.push_back(ch);
  }
  return s;
}
// O identificador do ficheiro a partir do endereco do objecto: o objecto de
// indice `n` vive em `kObjFileBase + n*0x40`.
std::uint32_t IdentificadorDeFicheiro(std::uint32_t obj) {
  return (obj >= kObjFileBase) ? (obj - kObjFileBase) / 0x40 : 0;
}
}  // namespace

bool Despacho::InstalarEntrada(const Saidas& saidas, std::uint32_t base) {
  if (entrada_pronta_) {
    traco_.RegistarFalta(Area::Entrada, "InstalarEntrada", "a entrada ja estava instalada");
    return false;
  }

  // O GUIAO DA ENTRADA, e de onde ele vem.
  //
  // `ZB2_ENTRADA` e o TEXTO do guiao, com `;` a separar os eventos (o formato esta
  // em `core/brew/ihid_entrada.h`). Ler o guiao do AMBIENTE e aceitavel dentro do
  // P4 -- e CONFIGURACAO lida UMA vez no arranque, e nao tempo nem aleatoriedade
  // lidos a cada passo: a mesma linha de comando com a mesma variavel da a mesma
  // corrida. O que continua proibido (e nao existe) e ler o teclado do hospedeiro
  // ou o relogio do hospedeiro.
  //
  // Sem a variavel o controle fica em REPOUSO -- quatro eixos no centro medido
  // (128) e nenhum botao premido.
  if (const char* guiao = std::getenv("ZB2_ENTRADA")) {
    if (*guiao != '\0') {
      std::string texto(guiao);
      for (char& c : texto) {
        if (c == ';') c = '\n';
      }
      std::string motivo;
      if (!EntradaDoZeebo::Ler(texto, &entrada_, &motivo)) {
        // RECUSA RUIDOSA: um guiao invalido NAO e aplicado pela metade.
        traco_.RegistarFalta(Area::Entrada, "ZB2_ENTRADA", motivo);
        return false;
      }
    }
  }

  if (!sinais_.Construir(saidas, base)) return false;
  if (!ihid_.Construir(saidas, base + Sinais::kSlotsNecessarios)) return false;
  base_da_entrada_ = base;
  entrada_pronta_ = true;
  traco_.Emitir(Area::Entrada, Nivel::Informacao, "ENTRADA_INSTALADA",
                "base=" + std::to_string(base) + " eventos_do_guiao=" +
                    std::to_string(entrada_.Quantos()) + " ihid=0x" +
                    std::to_string(ihid_.EnderecoDoIhid()) + " fabrica=0x" +
                    std::to_string(sinais_.EnderecoDaFabrica()));
  return true;
}

bool Despacho::AtenderEntrada(ICpu& cpu, std::uint32_t indice) {
  if (!entrada_pronta_) return false;
  return sinais_.Atender(cpu, indice) || ihid_.Atender(cpu, indice);
}

void Despacho::InstalarAjudantes(const Saidas& saidas, Endereco tabela) {
  // A FAIXA DE SAIDA DO IMEDIA, e a vtable dele: escrita UMA vez, aqui, antes
  // do primeiro `Criar`. O `Media` guarda uma COPIA da faixa, logo ela tem de
  // estar configurada neste momento -- o `Instalar` recusa se nao estiver.
  media_ = std::make_unique<Media>(mem_, traco_, saidas, misturador_, &vfs_);
  const auto instalacao = media_->Instalar();
  if (!instalacao.ok) {
    traco_.Emitir(Area::Audio, Nivel::Erro, "IMEDIA_NAO_INSTALADO", instalacao.motivo);
  }
  // A TABELA UNICA: offset no `AEEHelperFuncs` x endereco de saida da
  // implementacao.
  //
  // E uma so lista de proposito. Havia duas -- as escritas individuais e uma
  // lista de "quem ja tem implementacao" que o laco de preenchimento consultava
  // -- e eu esqueci-me de acrescentar a segunda UMA vez. O sintoma foi identico
  // ao do erro que essa lista existia para evitar: a bateria a dizer "falta
  // aee_GetUpTimeMS" com o `aee_GetUpTimeMS` escrito e a funcionar.
  //
  // **Duas listas que tem de concordar sao zero listas.** Com uma so, e
  // impossivel acrescentar uma implementacao sem que o laco a respeite.
    const LigacaoAjudante kLigados[] = {
      {0x68, 0},  // malloc -- tratado a parte, pelo alocador
      {0x6c, 1},  // free
      {kSlotDbgPrintf, kBaseDoSlot + 500},
      {kSlotStrlen, kSlotIdStrlen},
      {kSlotMemset, kSlotIdMemset},
      {kSlotStrcpy, kSlotIdStrcpy},
      {kSlotStrcmp, kSlotIdStrcmp},
      {kSlotStrchr, kSlotIdStrchr},
      {kSlotMemmove, kSlotIdMemmove},
      {kSlotStrtowstr, kSlotIdStrtowstr},
      {kSlotGetAeeVersion, kSlotIdGetAeeVersion},
      {kSlotAeeGetRand, kSlotIdAeeGetRand},
      // `aee_GetUpTimeMS`: o relogio do sistema, pedido por 11 titulos. Devolve
      // o tempo VIRTUAL, e nao o do sistema -- e o que mantem o determinismo.
      {0x0b0, kSlotIdGetUpTime},   // aee_GetUpTimeMS (derivado da struct, 0x0b0)
      {0x0c0, kSlotIdGetAppInstance},
      {0x020, kSlotIdSprintf},
      {0x13c, kSlotIdVsprintf},
  };
  for (const auto& lig : kLigados) {
    mem_.Escrever32(tabela + lig.off, saidas.Endereco(lig.saida));
  }
  // O WIDGET, no fim da instalacao dos ajudantes. Fica AQUI -- e nao num sitio
  // que a bateria tenha de chamar -- porque esta frente nao pode obrigar a mudar
  // a ferramenta: `tools/bateria.cpp` e partilhado. O `InstalarWidgets` e
  // idempotente, logo quem o quiser chamar explicitamente tambem pode.
  (void)InstalarWidgets(saidas);

  const auto ja_tem = [&](std::uint32_t off) {
    for (const auto& lig : kLigados) {
      if (lig.off == off) return true;
    }
    return false;
  };
  for (std::uint32_t off = 0; off < 117 * 4; off += 4) {
    if (off == 0x68 || off == 0x6c || ja_tem(off)) continue;
    // UM endereco de saida POR OFFSET, e nao um stub generico para todos.
    //
    // MOTIVO, medido: com um stub so, 44 titulos pediam algo e o registo dizia
    // "slot_de_saida_2" 44 vezes -- um numero sem nome. Com um endereco por
    // offset, a bateria diz QUAL funcao do sistema cada titulo pediu, e a lista
    // do que falta passa a ser ordenada por demanda em vez de por intuicao. E o
    // mesmo metodo que nomeou os slots de GL na arvore antiga.
    mem_.Escrever32(tabela + off, saidas.Endereco(kBaseDoSlot + off / 4));
  }
}

bool Despacho::PrepararCallbackDoTemporizador(ICpu& cpu) {
  if (!timer_.ativo || timer_.pfn == 0) return false;
  // O PAR `(funcao, contexto)` VEM DO PROPRIO `SetTimer`, e nao de um
  // `AEECallback` lido da memoria: o cabecalho passa-os em dois argumentos.
  const std::uint32_t fn = timer_.pfn;
  const std::uint32_t ctx = timer_.puser;
  timer_.ativo = false;
  // A FAIXA DO MODULO VEM DE FORA (`DefinirFaixaDoModulo`), porque o TAMANHO e do
  // titulo que esta carregado. Base zero e o valor medido; sem tamanho definido,
  // nenhum callback e aceite -- que e a resposta certa para "nao sei onde esta o
  // codigo do titulo".
  // O TAMANHO vem do `Sinais`, que e quem o guarda (`DefinirFaixaDoModulo`): dois
  // sitios a guardar a mesma faixa seriam dois sitios a divergir.
  if (fn < sinais_.BaseDoModulo() || fn >= sinais_.BaseDoModulo() + sinais_.TamanhoDoModulo()) {
    traco_.RegistarFalta(Area::Guarda, "callback_de_temporizador",
                         "funcao 0x" + std::to_string(fn) + " fora do modulo");
    return false;
  }
  cpu.Set(kLR, kSentinela);       // o retorno do callback volta para ca
  cpu.Set(kPC, fn);
  cpu.Set(kR0, ctx);
  return true;
}

ResultadoFase Despacho::Correr(ICpu& cpu, std::uint64_t limite, std::uint32_t pp_saida) {
  ResultadoFase resultado;
  std::uint32_t saidas = 0;
  bool continuar_no_laco = false;
  while (resultado.passos < limite) {
    // O ORCAMENTO DE TEMPO, verificado a cada 65536 passos.
    //
    // A cada passo seria caro; a cada 65536 o erro maximo e de um bloco, e o
    // custo e nulo. O motivo fica REGISTADO: um titulo que bate no orcamento nao
    // e um titulo que falhou -- e um titulo que ainda estava a andar, e isso
    // muda o que se conclui dele.
    // O ORCAMENTO DE FASE PASSA A SER EM PASSOS, e nao em relogio do hospedeiro.
    //
    // Era `std::chrono::steady_clock`, e isso e uma violacao do P4
    // (determinismo por construcao): o emulador passava a medir a CARGA DA
    // MAQUINA. Ficou LATENTE enquanto o limite de passos dominou -- os 36 titulos
    // com `orcamento_esgotado` param exactamente em 4000000 passos, nenhum pelo
    // relogio. Mas no dia em que um titulo ficasse mais lento por passo, a
    // bateria acusaria regressoes de JOGABILIDADE que eram regressoes de
    // CARGA -- e o `tools/comparar` (etapa 9) acreditaria nelas.
    //
    // Achado pelo sub-agente `regressoes`, na revisao da etapa 9. **O agente que
    // constroi o juiz foi quem viu que o juiz ia julgar a coisa errada.**
    const std::uint32_t pc = cpu.Get(kPC);
    if (pc == kSentinela) {
      // A sentinela tem dois significados: o retorno da chamada de entrada, ou
      // o retorno de um callback de temporizador. Distinguir os dois e o que
      // permite o laco de eventos -- sem isto, o primeiro callback do jogo
      // seria lido como "o modulo retornou".
      if (continuar_no_laco) { continuar_no_laco = false; continue; }
      resultado.motivo = "retornou";
      return resultado;
    }
    std::uint32_t idx = 0;
    if (cpu.GetSaidas().Contem(pc, &idx)) {
      const std::uint32_t lr = cpu.Get(kLR);
      const std::uint32_t r0 = cpu.Get(kR0);
      if (idx == 0) {
        cpu.Set(kR0, al_.Malloc(r0));
      } else if (idx == 1) {
        al_.Free(r0);
        cpu.Set(kR0, kAeeSuccess);
      } else if (idx == 3) {
        // IShell::AddRef -- devolve a contagem de referencias, que e o que a
        // interface do SDK promete.
        const std::uint32_t n = mem_.Ler32(r0 + 4) + 1;
        mem_.Escrever32(r0 + 4, n);
        cpu.Set(kR0, n);
      } else if (idx == 4) {
        // IShell::Release
        const std::uint32_t n = mem_.Ler32(r0 + 4);
        if (n > 0) mem_.Escrever32(r0 + 4, n - 1);
        cpu.Set(kR0, n > 0 ? n - 1 : 0);
      } else if (AtenderEntrada(cpu, idx)) {
        // A ENTRADA (etapa 8): IHID, IHIDDevice e os sinais do BREW.
        //
        // ESTE RAMO VEM ANTES DOS OUTROS, e nao por gosto: os indices desta faixa
        // sao 20000+, e o ramo `idx >= kBaseDoShell` (2000) mais abaixo apanhava-os
        // e dava-lhes o NOME de um metodo do IShell. Foi por um nome errado num
        // ramo generico que o `SetTimer` ja se perdeu uma vez nesta arvore.
      } else if (idx == kBaseDoShell + 2) {
        // IShell::CreateInstance(po, ClsId, ppobj) -- IShell slot 2.
        //
        // NAO e `QueryInterface`: o `INHERIT_IBase` deste SDK tem DOIS membros
        // (`AddRef`, `Release`) e nao ha `QueryInterface` nele; o slot 2 do IShell
        // e o `CreateInstance` (`AEEIShell.h`). A semantica do codigo ja era essa
        // (r1 = ClsId, r2 = &ppobj) -- **era o NOME que estava errado**, e este
        // comentario andou a mentir durante varias rondas. Apontado por dois
        // sub-agentes independentes (`imedia` e `hid-entrada`).
        //
        // Os dois IIDs que o corpus pede
        // sao conhecidos por medicao; o que nao for conhecido devolve
        // ECLASSNOTSUPPORT com o ponteiro a zero -- recusar, nao mentir.
        const std::uint32_t iid = cpu.Get(kR1);
        const std::uint32_t ppo = cpu.Get(kR2);
        std::uint32_t devolver = 0;
        if (media_ && zb2::brew::ClasseDeMidia(iid)) {
          // A FAMILIA AEECLSID_MULTIMEDIA (0x01005500): o objecto de midia, a
          // tabela do IMedia e o ciclo de vida vivem em core/brew/imedia.
          if (ppo != 0) mem_.Escrever32(ppo, 0);
          cpu.Set(kR0, static_cast<std::uint32_t>(media_->Criar(iid, ppo)));
          cpu.Set(kPC, lr);
          continue;
        }
        if (iid == kIidDisplay) devolver = zb2::brew::kObjDisplay;
        else if (iid == kIidFileMgr) devolver = zb2::brew::kObjFileMgr;
        // A ENTRADA. O `AEECLSID_HID` deixou de ser um objecto GENERICO: existe um
        // IHID a serio. E a fabrica de sinais tambem (0x01041207, pedida por 37
        // dos 62 titulos -- medido), porque sem ela o jogo nao tem como pedir o
        // par (funcao, contexto) que o `RegisterForPositionChange` recebe.
        else if (entrada_pronta_ && iid == kIidHid) devolver = ihid_.EnderecoDoIhid();
        else if (entrada_pronta_ && iid == kClsidSignalCBFactory) {
          devolver = sinais_.EnderecoDaFabrica();
        }
        // Os que tem objecto generico: o jogo fica com uma interface cujos
        // metodos recusam, e a bateria aprende quais sao.
        else {
          for (std::uint32_t k = 0; k < kNGenericos; ++k) {
            if (iid == kGenericos[k].iid) devolver = zb2::brew::ObjGenerico(k);
          }
        }
        if (ppo != 0) mem_.Escrever32(ppo, devolver);
        cpu.Set(kR0, devolver != 0 ? kAeeSuccess : kAeeClassNotSupported);
        if (devolver == 0) {
          char det[96];
          std::snprintf(det, sizeof(det), "iid=0x%08x ppo=0x%08x", iid, ppo);
          traco_.RegistarFalta(Area::Brew, "IShell::CreateInstance CLSID desconhecido", det);
        }
      } else if (media_ && media_->Atender(idx, cpu)) {
        // O IMedia (core/brew/imedia) atendeu este indice de saida.
      } else if (AtenderWidgets(cpu, idx)) {
        // O WIDGET: `IRootForm`, `IForm`, `IHandler` e `IWidget`.
        //
        // ESTE RAMO VEM ANTES DO `idx >= kBaseDoShell`, e a razao e a classe de
        // erro que ja apareceu duas vezes nesta arvore: os indices da raiz do
        // `IRootForm` CAEM DENTRO da faixa generica (9000 + 4*64), e o ramo
        // generico dava-lhes o nome certo mas o comportamento errado -- recusava
        // e dizia `<interface>::slotN`. Com o ramo do widget a frente, o pedido
        // e ATENDIDO e o nome so aparece quando o que faltou e mesmo um metodo
        // que nao existe.
      } else if (idx >= kBaseDoShell) {
        // O NOME tem de dizer de QUE interface e o slot. Um so "IShell::slot"
        // para tudo dava `IShell::slot4004` para um metodo do IDisplay -- numero
        // sem nome outra vez, e ja foi esse o defeito que me fez perder uma
        // ronda inteira a olhar para a lista errada.
        char nome[64], det[128];
        const char* iface = "IShell";
        std::uint32_t slot = 0;
        // Os objectos GENERICOS tem vtable propria por interface: sem isto o
        // `slot 7` de um `ISound` aparecia nomeado como `IFileMgr`, e a lista de
        // demanda mentia sobre o que os titulos pedem.
        const std::uint32_t kgen = (idx >= zb2::brew::kVtableGenericoBase)
                                       ? (idx - zb2::brew::kVtableGenericoBase) / 64u
                                       : kNGenericos;
        if (kgen < kNGenericos) {
          iface = kGenericos[kgen].nome;
          slot = idx - zb2::brew::VtGenerico(kgen);
        } else if (idx >= zb2::brew::kVtableFileMgr) { iface = "IFileMgr"; slot = idx - zb2::brew::kVtableFileMgr; }
        else if (idx >= zb2::brew::kVtableDisplay) { iface = "IDisplay"; slot = idx - zb2::brew::kVtableDisplay; }
        else { iface = "IShell"; slot = idx - kBaseDoShell; }
        std::snprintf(nome, sizeof(nome), "%s::slot%u", iface, slot);
        // Os ARGUMENTOS no detalhe: para o CreateInstance (slot 2) o r1 e o ClsId
        // pedido, e sem ele nao se sabe o que responder. Foi assim que se
        // percebeu, na arvore antiga, quais das interfaces eram as mesmas por
        // dois nomes diferentes.
        // O LR entra no detalhe porque sem ele nao se sabe QUEM chama.
        //
        // Foi a falta dele que me deixou a olhar para um `IDisplay::slot2` com uma
        // FONTE (`AEE_FONT_NORMAL = 0x8000`) no r1 -- argumento que nenhum metodo
        // daquele slot aceita -- sem forma de saber de onde vinha a chamada. Com o
        // LR, vai-se ao sitio e le-se a instrucao.
        // r3 E OS ARGUMENTOS NA PILHA entram tambem: ha metodos com SEIS
        // argumentos (`LoadResDataEx`, `MeasureTextEx`), e sem eles nao se sabe
        // o que o pedido quer -- so se sabe que existe.
        const std::uint32_t sp = cpu.Get(kSP);
        // SE O r1 FOR UM PONTEIRO PARA TEXTO, LE-SE O TEXTO.
        //
        // Um nome de ficheiro de recurso diz mais do que o numero do ponteiro --
        // e foi assim que se descobriu que o `pacmania` pede um recurso de um
        // ficheiro concreto. Sem isto ficava-se a olhar para 0x80202a70.
        char txt[48] = {0};
        const std::uint32_t possivel = cpu.Get(kR1);
        if (possivel >= 0x00100000u && possivel < 0x81000000u) {
          bool imprimivel = true;
          for (int k = 0; k < 40; ++k) {
            const std::uint8_t ch = mem_.Ler8(possivel + static_cast<std::uint32_t>(k));
            if (ch == 0) break;
            if (ch < 0x20 || ch > 0x7e) { imprimivel = false; break; }
            txt[k] = static_cast<char>(ch);
          }
          if (!imprimivel) txt[0] = 0;
        }
        std::snprintf(det, sizeof(det),
                      "r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x sp0=0x%08x sp1=0x%08x lr=0x%08x txt=%s",
                      r0, cpu.Get(kR1), cpu.Get(kR2), cpu.Get(kR3), mem_.Ler32(sp),
                      mem_.Ler32(sp + 4), cpu.Get(kLR), txt);
        traco_.RegistarFalta(Area::Brew, nome, det);
        cpu.Set(kR0, kAeeUnsupported);
        if (++saidas > 200) { resultado.motivo = "parou_em_slot_nao_implementado"; return resultado; }
      } else if (idx == kSlotIdStrlen) {
        // size_t strlen(const char *s) -- conta ate ao NUL, sem limite
        // artificial: a memoria do guest responde zero onde nao ha nada.
        std::uint32_t n = 0;
        while (mem_.Ler8(r0 + n) != 0) ++n;
        cpu.Set(kR0, n);
      } else if (idx == kSlotIdMemset) {
        // void *memset(void *d, int c, size_t n) -- devolve o destino
        const std::uint32_t n = cpu.Get(kR2);
        for (std::uint32_t i = 0; i < n; ++i) mem_.Escrever8(r0 + i, static_cast<std::uint8_t>(cpu.Get(kR1)));
        cpu.Set(kR0, r0);
      } else if (idx == kSlotIdStrcpy) {
        const std::uint32_t src = cpu.Get(kR1);
        std::uint32_t i = 0;
        for (;;) {
          const std::uint8_t b = mem_.Ler8(src + i);
          mem_.Escrever8(r0 + i, b);
          if (b == 0) break;
          ++i;
        }
        cpu.Set(kR0, r0);
      } else if (idx == kSlotIdStrcmp) {
        const std::uint32_t a2 = r0, b2 = cpu.Get(kR1);
        std::uint32_t i = 0;
        for (;;) {
          const std::uint8_t ca = mem_.Ler8(a2 + i), cb2 = mem_.Ler8(b2 + i);
          if (ca != cb2 || ca == 0 || cb2 == 0) {
            cpu.Set(kR0, static_cast<std::uint32_t>(static_cast<std::int32_t>(ca) -
                                                    static_cast<std::int32_t>(cb2)));
            break;
          }
          ++i;
        }
      } else if (idx == kSlotIdStrchr) {
        const std::uint8_t c2 = static_cast<std::uint8_t>(cpu.Get(kR1));
        std::uint32_t i = 0, achou = 0;
        for (;;) {
          const std::uint8_t b = mem_.Ler8(r0 + i);
          if (b == c2) { achou = r0 + i; break; }
          if (b == 0) break;
          ++i;
        }
        cpu.Set(kR0, achou);
      } else if (idx == kSlotIdMemmove) {
        const std::uint32_t src = cpu.Get(kR1), n = cpu.Get(kR2);
        std::vector<std::uint8_t> copia(n);   // copia intermediaria: o C permite sobreposicao
        mem_.LerBloco(src, copia.data(), n);
        mem_.EscreverBloco(r0, copia.data(), n);
        cpu.Set(kR0, r0);
      } else if (idx == kSlotIdStrtowstr) {
        // AECHAR *strtowstr(const char *pszIn, AECHAR *pDest, int nSize).
        // AECHAR e UTF-16; nSize e em CARACTERES, e a funcao termina o destino.
        const std::uint32_t destino = cpu.Get(kR1);
        const std::uint32_t tam = cpu.Get(kR2);
        std::uint32_t i = 0;
        for (; static_cast<int>(i) + 1 < static_cast<int>(tam); ++i) {
          const std::uint8_t c2 = mem_.Ler8(r0 + i);
          mem_.Escrever16(destino + i * 2, c2);
          if (c2 == 0) break;
        }
        if (static_cast<int>(i) + 1 >= static_cast<int>(tam) && destino != 0) {
          mem_.Escrever16(destino + (tam - 1) * 2, 0);
        }
        cpu.Set(kR0, destino);
      } else if (idx == kSlotIdGetAeeVersion) {
        // Devolve a versao, e escreve-a em *pVer quando ha ponteiro. 4.0.2
        // codificada como o SDK a codifica: AEE_VER(4,0,2).
        const std::uint32_t pver = cpu.Get(kR1);
        const std::uint32_t ver = 0x00400002u;
        if (pver != 0) mem_.Escrever32(pver, ver);
        cpu.Set(kR0, ver);
      } else if (idx == kSlotIdAeeGetRand) {
        // `aee_GetRand` -- gerador DETERMINISTA (principio P4). Um gerador do
        // sistema tornaria duas corridas diferentes, e o emulador deixaria de
        // ser reproduzivel -- que e o que sustenta todas as medicoes.
        static std::uint32_t semente = 0x12345678u;
        semente = semente * 1103515245u + 12345u;
        cpu.Set(kR0, (semente >> 16) & 0x7FFFu);
      } else if (idx == kSlotIdSetColor) {
        // `void SetColor(IDisplay *po, RGBVAL rgb)`. O Zeebo usa RGB565.
        tela_.CorAtual(cpu.Get(kR1));
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdSetClipRect) {
        // `void SetClipRect(IDisplay *po, AEERect *prc)` -- prc nulo limpa o clip.
        const std::uint32_t prc = cpu.Get(kR1);
        if (prc == 0) {
          tela_.ClipLimpo();
        } else {
          tela_.Clip(mem_.Ler32(prc), mem_.Ler32(prc + 4), mem_.Ler32(prc + 8),
                      mem_.Ler32(prc + 12));
        }
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdDrawRect) {
        // `void DrawRect(IDisplay *po, const AEERect *pRect, RGBVAL clrFrame,
        //                RGBVAL clrFill, uint32 dwFlags)`.
        //
        // ASSINATURA CORRIGIDA. Eu tinha escrito a versao do BREW 4.x, em que o
        // r1 era a rect e nao havia cores. Neste SDK o r1 e a RECT, o r2 e a cor
        // do contorno e o r3 a do preenchimento -- e o bit `DW_RECT_DRAW` do
        // dwFlags e que diz se e contorno ou cheio.
        const std::uint32_t prc = cpu.Get(kR1);
        if (prc != 0) {
          const std::uint32_t x = mem_.Ler32(prc), y = mem_.Ler32(prc + 4);
          const std::uint32_t w = mem_.Ler32(prc + 8), h = mem_.Ler32(prc + 12);
          const std::uint32_t clrframe = cpu.Get(kR2), clrfill = cpu.Get(kR3);
          const std::uint32_t flags = mem_.Ler32(cpu.Get(kSP) + 0);
          // Os bits do `AEERectFlags`: DRAW = contorno, FILL = cheio.
          const bool contorno = (flags & 0x01u) != 0, cheio = (flags & 0x02u) != 0;
          if (cheio || contorno) {
            tela_.CorAtual(cheio ? clrfill : clrframe);
            tela_.Retangulo(x, y, w, h, cheio);
          }
        }
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdDrawText) {
        // `int DrawText(IDisplay *po, AEEFont nFont, const AECHAR *pcText,
        //              int nChars, int x, int y, const AEERect *prcBackground,
        //              uint32 dwFlags)`.
        //
        // ASSINATURA CORRIGIDA: os argumentos 5 e 6 sao COORDENADAS, nao uma
        // rect. A minha versao lia o r3 como ponteiro de rect e desenhava a barra
        // no sitio errado -- e um ponteiro de rect interpretado como x daria uma
        // barra numa linha absurda, ou fora do ecra, sem nada a acusar.
        //
        // Nao ha fonte carregada, logo NAO se rasteriza texto: desenha-se uma
        // barra com a cor actual, com a largura declarada de 8 px por caracter.
        // Fica declarado como aproximacao, e a bateria conta `textos`.
        const std::uint32_t nchars = cpu.Get(kR3);
        const std::uint32_t x = mem_.Ler32(cpu.Get(kSP) + 0);
        const std::uint32_t y = mem_.Ler32(cpu.Get(kSP) + 4);
        const std::uint32_t prcfundo = mem_.Ler32(cpu.Get(kSP) + 8);
        if (prcfundo != 0) {
          // O fundo e pedido explicitamente: pinta-se com a cor actual antes.
          tela_.Retangulo(mem_.Ler32(prcfundo), mem_.Ler32(prcfundo + 4),
                           mem_.Ler32(prcfundo + 8), mem_.Ler32(prcfundo + 12), true);
        }
        const std::uint32_t larg = (nchars > 0 ? nchars : 1) * 8;
        for (std::uint32_t i = 0; i < larg; ++i) tela_.Ponto(static_cast<int>(x + i), static_cast<int>(y));
        ++textos_;
        cpu.Set(kR0, static_cast<std::uint32_t>(larg));
      } else if (idx == kSlotIdBitBlt) {
        // `void BitBlt(IDisplay *po, int xDest, int yDest, int cxDest, int cyDest,
        //              const void *pbmSource, int xSrc, int ySrc, AEERasterOp dwRopCode)`.
        //
        // ASSINATURA CORRIGIDA, e a correccao e grande: a origem NAO e um
        // `IBitmap` com cabecalho -- e um bloco CRU de pixels, sem largura nem
        // altura. Quem sabe as dimensoes e quem chamou. A largura da origem vem
        // do proprio passo: assume-se que a origem tem a largura pedida
        // (`cxDest`), que e a convencao do BREW para blits sem escalonamento.
        const std::int32_t xd = static_cast<std::int32_t>(cpu.Get(kR1));
        const std::int32_t yd = static_cast<std::int32_t>(cpu.Get(kR2));
        const std::int32_t cx = static_cast<std::int32_t>(cpu.Get(kR3));
        const std::int32_t cy = static_cast<std::int32_t>(mem_.Ler32(cpu.Get(kSP) + 0));
        const std::uint32_t origem = mem_.Ler32(cpu.Get(kSP) + 4);
        const std::int32_t xs = static_cast<std::int32_t>(mem_.Ler32(cpu.Get(kSP) + 8));
        const std::int32_t ys = static_cast<std::int32_t>(mem_.Ler32(cpu.Get(kSP) + 12));
        if (origem != 0 && cx > 0 && cy > 0) {
          for (std::int32_t j = 0; j < cy; ++j) {
            for (std::int32_t i = 0; i < cx; ++i) {
              const std::uint32_t u = static_cast<std::uint32_t>(xs + i);
              const std::uint32_t v = static_cast<std::uint32_t>(ys + j);
              tela_.CorAtual(mem_.Ler16(origem + (v * static_cast<std::uint32_t>(cx) + u) * 2));
              tela_.Ponto(xd + i, yd + j);
            }
          }
          ++blits_;
        }
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdCreateDIBitmap) {
        // `int CreateDIBitmap(IDisplay *po, IDIB **ppIDIB, uint8 colorDepth,
        //                     uint16 w, uint16 h)`.
        //
        // ASSINATURA CORRIGIDA. O ponteiro de saida e o SEGUNDO argumento, e o
        // que se devolve no r0 e um codigo (0 = SUCCESS). A minha versao
        // devolvia o objecto no r0 e ignorava o `ppIDIB` -- o chamador ficava com
        // o ponteiro por preencher e o objecto perdido.
        const std::uint32_t ppidib = cpu.Get(kR1);
        const std::uint32_t prof = cpu.Get(kR2) & 0xFFu;
        const std::uint32_t w = cpu.Get(kR3) & 0xFFFFu;
        const std::uint32_t h = mem_.Ler32(cpu.Get(kSP) + 0) & 0xFFFFu;
        const std::uint32_t obj = zb2::brew::kObjDibBase + dibs_ * 0x40;
        ++dibs_;
        // O IDIB tem cabecalho proprio: dimensoes, profundidade, e o PASSAPORTE
        // de acesso aos pixels (`pData`), que o `IDIB_GetBuffer` devolve.
        mem_.Escrever32(obj + 0, vtable_bitmap_);
        mem_.Escrever32(obj + 4, 1);
        mem_.Escrever32(obj + 8, 0);  // pData -- por atribuir
        mem_.Escrever32(obj + 12, w);
        mem_.Escrever32(obj + 16, h);
        mem_.Escrever32(obj + 20, prof);
        if (ppidib != 0) mem_.Escrever32(ppidib, obj);
        cpu.Set(kR0, 0);
            } else if (idx == kSlotIdGetFontMetrics) {
        // `int GetFontMetrics(IDisplay *po, AEEFont nFont, int *pnAscent,
        //                     int *pnDescent)`.
        //
        // ASSINATURA CORRIGIDA: o r1 e a FONTE e o r2/r3 sao os dois ponteiros de
        // saida. Eu tinha escrito a versao do BREW 4.x, com uma struct de
        // metricas no r1 -- que aqui seria lido como um `AEEFont` e a escrita
        // ia para o sitio errado. **Este era um defeito silencioso de verdade.**
        const std::uint32_t pascent = cpu.Get(kR2), pdescent = cpu.Get(kR3);
        const int asc = -10, desc = 2;  // fonte de 12 px, valores DECLARADOS
        if (pascent != 0) mem_.Escrever32(pascent, static_cast<std::uint32_t>(asc));
        if (pdescent != 0) mem_.Escrever32(pdescent, static_cast<std::uint32_t>(desc));
        cpu.Set(kR0, 12);
      } else if (idx == kSlotIdMeasureText) {
        // `int MeasureTextEx(IDisplay *po, AEEFont nFont, const AECHAR *pcText,
        //                    int nChars, int nMaxWidth, int *pnFits)`.
        //
        // ASSINATURA CORRIGIDA: `pnFits` e o 6.o argumento, na pilha, e recebe a
        // largura que CABE. Largura DECLARADA de 8 px por caracter.
        const std::uint32_t n = cpu.Get(kR3);
        const std::uint32_t nmax = mem_.Ler32(cpu.Get(kSP) + 0);
        const std::uint32_t pfits = mem_.Ler32(cpu.Get(kSP) + 4);
        const std::uint32_t larg = (n > 0 ? n : 1) * 8;
        if (pfits != 0) {
          mem_.Escrever32(pfits, nmax == 0 ? larg : (larg < nmax ? larg : nmax));
        }
        cpu.Set(kR0, static_cast<std::uint32_t>(larg));
      } else if (idx == kSlotIdUpdate) {
        ++updates_;
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdBacklight) {
        // `void Backlight(IDisplay *po, boolean bOn)`. Sem ecra fisico: conta.
        ++backlights_;
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdSetTimer) {
        // `int SetTimer(IShell *po, int32 dwMsecs, void (*pfn)(void *), void *pUser)`
        //
        // ASSINATURA CORRIGIDA, e a correccao tira o laco de quadro de todos os
        // titulos: o r1 e a DURACAO e o r2 e a FUNCAO. A versao anterior lia
        // `r1` como ponteiro de `AEECallback` e `r2` como milissegundos.
        //
        // A prova sao DUAS fontes independentes, como o projecto exige:
        //   1. o cabecalho, `platform/system/inc/AEEIShell.h:299`
        //      (`INHERIT_IShell`): `int (*SetTimer)(iname *po, int32 dwMsecs,
        //      void (*pfn)(void *), void * pUser)` -- e o inline, linha 403;
        //   2. o codigo do guest, `mod/280214/asq.mod` em `0x8cf34`:
        //      `mov r1,#100` (100 ms) e `ldr r2,[pc,#572]` (= `*0x8d18c` =
        //      `0x8c3e8`, um endereco de CODIGO) com `mov r3,r5` (o contexto).
        //      Um ponteiro de funcao lido de um literal, no r2 -- nao um periodo.
        //
        // CONSEQUENCIA MEDIDA da versao errada: `PrepararCallbackDoTemporizador`
        // fazia `Ler32(100)` e recusava ("fora do modulo"). NENHUM titulo do
        // corpus armava um temporizador, e sem laco de quadro nenhum chega ao
        // codigo que desenha. E o pedido de demanda mais alto do IShell.
        timer_.ativo = true;
        timer_.pfn = cpu.Get(kR2);
        timer_.puser = cpu.Get(kR3);
        timer_.vence_em_ms = agora_ms_ + static_cast<std::int64_t>(cpu.Get(kR1));
        traco_.Emitir(Area::Guarda, Nivel::Depuracao, "SET_TIMER",
                     "pfn=0x" + std::to_string(timer_.pfn) + " puser=0x" +
                         std::to_string(timer_.puser) + " em " +
                         std::to_string(cpu.Get(kR1)) + " ms");
        cpu.Set(kR0, kAeeSuccess);
      } else if (idx == kSlotIdGetUpTime) {
        cpu.Set(kR0, static_cast<std::uint32_t>(agora_ms_));
      } else if (idx == kSlotIdGetDest) {
        // `IBitmap *GetDestination(IDisplay *po)` -- IDisplay slot 16.
        // Devolve o bitmap que esta a receber o desenho. O jogo usa-o para saber
        // o TAMANHO da tela (via IBitmap::GetInfo) antes de calcular posicoes.
        const std::uint32_t obj = zb2::brew::kObjDibBase + 0x300;
        mem_.Escrever32(obj + 0, vtable_bitmap_);
        mem_.Escrever32(obj + 4, 1);
        mem_.Escrever32(obj + 8, 0);
        mem_.Escrever32(obj + 12, zb2::brew::Tela::kLargura);
        mem_.Escrever32(obj + 16, zb2::brew::Tela::kAltura);
        mem_.Escrever32(obj + 20, 16);
        destino_ = obj;
        cpu.Set(kR0, obj);
      } else if (idx == kSlotIdSetDest) {
        // `int SetDestination(IDisplay *po, IBitmap *pDst)` -- IDisplay slot 15.
        // So se ACEITA um bitmap nosso: aceitar um ponteiro qualquer poria o
        // desenho num sitio que nao existe.
        const std::uint32_t pdst = cpu.Get(kR1);
        if (pdst >= zb2::brew::kObjDibBase && pdst < zb2::brew::kObjDibBase + 0x1000) {
          destino_ = pdst;
          cpu.Set(kR0, 0);  // SUCCESS
        } else {
          cpu.Set(kR0, kAeeUnsupported);
        }
      } else if (idx == kSlotIdGetDeviceBitmap) {
        // `int GetDeviceBitmap(IDisplay *po, IBitmap **ppIBitmap)` -- IDisplay
        // slot 16. O jogo quer o bitmap do ECRA para desenhar por cima dele.
        // E o mesmo objecto que o `GetDestination` devolve.
        const std::uint32_t pp = cpu.Get(kR1);
        if (pp != 0) {
          mem_.Escrever32(pp, zb2::brew::kObjDibBase + 0x300);
          cpu.Set(kR0, 0);  // SUCCESS
        } else {
          cpu.Set(kR0, kAeeUnsupported);
        }
      } else if (idx == kSlotIdGetClipRect) {
        // `void GetClipRect(IDisplay *po, AEERect *pRect)` -- IDisplay slot 19.
        // Devolve o clip ACTUAL, que o `SetClipRect` guardou.
        const std::uint32_t prc = cpu.Get(kR1);
        if (prc != 0) {
          const std::uint32_t* c = tela_.ClipAtual();
          mem_.Escrever32(prc + 0, c[0]);
          mem_.Escrever32(prc + 4, c[1]);
          mem_.Escrever32(prc + 8, c[2]);
          mem_.Escrever32(prc + 12, c[3]);
        }
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdCancelTimer) {
        // `int CancelTimer(IShell *po, void (*pfn)(void *), void *pUser)` --
        // IShell slot 12 (`AEEIShell.h:300`, e o inline na linha 408). Os campos
        // veem SEPARADOS aqui tambem, pela mesma razao do `SetTimer`.
        //
        // So se desarma se for o MESMO callback: cancelar um temporizador alheio
        // pararia o laco de quadro de outra coisa.
        if (timer_.ativo && timer_.pfn == cpu.Get(kR1)) timer_.ativo = false;
        cpu.Set(kR0, kAeeSuccess);
      } else if (idx == kSlotIdHeapLock || idx == kSlotIdHeapLock + 0) {
        // `int Lock(IHeap1 *po)` -- IHeap1 slot 7. Bloqueia o heap para uso
        // exclusivo.
        //
        // NAO ha nada a bloquear: o principio P6 do desenho e UM ESCRITOR para a
        // memoria do guest, e nao ha threads de subsistema. Devolver sucesso e a
        // resposta CORRECTA, e nao um stub: o contrato e "a partir daqui es o
        // unico a mexer", e isso ja e verdade.
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdFreeResData) {
        // `void FreeResData(IShell *po, void *pData)` -- IShell slot 20.
        // Liberta o que o `LoadResData` devolveu. Enquanto os recursos nao
        // existirem, nao ha nada para libertar -- e passar um ponteiro alheio ao
        // alocador seria pior do que nao fazer nada.
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdCheckPriv) {
        // `boolean CheckPrivLevel(IShell *po, uint32 dwPriv)` -- IShell slot 39.
        // Responde TRUE aos privilegios de que este emulador precisa: ficheiro,
        // percentagem de memoria, e o nivel de sistema. Recusar faria o jogo
        // desistir de escrever onde tem de escrever.
        cpu.Set(kR0, 1);
      } else if (idx == kSlotIdSprintf || idx == kSlotIdVsprintf) {
        // `int sprintf(char *pBuf, const char *pFmt, ...)` -- AEEHelperFuncs
        // 0x020; `int vsprintf(char*, const char*, va_list)` -- 0x13c.
        //
        // VARARGS no AAPCS: os quatro primeiros argumentos vao em r0..r3 e o
        // resto na PILHA a partir do `sp`. O `vsprintf` recebe um PONTEIRO para os
        // argumentos; o `sprintf` recebe-os soltos. O trabalho esta em `Formato`,
        // que tem testes contra o `snprintf` DO SISTEMA.
        std::uint32_t args[8];
        int n = 0;
        if (idx == kSlotIdVsprintf) {
          const std::uint32_t va = cpu.Get(kR2);
          for (int i = 0; i < 8; ++i) {
            args[n++] = va != 0 ? mem_.Ler32(va + static_cast<std::uint32_t>(i) * 4) : 0;
          }
        } else {
          args[n++] = cpu.Get(kR2);
          args[n++] = cpu.Get(kR3);
          for (int i = 0; i < 6; ++i) {
            args[n++] = mem_.Ler32(cpu.Get(kSP) + static_cast<std::uint32_t>(i) * 4);
          }
        }
        const std::uint32_t pbuf = cpu.Get(kR0);
        if (pbuf == 0 || cpu.Get(kR1) == 0) {
          cpu.Set(kR0, 0);
        } else {
          cpu.Set(kR0, Formatar(mem_, pbuf, cpu.Get(kR1), args, n));
        }
      } else if (idx == kSlotIdOpenFile) {
        // `IFile *OpenFile(IFileMgr *po, const char *pszFile, OpenFileMode mode)`
        // -- IFileMgr slot 2. O trabalho esta em `Arquivos`, que TEM TESTES.
        //
        // Isto era uma reimplementacao propria dentro da ferramenta: a mesma
        // regra escrita duas vezes, e so uma delas testada. E o defeito que a
        // migracao existe para corrigir.
        const std::string nome = LerTextoDe(mem_, cpu.Get(kR1), 512);
        const std::uint32_t id = arquivos_.Abrir(nome, cpu.Get(kR2), dir_ + "/" + pasta_);
        if (id == 0) {
          cpu.Set(kR0, 0);  // NULL -- nao ha IFile
        } else {
          const std::uint32_t obj = kObjFileBase + id * 0x40;
          mem_.Escrever32(obj + 0, vtable_ficheiro_);
          mem_.Escrever32(obj + 4, 1);
          cpu.Set(kR0, obj);
        }
      } else if (idx == kSlotIdFileRelease) {
        arquivos_.Fechar(IdentificadorDeFicheiro(cpu.Get(kR0)));
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdFileRead) {
        // `int32 Read(IFile *po, void *pDest, uint32 nWant)` -- IAStream slot 3.
        const std::int32_t n = arquivos_.Ler(IdentificadorDeFicheiro(cpu.Get(kR0)), mem_,
                                            cpu.Get(kR1), cpu.Get(kR2));
        cpu.Set(kR0, static_cast<std::uint32_t>(n));
      } else if (idx == kSlotIdFileSeek) {
        // `int32 Seek(IFile *po, FileSeekType seek, int32 position)` -- slot 7.
        const std::int32_t r = arquivos_.Posicionar(IdentificadorDeFicheiro(cpu.Get(kR0)),
                                                    cpu.Get(kR1),
                                                    static_cast<std::int32_t>(cpu.Get(kR2)));
        cpu.Set(kR0, static_cast<std::uint32_t>(r));
      } else if (idx == kSlotIdFileInfo) {
        // `int GetInfo(IFile *po, FileInfo *pInfo)` -- slot 6.
        const bool ok = arquivos_.Informacao(IdentificadorDeFicheiro(cpu.Get(kR0)), mem_,
                                             cpu.Get(kR1));
        cpu.Set(kR0, ok ? kAeeSuccess : kAeeUnsupported);
      } else if (idx == kSlotIdFileWrite) {
        // `uint32 Write(IFile*, const void *p, uint32 n)` -- slot 5.
        // A VFS e SO DE LEITURA por DECISAO. Recusa declarada, zero bytes.
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdSqlOpen) {
        // `int Open(ISQLMgr *po, const char *pszFile, ISQL **ppiSQL, uint32 flags)`
        // -- ISQLMgr slot 3.
        //
        // Recusa DECLARADA: nao ha SQLite aqui, e implementar meia base de dados
        // seria a pior especie de mentira -- a que so falha mais tarde, ja dentro
        // do jogo. O `tectoy` e o unico que o pede.
        if (cpu.Get(kR3) != 0) mem_.Escrever32(cpu.Get(kR3), 0);
        cpu.Set(kR0, kAeeUnsupported);
      } else if (idx == kSlotIdGetDeviceInfo) {
        // `void GetDeviceInfo(IShell *po, AEEDeviceInfo *pi)` -- IShell slot 4,
        // e a demanda MAIS ALTA do corpus: 18 titulos.
        //
        // O jogo le daqui o TAMANHO DO ECRA e a profundidade de cor, para calcular
        // posicoes e para decidir que superficies pode criar. Sem isto, 18 titulos
        // pediam-no e nao recebiam nada.
        //
        // 320x240 e 16 bits: os valores do ZEEBO, DECLARADOS como tal.
        const std::uint32_t pi = cpu.Get(kR1);
        if (pi != 0) {
          AeeDeviceInfo di{};
          di.cx_screen = 320; di.cy_screen = 240;
          di.cx_alt_screen = 320; di.cy_alt_screen = 240;
          di.cx_scroll_bar = 10;
          di.w_encoding = 0;          // AEE_ENC_UNICODE
          di.w_menu_text_scroll = 30;
          di.n_color_depth = 16;
          di.unused2 = 0;
          di.w_menu_image_delay = 100;
          di.dw_ram = 0;              // deprecated no cabecalho
          di.b_alt_display = 0; di.b_flip = 0; di.b_vibrator = 0; di.b_ext_speaker = 0;
          di.b_vr = 0; di.b_pos_loc = 0; di.b_midi = 1; di.b_cmx = 0; di.b_pen = 1;
          di.dw_prompt_props = 0;
          di.w_key_close_app = 0; di.w_key_close_all_apps = 0;
          di.dw_lang = 0;             // AEE_LNG_ENGLISH
          di.w_struct_size = static_cast<std::uint16_t>(sizeof(AeeDeviceInfo));
          di.dw_net_linger = 0; di.dw_sleep_defer = 0;
          di.w_max_path = 256;
          di.dw_platform_id = 0;
          const auto* b = reinterpret_cast<const std::uint8_t*>(&di);
          for (std::size_t k = 0; k < sizeof(AeeDeviceInfo); ++k) {
            mem_.Escrever8(pi + static_cast<std::uint32_t>(k), b[k]);
          }
        }
        cpu.Set(kR0, 0);
      } else if (idx == kSlotIdRmDir) {
        // `int RmDir(IFileMgr *po, const char *pszDir)` -- IFileMgr slot 7.
        //
        // A VFS desta etapa e SO DE LEITURA, e e deliberado: um jogo que apague
        // um ficheiro do modulo destroi a reprodutibilidade. Recusa-se em voz
        // alta (principio P2) em vez de mentir com um sucesso que nao aconteceu.
        cpu.Set(kR0, kAeeUnsupported);
      } else if (idx == kSlotIdGetAppInstance) {
        // `void *GetAppInstance(void)` -- o ponteiro do applet, para o codigo que
        // nao tem o `po` a mao. Nao tem argumentos: os registos que a bateria
        // imprime sao RESIDUAIS, e foi por isso que quase persegui uma "fuga de
        // enderecos de saida para o guest" que nao existia.
        cpu.Set(kR0, applet_);
      } else if (idx == kSlotIdQueryClass) {
        // `boolean QueryClass(IShell *po, AEECLSID cls, AEEAppInfo *pai)`.
        //
        // Responde se a classe existe, e preenche o `AEEAppInfo` quando ha
        // ponteiro. As classes que SEI criar sao as que o `CreateInstance` e o
        // `CreateInstance` ja servem; o resto devolve FALSE -- recusar, nao
        // mentir. Um `AEEAppInfo` a zeros com `TRUE` seria a versao em dados do
        // stub silencioso.
        const std::uint32_t cls = cpu.Get(kR1);
        const std::uint32_t pai = cpu.Get(kR2);
        const bool conhecida = (cls == kIidDisplay || cls == kIidFileMgr ||
                                cls == kIidHeap || cls == kIidFile || cls == kIidSound ||
                                cls == kIidGraphics || cls == kIidRootForm ||
                                cls == kIidHid || cls == kIidSqlMgr ||
                                (entrada_pronta_ && cls == kClsidSignalCBFactory));
        if (pai != 0) {
          // AEEAppInfo: cls(0), pszName(4), pszIcon(8), dwIconSize(12), ...
          mem_.Escrever32(pai + 0, cls);
          mem_.Escrever32(pai + 4, 0);
          mem_.Escrever32(pai + 8, 0);
          mem_.Escrever32(pai + 12, 0);
          mem_.Escrever32(pai + 16, 0);
        }
        cpu.Set(kR0, conhecida ? 1u : 0u);
      } else if (idx == kSlotIdFmTest) {
        // `int Test(IFileMgr *po, const char *pszName)` -- devolve AEE_SUCCESS
        // se o ficheiro existe no sistema de ficheiros virtual.
        std::string nome;
        mem_.LerCadeia(cpu.Get(kR1), &nome, 512);
        const std::uint32_t existe = vfs_.Existe(nome) ? kAeeSuccess : kAeeFailed;
        traco_.Emitir(Area::Brew, Nivel::Depuracao, "FM_TEST",
                                        nome + (existe == 0 ? " -> OK" : " -> MISS"));
        cpu.Set(kR0, existe);
      } else if (idx == kSlotIdFmFree) {
        // `uint32 GetFreeSpace(IFileMgr *po, uint32 *pdwTotal)`. Valor
        // DECLARADO: nao ha disco neste emulador, e inventar um espaco
        // plausivel e melhor do que devolver zero -- zero faria um jogo recusar
        // gravar. Fica registado como valor declarado.
        if (cpu.Get(kR1) != 0) mem_.Escrever32(cpu.Get(kR1), 0x00100000u);
        cpu.Set(kR0, 0x00080000u);
      } else if (idx == kSlotIdFmLastErr) {
        cpu.Set(kR0, 0);
      } else if (idx == kBaseDoSlot + 500) {
        // dbgprintf
        std::string msg;
        mem_.LerCadeia(r0, &msg, 512);
        traco_.Emitir(Area::Brew, Nivel::Depuracao, "GUEST_DBGPRINTF", msg);
        cpu.Set(kR0, 0);
      } else if (idx >= kBaseDoSlot &&
                 AtenderAjudanteExtra(cpu, mem_, al_, traco_, (idx - kBaseDoSlot) * 4)) {
        // A TABELA DOS AJUDANTES EXTRA, e este ramo vem ANTES do ramo generico
        // dos 117 slots. **A ORDEM E O DEFEITO**: com a condicao invertida
        // (`!Atender...`) o caso ATENDIDO cai no ramo generico, que escreve
        // AEE_EUNSUPPORTED por cima do resultado e regista um
        // `servico_sem_nome_idx<idx>`. Foi o que a primeira versao deste gancho
        // fez, e a lista de demanda mostrou-o na ronda seguinte: os tres offsets
        // tratados apareceram como `servico_sem_nome_idx1017/1020/1078`. E a
        // setima vez que esta classe de erro aparece nesta arvore.
        //
        // R0 ja esta escrito (implementacao, ou recusa com o nome e a
        // assinatura). O `saidas` continua a contar no fim do laco.
      } else if (idx >= kBaseDoSlot) {
        const std::uint32_t off = (idx - kBaseDoSlot) * 4;
        const char* conhecido = zb2::brew::NomeDoAjudante(off);
        char nome[64];
        if (conhecido != nullptr) {
          std::snprintf(nome, sizeof(nome), "AEEHelperFuncs[0x%03x] %s", off, conhecido);
        } else {
          std::snprintf(nome, sizeof(nome), "AEEHelperFuncs[0x%03x]", off);
        }
        char det2[128];
        std::snprintf(det2, sizeof(det2), "r0=0x%08x r1=0x%08x r2=0x%08x", r0, cpu.Get(kR1),
                      cpu.Get(kR2));
        traco_.RegistarFalta(Area::Brew, nome, det2);
        cpu.Set(kR0, kAeeUnsupported);
        if (++saidas > 200) { resultado.motivo = "parou_em_slot_nao_implementado"; return resultado; }
      } else {
        traco_.RegistarFalta(Area::Brew, "servico_sem_nome_idx" + std::to_string(idx),
                            "chamado com r0=0x" + std::to_string(r0));
        cpu.Set(kR0, kAeeUnsupported);
        if (++saidas > 200) { resultado.motivo = "parou_em_slot_nao_implementado"; return resultado; }
      }
      cpu.Set(kPC, lr);
      if (++saidas > 20000) { resultado.motivo = "laco_de_saidas"; return resultado; }
      continue;
    }
    // O LIMITE E O TAMANHO DA IMAGEM, quando ele e conhecido. Sem ele, cai-se no
    // `kBase + 16 MB` de antes -- que e um limite generoso de mais, mas melhor do
    // que nenhum.
    const std::uint32_t fim = (faixa_fim_ > faixa_base_) ? faixa_fim_ : (kBase + 0x01000000u);
    if (pc < kBase || pc >= fim) {
      resultado.motivo = "saiu_do_modulo_para_0x" + std::to_string(pc);
      (void)pp_saida;
      return resultado;
    }
    // O LACO DE EVENTOS, em tempo VIRTUAL (principio P4): a cada passo avanca-se
    // 1 ms de tempo emulado, e um temporizador vencido e cumprido aqui.
    //
    // O callback de um `AEECallback` e um par `(funcao, contexto)` nos dois
    // primeiros campos. Chama-se com o contexto no r0, como o SDK define, e o
    // proprio callback re-arma o temporizador -- que e como um laco de quadro
    // se sustenta em BREW.
    ++agora_ms_;
    // O RELOGIO VIRTUAL E UM SO, e a entrada le-o daqui.
    //
    // A `EntradaDoZeebo` tem o seu proprio contador (e e ele que decide que
    // eventos do guiao ja valem), mas quem o avanca e o laco, com o MESMO relogio
    // que faz vencer os temporizadores. Dois relogios dentro da mesma corrida
    // seriam duas fontes de tempo -- e o P4 existe para haver uma.
    if (entrada_pronta_) entrada_.Repor(static_cast<std::uint32_t>(agora_ms_));
    if (timer_.ativo && agora_ms_ >= timer_.vence_em_ms && timer_.pfn != 0) {
      // UMA SO IMPLEMENTACAO do disparo do callback: a mesma que quem dirige o
      // titulo de fora usa. Duas copias disto divergiriam -- e a copia que aqui
      // estava guardava um `pc_salvo`/`lr_salvo` que nunca serviu para nada.
      if (PrepararCallbackDoTemporizador(cpu)) {
        ++resultado.passos;
        continuar_no_laco = true;
      }
    }

    // A ENTRADA, no mesmo lugar do temporizador e pela mesma razao: o laco de
    // eventos e o unico sitio onde o tempo VIRTUAL avanca (P4).
    if (entrada_pronta_ && ihid_.Bombear(cpu)) {
      continuar_no_laco = true;
      continue;
    }

    // A MIDIA ANDA COM O RELOGIO VIRTUAL DO LACO (P4), e o aviso e entregue aqui.
    //
    // As duas coisas juntas, e NAO dentro de um handler: a entrega reentra no
    // codigo do guest, e o `EntregarAviso` guarda e repoe os 16 registradores, o
    // CPSR e o PC -- o que so e seguro no passo, onde o guest esta numa fronteira
    // de instrucao.
    //
    // Sem o avanco, o `Play` de um titulo nunca chega ao fim e o aviso DONE -- o
    // que o `cnk2` conta para so tocar a musica da pista -- nunca nasce.
    if (media_ != nullptr) {
      media_->Avancar(kAmostrasDeMidiaPorMs);
      media_->EntregarAviso(cpu, kSentinela, 200000);
    }
    if (saidas > 200) { resultado.motivo = "parou_em_slot_nao_implementado"; return resultado; }
    cpu.Passo();
    ++resultado.passos;
  }
  resultado.motivo = "orcamento_esgotado";
  // O `return` EXPLICITO nao e estilo: cair fora do fim de uma funcao que devolve
  // por valor e COMPORTAMENTO INDEFINIDO, e o `ResultadoFase` tem um
  // `std::string` dentro -- o resultado medido foi `free(): double free detected`
  // no segundo titulo, com a pilha a apontar para o `append` de uma cadeia
  // VIZINHA. O `append` era a vitima; o culpado era este `return` em falta.
  return resultado;
}

}  // namespace zb2::brew
