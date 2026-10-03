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
#include <string.h>
#include <ctype.h>
#include "waddef.h"

// Max allocation limits for the compiler workspace
#define MAX_WORKSPACE_PATCHES 4096
#define MAX_WORKSPACE_TEXTURES 2048
#define MAX_PATCHES_PER_TEXTURE 64

// Structs to hold the textual layout data while parsing
typedef struct {
    char name[8];
    int16_t width;
    int16_t height;
    int16_t num_patches;
    f_patch_map_t patches[MAX_PATCHES_PER_TEXTURE];
} workspace_texture_t;

// Utility helper to trim trailing spacing and newlines
static void trim_string(char *str) {
    int len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

int compile_textures(const char *texture_txt_path, const char *pnames_output_wad, const char *texture1_output_wad) {
    FILE *script = fopen(texture_txt_path, "r");
    if (!script) {
        fprintf(stderr, "Error: Could not open texture layout script '%s'\n", texture_txt_path);
        return 1;
    }

    // 1. Allocate workspace arrays to harvest tokens
    char (*pnames_list)[8] = malloc(MAX_WORKSPACE_PATCHES * 8);
    int pnames_count = 0;

    workspace_texture_t *textures_list = malloc(sizeof(workspace_texture_t) * MAX_WORKSPACE_TEXTURES);
    int textures_count = 0;

    if (!pnames_list || !textures_list) {
        fprintf(stderr, "Error: Memory allocation failed for texture compiler workspace.\n");
        if (pnames_list) free(pnames_list);
        if (textures_list) free(textures_list);
        fclose(script);
        return 1;
    }

    char line[256];
    workspace_texture_t *current_tex = NULL;

    printf("[FriedenTools] Parsing texture script: '%s'...\n", texture_txt_path);

    // 2. Token Harvesting Pass (Line-by-Line Text Scanning)
    while (fgets(line, sizeof(line), script)) {
        trim_string(line);

        // Skip blank lines and comments
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
            continue;
        }

        char token[32];
        if (sscanf(line, "%31s", token) != 1) continue;

        // Command Token: Initiate a new texture declaration block
        if (strcasecmp(token, "[TEXTURE]") == 0) {
            if (textures_count >= MAX_WORKSPACE_TEXTURES) {
                fprintf(stderr, "Error: Exceeded maximum text texture compilation limit (%d).\n", MAX_WORKSPACE_TEXTURES);
                break;
            }
            current_tex = &textures_list[textures_count++];
            memset(current_tex, 0, sizeof(workspace_texture_t));
            continue;
        }

        if (!current_tex) continue; // Ignore loose metadata if no texture block is open

        // Parse individual attribute keys inside an active texture block
        if (strcasecmp(token, "NAME") == 0) {
            char tex_name[32];
            if (sscanf(line, "%*s %31s", tex_name) == 1) {
                memset(current_tex->name, 0, 8);
                strncpy(current_tex->name, tex_name, 8);
                // Force uppercase to align with standard engine layout tables
                for (int j = 0; j < 8 && current_tex->name[j]; j++) {
                    current_tex->name[j] = toupper((unsigned char)current_tex->name[j]);
                }
            }
        } 
        else if (strcasecmp(token, "WIDTH") == 0) {
            int val;
            if (sscanf(line, "%*s %d", &val) == 1) current_tex->width = (int16_t)val;
        } 
        else if (strcasecmp(token, "HEIGHT") == 0) {
            int val;
            if (sscanf(line, "%*s %d", &val) == 1) current_tex->height = (int16_t)val;
        } 
        else if (strcasecmp(token, "PATCH") == 0) {
            if (current_tex->num_patches >= MAX_PATCHES_PER_TEXTURE) {
                fprintf(stderr, "Warning: Texture '%-8s' exceeded max patch composition slots.\n", current_tex->name);
                continue;
            }

            char patch_name[32];
            int ox, oy;
            if (sscanf(line, "%*s %31s %d %d", patch_name, &ox, &oy) == 3) {
                // Force uppercase patch string name alignment
                for (int j = 0; patch_name[j]; j++) {
                    patch_name[j] = toupper((unsigned char)patch_name[j]);
                }

                // Check if patch name is already recorded in the global PNAMES array table
                int found_pname_idx = -1;
                for (int p = 0; p < pnames_count; p++) {
                    if (strncmp(pnames_list[p], patch_name, 8) == 0) {
                        found_pname_idx = p;
                        break;
                    }
                }

                // If it is a completely new patch graphic, register it inside PNAMES dictionary
                if (found_pname_idx == -1) {
                    if (pnames_count >= MAX_WORKSPACE_PATCHES) {
                        fprintf(stderr, "Error: Global PNAMES unique layout bounds exceeded.\n");
                        continue;
                    }
                    found_pname_idx = pnames_count++;
                    memset(pnames_list[found_pname_idx], 0, 8);
                    strncpy(pnames_list[found_pname_idx], patch_name, 8);
                }

                // Link the patch positioning blueprint parameters directly
                f_patch_map_t *p_map = &current_tex->patches[current_tex->num_patches++];
                p_map->origin_x = (int16_t)ox;
                p_map->origin_y = (int16_t)oy;
                p_map->patch_index = (int16_t)found_pname_idx;
                p_map->step_dir = 1;  // Obsolete standard legacy constraint padding
                p_map->colormap = 0;  // Obsolete standard legacy constraint padding
            }
        }
    }
    fclose(script);

    // 3. Binary Serialization: Build and Write PNAMES container file
    FILE *f_pnames = fopen(pnames_output_wad, "wb");
    if (!f_pnames) {
        fprintf(stderr, "Error: Could not open output path '%s' for writing.\n", pnames_output_wad);
        free(pnames_list); free(textures_list);
        return 1;
    }

    f_pnames_header_t pn_head = { .num_map_patches = pnames_count };
    fwrite(&pn_head, sizeof(f_pnames_header_t), 1, f_pnames);
    fwrite(pnames_list, 8, pnames_count, f_pnames);
    fclose(f_pnames);
    printf("[FriedenTools] Compiled PNAMES binary block with %d patches.\n", pnames_count);

    // 4. Binary Serialization: Build and Write TEXTURE1 composition map lump file
    FILE *f_tex1 = fopen(texture1_output_wad, "wb");
    if (!f_tex1) {
        fprintf(stderr, "Error: Could not open output path '%s' for writing.\n", texture1_output_wad);
        free(pnames_list); free(textures_list);
        return 1;
    }

    f_texture_header_t tx_head = { .num_textures = textures_count };
    fwrite(&tx_head, sizeof(f_texture_header_t), 1, f_tex1);

    // First section inside TEXTURE1 lump: Pass an array of int32 absolute file offsets
    int32_t *offset_table = malloc(sizeof(int32_t) * textures_count);
    if (!offset_table) {
        free(pnames_list); free(textures_list); fclose(f_tex1);
        return 1;
    }

    // Calculate where the data block records start drawing right after the pointer table block
    int32_t running_offset = sizeof(f_texture_header_t) + (sizeof(int32_t) * textures_count);

    for (int t = 0; t < textures_count; t++) {
        offset_table[t] = running_offset;
        
        // Step forward by base size + patch structures array size
        int32_t record_size = sizeof(f_texture_map_t) + (sizeof(f_patch_map_t) * textures_list[t].num_patches);
        
        // ── CRITICAL FIX 1: Align offset_table indices for memory line safety ──
        int32_t remainder = record_size % 4;
        if (remainder != 0) {
            record_size += (4 - remainder);
        }
        
        running_offset += record_size;
    }
    fwrite(offset_table, sizeof(int32_t), textures_count, f_tex1);
    free(offset_table);

    // Final section: Write out the packed structure bytes of the textures and nested maps sequentially
    for (int t = 0; t < textures_count; t++) {
        workspace_texture_t *src = &textures_list[t];
        
        f_texture_map_t out_tex;
        memset(&out_tex, 0, sizeof(f_texture_map_t));
        memcpy(out_tex.name, src->name, 8); // Uses array size standard from header
        out_tex.masked = 0;
        out_tex.width = src->width;
        out_tex.height = src->height;
        out_tex.col_dir = 0;
        out_tex.num_patches = src->num_patches;

        fwrite(&out_tex, sizeof(f_texture_map_t), 1, f_tex1);
        fwrite(src->patches, sizeof(f_patch_map_t), src->num_patches, f_tex1);
// ── CRITICAL FIX 2: Write explicit zero-padding streams into physical WAD ──
int32_t exact_bytes_written = sizeof(f_texture_map_t) + (sizeof(f_patch_map_t) * src->num_patches);
int32_t stream_remainder = exact_bytes_written % 4;
    if (stream_remainder != 0) {
int padding_needed = 4 - stream_remainder;
uint8_t padding_bytes[3] = {0, 0, 0};
        fwrite(padding_bytes, 1, padding_needed, f_tex1);
    }
}

fclose(f_tex1);
printf("[FriedenTools] Compiled TEXTURE1 composition map block with %d textures.\n", textures_count);
printf("[SUCCESS] Texture compilation assets generated successfully.\n");
free(pnames_list);
free(textures_list);

return 0;
}