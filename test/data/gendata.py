#!/usr/bin/env python3
"""
Generate test files and compress them for DEFLATE/INFLATE testing
Combines raw file generation and compression into a single script
"""

import os
import struct
import random
import zlib
import sys

def ensure_dir(path):
    """Ensure directory exists"""
    os.makedirs(path, exist_ok=True)

def write_file(filename, data):
    """Write data to file"""
    path = f"raw/{filename}"
    with open(path, 'wb') as f:
        if isinstance(data, str):
            f.write(data.encode('utf-8'))
        else:
            f.write(data)
    print(f"Created: {path} ({len(data)} bytes)")

def compress_file(input_path, output_path):
    """Compress a single file using zlib DEFLATE"""
    try:
        with open(input_path, 'rb') as f:
            data = f.read()
        
        # Use zlib.compress with DEFLATE format (no header/trailer)
        # wbits=-15 means raw DEFLATE without zlib header
        compressed = zlib.compress(data, level=6, wbits=-15)
        
        with open(output_path, 'wb') as f:
            f.write(compressed)
        
        ratio = len(compressed) / len(data) * 100 if len(data) > 0 else 0
        print(f"Compressed: {os.path.basename(input_path)} ({len(data)} -> {len(compressed)} bytes, {ratio:.1f}%)")
        
        return True
        
    except Exception as e:
        print(f"Error compressing {input_path}: {e}")
        return False

def compress_file_variants(input_path, base_output_path):
    """Compress a file with multiple compression variants"""
    try:
        with open(input_path, 'rb') as f:
            data = f.read()
        
        if len(data) == 0:
            # Special case for empty files
            compressed = zlib.compress(data, level=1, wbits=-15)
            with open(base_output_path, 'wb') as f:
                f.write(compressed)
            return True
        
        variants = []
        
        # Default compression (level 6)
        compressed = zlib.compress(data, level=6, wbits=-15)
        variants.append(("default", compressed))
        
        # For small files, try different compression strategies
        if len(data) < 1000:
            # No compression (store only) - force with level 0
            try:
                store_only = zlib.compress(data, level=0, wbits=-15)
                variants.append(("store", store_only))
            except:
                pass
                
            # Maximum compression
            try:
                max_comp = zlib.compress(data, level=9, wbits=-15)
                variants.append(("max", max_comp))
            except:
                pass
        
        # Choose the best variant (usually default is fine)
        best_name, best_compressed = variants[0]
        
        with open(base_output_path, 'wb') as f:
            f.write(best_compressed)
        
        ratio = len(best_compressed) / len(data) * 100 if len(data) > 0 else 0
        print(f"Compressed: {os.path.basename(input_path)} ({len(data)} -> {len(best_compressed)} bytes, {ratio:.1f}%) [{best_name}]")
        
        return True
        
    except Exception as e:
        print(f"Error compressing {input_path}: {e}")
        return False

def generate_edge_cases(compressed_dir):
    """Generate specific edge case compressed files"""
    
    # Create some manually crafted edge cases that test specific decompression paths
    edge_cases = {
        # Empty static block
        'empty_static_block': bytes([0x03, 0x00]),
        
        # Single literal in static block
        'single_static_literal': bytes([0xF3, 0x70, 0x04, 0x00]),  # Static 'A' + EOB
        
        # Uncompressed block with exact boundary
        'uncompressed_boundary': bytes([
            0x01,  # BFINAL=1, BTYPE=00
            0xFF, 0xFF,  # LEN=65535 (max)
            0x00, 0x00,  # NLEN=0
        ]) + b'X' * 65535,
        
        # Multiple empty blocks
        'multi_empty_blocks': bytes([
            0x00, 0x00, 0x00, 0xFF, 0xFF,  # Empty uncompressed block (not final)
            0x01, 0x00, 0x00, 0xFF, 0xFF   # Empty uncompressed block (final)
        ]),
        
        # Minimum dynamic block
        'minimal_dynamic': bytes([
            0x05,  # BFINAL=1, BTYPE=10
            0x00, 0x00, 0x00,  # HLIT=0, HDIST=0, HCLEN=0 (minimal)
            0x00, 0x00, 0x00, 0x00,  # Code length codes
            0x00  # Single symbol + EOB
        ])
    }
    
    for name, data in edge_cases.items():
        output_path = os.path.join(compressed_dir, f"edge_{name}")
        try:
            with open(output_path, 'wb') as f:
                f.write(data)
            print(f"Generated edge case: edge_{name} ({len(data)} bytes)")
        except Exception as e:
            print(f"Failed to generate {name}: {e}")

