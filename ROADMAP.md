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

P3. Расширить распараллеливание выше chance-узлов
Критичность: Critical. Выигрыш: 1.3-1.8x на 5950X после P1/P2, иногда больше.
Сейчас OpenMP реально работает в основном на chance-узлах в PCfrSolver.cpp#L301, а action recursion идёт последовательно в PCfrSolver.cpp#L457, весь solve запускается без task scheduler в PCfrSolver.cpp#L789. Нужен task-based scheduler или work-stealing по поддеревьям/action branches, плюс thread-local accumulation вместо shared writes. Это самый вероятный путь поднять загрузку CPU заметно выше текущих 40%.

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
