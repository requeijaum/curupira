#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "core/audio/misturador.h"
// O LIMITE DA REGIAO DE MIDIA e o endereco do modulo que vem a seguir: o
// `kObjIgl` (`core/brew/igl.h`, "0x800B0000 e o primeiro bloco livre"). A
// regiao deste modulo tem de acabar ANTES dele, e o teste prova-o com a
// constante do outro modulo, e nao com um literal escrito aqui.
#include "core/brew/igl.h"
#include "core/brew/imedia.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

using zb2::ArmInterpreter;
using zb2::DestinoMemoria;
using zb2::Memoria;
using zb2::Traco;
using zb2::brew::Media;

namespace zb2::brew {
namespace {

// ---------------------------------------------------------------------------
// MONTADOR MINIMO. Mesmo padrao (e mesma razao) do `tests/cpu_test.cpp`: as
// instrucoes constroem-se a partir dos CAMPOS, e nao de literais hexadecimais.
// Um literal escrito a mao ja acusou o emulador por um erro do teste.
// ---------------------------------------------------------------------------
constexpr std::uint32_t kAl = 0xEu;

constexpr std::uint32_t DpImediato(std::uint32_t opcode, std::uint32_t rd, std::uint32_t rn,
                                   std::uint32_t imm) {
  return (kAl << 28) | (1u << 25) | ((opcode & 0xF) << 21) | ((rn & 0xF) << 16) |
         ((rd & 0xF) << 12) | (imm & 0xFF);
}
constexpr std::uint32_t DpRegistrador(std::uint32_t opcode, std::uint32_t rd, std::uint32_t rn,
                                      std::uint32_t rm) {
  return (kAl << 28) | ((opcode & 0xF) << 21) | ((rn & 0xF) << 16) | ((rd & 0xF) << 12) |
         (rm & 0xF);
}
// ARM ARM: LDR/STR imediato, pre-indexado, U=1 (soma), palavra (B=0).
// O bit 20 e o L (1 = carrega, 0 = guarda). Faltava-o, e o `static_assert` logo
// abaixo apanhou-o contra uma palavra lida de um modulo REAL -- que e para isso
// que ele existe.
constexpr std::uint32_t LdrImediato(std::uint32_t rd, std::uint32_t rn, std::uint32_t desloc) {
  return (kAl << 28) | (1u << 26) | (1u << 24) | (1u << 23) | (1u << 20) | ((rn & 0xF) << 16) |
         ((rd & 0xF) << 12) | (desloc & 0xFFF);
}
constexpr std::uint32_t StrImediato(std::uint32_t rd, std::uint32_t rn, std::uint32_t desloc) {
  return (kAl << 28) | (1u << 26) | (1u << 24) | (1u << 23) | ((rn & 0xF) << 16) |
         ((rd & 0xF) << 12) | (desloc & 0xFFF);
}
constexpr std::uint32_t SomaRegistrador(std::uint32_t rd, std::uint32_t rn, std::uint32_t rm) {
  return DpRegistrador(0x4, rd, rn, rm);
}
constexpr std::uint32_t SomaImediata(std::uint32_t rd, std::uint32_t rn, std::uint32_t imm) {
  return DpImediato(0x4, rd, rn, imm);
}
constexpr std::uint32_t MoveRegistrador(std::uint32_t rd, std::uint32_t rm) {
  return DpRegistrador(0xD, rd, 0, rm);
}
constexpr std::uint32_t MovImediato(std::uint32_t rd, std::uint32_t imm,
                                   bool mexe_nas_bandeiras = false) {
  return (kAl << 28) | (1u << 25) | ((mexe_nas_bandeiras ? 1u : 0u) << 20) |
         ((rd & 0xF) << 12) | (imm & 0xFF);
}
constexpr std::uint32_t Bx(std::uint32_t rm) { return (kAl << 28) | 0x012FFF10u | (rm & 0xF); }
// `b .` -- um laco que nao acaba. E o callback que NAO volta.
constexpr std::uint32_t BLacoInfinito() { return (kAl << 28) | 0x0AFFFFFEu; }
constexpr std::uint32_t Blx(std::uint32_t rm) { return (kAl << 28) | 0x012FFF30u | (rm & 0xF); }

// Confere o montador contra DUAS palavras que foram lidas do corpo de um titulo
// real (`cnk2.mod`, no tratador do aviso de midia): se estes numeros nao
// baterem, o montador esta errado e todos os testes abaixo medem outra coisa.
static_assert(LdrImediato(3, 1, 8) == 0xE5913008u, "ldr r3,[r1,#8] do cnk2");
static_assert(SomaImediata(3, 3, 1) == 0xE2833001u, "add r3,r3,#1 do cnk2");
static_assert(MoveRegistrador(12, 13) == 0xE1A0C00Du, "mov r12,sp do cnk2");

// --- os enderecos da bancada ------------------------------------------------
constexpr std::uint32_t kSentinela = 0xEEEE0000u;
constexpr std::uint32_t kTrampolim = 0x00100000u;
constexpr std::uint32_t kTratador = 0x00100100u;
constexpr std::uint32_t kDados = 0x00100200u;      // o `pUser` do callback
constexpr std::uint32_t kMediaData = 0x00100300u;  // um `AEEMediaData`
constexpr std::uint32_t kFicheiro = 0x00100400u;   // um nome de ficheiro
constexpr std::uint32_t kSaida = 0x00100500u;      // onde o `GetMediaParm` escreve
constexpr std::uint32_t kDestruidor = 0x00100180u; // callback que escreve em r0..r12
constexpr std::uint32_t kPreso = 0x00100280u;      // callback que NUNCA volta
// O PCM fica LONGE das outras areas: um teste entrega 22050 amostras (44100
// bytes), e uma area sobreposta fazia o teste medir os proprios dados de apoio.
constexpr std::uint32_t kBuffer = 0x00140000u;

// As palavras que o `pUser` do callback recebe:
//   0 = ultimo nCmd, 4 = ultimo nStatus, 8 = quantas vezes foi chamado,
//   12 = ultimo pCmdData, 16 = ultimo dwSize, 20 = ultimo pIMedia
constexpr std::uint32_t kOffUserContador = 8;

// A faixa de saida tem de estar CONFIGURADA antes de o `Media` nascer: o
// constructor guarda uma copia dela, e foi exatamente assim que um defeito real
// apareceu (a vtable era escrita em enderecos derivados de uma base zero, e a
// leitura de volta confirmava-os).
Saidas FaixaDeSaidaDoTeste() {
  Saidas s;
  s.base = 0xF0000000u;
  s.passo = 4;
  s.quantos = 100000;
  s.ativa = true;
  return s;
}

class Bancada {
 public:
  Bancada(Vfs* vfs = nullptr)
      : cpu_(mem_, &traco_), media_(mem_, traco_, saidas_, misturador_, vfs) {
    mem_.EscritorUnico("teste_de_midia");
    traco_.JuntarDestino(&destino_);
    cpu_.ConfigurarSaidas(saidas_);

    // O TRAMPOLIM: chama `vtable[r4]` de `po` com os argumentos que o C++ pos
    // em r1..r3. Nao ha endereco absoluto no codigo do guest -- tudo e lido do
    // objecto, e por isso o teste nao depende de onde o modulo foi carregado.
    const std::uint32_t trampolim[] = {
        MoveRegistrador(5, 14),         // mov r5, lr     (guarda o retorno)
        LdrImediato(12, 0, 0),          // ldr r12,[r0]   (a vtable)
        SomaRegistrador(12, 12, 4),     // add r12,r12,r4 (o deslocamento do slot)
        LdrImediato(12, 12, 0),         // ldr r12,[r12]  (o endereco de saida)
        Blx(12),                        // blx r12
        MoveRegistrador(14, 5),         // mov lr, r5
        Bx(14),                         // bx lr
    };
    // O TRATADOR: le os MESMOS dois campos que o tratador real do `cnk2` le
    // (`+8` o nCmd, `+16` o nStatus), conta as chamadas, e guarda tambem
    // `pCmdData` e `dwSize`.
    const std::uint32_t tratador[] = {
        LdrImediato(3, 1, 8),  StrImediato(3, 0, 0),    // nCmd    -> pUser+0
        LdrImediato(3, 1, 16), StrImediato(3, 0, 4),    // nStatus -> pUser+4
        LdrImediato(3, 0, 8),  SomaImediata(3, 3, 1), StrImediato(3, 0, 8),
        LdrImediato(3, 1, 20), StrImediato(3, 0, 12),   // pCmdData
        LdrImediato(3, 1, 24), StrImediato(3, 0, 16),   // dwSize
        // E o `pIMedia`, que e por onde o jogo CORRELA o aviso com o objecto
        // (`AEEIMedia.h`, "Callback Events"): sem o ler, um teste nao consegue
        // distinguir um aviso do objecto certo de um aviso de outro.
        LdrImediato(3, 1, 4),  StrImediato(3, 0, 20),   // pIMedia -> pUser+20
        Bx(14),
    };
    // O DESTRUIDOR: um callback que escreve valores em r0..r12 e mexe nas
    // bandeiras. Existe para o teste da reposicao: se a entrega nao guardar e
    // repor os registradores, o quadro do guest fica estragado.
    const std::uint32_t destruidor[] = {
        // Primeiro de tudo: guardar o `pUser` (r0, posto pela entrega) em
        // `[r11]`, que o teste aponta para um marcador. E a PROVA de que o
        // callback correu MESMO e de que o argumento chegou no sitio certo --
        // o resto da rotina estraga os registradores de proposito.
        StrImediato(0, 11, 0),
        MovImediato(0, 1),  MovImediato(1, 2),  MovImediato(2, 3),  MovImediato(3, 4),
        MovImediato(4, 5),  MovImediato(5, 6),  MovImediato(6, 7),  MovImediato(7, 8),
        MovImediato(8, 9),  MovImediato(9, 10), MovImediato(10, 11), MovImediato(11, 12),
        MovImediato(12, 13), MovImediato(3, 0, true),  // mexe nas bandeiras (Z)
        Bx(14),
    };
    const std::uint32_t preso[] = {BLacoInfinito()};
    PorPalavras(kTrampolim, trampolim, sizeof(trampolim) / sizeof(trampolim[0]));
    PorPalavras(kTratador, tratador, sizeof(tratador) / sizeof(tratador[0]));
    PorPalavras(kDestruidor, destruidor, sizeof(destruidor) / sizeof(destruidor[0]));
    PorPalavras(kPreso, preso, sizeof(preso) / sizeof(preso[0]));
    for (std::uint32_t k = 0; k < 8; ++k) mem_.Escrever32(kDados + k * 4, 0);
    instalar_ = media_.Instalar();
  }

  const ResultadoCablagem& Instalacao() const { return instalar_; }

  // Chama o slot `slot` do objecto `po`, com ate tres argumentos. O `r4` leva o
  // deslocamento em bytes, e o `lr` leva a sentinela: o trampolim volta para la.
  std::uint32_t Chamar(std::uint32_t slot, std::uint32_t po, std::uint32_t a1 = 0,
                       std::uint32_t a2 = 0, std::uint32_t a3 = 0) {
    cpu_.Set(kR0, po);
    cpu_.Set(kR1, a1);
    cpu_.Set(kR2, a2);
    cpu_.Set(kR3, a3);
    cpu_.Set(4, slot * 4);
    cpu_.Set(kLR, kSentinela);
    cpu_.Set(kPC, kTrampolim);
    Correr();
    return cpu_.Get(kR0);
  }

  std::uint32_t CriarMedia(std::uint32_t cls, std::uint32_t pponovo) {
    const std::uint32_t r = media_.Criar(cls, pponovo);
    codigo_de_criar_ = r;
    return mem_.Ler32(pponovo);
  }

  void PorPcm(const std::vector<std::int16_t>& amostras) {
    for (std::size_t k = 0; k < amostras.size(); ++k) {
      const std::uint16_t par = static_cast<std::uint16_t>(amostras[k]);
      mem_.Escrever8(kBuffer + static_cast<std::uint32_t>(k) * 2,
                     static_cast<std::uint8_t>(par & 0xFF));
      mem_.Escrever8(kBuffer + static_cast<std::uint32_t>(k) * 2 + 1,
                     static_cast<std::uint8_t>((par >> 8) & 0xFF));
    }
    mem_.Escrever32(kMediaData + kOffMidiaClsData, kMmdBuffer);
    mem_.Escrever32(kMediaData + kOffMidiaPData, kBuffer);
    mem_.Escrever32(kMediaData + kOffMidiaDwSize, static_cast<std::uint32_t>(amostras.size() * 2));
  }

