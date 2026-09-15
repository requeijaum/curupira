#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

#include "core/memoria/memoria.h"
#include "core/tempo/tempo.h"
#include "core/traco/traco.h"

using zb2::Area;
using zb2::DestinoFicheiro;
using zb2::DestinoMemoria;
using zb2::Memoria;
using zb2::Nivel;
using zb2::Tempo;
using zb2::Traco;

// ===========================================================================
// TEMPO -- principio P4
// ===========================================================================

TEST(Tempo, SoAvancaQuandoAlguemAvanca) {
  // A propriedade que sustenta todas as medicoes: nada aqui le o relogio do
  // sistema. Se este teste falhar, o determinismo acabou.
  Tempo t;
  EXPECT_EQ(t.Agora(), 0);
  for (int i = 0; i < 100; ++i) EXPECT_EQ(t.Agora(), 0);
}

TEST(Tempo, AvancarSomaExactamente) {
  Tempo t;
  t.Avancar(16 * Tempo::kMs);
  t.Avancar(16 * Tempo::kMs);
  EXPECT_EQ(t.Agora(), 32 * Tempo::kMs);
}

TEST(Tempo, AvancarParaTrasENaoEngolidoEmSilencio) {
  // Um avanco negativo e um defeito de quem chama. No Zeebulator antigo havia
  // caminhos que comparavam instantes de corridas diferentes; se o relogio
  // aceitasse andar para tras em silencio, uma contagem invertida passaria
  // despercebida.
  Tempo t;
  t.Avancar(1000);
  t.Avancar(-5);
  EXPECT_EQ(t.Agora(), 1000) << "o tempo nao anda para tras";
  EXPECT_EQ(t.AvancosInvalidos(), 1u) << "e a tentativa fica contada";
  EXPECT_EQ(t.UltimoAvancoNegativo(), -5);
}

TEST(Tempo, ReporDevolveAoZeroParaDuasCorridasSeremComparaveis) {
  Tempo t;
  t.Avancar(5000);
  t.Repor();
  EXPECT_EQ(t.Agora(), 0);
  EXPECT_EQ(t.AvancosInvalidos(), 0u);
}

// ===========================================================================
// TRACO -- o modulo que impede os sete defeitos de instrumento
// ===========================================================================

TEST(Traco, CadaEmissaoTemNomeEOsNomesSaoContaveis) {
  // Defeito 1 e 2 do Zeebulator antigo: 71 handlers sem log, e uma linha
  // duplicada que parecia o jogo a chamar duas vezes. Aqui conta-se por nome.
  Tempo t;
  Traco tr("teste", &t);
  DestinoMemoria dm;
  tr.JuntarDestino(&dm);
  tr.Emitir(Area::Video, Nivel::Informacao, "BindTexture", "alvo=0xde1 nome=3");
  tr.Emitir(Area::Video, Nivel::Informacao, "BindTexture", "alvo=0xde1 nome=4");
  tr.Emitir(Area::Video, Nivel::Informacao, "Enable", "cap=0xb44");
  EXPECT_EQ(dm.QuantosComNome("BindTexture"), 2u);
  EXPECT_EQ(dm.QuantosComNome("Enable"), 1u);
  EXPECT_EQ(dm.eventos.size(), 3u);
}

TEST(Traco, EmissaoSemNomeViraErroVisivelEmVezDeLinhaMuda) {
  Tempo t;
  Traco tr("teste", &t);
  DestinoMemoria dm;
  tr.JuntarDestino(&dm);
  tr.Emitir(Area::Brew, Nivel::Informacao, "", "detalhe qualquer");
  ASSERT_EQ(dm.eventos.size(), 1u);
  EXPECT_EQ(dm.eventos[0].nome, "EMISSAO_SEM_NOME");
  EXPECT_EQ(dm.eventos[0].nivel, Nivel::Erro) << "nao passa como informacao";
}

TEST(Traco, LogRelativoERecusadoComMotivo) {
  // Defeito 3: os logs de depuracao nasceram na pasta do titulo, que e a midia
  // de ROM do utilizador. Um caminho relativo e recusado, e a recusa explica-se.
  DestinoFicheiro d("zeeb_wwatch.log");
  EXPECT_FALSE(d.Abriu());
  EXPECT_FALSE(d.Motivo().empty());
  EXPECT_NE(d.Motivo().find("relativo"), std::string::npos);
}

