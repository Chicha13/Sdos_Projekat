/*****************************************************************************
 * CompressGrayPicture.c
 *****************************************************************************/
#include <sys/platform.h>
#include "adi_initialize.h"
#include "CompressGrayPicture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "CityLandscape.bmp.h"
#include <cycle_count.h>
#include <builtins.h>  // ADSP intrinsics


cycle_t start_count;
cycle_t final_count;


#pragma section("seg_sdram1")
short blocks[200000];



#define DCT_N 8
#define DCT_BLOCK_SZ (DCT_N * DCT_N)
#define MAX_BLOCKS_FROM_BLOCKS_ARRAY 3125
#pragma section("seg_sdram1")
float dct_blocks_sdram_f[MAX_BLOCKS_FROM_BLOCKS_ARRAY * DCT_BLOCK_SZ];

#pragma section("seg_sdram1")
short quant_blocks_sdram[MAX_BLOCKS_FROM_BLOCKS_ARRAY * DCT_BLOCK_SZ];

#ifndef M_PI
/* float konstanta pi (korišcena sa cosf/sqrtf) */
#define M_PI_F 3.14159265358979323846f
#else
/* ako je postojeci M_PI definisan kao double, napravimo float alias */
#define M_PI_F ((float)M_PI)
#endif

/*
#define BLOCK_SIZE 64
#define BLOCK_PIXELS (BLOCK_SIZE * BLOCK_SIZE * 3)// Manji blok za bolju cache lokalnost

#pragma section("seg_dm_fast")
 unsigned char rgb_block[BLOCK_PIXELS];

 #ifndef MAX_RAW_BYTES
#define MAX_RAW_BYTES (300000)
#endif

*/
static inline float my_roundf(float x)
{
    /* cast via long
       We use truncation toward zero of the cast and adjust by +/-0.5. */
    if (x >= 0.0f) {
        return (float)( (long)(x + 0.5f) );
    } else {
        return (float)( (long)(x - 0.5f) );
    }
}

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


void rgb_to_grayscale_fixed_32load(const unsigned char * restrict rgb,
                                          unsigned char * restrict gray,
                                          int width, int height)
{
    const int R = 76, G = 150, B = 29;
    const unsigned char *p = rgb;
    int pixels = width * height;
    int i = 0;

    /* obrada 4 piksela po iteraciji */
    for (; i + 3 < pixels; i += 4, p += 12) {

        int r0 = p[0],  g0 = p[1],  b0 = p[2];
        int r1 = p[3],  g1 = p[4],  b1 = p[5];
        int r2 = p[6],  g2 = p[7],  b2 = p[8];
        int r3 = p[9],  g3 = p[10], b3 = p[11];

        gray[i + 0] = (unsigned char)((r0*R + g0*G + b0*B + 128) >> 8);
        gray[i + 1] = (unsigned char)((r1*R + g1*G + b1*B + 128) >> 8);
        gray[i + 2] = (unsigned char)((r2*R + g2*G + b2*B + 128) >> 8);
        gray[i + 3] = (unsigned char)((r3*R + g3*G + b3*B + 128) >> 8);
    }

    /* tail */
    for (; i < pixels; ++i, p += 3) {
        int r = p[0], g = p[1], b = p[2];
        gray[i] = (unsigned char)((r*R + g*G + b*B + 128) >> 8);
    }
}





