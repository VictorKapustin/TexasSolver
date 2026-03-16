P2. Починить memory layout trainables

Критичность: Critical
Ожидаемый выигрыш: 10-25% после полного phase; один фикс r_plus_sum сам по себе даст только малый локальный эффект.
Цель: убрать лишний persistent и temporary footprint в trainables, снять river-only ограничение для compressed storage и улучшить масштабирование по потокам.

Scope

Исправить реальные layout bugs.
Убрать HF shadow-copy, который сейчас съедает почти весь выигрыш от half storage.
Включить use_halffloats на всех улицах, а не только на river.
Протянуть use_halffloats в CLI/benchmark, чтобы phase можно было реально измерять.
Добавить layout-aware оценку памяти trainables.
Не менять precision-модель SF в этой фазе.
P2a. Correctness + Instrumentation

В ActionNode.cpp убрать принудительное (round == RIVER) ? use_halffloats : 0; storage должен выбираться по use_halffloats на всех улицах.
В DiscountedCfrTrainableSF.cpp и DiscountedCfrTrainableHF.cpp исправить getcurrentStrategyNoCache(): временный r_plus_sum должен иметь размер card_number, а не action_count * card_count.
В CommandLineTool.cpp добавить CLI/benchmark-параметр use_halffloats, чтобы профилирование реально гоняло режимы 0/1/2.
В GameTree.cpp и/или benchmark metadata обновить оценку памяти под реальные storage layouts; RSS использовать только как вторичную метрику.
P2b. Remove wasted HF persistent footprint

В DiscountedCfrTrainableHF.h удалить member r_plus_local.
В DiscountedCfrTrainableHF.cpp переписать getcurrentStrategyInPlace() и updateRegretsInPlace() так, чтобы не хранить persistent float-копию всего r_plus.
Предпочтительный вариант: временную materialization делать через per-thread scratch из PCfrSolver.h.
Допустимый fallback: двухпроходный доступ по half-массиву без полного persistent float buffer.
r_plus_sum оставить как небольшой per-object буфер размера card_number.


P2c. Safe storage policy

Оставить SF как control variant: evs=half, r_plus=float, cum_r_plus=float.
Оставить HF как compressed variant: evs=half, r_plus=half, cum_r_plus=half, без r_plus_local.
Не переводить CumRplusStorage в half для SF в этой фазе.
Не добавлять fixed-point storage в основной merge этой фазы.


P2d. Optional follow-up

Отдельным флагом проверить cum_r_plus fixed-point / альтернативное half-storage.
Мержить только если drift по exploitability и time-to-target остаётся в допустимом диапазоне.
Verification

Функционально: сравнить use_halffloats=0/1/2 на одинаковом дереве и одинаковом числе итераций по exploitability и dump strategy.
Производительно: после добавления CLI-параметра прогнать run_benchmark_matrix.ps1 и сравнить solve_wall_ms, median iteration_total_ms, solver_profile.strategy_fetch, solver_profile.regret_update, allocator_profile.bytes/calls.
По масштабированию: отдельно сравнить 16 и 32 потоков для каждого storage mode.
По памяти: сравнить layout-aware estimate trainables и RSS; основным критерием считать estimate, а не RSS.
Acceptance Criteria

use_halffloats=1/2 работает на flop/turn/river, а не только на river.
HF даёт реальное снижение persistent footprint относительно SF на всех улицах.
После P2b/P2c виден выигрыш по strategy_fetch и/или regret_update.
Drift относительно float baseline укладывается в заранее заданный допуск.