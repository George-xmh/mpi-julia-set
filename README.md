# Julia Set MPI

Parallel computation of filled Julia sets using MPI/C and OpenGL rendering.

## Files

| File | Description |
|------|-------------|
| `julia_mpi.c` | MPI parallel computation (cyclic row distribution for load balancing) |
| `julia_render.c` | OpenGL/GLUT renderer, outputs JPEG via libjpeg |
| `Makefile` | Build both programs |
| `run_julia.sh` | SLURM batch script — computes all poster cases |
| `render_julia.sh` | SLURM batch script — renders all `.bin` files to JPEG |
| `chapter_exercises.md` | Solutions to selected exercises from Devaney Ch. 6 & 7 |

## Building

```bash
module load intel/2020
module load intelmpi/2019
make
```

## Running

```bash
# quick test (c=0 should produce a disk)
mpirun -np 4 ./julia_mpi -f quad -c1 0.0 -c2 0.0 -g 500 -m 100 -o test.bin

# full batch job
sbatch run_julia.sh

# render outputs
sbatch render_julia.sh
```

## Supported families

- `quad` — Q_c(z) = z² + c
- `cubic` — T_c(z) = z³ + c

## Load balancing

Uses cyclic row distribution: rank `p` owns rows `p, p+P, p+2P, ...`  
This interleaves cheap (far from boundary) and expensive (near J_c) rows across all ranks.
