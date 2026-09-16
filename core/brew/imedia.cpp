#include "core/brew/imedia.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace zb2::brew {

// A ARITMETICA DAS TRES REGIOES deste modulo, provada em TEMPO DE COMPILACAO: os
// objectos, o bloco de aviso de cada objecto (`kTamanhoDoAviso` = 28 bytes, um
// por objecto) e os dados que o aviso aponta tem de caber INTEIROS na regiao. O
// teste `Media.ARegiaoDeMidiaNaoInvadeAQuemVemADepoisDela` prova o mesmo contra o
// `kObjIgl` do `core/brew/igl.h`, que e quem nasce a seguir.
static_assert(kObjMediaBase + kMaxObjetosDeMidia * kPassoDoObjetoMedia <= kAvisoBase,
              "os enderecos dos objectos nao cabem antes dos avisos");
static_assert(kAvisoBase + kMaxObjetosDeMidia * kTamanhoDoAviso <= kAvisoDadosBase,
              "um bloco de aviso por objecto nao cabe antes dos dados dos avisos");
static_assert(kAvisoDadosBase + kMaxObjetosDeMidia * 4 <= kFimDaRegiaoDeMidia,
              "os dados dos avisos nao cabem antes do fim da regiao de midia");

const char* NomeDoStatusDeMidia(std::int32_t status) {
  // Os nomes e os valores, transcritos de `AEEIMedia.h` (MM_STATUS_BASE = 1).
  switch (status) {
    case kMmStatusStart: return "MM_STATUS_START";
    case kMmStatusDone: return "MM_STATUS_DONE";
    case kMmStatusAbort: return "MM_STATUS_ABORT";
    case kMmStatusMediaSpec: return "MM_STATUS_MEDIA_SPEC";
    case kMmStatusTickUpdate: return "MM_STATUS_TICK_UPDATE";
    case kMmStatusSeek: return "MM_STATUS_SEEK";
    case kMmStatusPause: return "MM_STATUS_PAUSE";
    case kMmStatusResume: return "MM_STATUS_RESUME";
    default: return "MM_STATUS_?";
  }
}

const char* NomeDoComandoDeMidia(std::int32_t cmd) {
  switch (cmd) {
    case kMmCmdSetMediaParm: return "MM_CMD_SETMEDIAPARM";
    case kMmCmdGetMediaParm: return "MM_CMD_GETMEDIAPARM";
    case kMmCmdPlay: return "MM_CMD_PLAY";
    case kMmCmdRecord: return "MM_CMD_RECORD";
    case kMmCmdGetTotalTime: return "MM_CMD_GETTOTALTIME";
    default: return "MM_CMD_?";
  }
}

namespace {

// Os nomes das classes da familia, de `platform/system/inc/AEEClassIDs.h`
// (`AEECLSID_MULTIMEDIA = QVERSION + 0x5500`).
struct LinhaDeClasse {
  std::uint32_t id;
  const char* nome;
};
const LinhaDeClasse kClasses[] = {
    {0x01005500u, "AEECLSID_MEDIA"},        {0x01005501u, "AEECLSID_MEDIAMIDI"},
    {0x01005502u, "AEECLSID_MEDIAMP3"},     {0x01005503u, "AEECLSID_MEDIAQCP"},
    {0x01005504u, "AEECLSID_MEDIAPMD"},     {0x01005505u, "AEECLSID_MEDIAMIDIOUTMSG"},
    {0x01005506u, "AEECLSID_MEDIAMIDIOUTQCP"}, {0x01005507u, "AEECLSID_MEDIAMPEG4"},
    {0x01005508u, "AEECLSID_MEDIAMMF"},     {0x01005509u, "AEECLSID_MEDIAPHR"},
    {0x0100550au, "AEECLSID_MEDIAADPCM"},   {0x0100550bu, "AEECLSID_MEDIAAAC"},
    {0x0100550cu, "AEECLSID_MEDIAIMELODY"}, {0x0100550du, "AEECLSID_MEDIAUTIL"},
    {0x0100550eu, "AEECLSID_MEDIAAMR"},     {0x0100550fu, "AEECLSID_MEDIAHVS"},
    {0x01005510u, "AEECLSID_MEDIASAF"},     {0x01005511u, "AEECLSID_MEDIAPCM"},
    {0x01005512u, "AEECLSID_MEDIAXMF"},     {0x01005513u, "AEECLSID_MEDIADLS"},
    {0x01005514u, "AEECLSID_MEDIASVG"},
};

// A `kSlots` mudou de sitio: esta agora em `kTabelaDeSlotsDoMedia`, no fim
// deste ficheiro, porque o TESTE precisa dela para provar a guarda por
// violacao. Um alias local mantem o resto do ficheiro curto.
constexpr std::size_t kQuantosSlotsDeclarados = kSlotsDoMedia;

// As constantes geradas pelo SDK tem de bater com o que esta tabela declara.
// Nao basta parecer: um erro de um slot ja custou uma ronda inteira.
static_assert(brew_slots::kMedia_RegisterNotify == 3, "RegisterNotify e o slot 3");
static_assert(brew_slots::kMedia_SetMediaParm == 4, "SetMediaParm e o slot 4");
static_assert(brew_slots::kMedia_GetMediaParm == 5, "GetMediaParm e o slot 5");
static_assert(brew_slots::kMedia_Play == 6, "Play e o slot 6");
static_assert(brew_slots::kMedia_Record == 7, "Record e o slot 7");
static_assert(brew_slots::kMedia_Stop == 8, "Stop e o slot 8");
static_assert(brew_slots::kMedia_Seek == 9, "Seek e o slot 9");
static_assert(brew_slots::kMedia_Pause == 10, "Pause e o slot 10");
static_assert(brew_slots::kMedia_Resume == 11, "Resume e o slot 11");
static_assert(brew_slots::kMedia_GetTotalTime == 12, "GetTotalTime e o slot 12");
static_assert(brew_slots::kMedia_GetState == 13, "GetState e o slot 13");
static_assert(kQuantosSlotsDeclarados == kSlotsDoMedia,
              "a tabela declarada tem de ter os 14 slots do IMedia");

// O volume e o `MM_PARM_VOLUME` do SDK: 0 a AEE_MAX_VOLUME (100).
// A lista de TUNE vem do proprio cabecalho: "Values: 0x34, 0x3f, 0x40(default),
// 0x41, 0x4c".
bool VolumeDeTuneValido(std::int32_t v) {
  return v == 0x34 || v == 0x3f || v == 0x40 || v == 0x41 || v == 0x4c;
}

const char* NomeDaClasse(std::uint32_t cls) {
  for (const auto& c : kClasses) {
    if (c.id == cls) return c.nome;
  }
  return "AEECLSID_?";
}

// Limite DECLARADO para um buffer de midia. Nao vem do SDK: vem do tamanho dos
// ficheiros do corpus (o maior `pak0.pakz` tem 21 588 027 bytes, medido em
// `docs/AUDITORIA-ZEEBX-v0.1.0.md`) arredondado para cima. Existe para um
// `dwSize` absurdo nao alocar o espaco que ele pede.
constexpr std::uint32_t kMaiorBufer = 32u * 1024u * 1024u;

std::string EmHex(std::uint32_t v) {
  char b[16];
  std::snprintf(b, sizeof(b), "0x%08x", v);
  return b;
}

}  // namespace

