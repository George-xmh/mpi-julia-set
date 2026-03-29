# Makefile - builds both the MPI compute program and the OpenGL renderer
# George [Last Name] - [Course Code]
#
# Usage:
#   make              - build both
#   make julia_mpi    - build just the compute program
#   make julia_render - build just the renderer
#   make clean        - remove binaries
#
# On SHARCNET load these modules first:
#   module load intel/2020
#   module load intelmpi/2019
#   module load freeglut
#   module load libjpeg-turbo

CC    = gcc
MPICC = mpicc
CFLAGS = -O2 -Wall -std=c99

.PHONY: all clean

all: julia_mpi julia_render

julia_mpi: julia_mpi.c
	$(MPICC) $(CFLAGS) -o $@ $< -lm

julia_render: julia_render.c
	$(CC) $(CFLAGS) -o $@ $< -lGL -lGLU -lglut -ljpeg -lm

clean:
	rm -f julia_mpi julia_render *.bin *.jpg
