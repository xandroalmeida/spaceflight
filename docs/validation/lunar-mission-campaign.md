# Earth → Moon validation campaign

> **Superado pelo Milestone 6.1.** Esta campanha media uma configuração sem busca
> — ponto de partida e tempo de voo fixos — e 52 das suas 100 épocas terminavam
> dentro da Terra depois de o solver reportar convergência. O diagnóstico, as
> correções e a campanha nova estão em
> [lunar-navigation-hardening.md](lunar-navigation-hardening.md); os dados, em
> [lunar-navigation-campaign-v2.csv](lunar-navigation-campaign-v2.csv).


Generated from 100 deterministic cases beginning 2026-01-01, spaced by 1 day(s). Each case uses the same spacecraft and parameters; only the TDB departure epoch changes.

Success rate: **18/100 (18.0%)**.
Lambert/position-corrector convergence: **26/100**.  
B-plane convergence: **79/100**.

| Metric | min | median | p95 | max |
|---|---:|---:|---:|---:|
| absolute periapsis error [km] | 0.004483 | 0.0728475 | 0.257868 | 0.28252 |
| insertion delta-v [m/s] | 809.465 | 841.427 | 904.794 | 916.998 |
| final eccentricity | 0.00160094 | 0.00176875 | 0.00210949 | 0.00218251 |
| final apoapsis altitude [km] | 102.78 | 103.265 | 103.917 | 104.048 |
| final periapsis altitude [km] | 96.0277 | 96.6636 | 97.0025 | 97.2792 |
| fuel used [kg] | 8.91059 | 16.6504 | 31.993 | 34.5242 |

## Failure modes

- 53 × lunar insertion remained hyperbolic
- 22 × position corrector did not converge
- 1 × trajectory intersects the lunar surface
- 6 × timeout after 30 s

The CSV beside this report contains every case. This campaign is a robustness sample, not evidence outside its epoch range, transfer time, B-plane angle, force model or spacecraft configuration.
