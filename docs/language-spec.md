# SIMPL Language Specification

## 1. Introduction

SIMPL (Simple Imperative Modular Programming Language) is an English-like, imperative
programming language created for educational use in a Compiler Design course. SIMPL
prioritises readability, semantic clarity and strong compile-time guarantees. The
language intentionally restricts low-level features (e.g. raw pointers, manual
memory management) so the compiler can perform sound static analysis and
semantic optimizations.

---

## 2. Design Goals

- Human-readable, natural-language inspired syntax
- Deterministic and unambiguous grammar
- Built-in abstract data types (ADTs): stack, queue, tree, graph
- Strong, early semantic validation (compile-time)
- Extensible architecture for IR generation and optimizations

---

## 3. Lexical Elements

### 3.1 Keywords

The following words are reserved keywords in SIMPL and cannot be used as identifiers:

```
let, set, be, to
if, then, else, end
while, do
print
stack, queue, tree, graph
push, pop
enqueue, dequeue
insert, remove
add_edge, remove_edge
```

### 3.2 Identifiers

- Must begin with a letter or underscore
- May contain letters, digits and underscores

Example:

```simpl
let counter be 10
```

### 3.3 Literals

Integer literals are sequences of digits. Example:

```simpl
let x be 42
```

---

## 4. Data Types

### 4.1 Primitive Types

- `int` (the only primitive numeric type in the initial design)

### 4.2 Built-in Abstract Data Types (ADTs)

- `stack`
- `queue`
- `tree`
- `graph`

These ADTs are first-class language constructs with compiler-enforced
semantics (see Semantic Rules below). The compiler treats ADTs differently from
primitive integers and enforces ADT-specific operations and invariants.

---

## 5. Statements

### 5.1 Variable Declaration

```simpl
let x be 10
```

Declares `x` and initializes it with the expression value. If the right-hand side
is an ADT type (e.g. `stack`) the declaration creates an ADT variable.

### 5.2 Assignment

```simpl
set x to x + 1
```

Assigns the result of the expression to an existing integer variable `x`.

### 5.3 Print

```simpl
print x
```

Printing an ADT is not treated as printing its raw memory; the compiler enforces
what is printable. By default, printing a primitive yields a numeric output;
printing an ADT is forbidden by the safety rules (or may produce a structured,
controlled serialization if explicitly supported).

---

## 6. Control Flow

### 6.1 Conditional Statements

```simpl
if x > y then
    print x
else
    print y
end
```

### 6.2 Loops

```simpl
while x > 0 do
    set x to x - 1
end
```

---

## 7. ADT Operations

### 7.1 Stack

```simpl
let s be stack
push s 10
pop s
```

### 7.2 Queue

```simpl
let q be queue
enqueue q 5
dequeue q
```

### 7.3 Tree

```simpl
let t be tree
insert t 10
remove t 10
```

### 7.4 Graph

```simpl
let g be graph
add_edge g 1 2
remove_edge g 1 2
```

ADTs are manipulated only via their dedicated operations; mixing ADT values in
arithmetic or assignments to primitive variables is a semantic error.

---

## 8. Semantic Rules

- Variables must be declared before use.
- Redeclaration of a variable is not allowed.
- ADT operations must match the declared ADT type.
- Type mismatches are detected at compile time.
- Logical misuse of ADTs (e.g. stack underflow, removing non-existent graph edge)
  is reported during semantic analysis when statically determinable.

The compiler collects and reports errors and warnings; it does not abort on the
first error so multiple issues can be seen at once.

---

## 9. Error Handling

SIMPL reports three classes of errors:

- Lexical errors
- Syntax errors
- Semantic errors (type/ADT/state errors)

Errors and warnings are accumulated and summarized after analysis.

---

## 10. Future Extensions

- Functions and procedures
- Intermediate Representation (IR) generation
- Optimization passes that use semantic information
- Code generation to target languages or bytecode

---

## 11. Summary

SIMPL demonstrates how language design can enable powerful compile-time
reasoning, semantic validation and domain-aware optimizations that are
impractical for low-level languages like C.

---

## Appendix: Grammar (overview)

The following is a concise presentation of the language grammar used by the
SIMPL parser (Bison). This is an informal, human-readable grammar summary.

### Start symbol

```
program
```

### Program structure

```
program -> statement_list

statement_list -> statement
               | statement_list statement
```

### Statements

```
statement -> declaration
          | assignment
          | print_stmt
          | if_stmt
          | while_stmt
          | adt_declaration
          | adt_operation
```

### Declarations

```
declaration -> let IDENTIFIER be expression

adt_declaration -> let IDENTIFIER be stack
                 | let IDENTIFIER be queue
                 | let IDENTIFIER be tree
                 | let IDENTIFIER be graph
```

### Assignment

```
assignment -> set IDENTIFIER to expression
```

### Print

```
print_stmt -> print expression
```

### Control flow

```
if_stmt -> if condition then statement_list end
         | if condition then statement_list else statement_list end

while_stmt -> while condition do statement_list end
```

### Conditions

```
condition -> expression < expression
           | expression > expression
           | expression == expression
           | expression != expression
```

### Expressions

```
expression -> expression + term
            | expression - term
            | term

term -> term * factor
      | term / factor
      | factor

factor -> NUMBER
        | IDENTIFIER
```

### ADT operations

```
adt_operation -> push IDENTIFIER expression
               | pop IDENTIFIER
               | enqueue IDENTIFIER expression
               | dequeue IDENTIFIER
               | insert IDENTIFIER expression
               | remove IDENTIFIER expression
               | add_edge IDENTIFIER expression expression
               | remove_edge IDENTIFIER expression expression
```

### Precedence and associativity

- Multiplication and division bind tighter than addition and subtraction.
- Operators are left-associative where applicable.

---

For full, exact grammar used by the parser (Bison file), see `src/parser/simpl.y`.
