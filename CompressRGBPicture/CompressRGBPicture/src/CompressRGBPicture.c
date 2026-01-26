/*****************************************************************************
 * CompressRGBPicture.c
 *****************************************************************************/
#include <sys/platform.h>
#include "adi_initialize.h"
#include "CompressRGBPicture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "CityLandscape.bmp.h"
#include <cycle_count.h>
#include <builtins.h>


cycle_t start_count;
cycle_t final_count;

// output text blocks
#pragma section("seg_sdram2")
 char textBlockR[150000];
#pragma section("seg_sdram3")
 char textBlockG[150000];
#pragma section("seg_sdram4")
 char textBlockB[150000];

 // block size 8x8
#define DCT_N 8
#define DCT_BLOCK_SZ (DCT_N * DCT_N)


#ifndef M_PI
#define M_PI_F 3.14159265358979323846f
#else
#define M_PI_F ((float)M_PI)
#endif

 //standard JPEG luminance quantization table
#pragma section("seg_pm_fast")
static const int default_qtable[DCT_N][DCT_N] = {
    {16, 11, 10, 16, 24, 40, 51, 61},
    {12, 12, 14, 19, 26, 58, 60, 55},
    {14, 13, 16, 24, 40, 57, 69, 56},
    {14, 17, 22, 29, 51, 87, 80, 62},
    {18, 22, 37, 56, 68,109,103, 77},
    {24, 35, 55, 64, 81,104,113, 92},
    {49, 64, 78, 87,103,121,120,101},
    {72, 92, 95, 98,112,100,103, 99}
};

//standard JPEG zig-zag order table
#pragma section("seg_pm_fast")
static const unsigned char zigzag_idx[64] = {
  0,  1,  5,  6, 14, 15, 27, 28,
  2,  4,  7, 13, 16, 26, 29, 42,
  3,  8, 12, 17, 25, 30, 41, 43,
  9, 11, 18, 24, 31, 40, 44, 53,
 10, 19, 23, 32, 39, 45, 52, 54,
 20, 22, 33, 38, 46, 51, 55, 60,
 21, 34, 37, 47, 50, 56, 59, 61,
 35, 36, 48, 49, 57, 58, 62, 63
};



#pragma section("seg_pm_fast")
static float DCT_C[DCT_N][DCT_N];
#pragma section("seg_pm_fast")
static float INV_Q[DCT_N][DCT_N];



//function initializes the DCT and quantization matrices
static void init_dct_and_invq(const int qtable[DCT_N][DCT_N])
{

    // init DCT matrix C
    for (int u = 0; u < DCT_N; ++u) {
        float alpha = (u == 0) ? sqrtf(1.0f / DCT_N) : sqrtf(2.0f / DCT_N);
        for (int x = 0; x < DCT_N; ++x) {
            DCT_C[u][x] = alpha * cosf((M_PI_F / (float)DCT_N) * ((float)x + 0.5f) * (float)u);
        }
    }

    // init invQ once from qtable
    for (int u = 0; u < DCT_N; ++u) {
        for (int v = 0; v < DCT_N; ++v) {
            int q = qtable[u][v];
            INV_Q[u][v] = 1.0f / (float)q;
        }
    }

}

/*
 * Split rgb array function
 * Manually unroll the loop to process 6 pixels per iteration and use 'restrict'
 * on rgb[], R[], G[], B[] to help compiler optimizations.
 */
