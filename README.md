# compression_challenge

Attempt at [Neuralink's Compression Challenge](https://content.neuralink.com/compression-challenge/README.html).

The task: losslessly compress ~5-second recordings of N1-implant electrode data,
stored as 16-bit mono PCM WAV at 19.531 kHz. Each file must round-trip
bit-for-bit through `encode` → `decode`.

## Building

```bash
make
```

This builds the self-contained `./encode` and `./decode` executables (no
external dependencies — a range coder and model live in `src/`).

## Usage

```bash
./encode input.wav compressed.nlc
./decode compressed.nlc restored.wav
# or via stdin/stdout:
./encode < input.wav > compressed.nlc
./decode < compressed.nlc > restored.wav
```

## Evaluation

```bash
./eval.sh            # all files in data/
./eval.sh 200        # first 200 files
```

For each file it encodes, decodes, verifies the result is byte-identical, and
reports the aggregate compression ratio. A more detailed spec comparison is
available via `python3 specs_comparison.py`.

## Results

On the full `data/` set (743 files, ~147 MB):

| Metric              | Value                                  |
|---------------------|----------------------------------------|
| Compression ratio   | **3.32x** (lossless, verified)         |
| Encode latency      | ~13 ms / 5 s file (~200x realtime)     |
| Decode latency      | ~11 ms / 5 s file                      |
| Losslessness        | byte-exact on all 743 files            |

For reference, `zlib` delta-coding gets ~2.28x and `xz -9e` gets ~2.91x on the
same data.

## Approach

Analysis of the data revealed the key structure: it is **heavily quantized** —
each file contains only a few hundred distinct sample values, and the next
sample is strongly predicted by the current signal level. The codec exploits
this:

1. **Split** the WAV header off and store it verbatim.
2. **Predict**: code `delta = sample - prev_sample` (zig-zag mapped).
3. **Model**: a two-level PPM-C model over an adaptive range coder
   (`src/codec.h`, `src/rangecoder.h`):
   - *level 1* — context is the quantized previous level (`prev >> 6`), which
     captures the order-1 structure that dominates this data;
   - *level 0* — a global context-free model;
   - *fallback* — a raw 17-bit code for never-before-seen values.
   Each level codes a PPM-C escape symbol to drop to the next; symbol counts
   are halved on overflow.

This gets close to the measured order-1 entropy of the data (~4.4 bits/sample).

## A note on the "200x" target

The challenge README frames the goal as 200x (200 Mbps → 1 Mbps). That is an
**aspirational, system-level bandwidth target**, not an achievable lossless
ratio: the order-0 entropy of the raw signal is already ~6.9 bits/sample and
the order-1 entropy ~4.4 bits/sample, which cap lossless compression at roughly
**3.3–3.6x**. No lossless entry to the public challenge came close to 200x;
reaching that would require lossy compression. This project targets the best
*lossless* ratio, which is what `eval.sh` measures.
