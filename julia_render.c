/*
 * julia_render.c
 * --------------
 * OpenGL / GLUT renderer for Julia-set binary files produced by
 * julia_mpi.c.  Reads the escape-time grid and maps each escape-time
 * band to an RGB colour, then saves a high-resolution JPEG via libjpeg.
 *
 * Colour scheme (10 bands of 5 iterations each, then "inside" = black):
 *   band 0  : iter  1– 5   → warm red
 *   band 1  : iter  6–10   → orange
 *   band 2  : iter 11–15   → yellow-gold
 *   band 3  : iter 16–20   → lime green
 *   band 4  : iter 21–25   → teal
 *   band 5  : iter 26–30   → sky blue
 *   band 6  : iter 31–35   → medium blue
 *   band 7  : iter 36–40   → violet
 *   band 8  : iter 41–45   → magenta
 *   band 9  : iter 46–50   → pink
 *   ≥ 50 / max_iter (inside) → black
 *
 * Compile:
 *   gcc -O2 -o julia_render julia_render.c \
 *       -lGL -lGLU -lglut -ljpeg -lm
 *
 * Run:
 *   ./julia_render -i julia.bin -o julia.jpg [-w 1000] [-h 1000]
 *
 * Authors : [Your Name]
 * Course  : [Course Code]
 * Date    : [Date]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef __APPLE__
#  include <OpenGL/gl.h>
#  include <OpenGL/glu.h>
#  include <GLUT/glut.h>
#else
#  include <GL/gl.h>
#  include <GL/glu.h>
#  include <GL/glut.h>
#endif

#include <jpeglib.h>

/* ------------------------------------------------------------------ */
/*  Colour table – 10 escape bands + 1 "inside" colour                */
/* ------------------------------------------------------------------ */
#define NUM_BANDS 10

/* Each row: { R, G, B } in [0,255] */
static const unsigned char BAND_COLORS[NUM_BANDS][3] = {
    { 220,  30,  30 },   /* band 0:  1– 5  warm red        */
    { 240, 120,   0 },   /* band 1:  6–10  orange           */
    { 230, 210,   0 },   /* band 2: 11–15  yellow-gold      */
    {  80, 200,  50 },   /* band 3: 16–20  lime green       */
    {   0, 180, 140 },   /* band 4: 21–25  teal             */
    {  30, 160, 240 },   /* band 5: 26–30  sky blue         */
    {  40,  80, 220 },   /* band 6: 31–35  medium blue      */
    { 130,  50, 200 },   /* band 7: 36–40  violet           */
    { 210,  30, 180 },   /* band 8: 41–45  magenta          */
    { 255, 130, 180 },   /* band 9: 46–50  pink             */
};

static const unsigned char INSIDE_COLOR[3] = { 0, 0, 0 };  /* black   */
#define BAND_WIDTH 5     /* iterations per colour band                  */

/* ------------------------------------------------------------------ */
/*  Global state                                                        */
/* ------------------------------------------------------------------ */
static int   *g_escape  = NULL;  /* flat N×N escape-time array          */
static int    g_N       = 0;     /* grid dimension                      */
static int    g_maxiter = 0;     /* maximum iteration count             */
static int    g_win_w   = 1000;  /* OpenGL window width  (pixels)       */
static int    g_win_h   = 1000;  /* OpenGL window height (pixels)       */
static char   g_outjpg[256] = "julia_out.jpg";

/* ------------------------------------------------------------------ */
/*  Map escape time → RGB                                              */
/* ------------------------------------------------------------------ */
static void escape_to_rgb(int et, unsigned char *r,
                           unsigned char *g, unsigned char *b)
{
    if (et >= g_maxiter) {          /* inside the filled Julia set      */
        *r = INSIDE_COLOR[0];
        *g = INSIDE_COLOR[1];
        *b = INSIDE_COLOR[2];
        return;
    }
    /* zero-based band index (clamp to last band if beyond table)       */
    int band = (et - 1) / BAND_WIDTH;
    if (band >= NUM_BANDS) band = NUM_BANDS - 1;

    *r = BAND_COLORS[band][0];
    *g = BAND_COLORS[band][1];
    *b = BAND_COLORS[band][2];
}

/* ------------------------------------------------------------------ */
/*  Build an RGB pixel buffer from the escape-time grid                */
/*  Output: width × height × 3 bytes (row 0 = top of image)           */
/* ------------------------------------------------------------------ */
static unsigned char *build_rgb_buffer(int width, int height)
{
    unsigned char *buf = (unsigned char *)malloc(
                             (size_t)width * height * 3);
    if (!buf) return NULL;

    for (int py = 0; py < height; py++) {
        /* Map pixel row py to grid row gy (nearest-neighbour scaling)  */
        int gy = (int)((double)py / height * g_N);
        if (gy >= g_N) gy = g_N - 1;

        for (int px = 0; px < width; px++) {
            int gx = (int)((double)px / width * g_N);
            if (gx >= g_N) gx = g_N - 1;

            int et = g_escape[gy * g_N + gx];
            unsigned char r, g, b;
            escape_to_rgb(et, &r, &g, &b);

            size_t off = ((size_t)py * width + px) * 3;
            buf[off + 0] = r;
            buf[off + 1] = g;
            buf[off + 2] = b;
        }
    }
    return buf;
}

