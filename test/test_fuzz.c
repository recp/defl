/*
 * Fuzz testing support for DEFLATE/INFLATE library
 * Can be used with AFL, libFuzzer, or standalone
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <defl/infl.h>

/* Platform-specific includes */
#ifdef _WIN32
#include <io.h>
#define read _read
#else
#include <unistd.h>
#endif

/* Maximum output buffer size for fuzzing */
#define MAX_OUTPUT_SIZE (1024 * 1024)  /* 1MB */

/* LibFuzzer entry point */
#ifdef LIBFUZZER
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static uint8_t *output = NULL;
    
    /* Allocate output buffer once */
    if (!output) {
        output = malloc(MAX_OUTPUT_SIZE);
        if (!output) return 0;
    }
    
    /* Try as raw DEFLATE */
    infl_buf(data, size, output, MAX_OUTPUT_SIZE, 0);
    
    /* Try as ZLIB */
    infl_buf(data, size, output, MAX_OUTPUT_SIZE, INFL_ZLIB);
    
    /* Try chunked input with various chunk sizes */
    if (size > 10) {
        infl_stream_t *stream = infl_init(output, MAX_OUTPUT_SIZE, 0);
        if (stream) {
            /* Random chunk boundaries */
            size_t pos = 0;
            while (pos < size) {
                size_t chunk = 1 + (data[pos] % 256);
                if (pos + chunk > size) chunk = size - pos;
                infl_include(stream, data + pos, chunk);
                pos += chunk;
            }
            infl(stream);
            infl_destroy(stream);
        }
    }
    
    return 0;
}
#endif

/* AFL persistent mode support */
#ifdef AFL_PERSISTENT

int main(int argc, char *argv[]) {
    uint8_t *data = malloc(100000);
    uint8_t *output = malloc(MAX_OUTPUT_SIZE);
    
    if (!data || !output) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }
    
#ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
#endif
    
    while (__AFL_LOOP(10000)) {
        /* Read from stdin */
#ifdef _WIN32
        size_t size = _read(0, data, 100000);
#else
        size_t size = read(0, data, 100000);
#endif
        
        if (size > 0) {
            /* Test various configurations */
            infl_buf(data, size, output, MAX_OUTPUT_SIZE, 0);
            infl_buf(data, size, output, MAX_OUTPUT_SIZE, INFL_ZLIB);
            
            /* Test with small output buffer */
            infl_buf(data, size, output, 100, 0);
        }
    }
    
    free(data);
    free(output);
    return 0;
}
#endif

/* Standalone fuzzer for testing */
#if !defined(LIBFUZZER) && !defined(AFL_PERSISTENT)

static uint32_t rng_state = 0x12345678;

static uint32_t simple_rand(void) {
    rng_state = rng_state * 1103515245 + 12345;
    return rng_state;
}

/* Generate semi-valid DEFLATE data for better coverage */
static size_t generate_fuzz_input(uint8_t *out, size_t max_size) {
    size_t size = (simple_rand() % max_size) + 1;
    size_t pos = 0;
    
    /* Randomly choose format */
    int format = simple_rand() % 4;
    
    switch (format) {
    case 0: /* Valid uncompressed block */
        if (size >= 10) {
            out[pos++] = 0x01;  /* BFINAL=1, BTYPE=00 */
            uint16_t len = simple_rand() % 100;
            out[pos++] = len & 0xFF;
            out[pos++] = (len >> 8) & 0xFF;
            out[pos++] = ~(len & 0xFF);
            out[pos++] = ~((len >> 8) & 0xFF);
            
            for (int i = 0; i < len && pos < size; i++) {
                out[pos++] = simple_rand() & 0xFF;
            }
        }
        break;
        
    case 1: /* Valid static block */
        out[pos++] = 0x03;  /* BFINAL=1, BTYPE=01 */
        while (pos < size - 1) {
            /* Random static Huffman codes */
            if (simple_rand() % 10 == 0) {
                out[pos++] = 0x00;  /* EOB */
                break;
            } else {
                out[pos++] = 0x30 + (simple_rand() % 144);
            }
        }
        break;
        
    case 2: /* ZLIB wrapped */
        out[pos++] = 0x78;
        out[pos++] = 0x9C;
        /* Fall through to random data */
        
    default: /* Random data */
        while (pos < size) {
            out[pos++] = simple_rand() & 0xFF;
        }
        break;
    }
    
    return pos;
}