void rgb_split(const unsigned char * restrict rgb,
               unsigned char * restrict R,
               unsigned char * restrict G,
               unsigned char * restrict B,
               int width, int height)
{
    int pixels = width * height;
    const unsigned char *p = rgb;
    int i = 0;

    for (; i + 5 < pixels; i += 6, p += 18) {
        int r0 = p[0],  g0 = p[1],  b0 = p[2];
        int r1 = p[3],  g1 = p[4],  b1 = p[5];
        int r2 = p[6],  g2 = p[7],  b2 = p[8];
        int r3 = p[9],  g3 = p[10], b3 = p[11];
        int r4 = p[12], g4 = p[13], b4 = p[14];
        int r5 = p[15], g5 = p[16], b5 = p[17];

        R[i + 0] = r0;
        R[i + 1] = r1;
        R[i + 2] = r2;
        R[i + 3] = r3;
        R[i + 4] = r4;
        R[i + 5] = r5;

        G[i + 0] = g0;
        G[i + 1] = g1;
        G[i + 2] = g2;
        G[i + 3] = g3;
        G[i + 4] = g4;
        G[i + 5] = g5;

        B[i + 0] = b0;
        B[i + 1] = b1;
        B[i + 2] = b2;
        B[i + 3] = b3;
        B[i + 4] = b4;
        B[i + 5] = b5;
    }

    // tail
    for (; i < pixels; ++i, p += 3) {
        R[i] = p[0];
        G[i] = p[1];
        B[i] = p[2];
    }
}

//Helper function used to write ints to the text block
static inline char* write_int(char *p, int v)
{
    if (v == 0) {
        *p++ = '0';
        return p;
    }

    if (v < 0) {
        *p++ = '-';
        v = -v;
    }

    char tmp[12];
    int n = 0;

    while (v > 0) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }

    while (n--)
        *p++ = tmp[n];

    return p;
}


/*
 * DCT and Quantization function
 * Perform an optimized 2D DCT on an 8×8 input block using a precomputed DCT matrix
 * Quantize the DCT coefficients by multiplying them with a precomputed
 * inverse quantization matrix avoiding float division(OPTIMIZED_DIVISON)
 */
 void dct_and_quant_block(const short * restrict blk_in, short * restrict out_quant)
{
    float f[DCT_N][DCT_N];
    float G[DCT_N][DCT_N];
    float F[DCT_N][DCT_N];

    // copy input block into float f[y][x]
    for (int y = 0; y < DCT_N; ++y)
        for (int x = 0; x < DCT_N; ++x)
            f[y][x] = (float) blk_in[y * DCT_N + x];

    // F = C * f * C^T

    // row-wise transform: G[y][u] = sum of f[y][x] * C[u][x]
    // G = f * C^T
    for (int y = 0; y < DCT_N; ++y) {
        for (int u = 0; u < DCT_N; ++u) {
            float s = 0.0f;
            for (int x = 0; x < DCT_N; ++x) s += DCT_C[u][x] * f[y][x];
            G[y][u] = s;
        }
    }

    // column-wise transform: F[u][v] = sum of C[u][y] * G[y][v]
    // F = C * G
    for (int u = 0; u < DCT_N; ++u) {
        for (int v = 0; v < DCT_N; ++v) {
            float s = 0.0f;
            for (int y = 0; y < DCT_N; ++y) s += DCT_C[u][y] * G[y][v];
            F[u][v] = s;
        }
    }

    //quantization using the multiplication with INV_Q
    for (int u = 0; u < DCT_N; ++u) {
        for (int v = 0; v < DCT_N; ++v) {
            float val = F[u][v];
            float scaled = val * INV_Q[u][v];
            int qi = (int)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
            if (qi > 32767) qi = 32767;
            else if (qi < -32768) qi = -32768;
            out_quant[u * DCT_N + v] = (short)qi;
        }
    }
}


 /*
  * Encode a quantized 8×8 DCT block into text format.
  * Applies zig-zag ordering, differential DC coding,
  * and run-length encoding of AC coefficients.
  */
 char* encode_quant_block_to_text(char *p, const short *restrict quant_block, int *prev_dc)
{
    short zig[64];
    int runs[64];
    short vals[64];

    // zig-zag
    for (int i = 0; i < 64; ++i)
        zig[i] = quant_block[zigzag_idx[i]];

    short dc = zig[0];
    short dc_diff = (short)(dc - *prev_dc);//value derived from previous DC
    *prev_dc = dc;//assign new previous DC

    int pair_count = 0;
    int run = 0;
    for (int i = 1; i < 64; ++i) {
        if (zig[i] != 0) {
            runs[pair_count] = run;
            vals[pair_count] = zig[i];
            pair_count++;
            run = 0;
        } else {
            run++;
        }
    }

    // write: dc_diff pair_count and AC pairs
    p = write_int(p, dc_diff);
    *p++ = ' ';
    p = write_int(p, pair_count);

    for (int i = 0; i < pair_count; ++i) {
        *p++ = ' ';
        p = write_int(p, runs[i]);
        *p++ = ' ';
        p = write_int(p, vals[i]);
    }
    //return pointer to textBlock
    *p++ = '\n';
    return p;
}


 /*
  * Segment arr[] into NBx * NBy blocks going from left to right and up to down
  * with each block being 8x8 in size (edge blocks are padded).
  * Start the processing pipeline on each block(with centered pixels) separately
  */
