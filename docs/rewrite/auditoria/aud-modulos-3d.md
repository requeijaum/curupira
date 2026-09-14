# AUDITORIA -- os modulos 3D: IGL, IEGL, rasterizador, Tela

Ficheiros auditados (e so estes):
`core/brew/igl.cpp` (1010 l), `core/brew/igl.h` (317), `core/brew/egl.cpp` (789),
`core/brew/egl.h` (220), `core/video/rasterizador.cpp` (572), `core/video/rasterizador.h` (245).

Arvore: `/tmp/wt-auditoria/curupira` (worktree, branch `auditoria`). Nada foi
modificado nesta arvore; o unico ficheiro escrito e este relatorio. Nenhum
`cmake`/build foi corrido (o build e do agente pai).

METODO. `grep` para candidatos; depois leitura do contexto E do cabecalho. O
cabecalho do IGL **nao estava extraido**: usei `tools/achar_aegl.py`, que extrai
o MSI para `/tmp/zb2-aegl-3741dbdcdf93/`, e **contei o `AEEGL.h` real** (77
métodos + 3 de cabeca). O IEGL foi conferido em `AEEGL.h`; as constantes em
`platform/ui/inc/gles/gl.h`; o contrato do `QueryInterface` em
`platform/system/inc/AEEIQI.h` e os codigos de erro em
`platform/system/inc/AEEStdErr.h`. Dai as citacoes do SDK no fim.

A DEMANDA tambem foi MEDIDA, e nao suposta: o binario `build/zb2_sonda_gl`
(ja construido, de 15:11) foi corrido nos QUATRO titulos que trazem o wrapper de
GL compilado dentro do `.mod`, e deu a lista de slots que cada um pede
(comandos e numeros na seccao 5). **O que isso mede e "o thunk existe no wrapper
compilado no .mod"; NAO mede que o titulo o chame em execucao.**

## 0. A PERGUNTA CENTRAL, RESPONDIDA COM NUMEROS

| pergunta | resposta medida |
|---|---|
| quantos slots tem a vtable do IGL? | **80**, e nao 84. `AEEGL.h`: `INHERIT_IQueryInterface (IGL)` = 3 slots de cabeca + **77** métodos. A ordem bate, um a um, com `tools/gl_slots.inc` (modulo o prefixo `gl`), e a sonda confirma: `slots fora da tabela de AEEGL.h: IGL 0 (max 79)`. O "84" do `DESIGN.md` ("71 dos 84 handlers de GL sem log") e um numero da ARVORE ANTIGA, nao da ABI. |
| quantos handlers existem? | **80 `case` para 80 slots** (nenhum slot por tratar) + o `default: return sem()`. |
| quantos RECUSAM com nome? | todos os que recusam passam pelo `Registar`, que emite `GL_<nome do slot>` + a falta com o mesmo nome. **0 recusas mudas.** |
| quantos devolvem sucesso/0 **sem evento nenhum**? | **0 de 80.** Verifiquei os 133 `return` do `Igl::Executar` e os 103 do `Egl::Executar`: **nenhum `return` contorna o `Registar`**. O defeito do `glCullFace` (86 377 chamadas sem rasto) NAO se repete aqui. |
| entao nao ha defeito? | ha, e e o INVERSO: ha eventos cujo TEXTO e falso (3 achados), e ha um sucesso que o GUEST nao consegue distinguir de um trabalho feito (`glGetError`). E o `default -> sem()` e inalcancavel pelo `despacho.cpp` (o ramo limita o indice a `kIglSlots`). |
| quantos slots a demanda medida pede? | **67 dos 80** do IGL e **17 dos 28** do IEGL (uniao dos 4 titulos). |


## 1. TABELA (ordenada por gravidade: SILENCIOSO, REGISTADO, CANDIDATO)

Adicionei uma 5.a coluna (sintoma) porque o metodo exige responder a ela por achado.

