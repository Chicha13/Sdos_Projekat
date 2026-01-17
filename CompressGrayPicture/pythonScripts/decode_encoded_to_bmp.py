#!/usr/bin/env python3
"""
decode_encoded_to_bmp.py

Usage:
    python decode_encoded_to_bmp.py in_encoded.dat out_recon.bmp

Reads ASCII file written by encode_quant_blocks_to_file (MJ01 header + lines per block)
and reconstructs image (8-bit grayscale BMP).
"""
import sys
import math
from PIL import Image

# ----- parameters (must match your C code) -----
DCT_N = 8
DCT_BLOCK_SZ = 64
default_qtable = [
 [16,11,10,16,24,40,51,61],
 [12,12,14,19,26,58,60,55],
 [14,13,16,24,40,57,69,56],
 [14,17,22,29,51,87,80,62],
 [18,22,37,56,68,109,103,77],
 [24,35,55,64,81,104,113,92],
 [49,64,78,87,103,121,120,101],
 [72,92,95,98,112,100,103,99],
]
zigzag_idx = [
 0,  1,  5,  6, 14, 15, 27, 28,
 2,  4,  7, 13, 16, 26, 29, 42,
 3,  8, 12, 17, 25, 30, 41, 43,
 9, 11, 18, 24, 31, 40, 44, 53,
 10, 19, 23, 32, 39, 45, 52, 54,
 20, 22, 33, 38, 46, 51, 55, 60,
 21, 34, 37, 47, 50, 56, 59, 61,
 35, 36, 48, 49, 57, 58, 62, 63
]

# Precompute DCT matrix C[u][x] exactly like the C code
def build_C():
    C = [[0.0]*DCT_N for _ in range(DCT_N)]
    for u in range(DCT_N):
        alpha = math.sqrt(1.0/DCT_N) if u == 0 else math.sqrt(2.0/DCT_N)
        for x in range(DCT_N):
            C[u][x] = alpha * math.cos((math.pi / float(DCT_N)) * (float(x) + 0.5) * float(u))
    return C

C = build_C()

def parse_text_file(path):
    """
    Parse ASCII encoded file.
    Returns (width, height, NBx, NBy, num_blocks, parsed_blocks)
    where parsed_blocks is list of tuples (dc_diff, pairs) and pairs is list of (run,val).
    """
    parsed_blocks = []
    with open(path, "r", encoding="utf-8", errors="surrogateescape") as f:
        # read header line
        header = f.readline()
        if not header:
            raise RuntimeError("Empty file")
        header = header.strip()
        parts = header.split()
        if len(parts) < 6 or parts[0] != "MJ01":
            raise RuntimeError("Header not found or malformed: " + header)
        width = int(parts[1]); height = int(parts[2])
        NBx = int(parts[3]); NBy = int(parts[4]); num_blocks = int(parts[5])
        # read next num_blocks lines
        for b in range(num_blocks):
            line = f.readline()
            if not line:
                raise RuntimeError(f"Unexpected EOF while reading block {b}")
            toks = line.strip().split()
            if len(toks) < 2:
                raise RuntimeError(f"Malformed block line {b}: '{line.strip()}'")
            dc_diff = int(toks[0])
            pair_count = int(toks[1])
            expected_len = 2 + pair_count * 2
            if len(toks) != expected_len:
                raise RuntimeError(f"Block {b}: expected {expected_len} tokens but got {len(toks)}")
            pairs = []
            for i in range(pair_count):
                run = int(toks[2 + 2*i])
                val = int(toks[2 + 2*i + 1])
                pairs.append((run, val))
            parsed_blocks.append((dc_diff, pairs))
    if len(parsed_blocks) != num_blocks:
        raise RuntimeError("Read blocks count mismatch")
    return width, height, NBx, NBy, num_blocks, parsed_blocks