const ParametroDeMidia kParametrosDeMidia[] = {
    // id                                   nome                        Set               Get
    {kMmParmMediaData, "MM_PARM_MEDIA_DATA", TratamentoDeParametro::Aplicado, TratamentoDeParametro::Aplicado},
    {kMmParmAudioDevice, "MM_PARM_AUDIO_DEVICE", TratamentoDeParametro::Guardado, TratamentoDeParametro::Guardado},
    {kMmParmAudioPath, "MM_PARM_AUDIO_PATH", TratamentoDeParametro::Guardado, TratamentoDeParametro::Guardado},
    {kMmParmVolume, "MM_PARM_VOLUME", TratamentoDeParametro::Aplicado, TratamentoDeParametro::Aplicado},
    {kMmParmMute, "MM_PARM_MUTE", TratamentoDeParametro::Aplicado, TratamentoDeParametro::Aplicado},
    {kMmParmTempo, "MM_PARM_TEMPO", TratamentoDeParametro::Guardado, TratamentoDeParametro::Guardado},
    {kMmParmTune, "MM_PARM_TUNE", TratamentoDeParametro::Guardado, TratamentoDeParametro::Guardado},
    {kMmParmPan, "MM_PARM_PAN", TratamentoDeParametro::Guardado, TratamentoDeParametro::Guardado},
    {kMmParmTickTime, "MM_PARM_TICK_TIME", TratamentoDeParametro::Guardado, TratamentoDeParametro::Guardado},
    {kMmParmRect, "MM_PARM_RECT", TratamentoDeParametro::Guardado, TratamentoDeParametro::Guardado},
    {kMmParmPlayRepeat, "MM_PARM_PLAY_REPEAT", TratamentoDeParametro::Aplicado, TratamentoDeParametro::Aplicado},
    {kMmParmPos, "MM_PARM_POS", TratamentoDeParametro::Guardado, TratamentoDeParametro::Guardado},
    // Os tres que o SDK declara SO de leitura.
    {kMmParmClsid, "MM_PARM_CLSID", TratamentoDeParametro::SoDeLeitura, TratamentoDeParametro::Aplicado},
    {kMmParmCaps, "MM_PARM_CAPS", TratamentoDeParametro::SoDeLeitura, TratamentoDeParametro::Aplicado},
    {kMmParmSeekCaps, "MM_PARM_SEEK_CAPS", TratamentoDeParametro::SoDeLeitura, TratamentoDeParametro::Aplicado},
    {kMmParmEnable, "MM_PARM_ENABLE", TratamentoDeParametro::Guardado, TratamentoDeParametro::Guardado},
    {kMmParmChannelShare, "MM_PARM_CHANNEL_SHARE", TratamentoDeParametro::Guardado, TratamentoDeParametro::Guardado},
    {kMmParmRate, "MM_PARM_RATE", TratamentoDeParametro::Guardado, TratamentoDeParametro::Guardado},
    {kMmParmPlayType, "MM_PARM_PLAY_TYPE", TratamentoDeParametro::Guardado, TratamentoDeParametro::Guardado},
    {kMmParmAudioSync, "MM_PARM_AUDIOSYNC", TratamentoDeParametro::Guardado, TratamentoDeParametro::Guardado},
    {kMmParmNotes, "MM_PARM_NOTES", TratamentoDeParametro::Guardado, TratamentoDeParametro::Guardado},
    // Sem implementacao nesta arvore: recusam nos dois sentidos, e ficam contados.
    {kMmParmFrame, "MM_PARM_FRAME", TratamentoDeParametro::Recusado, TratamentoDeParametro::Recusado},
    {kMmParmReserved1, "MM_PARM_RESERVED_1", TratamentoDeParametro::Recusado, TratamentoDeParametro::Recusado},
    {kMmParmReserved2, "MM_PARM_RESERVED_2", TratamentoDeParametro::Recusado, TratamentoDeParametro::Recusado},
};
const std::size_t kQuantosParametrosDeMidia =
    sizeof(kParametrosDeMidia) / sizeof(kParametrosDeMidia[0]);

const ParametroDeMidia* ParametroPorId(std::int32_t id) {
  for (std::size_t k = 0; k < kQuantosParametrosDeMidia; ++k) {
    if (kParametrosDeMidia[k].id == id) return &kParametrosDeMidia[k];
  }
  return nullptr;
}

bool ClasseDeMidia(std::uint32_t cls) {
  return cls >= kClasseMultimidia && cls <= kUltimaClasseMultimidia;
}

const char* NomeDaClasseDeMidia(std::uint32_t cls) { return NomeDaClasse(cls); }

Media::Media(Memoria& mem, Traco& traco, const Saidas& saidas,
             audio::Misturador& misturador, Vfs* vfs)
    : mem_(mem), traco_(traco), saidas_(saidas), misturador_(misturador), vfs_(vfs) {
  // O CONJUNTO NASCE VAZIO e cresce a pedido. Era um `resize(16)` com os 16
  // enderecos escritos a partida -- e o 17.o pedido do jogo respondia "sem
  // memoria". Cada lugar custa um `Objeto` (com o PCM dele): reservar 1024
  // lugares para um titulo que pede 2 seria pagar por todos. O endereco de cada
  // lugar sai do INDICE, no `Criar`, e nao de uma reserva feita aqui.
}

void Media::Recusar(const std::string& o_que, const std::string& porque) {
  ++pedidos_recusados_;
  ultimo_motivo_ = o_que + ": " + porque;
  // P2: o caminho nao implementado RECUSA e REGISTA. Nunca "devolve sucesso e
  // nao faz nada" -- foi um stub desses que descartou 86 377 chamadas de
  // `glCullFace` em silencio na arvore antiga.
  traco_.RegistarFalta(Area::Audio, o_que, porque);
}

ResultadoCablagem Media::Instalar() {
  // 0. A FAIXA DE SAIDA TEM DE ESTAR CONFIGURADA E COBRIR ESTES INDICES.
  //
  // Esta guarda nasceu de um defeito REAL, apanhado pelo proprio teste: o
  // `Saidas` foi copiado para dentro do objecto ANTES de o chamador o
  // configurar, e o `Instalar` escreveu a vtable em enderecos derivados de uma
  // base ZERO. Nada acusou -- a leitura de volta confirmava o que se tinha
  // escrito. Uma cablagem num endereco que nao existe e o pior tipo de defeito:
  // passa na conferencia e nao serve para nada.
  if (!saidas_.ativa || saidas_.quantos < kVtableDoMedia + kSlotsPorVtable ||
      saidas_.quantos < kBaseDoMedia + kSlotsDoMedia) {
    return {false, "faixa de saida nao configurada (ativa=" + std::to_string(saidas_.ativa) +
                       " quantos=" + std::to_string(saidas_.quantos) + ")"};
  }

  // 1. A tabela declarada tem de cobrir TODOS os slots, sem repetir nenhum.
  const ResultadoCablagem tabela = ConferirTabelaDeSlots(
      kTabelaDeSlotsDoMedia, kQuantosSlotsDoMediaDeclarados, kSlotsDoMedia);
  if (!tabela.ok) return tabela;

  // 2. Escrever, e CONFIRMAR com leitura de volta.
  const std::uint32_t vt = saidas_.Endereco(kVtableDoMedia);
  mem_.Escrever32(vt + 0, saidas_.Endereco(3));  // IBase::AddRef, do motor
  mem_.Escrever32(vt + 4, saidas_.Endereco(4));  // IBase::Release, do motor
  for (std::uint32_t s = 2; s < kSlotsDoMedia; ++s) {
    mem_.Escrever32(vt + s * 4, saidas_.Endereco(kBaseDoMedia + s));
  }
  for (std::uint32_t s = 0; s < kSlotsDoMedia; ++s) {
    const std::uint32_t esperado = (s < 2) ? saidas_.Endereco(3 + s)
                                           : saidas_.Endereco(kBaseDoMedia + s);
    const std::uint32_t lido = mem_.Ler32(vt + s * 4);
    if (lido != esperado) {
      return {false, std::string("cablagem perdida no slot ") + std::to_string(s) +
                         " (" + kTabelaDeSlotsDoMedia[s].nome + ")"};
    }
  }
  instalada_ = true;
  traco_.Emitir(Area::Audio, Nivel::Informacao, "IMEDIA_INSTALADO",
                std::to_string(kSlotsDoMedia) + " slots, vtable na saida " +
                    std::to_string(kVtableDoMedia));
  return {true, ""};
}

