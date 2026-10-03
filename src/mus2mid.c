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

#include <stdlib.h>
#include <string.h>
#include "mus2mid.h"

#define MUS_MAGIC  "MUS\x1A"
#define MIDI_MAGIC "MThd"

#define MUS_CMD_RELEASE_NOTE  0
#define MUS_CMD_PLAY_NOTE     1
#define MUS_CMD_PITCH_BEND    2
#define MUS_CMD_SYSTEM_CTRL   3
#define MUS_CMD_CHANGE_CTRL   4
#define MUS_CMD_SCORE_END     6

static void write_var_len(uint8_t **dest, uint32_t value) {
    uint32_t buffer = value & 0x7F;
    while ((value >>= 7) > 0) {
        buffer <<= 8;
        buffer |= 0x80;
        buffer |= (value & 0x7F);
    }
    while (1) {
        *(*dest)++ = (uint8_t)(buffer & 0xFF);
        if (buffer & 0x80) {
            buffer >>= 8;
        } else {
            break;
        }
    }
}

uint8_t* translate_mus_to_midi(const uint8_t *mus_data, size_t mus_size, size_t *out_mid_size) {
    if (!mus_data || mus_size < 4 || !out_mid_size) return NULL;

    if (memcmp(mus_data, MIDI_MAGIC, 4) == 0) {
        uint8_t *midi_copy = (uint8_t*)malloc(mus_size);
        if (!midi_copy) return NULL;
        memcpy(midi_copy, mus_data, mus_size);
        *out_mid_size = mus_size;
        return midi_copy;
    }

    if (memcmp(mus_data, MUS_MAGIC, 4) != 0) return NULL;
    if (mus_size < sizeof(mus_header_t)) return NULL;

    const mus_header_t *header = (const mus_header_t*)mus_data;
    if (header->score_start >= mus_size || (header->score_start + header->score_len) > mus_size) {
        return NULL;
    }

    size_t max_midi_alloc = mus_size * 4 + 1024;
    uint8_t *midi_buf = (uint8_t*)malloc(max_midi_alloc);
    if (!midi_buf) return NULL;

    uint8_t *mid = midi_buf;

    memcpy(mid, "MThd\x00\x00\x00\x06\x00\x00\x00\x01", 12); 
    mid += 12;
    
    *mid++ = 0x00;
    *mid++ = 0x46;

    memcpy(mid, "MTrk\x00\x00\x00\x00", 8);
    uint8_t *track_len_ptr = mid + 4;
    mid += 8;

    uint8_t *track_start = mid;

    uint8_t last_velocity[16];
    for (int i = 0; i < 16; i++) last_velocity[i] = 64;

    const uint8_t *src = mus_data + header->score_start;
    const uint8_t *src_end = src + header->score_len;
    uint32_t delta_ticks = 0;

    while (src < src_end) {
        if ((size_t)(mid - midi_buf) >= max_midi_alloc - 32) break;

        uint8_t event_byte = *src++;
        uint8_t command = (event_byte >> 4) & 7;
        uint8_t channel = event_byte & 15;

        if (channel == 15) channel = 9;

        switch (command) {
            case MUS_CMD_RELEASE_NOTE: {
                uint8_t note = *src++ & 0x7F;
                write_var_len(&mid, delta_ticks);
                delta_ticks = 0;
                *mid++ = (0x80 | channel);
                *mid++ = note;
                *mid++ = 0;
                break;
            }
            case MUS_CMD_PLAY_NOTE: {
                uint8_t note_byte = *src++;
                uint8_t note = note_byte & 0x7F;
                if (note_byte & 0x80) {
                    last_velocity[channel] = *src++ & 0x7F;
                }
                write_var_len(&mid, delta_ticks);
                delta_ticks = 0;
                *mid++ = (0x90 | channel);
                *mid++ = note;
                *mid++ = last_velocity[channel];
                break;
            }
            case MUS_CMD_PITCH_BEND: {
                uint8_t bend = *src++;
                write_var_len(&mid, delta_ticks);
                delta_ticks = 0;
                *mid++ = (0xE0 | channel);
                *mid++ = (bend & 1) << 6;
                *mid++ = (bend >> 1) & 0x7F;
                break;
            }
            case MUS_CMD_SYSTEM_CTRL: {
                uint8_t ctrl = *src++ & 0x7F;
                write_var_len(&mid, delta_ticks);
                delta_ticks = 0;
                *mid++ = (0xB0 | channel);
                if (ctrl == 4) {
                    *mid++ = 121;
                    *mid++ = 0;
                } else {
                    *mid++ = 123;
                    *mid++ = 0;
                }
                break;
            }
            case MUS_CMD_CHANGE_CTRL: {
                uint8_t ctrl = *src++;
                uint8_t val = *src++;
                write_var_len(&mid, delta_ticks);
                delta_ticks = 0;
                *mid++ = (0xB0 | channel);
                
                uint8_t midi_ctrl = 0;
                if (ctrl == 0)      midi_ctrl = 0;
                else if (ctrl == 1) midi_ctrl = 1;
                else if (ctrl == 2) midi_ctrl = 7;
                else if (ctrl == 3) midi_ctrl = 10;
                else if (ctrl == 4) midi_ctrl = 11;
                else if (ctrl == 5) midi_ctrl = 91;
                else if (ctrl == 6) midi_ctrl = 93;

                if (ctrl == 0) {
                    mid[-1] = (0xC0 | channel);
                    *mid++ = val & 0x7F;
                } else {
                    *mid++ = midi_ctrl;
                    *mid++ = val & 0x7F;
                }
                break;
            }
            case MUS_CMD_SCORE_END: {
                src = src_end;
                break;
            }
            default:
                break;
        }

        if (event_byte & 0x80) {
            uint32_t delay = 0;
            while (1) {
                uint8_t d_byte = *src++;
                delay = (delay << 7) | (d_byte & 0x7F);
                if (!(d_byte & 0x80)) break;
            }
            delta_ticks += delay;
        }
    }

    write_var_len(&mid, delta_ticks);
    *mid++ = 0xFF;
    *mid++ = 0x2F;
    *mid++ = 0x00;

    size_t track_length = (size_t)(mid - track_start);
    track_len_ptr[0] = (uint8_t)((track_length >> 24) & 0xFF);
    track_len_ptr[1] = (uint8_t)((track_length >> 16) & 0xFF);
    track_len_ptr[2] = (uint8_t)((track_length >> 8)  & 0xFF);
    track_len_ptr[3] = (uint8_t)(track_length         & 0xFF);

    *out_mid_size = (size_t)(mid - midi_buf);
    return midi_buf;
}