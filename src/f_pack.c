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
#include <sys/stat.h>
#include "waddef.h"

int execute_pack(const char *src_dir, const char *wad_path, int is_iwad) {
    char script_path[512];
    snprintf(script_path, sizeof(script_path), "%s/wadinfo.txt", src_dir);

    // 1. Open the master muscle-memory layout script
    FILE *script = fopen(script_path, "r");
    if (!script) {
        fprintf(stderr, "Error: Setup file layout missing! Could not open '%s'\n", script_path);
        return 1;
    }

    // 2. Open output target container stream
    FILE *wad = fopen(wad_path, "wb");
    if (!wad) {
        fprintf(stderr, "Error: Could not open output file '%s' for writing.\n", wad_path);
        fclose(script);
        return 1;
    }

    // 3. Write out a temporary 12-byte header block setup
    f_wad_header_t header;
    memcpy(header.magic, is_iwad ? "IWAD" : "PWAD", 4);
    header.num_lumps = 0;
    header.dir_offset = 0;
    fwrite(&header, sizeof(f_wad_header_t), 1, wad);

    int max_lumps = 256;
    f_lump_entry_t *directory = malloc(sizeof(f_lump_entry_t) * max_lumps);
    if (!directory) {
        fprintf(stderr, "Error: Memory allocation failed for directory workspace.\n");
        fclose(wad);
        fclose(script);
        return 1;
    }

    char line[256];
    char target_file_path[512];
    printf("[FriedenTools] Assembling container using master rules: '%s'\n", script_path);

    // 4. Sequential Assembly Loop (Line-by-Line)
    while (fgets(line, sizeof(line), script)) {
        // Strip trailing newline characters cleanly
        line[strcspn(line, "\r\n")] = '\0';

        // Skip blank utility spaces or comment definitions
        if (line[0] == '\0' || line[0] == '#' || line[0] == ';') {
            continue;
        }

        // Map complete path to check for local asset files BEFORE name modifications
        snprintf(target_file_path, sizeof(target_file_path), "%s/%s", src_dir, line);

        // ── BUG 1 FIX: Sanitize name tokens, strip file extensions, force uppercase ──
        char lump_name[9];
        memset(lump_name, 0, sizeof(lump_name));
        
        // Find dot modifier extension splitter pointer
        char *dot_ptr = strchr(line, '.');
        size_t name_len = dot_ptr ? (size_t)(dot_ptr - line) : strlen(line);
        if (name_len > 8) name_len = 8; // Enforce maximum 8-byte rule

        for (size_t j = 0; j < name_len; j++) {
            lump_name[j] = toupper((unsigned char)line[j]);
        }

        struct stat st;
        int32_t file_size = 0;
        int file_exists = (stat(target_file_path, &st) == 0 && S_ISREG(st.st_mode));
        
        if (file_exists) {
            file_size = (int32_t)st.st_size;
        }

        // Dynamically scale mapping arrays if tracking heavily packed mods
        if (header.num_lumps >= max_lumps) {
            max_lumps *= 2;
            f_lump_entry_t *temp = realloc(directory, sizeof(f_lump_entry_t) * max_lumps);
            if (!temp) {
                fprintf(stderr, "Error: Heap exhaustion processing lump references.\n");
                free(directory);
                fclose(wad);
                fclose(script);
                return 1;
            }
            directory = temp;
        }

        // ── BUG 2 FIX: Enforce Strict 4-Byte Int Data Stream Alignment Padding ──
        long current_pos = ftell(wad);
        long alignment_remainder = current_pos % 4;
        if (alignment_remainder != 0) {
            int padding_bytes_needed = 4 - alignment_remainder;
            uint8_t padding_buffer[3] = {0, 0, 0};
            fwrite(padding_buffer, 1, padding_bytes_needed, wad);
        }

        // Populate table indices using finalized aligned position offsets
        f_lump_entry_t *current_entry = &directory[header.num_lumps];
        current_entry->lump_offset = (int32_t)ftell(wad);
        current_entry->lump_size = file_size;
        
        memset(current_entry->lump_name, 0, 8);
        strncpy(current_entry->lump_name, lump_name, 8);

        // Write file bytes if it exists and holds data bytes; else write it as an empty marker
        if (file_exists && file_size > 0) {
            FILE *asset = fopen(target_file_path, "rb");
            if (asset) {
                uint8_t *buffer = malloc(file_size);
                if (buffer) {
                    if (fread(buffer, 1, file_size, asset) == file_size) {
                        fwrite(buffer, 1, file_size, wad);
                    }
                    free(buffer);
                }
                fclose(asset);
            }
        }

        header.num_lumps++;
    }
    fclose(script);

    // ── Enforce alignment for the central directory pointer blocks too! ──
    long current_pos = ftell(wad);
    long alignment_remainder = current_pos % 4;
    if (alignment_remainder != 0) {
        int padding_bytes_needed = 4 - alignment_remainder;
        uint8_t padding_buffer[3] = {0, 0, 0};
        fwrite(padding_buffer, 1, padding_bytes_needed, wad);
    }

    // 5. Build and Append the Central Directory Table Map Block
    header.dir_offset = (int32_t)ftell(wad);
    fwrite(directory, sizeof(f_lump_entry_t), header.num_lumps, wad);

    // 6. Finalize and Overwrite the Header pointer tables
    fseek(wad, 0, SEEK_SET);
    fwrite(&header, sizeof(f_wad_header_t), 1, wad);

    printf("[SUCCESS] Generated %s containing %d items cleanly at: '%s'\n", 
           is_iwad ? "IWAD" : "PWAD", header.num_lumps, wad_path);

    free(directory);
    fclose(wad);
    return 0;
}