int segment_and_stream_block_pipeline(const unsigned char * restrict arr,
                                     int width, int height,
                                     char * restrict textBlock,
                                     int *num_blocks_x,
                                     int *num_blocks_y,
                                     int *padded_width,
                                     int *padded_height)
{
    if (!arr || !textBlock) return -1;

    //has to be divisible by 8
    *padded_width  = ((width + 7) >> 3) << 3;
    *padded_height = ((height + 7) >> 3) << 3;
    *num_blocks_x = *padded_width >> 3;
    *num_blocks_y = *padded_height >> 3;
    int NBx = *num_blocks_x;
    int NBy = *num_blocks_y;

    const int width_minus_1 = width - 1;
    const int height_minus_1 = height - 1;
    const int inner_blocks_y = height >> 3;
    const int inner_blocks_x = width  >> 3;

    int num_blocks = NBx * NBy;

    char *p = textBlock;

    // write the header to textBlock
    *p++ = 'C'; *p++ = 'B'; *p++ = 'M'; *p++ = 'P'; *p++ = ' ';
    p = write_int(p, width);  *p++ = ' ';
    p = write_int(p, height); *p++ = ' ';
    p = write_int(p, NBx);    *p++ = ' ';
    p = write_int(p, NBy);    *p++ = ' ';
    p = write_int(p, num_blocks);
    *p++ = '\n';

    short blk[DCT_BLOCK_SZ];
    short quant[DCT_BLOCK_SZ];
    //previous dc value
    int prev_dc = 0;

    for (int by = 0; by < NBy; ++by) {
        const int base_y = by << 3;
        int y_in_bounds = (by < inner_blocks_y);

        for (int bx = 0; bx < NBx; ++bx) {
            const int base_x = bx << 3;
            int x_in_bounds = (bx < inner_blocks_x);


            if (y_in_bounds && x_in_bounds) {
                for (int r = 0; r < 8; ++r) {
                    int y = base_y + r;
                    int row_offset = y * width + base_x;
                    blk[r*8 + 0] = (short)arr[row_offset] - 128;
                    blk[r*8 + 1] = (short)arr[row_offset + 1] - 128;
                    blk[r*8 + 2] = (short)arr[row_offset + 2] - 128;
                    blk[r*8 + 3] = (short)arr[row_offset + 3] - 128;
                    blk[r*8 + 4] = (short)arr[row_offset + 4] - 128;
                    blk[r*8 + 5] = (short)arr[row_offset + 5] - 128;
                    blk[r*8 + 6] = (short)arr[row_offset + 6] - 128;
                    blk[r*8 + 7] = (short)arr[row_offset + 7] - 128;

                }
            } else {
            	//The check if padding is required only happens here
                for (int r = 0; r < 8; ++r) {
                    int y = base_y + r;
                    int src_y = y;
                    if (!y_in_bounds) src_y = (y < height) ? y : height_minus_1;
                    int row_offset = src_y * width;
                    int x = base_x;
                    blk[r*8 + 0] = (short)arr[row_offset + ((x < width) ? x : width_minus_1)] - 128; x++;
                    blk[r*8 + 1] = (short)arr[row_offset + ((x < width) ? x : width_minus_1)] - 128; x++;
                    blk[r*8 + 2] = (short)arr[row_offset + ((x < width) ? x : width_minus_1)] - 128; x++;
                    blk[r*8 + 3] = (short)arr[row_offset + ((x < width) ? x : width_minus_1)] - 128; x++;
                    blk[r*8 + 4] = (short)arr[row_offset + ((x < width) ? x : width_minus_1)] - 128; x++;
                    blk[r*8 + 5] = (short)arr[row_offset + ((x < width) ? x : width_minus_1)] - 128; x++;
                    blk[r*8 + 6] = (short)arr[row_offset + ((x < width) ? x : width_minus_1)] - 128; x++;
                    blk[r*8 + 7] = (short)arr[row_offset + ((x < width) ? x : width_minus_1)] - 128;
                }
            }

            // call DCT and quantization transformation on the block
            dct_and_quant_block(blk, quant);

            // encode the block to textBlock
            p = encode_quant_block_to_text(p, quant, &prev_dc);
        }
    }

    *p = '\0';
    int used = (int)(p - textBlock);
    //printf("TEXTBLOCK USED = %d\n", used);
    return used;
}






