# ============================================================
# SIMPL Compiler v1.0 — Makefile
# ============================================================

CC     = gcc
CFLAGS = -Wall -Wextra -g \
         -I src/parser    \
         -I src/semantic  \
         -I src/ir        \
         -I src/optimizer \
         -I src/codegen

SRCS =  src/parser/simpl.tab.c      \
        src/lexer/lex.yy.c           \
        src/parser/ast.c             \
        src/semantic/symbol_table.c  \
        src/semantic/semantic.c      \
        src/ir/ir.c                  \
        src/optimizer/optimizer.c    \
        src/codegen/codegen.c        \
        src/main.c

TARGET = simpl

.PHONY: all clean generate run

all: generate $(TARGET)

generate:
	bison -d src/parser/simpl.y -o src/parser/simpl.tab.c
	flex  -o src/lexer/lex.yy.c    src/lexer/simpl.l

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

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
	rm -f $(TARGET)
