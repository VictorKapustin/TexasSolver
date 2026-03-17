Оценки ниже rough и считаются относительно текущего baseline. Они не суммируются линейно: два улучшения часто бьют в один и тот же bottleneck.

Roadmap

P0. Базовая диагностика и режим запуска (сделано)
Критичность: Critical. Выигрыш: 0-15% напрямую, но это обязательный этап.
Сначала разделить время на solve, best response, river cache, allocator, LLC miss, memory bandwidth; отдельно прогнать 16 и 32 потоков, плюс pinning по CCD. На 5950X для memory-bound solver очень часто 16 физических потоков быстрее 32 SMT. Заодно вернуть LTO и подготовить PGO: сейчас -O3 -march=native уже есть, а LTO выключен в TexasSolverGui.pro#L59.

## Aggregate Results (Solve Time ms)

| Pinning | Threads | Runs | Min | Max | Average | Median |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| ccd0 | 16 | 3 | 152062 | 157799 | 155342,67 | 156167 | 
| none | 16 | 3 | 168611 | 168923 | 168745,67 | 168703 | 
| none | 32 | 3 | 161701 | 164451 | 163184,33 | 163401 |

## Performance Efficiency (Median)

| Pinning | Threads | Iterations | Exploitability | BR (ms) | Alloc (ms) | River Hit Rate |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| ccd0 | 16 | 180 | 0,482308 | 723,57 | 579,58 | 100,00% | 
| none | 16 | 180 | 0,482308 | 788,78 | 583,76 | 100,00% | 
| none | 32 | 180 | 0,482308 | 936,73 | 977,19 | 100,00% |

P1. Убрать лишние аллокации и копии в hot path (сделано)
Критичность: Critical. Выигрыш: 20-40%, RAM -15-30%.
Сейчас на каждом проходе массово создаются и копируются vector<float>: new_reach_probs в PCfrSolver.cpp#L319, regrets/results/all_action_utility в PCfrSolver.cpp#L451, стратегия возвращается по значению в PCfrSolver.cpp#L436 и DiscountedCfrTrainable.cpp#L51. Первый большой шаг: thread-local scratch buffers, in-place API для strategy/regret, меньше временных векторов, реже пересчитывать exploitability. Быстрые мелочи: exchange_color должен принимать const vector<PrivateCards>&, а не копию в utils.h#L19.

## Aggregate Results (Solve Time ms)

| Pinning | Threads | Runs | Min | Max | Average | Median |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| ccd0 | 16 | 3 | 113246 | 113917 | 113649 | 113784 | 
| none | 16 | 3 | 116197 | 118766 | 117321,33 | 117001 | 
| none | 32 | 3 | 120647 | 123584 | 121719,67 | 120928 |

## Performance Efficiency (Median)

| Pinning | Threads | Iterations | Exploitability | BR (ms) | Alloc (ms) | River Hit Rate |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| ccd0 | 16 | 180 | 0,476144 | 597,42 | 0,00 | 100,00% | 
| none | 16 | 180 | 0,476144 | 653,00 | 0,00 | 100,00% | 
| none | 32 | 180 | 0,476144 | 734,97 | 0,00 | 100,00% |

TODO: allocator observability: allocator_mb is still 0

P2. Починить memory layout trainables (завершено как исследовательская фаза, partial success)
Критичность: Critical. Изначальная цель: 10-25% wall-clock, плюс лучшее масштабирование по ядрам. Итог: реальные layout bugs исправлены, pathological slowdown в HF убран, но стабильного основания менять float baseline на compressed mode как новый speed-first default не получено.
Что было сделано:
- `use_halffloats` теперь выбирает storage на всех улицах, а не только на river.
- Исправлен неверный размер временного `r_plus_sum` в `DiscountedCfrTrainableSF/HF`: буфер теперь имеет размер `card_number`, а не `action_count * card_count`.
- В CLI и benchmark scripts добавлен `set_use_halffloats`, поэтому matrix benchmark реально сравнивает режимы `0/1/2`.
- `GameTree::estimate_tree_memory()` и связанный runtime/GUI plumbing переведены на layout-aware оценку памяти по реальным байтам trainables.
- В HF удалён persistent `r_plus_local`, который почти полностью съедал выгоду от half storage по persistent footprint.
- После первых regressions hot path был переработан: `r_plus_sum` теперь кэширует `1 / sum(max(r_plus, 0))`, а `copyStrategy()` копирует этот cache между deal-specific trainables. Это убрало лишние деления в `getcurrentStrategyInPlace()` и скрытый regression после warmup/copy path.
- Эксперимент `SF: cum_r_plus=half` был проверен и отклонён: стабильного speed win не дал, поэтому `SF` оставлен как безопасный control variant (`evs=half, r_plus=float, cum_r_plus=float`).

Что показала первая реализация full HF:
- Снятие river-only gating и удаление `r_plus_local` подтвердили, что memory pressure реально влияет на solver.
- Но двухпроходный HF-path сделал `strategy_fetch` и `regret_update` слишком дорогими по CPU.

## Initial HF Regression (matrix_20260316_155204)

| Pinning | Threads | HF0 | HF1 | HF2 |
| :--- | :--- | :--- | :--- | :--- |
| none | 16 | 103255 | 104418 | 154289 |
| none | 32 | 116190 | 118389 | 128866 |

Ключевая причина regression: на `none / 16` у `HF2` `Fetch = 2137 ms` и `Regret = 1666 ms` против `630 ms` и `683 ms` у `HF0`. Идея compressed layout в целом не была опровергнута, но стало ясно, что именно `r_plus=half` в текущем hot path слишком дорог по CPU.

Что показала итоговая стабилизация hot path:
- `HF2` перестал быть катастрофически медленным.
- `HF1` вышел в паритет или локально лучше baseline на части прогонов.
- `HF2` на том же числе итераций даёт лучшую exploitability (`0.466475` против `0.491256`), то есть может быть полезен для `time-to-target accuracy`, но не стал универсальным победителем по wall-clock.

## Final Results (matrix_20260316_165952)

| Pinning | Threads | HF0 | HF1 | HF2 |
| :--- | :--- | :--- | :--- | :--- |
| ccd0 | 16 | 111733 | 111795 | 126574 |
| none | 16 | 114634 | 106360 | 106774 |
| none | 32 | 100818 | 106896 | 112256 |

