# Ferramentas de validação (Milestone 1+)

Espaço reservado para comparações em lote que não cabem na suíte de testes:

* varredura de estados contra o JPL Horizons ao longo de anos, saída CSV;
* comparação do nosso integrador contra REBOUND/IAS15 no mesmo problema (§29);
* estudos de convergência (erro × tolerância × custo);
* comparação de correções relativísticas, quando existirem (Milestone 4).

O que **é** feito em `tests/` é o que precisa passar em toda build. O que vive
aqui é o que se roda deliberadamente, demora, e produz um relatório.
