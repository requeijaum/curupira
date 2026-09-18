#ifndef ZB2_CORE_AUDIO_WAV_H
#define ZB2_CORE_AUDIO_WAV_H

#include <cstdint>
#include <optional>
#include <vector>

namespace zb2::audio {

struct Wav {
  std::uint32_t taxa = 0;
  std::uint16_t canais = 0;
  std::vector<std::int16_t> amostras;  // intercaladas por canal
};

// RIFF/WAVE PCM (8/16 bits) e IMA-ADPCM (format tag 17).
std::optional<Wav> DescodificarWav(const std::vector<std::uint8_t>& bytes);

}  // namespace zb2::audio
#endif