Media::Objeto* Media::PorEndereco(std::uint32_t objeto) {
  for (auto& o : objetos_) {
    if (o.vivo && o.endereco == objeto) return &o;
  }
  return nullptr;
}

const Media::Objeto* Media::PorEndereco(std::uint32_t objeto) const {
  for (const auto& o : objetos_) {
    if (o.vivo && o.endereco == objeto) return &o;
  }
  return nullptr;
}

std::uint32_t Media::ObjetosVivos() const {
  std::uint32_t n = 0;
  for (const auto& o : objetos_) {
    if (o.vivo) ++n;
  }
  return n;
}

std::int32_t Media::EstadoDe(std::uint32_t objeto) const {
  const Objeto* o = PorEndereco(objeto);
  return o != nullptr ? o->estado : 0;
}

bool Media::EnderecoDoAviso(const Objeto& o, std::uint32_t* aviso, std::uint32_t* dados) {
  // UMA CONTA SO, para os DOIS sitios que dela dependem: o `EmitirAviso` (o bloco
  // do `AEEMediaCmdNotify`) e o `GetTotalTime` (os 4 bytes que ele aponta). Duas
  // contas que tem de concordar sao zero contas -- e aqui a segunda apontaria
  // para o bloco de outro objecto.
  const std::uint32_t indice = (o.endereco - kObjMediaBase) / kPassoDoObjetoMedia;
  if (o.endereco < kObjMediaBase || indice >= kMaxObjetosDeMidia) {
    Recusar("IMedia::Aviso",
            "o objecto " + EmHex(o.endereco) + " esta fora da regiao de midia (" +
                EmHex(kObjMediaBase) + ".." + EmHex(kAvisoBase) + "): sem bloco de aviso");
    return false;
  }
  if (aviso != nullptr) *aviso = kAvisoBase + indice * kTamanhoDoAviso;
  if (dados != nullptr) *dados = kAvisoDadosBase + indice * 4;
  return true;
}

void Media::RecolherLibertados() {
  // O `Release` do guest e o da IBase, que o MOTOR trata (slot 1 -> saida 4),
  // porque `Release` e igual para todos os objectos ROPI. Logo o objecto nao
  // avisa ninguem quando chega a zero: a contagem fica na memoria do guest e o
  // recolhedor LE-A. Sem isto, os objectos que o jogo ja soltou ficavam ocupados
  // para sempre, e um titulo que cria e solta som em ciclo encheria a regiao sem
  // razao nenhuma.
  //
  // O LUGAR E DO INDICE, e nao do `Objeto`: por isso o endereco e guardado antes
  // de o lugar ser limpo, e devolvido depois. (Antes isto eram DOIS lacos: o
  // segundo repunha os enderecos dos lugares cujo campo tinha ficado a zero --
  // uma lista a adivinhar o que a outra fez. Um lugar tem UM dono do endereco.)
  for (auto& o : objetos_) {
    if (!o.vivo) continue;
    if (mem_.Ler32(o.endereco + kOffObjRefs) == 0) {
      traco_.Emitir(Area::Audio, Nivel::Depuracao, "IMEDIA_RECOLHIDO",
                    "obj=" + EmHex(o.endereco));
      const std::uint32_t endereco = o.endereco;
      o = Objeto{};
      o.endereco = endereco;
    }
  }
}

std::int32_t Media::Criar(std::uint32_t cls, std::uint32_t pponovo) {
  if (!instalada_) {
    Recusar("IMedia::Criar", "vtable nao instalada");
    return kAeeFalhou;
  }
  if (!ClasseDeMidia(cls)) {
    Recusar("IMedia::Criar", "CLSID " + EmHex(cls) + " nao e da familia de midia");
    return kAeeClasseNaoSuportada;
  }
  if (pponovo == 0) {
    Recusar("IMedia::Criar", "ppobj nulo");
    return kAeeParametroErrado;
  }
  RecolherLibertados();
  Objeto* livre = nullptr;
  for (auto& o : objetos_) {
    if (!o.vivo) {
      livre = &o;
      break;
    }
  }
  if (livre == nullptr) {
    // O CONJUNTO CRESCE A PEDIDO, E O LIMITE E A REGIAO, NAO UM NUMERO ESCOLHIDO
    // AQUI. Era um conjunto FIXO de 16 lugares, e o 17.o pedido respondia "sem
    // memoria". MEDIDO na corrida de referencia do `slot32`
    // (`/tmp/corrida_slot32.json`, `ZB2_QUADROS=300 ZB2_EVT_START=1`): **29
    // pedidos recusados com aquele motivo**, por titulo `abd` 19, `ridgeracer`
    // 9, `torkandkral` 1. Os jogos guardam UM `IMedia` POR SOM e nao o soltam
    // entre sons -- o `abd` sozinho tem 19 pedidos.
    //
    // O SDK **nao** tem este numero: o que a doc limita sao as vozes que tocam
    // ao mesmo tempo (`AEEMedia.txt`, "IMedia - Simultaneous media playback").
    // Citá-lo aqui seria inventar proveniencia -- o motivo esta escrito em
    // `kMaxObjetosDeMidia` (imedia.h), com a linha do SDK transcrita.
    if (objetos_.size() >= kMaxObjetosDeMidia) {
      Recusar("IMedia::Criar",
              "os " + std::to_string(kMaxObjetosDeMidia) +
                  " enderecos da regiao de midia (" + EmHex(kObjMediaBase) + ".." +
                  EmHex(kAvisoBase) + ") estao ocupados");
      return kAeeSemMemoria;
    }
    objetos_.push_back(Objeto{});
    livre = &objetos_.back();
    // O ENDERECO E DO INDICE, e nao do `Objeto`: e o que da a cada objecto um
    // lugar proprio na regiao -- e um lugar por objecto e o que a regiao dos
    // avisos usa para nao escrever o aviso de um em cima do bloco de outro.
    livre->endereco =
        kObjMediaBase + static_cast<std::uint32_t>(objetos_.size() - 1) * kPassoDoObjetoMedia;
  }
  const std::uint32_t endereco = livre->endereco;
  *livre = Objeto{};
  livre->endereco = endereco;
  livre->vivo = true;
  // A IDENTIDADE: um numero novo por cada objecto criado, e nao o endereco --
  // o endereco volta a ser entregue quando este objecto morre (o recolhedor
  // abaixo), e um aviso guardado tem de poder distinguir os dois.
  livre->serie = proxima_serie_++;
  livre->classe = cls;
  livre->estado = kMmEstadoOcioso;
  mem_.Escrever32(endereco + kOffObjVtable, saidas_.Endereco(kVtableDoMedia));
  mem_.Escrever32(endereco + kOffObjRefs, 1);  // quem recebe o objecto ja o detem
  mem_.Escrever32(endereco + kOffObjEstado, static_cast<std::uint32_t>(kMmEstadoOcioso));
  mem_.Escrever32(endereco + kOffObjClasse, cls);
  mem_.Escrever32(endereco + kOffObjAvisoFn, 0);
  mem_.Escrever32(endereco + kOffObjAvisoUsuario, 0);
  mem_.Escrever32(endereco + kOffObjAmostrasTotal, 0);
  mem_.Escrever32(endereco + kOffObjPosicao, 0);
  mem_.Escrever8(endereco + kOffObjProntoMedidoNoDdragonz, 0);
  mem_.Escrever32(pponovo, endereco);
  ++classes_pedidas_[cls];
  ++pedidos_aceitos_;
  traco_.Emitir(Area::Audio, Nivel::Informacao, "IMEDIA_CRIADO",
                std::string(NomeDaClasse(cls)) + " obj=" + EmHex(endereco) +
                    " -> *ppobj=" + EmHex(pponovo));
  return kAeeSucesso;
}

