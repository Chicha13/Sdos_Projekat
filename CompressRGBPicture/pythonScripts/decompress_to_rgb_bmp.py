"""
decompress_to_rgb_bmp.py

Usage:
    python decompress_to_rgb_bmp.py in_compressed.txt out.bmp [originalRGB.raw]

Reads the compressed ASCII file written by the C code 
and reconstructs the image (24-bit rgb BMP).
Also compares the pixels in the reconstructed image to the original
pixels if they are provided as an input parameter
"""
import sys, math
from PIL import Image

# parameters matching the DSP compress code
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

# Precompute DCT matrix C[u][x] exactly like on the DSP side
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
    Parse ASCII compress .txt file.
    Returns (width, height, NBx, NBy, num_blocks, parsed_blocks)
    width is the image width.
    height is the image height.
    parsed_blocks is a list of tuples (dc_diff, pairs) where
    pairs is a list of (run,val) where run is the number of zeros before val (non zero value).
    NBx is the number of blocks by the columns.
    NBy is the number of blocks by the rows.
    num_blocks is the total number of blocks NBx * NBy.
    """
    parsed_blocks = []
    with open(path, "r", encoding="utf-8", errors="surrogateescape") as f:
        # read the header
        header = f.readline()
        if not header:
            raise RuntimeError("Empty file")
        header = header.strip()
        parts = header.split()
        if len(parts) < 6 or parts[0] != "CBMP":
            raise RuntimeError("Header not found or malformed: " + header)
        width = int(parts[1]); height = int(parts[2])
        NBx = int(parts[3]); NBy = int(parts[4]); num_blocks = int(parts[5])
        # for each block
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
            parsed_blocks.append((dc_diff, pairs)) # for each block one tuple
    if len(parsed_blocks) != num_blocks:
        raise RuntimeError("Read blocks count mismatch")
    return width, height, NBx, NBy, num_blocks, parsed_blocks

def c_round(x):
    return int(x + 0.5) if x >= 0.0 else int(x - 0.5)


def decompress_function(parsed_blocks, width, height, NBx, NBy):
    """
    Does all the inverse operations of the ones on the DSP side
    
    Returns bytes of one channel (r/g/b) (width*height) after placing blocks in padded image and cropping.
    """
    num_blocks = len(parsed_blocks)
    padded_w = ((width + 7) // 8) * 8
    padded_h = ((height + 7) // 8) * 8

    # convert dc diffs to absolute DCs for each block
    abs_dcs = []
    prev_dc = 0
    for dc_diff, pairs in parsed_blocks:
        dc = prev_dc + dc_diff
        prev_dc = dc
        abs_dcs.append(dc)

    # prepare Quantization table
    Qseq = [0]*64
    for u in range(8):
        for v in range(8):
            Qseq[u*8 + v] = default_qtable[u][v] if default_qtable[u][v] != 0 else 1

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
                # malformed, break
                break
            zig[idx] = val
            idx += 1

        # de zigzag
        blk_q = [0]*64
        for i in range(64):
            pos = zigzag_idx[i]
            blk_q[pos] = zig[i]

        # dequantize
        F = [0.0]*64
        for off in range(64):
            F[off] = float(blk_q[off]) * float(Qseq[off])


        # IDCT f = C^T * F * C

        # temp[u][x] = sum of F[u][v] * C[v][x]
        # temp = F * C
        temp = [[0.0]*DCT_N for _ in range(DCT_N)]
        for u in range(DCT_N):
            base = u * DCT_N
            for x in range(DCT_N):
                s = 0.0
                for v in range(DCT_N):
                    s += F[base + v] * C[v][x]
                temp[u][x] = s

        # f[y][x] = sum of C[u][y] * temp[u][x]
        # f = C^T * temp
        fblk = [[0.0]*DCT_N for _ in range(DCT_N)]
        for y in range(DCT_N):
            for x in range(DCT_N):
                s = 0.0
                for u in range(DCT_N):
                    s += temp[u][x] * C[u][y] 
                fblk[y][x] = s

        # place block into padded image (add 128 and round)
        bx = b % NBx
        by = b // NBx
        base_x = bx * 8
        base_y = by * 8
        for yy in range(8):
            for xx in range(8):
                img_x = base_x + xx
                img_y = base_y + yy
                if img_x < padded_w and img_y < padded_h:
                    v = c_round(fblk[yy][xx] + 128.0) 
                    if v < 0: v = 0
                    elif v > 255: v = 255
                    out[img_y * padded_w + img_x] = v

    # crop to original width/height
    final = bytearray(width * height)
    for r in range(height):
        row_src = r * padded_w
        final[r*width:(r+1)*width] = bytes(out[row_src:row_src+width])
    return final


def compute_mse(orig_seq, recon_seq):
    """
    Mean Squared Error
    orig_seq and recon_seq: sequences of bytes.
    Requires sequences to have exactly the same length.
    Returns (mse, n) where mse is the error and n is the length of the sequences.
    """
    n_orig = len(orig_seq)
    n_recon = len(recon_seq)
    if n_orig != n_recon:
        raise ValueError(f"Length mismatch: orig={n_orig} recon={n_recon}. MSE requires equal lengths.")
    n = n_orig
    if n == 0:
        raise ValueError("Empty sequences: cannot compute MSE on zero-length data.")
    s = 0
    for i in range(n):
        d = int(orig_seq[i]) - int(recon_seq[i])
        s += d * d
    mse = float(s) / float(n)
    return mse, n
    
def main():
    if len(sys.argv) not in (5, 6):
        print("Usage: python decompress_to_rgb_bmp.py R.txt G.txt B.txt out.bmp [originalRGB.raw]")
        sys.exit(1)

    rfile, gfile, bfile, outbmp = sys.argv[1:5]
    orig_raw = sys.argv[5] if len(sys.argv) == 6 else None

    # parse the compressed files for each channel (r/g/b)
    r_parsed = parse_text_file(rfile)
    g_parsed = parse_text_file(gfile)
    b_parsed = parse_text_file(bfile)

    if r_parsed[:4] != g_parsed[:4] or r_parsed[:4] != b_parsed[:4]:
        raise RuntimeError("R/G/B headers do not match")

    width, height, NBx, NBy, _, _ = r_parsed
    # decompress pixels for each channel (r/g/b)
    r = decompress_function(r_parsed[5], width, height, NBx, NBy)
    g = decompress_function(g_parsed[5], width, height, NBx, NBy)
    b = decompress_function(b_parsed[5], width, height, NBx, NBy)

    # reconstruct the entire rgb .bmp 
    img = Image.new("RGB", (width, height))
    pix = img.load()
    idx = 0
    for y in range(height):
        for x in range(width):
            pix[x,y] = (r[idx], g[idx], b[idx])
            idx += 1

    img.save(outbmp, format="BMP")
    print("Saved:", outbmp)

    if not orig_raw:
        return

    with open(orig_raw, "rb") as f:
        orig = f.read()
        
    # compute Mean Square Error for each channel separately then average them 
    mse_r, _ = compute_mse(orig[0::3], r)
    mse_g, _ = compute_mse(orig[1::3], g)
    mse_b, _ = compute_mse(orig[2::3], b)
    mse = (mse_r + mse_g + mse_b) / 3.0


    psnr = float("inf") if mse == 0 else 10.0 * math.log10((255.0**2) / mse)

    print(f"MSE R: {mse_r:.4f}")
    print(f"MSE G: {mse_g:.4f}")
    print(f"MSE B: {mse_b:.4f}")
    print(f"MSE total: {mse:.4f}")
    print(f"PSNR: {psnr:.3f} dB")

if __name__ == "__main__":
    main()
