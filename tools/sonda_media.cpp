// zb2_sonda_media -- a SONDA DA ETAPA 5 (midia e audio).
//
// A sonda existe para duas perguntas, e as duas tem de ser respondidas com
// NUMEROS:
//
//   1. O MOTOR conhece o `AEECLSID_MEDIA`? Hoje NAO: o caso do
//      `IShell::CreateInstance` em `core/brew/despacho.cpp` tem uma lista de
//      classes conhecidas e a familia `AEECLSID_MULTIMEDIA` (0x01005500) nao
//      esta nela. Isto e medido AQUI, com o motor de verdade e um pedido de
//      midia escrito em codigo ARM, e nao por leitura do ficheiro.
//
//      >>> ESTE E O COMANDO QUE FICA VERMELHO. O codigo de saida e 1 enquanto o
//      motor recusar o CLSID de midia, e 0 quando o despacho aprender a chama-lo
//      (o remendo esta escrito no relatorio da etapa; a arvore partilhada nao foi
//      tocada).
//
//   2. A INTERFACE do IMedia, ja implementada em `core/brew/imedia`, faz o que
//      o SDK diz? A segunda metade mede o ciclo de vida completo e imprime os
//      contadores: slots instalados, objectos criados, avisos por pedido,
//      amostras que o misturador recebeu.
//
// Uso:  ./build/zb2_sonda_media
//       echo $?     # 1 = o motor ainda nao conhece o AEECLSID_MEDIA

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/audio/misturador.h"
#include "core/brew/ajudantes.h"
#include "core/brew/despacho.h"
#include "core/brew/imedia.h"
#include "core/brew/interface.h"
#include "core/brew/vfs.h"
#include "core/cpu/arm_interpreter.h"
#include "core/memoria/memoria.h"
#include "core/traco/traco.h"

using zb2::ArmInterpreter;
using zb2::kR0;
using zb2::kR1;
using zb2::kR2;
using zb2::kR3;
using zb2::kLR;
using zb2::kPC;
using zb2::Memoria;
using zb2::Saidas;
using zb2::Traco;