void segment_8x8_centered_replicate_ADSP_opt(
    const unsigned char * restrict gray,
    int width,
    int height,
    short * restrict blocks,
    int *num_blocks_x,
    int *num_blocks_y,
    int *padded_width,
    int *padded_height
) {
    // Padded dimensions
    *padded_width = ((width + 7) >> 3) << 3;  //  /* 8
    *padded_height = ((height + 7) >> 3) << 3;

    *num_blocks_x = *padded_width >> 3;  //   /8
    *num_blocks_y = *padded_height >> 3;

    const int blocks_x = *num_blocks_x;
    const int blocks_y = *num_blocks_y;
    const int width_minus_1 = width - 1;
    const int height_minus_1 = height - 1;

    // Indexes for non padding blocks
    const int inner_blocks_y = height >> 3;
    const int inner_blocks_x = width >> 3;

    // Main loop
   // #pragma SIMD_for
    for (int by = 0; by < blocks_y; ++by) {
        // Y indexes for the entire row of blocks
        const int base_y = by << 3;  // *8


        // Is the row in the non paddded area
        const int y_in_bounds = (by < inner_blocks_y);
       // #pragma SIMD_for
        for (int bx = 0; bx < blocks_x; ++bx) {
            short *blk = blocks + ((by * blocks_x + bx) << 6);  // *64

            const int base_x = bx << 3;  // *8


            // Is the column in the non padded area
            const int x_in_bounds = (bx < inner_blocks_x);

            if (y_in_bounds && x_in_bounds) {
                // Block is in the non padded area
                for (int r = 0; r < 8; ++r) {
                    const int y = base_y + r;
                    const int row_offset = y * width + base_x;

                    // Unroll of the inner loop
                    blk[0] = (short)gray[row_offset] - 128;
                    blk[1] = (short)gray[row_offset + 1] - 128;
                    blk[2] = (short)gray[row_offset + 2] - 128;
                    blk[3] = (short)gray[row_offset + 3] - 128;
                    blk[4] = (short)gray[row_offset + 4] - 128;
                    blk[5] = (short)gray[row_offset + 5] - 128;
                    blk[6] = (short)gray[row_offset + 6] - 128;
                    blk[7] = (short)gray[row_offset + 7] - 128;

                    blk += 8;  // next row in the block
                }
            }
			else { // Edge blocks that need to be padded

                for (int r = 0; r < 8; ++r) {
                    // Y index for the padded blocks
                    int y = base_y + r;
                    int src_y = y;
                    if (!y_in_bounds) {
                        src_y = (y < height) ? y : height_minus_1;
                    }

                    const int row_offset = src_y * width;

                    // Unroll of the inner loop
                    int x = base_x;
                    blk[0] = (short)gray[row_offset + ((x < width) ? x : width_minus_1)] - 128;  x++;
                    blk[1] = (short)gray[row_offset + ((x < width) ? x : width_minus_1)] - 128;  x++;
                    blk[2] = (short)gray[row_offset + ((x < width) ? x : width_minus_1)] - 128;  x++;
                    blk[3] = (short)gray[row_offset + ((x < width) ? x : width_minus_1)] - 128;  x++;
                    blk[4] = (short)gray[row_offset + ((x < width) ? x : width_minus_1)] - 128;  x++;
                    blk[5] = (short)gray[row_offset + ((x < width) ? x : width_minus_1)] - 128;  x++;
                    blk[6] = (short)gray[row_offset + ((x < width) ? x : width_minus_1)] - 128;  x++;
                    blk[7] = (short)gray[row_offset + ((x < width) ? x : width_minus_1)] - 128;

                    blk += 8;
                }
            }
        }
    }
}


void dct8x8_blocks_to_sdram_float(const short *blocks, int num_blocks)
{
    if (!blocks) {
        printf("dct: null blocks ptr\n");
        return;
    }
    if (num_blocks <= 0) {
        printf("dct: num_blocks <= 0\n");
        return;
    }
    if (num_blocks > MAX_BLOCKS_FROM_BLOCKS_ARRAY) {
        printf("dct: num_blocks %d > max %d\n", num_blocks, MAX_BLOCKS_FROM_BLOCKS_ARRAY);
        return;
    }

    static float C[DCT_N][DCT_N];
    static int C_init = 0;
    if (!C_init) {
        for (int u = 0; u < DCT_N; ++u) {
            float alpha = (u == 0) ? sqrtf(1.0f / DCT_N) : sqrtf(2.0f / DCT_N);
            for (int x = 0; x < DCT_N; ++x) {
                C[u][x] = alpha * cosf((M_PI_F / (float)DCT_N) * ((float)x + 0.5f) * (float)u);
            }
        }
        C_init = 1;
    }

    float f[DCT_N][DCT_N], G[DCT_N][DCT_N], F[DCT_N][DCT_N];

    for (int b = 0; b < num_blocks; ++b) {
        const short *blk_in = blocks + (size_t)b * DCT_BLOCK_SZ;
        float *blk_out = dct_blocks_sdram_f + (size_t)b * DCT_BLOCK_SZ;

        /* copy input: f[y][x] = blk_in[y*8 + x] */
        for (int y = 0; y < DCT_N; ++y)
            for (int x = 0; x < DCT_N; ++x)
                f[y][x] = (float) blk_in[y * DCT_N + x];

        /* 1) row-wise DCT: G[y][u] = sum_x f[y][x] * C[u][x] */
        for (int y = 0; y < DCT_N; ++y) {
            for (int u = 0; u < DCT_N; ++u) {
                float s = 0.0f;

                for (int x = 0; x < DCT_N; ++x) s += C[u][x] * f[y][x];
                G[y][u] = s;
            }
        }

        /* 2) column-wise DCT: F[u][v] = sum_y C[u][y] * G[y][v] */
        for (int u = 0; u < DCT_N; ++u) {
            for (int v = 0; v < DCT_N; ++v) {
                float s = 0.0f;

                for (int y = 0; y < DCT_N; ++y) s += C[u][y] * G[y][v];
                F[u][v] = s;
            }
        }

        /* store output: blk_out[u*8 + v] = F[u][v] */
        for (int u = 0; u < DCT_N; ++u)
            for (int v = 0; v < DCT_N; ++v)
                blk_out[u * DCT_N + v] = F[u][v];
    }

}


