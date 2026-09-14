#ifndef ZB2_CORE_BREW_TELA_H
#define ZB2_CORE_BREW_TELA_H

// A tela, em software.
//
// EXISTE PARA HAVER UMA MEDIDA VISUAL que nao dependa de capturar ecra: quantos
// pixels distintos cada titulo escreveu, e de que cor. E a versao honesta de "o
// jogo desenha" -- na arvore antiga o censo media a IMAGEM, e por isso dizia
// "sem desenho" sobre titulos que desenhavam.
//
// VIVE NO MOTOR: a tela e do emulador, nao da ferramenta que a observa.

#include <cstdint>
#include <set>
#include <vector>

namespace zb2::brew {

class Tela {
 public:
  static constexpr int kLargura = 640;
  static constexpr int kAltura = 480;

  Tela();

  void Limpar();
  void Ponto(int x, int y);
  // LIMITE ANTES DE PERCORRER, e nao so dentro do `Ponto`.
  //
  // O `Ponto` recusa o que sai do ecra, mas o laco corria na mesma `w*h` vezes.
  // Com uma rect grande vinda do guest isso sao milhares de milhoes de iteracoes:
  // medido, mais de 900 s para UM titulo. **Um limite verificado so no destino
  // nao limita o trabalho.**
  void Retangulo(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h, bool cheio);

  void CorAtual(std::uint32_t rgb565) { cor_ = rgb565 & 0xFFFFu; }
  std::uint32_t Cor() const { return cor_; }

  void Clip(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h);
  void ClipLimpo();
  const std::uint32_t* ClipAtual() const { return clip_; }

  std::uint32_t Escritos() const { return escritos_; }
  std::uint32_t CoresDistintas() const;
  std::uint32_t CoresEm(int x, int y, int w, int h) const;

 private:
  std::vector<std::uint32_t> pixels_;
  std::uint32_t cor_ = 0;
  std::uint32_t escritos_ = 0;
  std::uint32_t clip_[4] = {0, 0, kLargura, kAltura};
};

}  // namespace zb2::brew

#endif
