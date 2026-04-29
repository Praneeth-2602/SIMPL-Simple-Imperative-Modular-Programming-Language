# ============================================================
# SIMPL Compiler v1.0 — Makefile
# ============================================================

SHELL  = /bin/sh
CC    ?= clang
BISON ?= bison
FLEX  ?= flex

CFLAGS = -Wall -Wextra -g \
         -I src/parser    \
         -I src/semantic  \
         -I src/ir        \
         -I src/optimizer \
         -I src/rl_agent  \
         -I src/codegen

LDLIBS = -lm

SRCS =  src/parser/simpl.tab.c      \
        src/lexer/lex.yy.c           \
        src/parser/ast.c             \
        src/semantic/symbol_table.c  \
        src/semantic/semantic.c      \
        src/ir/ir.c                  \
        src/optimizer/optimizer.c    \
        src/rl_agent/rl_agent.c      \
        src/codegen/codegen.c        \
        src/main.c

TARGET = simpl

.PHONY: all clean generate run

all: generate $(TARGET)

generate:
	$(BISON) -d -o src/parser/simpl.tab.c src/parser/simpl.y
	$(FLEX)  -o src/lexer/lex.yy.c src/lexer/simpl.l

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) $(LDLIBS)

# Run a .simpl file end-to-end and produce a native binary
# Usage: make run FILE=test.simpl
run: all
	./$(TARGET) $(FILE) output.ll
	clang output.ll -o program
	./program

clean:
	rm -f src/parser/simpl.tab.c src/parser/simpl.tab.h
	rm -f src/lexer/lex.yy.c
	rm -f output.ll program
	rm -f $(TARGET) $(TARGET).exe
