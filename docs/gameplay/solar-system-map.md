# O mapa do Sistema Solar

> Milestone 8, regras 26–31 e 71–75.

---

## Dois mapas, dois referenciais

`M` abre o mapa. `Shift+M` alterna entre:

| | LOCAL | SYSTEM |
|---|---|---|
| centro | o corpo que a nave orbita | o Sol |
| unidades | unidades de cena, pela origem flutuante | **metros** |
| desenha | órbita osculadora, caminho do alvo, transferência, queimas | Sol, planetas, órbitas planetárias, nave, transferência, queimas |
| anéis | km / milhões de km | UA |

A separação não é de escala, é de **frame**. O mapa local ancora cada ponto do
arco planejado no corpo de origem; sobre uma transferência translunar de 4,75
dias a Terra anda 12,7 milhões de quilômetros, e desenhar o arco em coordenadas
absolutas fazia o mapa do Milestone 6.2 mostrar uma linha de 13 milhões de km
contra uma Lua a 361 mil. Sobre 204 dias a Terra anda 500 milhões, e o erro é o
mesmo com quarenta vezes o tamanho.

Então cada camada declara o referencial em que chega, e o mapa converte tudo para
**um** antes de desenhar (regra 31). O payload de `get_system_map()` carrega o
nome do frame como um campo, não como um comentário:

```text
frame   "Sun-centred J2000, metres"
centre  "Sun"
```

E o mapa imprime isso no canto, para quem está olhando.

---

## O zoom é logarítmico

O mapa tem de cobrir de **1e6 m** (uma órbita baixa) a **1e12 m** (a órbita de
Netuno) — seis ordens de grandeza.

Com zoom linear não existe um passo que sirva: o que é confortável perto da Terra
atravessa o sistema solar exterior num clique, e o que funciona em Netuno não sai
do lugar em órbita baixa. O fator de zoom é elevado a uma potência, de modo que
cada clique da roda é um passo constante em **ordens de grandeza**.

---

## As órbitas planetárias vêm da efeméride

Não são elipses keplerianas desenhadas a partir de elementos: são **amostras do
SPICE** (regra 29). A diferença entre a órbita que um planeta tem e a que ele
teria se fosse um problema de dois corpos é de centenas de milhares de
quilômetros, e um mapa que desenhasse a segunda estaria a inventar uma
trajetória que a simulação não voa.

Noventa e seis amostras por corpo, por uma revolução, centradas em **agora** —
meia revolução para cada lado. O período vem dos elementos osculadores do próprio
corpo, lidos na hora: uma tabela de períodos aqui seria um fato sobre Marte
escrito onde ninguém o verifica.

### O ano de Netuno não cabe

O `de440s.bsp` cobre 1849 a 2150 — 301 anos. O ano de Netuno tem 165. Partindo de
2026, meia revolução para cada lado vai de 1944 a 2108 e cabe. Partindo de 2100,
não caberia.

Quando não cabe, o arco é **recortado** contra a cobertura dos kernels e o campo
`clipped` diz que foi. A alternativa seria extrapolar em silêncio, que é o que
este projeto não faz.

As luas não recebem caminho heliocêntrico: nesse zoom a Lua e a Terra são o mesmo
pixel (regra 27).

---

## Rótulos

Antiaglomeração básica (regra 73): um rótulo só é desenhado se não cair a menos
de uma distância mínima de outro já colocado. Sem isso, os quatro planetas
interiores viram uma mancha de texto sempre que o mapa está aberto o bastante
para mostrar Marte.

---

## Custo

`get_system_map()` é reconstruído com os demais traçados, quatro vezes por
segundo, e **só quando o mapa está aberto no modo SYSTEM**. Ele custa uma consulta
à efeméride por amostra do arco planejado.

`get_system_orbit_paths()` é pedido **uma vez**, quando o modo é ligado: são cerca
de mil consultas à efeméride e o resultado não muda de quadro para quadro.

---

## O que o mapa não faz

Não integra, não resolve Kepler, não propaga e não tem opinião sobre para onde a
nave vai. Tudo o que ele desenha chegou pronto do core (regra 5). Clicar num
corpo para selecioná-lo (regra 75) não está implementado: a seleção pelo
computador de bordo basta, e a regra diz que basta.