void quantize_dct_blocks_to_sdram_short(const float *dct_buf, int num_blocks, const int qtable[DCT_N][DCT_N])
{
    if (!dct_buf) dct_buf = dct_blocks_sdram_f;
    if (!dct_buf) {
        printf("quantize: no dct buffer\n");
        return;
    }
    if (num_blocks <= 0) return;
    if (num_blocks > MAX_BLOCKS_FROM_BLOCKS_ARRAY) {
        printf("quantize: num_blocks %d > max %d\n", num_blocks, MAX_BLOCKS_FROM_BLOCKS_ARRAY);
        return;
    }

    const int (*Q)[DCT_N] = (qtable != NULL) ? qtable : default_qtable;
    float invQ[DCT_N][DCT_N];

       /* precompute reciprocals once */
       for (int u = 0; u < DCT_N; ++u)
           for (int v = 0; v < DCT_N; ++v)
               invQ[u][v] = 1.0f / (float)(Q[u][v] <= 0 ? 1 : Q[u][v]);

    for (int b = 0; b < num_blocks; ++b) {
        const float *in_blk = dct_buf + (size_t)b * DCT_BLOCK_SZ;
        short *out_blk = quant_blocks_sdram + (size_t)b * DCT_BLOCK_SZ;

        for (int u = 0; u < DCT_N; ++u) {
            for (int v = 0; v < DCT_N; ++v) {
                float val = in_blk[u * DCT_N + v];

                float scaled = val * invQ[u][v];
                int qi = (int) (scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
                 if (qi > 32767) qi = 32767;
                 else if (qi < -32768) qi = -32768;
                 out_blk[u * DCT_N + v] = (short) qi;

            }
        }
    }
}



int encode_quant_blocks_to_file(const char *filename,
                                const short *quant_blocks,
                                int num_blocks,
                                int width, int height,
                                int NBx, int NBy)
{
    if (!filename) return -1;
    if (num_blocks <= 0) return -2;

    if (!quant_blocks) quant_blocks = quant_blocks_sdram; /* use global */

    FILE *f = fopen(filename, "w");
    if (!f) {
        perror("fopen encode output");
        return -3;
    }

    /* write header */

    fprintf(f, "MJ01 %d %d %d %d %d\n",
            WIDTH, HEIGHT, NBx, NBy, NBx * NBy);

    /* temporary arrays for RLE pairs (worst-case <=63 pairs) */
    int runs[64];
    short vals[64];

    int prev_dc = 0;

    for (int b = 0; b < num_blocks; ++b) {
        const short *blk = quant_blocks + (size_t)b * DCT_BLOCK_SZ;

        /* zig-zag reorder into zig[0..63] */
        short zig[64];
        for (int i = 0; i < 64; ++i) {
            unsigned char idx = zigzag_idx[i]; /* 0..63 */
            zig[i] = blk[(int)idx];
        }

        /* DC differencing */
        short dc = zig[0];
        short dc_diff = (short)(dc - prev_dc);
        prev_dc = dc;

        /* collect RLE pairs for AC coefficients (indices 1..63) */
        int pair_count = 0;
        int run = 0;
        for (int i = 1; i < 64; ++i) {
            if (zig[i] != 0) {
                runs[pair_count] = run;
                vals[pair_count] = zig[i];
                ++pair_count;
                run = 0;
            } else {
                ++run;
            }
        }
        /* trailing zeros are simply not emitted (pair_count may be 0) */

        /* write block: dc_diff, pair_count, then pairs */
        fprintf(f,"%d ",dc_diff);
        fprintf(f,"%d ",pair_count);

        for (int p = 0; p < pair_count; ++p) {
            int run_i = runs[p];
            short val_i = vals[p];
            fprintf(f,"%d ",run_i);
            fprintf(f,"%d ",val_i);

        }
        fprintf(f,"\n");
    }

    fclose(f);
    return 0;
}

void compute_dequant_errors(int num_blocks, const int qtable[DCT_N][DCT_N])
{
    if (num_blocks <= 0) {
        printf("compute_dequant_errors: num_blocks <= 0\n");
        return;
    }
    if (num_blocks > MAX_BLOCKS_FROM_BLOCKS_ARRAY) {
        printf("compute_dequant_errors: num_blocks %d > max %d\n",
               num_blocks, MAX_BLOCKS_FROM_BLOCKS_ARRAY);
        return;
    }

    const int (*Q)[DCT_N] = (qtable != NULL) ? qtable : default_qtable;

    long total_coeffs = (long)num_blocks * DCT_BLOCK_SZ;
    double sum_abs = 0.0;
    double sum_sq = 0.0;
    double max_abs = 0.0;
    int max_block = -1, max_u = -1, max_v = -1;
    float max_orig = 0.0f, max_recon = 0.0f;

    long nonzero_count = 0;

    for (int b = 0; b < num_blocks; ++b) {
        const float *orig_blk = dct_blocks_sdram_f + (size_t)b * DCT_BLOCK_SZ;
        const short *q_blk = quant_blocks_sdram + (size_t)b * DCT_BLOCK_SZ;

        for (int off = 0; off < DCT_BLOCK_SZ; ++off) {
            int u = off / DCT_N;
            int v = off % DCT_N;
            float orig = orig_blk[off];
            int q = (int) q_blk[off];
            if (q != 0) ++nonzero_count;

            float recon = (float) q * (float) Q[u][v];
            float err = orig - recon;
            double aerr = fabs((double)err);
            sum_abs += aerr;
            sum_sq += (double)err * (double)err;
            if (aerr > max_abs) {
                max_abs = aerr;
                max_block = b;
                max_u = u;
                max_v = v;
                max_orig = orig;
                max_recon = recon;
            }
        }
    }

    double mean_abs = sum_abs / (double) total_coeffs;
    double rms = sqrt(sum_sq / (double) total_coeffs);
    double sparsity = 100.0 * (1.0 - ((double)nonzero_count / (double) total_coeffs));

    printf("\n=== Dequantization error summary ===\n");
    printf("Blocks: %d, Coeffs total: %ld\n", num_blocks, total_coeffs);
    printf("Nonzero quantized coeffs: %ld (%.2f%%), Zeros: %ld (%.2f%%)\n",
           nonzero_count,
           100.0 * ((double)nonzero_count / (double) total_coeffs),
           total_coeffs - nonzero_count,
           sparsity);
    printf("Global mean absolute error (MAE): %g\n", mean_abs);
    printf("Global RMS error: %g\n", rms);
    printf("Max absolute error: %g at block=%d (u=%d,v=%d)\n", max_abs, max_block, max_u, max_v);
    printf("  orig = %g, recon = %g, orig-recon = %g\n", (double)max_orig, (double)max_recon, (double)(max_orig - max_recon));
    printf("====================================\n");
}



int main(int argc, char *argv[]) {
    adi_initComponents();

    if (WIDTH <= 0 || HEIGHT <= 0) {
        printf("Error: invalid dims %d x %d\n", WIDTH, HEIGHT);
        return 3;
    }
    printf("Picture Size: %d x %d\n", WIDTH, HEIGHT);

    size_t bytes = (size_t) WIDTH * (size_t) HEIGHT;
    if (bytes > sizeof(gray)) {
        printf("ERROR: gray buffer (%zu) too small for image bytes=%u\n", sizeof(gray), bytes);
        return 4;
    }


    printf("rgb @ %p\n", (void*)rgb);
    printf("Quantization Table @ %p\n", (void*)default_qtable);
    /* measure grayscale conversion */
    START_CYCLE_COUNT(start_count);
    rgb_to_grayscale_fixed_32load(rgb, gray, WIDTH, HEIGHT);
    STOP_CYCLE_COUNT(final_count, start_count);
    PRINT_CYCLES("Grayscale cycles: ", final_count);

    printf("Gray @ %p\n", (void*)gray);
    //===========================================================================


    int NBx = 0, NBy = 0, Wp = 0, Hp = 0;

    
    START_CYCLE_COUNT(start_count);
    segment_8x8_centered_replicate_ADSP_opt(
        gray,           
        WIDTH,
        HEIGHT,
        blocks,         
        &NBx,
        &NBy,
        &Wp,
        &Hp
    );
    STOP_CYCLE_COUNT(final_count, start_count);
    PRINT_CYCLES("Segmentation/Center cycles: ", final_count);
    printf("Blocks @ %p\n", (void*)blocks);
    int num_blocks = NBx * NBy;
    printf("Segmented into NBx=%d, NBy=%d, total blocks=%d, padded %dx%d\n",
           NBx, NBy, num_blocks, Wp, Hp);



        START_CYCLE_COUNT(start_count);
        dct8x8_blocks_to_sdram_float(blocks, num_blocks);
        STOP_CYCLE_COUNT(final_count, start_count);
         PRINT_CYCLES("DCT II cycles: ", final_count);

         printf("DCT @ %p\n", (void*)dct_blocks_sdram_f);
/*
             printf("\n");
             printf("Random %f : \n", dct_blocks_sdram_f[0]);
             printf("Random %f : \n", dct_blocks_sdram_f[1]);
             printf("Random %f : \n", dct_blocks_sdram_f[2]);
             printf("Random %f : \n", dct_blocks_sdram_f[3]);
             printf("Random %f : \n", dct_blocks_sdram_f[4]);
             printf("Random %f : \n", dct_blocks_sdram_f[60]);

             printf("Random %f : \n", dct_blocks_sdram_f[42487]);
             printf("Random %f : \n", dct_blocks_sdram_f[42488]);
             printf("Random %f : \n", dct_blocks_sdram_f[42489]);
             printf("Random %f : \n", dct_blocks_sdram_f[42486]);
             printf("Random %f : \n", dct_blocks_sdram_f[42432]);
             printf("\n");
*/
         START_CYCLE_COUNT(start_count);
         quantize_dct_blocks_to_sdram_short(dct_blocks_sdram_f, num_blocks, NULL);
         STOP_CYCLE_COUNT(final_count, start_count);
         PRINT_CYCLES("Quantization cycles: ", final_count);
         printf("Quantization @ %p\n", (void*)quant_blocks_sdram);

         printf("\n");
            printf("Random %d : \n", (int)quant_blocks_sdram[0]);
            printf("Random %d : \n", (int)quant_blocks_sdram[1]);
            printf("Random %d : \n",(int) quant_blocks_sdram[2]);

            printf("Random %d : \n", (int)quant_blocks_sdram[3]);
            printf("Random %d : \n",(int) quant_blocks_sdram[4]);
            printf("Random5 %d : \n", (int)quant_blocks_sdram[5]);
            	  printf("Random %d : \n", (int)quant_blocks_sdram[42487]);
                  printf("Random %d : \n", (int)quant_blocks_sdram[42488]);
                  printf("Random %d : \n", (int)quant_blocks_sdram[42489]);
                  printf("Random %d : \n", (int)quant_blocks_sdram[42486]);
                  printf("Random %d : \n", (int)quant_blocks_sdram[42432]);
                  printf("Random %d : \n", (int)quant_blocks_sdram[127]);
            printf("\n");


            int zeros = 0, nonzeros = 0;
            for (int i=0; i<num_blocks*64; ++i) {
              if (quant_blocks_sdram[i] == 0) ++zeros; else ++nonzeros;
            }
            printf("quantized: zeros=%d nonzeros=%d (total=%d)\n", zeros, nonzeros, num_blocks*64);

    printf("\n");

    compute_dequant_errors(num_blocks,NULL);
    printf("\n");


    //====================================================================================


    START_CYCLE_COUNT(start_count);
    int rc = encode_quant_blocks_to_file("out_encoded.dat",
                                         quant_blocks_sdram,
                                         num_blocks,
                                         WIDTH, HEIGHT,
                                         NBx, NBy);
    STOP_CYCLE_COUNT(final_count, start_count);
    PRINT_CYCLES("Encode cycles: ", final_count);

    if (rc == 0) printf("Encoding OK, blocks=%d\n", num_blocks);
    else printf("Encoding failed, rc=%d\n", rc);
    return 0;
}
