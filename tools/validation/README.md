# Ferramentas de validação (Milestone 1+)

Espaço reservado para comparações em lote que não cabem na suíte de testes:

* varredura de estados contra o JPL Horizons ao longo de anos, saída CSV;
* comparação do nosso integrador contra REBOUND/IAS15 no mesmo problema (§29);
* estudos de convergência (erro × tolerância × custo);
* comparação de correções relativísticas, quando existirem (Milestone 4).

O que **é** feito em `tests/` é o que precisa passar em toda build. O que vive
aqui é o que se roda deliberadamente, demora, e produz um relatório.

## `plot_trajectory.py`

Desenha uma trajetória já calculada. Só biblioteca padrão do Python — uma
ferramenta de validação que exige `pip install` é uma ferramenta que não se roda.
Saída: um HTML único, com SVG inline, sem rede e sem assets.

```bash
./build/bin/orbit-cli propagate tests/scenarios/leo-circular.json \
    --samples 600 --csv /tmp/leo.csv
./tools/validation/plot_trajectory.py /tmp/leo.csv --body Earth --title "LEO 400 km"
open /tmp/leo.html
```

Mostra a órbita projetada no **próprio plano orbital** (uma projeção XY de uma
órbita inclinada aparece achatada e convida a concluir que a excentricidade está
errada), o corpo central em escala, e as derivas de energia e momento angular.

Isto **não** é o renderizador: não sabe o que é câmera, floating origin ou Godot
(ADR-0002). Serve para um humano olhar para números e reconhecer uma trajetória
errada.
