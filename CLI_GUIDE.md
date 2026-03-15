# TexasSolver CLI Guide

The CLI (Command Line Interface) version of TexasSolver allows you to automate poker calculations using simple text commands. This is ideal for benchmarking, bulk solving, or integration with other tools.

## Core Workflow

The solver works by following these steps in order:
1. **Set Environment**: Define the bank, stacks, and board.
2. **Set Ranges**: Define the ranges for IP (In Position) and OOP (Out of Position) players.
3. **Configure Tree**: Define bet and raise sizes for all streets.
4. **Build Tree**: Generate the game tree in memory.
5. **Solve**: Run the CFR algorithm until the desired accuracy is reached.
6. **Export**: Save the results to a JSON file.

## Command Reference

### Environment & Ranges
- `set_pot <size>`: Total pot size.
- `set_effective_stack <size>`: Remaining effective stack.
- `set_board <cards>`: Cards separated by commas (e.g., `As,Kh,7d`). Accepts 3, 4, or 5 cards.
- `set_range_ip <range>`: Range string in standard format (e.g., `AA,KK,QQ:0.75`).
- `set_range_oop <range>`: Range string for the OOP player.

### Bet Sizes (Tree Building)
Format: `set_bet_sizes <player>,<street>,<type>,<values>`
- **Player**: `ip` or `oop`
- **Street**: `flop`, `turn`, `river`
- **Type**: `bet`, `raise`, `donk`, `allin`
- **Values**: Comma-separated percentages of the pot (e.g., `30,70,100`).

Example:
```bash
set_bet_sizes oop,flop,bet,30,70
set_bet_sizes ip,flop,allin
```

### Solver Configuration
- `build_tree`: MUST be called after all sizes and environment settings are done.
- `set_thread_num <n>`: Number of threads to use.
- `set_accuracy <value>`: Target exploitability (e.g., `0.3`).
- `set_max_iteration <n>`: Maximum number of rounds.
- `set_use_isomorphism <0|1>`: Enable card isomorphism for speed.

### Execution & Output
- `start_solve`: Begins the calculation. Progress will be printed to stdout.
- `dump_result <filename.json>`: Saves the strategy to a file.
- `set_dump_rounds <n>`: Accuracy of the dump (usually 1 or 2).

## Example Script (`script.txt`)

You can save these commands in a file and pipe them to the solver:

```text
set_pot 10
set_effective_stack 95
set_board Qs,Jh,2h
set_range_oop AA,KK,QQ,JJ,TT
set_range_ip AK,AQ,AJ,AT
set_bet_sizes oop,flop,bet,50,100
set_bet_sizes ip,flop,raise,50
build_tree
set_thread_num 8
start_solve
dump_result solver_output.json
```

## Running the CLI

If you have built the console version, run it as:
```bash
./release/TexasSolverConsole.exe --input_file script.txt
```
Or interactively by just launching the executable.
