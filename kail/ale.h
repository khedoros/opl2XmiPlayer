#pragma once
#include<stdint.h>
#include "yamaha.h"

// These are the state tracking for the actually executing sound effects
uint16_t S_f_offset[NUM_SLOTS];
uint16_t S_f_counter[NUM_SLOTS];
uint16_t S_f_val[NUM_SLOTS];
uint16_t S_f_increment[NUM_SLOTS];

uint16_t S_v0_offset[NUM_SLOTS];
uint16_t S_v0_counter[NUM_SLOTS];
uint16_t S_v0_val[NUM_SLOTS];
uint16_t S_v0_increment[NUM_SLOTS];

uint16_t S_v1_offset[NUM_SLOTS];
uint16_t S_v1_counter[NUM_SLOTS];
uint16_t S_v1_val[NUM_SLOTS];
uint16_t S_v1_increment[NUM_SLOTS];

uint16_t S_p_offset[NUM_SLOTS];
uint16_t S_p_counter[NUM_SLOTS];
uint16_t S_p_val[NUM_SLOTS];
uint16_t S_p_increment[NUM_SLOTS];

uint16_t S_fb_offset[NUM_SLOTS];
uint16_t S_fb_counter[NUM_SLOTS];
uint16_t S_fb_val[NUM_SLOTS];
uint16_t S_fb_increment[NUM_SLOTS];

uint16_t S_m0_offset[NUM_SLOTS];
uint16_t S_m0_counter[NUM_SLOTS];
uint16_t S_m0_val[NUM_SLOTS];
uint16_t S_m0_increment[NUM_SLOTS];

uint16_t S_m1_offset[NUM_SLOTS];
uint16_t S_m1_counter[NUM_SLOTS];
uint16_t S_m1_val[NUM_SLOTS];
uint16_t S_m1_increment[NUM_SLOTS];

uint16_t S_ws_offset[NUM_SLOTS];
uint16_t S_ws_counter[NUM_SLOTS];
uint16_t S_ws_val[NUM_SLOTS];
uint16_t S_ws_increment[NUM_SLOTS];


// ;This struct defines the non-variable length portion of a TVFX definition.
// ;It's invariably followed by chunks of data specifying time-variant changes to make to the MIDI/OPL controls.

typedef struct TVFX {
    uint16_t T_length; // 00,01
    uint8_t T_transpose; // 02
    uint8_t T_type; // 03; 1 = TV_inst, 2 = TV_effect?
    uint16_t T_duration; // 04, 05 duration in 120Hz ticks?

    uint16_t T_init_f_val; // 06, 07
    uint16_t T_init_f_offset; // 08, 09
    uint16_t T_f_rel_offset; // 0a, 0b

    uint16_t T_init_v0_val;    // dw ?; 0C, 0D
    uint16_t T_init_v0_offset; // dw ?; 0E, 0F
    uint16_t T_v0_rel_offset;  // dw ?; 10, 11

    uint16_t T_init_v1_val;    // dw ?; 12, 13
    uint16_t T_init_v1_offset; // dw ?; 14, 15
    uint16_t T_v1_rel_offset;  // dw ?; 16, 17

    uint16_t T_init_p_val;    // dw ? ; 18, 19
    uint16_t T_init_p_offset; // dw ? ; 1A, 1B
    uint16_t T_p_rel_offset;  // dw ? ; 1C, 1D

    uint16_t T_init_fb_val;    // dw ?; 1E, 1F
    uint16_t T_init_fb_offset; // dw ?; 20, 21
    uint16_t T_fb_rel_offset;  // dw ?; 22, 23

    uint16_t T_init_m0_val;    // dw ?; 24, 25
    uint16_t T_init_m0_offset; // dw ?; 26, 27
    uint16_t T_m0_rel_offset;  // dw ?; 28, 29

    uint16_t T_init_m1_val;    // dw ?; 2A, 2B
    uint16_t T_init_m1_offset; // dw ?; 2C, 2D
    uint16_t T_m1_rel_offset;  // dw ?; 2E, 2F

    uint16_t T_init_ws_val;    // dw ?; 30, 31
    uint16_t T_init_ws_offset; // dw ?; 32, 33
    uint16_t T_ws_rel_offset;  // dw ?; 34, 35

    // ;These next 8 fields are present in some TVFX structs, but in others, default values are used.
    // ;The only way to differentiate is with the T_init_f_offset value. It is 34h if these values are missing and 3Ch if they're present.
    uint8_t T_play_ad_1;    // db ? ; 36
    uint8_t T_play_sr_1;    // db ? ; 37
    uint8_t T_play_ad_0;    // db ? ; 38
    uint8_t T_play_sr_0;    // db ? ; 39
    uint8_t T_rel_ad_1;     // db ? ; 3A
    uint8_t T_rel_sr_1;     // db ? ; 3B
    uint8_t T_rel_ad_0;     // db ? ; 3C
    uint8_t T_rel_sr_0;     // db ? ; 3D

    uint16_t data[200]; // Stand-in for the variable-size chunks of command-lists
};

