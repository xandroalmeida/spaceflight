# M7 — checklist de teste manual

Parte da Definition of Done (regra 63). Uma pessoa senta, executa a lista, e
marca. Nada aqui é automatizável: a pergunta é se **funciona de forma
satisfatória para alguém que nunca viu o projeto**, e isso é julgamento humano.

Para rodar:

```bash
cmake -S . -B build-godot -DSPACEFLIGHT_BUILD_GODOT=ON
cmake --build build-godot --target spaceflight_gdextension -j
./external/godot/Godot.app/Contents/MacOS/Godot --path godot/project
```

## Primeiro contato (regras 64, 66)

```
[ ] o simulador abre direto no cenário, sem menu
[ ] em poucos segundos há cockpit, Terra e estrelas — não uma tela preta
[ ] dá para perceber sem ler nada que a nave está em órbita da Terra
[ ] os mostradores já estão a mostrar números
```

## Olhar em volta (regras 10, 11)

```
[ ] arrastar com o botão direito vira a cabeça
[ ] a cabeça para nos limites e não atravessa o painel nem as paredes
[ ] dá para ler os três mostradores e a faixa de sistemas
[ ] olhando para baixo aparecem os sete botões
[ ] olhando para cima aparece o painel superior, não o espaço
[ ] Home recentra
```

## Controle manual (regras 13, 14, 15)

```
[ ] W/S/A/D/Q/E giram a nave e o giro custa propelente (a massa cai)
[ ] I/J/K/L/U/O transladam sem girar
[ ] Shift/Ctrl abrem e fecham o acelerador continuamente
[ ] Z abre tudo, X corta
[ ] a barra do acelerador e o empuxo mudam juntos
[ ] V desliga o RCS e o apontamento para junto
[ ] P aponta prógrado e a linha âmbar do FLIGHT fecha até o marcador
```

## O que se vê da nave (regras 5, 16, 29)

```
[ ] C leva a EXTERNAL e a nave é reconhecível: cockpit, habitat, tanques,
    radiadores, motor, antenas
[ ] arrastar orbita a nave; a roda aproxima
[ ] azimute 180° mostra a cauda — dá para chegar atrás da nave
[ ] o lado contrário ao Sol fica escuro
[ ] com o acelerador aberto a pluma acende e o comprimento acompanha o empuxo
[ ] com o tanque vazio a pluma apaga mesmo com o acelerador cheio
[ ] ao comandar uma guinada, os jatos que acendem são os do binário certo
[ ] um comando diagonal acende mais jatos, com chamas de tamanhos diferentes
```

## Instrumentos (regras 17-22)

```
[ ] FLIGHT: os marcadores estão nos sítios certos e o nariz está no centro
[ ] NAVIGATION: a elipse fecha, AP e PE estão nos extremos, a seta aponta no
    sentido da marcha
[ ] TARGET: distância e velocidade relativa da Lua batem com o HUD técnico (F3)
[ ] SYSTEMS: as doze lâmpadas de RCS acendem com os jatos e apagam com eles
[ ] RELATIVITY mostra β ~ 1e-4 e não ocupa meio painel
[ ] as unidades mudam sozinhas: m → km → AU, m/s → km/s → c
```

## Mapa e alvo (regras 20, 53, 55)

```
[ ] M abre o mapa; a roda faz zoom; M fecha
[ ] Terra, órbita atual, nave e caminho da Lua aparecem
[ ] os anéis de escala têm distâncias legíveis
[ ] ' e ; trocam o alvo e o TARGET acompanha
```

## Missão (regras 25-27, 68)

```
[ ] Tab abre o computador; ◀ ▶ escolhem o alvo
[ ] PLAN TRANSFER avisa ANTES de travar o quadro
[ ] o resumo mostra partida, chegada, tempo de voo, Δv por queima, propelente e
    a órbita prevista
[ ] a lista de candidatos que a busca voou aparece abaixo
[ ] CANCEL descarta sem armar nada
[ ] EXECUTE arma, e a mensagem MISSION PLAN ACCEPTED aparece
[ ] o mapa mostra a trajetória planejada e os marcadores de queima com ETA
[ ] o canto superior direito mostra a fase e a contagem para o próximo evento
[ ] Shift+K aborta e devolve a atitude ao piloto sem teleportar a nave
```

## A Terra e a Lua, de perto (regras 31, 32, 47)

```
[ ] F foca a Terra: o disco inteiro aparece, com continentes reconhecíveis
[ ] há nuvens, e elas deixam ver o que está por baixo
[ ] o terminador é nítido e o lado noturno tem luzes de cidade
[ ] NÃO há uma linha vertical atravessando o planeta (a costura de UV)
[ ] as calotas polares estão nos POLOS e não no equador
[ ] com o tempo a Terra gira, e gira para leste
[ ] F de novo até a Lua: mares na face voltada para a Terra, crateras nas bordas
[ ] o casco da nave tem painéis, costuras e fixadores visíveis de perto
[ ] o painel do cockpit tem textura, e nenhum texto embutido nela
```

## A viagem (regra 56 — o vertical slice)

```
[ ] . sobe o warp e a mudança aparece na tela
[ ] durante a injeção a nave aponta sozinha e a pluma acende
[ ] em warp alto a Lua cresce visivelmente
[ ] a câmera TARGET enquadra a Lua durante a aproximação
[ ] a queima de captura acontece
[ ] no fim há uma órbita lunar: F3 mostra `about Moon CAPTURED` com e < 1
[ ] a Lua tem superfície com crateras e mares, não é uma esfera cinza lisa
```

## Interface e ajustes (regras 38, 39, 69, 70, 75)

```
[ ] ` alterna cockpit / mínimo / nenhum
[ ] F3 mostra o HUD técnico com todos os números e F3 esconde
[ ] F1 mostra a ajuda e as teclas nela batem com o que o teclado faz
[ ] Space pausa: o tempo para, a câmera continua a responder
[ ] Esc abre o menu; RESUME, CONTROLS e QUIT funcionam
[ ] os quatro deslizadores fazem o que dizem
```

## Áudio (regras 36, 37)

```
[ ] há ventilação de fundo dentro da nave
[ ] o motor soa e o volume acompanha o empuxo
[ ] cada disparo de RCS dá um golpe
[ ] os botões clicam, os avisos tocam
[ ] na câmera EXTERNAL o som da nave some — nada atravessa o vácuo
```

## Resoluções (regra 59)

```
[ ] 1920x1080   nada cortado, nada ilegível
[ ] 2560x1440   idem
[ ] 3840x2160   idem
```

```bash
./external/godot/Godot.app/Contents/MacOS/Godot --path godot/project --resolution 2560x1440
```

## Desempenho (regra 58)

```
[ ] F3 mostra o quadro abaixo de 16,7 ms em warp 1x com o cockpit aberto
[ ] durante o warp pesado o número cai — e isso é o core, não o desenho
```

---

## Resultado

| data | quem | versão | resultado |
|---|---|---|---|
| | | | |
