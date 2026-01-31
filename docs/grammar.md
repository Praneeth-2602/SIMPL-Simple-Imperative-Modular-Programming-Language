# SIMPL Grammar Specification

## Overview

This document describes the context-free grammar of SIMPL as implemented with Bison. The grammar is suitable for bottom-up parsing and uses left recursion for natural expression associativity.

## Start symbol

program

## Grammar (BNF-style)

The grammar below uses uppercase tokens for terminals and lowercase names for nonterminals.

```bnf
program ::= statement_list

statement_list ::= statement
		   | statement_list statement

statement ::= declaration
	     | assignment
	     | print_stmt
	     | if_stmt
	     | while_stmt
	     | adt_declaration
	     | adt_operation

declaration ::= "let" IDENTIFIER "be" expression

adt_declaration ::= "let" IDENTIFIER "be" "stack"
		    | "let" IDENTIFIER "be" "queue"
		    | "let" IDENTIFIER "be" "tree"
		    | "let" IDENTIFIER "be" "graph"

assignment ::= "set" IDENTIFIER "to" expression

print_stmt ::= "print" expression

if_stmt ::= "if" condition "then" statement_list "end"
	   | "if" condition "then" statement_list "else" statement_list "end"

while_stmt ::= "while" condition "do" statement_list "end"

condition ::= expression "<" expression
	     | expression ">" expression
	     | expression "==" expression
	     | expression "!=" expression

expression ::= expression "+" term
	      | expression "-" term
	      | term

term ::= term "*" factor
	| term "/" factor
	| factor

factor ::= NUMBER
	  | IDENTIFIER

adt_operation ::= "push" IDENTIFIER expression
		  | "pop" IDENTIFIER
		  | "enqueue" IDENTIFIER expression
		  | "dequeue" IDENTIFIER
		  | "insert" IDENTIFIER expression     /* tree/graph node insert */
		  | "remove" IDENTIFIER expression     /* tree/graph node remove */
		  | "add_edge" IDENTIFIER expression expression
		  | "remove_edge" IDENTIFIER expression expression

```

## Precedence and associativity

- Multiplication and division bind tighter than addition and subtraction.
- All binary arithmetic operators are left-associative (implemented using left recursion above).

When using Bison, declare precedence to resolve any remaining shift/reduce conflicts:

```yacc
%left '+' '-'
%left '*' '/'
```

## Notes

- ADT operations are statements (not expressions) and are subject to semantic checks (ADT type correctness, underflow, invalid edge operations, etc.).
- The grammar intentionally avoids pointers and implicit casts; deeper correctness is enforced during semantic analysis.
- Error recovery in the parser should attempt to synchronize at statement boundaries (e.g., after `end` or on a newline) to continue producing useful diagnostics.

## Example

Source:

```simpl
set x to x - 1
```

Corresponding AST (conceptual):

```
ASSIGN
 ├── IDENTIFIER(x)
 └── SUB
     ├── IDENTIFIER(x)
     └── NUMBER(1)
```