| ficheiro:linha | o que devolve | o que devia fazer | gravidade | sintoma num titulo |
|---|---|---|---|---|
| `core/brew/igl.cpp:975` | `glGetError` devolve `GL_NO_ERROR` (0) SEMPRE. E pior do que o texto diz: **nenhuma recusa do IGL escreve um codigo de erro** -- `GL_INVALID_ENUM`/`GL_INVALID_OPERATION`/`GL_OUT_OF_MEMORY` estao em `gl_slots.inc` (196-201) e NAO sao usados em `core/` nenhum. | a recusa tem de ser visivel a quem decide: por um codigo de erro do GL (`GL_INVALID_OPERATION`), ou por uma recusa que o guest note. O irmao `egl.cpp` ja o faz: `Recusar` guarda `erro_` e o `eglGetError` serve-o (egl.cpp:242-251, 398-403). | **SILENCIOSO** | o titulo faz uma chamada que o emulador RECUSOU e o `glGetError` a seguir diz "sem erro": toma o caminho de sucesso. `glGetError` e `glGetIntegerv` estao entre os thunks de chessbots e tectoy (medido). O ddragonz, na bancada, levou 2 recusas (`glCompressedTexImage2D`, `glOrthox`) sem que o guest possa saber. |
| `core/brew/egl.cpp:612` | `eglCreateWindowSurface` devolve `feito` + r0 = handle, com o motivo a afirmar "janela 0x... **guardada** e NAO interpretada ... N par(es) **guardado(s)**". `janela` **nao e escrita em membro nenhum** (`Egl` nao tem onde a guardar: egl.h:197-215) e os atributos vao para `ultimos_atributos_`, um vector UNICO que o `ChooseConfig`/`CreateContext` seguintes apagam. | guardar o que o texto diz que guarda, ou dizer o que NAO guarda. E o caso MODELO do `imedia` ao contrario: la a aceitacao deixa um evento VERDADEIRO. | **SILENCIOSO** | nenhum sintoma medido num titulo (o handle da janela nao e lido de volta por nenhum caminho). O dano e no instrumento: o traco afirma o contrario do codigo. ddragonz, chessbots e nfs trazem `eglCreateWindowSurface` (medido). |
| `core/brew/egl.cpp:778` | `eglSwapBuffers` devolve `feito`/`EGL_TRUE` com o motivo "troca contada; nada a apresentar -- **nenhum rasterizador escreveu pixels**". O rasterizador existe e escreve na Tela: o `igl.cpp:204` emite o contrario (`RASTERIZADOR ... Escreve na Tela`) e, na MESMA corrida da sonda, o `glClear` escreveu 307200 pixels e o `glDrawArrays` 38400 (PIXELS 345600). | o motivo tem de ser verdadeiro (P7). Hoje o que e verdade e "a superficie do IEGL nao esta ligada a Tela": escreva-se isso. | **SILENCIOSO** | o auditor (ou o relatorio da etapa) conclui "nao ha desenho" com o desenho a acontecer. Mesmo texto morto em `egl.cpp:644` e `:650` ("sem rasterizador" no `eglQuerySurface`) e `:728` ("sem rasterizador" no `eglMakeCurrent`). ddragonz, chessbots e nfs trazem `eglSwapBuffers`. |
| `core/brew/igl.cpp:736` | `glTexParameterx` aceita **qualquer** `pname` e guarda-o em `parametros_`; so `GL_TEXTURE_MIN_FILTER`/`GL_TEXTURE_MAG_FILTER` tem consequencia (igl.cpp:292-298). Um `pname` de wrap (`GL_TEXTURE_WRAP_S` = 0x2802, `gles/gl.h:473`) e aceite, guardado e NUNCA aplicado: o rasterizador prende a coordenada na borda (rasterizador.cpp:317-318). | recusar o `pname` desconhecido, ou juntar o wrap a lista de ressalvas como o filtro. Precisa da constante: `GL_TEXTURE_WRAP_S`/`GL_REPEAT` NAO estao em `gl_slots.inc` (a lista de `gerar_slots.py:773-789` e escolhida a mao). | **SILENCIOSO** | um titulo que peca `GL_REPEAT` fica com clamp e o desenho sai diferente, com o mesmo evento "feito" que o `GL_NEAREST`; **a diferenca de pixel nao tem nome em lado nenhum**, apesar de `rasterizador.h:48-49` PROMETER "isso fica registado no detalhe do desenho" (o detalhe, rasterizador.cpp:563-567, so tem vertices/pixels/triangulos). `glTexParameterx` e pedido pelos QUATRO titulos com GL (medido). |
| `core/brew/igl.cpp:997` | `glGetString` RECUSA com o nome (certo por P1) mas deixa o r0 a 0: o guest recebe **NULL**. | o irmao IEGL resolveu o MESMO problema em sentido contrario: `eglQueryString` nunca devolve nulo, escreve a string VAZIA na memoria do guest e poe o motivo no traco (egl.cpp:441-446). O IGL devia fazer o mesmo. | **REGISTADO** | chessbots e tectoy trazem `glGetString` (medido). Um `strstr((char*)glGetString(GL_EXTENSIONS), ...)` -- que e exactamente o caso medido do ddragonz no EGL -- le o endereco 0. A recusa e honesta; o VALOR entregue e que nao e. |
| `core/brew/egl.cpp:623` | `eglCreatePbufferSurface` RECUSA com `EGL_BAD_MATCH` e o motivo "nao ha memoria de pbuffer". | esta CERTO e e coerente: o config diz `EGL_MAX_PBUFFER_* = 0` (egl.cpp:124-126) e `EGL_SURFACE_TYPE = EGL_WINDOW_BIT` (egl.cpp:109). Nao ha nada a corrigir; fica contado. | **REGISTADO** | **tectoy traz o thunk (medido)** e recebe EGL_BAD_MATCH. Se usar o pbuffer como destino, fica sem superficie -- e a recusa e verdadeira, mas e um caminho que a demanda pede e o emulador nao tem. |
| `core/brew/igl.cpp:508` | `QueryInterface` com um IID nao servido devolve r0 = **0xE0000001** escrito a mao, com o comentario `// ECLASSNOTSUPPORT`. | o codigo medido e **3**: `AEEStdErr.h:19` `#define AEE_ECLASSNOTSUPPORT 3` (citado no proprio projeto, `ajudantes.h:54`). 0xE0000001 e exactamente "um valor que nao existe em cabecalho nenhum" -- a frase que `despacho.cpp:43-47` usa para descrever o defeito que JA corrigiu. Falta ainda: o `IQI_SELF` do SDK obriga a servir `AEEIID_IQI` (0x01000001, AEEIQI.h:11) e a dar `AddRef` no sucesso; este ramo recusa o primeiro e nao faz o segundo. | **CANDIDATO** | nenhum titulo do corpus faz `QueryInterface` ao IGL (nao medido). E o IEGL responde a MESMA pergunta com 0 (egl.cpp:394): **dois modulos, tres respostas** para o mesmo contrato. |
| `core/brew/igl.cpp:749` | `glTexImage2D` NAO exige textura ligada: escreve em `texturas_[textura_ligada_]`, ou seja em `texturas_[0]` quando nada esta ligado. E `MontarEstado` (igl.cpp:249) so amostra textura com id != 0 -- logo a entrada 0 e ESCRITA e NUNCA LIDA. | recusar "sem textura ligada", como o `glTexSubImage2D` ja faz (igl.cpp:766). | **CANDIDATO** | um titulo que use o objecto de textura 0 (legal em GL ES 1.x) fica com o desenho na COR, sem textura, e nenhum evento o diz. Nao medido: nenhum dos 4 titulos foi observado a enviar textura com o nome 0. |
| `core/brew/igl.h:160` | `EstadoDaTextura::formato` (o formato INTERNO, 3.o argumento do `glTexImage2D`) e escrito em dois sitios (igl.cpp:752 e :791) e **nao e lido em lado nenhum**: `grep '\.formato|->formato'` em `core/`, `tests/`, `tools/` devolve so as duas escritas (o campo homonimo do rasterizador e outro, e esse e lido). | ou o rasterizador o usa, ou o campo sai do cabecalho. Um campo escrito para ninguem e a definicao de fachada que a arvore antiga tinha. | **CANDIDATO** | nenhum: o valor aparece no traco (o `args[2]` da linha `GL_glTexImage2D`), e por isso o pedido nao fica invisivel -- so a estrutura mente. |
| `core/brew/igl.cpp:984` | `glGetIntegerv(GL_MAX_TEXTURE_SIZE)` RECUSA com o motivo "a memoria de texturas nao existe nesta etapa" e **nao escreve nada** no destino. | o motivo e falso desde a etapa 6: o modulo guarda texturas (igl.h:154-168) e o proprio `glTexImage2D` recusa texturas maiores de 4096 (igl.cpp:748). Ou devolve 4096, ou diz que o valor da MAQUINA nao foi medido. | **CANDIDATO** | chessbots e tectoy trazem `glGetIntegerv` (medido). O destino do guest fica com o valor ANTERIOR; um titulo que dimensione a textura por ele pode calcular 0 e nao criar textura nenhuma. Nao medido qual dos dois o chama com ESTE pname. |
| `core/brew/igl.cpp:777` | `glTexSubImage2D` le so (formato, tipo, pixels) da pilha e **descarta `level`, `xoffset` e `yoffset`**: o ponteiro passa a ser amostrado como se fosse a textura INTEIRA (dito no comentario, igl.cpp:768-772), e o motivo da chamada NAO o diz. | ou aplicar os offsets, ou dizer no motivo que a sub-imagem e usada como textura inteira -- `rasterizador.h` ponto 6 promete que o que ficou de fora "fica dito", e isto nao ficou. | **CANDIDATO** | um titulo que actualize uma textura por sub-imagem (padrao comum em video/HUD) ve a REGIAO errada esticada no poligono. chessbots traz o thunk (medido); a chamada em execucao nao foi medida. |
| `core/video/rasterizador.cpp:515` | `vertices.reserve(pedido.quantos)` com `quantos` vindo do GUEST (r0+2 do `glDrawArrays`) e **sem limite nenhum**: 0x40000000 * 40 bytes = 64 GB de reserva. | limitar o `count` antes de reservar. O `tela.h:28-33` ja aprendeu esta licao do lado do pixel ("um limite verificado so no destino nao limita o trabalho"); aqui nao ha limite NEM no destino. | **CANDIDATO** | um titulo (ou um valor corrompido) com `count` absurdo mata o emulador com `bad_alloc` em vez de recusar. A recusa de `count == 0` existe (igl.cpp:915); a de `count` grande, nao. |
| `core/brew/egl.cpp:296` | `kPrimeiraSuperficie + superficies_criadas_ * kPassoDeObjeto`: o contador **so cresce** (nunca ha reutilizacao, e o `Terminate` nao o reinicia) enquanto o limite e sobre as VIVAS (16). A 16.a criacao o handle cai em `kPrimeiroContexto` (0x800B1300) e a 32.a em `kZonaDeStrings` (0x800B1400) -- os dois numeros da MESMA struct. | reutilizar handle livre, ou recusar quando o handle sai do bloco reservado. | **CANDIDATO** | dois objectos com o MESMO handle: um titulo que crie/apague superficie entre quadros passa a ter `draw == ctx` no `eglMakeCurrent` e a comparar handles iguais. Nao medido. E a mesma classe do erro que o `igl.h:72-96` documenta e corrigiu. |
| `core/brew/egl.cpp:471` | `EGL_VENDOR` escreve a string **1**, que e a MESMA de `EGL_EXTENSIONS` (egl.cpp:458): as duas consultas devolvem o MESMO endereco na memoria do guest. | uma string por consulta (`kQuantasStrings` = 3 e o indice 1 esta a ser usado duas vezes). | **CANDIDATO** | hoje as duas sao "" e nao ha sintoma; no dia em que o vendor for preenchido, o ponteiro que o titulo guardou das extensoes passa a ler o vendor. Mesma classe da colisao de enderecos. |
| `core/brew/igl.cpp:845` | `glLineWidthx` e `glPointSizex` guardam o valor e devolvem `feito(1)` **sem motivo**, ao contrario dos 20 slots vizinhos que dizem "parametro acumulado; sem rasterizador que o use" (igl.cpp:875, 882, 893). | usar a mesma forma dos vizinhos: se o valor nao e usado, o traco tem de o dizer. | **CANDIDATO** | a largura de linha de um titulo 3D e ignorada em silencio. **Nao aparece na demanda medida**: `glLineWidthx` e `glPointSizex` nao estao em nenhum dos 4 titulos. |
| `core/brew/egl.cpp:531` | `config_do_pedido_ = id` guarda o **id do atributo** que esta a ser examinado no laco; o acessor chama-se `UltimoConfigPedido()` (egl.h:176). | guardar o id do CONFIG, ou mudar o nome do campo e do acessor. | **CANDIDATO** | nenhum num titulo: e estado de sonda/teste. Mas um teste ou um relatorio que leia `UltimoConfigPedido()` le outra coisa. |
| `core/brew/egl.cpp:161` | a falta da cablagem do IEGL chama-se `"cablagem_do_iegL_perdida"` (L maiusculo), enquanto a do IGL e `"cablagem_do_igl_perdida"` (igl.cpp:187). | um nome so. | **CANDIDATO** | nenhum num titulo. E um `grep` que encontra metade das linhas de falta do GL -- e nesta arvore o `grep` do nome e o instrumento de trabalho. |