def dequantize_and_idct(parsed_blocks, width, height, NBx, NBy):
    """
    parsed_blocks: list of (dc_diff, pairs) with DC differencing.
    Returns grayscale bytes (width*height) after placing blocks in padded image and cropping.
    """
    num_blocks = len(parsed_blocks)
    padded_w = ((width + 7) // 8) * 8
    padded_h = ((height + 7) // 8) * 8

    # convert dc diffs to absolute DCs
    abs_dcs = []
    prev_dc = 0
    for dc_diff, pairs in parsed_blocks:
        dc = prev_dc + dc_diff
        prev_dc = dc
        abs_dcs.append(dc)

    # pre-flatten Q
    Qflat = [0]*64
    for u in range(8):
        for v in range(8):
            Qflat[u*8 + v] = default_qtable[u][v] if default_qtable[u][v] != 0 else 1

    # output buffer (padded)
    out = [0] * (padded_w * padded_h)

    # for each block:
    for b in range(num_blocks):
        dc = abs_dcs[b]
        _, pairs = parsed_blocks[b]

        # build zig array (indices 0..63)
        zig = [0]*64
        zig[0] = dc
        idx = 1
        for run, val in pairs:
            idx += run
            if idx >= 64:
                # malformed, but stop gracefully
                break
            zig[idx] = val
            idx += 1

        # map zig to row-major block (blk_q[pos] = zig[i] where pos = zigzag_idx[i])
        blk_q = [0]*64
        for i in range(64):
            pos = zigzag_idx[i]
            blk_q[pos] = zig[i]

        # dequantize -> F[u*8 + v] floats (row-major)
        F = [0.0]*64
        for off in range(64):
            F[off] = float(blk_q[off]) * float(Qflat[off])

        # temp[u][x] = sum_v F[u][v] * C[v][x]
        temp = [[0.0]*DCT_N for _ in range(DCT_N)]
        for u in range(DCT_N):
            base = u * DCT_N
            for x in range(DCT_N):
                s = 0.0
                for v in range(DCT_N):
                    s += F[base + v] * C[v][x]
                temp[u][x] = s

        # f[y][x] = sum_u C[u][y] * temp[u][x]
        fblk = [[0.0]*DCT_N for _ in range(DCT_N)]
        for y in range(DCT_N):
            for x in range(DCT_N):
                s = 0.0
                for u in range(DCT_N):
                    s += C[u][y] * temp[u][x]
                fblk[y][x] = s

        # place block into padded image (add 128 and clamp)
        bx = b % NBx
        by = b // NBx
        base_x = bx * 8
        base_y = by * 8
        for yy in range(8):
            for xx in range(8):
                img_x = base_x + xx
                img_y = base_y + yy
                if img_x < padded_w and img_y < padded_h:
                    v = int(round(fblk[yy][xx] + 128.0))
                    if v < 0: v = 0
                    elif v > 255: v = 255
                    out[img_y * padded_w + img_x] = v

    # crop to original width/height
    final = bytearray(width * height)
    for r in range(height):
        row_src = r * padded_w
        final[r*width:(r+1)*width] = bytes(out[row_src:row_src+width])
    return final

def save_gray_bmp(gray_bytes, width, height, out_path):
    """
    Save an 8-bit grayscale image (BMP). gray_bytes must be length width*height.
    """
    if len(gray_bytes) != width * height:
        raise ValueError(f"gray_bytes length {len(gray_bytes)} != width*height {width*height}")

    img = Image.frombytes("L", (width, height), bytes(gray_bytes))
    img.save(out_path, format="BMP")
    print("Saved grayscale image:", out_path)


def save_raw_gray(gray_bytes, out_raw_path):
    """Optional helper: write raw gray bytes to disk (width*height bytes)."""
    with open(out_raw_path, "wb") as f:
        f.write(bytes(gray_bytes))
    print("Saved raw grayscale bytes:", out_raw_path)

def main():
    if len(sys.argv) != 3:
        print("Usage: python decode_text_encoded.py in_encoded.txt out_recon.bmp")
        sys.exit(1)
    in_file = sys.argv[1]
    out_file = sys.argv[2]

    print("Parsing:", in_file)
    width, height, NBx, NBy, num_blocks, parsed_blocks = parse_text_file(in_file)
    print("Header: width=%d height=%d NBx=%d NBy=%d num_blocks=%d" % (width, height, NBx, NBy, num_blocks))
    # quick validation
    if num_blocks != NBx * NBy:
        print("Warning: num_blocks != NBx*NBy (got %d vs %d). Continuing..." % (num_blocks, NBx*NBy))

    gray = dequantize_and_idct(parsed_blocks, width, height, NBx, NBy)

    # save grayscale BMP/PNG (one byte per pixel)
    save_gray_bmp(gray, width, height, out_file)

    # optional: also write raw bytes for inspection
    # save_raw_gray(gray, out_file + ".raw")

    print("Done.")

if __name__ == "__main__":
    main()