TEST(Traco, LogAbsolutoAbreEEscreve) {
  const std::string caminho = "/tmp/zb2_traco_teste.log";
  std::remove(caminho.c_str());
  {
    Tempo t;
    Traco tr("teste", &t);
    DestinoFicheiro d(caminho);
    ASSERT_TRUE(d.Abriu()) << d.Motivo();
    tr.JuntarDestino(&d);
    tr.Emitir(Area::Carga, Nivel::Informacao, "ModuloCarregado", "base=0x00100000");
  }
  std::FILE* f = std::fopen(caminho.c_str(), "rb");
  ASSERT_NE(f, nullptr);
  std::string conteudo;
  char buf[256];
  std::size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) conteudo.append(buf, n);
  std::fclose(f);
  EXPECT_NE(conteudo.find("ModuloCarregado"), std::string::npos);
  std::remove(caminho.c_str());
}

TEST(Traco, NaoImplementadoFicaContadoENomeado) {
  // Defeito 5 e 6: "sem linha no log" era lido como "nao aconteceu", e uma
  // vtable inteira podia estar por preencher sem ninguem saber. A recusa e
  // ruidosa e o que falta fica contado -- principio P2.
  Tempo t;
  Traco tr("teste", &t);
  DestinoMemoria dm;
  tr.JuntarDestino(&dm);
  tr.RegistarFalta(Area::Video, "glScissor", "slot 95 do IGLES11, 4057 chamadas medidas");
  tr.RegistarFalta(Area::Video, "glScissor", "segunda vez");
  tr.RegistarFalta(Area::Video, "glColorMask", "");
  ASSERT_EQ(tr.ContagemFaltas().size(), 2u);
  EXPECT_EQ(tr.ContagemFaltas().at("glScissor"), 2u);
  EXPECT_EQ(dm.Quantos(Area::Video, Nivel::Aviso), 3u);
  EXPECT_EQ(dm.eventos[0].nome, "NAO_IMPLEMENTADO: glScissor");
}

TEST(Traco, UmValorDECLARADOFicaContadoENomeado) {
  // Defeito 8: a classe "declarado" (um valor plausivel que NAO se mediu) vivia
  // num `Emitir(..., Informacao, ...)` e a bateria so publicava as faltas -- ou
  // seja, existia no codigo e era invisivel na corrida. Agora tem caminho
  // proprio, contagem por nome, e vai ao JSON da bateria.
  Tempo t;
  Traco tr("teste", &t);
  DestinoMemoria dm;
  tr.JuntarDestino(&dm);
  tr.RegistarPressuposto(Area::Entrada, "IHIDDevice::GetDeviceInfo", "VID/PID do comando");
  tr.RegistarPressuposto(Area::Entrada, "IHIDDevice::GetDeviceInfo", "segunda vez");
  tr.RegistarPressuposto(Area::Brew, "ICM::GetSSInfo", "sinal 4 barras");
  ASSERT_EQ(tr.ContagemPressupostos().size(), 2u);
  EXPECT_EQ(tr.ContagemPressupostos().at("IHIDDevice::GetDeviceInfo"), 2u);
  EXPECT_EQ(dm.eventos[0].nome, "PRESSUPOSTO: IHIDDevice::GetDeviceInfo");
  // FRONTEIRA: declarar NAO e recusar. Um pressuposto nao pode entrar nas
  // faltas -- se entrasse, a lista do que falta implementar passava a contar
  // caminhos que estao implementados, e a medida perdia o significado.
  EXPECT_TRUE(tr.ContagemFaltas().empty());
  EXPECT_EQ(dm.Quantos(Area::Entrada, Nivel::Aviso), 0u);
  EXPECT_EQ(dm.Quantos(Area::Entrada, Nivel::Informacao), 2u);
}

