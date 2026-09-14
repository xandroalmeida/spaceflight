# Cockpit

A geometria é **procedural** (regra 49): `scripts/cockpit/cockpit_interior.gd`
constrói a casca, as janelas, o painel, os consoles, o assento e a luz a partir
de primitivas. Não há modelo artístico e o M7 não espera por um.

## Os ângulos não são decoração

A câmera tem 75 graus verticais e a cabeça repousa 10 graus abaixo da linha do
nariz, de modo que a banda visível vai de +27,5 a −47,5 graus. Daí saem as
posições das fileiras:

```
janelas          +2  a  +21 graus
lâmpadas               −23
FLIGHT/NAV/TGT         −28
SYSTEMS                −43
botões                 −49        (a borda: pede um olhar para baixo)
```

A primeira versão pôs o painel a 55 graus abaixo, com o que ele ficava
inteiramente fora do quadro e o cockpit aparecia como uma caixa vazia com
estrelas ao fundo. Os números estão escritos no arquivo porque são o que decide
se o instrumento existe para quem está sentado.

## Três armadilhas de geometria que custaram uma captura cada

**1. Tampas de cilindro.** `CylinderMesh` nasce COM tampas, e a tampa traseira da
carenagem do cockpit é um disco de 1,05 m de raio a 85 cm do olho: 51 graus de
meio-ângulo contra os 34 da câmera. A primeira captura deste milestone foi um
retângulo castanho uniforme — o piloto estava a olhar para o interior de uma
tampa. As seções em que a câmera pode estar dentro são construídas sem tampas.

**2. O bisel por cima da tela.** O aro é uma CAIXA de 12 mm centrada em z = 0, ou
seja de −6 a +6 mm; a tela estava a +4 mm, dentro dela. O painel aparecia como
uma tábua escura com quatro lâmpadas e nenhum mostrador.

**3. O teto curto.** O viewport próximo é transparente onde não há geometria — é
isso que faz a janela funcionar, e faz um buraco no teto funcionar igualmente
bem. O teto acabava meio metro antes da janela e havia um triângulo de espaço no
canto superior da tela.

## Materiais

Todos em `scripts/world/materials.gd`, compartilhados (regra 50):
`CockpitPanel` (grafite fosco, rugosidade 0,92), `DarkComposite`, `BareMetal`,
`Glass`, `DisplayGlass`.

A rugosidade do painel é 0,92 e não 0,72 por medição: a 0,72 a luz do painel
deixava um halo especular redondo e brilhante no meio da superfície, que lê como
um defeito de renderização. Uma superfície de cockpit é fosca.

## Iluminação

Duas luzes fracas e os próprios mostradores (regra 51). O interior tem de ser
legível E o exterior tem de continuar observável, e a maneira de conseguir os
dois não é iluminar mais: é iluminar POUCO. Os mostradores emitem, o resto é
penumbra, e o monitor tem alcance dinâmico de sobra para a diferença entre um
painel a 0,3 e a Terra a 1,0.

A exposição é fixa e não automática: com auto-exposição, virar a cabeça da Terra
para o painel faria a cabine inteira pulsar de brilho, e o que se quer é
exatamente que os dois coexistam sem que nenhum mande no outro.

## Vidro

Alfa 0,032 e rugosidade 0,03, sem reflexo de ambiente (regra 52). O objetivo é
pilotar: uma janela que reflete bonito e esconde a Terra é uma janela que falhou.

## Asset externo

Um só, e opcional:
[`panel-surface-codex-prompt.md`](panel-surface-codex-prompt.md) — a textura da
superfície do painel, **sem texto nenhum** (regra 44). Sem ela o painel usa o
material liso, que já lê bem.
