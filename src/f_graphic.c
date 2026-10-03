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
#include <string.h>
#include <math.h>
#include "waddef.h"
#include "stb_image.h"

typedef struct {
    uint8_t r, g, b;
} pal_entry_t;

// Standard 3D Euclidean Color Quantization Matching Loop
static uint8_t find_closest_palette_index(uint8_t r, uint8_t g, uint8_t b, const pal_entry_t *pal) {
    uint8_t best_idx = 0;
    double min_dist = 1000000.0;

    for (int i = 0; i < 256; i++) {
        double dr = (double)r - pal[i].r;
        double dg = (double)g - pal[i].g;
        double db = (double)b - pal[i].b;
        double dist = sqrt(dr*dr + dg*dg + db*db);

        if (dist < min_dist) {
            min_dist = dist;
            best_idx = (uint8_t)i;
        }
    }
    return best_idx;
}

int compile_graphic(const char *image_path, const char *pal_path, const char *output_path, int16_t ox, int16_t oy) {
    // 1. Load the FOSS Master Palette data matrix
    FILE *f_pal = fopen(pal_path, "rb");
    if (!f_pal) {
        fprintf(stderr, "Error: Missing master palette file '%s' for graphic quantization.\n", pal_path);
        return 1;
    }
    pal_entry_t palette[256];
    if (fread(palette, sizeof(pal_entry_t), 256, f_pal) != 256) {
        fprintf(stderr, "Error: Quantization palette map must be at least 768 bytes.\n");
        fclose(f_pal);
        return 1;
    }
    fclose(f_pal);

    // 2. Decode the incoming custom PNG file via stb_image
    int width = 0, height = 0, channels = 0;
    unsigned char *rgba_data = stbi_load(image_path, &width, &height, &channels, 4); // Force 4-channel RGBA
    if (!rgba_data) {
        fprintf(stderr, "Error: stb_image failed to open graphic '%s'\n", image_path);
        return 1;
    }

    // 3. Automated Transparency Bounding Box Trimming Pass
    int min_x = width, max_x = -1;
    int min_y = height, max_y = -1;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t alpha = rgba_data[(y * width + x) * 4 + 3];
            if (alpha > 128) { // Visible pixel threshold boundary
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (y < min_y) min_y = y;
                if (y > max_y) max_y = y;
            }
        }
    }

    // Handle empty transparent fallback states securely
    if (max_x == -1) {
        min_x = max_x = min_y = max_y = 0;
    }

    int crop_w = (max_x - min_x) + 1;
    int crop_h = (max_y - min_y) + 1;

    // Allocate continuous column memory maps
    uint8_t *indexed_pixels = malloc(crop_w * crop_h);
    uint8_t *transparency_mask = malloc(crop_w * crop_h);
    memset(transparency_mask, 0, crop_w * crop_h);

    // 4. Quantize cropped grid regions straight down to 8-bit color index slots
    for (int y = 0; y < crop_h; y++) {
        for (int x = 0; x < crop_w; x++) {
            int src_x = min_x + x;
            int src_y = min_y + y;
            int src_idx = (src_y * width + src_x) * 4;

            uint8_t r = rgba_data[src_idx + 0];
            uint8_t g = rgba_data[src_idx + 1];
            uint8_t b = rgba_data[src_idx + 2];
            uint8_t a = rgba_data[src_idx + 3];

            int target_idx = y * crop_w + x;
            if (a > 128) {
                indexed_pixels[target_idx] = find_closest_palette_index(r, g, b, palette);
                transparency_mask[target_idx] = 1; // Mark as solid data
            } else {
                indexed_pixels[target_idx] = 0;
            }
        }
    }
    stbi_image_free(rgba_data);

    // 5. Open output target stream file container
    FILE *fout = fopen(output_path, "wb");
    if (!fout) {
        fprintf(stderr, "Error: Could not open output path '%s' for writing.\n", output_path);
        free(indexed_pixels); free(transparency_mask);
        return 1;
    }

    // Adjust relative origin offsets based on bounding box trim values
    int16_t final_ox = ox - (int16_t)min_x;
    int16_t final_oy = oy - (int16_t)min_y;

    // Write primary fixed 8-byte Graphic Header
    uint16_t out_w = (uint16_t)crop_w;
    uint16_t out_h = (uint16_t)crop_h;
    fwrite(&out_w, sizeof(uint16_t), 1, fout);
    fwrite(&out_h, sizeof(uint16_t), 1, fout);
    fwrite(&final_ox, sizeof(int16_t), 1, fout);
    fwrite(&final_oy, sizeof(int16_t), 1, fout);

    // Prepare Column Offset Table pointer arrays
    int32_t *col_offsets = malloc(sizeof(int32_t) * crop_w);
    long table_pos = ftell(fout);
    
    // Write a dummy offset table placeholder to step forward in stream
    memset(col_offsets, 0, sizeof(int32_t) * crop_w);
    fwrite(col_offsets, sizeof(int32_t), crop_w, fout);

    // 6. Column-Packed Serialization Loop (Tip image sideways into posts)
    for (int x = 0; x < crop_w; x++) {
        col_offsets[x] = (int32_t)ftell(fout);
        
        int inside_post = 0;
        uint8_t post_start_y = 0;
        uint8_t post_length = 0;
        long post_header_pos = 0;

        for (int y = 0; y < crop_h; y++) {
            int is_solid = transparency_mask[y * crop_w + x];

            if (is_solid && !inside_post) {
                // Initialize a brand-new solid pixel Post block
                inside_post = 1;
                post_start_y = (uint8_t)y;
                post_length = 0;
                post_header_pos = ftell(fout);
                
                // Write placeholder header bytes
                uint8_t dummy_header[2] = { post_start_y, 0 };
                fwrite(dummy_header, 1, 2, fout);
            }

            if (is_solid && inside_post) {
                fwrite(&indexed_pixels[y * crop_w + x], 1, 1, fout);
                post_length++;
            }

            if ((!is_solid || y == crop_h - 1) && inside_post) {
                // Terminate and fix up current active Post header parameters
                inside_post = 0;
                long current_pos = ftell(fout);
                
                // Jump back to overwrite length byte slots securely
                fseek(fout, post_header_pos + 1, SEEK_SET);
                fwrite(&post_length, 1, 1, fout);
                fseek(fout, current_pos, SEEK_SET);
                
                // Enforce standard id Tech 1 legacy padding boundary byte
                uint8_t legacy_pad = 0;
                fwrite(&legacy_pad, 1, 1, fout);
            }
        }
        // Write standard 0xFF (255) column termination marker byte
        uint8_t col_term = 255;
        fwrite(&col_term, 1, 1, fout);
    }

    // 7. Final Table Fixup Pass and Alignment Checks
    long end_of_lump_pos = ftell(fout);
    fseek(fout, table_pos, SEEK_SET);
    fwrite(col_offsets, sizeof(int32_t), crop_w, fout);
    fseek(fout, end_of_lump_pos, SEEK_SET);

    // Enforce Chocolate Doom 4-byte boundary stream alignment restrictions
    long alignment_remainder = end_of_lump_pos % 4;
    if (alignment_remainder != 0) {
        int padding_needed = 4 - alignment_remainder;
        uint8_t zero_padding[3] = {0, 0, 0};
        fwrite(zero_padding, 1, padding_needed, fout);
    }

    fclose(fout);
    free(indexed_pixels); free(transparency_mask); free(col_offsets);
    printf("[FriedenTools] Compiled Graphic successfully: '%s' -> %s (%dx%d, Cropped)\n",
           image_path, output_path, crop_w, crop_h);
    return 0;
}