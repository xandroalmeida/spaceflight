# Controles

> Gerado por `spaceflight --dump-controls` a partir de
> `app/presentation/input_actions.cpp`. Não editar à mão: rode
> `scripts/dump_controls.sh` depois de mudar um atalho.

O mouse:

| | |
|---|---|
| arrastar com o botão direito | no cockpit, olhar em volta; fora dele, orbitar a nave |
| `Alt` + arrastar | girar a câmera no lugar, deixando o alvo para trás |
| roda | aproximar e afastar (fora do cockpit) |
| clique esquerdo | premir o botão do painel sob o ponteiro |

As setas movem a CÂMERA; `WASD` move a NAVE. São duas famílias de teclas
porque são duas coisas diferentes: girar a nave queima propelente, girar a
câmera não muda um número do estado.

## ATITUDE

| tecla | ação |
|---|---|
| `W` | arfagem para cima |
| `S` | arfagem para baixo |
| `A` | guinada à esquerda |
| `D` | guinada à direita |
| `Q` | rolagem à esquerda |
| `E` | rolagem à direita |

## TRANSLAÇÃO

| tecla | ação |
|---|---|
| `I` | à frente |
| `K` | atrás |
| `J` | à esquerda |
| `L` | à direita |
| `U` | acima |
| `O` | abaixo |

## MOTOR

| tecla | ação |
|---|---|
| `Shift` | abre o acelerador |
| `Ctrl` | fecha o acelerador |
| `Z` | acelerador cheio |
| `X` | corte do motor |
| `G` | modo IMPULSE / CRUISE |
| `V` | RCS ligado / desligado |
| `B` | RCS rotação / translação |

## APONTAMENTO

| tecla | ação |
|---|---|
| `P` | prógrado |
| `Shift+P` | retrógrado |
| `N` | normal |
| `Shift+N` | anti-normal |
| `R` | radial para fora |
| `Shift+R` | radial para dentro |
| `T` | para o alvo (ainda indisponível) |
| `Shift+T` | contra o alvo (ainda indisponível) |
| `0` | manter atitude |

## CÂMERA

| tecla | ação |
|---|---|
| `C` | alterna o modo de câmera |
| `Home` | recentra o olhar |
| `Alt` | olhar em volta (mantido) |
| `]` | aproxima |
| `[` | afasta |
| `F` | foca o próximo corpo |

## MISSÃO

| tecla | ação |
|---|---|
| `'` | próximo alvo |
| `;` | alvo anterior |
| `Shift+J` | planeja a transferência |
| `Enter` | executa o plano |
| `Shift+K` | ABORTA a missão |
| `Tab` | computador de navegação |
| `M` | mapa orbital |
| `Shift+M` | mapa local / sistema solar |

## TEMPO

| tecla | ação |
|---|---|
| `.` | sobe o time warp |
| `,` | desce o time warp |
| `Space` | pausa |
| `Escape` | menu |

## INTERFACE

| tecla | ação |
|---|---|
| `Crase` | HUD completo / mínimo / nenhum |
| `F3` | HUD técnico (todos os números) |
| `F1` | ajuda dos controles |
| `PageUp` | exposição do céu + |
| `PageDown` | exposição do céu - |

## TÉCNICO

| tecla | ação |
|---|---|
| `F5` | reinicia a órbita de partida |
| `F6` | planejador: finito / piloto automático |
| `F7` | escada de β visual |
| `F8` | exagero de escala dos corpos |
| `Alt+C` | queima de cruzeiro (β relativístico) |
| `Alt+A` | aberração liga/desliga |
| `Alt+D` | Doppler liga/desliga |
| `Alt+B` | beaming liga/desliga |
| `Alt+L` | tempo de luz liga/desliga |

## O que NÃO tem tecla, e porquê

**Apontar para o alvo** (`T`) está no mapa e responde dizendo que não
está disponível. O controlador de atitude do core aceita LEIS DE
GUIAMENTO -- prógrado, normal, radial -- e "para onde a Lua está" não é
uma lei, é uma direção. Dar-lhe uma tecla que falha em voz alta é melhor
do que dar-lhe uma tecla que não existe, e melhor do que inventar um modo
de guiamento no renderizador. Está no backlog.

**O manche** do console direito não é interativo. Quem pilota é o teclado;
um manche que se mexesse sem comandar nada seria decoração a fingir ser
instrumento.
