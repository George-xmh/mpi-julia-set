#!/bin/bash
#SBATCH --job-name=julia_mpi
#SBATCH --account=def-yourprofname     # replace with your allocation
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=16           # 32 MPI ranks total
#SBATCH --cpus-per-task=1
#SBATCH --mem-per-cpu=512M
#SBATCH --time=0-00:30:00
#SBATCH --output=julia_%j.out
#SBATCH --error=julia_%j.err

module load intel/2020
module load intelmpi/2019

JULIA=./julia_mpi

echo "=== starting julia jobs: $(date) ==="

# test cases from Devaney p.91

# c=0 should produce a disk (basic sanity check)
mpirun -np 32 $JULIA -f quad -c1  0.0     -c2  0.0      -g 2000 -m 200 -o j_c0.bin

# c=-1
mpirun -np 32 $JULIA -f quad -c1 -1.0     -c2  0.0      -g 2000 -m 200 -o j_cm1.bin

# c=0.3-0.4i
mpirun -np 32 $JULIA -f quad -c1  0.3     -c2 -0.4      -g 2000 -m 200 -o j_03m04.bin

# c=0.360284+0.100376i (san marco)
mpirun -np 32 $JULIA -f quad -c1  0.360284 -c2  0.100376 -g 2000 -m 200 -o j_sanmarco.bin

# c=-0.1+0.8i
mpirun -np 32 $JULIA -f quad -c1 -0.1     -c2  0.8      -g 2000 -m 200 -o j_m01_08.bin

# additional interesting cases for the poster

# douady rabbit
mpirun -np 32 $JULIA -f quad -c1 -0.12    -c2  0.74     -g 2000 -m 200 -o j_rabbit.bin

# dendrite
mpirun -np 32 $JULIA -f quad -c1  0.0     -c2  1.0      -g 2000 -m 200 -o j_dendrite.bin

# airplane
mpirun -np 32 $JULIA -f quad -c1 -1.755   -c2  0.0      -g 2000 -m 200 -o j_airplane.bin

# siegel disk
mpirun -np 32 $JULIA -f quad -c1 -0.391   -c2 -0.587    -g 2000 -m 200 -o j_siegel.bin

# cubic family: Tc(z) = z^3 + c
mpirun -np 32 $JULIA -f cubic -c1  0.0    -c2  0.0      -g 2000 -m 200 -o j_cubic_c0.bin
mpirun -np 32 $JULIA -f cubic -c1  0.5    -c2  0.5      -g 2000 -m 200 -o j_cubic_05_05.bin
mpirun -np 32 $JULIA -f cubic -c1 -0.5    -c2  0.2      -g 2000 -m 200 -o j_cubic_m05_02.bin

echo "=== all done: $(date) ==="
