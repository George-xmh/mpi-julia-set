#!/bin/bash
#SBATCH --job-name=julia_mpi
#SBATCH --account=def-yourpi          # <-- replace with your allocation
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=16          # 32 MPI ranks total
#SBATCH --cpus-per-task=1
#SBATCH --mem-per-cpu=512M
#SBATCH --time=0-00:30:00             # 30-minute wall limit
#SBATCH --output=julia_%j.out
#SBATCH --error=julia_%j.err

# ── Load required modules ────────────────────────────────────────────
module load intel/2020
module load intelmpi/2019

# ── Binary location ──────────────────────────────────────────────────
JULIA=./julia_mpi

# ─────────────────────────────────────────────────────────────────────
#  Generate all benchmark / poster cases
#  Format: mpirun -np RANKS ./julia_mpi -f FAMILY -c1 CR -c2 CI \
#                             -g GRID_SIZE -m MAX_ITER -o OUTFILE
# ─────────────────────────────────────────────────────────────────────

echo "=== Julia MPI batch run  $(date) ==="

# ── Debugging / benchmark cases (page 91 of Devaney) ─────────────────
# c = 0  (should produce a disk)
mpirun -np 32 $JULIA -f quad -c1 0.0    -c2  0.0    -g 2000 -m 200 -o j_c0.bin

# c = -1
mpirun -np 32 $JULIA -f quad -c1 -1.0   -c2  0.0    -g 2000 -m 200 -o j_cm1.bin

# c = 0.3 - 0.4i
mpirun -np 32 $JULIA -f quad -c1  0.3   -c2 -0.4    -g 2000 -m 200 -o j_03m04.bin

# c = 0.360284 + 0.100376i
mpirun -np 32 $JULIA -f quad -c1  0.360284 -c2 0.100376 -g 2000 -m 200 -o j_san_marco.bin

# c = -0.1 + 0.8i
mpirun -np 32 $JULIA -f quad -c1 -0.1  -c2  0.8    -g 2000 -m 200 -o j_m01_08.bin

# ── Additional visually interesting quadratic Julia sets ───────────────
# Douady rabbit
mpirun -np 32 $JULIA -f quad -c1 -0.12  -c2  0.74   -g 2000 -m 200 -o j_rabbit.bin

# Dendrite
mpirun -np 32 $JULIA -f quad -c1  0.0   -c2  1.0    -g 2000 -m 200 -o j_dendrite.bin

# Airplane
mpirun -np 32 $JULIA -f quad -c1 -1.755 -c2  0.0    -g 2000 -m 200 -o j_airplane.bin

# Siegel disk
mpirun -np 32 $JULIA -f quad -c1 -0.391 -c2 -0.587  -g 2000 -m 200 -o j_siegel.bin

# ── Cubic Julia sets: Tc(z) = z^3 + c ────────────────────────────────
mpirun -np 32 $JULIA -f cubic -c1 0.0   -c2  0.0    -g 2000 -m 200 -o j_cubic_c0.bin
mpirun -np 32 $JULIA -f cubic -c1 0.5   -c2  0.5    -g 2000 -m 200 -o j_cubic_05_05.bin
mpirun -np 32 $JULIA -f cubic -c1 -0.5  -c2  0.2    -g 2000 -m 200 -o j_cubic_m05_02.bin

echo "=== All computations complete  $(date) ==="
