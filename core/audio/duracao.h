#ifndef ZB2_CORE_AUDIO_DURACAO_H
#define ZB2_CORE_AUDIO_DURACAO_H

#include <cstdint>
#include <optional>
#include <vector>

namespace zb2::audio {

enum class TipoCronometrado { Mp3, Midi };
struct Cronometragem { TipoCronometrado tipo; std::uint32_t milissegundos; };

// Le MP3 Layer III ou Standard MIDI File e devolve duracao de relogio virtual.
std::optional<Cronometragem> CronometrarFluxo(const std::vector<std::uint8_t>& bytes);

}  // namespace zb2::audio
#endif