def generate_test_files():
    """Generate all test files"""
    ensure_dir("raw")
    
    print("Generating test files in raw/")
    print("=" * 40)
    
    # 1. Empty file
    write_file("empty", b"")
    
    # 2. Single character
    write_file("single_char", "A")
    
    # 3. Hello
    write_file("hello", "Hello")
    
    # 4. Hello World
    write_file("hello_world", "Hello World")
    
    # 5. ABC pattern
    write_file("abc", "ABC")
    
    # 6. Repeated pattern (LZ77 test)
    write_file("repeated", "ABCABCABC")
    
    # 7. All ASCII printable
    ascii_chars = bytes(range(32, 127))
    write_file("ascii", ascii_chars)
    
    # 8. All bytes 0-255
    all_bytes = bytes(range(256))
    write_file("all_bytes", all_bytes)
    
    # 9. Zeros - highly compressible
    write_file("zeros_1k", bytes(1000))
    write_file("zeros_10k", bytes(10000))
    write_file("zeros_64k", bytes(65536))
    
    # 10. Repeated character - test RLE
    write_file("repeated_a_500", b"A" * 500)
    write_file("repeated_a_258", b"A" * 258)  # Max match length
    
    # 11. JSON data
    json_data = '{"status":"ok","data":[1,2,3],"values":{"a":100,"b":200},"array":[' + ','.join(str(i) for i in range(20)) + ']}'
    write_file("json", json_data)
    
    # 12. HTML fragment
    html = """<!DOCTYPE html>
<html>
<head>
    <title>Test Page</title>
    <meta charset="UTF-8">
</head>
<body>
    <h1>Hello World</h1>
    <p>This is a test paragraph with some <strong>bold</strong> and <em>italic</em> text.</p>
    <ul>
        <li>Item 1</li>
        <li>Item 2</li>
        <li>Item 3</li>
    </ul>
    <div class="container">
        <p>Nested content with repeated patterns.</p>
        <p>Nested content with repeated patterns.</p>
    </div>
</body>
</html>"""
    write_file("html", html)
    
    # 13. Text with repetition
    text = "The quick brown fox jumps over the lazy dog. " * 10
    write_file("text_repeated", text)
    
    # 14. Binary pattern
    binary = bytes((i * 17) & 0xFF for i in range(256))
    write_file("binary_pattern", binary)
    
    # 15. Distance tests - patterns at various distances
    dist_test = bytearray(b"START")
    dist_test.extend(b"." * 990)
    dist_test.extend(b"START")  # Reference 990 bytes back
    write_file("distance_test", dist_test)
    
    # 16. Mixed content
    mixed = bytearray()
    for i in range(1000):
        if i % 100 < 50:
            mixed.append(ord('A') + (i % 26))  # Text
        else:
            mixed.append(i & 0xFF)  # Binary
    write_file("mixed_content", mixed)
    
    # 17. PNG-like data (filter bytes + data)
    png_like = bytearray()
    for y in range(32):
        png_like.append(0)  # Filter byte
        for x in range(31):
            png_like.append((x + y) & 0xFF)
    write_file("png_like", png_like)
    
    # 18. Highly random (incompressible)
    random.seed(0x12345678)
    random_data = bytes(random.randint(0, 255) for _ in range(1000))
    write_file("random", random_data)
    
    # 19. Boundary sizes
    for size in [1, 2, 3, 4, 5, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129, 255, 256, 257]:
        data = bytes((ord('A') + i % 26) for i in range(size))
        write_file(f"size_{size}", data)
    
    # 20. Large text file
    large_text = ("The quick brown fox jumps over the lazy dog. " * 1500)[:65536]
    write_file("large_text_64k", large_text)
    
    # 21. Alternating pattern
    alternating = bytes(0xFF if i & 1 else 0x00 for i in range(65536))
    write_file("alternating_64k", alternating)
    
    # 22. Back-reference distance tests
    for dist in [1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768]:
        if dist <= 1000:
            data = bytearray(b"REF")
            data.extend(b"." * (dist - 3))
            data.extend(b"REF")
            write_file(f"backref_{dist}", data)
    
    # 23. LZ77 specific patterns
    # Pattern that should compress well with LZ77
    lz77_good = b"ABC" * 100 + b"DEF" * 100 + b"ABC" * 100
    write_file("lz77_good", lz77_good)
    
    # Pattern with varying repetition distances
    lz77_varied = b""
    for i in range(10):
        lz77_varied += b"PATTERN" + bytes([i]) * i
    write_file("lz77_varied", lz77_varied)
    
    # 24. Edge cases
    # Almost max uncompressed block size
    write_file("almost_max_block", b"X" * 65534)
    write_file("max_block", b"Y" * 65535)
    
    # Multiple exact block boundaries
    write_file("multiple_blocks", b"Z" * (65535 * 3 + 100))
    
    # 25. Real-world file patterns
    # C source code pattern
    c_code = """#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    printf("Hello, World!\\n");
    for (int i = 0; i < 10; i++) {
        printf("Count: %d\\n", i);
    }
    return 0;
}
""" * 5
    write_file("c_source", c_code)
    
    # CSV data
    csv_data = "Name,Age,City,Country\n"
    for i in range(100):
        csv_data += f"Person{i},{20+i%50},City{i%10},Country{i%5}\n"
    write_file("csv_data", csv_data)
    
    # Base64-like data
    base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
    base64_data = "".join(base64_chars[i % 64] for i in range(1000))
    write_file("base64_like", base64_data)
    
    # 26. Huffman tree edge cases
    # Single byte repeated (should use minimal Huffman tree)
    write_file("single_byte_1k", b"\x42" * 1000)
    
    # Two bytes alternating (simple Huffman tree)
    write_file("two_bytes_alt", bytes(0x41 if i & 1 else 0x42 for i in range(1000)))
    
    # Skewed frequency distribution
    skewed = bytearray()
    for _ in range(900):
        skewed.append(ord('A'))
    for _ in range(90):
        skewed.append(ord('B'))
    for _ in range(9):
        skewed.append(ord('C'))
    skewed.append(ord('D'))
    write_file("skewed_freq", skewed)
    
    # 27. Specific DEFLATE test cases
    # Tests for uncompressed blocks
    write_file("uncompressed_small", b"DATA" * 5)
    write_file("uncompressed_medium", b"UNCOMPRESSED" * 100)
    
    # Tests for static Huffman
    write_file("static_huffman_1", "abcdefghijklmnopqrstuvwxyz" * 20)
    write_file("static_huffman_2", "The quick brown fox jumps over the lazy dog")
    
    # Tests for dynamic Huffman
    write_file("dynamic_huffman_1", "AAAABBBBCCCCDDDDEEEEFFFFGGGGHHHHIIIIJJJJKKKKLLLLMMMMNNNNOOOOPPPPQQQQRRRRSSSSTTTTUUUUVVVVWWWWXXXXYYYYZZZZ")
    write_file("dynamic_huffman_2", "This is a test string with various character frequencies to test dynamic Huffman coding" * 10)
    
    # Tests for multiple blocks
    write_file("multi_block_1", b"BLOCK1" * 10000 + b"BLOCK2" * 10000)
    write_file("multi_block_2", b"A" * 30000 + b"B" * 30000 + b"C" * 30000)
    
    # Tests for match length boundaries
    write_file("match_len_3", b"ABC" + b"XYZ" * 1000 + b"ABC")
    write_file("match_len_4", b"ABCD" + b"XYZ" * 1000 + b"ABCD")
    write_file("match_len_258", b"X" * 258 + b"Y" * 1000 + b"X" * 258)
    
    # Tests for distance boundaries
    write_file("distance_1", b"AB" + b"AB")
    write_file("distance_32768", b"REF" + b"X" * 32765 + b"REF")
    
    # Pathological cases
    write_file("pathological_1", b"ABCDEFGHIJKLMNOPQRSTUVWXYZ" * 2000)
    write_file("pathological_2", bytes(i & 0xFF for i in range(100000)))
    
    # Unicode/UTF-8 content
    write_file("unicode", "Hello 世界 🌍 Testing UTF-8 characters: àáâãäåæçèéêë" * 50)
    
    # Log file pattern
    log_pattern = ""
    for i in range(500):
        log_pattern += f"2024-01-{i%30+1:02d} 12:34:56 INFO: Processing item {i} - Status: OK\n"
    write_file("log_pattern", log_pattern)
    
    # 28. Huffman edge cases (from test_huffman_detail.c)
    # Single symbol files for minimal Huffman trees
    write_file("huffman_single_a", "A" * 1000)
    write_file("huffman_single_z", "Z" * 1000)
    
    # Two symbol files for simple trees
    write_file("huffman_two_symbols", "ABABABABAB" * 100)
    write_file("huffman_ab_pattern", "".join("AB"[i%2] for i in range(2000)))
    
    # Skewed frequency for complex trees
    skewed_text = "A" * 1000 + "B" * 100 + "C" * 10 + "D"
    write_file("huffman_skewed", skewed_text)
    
    # All possible byte values for maximum tree complexity
    write_file("huffman_all_bytes", bytes(range(256)) * 100)
    
    # Specific patterns for distance testing
    for dist in [1, 2, 3, 4, 5, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384]:
        if dist <= 10000:  # Keep reasonable file sizes
            pattern = b"PATTERN" + b"X" * (dist - 7) + b"PATTERN"
            write_file(f"distance_test_{dist}", pattern)
    
    # Length boundary tests
    for length in [3, 4, 5, 6, 7, 8, 9, 10, 11, 258]:
        pattern = b"X" * length + b"Y" * 100 + b"X" * length
        write_file(f"length_test_{length}", pattern)
    
    # 29. Error condition files (will be compressed but expected to decompress properly)
    # These test the compressor's handling of edge cases
    write_file("edge_empty_repeat", b"" + b"A" * 100)
    write_file("edge_single_then_repeat", b"X" + b"Y" * 257)  # Single + max length
    write_file("edge_max_uncompressed", b"Z" * 65535)  # Exactly max uncompressed block
    
    # 30. Real-world simulation patterns
    # PNG-like data (common in image processing)
    png_simulation = bytearray()
    for row in range(100):
        png_simulation.append(0)  # Filter byte
        # Gradient pattern
        for col in range(100):
            png_simulation.extend([(row + col) & 0xFF, row & 0xFF, col & 0xFF, 0xFF])
    write_file("png_simulation", png_simulation)
    
    # HTTP-like headers
    http_headers = ""
    for i in range(50):
        http_headers += f"Cache-Control: max-age=3600\r\n"
        http_headers += f"Content-Type: application/json\r\n"
        http_headers += f"X-Request-ID: req_{i:06d}\r\n"
        http_headers += f"Date: Mon, 01 Jan 2024 12:00:00 GMT\r\n"
    write_file("http_headers", http_headers)
    
    # ZIP-like directory structure
    zip_structure = ""
    for i in range(200):
        zip_structure += f"folder_{i//20}/subfolder_{i//5}/file_{i:04d}.txt\x00"
        zip_structure += f"This is file content for file number {i} " * 3 + "\x00"
    write_file("zip_structure", zip_structure)
    
    # 31. Bit alignment edge cases
    # Files designed to test non-byte-aligned operations
    write_file("bit_align_1", b"A")  # 1 byte
    write_file("bit_align_2", b"AB")  # 2 bytes  
    write_file("bit_align_3", b"ABC")  # 3 bytes
    write_file("bit_align_7", b"ABCDEFG")  # 7 bytes
    write_file("bit_align_9", b"ABCDEFGHI")  # 9 bytes
    write_file("bit_align_15", b"ABCDEFGHIJKLMNO")  # 15 bytes
    write_file("bit_align_17", b"ABCDEFGHIJKLMNOPQ")  # 17 bytes
    
    # 32. Compression ratio tests
    # Highly compressible
    write_file("compress_excellent", b"A" * 10000)
    write_file("compress_good", ("The quick brown fox jumps over the lazy dog. " * 200))
    
    # Poorly compressible  
    write_file("compress_poor", bytes((i * 17 + 83) & 0xFF for i in range(10000)))
    
    # 33. Real file format simulations
    # XML-like structure
    xml_content = '<?xml version="1.0" encoding="UTF-8"?>\n<root>\n'
    for i in range(100):
        xml_content += f'  <item id="{i}" name="item_{i}" value="{i*2}">\n'
        xml_content += f'    <description>This is item number {i}</description>\n'
        xml_content += f'    <metadata type="test" created="2024-01-01"/>\n'
        xml_content += f'  </item>\n'
    xml_content += '</root>\n'
    write_file("xml_content", xml_content)
    
    # CSS-like repeated patterns
    css_content = ""
    for i in range(100):
        css_content += f".class-{i} {{\n"
        css_content += f"  color: #ff{i:02x}{i:02x};\n"
        css_content += f"  font-size: {12 + i % 20}px;\n"
        css_content += f"  margin: {i % 10}px;\n"
        css_content += f"  padding: {i % 5}px;\n"
        css_content += f"}}\n\n"
    write_file("css_content", css_content)