void TVFX_switch_voice() {
    for(int i = 0; i < NUM_SLOTS; i++) {
        if(S_status[i] == FREE) continue;
        if(S_voice[i] != -1) continue;
        for(int j = 0; j < NUM_VOICES; j++) {
            if(V_channel[j] != -1) continue;
            S_voice[i] = j;
            MIDI_voices[S_channel[i]]++;
            V_channel[j] = S_channel[i];
            S_update[i] = U_ALL_REGS;
            update_priority();
        }
    }
}

// ; Subroutine is passed a slot number and a pointer to one of the offset variables associated with a register value to modify.
void TVFX_increment_stage(int slotIndex, uint16_t* offset) {
    // TODO: Re-arrange storage of these values to allow for a more elegant method than this
    uint16_t *counter;
    uint16_t *val;
    uint16_t *increment;
    if(offset == &S_f_offset) {
        counter = &S_f_counter[slotIndex]; val = &S_f_val[slotIndex]; increment = &S_f_increment[slotIndex];
    }
    else if(offset == &S_v0_offset) {
        counter = &S_v0_counter[slotIndex]; val = &S_v0_val[slotIndex]; increment = &S_v0_increment[slotIndex];
    }
    else if(offset == &S_v1_offset) {
        counter = &S_v1_counter[slotIndex]; val = &S_v1_val[slotIndex]; increment = &S_v1_increment[slotIndex];
    }
    else if(offset == &S_fb_offset) {
        counter = &S_fb_counter[slotIndex]; val = &S_fb_val[slotIndex]; increment = &S_fb_increment[slotIndex];
    }
    else if(offset == &S_m0_offset) {
        counter = &S_m0_counter[slotIndex]; val = &S_m0_val[slotIndex]; increment = &S_m0_increment[slotIndex];
    }
    else if(offset == &S_m1_offset) {
        counter = &S_m1_counter[slotIndex]; val = &S_m1_val[slotIndex]; increment = &S_m1_increment[slotIndex];
    }

    union opl2Bank_t* timbre = S_timbre_ptr[slotIndex];
    // si has slot number, bx points to slot's offset variable, di points to the offset in the timbre's command list
    int dataOffset = offset[slotIndex] - timbre->tv.T_init_f_offset;

    for(int cmd = 0; cmd < 10; cmd++) {
        uint16_t command = timbre->tv.data[dataOffset];
        uint16_t data = timbre->tv.data[dataOffset+1];
        if(command == 0) {
            dataOffset += (data / 2); // suspect that the data values are set up to jump by 2 bytes
            offset[slotIndex] += (data / 2);
        }
        else {
            dataOffset += 2;
            offset[slotIndex] += 2;
            if(command == 0xffff) { // ax == -1 (FFFFh): Set current value
                *val = data;
            }
            else if(command == 0xfffe) { // ax == -2 (FFFEh): Directly update specific Yamaha register
                if(offset == &S_f_offset) {
                    S_BLOCK[slotIndex] = data>>8;
                    if(S_type[slotIndex] == TV_INST) {
                        S_BLOCK[slotIndex] &= 0xe0;
                    }
                }
                else if(offset == &S_v0_offset) {
                    S_KSLTL_0[slotIndex] = data & 0xff;
                }
                else if(offset == &S_v1_offset) {
                    S_KSLTL_1[slotIndex] = data & 0xff;
                }
                else if(offset == &S_fb_offset) {
                    S_FBC[slotIndex] = data>>8;
                }
                else if(offset == &S_m0_offset) {
                    S_AVEKM_0[slotIndex] = data & 0xff;
                }
                else if(offset == &S_m1_offset) {
                    S_AVEKM_1[slotIndex] = data & 0xff;
                }
            }
            else {
                *counter = command;
                *increment = data;
                return;
            }
        }
    }
    *increment = 0;
    *counter = 0xffff;
}