void Media::EmitirAviso(Objeto& o, std::int32_t comando, std::int32_t sub,
                        std::int32_t status, std::uint32_t dados, std::uint32_t tamanho) {
  if (o.fn == 0) {
    // O SDK diz que registar o callback e OPCIONAL. Sem callback registado nao
    // ha aviso a entregar -- e isso e diferente de um aviso perdido, por isso
    // fica escrito no traco.
    traco_.Emitir(Area::Audio, Nivel::Depuracao, "IMEDIA_AVISO_SEM_CALLBACK",
                  std::string(NomeDoComandoDeMidia(comando)) + " " +
                      NomeDoStatusDeMidia(status));
    return;
  }
  // O BLOCO DO AVISO SAI DO INDICE DO OBJECTO, e nao de um contador que roda:
  // dois objectos vivos NUNCA escrevem no mesmo bloco. A regiao esta dimensionada
  // para `kMaxObjetosDeMidia` objectos (os `static_assert` no topo deste
  // ficheiro), e um objecto fora dela escreveria o aviso em cima do bloco de
  // outro -- por isso a guarda RECUSA em voz alta (P2) em vez de escrever.
  std::uint32_t endereco = 0;
  if (!EnderecoDoAviso(o, &endereco, nullptr)) return;
  // O `AEEMediaCmdNotify` que o callback recebe. Os deslocamentos 8 e 16 sao os
  // medidos no tratador do `cnk2` (ver o cabecalho do imedia.h).
  mem_.Escrever32(endereco + kOffAvisoClsMedia, o.classe);
  mem_.Escrever32(endereco + kOffAvisoPIMedia, o.endereco);
  mem_.Escrever32(endereco + kOffAvisoCmd, static_cast<std::uint32_t>(comando));
  mem_.Escrever32(endereco + kOffAvisoSubCmd, static_cast<std::uint32_t>(sub));
  mem_.Escrever32(endereco + kOffAvisoStatus, static_cast<std::uint32_t>(status));
  mem_.Escrever32(endereco + kOffAvisoDados, dados);
  mem_.Escrever32(endereco + kOffAvisoTamanho, tamanho);

  Aviso a;
  a.objeto = o.endereco;
  a.serie = o.serie;
  a.comando = comando;
  a.sub_comando = sub;
  a.status = status;
  a.dados = dados;
  a.tamanho = tamanho;
  a.fn = o.fn;
  a.usuario = o.usuario;
  a.endereco = endereco;
  fila_.push_back(a);
  ++avisos_emitidos_;
  traco_.Emitir(Area::Audio, Nivel::Informacao, "IMEDIA_AVISO",
                std::string(NomeDoComandoDeMidia(comando)) + " " + NomeDoStatusDeMidia(status) +
                    " obj=" + EmHex(o.endereco) + " serie=" + std::to_string(o.serie) +
                    " fn=" + EmHex(o.fn) +
                    " nCmd=" + std::to_string(comando) + " nStatus=" + std::to_string(status));
}

bool Media::EntregarAviso(ICpu& cpu, std::uint32_t sentinela, std::uint64_t limite_de_passos) {
  Aviso a;
  if (!RetirarAviso(&a)) return false;

  // 0. A IDENTIDADE, QUE E A REGRA MAIS FACIL DE PERDER.
  //
  // O aviso nasceu com um objecto, e entre esse instante e este o GUEST CONTINUOU
  // A CORRER: pode ter soltado esse objecto e ter criado outro, que ficou com o
  // MESMO endereco -- o recolhedor entrega os enderecos outra vez, e isso e o
  // comportamento medido (e o da arvore antiga, que reciclava por
  // `free_object_addresses_`). O `pIMedia` do aviso e um ENDERECO, e o jogo
  // correla o aviso por ele (`AEEIMedia.h`, "Callback Events"); entregar este
  // aviso agora diria ao jogo que o `DONE` de um som era o `DONE` de outro.
  //
  // Os tres casos sao diferentes, e nenhum deles e "descartar sempre":
  //   - mesmo objecto (serie igual)                  -> entregar;
  //   - soltado, endereco ainda sem dono             -> ENTREGAR: o `fn` e o
  //     `pUser` foram congelados no nascimento, e o `zeebx` mediu que um jogo
  //     espera o `DONE` de um som que parou e soltou (ver `Aviso`);
  //   - endereco ja de OUTRO objecto                 -> DESCARTAR e REGISTAR.
  const Objeto* dono = PorEndereco(a.objeto);
  if (dono != nullptr && dono->serie != a.serie) {
    ++avisos_descartados_;
    Recusar("IMedia::EntregarAviso",
            "aviso da serie " + std::to_string(a.serie) + " (" + EmHex(a.objeto) + ", " +
                NomeDoStatusDeMidia(a.status) + ") chegou quando o endereco ja era do objecto "
                "serie " + std::to_string(dono->serie) + ": aviso descartado");
    return true;
  }

  if (a.fn == 0) {
    // Sem callback registado nao ha nada a chamar. Nao e um aviso perdido: e a
    // opcao que o SDK declara ("this step is optional"), e o `EmitirAviso` ja
    // deixou o evento no traco.
    return true;
  }
  // 1. GUARDAR. Os 16 registradores e o CPSR: o guest tem registradores vivos
  //    neste ponto, e uma chamada de callback que os deixe mudados estraga o
  //    quadro em curso DELE.
  std::array<std::uint32_t, 16> guardados{};
  for (int r = 0; r < 16; ++r) guardados[static_cast<std::size_t>(r)] = cpu.Get(r);
  const std::uint32_t cpsr_guardado = cpu.Cpsr();
  const std::uint32_t pc_do_laco = cpu.Get(kPC);

  // 2. CHAMAR como o SDK define: o `pUser` no r0, o `AEEMediaCmdNotify` no r1.
  cpu.Set(kR0, a.usuario);
  cpu.Set(kR1, a.endereco);
  cpu.Set(kLR, sentinela);
  cpu.Set(kPC, a.fn);

  std::uint64_t passos = 0;
  while (cpu.Get(kPC) != sentinela && passos < limite_de_passos) {
    cpu.Passo();
    ++passos;
  }
  const bool voltou = (cpu.Get(kPC) == sentinela);
  if (voltou) {
    ++avisos_entregues_;
  } else {
    ++avisos_nao_entregues_;
    // P2: o caminho que nao concluiu REGISTA, e nunca devolve sucesso e nao faz
    // nada. O endereco do callback e o pedido entram no detalhe, porque um
    // "callback nao voltou" sem nome nao se depura.
    Recusar("IMedia::EntregarAviso",
            "o callback 0x" + EmHex(a.fn) + " nao voltou em " + std::to_string(passos) +
                " passos (cmd=" + std::to_string(a.comando) + " status=" +
                std::to_string(a.status) + ")");
  }

  // 3. REPOR: os registradores (o r15 E o PC, logo este laco repoe tambem o PC),
  //    o CPSR, e -- explicitamente -- o PC do laco. A linha do PC e redudante com
  //    o laco de proposito: ela diz em voz alta o que a reposicao do r15 faz, e
  //    foi um teste de violacao (tirar so esta linha) que provou a redundancia
  //    em vez de a supor.
  for (int r = 0; r < 16; ++r) cpu.Set(r, guardados[static_cast<std::size_t>(r)]);
  cpu.SetCpsr(cpsr_guardado);
  cpu.Set(kPC, pc_do_laco);
  return true;
}

