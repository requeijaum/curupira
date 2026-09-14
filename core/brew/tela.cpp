#include "core/brew/tela.h"

#include <algorithm>

namespace zb2::brew {

Tela::Tela() : pixels_(static_cast<std::size_t>(kLargura) * kAltura, 0) {}

void Tela::Limpar() {
  std::fill(pixels_.begin(), pixels_.end(), 0u);
  cor_ = 0;
  escritos_ = 0;
  ClipLimpo();
}

void Tela::Ponto(int x, int y) {
  if (x < static_cast<int>(clip_[0]) || y < static_cast<int>(clip_[1])) return;
  if (x >= static_cast<int>(clip_[0] + clip_[2])) return;
  if (y >= static_cast<int>(clip_[1] + clip_[3])) return;
  if (x < 0 || y < 0 || x >= kLargura || y >= kAltura) return;
  pixels_[static_cast<std::size_t>(y) * kLargura + x] = cor_;
  ++escritos_;
}

void Tela::Retangulo(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h,
                     bool cheio) {
  if (x >= static_cast<std::uint32_t>(kLargura) || y >= static_cast<std::uint32_t>(kAltura)) return;
  if (w > static_cast<std::uint32_t>(kLargura) - x) w = static_cast<std::uint32_t>(kLargura) - x;
  if (h > static_cast<std::uint32_t>(kAltura) - y) h = static_cast<std::uint32_t>(kAltura) - y;
  if (w == 0 || h == 0) return;
  if (cheio) {
    for (std::uint32_t j = 0; j < h; ++j) {
      for (std::uint32_t i = 0; i < w; ++i) {
        Ponto(static_cast<int>(x + i), static_cast<int>(y + j));
      }
    }
  } else {
    for (std::uint32_t i = 0; i < w; ++i) {
      Ponto(static_cast<int>(x + i), static_cast<int>(y));
      Ponto(static_cast<int>(x + i), static_cast<int>(y + h - 1));
    }
    for (std::uint32_t j = 0; j < h; ++j) {
      Ponto(static_cast<int>(x), static_cast<int>(y + j));
      Ponto(static_cast<int>(x + w - 1), static_cast<int>(y + j));
    }
  }
}

void Tela::Clip(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h) {
  clip_[0] = x;
  clip_[1] = y;
  clip_[2] = w;
  clip_[3] = h;
}

void Tela::ClipLimpo() {
  clip_[0] = 0;
  clip_[1] = 0;
  clip_[2] = kLargura;
  clip_[3] = kAltura;
}

std::uint32_t Tela::CoresDistintas() const {
  std::set<std::uint32_t> s(pixels_.begin(), pixels_.end());
  return static_cast<std::uint32_t>(s.size());
}

std::uint32_t Tela::CoresEm(int x, int y, int w, int h) const {
  std::set<std::uint32_t> s;
  for (int j = y; j < y + h && j < kAltura; ++j) {
    if (j < 0) continue;
    for (int i = x; i < x + w && i < kLargura; ++i) {
      if (i < 0) continue;
      s.insert(pixels_[static_cast<std::size_t>(j) * kLargura + i]);
    }
  }
  return static_cast<std::uint32_t>(s.size());
}

}  // namespace zb2::brew