  std::uint32_t DefinirDados() {
    return Chamar(brew_slots::kMedia_SetMediaParm, po_, kMmParmMediaData, kMediaData, 0);
  }
  std::uint32_t SetParm(std::int32_t id, std::int32_t p1, std::int32_t p2 = 0) {
    return Chamar(brew_slots::kMedia_SetMediaParm, po_,
                  static_cast<std::uint32_t>(id), static_cast<std::uint32_t>(p1),
                  static_cast<std::uint32_t>(p2));
  }
  std::uint32_t GetParm(std::int32_t id, std::uint32_t pp1, std::uint32_t pp2 = 0) {
    return Chamar(brew_slots::kMedia_GetMediaParm, po_,
                  static_cast<std::uint32_t>(id), pp1, pp2);
  }
  void RegistrarNotify() { Chamar(brew_slots::kMedia_RegisterNotify, po_, kTratador, kDados); }
  // AVANCAR entregando: as amostras sao consumidas e os avisos que nascem sao
  // LEVADOS ao callback. Chamar `OMedia().Avancar` por fora do laco nao entrega
  // nada -- o aviso fica em fila, que e o comportamento correto (a entrega e do
  // laco), e por isso a bancada tem este metodo.
  void Avancar(std::uint32_t amostras) {
    media_.Avancar(amostras);
    EntregarAvisos();
  }
  void Play() { Chamar(brew_slots::kMedia_Play, po_); }
  void Stop() { Chamar(brew_slots::kMedia_Stop, po_); }
  // O `Release` do guest, que e o da IBase -- slot 1, do MOTOR --, SEM entregar
  // os avisos na volta do laco. A bancada entrega os avisos em cada volta
  // (`Correr`), e o teste do aviso nascido antes do `Release` precisa de ESCOLHER
  // o momento da entrega; desligar a entrega e a unica maneira de o fazer sem
  // fingir o resultado do `Release` (escrever a contagem a zero a mao).
  void Soltar(std::uint32_t po) {
    entregar_no_laco_ = false;
    Chamar(1, po, 0, 0, 0);
    entregar_no_laco_ = true;
  }
  void Passo(std::uint32_t slots) { Chamar(brew_slots::kMedia_Seek, po_, slots, 0); }
  std::uint32_t Avisos() const { return mem_.Ler32(kDados + kOffUserContador); }
  std::uint32_t UltimoCmd() const { return mem_.Ler32(kDados + 0); }
  std::uint32_t UltimoStatus() const { return mem_.Ler32(kDados + 4); }
  std::uint32_t UltimoPcmdData() const { return mem_.Ler32(kDados + 12); }
  std::uint32_t UltimoDwSize() const { return mem_.Ler32(kDados + 16); }
  // O `pIMedia` do ultimo aviso entregue: a IDENTIDADE que o jogo ve.
  std::uint32_t UltimoPimidia() const { return mem_.Ler32(kDados + 20); }

  void ReporContador() { mem_.Escrever32(kDados + kOffUserContador, 0); }
  void ApontarParaObjeto(std::uint32_t po) { po_ = po; }

  Memoria& Mem() { return mem_; }
  const ::zb2::Saidas& AsSaidas() const { return saidas_; }
  ArmInterpreter& Cpu() { return cpu_; }
  // AVANCAR sem entregar: o teste do callback quer controlar a entrega a mao.
  void AvancarSemEntregar(std::uint32_t amostras) { media_.Avancar(amostras); }
  Traco& OTraco() { return traco_; }
  DestinoMemoria& Eventos() { return destino_; }
  Media& OMedia() { return media_; }
  audio::Misturador& OMisturador() { return misturador_; }

 private:
  void PorPalavras(std::uint32_t onde, const std::uint32_t* palavras, std::size_t quantas) {
    for (std::size_t k = 0; k < quantas; ++k) {
      mem_.Escrever32(onde + static_cast<std::uint32_t>(k) * 4, palavras[k]);
    }
  }

  // O laco minimo da bancada. Faz o que o laco do motor faz para esta faixa:
  // trata a IBase (AddRef/Release), entrega os indices de saida do IMedia, e
  // leva os avisos ao callback do guest.
  void Correr() {
    for (int k = 0; k < 100000; ++k) {
      const std::uint32_t pc = cpu_.Get(kPC);
      if (pc == kSentinela) return;
      std::uint32_t idx = 0;
      if (saidas_.Contem(pc, &idx)) {
        const std::uint32_t lr = cpu_.Get(kLR);
        const std::uint32_t r0 = cpu_.Get(kR0);
        if (idx == 3) {
          const std::uint32_t n = mem_.Ler32(r0 + kOffObjRefs) + 1;
          mem_.Escrever32(r0 + kOffObjRefs, n);
          cpu_.Set(kR0, n);
        } else if (idx == 4) {
          const std::uint32_t n = mem_.Ler32(r0 + kOffObjRefs);
          if (n > 0) mem_.Escrever32(r0 + kOffObjRefs, n - 1);
          cpu_.Set(kR0, n > 0 ? n - 1 : 0);
        } else {
          EXPECT_TRUE(media_.Atender(idx, cpu_))
              << "o indice de saida " << idx << " nao e do IMedia nem da IBase";
        }
        cpu_.Set(kPC, lr);
        if (entregar_no_laco_) EntregarAvisos();
        continue;
      }
      cpu_.Passo();
    }
    ADD_FAILURE() << "o guest nao voltou a sentinela";
  }

  // ENTREGA DOS AVISOS. O callback do guest e chamado como uma ENTRADA NO
  // CONVIDADO a partir do hospedeiro: os 16 registradores e o CPSR sao guardados
  // e repostos, porque no momento da entrega o guest tem registradores vivos que
  // uma chamada de callback pode estragar. A arvore antiga tem esta primitiva
  // medida (`HleRuntime::CallArmFunctionPreservingContext`,
  // `core/brew/hle_runtime.cpp`), e e a mesma disciplina.
  void EntregarAvisos() {
    // A entrega e a DO MODULO (`Media::EntregarAviso`): a bancada nao
    // reimplementa a reposicao dos registradores que esta a ser testada.
    while (media_.AvisosPendentes() > 0) {
      if (!media_.EntregarAviso(cpu_, kSentinela, 20000)) break;
      ++avisos_entregues_;
    }
  }

  std::array<std::uint32_t, 16> GuardarRegistradores() const {
    std::array<std::uint32_t, 16> v{};
    for (int r = 0; r < 16; ++r) v[static_cast<std::size_t>(r)] = cpu_.Get(r);
    return v;
  }