## 2. CITACAO LITERAL DE CADA LINHA ACUSADA

Cada bloco traz a linha com o numero, copiada do ficheiro sem alteracao.
Os rotulos (A1-A4 = SILENCIOSO, B1-B2 = REGISTADO, C1-C11 = CANDIDATO) sao os
usados nas seccoes 3 e 4.

### A1 -- glGetError sempre zero (SILENCIOSO)

`core/brew/igl.cpp:970-976`

```
969|    // --- consultas ----------------------------------------------------------
970|    case kIgl_GetError:
971|      // Devolve sempre GL_NO_ERROR, e di-lo: nao ha estado de erro implementado,
972|      // e INVENTAR um codigo seria pior do que o zero. As recusas nao ficam
973|      // invisiveis -- cada uma vai para o traco com o nome do metodo e para a
974|      // lista de faltas.
975|      if (retorno != nullptr) *retorno = GL_NO_ERROR;
976|      return feito_com(0, "sem estado de erro: as recusas estao no traco, com nome");
```

### A2 -- CreateWindowSurface: "guardada" o que nao e guardado (SILENCIOSO)

`core/brew/egl.cpp:608-616`

```
607|      if (s == 0) return recusa(4, "limite de superficies deste modulo atingido", EGL_BAD_ALLOC);
608|      // A JANELA E UM OBJECTO DO GUEST (no `ddragonz` e um IDIB criado com o
609|      // AEECLSID 0x01001045), e nao ha janela nativa nenhuma neste emulador:
610|      // guarda-se o ponteiro e diz-se que nao e interpretado. Zero e aceite -- o
611|      // EGL permite-o, e o jogo que ainda nao tem IDIB chega aqui.
612|      return feito_com(4, s,
613|                       "superficie de janela criada; janela " + Hex(janela) +
614|                           " guardada e NAO interpretada (nao ha janela nativa); atributos: " +
615|                           std::to_string(ultimos_atributos_.size() / 2) +
616|                           " par(es) guardado(s) sem interpretacao");
```

### A3 -- SwapBuffers: "nenhum rasterizador escreveu pixels" (SILENCIOSO)

`core/brew/egl.cpp:774-779`

```
774|      ++trocas_;
775|      // A TROCA E FEITA (o contador sobe, o jogo continua o seu laco) E A VERDADE
776|      // FICA DITA: nada foi desenhado, porque nao ha rasterizador. Recusar aqui
777|      // pararia o laco de quadro do jogo por um motivo que nao e dele.
778|      return feito_com(2, EGL_TRUE,
779|                       "troca contada; nada a apresentar -- nenhum rasterizador escreveu pixels");

---- e o mesmo texto morto, mais tres vezes:
643|        return feito_com(4, EGL_TRUE,
644|                         "EGL_WIDTH = 640, da Tela (tela.h: Tela::kLargura) -- sem rasterizador, "
645|                         "esta e a largura do unico framebuffer que existe");
649|        return feito_com(4, EGL_TRUE,
650|                         "EGL_HEIGHT = 480, da Tela (tela.h: Tela::kAltura) -- sem rasterizador");
726|      return feito_com(4, EGL_TRUE,
727|                       "contexto corrente posto; o IGL nao tem estado por contexto (um so objecto "
728|                       "GL, sem rasterizador)");
```

### A4 -- glTexParameterx aceita qualquer pname, e o wrap nao deixa rasto (SILENCIOSO)

`core/brew/igl.cpp:734-738, e a promessa em core/video/rasterizador.h:46-49`

```
734|    case kIgl_TexParameterx: {
735|      if (a.reg[0] != GL_TEXTURE_2D) return recusa("alvo diferente de GL_TEXTURE_2D");
736|      parametros_[ChaveDeParametro(slot, a.reg[1])] = {a.reg[2]};
737|      return feito(3);
738|    }

---- a promessa:
46|//   5. SEM MIPMAPS e SEM FILTRAGEM BILINEAR: um texel por pixel, o do canto
47|//      inferior esquerdo da celula (`floor(u * largura)`), com CLAMP nas
48|//      bordas. O wrap `GL_REPEAT` nao existe: uma coordenada fora de [0,1] e
49|//      presa na borda, e isso fica registado no detalhe do desenho.

---- e a lista que so tem o filtro:
290|  // O FILTRO DA TEXTURA. O rasterizador amostra sempre o texel mais proximo; um
291|  // titulo que peca GL_LINEAR fica com essa diferenca escrita, e nao silenciosa.
292|  for (const std::uint32_t pname : {GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER}) {
293|    const std::vector<std::uint32_t>* v = Parametro(kIgl_TexParameterx, pname);
294|    if (v != nullptr && !v->empty() && (*v)[0] != GL_NEAREST) {
295|      e.capacidades_por_fazer.push_back("filtro_de_textura_alem_de_GL_NEAREST");
296|      break;
297|    }
298|  }
```

