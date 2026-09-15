#ifndef ZB2_CORE_CARGA_INFLATE_H
#define ZB2_CORE_CARGA_INFLATE_H

// INFLATE -- RFC1950 (zlib) e RFC1951 (deflate), sem uma unica excecao.
//
// PORQUE EXISTE: os recursos do Zeebo guardam as ROMs de Neo Geo dentro de
// ficheiros `.pkg`, e CADA ENTRADA de um `.pkg` e um stream zlib CRU (nao gzip).
// Sem um descompressor dentro do emulador, uma unica ROM desses titulos nao pode
// ser lida -- e a familia inteira fica no arranque. O formato do pacote esta
// medido em `core/carga/pack.h`.
//
// ---------------------------------------------------------------------------
// NADA AQUI LANCA
// ---------------------------------------------------------------------------
// O projeto nao usa excecoes em uso: uma recusa e um `bool` falso com o MOTIVO
// escrito, e o motivo diz QUAL campo falhou e com que valores (principio P2, o
// mesmo que produziu `tools/regressao.sh` a sair 77 em vez de "passou").
//
// Uma excecao a escapar de um parser binario nao e uma recusa: e um processo a
// morrer a meio de uma leitura, que e exactamente o que o projeto antigo fazia
// com um stub que devolvia sucesso e nao fazia nada. Aqui nenhuma leitura
// indexa fora do buffer: o leitor de bits responde "acabou" e o descompressor
// transforma isso em motivo.
//
// ---------------------------------------------------------------------------
// O QUE E CONFERIDO, e o que se recusa (RFC1950 secao 2.2)
// ---------------------------------------------------------------------------
//   CMF = 0x78 em TODAS as 127 entradas dos 9 `.pkg` reais do corpus (medido):
//   (CMF & 0x0f) == 8  -> CM = 8, "deflate" e o unico metodo que o RFC1950 define;
//   (CMF >> 4)  == 7   -> CINFO = 7, janela de 32 KiB, o maior que o RFC permite;
//   ((CMF << 8) | FLG) % 31 == 0  -> o resto do cabecalho, como o RFC exige;
//   (FLG & 0x20) == 0  -> FDICT desligado; um dicionario predefinido e RECUSADO
//                         (nao ha dicionario nenhum nos ficheiros medidos, e
//                          inventar um seria descomprimir dados errados);
//   o adler32 do fim e RECALCULADO sobre a saida e comparado com o do stream.
//
// E o RFC1951: blocos stored (00), fixed (01) e dynamic (10); BTYPE=11 e
// reservado e RECUSADO com o valor medido. O comprimento do bloco stored tem de
// ter `LEN` e `NLEN` complementares. Nos arvores de Huffman, um codigo
// sobre-subscrito e recusado; um codigo INCOMPLETO e aceite so onde o RFC1951 o
// permite (a arvore fixa das distancias, 3.2.6, declara 30 codigos de 5 bits e
// deixa dois por usar -- os valores 30 e 31 "nunca ocorrem") ou onde todos os
// codigos tem comprimento <= 1 (caso do bloco com um unico simbolo).
//
// ---------------------------------------------------------------------------
// AS TRES FORMAS DE BLOCO SAO PROVADAS, e nao supostas
// ---------------------------------------------------------------------------
// `tests/pack_test.cpp` descomprime tres streams do MESMO texto (1158 bytes) e
// compara os tres bytes a bytes:
//
//   nivel 0 (stored, 1169 bytes)    -> um bloco `00` com o texto em claro;
//   nivel 1 (fixed, 249 bytes)      -> arvore fixa, um bloco `01`;
//   nivel 9 (dynamic, 246 bytes)    -> arvore construida no proprio stream,
//                                      com os codigos 16/17/18 de repeticao.
//
// Um descompressor que so tivesse um destes caminhos passaria um teste de
// "descomprimiu" e falharia nos outros dois -- e a diferenca entre eles e
// exactamente onde um `inflate` artesanal se engana.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace zb2 {

// Adler32 do RFC1950 (secao 9, anexo). `semente` = 1 no inicio do stream.
//
// Exposto porque nao serve so para CONFERIR: um teste que MONTA um stream zlib
// (o do bloco stored; ver `tests/pack_test.cpp`) tem de escrever o adler32 certo
// -- e escreve-o com ESTA funcao, e nao com uma segunda copia da regra.
std::uint32_t Adler32(const std::uint8_t* dados, std::size_t n, std::uint32_t semente = 1u);

// Descomprime um stream zlib COMPLETO que comeca em `entrada`.
//
// `saida` fica com os bytes descomprimidos (e vazia numa recusa).
// `motivo` fica vazio quando devolve true, e com a razao medida quando devolve
// false. Pode ser nulo: quem nao quer saber o motivo nao e obrigado a olhar.
//
// `teto` > 0 limita a saida: mais bytes do que isso e recusa COM O LIMITE no
// motivo. E o que o `.pkg` sabe de antemao (`tamanho_descomprimido`), e o que
// impede um cabecalho mentiroso de encher a memoria do hospedeiro.
//
// `consumido`, quando nao nulo, sai com os bytes do stream que foram LIDOS
// (cabecalho + blocos + os 4 do adler32). Quem le de um recipiente maior -- o
// `.pkg` -- confere com isto que a entrada acaba onde o indice diz.
bool Inflar(const std::uint8_t* entrada, std::size_t tamanho, std::vector<std::uint8_t>* saida,
            std::string* motivo, std::uint32_t teto = 0u, std::size_t* consumido = nullptr);

}  // namespace zb2

#endif  // ZB2_CORE_CARGA_INFLATE_H
