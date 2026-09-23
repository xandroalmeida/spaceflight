# Auditoria de unidades nas interfaces do core

Data: 2026-09-13

O core usa SI internamente. A busca por interfaces ambíguas encontrou uma falha
de alto risco no plano B: o ângulo era um `double` em radianos enquanto CLI e
apresentação (então o Godot) recebem graus. Ele agora é `units::Angle`, construído explicitamente com
`Angle::radians()` ou `Angle::degrees()`.

Proteções já existentes:

- tempo de voo e passos usam `time::Duration`; instante usa `CoordinateTime`;
- frames usam `ReferenceFrame`, e corpos usam `BodyId`;
- conversões km/m, dia/s e grau/rad ficam na fronteira de I/O;
- escala de render usa métodos nomeados `radius_to_render` e `vector_to_render`.

Riscos remanescentes priorizados:

| API | risco | decisão |
|---|---|---|
| `Quaternion::from_axis_angle(..., double)` | grau passado como radiano | migrar para `units::Angle` no próximo refactor de atitude |
| campos angulares de `OrbitalElements` | unidade indicada só em comentário | considerar `Angle` sem penalizar o hot path |
| `equatorial_to_unit_vector(..._deg)` | baixo; nomes carregam a unidade | manter |
| massas, distâncias e velocidades escalares | mesmo tipo dimensional | strong types completos teriam custo amplo; manter nomes/sufixos e validação de fronteira por enquanto |
| tolerância de tempo de luz em `double` segundos | confusão possível | migrar para `Duration` junto com a próxima mudança de API |

Não foi tentada uma conversão mecânica de todo `double`: isso esconderia
mudanças semânticas extensas dentro de um milestone de verificação.