### B1 -- glGetString devolve NULL (REGISTADO)

`core/brew/igl.cpp:992-997`

```
992|    case kIgl_GetString:
993|      // A STRING E UMA AFIRMACAO SOBRE O HARDWARE. Devolver "ATI" ou uma lista de
994|      // extensoes que nao foram medidas seria inventar -- e ha um caso medido na
995|      // arvore antiga (o `ddragonz` mete o resultado do `eglQueryString` num
996|      // `strstr` sem testar o nulo) que torna isto uma armadilha. Recusa-se.
997|      return recusa("sem medida do que a maquina responde (glGetString)");

---- o irmao IEGL decidiu o CONTRARIO:
441|    case kIegl_QueryString: {
442|      // NUNCA DEVOLVE NULO, e isto e uma decisao com medicao por tras: na arvore
443|      // antiga mediu-se um `strstr` sobre o resultado do `eglQueryString` sem
444|      // teste de nulo. Aqui a resposta e sempre um endereco valido na memoria do
445|      // guest; o que nao tem resposta verdadeira devolve a string VAZIA e fica
446|      // registado como falta, com o nome da consulta.
447|      if (!display_ok(a.reg[0], 2)) return ResultadoGl::Recusado;
```

### B2 -- eglCreatePbufferSurface recusa (REGISTADO, e coerente com o config)

`core/brew/egl.cpp:623-625`

```
623|    case kIegl_CreatePbufferSurface:
624|      return recusa(3, "nao ha memoria de pbuffer (o config tem EGL_MAX_PBUFFER_* a zero)",
625|                    EGL_BAD_MATCH);
123|    // O MAXIMO DE PBUFFER E ZERO, e e a verdade: nao ha memoria de pbuffer.
124|    {EGL_MAX_PBUFFER_WIDTH, 0, "nao ha memoria de pbuffer"},
125|    {EGL_MAX_PBUFFER_HEIGHT, 0, "nao ha memoria de pbuffer"},
126|    {EGL_MAX_PBUFFER_PIXELS, 0, "nao ha memoria de pbuffer"},
```

### C1 -- QueryInterface: 0xE0000001 escrito a mao (CANDIDATO)

`core/brew/igl.cpp:497-510`

```
497|    case kIgl_QueryInterface: {
498|      // `ISHELL`/`IGL` so se devolvem a si proprios: nao ha outra interface
499|      // dentro deste objecto. Recusar com o ponteiro a zero e a resposta certa.
500|      const std::uint32_t iid = a.reg[1], ppo = a.reg[2];
501|      if (ppo == 0) return recusa("ppObj nulo");
502|      if (iid == kClsidIgl) {
503|        mem_.Escrever32(ppo, objeto_);
504|        if (retorno != nullptr) *retorno = 0;  // SUCCESS
505|        return feito(3);
506|      }
507|      mem_.Escrever32(ppo, 0);
508|      if (retorno != nullptr) *retorno = 0xE0000001u;  // ECLASSNOTSUPPORT
509|      return recusa("IID nao servido por este objecto");
510|    }

---- o SDK, medido (extraido do MSI e do toolkit):
platform/system/inc/AEEStdErr.h:19|	#define  AEE_ECLASSNOTSUPPORT     3  // specified class unsupported
platform/system/inc/AEEIQI.h:11 |#define AEEIID_IQI 0x01000001
platform/system/inc/AEEQueryInterface.h (IQI_SELF)|
   int ISomething_QueryInterface(ISomething *me, AEEIID idReq, void **ppo)
   {  if (NULL != IQI_SELF(me, idReq, ppo, AEEIID_ISomething)) {
         IQI_AddRef(*ppo);
         return AEE_SUCCESS;
      } else {
         return AEE_ECLASSNOTSUPPORT;
      } }

---- e o despacho JA corrigiu este valor uma vez:
43|// OS CODIGOS DE ERRO JA NAO ESTAO AQUI. Estavam, com `kAeeUnsupported` a valer
44|// `0xE0000001` -- um valor que nao existe em cabecalho nenhum -- e a copia local
45|// ESCONDIA o enum de `ajudantes.h` (onde o `AEE_EUNSUPPORTED` e 20,
46|// AEEStdErr.h:36). Tirei a copia: agora o nome resolve para o enum, e ha um
47|// numero medido num sitio so.
52|  kAeeSuccess = 0,             // AEEStdErr.h:16
53|  kAeeFailed = 1,              // AEEStdErr.h:17
54|  kAeeClassNotSupported = 3,   // AEEStdErr.h:19

---- o IEGL responde a mesma pergunta assim:
386|    case kIegl_QueryInterface: {
387|      const std::uint32_t iid = a.reg[1], ppo = a.reg[2];
388|      if (ppo == 0) return recusa(3, "ppObj nulo", EGL_BAD_PARAMETER);
389|      if (iid == kClsidIegl) {
390|        mem_.Escrever32(ppo, objeto_);
391|        return feito(3, 0);  // SUCCESS
392|      }
393|      mem_.Escrever32(ppo, 0);
394|      return recusa(3, "IID nao servido por este objecto", EGL_BAD_PARAMETER);
395|    }
```

### C2 -- glTexImage2D sem textura ligada escreve em texturas_[0], que nunca e lida (CANDIDATO)

`core/brew/igl.cpp:744-750, e o consumidor em igl.cpp:249-251`

```
744|      const std::uint32_t alvo = a.reg[0], formato = a.reg[2];
745|      const std::uint32_t larg = Arg(3, a), alt = Arg(4, a);
746|      if (alvo != GL_TEXTURE_2D) return recusa("alvo diferente de GL_TEXTURE_2D");
747|      if (larg == 0 || alt == 0) return recusa("textura com largura ou altura zero");
748|      if (larg > 4096 || alt > 4096) return recusa("textura maior do que o maximo suportado");
749|      EstadoDaTextura& t = texturas_[textura_ligada_];
750|      t.largura = larg;

---- o TexSubImage2D, no mesmo ficheiro, RECUSA o mesmo caso:
764|    case kIgl_TexSubImage2D: {
765|      if (!esp(9)) return recusa("argumentos na pilha sem sp valido");
766|      if (textura_ligada_ == 0) return recusa("sem textura ligada");
767|      // Os argumentos sao (alvo, nivel, x, y, larg, alt, formato, tipo, pixels).

---- e o unico consumidor exige id != 0:
247|  // A TEXTURA LIGADA SO CONTA COM O `GL_TEXTURE_2D` LIGADO: e o que o GL faz. Uma
248|  // textura com o alvo desligado nao e amostrada, e o desenho usa a cor.
249|  if (InterruptorLigado(GL_TEXTURE_2D) && textura_ligada_ != 0) {
250|    const EstadoDaTextura* t = Textura(textura_ligada_);
251|    if (t != nullptr) {
```

### C3 -- campo escrito e nunca lido: o formato INTERNO (CANDIDATO)

