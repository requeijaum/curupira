// O `main` do comparador. Fica separado da biblioteca (`tools/comparar.cpp`)
// para que o teste possa ligar contra a MESMA implementacao: uma segunda copia da
// regra dentro do teste provaria apenas que o teste e consistente consigo mesmo.
//
// Uso: zb2_comparar <referencia.json> <corrida.json>
// Le os dois ficheiros e delega tudo em `CompararTextos`. O unico trabalho daqui
// e o que depende do sistema de ficheiros: ler, e dizer claramente quando nao
// conseguiu ler (codigo 2).

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "tools/comparar.h"

namespace {

bool Ler(const std::string& caminho, std::string* saida) {
  std::ifstream f(caminho, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  *saida = ss.str();
  return f.eof() || f.good();  // um ficheiro lido a meio NAO conta como lido
}

}  // namespace

int main(int argc, char** argv) {
  bool detalhar_neutros = true;
  // As opcoes vem DEPOIS dos dois ficheiros, para que os dois primeiros
  // argumentos sejam sempre as duas corridas.
  for (int i = 3; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--sem-neutros") detalhar_neutros = false;
    else {
      std::fprintf(stderr, "argumento desconhecido: %s\n", argv[i]);
      return zb2::comparar::kUso;
    }
  }
  if (argc < 3) {
    std::fprintf(stderr,
                 "uso: %s <referencia.json> <corrida.json>\n"
                 "  Compara duas corridas da bateria, titulo a titulo.\n"
                 "  Codigos de saida: 0 sem regressao | 1 regressao | 2 uso/leitura |\n"
                 "                    3 configuracoes diferentes | 4 JSON fora do formato\n"
                 "  Opcoes: --sem-neutros  (nao lista o detalhe dos campos neutros)\n",
                 argv[0]);
    return zb2::comparar::kUso;
  }
  std::string texto_referencia;
  std::string texto_corrida;
  if (!Ler(argv[1], &texto_referencia)) {
    std::fprintf(stderr, "nao consegui ler a referencia: %s\n", argv[1]);
    return zb2::comparar::kUso;
  }
  if (!Ler(argv[2], &texto_corrida)) {
    std::fprintf(stderr, "nao consegui ler a corrida: %s\n", argv[2]);
    return zb2::comparar::kUso;
  }
  zb2::comparar::Opcoes opcoes;
  opcoes.detalhar_neutros = detalhar_neutros;
  const zb2::comparar::Resultado r =
      zb2::comparar::CompararTextos(texto_referencia, texto_corrida, argv[1], argv[2], opcoes);
  std::fputs(r.relatorio.c_str(), stdout);
  return r.codigo;
}
