#ifndef ZB2_CORE_BREW_SHA256_H
#define ZB2_CORE_BREW_SHA256_H

// SHA-256, para a IDENTIDADE da entrada.
//
// PORQUE ISTO EXISTE, e vem de um achado do sub-agente `regressoes`: o comparador
// de corridas deriva a configuracao da lista de `pasta/mod`, logo **dois corpus
// diferentes que mantenham os mesmos 62 pasta/mod sao indistinguiveis**. Duas
// dumps da mesma ROM dao o mesmo corpus e bytes diferentes, e comparar pixels
// entre entradas diferentes e exactamente o erro que a ferramenta existe para
// impedir.
//
// Escrito a mao em vez de trazer uma dependencia: sao ~60 linhas, e uma
// dependencia criptografica inteira para um resumo de ficheiro nao se paga. Tem
// VECTORES DE TESTE CONHECIDOS (FIPS 180-4) em `tests/sha256_test.cpp`.
//
// NAO e usado para seguranca. E usado para dizer "isto e a mesma entrada?".

#include <cstdint>
#include <string>

namespace zb2::brew {

// O resumo, em 64 caracteres hexadecimais minusculos.
std::string Sha256Hex(const std::uint8_t* dados, std::size_t quantos);
std::string Sha256Hex(const std::string& texto);

}  // namespace zb2::brew

#endif
