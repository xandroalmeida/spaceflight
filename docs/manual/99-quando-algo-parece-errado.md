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
de cerca de 2,6 graus e não chega a zero: um controlador proporcional-derivativo
perseguindo um alvo que gira fica atrás dele. Isso é o comportamento de um
controlador desse tipo, não um defeito do simulador.

## `PLAN TRANSFER` não encontra nada

A mensagem diz o motivo. As causas comuns:

- a janela de busca de duas horas não contém uma partida boa a partir da órbita
  atual. Espere uma fração de órbita e tente de novo;
- o corretor não convergiu. Ele tenta até três candidatos e recusa um plano que
  não chega ao alvo, com o motivo — em vez de devolver uma trajetória que erra.

Um plano recusado é melhor que um plano falso: você continua no mesmo estado.

## O periapsis está negativo

Está certo. Os elementos do cockpit são medidos em torno do **corpo de
referência**, que continua sendo a Terra durante toda a travessia. A caminho da
Lua a órbita terrestre é uma elipse muito alongada cujo periapsis está dentro do
planeta, e a altitude desse periapsis é negativa.

Quando você chega perto da Lua, o mostrador técnico passa a mostrar também a
órbita **em torno do alvo**, que é a que interessa ali.

## O quadro fica lento em warp alto

Esperado, e a causa é o núcleo e não o desenho. Em warp alto cada quadro pede
muito mais tempo coordenado, e perto de um corpo o controle de erro do propagador
encurta o passo — mais trabalho por quadro. `{{key:debug_hud}}` separa os tempos.

## As luzes da cidade aparecem do lado errado

Não aparecem. A orientação da Terra vem dos mesmos kernels que a trajetória, e
está conferida contra um fato externo: às 00:00 UTC o ponto subsolar está a
180,9° leste e 23° sul, porque o meio-dia solar em Greenwich é às 12:00 e porque
é janeiro. Se alguma vez parecer errado, é um defeito e vale reportar.
