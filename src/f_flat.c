/*
 * Copyright 2026 Rafael Peixoto
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, 
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, 
 *    this list of conditions and the following disclaimer in the documentation 
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its 
 *    contributors may be used to endorse or promote products derived from 
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "waddef.h"

// We instantiate the implementation definition wrapper *only* in this source file
#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#include "stb_image.h"

int compile_flat(const char *image_input_path, const char *flat_output_path) {
    int width = 0;
    int height = 0;
    int channels = 0;

    // Force stb_image to downsample the file to a 1-channel 8-bit unsigned byte stream (0-255)
    unsigned char *image_data = stbi_load(image_input_path, &width, &height, &channels, 1);
    
    if (!image_data) {
        fprintf(stderr, "Error: stb_image failed to decode file '%s' (Reason: %s)\n", 
                image_input_path, stbi_failure_reason());
        return 1;
    }

    // Strict validation check of id Tech 1 architectural constraints
    if (width != 64 || height != 64) {
        fprintf(stderr, "Error: Flat surface asset dimensions must be exactly 64x64 pixels!\n");
        fprintf(stderr, "  File '%s' is resolved as %dx%d\n", image_input_path, width, height);
        stbi_image_free(image_data);
        return 1;
    }

    FILE *fout = fopen(flat_output_path, "wb");
    if (!fout) {
        fprintf(stderr, "Error: Could not open output flat path '%s' for writing.\n", flat_output_path);
        stbi_image_free(image_data);
        return 1;
    }

    // Write out the exact, raw 4,096 bytes directly to disk with zero structural headers
    size_t bytes_written = fwrite(image_data, 1, 4096, fout);
    fclose(fout);
    stbi_image_free(image_data);

    if (bytes_written != 4096) {
        fprintf(stderr, "Error: Truncated file stream write block. Only wrote %zu of 4096 bytes.\n", bytes_written);
        return 1;
    }

    printf("[FriedenTools] Compiled Flat asset successfully: '%s' -> %s (4096 bytes written)\n", 
           image_input_path, flat_output_path);
    return 0;
}