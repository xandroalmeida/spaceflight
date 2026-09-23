# Quando algo parece errado

## O simulador abre e a tela fica preta

Quase sempre é a câmera dentro de um corpo. `{{key:body_scale}}` percorre o exagero
de escala dos planetas, e a 1000× o raio desenhado da Terra é maior que a
distância a que a nave está dela. Volte a `1x`.

`{{key:camera_recentre}}` devolve a câmera ao preset.

## Não há estrelas

O catálogo não foi baixado. O mostrador técnico (`{{key:debug_hud}}`) diz
exatamente isso:

```
sky            no catalogue (scripts/fetch_star_catalog.sh)
```

O céu fica vazio e **nada na dinâmica muda**. É a mesma política dos kernels: um
dado ausente é reportado, não fingido.

## Os planetas estão nos lugares errados

Se a Terra e a Lua não aparecem, faltam os kernels SPICE. O simulador diz qual
script rodar e recusa-se a começar — inventar posições seria pior do que não
começar.

## A nave gira sozinha

Há um modo de apontamento armado. `{{key:point_hold}}` manda parar; o RCS então
freia a rotação e para. Se `AP` estiver aceso, é uma missão que está pilotando —
`{{key:mission_abort}}` devolve o controle.

## O acelerador sobe sozinho

Você está segurando `Shift` para outro comando. Veja a nota em *Pilotar*: o
acelerador só arma depois de 0,2 s com o modificador sozinho, mas segurar `Shift`
por mais tempo que isso abre o acelerador — é o que ele faz.

## A nave não aponta exatamente para onde eu mandei

Ela não vai apontar. O controlador de atitude assenta num **atraso de rastreio**
de menos de um grau — entre 0,4 e 0,7 grau em órbita baixa — e não chega a zero:
um controlador proporcional-derivativo perseguindo um alvo que gira fica atrás
dele. Isso é o comportamento de um controlador desse tipo, não um defeito do
simulador.

Se o erro for de vários graus e não fechar, confira o corpo de referência no
instrumento `NAVIGATION`: os apontamentos são relativos a ele.

## As teclas de função não fazem nada

No macOS, `F1`, `F3` e as outras são teclas de mídia por padrão. Use `fn` junto,
ou ligue "usar F1, F2 etc. como teclas de função padrão" nos ajustes do teclado.

## `{{key:point_target}}` diz que não está disponível

Está certo: apontar para o alvo ainda não existe. Veja *Pilotar*.

## `SEARCH` não encontra nada

A mensagem diz o motivo, e a lista `REFUSED` diz por que cada geometria voada
foi recusada. As causas comuns:

- a janela de busca não contém uma partida boa a partir da órbita atual. Espere
  uma fração de órbita e tente de novo;
- o corretor não convergiu. A busca examina 24 candidatas, voa as 6 melhores com
  o modelo completo e recusa um plano que não chega ao alvo, com o motivo — em
  vez de devolver uma trajetória que erra.

Um plano recusado é melhor que um plano falso: você continua no mesmo estado.

## O periapsis está negativo

Está certo. Os elementos do cockpit são medidos em torno do **corpo de
referência**, que é a Terra durante a maior parte da travessia. A caminho da Lua
a órbita terrestre é uma elipse muito alongada cujo periapsis está dentro do
planeta, e a altitude desse periapsis é negativa.

O corpo de referência troca sozinho quando a nave entra na vizinhança
gravitacional de outro — perto da Lua, passa a ser a Lua —, e os números passam
a ser em torno dela.

## O quadro fica lento em warp alto

Esperado, e a causa é o núcleo e não o desenho. Em warp alto cada quadro pede
muito mais tempo coordenado, e perto de um corpo o controle de erro do propagador
encurta o passo — mais trabalho por quadro. `{{key:debug_hud}}` mostra a taxa de
quadros e quanto tempo cada um leva.

## As luzes da cidade aparecem do lado errado

Não aparecem. A orientação da Terra vem dos mesmos kernels que a trajetória, e
está conferida contra um fato externo: em 1º de janeiro de 2026, às 00:00 UTC, o
ponto subsolar está a 180,9° leste e 23° sul, porque o meio-dia solar em
Greenwich é às 12:00 e porque é janeiro. Se alguma vez parecer errado, é um
defeito e vale reportar.

## A Terra parece só oceano

Depende da hora e do lugar: um terço da Terra é o Pacífico. O voo começa sobre a
África às duas da tarde, hora local, justamente para que o primeiro olhar pela
janela encontre um continente; noventa minutos de órbita depois, a nave pode
estar sobre o mar ou do lado noturno.