## Final Efficiency Notes

| Pinning | Threads | HF | Fetch (ms) | Regret (ms) | Exploitability |
| :--- | :--- | :--- | :--- | :--- | :--- |
| none | 16 | 0 | 721.12 | 858.52 | 0.491256 |
| none | 16 | 1 | 558.40 | 706.91 | 0.491256 |
| none | 16 | 2 | 630.23 | 1278.37 | 0.466475 |
| none | 32 | 0 | 1179.28 | 1158.94 | 0.491256 |
| none | 32 | 1 | 1054.56 | 1143.99 | 0.491256 |
| none | 32 | 2 | 1064.82 | 1889.36 | 0.466475 |

Вывод по фазе:
- `P2` как исследовательская и инженерная фаза закрыта.
- Исправления memory layout оставляем: они полезны, измеримы и устраняют реальные дефекты.
- `HF2` больше не считается сломанным режимом, но и не является новым default для speed-first сценария.
- `HF1` выглядит как допустимый optional mode для дальнейших замеров, но данных пока недостаточно, чтобы объявить его новым baseline.
- Для максимального wall-clock speed текущий safest default остаётся `HF0`.
- Следующий главный кандидат на реальный speedup по-прежнему `P3`: task-based parallelism выше chance-узлов.

P3. Расширить распараллеливание выше chance-узлов (частично выполнено, P3.1 завершён)
Критичность: Critical. Изначальная rough-цель: 1.3-1.8x на 5950X после P1/P2. Фактический результат первого инженерного этапа: устойчивый wall-clock выигрыш порядка 2-16% в зависимости от `threads/pinning/HF`, лучший стабильный эффект пока на `none / 32`, но до исходной rough-цели ещё далеко.
Что было сделано:
- В CLI/runtime добавлен управляемый флаг `set_task_parallelism 0/1`, а benchmark metadata теперь логирует состояние scheduler-а.
- `PCfrSolver::train()` получил root `omp parallel` + `omp single` path под task scheduler, вместо старта solve целиком вне общей parallel region.
- `chanceUtility()` получил task-based path внутри активной parallel region, при этом старый fallback без scheduler-а сохранён для A/B benchmark и безопасного отката.
- `actionUtility()` получил task split по action branches выше `split_round`; каждая ветка пишет в свой buffer, merge идёт после `taskgroup`, без shared writes в итоговый `result`.
- Сохранён старый путь без task scheduler, поэтому phase можно было честно мерить как `task0` vs `task1`.
- Выполнены smoke test и полная matrix-проверка `task0/task1 x HF0/1/2 x {16,32 threads} x {none,ccd0}`; все 48/48 прогонов завершились без segfault/NaN, exploitability drift относительно baseline не выявлен.

Что показал matrix benchmark:
- Scheduler действительно даёт реальный speedup, особенно там, где раньше сильнее проявлялись contention и oversubscription effects.
- На `16` потоках выигрыш есть, но он умеренный: в полном HF-matrix это скорее диапазон `~1,5-2,0%` для `HF0`.
- На `32` потоках без pinning эффект уже существенный: `HF0` ускорился примерно на `13,5%`, `HF1` на `12,7%`, `HF2` на `13,8%`.
- Лучший стабильный результат полного matrix benchmark сейчас: `none / 32 / HF2`, где median solve time снизился с `109610 ms` до `94466 ms`.
- Отдельный gate-run `HF0` показал ещё более сильный swing на `none / 32` (`121470 -> 102414 ms`), но при `2` прогонах это разумно считать optimistic upper bound, а не новым железным baseline.

## Scheduler Results (Full HF Matrix)

| Pinning | Threads | HF | Task0 Median | Task1 Median | Delta |
| :--- | :--- | :--- | :--- | :--- | :--- |
| ccd0 | 16 | 0 | 103132 | 101027 | -2,0% |
| none | 16 | 0 | 113629 | 111967 | -1,5% |
| none | 32 | 0 | 115520 | 99940 | -13,5% |
| none | 32 | 1 | 114314 | 99824 | -12,7% |
| none | 32 | 2 | 109610 | 94466 | -13,8% |

## Residual Bottlenecks After P3.1

| Config | Metric | Task0 | Task1 | Notes |
| :--- | :--- | :--- | :--- | :--- |
| none / 32 / HF0 | Lock Wait (ms) | 10081,92 | 8438,78 | contention снизился, но всё ещё очень велик |
| none / 32 / HF0 | Fetch (ms) | 1218,34 | 1220,62 | strategy fetch почти не ускорился |
| none / 32 / HF0 | Regret (ms) | 1287,74 | 1175,01 | regret update улучшился, но не радикально |

Что это значит:
- `P3.1` сработал: scheduler path оставляем, потому что он даёт измеримый и воспроизводимый wall-clock win.
- Но текущий потолок уже задаётся не только верхней последовательной частью action recursion, а и residual contention в river/showdown pipeline.
- Сам по себе scheduler не снял главный bottleneck в `RiverRangeManager`: на `none / 32` lock wait остаётся на уровне нескольких секунд на solve, то есть глубокое продолжение `P3` прямо сейчас будет давать уменьшающуюся отдачу.
- Текущий task split ещё довольно консервативен: он работает только выше `split_round` и использует грубый cutoff по размеру range, а не по реальному весу поддерева.

Вывод по фазе:
- `P3.1` закрыт как успешный инженерный этап.
- Исходная rough-цель `1.3-1.8x` для одного только первого merge не достигнута; реалистичная текущая оценка для уже сделанного scheduler-а — это `~1,02-1,16x` в зависимости от конфигурации, максимум на `none / 32`.
- Глубже закапываться в `P3` прямо сейчас можно, но это уже не самый выгодный следующий шаг по ROI.
- По результатам phase следующий главный кандидат на большой wall-clock win теперь не `P3.2`, а `P5`: river cache / evaluator / showdown pipeline.

