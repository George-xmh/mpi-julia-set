/*
 * julia_mpi.c
 * Computes filled Julia sets in parallel using MPI.
 * Supports Qc(z) = z^2 + c  and  Tc(z) = z^3 + c.
 *
 * Each point on an NxN grid is iterated up to max_iter times.
 * If the orbit escapes |z| > 2 we record which iteration it escaped on.
 * If it never escapes, the point is inside the filled Julia set.
 *
 * Load balancing: instead of giving each rank a contiguous block of rows
 * (which would dump all the expensive boundary rows onto one rank),
 * we use cyclic distribution: rank p gets rows p, p+size, p+2*size, ...
 * This spreads cheap and expensive rows evenly across all ranks.
 *
 * Output is a binary file: grid_size, max_iter, then NxN escape times.
 * This file is read by julia_render.c to produce the image.
 *
 * Compile:  mpicc -O2 -o julia_mpi julia_mpi.c -lm
 * Run:      mpirun -np 4 ./julia_mpi -f quad -c1 -0.1 -c2 0.8 -g 1000 -m 200 -o out.bin
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <mpi.h>

#define DEFAULT_GRID      1000
#define DEFAULT_MAX_ITER  200
#define DEFAULT_OUTFILE   "julia.bin"
#define ESCAPE_RADIUS_SQ  4.0   /* |z|^2 > 4 means the point escaped */
#define VIEW_RADIUS       2.0   /* view window is [-2,2] x [-2,2] */

typedef enum { QUAD, CUBIC } FamilyType;

/* iterate z -> z^2 + c, return which iteration |z| exceeded 2.
   if the orbit stays bounded the whole time, return max_iter. */
static inline int iterate_quad(double zr, double zi,
                                double cr, double ci,
                                int max_iter)
{
    int k;
    double zr2, zi2, tmp;
    for (k = 0; k < max_iter; k++) {
        zr2 = zr * zr;
        zi2 = zi * zi;
        if (zr2 + zi2 > ESCAPE_RADIUS_SQ) return k + 1;
        tmp = 2.0 * zr * zi + ci;
        zr  = zr2 - zi2 + cr;
        zi  = tmp;
    }
    return max_iter;
}

/* same but for z -> z^3 + c
   using: (a+bi)^3 = a^3 - 3ab^2 + i(3a^2b - b^3) */
static inline int iterate_cubic(double zr, double zi,
                                 double cr, double ci,
                                 int max_iter)
{
    int k;
    double zr2, zi2, zr3, zi3, tmp_r, tmp_i;
    for (k = 0; k < max_iter; k++) {
        zr2   = zr * zr;
        zi2   = zi * zi;
        if (zr2 + zi2 > ESCAPE_RADIUS_SQ) return k + 1;
        zr3   = zr * zr2 - 3.0 * zr * zi2;
        zi3   = 3.0 * zr2 * zi - zi * zi2;
        tmp_r = zr3 + cr;
        tmp_i = zi3 + ci;
        zr    = tmp_r;
        zi    = tmp_i;
    }
    return max_iter;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  -f  quad|cubic   which Julia family    (default: quad)\n"
        "  -c1 <real>       real part of c        (default: 0.0)\n"
        "  -c2 <imag>       imaginary part of c   (default: 0.0)\n"
        "  -g  <int>        grid size N (NxN)     (default: %d)\n"
        "  -m  <int>        max iterations        (default: %d)\n"
        "  -o  <file>       output file           (default: %s)\n",
        prog, DEFAULT_GRID, DEFAULT_MAX_ITER, DEFAULT_OUTFILE);
}