// ; Subroutine handles setting values to update when necessary, decrementing TVFX counters, calling the TVFX iterator when they run out, etc
// ; Also handles calling the update_priority subroutine 5 times a second
void serve_synth() {
    TV_accum += 60;
    if(TV_accum >= 120) {
        TV_accum -= 120;
        vol_update ^= U_KSLTL;
        for(int slot=0; slot < NUM_SLOTS; slot++) {
            if(S_status[slot] == FREE) continue;
            if(S_type[slot] == BNK_INST) continue;

            // Increment frequency
            if(S_f_increment[slot] != 0) {
                S_f_val[slot] += S_f_increment[slot];
                S_update[slot] |= U_FREQ;
            }
            S_f_counter[slot]--;
            if(S_f_counter[slot] == 0) {
                TVFX_increment_stage(slot, &S_f_offset);
                S_update[slot] |= U_FREQ;
            }

            // Increment feedback
            if(S_fb_increment[slot] != 0) {
                S_fb_val[slot] += S_fb_increment[slot];
                S_update[slot] |= U_FBC;
            }
            S_fb_counter[slot]--;
            if(S_fb_counter[slot] == 0) {
                TVFX_increment_stage(slot, &S_fb_offset);
                S_update[slot] |= U_FBC;
            }

            // Increment modulator multiplier
            if(S_m0_increment[slot] != 0) {
                S_m0_val[slot] += S_m0_increment[slot];
                S_update[slot] |= U_AVEKM;
            }
            S_m0_counter[slot]--;
            if(S_m0_counter[slot] == 0) {
                TVFX_increment_stage(slot, &S_m0_offset);
                S_update[slot] |= U_AVEKM;
            }

            // Increment carrier multiplier
            if(S_m1_increment[slot] != 0) {
                S_m1_val[slot] += S_m1_increment[slot];
                S_update[slot] |= U_AVEKM;
            }
            S_m1_counter[slot]--;
            if(S_m1_counter[slot] == 0) {
                TVFX_increment_stage(slot, &S_m1_offset);
                S_update[slot] |= U_AVEKM;
            }

            // Increment volume0
            if(S_v0_increment[slot] != 0) {
                uint16_t previous = S_v0_val[slot];
                S_v0_val[slot] += S_v0_increment[slot];
                if(S_status[slot] == KEYOFF) {
                    uint16_t newVal = S_v0_val[slot];
                    if(previous ^ newVal > 0x8000) {
                        newVal ^= S_v0_increment[slot];
                        if(newVal < 0x8000) {
                            S_v0_val[slot] = 0;
                        }
                    }
                }
                S_update[slot] |= vol_update;
            }
            S_v0_counter[slot]--;
            if(S_v0_counter[slot] == 0) {
                TVFX_increment_stage(slot, &S_v0_offset);
                S_update[slot] |= U_KSLTL;
            }

            // Increment volume1
            if(S_v1_increment[slot] != 0) {
                uint16_t previous = S_v1_val[slot];
                S_v1_val[slot] += S_v1_increment[slot];
                if(S_status[slot] == KEYOFF) {
                    uint16_t newVal = S_v1_val[slot];
                    if(previous ^ newVal > 0x8000) {
                        newVal ^= S_v1_increment[slot];
                        if(newVal < 0x8000) {
                            S_v1_val[slot] = 0;
                        }
                    }
                }
                S_update[slot] |= vol_update;
            }
            S_v1_counter[slot]--;
            if(S_v1_counter[slot] == 0) {
                TVFX_increment_stage(slot, &S_v1_offset);
                S_update[slot] |= U_KSLTL;
            }

            // Increment waveform select
            if(S_ws_increment[slot] != 0) {
                S_ws_val[slot] += S_ws_increment[slot];
                S_update[slot] |= U_WS;
            }
            S_ws_counter[slot]--;
            if(S_ws_counter[slot] == 0) {
                TVFX_increment_stage(slot, &S_ws_offset);
                S_update[slot] |= U_WS;
            }

            S_p_val[slot] += S_p_increment[slot];
            S_p_counter[slot]--;
            if(S_p_counter[slot] == 0) {
                TVFX_increment_stage(slot, &S_p_offset);
            }

            if(S_update[slot] != 0) {
                update_voice(slot);
            }

            if(!S_status[slot] == KEYOFF) {
                S_duration[slot]--;
                if(S_duration[slot] > 0) {
                    continue;
                }
                else {
                    S_status[slot] = KEYOFF;
                    TV_phase(slot);
                    continue;
                }
            }
            if(S_v0_val[slot] >= 0x400 || S_v1_val[slot] >= 0x400) {
                continue;
            }

            release_voice(slot);
            S_status[slot] = FREE;
            TVFX_switch_voice();
        }
    }

    pri_accum += 5;
    if(pri_accum >= 120) {
        pri_accum -= 120;
        update_priority();
    }
}