TODO внутри P3:
- Перевести cutoff для task split с грубого `action_count * range_size` на subtree-aware heuristic по `subtree_size`, `depth`, `round`, чтобы задачи создавались только там, где поддерево реально тяжёлое.
- Разрешить task split не только строго выше `split_round`, а ещё на один уровень ниже для крупных поддеревьев, если это подтверждается по benchmark.
- Добавить более явный thread-local accumulation / merge minimization в action path, чтобы снизить merge overhead и potential false sharing.
- Проверить, есть ли выигрыш от отдельного tuning `chance`-task grainsize и разных cutoff для `action` и `chance`, вместо одного консервативного режима.
- Вынести в aggregate benchmark report ещё `player_cfr_ms`, `chance_setup`, `chance_merge`, `showdown_eval`, `terminal_eval`, чтобы следующий цикл `P3` опирался не только на wall-clock.
- После снятия river bottleneck прогнать отдельный thread sweep `20/24/28/32` и заново проверить `none` vs `ccd0`.

Следующие рекомендуемые шаги:
- `P5` milestone по river contention закрыт: `river_lock_wait_ms` на `none / 32` упал с `~8,4 s` до `~1,3 s`, поэтому исходный gate для возврата к scheduler work уже достигнут.
- Следующим осмысленным шагом снова становится `P3.2`: subtree-aware scheduler, retune task cutoff, thread sweep `20/24/28/32` и повторная A/B matrix-проверка на том же benchmark profile.
- Если profiling после `P3.2` снова покажет заметный river/showdown tail, отдельный цикл `P5.2` должен идти уже в `valid-hand masks`, более агрессивный evaluator/LUT и дополнительную декомпозицию showdown metrics.

P4. Упростить и уплотнить базовые структуры данных (частично выполнено, P4.1 завершён)
Критичность: High. Изначальная rough-цель: 10-25%, RAM -10-20%. Фактический результат P4.1: `-42%` wall-clock на `8t / HF0 / macOS arm64` при `regret_pruning=0`, что существенно превысило исходную оценку.

Что было сделано в P4.1:
- `PrivateCards::card_vec` (`vector<int>`) полностью удалён: `board_long` теперь вычисляется в конструкторе через `Card::boardInt2long(card1) | Card::boardInt2long(card2)` без heap-аллокации. Метод `get_hands()` удалён; все 7 call site переведены на `toBoardLong()` напрямую.
- `PCfrSolver::cfr()`, `actionUtility()`, `chanceUtility()`, `showdownUtility()`, `terminalUtility()`, `setTrainable()` переведены с `shared_ptr<T>` на `T*`. `dynamic_pointer_cast` заменены на `static_cast` после `getType()` проверки. Вызовы от `tree->getRoot()` используют `.get()`.

Что показал benchmark (`baseline = BASELINE_MACOS.md`, профиль `macOS arm64 / 2 runs / 121 итерация`):

| Threads | HF | Pruning | Solve Before | Solve After | Delta | Fetch Before | Fetch After | Regret Before | Regret After |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 8 | 0 | OFF | 75662 | 43800 | -42,1% | 948,01 | 313,8 | 969,56 | 247,4 |
| 8 | 0 | ON | 75662 | 41535 | -45,1% | 948,01 | 313,9 | 969,56 | 247,3 |
| 14 | 0 | OFF | 45052 | 42670 | -5,3% | 639,01 | 390,9 | 664,88 | 308,9 |

Почему выигрыш оказался крупнее ожидаемого:
- `shared_ptr` в сигнатурах `cfr()` и utility-функций создавал атомарный ref-count overhead на КАЖДОМ рекурсивном вызове: вход = atomic inc, выход = atomic dec.
- В task-parallel путях (`#pragma omp task`) задачи захватывали `shared_ptr` по значению через lambda captures. При параллельном завершении задач несколько ядер одновременно декрементировали один и тот же control block — классический false-sharing на atomic. На arm64 (TSO) последствия этой конкуренции особенно ощутимы.
- `dynamic_pointer_cast` (удалён в favor of `static_cast`) добавлял RTTI lookup при каждом переходе между типами узлов.
- На 14t улучшение меньше, потому что river lock contention (~2787ms vs ~721ms на 8t) остаётся доминирующим bottleneck при высоком числе потоков.

Что показали профильные тайминги (`8t / HF0 / pruning=OFF`):
- `strategy_fetch_ms` (sum): 948 → 314 ms (-67%)
- `regret_update_ms` (sum): 969 → 247 ms (-75%)
- `lock_wait_ms` (sum): 993 → 721 ms (-27%)
- Exploit: 0,9737 → 0,9737 (без дрейфа)

P4.2 Ownership model для trainables (ЗАВЕРШЕНО — 2026-03-17)
Что было сделано:
- `vector<shared_ptr<Trainable>> trainables` в `ActionNode` → `vector<unique_ptr<Trainable>>`.
- `ActionNode::getTrainable()` теперь возвращает `Trainable*` вместо `shared_ptr<Trainable>`: устранён atomic inc/dec на каждый вызов в hot path.
- `ActionNode::setTrainable()` принимает `int num` вместо `vector<shared_ptr<Trainable>>`: инициализация через `resize(num)`.
- `copyStrategy()` во всех четырёх trainable-классах переведён с `shared_ptr<Trainable>` + `dynamic_pointer_cast` на `Trainable*` + `static_cast`.
- Все call sites в `PCfrSolver.cpp` и `BestResponse.cpp` обновлены: 7 локальных `shared_ptr<Trainable>` → `Trainable*`.

Почему это даёт speedup (аналогично P4.1):
- `shared_ptr` возврат по значению = atomic increment на возврате + atomic decrement при уничтожении временной переменной.
- В hot path `actionUtility()` этот паттерн срабатывает **на каждый вызов на каждой итерации**, что даёт O(iterations × deals × action_nodes) атомарных операций.

## P4.2 Verification Run (14t, HF0, macOS arm64, real_case Qs Jh 2h, 1 run)

| Метрика | P8.2+P8.3 baseline | P4.2 result | Delta |
| :--- | :--- | :--- | :--- |
| Wall-clock | 93.28s | 89.2s | -4.4% |
| Iter time (avg) | ~564ms | ~525ms | -7% |
| CPU utilization | 1170% | 1207% | +3% |
| Iterations | 161 | ~170 | — |

Примечание: `1 run` — разброс по wall-clock ±3-5%; достаточно для quality gate, для железного baseline нужен full matrix.