namespace {

constexpr std::uint32_t kAl = 0xEu;
// A MESMA sentinela do laco do motor (`core/brew/despacho.cpp`): o retorno de
// uma chamada do guest. Com outro valor, o laco sairia por "saiu_do_modulo" e o
// motivo medido mentiria sobre o que aconteceu.
constexpr std::uint32_t kSentinela = 0xFFFFFFF0u;
constexpr std::uint32_t kTrampolim = 0x00100000u;
constexpr std::uint32_t kTratador = 0x00100100u;
constexpr std::uint32_t kDados = 0x00100200u;
constexpr std::uint32_t kMediaData = 0x00100300u;
// Longe das outras areas: o PCM tem 44100 bytes.
constexpr std::uint32_t kBuffer = 0x00140000u;
constexpr std::uint32_t kClsMedia = 0x01005500u;  // AEECLSID_MEDIA
// Onde o guest gira enquanto a midia toca: `b .`. E o estado de um jogo no laco.
constexpr std::uint32_t kLacoDoGuest = 0x00100500u;
// Deslocamento do contador de avisos dentro do `pUser` do tratador (ver o
// `EscreverTratador`: +0 o nCmd, +4 o nStatus, +8 quantas vezes foi chamado).
constexpr std::uint32_t kOffAvisoContadorDoGuest = 8;

constexpr std::uint32_t DpRegistrador(std::uint32_t opcode, std::uint32_t rd, std::uint32_t rn,
                                      std::uint32_t rm) {
  return (kAl << 28) | ((opcode & 0xF) << 21) | ((rn & 0xF) << 16) | ((rd & 0xF) << 12) |
         (rm & 0xF);
}
constexpr std::uint32_t DpImediato(std::uint32_t opcode, std::uint32_t rd, std::uint32_t rn,
                                   std::uint32_t imm) {
  return (kAl << 28) | (1u << 25) | ((opcode & 0xF) << 21) | ((rn & 0xF) << 16) |
         ((rd & 0xF) << 12) | (imm & 0xFF);
}
constexpr std::uint32_t LdrImediato(std::uint32_t rd, std::uint32_t rn, std::uint32_t d) {
  return (kAl << 28) | (1u << 26) | (1u << 24) | (1u << 23) | (1u << 20) | ((rn & 0xF) << 16) |
         ((rd & 0xF) << 12) | (d & 0xFFF);
}
constexpr std::uint32_t StrImediato(std::uint32_t rd, std::uint32_t rn, std::uint32_t d) {
  return (kAl << 28) | (1u << 26) | (1u << 24) | (1u << 23) | ((rn & 0xF) << 16) |
         ((rd & 0xF) << 12) | (d & 0xFFF);
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
constexpr std::uint32_t Bx(std::uint32_t rm) { return (kAl << 28) | 0x012FFF10u | (rm & 0xF); }
// `b .`: um laco que nao acaba, dentro do modulo. E o guest a correr sozinho.
constexpr std::uint32_t BLacoInfinito() { return (kAl << 28) | 0x0AFFFFFEu; }
constexpr std::uint32_t Blx(std::uint32_t rm) { return (kAl << 28) | 0x012FFF30u | (rm & 0xF); }

void EscreverPalavras(Memoria& m, std::uint32_t onde, const std::uint32_t* p, std::size_t n) {
  for (std::size_t k = 0; k < n; ++k) m.Escrever32(onde + static_cast<std::uint32_t>(k) * 4, p[k]);
}

// O trampolim: chama `vtable[r4]` do objecto em r0, com r1..r3 como argumentos.
void EscreverTrampolim(Memoria& m) {
  const std::uint32_t codigo[] = {
      MoveRegistrador(5, 14), LdrImediato(12, 0, 0), SomaRegistrador(12, 12, 4),
      LdrImediato(12, 12, 0), Blx(12), MoveRegistrador(14, 5), Bx(14),
  };
  EscreverPalavras(m, kTrampolim, codigo, sizeof(codigo) / sizeof(codigo[0]));
  // O laco do guest: `b .` -- um passo por instrucao, e o relogio virtual do laco
  // do motor avanca 1 ms em cada um.
  const std::uint32_t laco[] = {BLacoInfinito()};
  EscreverPalavras(m, kLacoDoGuest, laco, 1);
}

// O tratador do aviso: le o nCmd (+8) e o nStatus (+16) -- os MESMOS campos que
// o tratador real do `cnk2` le -- conta as chamadas, e guarda pCmdData/dwSize.
void EscreverTratador(Memoria& m) {
  const std::uint32_t codigo[] = {
      LdrImediato(3, 1, 8),  StrImediato(3, 0, 0),
      LdrImediato(3, 1, 16), StrImediato(3, 0, 4),
      LdrImediato(3, 0, 8),  SomaImediata(3, 3, 1), StrImediato(3, 0, 8),
      LdrImediato(3, 1, 20), StrImediato(3, 0, 12),
      LdrImediato(3, 1, 24), StrImediato(3, 0, 16),
      Bx(14),
  };
  EscreverPalavras(m, kTratador, codigo, sizeof(codigo) / sizeof(codigo[0]));
}

const char* TextoDoResultado(bool ok) { return ok ? "ok" : "recusado"; }

}  // namespace

// ---------------------------------------------------------------------------
// PARTE 1 -- o MOTOR de verdade, com um pedido de midia escrito em ARM.
// ---------------------------------------------------------------------------
int MedirOMotor() {
  Traco traco("sonda_media.motor");
  Memoria mem(&traco);
  mem.EscritorUnico("sonda");
  ArmInterpreter cpu(mem, &traco);
  constexpr std::uint32_t kHeap = 0x80200000u, kHeapTam = 0x00C00000u, kTabela = 0x80010000u;
  zb2::Alocador al(mem, kHeap, kHeapTam, &traco);

  Saidas s;
  s.base = 0xF0000000u;
  s.passo = 4;
  s.quantos = 100000;
  s.ativa = true;
  cpu.ConfigurarSaidas(s);

  zb2::brew::Vfs vfs;  // vazia: esta parte nao le ficheiros
  zb2::brew::Despacho despacho(mem, traco, al, vfs);
  despacho.InstalarAjudantes(s, kTabela);
  // O objecto do shell, exactamente como a bateria o constroi. O slot 2 dele
  // (`CreateInstance`) fica apontado para a saida `kBaseDoShell + 2`, que e o
  // caso que o despacho trata.
  zb2::brew::ConstruirObjeto(mem, s, zb2::brew::kObjShell, s.Endereco(zb2::brew::kVtableShell),
                             zb2::brew::kSlotsPorVtable, zb2::brew::kBaseDoShell);
  EscreverTrampolim(mem);
  // O TRATADOR E O BLOCO DE DADOS dele tem de estar na memoria ANTES de o guest os
  // usar. Sem isto, o endereco do callback tem ZEROS e o guest corre `andeq` (NOP)
  // ate ao limite de passos -- e o aviso entregue fica a parecer PERDIDO.
  // [DEBUG-midia] foi o que custou um diagnostico nesta etapa: o objecto acabava em
  // estado PRONTO (a midia correu) e o contador do guest ficava a zero.
  EscreverTratador(mem);
  for (std::uint32_t k = 0; k < 8; ++k) mem.Escrever32(kDados + k * 4, 0);

  constexpr std::uint32_t kPponovo = 0x00100B00u;
  mem.Escrever32(kPponovo, 0xDEADBEEFu);
  cpu.Set(kR0, zb2::brew::kObjShell);
  cpu.Set(kR1, kClsMedia);                    // o ClsId pedido
  cpu.Set(kR2, kPponovo);                     // void **ppobj
  cpu.Set(4, brew_slots::kShell_CreateInstance * 4);
  cpu.Set(kLR, kSentinela);
  cpu.Set(kPC, kTrampolim);

  const auto resultado = despacho.Correr(cpu, 2000000, 0);
  const std::uint32_t devolvido = cpu.Get(kR0);
  const std::uint32_t ponteiro = mem.Ler32(kPponovo);
  const bool respondeu = (ponteiro != 0xDEADBEEFu);
  const bool aceitou = respondeu && ponteiro != 0 && devolvido == 0;

  std::printf("MOTOR: IShell::CreateInstance(0x%08x) -> r0=0x%08x (AEE_SUCCESS=%u), *ppobj=0x%08x\n",
              kClsMedia, devolvido, static_cast<unsigned>(zb2::brew::kAeeSucesso), ponteiro);
  std::printf("MOTOR: passos=%llu motivo=%s\n",
              static_cast<unsigned long long>(resultado.passos), resultado.motivo.c_str());
  // As faltas ficam no TRACO (o caminho unico de "nao implementado", P2), e nao
  // no mapa da ferramenta -- foi preciso ler o primeiro para as ver.
  for (const auto& par : traco.ContagemFaltas()) {
    if (par.first.find("CLSID") != std::string::npos || par.first.find("Media") != std::string::npos ||
        par.first.find("media") != std::string::npos) {
      std::printf("MOTOR: falta \"%s\" x%llu\n", par.first.c_str(),
                  static_cast<unsigned long long>(par.second));
    }
  }
  if (!aceitou) {
    std::printf(
        "MOTOR: VERMELHO -- o motor NAO conhece o AEECLSID_MEDIA. Nenhum titulo pede IMedia\n"
        "MOTOR:            porque o pedido morre aqui, com ECLASSNOTSUPPORT e a falta registada.\n"
        "MOTOR:            (a parte do ciclo de vida pelo laco fica SALTADA neste estado)\n");
    return 1;
  }

  // ------------------------------------------------------------------------
  // O CAMINHO COMPLETO PELO MOTOR, e nao so a criacao do objecto.
  //
  // O guest pe os dados, arma o callback, da `Play` -- e quem entrega o aviso de
  // fim e o LACO, com o relogio virtual a avancar a midia. Nada aqui chama
  // `Avancar` nem `EntregarAviso` a mao: se o aviso chegar ao tratador do guest,
  // chegou pelo caminho do motor.
  // ------------------------------------------------------------------------
  constexpr std::uint32_t kAmostras = 22050;  // 1 s na taxa declarada
  for (std::uint32_t k = 0; k < kAmostras; ++k) {
    const std::int16_t v = ((k % 2) == 0) ? static_cast<std::int16_t>(3000) : 0;
    mem.Escrever16(kBuffer + k * 2, static_cast<std::uint16_t>(v));
  }
  mem.Escrever32(kMediaData + zb2::brew::kOffMidiaClsData, zb2::brew::kMmdBuffer);
  mem.Escrever32(kMediaData + zb2::brew::kOffMidiaPData, kBuffer);
  mem.Escrever32(kMediaData + zb2::brew::kOffMidiaDwSize, kAmostras * 2);
  for (std::uint32_t k = 0; k < 8; ++k) mem.Escrever32(kDados + k * 4, 0);
  const std::uint32_t pmedia = mem.Ler32(kPponovo);

  const auto chamar_pelo_motor = [&](std::uint32_t slot, std::uint32_t a1, std::uint32_t a2) {
    cpu.Set(kR0, pmedia);
    cpu.Set(kR1, a1);
    cpu.Set(kR2, a2);
    cpu.Set(4, slot * 4);
    cpu.Set(kLR, kSentinela);
    cpu.Set(kPC, kTrampolim);
    despacho.Correr(cpu, 2000000, 0);
    return cpu.Get(kR0);
  };
  const std::uint32_t registou =
      chamar_pelo_motor(brew_slots::kMedia_RegisterNotify, kTratador, kDados);
  const std::uint32_t dados = chamar_pelo_motor(
      brew_slots::kMedia_SetMediaParm, static_cast<std::uint32_t>(zb2::brew::kMmParmMediaData),
      kMediaData);
  const std::uint32_t play = chamar_pelo_motor(brew_slots::kMedia_Play, 0, 0);

  // E agora o LACO, com o guest a correr codigo proprio (`b .`), que e o estado em
  // que um jogo esta quando a midia toca. O aviso tem de chegar ao tratador.
  cpu.Set(kPC, kLacoDoGuest);
  const auto passos = despacho.Correr(cpu, 200000, 0);
  const std::uint32_t avisos = mem.Ler32(kDados + kOffAvisoContadorDoGuest);
  std::printf("MOTOR: RegisterNotify=%u SetMediaParm(MEDIA_DATA)=%u Play=%u em obj=0x%08x\n",
              registou, dados, play, pmedia);
  std::printf("MOTOR: avancos do laco=%llu motivo=%s\n",
              static_cast<unsigned long long>(passos.passos), passos.motivo.c_str());
  std::printf("MOTOR: avisos que o LACO entregou ao tratador do guest=%u (nCmd=%u nStatus=%u)\n",
              avisos, mem.Ler32(kDados + 0), mem.Ler32(kDados + 4));
  if (avisos != 1 || mem.Ler32(kDados + 0) != 4 || mem.Ler32(kDados + 4) != 2) {
    std::printf("MOTOR: VERMELHO -- o motor criou o IMedia mas o aviso de fim nao chegou\n");
    return 1;
  }
  std::printf("MOTOR: VERDE -- o motor entrega o IMedia (0x%08x) E o aviso de fim\n", ponteiro);
  return 0;
  
  std::printf(
      "MOTOR: VERMELHO -- o motor NAO conhece o AEECLSID_MEDIA. Nenhum titulo pede IMedia\n"
      "MOTOR:            porque o pedido morre aqui, com ECLASSNOTSUPPORT e a falta registada.\n");
  return 1;
}

// ---------------------------------------------------------------------------
// PARTE 2 -- a interface, com o mesmo CPU. Aqui mede-se o ciclo de vida.
// ---------------------------------------------------------------------------
void MedirAInterface() {
  Traco traco("sonda_media.modulo");
  Memoria mem(&traco);
  mem.EscritorUnico("sonda");
  ArmInterpreter cpu(mem, &traco);
  Saidas s;
  s.base = 0xF0000000u;
  s.passo = 4;
  s.quantos = 100000;
  s.ativa = true;
  cpu.ConfigurarSaidas(s);

  zb2::audio::Misturador misturador;
  zb2::brew::Media media(mem, traco, s, misturador, nullptr);
  const zb2::brew::ResultadoCablagem instalacao = media.Instalar();
  std::printf("MODULO: Instalar() %s%s\n", instalacao.ok ? "OK" : "RECUSADO",
              instalacao.ok ? "" : (" -- " + instalacao.motivo).c_str());
  if (!instalacao.ok) return;

  EscreverTrampolim(mem);
  EscreverTratador(mem);
  for (std::uint32_t k = 0; k < 8; ++k) mem.Escrever32(kDados + k * 4, 0);

  // Um PCM de 22050 amostras (1 segundo na taxa declarada), com metade delas
  // nao nulas: e o numero que o criterio da etapa pede.
  constexpr std::uint32_t kAmostras = 22050;
  for (std::uint32_t k = 0; k < kAmostras; ++k) {
    const std::int16_t v = ((k % 2) == 0) ? static_cast<std::int16_t>(3000) : 0;
    mem.Escrever16(kBuffer + k * 2, static_cast<std::uint16_t>(v));
  }
  mem.Escrever32(kMediaData + zb2::brew::kOffMidiaClsData, zb2::brew::kMmdBuffer);
  mem.Escrever32(kMediaData + zb2::brew::kOffMidiaPData, kBuffer);
  mem.Escrever32(kMediaData + zb2::brew::kOffMidiaDwSize, kAmostras * 2);

  constexpr std::uint32_t kPponovo = 0x00100C00u;
  const std::int32_t codigo = media.Criar(kClsMedia, kPponovo);
  const std::uint32_t po = mem.Ler32(kPponovo);
  std::printf("MODULO: Criar(0x%08x) -> %d, IMedia em 0x%08x\n", kClsMedia, codigo, po);
  if (codigo != 0 || po == 0) return;

  // Pelos SLOTS da vtable, com o guest a chamar: os registradores carregam-se
  // como o jogo os carrega, e o trampolim faz `blx` para o endereco de saida.
  const auto chamar = [&](std::uint32_t slot, std::uint32_t a1, std::uint32_t a2,
                          std::uint32_t a3) {
    cpu.Set(kR0, po);
    cpu.Set(kR1, a1);
    cpu.Set(kR2, a2);
    cpu.Set(kR3, a3);
    cpu.Set(4, slot * 4);
    cpu.Set(kLR, kSentinela);
    cpu.Set(kPC, kTrampolim);
    for (int k = 0; k < 100000 && cpu.Get(kPC) != kSentinela; ++k) {
      const std::uint32_t pc = cpu.Get(kPC);
      std::uint32_t idx = 0;
      if (s.Contem(pc, &idx)) {
        const std::uint32_t lr = cpu.Get(kLR);
        const std::uint32_t r0 = cpu.Get(kR0);
        if (idx == 3) {
          const std::uint32_t n = mem.Ler32(r0 + zb2::brew::kOffObjRefs) + 1;
          mem.Escrever32(r0 + zb2::brew::kOffObjRefs, n);
          cpu.Set(kR0, n);
        } else if (idx == 4) {
          const std::uint32_t n = mem.Ler32(r0 + zb2::brew::kOffObjRefs);
          if (n > 0) mem.Escrever32(r0 + zb2::brew::kOffObjRefs, n - 1);
          cpu.Set(kR0, n > 0 ? n - 1 : 0);
        } else if (!media.Atender(idx, cpu)) {
          std::printf("MODULO: ERRO indice de saida %u nao e do IMedia\n", idx);
        }
        cpu.Set(kPC, lr);
      } else {
        cpu.Passo();
      }
    }
    // Os avisos sao ENTREGUES, nunca chamados de dentro do handler.
    zb2::brew::Media::Aviso a;
    while (media.RetirarAviso(&a)) {
      if (a.fn == 0) continue;
      cpu.Set(kR0, a.usuario);
      cpu.Set(kR1, a.endereco);
      cpu.Set(kLR, kSentinela);
      cpu.Set(kPC, a.fn);
      for (int k = 0; k < 4000 && cpu.Get(kPC) != kSentinela; ++k) cpu.Passo();
    }
    return cpu.Get(kR0);
  };

  chamar(brew_slots::kMedia_RegisterNotify, kTratador, kDados, 0);
  const std::uint32_t dados = chamar(brew_slots::kMedia_SetMediaParm,
                                     static_cast<std::uint32_t>(zb2::brew::kMmParmMediaData),
                                     kMediaData, 0);
  const std::uint32_t volume = chamar(brew_slots::kMedia_SetMediaParm,
                                      static_cast<std::uint32_t>(zb2::brew::kMmParmVolume), 80, 0);
  const std::uint32_t play = chamar(brew_slots::kMedia_Play, 0, 0, 0);
  const auto entregar = [&]() {
    zb2::brew::Media::Aviso a;
    while (media.RetirarAviso(&a)) {
      if (a.fn == 0) continue;
      cpu.Set(kR0, a.usuario);
      cpu.Set(kR1, a.endereco);
      cpu.Set(kLR, kSentinela);
      cpu.Set(kPC, a.fn);
      for (int k = 0; k < 4000 && cpu.Get(kPC) != kSentinela; ++k) cpu.Passo();
    }
  };
  for (std::uint32_t k = 0; k < kAmostras; k += 1000) {
    media.Avancar(1000);
    entregar();
  }

  const auto& m = misturador.MedidaAcumulada();
  std::printf("MODULO: SetMediaParm(MEDIA_DATA)=%u SetMediaParm(VOLUME)=%u Play=%u\n", dados, volume,
              play);
  std::printf(
      "MODULO: avisos=%llu (nCmd=%u nStatus=%u) | misturador: recebidas=%llu nao_nulas=%llu "
      "pico=%d blocos=%llu\n",
      static_cast<unsigned long long>(media.AvisosEmitidos()), mem.Ler32(kDados + 0),
      mem.Ler32(kDados + 4), static_cast<unsigned long long>(m.amostras_recebidas),
      static_cast<unsigned long long>(m.amostras_nao_nulas), m.pico,
      static_cast<unsigned long long>(m.blocos));
  std::printf("MODULO: pedidos aceitos=%u recusados=%u\n", media.PedidosAceitos(),
              media.PedidosRecusados());
}

int main() {
  const int motor = MedirOMotor();
  MedirAInterface();
  if (motor == 0) {
    std::printf("\nRESUMO: VERDE -- o motor entrega o IMedia e o modulo toca o ciclo de vida.\n");
  } else {
    std::printf(
        "\nRESUMO: VERMELHO -- o modulo do IMedia esta implementado e medido, mas o MOTOR\n"
        "        ainda nao o alcanca. O remendo (2 ficheiros partilhados, ~25 linhas) esta\n"
        "        no relatorio da etapa 5, e foi provado numa COPIA da arvore.\n");
  }
  return motor;
}