TEST(Traco, RegistarPressupostoSemNomeEUmErro) {
  Tempo t;
  Traco tr("teste", &t);
  DestinoMemoria dm;
  tr.JuntarDestino(&dm);
  tr.RegistarPressuposto(Area::Video, "", "");
  ASSERT_EQ(dm.eventos.size(), 1u);
  EXPECT_EQ(dm.eventos[0].nome, "PRESSUPOSTO_SEM_NOME");
  EXPECT_TRUE(tr.ContagemPressupostos().empty());
}

TEST(Traco, RegistarFaltaSemNomeEUmErro) {
  Tempo t;
  Traco tr("teste", &t);
  DestinoMemoria dm;
  tr.JuntarDestino(&dm);
  tr.RegistarFalta(Area::Video, "", "");
  ASSERT_EQ(dm.eventos.size(), 1u);
  EXPECT_EQ(dm.eventos[0].nome, "FALTA_SEM_NOME");
}

TEST(Traco, CompararRecusaConfiguracoesDiferentes) {
  // Defeito 4, e a licao mais cara da sessao anterior: comparei numeros de
  // corridas diferentes DUAS vezes e chamei a um deles regressao. A comparacao
  // tem de se recusar a acontecer.
  Tempo t1, t2;
  Traco a("cpu=interp", &t1);
  Traco b("cpu=jit bloco=1", &t2);
  a.RegistarFalta(Area::Video, "glScissor", "");
  a.RegistarFalta(Area::Video, "glScissor", "");
  b.RegistarFalta(Area::Video, "glScissor", "");

  auto r = Traco::Comparar(a, b, "glScissor");
  EXPECT_FALSE(r.comparavel);
  EXPECT_NE(r.motivo.find("configuracoes diferentes"), std::string::npos);
  EXPECT_EQ(r.diferenca, 0) << "sem numero quando nao se pode comparar";
}

TEST(Traco, CompararAceitaMesmaConfiguracaoEDaODiferenca) {
  Tempo t1, t2;
  Traco a("cpu=jit bloco=1", &t1);
  Traco b("cpu=jit bloco=1", &t2);
  a.RegistarFalta(Area::Video, "glScissor", "");
  a.RegistarFalta(Area::Video, "glScissor", "");
  b.RegistarFalta(Area::Video, "glScissor", "");
  auto r = Traco::Comparar(a, b, "glScissor");
  ASSERT_TRUE(r.comparavel);
  EXPECT_EQ(r.diferenca, 1);
}

TEST(Traco, MarcasDeDepuracaoSaoListaveisParaLimpeza) {
  // Defeito 7: instrumentacao de investigacao a poluir o artefacto. O prefixo
  // `[DEBUG-` existe para a limpeza ser um `grep`.
  Tempo t;
  Traco tr("teste", &t);
  tr.Depurar("a4f2", "estado do struct");
  tr.Depurar("a4f2", "outra linha");
  tr.Depurar("b7c1", "outra coisa");
  auto marcas = tr.MarcasDeDepuracao();
  ASSERT_EQ(marcas.size(), 2u) << "marcas distintas, nao linhas";
  EXPECT_EQ(marcas[0], "[DEBUG-a4f2]");
  EXPECT_EQ(marcas[1], "[DEBUG-b7c1]");
}

// ===========================================================================
// MEMORIA
// ===========================================================================

TEST(Memoria, LerNaoAlocaPagina) {
  // No Zeebulator antigo algumas rotinas liam "para ver o que ha" e criavam
  // paginas sem querer. Ler de memoria desconhecida devolve zero e nao aloca.
  Memoria m;
  EXPECT_EQ(m.PaginasAlocadas(), 0u);
  EXPECT_EQ(m.Ler32(0x80001000), 0u);
  EXPECT_EQ(m.PaginasAlocadas(), 0u) << "ler nao pode alocar";
  m.Escrever32(0x80001000, 0xDEADBEEF);
  EXPECT_EQ(m.PaginasAlocadas(), 1u) << "escrever aloca";
  EXPECT_EQ(m.Ler32(0x80001000), 0xDEADBEEFu);
}

