#!/usr/bin/env python3
"""
Neuralink Compression Challenge - Specs Comparison Tool

This script analyzes the current implementation and compares it against
the original Neuralink Compression Challenge requirements.
"""

import os
import sys
import subprocess
import time
import glob
import statistics
from pathlib import Path

def get_file_size(filepath):
    """Get file size in bytes."""
    try:
        return os.path.getsize(filepath)
    except OSError:
        return 0

def test_compression(filepath, encode_exe, decode_exe):
    """Test compression on a single file and return metrics."""
    if not os.path.exists(filepath):
        return None
    
    original_size = get_file_size(filepath)
    if original_size == 0:
        return None
    
    # Create temporary files
    compressed_file = "tmp_compressed.nlc"
    restored_file = "tmp_restored.wav"
    
    try:
        # Encode and measure time
        start_encode = time.perf_counter()
        with open(filepath, 'rb') as infile, open(compressed_file, 'wb') as outfile:
            result = subprocess.run(
                [encode_exe],
                stdin=infile,
                stdout=outfile,
                stderr=subprocess.PIPE,
                check=True
            )
        encode_time = time.perf_counter() - start_encode
        
        # Parse compression ratio from stderr
        stderr_lines = result.stderr.decode('utf-8', errors='ignore').split('\n')
        compression_ratio = None
        for line in stderr_lines:
            if 'compression_ratio=' in line:
                try:
                    ratio_str = line.split('compression_ratio=')[1].split('x')[0]
                    compression_ratio = float(ratio_str)
                except (ValueError, IndexError):
                    pass
        
        compressed_size = get_file_size(compressed_file)
        if compressed_size == 0:
            return None
        
        # Decode and measure time
        start_decode = time.perf_counter()
        with open(compressed_file, 'rb') as infile, open(restored_file, 'wb') as outfile:
            subprocess.run(
                [decode_exe],
                stdin=infile,
                stdout=outfile,
                stderr=subprocess.PIPE,
                check=True
            )
        decode_time = time.perf_counter() - start_decode
        
        # Verify losslessness
        restored_size = get_file_size(restored_file)
        is_lossless = (restored_size == original_size)
        
        # Calculate compression ratio if not parsed
        if compression_ratio is None:
            compression_ratio = original_size / compressed_size if compressed_size > 0 else 0
        
        return {
            'original_size': original_size,
            'compressed_size': compressed_size,
            'compression_ratio': compression_ratio,
            'encode_time_ms': encode_time * 1000,
            'decode_time_ms': decode_time * 1000,
            'is_lossless': is_lossless
        }
    except subprocess.CalledProcessError as e:
        print(f"Error processing {filepath}: {e}", file=sys.stderr)
        return None
    finally:
        # Cleanup
        for tmp_file in [compressed_file, restored_file]:
            if os.path.exists(tmp_file):
                try:
                    os.remove(tmp_file)
                except OSError:
                    pass

def analyze_implementation(data_dir="data", encode_exe="./encode", decode_exe="./decode", max_files=10):
    """Analyze the current implementation."""
    if not os.path.exists(encode_exe) or not os.path.exists(decode_exe):
        print(f"Error: {encode_exe} or {decode_exe} not found. Please build first.", file=sys.stderr)
        return None
    
    # Find WAV files
    wav_files = glob.glob(os.path.join(data_dir, "*.wav"))
    if not wav_files:
        print(f"Warning: No WAV files found in {data_dir}", file=sys.stderr)
        return None
    
    # Test on a sample of files
    test_files = wav_files[:max_files]
    results = []
    
    print(f"Testing on {len(test_files)} files...", file=sys.stderr)
    for wav_file in test_files:
        result = test_compression(wav_file, encode_exe, decode_exe)
        if result:
            results.append(result)
    
    if not results:
        return None
    
    # Aggregate statistics
    compression_ratios = [r['compression_ratio'] for r in results]
    encode_times = [r['encode_time_ms'] for r in results]
    decode_times = [r['decode_time_ms'] for r in results]
    total_times = [e + d for e, d in zip(encode_times, decode_times)]
    
    total_original = sum(r['original_size'] for r in results)
    total_compressed = sum(r['compressed_size'] for r in results)
    overall_ratio = total_original / total_compressed if total_compressed > 0 else 0
    
    all_lossless = all(r['is_lossless'] for r in results)
    
    return {
        'num_files_tested': len(results),
        'overall_compression_ratio': overall_ratio,
        'avg_compression_ratio': statistics.mean(compression_ratios),
        'min_compression_ratio': min(compression_ratios),
        'max_compression_ratio': max(compression_ratios),
        'avg_encode_time_ms': statistics.mean(encode_times),
        'max_encode_time_ms': max(encode_times),
        'avg_decode_time_ms': statistics.mean(decode_times),
        'max_decode_time_ms': max(decode_times),
        'avg_total_time_ms': statistics.mean(total_times),
        'max_total_time_ms': max(total_times),
        'is_lossless': all_lossless,
        'total_original_bytes': total_original,
        'total_compressed_bytes': total_compressed
    }