def compress_all_files():
    """Compress all files from raw/ to compressed/"""
    # Get current working directory and paths
    current_dir = os.getcwd()
    raw_dir = os.path.join(current_dir, "raw")
    compressed_dir = os.path.join(current_dir, "compressed")
    
    print(f"\nCompressing files from raw/ to compressed/")
    print("=" * 50)
    
    if not os.path.exists(raw_dir):
        print(f"Error: {raw_dir} directory not found!")
        return False
    
    ensure_dir(compressed_dir)
    
    raw_files = sorted([f for f in os.listdir(raw_dir) if os.path.isfile(os.path.join(raw_dir, f))])
    
    if not raw_files:
        print(f"No files found in {raw_dir}/")
        return False
    
    print(f"Found {len(raw_files)} files to compress")
    
    success_count = 0
    total_original = 0
    total_compressed = 0
    
    # Special handling for specific test cases
    special_cases = {
        'empty': True,
        'single_char': True, 
        'bit_align_1': True,
        'bit_align_2': True,
        'bit_align_3': True
    }
    
    for filename in raw_files:
        input_path = os.path.join(raw_dir, filename)
        output_path = os.path.join(compressed_dir, filename)
        
        # Use variant compression for special cases, regular for others
        if filename in special_cases:
            success = compress_file_variants(input_path, output_path)
        else:
            success = compress_file(input_path, output_path)
            
        if success:
            success_count += 1
            total_original += os.path.getsize(input_path)
            total_compressed += os.path.getsize(output_path)
    
    # Generate some edge case compressed files manually
    generate_edge_cases(compressed_dir)
    
    print(f"\nCompression complete!")
    print(f"Successfully compressed: {success_count}/{len(raw_files)} files")
    
    if total_original > 0:
        overall_ratio = total_compressed / total_original * 100
        print(f"Overall compression: {total_original} -> {total_compressed} bytes ({overall_ratio:.1f}%)")
    
    print(f"\nCompressed files are in: {compressed_dir}/")
    print("Now you can test your DEFLATE decompressor against these files!")
    
    return success_count == len(raw_files)

def main():
    """Main function to generate raw files and compress them"""
    print("=== DEFLATE Test Data Generator ===")
    print("Generating raw test files and compressing them\n")
    
    # Step 1: Generate raw test files
    generate_test_files()
    
    file_count = len(os.listdir('raw'))
    print(f"\nGenerated {file_count} test files")
    
    # Step 2: Compress all files
    success = compress_all_files()
    
    if success:
        print("\n=== Success! ===")
        print("Test data generation complete.")
        print("You can now run your DEFLATE tests with:")
        print("  cd ../..")
        print("  make run_tests")
        print("  # or")
        print("  make test")
    else:
        print("\n=== Error ===")
        print("Some files failed to compress.")
        return 1
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