bool Media::RetirarAviso(Aviso* saida) {
  if (fila_.empty()) return false;
  *saida = fila_.front();
  fila_.erase(fila_.begin());
  return true;
}

void Media::Avancar(std::uint32_t amostras) {
  RecolherLibertados();
  // POR BLOCOS, e nao de uma vez: o misturador soma as vozes DENTRO de um bloco,
  // e um pedido maior do que o bloco tem de fechar varios. Sem isto, as amostras
  // acima do limite do bloco nao seriam contadas -- um numero a menos, do tipo
  // que este projeto ja pagou caro para aprender a nao ter.
  std::uint32_t restante = amostras;
  const std::uint32_t tamanho_do_bloco =
      static_cast<std::uint32_t>(audio::Misturador::kAmostrasPorBlocoMax);
  while (restante > 0) {
  const std::uint32_t passo = (restante < tamanho_do_bloco) ? restante : tamanho_do_bloco;
  for (auto& o : objetos_) {
    if (!o.vivo || o.estado != kMmEstadoTocando) continue;
    const std::size_t total = o.amostras.size();
    if (total == 0) continue;
    const std::size_t restantes = total - o.posicao;
    const std::size_t quantas = (passo < restantes) ? passo : restantes;
    // AS AMOSTRAS CONSUMIDAS PASSAM PELO MISTURADOR, que as CONTA. Nao ha som:
    // ha o numero. `volume` e `mudo` vem dos parametros que o jogo pos.
    //
    // O DESLOCAMENTO E ZERO: todas as vozes escrevem no inicio do bloco, e e por
    // isso que duas vozes que tocam juntas SOMAM em vez de ficarem em fila.
    misturador_.MisturarNoBloco(o.amostras.data() + o.posicao, quantas, o.volume, o.mudo, 0);
    o.posicao += quantas;
    mem_.Escrever32(o.endereco + kOffObjPosicao, static_cast<std::uint32_t>(o.posicao));
    if (o.posicao >= total) {
      if (o.repetir == 0) {
        o.posicao = 0;  // MM_PARM_PLAY_REPEAT = 0: toca para sempre
      } else if (o.repetir > 1) {
        --o.repetir;
        o.posicao = 0;
      } else {
        // FIM DA MIDIA. Aqui e onde um aviso A MAIS apareceria (a cada volta do
        // laco depois do fim) e onde um aviso A MENOS apareceria (nunca). O
        // estado muda ANTES de o aviso nascer, e por isso nao ha segunda vez.
        o.estado = kMmEstadoPronto;
        o.posicao = 0;
        mem_.Escrever32(o.endereco + kOffObjEstado, static_cast<std::uint32_t>(kMmEstadoPronto));
        EmitirAviso(o, kMmCmdPlay, 0, kMmStatusDone, 0, 0);
      }
    }
  }
  misturador_.FecharBloco();
  restante -= passo;
  }
}

std::int32_t Media::DefinirDados(Objeto& o, std::int32_t p1, std::int32_t p2) {
  // `IMedia_SetMediaData` do SDK e este mesmo parametro:
  //   static __inline int IMedia_SetMediaData(IMedia *p, AEEMediaData *pmd)
  //   { return AEEGETPVTBL(p, IMedia)->SetMediaParm(p, MM_PARM_MEDIA_DATA,
  //                                                (int32)(pmd), (int32)(0)); }
  // (AEEIMedia.h, junto ao fim.) Nao ha slot proprio para "dar os dados".
  if (o.estado == kMmEstadoTocando || o.estado == kMmEstadoPausado ||
      o.estado == kMmEstadoGravando) {
    Recusar("IMedia::SetMediaParm(MM_PARM_MEDIA_DATA)",
            "objecto em reproducao (estado " + std::to_string(o.estado) + ")");
    return kAeeEstadoErrado;
  }
  if (p1 == 0) {
    Recusar("IMedia::SetMediaParm(MM_PARM_MEDIA_DATA)", "AEEMediaData nulo");
    return kAeeParametroErrado;
  }
  const std::uint32_t cls_data = mem_.Ler32(static_cast<std::uint32_t>(p1) + kOffMidiaClsData);
  const std::uint32_t p_data = mem_.Ler32(static_cast<std::uint32_t>(p1) + kOffMidiaPData);
  const std::uint32_t tam = mem_.Ler32(static_cast<std::uint32_t>(p1) + kOffMidiaDwSize);
  if (p2 != 0) {
    traco_.Emitir(Area::Audio, Nivel::Depuracao, "IMEDIA_SETDATA_N",
                  "n=" + std::to_string(p2) + " (do IMedia_SetMediaDataEx; ignorado)");
  }

  if (cls_data == kMmdBuffer) {
    // PCM de 16 bits com sinal, que e o que o `pData` de um `MMD_BUFFER` contem
    // nos titulos que o usam (a arvore antiga mediu `a3d` e `allstarcards` por
    // este caminho; NAO re-medido aqui).
    if (p_data == 0 || tam == 0) {
      Recusar("IMedia::SetMediaParm(MMD_BUFFER)", "pData=0x" + EmHex(p_data) + " dwSize=" +
                                                      std::to_string(tam));
      return kAeeParametroErrado;
    }
    if (tam > kMaiorBufer) {
      Recusar("IMedia::SetMediaParm(MMD_BUFFER)",
              "dwSize=" + std::to_string(tam) + " acima do limite declarado " +
                  std::to_string(kMaiorBufer));
      return kAeeParametroErrado;
    }
    if ((tam & 1u) != 0) {
      // Um numero impar de bytes nao e uma sequencia de amostras de 16 bits.
      // Aceitar e arredondar seria inventar som.
      Recusar("IMedia::SetMediaParm(MMD_BUFFER)",
              "dwSize=" + std::to_string(tam) + " impar para PCM de 16 bits");
      return kAeeParametroErrado;
    }
    std::vector<std::uint8_t> bruto(tam);
    mem_.LerBloco(p_data, bruto.data(), tam);
    o.amostras.resize(tam / 2);
    for (std::size_t k = 0; k < o.amostras.size(); ++k) {
      // Little-endian, montado byte a byte: a ordem dos bytes do Zeebo e do
      // ficheiro, e nao a do hospede.
      const std::uint16_t par = static_cast<std::uint16_t>(
          static_cast<std::uint16_t>(bruto[k * 2]) |
          (static_cast<std::uint16_t>(bruto[k * 2 + 1]) << 8));
      o.amostras[k] = static_cast<std::int16_t>(par);
    }
    o.tem_dados = true;
    o.estado = kMmEstadoPronto;  // o SDK: "SetMediaData puts IMedia in Ready state"
    o.posicao = 0;
    o.cls_data = static_cast<std::int32_t>(cls_data);
    o.p_data = p_data;
    o.tam_data = tam;
    mem_.Escrever32(o.endereco + kOffObjAmostrasTotal,
                    static_cast<std::uint32_t>(o.amostras.size()));
    mem_.Escrever32(o.endereco + kOffObjPosicao, 0);
    mem_.Escrever32(o.endereco + kOffObjEstado, static_cast<std::uint32_t>(o.estado));
    std::uint32_t nao_nulas = 0;
    for (const std::int16_t a : o.amostras) {
      if (a != 0) ++nao_nulas;
    }
    ++pedidos_aceitos_;
    traco_.Emitir(Area::Audio, Nivel::Informacao, "IMEDIA_DADOS",
                  "MMD_BUFFER " + std::to_string(tam) + " bytes -> " +
                      std::to_string(o.amostras.size()) + " amostras, " +
                      std::to_string(nao_nulas) + " nao nulas");
    return kAeeSucesso;
  }

  if (cls_data == kMmdNomeDeFicheiro) {
    std::string nome;
    if (p_data != 0) mem_.LerCadeia(p_data, &nome, 512);
    // RECUSA EM VOZ ALTA, com as DUAS causas separadas: um nome que o VFS nao
    // resolve e um codec que nao existe exigem correcoes completamente
    // diferentes, e juntas no log sao indistinguiveis (foi uma medicao da
    // arvore antiga, no `media_hle.cpp`).
    const bool existe = (vfs_ != nullptr) && vfs_->Existe(nome);
    Recusar("IMedia::SetMediaParm(MMD_FILE_NAME)",
            "'" + nome + "' " +
                (existe ? "esta no VFS, mas NAO ha descodificador de audio nesta arvore"
                        : "nao esta no VFS"));
    return kAeeNaoSuportado;
  }
  if (cls_data == kMmdFonte) {
    Recusar("IMedia::SetMediaParm(MMD_ISOURCE)", "fonte ISource nao implementada");
    return kAeeNaoSuportado;
  }
  Recusar("IMedia::SetMediaParm(MM_PARM_MEDIA_DATA)",
          "clsData " + EmHex(cls_data) + " nao e MMD_FILE_NAME/BUFFER/ISOURCE");
  return kAeeParametroErrado;
}