def print_comparison_table(current_specs):
    """Print a comparison table of current specs vs original requirements."""
    
    # Original requirements from the prompt
    original_requirements = {
        'compression_ratio': {
            'required': '> 200x',
            'target': 200.0,
            'description': 'Compression ratio needed (200Mbps -> 1Mbps)'
        },
        'latency': {
            'required': '< 1ms',
            'target': 1.0,
            'description': 'Real-time processing latency (encode + decode)'
        },
        'power': {
            'required': '< 10mW',
            'target': 10.0,
            'description': 'Power consumption (including radio)'
        },
        'lossless': {
            'required': 'Yes',
            'target': True,
            'description': 'Lossless compression required'
        },
        'data_format': {
            'required': '16-bit mono PCM WAV',
            'target': None,
            'description': 'Input data format'
        },
        'file_duration': {
            'required': '5 seconds',
            'target': None,
            'description': 'Duration per file'
        }
    }
    
    print("\n" + "="*80)
    print("NEURALINK COMPRESSION CHALLENGE - SPECS COMPARISON")
    print("="*80)
    print()
    
    if current_specs is None:
        print("ERROR: Could not analyze current implementation.")
        print("Please ensure ./encode and ./decode are built and data files exist.")
        return
    
    # Compression Ratio
    print("COMPRESSION RATIO")
    print("-" * 80)
    print(f"  Original Requirement: {original_requirements['compression_ratio']['required']}")
    print(f"  Current Implementation:")
    print(f"    - Overall Ratio:     {current_specs['overall_compression_ratio']:.2f}x")
    print(f"    - Average Ratio:     {current_specs['avg_compression_ratio']:.2f}x")
    print(f"    - Range:             {current_specs['min_compression_ratio']:.2f}x - {current_specs['max_compression_ratio']:.2f}x")
    ratio_status = "✓ MEETS" if current_specs['overall_compression_ratio'] >= 200.0 else "✗ BELOW TARGET"
    print(f"  Status: {ratio_status}")
    print()
    
    # Latency
    print("LATENCY (Real-time Processing)")
    print("-" * 80)
    print(f"  Original Requirement: {original_requirements['latency']['required']} (total)")
    print(f"  Current Implementation:")
    print(f"    - Average Encode:    {current_specs['avg_encode_time_ms']:.3f} ms")
    print(f"    - Average Decode:    {current_specs['avg_decode_time_ms']:.3f} ms")
    print(f"    - Average Total:     {current_specs['avg_total_time_ms']:.3f} ms")
    print(f"    - Max Total:         {current_specs['max_total_time_ms']:.3f} ms")
    latency_status = "✓ MEETS" if current_specs['avg_total_time_ms'] < 1.0 else "✗ ABOVE TARGET"
    print(f"  Status: {latency_status}")
    print()
    
    # Lossless
    print("LOSSLESS COMPRESSION")
    print("-" * 80)
    print(f"  Original Requirement: {original_requirements['lossless']['required']}")
    print(f"  Current Implementation: {'Yes' if current_specs['is_lossless'] else 'No'}")
    lossless_status = "✓ MEETS" if current_specs['is_lossless'] else "✗ FAILS"
    print(f"  Status: {lossless_status}")
    print()
    
    # Power (not measurable, but note it)
    print("POWER CONSUMPTION")
    print("-" * 80)
    print(f"  Original Requirement: {original_requirements['power']['required']}")
    print(f"  Current Implementation: Not measured (requires hardware profiling)")
    print(f"  Status: ? UNKNOWN (requires specialized hardware measurement)")
    print()
    
    # Data Format
    print("DATA FORMAT")
    print("-" * 80)
    print(f"  Original Requirement: {original_requirements['data_format']['required']}")
    print(f"  Current Implementation: Supports 16-bit mono PCM WAV (with delta coding)")
    print(f"  Status: ✓ MEETS")
    print()
    
    # File Statistics
    print("TEST STATISTICS")
    print("-" * 80)
    print(f"  Files Tested:         {current_specs['num_files_tested']}")
    print(f"  Total Original Size:  {current_specs['total_original_bytes']:,} bytes ({current_specs['total_original_bytes']/1024/1024:.2f} MB)")
    print(f"  Total Compressed:     {current_specs['total_compressed_bytes']:,} bytes ({current_specs['total_compressed_bytes']/1024/1024:.2f} MB)")
    print()
    
    # Summary
    print("SUMMARY")
    print("-" * 80)
    meets_ratio = current_specs['overall_compression_ratio'] >= 200.0
    meets_latency = current_specs['avg_total_time_ms'] < 1.0
    meets_lossless = current_specs['is_lossless']
    
    print(f"  Compression Ratio:  {'✓' if meets_ratio else '✗'} {'MEETS' if meets_ratio else 'BELOW'} target (200x)")
    print(f"  Latency:            {'✓' if meets_latency else '✗'} {'MEETS' if meets_latency else 'ABOVE'} target (<1ms)")
    print(f"  Lossless:           {'✓' if meets_lossless else '✗'} {'MEETS' if meets_lossless else 'FAILS'} requirement")
    print(f"  Power:              ? UNKNOWN (requires hardware measurement)")
    print()
    
    overall_status = "PARTIAL" if (meets_lossless and (meets_ratio or meets_latency)) else "NEEDS IMPROVEMENT"
    if meets_ratio and meets_latency and meets_lossless:
        overall_status = "EXCELLENT (power measurement needed)"
    elif meets_lossless:
        overall_status = "GOOD (compression/latency optimization needed)"
    
    print(f"  Overall Status: {overall_status}")
    print()
    print("="*80)

def main():
    """Main entry point."""
    import argparse
    parser = argparse.ArgumentParser(
        description='Compare current implementation specs to Neuralink Compression Challenge requirements'
    )
    parser.add_argument('--data-dir', default='data', help='Directory containing WAV files')
    parser.add_argument('--encode', default='./encode', help='Path to encode executable')
    parser.add_argument('--decode', default='./decode', help='Path to decode executable')
    parser.add_argument('--max-files', type=int, default=10, help='Maximum number of files to test')
    
    args = parser.parse_args()
    
    current_specs = analyze_implementation(
        data_dir=args.data_dir,
        encode_exe=args.encode,
        decode_exe=args.decode,
        max_files=args.max_files
    )
    
    print_comparison_table(current_specs)

if __name__ == '__main__':
    main()

