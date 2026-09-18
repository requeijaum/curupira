#include "core/audio/duracao.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace zb2::audio {
namespace {

std::uint32_t Be32(const std::uint8_t* p) {
  return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
         (std::uint32_t(p[2]) << 8) | p[3];
}
bool Vlq(const std::vector<std::uint8_t>& d, std::size_t* p, std::size_t fim, std::uint32_t* v) {
  std::uint32_t r = 0;
  for (int i = 0; i < 4 && *p < fim; ++i) {
    const std::uint8_t b = d[(*p)++];
    r = (r << 7) | (b & 127);
    if (!(b & 128)) { *v = r; return true; }
  }
  return false;
}
std::optional<Cronometragem> Mp3(const std::vector<std::uint8_t>& d) {
  std::size_t p = 0;
  if (d.size() >= 10 && d[0] == 'I' && d[1] == 'D' && d[2] == '3') {
    std::size_t tag = 0;
    for (int i = 6; i < 10; ++i) tag = (tag << 7) | (d[i] & 127);
    p = tag + 10;
  }
  constexpr std::uint32_t taxa1[] = {44100,48000,32000}, taxa2[] = {22050,24000,16000}, taxa25[] = {11025,12000,8000};
  constexpr std::uint32_t bit1[] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
  constexpr std::uint32_t bit2[] = {0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};
  std::uint64_t amostras = 0; std::uint32_t taxa = 0;
  while (p + 4 <= d.size()) {
    const std::uint8_t a=d[p], b=d[p+1], c=d[p+2];
    if (a != 0xff || (b & 0xe0) != 0xe0 || ((b >> 1) & 3) != 1) break;
    const unsigned versao=(b>>3)&3, bi=(c>>4)&15, ri=(c>>2)&3;
    if (versao == 1 || bi == 0 || bi == 15 || ri == 3) return std::nullopt;
    const bool mpeg1=versao==3; const std::uint32_t rate=(mpeg1?taxa1:versao==2?taxa2:taxa25)[ri];
    const std::uint32_t bitrate=(mpeg1?bit1:bit2)[bi]*1000, por_frame=mpeg1?1152:576;
    const std::size_t tamanho=(mpeg1?144:72)*bitrate/rate + ((c>>1)&1);
    if (tamanho < 4 || p + tamanho > d.size()) return std::nullopt;
    if (taxa != 0 && taxa != rate) return std::nullopt;
    taxa=rate; amostras += por_frame; p += tamanho;
  }
  if (amostras == 0 || taxa == 0) return std::nullopt;
  return Cronometragem{TipoCronometrado::Mp3, static_cast<std::uint32_t>((amostras*1000 + taxa-1)/taxa)};
}
std::optional<Cronometragem> Midi(const std::vector<std::uint8_t>& d) {
  if (d.size() < 14 || std::memcmp(d.data(), "MThd", 4) != 0 || Be32(d.data() + 4) != 6) return std::nullopt;
  const std::uint16_t tracks = (std::uint16_t(d[10]) << 8) | d[11];
  const std::uint16_t divisao = (std::uint16_t(d[12]) << 8) | d[13];
  if (tracks == 0 || divisao == 0 || (divisao & 0x8000)) return std::nullopt;
  std::size_t p = 14;
  std::uint64_t max_tick = 0;
  std::vector<std::pair<std::uint64_t, std::uint32_t>> tempos;
  for (std::uint16_t t = 0; t < tracks; ++t) {
    if (p + 8 > d.size() || std::memcmp(d.data() + p, "MTrk", 4) != 0) return std::nullopt;
    const std::size_t fim = p + 8 + Be32(d.data() + p + 4);
    p += 8;
    if (fim > d.size()) return std::nullopt;
    std::uint64_t tick = 0;
    std::uint8_t running = 0;
    while (p < fim) {
      std::uint32_t delta = 0;
      if (!Vlq(d, &p, fim, &delta)) return std::nullopt;
      tick += delta;
      if (p >= fim) return std::nullopt;
      std::uint8_t status = d[p];
      if (status >= 0x80) ++p;
      else {
        if (running == 0) return std::nullopt;
        status = running;
      }
      if (status == 0xff) {
        if (p >= fim) return std::nullopt;
        const std::uint8_t tipo = d[p++];
        std::uint32_t n = 0;
        if (!Vlq(d, &p, fim, &n) || n > fim - p) return std::nullopt;
        if (tipo == 0x51 && n == 3) {
          tempos.emplace_back(tick, (std::uint32_t(d[p]) << 16) |
                                       (std::uint32_t(d[p + 1]) << 8) | d[p + 2]);
        }
        p += n;
      } else if (status == 0xf0 || status == 0xf7) {
        std::uint32_t n = 0;
        if (!Vlq(d, &p, fim, &n) || n > fim - p) return std::nullopt;
        p += n;
      } else if (status >= 0x80 && status < 0xf0) {
        running = status;
        const unsigned n = ((status & 0xe0) == 0xc0 || (status & 0xe0) == 0xd0) ? 1 : 2;
        if (n > fim - p) return std::nullopt;
        p += n;
      } else return std::nullopt;
    }
    max_tick = std::max(max_tick, tick);
  }
  std::sort(tempos.begin(), tempos.end());
  std::uint64_t anterior = 0, micros = 0;
  std::uint32_t tempo = 500000;
  for (const auto& e : tempos) {
    if (e.first > max_tick) break;
    micros += (e.first - anterior) * tempo / divisao;
    anterior = e.first;
    tempo = e.second;
  }
  micros += (max_tick - anterior) * tempo / divisao;
  return Cronometragem{TipoCronometrado::Midi, static_cast<std::uint32_t>((micros + 999) / 1000)};
}
} // namespace
std::optional<Cronometragem> CronometrarFluxo(const std::vector<std::uint8_t>& bytes) {
  if(bytes.size()>=4 && bytes[0]=='M'&&bytes[1]=='T'&&bytes[2]=='h'&&bytes[3]=='d')return Midi(bytes);
  return Mp3(bytes);
}
}  // namespace zb2::audio
