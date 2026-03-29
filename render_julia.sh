#!/bin/bash
#SBATCH --job-name=julia_render
#SBATCH --account=def-yourpi
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=0-00:15:00
#SBATCH --output=render_%j.out

# ── Load required modules ────────────────────────────────────────────
module load freeglut
module load libjpeg-turbo

# Allow headless OpenGL rendering via Mesa / Xvfb
module load mesa
export DISPLAY=:99
Xvfb :99 -screen 0 2000x2000x24 &
sleep 2

RENDER=./julia_render
W=2000; H=2000

for BIN in j_*.bin; do
    JPG="${BIN%.bin}.jpg"
    echo "Rendering $BIN → $JPG"
    $RENDER -i "$BIN" -o "$JPG" -w $W -H $H
done

echo "Rendering complete."
