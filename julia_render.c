/*
 * julia_render.c
 * George [Last Name] - [Course Code] - [Date]
 *
 * Reads the binary file produced by julia_mpi.c and renders it
 * as a colour image using OpenGL, then saves it as a JPEG.
 *
 * Colour scheme: escape time is split into bands of 5 iterations each.
 * Points that never escape (inside the Julia set) are coloured black.
 * The 10 bands go from red (escaped quickly) to pink (escaped slowly).
 *
 * Compile:  gcc -O2 -o julia_render julia_render.c -lGL -lGLU -lglut -ljpeg -lm
 * Run:      ./julia_render -i julia.bin -o julia.jpg -w 2000 -H 2000
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

#define NUM_BANDS  10
#define BAND_WIDTH  5   /* iterations per colour band */

/* RGB colours for each escape-time band, fastest escape first */
static const unsigned char BAND_COLORS[NUM_BANDS][3] = {
    { 220,  30,  30 },   /* 1-5    red    */
    { 240, 120,   0 },   /* 6-10   orange */
    { 230, 210,   0 },   /* 11-15  yellow */
    {  80, 200,  50 },   /* 16-20  green  */
    {   0, 180, 140 },   /* 21-25  teal   */
    {  30, 160, 240 },   /* 26-30  sky    */
    {  40,  80, 220 },   /* 31-35  blue   */
    { 130,  50, 200 },   /* 36-40  violet */
    { 210,  30, 180 },   /* 41-45  magenta*/
    { 255, 130, 180 },   /* 46-50  pink   */
};

static const unsigned char INSIDE_COLOR[3] = { 0, 0, 0 };  /* black = inside set */

/* globals loaded from the binary file */
static int  *g_escape  = NULL;
static int   g_N       = 0;
static int   g_maxiter = 0;

/* output settings */
static int   g_win_w = 1000;
static int   g_win_h = 1000;
static char  g_outjpg[256] = "julia_out.jpg";

/* pick the right colour for a given escape time */
static void escape_to_rgb(int et, unsigned char *r,
                           unsigned char *g, unsigned char *b)
{
    if (et >= g_maxiter) {
        /* point stayed bounded — inside the filled Julia set */
        *r = INSIDE_COLOR[0];
        *g = INSIDE_COLOR[1];
        *b = INSIDE_COLOR[2];
        return;
    }
    int band = (et - 1) / BAND_WIDTH;
    if (band >= NUM_BANDS) band = NUM_BANDS - 1;
    *r = BAND_COLORS[band][0];
    *g = BAND_COLORS[band][1];
    *b = BAND_COLORS[band][2];
}

/* build a width x height RGB buffer by scaling the NxN grid to fit */
static unsigned char *build_rgb_buffer(int width, int height)
{
    unsigned char *buf = (unsigned char *)malloc((size_t)width * height * 3);
    if (!buf) return NULL;

    for (int py = 0; py < height; py++) {
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

/* write the RGB buffer to a JPEG file using libjpeg */
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

/* GLUT calls this to draw the window — we also save the JPEG here */
static void display(void)
{
    glClear(GL_COLOR_BUFFER_BIT);

    unsigned char *rgb = build_rgb_buffer(g_win_w, g_win_h);
    if (!rgb) { fprintf(stderr, "build_rgb_buffer failed\n"); return; }

    /* glDrawPixels starts from bottom-left, our buffer is top-left,
       so flip vertically with glPixelZoom */
    glPixelZoom(1.0f, -1.0f);
    glRasterPos2i(0, g_win_h - 1);
    glDrawPixels(g_win_w, g_win_h, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    glFlush();

    printf("Saving %s ...\n", g_outjpg);
    if (save_jpeg(g_outjpg, rgb, g_win_w, g_win_h, 95) == 0)
        printf("Saved %s (%d x %d)\n", g_outjpg, g_win_w, g_win_h);

    free(rgb);
}

/* press q or ESC to exit */
static void keyboard(unsigned char key, int x, int y)
{
    (void)x; (void)y;
    if (key == 27 || key == 'q' || key == 'Q') {
        free(g_escape);
        exit(0);
    }
}

/* read the binary file written by julia_mpi.c */
static int read_binary(const char *filename)
{
    FILE *fp = fopen(filename, "rb");
    if (!fp) { perror("fopen input"); return -1; }

    if (fread(&g_N,       sizeof(int), 1, fp) != 1 ||
        fread(&g_maxiter, sizeof(int), 1, fp) != 1) {
        fprintf(stderr, "failed to read header\n");
        fclose(fp);
        return -1;
    }
    printf("reading %d x %d grid (max_iter=%d) from %s\n",
           g_N, g_N, g_maxiter, filename);

    g_escape = (int *)malloc((size_t)g_N * g_N * sizeof(int));
    if (!g_escape) { fclose(fp); return -1; }

    size_t n = (size_t)g_N * g_N;
    if (fread(g_escape, sizeof(int), n, fp) != n) {
        fprintf(stderr, "failed to read grid data\n");
        free(g_escape);
        g_escape = NULL;
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s -i <input.bin> [-o <output.jpg>] [-w <width>] [-H <height>]\n",
        prog);
}

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
        fprintf(stderr, "Error: no input file given.\n");
        usage(argv[0]);
        return 1;
    }

    if (read_binary(infile) != 0) return 1;

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
