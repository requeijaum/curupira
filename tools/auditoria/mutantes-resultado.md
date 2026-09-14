| 1 | `core/traco/traco.cpp` | etapa0: a recusa de caminho relativo (P7/regra 3) | `Traco.LogRelativo*` | VERMELHO | VERMELHO | Traco.LogRelativoERecusadoComMotivo |
| 2 | `core/traco/traco.cpp` | etapa0: recusa de comparar configuracoes diferentes | `Traco.CompararRecusa*` | VERMELHO | VERMELHO | Traco.CompararRecusaConfiguracoesDiferentes |
| 3 | `core/traco/traco.cpp` | P2: a falta fica contada | `Traco.NaoImplementadoFicaContadoENomeado` | VERMELHO | VERMELHO | Traco.NaoImplementadoFicaContadoENomeado |
| 4 | `core/traco/traco.cpp` | regra 4: marcas distintas, nao linhas | `Traco.MarcasDeDepuracao*` | VERMELHO | VERMELHO | Traco.MarcasDeDepuracaoSaoListaveisParaLimpeza |
| 5 | `core/memoria/memoria.cpp` | P6: uma vez por endereco | `Memoria.VigiaRegistaOPrimeiroEscritor*` | VERMELHO | VERMELHO | Memoria.VigiaRegistaOPrimeiroEscritorDeCadaEndereco |
| 6 | `core/memoria/memoria.cpp` | a faixa da vigia limita o registo | `Memoria.VigiaRegistaOPrimeiroEscritor*` | VERMELHO | VERMELHO | Memoria.VigiaRegistaOPrimeiroEscritorDeCadaEndereco |
| 7 | `core/memoria/memoria.cpp` | P6: escritas sem escritor declarado sao contadas | `Memoria.EscreverSemEscritorDeclaradoEsContado` | VERMELHO | VERMELHO | Memoria.EscreverSemEscritorDeclaradoEsContado |
| 8 | `core/memoria/memoria.cpp` | limite da cadeia terminada em NUL | `Memoria.CadeiaTerminadaEmNul*` | VERMELHO | VERMELHO | Memoria.CadeiaTerminadaEmNulRespeitaOLimite |
| 9 | `core/cpu/arm_interpreter.cpp` | etapa1: cpsr inicial e modo valido | `Cpu.AposRepor*,Cpu.MrsDevolve*` | VERMELHO | VERDE | - |
| 10 | `core/cpu/arm_interpreter.cpp` | etapa1: zero nao e modo nenhum | `Cpu.ZeroNaoEUmModoValido` | VERMELHO | VERMELHO | Cpu.ZeroNaoEUmModoValido |
| 11 | `core/cpu/arm_interpreter.cpp` | etapa1: modo invalido escrito e recusado e contado | `Cpu.EscreverUmModoInvalidoERecusado` | VERMELHO | VERMELHO | Cpu.EscreverUmModoInvalidoERecusado |
| 12 | `core/carga/mod.cpp` | etapa2: a tabela de ajudantes em base-4 (ROPI) | `Carga.OCarregadorEscreveATabelaEmBaseMenosQuatro` | VERMELHO | VERMELHO | Carga.OCarregadorEscreveATabelaEmBaseMenosQuatro |
| 13 | `core/carga/mod.cpp` | etapa2: o ponto de entrada | `Carga.OCarregadorEscreveATabelaEmBaseMenosQuatro` | VERMELHO | VERMELHO | Carga.OCarregadorEscreveATabelaEmBaseMenosQuatro |
| 14 | `core/cpu/arm_interpreter.cpp` | etapa1: a ordem do MLA (o bug real encontrado pelos testes) | `Cpu.MulEMla` | VERMELHO | VERMELHO | Cpu.MulEMla |
| 15 | `core/video/rasterizador.cpp` | etapa3: a inversao do y na projeccao | `Rasterizador.*` | VERMELHO | VERMELHO | Rasterizador.TrianguloConhecidoDaExactamenteSeisPixels, Rasterizador.ArestaPartilhadaNaoEscreveDuasVezesNemDeixaFenda |
| 16 | `core/video/rasterizador.cpp` | etapa3: a regra do canto (top-left) | `Rasterizador.*` | VERMELHO | VERMELHO | Rasterizador.ArestaPartilhadaNaoEscreveDuasVezesNemDeixaFenda, Rasterizador.ATexturaAmostraOCantoCerto |
| 17 | `core/video/rasterizador.cpp` | etapa3: o limite da janela no pixel | `Rasterizador.*` | VERMELHO | VERDE | - |
| 18 | `core/video/rasterizador.cpp` | etapa3: o teste de profundidade | `Rasterizador.*` | VERMELHO | VERMELHO | Rasterizador.OTesteDeProfundidadeDecideQuemFica |
| 19 | `core/video/rasterizador.cpp` | etapa3: o descarte de faces usa a orientacao | `Rasterizador.*` | VERMELHO | VERDE | - |
| 20 | `core/carga/bar.cpp` | FRENTE B: find("mime") apanha o diagnostico ERRADO -- assercao generica | `BlobSintetico.RecusaMimeSemTerminador` | VERDE | VERDE | - |
| 21 | `core/carga/bar.cpp` | FRENTE B: guarda da recusa arrancada e o teste continua verde (a outra recusa tem "mime") | `BlobSintetico.RecusaMimeSemTerminador` | VERDE | VERMELHO | BlobSintetico.RecusaMimeSemTerminador |
| 22 | `core/carga/bar.cpp` | guarda do mime vazio: aqui o teste TEM dentes | `BlobSintetico.RecusaMimeVazio` | VERMELHO | VERMELHO | BlobSintetico.RecusaMimeVazio |
| 23 | `core/brew/igl.cpp` | etapa6: o desenho sem tela recusa | `EstadoGl.DrawArraysSemTelaRecusaMasContaAGeometria` | VERMELHO | VERMELHO | EstadoGl.DrawArraysSemTelaRecusaMasContaAGeometria |
| 24 | `core/brew/igl.cpp` | etapa6: o tamanho do vertice 2..4 | `EstadoGl.*` | VERMELHO | VERMELHO | EstadoGl.ArraysDeVerticesGuardamOTipoEOPasso |
| 25 | `core/brew/egl.cpp` | IEGL: o versionamento 2 recusa | `CicloDoEgl.OCriaContextoRecusaOVersionamento2` | VERMELHO | VERMELHO | CicloDoEgl.OCriaContextoRecusaOVersionamento2 |
| 26 | `core/brew/egl.cpp` | IEGL: o valor servido vem do cabecalho | `CicloDoEgl.OGetConfigAttrib*` | VERMELHO | VERMELHO | CicloDoEgl.OGetConfigAttribServeOMedidoERecusaONaoMedido |
| 27 | `core/audio/misturador.cpp` | etapa5: a escala do volume | `Misturador.*` | VERMELHO | VERMELHO | Misturador.ContaAsAmostrasEAsNaoNulas, Misturador.VolumeMetadeBaixaOPicoAMetade |
| 28 | `core/brew/imedia.cpp` | etapa5: amostras nao nulas contadas | `Media.OMisturadorContaAsAmostrasQueAMidiaEntrega` | VERMELHO | VERDE | - |
| 29 | `core/brew/ihid_entrada.cpp` | etapa8: o nState 0/1 | `Hid.GuiaoInvalidoEhRecusadoInteiro` | VERMELHO | VERMELHO | Hid.GuiaoInvalidoEhRecusadoInteiro |
| 30 | `core/brew/ihid_entrada.cpp` | etapa8: o guiao fora de ordem | `Hid.GuiaoInvalidoEhRecusadoInteiro` | VERMELHO | VERMELHO | Hid.GuiaoInvalidoEhRecusadoInteiro |
| 31 | `core/brew/classes.cpp` | classes: o nome do metodo vem da tabela do cabecalho | `Classes.OMetodoNaoImplementadoRecusaComONomeDoMetodo` | VERMELHO | VERMELHO | Classes.OMetodoNaoImplementadoRecusaComONomeDoMetodo |
| 32 | `tools/comparar.cpp` | etapa9: campo desconhecido e recusado | `Comparar.CampoDesconhecidoERecusado` | VERMELHO | VERMELHO | Comparar.CampoDesconhecidoERecusado |
| 33 | `core/brew/formato.cpp` | formato: o hexadecimal do sistema | `Formato.HexadecimalComLargura` | VERMELHO | VERDE | - |
| 34 | `core/brew/ihiddevice.cpp` | etapa8: botao desconhecido recusa com E_ENOSUCH | `Hid.BotaoDesconhecidoRecusaComEnosuch` | VERMELHO | VERMELHO | Hid.BotaoDesconhecidoRecusaComEnosuch |
| 35 | `core/video/rasterizador.cpp` | etapa3: o descarte de faces (mutante nao-equivalente) | `Rasterizador.ODescarteDeFaces*` | VERMELHO | VERMELHO | Rasterizador.ODescarteDeFacesUsaAOrientacaoEmNDC |
| 36 | `core/brew/despacho.cpp` | etapa8/laco: a assinatura do SetTimer (duracao no r1, funcao no r2) | `Widget.OSetTimer*` | VERMELHO | VERMELHO | Widget.OSetTimerLeADuracaoDoR1EAFuncaoDoR2, Widget.OSetTimerComARelacaoTrocadaNaoArmaOLaco |
| 37 | `core/brew/imedia.cpp` | FRENTE B: find("nao voltou") nao fixa o formato -- assercao generica | `Media.UmCallbackQueNaoVolta*` | VERDE | VERDE | - |
| 38 | `core/brew/interface.cpp` | a IBase nao se cabla | `Interface.CablarRecusaOSlotDaIBase` | VERMELHO | VERMELHO | Interface.CablarRecusaOSlotDaIBase |
| 39 | `core/cpu/arm_interpreter.cpp` | FRENTE B: o cpsr do CONSTRUTOR (o bug do projeto antigo) pode ficar a zero -- nenhum teste apanha | `Cpu.*` | VERDE | VERDE | - |
| 40 | `core/brew/formato.cpp` | o caminho sem largura do %x | `Formato.*` | ? | VERDE | - |
| 41 | `core/brew/imedia.cpp` | o contador nao_nulas do imedia (so vai para o log) | `Media.*` | ? | VERDE | - |
| 42 | `core/brew/ajudantes.cpp` | etapa7: instalar uma tabela com slot por preencher RECUSA | `Ajudantes.RecusaInstalarComSlotPorImplementar` | VERMELHO | VERMELHO | Ajudantes.RecusaInstalarComSlotPorImplementar |
| 43 | `core/brew/interface.cpp` | etapa7: a leitura de volta da cablagem | `Interface.CablarDetetaUmaCablagemPerdida` | VERMELHO | VERDE | - |
| 44 | `core/brew/ihiddevice.cpp` | etapa8: handle de dispositivo desconhecido | `Hid.OIhidEntregaUmDispositivo*` | ? | VERDE | - |
| 45 | `core/brew/ihiddevice.cpp` | etapa8: handle desconhecido (filtro largo) | `Hid.*` | ? | VERDE | - |