# SIMPL Compiler

**SIMPL (Simple Imperative Modular Programming Language)** is a custom-designed, English-like programming language developed as part of a Compiler Design course project.

The project implements a complete **compiler frontend**, including:

- Lexical analysis
- Syntax analysis
- Abstract Syntax Tree (AST) construction
- Semantic analysis with type and ADT correctness checks

SIMPL is intentionally designed with restricted and well-defined semantics to enable strong compile-time guarantees and advanced semantic analysis that are difficult or impossible in low-level languages like C.

---

## ✨ Key Features

- English-like, readable syntax
- Built-in abstract data types (ADTs):
  - Stack
  - Queue
  - Tree
  - Graph
- First-class semantic validation of ADT usage
- Compile-time detection of:
  - Undeclared variables
  - Redeclarations
  - Type mismatches
  - Invalid ADT operations
  - Stack/Queue underflow
  - Invalid graph edge operations
- Structured AST generation for further compilation stages
- Transparent optimization reporting
- Designed for extensibility toward IR generation and optimization

---

## 🧱 Compiler Architecture

```
Source Code (.simpl)
        ↓
   Lexer (Flex)
        ↓
   Parser (Bison)
        ↓
 Abstract Syntax Tree (AST)
        ↓
 Semantic Analysis
 (Symbol Table + Type Checking + ADT State Tracking)
        ↓
 [IR Generation & Optimization — Planned]
```

---

## 🔥 Features GCC Cannot Provide

SIMPL's semantic analyzer provides guarantees that are **impossible in C/GCC**:

| Feature | Description |
|---------|-------------|
| **ADT Safety** | Stacks/queues cannot be printed or used in arithmetic |
| **Compile-Time Underflow** | Detects stack/queue underflow before runtime |
| **Graph Edge Tracking** | Detects removal of non-existent edges at compile time |
| **Optimization Reporting** | Transparent reporting of optimization opportunities |
| **Dead Code Detection** | Identifies patterns like push-then-pop |

---

## 🛠 Toolchain

| Tool | Purpose |
|------|---------|
| **C** | Implementation language |
| **Flex** | Lexer generator |
| **Bison** | Parser generator |
| **GCC** | C compiler (via MSYS2 MinGW) |
| **Platform** | Windows (MSYS2 MinGW64) |

---

## 📂 Project Structure

```
SIMPL/
├── src/
│   ├── lexer/
│   │   ├── simpl.l          # Flex lexer specification
│   │   └── tokens.h         # Token definitions
│   ├── parser/
│   │   ├── simpl.y          # Bison grammar specification
│   │   ├── ast.h            # AST node definitions
│   │   └── ast.c            # AST construction functions
│   ├── semantic/
│   │   ├── semantic.h       # Semantic analyzer interface
│   │   ├── semantic.c       # Semantic analysis implementation
│   │   ├── symbol_table.h   # Symbol table interface
│   │   └── symbol_table.c   # Symbol table + ADT state tracking
│   └── optimizer/           # (planned)
├── docs/
│   ├── language-spec.md
│   ├── grammar.md
│   └── lexer-parser-design.md
├── tests/
│   ├── lexer/
│   └── parser/
├── build/
├── Makefile
└── README.md
```

---

## 🚀 Building the Compiler

From the project root:

```bash
# Generate parser
bison -d src/parser/simpl.y -o src/parser/simpl.tab.c

# Generate lexer
flex -o src/lexer/lex.yy.c src/lexer/simpl.l

# Compile everything
gcc -I src/parser -I src/semantic \
    src/parser/simpl.tab.c \
    src/lexer/lex.yy.c \
    src/parser/ast.c \
    src/semantic/symbol_table.c \
    src/semantic/semantic.c \
    -o simpl
```

---

## ▶️ Running SIMPL Programs

### Interactive Mode

```bash
./simpl
```

Then type your code and press `Ctrl+D` (Unix) or `Ctrl+Z` (Windows) to compile.

### Pipe Input

```bash
echo "let x be 10
print x" | ./simpl
```

### From File

```bash
./simpl < program.simpl
```

---

## 📝 Language Syntax Examples

### Variable Declaration

```simpl
let x be 10
let name be 42
```

### Assignment

```simpl
set x to 20
set x to x + 5
```

### Arithmetic

```simpl
let result be x + y * 2
```

### Control Flow

```simpl
if x > 10 then
    print x
else
    print 0
end

while x > 0 do
    set x to x - 1
end
```

### ADT Operations

```simpl
# Stack
let s be stack
push s 10
push s 20
pop s

# Queue
let q be queue
enqueue q 1
enqueue q 2
dequeue q

# Graph
let g be graph
add_edge g 1 2
add_edge g 2 3
remove_edge g 1 2
```

---

## 🧪 Example Test Cases

### ✅ Valid Program

```simpl
let s be stack
push s 10
push s 20
pop s
let x be 5
print x
```

**Output:**

```
Parsing successful.

=== SIMPL Semantic Analysis ===
Features: ADT Safety, State Tracking, Optimization
===============================

  [DECL] s : stack
  [SIM] Stack 's' size: 0 -> 1
  [SIM] Stack 's' size: 1 -> 2
  [SIM] Stack 's' size: 2 -> 1
  [DECL] x : int

=== Summary ===
Errors:   0
Warnings: 0
===============

Compilation successful!
  - ADT safety: verified
  - State tracking: complete
```

### ❌ Stack Underflow (Compile-Time Detection)

```simpl
let s be stack
pop s
```

**Output:**

```
Semantic error: STACK UNDERFLOW at compile time! Stack 's' is empty (size=0)
```

### ❌ Cannot Print ADT

```simpl
let s be stack
print s
```

**Output:**

```
Semantic error: cannot print stack - ADTs are not printable (SIMPL safety)
```

### ❌ Invalid Graph Edge Removal

```simpl
let g be graph
remove_edge g 1 2
```

**Output:**

```
Semantic error: INVALID EDGE REMOVAL at compile time! Edge (1 -> 2) does not exist in graph 'g'
```

---

## 🎓 Academic Context

This compiler was developed as part of a **Compiler Design** course project. The design emphasizes:

1. **Language-Level Safety** — Guarantees that cannot be provided by compiling to C
2. **Compile-Time Analysis** — Catching errors before runtime
3. **Transparent Compilation** — Clear reporting of analysis and optimization decisions
4. **Extensibility** — Clean architecture for adding IR generation and optimization

---

## 📄 License

This project is developed for educational purposes as part of a university course.

---

## 👤 Author

Developed as a Compiler Design course project.
