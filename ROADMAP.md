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
- Сейчас идти в `P5`: убрать глобальный mutex/serial contention из `RiverRangeManager`, сделать board-local cache и ускорить evaluator/showdown pipeline.
- Возвращаться к `P3` не после полного завершения `P5`, а сразу после первого измеримого milestone: когда `river_lock_wait_ms` на `none / 32` упадёт хотя бы примерно вдвое или перестанет быть одним из доминирующих пунктов профиля.
- В этот момент запускать `P3.2`: subtree-aware scheduler, retune task cutoff, thread sweep и повторную A/B matrix-проверку на том же benchmark profile.

P4. Упростить и уплотнить базовые структуры данных
Критичность: High. Выигрыш: 10-25%, RAM -10-20%.
PrivateCards хранит внутри heap-овый vector<int> на каждую руку в PrivateCards.h#L21 и PrivateCards.cpp#L15; дерево построено на shared_ptr-графе с множеством мелких объектов в GameTreeNode.h и GameTree.cpp. Для solver-ядра лучше перейти на POD/SoA для рук, плоские массивы/arena для узлов, индексы вместо shared_ptr, contiguous layout для children/actions/trainables.

P5. Ускорить evaluator и river/showdown pipeline
Критичность: High. Выигрыш: 15-35% на turn/river-heavy деревьях.
Сейчас RiverRangeManager держит глобальный mutex в RiverRangeManager.cpp#L35, а rank считается через bitmask -> vector<int> -> перебор 5-card combinations в Dic5Compairer.cpp#L261 и Dic5Compairer.cpp#L287. Тут большой резерв: board-specific cache без глобальной блокировки, precompute valid-hand masks, более быстрый 7-card evaluator/LUT, меньше преобразований uint64_t <-> vector<int>.

P6. Алгоритмические ускорители без смены класса solver
Критичность: High. Выигрыш: 1.5-3x, зависит от дерева.
Наиболее реалистичны regret-based pruning, lazy CFR updates, skipping low-probability nodes, strategy freezing, adaptive iteration scheduling. Это лучше внедрять после P1-P5, иначе код станет сложнее, а bottleneck по памяти всё равно останется. У вас уже есть задел на discounted CFR; CFR+ как “просто переключить” сейчас не даст такого эффекта и вообще не доведён до рабочего пути в PCfrSolver.cpp#L120.

P7. Большие архитектурные ускорители
Критичность: Medium/High. Выигрыш: 2-10x, но уже с компромиссами и большим объёмом работ.
Сюда входят Monte-Carlo / hybrid CFR, subgame solving, depth-limited solving, warm start, reusing solved subgames, coarse-to-fine solving. В коде уже есть задел под Monte Carlo enum, но runtime всегда запускает NONE в PokerSolver.cpp#L122. Это хороший R&D этап, но не первый.

Приоритет по критичности

Самое срочное: P1, P2, P3.
Следом: P4, P5.
Потом: P6.
Дальше как отдельная ветка продукта: P7.
Низкий приоритет сейчас: GPU, NUMA-aware оптимизации, AVX512. Для 5950X сначала нужен AVX2/FMA и нормальный data layout.
Реалистичный итог

Без смены solver-класса и без sampling: 2-4x выглядит реалистично.
С удачным task scheduler + pruning + evaluator rewrite: можно целиться выше.
С sampling/subgame solving: потенциально 4-10x+, но уже ценой большей сложности и аппроксимаций.