`core/brew/igl.h:154-160, escritas em igl.cpp:752 e :791`

```
150|// O que se guarda de uma textura. NAO ha pixels: o `glTexImage2D` aponta para a
151|// memoria do guest, e leva-los para o hospedeiro e trabalho do rasterizador, que
152|// NAO faz parte desta etapa. Fica o que permite MEDIR que a textura foi pedida,
153|// com que tamanho, com que formato e quantas vezes.
154|struct EstadoDaTextura {
155|  std::uint32_t largura = 0, altura = 0;
156|  // `formato` e o formato INTERNO (o 3.o argumento do `glTexImage2D`); o
157|  // `formato_do_pixel` e o 6.o (GL_RGBA, GL_RGB, GL_LUMINANCE) e o `tipo` e o
158|  // 8.o. O rasterizador precisa dos dois ultimos para ler os texels: sem eles
159|  // teria de adivinhar quantos bytes tem cada texel.
160|  std::uint32_t formato = 0, tipo = 0;
161|  std::uint32_t formato_do_pixel = 0;
162|  // O NONO ARGUMENTO: os texels, NA MEMORIA DO GUEST. Nao ha copia no acto do
163|  // `glTexImage2D` (o rasterizador le de la a cada amostragem) -- e isso esta
164|  // escrito no topo de `core/video/rasterizador.h`, com a consequencia.
165|  std::uint32_t ponteiro = 0;
166|  std::uint32_t uploade = 0;
167|  bool comprimida = false;
168|};
751|      t.altura = alt;
752|      t.formato = formato;
753|      // OS DOIS CAMPOS QUE O RASTERIZADOR PRECISA PARA LER OS TEXELS: o formato
790|      t.altura = altura;
791|      t.formato = a.reg[2];
792|      t.comprimida = true;
```

### C4 -- glGetIntegerv(GL_MAX_TEXTURE_SIZE): motivo falso e destino por escrever (CANDIDATO)

`core/brew/igl.cpp:977-991`

```
977|    case kIgl_GetIntegerv: {
978|      const std::uint32_t pname = a.reg[0], destino = a.reg[1];
979|      if (destino == 0) return recusa("destino de glGetIntegerv nulo");
980|      std::uint32_t valor = 0;
981|      if (pname == GL_MAX_MODELVIEW_STACK_DEPTH) valor = kFundoModelView;
982|      else if (pname == GL_MAX_PROJECTION_STACK_DEPTH) valor = kFundoProjection;
983|      else if (pname == GL_MAX_TEXTURE_STACK_DEPTH) valor = kFundoTexture;
984|      else if (pname == GL_MAX_TEXTURE_SIZE) {
985|        return recusa("a memoria de texturas nao existe nesta etapa");
986|      } else {
987|        return recusa("pname nao servido (sem medida do que a maquina responde)");
988|      }
989|      mem_.Escrever32(destino, valor);
990|      return feito(2);
991|    }
```

### C5 -- glTexSubImage2D descarta level/xoffset/yoffset (CANDIDATO)

`core/brew/igl.cpp:764-778`

```
767|      // Os argumentos sao (alvo, nivel, x, y, larg, alt, formato, tipo, pixels).
768|      // Guarda-se o ponteiro tal como veio: a amostragem le os texels dessa
769|      // memoria, como se a textura tivesse sido enviada inteira de uma vez. A
770|      // consequencia (um buffer reutilizado para outra textura) esta escrita no
771|      // topo de `core/video/rasterizador.h`.
772|      EstadoDaTextura& t = texturas_[textura_ligada_];
773|      t.formato_do_pixel = c.args[6];
774|      t.tipo = c.args[7];
775|      t.ponteiro = c.args[8];
776|      ++t.uploade;
777|      return feito_com(9, "os pixels ficam na memoria do guest e sao lidos na amostragem");
778|    }
```

### C6 -- reserve() com o count do guest, sem limite (CANDIDATO)

`core/video/rasterizador.cpp:510-530`

```
510|  const std::uint64_t pixels_antes = pixels_;
511|  const std::uint64_t triangulos_antes = triangulos_;
512|  const std::uint64_t descartados_antes = descartados_;
513|  std::string recusa_do_vertice;
514|  std::vector<Vertice> vertices;
515|  vertices.reserve(pedido.quantos);
516|  for (std::uint32_t k = 0; k < pedido.quantos; ++k) {
517|    std::uint32_t indice = pedido.primeiro + k;

---- o irmao do outro lado (a licao ja aprendida):
28|  // LIMITE ANTES DE PERCORRER, e nao so dentro do `Ponto`.
29|  //
30|  // O `Ponto` recusa o que sai do ecra, mas o laco corria na mesma `w*h` vezes.
31|  // Com uma rect grande vinda do guest isso sao milhares de milhoes de iteracoes:
32|  // medido, mais de 900 s para UM titulo. **Um limite verificado so no destino
33|  // nao limita o trabalho.**
34|  void Retangulo(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h, bool cheio);
```

### C7 -- os handles de superficie saem do bloco reservado (CANDIDATO)

`core/brew/egl.cpp:294-300, com as constantes em egl.cpp:87-100`

```
294|std::uint32_t Egl::CriarSuperficie() {
295|  if (superficies_vivas_.size() >= kMaxSuperficies) return 0;
296|  const std::uint32_t s = kPrimeiraSuperficie + superficies_criadas_ * kPassoDeObjeto;
297|  ++superficies_criadas_;
298|  superficies_vivas_.push_back(s);
299|  return s;
300|}
87|// proprio modulo ve nao se pode auditar (P1).
88|const AtributoDaConfig kConfigDoZeebulator[] = {
89|    // O TAMANHO E O FORMATO DO PIXEL -- da TELA, e nao de um folheto.
90|    // `tela.h`: `void CorAtual(std::uint32_t rgb565)`; 5+6+5 = 16.
91|    {EGL_BUFFER_SIZE, 16, "tela.h: o pixel da Tela e RGB565 (5+6+5)"},
92|    {EGL_RED_SIZE, 5, "tela.h: CorAtual(rgb565) -- 5 bits de vermelho"},
93|    {EGL_GREEN_SIZE, 6, "tela.h: CorAtual(rgb565) -- 6 bits de verde"},
94|    {EGL_BLUE_SIZE, 5, "tela.h: CorAtual(rgb565) -- 5 bits de azul"},
95|    // E o pedido do corpus confere: `ddragonz.mod` 0x14fc10 pede R=5, G=6, B=5.
96|    {EGL_ALPHA_SIZE, 0, "nao ha canal alfa em nenhum buffer deste emulador"},
97|    {EGL_DEPTH_SIZE, 0, "nao ha buffer de profundidade (tela.h so tem pixels)"},
98|    {EGL_STENCIL_SIZE, 0, "nao ha buffer de stencil"},
99|    {EGL_LUMINANCE_SIZE, 0, "nao ha config de luminancia"},
```

### C8 -- EGL_VENDOR e EGL_EXTENSIONS partilham a string 1 (CANDIDATO)

`core/brew/egl.cpp:455-474`

