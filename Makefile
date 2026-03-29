# Makefile for Julia-set MPI project
# ====================================
# Targets:
#   all           – build both programs
#   julia_mpi     – MPI computation program
#   julia_render  – OpenGL / JPEG renderer
#   clean         – remove binaries and .bin files
#
# Tested on SHARCNET (Graham / Niagara) with:
#   module load intel/2020  intelmpi/2019  freeglut  libjpeg-turbo

CC      = gcc
MPICC   = mpicc

# ---------- Compiler flags ----------
CFLAGS_COMMON = -O2 -Wall -Wextra -std=c99
CFLAGS_MPI    = $(CFLAGS_COMMON)
CFLAGS_GL     = $(CFLAGS_COMMON)

# ---------- Linker flags ----------
# Adjust paths if modules place libraries elsewhere
LDFLAGS_MPI = -lm
LDFLAGS_GL  = -lGL -lGLU -lglut -ljpeg -lm

.PHONY: all clean

all: julia_mpi julia_render

julia_mpi: julia_mpi.c
	$(MPICC) $(CFLAGS_MPI) -o $@ $< $(LDFLAGS_MPI)

julia_render: julia_render.c
	$(CC) $(CFLAGS_GL) -o $@ $< $(LDFLAGS_GL)

clean:
	rm -f julia_mpi julia_render *.bin *.jpg
