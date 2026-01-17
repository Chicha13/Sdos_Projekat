#!/usr/bin/env python3
# decode_rgb_text_encoded.py
# Usage:
#   python decode_rgb_dct.py out_R.dat out_G.dat out_B.dat out.bmp

import sys, math
from PIL import Image

DCT_N = 8
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
 0,1,5,6,14,15,27,28,
 2,4,7,13,16,26,29,42,
 3,8,12,17,25,30,41,43,
 9,11,18,24,31,40,44,53,
 10,19,23,32,39,45,52,54,
 20,22,33,38,46,51,55,60,
 21,34,37,47,50,56,59,61,
 35,36,48,49,57,58,62,63
]

def build_C():
    C = [[0.0]*DCT_N for _ in range(DCT_N)]
    for u in range(DCT_N):
        alpha = math.sqrt(1.0/DCT_N) if u == 0 else math.sqrt(2.0/DCT_N)
        for x in range(DCT_N):
            C[u][x] = alpha * math.cos((math.pi / float(DCT_N)) * (float(x) + 0.5) * float(u))
    return C

C = build_C()

def parse_text_file(path):
    """Parse ASCII file written by your C encoder.
       Returns: (width, height, NBx, NBy, parsed_blocks)
       where parsed_blocks is list of (dc_diff, pairs) and pairs is list of (run,val).
    """
    parsed_blocks = []
    with open(path, "r", encoding="utf-8", errors="surrogateescape") as f:
        header = f.readline()
        if not header:
            raise RuntimeError("Empty file")
        parts = header.strip().split()
        if len(parts) < 6 or parts[0] != "MJ01":
            raise RuntimeError("Header malformed: " + header.strip())
        width = int(parts[1]); height = int(parts[2])
        NBx = int(parts[3]); NBy = int(parts[4]); num_blocks = int(parts[5])

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
    return width, height, NBx, NBy, parsed_blocks

def reconstruct_channel(parsed):
    """parsed: (width,height,NBx,NBy,parsed_blocks)
       returns 2D list channel[y][x] clipped to original width/height.
    """
    width, height, NBx, NBy, parsed_blocks = parsed
    num_blocks = len(parsed_blocks)
    padded_w = ((width + 7)//8)*8
    padded_h = ((height + 7)//8)*8

    # compute absolute DCs
    abs_dc = []
    prev_dc = 0
    for dc_diff, pairs in parsed_blocks:
        dc = prev_dc + dc_diff
        abs_dc.append(dc)
        prev_dc = dc

    # flatten Q
    Qflat = [0]*64
    for u in range(8):
        for v in range(8):
            q = default_qtable[u][v]
            Qflat[u*8+v] = q if q != 0 else 1

    # allocate channel (padded)
    channel = [[0]*padded_w for _ in range(padded_h)]

    for b in range(num_blocks):
        by = b // NBx
        bx = b % NBx

        dc = abs_dc[b]
        _, pairs = parsed_blocks[b]

        # build zig array (0..63)
        zig = [0]*64
        zig[0] = dc
        idx = 1
        for run,val in pairs:
            idx += run
            if idx >= 64:
                # malformed run — clamp/stop
                break
            zig[idx] = val
            idx += 1

        # de-zigzag into blk_q (row-major off = u*8 + v)
        blk_q = [0]*64
        for i in range(64):
            pos = zigzag_idx[i]
            blk_q[pos] = zig[i]

        # dequantize -> F[off]
        F = [float(blk_q[i]) * float(Qflat[i]) for i in range(64)]

        # IDCT: tmp[u][x] = sum_v F[u,v] * C[v][x]
        tmp = [[0.0]*8 for _ in range(8)]
        for u in range(8):
            base = u*8
            for x in range(8):
                s = 0.0
                for v in range(8):
                    s += F[base + v] * C[v][x]
                tmp[u][x] = s

        # f[y][x] = sum_u C[u][y] * tmp[u][x]
        fblk = [[0.0]*8 for _ in range(8)]
        for y in range(8):
            for x in range(8):
                s = 0.0
                for u in range(8):
                    s += C[u][y] * tmp[u][x]
                # add 128 and clamp to byte
                vv = int(round(s + 128.0))
                if vv < 0: vv = 0
                elif vv > 255: vv = 255
                fblk[y][x] = vv

        # place into padded channel
        base_x = bx * 8
        base_y = by * 8
        for yy in range(8):
            for xx in range(8):
                channel[base_y + yy][base_x + xx] = fblk[yy][xx]

    # crop to original width x height, return as 2D list
    out = [channel[r][:width] for r in range(height)]
    return out

def save_rgb(r_chan, g_chan, b_chan, width, height, out):
    img = Image.new("RGB", (width, height))
    pix = img.load()
    for y in range(height):
        for x in range(width):
            pix[x,y] = (r_chan[y][x], g_chan[y][x], b_chan[y][x])
    img.save(out)
    print("Saved:", out)

def main():
    if len(sys.argv) != 5:
        print("Usage: python decode_rgb_dct.py out_R.dat out_G.dat out_B.dat out.bmp")
        sys.exit(1)

    rfile, gfile, bfile, outbmp = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]

    r_parsed = parse_text_file(rfile)
    g_parsed = parse_text_file(gfile)
    b_parsed = parse_text_file(bfile)

    # basic sanity: matching dims and NBx/NBy
    if (r_parsed[0:4] != g_parsed[0:4]) or (r_parsed[0:4] != b_parsed[0:4]):
        raise RuntimeError("R/G/B headers differ — files must match (width,height,NBx,NBy)")

    width, height = r_parsed[0], r_parsed[1]

    r_chan = reconstruct_channel(r_parsed)
    g_chan = reconstruct_channel(g_parsed)
    b_chan = reconstruct_channel(b_parsed)

    save_rgb(r_chan, g_chan, b_chan, width, height, outbmp)

if __name__ == "__main__":
    main()
