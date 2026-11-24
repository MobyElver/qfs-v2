# Makefile -- build the QFS version 1 programs
#
#   make            build every program
#   make mkfs_qfs   build one program (from mkfs_qfs.c)
#   make clean      remove the programs
#
# Each program is built from the .c file of the same name. Every program
# includes qfs.h, so each one is rebuilt whenever qfs.h changes.

CC     := gcc
CFLAGS ?= -Wall -Wextra -std=c11 -g

PROGS := mkfs_qfs list_information read_file recover_files \
         delete_file write_file

.PHONY: all clean

all: $(PROGS)

%: %.c qfs.h
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(PROGS)