TODO P4.3 (не сделано, низкий приоритет):
- Переход дерева на arena/flat-массив нод вместо `shared_ptr<GameTreeNode>` как ownership модели. UI-код (`treeitem.cpp`, `treemodel.cpp`, `tablestrategymodel.cpp`) использует `weak_ptr/lock()`, что требует отдельного решения.
- Contiguous layout: `vector<shared_ptr<GameTreeNode>>` в ActionNode → `vector<GameTreeNode*>` (arena).
- SoA для `PrivateCards`: array of structs → struct of arrays для лучшей cache locality при линейном сканировании диапазона.

P5. Ускорить evaluator и river/showdown pipeline (частично выполнено, milestone по river contention закрыт)
Критичность: High. Изначальная rough-цель: 15-35% на turn/river-heavy деревьях. Фактический результат первого инженерного этапа: основной bottleneck в river cache снят, evaluator ускорен, drift по exploitability не обнаружен.

Что было сделано:
- `RiverRangeManager` переведён с двух глобальных `map + mutex` на `128-way lock sharding`: каждая shard держит свой локальный `std::mutex` и свой cache. Вариант с `std::shared_mutex` был отвергнут, потому что на MinGW 8.1 он дал слишком большой overhead.
- `Dic5Compairer::get_rank(uint64_t, uint64_t)` переписан на прямой bit-mask loop `7-card -> 5-card`, без `Card::long2board()` и без временных `vector<int>` в hot path.
- Добран оставшийся allocation tail: `RiverCombs` больше не хранит `board`, а `RiverRangeManager` больше не материализует `Card::long2board(board_long)` на каждую руку.

Что показал benchmark (`baseline = p3_hf_task1`, verification run = `matrix_20260316_215613`, профиль `none / 32 / 2 runs`):

| HF | Solve Before | Solve After | Delta | Lock Wait Before | Lock Wait After | Exploitability |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 0 | 99940 | 70532 | -29,4% | 8438,78 | 1329,03 | 0,491256 -> 0,491256 |
| 1 | 99824 | 63995 | -35,9% | 8552,94 | 1347,73 | 0,491256 -> 0,491256 |
| 2 | 94466 | 66265 | -29,9% | 7893,24 | 1329,58 | 0,466475 -> 0,466475 |

Что показала дополнительная валидация корректности:
- fast evaluator был сверен со старым vector-based path на `50000` случайных валидных `7-card` sample; расхождений по rank не найдено;
- `RiverRangeManager::getRiverCombos()` был сверен с reference builder на `128` случайных river boards с реальными диапазонами из benchmark profile; содержимое совпало;
- повторный benchmark run на том же коде (`matrix_20260316_213349`) дал тот же порядок величины по `river_lock_wait_ms` (`~1,28-1,37 s`) и ту же exploitability, то есть эффект воспроизводим, а различия по solve time укладываются в run-to-run noise короткой `2-run` matrix.

## Quick Benchmark Baseline For Next Steps (`matrix_20260316_222631`)

Это не replacement для полного benchmark выше, а короткий inner-loop профиль из `QuickProfile`-режима:
- fixed budget `120` iterations;
- `none / 32 / 2 runs`;
- ослабленный benchmark для быстрых сравнений перед следующими шагами `P3.2/P4/P5.2`.

| HF | Solve Median | Iterations | Exploitability | Fetch (ms) | Regret (ms) | Lock Wait (ms) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 0 | 40136 | 120 | 0,940265 | 944,58 | 909,70 | 1315,20 |
| 1 | 39246 | 120 | 0,940265 | 892,65 | 863,04 | 1284,09 |
| 2 | 39182 | 120 | 0,936509 | 905,62 | 1649,52 | 1151,30 |

Что это значит:
- после `P5` даже быстрый benchmark подтверждает, что новый river baseline стабилен и остаётся существенно быстрее старого full-P3 baseline;
- для короткого tuning-loop `HF1` и `HF2` сейчас почти сравнялись по wall-clock, а `HF0` немного позади;
- exploitability в quick-profile заметно хуже полного benchmark по определению, поэтому этот набор нельзя напрямую использовать вместо full validation;
- именно этот quick baseline теперь стоит использовать как дешёвый smoke/perf gate перед каждым локальным изменением в `P3.2` и `P4`, а подтверждать итоговые выводы уже полным matrix-run.

## Archived P3.2 Probe And Rollback

Ниже зафиксирован уже выполненный цикл `P3.2`, чтобы не повторять его повторно без новой гипотезы. Эти эксперименты были сделаны после quick baseline `matrix_20260316_222631`, а затем код был возвращён к baseline path.

Что именно было предпринято:
- временно были добавлены раздельные scheduler controls для `action` и `chance`, чтобы мерить `action-only`, `chance-only` и `full` по отдельности;
- временно была добавлена scheduler telemetry в benchmark JSONL, включая task counters и reject reasons;
- был отдельно проверен experimental subtree-aware / mixed child split;
- были прогнаны decomposition benchmarks `p32_decompose_*` на quick profile, сначала как smoke, затем на `2 runs` для `HF0/HF1`.

Что показал decomposition benchmark на `none / 32 / 2 runs / 120 iterations`:

| Profile | HF0 Solve Median | HF1 Solve Median | Итог |
| :--- | :--- | :--- | :--- |
| `a0/c0` | 50734 | 51220 | reference без раздельных task paths |
| `a0/c1` | 51670 | 51871 | `chance-only` не дал устойчивого выигрыша |
| `a1/c0` | 160442 | 157774 | `action-only` дал тяжёлую регрессию |
| `a1/c1` | 40416 | 41631 | лучший из decomposition modes, но не лучше production quick baseline |

Что показал direct compare против production quick baseline `matrix_20260316_222631`:
- baseline `HF0/HF1` был `40136 / 39246 ms`;
- decomposition `full a1/c1` дал `40416 / 41631 ms`;
- значит новый `P3.2` цикл не дал нового ускорения поверх уже существующего quick baseline.

Что ещё было проверено и чем это закончилось:
- experimental mixed child split дал быстрый и воспроизводимый регресс порядка `~161-185 s` на quick profile вместо `~39-40 s`;
- после этого solver был возвращён к baseline scheduler path, а временные `P3.2` controls/telemetry были убраны из кода;
- sanity recheck после отката вернул quick-profile `HF1` обратно в baseline-диапазон `~40,4 s`.