/* ------------------------------------------------------------------ */
/*  Save RGB buffer to JPEG via libjpeg                                */
/* ------------------------------------------------------------------ */
static int save_jpeg(const char *filename,
                     unsigned char *data,
                     int width, int height, int quality)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr       jerr;
    FILE *fp;
    JSAMPROW row_pointer;

    fp = fopen(filename, "wb");
    if (!fp) { perror("fopen jpeg"); return -1; }

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, fp);

    cinfo.image_width      = (JDIMENSION)width;
    cinfo.image_height     = (JDIMENSION)height;
    cinfo.input_components = 3;
    cinfo.in_color_space   = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer = data + (size_t)cinfo.next_scanline * width * 3;
        jpeg_write_scanlines(&cinfo, &row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    fclose(fp);
    jpeg_destroy_compress(&cinfo);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  GLUT display callback                                              */
/* ------------------------------------------------------------------ */
static void display(void)
{
    glClear(GL_COLOR_BUFFER_BIT);

    unsigned char *rgb = build_rgb_buffer(g_win_w, g_win_h);
    if (!rgb) { fprintf(stderr, "build_rgb_buffer failed\n"); return; }

    /* Draw as a single OpenGL texture quad (fastest path)             */
    glRasterPos2i(0, 0);
    /* glDrawPixels origin is bottom-left; our buffer is top-left       */
    /* → flip vertically when drawing                                   */
    glPixelZoom(1.0f, -1.0f);
    glRasterPos2i(0, g_win_h - 1);
    glDrawPixels(g_win_w, g_win_h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    glFlush();

    /* Save JPEG */
    printf("Saving %s …\n", g_outjpg);
    if (save_jpeg(g_outjpg, rgb, g_win_w, g_win_h, 95) == 0)
        printf("Saved  %s  (%d × %d)\n", g_outjpg, g_win_w, g_win_h);

    free(rgb);
}

/* ------------------------------------------------------------------ */
/*  GLUT keyboard callback – press 'q' or ESC to quit                  */
/* ------------------------------------------------------------------ */
static void keyboard(unsigned char key, int x, int y)
{
    (void)x; (void)y;
    if (key == 27 || key == 'q' || key == 'Q') {
        free(g_escape);
        exit(0);
    }
}

/* ------------------------------------------------------------------ */
/*  Read binary file written by julia_mpi.c                           */
/* ------------------------------------------------------------------ */
static int read_binary(const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp) { perror("fopen input"); return -1; }

    if (fread(&g_N,       sizeof(int), 1, fp) != 1 ||
        fread(&g_maxiter, sizeof(int), 1, fp) != 1) {
        fprintf(stderr, "read_binary: header read failed\n");
        fclose(fp); return -1;
    }
    printf("Reading grid %d × %d  (max_iter=%d) from '%s'\n",
           g_N, g_N, g_maxiter, filename);

    g_escape = (int *)malloc((size_t)g_N * g_N * sizeof(int));
    if (!g_escape) { fclose(fp); return -1; }

    size_t n = (size_t)g_N * g_N;
    if (fread(g_escape, sizeof(int), n, fp) != n) {
        fprintf(stderr, "read_binary: data read failed\n");
        free(g_escape); g_escape = NULL;
        fclose(fp); return -1;
    }
    fclose(fp);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Usage                                                              */
/* ------------------------------------------------------------------ */
static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s -i <input.bin> [-o <output.jpg>] [-w <width>] [-H <height>]\n"
        "  -i  input binary file (from julia_mpi)\n"
        "  -o  output JPEG file  (default: julia_out.jpg)\n"
        "  -w  image width  in pixels (default: 1000)\n"
        "  -H  image height in pixels (default: 1000)\n",
        prog);
}

/* ================================================================== */
/*  MAIN                                                                */
/* ================================================================== */
int main(int argc, char *argv[])
{
    char infile[256] = "";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i+1 < argc)
            strncpy(infile, argv[++i], sizeof(infile));
        else if (strcmp(argv[i], "-o") == 0 && i+1 < argc)
            strncpy(g_outjpg, argv[++i], sizeof(g_outjpg));
        else if (strcmp(argv[i], "-w") == 0 && i+1 < argc)
            g_win_w = atoi(argv[++i]);
        else if (strcmp(argv[i], "-H") == 0 && i+1 < argc)
            g_win_h = atoi(argv[++i]);
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i],"--help") == 0) {
            usage(argv[0]); return 0;
        }
    }

    if (infile[0] == '\0') {
        fprintf(stderr, "Error: no input file specified.\n");
        usage(argv[0]); return 1;
    }

    if (read_binary(infile) != 0) return 1;

    /* ---- GLUT setup ------------------------------------------------ */
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_SINGLE | GLUT_RGB);
    glutInitWindowSize(g_win_w, g_win_h);
    glutCreateWindow("Julia Set Renderer");

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0, g_win_w, 0, g_win_h);
    glMatrixMode(GL_MODELVIEW);

    glutDisplayFunc(display);
    glutKeyboardFunc(keyboard);
    glutMainLoop();

    return 0;
}
