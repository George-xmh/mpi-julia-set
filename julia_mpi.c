/*
 * julia_mpi.c
 * -----------
 * MPI-parallel computation of filled Julia sets for
 *   Qc(z) = z^2 + c   (quadratic family)
 *   Tc(z) = z^3 + c   (cubic family)
 *
 * Each grid point z0 is iterated up to MAX_ITER times.
 * The escape-time (first iterate that leaves |z| > ESCAPE_RADIUS) is
 * recorded and stored in a flat binary file for the OpenGL renderer.
 *
 * Parallelisation strategy  (load-balanced row distribution)
 * -----------------------------------------------------------
 * Julia-set rows near the boundary of the filled set are far more
 * expensive than rows that lie entirely inside or entirely outside,
 * because only boundary points require close-to-MAX_ITER iterations.
 * A naïve block decomposition would give process 0 all the cheap top
 * rows and process N-1 all the expensive middle rows.
 *
 * We use *cyclic row distribution*: process p owns rows
 *   p, p+size, p+2*size, ...
 * This interleaves cheap and expensive rows across all ranks and
 * achieves near-perfect empirical balance without dynamic scheduling.
 *
 * Compile (on SHARCNET / any MPI cluster):
 *   mpicc -O2 -o julia_mpi julia_mpi.c -lm
 *
 * Run (example – 4 MPI ranks, quadratic Julia, c = -0.1 + 0.8i):
 *   mpirun -np 4 ./julia_mpi -f quad -c1 -0.1 -c2 0.8 \
 *          -g 2000 -m 200 -o julia_quad_m01_08.bin
 *
 * Output binary format (little-endian):
 *   int32  grid_size          (N)
 *   int32  max_iter           (M)
 *   int32  escape_times[N*N]  (row-major; value == M means "inside set")
 *
 * Authors : [Your Name]
 * Course  : [Course Code]
 * Date    : [Date]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <mpi.h>

/* ------------------------------------------------------------------ */
/*  Constants                                                           */
/* ------------------------------------------------------------------ */
#define DEFAULT_GRID      1000      /* grid dimension (N × N)          */
#define DEFAULT_MAX_ITER  200       /* maximum iterations per point     */
#define DEFAULT_OUTFILE   "julia.bin"
#define ESCAPE_RADIUS_SQ  4.0      /* |z|^2 > 4  ⟹  escaped          */
#define VIEW_RADIUS       2.0      /* viewport: [-2,2] × [-2,2]        */

/* ------------------------------------------------------------------ */
/*  Supported Julia-set families                                        */
/* ------------------------------------------------------------------ */
typedef enum { QUAD, CUBIC } FamilyType;

/* ------------------------------------------------------------------ */
/*  Single-point iteration – quadratic Qc(z) = z^2 + c                */
/*  Returns escape time (1-based); returns max_iter if bounded.        */
/* ------------------------------------------------------------------ */
static inline int iterate_quad(double zr, double zi,
                                double cr, double ci,
                                int max_iter)
{
    int k;
    double zr2, zi2, tmp;
    for (k = 0; k < max_iter; k++) {
        zr2 = zr * zr;
        zi2 = zi * zi;
        if (zr2 + zi2 > ESCAPE_RADIUS_SQ) return k + 1;  /* escaped   */
        tmp = 2.0 * zr * zi + ci;
        zr  = zr2 - zi2 + cr;
        zi  = tmp;
    }
    return max_iter;  /* inside filled Julia set                        */
}

/* ------------------------------------------------------------------ */
/*  Single-point iteration – cubic Tc(z) = z^3 + c                    */
/*  z^3 = (zr + i·zi)^3  =  zr^3 - 3·zr·zi^2  +  i·(3·zr^2·zi - zi^3) */
/* ------------------------------------------------------------------ */
static inline int iterate_cubic(double zr, double zi,
                                 double cr, double ci,
                                 int max_iter)
{
    int k;
    double zr2, zi2, zr3, zi3, tmp_r, tmp_i;
    for (k = 0; k < max_iter; k++) {
        zr2 = zr * zr;
        zi2 = zi * zi;
        if (zr2 + zi2 > ESCAPE_RADIUS_SQ) return k + 1;
        /* compute z^3 */
        zr3   = zr * zr2 - 3.0 * zr * zi2;
        zi3   = 3.0 * zr2 * zi - zi * zi2;
        tmp_r = zr3 + cr;
        tmp_i = zi3 + ci;
        zr    = tmp_r;
        zi    = tmp_i;
    }
    return max_iter;
}

/* ------------------------------------------------------------------ */
/*  Print usage string                                                  */
/* ------------------------------------------------------------------ */
static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  -f  quad|cubic     Julia-set family      (default: quad)\n"
        "  -c1 <real>         Real part of c        (default: 0.0)\n"
        "  -c2 <imag>         Imaginary part of c   (default: 0.0)\n"
        "  -g  <int>          Grid size N (NxN)     (default: %d)\n"
        "  -m  <int>          Max iterations        (default: %d)\n"
        "  -o  <file>         Output binary file    (default: %s)\n",
        prog, DEFAULT_GRID, DEFAULT_MAX_ITER, DEFAULT_OUTFILE);
}