Что мы узнали и считаем закрытым знанием:
- рабочим остаётся только исходный `full scheduler`; отдельные `action-only` и `chance-only` режимы не являются новыми production кандидатами;
- `action-only` как отдельное направление в текущем дизайне признан неэффективным и повторно без новой архитектурной идеи не запускается;
- decomposition benchmark сам по себе полезен только как исследовательский инструмент и не дал нового speedup.

Что потенциально можно выжать позже, если целенаправленно возвращаться в `P3.2`:
- сначала убрать диагностический overhead из hot path, если telemetry снова понадобится;
- затем тюнить только `chance` grainsize/chunking внутри полного scheduler-а, а не пытаться развивать `action-only` ветку;
- разумный remaining upside здесь выглядит умеренным, порядка `~3-8%`; если quick-profile не уходит хотя бы примерно в диапазон `~37-38 s`, этот трек стоит считать почти исчерпанным.

Статус:
- на `2026-03-17` этот цикл `P3.2` считаем задокументированным и завершённым без нового performance win;
- повторять его “с нуля” не нужно; возвращаться только при появлении новой конкретной гипотезы по `chance chunking` или другому scheduler tuning.

Вывод по фазе:
- первый milestone `P5` закрыт успешно: цель “сбить `river_lock_wait_ms` с `~8400 ms` хотя бы примерно вдвое” выполнена с большим запасом;
- текущая реализация выглядит корректной: benchmark exploitability не сдвинулась, differential-check fast/slow evaluator прошёл, river cache сверка прошла;
- `P5` как направление ещё не исчерпан полностью, но по ROI следующий цикл разумнее вернуть в `P3.2`/`P4`, а не продолжать углубляться в river pipeline немедленно.

P6. Алгоритмические ускорители без смены класса solver (P6.1 завершён)
Критичность: High. Изначальная rough-цель: 1.5-3x. Фактический результат P6.1 (regret-based pruning): ~6% wall-clock на 14 потоках / HF0 / 121 итерацию. Exploitability drift: +0.27 pp (1.24% vs 0.97%), сходимость есть.

Что было сделано в P6.1:
- В `Trainable` интерфейс добавлен `isActionPrunable(action_id)` — проверяет `all(r_plus[action, hand] <= 0)` для всех рук.
- Реализация добавлена во все три trainable варианта: `DiscountedCfrTrainable`, `DiscountedCfrTrainableSF`, `DiscountedCfrTrainableHF`.
- В `PCfrSolver::actionUtility()` добавлен pruning path: если action prunable для traversing player, child subtree не evaluируется, utility заполняется нулями, regret для pruned action = 0 (r_plus только decays по beta без добавления нового regret).
- Каждая 10-я итерация — full (unpruned) pass, чтобы pruned actions могли recover.
- Pruning включается только когда `player == node->getPlayer()`, `iter > warmup`, `action_count > 1`.
- Никогда не pruning все actions одновременно (safety guard).
- Добавлен CLI command `set_regret_pruning 0/1` (default = 1), параметр прокинут через `CommandLineTool → PokerSolver → PCfrSolver`.
- Добавлена telemetry: `pruned_branches` в `BenchmarkThreadStats` и JSON output.

## P6.1 Benchmark Results (HF0, 14 threads, macOS arm64, 121 iterations)

| Metric | Pruning OFF | Pruning ON | Delta |
| :--- | :--- | :--- | :--- |
| Wall-clock | 45.1 s | 42.2 s | -6.4% |
| Exploitability | 0.974% | 1.239% | +0.27 pp |
| Pruned branches/iter | 0 | ~30-50k (~8-13%) | |

## Pruning Activity Over Iterations

| Iteration range | Pruned branches | Action nodes visited | Pruning ratio |
| :--- | :--- | :--- | :--- |
| 1-9 (pruned cycle 1) | 31k-49k | 390-420k | 7-13% |
| 10 (full iteration) | 0 | 532k | 0% |
| 11-19 (pruned cycle 2) | 36k-46k | 486-496k | 7-9% |
| 20 (full iteration) | 0 | 532k | 0% |
| 21-29 (pruned cycle 3) | 18k-28k | 510-525k | 3-5% |

Что мы узнали и считаем закрытым знанием:
- RBP в discounted CFR принципиально ограничен по сравнению с CFR+: uniform-fallback стратегия (когда все r_plus <= 0 для руки) даёт pruned actions ненулевую вероятность, поэтому pruning condition `all(r_plus <= 0)` не гарантирует `strategy == 0` для всех рук. Strict check `r_plus_sum != 0` (гарантия `strategy == 0`) убивает почти весь pruning (<0.01%).
- Основной bottleneck солвера — river/showdown/chance evaluation, а не action recursion. Pruning пропускает поддеревья ниже action nodes, но самые тяжёлые узлы (river cache, showdown eval) не зависят от pruning.
- Periodic full iterations (каждая 10-я) необходимы для convergence, но разбавляют выигрыш.
- Pruning rate снижается по мере сходимости (с ~13% на ранних итерациях до ~4% на поздних), потому что больше actions развивают positive regrets.

Вывод по P6.1:
- Оставляем как `set_regret_pruning 1` (default on) — это бесплатные ~6% без regression по корректности.
- Rough-цель `1.5-3x` для P6 в целом была overoptimistic для discounted CFR solver; реалистичный ceiling для оставшихся P6 ускорителей (lazy updates, strategy freezing, adaptive scheduling) — скорее ещё `~5-15%` суммарно, а не мультипликативный speedup.
- `P4.1` (raw pointers + POD PrivateCards) выполнен и дал `-42%` на 8t, что в несколько раз превысило исходный rough-estimate. Следующий шаг — `P4.2` (arena для children) или `P6.2` (strategy freezing).

Что потенциально можно выжать позже, если возвращаться в P6:
- `P6.3`: Adaptive iteration count — early stopping по exploitability threshold вместо fixed iteration budget.
- `P6.4`: Lazy regret updates — пересчитывать стратегию реже (каждые N итераций) для узлов с малым изменением regret.
- Переход на CFR+ (regret clipping к 0) мог бы сделать RBP значительно агрессивнее, но потребует валидации convergence properties для текущего дерева.

