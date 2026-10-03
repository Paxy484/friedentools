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
#include "waddef.h"

// Forward declarations of your modular tool components
void print_help(const char *exec_name);
int execute_unpack(const char *wad_path, const char *dest_dir, int enable_mus2mid);
int execute_pack(const char *src_dir, const char *wad_path, int is_iwad);
int compile_textures(const char *texture_txt_path, const char *pnames_output_wad, const char *texture1_output_wad);
int compile_flat(const char *image_input_path, const char *flat_output_path);
int compile_sound(const char *wav_input_path, const char *sfx_output_path);
int compile_graphic(const char *image_path, const char *pal_path, const char *output_path, int16_t ox, int16_t oy);

int main(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        return 0;
    }

    // ── 1. Unpacker Routing Branch (With Order-Independent Flag Scanner) ──
    if (strcmp(argv[1], "-u") == 0 || strcmp(argv[1], "--unpack") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Error: Missing arguments for unpacking.\n");
            fprintf(stderr, "Usage: %s --unpack <input.wad> <output_directory> [--mus2mid]\n", argv[0]);
            return 1;
        }

        const char *wad_path = NULL;
        const char *dest_dir = NULL;
        int enable_mus2mid = 0;
        int positional_args = 0;

        // Parse remaining strings dynamically to tolerate any argument switch layout positioning
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--mus2mid") == 0) {
                enable_mus2mid = 1;
            } else {
                if (positional_args == 0) {
                    wad_path = argv[i];
                    positional_args++;
                } else if (positional_args == 1) {
                    dest_dir = argv[i];
                    positional_args++;
                }
            }
        }

        if (!wad_path || !dest_dir) {
            fprintf(stderr, "Error: Missing positional parameters for unpacking.\n");
            fprintf(stderr, "Usage: %s --unpack <input.wad> <output_directory> [--mus2mid]\n", argv[0]);
            return 1;
        }

        return execute_unpack(wad_path, dest_dir, enable_mus2mid);
    } 
    
    // ── 2. Packer Routing Branch ──
    else if (strcmp(argv[1], "-p") == 0 || strcmp(argv[1], "--pack") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Error: Missing arguments for packing.\n");
            fprintf(stderr, "Usage: %s --pack <input_directory> <output.wad> <IWAD|PWAD>\n", argv[0]);
            return 1;
        }
        
        int is_iwad = 0;
        if (strcasecmp(argv[4], "IWAD") == 0) {
            is_iwad = 1;
        } else if (strcasecmp(argv[4], "PWAD") != 0) {
            fprintf(stderr, "Error: Invalid container type '%s'. Must be IWAD or PWAD.\n", argv[4]);
            return 1;
        }
        
        return execute_pack(argv[2], argv[3], is_iwad);
    }
    
    // ── 3. Texture/PNAMES Compiler Routing Branch ──
    else if (strcmp(argv[1], "-t") == 0 || strcmp(argv[1], "--texture") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Error: Missing arguments for texture compilation.\n");
            fprintf(stderr, "Usage: %s --texture <texture.txt> <output_pnames.lmp> <output_texture1.lmp>\n", argv[0]);
            return 1;
        }
        return compile_textures(argv[2], argv[3], argv[4]);
    }

    // ── 4. Flat Compiler Routing Branch (64x64 Floor/Ceiling) ──
    else if (strcmp(argv[1], "-f") == 0 || strcmp(argv[1], "--flat") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Error: Missing arguments for Flat compilation.\n");
            fprintf(stderr, "Usage: %s --flat <input_image.png> <output_flat.lmp>\n", argv[0]);
            return 1;
        }
        return compile_flat(argv[2], argv[3]);
    }

    // ── 5. Sound Compiler Routing Branch (WAV to 8-bit DMX Format) ──
    else if (strcmp(argv[1], "-s") == 0 || strcmp(argv[1], "--sound") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Error: Missing arguments for Sound compilation.\n");
            fprintf(stderr, "Usage: %s --sound <input_audio.wav> <output_sound.lmp>\n", argv[0]);
            return 1;
        }
        return compile_sound(argv[2], argv[3]);
    }

    // ── 6. Patch/Sprite Graphic Compiler Routing Branch (Quantized Columns) ──
    else if (strcmp(argv[1], "-g") == 0 || strcmp(argv[1], "--graphic") == 0) {
        if (argc < 7) {
            fprintf(stderr, "Error: Missing arguments for Graphic compilation.\n");
            fprintf(stderr, "Usage: %s --graphic <input.png> <playpal.lmp> <output.lmp> <origin_x> <origin_y>\n", argv[0]);
            return 1;
        }
        return compile_graphic(argv[2], argv[3], argv[4], (int16_t)atoi(argv[5]), (int16_t)atoi(argv[6]));
    }

    fprintf(stderr, "Error: Unrecognized option '%s'. Type '%s --help' for options.\n", argv[1], argv[0]);
    return 1;
}

void print_help(const char *exec_name) {
    printf("Usage: %s [options]\n\n", exec_name);
    printf("Options:\n");
    printf("  -h, --help                        Show parameter reference\n");
    printf("  -u, --unpack <wad> <dir> [--mus2mid] Extract WAD (Optional: transcode MUS chunks to MID)\n");
    printf("  -p, --pack <dir> <wad> <type>     Compile directory layout to WAD container\n");
    printf("  -t, --texture <txt> <pn> <tx1>    Compile text layouts to binary PNAMES & TEXTURE1 lumps\n");
    printf("  -f, --flat <image> <output.lmp>   Compile a 64x64 PNG file into a raw 4KB Flat lump\n");
    printf("  -s, --sound <wav> <output.lmp>    Compile a WAV file into an 8-bit raw Sound lump\n");
    printf("  -g, --graphic <png> <pal> <out> <x> <y> Convert transparent PNG into Indexed Graphic (Doom Format)\n");
}