bool Media::FaixaValida(std::int32_t id, std::int32_t p1) {
  switch (id) {
    case kMmParmVolume:
      return p1 >= 0 && p1 <= static_cast<std::int32_t>(audio::kVolumeMaximo);
    case kMmParmMute:
      return p1 == 0 || p1 == 1;
    case kMmParmPan:
      return p1 >= 0 && p1 <= kMmMaxPan;
    case kMmParmTune:
      return VolumeDeTuneValido(p1);
    case kMmParmAudioPath:
      return p1 == kMmCaminhoLocal || p1 == kMmCaminhoRinger || p1 == kMmCaminhoRemoto ||
             p1 == kMmCaminhoAmbos;
    case kMmParmAudioDevice:
      return p1 >= 0 && p1 <= 0x7f;  // AEE_SOUND_DEVICE_LAST (AEEISound.h)
    case kMmParmPlayRepeat:
      return p1 >= 0;  // 0 = para sempre; 1 = uma vez; n>1 = n vezes
    case kMmParmPlayType:
      return p1 >= 1 && p1 <= 3;  // MM_PLAY_TYPE_NORMAL/RINGER/REMINDER
    case kMmParmTempo:
      return p1 > 0;  // percentagem: 100 e o valor por omissao
    case kMmParmRate:
      // "Zero denominator is treated as 1" (AEEIMedia.h, MM_PARM_RATE).
      return true;
    default:
      return true;
  }
}

