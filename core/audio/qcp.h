#ifndef ZB2_CORE_AUDIO_QCP_H
#define ZB2_CORE_AUDIO_QCP_H

#include <cstdint>
#include <string>
#include <vector>

namespace zb2::audio {

enum class CodecQcp { Desconhecido, Qcelp, Evrc };

// Resultado de uma descodificacao QCP/PureVoice inteiramente em memoria.
// `motivo` fica vazio somente quando `ok()` e verdadeiro.
struct Qcp {
  CodecQcp codec = CodecQcp::Desconhecido;
  std::uint32_t taxa = 0;
  std::uint16_t canais = 0;
  std::vector<std::int16_t> amostras;  // PCM S16 intercalado por canal
  std::string motivo;

  bool ok() const { return motivo.empty(); }
};

const char* NomeDoCodecQcp(CodecQcp codec);
Qcp DescodificarQcp(const std::vector<std::uint8_t>& bytes);

}  // namespace zb2::audio
#endif  // ZB2_CORE_AUDIO_QCP_H