```
455|      if (qual == EGL_EXTENSIONS) {
456|        // A STRING VAZIA E A VERDADE, e nao uma recusa: este modulo nao implementa
457|        // extensao nenhuma (`glGetProcAddress`/`eglGetProcAddress` devolvem zero).
458|        EscreverString(1, "");
459|        return feito_com(2, kZonaDeStrings + 1 * kPassoDeString,
460|                         "EGL_EXTENSIONS = \"\": nenhuma extensao implementada nesta arvore");
461|      }
462|      if (qual == EGL_CLIENT_APIS) {
463|        EscreverString(2, "OpenGL_ES");
464|        return feito_com(2, kZonaDeStrings + 2 * kPassoDeString,
465|                         "EGL_CLIENT_APIS = \"OpenGL_ES\": a unica API servida (IGL)");
466|      }
467|      // O VENDOR E UMA AFIRMACAO SOBRE O FABRICANTE, e nao foi medido. A string
468|      // vazia mantem o guest vivo (um `strstr` sobre ela devolve nulo, e nao
469|      // rebenta) e a falta fica com o nome.
470|      if (qual == EGL_VENDOR) {
471|        EscreverString(1, "");
472|        return feito_com(2, kZonaDeStrings + 1 * kPassoDeString,
473|                         "EGL_VENDOR = \"\": sem medida do que a maquina responde");
474|      }
475|      EscreverString(1, "");
```

### C9 -- glLineWidthx / glPointSizex: feito sem motivo (CANDIDATO)

`core/brew/igl.cpp:843-852`

```
841|      parametros_[ChaveDeParametro(slot, 0)] = {a.reg[0], a.reg[1], a.reg[2], a.reg[3]};
842|      return feito_com(4, "o rectangulo ficou guardado; o rasterizador nao aplica o scissor");
843|    case kIgl_LineWidthx: {
844|      if (Fixo(0, a) <= 0.0f) return recusa("largura de linha nao positiva");
845|      parametros_[ChaveDeParametro(slot, 0)] = {a.reg[0]};
846|      return feito(1);
847|    }
848|    case kIgl_PointSizex: {
849|      if (Fixo(0, a) <= 0.0f) return recusa("tamanho de ponto nao positivo");
850|      parametros_[ChaveDeParametro(slot, 0)] = {a.reg[0]};
851|      return feito(1);
852|    }

---- compare-se com os vizinhos:
861|    case kIgl_BlendFunc:
862|    case kIgl_AlphaFuncx:
863|    case kIgl_DepthRangex:
864|    case kIgl_Hint:
865|    case kIgl_PolygonOffsetx:
866|    case kIgl_SampleCoveragex:
867|    case kIgl_LogicOp:
868|    case kIgl_StencilFunc:
869|    case kIgl_StencilMask:
870|    case kIgl_StencilOp:
871|      // So acumulam parametros: guardam-se TODOS, para nao haver um caminho que
872|      // "devolve sucesso e nao faz nada". O consumidor (o rasterizador) nao
873|      // existe, e o detalhe da chamada di-lo.
874|      parametros_[ChaveDeParametro(slot, 0)] = {a.reg[0], a.reg[1], a.reg[2], a.reg[3]};
875|      return feito_com(4, "parametro acumulado; sem rasterizador que o use");
```

### C10 -- config_do_pedido_ guarda um id de ATRIBUTO (CANDIDATO)

`core/brew/egl.cpp:528-532, com o campo em egl.h:173-176`

```
528|      for (std::size_t k = 0; k + 1 < ultimos_atributos_.size(); k += 2) {
529|        const std::uint32_t id = ultimos_atributos_[k];
530|        const std::uint32_t valor = ultimos_atributos_[k + 1];
531|        config_do_pedido_ = id;
532|        if (id == EGL_MATCH_NATIVE_PIXMAP) {
533|          serve = false;
534|          primeiro_que_falhou = "EGL_MATCH_NATIVE_PIXMAP: nao ha pixmap nativa para comparar";
535|          break;
173|  // O ultimo `attrib_list` lido do guest, em pares (id, valor), tal como chegou.
174|  // Nao e uma interpretacao: e o que estava na memoria do guest.
175|  const std::vector<std::uint32_t>& UltimosAtributos() const { return ultimos_atributos_; }
176|  std::uint32_t UltimoConfigPedido() const { return config_do_pedido_; }
```

### C11 -- o nome da falta do IEGL tem um L maiusculo (CANDIDATO)

`core/brew/egl.cpp:157-162`

```
157|    if (lido != esperado) {
158|      char det[160];
159|      std::snprintf(det, sizeof(det),
160|                    "slot %u da vtable do IEGL tem 0x%08x, devia ter 0x%08x", i, lido, esperado);
161|      traco_.RegistarFalta(Area::Video, "cablagem_do_iegL_perdida", det);
162|      return 0;
163|    }
183|    if (lido != esperado) {
184|      char det[160];
185|      std::snprintf(det, sizeof(det),
186|                    "slot %u da vtable do IGL tem 0x%08x, devia ter 0x%08x", i, lido, esperado);
187|      traco_.RegistarFalta(Area::Video, "cablagem_do_igl_perdida", det);
188|      return 0;
189|    }
```


## 3. NAO E DEFEITO MAS PARECE

Contados tambem, como pedido. Cada um foi lido com o cabecalho ao lado.

1. `igl.cpp:1005` `default: return sem()` e `egl.cpp:784` `default: return sem()`.
   Parecem o buraco por onde passa o stub mudo. **Nao sao**: o `despacho.cpp:503`
   e `:520` so entregam indices dentro de `kIglSlots`/`kIeglSlots`, logo o
   `default` e INALCANCAVEL pelo guest -- e e um `sem()` com nome e falta, nao um
   sucesso.
2. `igl.cpp:620-624` `glClearStencil`: guarda o valor e diz no motivo
   "sem buffer de stencil". O contrato e nao haver buffer de stencil: aceitar e
   registar e a resposta certa (o caso MODELO do `imedia`).
3. `igl.cpp:837-842` `glScissor`: guardado e nao aplicado, e o motivo di-lo
   ("o rasterizador nao aplica o scissor"); o `GL_SCISSOR_TEST` entra ainda em
   `capacidades_por_fazer` (igl.cpp:281).
4. `igl.cpp:602-609` `glColorMask`: guardado, nao aplicado, e a ressalva e
   emitida por `MontarEstado` (igl.cpp:287-289).
5. `igl.cpp:799` `CompressedTexSubImage2D`/`CopyTexImage2D`/`CopyTexSubImage2D` ->
   `sem()` (`NaoImplementado`) **com o nome do slot e a falta**. Recusar e o
   estado por omissao correto (P2).
6. `igl.cpp:998-1003` `glReadPixels` recusa porque a `tela.h` nao expoe leitura
   de pixel. Conferi o cabecalho: `tela.h` tem `Escritos()`, `CoresDistintas()`
   e `CoresEm(x,y,w,h)` e **nenhum leitor de um pixel**. A recusa e verdadeira.
7. `rasterizador.cpp:34` `BytesDoTipo` -> `default: return 0` e `:73-74`
   `LerFloat` -> `default: return 0.0f`. Um zero que se pode ler como valor: mas
   TODOS os chamadores recusam antes (rasterizador.cpp:216-221, 243-247,
   277-282), com o NOME do tipo. Guardas defensivas, nao stubs.
