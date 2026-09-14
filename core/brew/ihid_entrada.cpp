#include "core/brew/ihid_entrada.h"

#include <cstdio>
#include <sstream>

namespace zb2::brew {

namespace {

std::string Hex(std::uint32_t v) {
  char b[16];
  std::snprintf(b, sizeof(b), "0x%08x", v);
  return std::string(b);
}

// Le um numero que pode vir em decimal ou em hexadecimal (`0x...`). Um `uid` de
// HID escrito em decimal e ilegivel; escrito em hexadecimal e o mesmo que esta no
// cabecalho. Aceitam-se os dois, e um sufixo a mais e RECUSADO (um `12x` nao e um
// doze).
bool LerNumero(const std::string& t, std::uint32_t* saida) {
  if (t.empty()) return false;
  std::size_t usados = 0;
  try {
    const unsigned long v = std::stoul(t, &usados, 0);
    if (usados != t.size() || v > 0xFFFFFFFFul) return false;
    *saida = static_cast<std::uint32_t>(v);
    return true;
  } catch (...) {
    return false;
  }
}

bool LerValor(const std::string& t, std::int32_t* saida) {
  if (t.empty()) return false;
  std::size_t usados = 0;
  try {
    const long v = std::stol(t, &usados, 0);
    if (usados != t.size()) return false;
    if (v < -0x7FFFFFFFl || v > 0x7FFFFFFFl) return false;
    *saida = static_cast<std::int32_t>(v);
    return true;
  } catch (...) {
    return false;
  }
}

// A VALIDACAO DO GUIAO, num sitio so. `Ler` (texto) e `Adicionar` (codigo) tem de
// concordar: duas validacoes que tem de concordar sao zero validacoes.
bool ValidaEvento(const std::vector<EventoDeEntrada>& ja,
                  const EventoDeEntrada& e, std::string* motivo) {
  if (!ja.empty() && e.t_ms < ja.back().t_ms) {
    if (motivo != nullptr) {
      *motivo = "guiao fora de ordem: " + std::to_string(e.t_ms) + " vem depois de " +
                std::to_string(ja.back().t_ms);
    }
    return false;
  }
  if (e.tipo == TipoNaEntrada::Eixo) {
    if (e.valor < EntradaDoZeebo::kValorMin || e.valor > EntradaDoZeebo::kValorMax) {
      if (motivo != nullptr) {
        *motivo = "eixo " + Hex(e.uid) + " com valor " + std::to_string(e.valor) +
                  " fora de " + std::to_string(EntradaDoZeebo::kValorMin) + ".." +
                  std::to_string(EntradaDoZeebo::kValorMax);
      }
      return false;
    }
  } else {
    if (e.valor != 0 && e.valor != 1) {
      // O cabecalho do `AEEHIDButtonInfo` diz o que o estado de um botao digital
      // e: `nState` 0 ou nao-zero, com `nButtonMin` 0 e `nButtonMax` 1. Um guiao
      // que peca 5 nao esta a medir o emulador, esta a medir a nossa tolerancia.
      if (motivo != nullptr) {
        *motivo = "botao " + Hex(e.uid) + " com valor " + std::to_string(e.valor) +
                  "; o nState de um botao digital e 0 ou 1 (AEEIHIDDevice.h, AEEHIDButtonInfo)";
      }
      return false;
    }
  }
  return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// EntradaDoZeebo
// ---------------------------------------------------------------------------

bool EntradaDoZeebo::Adicionar(const EventoDeEntrada& e, std::string* motivo) {
  if (!ValidaEvento(eventos_, e, motivo)) return false;
  eventos_.push_back(e);
  return true;
}

bool EntradaDoZeebo::Ler(const std::string& texto, EntradaDoZeebo* saida, std::string* motivo) {
  std::vector<EventoDeEntrada> lidos;
  std::istringstream linhas(texto);
  std::string linha;
  std::uint32_t numero_da_linha = 0;
  while (std::getline(linhas, linha)) {
    ++numero_da_linha;
    const std::size_t comentario = linha.find('#');
    if (comentario != std::string::npos) linha = linha.substr(0, comentario);
    std::istringstream campos(linha);
    std::string t, tipo, uid, valor;
    if (!(campos >> t)) continue;  // linha vazia (ou so comentario)
    if (!(campos >> tipo >> uid >> valor)) {
      if (motivo != nullptr) {
        *motivo = "linha " + std::to_string(numero_da_linha) +
                  ": esperado '<t_ms> <eixo|botao> <uid> <valor>'";
      }
      return false;
    }
    EventoDeEntrada e;
    std::uint32_t t_ms = 0, uid_v = 0;
    std::int32_t valor_v = 0;
    if (!LerNumero(t, &t_ms) || !LerNumero(uid, &uid_v) || !LerValor(valor, &valor_v)) {
      if (motivo != nullptr) {
        *motivo = "linha " + std::to_string(numero_da_linha) + ": numero invalido";
      }
      return false;
    }
    if (tipo == "eixo") {
      e.tipo = TipoNaEntrada::Eixo;
    } else if (tipo == "botao") {
      e.tipo = TipoNaEntrada::Botao;
    } else {
      if (motivo != nullptr) {
        *motivo = "linha " + std::to_string(numero_da_linha) + ": tipo '" + tipo +
                  "' desconhecido (eixo|botao)";
      }
      return false;
    }
    e.t_ms = t_ms;
    e.uid = uid_v;
    e.valor = valor_v;
    std::string razao;
    if (!ValidaEvento(lidos, e, &razao)) {
      if (motivo != nullptr) {
        *motivo = "linha " + std::to_string(numero_da_linha) + ": " + razao;
      }
      return false;
    }
    lidos.push_back(e);
  }
  EntradaDoZeebo nova;
  nova.eventos_ = lidos;
  *saida = nova;
  return true;
}

std::int32_t EntradaDoZeebo::ValorEm(std::uint32_t t_ms, std::uint32_t uid,
                                     bool* nao_conhecido) const {
  bool achou = false;
  std::int32_t valor = 0;
  for (const auto& e : eventos_) {
    if (e.t_ms > t_ms) break;  // a lista e nao decrescente em tempo
    if (e.uid == uid) {
      achou = true;
      valor = e.valor;
    }
  }
  if (nao_conhecido != nullptr) *nao_conhecido = !achou;
  return valor;
}

// ---------------------------------------------------------------------------
// Sinais
// ---------------------------------------------------------------------------

Sinais::Sinais(Memoria& mem, Traco& traco) : mem_(mem), traco_(traco) {}

bool Sinais::Construir(const Saidas& saidas, std::uint32_t base_das_saidas) {
  base_ = base_das_saidas;
  if (!saidas.ativa) {
    traco_.RegistarFalta(Area::Entrada, "Sinais::Construir", "a faixa de saida nao esta activa");
    return false;
  }
  if (base_ + kSlotsNecessarios > saidas.quantos) {
    // RECUSA, e nao "escreve o que couber": cablar metade deixaria um objecto
    // meio construido, que e a pior especie de objecto.
    traco_.RegistarFalta(Area::Entrada, "Sinais::Construir",
                         "a faixa de saida so tem " + std::to_string(saidas.quantos) +
                             " indices; este modulo precisa ate ao " +
                             std::to_string(base_ + kSlotsNecessarios));
    return false;
  }

  endereco_vtable_ = kVtableFabricaDeSinais;
  objeto_fabrica_ = kObjFabricaDeSinais;
  endereco_vtable_sinal_ = kVtableSinal;

  // O `ConstruirObjeto` escreve a vtable, o cabecalho ROPI do objecto, a IBase
  // (os slots 0 e 1 apontam para os enderecos 3 e 4 do despacho) e um endereco de
  // saida DISTINTO por slot -- o mesmo desenho de todos os outros objectos.
  ConstruirObjeto(mem_, saidas, objeto_fabrica_, endereco_vtable_, kSlotsPorVtable,
                  base_ + kBaseDaFabrica);
  ConstruirObjeto(mem_, saidas, kObjSinalBase, endereco_vtable_sinal_, kSlotsPorVtable,
                  base_ + kBaseDoSinal);
  // Os objectos de sinal que ainda nao existem: vtable a zero, logo nao sao
  // objectos (`Conhece` olha para a vtable). Escrever zero em vez de deixar lixo
  // e o que faz uma leitura errada ser uma recusa, e nao um callback para a nada.
  for (std::uint32_t n = 1; n < kSinaisDisponiveis; ++n) {
    const std::uint32_t obj = kObjSinalBase + n * kPassoDoSinal;
    mem_.Escrever32(obj + 0, 0);
    mem_.Escrever32(obj + 4, 0);
    mem_.Escrever32(obj + kSinal_pfn, 0);
    mem_.Escrever32(obj + kSinal_pUser, 0);
  }

  // A LEITURA DE VOLTA: cada slot que eu espero ter escrito aponta para o
  // endereco de saida que lhe pertence? Uma cablagem ja se perdeu sem sintoma.
  const std::uint32_t vt[2] = {endereco_vtable_, endereco_vtable_sinal_};
  const std::uint32_t bases[2] = {base_ + kBaseDaFabrica, base_ + kBaseDoSinal};
  for (int v = 0; v < 2; ++v) {
    for (std::uint32_t slot = 2; slot < 8; ++slot) {
      const std::uint32_t esperado = saidas.Endereco(bases[v] + slot);
      const std::uint32_t lido = mem_.Ler32(vt[v] + slot * 4);
      if (lido != esperado) {
        traco_.RegistarFalta(Area::Entrada, "Sinais::Construir",
                             "cablagem perdida no slot " + std::to_string(slot) + " de 0x" +
                                 Hex(vt[v]));
        return false;
      }
    }
  }
  construido_ = true;
  traco_.Emitir(Area::Entrada, Nivel::Informacao, "FABRICA_DE_SINAIS",
                "fabrica=0x" + Hex(objeto_fabrica_) + " vtable=0x" + Hex(endereco_vtable_));
  return true;
}

std::uint32_t Sinais::ObjetoSinal(std::uint32_t n) const { return kObjSinalBase + n * kPassoDoSinal; }

bool Sinais::Conhece(std::uint32_t endereco) const {
  if (endereco < kObjSinalBase) return false;
  const std::uint32_t delta = endereco - kObjSinalBase;
  if (delta % kPassoDoSinal != 0) return false;
  const std::uint32_t n = delta / kPassoDoSinal;
  if (n >= kSinaisDisponiveis) return false;
  return mem_.Ler32(endereco) == kVtableSinal;
}

void Sinais::Marcar(std::uint32_t endereco) {
  if (!Conhece(endereco)) {
    traco_.RegistarFalta(Area::Entrada, "ISignal_Set",
                         "o endereco 0x" + Hex(endereco) + " nao e um sinal nosso");
    return;
  }
  const std::uint32_t pfn = mem_.Ler32(endereco + kSinal_pfn);
  const std::uint32_t puser = mem_.Ler32(endereco + kSinal_pUser);
  if (pfn == 0) {
    // Sem funcao nao ha callback. E uma RECUSA registada, e nao um sucesso mudo.
    traco_.RegistarFalta(Area::Entrada, "ISignal_Set",
                         "o sinal 0x" + Hex(endereco) + " nao tem funcao (pfn=0)");
    return;
  }
  pendentes_.emplace_back(pfn, puser);
  ++marcados_;
}

bool Sinais::PrepararProximoCallback(ICpu& cpu) {
  while (!pendentes_.empty()) {
    const auto par = pendentes_.front();
    pendentes_.pop_front();
    if (par.first < kBaseDoModulo || par.first >= kFimDoModulo) {
      // UM CALLBACK PARA FORA DO MODULO NAO SE CHAMA. Saltar para 0 levava o
      // emulador a "saiu_do_modulo" -- um sintoma que nao diz nada sobre a causa.
      // Aqui a causa fica escrita.
      traco_.RegistarFalta(Area::Entrada, "callback_de_sinal",
                           "funcao 0x" + Hex(par.first) + " fora do modulo (0x" +
                               Hex(kBaseDoModulo) + "..0x" + Hex(kFimDoModulo) + ")");
      continue;
    }
    cpu.Set(kLR, sentinela_);
    cpu.Set(kR0, par.second);
    cpu.Set(kPC, par.first);
    ++entregues_;
    traco_.Emitir(Area::Entrada, Nivel::Depuracao, "CALLBACK_DE_SINAL",
                  "funcao=0x" + Hex(par.first) + " contexto=0x" + Hex(par.second));
    return true;
  }
  return false;
}

bool Sinais::Recusar(ICpu& cpu, const char* o_que, const std::string& porque) {
  traco_.RegistarFalta(Area::Entrada, o_que, porque);
  cpu.Set(kR0, kAeeBadParm);
  return true;
}

bool Sinais::Atender(ICpu& cpu, std::uint32_t indice) {
  if (!construido_) return false;
  if (indice >= base_ + kBaseDaFabrica && indice < base_ + kBaseDaFabrica + 8) {
    const std::uint32_t slot = indice - (base_ + kBaseDaFabrica);
    return AtenderFabrica(cpu, slot);
  }
  if (indice >= base_ + kBaseDoSinal && indice < base_ + kBaseDoSinal + 8) {
    const std::uint32_t slot = indice - (base_ + kBaseDoSinal);
    return AtenderSinal(cpu, slot, cpu.Get(kR0));
  }
  return false;
}

bool Sinais::AtenderFabrica(ICpu& cpu, std::uint32_t slot) {
  const std::uint32_t po = cpu.Get(kR0);
  if (po != objeto_fabrica_) {
    return Recusar(cpu, "ISignalCBFactory::slot",
                   "po 0x" + Hex(po) + " nao e a nossa fabrica (0x" + Hex(objeto_fabrica_) + ")");
  }
  if (slot == 2) {
    // QueryInterface: o slot 2 da IQI. O `ConstruirObjeto` escreve-o como um slot
    // de saida generico, logo quem o serve tem de ser este despacho.
    const std::uint32_t iid = cpu.Get(kR1);
    const std::uint32_t ppo = cpu.Get(kR2);
    if (ppo == 0) return Recusar(cpu, "ISignalCBFactory::QueryInterface", "ppo nulo");
    if (iid == kIidISignalCBFactory) {
      mem_.Escrever32(ppo, objeto_fabrica_);
      cpu.Set(kR0, kAeeSuccess);
    } else {
      mem_.Escrever32(ppo, 0);
      cpu.Set(kR0, kAeeClassNotSupported);
    }
    return true;
  }
  if (slot != kISignalCBFactory_CreateSignal) {
    // UM SLOT NAO IMPLEMENTADO RECUSA EM VOZ ALTA (P2). O caminho antigo
    // devolvia sucesso mudo em todos os slots por preencher, e foram 86 377
    // chamadas descartadas sem ninguem saber.
    return Recusar(cpu, "ISignalCBFactory::slot", "slot " + std::to_string(slot) + " sem implementacao");
  }
  const std::uint32_t pfn = cpu.Get(kR1);
  const std::uint32_t pcx = cpu.Get(kR2);
  const std::uint32_t ppi_sig = cpu.Get(kR3);
  const std::uint32_t ppi_ctl = mem_.Ler32(cpu.Get(kSP));
  if (pfn == 0) {
    return Recusar(cpu, "ISignalCBFactory::CreateSignal",
                   "funcao nula: um sinal sem funcao nunca entrega nada");
  }
  if (ppi_sig == 0 && ppi_ctl == 0) {
    return Recusar(cpu, "ISignalCBFactory::CreateSignal", "os dois ponteiros de saida sao nulos");
  }
  if (quantos_sinais_ >= kSinaisDisponiveis) {
    return Recusar(cpu, "ISignalCBFactory::CreateSignal",
                   "a tabela de sinais do emulador acabou (" + std::to_string(kSinaisDisponiveis) + ")");
  }
  const std::uint32_t obj = ObjetoSinal(quantos_sinais_);
  mem_.Escrever32(obj + 0, endereco_vtable_sinal_);
  mem_.Escrever32(obj + 4, 1);
  mem_.Escrever32(obj + kSinal_pfn, pfn);
  mem_.Escrever32(obj + kSinal_pUser, pcx);
  ++quantos_sinais_;
  // O MESMO objecto serve de `ISignal` e de `ISignalCtl`.
  //
  // Nao e economia: e o que o sample do SDK faz. `GamepadMgr.c` cria o sinal com
  // `ISignalCBFactory_CreateSignal(..., (ISignal **)0, (ISignalCtl **)&p)`,
  // recebe-o como `ISignalCtl *` e passa `(ISignal *)p` ao
  // `RegisterForPositionChange`. A vtable do `ISignalCtl` comeca pelos slots do
  // `ISignal` (`INHERIT_ISignalCtl` = `INHERIT_ISignal` + Detach + Enable), logo o
  // mesmo ponteiro E as duas coisas.
  if (ppi_sig != 0) mem_.Escrever32(ppi_sig, obj);
  if (ppi_ctl != 0) mem_.Escrever32(ppi_ctl, obj);
  cpu.Set(kR0, kAeeSuccess);
  traco_.Emitir(Area::Entrada, Nivel::Depuracao, "CRIA_SINAL",
                "sinal=0x" + Hex(obj) + " funcao=0x" + Hex(pfn) + " contexto=0x" + Hex(pcx));
  return true;
}

bool Sinais::AtenderSinal(ICpu& cpu, std::uint32_t slot, std::uint32_t endereco) {
  if (!Conhece(endereco)) return false;  // nao e um dos nossos: o chamador segue a cadeia
  if (slot == 2) {
    const std::uint32_t iid = cpu.Get(kR1);
    const std::uint32_t ppo = cpu.Get(kR2);
    if (ppo == 0) return Recusar(cpu, "ISignal::QueryInterface", "ppo nulo");
    if (iid == kIidISignal || iid == kIidISignalCtl) {
      mem_.Escrever32(ppo, endereco);
      cpu.Set(kR0, kAeeSuccess);
    } else {
      mem_.Escrever32(ppo, 0);
      cpu.Set(kR0, kAeeClassNotSupported);
    }
    return true;
  }
  if (slot == kISignal_Set) {
    Marcar(endereco);
    cpu.Set(kR0, kAeeSuccess);
    return true;
  }
  if (slot == kISignalCtl_Detach) {
    // `Detach`: o sinal deixa de avisar. O contrato do `AEEISignal.h` diz que,
    // depois de desanexado, um `Set` nao faz nada; apagar o par e o que torna
    // isso verdade aqui.
    mem_.Escrever32(endereco + kSinal_pfn, 0);
    mem_.Escrever32(endereco + kSinal_pUser, 0);
    cpu.Set(kR0, kAeeSuccess);
    return true;
  }
  if (slot == kISignalCtl_Enable) {
    // Nao ha fila de sinais desactivados: nao ha nada a fazer, e o sucesso e o
    // contrato. Fica dito aqui em vez de parecer um stub.
    cpu.Set(kR0, kAeeSuccess);
    return true;
  }
  return Recusar(cpu, "ISignal::slot", "slot " + std::to_string(slot) + " sem implementacao");
}

}  // namespace zb2::brew