int main(int argc, char *argv[])
{
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* defaults */
    FamilyType family = QUAD;
    double cr = 0.0, ci = 0.0;
    int grid_size = DEFAULT_GRID;
    int max_iter  = DEFAULT_MAX_ITER;
    char outfile[256];
    strncpy(outfile, DEFAULT_OUTFILE, sizeof(outfile));

    /* all ranks parse arguments so everyone has the same parameters */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 && i+1 < argc) {
            if (strcmp(argv[++i], "cubic") == 0) family = CUBIC;
        } else if (strcmp(argv[i], "-c1") == 0 && i+1 < argc) {
            cr = atof(argv[++i]);
        } else if (strcmp(argv[i], "-c2") == 0 && i+1 < argc) {
            ci = atof(argv[++i]);
        } else if (strcmp(argv[i], "-g") == 0 && i+1 < argc) {
            grid_size = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-m") == 0 && i+1 < argc) {
            max_iter = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-o") == 0 && i+1 < argc) {
            strncpy(outfile, argv[++i], sizeof(outfile));
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            if (rank == 0) usage(argv[0]);
            MPI_Finalize();
            return 0;
        } else {
            if (rank == 0)
                fprintf(stderr, "Unknown argument: %s\n", argv[i]);
        }
    }

    if (family == QUAD && cr*cr + ci*ci > 4.0 + 1e-9)
        if (rank == 0)
            fprintf(stderr, "Warning: |c| > 2, Julia set may be empty.\n");

    if (rank == 0) {
        printf("family=%s  c=%g%+gi  grid=%dx%d  max_iter=%d  ranks=%d\n",
               (family == QUAD ? "quad" : "cubic"),
               cr, ci, grid_size, grid_size, max_iter, size);
        fflush(stdout);
    }

    double t_start = MPI_Wtime();

    /* convert pixel (col, row) to complex point x + iy
       row 0 is top of the image = y=+2, bottom row = y=-2 */
    double step = 2.0 * VIEW_RADIUS / (double)(grid_size - 1);

    /* cyclic row distribution: rank p owns rows p, p+size, p+2*size, ... */
    int local_rows = 0;
    for (int r = rank; r < grid_size; r += size) local_rows++;

    int *local_data = (int *)malloc((size_t)local_rows * grid_size * sizeof(int));
    if (!local_data) {
        fprintf(stderr, "Rank %d: malloc failed\n", rank);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    /* compute the escape time for every point in our assigned rows */
    int local_idx = 0;
    for (int r = rank; r < grid_size; r += size) {
        double yi = VIEW_RADIUS - r * step;         /* imaginary part */
        for (int col = 0; col < grid_size; col++) {
            double xr = -VIEW_RADIUS + col * step;  /* real part */
            int et;
            if (family == QUAD)
                et = iterate_quad(xr, yi, cr, ci, max_iter);
            else
                et = iterate_cubic(xr, yi, cr, ci, max_iter);
            local_data[local_idx++] = et;
        }
    }

    /* gather everything to rank 0.
       rows arrive grouped by rank, not in image order, so rank 0
       will re-sort them into the correct top-to-bottom order after. */
    int *recv_counts = NULL;
    int *displs      = NULL;
    int *gathered    = NULL;

    if (rank == 0) {
        recv_counts = (int *)malloc(size * sizeof(int));
        displs      = (int *)malloc(size * sizeof(int));
        gathered    = (int *)malloc((size_t)grid_size * grid_size * sizeof(int));
        if (!recv_counts || !displs || !gathered) {
            fprintf(stderr, "Rank 0: malloc failed\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        int disp = 0;
        for (int p = 0; p < size; p++) {
            int nrows_p = 0;
            for (int r = p; r < grid_size; r += size) nrows_p++;
            recv_counts[p] = nrows_p * grid_size;
            displs[p]      = disp;
            disp          += recv_counts[p];
        }
    }

    MPI_Gatherv(local_data, local_rows * grid_size, MPI_INT,
                gathered, recv_counts, displs, MPI_INT,
                0, MPI_COMM_WORLD);

    free(local_data);

    if (rank == 0) {
        /* put rows back in correct order.
           gathered buffer has all of rank 0's rows, then rank 1's, etc.
           we copy each row into its correct position in the full grid. */
        int *full = (int *)malloc((size_t)grid_size * grid_size * sizeof(int));
        if (!full) {
            fprintf(stderr, "Rank 0: malloc failed\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        int *ptr = gathered;
        for (int p = 0; p < size; p++) {
            int row = p;
            while (row < grid_size) {
                memcpy(full + (size_t)row * grid_size, ptr, grid_size * sizeof(int));
                ptr += grid_size;
                row += size;
            }
        }
        free(gathered);
        free(recv_counts);
        free(displs);

        /* write binary output: grid_size, max_iter, then NxN escape times */
        FILE *fp = fopen(outfile, "wb");
        if (!fp) {
            perror("fopen output file");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        fwrite(&grid_size, sizeof(int), 1, fp);
        fwrite(&max_iter,  sizeof(int), 1, fp);
        fwrite(full, sizeof(int), (size_t)grid_size * grid_size, fp);
        fclose(fp);
        free(full);

        double t_end = MPI_Wtime();
        printf("done. output: %s  (%.2f s)\n", outfile, t_end - t_start);
    }

    MPI_Finalize();
    return 0;
}