8. `rasterizador.cpp:312` e `:334` (`return e.cor` quando nao ha textura ou o
   formato nao tem caminho) e `:356` `default: passa = true` no switch da funcao
   de profundidade: inalcancaveis, porque `Desenhar` recusa formatos
   desconhecidos (rasterizador.cpp:492-505) e o `igl.cpp:824` valida
   `GL_NEVER..GL_ALWAYS`. Se um dia o IGL deixar de validar, o `default` PASSA o
   teste em silencio -- por isso ficam aqui, e nao no silencio.
9. `igl.cpp:354-367` `ArrayDeClienteLigado`: `glEnable(GL_VERTEX_ARRAY)` e
   `glEnableClientState(GL_VERTEX_ARRAY)` contam os dois. E o contrato do GL ES
   1.x (dois caminhos para a mesma capacidade), com o comentario a dize-lo.
10. `igl.cpp:170-190` e `egl.cpp:146-164` `Instalar` devolve 0 quando a cablagem
    se perde -- **com `RegistarFalta` e o nome**, e o `despacho.cpp:308-310`
    registra `cablagem_do_GL`. Nao e um erro engolido.
11. `egl.cpp:294-295` e `:302-303` `CriarSuperficie`/`CriarContexto` devolvem 0
    no limite: o chamador converte em `EGL_BAD_ALLOC` com motivo. Certo (o
    problema do C7 e o ENDERECO do handle, nao esta recusa).
12. `egl.cpp:486-495` `eglGetProcAddress` devolve 0. E o contrato do EGL (nome
    desconhecido -> nulo) **e o pedido fica registado com o nome que o jogo
    pediu** -- a forma certa.
13. `egl.cpp:398-403` `eglGetError` consome o erro e poe `EGL_SUCCESS`: e o que o
    EGL define. Aqui o irmao IEGL esta mais correto do que o IGL (ver A1).
14. `rasterizador.cpp:184-203` `Limpar` nao escreve nada quando a mascara nao
    tem `GL_COLOR_BUFFER_BIT` e devolve 0: o `igl.cpp:643` ve o 0 e escreve-o no
    detalhe.
15. `igl.h:27-33` o `po` nao e passado aos métodos `gl*`: MEDIDO no `conftest.elf`
    (seis instrucoes do `glCullFace`), e a sonda confirma-o nos 4 titulos.
16. `igl.cpp:274-289` a lista `por_fazer` (blend, lighting, fog, alpha test,
    polygon offset, scissor, dither, color mask, filtro != NEAREST) e o
    ANTI-stub: cada capacidade que um titulo liga entra UMA vez no traco, com
    nome. Medido na bancada do ddragonz: `filtro_de_textura_alem_de_GL_NEAREST 1x`.


## 4. CONTRADIZ O QUE SE PENSAVA

1. **"84 handlers de GL" nao e o numero da ABI.** O `DESIGN.md` e o `igl.h`
   falam em 84 (`71 dos 84 handlers de GL sem log`), numero da ARVORE ANTIGA.
   O `AEEGL.h` real -- extraido do `Installer.msi` por `tools/achar_aegl.py` e
   contado -- tem **77 métodos + 3 slots de cabeca = 80**, e a ordem coincide
   uma a uma com `tools/gl_slots.inc`. A sonda confirma: `max 79`, `IGL 0` fora
   da tabela. Quem escrever "84 slots" a seguir escreve um numero que nao existe.
2. **"stub que devolve sucesso e nao faz nada" NAO existe neste subsistema** --
   `0 de 80` e `0 de 28` caminhos sem evento. O defeito que ficou e o CONTRARIO:
   o evento existe e MENTE (A2, A3) ou o guest nao o pode ver (A1). A caca ao
   "sucesso e nada" neste modulo encontra sobretudo um problema de TEXTO.
3. **A colisao de enderecos que o `igl.h:72-96` corrigiu voltou do outro lado,
   dentro do proprio `egl.cpp`** (C7): os handles de superficie caminham para a
   faixa dos contextos e para a da string a partir da 16.a criacao.
