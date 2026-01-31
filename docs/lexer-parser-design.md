 # Lexer and Parser Design

 ## 1. Overview

 The SIMPL compiler frontend is implemented using:
 - **Flex** for lexical analysis
 - **Bison** for syntax analysis

 The lexer and parser are cleanly separated and communicate through shared token definitions.

 ---

 ## 2. Lexical Analysis (Lexer)

 ### Responsibilities
 - Read source code character streams
 - Group characters into tokens
 - Discard whitespace and comments
 - Detect invalid characters

 ---

 ### Token Categories

 - Keywords
 - Identifiers
 - Numeric literals
 - Operators
 - ADT-related keywords

 ---

 ### Design Decisions

 - Keywords are matched before identifiers
 - Tokens are centrally defined to ensure lexer–parser consistency
 - Line-based error tracking is supported

 ---

 ## 3. Syntax Analysis (Parser)

 ### Responsibilities
 - Validate program structure
 - Enforce grammar rules
 - Construct the Abstract Syntax Tree (AST)

 ---

 ### Grammar Characteristics

 - Context-free grammar
 - Designed for bottom-up parsing
 - Left recursion used for expression handling
 - Explicit statement boundaries via keywords

 ---

 ## 4. AST Construction

 - Each grammar rule constructs an AST node
 - AST nodes represent semantic structure, not syntax
 - AST is independent of surface syntax

 Example:

 ```simpl
 set x to x - 1
 ```

 AST:

 ```
 ASSIGN
  ├── IDENTIFIER(x)
  └── SUB
      ├── IDENTIFIER(x)
      └── NUMBER(1)
 ```

 ## 5. Error Handling

 - Lexer reports invalid tokens

 - Parser reports syntax errors with context

 - Errors do not halt analysis prematurely — the pipeline collects errors and attempts to continue where sensible to produce useful diagnostics

 ## 6. Extensibility

 The lexer and parser are designed to support:

 - semantic analysis

 - IR generation

 - optimization passes

 No refactoring is required to extend the compiler pipeline.

 ## 7. Summary

 The lexer-parser architecture of SIMPL adheres to standard compiler design principles while enabling language-specific semantic reasoning and future optimization work.

 ---

 # Repository Description

 ## Short description

 > A compiler frontend for SIMPL — a custom English-like programming language with built-in ADTs, AST construction, and semantic analysis using Flex and Bison.

 ## Long description (optional)

 > SIMPL is a custom-designed imperative programming language developed as part of a Compiler Design course. This repository implements lexical analysis, syntax analysis, AST construction, and semantic validation (including ADT correctness) using Flex, Bison, and C. The project emphasizes semantic reasoning and extensibility toward IR generation and optimization.

 ---

 ## What You Have Now

 ✔ Professional README  
 ✔ Complete language specification  
 ✔ Formal grammar documentation  
 ✔ Lexer–parser architecture explanation  
 ✔ Repo description ready for GitHub  

 This is submission-ready and evaluation-safe.