  Traco traco_{"teste_de_midia"};
  DestinoMemoria destino_;
  Memoria mem_{&traco_};
  Saidas saidas_ = FaixaDeSaidaDoTeste();
  ArmInterpreter cpu_;
  audio::Misturador misturador_;
  Media media_;
  ResultadoCablagem instalar_{};
  std::uint32_t po_ = 0;
  std::uint32_t codigo_de_criar_ = 0;
  std::uint32_t avisos_entregues_ = 0;
  // Ligado por omissao: a bancada entrega os avisos a cada volta do laco, como o
  // motor. Ha UM teste (o do aviso nascido antes do `Release`) que o desliga.
  bool entregar_no_laco_ = true;
};

std::vector<std::int16_t> Onda(std::size_t quantas, std::int16_t amplitude) {
  std::vector<std::int16_t> v(quantas);
  for (std::size_t k = 0; k < quantas; ++k) {
    v[k] = ((k % 4) < 2) ? amplitude : static_cast<std::int16_t>(-amplitude);
  }
  return v;
}

// ---------------------------------------------------------------------------
// A TABELA DE SLOTS: do cabecalho do SDK, e nao da memoria.
// ---------------------------------------------------------------------------
TEST(Media, ATabelaDeSlotsEADoSDK) {
  // `INHERIT_IMedia` comeca em `INHERIT_IQI` (AddRef, Release, QueryInterface),
  // e nao em `INHERIT_IBase`: quem contar tres slots de cabeca com a IBase erra
  // TODOS os numeros por um -- foi o que ja aconteceu neste trabalho.
  EXPECT_EQ(brew_slots::kMedia_RegisterNotify, 3u);
  EXPECT_EQ(brew_slots::kMedia_GetState, 13u);
  EXPECT_EQ(kSlotsDoMedia, 14u);
  EXPECT_EQ(kIidMedia, 0x01005500u);
}

TEST(Media, OClsidDoMidiaOutMsgNaoEMPEG4) {
  // ARMADILHA DECLARADA no cabecalho deste trabalho: `0x01005505` ja foi lido
  // como MPEG4 uma vez. Em `AEEClassIDs.h` a familia comeca em 0x01005500:
  // MIDIOUTMSG = +5 e MPEG4 = +7.
  EXPECT_TRUE(ClasseDeMidia(0x01005505u));
  EXPECT_STREQ(NomeDaClasseDeMidia(0x01005505u), "AEECLSID_MEDIAMIDIOUTMSG");
  EXPECT_STREQ(NomeDaClasseDeMidia(0x01005507u), "AEECLSID_MEDIAMPEG4");
  EXPECT_FALSE(ClasseDeMidia(0x01005505u + 0x100u));
}

TEST(Media, InstalarRecusaUmaFaixaDeSaidaNaoConfigurada) {
  // PROVA DA GUARDA POR VIOLACAO, e esta guarda nasceu de um defeito REAL: o
  // `Saidas` era copiado para dentro do objecto antes de o chamador o
  // configurar, e a vtable era escrita em enderecos derivados de uma base ZERO.
  // A leitura de volta CONFIRMAVA tudo -- lia o que se tinha acabado de
  // escrever. So um teste com a faixa vazia denuncia isto.
  Traco traco("teste_da_guarda");
  Memoria mem(&traco);
  audio::Misturador misturador;
  Saidas vazias;  // ativa = false, quantos = 0
  Media media(mem, traco, vazias, misturador, nullptr);
  const ResultadoCablagem r = media.Instalar();
  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.motivo.find("faixa de saida"), std::string::npos) << r.motivo;
}

TEST(Media, InstalarEscreveOsQuatorzeSlotsDoIMedia) {
  Bancada b;
  ASSERT_TRUE(b.Instalacao().ok) << b.Instalacao().motivo;
  // Cada slot PROPRIO tem um endereco de saida diferente: um stub so para todos
  // foi o defeito que deixou 86 377 chamadas de GL sem nome na arvore antiga.
  for (std::uint32_t s = 2; s < kSlotsDoMedia; ++s) {
    EXPECT_EQ(b.Mem().Ler32(b.AsSaidas().Endereco(kVtableDoMedia) + s * 4),
              b.AsSaidas().Endereco(kBaseDoMedia + s))
        << "slot " << s;
  }
  EXPECT_EQ(b.Mem().Ler32(b.AsSaidas().Endereco(kVtableDoMedia) + 0), b.AsSaidas().Endereco(3));
  EXPECT_EQ(b.Mem().Ler32(b.AsSaidas().Endereco(kVtableDoMedia) + 4), b.AsSaidas().Endereco(4));
}

TEST(Media, AGuardaDaTabelaDeclaradaRecusaUmSlotPorPreencher) {
  // PROVA DA GUARDA POR VIOLACAO, dentro do proprio teste: uma tabela a que
  // falta o slot 13 tem de ser RECUSADA. Sem esta guarda, um slot por preencher
  // era instalado em silencio -- que e exatamente o defeito dos 86 377
  // `glCullFace` descartados na arvore antiga.
  SlotDoMedia inteira[14];
  for (std::uint32_t s = 0; s < 14; ++s) inteira[s] = {s, "x"};
  EXPECT_TRUE(ConferirTabelaDeSlots(inteira, 14, 14).ok);
  EXPECT_FALSE(ConferirTabelaDeSlots(inteira, 13, 14).ok);  // falta o slot 13

  SlotDoMedia repetida[14];
  for (std::uint32_t s = 0; s < 13; ++s) repetida[s] = {s, "x"};
  repetida[13] = {12, "x"};  // o 12 duas vezes, o 13 nenhuma
  const ResultadoCablagem r = ConferirTabelaDeSlots(repetida, 14, 14);
  EXPECT_FALSE(r.ok);
  // A guarda percorre os slots POR ORDEM: o primeiro problema que encontra e o
  // 12 declarado duas vezes. E o motivo TEM de nomear o slot -- um "tabela
  // invalida" sem nome obriga a procurar a mao, que e o defeito de instrumento
  // que este projeto existe para nao repetir.
  EXPECT_NE(r.motivo.find("slot 12"), std::string::npos) << r.motivo;
  EXPECT_NE(r.motivo.find("2 vezes"), std::string::npos) << r.motivo;

  // E a tabela VERDADEIRA passa, com os 14 slots.
  EXPECT_TRUE(ConferirTabelaDeSlots(kTabelaDeSlotsDoMedia, kQuantosSlotsDoMediaDeclarados,
                                    kSlotsDoMedia)
                  .ok);
  EXPECT_EQ(kQuantosSlotsDoMediaDeclarados, 14u);
  EXPECT_STREQ(kTabelaDeSlotsDoMedia[2].nome, "IMedia::QueryInterface");
  EXPECT_STREQ(kTabelaDeSlotsDoMedia[8].nome, "IMedia::Stop");
}

// ---------------------------------------------------------------------------
// O CICLO DE VIDA, pelos SLOTS da vtable, com codigo do guest a correr.
// ---------------------------------------------------------------------------
namespace {
constexpr std::uint32_t kPponovo = 0x00100A00u;
}

TEST(Media, CriarAceitaAMediaEEscreveOPonteiroNoGuest) {
  Bancada b;
  const std::uint32_t po = b.CriarMedia(kClasseMultimidia, kPponovo);
  EXPECT_EQ(po, kObjMediaBase);
  EXPECT_EQ(b.OMedia().ObjetosVivos(), 1u);
  // O objecto e ROPI: `[0]` = a vtable, `[4]` = a contagem de referencias.
  EXPECT_EQ(b.Mem().Ler32(po + kOffObjVtable), b.AsSaidas().Endereco(kVtableDoMedia));
  EXPECT_EQ(b.Mem().Ler32(po + kOffObjRefs), 1u);
  EXPECT_EQ(b.OMedia().EstadoDe(po), kMmEstadoOcioso);
}

TEST(Media, CriarRecusaUmaClasseQueNaoEMidia) {
  // Recusar tem de ser RUIDOSO (P2): codigo do SDK, nada escrito na memoria do
  // guest, e uma falta registada com o nome.
  Bancada b;
  b.Mem().Escrever32(kPponovo, 0xDEADBEEFu);
  EXPECT_EQ(b.OMedia().Criar(0x01001001u, kPponovo), kAeeClasseNaoSuportada);
  EXPECT_EQ(b.Mem().Ler32(kPponovo), 0xDEADBEEFu);
  EXPECT_GE(b.OMedia().PedidosRecusados(), 1u);
  EXPECT_NE(b.OMedia().UltimoMotivoDeRecusa().find("0x01001001"), std::string::npos);
}

TEST(Media, OReleaseDoGuestDevolveOLugarAoConjunto) {
  // O `Release` do guest chega pela IBase, que e do MOTOR (slot 1 -> saida 4).
  // Sem recolher os objectos libertados, o conjunto esgotava-se e o pedido
  // seguinte respondia "sem memoria" -- um erro que parece do jogo e nao e.
  //
  // Os 19 sao o numero que o `abd` pede na corrida de referencia do `slot32`
  // (`IMedia::Criar` 19 faltas): o jogo guarda UM `IMedia` POR SOM. Aqui prova-se
  // que o `Release` de um deles DEVOLVE o lugar, e com o mesmo endereco -- um
  // pool que so cresce tambem serviria o `abd`, mas nao devolver nada seria
  // deixar a memoria do guest a encher sem razao.
  Bancada b;
  std::vector<std::uint32_t> objetos;
  for (std::uint32_t k = 0; k < 19; ++k) {
    objetos.push_back(b.CriarMedia(kClasseMultimidia, kPponovo));
  }
  EXPECT_EQ(b.OMedia().ObjetosVivos(), 19u);
  EXPECT_EQ(b.OMedia().PedidosRecusados(), 0u);

  const std::uint32_t primeiro = objetos.front();
  EXPECT_EQ(primeiro, kObjMediaBase);
  b.ApontarParaObjeto(primeiro);
  b.Chamar(1, primeiro, 0, 0, 0);  // vtable[1] = Release
  EXPECT_EQ(b.Mem().Ler32(primeiro + kOffObjRefs), 0u);
  EXPECT_EQ(b.CriarMedia(kClasseMultimidia, kPponovo), primeiro);
  EXPECT_EQ(b.OMedia().ObjetosVivos(), 19u);
}

TEST(Media, OConjuntoDeObjetosServeUmIMediaPorSom) {
  // A MEDICAO QUE ISTO GUARDA: `/tmp/corrida_slot32.json`, com o `GetHandler` ja
  // a resolver o MIME numa classe de midia -- `abd` 19 pedidos, `ridgeracer` 9,
  // `torkandkral` 1, 29 recusas no total. Os jogos guardam UM `IMedia` POR SOM e
  // nao o soltam entre sons; com um conjunto fixo de 16, o 17.o pedido do `abd`
  // respondia "sem memoria".
  //
  // O SDK NAO TEM ESTE NUMERO. O que a doc limita sao as VOZES ao mesmo tempo
  // (`AEEMedia.txt`, "IMedia - Simultaneous media playback": 1 MIDI/MMF/PMD + 4
  // QCP/AMR/ADPCM, e 4+4 em 6550 e acima). Criar objectos nao tem limite no SDK.
  Bancada b;
  constexpr std::uint32_t kObjetosDoAbd = 19;
  std::vector<std::uint32_t> objetos;
  for (std::uint32_t k = 0; k < kObjetosDoAbd; ++k) {
    // O ENDERECO E O PROPRIO TESTE DA RECUSA: cada pedido tem de receber o
    // endereco seguinte da regiao. Um pedido recusado devolve o ponteiro que
    // estiver na memoria (o do ultimo objecto), e nao um endereco novo.
    const std::uint32_t po = b.CriarMedia(kClasseMultimidia, kPponovo);
    ASSERT_EQ(po, kObjMediaBase + k * kPassoDoObjetoMedia)
        << "o pedido " << (k + 1) << " de " << kObjetosDoAbd << " nao recebeu um objecto";
    objetos.push_back(po);
  }
  EXPECT_EQ(b.OMedia().ObjetosVivos(), kObjetosDoAbd);
  EXPECT_EQ(b.OMedia().PedidosRecusados(), 0u);

  // E o ULTIMO dos 19 TEM de funcionar: criar 19 e nao conseguir tocar o 19.o nao
  // serve de nada. O `pIMedia` do aviso e por onde o jogo correla o som.
  const std::uint32_t ultimo = objetos.back();
  b.ApontarParaObjeto(ultimo);
  b.RegistrarNotify();
  b.PorPcm(Onda(64, 4000));
  ASSERT_EQ(b.DefinirDados(), kAeeSucesso);
  b.Play();
  b.Avancar(64);
  EXPECT_EQ(b.Avisos(), 1u);
  EXPECT_EQ(b.UltimoStatus(), static_cast<std::uint32_t>(kMmStatusDone));
  EXPECT_EQ(b.UltimoPimidia(), ultimo);
  EXPECT_EQ(b.OMedia().EstadoDe(ultimo), kMmEstadoPronto);
}

TEST(Media, ARegiaoDosObjetosTemUmEnderecoPorObjectoEAUltimaRecusaNomeia) {
  // O LIMITE QUE FICA, e porque fica: NAO ha numero de objectos no SDK, e o que
  // limita e a REGIAO DE MEMORIA que este modulo reservou para eles
  // (`[kObjMediaBase, kAvisoBase)`, a `kPassoDoObjetoMedia`). Um conjunto que
  // cresce sem fim escreveria fora dela.
  Bancada b;
  for (std::uint32_t k = 0; k < kMaxObjetosDeMidia; ++k) {
    ASSERT_EQ(b.CriarMedia(kClasseMultimidia, kPponovo), kObjMediaBase + k * kPassoDoObjetoMedia)
        << "criacao " << k;
  }
  EXPECT_EQ(b.OMedia().ObjetosVivos(), kMaxObjetosDeMidia);
  EXPECT_EQ(b.OMedia().PedidosRecusados(), 0u);
  // O ultimo objecto fica ANTES do fim da regiao: o endereco do proximo seria o
  // dos avisos.
  const std::uint32_t ultimo = kObjMediaBase + (kMaxObjetosDeMidia - 1) * kPassoDoObjetoMedia;
  EXPECT_EQ(b.OMedia().EstadoDe(ultimo), kMmEstadoOcioso);
  EXPECT_LT(ultimo, kAvisoBase);
  EXPECT_EQ(ultimo + kPassoDoObjetoMedia, kAvisoBase);

  b.Mem().Escrever32(kPponovo, 0x1234u);
  EXPECT_EQ(b.OMedia().Criar(kClasseMultimidia, kPponovo), kAeeSemMemoria);
  EXPECT_EQ(b.Mem().Ler32(kPponovo), 0x1234u);  // nada foi escrito
  // E o motivo NOMEIA o que acabou, com o endereco: "limite" sem numero obriga a
  // ler o codigo para saber que limite era.
  const std::string motivo = b.OMedia().UltimoMotivoDeRecusa();
  EXPECT_NE(motivo.find(std::to_string(kMaxObjetosDeMidia)), std::string::npos) << motivo;
  EXPECT_NE(motivo.find("regiao de midia"), std::string::npos) << motivo;
}

TEST(Media, ARegiaoDeMidiaNaoInvadeAQuemVemADepoisDela) {
  // A ARITMETICA DAS TRES REGIOES, provada em vez de suposta: os objectos, os
  // avisos (`AEEMediaCmdNotify`, um por objecto) e os dados dos avisos tem de
  // caber INTEIROS na regiao de midia. Foi por nao estar escrito que o `kObjIgl`
  // ia nascer dentro dela (`core/brew/igl.h`, o mapa dos enderecos de objecto).
  EXPECT_LE(kObjMediaBase + kMaxObjetosDeMidia * kPassoDoObjetoMedia, kAvisoBase);
  EXPECT_LE(kAvisoBase + kMaxObjetosDeMidia * kTamanhoDoAviso, kAvisoDadosBase);
  EXPECT_LE(kAvisoDadosBase + kMaxObjetosDeMidia * 4, kObjIgl);
}

TEST(Media, SetMediaParmAceitaOsParametrosQueOSDKDefine) {
  Bancada b;
  const std::uint32_t po = b.CriarMedia(kClasseMultimidia, kPponovo);
  b.ApontarParaObjeto(po);
  b.PorPcm(Onda(64, 1000));
  EXPECT_EQ(b.DefinirDados(), kAeeSucesso);
  for (std::size_t k = 0; k < kQuantosParametrosDeMidia; ++k) {
    const ParametroDeMidia& p = kParametrosDeMidia[k];
    if (p.id == kMmParmMediaData) continue;  // ja tratado acima
    if (p.no_set != TratamentoDeParametro::Aplicado &&
        p.no_set != TratamentoDeParametro::Guardado) {
      continue;  // os recusados e os so de leitura tem teste proprio
    }
    std::int32_t valor = 1;
    switch (p.id) {
      case kMmParmAudioDevice: valor = 9; break;      // AEE_SOUND_DEVICE_SPEAKER
      case kMmParmAudioPath: valor = kMmCaminhoLocal; break;
      case kMmParmVolume: valor = 90; break;
      case kMmParmMute: valor = 0; break;
      case kMmParmTempo: valor = 100; break;
      case kMmParmTune: valor = 0x40; break;
      case kMmParmPan: valor = kMmMaxPan / 2; break;
      case kMmParmTickTime: valor = 1000; break;
      case kMmParmRect: valor = 0; break;
      case kMmParmPlayRepeat: valor = 1; break;
      case kMmParmPos: valor = 0; break;
      case kMmParmEnable: valor = kMmCapsAudio; break;
      case kMmParmChannelShare: valor = 1; break;
      case kMmParmRate: valor = 0x00010001; break;
      case kMmParmPlayType: valor = kMmTipoNormal; break;
      case kMmParmAudioSync: valor = 1; break;
      case kMmParmNotes: valor = 8; break;
      default: valor = 0; break;
    }
    EXPECT_EQ(b.SetParm(p.id, valor), kAeeSucesso) << p.nome;
  }
}

TEST(Media, GetMediaParmDevolveOQueOSetGuardou) {
  Bancada b;
  const std::uint32_t po = b.CriarMedia(kClasseMultimidia, kPponovo);
  b.ApontarParaObjeto(po);
  // Os que o `Get` devolve em `pP1`. O `MM_PARM_VOLUME` e o caso que a arvore
  // antiga ja tinha medido ("volume ja devolvido do estado").
  EXPECT_EQ(b.SetParm(kMmParmVolume, 42), kAeeSucesso);
  EXPECT_EQ(b.GetParm(kMmParmVolume, kSaida), kAeeSucesso);
  EXPECT_EQ(b.Mem().Ler32(kSaida), 42u);
  EXPECT_EQ(b.SetParm(kMmParmMute, 1), kAeeSucesso);
  EXPECT_EQ(b.GetParm(kMmParmMute, kSaida), kAeeSucesso);
  EXPECT_EQ(b.Mem().Ler32(kSaida), 1u);
  EXPECT_EQ(b.SetParm(kMmParmPlayRepeat, 0), kAeeSucesso);
  EXPECT_EQ(b.GetParm(kMmParmPlayRepeat, kSaida), kAeeSucesso);
  EXPECT_EQ(b.Mem().Ler32(kSaida), 0u);
}

TEST(Media, OsParametrosSoDeLeituraRecusamOSet) {
  // `AEEIMedia.h` marca `MM_PARM_CLSID`, `MM_PARM_CAPS` e `MM_PARM_SEEK_CAPS`
  // como `Get`. Um `Set` neles nao pode "aceitar e guardar": recusa, com o nome.
  Bancada b;
  const std::uint32_t po = b.CriarMedia(kClasseMultimidia, kPponovo);
  b.ApontarParaObjeto(po);
  const std::int32_t so_de_leitura[] = {kMmParmClsid, kMmParmCaps, kMmParmSeekCaps};
  for (const std::int32_t id : so_de_leitura) {
    EXPECT_EQ(b.SetParm(id, 0), kAeeNaoSuportado) << id;
    EXPECT_EQ(b.GetParm(id, kSaida, kSaida + 4), kAeeSucesso) << id;
  }
  // E o `Get` do CLSID devolve a classe com que o objecto nasceu.
  b.GetParm(kMmParmClsid, kSaida);
  EXPECT_EQ(b.Mem().Ler32(kSaida), kClasseMultimidia);
  // O CAPS diz o que esta arvore faz mesmo: audio.
  b.GetParm(kMmParmCaps, kSaida, kSaida + 4);
  EXPECT_EQ(b.Mem().Ler32(kSaida), kMmCapsAudio);
}

TEST(Media, UmParametroForaDaFaixaERecusado) {
  // `MM_PARM_VOLUME` e 0 a AEE_MAX_VOLUME (100). 101 nao e "quase 100".
  Bancada b;
  const std::uint32_t po = b.CriarMedia(kClasseMultimidia, kPponovo);
  b.ApontarParaObjeto(po);
  const std::uint32_t antes = b.OMedia().PedidosRecusados();
  EXPECT_EQ(b.SetParm(kMmParmVolume, 101), kAeeParametroErrado);
  EXPECT_EQ(b.SetParm(kMmParmMute, 2), kAeeParametroErrado);
  EXPECT_EQ(b.SetParm(kMmParmTune, 0x41 + 1), kAeeParametroErrado);
  EXPECT_EQ(b.SetParm(kMmParmPan, kMmMaxPan + 1), kAeeParametroErrado);
  EXPECT_EQ(b.OMedia().PedidosRecusados(), antes + 4);
}

TEST(Media, UmParametroQueNaoExisteNoSDKERecusado) {
  Bancada b;
  const std::uint32_t po = b.CriarMedia(kClasseMultimidia, kPponovo);
  b.ApontarParaObjeto(po);
  EXPECT_EQ(b.SetParm(99, 0), kAeeParametroErrado);
  EXPECT_EQ(b.GetParm(99, kSaida), kAeeParametroErrado);
}

TEST(Media, ORecordRecusaEDeixaFaltaRegistada) {
  // P2: gravacao nao esta implementada, e o caminho RECUSA e REGISTA. Um
  // "devolve sucesso e nao faz nada" aqui seria um stub silencioso.
  Bancada b;
  const std::uint32_t po = b.CriarMedia(kClasseMultimidia, kPponovo);
  b.ApontarParaObjeto(po);
  EXPECT_EQ(b.Chamar(brew_slots::kMedia_Record, po), static_cast<std::uint32_t>(kAeeNaoSuportado));
  EXPECT_GE(b.OTraco().ContagemFaltas().count("IMedia::Record"), 1u);
}

TEST(Media, PlaySemDadosERecusado) {
  Bancada b;
  const std::uint32_t po = b.CriarMedia(kClasseMultimidia, kPponovo);
  b.ApontarParaObjeto(po);
  b.Chamar(brew_slots::kMedia_Play, po);
  EXPECT_EQ(b.OMedia().EstadoDe(po), kMmEstadoOcioso);
  EXPECT_GE(b.OTraco().ContagemFaltas().count("IMedia::Play"), 1u);
}

// O `MMD_BUFFER` DE UM DESCODIFICADOR NAO E PCM. O `AEECLSID_MEDIAMP3` do
// `a3d` recebe o ficheiro inteiro (`a3d_sound_bgm_00.mp3`, 150352 bytes; a
// segunda faixa, 85261) e nos liamos aquilo como amostras de 16 bits: 99,8% de
// amostras nao nulas, som inventado devolvido com SUCCESS. Estes tres testes
// prendem a correcao -- e o do meio e o que teria apanhado o defeito, porque o
// PAR era o caso silencioso.
TEST(Media, OBufferImparDeUmDescodificadorEGuardadoEOEstadoFicaPronto) {
  Bancada b;
  const std::uint32_t po = b.CriarMedia(0x01005502u /* AEECLSID_MEDIAMP3 */, kPponovo);
  b.ApontarParaObjeto(po);
  b.Mem().Escrever32(kMediaData + kOffMidiaClsData, kMmdBuffer);
  b.Mem().Escrever32(kMediaData + kOffMidiaPData, kBuffer);
  b.Mem().Escrever32(kMediaData + kOffMidiaDwSize, 85261);  // impar, e o normal num fluxo
  EXPECT_EQ(b.DefinirDados(), kAeeSucesso);
  EXPECT_EQ(b.OMedia().EstadoDe(po), kMmEstadoPronto);
  EXPECT_EQ(b.Mem().Ler32(po + kOffObjAmostrasTotal), 0u);
  EXPECT_GE(b.OTraco().ContagemFaltas().count("IMedia::SetMediaParm(MMD_BUFFER)"), 1u);
}

TEST(Media, OBufferParDeUmDescodificadorNAOSeTornaEmAmostras) {
  Bancada b;
  const std::uint32_t po = b.CriarMedia(0x01005502u /* AEECLSID_MEDIAMP3 */, kPponovo);
  b.ApontarParaObjeto(po);
  b.Mem().Escrever32(kMediaData + kOffMidiaClsData, kMmdBuffer);
  b.Mem().Escrever32(kMediaData + kOffMidiaPData, kBuffer);
  b.Mem().Escrever32(kMediaData + kOffMidiaDwSize, 150352);  // par, e era aqui que se inventava
  EXPECT_EQ(b.DefinirDados(), kAeeSucesso);
  EXPECT_EQ(b.OMedia().EstadoDe(po), kMmEstadoPronto);
  EXPECT_EQ(b.Mem().Ler32(po + kOffObjAmostrasTotal), 0u);
  EXPECT_GE(b.OTraco().ContagemFaltas().count("IMedia::SetMediaParm(MMD_BUFFER)"), 1u);
}

TEST(Media, OBufferImparDePCMERecusado) {
  // No PCM o numero impar CONTINUA a ser defeito: nao ha amostras de 16 bits a
  // sair de um numero impar de bytes, e arredondar seria inventar som.
  Bancada b;
  const std::uint32_t po = b.CriarMedia(kClsMediaPcm, kPponovo);
  b.ApontarParaObjeto(po);
  b.Mem().Escrever32(kMediaData + kOffMidiaClsData, kMmdBuffer);
  b.Mem().Escrever32(kMediaData + kOffMidiaPData, kBuffer);
  b.Mem().Escrever32(kMediaData + kOffMidiaDwSize, 7);  // impar
  EXPECT_EQ(b.DefinirDados(), kAeeParametroErrado);
  EXPECT_EQ(b.OMedia().EstadoDe(po), kMmEstadoOcioso);
}

static std::string RaizDosModsDaMidia() {
  if (const char* env = std::getenv("ZB2_MODS")) {
    if (*env != '\0') return env;
  }
  return "/media/rafaelfrequiao/8C5F-19E51/zeebo/ROMs/debug_nand/mod";
}

TEST(Media, ONomeDeFicheiroExistenteGuardaOFluxoESemDecodificador) {
  // O ficheiro REAL do `gof`, com SKIP se a midia nao estiver nesta maquina
  // (precedente `Aez.VfsServeOsCaminhosQueOGuestPede`).
  Vfs vfs;
  vfs.Registar(RaizDosModsDaMidia() + "/277380");
  if (!vfs.Existe("GalaxyOnFire1_Won.mp3")) {
    GTEST_SKIP() << "sem o MP3 do gof nesta maquina -- PULAR";
  }
  std::error_code ec;
  const std::uint64_t tamanho_real =
      std::filesystem::file_size(RaizDosModsDaMidia() + "/277380/GalaxyOnFire1_Won.mp3", ec);
  ASSERT_FALSE(ec) << "o ficheiro existe no VFS mas nao no disco";

  Bancada b(&vfs);
  const std::uint32_t po = b.CriarMedia(kClasseMultimidia, kPponovo);
  b.ApontarParaObjeto(po);
  const std::string nome = "GalaxyOnFire1_Won.mp3";
  for (std::size_t k = 0; k < nome.size(); ++k) {
    b.Mem().Escrever8(kFicheiro + static_cast<std::uint32_t>(k),
                      static_cast<std::uint8_t>(nome[k]));
  }
  b.Mem().Escrever8(kFicheiro + static_cast<std::uint32_t>(nome.size()), 0);
  b.Mem().Escrever32(kMediaData + kOffMidiaClsData, kMmdNomeDeFicheiro);
  b.Mem().Escrever32(kMediaData + kOffMidiaPData, kFicheiro);
  b.Mem().Escrever32(kMediaData + kOffMidiaDwSize, 0);
  // MP3 nao vira PCM inventado: a moldura e contada para que Play/DONE usem
  // tempo virtual real. O fluxo permanece guardado para GetMediaParm futuro.
  EXPECT_EQ(b.DefinirDados(), kAeeSucesso);
  EXPECT_EQ(b.OMedia().EstadoDe(po), kMmEstadoPronto);
  EXPECT_EQ(b.OMedia().TamanhoDoFluxoGuardado(po), static_cast<std::uint32_t>(tamanho_real));
  EXPECT_GT(b.Mem().Ler32(po + kOffObjAmostrasTotal), 0u);
  const auto& faltas = b.OTraco().ContagemFaltas();
  EXPECT_EQ(faltas.count("IMedia::SetMediaParm(MMD_FILE_NAME)"), 0u);
}

TEST(Media, ONomeDeFicheiroERecusadoEmVozAltaEComOMotivo) {
  // Nao ha descodificador de audio nesta arvore. Aceitar o nome e nao tocar nada
  // seria o stub proibido; a recusa diz QUAL das duas causas e (o ficheiro nao
  // esta no VFS, ou esta e nao ha descodificador).
  Bancada b;
  const std::uint32_t po = b.CriarMedia(kClasseMultimidia, kPponovo);
  b.ApontarParaObjeto(po);
  const std::string nome = "sons/menu.wav";
  for (std::size_t k = 0; k < nome.size(); ++k) {
    b.Mem().Escrever8(kFicheiro + static_cast<std::uint32_t>(k),
                      static_cast<std::uint8_t>(nome[k]));
  }
  b.Mem().Escrever8(kFicheiro + static_cast<std::uint32_t>(nome.size()), 0);
  b.Mem().Escrever32(kMediaData + kOffMidiaClsData, kMmdNomeDeFicheiro);
  b.Mem().Escrever32(kMediaData + kOffMidiaPData, kFicheiro);
  b.Mem().Escrever32(kMediaData + kOffMidiaDwSize, 0);
  EXPECT_EQ(b.DefinirDados(), kAeeNaoSuportado);
  const auto& faltas = b.OTraco().ContagemFaltas();
  ASSERT_GE(faltas.count("IMedia::SetMediaParm(MMD_FILE_NAME)"), 1u);
  EXPECT_NE(b.OMedia().UltimoMotivoDeRecusa().find("menu.wav"), std::string::npos);
}

// ---------------------------------------------------------------------------
// OS AVISOS: O NUMERO. "O erro classico aqui e uma notificacao a mais ou a
// menos" -- por isso estes testes CONTAM.
// ---------------------------------------------------------------------------
namespace {
// Uma bancada pronta a tocar: objecto criado, callback registado, 200 amostras.
void Preparar(Bancada& b, std::uint32_t* po, std::size_t amostras = 200) {
  *po = b.CriarMedia(kClasseMultimidia, kPponovo);
  b.ApontarParaObjeto(*po);
  b.RegistrarNotify();
  b.PorPcm(Onda(amostras, 4000));
  ASSERT_EQ(b.DefinirDados(), kAeeSucesso);
}
}  // namespace

TEST(Media, PlayAteOFimAvisaUmaVezSoComDone) {
  Bancada b;
  std::uint32_t po = 0;
  Preparar(b, &po);
  b.Play();
  EXPECT_EQ(b.Avisos(), 0u);  // o Play em si NAO avisa: quem avisa e o FIM
  b.Avancar(200);
  EXPECT_EQ(b.Avisos(), 1u);
  // Os dois campos que o tratador REAL do `cnk2` le: +8 o nCmd (4 = MM_CMD_PLAY)
  // e +16 o nStatus (2 = MM_STATUS_DONE).
  EXPECT_EQ(b.UltimoCmd(), static_cast<std::uint32_t>(kMmCmdPlay));
  EXPECT_EQ(b.UltimoStatus(), static_cast<std::uint32_t>(kMmStatusDone));
  EXPECT_EQ(b.OMedia().EstadoDe(po), kMmEstadoPronto);
}

TEST(Media, DepoisDoFimNaoHaUmSegundoAviso) {
  // GUARDA CONTRA UMA NOTIFICACAO A MAIS. O estado muda para PRONTO antes de o
  // aviso nascer; sem isso, cada volta seguinte do laco veria a mesma midia
  // "acabada" e avisaria outra vez -- e um jogo que conta sons activos
  // descontaria o mesmo som varias vezes.
  Bancada b;
  std::uint32_t po = 0;
  Preparar(b, &po);
  b.Play();
  b.Avancar(200);
  b.Avancar(200);
  b.Avancar(1000);
  EXPECT_EQ(b.Avisos(), 1u);
  EXPECT_EQ(b.OMedia().AvisosEmitidos(), 1u);
}

TEST(Media, PlayDuranteReproducaoAvisaAbortEOProprioPedidoAvisaDone) {
  // Um segundo `Play` no mesmo objecto RECLAMA o canal: o pedido antigo nao
  // pode ficar sem aviso (seria um a menos), e o status dele e ABORT (3).
  Bancada b;
  std::uint32_t po = 0;
  Preparar(b, &po);
  b.Play();
  b.Avancar(50);
  b.Play();  // reclama
  EXPECT_EQ(b.Avisos(), 1u);
  EXPECT_EQ(b.UltimoCmd(), static_cast<std::uint32_t>(kMmCmdPlay));
  EXPECT_EQ(b.UltimoStatus(), static_cast<std::uint32_t>(kMmStatusAbort));
  b.Avancar(200);  // o segundo pedido chega ao fim
  EXPECT_EQ(b.Avisos(), 2u);
  EXPECT_EQ(b.UltimoStatus(), static_cast<std::uint32_t>(kMmStatusDone));
}

TEST(Media, StopSemNadaATocarNaoAvisa) {
  // GUARDA CONTRA UMA NOTIFICACAO A MAIS, no sitio onde ela nasceria: um
  // `Stop` sem reproducao em curso.
  Bancada b;
  std::uint32_t po = 0;
  Preparar(b, &po);
  b.Stop();
  EXPECT_EQ(b.Avisos(), 0u);
  EXPECT_EQ(b.OMedia().AvisosEmitidos(), 0u);
}

TEST(Media, StopDuranteReproducaoAvisaDoneUmaVez) {
  Bancada b;
  std::uint32_t po = 0;
  Preparar(b, &po);
  b.Play();
  b.Avancar(50);
  b.Stop();
  EXPECT_EQ(b.Avisos(), 1u);
  EXPECT_EQ(b.UltimoCmd(), static_cast<std::uint32_t>(kMmCmdPlay));
  EXPECT_EQ(b.UltimoStatus(), static_cast<std::uint32_t>(kMmStatusDone));
  // E um Stop repetido nao avisa outra vez.
  b.Stop();
  EXPECT_EQ(b.Avisos(), 1u);
  EXPECT_EQ(b.OMedia().EstadoDe(po), kMmEstadoPronto);
}

TEST(Media, UmAvisoNuncaEEntregueComOEnderecoDeOutroObjeto) {
  // O DEFEITO: o aviso leva o ENDERECO do objecto, e o endereco de um objecto
  // soltado VOLTA a ser entregue a outro. O jogo correla o aviso pelo
  // `IMedia *` que vem dentro dele -- e o que o SDK diz (`AEEIMedia.h`,
  // "Callback Events": "You can correlate using either the IMedia pointer or
  // class ID returned in the callback data") --, logo um aviso que chegue
  // depois de o endereco mudar de dono e lido como sendo do objecto NOVO: o
  // `DONE` de um som passava a ser o `DONE` de outro som.
  //
  // A arvore antiga ja validava isto por IDENTIDADE (`MediaHle::Tick` compara
  // `generation`, `notify_fn` e `notify_user` antes de entregar, e descarta o
  // aviso quando o objecto "sumiu/reutilizado"); aqui entregava-se sem olhar.
  Bancada b;
  std::uint32_t po = 0;
  Preparar(b, &po);
  b.Play();
  b.AvancarSemEntregar(200);  // o DONE nasce e fica em fila
  ASSERT_EQ(b.OMedia().AvisosPendentes(), 1u);
  const std::uint32_t avisos_do_guest = b.Avisos();
  const std::uint32_t emitidos = static_cast<std::uint32_t>(b.OMedia().AvisosEmitidos());

  // O guest solta o objecto ANTES de o aviso ser entregue (slot 1 da IBase, que
  // e do MOTOR), e cria outro logo a seguir.
  b.Soltar(po);
  const std::uint32_t outro = b.CriarMedia(kClasseMultimidia, kPponovo);
  EXPECT_EQ(outro, po) << "o endereco volta a ser entregue (comportamento medido)";
  b.ApontarParaObjeto(outro);

  // A ENTREGA: o aviso e DESCARTADO, e o guest NAO e chamado.
  EXPECT_TRUE(b.OMedia().EntregarAviso(b.Cpu(), kSentinela, 20000));
  EXPECT_EQ(b.Avisos(), avisos_do_guest)
      << "o guest recebeu um aviso com o endereco de OUTRO objecto";
  EXPECT_EQ(b.OMedia().AvisosDescartados(), 1u);
  EXPECT_EQ(b.OMedia().AvisosEntregues(), 0u);
  EXPECT_EQ(b.OMedia().AvisosPendentes(), 0u) << "o aviso sai da fila de qualquer maneira";
  // A CONTA FECHA: nenhum aviso desaparece sem ser contado.
  EXPECT_EQ(b.OMedia().AvisosEmitidos(), emitidos);
  EXPECT_EQ(b.OMedia().AvisosEmitidos(),
            b.OMedia().AvisosEntregues() + b.OMedia().AvisosNaoEntregues() +
                b.OMedia().AvisosDescartados());
  // E a recusa tem NOME e motivo (P2): um aviso que desaparece em silencio e
  // indistinguivel de um aviso que nunca nasceu.
  EXPECT_GE(b.OTraco().ContagemFaltas().count("IMedia::EntregarAviso"), 1u);
  EXPECT_NE(b.OMedia().UltimoMotivoDeRecusa().find("serie"), std::string::npos)
      << b.OMedia().UltimoMotivoDeRecusa();
  EXPECT_NE(b.OMedia().UltimoMotivoDeRecusa().find("descartado"), std::string::npos)
      << b.OMedia().UltimoMotivoDeRecusa();
}

TEST(Media, OReleaseNaoApagaUmAvisoJaNascido) {
  // A OUTRA METADE DA REGRA, e a que impede a correccao ingenua ("descartar todo
  // o aviso cujo objecto foi soltado"). Medida no `zeebx`
  // (`src/machine/media.rs:556`): "o aviso NAO some com o `Release`: o tratador e
  // guardado quando o aviso nasce" -- o Zeebo F.C. Super League para o som, solta
  // o objecto e ESPERA o `DONE` desse som; descartado junto com o objecto, a
  // abertura parava na tela de aviso.
  //
  // Aqui o objecto e soltado e o endereco NAO tem outro dono, logo o aviso tem de
  // chegar -- e com o `pIMedia` do objecto que o gerou.
  Bancada b;
  std::uint32_t po = 0;
  Preparar(b, &po);
  b.Play();
  b.AvancarSemEntregar(200);
  ASSERT_EQ(b.OMedia().AvisosPendentes(), 1u);
  EXPECT_EQ(b.Avisos(), 0u);

  b.Soltar(po);  // o guest solta o objecto antes da entrega
  ASSERT_EQ(b.Mem().Ler32(po + kOffObjRefs), 0u);
  ASSERT_EQ(b.OMedia().AvisosPendentes(), 1u);  // a entrega foi adiada, nao perdida

  EXPECT_TRUE(b.OMedia().EntregarAviso(b.Cpu(), kSentinela, 20000));
  EXPECT_EQ(b.Avisos(), 1u) << "o aviso nascido antes do `Release` tem de chegar";
  EXPECT_EQ(b.UltimoCmd(), static_cast<std::uint32_t>(kMmCmdPlay));
  EXPECT_EQ(b.UltimoStatus(), static_cast<std::uint32_t>(kMmStatusDone));
  EXPECT_EQ(b.UltimoPimidia(), po) << "e com o ponteiro do objecto que o gerou";
  EXPECT_EQ(b.OMedia().AvisosEntregues(), 1u);
  EXPECT_EQ(b.OMedia().AvisosDescartados(), 0u);
  EXPECT_EQ(b.OMedia().AvisosPendentes(), 0u);
}

TEST(Media, CadaPedidoAceiteDaExactamenteUmAviso) {
  // A propriedade, medida em 20 voltas: 20 pedidos aceites (Play + fim) e 20
  // avisos. Um aviso a mais ou a menos quebra a conta.
  Bancada b;
  std::uint32_t po = 0;
  Preparar(b, &po);
  for (int k = 0; k < 20; ++k) {
    b.Play();
    b.Avancar(200);
  }
  EXPECT_EQ(b.Avisos(), 20u);
  EXPECT_EQ(b.OMedia().AvisosEmitidos(), 20u);
  EXPECT_EQ(b.OMedia().AvisosPendentes(), 0u);
}

TEST(Media, UmaMidiaEmRepeticaoInfinitanNaoAvisaNunca) {
  // `MM_PARM_PLAY_REPEAT` = 0 e "toca para sempre" (AEEIMedia.h). Um aviso DONE
  // aqui seria o erro que o `cnk2` mediu: o jogo so toca a musica da pista
  // quando a conta de sons activos zera, e a musica de menu e em laco.
  Bancada b;
  std::uint32_t po = 0;
  Preparar(b, &po);
  b.SetParm(kMmParmPlayRepeat, 0);
  b.Play();
  for (int k = 0; k < 10; ++k) b.Avancar(200);
  EXPECT_EQ(b.Avisos(), 0u);
  EXPECT_EQ(b.OMedia().EstadoDe(po), kMmEstadoTocando);
}

TEST(Media, SemCallbackRegistadoNaoHaAvisoAEntregar) {
  // Registar o callback e OPCIONAL no SDK. Sem callback nao ha entrega, e isso
  // fica escrito no traco -- nao e um aviso perdido.
  Bancada b;
  const std::uint32_t po = b.CriarMedia(kClasseMultimidia, kPponovo);
  b.ApontarParaObjeto(po);
  b.PorPcm(Onda(64, 1000));
  ASSERT_EQ(b.DefinirDados(), kAeeSucesso);
  b.Play();
  b.Avancar(64);
  EXPECT_EQ(b.OMedia().AvisosEmitidos(), 0u);
  EXPECT_GE(b.OTraco().ContagemFaltas().count("IMedia::Record"), 0u);  // traco vivo
}

TEST(Media, OMisturadorContaAsAmostrasQueAMidiaEntrega) {
  // O criterio da etapa 5, medido: "o misturador reporta amostras nao nulas".
  Bancada b;
  std::uint32_t po = 0;
  Preparar(b, &po, 300);
  b.Play();
  b.Avancar(300);
  const auto& m = b.OMisturador().MedidaAcumulada();
  EXPECT_EQ(m.amostras_recebidas, 300u);
  EXPECT_EQ(m.amostras_nao_nulas, 300u);
  EXPECT_EQ(m.pico, 4000);
  EXPECT_EQ(b.Avisos(), 1u);
}

TEST(Media, MudoNaoProduzPicoMasContinuaAContarAsAmostras) {
  Bancada b;
  std::uint32_t po = 0;
  Preparar(b, &po, 100);
  b.SetParm(kMmParmMute, 1);
  b.Play();
  b.Avancar(100);
  const auto& m = b.OMisturador().MedidaAcumulada();
  EXPECT_EQ(m.amostras_recebidas, 100u);
  EXPECT_EQ(m.amostras_nao_nulas, 100u);
  EXPECT_EQ(m.pico, 0);
}

TEST(Media, OVolumeChegaAoMisturadorPeloParametroDoSDK) {
  Bancada b;
  std::uint32_t po = 0;
  Preparar(b, &po, 40);
  b.SetParm(kMmParmVolume, 50);
  b.Play();
  b.Avancar(40);
  EXPECT_EQ(b.OMisturador().MedidaAcumulada().pico, 2000);  // 4000 a metade
}

TEST(Media, GetTotalTimeAvisaComOTempoEmMilissegundos) {
  // O SDK entrega o tempo total PELO AVISO (`pCmdData` = uint32 em ms). O tempo
  // sai da taxa DECLARADA de 22050 Hz -- nao ha descodificador de WAV nesta
  // arvore, e o numero diz isso no codigo.
  Bancada b;
  std::uint32_t po = 0;
  Preparar(b, &po, 22050);
  EXPECT_EQ(b.Chamar(brew_slots::kMedia_GetTotalTime, po), kAeeSucesso);
  EXPECT_EQ(b.Avisos(), 1u);
  EXPECT_EQ(b.UltimoCmd(), static_cast<std::uint32_t>(kMmCmdGetTotalTime));
  EXPECT_EQ(b.UltimoStatus(), static_cast<std::uint32_t>(kMmStatusDone));
  EXPECT_EQ(b.UltimoDwSize(), 4u);
  EXPECT_EQ(b.Mem().Ler32(b.UltimoPcmdData()), 1000u);  // 22050 amostras a 22050 Hz
}

TEST(Media, OSeekDeTempoFuncionaEODeQuadrosRecusa) {
  Bancada b;
  std::uint32_t po = 0;
  Preparar(b, &po, 22050);
  // 500 ms a 22050 Hz = 11025 amostras.
  EXPECT_EQ(b.Chamar(brew_slots::kMedia_Seek, po, kMmSeekModoTempo | kMmSeekInicio, 500),
            kAeeSucesso);
  EXPECT_EQ(b.Mem().Ler32(po + kOffObjPosicao), 11025u);
  // Modalidade de QUADROS: nao ha video, logo recusa.
  EXPECT_EQ(b.Chamar(brew_slots::kMedia_Seek, po, kMmSeekModoQuadro | kMmSeekInicio, 3),
            static_cast<std::uint32_t>(kAeeNaoSuportado));
  // E um alvo depois do fim tambem e recusado, em vez de silenciosamente travado.
  EXPECT_EQ(b.Chamar(brew_slots::kMedia_Seek, po, kMmSeekModoTempo | kMmSeekInicio, 999999),
            static_cast<std::uint32_t>(kAeeParametroErrado));
}

TEST(Media, OQueryInterfaceDevolveOMesmoObjetoSoParaOAEEIIDDoMedia) {
  Bancada b;
  const std::uint32_t po = b.CriarMedia(kClasseMultimidia, kPponovo);
  b.ApontarParaObjeto(po);
  EXPECT_EQ(b.Chamar(2 /*QueryInterface*/, po, kIidMedia, kSaida), kAeeSucesso);
  EXPECT_EQ(b.Mem().Ler32(kSaida), po);
  EXPECT_EQ(b.Mem().Ler32(po + kOffObjRefs), 2u);  // o SDK incrementa no sucesso
  EXPECT_EQ(b.Chamar(2 /*QueryInterface*/, po, 0x01001001u, kSaida),
            static_cast<std::uint32_t>(kAeeClasseNaoSuportada));
  EXPECT_EQ(b.Mem().Ler32(kSaida), 0u);
}

TEST(Media, GetStateDizOTamanhoEAEstadoEPausaERetoma) {
  Bancada b;
  std::uint32_t po = 0;
  Preparar(b, &po);
  EXPECT_EQ(b.Chamar(brew_slots::kMedia_GetState, po, kSaida), kMmEstadoPronto);
  EXPECT_EQ(b.Mem().Ler32(kSaida), 0u);  // pbStateChanging = falso
  b.Play();
  EXPECT_EQ(b.Chamar(brew_slots::kMedia_GetState, po, kSaida), kMmEstadoTocando);
  b.Avancar(10);
  EXPECT_EQ(b.Chamar(brew_slots::kMedia_Pause, po), kAeeSucesso);
  EXPECT_EQ(b.OMedia().EstadoDe(po), kMmEstadoPausado);
  const auto antes = b.OMisturador().MedidaAcumulada().amostras_recebidas;
  b.Avancar(100);  // em pausa nao consome
  EXPECT_EQ(b.OMisturador().MedidaAcumulada().amostras_recebidas, antes);
  EXPECT_EQ(b.Chamar(brew_slots::kMedia_Resume, po), kAeeSucesso);
  EXPECT_EQ(b.OMedia().EstadoDe(po), kMmEstadoTocando);
  // Pausa fora de reproducao recusa.
  b.Stop();
  EXPECT_EQ(b.Chamar(brew_slots::kMedia_Pause, po), static_cast<std::uint32_t>(kAeeEstadoErrado));
}

TEST(Media, OsParametrosGuardadosFicamEscritosNoTraco) {
  // Aceitar e guardar sem aplicar NAO pode ser silencioso (P2). Cada aceitacao
  // deixa um evento com o nome e o valor do parametro.
  Bancada b;
  const std::uint32_t po = b.CriarMedia(kClasseMultimidia, kPponovo);
  b.ApontarParaObjeto(po);
  b.SetParm(kMmParmAudioPath, kMmCaminhoLocal);
  b.SetParm(kMmParmPan, 64);
  b.SetParm(kMmParmNotes, 8);
  EXPECT_GE(b.Eventos().QuantosComNome("IMEDIA_PARM_GUARDADO"), 3u);
}



// ---------------------------------------------------------------------------
// A ENTREGA DO AVISO AO GUEST: a reposicao dos registradores.
//
// O pai do projeto pediu esta classe de defeito explicitamente: "guardar e repor
// os 16 registradores e o CPSR a volta do callback". Nao havia teste nenhum para
// ela nesta arvore, e o defeito e invisivel quando nao existe: o callback do
// jogo corre, escreve em r0-r12, e o quadro de QUEM ESTAVA A CORRER fica
// estragado sem sintoma no ponto da chamada.
// ---------------------------------------------------------------------------
TEST(Media, AEntregaGuardaERepoeOsDezasseisRegistradoresEOsCpsr) {
  Bancada b;
  std::uint32_t po = 0;
  Preparar(b, &po);
  b.Chamar(brew_slots::kMedia_RegisterNotify, po, kDestruidor, kDados);
  b.Play();
  b.AvancarSemEntregar(200);  // a midia acaba: o aviso nasce e fica em fila
  ASSERT_EQ(b.OMedia().AvisosPendentes(), 1u);

  // O estado do guest no momento da entrega: registradores VIVOS. O r11 leva o
  // endereco do marcador onde o callback escreve o `pUser` que recebeu.
  for (std::uint32_t r = 0; r < 13; ++r) b.Cpu().Set(static_cast<int>(r), 0xAAAA0000u + r);
  b.Cpu().Set(11, kDados + 20);
  b.Mem().Escrever32(kDados + 20, 0);
  b.Cpu().Set(13, 0x00130000u);  // SP
  b.Cpu().Set(14, 0x00140000u);  // LR
  constexpr std::uint32_t kPcDoLaco = 0x0012345Cu;
  b.Cpu().Set(kPC, kPcDoLaco);
  // O CPSR com o Z LIMPO, para o `movs` do callback ter o que mudar.
  const std::uint32_t cpsr = b.Cpu().Cpsr() & ~Cpsr::kZ;
  b.Cpu().SetCpsr(cpsr);
  // A FOTOGRAFIA do estado: e com isto que a reposicao e comparada, e nao com
  // valores escritos a mao no EXPECT (o r11, por exemplo, nao vale o mesmo que
  // os outros -- foi reescrito pelo teste).
  std::array<std::uint32_t, 16> esperados{};
  for (int r = 0; r < 16; ++r) esperados[static_cast<std::size_t>(r)] = b.Cpu().Get(r);

  EXPECT_TRUE(b.OMedia().EntregarAviso(b.Cpu(), kSentinela, 10000));
  EXPECT_EQ(b.OMedia().AvisosEntregues(), 1u);
  for (int r = 0; r < 16; ++r) {
    EXPECT_EQ(b.Cpu().Get(r), esperados[static_cast<std::size_t>(r)]) << "r" << r;
  }
  EXPECT_EQ(b.Cpu().Get(kPC), kPcDoLaco) << "o PC tem de ser REPOSTO pelo modulo";
  EXPECT_EQ(b.Cpu().Cpsr(), cpsr) << "o CPSR tem de ser reposto pelo modulo";
  // E o callback correu MESMO: ele escreveu o `pUser` no marcador antes de
  // estragar os registradores -- o que prova tambem que o argumento chegou no r0.
  EXPECT_EQ(b.Mem().Ler32(kDados + 20), kDados);
}

TEST(Media, UmCallbackQueNaoVoltaERegistadoENaoDaSucesso) {
  // P2: o caminho que nao concluiu RECUSA e REGISTA. Um callback que gira para
  // sempre nao pode ser dado como entregue, e o emulador nao pode ficar preso
  // nele -- o limite de passos existe para isso.
  Bancada b;
  std::uint32_t po = 0;
  Preparar(b, &po);
  b.Chamar(brew_slots::kMedia_RegisterNotify, po, kPreso, kDados);
  b.Play();
  b.AvancarSemEntregar(200);
  ASSERT_EQ(b.OMedia().AvisosPendentes(), 1u);
  const std::uint32_t pc_antes = 0x0012345Cu;
  b.Cpu().Set(kPC, pc_antes);

  EXPECT_TRUE(b.OMedia().EntregarAviso(b.Cpu(), kSentinela, 500));
  EXPECT_EQ(b.OMedia().AvisosEntregues(), 0u);
  EXPECT_EQ(b.OMedia().AvisosNaoEntregues(), 1u);
  EXPECT_GE(b.OTraco().ContagemFaltas().count("IMedia::EntregarAviso"), 1u);
  EXPECT_NE(b.OMedia().UltimoMotivoDeRecusa().find("nao voltou"), std::string::npos);
  // O PC voltou a ser o do laco: nem preso no callback, nem na sentinela.
  EXPECT_EQ(b.Cpu().Get(kPC), pc_antes);
}

TEST(Media, CadaEntregaLevaUmAvisoEMaisNenhum) {
  // COM DOIS AVISOS EM FILA, e nao um de cada vez: e assim que a propriedade
  // "uma entrega = um callback" e testada de verdade. Com a fila a esvaziar-se
  // de uma vez, o jogo perde avisos que contava -- o mesmo defeito de contagem
  // (uma a mais ou a menos) que a etapa existe para fechar.
  Bancada b;
  b.PorPcm(Onda(200, 3000));
  const std::uint32_t primeiro = b.CriarMedia(kClasseMultimidia, kPponovo);
  b.Chamar(brew_slots::kMedia_RegisterNotify, primeiro, kTratador, kDados);
  b.Chamar(brew_slots::kMedia_SetMediaParm, primeiro, kMmParmMediaData, kMediaData, 0);
  const std::uint32_t segundo = b.CriarMedia(kClasseMultimidia, kPponovo);
  ASSERT_NE(segundo, primeiro);
  b.Chamar(brew_slots::kMedia_RegisterNotify, segundo, kTratador, kDados);
  b.Chamar(brew_slots::kMedia_SetMediaParm, segundo, kMmParmMediaData, kMediaData, 0);

  b.Chamar(brew_slots::kMedia_Play, primeiro);
  b.Chamar(brew_slots::kMedia_Play, segundo);
  b.AvancarSemEntregar(200);  // as duas chegam ao fim: DOIS avisos em fila
  ASSERT_EQ(b.OMedia().AvisosPendentes(), 2u);

  const std::uint32_t antes = b.Mem().Ler32(kDados + kOffUserContador);
  EXPECT_TRUE(b.OMedia().EntregarAviso(b.Cpu(), kSentinela, 20000));
  EXPECT_EQ(b.OMedia().AvisosPendentes(), 1u) << "uma entrega leva UM aviso";
  EXPECT_EQ(b.Mem().Ler32(kDados + kOffUserContador), antes + 1);
  EXPECT_TRUE(b.OMedia().EntregarAviso(b.Cpu(), kSentinela, 20000));
  EXPECT_EQ(b.OMedia().AvisosPendentes(), 0u);
  EXPECT_EQ(b.Mem().Ler32(kDados + kOffUserContador), antes + 2);
  EXPECT_EQ(b.OMedia().AvisosEntregues(), 2u);
  EXPECT_FALSE(b.OMedia().EntregarAviso(b.Cpu(), kSentinela, 200000));
  EXPECT_EQ(b.OMedia().AvisosEntregues(), 2u) << "sem aviso na fila nao ha entrega";
}


// ---------------------------------------------------------------------------
// O IMEDIAUTIL (`AEECLSID_MEDIAUTIL`, 0x0100550d): a SEGUNDA interface da
// familia de midia, e a que este emulador servia com a vtable ERRADA.
//
// A MEDICAO QUE ISTO GUARDA (`/tmp/pesquisa/setup.md`): o `allstarcards` cria o
// `AEECLSID_MEDIAUTIL` pelo slot 2 do `IShell` e chama o SLOT 3 do objecto que
// recebe (`0000f394  ldr r3,[r0,#12]` / `0000f39c  blx r3`) com o `ppm` no r2
// (`0000f38c  add r2,r4,#28`). O nosso emulador dava a esse CLSID a vtable do
// `IMedia`, cujo slot 3 e o `RegisterNotify`: respondia SUCCESS, NAO escrevia o
// `ppm`, o jogo ficava com o ponteiro a ZERO e chamava pela PRIMEIRA PALAVRA DO
// PROPRIO MODULO -- 53x em pc=0xf3f4 + 53x em pc=0xf428, 2 recusas de CPU por
// quadro, 106 por corrida.
// ---------------------------------------------------------------------------
namespace {
constexpr std::uint32_t kPpm = 0x00100B00u;      // o `IMedia **ppm` de saida
constexpr std::uint32_t kCmi = 0x00100C00u;      // uma `AEEMediaCreateInfo`
constexpr std::uint32_t kListaEx = 0x00100C80u;  // a `pmdList` dela
}

TEST(Media, OMediaUtilTemOsSeisSlotsDoSDKEOCreateMediaExNaoESlot4) {
  // A TABELA VEM DO CABECALHO, e o `EncodeMedia` esta NO MEIO dela:
  //
  //   AEEINTERFACE(IMediaUtil) {                       // sdk/inc/AEEMediaUtil.h
  //     INHERIT_IQueryInterface(IMediaUtil);
  //     int (*CreateMedia)(IMediaUtil*, AEEMediaData*, IMedia**);             // 3
  //     int (*EncodeMedia)(IMediaUtil*, AEEMediaEncodeResult*, AEECLSID,
  //                        AEEMediaEncodeInfo*, AEECallback*);                // 4
  //     int (*CreateMediaEx)(IMediaUtil*, AEEMediaCreateInfo*, IMedia**);     // 5
  //   }
  //
  // ESTE TESTE EXISTE POR CAUSA DE UMA DESCRICAO ERRADA: um pedido de trabalho
  // desta sessao dizia "4 slots: IQI + CreateMedia + CreateMediaEx, o Ex no 4".
  // Se alguem arrumar a tabela para bater com essa descricao, o slot 4 passa a
  // responder `CreateMediaEx` a um jogo que pediu o `EncodeMedia` -- e o NOME no
  // registo de faltas mente sobre o que o jogo pediu.
  EXPECT_EQ(kClsMediaUtil, 0x0100550du);
  EXPECT_EQ(kSlotsDoMediaUtil, 6u);
  EXPECT_EQ(kMediaUtil_CreateMedia, 3u);
  EXPECT_EQ(kMediaUtil_EncodeMedia, 4u);
  EXPECT_EQ(kMediaUtil_CreateMediaEx, 5u);
  EXPECT_EQ(kQuantosSlotsDoMediaUtilDeclarados, 6u);
  EXPECT_STREQ(kTabelaDeSlotsDoMediaUtil[2].nome, "IMediaUtil::QueryInterface");
  EXPECT_STREQ(kTabelaDeSlotsDoMediaUtil[3].nome, "IMediaUtil::CreateMedia");
  EXPECT_STREQ(kTabelaDeSlotsDoMediaUtil[4].nome, "IMediaUtil::EncodeMedia");
  EXPECT_STREQ(kTabelaDeSlotsDoMediaUtil[5].nome, "IMediaUtil::CreateMediaEx");
  EXPECT_TRUE(ConferirTabelaDeSlots(kTabelaDeSlotsDoMediaUtil,
                                    kQuantosSlotsDoMediaUtilDeclarados, kSlotsDoMediaUtil)
                  .ok);
  // A CONFUSAO QUE CUSTOU AS 106 RECUSAS, escrita: o slot 3 das duas interfaces
  // sao metodos DIFERENTES.
  EXPECT_STREQ(kTabelaDeSlotsDoMedia[3].nome, "IMedia::RegisterNotify");
  // E o valor de slot 3 do IMedia esta fixado no cabecalho gerado do SDK.
  EXPECT_EQ(brew_slots::kMedia_RegisterNotify, 3u);
}

TEST(Media, InstalarEscreveOsSeisSlotsDoMediaUtil) {
  Bancada b;
  ASSERT_TRUE(b.Instalacao().ok) << b.Instalacao().motivo;
  const std::uint32_t vt = b.AsSaidas().Endereco(kVtableDoMediaUtil);
  EXPECT_EQ(b.Mem().Ler32(vt + 0), b.AsSaidas().Endereco(3));  // IBase::AddRef, do motor
  EXPECT_EQ(b.Mem().Ler32(vt + 4), b.AsSaidas().Endereco(4));  // IBase::Release, do motor
  for (std::uint32_t s = 2; s < kSlotsDoMediaUtil; ++s) {
    EXPECT_EQ(b.Mem().Ler32(vt + s * 4), b.AsSaidas().Endereco(kBaseDoMediaUtil + s))
        << "slot " << s;
  }
  // AS DUAS VTBALES SAO PAGINAS DIFERENTES: se fossem a mesma, os dois objectos
  // teriam os mesmos slots e a distincao nao existiria.
  EXPECT_NE(b.AsSaidas().Endereco(kVtableDoMedia), b.AsSaidas().Endereco(kVtableDoMediaUtil));
}

TEST(Media, OMediaUtilNasceComAVtableDoMediaUtilENaoComADoIMedia) {
  // O DEFEITO MEDIDO, fixado: o `Criar` dava ao `AEECLSID_MEDIAUTIL` a vtable do
  // `IMedia` (`kVtableDoMedia`), e o slot 3 dela e o `RegisterNotify`.
  Bancada b;
  const std::uint32_t util = b.CriarMedia(kClsMediaUtil, kPponovo);
  ASSERT_EQ(util, kObjMediaBase);
  EXPECT_EQ(b.Mem().Ler32(util + kOffObjVtable), b.AsSaidas().Endereco(kVtableDoMediaUtil));
  EXPECT_NE(b.Mem().Ler32(util + kOffObjVtable), b.AsSaidas().Endereco(kVtableDoMedia));
  EXPECT_EQ(b.Mem().Ler32(util + kOffObjRefs), 1u);  // ROPI: [0] vtable, [4] referencias
  // O SLOT 3 DELE E O `CreateMedia`: le-se a palavra que o trampolim do guest vai
  // buscar (`[vtable+12]`), e nao um id interno.
  const std::uint32_t slot3 = b.Mem().Ler32(b.AsSaidas().Endereco(kVtableDoMediaUtil) + 12);
  EXPECT_EQ(slot3, b.AsSaidas().Endereco(kBaseDoMediaUtil + kMediaUtil_CreateMedia));
  EXPECT_NE(slot3, b.AsSaidas().Endereco(kBaseDoMedia + brew_slots::kMedia_RegisterNotify));
  EXPECT_EQ(b.OMedia().ClasseDe(util), static_cast<std::int32_t>(kClsMediaUtil));
  EXPECT_EQ(b.OMedia().ObjetosDeMediaUtilVivos(), 1u);
}

TEST(Media, OCreateMediaDoMediaUtilEscreveOPpmEOCriaComOsDados) {
  // A CHAMADA DO `allstarcards`, reduzida ao essencial: cria o MediaUtil, chama o
  // SLOT 3 PELA VTABLE (o trampolim le `[po]` e `[vtable+12]` do proprio guest)
  // com o `AEEMediaData` no r1 e o `ppm` no r2, e exige as tres coisas que o jogo
  // exige: o retorno SUCCESS, o `ppm` ESCRITO e NAO nulo, e um objecto que
  // responde como `IMedia` com os dados ja lidos.
  Bancada b;
  const std::uint32_t util = b.CriarMedia(kClsMediaUtil, kPponovo);
  ASSERT_EQ(util, kObjMediaBase);
  b.PorPcm(Onda(256, 3000));
  b.Mem().Escrever32(kPpm, 0xDEADBEEFu);  // lixo: quem escreve o `ppm` e a chamada

  const std::uint32_t r = b.Chamar(kMediaUtil_CreateMedia, util, kMediaData, kPpm);
  EXPECT_EQ(r, kAeeSucesso);
  const std::uint32_t po = b.Mem().Ler32(kPpm);
  EXPECT_NE(po, 0u) << "o `ppm` ficou a ZERO: e este o defeito das 106 recusas";
  EXPECT_NE(po, 0xDEADBEEFu);
  // O objecto NOVO, no lugar seguinte do conjunto (o MediaUtil ocupa o primeiro).
  ASSERT_EQ(po, kObjMediaBase + kPassoDoObjetoMedia);
  EXPECT_EQ(b.Mem().Ler32(po + kOffObjVtable), b.AsSaidas().Endereco(kVtableDoMedia));
  // O `SetMediaData` poe o objecto em Pronto, e as amostras ficam contadas no
  // cabecalho do objecto -- que e o que o `GetState`/`GetTotalTime` leem.
  EXPECT_EQ(b.OMedia().EstadoDe(po), kMmEstadoPronto);
  EXPECT_EQ(b.Mem().Ler32(po + kOffObjAmostrasTotal), 256u);
  EXPECT_EQ(b.OMedia().ObjetosDeMediaUtilVivos(), 1u);
  EXPECT_EQ(b.OMedia().ObjetosVivos(), 2u);
}

TEST(Media, OCreateMediaDoMediaUtilRecusaODadoQueNaoSabeLerEPoeOPpmAZero) {
  // A OUTRA METADE DA REGRA: quando nao se cria, o `ppm` fica a ZERO. Deixar la o
  // que estava, com o retorno a SUCCESS, foi exatamente o que fez o jogo tomar a
  // primeira palavra do modulo por vtable.
  Bancada b;
  const std::uint32_t util = b.CriarMedia(kClsMediaUtil, kPponovo);
  b.Mem().Escrever8(kFicheiro, 's');
  b.Mem().Escrever8(kFicheiro + 1, 'o');
  b.Mem().Escrever8(kFicheiro + 2, 'm');
  b.Mem().Escrever8(kFicheiro + 3, 0);
  b.Mem().Escrever32(kMediaData + kOffMidiaClsData, kMmdNomeDeFicheiro);
  b.Mem().Escrever32(kMediaData + kOffMidiaPData, kFicheiro);
  b.Mem().Escrever32(kMediaData + kOffMidiaDwSize, 0);
  b.Mem().Escrever32(kPpm, 0xDEADBEEFu);

  const std::uint32_t r = b.Chamar(kMediaUtil_CreateMedia, util, kMediaData, kPpm);
  EXPECT_NE(r, kAeeSucesso);
  EXPECT_EQ(b.Mem().Ler32(kPpm), 0u);
  // O LUGAR DO OBJECTO VOLTA AO CONJUNTO: so o MediaUtil fica vivo. Um lugar
  // perdido por cada recusa acabaria por esgotar os 1024 da regiao.
  EXPECT_EQ(b.OMedia().ObjetosVivos(), 1u);
  EXPECT_EQ(b.OMedia().ObjetosDeMediaUtilVivos(), 1u);
  // E a recusa NOMEIA a causa (o nome de ficheiro nao tem descodificador).
  EXPECT_NE(b.OMedia().UltimoMotivoDeRecusa().find("MMD_FILE_NAME"), std::string::npos)
      << b.OMedia().UltimoMotivoDeRecusa();
}

TEST(Media, OCreateMediaExLeAAEEMediaCreateInfo) {
  // `AEEMediaCreateInfo`: `dwStructSize`, `nState`, `nCount`, `pmdList`,
  // `pnCapsList` -- cinco campos de 32 bits. A doc do MESMO cabecalho escreve
  // `int16 nState/int16 nCount`; vale a struct, que e o que o SDK compila.
  //
  // A LISTA traz um `AEEMediaDataEx`, cujos TRES PRIMEIROS campos sao os de um
  // `AEEMediaData` (`AEEIMedia.h:287`), e por isso o mesmo leitor de dados os le.
  Bancada b;
  const std::uint32_t util = b.CriarMedia(kClsMediaUtil, kPponovo);
  b.PorPcm(Onda(64, 1000));
  b.Mem().Escrever32(kListaEx + kOffMidiaClsData, kMmdBuffer);
  b.Mem().Escrever32(kListaEx + kOffMidiaPData, kBuffer);
  b.Mem().Escrever32(kListaEx + kOffMidiaDwSize, 128u);
  b.Mem().Escrever32(kCmi + kOffCmiTamanho, kTamanhoDoCmi);
  b.Mem().Escrever32(kCmi + kOffCmiEstado, kMmEstadoPronto);
  b.Mem().Escrever32(kCmi + kOffCmiQuantos, 1u);
  b.Mem().Escrever32(kCmi + kOffCmiLista, kListaEx);
  b.Mem().Escrever32(kCmi + kOffCmiCaps, 0u);
  b.Mem().Escrever32(kPpm, 0u);

  EXPECT_EQ(b.Chamar(kMediaUtil_CreateMediaEx, util, kCmi, kPpm), kAeeSucesso);
  const std::uint32_t po = b.Mem().Ler32(kPpm);
  ASSERT_NE(po, 0u);
  EXPECT_EQ(b.OMedia().EstadoDe(po), kMmEstadoPronto);
  EXPECT_EQ(b.Mem().Ler32(po + kOffObjAmostrasTotal), 64u);
}

TEST(Media, OCreateMediaExDoEstadoOciosoNaoLeDadosEORestoRecusa) {
  Bancada b;
  const std::uint32_t util = b.CriarMedia(kClsMediaUtil, kPponovo);
  // MM_STATE_IDLE com a lista vazia: o SDK diz que assim NAO se chama o
  // `SetMediaData` ("the user needs to set the media data"), logo o objecto nasce
  // Ocioso e sem amostras.
  b.Mem().Escrever32(kCmi + kOffCmiTamanho, kTamanhoDoCmi);
  b.Mem().Escrever32(kCmi + kOffCmiEstado, kMmEstadoOcioso);
  b.Mem().Escrever32(kCmi + kOffCmiQuantos, 0u);
  b.Mem().Escrever32(kCmi + kOffCmiLista, 0u);
  b.Mem().Escrever32(kPpm, 0u);
  EXPECT_EQ(b.Chamar(kMediaUtil_CreateMediaEx, util, kCmi, kPpm), kAeeSucesso);
  const std::uint32_t po = b.Mem().Ler32(kPpm);
  ASSERT_NE(po, 0u);
  EXPECT_EQ(b.OMedia().EstadoDe(po), kMmEstadoOcioso);
  EXPECT_EQ(b.Mem().Ler32(po + kOffObjAmostrasTotal), 0u);

  const std::uint32_t vivos = b.OMedia().ObjetosVivos();
  // DUAS FONTES: RECUSA em voz alta. O SDK usa esta lista para JUNTAR midias, e
  // servir so a primeira seria dizer ao jogo que ele tocou o que nao tocou.
  b.Mem().Escrever32(kCmi + kOffCmiEstado, kMmEstadoPronto);
  b.Mem().Escrever32(kCmi + kOffCmiQuantos, 2u);
  b.Mem().Escrever32(kCmi + kOffCmiLista, kListaEx);
  EXPECT_EQ(b.Chamar(kMediaUtil_CreateMediaEx, util, kCmi, kPpm), kAeeNaoSuportado);
  EXPECT_NE(b.OMedia().UltimoMotivoDeRecusa().find("nCount=2"), std::string::npos)
      << b.OMedia().UltimoMotivoDeRecusa();

  // `pcmi` nulo: EBADPARM, e nada e criado.
  EXPECT_EQ(b.Chamar(kMediaUtil_CreateMediaEx, util, 0u, kPpm), kAeeParametroErrado);

  // `dwStructSize` que NAO cobre os campos que se vao ler: recusa, em vez de ler
  // memoria que o jogo nao declarou ter.
  b.Mem().Escrever32(kCmi + kOffCmiTamanho, 8u);
  b.Mem().Escrever32(kCmi + kOffCmiQuantos, 0u);
  b.Mem().Escrever32(kCmi + kOffCmiEstado, kMmEstadoOcioso);
  EXPECT_EQ(b.Chamar(kMediaUtil_CreateMediaEx, util, kCmi, kPpm), kAeeParametroErrado);

  // MM_STATE_READY sem fonte nenhuma: contradicao, recusa em voz alta.
  b.Mem().Escrever32(kCmi + kOffCmiTamanho, kTamanhoDoCmi);
  b.Mem().Escrever32(kCmi + kOffCmiEstado, kMmEstadoPronto);
  b.Mem().Escrever32(kCmi + kOffCmiQuantos, 0u);
  EXPECT_EQ(b.Chamar(kMediaUtil_CreateMediaEx, util, kCmi, kPpm), kAeeParametroErrado);
  EXPECT_EQ(b.OMedia().ObjetosVivos(), vivos);  // nenhuma criacao a mais
}

TEST(Media, OEncodeMediaDoMediaUtilRecusaEmVozAlta) {
  // NAO IMPLEMENTADO DIZ-SE COM O NOME (P2). Codificar pede um codificador, e
  // esta arvore nao tem descodificador nenhum -- aceitar o pedido e nao escrever
  // o `per` seria o MESMO defeito que esta frente corrige no `CreateMedia`.
  Bancada b;
  const std::uint32_t util = b.CriarMedia(kClsMediaUtil, kPponovo);
  EXPECT_EQ(b.Chamar(kMediaUtil_EncodeMedia, util, 0u, 0u, 0u), kAeeNaoSuportado);
  EXPECT_NE(b.OMedia().UltimoMotivoDeRecusa().find("EncodeMedia"), std::string::npos)
      << b.OMedia().UltimoMotivoDeRecusa();
  EXPECT_EQ(b.OMedia().ObjetosVivos(), 1u);
}

TEST(Media, OQueryInterfaceDoMediaUtilSoDaAPropriaClasse) {
  // O SDK: "If the value passed back is NULL, the interface or data that you
  // query are not available. / If an interface is retrieved, then this function
  // increments its reference count." Devolver este ponteiro para QUALQUER CLSID
  // seria o modo mudo; recusar tudo o que o SDK manda aceitar tambem esta errado.
  Bancada b;
  const std::uint32_t util = b.CriarMedia(kClsMediaUtil, kPponovo);
  const std::uint32_t antes = b.Mem().Ler32(util + kOffObjRefs);
  b.Mem().Escrever32(kSaida, 0u);
  EXPECT_EQ(b.Chamar(2, util, kClsMediaUtil, kSaida), kAeeSucesso);
  EXPECT_EQ(b.Mem().Ler32(kSaida), util);
  EXPECT_EQ(b.Mem().Ler32(util + kOffObjRefs), antes + 1);

  b.Mem().Escrever32(kSaida, 0xDEADBEEFu);
  EXPECT_EQ(b.Chamar(2, util, 0x01001001u, kSaida), kAeeClasseNaoSuportada);
  EXPECT_EQ(b.Mem().Ler32(kSaida), 0u);
  EXPECT_NE(b.OMedia().UltimoMotivoDeRecusa().find("0x01001001"), std::string::npos);
}

TEST(Media, OCreateMediaDoMediaUtilSemLugarNaRegiaoRespondeSemMemoria) {
  // O `MM_ENOMEDIAMEMORY` da doc do `IMEDIAUTIL_CreateMedia`. O VALOR NUMERICO
  // dele NAO esta nos cabecalhos que temos, logo devolve-se o `AEE_ENOMEMORY` do
  // `AEEStdErr.h` -- o que se pode citar -- e o motivo NOMEIA a regiao que
  // acabou. Inventar um numero para "parecer do SDK" seria pior do que isto.
  Bancada b;
  for (std::uint32_t k = 0; k + 1 < kMaxObjetosDeMidia; ++k) {
    ASSERT_EQ(b.CriarMedia(kClasseMultimidia, kPponovo), kObjMediaBase + k * kPassoDoObjetoMedia)
        << "criacao " << k;
  }
  const std::uint32_t util = b.CriarMedia(kClsMediaUtil, kPponovo);
  ASSERT_NE(util, 0u);
  ASSERT_EQ(b.OMedia().ObjetosVivos(), kMaxObjetosDeMidia);

  b.PorPcm(Onda(8, 100));
  b.Mem().Escrever32(kPpm, 0xDEADBEEFu);
  EXPECT_EQ(b.Chamar(kMediaUtil_CreateMedia, util, kMediaData, kPpm), kAeeSemMemoria);
  EXPECT_EQ(b.Mem().Ler32(kPpm), 0u);
}

TEST(Media, OMediaUtilNaoERespondidoPelaFaixaDoIMedia) {
  // GUARDA DEFENSIVA, e a razao de ela existir: era ESTE o defeito -- um
  // IMediaUtil servido pela faixa do IMedia, onde o slot 3 e o `RegisterNotify`.
  // Aqui chama-se o `Atender` com o ENDERECO do objecto E o indice da faixa do
  // IMedia (o pior caso) e exige-se RECUSA com o nome, e nunca um SUCCESS mudo.
  //
  // Este teste chama o `Atender` de proposito: o que esta a ser provado e o
  // comportamento do DESPACHO quando o `this` nao bate com a faixa -- a cablagem
  // e o que os testes acima provam, lendo a palavra da vtable.
  Bancada b;
  const std::uint32_t util = b.CriarMedia(kClsMediaUtil, kPponovo);
  const std::uint32_t vivos = b.OMedia().ObjetosVivos();
  b.Cpu().Set(kR0, util);
  EXPECT_TRUE(b.OMedia().Atender(kBaseDoMedia + brew_slots::kMedia_RegisterNotify, b.Cpu()));
  EXPECT_EQ(b.Cpu().Get(kR0), static_cast<std::uint32_t>(kAeeParametroErrado));
  EXPECT_EQ(b.OMedia().ObjetosVivos(), vivos);
  EXPECT_NE(b.OMedia().UltimoMotivoDeRecusa().find("IMediaUtil"), std::string::npos)
      << b.OMedia().UltimoMotivoDeRecusa();
}

}  // namespace
}  // namespace zb2::brew
