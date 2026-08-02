# SIMPL Compiler

**SIMPL (Simple Imperative Modular Programming Language)** is an English-like language and **full compiler pipeline** built for a Compiler Design course: frontend with strong ADT safety, three-address IR, classical opts, **Q-learning pass selection**, and **LLVM IR** codegen.

SIMPL’s restricted semantics enable compile-time guarantees (stack underflow, invalid graph edges, non-printable ADTs) that C/GCC cannot give at the language level.

## Key features

- English-like syntax with built-in ADTs: **stack**, **queue**, **tree**, **graph**
- Compile-time detection of undeclared/redeclared names, type errors, ADT misuse, underflow, invalid edge removal
- Three-address IR + optimizers: const fold/prop, copy prop, CSE, DCE, unreachable elim
- Tabular **Q-learning** (~5,184 states × 6 actions) choosing optimization passes (`--train` / `--eval`)
- Emit **LLVM IR** (`.ll`) and run via Clang

## Compiler architecture

```
Source (.simpl)
      ↓
 Lexer (Flex) → Parser (Bison) → AST
      ↓
 Semantic analysis (symbol table + ADT state tracking)
      ↓
 Three-address IR
      ↓
 Optimizer (± RL agent selecting passes)
      ↓
 LLVM IR codegen → Clang → native binary
```

See [`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md) for RL MDP details and training status.

## Toolchain

| Tool | Purpose |
|---|---|
| C (+ Clang recommended) | Compiler implementation |
| Flex / Bison | Lexer & parser |
| Clang | Compile generated `.ll` |
| Make | Build driver |

Works on macOS/Linux; Windows via MSYS2/MinGW is possible with the same tools.

## Project structure

```
src/
  lexer/ parser/ semantic/   # Frontend
  ir/ optimizer/             # Mid-end
  rl_agent/                  # Q-learning pass selection
  codegen/                   # LLVM IR
  main.c                     # CLI: compile / --train / --eval
docs/                        # Language spec, grammar, implementation plan
tests/ eval_files/           # Fixtures & RL eval corpus
train.sh eval.sh             # Training / evaluation harnesses
Makefile
```

## Build

```bash
make            # bison + flex + build ./simpl
make clean      # optional
```

## Run

```bash
# Compile a program → output.ll (then native via helper)
make run FILE=test.simpl

# Or manually:
./simpl test.simpl output.ll
clang output.ll -o program && ./program

# RL training / evaluation (uses rl_qtable.bin)
./simpl program.simpl --train --quiet
./train.sh
./eval.sh
./simpl --dump-qtable --qtable rl_qtable.bin
```

Normal compile loads the Q-table when present and may apply the learned pass policy; see implementation plan for flags (`--qtable`, `--eval`).

## Language examples

### Basics

```simpl
let x be 10
set x to x + 5

if x > 10 then
    print x
else
    print 0
end

while x > 0 do
    set x to x - 1
end
```

### ADTs

```simpl
let s be stack
push s 10
push s 20
pop s

let q be queue
enqueue q 1
dequeue q

let g be graph
add_edge g 1 2
remove_edge g 1 2
```

### Compile-time safety

```simpl
let s be stack
pop s
# → semantic error: STACK UNDERFLOW at compile time
```

```simpl
let s be stack
print s
# → cannot print stack — ADTs are not printable
```

## Docs

- [`docs/language-spec.md`](docs/language-spec.md)
- [`docs/grammar.md`](docs/grammar.md)
- [`docs/IMPLEMENTATION_PLAN.md`](docs/IMPLEMENTATION_PLAN.md) — IR / RL / eval pipeline

## Academic context

Course project emphasizing language-level safety, transparent analysis, and an energy-aware (instruction-elim vs compile-time) RL bandit over optimization passes.

## License

Educational / course use.
