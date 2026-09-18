#ifndef ZB2_CORE_VIDEO_ATITC_H
#define ZB2_CORE_VIDEO_ATITC_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/video/rasterizador.h"

namespace zb2::video {

enum class FormatoAtitc { Rgb, RgbaExplicito };

// Decodifica blocos crus ATITC em texels RGBA8, em ordem row-major. O header
// QXT e removido pelo codigo QX do guest antes de glCompressedTexImage2D.
std::optional<std::vector<Rgba>> DescodificarAtitc(const std::uint8_t* dados,
                                                    std::size_t tamanho,
                                                    std::uint32_t largura,
                                                    std::uint32_t altura,
                                                    FormatoAtitc formato);

}  // namespace zb2::video
#endif
