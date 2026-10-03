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
#include <sys/stat.h>
#include <sys/types.h>
#include "waddef.h"
#include "mus2mid.h"

// ── CRITICAL CROSS-PLATFORM MACRO FIX: Handle POSIX vs Windows UCRT64 ──
static void safe_mkdir(const char *path) {
#if defined(_WIN32) || defined(_WIN64)
    mkdir(path);      // Windows UCRT only accepts a single directory path argument parameter
#else
    mkdir(path, 0755); // Standard Linux, FreeBSD, and POSIX permissions bitmask pass
#endif
}

// Check if a lump name matches standard map data components
static int is_map_component(const char *name) {
    const char *components[] = {
        "THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS",
        "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP", "BEHAVIOR"
    };
    for (size_t i = 0; i < sizeof(components) / sizeof(components[0]); i++) {
        if (strcmp(name, components[i]) == 0) return 1;
    }
    return 0;
}

// Check if a lump name is a map header (e.g. MAP01 or E1M1)
static int is_map_header(const char *name) {
    if (name[0] == 'M' && name[1] == 'A' && name[2] == 'P') return 1; // MAPxx
    if (name[0] == 'E' && name[2] == 'M') return 1;                  // ExMx
    return 0;
}

int execute_unpack(const char *wad_path, const char *dest_dir, int enable_mus2mid) {
    FILE *wad = fopen(wad_path, "rb");
    if (!wad) {
        fprintf(stderr, "Error: Failed to open WAD file '%s'\n", wad_path);
        return 1;
    }

    f_wad_header_t header;
    if (fread(&header, sizeof(f_wad_header_t), 1, wad) != 1) {
        fprintf(stderr, "Error: Truncated or invalid WAD header.\n");
        fclose(wad);
        return 1;
    }

    if (strncmp(header.magic, "IWAD", 4) != 0 && strncmp(header.magic, "PWAD", 4) != 0) {
        fprintf(stderr, "Safety Check FAILED: Invalid magic bytes '%.4s'.\n", header.magic);
        fclose(wad);
        return 1;
    }

    // Base output folder
    safe_mkdir(dest_dir);

    if (fseek(wad, header.dir_offset, SEEK_SET) != 0) {
        fprintf(stderr, "Error: Corrupted directory table offset.\n");
        fclose(wad);
        return 1;
    }

    f_lump_entry_t *directory = malloc(sizeof(f_lump_entry_t) * header.num_lumps);
    if (!directory) {
        fprintf(stderr, "Error: Memory allocation failed for directory indices.\n");
        fclose(wad);
        return 1;
    }

    if (fread(directory, sizeof(f_lump_entry_t), header.num_lumps, wad) != header.num_lumps) {
        fprintf(stderr, "Error: Failed to read complete directory table structures.\n");
        free(directory);
        fclose(wad);
        return 1;
    }

    printf("[FriedenTools] Extracting %d lumps into structured directory: '%s'\n", header.num_lumps, dest_dir);

    // State Tracking variables
    char current_namespace[32] = "";
    char current_map[32] = "";

    char clean_name[9];
    char sub_dir_path[1024];
    char target_path[1024];
    
    for (int i = 0; i < header.num_lumps; i++) {
        memcpy(clean_name, directory[i].lump_name, 8);
        clean_name[8] = '\0';

        // ── 1. Handle Namespace Fences (Markers) ──
        if (strstr(clean_name, "_START") != NULL) {
            char prefix[32];
            strcpy(prefix, clean_name);
            char *token = strtok(prefix, "_");
            if (token) {
                snprintf(current_namespace, sizeof(current_namespace), "%s", token);
                snprintf(sub_dir_path, sizeof(sub_dir_path), "%s/%s", dest_dir, current_namespace);
                safe_mkdir(sub_dir_path);
            }
        }

        // ── 2. Handle Map Contexts ──
        if (is_map_header(clean_name)) {
            snprintf(current_map, sizeof(current_map), "%s", clean_name);
            snprintf(sub_dir_path, sizeof(sub_dir_path), "%s/%s", dest_dir, current_map);
            safe_mkdir(sub_dir_path);
        } else if (current_map[0] != '\0' && !is_map_component(clean_name)) {
            current_map[0] = '\0';
        }

        // ── 3. Resolve Target Pathing Blueprint ──
        if (current_map[0] != '\0' && is_map_component(clean_name)) {
            snprintf(target_path, sizeof(target_path), "%s/%s/%s", dest_dir, current_map, clean_name);
        } else if (current_namespace[0] != '\0' && strstr(clean_name, "_END") == NULL && strstr(clean_name, "_START") == NULL) {
            snprintf(target_path, sizeof(target_path), "%s/%s/%s", dest_dir, current_namespace, clean_name);
        } else {
            snprintf(target_path, sizeof(target_path), "%s/%s", dest_dir, clean_name);
        }

        // Handle 0-byte markers or headers safely
        if (directory[i].lump_size == 0) {
            FILE *marker = fopen(target_path, "wb");
            if (marker) fclose(marker);
            
            if (strstr(clean_name, "_END") != NULL) {
                current_namespace[0] = '\0';
            }
            continue;
        }

        // ── 4. Write Raw Binary Block to Disk (With Optional Overwrite Transcoding) ──
        fseek(wad, directory[i].lump_offset, SEEK_SET);
        uint8_t *lump_buffer = malloc(directory[i].lump_size);
        if (!lump_buffer) {
            fprintf(stderr, "Error: Memory allocation failed for lump '%s'.\n", clean_name);
            continue;
        }

        if (fread(lump_buffer, 1, directory[i].lump_size, wad) == directory[i].lump_size) {
            
            // Check if this lump contains the unique binary MUS file signature format
            int is_mus_lump = (directory[i].lump_size >= 4 && memcmp(lump_buffer, "MUS\x1A", 4) == 0);

            if (enable_mus2mid && is_mus_lump) {
                size_t midi_size = 0;
                uint8_t *midi_data = translate_mus_to_midi(lump_buffer, directory[i].lump_size, &midi_size);
                
                if (midi_data != NULL) {
                    // Overwrite destination path logic directly to target the standard .mid extension
                    char midi_target_path[1024];
                    snprintf(midi_target_path, sizeof(midi_target_path), "%s.mid", target_path);
                    
                    FILE *output_file = fopen(midi_target_path, "wb");
                    if (output_file) {
                        fwrite(midi_data, 1, midi_size, output_file);
                        fclose(output_file);
                    }
                    free(midi_data);
                } else {
                    // Safe verification fallback: write unmodified data bytes if transcoding fails
                    FILE *output_file = fopen(target_path, "wb");
                    if (output_file) {
                        fwrite(lump_buffer, 1, directory[i].lump_size, output_file);
                        fclose(output_file);
                    }
                }
            } else {
                // Stock Output State: Writes file assets exactly as unhampered .mus / .lmp data blocks
                FILE *output_file = fopen(target_path, "wb");
                if (output_file) {
                    fwrite(lump_buffer, 1, directory[i].lump_size, output_file);
                    fclose(output_file);
                } else {
                    fprintf(stderr, "Error: Failed to write data file '%s'\n", target_path);
                }
            }
        }
        free(lump_buffer);
    }

    printf("[SUCCESS] Unpacking completed cleanly.\n");
    free(directory);
    fclose(wad);
    return 0;
}