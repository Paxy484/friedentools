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
#include <math.h>
#include "waddef.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

// Architectural engine audio thresholds
#define ENGINE_TARGET_SAMPLE_RATE 11025

int compile_sound(const char *wav_input_path, const char *sfx_output_path) {
    drwav wav;
    
    // Initialize the tracker file handle matrix
    if (!drwav_init_file(&wav, wav_input_path, NULL)) {
        fprintf(stderr, "Error: dr_wav failed to open or parse audio file '%s'\n", wav_input_path);
        return 1;
    }

    uint32_t src_sample_rate = wav.sampleRate;
    uint32_t src_channels = wav.channels;
    uint64_t src_total_frames = wav.totalPCMFrameCount;

    // 1. Allocate heap workspace to harvest raw float samples directly from dr_wav
    // Floats preserve absolute precision during multi-channel downmixing maps
    float *buffer_float = malloc(src_total_frames * src_channels * sizeof(float));
    if (!buffer_float) {
        fprintf(stderr, "Error: Out of memory allocating float decode workspace.\n");
        drwav_uninit(&wav);
        return 1;
    }

    size_t frames_decoded = drwav_read_pcm_frames_f32(&wav, src_total_frames, buffer_float);
    drwav_uninit(&wav);

    if (frames_decoded == 0) {
        fprintf(stderr, "Error: Zero audio frames decoded out of file data streams.\n");
        free(buffer_float);
        return 1;
    }

    // 2. Perform Monaural Channel Mixdown (Stereo to Mono transformation pass)
    float *mono_buffer = malloc(frames_decoded * sizeof(float));
    if (!mono_buffer) {
        fprintf(stderr, "Error: Memory allocation failed during channel mixdown.\n");
        free(buffer_float);
        return 1;
    }

    for (size_t f = 0; f < frames_decoded; f++) {
        float channel_sum = 0.0f;
        for (uint32_t c = 0; c < src_channels; c++) {
            channel_sum += buffer_float[f * src_channels + c];
        }
        // Normalize vector amplitude limits back to a clean -1.0 to 1.0 boundary range
        mono_buffer[f] = channel_sum / (float)src_channels;
    }
    free(buffer_float);

    // 3. Mathematical Downsampling (Linear Interpolation Array Step Pass)
    // Map input frequency ratios cleanly straight down to the 11,025 Hz grid boundary
    double sample_ratio = (double)src_sample_rate / (double)ENGINE_TARGET_SAMPLE_RATE;
    uint32_t target_num_samples = (uint32_t)floor((double)frames_decoded / sample_ratio);
    
    uint8_t *output_samples = malloc(target_num_samples);
    if (!output_samples) {
        fprintf(stderr, "Error: Memory exhaustion allocating downsampled output indices.\n");
        free(mono_buffer);
        return 1;
    }

    for (uint32_t i = 0; i < target_num_samples; i++) {
        double src_index_raw = (double)i * sample_ratio;
        size_t src_index_low = (size_t)floor(src_index_raw);
        size_t src_index_high = src_index_low + 1;

        if (src_index_high >= frames_decoded) {
            src_index_high = frames_decoded - 1;
        }

        double weight_high = src_index_raw - (double)src_index_low;
        double weight_low = 1.0 - weight_high;

        // Linear vector blending interpolation calculation rule
        float blended_sample = (mono_buffer[src_index_low] * (float)weight_low) + 
                               (mono_buffer[src_index_high] * (float)weight_high);

        // Clamp floating point amplitudes safely within hardware safety values (-1.0f to 1.0f)
        if (blended_sample < -1.0f) blended_sample = -1.0f;
        if (blended_sample > 1.0f)  blended_sample = 1.0f;

        // 4. Transform Signed Float to Unsigned 8-bit PCM (DMX Spec: 128 is absolute equilibrium silence)
        int32_t sample_s8 = (int32_t)(blended_sample * 127.0f);
        output_samples[i] = (uint8_t)(sample_s8 + 128);
    }
    free(mono_buffer);

    // 5. Binary Serialization matching strict Vanilla DMX Engine specifications
    FILE *fout = fopen(sfx_output_path, "wb");
    if (!fout) {
        fprintf(stderr, "Error: Could not open output sound path '%s' for writing.\n", sfx_output_path);
        free(output_samples);
        return 1;
    }

    uint16_t format_type = 3; 
    uint16_t target_rate = (uint16_t)ENGINE_TARGET_SAMPLE_RATE;
    
    // CHOCOLATE DOOM VERIFICATION FIX: Header count MUST reflect total written footprint (Samples + 8 padding bytes) + 8 extra bytes
    uint32_t num_samples_header = target_num_samples + 16;

    fwrite(&format_type, sizeof(uint16_t), 1, fout);
    fwrite(&target_rate, sizeof(uint16_t), 1, fout);
    fwrite(&num_samples_header, sizeof(uint32_t), 1, fout);

    // CHOCOLATE DOOM VERIFICATION FIX: Write the 4-byte front padding stabilization ring
    uint8_t first_sample = output_samples[0];
    uint8_t front_pad[4] = { first_sample, first_sample, first_sample, first_sample };
    fwrite(front_pad, 1, 4, fout);

    // Write out the actual raw audio data bytes
    fwrite(output_samples, 1, target_num_samples, fout);

    // CHOCOLATE DOOM VERIFICATION FIX: Write the 4-byte tail padding stabilization ring
    uint8_t last_sample = output_samples[target_num_samples - 1];
    uint8_t tail_pad[4] = { last_sample, last_sample, last_sample, last_sample };
    fwrite(tail_pad, 1, 4, fout);
    
    free(output_samples);

    // 6. Enforce clean 4-byte physical disk structural alignment padding
    long current_pos = ftell(fout);
    long alignment_remainder = current_pos % 4;
    if (alignment_remainder != 0) {
        int padding_bytes_needed = 4 - alignment_remainder;
        uint8_t zero_padding_buffer[3] = {0, 0, 0};
        fwrite(zero_padding_buffer, 1, padding_bytes_needed, fout);
    }

    fclose(fout);
    printf("[FriedenTools] Compiled Sound asset successfully: '%s' -> %s (%u samples, %uHz, Mono)\n",
           wav_input_path, sfx_output_path, target_num_samples, target_rate);

    return 0;
}