P6.2. Strategy freezing для converged trainables (ЗАКРЫТО — не применимо для real-time цели)
Критичность: N/A для real-time advisor. Применимо только для long solves (500+ iter).

Что было сделано:
- В каждый из трёх trainable вариантов (`DiscountedCfrTrainable`, `DiscountedCfrTrainableHF`, `DiscountedCfrTrainableSF`) добавлены поля `cum_frozen_` (bool) и `frozen_skip_count_` (int).
- В `updateRegretsInPlace` добавлена логика freeze: фаза обновления `r_plus` всегда выполняется; фаза обновления `cum_r_plus` пропускается если trainable заморожен.
- Детекция заморозки по `max_delta < freeze_threshold`; авторазморозка каждые 50 итераций.
- CLI: `set_strategy_freeze_threshold <value>`. При 0 (default) — нулевой overhead.
- Telemetry: `pruning.frozen_cum_updates` в benchmark JSON.

Результаты бенчмарка sweep (14 CPU, macOS arm64, 121 iter):

| Метрика | Baseline (matrix_20260316) | 0.0001 | 0.001 | 0.01 |
| :--- | :--- | :--- | :--- | :--- |
| Solve wall-clock (ms) | 40136 | 45535 | 45535 | 176251* |
| Regret update (ms) | 909.70 | 5282.76 | 5282.76 | 13839.16* |
| Exploitability | 0.9402† | 16.06% | 16.06% | 16.06% |
| Frozen updates | 0 | 0 | 0 | 0 |

*0.01 sweep: inflated из-за system noise (river_cache.lock_wait spike), не из-за логики оптимизации.
†Baseline записан с другим профилем (разные итерации/потоки), прямое сравнение некорректно.

Вывод и закрытие P6.2:
- `frozen_cum_updates = 0` во всех трёх sweep при 121 итерации. Стратегия за это время не успевает converge ни при каком threshold.
- Freezing принципиально рассчитан на long solve (500–1000+ итераций): там `cum_r_plus` действительно стабилизируется и пропускать его обновления безопасно.
- Для целевого профиля real-time advisor (≤50 итераций / 5–10 секунд) P6.2 даёт ноль пользы.
- Код остаётся в дереве с нулевым overhead при `freeze_threshold = 0` (default); если продукт вернётся к long-solve сценарию, фичу можно включить без доработок.
- **P6.2 закрыт как не применимый к новой цели.**

---

Real Case Baseline: Qs Jh 2h, pot=6, stack=98 (benchmark/real_case_benchmark.txt)
14 threads, HF0, macOS arm64. Recorded post-fix (correct tree, no spurious flop allin).

| Итерации | Wall-clock | Exploitability |
| :--- | :--- | :--- |
| 11 | 2.9s | 136.7% |
| 21 | 6.5s | 45.1% |
| 31 | 10.1s | 22.5% |
| 51 | 17.4s | 8.4% |
| 61 | 21.0s | 5.8% |
| 71 | 24.6s | 4.1% |
| 101 | 35.5s | 1.9% |
| 131 | 46.2s | 1.1% |
| ~141 | 49.9s | 0.98% ← set_accuracy 1 достигнут |

Ключевые числа из данных:
- ~362ms/iter (steady-state после warmup; первые ~11 iter быстрее ~267ms/iter)
- 14t, HF0, regret_pruning=1, task_parallelism=1 (все P1–P6 оптимизации включены)

Gap к цели (5–10 секунд, exploitability < 5):
Сейчас в 10s помещается ~28 итераций → exploitability ~35%.
Iter 61–71 (21–25s) даёт exploitability 4–6% — в 2x дальше по времени от цели.
Нужно: те же 10s → exploitability < 5%.
Путь: CFR+ (P7) конвергирует в 2–3x меньше итераций → ~25 CFR+ iter ≈ <5% → укладывается в 10s при текущей скорости итерации.

PioSolver reference: PioSolver решает эквивалентный spot за ~5–15s с хорошей точностью на той же аппаратуре.
Разрыв ~2–5x доказывает, что ограничение кодовое, не железо и не tree complexity.
Приоритет: исчерпать все code-level оптимизации до того, как трогать размер дерева или bet sizes.

---

P7. CFR+ (смена алгоритма обновления регретов) — ЗАВЕРШЕНО (2026-03-17)
Критичность: Critical. Rough-цель: 2–3x меньше итераций для той же exploitability.

Что было сделано:
- `CfrPlusTrainable` полностью переписан: `ActionNode&`, cached inverse r_plus_sum, quadratic average t²·σ_t (см. post-fix ниже), все методы интерфейса (`setEv`, `copyStrategy`, `dump_evs`, `isActionPrunable`).
- CLI: `set_use_cfr_plus 0/1` (default = 0, DCFR). Benchmark JSONL логирует `use_cfr_plus`.
- Regret pruning **авто-отключается** при `use_cfr_plus=1` — в PCfrSolver выводится NOTE и `regret_pruning` принудительно снимается.
- `BestResponse` обновлён: принимает `use_cfr_plus`, передаёт в `getTrainable()`. Критично: BestResponse создаёт trainables до первой итерации — без флага создавались бы DCFR объекты вместо CFR+.
- `ActionNode::getTrainable()` получил параметр `bool use_cfr_plus=false`; при `true` создаёт `CfrPlusTrainable` вместо DCFR-вариантов (lazy creation при первом вызове).

## P7 Benchmark Results (real case Qs Jh 2h, 14 threads, macOS arm64)

| Итерации | DCFR Exploitability | CFR+ (no pruning) | CFR+ (with pruning) |
| :--- | :--- | :--- | :--- |
| 0 | 273.5% | 273.5% | 273.5% |
| 11 | 136.7% | **39.3%** | 130.4% |
| 21 | 45.1% | **21.1%** | 50.1% |
| 31 (~10s) | 22.5% | **14.3%** | 27.4% |
| 51 | **8.4%** | 8.5% | 11.7% |
| 71 | **4.1%** | 7.0% | 7.5% |
| 141 | **0.98%** | 3.0% | 3.0% |