4. **A promessa do `rasterizador.h:48-49`** ("o wrap nao existe ... e isso fica
   registado no detalhe do desenho") **nao pode ser cumprida hoje**: o
   `glTexParameterx` nao distingue um pname de wrap (nao ha a constante) e o
   detalhe do desenho nao tem esse campo. O ponto 5 do cabecalho descreve um
   comportamento que o codigo nao produz -- P7: um log so entra se puder ser
   verdadeiro, e aqui o que nao pode ser verdadeiro e o COMENTARIO.
5. **O ruling do `despacho.cpp:43-47`** -- "`0xE0000001` e um valor que nao
   existe em cabecalho nenhum", dito ao corrigir o `kAeeUnsupported` -- **e
   violado no proprio subsistema 3D**: `igl.cpp:508`. O valor
   medido e 3 (`AEEStdErr.h:19`).
6. **"o rasterizador nao existe" contradiz-se dentro do mesmo ficheiro**:
   `igl.cpp:872-873` ("O consumidor (o rasterizador) nao existe") e
   `igl.h:119-120` ("o que NAO existe (o rasterizador)") contra
   `igl.cpp:204-208` ("RASTERIZADOR ... Escreve na Tela") e contra
   `rasterizador.cpp` inteiro. Mesmo padrao em `egl.cpp:644/650/728/779`.
7. **O IGL e o IEGL dao respostas OPOSTAS ao mesmo caso medido**: o
   `eglQueryString` devolve sempre um endereco valido, com o motivo escrito, por
   causa de um `strstr` sem teste de nulo medido no ddragonz (egl.cpp:441-446);
   o `glGetString` entrega NULL (igl.cpp:997). O mesmo defeito medido, duas
   decisoes contrarias no mesmo par de ficheiros.
8. **Dois testes FIXAM os dois comportamentos que acuso**:
   `tests/igl_test.cpp:504` `TEST(RegistoGl, GetErrorDevolveZeroEDiLo)` e
   `:496` `TEST(RegistoGl, GetStringRecusaEmVezDeInventar)`. Um teste que afirma
   a escolha nao mede a consequencia dela: um `EXPECT_EQ(..., GL_NO_ERROR)` fica
   verde exactamente no caso em que a chamada anterior foi recusada.
9. **A demanda, medida, e maior do que a lista de implementados sugere**: 67 dos
   80 slots do IGL sao pedidos por pelo menos um dos 4 titulos (e 17 dos 28 do
   IEGL). Os 13 slots do IGL que **nenhum dos 4 pede** sao: `AddRef`, `Release`,
   `QueryInterface`, `CompressedTexSubImage2D`, `CopyTexImage2D`,
   `CopyTexSubImage2D`, `Flush`, `LineWidthx`, `MultiTexCoord4x`, `Normal3x`,
   `PointSizex`, `SampleCoveragex`, `StencilMask`. -- Ou seja: **os TRES slots que caem no `sem()`
   (`CompressedTexSubImage2D`, `CopyTexImage2D`, `CopyTexSubImage2D`) estao
   TODOS na lista dos que nenhum dos 4 titulos pede**, e os que so guardam
   parametros sem consequencia (`LineWidthx`, `PointSizex`, `SampleCoveragex`,
   `StencilMask`, `MultiTexCoord4x`, `Normal3x`) tambem. Isto NAO absolve: o corpus tem 62 titulos e so 4
   trazem wrapper de GL; os outros 58 nao foram medidos para GL.


## 5. DEMANDA MEDIDA -- o que cada titulo pede, por nome de slot

Instrumento: `build/zb2_sonda_gl` (ja construido; nao corri build nenhum).
Corrida:

    cd /tmp/wt-auditoria/curupira
    ./build/zb2_sonda_gl /home/rafaelfrequiao/projects/zeebo-lab/games/brew/mod/{263019/chessbots,274754/ddragonz,274755/tectoy,276121/nfs}.mod

O que mede: os thunks do wrapper de GL compilados DENTRO do `.mod`, e se a
chamada chega ao modulo pelo `Despacho` do motor. **NAO mede que o titulo CHAME
o thunk em execucao**; mede que a chamada esta no codigo do titulo.

| titulo | thunks (IGL/IEGL) | chegaram | slots fora da AEEGL.h |
|---|---|---|---|
| ddragonz | 41 (28/13) | 41 de 41 | 0 |
| chessbots | 74 (61/13) | 74 de 74 | 0 |
| tectoy | 61 (49/12) | 61 de 61 | 0 |
| nfs | 56 (44/12) | 56 de 56 | 0 |

Pedido pelos QUATRO (a lista completa sai no fim de cada corrida da sonda):
`glAlphaFuncx glBindTexture glBlendFunc glClear glClearColorx glColor4x
glCompressedTexImage2D glCullFace glDeleteTextures glDepthFunc glDisable
glDisableClientState glDrawArrays glEnable glEnableClientState glLoadIdentity
glMatrixMode glTexCoordPointer glTexImage2D glTexParameterx glVertexPointer`
e, do lado EGL: `eglChooseConfig eglCreateContext eglDestroyContext
eglDestroySurface eglGetDisplay eglInitialize eglMakeCurrent eglTerminate`.

Nomes com relevancia para os achados desta auditoria:

| nome | quantos titulos pedem | achado ligado |
|---|---|---|
| `glTexParameterx` | **4** | A4 (o wrap) |
| `glCompressedTexImage2D` | **4** | aceita e regista (certo) |
| `glGetString` | 2 (chessbots, tectoy) | B1 (NULL) |
| `glGetError` | 2 (chessbots, tectoy) | A1 |
| `glGetIntegerv` | 2 (chessbots, tectoy) | C4 |
| `glDrawElements` | 3 | --
| `eglSwapBuffers` | 3 | A3 |
| `eglCreateWindowSurface` | 3 | A2 |
| `glTexSubImage2D` | 1 (chessbots) | C5 |
| `glReadPixels` | 1 (chessbots) | recusa registada (B/NAO E DEFEITO 6) |
| `eglCreatePbufferSurface` | 1 (tectoy) | B2 |
| `glLineWidthx`, `glPointSizex`, `glNormal3x`, `glMultiTexCoord4x`, `glSampleCoveragex`, `glStencilMask`, `glFlush`, `AddRef`, `Release`, `QueryInterface`, `glCopyTexImage2D`, `glCopyTexSubImage2D`, `glCompressedTexSubImage2D` | **0 de 4** | varios |

ATENCAO ao ler as recusas da sonda: ela chama cada thunk UMA vez com os
argumentos da propria bancada. As recusas que aparecem na corrida (por exemplo
`glCompressedTexImage2D: recusado: alvo diferente de GL_TEXTURE_2D`, em 4 de 4,
e `glOrthox: recusado: ortho degenerado`) sao recusas com os argumentos DA
BANCADA, e nao a prova de que o titulo chame o método com esses valores. O que
a sonda prova e a DEMANDA (o thunk existe) e a CABLAGEM (chegou ao modulo, com o
nome certo). O grep sugerido no pedido --
`grep -o 'nome' tools/baseline/bateria.json` e
`grep -o 'nome' tools/brew_slots.inc` -- **nao serve para isto**: nenhum nome
`gl*`/`egl*` aparece em `tools/baseline/bateria.json` nem em
`tools/brew_slots.inc` (conferido nome a nome). A bateria NAO mede GL; o
`brew_slots.inc` e so a tabela do lado BREW. A demanda de GL so existe na sonda
e no `docs/rewrite/REMENDO-GL-DESPACHO.md` (232 chamadas em 4 titulos).


## 6. O QUE PROCUREI E NAO ENCONTREI (resultados negativos)

Uma negativa honesta vale mais do que um achado inventado.

1. **Nao encontrei nenhum caminho que devolva sucesso sem evento** -- nem no IGL
   nem no IEGL. Contei os `return` de dentro das duas funcoes `Executar`
   (133 no `Igl::Executar`, 103 no `Egl::Executar`); os unicos `return` que nao
   passam por `Registar` sao os das lambdas auxiliares `esp`/`tem_pilha`/
   `display_ok`/`iniciado`, e esses ou nao sao terminais ou ja registaram antes.
2. **Nao encontrei nenhum erro engolido**: nenhum resultado de operacao e
   descartado. `ConstruirObjeto` e `void`; o que devolve valor
   (`Igl::Instalar`/`Egl::Instalar`, `CriarSuperficie`, `CriarContexto`,
   `rasterizador_.Desenhar`, `rasterizador_.Limpar`) e testado pelo chamador --
   e o `Instalar` de 0 chega ao `RegistarFalta("cablagem_do_GL")` do
   `despacho.cpp:308-310`.
3. **Nao encontrei um `bool` a fingir sucesso** no rasterizador: `Desenhar`
   devolve `false` em 15 sitios, sempre com o `*motivo` escrito (o teste
   `Rasterizador.PrimitivaSemCaminhoRecusaComONome` cobre o caso).
4. **Nao encontrei nenhuma funcao que devolva sempre o mesmo valor** -- excepto
   `Rasterizador::Pixels()`/`Triangulos()` e companhia, que sao contadores (nao
   contam como defeito) e o `glGetError` (A1, esse conta).
5. **Nao encontrei `if` que nunca acontece** dentro dos despachos: os 80 `case`
   sao todos alcancaveis por um slot de 0 a 79, e o `default` e o unico
   inalcancavel (seccao 3, ponto 1). Os `default` inalcancaveis que sobraram
   estao na seccao 3, pontos 7 e 8, e ficam declarados.
6. **Nao encontrei o cabecalho `AEEGL.h` na extracao do SDK** -- nao esta la, e
   `tools/achar_aegl.py` explica por que (vive dentro do MSI). Extrai-o do MSI
   com a ferramenta do proprio projeto para poder CONTAR a interface; o
   cabecalho do `IQueryInterface` (`AEEQueryInterface.h`, `AEEStdErr.h`,
   `AEEIQI.h`) e do `gl.h` estao na extracao e foram lidos.

### Limites desta auditoria (ditos, e nao escondidos)

- Nenhum titulo foi corrido ate ao DESENHO com os argumentos dele: a unica
  corrida de GL foi a sonda, com argumentos da bancada. Logo, os sintomas dos
  achados C5, C7 e C8 sao previsoes de leitura de codigo, e estao marcados
  CANDIDATO por isso.
- O `pbc` nao foi olhado: nao e dos meus ficheiros.
- A colisao de faixas com a ENTRADA (`igl.h:72-96`) foi conferida por leitura
  dos numeros (`bateria.cpp:65` instala em 20000; o IGL esta em 30000): os dois
  nao se tocam hoje.

