P3. Расширить распараллеливание выше chance-узлов

Критичность: Critical
Ожидаемый выигрыш: 1.3-1.8x на Ryzen 5950X после P1/P2, при удачном scheduler tuning местами выше.

Цель: убрать текущий потолок загрузки CPU, при котором solver хорошо распараллеливает в основном `chance`-узлы, но остаётся слишком последовательным на верхней части дерева действий.

Текущее состояние

- `chanceUtility()` уже умеет распараллеливать обработку валидных карт, но в основном через локальный `omp parallel for`.
- `actionUtility()` обходит ветки действий последовательно.
- `train()` запускает `cfr()` без общего task scheduler, поэтому параллелизм не протягивается сквозь рекурсию естественным образом.
- Из-за этого CPU utilisation упирается в верхнюю последовательную часть дерева, а под нагрузкой после `P1/P2` это выглядит как следующий главный bottleneck.

Scope

- Добавить управляемый task-based scheduler для solve path.
- Расширить параллелизм на action-ветки выше первого `chance`-разбиения (`split_round`).
- Сохранить или восстановить параллелизм на самих `chance`-узлах при активном root parallel region.
- Исключить shared accumulation в action split: каждая ветка пишет в свой буфер, merge идёт после `taskwait`.
- Оставить fallback на старый последовательный путь для безопасного отката и A/B benchmark.
- Не менять математическую модель CFR, storage policy trainables или структуру дерева в этой фазе.

P3a. Управляемое включение scheduler-а

- Добавить флаг `task_parallelism` в CLI/runtime plumbing.
- Для GUI/API не включать новый режим неявно: первый rollout должен быть измеряемым и обратимым.
- В benchmark metadata логировать, включён ли task scheduler.
- Автоматически запрещать task-path при `threads <= 1` и при `MonteCarolAlg != NONE`, чтобы не вносить гонки в shared `round_deal`.

P3b. Root parallel region

- В `PCfrSolver::train()` запускать `cfr()` внутри `#pragma omp parallel` + `#pragma omp single` только когда `task_parallelism` активен.
- Сохранить старый путь для обычного режима, чтобы не ломать текущие baseline measurements.
- Такой root scheduler нужен для того, чтобы и action tasks, и chance tasks жили в одном OpenMP team, а не в наборе несвязанных локальных `parallel for`.

P3c. Action branch tasks выше split_round

- В `actionUtility()` добавить task split только для action-узлов, находящихся выше первого `chance`-узла:
  - `root=PREFLOP` -> task split только на preflop action nodes.
  - `root=FLOP` -> task split только на flop action nodes.
  - `root=TURN` -> task split только на turn action nodes.
  - `root=RIVER` -> task split не включать.
- Каждая action-ветка должна:
  - использовать собственный `branch_result` slice;
  - при необходимости строить `new_reach_probs` в thread-local scratch;
  - не писать напрямую в общий `result`.
- После `taskgroup` делать детерминированный merge всех `branch_result` в итоговый `result`, затем считать regrets/EVs как и раньше.

P3d. Chance task path внутри root scheduler

- При активном root parallel region перевести `chanceUtility()` с локального `omp parallel for` на task-based dispatch по `valid_cards`.
- Старый `omp parallel for` оставить как fallback для режима без root scheduler.
- Использовать порог по числу `valid_cards`, чтобы не плодить слишком мелкие задачи.
- Сохранить текущую схему merge через `results_flat`, потому что она уже отделяет вычисление веток от итоговой агрегации.

P3e. Instrumentation и benchmark loop

- Логировать `task_parallelism` в `benchmark_setup` и `solver_session`.
- На matrix benchmark сравнивать минимум такие пары:
  - `task_parallelism=0` vs `1`
  - `threads=16` и `32`
  - `hf=0/1/2`
  - `pinning=none` и `ccd0`
- Основные метрики:
  - `solve_wall_ms`
  - `iteration_total_ms`
  - `player_cfr_ms`
  - `solver_profile.strategy_fetch`
  - `solver_profile.regret_update`
  - `solver_profile.chance_setup`
  - `solver_profile.chance_merge`
  - exploitability на одинаковом iteration budget

Execution Order

1. Включить plumbing-флаг и metadata.
2. Поднять root parallel region под флагом.
3. Перевести `chanceUtility()` на task-path внутри parallel region.
4. Добавить action task split выше `split_round`.
5. Собрать проект и проверить корректность на небольшом solve.
6. Прогнать matrix benchmark и сравнить с baseline `P2`.
7. Только после этого решать, делать ли режим default для CLI или шире.

Verification

Функционально:

- На одинаковом дереве и одинаковом числе итераций сравнить `task_parallelism=0/1` по exploitability.
- Проверить, что dump strategy не ломается и solver доходит до `collecting statics`.
- Проверить, что warmup boundary (`iter == warmup`) работает без regressions при lazy creation/copy trainables.

Производительно:

- Сравнить `solve_wall_ms` и median `iteration_total_ms`.
- Отдельно смотреть `player_cfr_ms[0/1]`, чтобы понять, где scheduler реально разгружает верхнюю рекурсию.
- Проверять, не ухудшились ли `strategy_fetch` и `regret_update`; `P3` не должен сводить на нет выигрыши `P1/P2`.

По устойчивости:

- Прогнать с `threads=1`, `16`, `32`.
- Прогнать с `task_parallelism=0`, чтобы убедиться, что fallback путь остался рабочим.
- Проверить, что scratch allocator не уходит в underflow и не растит аллокации взрывным образом.

Acceptance Criteria

- Новый task scheduler включается и выключается runtime-флагом.
- При `task_parallelism=1` solver корректно проходит полный `train()` и `collecting statics`.
- При `task_parallelism=1` chance parallelism не теряется после ввода root parallel region.
- Action recursion выше `split_round` реально перестаёт быть полностью последовательной.
- По крайней мере на одном из основных benchmark profiles виден воспроизводимый выигрыш относительно `task_parallelism=0`.

Первый merge этой фазы

- Plumbing флага `task_parallelism`.
- Root parallel region под флагом.
- Task-path для `chanceUtility()` внутри parallel region.
- Task split для `actionUtility()` только выше `split_round`.
- Без попытки сразу тюнить grainsize, work-stealing heuristics или менять default для GUI.