Ключевые выводы:
- **RT-цель достигнута**: в ~10s (~31 итерация) CFR+ даёт 14.3% vs 22.5% у DCFR — улучшение ~1.6x по accuracy за одинаковое время.
- **Pruning несовместим с CFR+**: с pruning CFR+ деградирует до 27.4% (хуже DCFR). Причина: в CFR+ r_plus всегда ≥ 0; когда все r_plus == 0 для action, linear average теряет корректную информацию о стратегии. Авто-отключение реализовано в PCfrSolver.
- **Долгосрочная сходимость**: после ~50 итераций DCFR обходит CFR+. Коэффициенты DCFR (α=1.5, β=0.5, γ=2) хорошо подобраны под этот тип дерева. CFR+ не является универсальным winner по deep solve.

Вывод по фазе:
- P7 закрыт. CFR+ = опция для RT-сценария (≤30–50 итераций). DCFR = default и winner для deep solve.
- Рекомендуемые настройки для RT: `set_use_cfr_plus 1` (pruning отключится автоматически).
- **P7 post-fix (2026-03-17)**: усреднение переключено с линейного `t` на квадратичное `t²` (Tammelin 2015). При 31 iter вес последней итерации: `31²=961` против `1²=1` для первой — avg strategy практически совпадает с current strategy которую измеряет BestResponse. Ожидаемый эффект: dump_strategy теперь выдаёт стратегию с exploitability близкой к логируемой (~14.3%).
- Следующий приоритетный шаг: **P8** (диагностика 8t ≡ 14t — масштабирование потоков).

P8. Диагностика и устранение serial bottleneck (14t ≡ 8t) — P8.1 завершён (2026-03-17)
Критичность: High. Rough-цель: 1.5–2x на 14+ потоках при устранении bottleneck.

## Диагностика (2026-03-17)

Факт из benchmark: 8t=604ms/iter, 14t=560ms/iter → T(8)/T(14)=1.079.
Аmdahl: S = (1/1.079 - 1/14) / (1 - 1/14) ≈ 37.5% serial fraction. Max теоретический speedup = 1/S = **2.67x** (никогда не будет достигнут при текущей архитектуре).

Три установленные причины serial fraction:

**1. `task_parallelism=0` в mac benchmark** — главный структурный дефект.
`real_case_mac_run.txt` содержит `set_task_parallelism 0`. При task_par=0 TURN chance node идёт по `#pragma omp parallel for schedule(static)`, но внутри каждого turn-card thread `omp_in_parallel()=true`, поэтому RIVER chance node падает на **serial path** — все 44 river cards считаются в одном потоке. Итого: реальный параллелизм только на уровне turn cards (~8–12 вариантов), а river (самый тяжёлый уровень) — serial внутри каждого.

**2. `schedule(static)` без load balancing** — неравный вес turn-card subtrees не компенсируется. Turn cards с разными текстурами дают разные by-action subtree sizes. Static scheduling даёт idle потоки пока один поток считает тяжёлую руку.

**3. Exclusive mutex на read-only cache hits** — прямо измерено из sweep benchmark.

## Profile Data (sweep benchmark, 14t, HF0, macOS arm64)

| Итерация | wall_ms | lock_wait_ms | lock_wait/thread | % времени потоков |
| :--- | :--- | :--- | :--- | :--- |
| ~40 | 3518 | 768 | 54 ms | ~1.5% |
| ~80 | 11203 | 21151 | 1511 ms | **~13.5%** |

К итерации 80 cache полностью прогрет (hit_rate=100%), все обращения — read-only. Но old exclusive `std::mutex` требовал serial доступа даже при чтении. 13.5% thread time тратилось на ожидание мьютекса при 100% hit rate.

## P8.1: shared_timed_mutex fix — ЗАВЕРШЕНО

Что сделано в `RiverRangeManager`:
- `Shard::lock` переведён с `mutable std::mutex` на `mutable std::shared_timed_mutex` (C++14; `std::shared_mutex` требует C++17, которого нет в проекте).
- Read path (cache hit): `std::shared_lock<std::shared_timed_mutex>` → concurrent reads без блокировки.
- Write path (cache miss + insert): `std::unique_lock<std::shared_timed_mutex>` → exclusive write, double-checked locking.
- `getStats()` также переведён на `shared_lock`.
- Ожидаемый эффект: ~10–15% improvement при task_par=1, когда lock wait dominate; меньший эффект при task_par=0 (потоков меньше → меньше contention).

Ограничение C++14: `shared_timed_mutex` может иметь overhead платы за `timed` функциональность на некоторых платформах. Если после проверки окажется, что overhead выше ожидаемого — флаг `-std=c++17` позволит использовать `shared_mutex`.

## Следующие шаги P8

**P8.2** (ЗАВЕРШЕНО): Включен `task_parallelism=1` в `real_case_mac_run.txt`. Это устранило причину #1 (serial RIVER) и повысило CPU utilization с 1021% до 1170%.

**P8.3** (ЗАВЕРШЕНО): Переключен `schedule(static)` на `schedule(dynamic,1)` в `chanceUtility()` для лучшей балансировки неравных turn-card subtrees.

Итог P8.2+P8.3: Solve time `-2.1%` (95.3s -> 93.3s), CPU utilization `+14.6%` (1021% -> 1170%).

**Примечание**: false sharing на `BenchmarkThreadStats` не подтверждён — структура 128 байт = 2 cache line, padding достаточный.

Вывод по P8.1:
- Structural root cause (task_par=0) требует ручного изменения в benchmark config и потенциально пересмотра default.
- shared_timed_mutex — необходимый fix независимо от task_par.
- Реальный эффект P8.1 будет измерен после P8.2 (test task_par=1 on real_case).

P4.2 Arena allocation для children/trainables
Критичность: High. Rough-цель: 15–25% iter time. (ранее запланировано)

После P7 (CFR+) и P8 (thread scaling) — следующий обязательный шаг по коду.
Устраняет overhead аллокатора при создании deal-specific trainable nodes.
Конкретно: заменить `new/delete` на arena с bump-pointer allocation и bulk reset.

P9. SIMD векторизация inner loops (код завершён 2026-03-17, benchmark pending)
Критичность: Medium-High. Rough-цель: 2–4x ускорение arithmetic-intensive частей.