// ; Does initial set-up when a TVFX effect/instrument is loaded, and also handles when it transitions to KEYOFF state
void TV_phase(int slotIndex) {
    S_FBC[slotIndex] = 0;
    S_KSLTL_0[slotIndex] = 0;
    S_KSLTL_1[slotIndex] = 0;
    S_AVEKM_0[slotIndex] = 0x20;
    S_AVEKM_1[slotIndex] = 0x20;
    uint8_t block = 0x20;
    uint8_t timbreType = S_timbre_ptr[slotIndex]->tv.T_type;

    if(timbreType != TV_INST) {
        block |= 0x08;
    }

    S_type[slotIndex] = timbreType;
    S_BLOCK[slotIndex] = block;

    uint16_t initFreqOffset = S_timbre_ptr[slotIndex]->tv.T_init_f_offset + 2;
    if(initFreqOffset == 0x36) {
        S_AD_0[slotIndex] = 0xff;
        S_SR_0[slotIndex] = 0x0f;
        S_AD_1[slotIndex] = 0xff;
        S_SR_1[slotIndex] = 0x0f;
    }
    else if(S_status[slotIndex] != KEYOFF) {
        S_AD_0[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_play_ad_0;
        S_SR_0[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_play_sr_0;
        S_AD_1[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_play_ad_1;
        S_SR_1[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_play_sr_1;
    }
    else {
        S_AD_0[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_rel_ad_0;
        S_SR_0[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_rel_sr_0;
        S_AD_1[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_rel_ad_1;
        S_SR_1[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_rel_sr_1;
    }

    if(S_status[slotIndex] == KEYOFF) {
        // DI: Index into the slot's timbre data, BX: Slot index for accessing word-sized arrays, SI: same, but for byte-sized
        // ; There is separate data for when the key has been released. This sets the offsets to that data.
        S_f_offset[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_f_rel_offset + 2;
        S_v0_offset[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_v0_rel_offset + 2; // Connected to S_KSLTL_0 Used as offset for TVFX_sub_365E
        S_v1_offset[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_v1_rel_offset + 2; // Connected to S_KSLTL_1 Used as offset for TVFX_sub_365E
        S_m0_offset[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_m0_rel_offset + 2; // Connected to S_AVEKM_0 Used as offset for TVFX_sub_365E
        S_m1_offset[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_m1_rel_offset + 2; // Connected to S_AVEKM_1 Used as offset for TVFX_sub_365E
        S_fb_offset[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_fb_rel_offset + 2; // Connected to S_FBC
        S_ws_offset[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_ws_rel_offset + 2;
        S_p_offset[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_p_rel_offset + 2;
    }
    else { // ; This is called when KEYON first becomes true, initializing the state of the sound effect
        // DI: Index into the slot's timbre data, BX: Slot index for accessing word-sized arrays, SI: same, but for byte-sized
        if(S_timbre_ptr[slotIndex]->tv.T_type != TV_INST) {
            S_duration[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_duration + 1;
        }
        else {
            S_duration[slotIndex] = 0xffff; // ; For TVFX, set it to keep playing until a NOTE_OFF 
        }                                   // ; MIDI command is triggered? FFFF is close to 10 minutes@120Hz
        S_f_val[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_init_f_val;
        S_f_offset[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_init_f_offset + 2;

        S_v0_val[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_init_v0_val;
        S_v0_offset[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_init_v0_offset + 2;  // ; Connected to S_KSLTL_0 Used as offset for FX update data

        S_v1_val[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_init_v1_val;
        S_v1_offset[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_init_v1_offset + 2;  // ; Connected to S_KSLTL_1 Used as offset for FX update data

        S_m0_val[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_init_m0_val;
        S_m0_offset[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_init_m0_offset + 2;  // ; connected to S_AVEKM_0 Used as offset for FX update data

        S_m1_val[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_init_m1_val;
        S_m1_offset[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_init_m1_offset + 2;  // ; connected to S_AVEKM_1 Used as offset for FX update data

        S_fb_val[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_init_fb_val;
        S_fb_offset[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_init_fb_offset + 2;  // ; connected to S_FBC Used as offset for FX update data

        S_ws_val[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_init_ws_val;
        S_ws_offset[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_init_ws_offset + 2;  // ; connected to S_WS Used as offset for FX update data

        S_p_val[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_init_p_val;
        S_p_offset[slotIndex] = S_timbre_ptr[slotIndex]->tv.T_init_p_offset + 2;  // ; connected to S_P Used as offset for FX update data
    }

    // DI: Index into the slot's timbre data, BX: Slot index for accessing word-sized arrays, SI: same, but for byte-sized
    S_update[slotIndex] = U_ALL_REGS; // ; Forces all of the values to be updated next time serve_synth is called
    S_f_counter[slotIndex] = 1;
    S_v0_counter[slotIndex] = 1;
    S_v1_counter[slotIndex] = 1;
    S_p_counter[slotIndex] = 1;
    S_fb_counter[slotIndex] = 1;
    S_m0_counter[slotIndex] = 1;
    S_m1_counter[slotIndex] = 1;
    S_ws_counter[slotIndex] = 1;
    S_f_increment[slotIndex] = 0;
    S_v0_increment[slotIndex] = 0;
    S_v1_increment[slotIndex] = 0;
    S_p_increment[slotIndex] = 0;
    S_fb_increment[slotIndex] = 0;
    S_m0_increment[slotIndex] = 0;
    S_m1_increment[slotIndex] = 0;
    S_ws_increment[slotIndex] = 0;
}
