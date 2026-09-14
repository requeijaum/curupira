// comparar -- o juiz das regressoes: compara DUAS corridas da bateria, titulo a
// titulo, e FALHA quando um titulo piora.
//
// PORQUE ISTO EXISTE, com o numero: o desenho (`docs/rewrite/DESIGN.md`, lista
// do que motivou a reescrita) nomeia "comparei numeros de corridas diferentes
// duas vezes e chamei a um deles regressao" como um dos defeitos medidos. E o
// PLANO, etapa 9, poe o criterio em uma frase: "mudar o emulador de forma a
// piorar um titulo FALHA a bateria, com o numero que piorou".
//
// Este ficheiro e a BIBLIOTECA: nao tem `main`. O `main` esta em
// `tools/comparar_main.cpp` para que `tests/comparar_test.cpp` possa ligar
// contra ele e provar as guardas dentro do `ctest`. A biblioteca e a unica
// implementacao: nao ha uma segunda copia da regra no teste.
//
// DUAS CORRIDAS SO SE COMPARAM SE FOREM A MESMA CONFIGURACAO.
// Uma diferenca de corpus (numero de titulos, ou o conjunto de pasta/mod) nao e
// uma regressao: e outra pergunta. O comparador RECUSA (codigo 3) em vez de
// comparar -- compara-las e literalmente o erro que esta ferramenta impede.
//
// AS DUAS FORMAS DO JSON DA BATERIA:
//   - a forma com cabecalho (a que o `bateria.cpp` escreve desde cfb031e):
//       {"config": {"corpus_sha256": ..., "titulos": N, "build": ...},
//        "titulos": [ ...fichas... ]}
//   - a lista nua de fichas (a forma antiga), aceite para as corridas ja guardadas.
// Uma corrida com cabecalho e outra sem NAO se comparam (codigo 3): de uma delas
// nao se sabe QUAL corpus correu.
//
// O que a configuracao inclui, declarado na tabela `kCabecalho`/`kCampos` do
// `.cpp`: o resumo do corpus (`corpus_sha256`), o tamanho de cada `.mod`
// (`tamanho`) e a lista de titulos. O `build` (o commit) e NEUTRO e so REPORTADO:
// comparar duas versoes do emulador e o uso normal da ferramenta.
//
// O QUE NAO ENTRA COMO CRITERIO, e por que (P1): ver a tabela `kCampos` abaixo.
// Cada linha tem a direcao declarada E a medicao que a sustenta.
//
// Uso: zb2_comparar <referencia.json> <corrida.json>
// Codigos de saida: ver `enum Codigo`.
#ifndef ZB2_TOOLS_COMPARAR_H
#define ZB2_TOOLS_COMPARAR_H

#include <string>

namespace zb2::comparar {

// CINCO codigos distintos, e nao um so "falhou", porque o que interessa separar
// nao e SE falhou: e DE QUEM e o defeito. 1 e um achado sobre o EMULADOR (o
// emulador piorou um titulo). 3, 4 e 2 sao achados sobre o INSTRUMENTO (as duas
// corridas nao sao comparaveis, ou o JSON saiu do formato declarado), e o sitio
// onde se corrige e outro. Um codigo unico obrigaria um humano a ler o texto
// para saber onde ir.
enum Codigo : int {
  kSemRegressao = 0,        // nenhum titulo piorou (pode haver melhorias)
  kRegressao = 1,           // pelo menos um titulo piorou
  kUso = 2,                 // argumentos errados, ficheiro ausente ou ilegivel
  kConfigIncompativel = 3,  // as duas corridas NAO sao a mesma configuracao
  kFormato = 4,             // JSON fora do formato declarado
};

struct Resultado {
  int codigo = kSemRegressao;
  std::string relatorio;  // texto pronto a imprimir; o `main` so o copia
};

// As mudancas dos campos NEUTROS sao as mais numerosas (63 delas na degradacao
// deliberada do `applet`, contra 21 de regressao), porque um titulo que anda
// para outro sitio muda sempre `passos_*`, `motivo` e `faltas`. O detalhe existe
// por omissao -- omitir informacao sem pedir seria o oposto do principio P7 --
// mas pode ser desligado por quem so quer a lista do que falhou.
struct Opcoes {
  bool detalhar_neutros = true;
};

// A = referencia (a corrida antiga, ja aceite). B = corrida nova.
// Regressao = um campo de B pior do que o mesmo campo em A.
Resultado CompararTextos(const std::string& referencia, const std::string& corrida,
                         const std::string& nome_referencia, const std::string& nome_corrida);
Resultado CompararTextos(const std::string& referencia, const std::string& corrida,
                         const std::string& nome_referencia, const std::string& nome_corrida,
                         const Opcoes& opcoes);

}  // namespace zb2::comparar

#endif  // ZB2_TOOLS_COMPARAR_H
