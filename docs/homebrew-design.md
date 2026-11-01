# Aurora Coleta

Jogo caseiro original executado pelo interpretador MIPS I do Aurora. O gerador
`makeHomebrewPsxExe()` produz um PS-X EXE de 16 KiB; não usa compilador MIPS externo,
BIOS, bibliotecas de jogos ou um núcleo de outro emulador.

O personagem verde precisa coletar cinco cristais amarelos, na ordem em que
aparecem, evitando o guarda vermelho que percorre o centro da arena. Os cinco
marcadores no topo mostram o progresso. Coletar tudo mostra **VOCE VENCEU!**;
encostar no guarda mostra **FIM DE JOGO**. START reinicia a partida em qualquer
fase. As posições, colisões, pontuação, vitória, derrota e desenho são processados
por instruções MIPS convidadas, não por lógica de jogo no aplicativo Windows.

## Protocolo do programa convidado

- Código e dados gráficos começam em `0x80010000`, com pilha em `0x801fff00`.
- O estado público começa em `0x80080000`; os offsets estão em `homebrew.hpp`.
- A cada VBlank, o programa lê `I_STAT` em `0x1f801070` e reconhece a solicitação
  com escrita zero. Não depende de entrega de interrupção à CPU.
- O controle digital usa JOY_DATA/STAT/MODE/CTRL/BAUD do PS1. Transmite
  `01 42 00 00 00`, verifica TX/RX em JOY_STAT e recebe `ff 41 5a lo hi`, com botões
  ativos em zero. O programa respeita load delay e branch delay do MIPS I.
- O desenho usa GP0 `60h` (retângulo sólido variável), com área de desenho e
  offsets inicializados pelos comandos E3/E4/E5/E6. Cada palavra GP0 espera a
  disponibilidade anunciada por GPUSTAT. Os glifos de 3 × 5 pixels e toda a arte
  são dados próprios desenhados por retângulos.
- A apresentação é de 320 × 240 pixels. Não há som, texturas ou geometria 3D.

## Estado e regras

| Campo | Offset | Valor inicial |
| --- | --- | --- |
| playerX / playerY | 0 / 4 | 32 / 64 |
| score | 8 | 0 |
| phase | 12 | 0: jogando; 1: vitória; 2: derrota |
| enemyX / enemyDirection | 16 / 20 | 144 / +1 |
| oldButtons | 24 | 0 |
| frameCounter | 28 | 0 |

O personagem e o guarda têm caixas de colisão de 16 × 16 pixels. O movimento é de
dois pixels por quadro; o personagem fica entre X 16–288 e Y 40–208. O guarda
move-se horizontalmente em Y 112, entre X 16 e 288. Os cristais ficam em
`(64,64)`, `(240,64)`, `(240,176)`, `(64,176)` e `(152,48)`.

START usa transição de botão solto para pressionado. Um reinício restaura o estado
inicial, produz `frameCounter=1` no quadro de reinício e não move os personagens
nesse quadro. Depois, segurar START não reinicia repetidamente. Vitória e derrota
congelam o movimento, mantendo a leitura do controle para permitir um reinício.

## Validação e limites

O teste de integração executa o PS-X EXE gerado no interpretador com o barramento
real do projeto: controles alteram pacotes SIO, instruções MIPS atualizam o estado
e comandos GP0 alteram VRAM. A rota de vitória e a de derrota são feitas com
entradas do controle, sem alterar posições ou pontuação diretamente na RAM.

O host ainda agenda VBlank por quadro e o SIO é síncrono. Isso demonstra o
subconjunto de hardware implementado, sem precisão de ciclos ou compatibilidade
geral com PS1. Não foi validado em um console PS1 físico; produzir um PS-X EXE e
executá-lo no Aurora não estabelece sozinho essa compatibilidade.