/* ================================================================== */
/*  MAIN                                                                */
/* ================================================================== */
int main(int argc, char *argv[])
{
    /* ---- MPI initialisation ---------------------------------------- */
    int rank, size;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    /* ---- Default parameters ---------------------------------------- */
    FamilyType family  = QUAD;
    double cr          = 0.0,  ci = 0.0;
    int    grid_size   = DEFAULT_GRID;
    int    max_iter    = DEFAULT_MAX_ITER;
    char   outfile[256];
    strncpy(outfile, DEFAULT_OUTFILE, sizeof(outfile));

    /* ---- Parse command-line arguments (all ranks read them) --------- */
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

    /* ---- Sanity-check |c| ≤ 2 for quadratic family ----------------- */
    if (family == QUAD && cr*cr + ci*ci > 4.0 + 1e-9) {
        if (rank == 0)
            fprintf(stderr, "Warning: |c| > 2; Julia set may be empty.\n");
    }

    /* ---- Print run parameters (rank 0 only) ------------------------- */
    if (rank == 0) {
        printf("Julia MPI  –  family=%s  c=%g%+gi  grid=%dx%d  "
               "max_iter=%d  ranks=%d\n",
               (family == QUAD ? "quad" : "cubic"),
               cr, ci, grid_size, grid_size, max_iter, size);
        fflush(stdout);
    }

    /* ---- Timing ------------------------------------------------------- */
    double t_start = MPI_Wtime();

    /* ---- Coordinate mapping ------------------------------------------ */
    /* pixel (col, row) → complex z = x + iy, with:
       x = -VIEW_RADIUS + col * (2*VIEW_RADIUS / (grid_size-1))
       y =  VIEW_RADIUS - row * (2*VIEW_RADIUS / (grid_size-1))
       (row 0 = top of image ↔ y = +2)                                  */
    double step = 2.0 * VIEW_RADIUS / (double)(grid_size - 1);

    /* ---- Local storage for rows owned by this rank ------------------- */
    /* Cyclic distribution: rank p owns rows p, p+size, p+2*size, ...    */
    int local_rows = 0;
    for (int r = rank; r < grid_size; r += size) local_rows++;

    /* Each local row stores grid_size escape-time values (int32).        */
    int *local_data = (int *)malloc((size_t)local_rows * grid_size * sizeof(int));
    if (!local_data) {
        fprintf(stderr, "Rank %d: malloc failed\n", rank);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    /* ---- Compute escape times for local rows -------------------------- */
    int local_idx = 0;           /* next slot in local_data              */
    for (int r = rank; r < grid_size; r += size) {
        double yi = VIEW_RADIUS - r * step;   /* imaginary part of z0    */
        for (int col = 0; col < grid_size; col++) {
            double xr = -VIEW_RADIUS + col * step;   /* real part of z0  */
            int et;
            if (family == QUAD)
                et = iterate_quad (xr, yi, cr, ci, max_iter);
            else
                et = iterate_cubic(xr, yi, cr, ci, max_iter);
            local_data[local_idx++] = et;
        }
    }

    /* ---- Gather results to rank 0 ------------------------------------- */
    /*
     * We need to reconstruct the full N×N grid in row-major order on
     * rank 0.  The tricky part is that each rank owns non-contiguous
     * rows (stride = size).  We use MPI_Gatherv so that rank 0 can
     * receive variable-length contributions from each rank.
     *
     * However, re-interleaving the rows requires an extra permutation
     * step on rank 0.  We therefore:
     *   1. Gather all local buffers into one large buffer on rank 0
     *      (rows from rank 0 first, then rank 1, etc.)
     *   2. Permute that buffer into the correct row-major order.
     */

    /* Compute send counts and displacements for Gatherv */
    int *recv_counts = NULL;
    int *displs      = NULL;
    int *gathered    = NULL;   /* rank 0's receive buffer                */

    if (rank == 0) {
        recv_counts = (int *)malloc(size * sizeof(int));
        displs      = (int *)malloc(size * sizeof(int));
        gathered    = (int *)malloc((size_t)grid_size * grid_size * sizeof(int));
        if (!recv_counts || !displs || !gathered) {
            fprintf(stderr, "Rank 0: malloc failed (gather buffers)\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        /* Rows per rank (cyclic): rank p owns ceil((N-p)/size) rows     */
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
                gathered,   recv_counts, displs, MPI_INT,
                0, MPI_COMM_WORLD);

    free(local_data);

    /* ---- Rank 0: permute gathered rows back into row-major order ----- */
    if (rank == 0) {
        int *full = (int *)malloc((size_t)grid_size * grid_size * sizeof(int));
        if (!full) {
            fprintf(stderr, "Rank 0: malloc failed (full grid)\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        /* gathered layout:
           [rows of rank 0] [rows of rank 1] ... [rows of rank size-1]
           rows of rank p are:  p, p+size, p+2*size, ...              */
        int *ptr = gathered;
        for (int p = 0; p < size; p++) {
            int row = p;
            while (row < grid_size) {
                memcpy(full + (size_t)row * grid_size,
                       ptr,
                       grid_size * sizeof(int));
                ptr += grid_size;
                row += size;
            }
        }
        free(gathered);
        free(recv_counts);
        free(displs);

        /* ---- Write binary output file -------------------------------- */
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
        printf("Done.  Output → %s  (%.2f s wall time)\n",
               outfile, t_end - t_start);
    }

    MPI_Finalize();
    return 0;
}
