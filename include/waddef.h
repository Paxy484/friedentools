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

#ifndef WADDEF_H
#define WADDEF_H

#include <stdint.h>

// ── AGNOSTIC WAD SYSTEM STRUCTURES ──

// Agnostic 12-byte master container header
typedef struct {
    char magic[4];       // "IWAD" or "PWAD"
    int32_t num_lumps;   // Total discrete entries
    int32_t dir_offset;  // Byte position where directory layout starts
} __attribute__((packed)) f_wad_header_t;

// Agnostic 16-byte individual lump directory entry
typedef struct {
    int32_t lump_offset; // Absolute file position of the raw binary chunk
    int32_t lump_size;   // Data length in bytes
    char lump_name[8];   // Agnostic 8-character string identifier (padded with \0)
} __attribute__((packed)) f_lump_entry_t;


// ── COMMUNITY TEXTURE SYSTEM STRUCTURES (id Tech 1 Specs) ──

// PNAMES (Patch Names Lump): A flat list of every distinct wall graphic used
typedef struct {
    int32_t num_map_patches; // Total number of individual patch string names
    // Followed immediately by: char patch_names[num_map_patches][8]
} __attribute__((packed)) f_pnames_header_t;

// TEXTURE1/TEXTURE2 Master Layout Header
typedef struct {
    int32_t num_textures;    // Total number of composite textures defined in this lump
    // Followed immediately by: int32_t texture_offsets[num_textures] pointers
} __attribute__((packed)) f_texture_header_t;

// Composite Wall Texture Mapping Metadata Record
typedef struct {
    char name[8];          // 8-character uppercase texture identifier string
    int32_t masked;          // Flag (usually 0 or 1 for transparent masking configurations)
    int16_t width;           // Composite wall texture width in pixels
    int16_t height;          // Composite wall texture height in pixels
    int32_t col_dir;         // Obsolete padding column directory pointer block (always 0)
    int16_t num_patches;     // Number of individual patch pieces that stitch this wall together
    // Followed immediately by: f_patch_map_t patches[num_patches]
} __attribute__((packed)) f_texture_map_t;

// Individual Patch Positioning Blueprint within a Composite Texture
typedef struct {
    int16_t origin_x;        // Pixel column offset where the patch starts drawing relative to texture left
    int16_t origin_y;        // Pixel row offset where the patch starts drawing relative to texture top
    int16_t patch_index;     // Zero-based index referencing the patch's name location inside PNAMES
    int16_t step_dir;        // Obsolete rendering step calculation block (always 1)
    int16_t colormap;        // Obsolete lighting colormap placeholder (always 0)
} __attribute__((packed)) f_patch_map_t;

#endif // WADDEF_H