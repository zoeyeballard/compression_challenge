# compression_challenge
Attempt at Neuralink's Compression Challenge (https://content.neuralink.com/compression-challenge/README.html)

## Building

```bash
make
```

This builds `./encode` and `./decode` executables.

## Testing Specs Comparison

To see how the current implementation compares to the original Neuralink Compression Challenge requirements:

```bash
python3 specs_comparison.py
```

This will:
- Test compression on sample files from the `data/` directory
- Measure compression ratio, latency, and verify losslessness
- Output a detailed comparison table showing:
  - Current implementation specs
  - Original requirements
  - Status (meets/doesn't meet each requirement)

### Original Requirements Summary:
- **Compression Ratio**: > 200x (200Mbps -> 1Mbps)
- **Latency**: < 1ms (real-time processing)
- **Power**: < 10mW (including radio)
- **Lossless**: Yes (required)
- **Data Format**: 16-bit mono PCM WAV files, 5 seconds per file
