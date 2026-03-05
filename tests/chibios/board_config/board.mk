# List of all the board related files.
BOARDDIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

BOARDSRC = $(BOARDDIR)/board.c

# Required include directories
BOARDINC = $(BOARDDIR)

# Shared variables
ALLCSRC += $(BOARDSRC)
ALLINC  += $(BOARDINC)