std::int32_t Media::HandlerDeSlot(std::uint32_t slot, std::uint32_t objeto, ICpu& cpu) {
  Objeto* o = PorEndereco(objeto);
  if (o == nullptr) {
    Recusar("IMedia::" + std::string(slot < kQuantosSlotsDeclarados ? kTabelaDeSlotsDoMedia[slot].nome : "slot"),
            "po=" + EmHex(objeto) + " nao e um IMedia vivo");
    return kAeeParametroErrado;
  }

  switch (slot) {
    case 2: {  // QueryInterface(po, iid, ppo)
      const std::uint32_t iid = cpu.Get(kR1);
      const std::uint32_t ppo = cpu.Get(kR2);
      if (iid == kIidMedia) {
        // O SDK: "If an interface is retrieved, then this function increments
        // its reference count." (AEEIMedia.h, IMedia_QueryInterface.)
        if (ppo != 0) mem_.Escrever32(ppo, objeto);
        const std::uint32_t n = mem_.Ler32(objeto + kOffObjRefs) + 1;
        mem_.Escrever32(objeto + kOffObjRefs, n);
        return kAeeSucesso;
      }
      if (ppo != 0) mem_.Escrever32(ppo, 0);
      Recusar("IMedia::QueryInterface", "iid " + EmHex(iid) + " nao e AEEIID_IMedia");
      return kAeeClasseNaoSuportada;
    }
    case 3: {  // RegisterNotify(po, PFNMEDIANOTIFY, void *pUser)
      const std::uint32_t fn = cpu.Get(kR1);
      if (fn == 0) {
        Recusar("IMedia::RegisterNotify", "callback nulo");
        return kAeeParametroErrado;
      }
      o->fn = fn;
      o->usuario = cpu.Get(kR2);
      mem_.Escrever32(o->endereco + kOffObjAvisoFn, fn);
      mem_.Escrever32(o->endereco + kOffObjAvisoUsuario, o->usuario);
      ++pedidos_aceitos_;
      traco_.Emitir(Area::Audio, Nivel::Informacao, "IMEDIA_REGISTER_NOTIFY",
                    "obj=" + EmHex(objeto) + " fn=" + EmHex(fn) +
                        " user=" + EmHex(o->usuario));
      return kAeeSucesso;
    }
    case 4: {  // SetMediaParm(po, nParamID, int32 p1, int32 p2)
      const std::int32_t id = static_cast<std::int32_t>(cpu.Get(kR1));
      const std::int32_t p1 = static_cast<std::int32_t>(cpu.Get(kR2));
      const std::int32_t p2 = static_cast<std::int32_t>(cpu.Get(kR3));
      const ParametroDeMidia* par = ParametroPorId(id);
      if (par == nullptr) {
        Recusar("IMedia::SetMediaParm", "parametro " + std::to_string(id) + " nao existe no SDK");
        return kAeeParametroErrado;
      }
      if (par->no_set == TratamentoDeParametro::SoDeLeitura) {
        Recusar("IMedia::SetMediaParm", std::string(par->nome) + " e so de leitura no SDK");
        return kAeeNaoSuportado;
      }
      if (par->no_set == TratamentoDeParametro::Recusado) {
        Recusar("IMedia::SetMediaParm", std::string(par->nome) + " sem implementacao");
        return kAeeNaoSuportado;
      }
      if (!FaixaValida(id, p1)) {
        Recusar("IMedia::SetMediaParm",
                std::string(par->nome) + " com valor fora da faixa: " + std::to_string(p1));
        return kAeeParametroErrado;
      }
      if (id == kMmParmMediaData) return DefinirDados(*o, p1, p2);
      if (id == kMmParmVolume) {
        o->volume = static_cast<std::uint32_t>(p1);
      } else if (id == kMmParmMute) {
        o->mudo = (p1 != 0);
      } else if (id == kMmParmPlayRepeat) {
        o->repetir = p1;
      }
      if (par->no_set == TratamentoDeParametro::Guardado) {
        // GUARDADO NAO E APLICADO, e isso e DITO: um evento por cada aceitacao,
        // com o nome e o valor. Aceitar em silencio seria o stub que o P2
        // proibe; recusar tudo partiria um titulo que so quer pôr o volume do
        // ringer.
        o->guardados[id] = p1;
        traco_.Emitir(Area::Audio, Nivel::Informacao, "IMEDIA_PARM_GUARDADO",
                      std::string(par->nome) + "=" + std::to_string(p1) +
                          " (guardado, nao aplicado ao som)");
      } else {
        o->guardados[id] = p1;
      }
      if (p2 != 0 && id != kMmParmAudioPath) {
        traco_.Emitir(Area::Audio, Nivel::Depuracao, "IMEDIA_PARM_P2",
                      std::string(par->nome) + " p2=" + std::to_string(p2) + " (ignorado)");
      }
      ++pedidos_aceitos_;
      return kAeeSucesso;
    }
    case 5: {  // GetMediaParm(po, nParamID, int32 *pP1, int32 *pP2)
      const std::int32_t id = static_cast<std::int32_t>(cpu.Get(kR1));
      const std::uint32_t pp1 = cpu.Get(kR2);
      const std::uint32_t pp2 = cpu.Get(kR3);
      const ParametroDeMidia* par = ParametroPorId(id);
      if (par == nullptr) {
        Recusar("IMedia::GetMediaParm", "parametro " + std::to_string(id) + " nao existe no SDK");
        return kAeeParametroErrado;
      }
      if (par->no_get == TratamentoDeParametro::Recusado) {
        Recusar("IMedia::GetMediaParm", std::string(par->nome) + " sem implementacao");
        return kAeeNaoSuportado;
      }
      if (id == kMmParmClsid) {
        if (pp1 != 0) mem_.Escrever32(pp1, o->classe);
      } else if (id == kMmParmCaps) {
        // MM_CAPS_AUDIO: esta arvore so tem audio. Declarado, e nao medido --
        // nao ha video nem texto nem sincronizacao implementados.
        if (pp1 != 0) mem_.Escrever32(pp1, kMmCapsAudio);
        if (pp2 != 0) mem_.Escrever32(pp2, 0);
      } else if (id == kMmParmSeekCaps) {
        // A unica modalidade de busca implementada e por TEMPO. `p1 = lista de
        // modalidades, p2 = tamanho em bytes` (AEEIMedia.h, MM_PARM_SEEK_CAPS).
        if (pp1 != 0) {
          mem_.Escrever8(pp1, static_cast<std::uint8_t>(kMmSeekModoTempo));
        }
        if (pp2 != 0) mem_.Escrever32(pp2, 1);
      } else if (id == kMmParmMediaData) {
        if (pp1 != 0) {
          mem_.Escrever32(pp1 + kOffMidiaClsData, static_cast<std::uint32_t>(o->cls_data));
          mem_.Escrever32(pp1 + kOffMidiaPData, o->p_data);
          mem_.Escrever32(pp1 + kOffMidiaDwSize, o->tam_data);
        }
      } else if (id == kMmParmRate) {
        const auto it = o->guardados.find(id);
        if (pp2 != 0) {
          mem_.Escrever32(pp2, it != o->guardados.end() ? static_cast<std::uint32_t>(it->second)
                                                        : 1u);
        }
      } else {
        const auto it = o->guardados.find(id);
        if (pp1 != 0) {
          mem_.Escrever32(pp1, it != o->guardados.end() ? static_cast<std::uint32_t>(it->second)
                                                        : static_cast<std::uint32_t>(-1));
        }
      }
      ++pedidos_aceitos_;
      return kAeeSucesso;
    }
    case 6: {  // Play(po)
      if (!o->tem_dados) {
        Recusar("IMedia::Play", "sem dados (o SDK exige MM_PARM_MEDIA_DATA antes)");
        return kAeeEstadoErrado;
      }
      if (o->estado == kMmEstadoGravando) {
        Recusar("IMedia::Play", "gravando");
        return kAeeEstadoErrado;
      }
      if (o->estado == kMmEstadoTocando) {
        // Um segundo `Play` no mesmo objecto: e assim que um jogo RECLAMA um
        // canal (medido na arvore antiga no `ddragonz`). O pedido ANTIGO nao
        // pode ficar sem aviso -- seria um aviso a menos --, e o status dele e
        // ABORT, e nao DONE.
        EmitirAviso(*o, kMmCmdPlay, 0, kMmStatusAbort, 0, 0);
        o->posicao = 0;
      } else if (o->estado == kMmEstadoPausado) {
        o->estado = kMmEstadoTocando;  // retoma de onde estava
      } else {
        o->posicao = 0;
        o->estado = kMmEstadoTocando;
      }
      mem_.Escrever32(o->endereco + kOffObjEstado, static_cast<std::uint32_t>(o->estado));
      mem_.Escrever32(o->endereco + kOffObjAmostrasTotal,
                      static_cast<std::uint32_t>(o->amostras.size()));
      // PISTA, nao medicao (ver o cabecalho do imedia.h): o byte que a arvore
      // antiga mediu no `ddragonz`.
      mem_.Escrever8(o->endereco + kOffObjProntoMedidoNoDdragonz, 1);
      ++pedidos_aceitos_;
      traco_.Emitir(Area::Audio, Nivel::Informacao, "IMEDIA_PLAY",
                    "obj=" + EmHex(objeto) + " amostras=" + std::to_string(o->amostras.size()) +
                        " repetir=" + std::to_string(o->repetir) + " vol=" +
                        std::to_string(o->volume));
      return kAeeSucesso;
    }
    case 7:  // Record
      Recusar("IMedia::Record", "gravacao nao implementada");
      return kAeeNaoSuportado;
    case 8: {  // Stop(po)
      const bool em_curso =
          (o->estado == kMmEstadoTocando || o->estado == kMmEstadoPausado) && o->tem_dados;
      if (em_curso) {
        o->estado = kMmEstadoPronto;
        o->posicao = 0;
        mem_.Escrever32(o->endereco + kOffObjEstado, static_cast<std::uint32_t>(o->estado));
        // DONE, e nao ABORT: MEDIDO no tratador do `cnk2` (0x00101a80), onde o
        // status 2 cai num tratador a serio (0x0010352c) e o 3 sai logo
        // (0x001034b8, `add r3,r3,#0x28 / sub r0,r0,#1 / str r3,[r1] / bx lr`).
        EmitirAviso(*o, kMmCmdPlay, 0, kMmStatusDone, 0, 0);
      }
      // STOP SEM REPRODUCAO EM CURSO NAO AVISA. E aqui que uma notificacao A
      // MAIS nasceria: o pedido antigo foi avisado quando acabou, e um segundo
      // aviso faria o jogo descontar duas vezes o mesmo som.
      ++pedidos_aceitos_;
      traco_.Emitir(Area::Audio, Nivel::Informacao, "IMEDIA_STOP",
                    "obj=" + EmHex(objeto) + (em_curso ? " (em curso -> DONE)" : " (nada a parar)"));
      return kAeeSucesso;
    }
    case 9: {  // Seek(po, AEEMediaSeek eSeek, int32 lSeekValue)
      // `AEEMediaSeek` e `int8` no ARM (AEEIMedia.h: `typedef int8 AEEMediaSeek`).
      const std::int32_t eSeek = static_cast<std::int8_t>(cpu.Get(kR1) & 0xFFu);
      const std::int32_t valor = static_cast<std::int32_t>(cpu.Get(kR2));
      const std::int32_t modo = eSeek & 0xF0;
      const std::int32_t ref = eSeek & 0x0F;
      if (modo != kMmSeekModoTempo) {
        Recusar("IMedia::Seek", "modalidade " + std::to_string(modo) + " (so MM_SEEK_MODE_TIME)");
        return kAeeNaoSuportado;
      }
      if (!o->tem_dados || o->amostras.empty()) {
        Recusar("IMedia::Seek", "sem dados");
        return kAeeEstadoErrado;
      }
      const std::int64_t total = static_cast<std::int64_t>(o->amostras.size());
      std::int64_t alvo = 0;
      if (ref == kMmSeekInicio) {
        alvo = static_cast<std::int64_t>(valor) * kTaxaDeclarada / 1000;
      } else if (ref == kMmSeekActual) {
        alvo = static_cast<std::int64_t>(o->posicao) + static_cast<std::int64_t>(valor) *
                                                          kTaxaDeclarada / 1000;
      } else if (ref == kMmSeekFim) {
        alvo = total + static_cast<std::int64_t>(valor) * kTaxaDeclarada / 1000;
      } else {
        Recusar("IMedia::Seek", "referencia " + std::to_string(ref) + " desconhecida");
        return kAeeParametroErrado;
      }
      if (alvo < 0 || alvo > total) {
        Recusar("IMedia::Seek", "alvo " + std::to_string(alvo) + " fora de [0," +
                                    std::to_string(total) + "]");
        return kAeeParametroErrado;
      }
      o->posicao = static_cast<std::size_t>(alvo);
      // O cabecalho do objecto espelha o estado: quem le a memoria do guest ve o
      // mesmo que quem chama os slots, e uma posicao que so existisse no C++
      // seria invisivel para a sonda.
      mem_.Escrever32(o->endereco + kOffObjPosicao, static_cast<std::uint32_t>(o->posicao));
      ++pedidos_aceitos_;
      return kAeeSucesso;
    }
    case 10:  // Pause(po)
      if (o->estado != kMmEstadoTocando) {
        Recusar("IMedia::Pause", "nao esta tocando");
        return kAeeEstadoErrado;
      }
      o->estado = kMmEstadoPausado;
      mem_.Escrever32(o->endereco + kOffObjEstado, static_cast<std::uint32_t>(o->estado));
      ++pedidos_aceitos_;
      return kAeeSucesso;
    case 11:  // Resume(po)
      if (o->estado != kMmEstadoPausado) {
        Recusar("IMedia::Resume", "nao esta em pausa");
        return kAeeEstadoErrado;
      }
      o->estado = kMmEstadoTocando;
      mem_.Escrever32(o->endereco + kOffObjEstado, static_cast<std::uint32_t>(o->estado));
      ++pedidos_aceitos_;
      return kAeeSucesso;
    case 12: {  // GetTotalTime(po)
      if (!o->tem_dados) {
        Recusar("IMedia::GetTotalTime", "sem dados");
        return kAeeEstadoErrado;
      }
      // O tempo total vem da TAXA DECLARADA (`kTaxaDeclarada`): nao ha
      // descodificador de WAV nesta arvore, logo este numero NAO e medido do
      // ficheiro. O SDK entrega o valor pelo aviso (`pCmdData` = uint32 com os
      // milissegundos), e e por isso que aqui nasce UM aviso, com DONE.
      const std::uint32_t ms = static_cast<std::uint32_t>(
          static_cast<std::uint64_t>(o->amostras.size()) * 1000 / kTaxaDeclarada);
      std::uint32_t dados = 0;
      if (!EnderecoDoAviso(*o, nullptr, &dados)) return kAeeFalhou;
      mem_.Escrever32(dados, ms);
      EmitirAviso(*o, kMmCmdGetTotalTime, 0, kMmStatusDone, dados, 4);
      ++pedidos_aceitos_;
      return kAeeSucesso;
    }
    case 13: {  // GetState(po, boolean *pbStateChanging)
      // `pbStateChanging` diz se o estado esta a MUDAR. Nao esta: as mudancas
      // acontecem dentro do handler e o estado que se devolve ja e o novo.
      if (cpu.Get(kR1) != 0) mem_.Escrever8(cpu.Get(kR1), 0);
      ++pedidos_aceitos_;
      return o->estado;
    }
    default:
      Recusar("IMedia::slot", std::to_string(slot) + " sem handler");
      return kAeeNaoSuportado;
  }
}

