# Convergência numérica e dependência do timestep

Data: 2026-09-13  
Propagador: Dormand–Prince 5(4), órbita Kepleriana excêntrica por uma revolução  
Oráculo: propagador universal-variable independente em `tests/support/kepler.hpp`

| rtol | erro de posição [m] | erro de velocidade [m/s] | deriva de energia | deriva de momento angular | deriva de tempo próprio [s] | passos |
|---:|---:|---:|---:|---:|---:|---:|
| 1e-6 | 819.678 | 0.406604 | 3.23145e-5 | 9.41240e-6 | 1.82e-12 | 29 |
| 1e-8 | 8.54956 | 0.00510217 | 6.47332e-8 | 1.67004e-8 | 5.46e-12 | 60 |
| 1e-10 | 0.0776174 | 5.20141e-5 | 4.47393e-10 | 2.78603e-10 | 0 | 145 |
| 1e-12 | 0.000641706 | 4.45688e-7 | 6.15239e-12 | 3.45268e-12 | 1.09e-11 | 361 |
| 1e-13 | 0.0000597816 | 4.19526e-8 | 6.37243e-13 | 3.53629e-13 | 1.82e-12 | 571 |

Posição e velocidade convergem monotonicamente; apertar `rtol` por `1e7`
reduziu o erro de posição por fator `1.37e7`. A deriva de tempo próprio já está
no piso de arredondamento e, por isso, não é monotônica.

O teste de particionamento executa a mesma missão com FPS, warp e número de
chamadas distintos. Entre 1× e 100000×, o pior caso medido foi `1.054e-3 m` e
`1.417e-6 m/s`; todos terminam exatamente na mesma `CoordinateTime`. `min_step`
impossível, orçamento de passos esgotado e mudanças de `max_step` são reportados,
nunca relaxados silenciosamente.

Reprodução:

```bash
./build/bin/test_integrator_convergence
./build/bin/test_verification_hardening
```