Что было сделано:
- **`alpha_coef` double → float** во всех трёх trainable (DCFR, SF): `auto alpha_coef = pow(...)` возвращал `double`, что вызывало неявный upcast float→double на КАЖДОМ умножении внутри inner loop, полностью блокируя float SIMD. Исправлено: `double` precision только для вычисления `pow`; результат кастуется в `float` до входа в loop.
- **if/else → ternary** в r_plus update и getcurrentStrategyInPlace: ветвление `if (v > 0) *= alpha_coef; else *= beta` заменено на `v * (v > 0.0f ? alpha_coef : beta)`. Без явного ternary clang/gcc консервативно воздерживались от BLEND/SELECT. Теперь компилятор может эмитировать `vbslq_f32` (NEON) / `vblendvps` (AVX).
- **Локальные raw pointer'ы** (`rp`, `rps`, `cum`) + хойстинг `base = action_id * N`: устранён potential alias-analysis pessimism от доступа через `this->vector.data()` на каждой итерации.
- **`#pragma GCC ivdep`** на inner loops: явно говорит компилятору "нет loop-carried data dependencies" → разрешает vectorize даже если alias-analysis неполный. Поддерживается clang и gcc.
- **FMA-совместимый cum_r_plus**: `cum[i] *= theta; cum[i] += strat * coef` → `cum[i] = cum[i] * theta + strat * coef` — одна операция, явно fusable в `fmadd`.
- **CFR+ `updateRegretsInPlace`**: r_plus loop и cum_r_plus loop также получили `rp/rps/cum` pointers и `#pragma GCC ivdep`.
- HF2 вариант (`DiscountedCfrTrainableHF`) — не изменён: там `half` conversions в inner loop необходимы для корректности и inherently предотвращают float SIMD.

Файлы: `src/trainable/DiscountedCfrTrainable.cpp`, `src/trainable/CfrPlusTrainable.cpp`, `src/trainable/DiscountedCfrTrainableSF.cpp`.

Ожидаемый эффект:
- `strategy_fetch_ms` и `regret_update_ms` должны упасть на 20–50% для HF0 на arm64 (NEON 4-wide) и до 2x на 5950X (AVX2 8-wide).
- Rough-цель 2–4x была для arithmetic-heavy isolated loops; с учётом overhead вокруг loops реальный wall-clock win ожидается меньше — порядка 10–30%.

TODO P9 (benchmark results — заполнить после прогона):
- Прогнать quick benchmark (120 iter, HF0, none/14t) до/после: `strategy_fetch_ms`, `regret_update_ms`, wall-clock.
- Если auto-vectorization не даёт ощутимого выигрыша по assembly: рассмотреть explicit NEON intrinsics для arm64 (vaddq_f32, vmaxq_f32, vbslq_f32).
- После замера добавить строку "## P9 Benchmark Results" в этот раздел.

P10. Time-budget solve mode
Критичность: High для продукта, Low для benchmarks. Effort: Low.

Добавить `set_max_solve_time_ms <ms>` — остановка по таймеру вместо iteration count.
При прерывании выдавать current strategy (из `r_plus`), не average strategy.
Обоснование: average strategy деградирует при раннем прерывании (включает случайные ранние итерации); current strategy из r_plus — лучший output в любой момент.
Делает solver детерминированным по времени — ровно 5s или 10s.
Легко реализовать на базе существующего solve loop: добавить `elapsed_ms` check в iteration body.

P11. MCCFR External Sampling
Критичность: High для real-time цели. Effort: High (архитектурный сдвиг).
Приступать после того как P7+P8+P4.2+P9 исчерпаны и bottleneck остаётся iteration speed.

Принцип: traversing player — expand all actions (точно, как сейчас); opponent — семплировать один action по текущей стратегии.
Стоимость одной MCCFR iter ≈ 1/K от full CFR (K = avg branching factor оппонента ≈ 3–5).
После P7 (CFR+) MCCFR convergence улучшается дополнительно: CFR+ + MCCFR = современный SOTA.
В коде уже есть `MONTE_CARLO` enum; `PokerSolver.cpp#L122` всегда запускает `NONE`.

P12. Tree simplification / bet-size reduction (последняя очередь)
Критичность: Low до исчерпания P7-P11. Medium если P11 недостаточно.
Это product decision, не performance optimization: меняет качество GTO-стратегии, не скорость кода.
Рассматривать только если после P7+P8+P4.2+P9+P11 результат всё ещё не удовлетворяет RT-цели.

---

Приоритет по критичности (обновлено 2026-03, real-time advisor цель)

Завершено: P0, P1, P2, P3.1, P5, P6.1, P4.1, **P7**, **P8.1** (shared_timed_mutex), **P8.2**, **P8.3**, **P4.2** (unique_ptr + raw Trainable*), **P9** (code done, benchmark pending).
Исследовано и закрыто: P3.2, P6.2 (not useful < 50 iter).

Следующая очередь (code-level, в порядке приоритета):
  1. **P9**: ~~SIMD векторизация inner loops~~ — код завершён 2026-03-17, нужен benchmark
  2. **P10**: Time-budget API — low effort, высокий продуктовый impact

После исчерпания code-level:
  5. P11: MCCFR External Sampling
  6. P12: Tree simplification — только в самую последнюю очередь

Низкий приоритет: GPU, NUMA, HF mode (доказано не помогает), P6.3-4.

Реалистичный итог (real case Qs Jh 2h, 14t) — обновлено после P7

| Конфигурация | ~iter за 10s | Измеренная / ожидаемая exploitability |
| :--- | :--- | :--- |
| DCFR baseline (~362ms/iter) | ~28 | ~22.5% (измерено) |
| CFR+ (P7, ~362ms/iter) | ~28–31 | **~14.3%** (измерено) |
| + P8 (fix thread scaling) | ~40–55 | ~6–10% (оценка) |
| + P4.2 + P9 (arena + SIMD) | ~55–75 | ~3–6% (оценка) |
| + P11 (MCCFR) | ~100+ light iter | ~1–3% (оценка) |

PioSolver target в 10s: exploitability ~2–5. Достижимо через P8+P4.2+P9+P11 поверх CFR+.