bool Media::Atender(std::uint32_t indice, ICpu& cpu) {
  if (indice < kBaseDoMedia || indice >= kBaseDoMedia + kSlotsDoMedia) return false;
  const std::uint32_t slot = indice - kBaseDoMedia;
  if (slot < 2) return false;  // IBase (AddRef/Release) e do motor
  const std::int32_t r = HandlerDeSlot(slot, cpu.Get(kR0), cpu);
  cpu.Set(kR0, static_cast<std::uint32_t>(r));
  return true;
}


// ---------------------------------------------------------------------------
// A TABELA DECLARADA DO IMEDIA, e a guarda que a confere.
//
// E UMA lista, e serve as tres coisas que tem de concordar: os nomes que o
// registo imprime, o que o `Instalar` escreve na vtable, e a conferencia de que
// todos os 14 slots estao preenchidos. **Duas listas que tem de concordar sao
// zero listas** -- esta escrito no ledger deste projeto, e ja se perdeu uma
// implementacao por causa disso (o `SetTimer` ficou escrito em todo o lado menos
// na vtable).
const SlotDoMedia kTabelaDeSlotsDoMedia[] = {
    {0, "IBase::AddRef"},          {1, "IBase::Release"},
    {2, "IMedia::QueryInterface"}, {3, "IMedia::RegisterNotify"},
    {4, "IMedia::SetMediaParm"},   {5, "IMedia::GetMediaParm"},
    {6, "IMedia::Play"},           {7, "IMedia::Record"},
    {8, "IMedia::Stop"},           {9, "IMedia::Seek"},
    {10, "IMedia::Pause"},         {11, "IMedia::Resume"},
    {12, "IMedia::GetTotalTime"},  {13, "IMedia::GetState"},
};
const std::size_t kQuantosSlotsDoMediaDeclarados =
    sizeof(kTabelaDeSlotsDoMedia) / sizeof(kTabelaDeSlotsDoMedia[0]);

ResultadoCablagem ConferirTabelaDeSlots(const SlotDoMedia* slots, std::size_t quantas,
                                        std::uint32_t quantos_slots) {
  for (std::uint32_t s = 0; s < quantos_slots; ++s) {
    std::uint32_t quantos = 0;
    for (std::size_t k = 0; k < quantas; ++k) {
      if (slots[k].slot == s) ++quantos;
    }
    if (quantos != 1) {
      // O MOTIVO diz QUAL slot e quantas vezes: a primeira versao desta guarda
      // dizia so "tabela invalida", e um relatorio que nao nomeia o que falta
      // obriga a procurar a mao.
      return {false, "slot " + std::to_string(s) + " declarado " +
                         std::to_string(quantos) + " vezes"};
    }
  }
  return {true, ""};
}

}  // namespace zb2::brew