/* Mutation strategies */
static void mutate_data(uint8_t *data, size_t size) {
    int strategy = simple_rand() % 5;
    
    switch (strategy) {
    case 0: /* Bit flip */
        if (size > 0) {
            size_t idx = simple_rand() % size;
            data[idx] ^= 1 << (simple_rand() % 8);
        }
        break;
        
    case 1: /* Byte replacement */
        if (size > 0) {
            size_t idx = simple_rand() % size;
            data[idx] = simple_rand() & 0xFF;
        }
        break;
        
    case 2: /* Insert byte */
        if (size > 1) {
            size_t idx = simple_rand() % (size - 1);
            memmove(data + idx + 1, data + idx, size - idx - 1);
            data[idx] = simple_rand() & 0xFF;
        }
        break;
        
    case 3: /* Delete byte */
        if (size > 2) {
            size_t idx = simple_rand() % (size - 1);
            memmove(data + idx, data + idx + 1, size - idx - 1);
        }
        break;
        
    case 4: /* Shuffle chunk */
        if (size > 10) {
            size_t start = simple_rand() % (size - 10);
            size_t len = (simple_rand() % 10) + 1;
            for (size_t i = 0; i < len / 2; i++) {
                uint8_t tmp = data[start + i];
                data[start + i] = data[start + len - 1 - i];
                data[start + len - 1 - i] = tmp;
            }
        }
        break;
    }
}

int main(int argc, char *argv[]) {
    uint8_t *data = malloc(100000);
    uint8_t *output = malloc(MAX_OUTPUT_SIZE);
    int iterations = 10000;
    int crashes = 0;
    int errors = 0;
    int success = 0;
    
    if (!data || !output) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }
    
    if (argc > 1) {
        iterations = atoi(argv[1]);
    }
    
    printf("Running %d fuzz iterations...\n", iterations);
    
    for (int i = 0; i < iterations; i++) {
        /* Generate or mutate input */
        size_t size;
        if (i % 2 == 0) {
            size = generate_fuzz_input(data, 10000);
        } else {
            size = (simple_rand() % 10000) + 1;
            for (size_t j = 0; j < size; j++) {
                data[j] = simple_rand() & 0xFF;
            }
            mutate_data(data, size);
        }
        
        /* Test decompression */
        int ret = infl_buf(data, size, output, MAX_OUTPUT_SIZE, 0);
        
        if (ret == UNZ_OK) {
            success++;
        } else if (ret == UNZ_ERR || ret == UNZ_EFULL) {
            errors++;
        } else {
            crashes++;
            printf("Unexpected return code %d at iteration %d\n", ret, i);
        }
        
        /* Progress */
        if ((i + 1) % 1000 == 0) {
            printf("Progress: %d/%d (success=%d, errors=%d, crashes=%d)\n",
                   i + 1, iterations, success, errors, crashes);
        }
    }
    
    printf("\nFuzz test complete:\n");
    printf("  Total iterations: %d\n", iterations);
    printf("  Successful: %d (%.1f%%)\n", success, 100.0 * success / iterations);
    printf("  Expected errors: %d (%.1f%%)\n", errors, 100.0 * errors / iterations);
    printf("  Crashes/Issues: %d\n", crashes);
    
    free(data);
    free(output);
    
    return crashes > 0 ? 1 : 0;
}
#endif /* Standalone fuzzer */