int main(int argc, char *argv[]) {
    adi_initComponents();
    init_dct_and_invq(default_qtable);
/*
    printf("rgb @ %p\n", (void*)rgb);
    printf("Rch @ %p\n", (void*)Rch);
    printf("Gch @ %p\n", (void*)Gch);
    printf("Bch @ %p\n", (void*)Bch);
    printf("default_qtable @ %p\n", (void*)default_qtable);
    printf("zigzag_idx @ %p\n", (void*)zigzag_idx);
    printf("DCT_C @ %p\n", (void*)DCT_C);
    printf("INV_Q @ %p\n", (void*)INV_Q);
*/
    if (WIDTH <= 0 || HEIGHT <= 0) {
        printf("Error: invalid dimensions %d x %d\n", WIDTH, HEIGHT);
        return 3;
    }
    printf("Picture dimensions: %d x %d\n", WIDTH, HEIGHT);


    // measure rgb split
    START_CYCLE_COUNT(start_count);
    rgb_split(rgb, Rch,Gch,Bch, WIDTH, HEIGHT);
    STOP_CYCLE_COUNT(final_count, start_count);
    PRINT_CYCLES("Split cycles: ", final_count);

    int NBx = 0, NBy = 0, Wp = 0, Hp = 0;
    int usedR, usedG, usedB, num_blocks;
    // compress the R pixel values
    START_CYCLE_COUNT(start_count);
    usedR = segment_and_stream_block_pipeline(Rch,
    		WIDTH, HEIGHT,textBlockR,&NBx, &NBy, &Wp, &Hp);
    STOP_CYCLE_COUNT(final_count, start_count);
    PRINT_CYCLES("R total cycles: ", final_count);
    printf("textBlockR Count = %d\n", usedR);

    num_blocks = NBx * NBy;
    printf("Segmented into NBx=%d, NBy=%d, total blocks=%d, padded %dx%d\n",
           NBx, NBy, num_blocks, Wp, Hp);

    printf("\n");
    // compress the G pixel values
    START_CYCLE_COUNT(start_count);
    usedG = segment_and_stream_block_pipeline(Gch,
    		WIDTH, HEIGHT,textBlockG,&NBx, &NBy, &Wp, &Hp);
    STOP_CYCLE_COUNT(final_count, start_count);
    PRINT_CYCLES("G total cycles: ", final_count);
    printf("textBlockG Count = %d\n", usedG);

    num_blocks = NBx * NBy;
    printf("Segmented into NBx=%d, NBy=%d, total blocks=%d, padded %dx%d\n",
           NBx, NBy, num_blocks, Wp, Hp);
    printf("\n");
    // compress the B pixel values
    START_CYCLE_COUNT(start_count);
    usedB = segment_and_stream_block_pipeline(Bch,
    		WIDTH, HEIGHT,textBlockB,&NBx, &NBy, &Wp, &Hp);
    STOP_CYCLE_COUNT(final_count, start_count);
    PRINT_CYCLES("B total cycles: ", final_count);
    printf("textBlockB Count = %d\n", usedB);

    num_blocks = NBx * NBy;
    printf("Segmented into NBx=%d, NBy=%d, total blocks=%d, padded %dx%d\n",
           NBx, NBy, num_blocks, Wp, Hp);
    printf("\n");




    return 0;
}