TEST(Memoria, EscreverSemEscritorDeclaradoEsContado) {
  // Principio P6: um escritor por memoria. Escrever sem se ter declarado nao e
  // proibido a forca -- e contado, para nao virar corrida silenciosa.
  Memoria m;
  m.Escrever8(0x1000, 1);
  EXPECT_EQ(m.EscritasDeOutroAutor(), 1u);
  m.EscritorUnico("cpu");
  m.Escrever8(0x1001, 2);
  EXPECT_EQ(m.Autor(), "cpu");
}

TEST(Memoria, VigiaRegistaOPrimeiroEscritorDeCadaEndereco) {
  // Foi assim que se derrubou, sem uma corrida a mais, a hipotese de que a
  // faixa de pilha do `Boiaz` nao era escrita pelo jogo.
  Memoria m;
  m.EscritorUnico("cpu");
  m.Vigiar({0x80000000, 0x80000010, "pilha"});
  m.PcAtual(0x0010AB7C);
  m.Escrever8(0x80000004, 1);
  m.Escrever8(0x80000005, 2);
  m.Escrever8(0x80000004, 3);  // repetido: nao acrescenta
  m.Escrever8(0x80000020, 4);  // fora da faixa: ignorado
  const auto& v = m.EscritasVigiadas();
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0].first, 0x80000004u);
  EXPECT_EQ(v[0].second, 0x0010AB7Cu);
  EXPECT_EQ(v[1].first, 0x80000005u);
}

TEST(Memoria, PararDeVigiarLimpaOHistorico) {
  Memoria m;
  m.Vigiar({0x80000000, 0x80000010, "x"});
  m.Escrever8(0x80000000, 1);
  EXPECT_EQ(m.EscritasVigiadas().size(), 1u);
  m.PararDeVigiar();
  EXPECT_TRUE(m.EscritasVigiadas().empty());
  EXPECT_TRUE(m.Vigias().empty());
}

TEST(Memoria, LeituraEmBlocoAtravessaPaginas) {
  Memoria m;
  for (std::uint32_t i = 0; i < 64; ++i) m.Escrever8(0x1000 + i, static_cast<std::uint8_t>(i));
  std::uint8_t buf[64] = {};
  m.LerBloco(0x1000, buf, 64);
  for (std::uint32_t i = 0; i < 64; ++i) EXPECT_EQ(buf[i], static_cast<std::uint8_t>(i));
  // Atravessar a fronteira de pagina, com a segunda pagina a existir.
  for (std::uint32_t i = 0; i < 16; ++i) m.Escrever8(0x2000 + i, 0xAA);
  std::uint32_t ini = 0x2000 - 8;
  std::uint8_t buf2[16] = {};
  m.LerBloco(ini, buf2, 16);
  for (int i = 0; i < 8; ++i) EXPECT_EQ(buf2[i], 0u) << "antes da segunda pagina";
  for (int i = 8; i < 16; ++i) EXPECT_EQ(buf2[i], 0xAAu);
}

TEST(Memoria, EscritaEmBlocoAtravessaPaginas) {
  Memoria m;
  std::uint8_t dados[32];
  for (int i = 0; i < 32; ++i) dados[i] = static_cast<std::uint8_t>(0x40 + i);
  const std::uint32_t ini = 0x3000 - 16;
  m.EscreverBloco(ini, dados, 32);
  for (int i = 0; i < 32; ++i) {
    EXPECT_EQ(m.Ler8(ini + i), static_cast<std::uint8_t>(0x40 + i)) << "byte " << i;
  }
}

TEST(Memoria, CadeiaTerminadaEmNulRespeitaOLimite) {
  Memoria m;
  const char* s = "audio/music/cave";
  for (std::size_t i = 0; i <= std::string(s).size(); ++i) {
    m.Escrever8(0x4000 + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(s[i]));
  }
  std::string saida;
  EXPECT_EQ(m.LerCadeia(0x4000, &saida), std::string(s).size());
  EXPECT_EQ(saida, s);
  // Uma cadeia sem NUL dentro do limite nao pode ler para fora dele.
  for (std::uint32_t i = 0; i < 32; ++i) m.Escrever8(0x5000 + i, 'x');
  std::string s2;
  EXPECT_EQ(m.LerCadeia(0x5000, &s2, 16), 16u);
  EXPECT_EQ(s2.size(), 16u);
}
