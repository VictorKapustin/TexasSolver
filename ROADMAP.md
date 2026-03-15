Оценки ниже rough и считаются относительно текущего baseline. Они не суммируются линейно: два улучшения часто бьют в один и тот же bottleneck.

Roadmap

P0. Базовая диагностика и режим запуска
Критичность: Critical. Выигрыш: 0-15% напрямую, но это обязательный этап.
Сначала разделить время на solve, best response, river cache, allocator, LLC miss, memory bandwidth; отдельно прогнать 16 и 32 потоков, плюс pinning по CCD. На 5950X для memory-bound solver очень часто 16 физических потоков быстрее 32 SMT. Заодно вернуть LTO и подготовить PGO: сейчас -O3 -march=native уже есть, а LTO выключен в TexasSolverGui.pro#L59.

P1. Убрать лишние аллокации и копии в hot path
Критичность: Critical. Выигрыш: 20-40%, RAM -15-30%.
Сейчас на каждом проходе массово создаются и копируются vector<float>: new_reach_probs в PCfrSolver.cpp#L319, regrets/results/all_action_utility в PCfrSolver.cpp#L451, стратегия возвращается по значению в PCfrSolver.cpp#L436 и DiscountedCfrTrainable.cpp#L51. Первый большой шаг: thread-local scratch buffers, in-place API для strategy/regret, меньше временных векторов, реже пересчитывать exploitability. Быстрые мелочи: exchange_color должен принимать const vector<PrivateCards>&, а не копию в utils.h#L19.

P2. Починить memory layout trainables
Критичность: Critical. Выигрыш: 10-25%, плюс лучшее масштабирование по ядрам.
Half-float сейчас помогает только на river, потому что выбор storage ограничен в ActionNode.cpp#L44. При этом даже в HF/SF ветках есть лишний временный footprint: r_plus_sum выделяется размером action_count * card_count, хотя индексируется только по card_count, в DiscountedCfrTrainableHF.cpp#L105 и DiscountedCfrTrainableSF.cpp#L60. Нужны: правильные размеры буферов, более широкая компрессия не только на river, lazy materialization strategy, и по возможности fixed-point/half storage для части cumulative данных.

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