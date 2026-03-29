#!/bin/bash
#SBATCH --job-name=julia_render
#SBATCH --account=def-yourprofname
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=0-00:15:00
#SBATCH --output=render_%j.out

module load freeglut
module load libjpeg-turbo
module load mesa

# need a virtual display for headless OpenGL on the cluster
export DISPLAY=:99
Xvfb :99 -screen 0 2000x2000x24 &
sleep 2

RENDER=./julia_render
W=2000
H=2000

# render every .bin file to a JPEG
for BIN in j_*.bin; do
    JPG="${BIN%.bin}.jpg"
    echo "rendering $BIN -> $JPG"
    $RENDER -i "$BIN" -o "$JPG" -w $W -H $H
done

echo "done."
