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

#include "core/brew/ecra.h"

namespace zb2::brew {

class Tela {
 public:
  // O TAMANHO VEM DE `ecra.h`. Nao ha aqui um 640 escrito a mao: se houvesse,
  // seria a segunda copia do numero, e foi assim que a arvore ficou com duas
  // resolucoes ao mesmo tempo.
  static constexpr int kLargura = static_cast<int>(kLarguraDoEcra);
  static constexpr int kAltura = static_cast<int>(kAlturaDoEcra);

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

  // RGBVAL (o que o IDisplay recebe) -> RGB565 (o que a tela guarda).
  //
  // O `MAKE_RGB` do SDK (`AEERGBVAL.h:24`) e `r << 8 | g << 16 | b << 24`: o
  // vermelho esta no SEGUNDO byte, nao no primeiro, e o byte baixo e alfa.
  // Truncar com `& 0xFFFF`, que era o que se fazia, guarda os bits ERRADOS:
  // `RGB_WHITE` (0xFFFFFF00) e `MAKE_RGB(255,0,0)` (0x0000FF00) davam AMBOS
  // 0xFF00 -- a mesma cor. Isso contaminava a coluna `CORES` da bateria, que e
  // uma das duas medidas de desenho que temos.
  //
  // A truncagem de 8 para 5/6 bits e a mesma do rasterizador
  // (`video/rasterizador.cpp`, `Para565`): descarta os bits baixos, nao
  // arredonda. As duas conversoes tem de dar o mesmo pixel para a mesma cor.
  // A PALETA DE ITENS DO IDisplay (`AEEClrItem`, 1..CLR_SYS_LAST-1).
  //
  // O `SetColor` do SDK nao poe "a cor de desenho": poe a cor de UM ITEM
  // nomeado (texto do utilizador, fundo, linha, titulo, ...) e DEVOLVE a que la
  // estava. O idioma do proprio cabecalho (`AEEIDisplay.h:134-136`) e guardar o
  // valor devolvido e repo-lo a seguir -- com o retorno a zero, repunha-se preto.
  std::uint32_t CorDoItem(std::uint32_t item) const {
    return item < kQuantosItens ? itens_[item] : 0u;
  }
  void DefinirCorDoItem(std::uint32_t item, std::uint32_t rgbval) {
    if (item < kQuantosItens) itens_[item] = rgbval;
  }

  static constexpr std::uint32_t RgbvalPara565(std::uint32_t rgbval) {
    const std::uint32_t r = (rgbval >> 8) & 0xFFu;
    const std::uint32_t g = (rgbval >> 16) & 0xFFu;
    const std::uint32_t b = (rgbval >> 24) & 0xFFu;
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
  }
  std::uint32_t Cor() const { return cor_; }

  void Clip(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h);
  void ClipLimpo();
  const std::uint32_t* ClipAtual() const { return clip_; }

  std::uint32_t Escritos() const { return escritos_; }
  std::uint32_t CoresDistintas() const;
  std::uint32_t CoresEm(int x, int y, int w, int h) const;

 private:
  // `CLR_SYS_LAST` e 17 (`AEEIDisplay.h:156`); 32 da folga para um item fora da
  // enumeracao sem sair do array. Zero = preto, que e o que o BREW mostra num
  // item que nunca foi posto.
  static constexpr std::uint32_t kQuantosItens = 32;

  std::vector<std::uint32_t> pixels_;
  std::uint32_t cor_ = 0;
  std::uint32_t escritos_ = 0;
  std::uint32_t clip_[4] = {0, 0, kLargura, kAltura};
  std::uint32_t itens_[kQuantosItens] = {0};
};

}  // namespace zb2::brew

#endif
