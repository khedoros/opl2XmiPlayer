/*
;████████████████████████████████████████████████████████████████████████████
;██                                                                        ██
;██   YAMAHA.INC                                                           ██
;██                                                                        ██
;██   IBM Audio Interface Library XMIDI interpreter for Ad Lib, etc.       ██
;██   including YMF262 (aka OPL3) 2-and-4-operator FM support              ██
;██                                                                        ██
;██   Version 1.00 of 15-Feb-92: Init version (replaces YM3812.INC v1.04)  ██
;██           1.01 of 20-Jun-92: Pro Audio Spectrum Plus/16 support added  ██
;██                              Remove REPTs to circumvent TASM bug       ██
;██                              Reset PAS to Ad Lib mode on shutdown      ██
;██           1.02 of 13-Nov-92: Detect OPL3 at 2x8, not 2x0               ██
;██                                                                        ██
;██   8086 ASM source compatible with Turbo Assembler v2.0 or later        ██
;██   Author: John Miles                                                   ██
;██                                                                        ██
;████████████████████████████████████████████████████████████████████████████
;██                                                                        ██
;██   Copyright (C) 1991, 1992 Miles Design, Inc.                          ██
;██                                                                        ██
;██   Miles Design, Inc.                                                   ██
;██   10926 Jollyville #308                                                ██
;██   Austin, TX 78759                                                     ██
;██   (512) 345-2642 / FAX (512) 338-9630 / BBS (512) 454-9990             ██
;██                                                                        ██
;████████████████████████████████████████████████████████████████████████████
*/

#include<stdint.h>
#include "ail.h"

//                ;
//                ;Driver-specific configuration equates
//                ;
#define OSI_ALE                         // TRUE to assemble OSI TVFX code

#define MAX_REC_CHAN            10      // Max channel recognized by synths
#define MAX_TRUE_CHAN            9      // Max channel available for locking
#define MIN_TRUE_CHAN            2      // Min channel # (1-based)

#define DEF_SYNTH_VOL          100      // Init vol=100%
#define MAX_TIMBS              192      // Max # of timbres in local cache
#define DEF_TC_SIZE           3584      // Room for 256 14-byte .BNK timbres

#ifdef YM3812
    #define NUM_VOICES           9      // # of physical voices available
    #define NUM_SLOTS           16      // # of virtual voices available
#elif defined(YMF262) 
    #define NUM_VOICES          18      // # of physical voices available
    #define NUM_4OP_VOICES       6
    #define NUM_SLOTS           20      // # of virtual voices available
#endif

#define VEL_SENS                 1      // Velocity sensitivity disabled if 0

#define VEL_TRUE                 0      // Full velocity sensitivity range if 1
                                        // (set to 0 to reduce playback noise)
#define DEF_PITCH_RANGE         12      // Default pitch wheel range (semitones)

#define DEF_AV_DEPTH    0b11000000      // Default AM/Vibrato depth
                                        // Bit 7: AM depth 4.8dB if 1, else 1dB
                                        // Bit 6: VIB depth 14c. if 1, else 7c.



#ifdef YMF262                           // Panpot thresholds for OPL3 voices

    #define R_PAN_THRESH        27      // Force right channel if pan <= n
    #define L_PAN_THRESH       100      // Force left channel if pan >= n

    #ifdef SBPRO2
        #define LEFT_MASK    0b11011111
        #define RIGHT_MASK   0b11101111
    #elif defined(PASOPL)
        #define LEFT_MASK    0b11011111
        #define RIGHT_MASK   0b11101111
    #else
        #define LEFT_MASK    0b11101111
        #define RIGHT_MASK   0b11011111
    #endif
#endif


#define CIRC_ASSIGN          true       // FALSE for "old" AIL voice assignment

//
// Internal equates
//

#ifdef SBPRO1
    #define SBPRO
#endif

#ifdef SBPRO2
    #define SBPRO
#endif

#ifdef PAS
    #define PROAUDIO
#endif

#ifdef PASOPL
    #define PROAUDIO
#endif            

//
// Driver Description Table (DDT)
// Returned by describe_driver() proc
//

const char *devnames[] = {
#ifdef ADLIBSTD
    "Ad Lib(R) Music Synthesizer Card",
#elif defined(SBSTD)
    "Creative Labs Sound Blaster(TM) FM Sound",
    "Media Vision Thunderboard(TM) FM Sound",
#elif defined(PAS)
    "Media Vistion Pro Audio Spectrum(TM) Plus/16 FM Sound",
#elif defined(SBPRO)
    "Creative Labs Sound Blaster Pro(TM) FM Sound",
#elif defined(ADLIBG)
    "Ad Lib(R) Gold Music Synthesizer Card",
#endif
    ""                                  // 0 to end list of device names
};

struct drvr_desc ds = {
    .min_API_version = 200,             // Minimum API version required = 2.00
    .drvr_type = 3,                     // Type 3: XMIDI emulation
    .data_suffix =
#ifdef YMF262
    "OPL",                              // Supports .OPL Global Timbre file
#else                                   // (backward-compatible w/.AD format)
    "AD",                               // Needs .AD Global Timbre file
#endif
    .dev_name_table = &devnames,        // Pointer to list of supported devices
// default_IO/IRQ/DMA/DRQ omitted because we'll just be talking to the device emulator
    .service_rate = QUANT_RATE,         // Request 120 calls/second
    .display_size = 0                   // No display
};

#ifdef YM3812
    #define IOWVAL 42
    #ifdef STEREO
        #define STEREO_3812 1
    #endif
#elif defined(YMF262)
    #define IOWVAL 8
#endif

#ifdef OSI_ALE
    #include "ale.h"
#endif


//
// Default setup values & internal constants
//

uint16_t
freq_table[] = {0x02b2,0x02b4,0x02b7,0x02b9,0x02bc,0x02be,0x02c1,0x02c3,0x02c6,0x02c9,
                0x02cb,0x02ce,0x02d0,0x02d3,0x02d6,0x02d8,0x02db,0x02dd,0x02e0,0x02e3, 
                0x02e5,0x02e8,0x02eb,0x02ed,0x02f0,0x02f3,0x02f6,0x02f8,0x02fb,0x02fe, 
                0x0301,0x0303,0x0306,0x0309,0x030c,0x030f,0x0311,0x0314,0x0317,0x031a, 
                0x031d,0x0320,0x0323,0x0326,0x0329,0x032b,0x032e,0x0331,0x0334,0x0337, 
                0x033a,0x033d,0x0340,0x0343,0x0346,0x0349,0x034c,0x034f,0x0352,0x0356, 
                0x0359,0x035c,0x035f,0x0362,0x0365,0x0368,0x036b,0x036f,0x0372,0x0375, 
                0x0378,0x037b,0x037f,0x0382,0x0385,0x0388,0x038c,0x038f,0x0392,0x0395, 
                0x0399,0x039c,0x039f,0x03a3,0x03a6,0x03a9,0x03ad,0x03b0,0x03b4,0x03b7, 
                0x03bb,0x03be,0x03c1,0x03c5,0x03c8,0x03cc,0x03cf,0x03d3,0x03d7,0x03da, 
                0x03de,0x03e1,0x03e5,0x03e8,0x03ec,0x03f0,0x03f3,0x03f7,0x03fb,0x03fe, 
                0xfe01,0xfe03,0xfe05,0xfe07,0xfe08,0xfe0a,0xfe0c,0xfe0e,0xfe10,0xfe12,
                0xfe14,0xfe16,0xfe18,0xfe1a,0xfe1c,0xfe1e,0xfe20,0xfe21,0xfe23,0xfe25, 
                0xfe27,0xfe29,0xfe2b,0xfe2d,0xfe2f,0xfe31,0xfe34,0xfe36,0xfe38,0xfe3a, 
                0xfe3c,0xfe3e,0xfe40,0xfe42,0xfe44,0xfe46,0xfe48,0xfe4a,0xfe4c,0xfe4f, 
                0xfe51,0xfe53,0xfe55,0xfe57,0xfe59,0xfe5c,0xfe5e,0xfe60,0xfe62,0xfe64, 
                0xfe67,0xfe69,0xfe6b,0xfe6d,0xfe6f,0xfe72,0xfe74,0xfe76,0xfe79,0xfe7b, 
                0xfe7d,0xfe7f,0xfe82,0xfe84,0xfe86,0xfe89,0xfe8b,0xfe8d,0xfe90,0xfe92, 
                0xfe95,0xfe97,0xfe99,0xfe9c,0xfe9e,0xfea1,0xfea3,0xfea5,0xfea8,0xfeaa, 
                0xfead,0xfeaf };

uint8_t
note_octave[] = {0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,
                 1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2, 
                 2,2,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4, 
                 4,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5, 
                 5,5,5,5,6,6,6,6,6,6,6,6,6,6,6,6,7, 
                 7,7,7,7,7,7,7,7,7,7,7 };

uint8_t
note_halftone[] = {0,1,2,3,4,5,6,7,8,9,10,11,0,1,2,3,4,
                   5,6,7,8,9,10,11,0,1,2,3,4,5,6,7,8,9,
                   10,11,0,1,2,3,4,5,6,7,8,9,10,11,0,1,2,
                   3,4,5,6,7,8,9,10,11,0,1,2,3,4,5,6,7,
                   8,9,10,11,0,1,2,3,4,5,6,7,8,9,10,11,0,
                   1,2,3,4,5,6,7,8,9,10,11 };

uint8_t
array0_init[] = { 0b00100000,0,0,0b01100000,0,0,0,0,0,0,0,0,0,0,0,   // 01-0f
                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                 // 10-1f
                    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,                 // 20-2f
                    1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,                 // 30-3f
                    63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63, // 40-4f
                    63,63,63,63,63,63,0,0,0,0,0,0,0,0,0,0,           // 50-5f
                    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
                    255,255,255,255,255,255,0,0,0,0,0,0,0,0,0,0,     // 70-7f
                    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15, // 80-8f
                    15,15,15,15,15,15,0,0,0,0,0,0,0,0,0,0,           // 90-9f
                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                 // a0-af
                    0,0,0,0,0,0,0,0,0,0,0,0,0,DEF_AV_DEPTH,0,0,      // b0-bf
                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                 // c0-cf
                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                 // d0-df
                    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                 // e0-ef
                    0,0,0,0,0,0 };                                   // f0-f5

uint8_t
array1_init[] = {  0,0,0,0,0b00000001,0,0,0,0,0,0,0,0,0,0,           // 01-0f
                   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                  // 10-1f
                   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,                  // 20-2f
                   1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,                  // 30-3f
                   63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,  // 40-4f
                   63,63,63,63,63,63,0,0,0,0,0,0,0,0,0,0,            // 50-5f
                   255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
                   255,255,255,255,255,255,0,0,0,0,0,0,0,0,0,0,      // 70-7f
                   15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,  // 80-8f
                   15,15,15,15,15,15,0,0,0,0,0,0,0,0,0,0,            // 90-9f
                   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                  // a0-af
                   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                  // b0-bf
                   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                  // c0-cf
                   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                  // d0-df
                   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,                  // e0-ef
                   0,0,0,0,0,0 };                                    // f0-f5
                             
uint8_t vel_graph[] = { 82,85,88,91,94,97,100,103,106,109,112,115,118,121,124,127 };

#ifdef STEREO_3812
uint8_t
pan_graph[] = {    0,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,                     
                   32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62,               
                   64,66,68,70,72,74,76,78,80,82,84,86,88,90,92,94,               
                   96,98,100,102,104,106,108,110,112,114,116,118,120,122,124,127,
                   127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,
                   127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,
                   127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,
                   127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127 };
#endif

uint8_t op_0[] = { 0,1,2,6,7,8,12,13,14,18,19,20,24,25,26,30,31,32 };
uint8_t op_1[] = { 3,4,5,9,10,11,15,16,17,21,22,23,27,28,29,33,34,35 };

uint8_t op_index[] = { 0,1,2,3,4,5,8,9,10,11,12,13,16,17,18,19,20,21,
                       0,1,2,3,4,5,8,9,10,11,12,13,16,17,18,19,20,21 };

uint8_t op_array[] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                       1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 };

uint8_t voice_num[] = { 0,1,2,3,4,5,6,7,8,0,1,2,3,4,5,6,7,8 };

uint8_t voice_array[] = { 0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1 };

uint8_t op4_base[] = { 1,1,1,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0 };
int8_t alt_voice[] = { 3,4,5,0,1,2,-1,-1,-1,12,13,14,9,10,11,-1,-1,-1 };

#ifdef YMF262
    int8_t alt_op_0[] = { 6,7,8,0,1,2,-1,-1,-1,24,25,26,18,19,20,-1,-1,-1 };
    int8_t alt_op_1[] = { 9,10,11,3,4,5,-1,-1,-1,27,28,29,21,22,23,-1,-1,-1 };
    uint8_t conn_sel[] = { 1,2,4,1,2,4,0,0,0,8,16,32,8,16,32,0,0,0 };

    uint8_t op4_voice[] = { 0,1,2,9,10,11 };

    uint8_t carrier_01[] = { 0b00, 0b01, 0b10, 0b01 };
    uint8_t carrier_23[] = { 0b10, 0b10, 0b10, 0b11b };
#endif

//
// Misc. data
//

#ifdef STEREO_3812
    uint16_t LFMADDR; 
    uint16_t LFMDATA;
    uint16_t RFMADDR;
    uint16_t RFMDATA;
#endif

#ifdef SBPRO
    uint16_t MIXADDR;
    uint16_t MIXDATA;
#endif

uint16_t DATA_OUT;                      // IO_addr+1
uint16_t ADDR_STAT;                     // IO_addr

#ifdef ADLIBG                           // (used during detection)
    uint16_t CTRL_ADDR;
    uint16_t CTRL_DATA;
#endif

uint16_t note_event_l;                  // used for LRU counting
uint16_t note_event_h;
uint16_t timb_hist_l[MAX_TIMBS];        // last note event count for LRU
uint16_t timb_hist_h[MAX_TIMBS]; 
uint16_t timb_offsets[MAX_TIMBS];       // offsets of resident timbres
uint8_t timb_bank[MAX_TIMBS];           // GTR bank
uint8_t timb_num[MAX_TIMBS];            // GTR #
uint8_t timb_attribs[MAX_TIMBS];        // bit 7=in use 6=protected

uint32_t cache_base;                    // timbre cache base addr
uint16_t cache_size;                    // total cache size in bytes
uint16_t cache_end;                     // offset of first free byte

uint16_t TV_accum;                      // DDA accum for refresh timing
uint16_t pri_accum;                     // DDA accum for priority watch
uint8_t vol_update;                     // 0 | U_KSLTL

uint16_t rover_2op;                     // circular voice # counters
uint16_t rover_4op;

uint8_t conn_shadow;                    // OPL3 Connection Select copy

// .BNK-style timbre definition
struct BNK_t {
    uint16_t B_length;
    uint8_t B_transpose;
    uint8_t B_mod_AVEKM;                // op_0 = FM modulator
    uint8_t B_mod_KSLTL;
    uint8_t B_mod_AD;
    uint8_t B_mod_SR;
    uint8_t B_mod_WS;
    uint8_t B_fb_c;
    uint8_t B_car_AVEKM;                // op_1 = FM carrier
    uint8_t B_car_KSLTL;
    uint8_t B_car_AD;
    uint8_t B_car_SR;
    uint8_t B_car_WS;
};

struct OPL3BNK_t {        // .BNK-style OPL3 timbre definition
    struct BNK_t base;
    uint8_t O_mod_AVEKM;                // op_2
    uint8_t O_mod_KSLTL;
    uint8_t O_mod_AD;
    uint8_t O_mod_SR;
    uint8_t O_mod_WS;
    uint8_t O_fb_c;
    uint8_t O_car_AVEKM;                // op_3
    uint8_t O_car_KSLTL;
    uint8_t O_car_AD;
    uint8_t O_car_SR;
    uint8_t O_car_WS;
};

union opl2Bank_t {
    struct TVFX tv;
    struct BNK_t bnk;
};

union opl2Bank_t* S_timbre_ptr[NUM_SLOTS];  // pointer to timbre in local cache
uint16_t S_duration[NUM_SLOTS];         // # of TV intervals left in keyon
uint8_t S_status[NUM_SLOTS];            // 2=key off, 1=key on, 0=slot free
uint8_t S_type[NUM_SLOTS];              // 0=BNK, 1=TV inst, 2=TV effect, 3=OPL3
uint8_t S_voice[NUM_SLOTS];             // YM3812 voice 0-8 or -1 assigned to slot
uint8_t S_channel[NUM_SLOTS];           // MIDI channel owning slot
uint8_t S_note[NUM_SLOTS];              // MIDI note # for slot's voice
uint8_t S_keynum[NUM_SLOTS];            // MIDI key # before RBS translation
uint8_t S_transpose[NUM_SLOTS];         // MIDI note transposition for slot
uint8_t S_velocity[NUM_SLOTS];          // keyon velocity for note
uint8_t S_sustain[NUM_SLOTS];           // note sustained if nonzero
uint8_t S_update[NUM_SLOTS];            // bit mask for YM3812 register updates

uint8_t S_KBF_shadow[NUM_SLOTS];        // shadowed KON-BLOCK-FNUM(H) registers
uint8_t S_BLOCK[NUM_SLOTS];             // KON/BLOCK values
uint8_t S_FBC[NUM_SLOTS];               // YM3812 multi-purpose registers
uint8_t S_KSLTL_0[NUM_SLOTS];
uint8_t S_KSLTL_1[NUM_SLOTS];
uint8_t S_AVEKM_0[NUM_SLOTS];
uint8_t S_AVEKM_1[NUM_SLOTS];
uint8_t S_AD_0[NUM_SLOTS];              // YM3812 envelope registers
uint8_t S_AD_1[NUM_SLOTS];
uint8_t S_SR_0[NUM_SLOTS];
uint8_t S_SR_1[NUM_SLOTS];

uint8_t S_scale_01[NUM_SLOTS];          // level scaling flags for ops 0-1

#ifndef OSI_ALE
    uint16_t S_ws_val[NUM_SLOTS];       // YM3812 registers
    uint16_t S_m1_val[NUM_SLOTS];       // (declared in ALE.INC if TVFX enabled)
    uint16_t S_m0_val[NUM_SLOTS];
    uint16_t S_fb_val[NUM_SLOTS];
    uint16_t S_p_val[NUM_SLOTS];
    uint16_t S_v1_val[NUM_SLOTS];
    uint16_t S_v0_val[NUM_SLOTS];
    uint16_t S_f_val[NUM_SLOTS];
#endif

#ifdef YMF262
    uint8_t S_KSLTL_2[NUM_SLOTS];       // YMF262 registers for operators 3-4
    uint8_t S_KSLTL_3[NUM_SLOTS];
    uint8_t S_AVEKM_2[NUM_SLOTS];
    uint8_t S_AVEKM_3[NUM_SLOTS];
    uint8_t S_AD_2[NUM_SLOTS];
    uint8_t S_AD_3[NUM_SLOTS];
    uint8_t S_SR_2[NUM_SLOTS];
    uint8_t S_SR_3[NUM_SLOTS];

    uint16_t S_ws_val_2[NUM_SLOTS];
    uint16_t S_m3_val[NUM_SLOTS];
    uint16_t S_m2_val[NUM_SLOTS];
    uint16_t S_v3_val[NUM_SLOTS];
    uint16_t S_v2_val[NUM_SLOTS];

    uint8_t S_scale_23[NUM_SLOTS];
#endif

enum slot_status {
    FREE   = 0,                         // S_status[] phase equates
    KEYON  = 1,
    KEYOFF = 2,
};

enum slot_type {
    BNK_INST = 0,                       // S_type[] equates
    TV_INST = 1,
    TV_EFFECT = 2,
    OPL3_INST = 3
};

uint8_t U_ALL_REGS = 0b11111001;        // Bit mask equates for S_update
uint8_t U_AVEKM    = 0b10000000;           
uint8_t U_KSLTL    = 0b01000000;
uint8_t U_ADSR     = 0b00100000;
uint8_t U_WS       = 0b00010000;
uint8_t U_FBC      = 0b00001000;
uint8_t U_FREQ     = 0b00000001;

uint8_t MIDI_vol[NUM_CHANS];            // volume 
uint8_t MIDI_pan[NUM_CHANS];            // panpot
uint8_t MIDI_pitch_l[NUM_CHANS];        // pitchwheel LSB
uint8_t MIDI_pitch_h[NUM_CHANS];        // pitchwheel MSB
uint8_t MIDI_express[NUM_CHANS];        // expression 
uint8_t MIDI_mod[NUM_CHANS];            // modulation 
uint8_t MIDI_sus[NUM_CHANS];            // HOLD1 pedal 
uint8_t MIDI_vprot[NUM_CHANS];          // voice protection
uint8_t MIDI_timbre[NUM_CHANS];         // timbre cache index for each channel
uint8_t MIDI_bank[NUM_CHANS];           // Patch Bank Select values
uint8_t MIDI_program[NUM_CHANS];        // program change # / channel

uint8_t RBS_timbres[128];               // RBS timbre offset cache

uint8_t MIDI_voices[NUM_CHANS];         // # of voices assigned to channel

uint8_t V_channel[NUM_VOICES];          // voice assigned to MIDI channel n or -1

uint8_t S_V_priority[NUM_SLOTS];        // adjusted voice priorities


// ****************************************************************************
// *                                                                          *
// * I/O routines                                                             *
// *                                                                          *
// ****************************************************************************

/* Doubt I'll actually need this
set_IO_parms    PROC IO_ADDR            ;Set I/O address parms for adapter
                USES ds,si,di           

                IFDEF PAS

                mov LFMADDR,388h          
                mov LFMDATA,389h          
                mov RFMADDR,38ah          
                mov RFMDATA,38bh          
                mov ax,LFMADDR 

                ELSEIFDEF PASOPL

                mov ax,388h

                ELSE

                mov ax,[IO_ADDR]

                ENDIF

                IFDEF SBSTD

                add ax,8             

                ELSEIFDEF SBPRO1

                mov LFMADDR,ax
                inc ax
                mov LFMDATA,ax
                inc ax
                mov RFMADDR,ax
                inc ax
                mov RFMDATA,ax
                inc ax
                mov MIXADDR,ax
                inc ax
                mov MIXDATA,ax
                add ax,3

                ELSEIFDEF SBPRO2

                push ax
                add ax,4
                mov MIXADDR,ax
                inc ax
                mov MIXDATA,ax
                pop ax

                ENDIF

                mov ADDR_STAT,ax
                inc ax
                mov DATA_OUT,ax

                ret
                ENDP
*/

// ;****************************************************************************

// ;
// ;YM3812/YMF262 register access routines must preserve DS, SI, DI!
// ;

void write_register(uint8_t operator, uint8_t base, uint8_t value) {

}

void send_byte(uint16_t voice, uint8_t base, uint8_t data) {

}

void do_update() {

}
/*
write_register  PROC Operator:BYTE,Base:BYTE,Value:BYTE
                mov bl,[Operator]
                mov bh,0
                mov ah,op_array[bx]
                mov bl,op_index[bx]
                add bl,[Base]
                mov bh,ah
                mov cl,[Value]
                jmp do_update
                ENDP
*/
/*
send_byte       PROC Voice:WORD,Base:BYTE,Data:BYTE
                mov bx,[Voice]
                mov ah,voice_array[bx]
                mov bl,voice_num[bx]
                add bl,[Base]
                mov bh,ah
                mov cl,[Data]
                jmp do_update
                ENDP

do_update:      pop bp                  ;discard stack frame

update_reg      PROC                    ;Write value CL to register BX
                                        ;(preserves BX)
                IFDEF STEREO_3812

                mov al,bl               ;AL=address, CL=data
                mov dx,LFMADDR          ;select left register address
                out dx,al               
                mov dx,RFMADDR          ;select right register address
                out dx,al
                mov dx,LFMADDR          ;delay 3.3 uS

                mov ah,6
__rept_1:       in al,dx
                dec ah
                jne __rept_1

                mov al,cl
                mov dx,LFMDATA          ;write left data
                out dx,al
                mov dx,RFMDATA          ;write right data (same as left)
                out dx,al
                mov dx,LFMADDR          ;delay 23 uS (3.3 for YMF262)

                mov ah,IOWVAL
__rept_2:       in al,dx
                dec ah
                jne __rept_2

                ELSE

                mov dx,ADDR_STAT

                IFDEF YMF262            ;index 2nd array if addr > 256
                add dl,bh               
                adc dh,0
                add dl,bh               
                adc dh,0
                ENDIF

                mov al,bl               ;AL=address, CL=data
                out dx,al               ;select register address

                mov ah,6
__rept_3:       in al,dx
                dec ah
                jne __rept_3

                mov al,cl
                inc dx
                out dx,al
                dec dx

                mov ah,IOWVAL
__rept_4:       in al,dx
                dec ah
                jne __rept_4

                ENDIF
                ret
                ENDP
*/

/*
                IFDEF STEREO_3812       ;Access 3812 stereo registers
stereo_register PROC Part:BYTE,Base:BYTE,RLValues:WORD
                mov bl,[Part]
                mov bh,0
                mov al,[Base]
                add al,op_index[bx]
                mov dx,LFMADDR          ;select left register address
                out dx,al               
                mov dx,RFMADDR          ;select right register address
                out dx,al
                mov dx,LFMADDR          ;delay 3.3 uS

                mov ah,6
__rept_1:       in al,dx
                dec ah
                jne __rept_1

                mov dx,LFMDATA          ;write left data
                mov al,BYTE PTR [RLValues+1]
                out dx,al
                mov dx,RFMDATA          ;write right data
                mov al,BYTE PTR [RLValues]
                out dx,al
                mov dx,LFMADDR          ;delay 23 uS

                mov ah,42
__rept_2:       in al,dx
                dec ah
                jne __rept_2

                ret
                ENDP
                ENDIF
*/
uint8_t read_status(void) {

}

/*
read_status     PROC                    ;Read YM3812 status register
                mov dx,ADDR_STAT        
                in al,dx
                mov ah,0
                ret
                ENDP
*/

// ;****************************************************************************

/*
                IFDEF ADLIBG            

IO_wait         PROC                    ;Wait for clear SB bit (Ad Lib Gold)
                mov cx,500
                mov dx,CTRL_ADDR
__wait:         in al,dx
                and al,01000000b
                loopnz __wait
                ret
                ENDP

get_FM_vol      PROC RegNum             ;Get FM VOLUME register value
                call IO_wait
                mov dx,CTRL_ADDR
                mov ax,[RegNum]
                out dx,al
                call IO_wait
                mov dx,CTRL_DATA
                in al,dx
                ret
                ENDP

set_FM_vol      PROC RegNum,Val         ;Set FM VOLUME register value
                call IO_wait
                mov dx,CTRL_ADDR
                mov ax,[RegNum]
                out dx,al
                call IO_wait
                mov dx,CTRL_DATA
                mov ax,[Val]
                out dx,al
                ret
                ENDP
                ENDIF

                */

/*
detect_device   PROC H,IO_ADDR,IRQ,DMA,DRQ  
                USES ds,si,di             ;Attempt to detect card

                pushf
                cli

                IFDEF ADLIBG              ;if Ad Lib Gold, look for control
                                          ;chip
                mov dx,IO_ADDR
                add dx,2
                mov CTRL_ADDR,dx
                inc dx
                mov CTRL_DATA,dx

                mov dx,CTRL_ADDR          ;attempt to enable control chip
                mov al,0ffh               
                out dx,al

                call get_FM_vol C,9       ;get left volume
                mov si,ax
                call get_FM_vol C,10      ;get right volume
                mov di,ax

                xor si,0101b              ;tweak a few bits
                xor di,1010b

                call set_FM_vol C,9,si    ;write the tweaked values back
                call set_FM_vol C,10,di

                call get_FM_vol C,9       ;see if changes took effect
                cmp ax,si
                mov ax,0                  ;(return failure)
                jne __return
                call get_FM_vol C,10
                cmp ax,di
                mov ax,0                  ;(return failure)
                jne __return

                xor si,0101b              ;control chip found: restore old
                xor di,1010b              ;values & re-enable FM sound

                call set_FM_vol C,9,si
                call set_FM_vol C,10,di

                call IO_wait
                mov dx,CTRL_ADDR          
                mov al,0feh               
                out dx,al

                mov ax,1                  ;return success
                jmp __return

                ELSE                      ;(not Ad Lib Gold card...)

                push DATA_OUT             ;preserve current I/O addresses
                push ADDR_STAT
                IFDEF STEREO_3812
                push LFMADDR
                push RFMADDR
                push LFMDATA
                push RFMDATA
                ENDIF
                IFDEF SBPRO
                push MIXDATA
                push MIXADDR
                ENDIF

                call set_IO_parms C,[IO_ADDR]

                IFDEF SBPRO2              ;do Ad Lib detection at 2x8, NOT 2x0
                add DATA_OUT,8            ;(avoids hangups on standard SB)
                add ADDR_STAT,8
                ENDIF                     

                call detect_Adlib         ;do Ad Lib compatibility test first
                cmp ax,0
                je __exit

                IFDEF SBPRO               ;then look for CT-1345A mixer chip
                mov dx,MIXADDR
                mov al,0ah                ;select Mic Vol control
                out dx,ax
                jmp $+2
                mov dx,MIXDATA
                in al,dx                  ;get original value
                jmp $+2
                mov ah,al                 ;save it
                xor al,110b               ;toggle its bits
                out dx,al                 ;write it back
                jmp $+2
                in al,dx                  ;read/verify changed value
                xor al,110b              
                cmp al,ah
                mov al,ah                 ;put the old value back
                out dx,al
                mov ax,0
                jne __exit 
                mov ax,1                  ;signal card found -- it's a SB PRO
                ENDIF

                IFDEF PROAUDIO            ;if Pro Audio Spectrum, look for 
                mov ax,0bc00h             ;MVSOUND.SYS device driver
                mov bx,03f3fh
                int 2fh                   ;DOS MPX interrupt
                xor bx,cx
                xor bx,dx
                cmp bx,'MV'               ;MediaVision flag
                mov ax,0
                jne __exit
                mov cx,0
                mov ax,0bc03h             ;get function table address
                int 2fh
                cmp ax,'MV'
                mov ax,0
                jne __exit
                cmp cx,10                 ;bail out if no FM Split routine
                jb __exit                 ;present (MVSOUND version < 1.02)
                mov es,dx
                mov di,bx
                mov bx,100
                mov cx,1
                call DWORD PTR es:[di+36] ;set FM Split = stereo mode
                mov ax,1                  ;signal PAS card found
                ENDIF

__exit:         IFDEF SBPRO
                pop MIXADDR
                pop MIXDATA
                ENDIF
                IFDEF STEREO_3812
                pop RFMDATA
                pop LFMDATA
                pop RFMADDR
                pop LFMADDR
                ENDIF
                pop ADDR_STAT
                pop DATA_OUT

                ENDIF                     ;IFDEF ADLIBG

__return:       POP_F                     ;return AX=0 if not found
                ret
                ENDP
*/

/*

                IFNDEF ADLIBG
detect_send     PROC Addr:BYTE,Data:BYTE  ;Write data byte to specified AL reg
                mov dx,ADDR_STAT        
                mov al,[Addr]
                out dx,al                 ;select register address
                mov cx,6
__3_3_us:       in al,dx                  ;delay 3.3 uS
                loop __3_3_us
                mov dx,DATA_OUT
                mov al,[Data]
                out dx,al
                mov dx,ADDR_STAT         
                mov cx,42
__23_us:        in al,dx                  ;delay 23 uS
                loop __23_us
                ret
                ENDP
*/

/*
detect_Adlib    PROC                      ;Detect standard YM3812 timer regs
                USES ds,si,di
                call detect_send C,4,60h  ;reset T1 and T2
                call detect_send C,4,80h  ;reset IRQ
                call read_status
                mov di,ax                 ;save timer status
                call detect_send C,2,0ffh ;set T1 to 0FFh
                call detect_send C,4,21h  ;unmask and start T1
                mov si,200                ;wait 100 uS for timer to count down
__wait_100_uS:  call read_status
                dec si
                jnz __wait_100_uS
                mov si,ax                 ;save timer status
                call detect_send C,4,60h  ;reset T1 and T2
                call detect_send C,4,80h  ;reset IRQ
                and si,0e0h               ;mask off undefined bits
                and di,0e0h
                mov ax,0                  ;assume board not detected
                cmp di,0
                jne __return              ;initial timer value not 0, exit
                cmp si,0c0h
                jne __return              ;timer didn't overflow, exit
                mov ax,1                  ;else Ad Lib-compatible board exists
__return:       ret
                ENDP
                ENDIF
*/

// ;****************************************************************************
/*
void reset_synth(void) { // ;Init regs & register map 
    #ifdef YMF262
    // write value 0b00000001 to register 0x105    // set OPL3 NEW bit
    // write value 0 to register 0x104             // init OPL3 to 18 2-op voices
    conn_shadow = 0;
    #endif

    for(int i = 1; i <= 0xf5; i++) {
        //write value array0_init[i-1] to register i
    }

    #ifdef YMF262
    for(int i = 0x101; i <= 0x1f5; i++) {
        //write value array1_init[i - 0x101] to register i
    }
    #endif
}
*/
// ;****************************************************************************
/*
void shutdown_synth(void) { // ;Establish Ad Lib compatibility mode
    #ifdef YMF262
    // write value 0 to register 0x105     // clear OPL3 NEW bit
    #endif

    #ifdef PAS
    mov ax,0bc07h             ;function 7: get state table entries
    int 2fh
    or bl,10000000b           ;clear FM split (bit 7)
    mov al,bl                 ;...and write to mixer port
    mov dx,0b88h
    out dx,al
    #endif
}
*/
// ;****************************************************************************
// ;*                                                                          *
// ;*  Timbre cache management / related API calls                             *
// ;*                                                                          *
// ;****************************************************************************
/*
index_timbre    PROC GNum               ;Get global timbre's local index 
                USES ds,si,di

                mov si,0
                mov ax,[GNum]   
__find_gnum:    test timb_attribs[si],10000000b
                jz __find_next          ;(timbre unused)
                cmp timb_bank[si],ah
                jne __find_next
                cmp timb_num[si],al
                je __found
__find_next:    inc si
                cmp si,MAX_TIMBS
                jb __find_gnum

                mov si,-1               ;return -1 if timbre not loaded

__found:        mov ax,si
                ret
                ENDP
*/
// ;****************************************************************************
/*
protect_timbre  PROC H,Bank:BYTE,Num:BYTE
                USES ds,si,di           ;Protect a timbre from replacement
                pushf
                cli

                mov al,[Num]
                mov ah,[Bank]

                cmp ax,-1
                je __prot_all

                call index_timbre C,ax
                cmp ax,-1
                je __exit               ;timbre not loaded, can't protect it

                mov bx,ax
                or timb_attribs[bx],01000000b
                jmp __exit

__prot_all:     mov bx,0
__prot_timb:    or timb_attribs[bx],01000000b
                inc bx
                cmp bx,MAX_TIMBS
                jb __prot_timb

__exit:         POP_F
                ret
                ENDP
*/
// ;****************************************************************************
/*
unprotect_timbre PROC H,Bank:BYTE,Num:BYTE            
                USES ds,si,di           ;Allow a timbre to be replaced
                pushf 
                cli

                mov al,[Num]
                mov ah,[Bank]

                cmp ax,-1
                je __unprot_all

                call index_timbre C,ax
                cmp ax,-1
                je __exit               ;timbre not loaded, can't unprotect it

                mov bx,ax
                and timb_attribs[bx],10111111b
                jmp __exit

__unprot_all:   mov bx,0
__unprot_timb:  and timb_attribs[bx],10111111b
                inc bx
                cmp bx,MAX_TIMBS
                jb __unprot_timb

__exit:         POP_F
                ret
                ENDP
*/
// ;****************************************************************************
/*
timbre_status   PROC H,Bank:BYTE,Num:BYTE            
                USES ds,si,di           ;Return 0 if timbre not resident
                pushf 
                cli

                mov al,[Num]
                mov ah,[Bank]

                call index_timbre C,ax
                cmp ax,-1
                je __exit               ;not resident, inc AX to return 0

                mov si,ax               ;else return offset+1 in local cache
                shl si,1
                mov ax,timb_offsets[si] ;(for diagnostic purposes)

__exit:         inc ax

                POP_F
                ret
                ENDP
*/
// ;****************************************************************************
uint16_t get_cache_size(void) {
    return DEF_TC_SIZE;
}

// ;****************************************************************************
/*
define_cache    PROC H,Addr:FAR PTR,Size
                USES ds,si,di
                pushf
                cli

                les di,[Addr]
                FAR_TO_HUGE es,di
                mov WORD PTR cache_base,di
                mov WORD PTR cache_base+2,es

                mov ax,[Size]
                mov cache_size,ax

                mov cache_end,0

                POP_F
                ret
                ENDP
*/
// ;****************************************************************************
/*
get_request     PROC H,Sequence
                USES ds,si,di
                pushf
                cli

                mov si,[Sequence]
                cmp si,-1
                je __no_request
                lds si,sequence_state[si]

                cmp WORD PTR [si].TIMB+2,0
                je __no_request         ;no requested timbres, exit

                lds si,[si].TIMB        ;make sure TIMB chunk is present
                cmp [si],'IT'
                jne __no_request        ;if not, no requests are possible
                cmp [si+2],'BM'
                jne __no_request

                add si,8
                mov di,[si]             ;get TIMB.cnt
__chk_index:    add si,2
                mov ax,[si]
                call index_timbre C,[si]
                cmp ax,-1               ;timbre in local cache?
                je __request            ;no, request it
__next_index:   dec di
                jne __chk_index
                jmp __no_request        ;all requested timbres loaded, exit

__request:      mov ax,[si]             ;else return request: AL=num, AH=bank
                jmp __exit

__no_request:   mov ax,-1
                
__exit:         POP_F
                ret
                ENDP
*/
// ;****************************************************************************
/*
delete_LRU      PROC                    ;Excise LRU timbre from cache, adjust
                LOCAL index,tsize,toff  ;links & references
                USES ds,si,di           
                cld

                mov index,-1
                mov ax,-1           
                mov bx,0                
                mov dx,-1
                mov si,0                
__find_LRU:     test timb_attribs[si],10000000b
                jz __next_LRU           ;(timbre not installed)
                test timb_attribs[si],01000000b
                jnz __next_LRU          ;(timbre protected)
                cmp timb_hist_h[bx],dx
                ja __next_LRU           ;(timbre not least-recently-used)
                jb __log_LRU
                cmp timb_hist_l[bx],ax
                ja __next_LRU
__log_LRU:      mov ax,timb_hist_l[bx]
                mov dx,timb_hist_h[bx]
                mov index,si
__next_LRU:     add bx,2
                inc si
                cmp si,MAX_TIMBS
                jb __find_LRU

                cmp index,-1        
                je __exit

                mov si,index            ;SI = LRU timbre index
                shl si,1
                mov ax,timb_offsets[si]
                mov toff,ax
                les di,cache_base
                add di,ax               ;ES:DI -> timbre to delete
                mov si,es:[di]
                mov tsize,si
                add si,di               ;ES:SI -> next timbre in cache

                mov cx,WORD PTR cache_base
                add cx,cache_end
                sub cx,si               ;CX = size of area to move down 

                push es
                pop ds
                REP_MOVSB

                mov di,index            ;clear attributes of deleted timbre
                mov timb_attribs[di],00000000b
                mov ax,tsize            ;adjust end-of-cache pointer
                sub cache_end,ax                

                mov di,0                ;invalidate channel references to
                mov bh,0                ;the deleted timbre
__for_chan:     mov bl,MIDI_timbre[di]  
                cmp bl,-1
                je __next_chan
                cmp bx,index
                jne __next_chan
                mov MIDI_timbre[di],-1
__next_chan:    inc di
                cmp di,NUM_CHANS
                jne __for_chan
                mov di,0
__for_RBS:      mov bl,RBS_timbres[di]
                cmp bx,index
                jne __next_RBS
                mov RBS_timbres[di],-1
__next_RBS:     inc di
                cmp di,SIZE RBS_timbres
                jne __for_RBS

                mov di,0                ;adjust offset pointers for all
                mov bx,0                ;timbres after the deleted timbre
__for_index:    test timb_attribs[bx],10000000b
                jz __next_index
                mov ax,timb_offsets[di] 
                cmp ax,toff
                jbe __next_index
                sub ax,tsize
                mov timb_offsets[di],ax
__next_index:   add di,2
                inc bx
                cmp bx,MAX_TIMBS
                jb __for_index

                mov ax,WORD PTR cache_base
                add toff,ax
                mov di,0                ;adjust pointers to any active timbres
                mov si,0                
__for_slot:     cmp S_status[si],FREE
                je __next_slot
                mov ax,S_timbre_off[di]     // uint8_t* S_timbre_ptr
                cmp ax,toff
                jb __next_slot
                jne __move_down
                call release_voice C,si
                mov S_status[si],FREE
                jmp __next_slot
__move_down:    sub ax,tsize
                mov S_timbre_off[di],ax   // uint8_t* S_timbre_ptr
__next_slot:    add di,2
                inc si
                cmp si,NUM_SLOTS
                jne __for_slot

__exit:         ret
                ENDP
*/
// ;****************************************************************************
/*
install_timbre  PROC H,Bank:BYTE,Num:BYTE,Addr:FAR PTR
                LOCAL index
                USES ds,si,di
                pushf
                cli
                cld

                mov al,[Num]
                mov ah,[Bank]
                call index_timbre C,ax
                mov index,ax
                cmp ax,-1
                jne __set_patch         ;if timbre already resident, index it

                mov ax,WORD PTR [Addr]
                or ax,WORD PTR [Addr+2]
                jz __exit

__get_space:    mov di,0                ;find first available timbre slot
__find_index:   test timb_attribs[di],10000000b
                jz __get_size           ;found it....
                inc di
                cmp di,MAX_TIMBS
                jne __find_index
                jmp __kill_LRU          ;else kill a timbre to free up a slot

__get_size:     lds si,[Addr]           
                mov bx,[si]             ;get new timbre size
                add bx,cache_end
                cmp bx,cache_size       ;enough free space in local cache?
                jbe __install           ;yes, install it

__kill_LRU:     call delete_LRU         ;get rid of old timbres until the
                jmp __get_space         ;new timbre can be installed

__install:      xchg cache_end,bx

                mov index,di            ;save index of new timbre

                mov al,[Num]            ;AL=num, AH=bank
                mov ah,[Bank]
                mov timb_num[di],al     ;record global # in slot
                mov timb_bank[di],ah    ;mark timbre "in use/unprotected"
                mov timb_attribs[di],10000000b                  
                shl di,1                ;update timbre's timestamp
                mov ax,note_event_l     
                mov dx,note_event_h     
                add note_event_l,1      
                adc note_event_h,0      
                mov timb_hist_l[di],ax  
                mov timb_hist_h[di],dx  
                mov timb_offsets[di],bx

                les di,cache_base       ;DS:SI -> new timbre source addr
                add di,bx               ;ES:DI -> cache address for new timbre
                mov cx,[si]             ;CX = size of new timbre
                REP_MOVSB

__set_patch:    mov al,[Num]                                     
                mov ah,[Bank]
                mov bx,index
                mov di,0                ;for all channels...
__set_requests: cmp MIDI_program[di],al ; if timbre was requested in channel,
                jne __next_req          ; update channel's timbre index
                cmp MIDI_bank[di],ah
                jne __next_req
                mov MIDI_timbre[di],bl
__next_req:     inc di
                cmp di,NUM_CHANS
                jne __set_requests

__exit:         POP_F
                ret
                ENDP
*/
// ;****************************************************************************
// ;*                                                                          *
// ;*  MIDI interpreter and related procedures                                 *
// ;*                                                                          *
// ;****************************************************************************

void init_synth(void) {
    note_event_l = 0;
    note_event_h = 0;
    for(int i=0; i < MAX_TIMBS; i++) {
        timb_attribs[i] = 0b00000000;
    }
    for(int i=0; i < NUM_CHANS; i++) {
        MIDI_timbre[i] = -1;
        MIDI_voices[i] = 0;
        MIDI_program[i] = -1;
        MIDI_bank[i] = 0;
    }
    for(int i=0; i < NUM_SLOTS; i++) {
        S_status[i] = FREE;
    }
    for(int i=0; i< NUM_VOICES; i++) {
        V_channel[i] = -1;
    }
    for(int i = 0; i < sizeof(RBS_timbres); i++) {
        RBS_timbres[i] =  -1;
    }

    TV_accum = 0;
    pri_accum = 0;
    vol_update = 0;
    rover_2op = -1;
    rover_4op = -1;
}

// ;****************************************************************************
/*
assign_voice    PROC Slot               ;Allocate hardware voice to slot
                USES ds,si,di
                
                IFDEF YMF262
                mov si,[Slot]           
                cmp S_type[si],OPL3_INST
                je __OPL3
                ENDIF

                mov dx,-1               ;try to find an unassigned voice
                mov bx,rover_2op
__search_free:  inc dx
                cmp dx,NUM_VOICES
                je __seize_voice        ;(# of active slots > # of voices)
                inc bx
                IF CIRC_ASSIGN
                cmp bx,NUM_VOICES
                jne __chk_free
                mov bx,0
__chk_free:     mov rover_2op,bx
                ENDIF
                cmp V_channel[bx],-1   
                jne __search_free

                mov S_voice[si],bl      ;found free voice, assign it to slot
                mov di,bx
                mov bl,S_channel[si]
                inc MIDI_voices[bx]
                mov V_channel[di],bl

__update:       mov S_update[si],U_ALL_REGS
                call update_voice C,si  ;update the hardware
                ret

__seize_voice:  call update_priority    ;assign voice based on priority search
                ret                     

                ENDP
*/
/*
// ;****************************************************************************
release_voice   PROC Slot               ;Release slot's voice
                USES ds,si,di

                mov si,[Slot]

                cmp S_voice[si],-1   
                je __exit            

                and S_BLOCK[si],11011111b
                or S_update[si],U_FREQ  ;force KON = 0...

                call update_voice C,si  ;...silence any note...

                mov bh,0                ;...and deallocate the voice
                mov bl,S_channel[si]     
                dec MIDI_voices[bx]
                mov bl,S_voice[si]

                cmp S_type[si],OPL3_INST
                jne __free_chan
                mov V_channel[bx+3],-1  ;free both "halves" of 4-op OPL3
__free_chan:    mov V_channel[bx],-1    ;voice

                mov S_voice[si],-1

                cmp S_type[si],OPL3_INST
                je __release            ;release any .BNK slots deallocated...
                cmp S_type[si],BNK_INST
                jne __exit              
__release:      mov S_status[si],FREE   ;otherwise, slot remains active

__exit:         ret
                ENDP
      */              
// ;****************************************************************************

// Write current MIDI state out to hardware
// TODO: Complete this
void update_voice(uint16_t slot) {
    uint8_t vol,update_save,scale_flags,AVEKM_0,AVEKM_1,KSLTL_0,KSLTL_1,AD_0,AD_1,SR_0,SR_1,FBC;
    uint16_t voice,voice0,voice1,f_num,array,m0_val,m1_val,v0_val,v1_val,ws_val,fb_val;

    if(S_voice[slot] == -1) {
        return;
    }
    if(S_update[slot] & U_KSLTL) {
        int midiChannel = S_channel[slot] & 0x0f;
        uint16_t volTemp = MIDI_vol[midiChannel];
        volTemp *= MIDI_express[midiChannel];
        volTemp *= 2;
        volTemp >>= 8;
        if(volTemp > 0) {
            volTemp++;
        }
        volTemp *= S_velocity[slot];
        volTemp *= 2;
        volTemp >>= 8;
        if(volTemp > 0) {
            volTemp++;
        }
        vol = volTemp; 
    }
    voice = S_voice[slot];
    voice0 = op_0[voice];
    voice1 = op_1[voice];

    m0_val = S_m0_val[slot];
    m1_val = S_m1_val[slot];
    AVEKM_0 = S_AVEKM_0[slot];
    AVEKM_1 = S_AVEKM_1[slot];
    v0_val = S_v0_val[slot];
    v1_val = S_v1_val[slot];
    KSLTL_0 = S_KSLTL_0[slot];
    KSLTL_1 = S_KSLTL_1[slot];
    AD_0 = S_AD_0[slot];
    AD_1 = S_AD_1[slot];
    SR_0 = S_SR_0[slot];
    SR_1 = S_SR_1[slot];
    fb_val = S_fb_val[slot];
    ws_val = S_ws_val[slot];
    FBC = S_FBC[slot];
    scale_flags = S_scale_01[slot];
    if(S_update[slot] & U_AVEKM) {
        uint8_t channel = S_channel[slot] & 0x0f;
        uint8_t vib = 0b01000000;
        if(MIDI_mod[channel] < 64) {
            vib = 0;
        }
        m0_val >>= 12; // take the top 4 bits
        write_register(voice0, 0x20, m0_val | vib | AVEKM_0);

        m1_val >>= 12; // take the top 4 bits
        write_register(voice1, 0x20, m1_val | vib | AVEKM_1);

        S_update[slot] &= ~U_AVEKM;
    }
    if(S_update[slot] & U_KSLTL) {
        v0_val >>= 10; // take the top 6 bits
        if(scale_flags & 1) {
            v0_val *= vol;
            v0_val /= 127;
        }
        v0_val = (~v0_val) & 0x3f;
        v0_val |= KSLTL_0;
        write_register(voice0, 0x40, v0_val);

        v1_val >>= 10; // take the top 6 bits
        if(scale_flags & 0b10) {
            v1_val *= vol;
            v1_val /= 127;
        }
        v1_val = (~v1_val) & 0x3f;
        v1_val |= KSLTL_1;
        write_register(voice1, 0x40, v1_val);

        S_update[slot] &= (~U_KSLTL);
    }
    if(S_update[slot] & U_ADSR) {
        write_register(voice0, 0x60, AD_0);
        write_register(voice1, 0x60, AD_1);
        write_register(voice0, 0x80, SR_0);
        write_register(voice1, 0x80, SR_1);
        S_update[slot] &= (~U_ADSR);
    }
    if(S_update[slot] & U_WS) {                      //   __WS:   mov al,BYTE PTR ws_val
        write_register(voice1, 0xe0, ws_val & 0xff); //           call write_register C,voice1,0e0h,ax
        write_register(voice0, 0xe0, ws_val >> 8);   //           mov al,BYTE PTR ws_val+1
        S_update[slot] &= (~U_WS);                   //           call write_register C,voice0,0e0h,ax
    }
    if(S_update[slot] & U_FBC) {
        fb_val >>= 4;
        fb_val &= 0b1110;
        int fbc = FBC & 1;
        send_byte(voice, 0xc0, fb_val | fbc);
        S_update[slot] &= (~U_FBC);
    }
    if(S_update[slot] & U_FREQ) {
        if(S_type[slot] != TV_EFFECT) { // MIDI-controlled
            if(S_BLOCK[slot] & 0b00100000) { // KEY-ON
                uint8_t channel = S_channel[slot];
                int16_t pitch = (MIDI_pitch_h[channel] << 7) | MIDI_pitch_l[channel]; // 14 bits
                pitch -= 0x2000;
                pitch /= 32; // puts it in the +0x100 to -0x100 range
                pitch *= DEF_PITCH_RANGE; // normally 12 (+0xc00 to -0xc00)

                uint8_t note = S_note[slot] + S_transpose[slot];
                note -= 24;
                while(note < 0) {
                    note += 12;
                }
                note += 12;
                while(note > 95) {
                    note -= 12;
                }

                int noteAndPitch = pitch + (note << 8) + 8;
                noteAndPitch >>= 4; // gets the note number, expressed in 1/16 halftones

                noteAndPitch -= (12*16);
                while(noteAndPitch < 0) {
                    noteAndPitch += (12*16);
                }

                noteAndPitch += (12*16);
                while(noteAndPitch > (96 * 16) - 1) {
                    noteAndPitch -= (12 * 16);
                }

                // noteAndPitch should have the same contents as ax at this point
                // TODO: Finish working out the port of the note calculations

                int di = noteAndPitch;
                di >>= 4;
                int dx = di;
                int bx = note_halftone[di];
                di = bx;
                di <<= 5;
                noteAndPitch <<= 1;
                noteAndPitch &= 0b11111;
                di += noteAndPitch;
                noteAndPitch = freq_table[di];

                di = dx;
                bx = note_octave[di] - 1;
                
/*
                mov di,ax
                mov cx,4
                shr di,cl               ;divide by 16 to get physical note #
                mov dx,di               ;DX = halftone/octave index   
                mov bl,note_halftone[di]
                mov bh,0
                mov di,bx               ;DI = halftone value within octave

                mov cx,5                ;derive table row
                shl di,cl
                shl ax,1
                and ax,11111b
                add di,ax               ;derive table index

                mov ax,freq_table[di]

                mov di,dx
                mov bl,note_octave[di]
                dec bl
                or ax,ax
                jge __bump_block
                inc bl
__bump_block:   or bl,bl
                jge __set_M_freq
                inc bl
                sar ax,1

__set_M_freq:   shl bl,1                ;merge F-NUM(H) with block #
                shl bl,1
                and ah,00000011b
                or ah,bl
                mov f_num,ax
                jmp __set_freq
*/
            }
            else { // KEY-OFF
                send_byte(voice, 0xb0, S_KBF_shadow[slot] & 0b11011111);
            }
        }
        else { // TVFX sound effect
            f_num = S_f_val[slot] >> 6;
        }

        send_byte(voice, 0xa0, f_num & 0xff);
        S_KBF_shadow[slot] = ((f_num >> 8) | S_BLOCK[slot]);
        send_byte(voice, 0xb0, S_KBF_shadow[slot]);
        S_update[slot] &= (~U_FREQ);
    }   
}


// ;****************************************************************************
/*
update_priority PROC                    ;Maintain synthesizer voice priority
                LOCAL slot_cnt,low_p,high_p,low_4_p
                LOCAL needful
                USES ds,si,di

                mov slot_cnt,0          ;zero active slot count

                mov si,-1              
__get_priority: inc si                  ;build adjusted priority table and
                cmp si,NUM_SLOTS        ;reallocate voices if necessary
                je __sort_p_list
                cmp S_status[si],FREE
                je __get_priority

                inc slot_cnt            ;slot active, bump count

                mov bx,si
                mov di,WORD PTR S_channel[si]
                and di,0fh              ;DI = slot's MIDI channel
                mov ax,0ffffh           
                cmp MIDI_vprot[di],64   ;priority = max if voice protection on
                jge __adj_priority       
                mov ax,S_p_val[bx][si]  
__adj_priority: mov cl,MIDI_voices[di]  ;AX = slot's base priority        
                mov ch,0
                sub ax,cx               ;priority -= # of voices in channel
                jnc __set_priority
                mov ax,0
__set_priority: mov S_V_priority[bx][si],ax
                jmp __get_priority

__sort_p_list:  mov ax,0                ;set AX = unvoiced highest priority                
                mov dx,-1               ;set DX = voiced lowest priority
                mov cx,-1               ;set CX = voiced 4-op lowest priority
                mov si,-1               
__for_slot:     inc si                  
                cmp si,NUM_SLOTS
                je __reassign
                cmp S_status[si],FREE
                je __for_slot
                mov bx,si
                mov di,S_V_priority[bx][si]
                mov bl,S_voice[si]
                cmp bl,-1      
                jne __chk_low_4
                cmp di,ax               
                jb __for_slot         
                mov ax,di              
                mov high_p,si           ;highest-priority unvoiced slot
                jmp __for_slot
__chk_low_4:    cmp op4_base[bx],0
                je __chk_low
                cmp di,cx
                ja __chk_low
                mov cx,di
                mov low_4_p,si          ;lowest-priority voiced 4-op slot
__chk_low:      cmp di,dx               
                ja __for_slot
                mov dx,di               
                mov low_p,si            ;lowest-priority voiced slot
                jmp __for_slot

__reassign:     cmp ax,dx               ;highest unvoiced < lowest voiced?
                jb __exit               ;yes, we're done

                cmp ax,0                ;if highest unvoiced priority = 0 (or 
                je __exit               ;none), bail out

                mov si,low_p            ;else steal voice from compatible slot

                IFDEF YMF262
                mov bx,high_p           ;is new voice a 4-op voice?
                cmp S_type[bx],OPL3_INST
                jne __rob_voice
                mov si,low_4_p          ;yes, must rob from 4-op slot
                cmp S_type[si],OPL3_INST
                je __rob_voice          ;4-op seizes 4-op, continue

                mov al,alt_voice[si]    ;4-op seizes 2 2-op voices...
                mov di,-1               
__kill_alt:     inc di                  ;find slot which owns 2nd half's
                cmp di,NUM_SLOTS        ;2-op voice (if any) and release it
                je __rob_voice
                cmp S_status[di],FREE
                je __kill_alt
                cmp S_voice[di],al
                jne __kill_alt

                call release_voice C,di
                ENDIF

__rob_voice:    mov bh,0
                mov bl,S_voice[si]    

                push bx
                call release_voice C,si
                pop bx

                mov si,high_p
                mov S_voice[si],bl
                mov di,bx
                mov bl,S_channel[si]
                inc MIDI_voices[bx]
                mov V_channel[di],bl
                cmp S_type[si],OPL3_INST
                jne __do_update
                mov V_channel[di+3],bl

__do_update:    mov S_update[si],U_ALL_REGS
                call update_voice C,si  ;update the hardware

                dec slot_cnt
                jnz __sort_p_list       ;keep sorting until priorities valid

__exit:         ret
                ENDP

*/
// ;****************************************************************************

/*
                IFDEF YMF262
OPL_phase       PROC Slot                   ;Set up 4-op slot parameters
                USES ds,si,di

                call BNK_phase C,[Slot]

                mov si,[Slot]
                mov bx,si
                shl bx,1
                mov di,S_timbre_off[bx]            // uint8_t* S_timbre_ptr
                mov ds,S_timbre_seg[bx]

                mov S_type[si],OPL3_INST

                mov al,[di].B_fb_c
                and ax,10000000b
                mov cl,6
                shr ax,cl
                or S_FBC[si],al             ;move 2nd connection bit to S_FBC

                push bx                     ;set scaling flags for additive
                mov bl,S_FBC[si]            ;operators, based on voice's
                mov bh,0                    ;algorithm (0-3)
                mov al,carrier_01[bx]       
                mov S_scale_01[si],al
                mov al,carrier_23[bx]
                mov S_scale_23[si],al
                pop bx

                mov al,0                    ;copy key scale/total level values
                mov ah,[di].O_mod_KSLTL
                mov dl,ah
                and dl,11000000b
                mov S_KSLTL_2[si],dl
                not ah
                and ah,00111111b
                shl ax,1
                shl ax,1
                mov S_v2_val[bx],ax

                mov al,0                    
                mov ah,[di].O_car_KSLTL
                mov dl,ah
                and dl,11000000b
                mov S_KSLTL_3[si],dl
                not ah
                and ah,00111111b
                shl ax,1
                shl ax,1
                mov S_v3_val[bx],ax

                mov al,[di].O_mod_AVEKM     ;copy AM/Vib/EG/KSR/Multi bits
                mov ah,al
                and al,11110000b            
                mov S_AVEKM_2[si],al
                mov al,0
                mov cl,4
                shl ax,cl
                mov S_m2_val[bx],ax

                mov al,[di].O_car_AVEKM     
                mov ah,al
                and al,11110000b            
                mov S_AVEKM_3[si],al
                mov al,0
                mov cl,4
                shl ax,cl
                mov S_m3_val[bx],ax

                mov al,[di].O_mod_AD        ;copy envelope parms
                mov S_AD_2[si],al           
                mov al,[di].O_mod_SR
                mov S_SR_2[si],al

                mov al,[di].O_car_AD
                mov S_AD_3[si],al
                mov al,[di].O_car_SR
                mov S_SR_3[si],al

                mov al,[di].O_car_WS        ;copy wave select values
                mov ah,[di].O_mod_WS
                mov S_ws_val_2[bx],ax

                ret
                ENDP
                ENDIF
                */

// ;****************************************************************************

/*
BNK_phase       PROC Slot                   ;Set up BNK slot parameters
                USES ds,si,di

                mov si,[Slot]
                mov bx,si              
                shl bx,1
                mov di,S_timbre_off[bx]             // uint8_t* S_timbre_ptr
                mov ds,S_timbre_seg[bx]
                
                mov S_BLOCK[si],00100000b   ;set KON, clear BLOCK mask

                mov S_type[si],BNK_INST
                mov S_duration[bx],-1

                mov S_p_val[bx],32767       ;BNK instrument priority = average

                mov al,0
                mov ah,[di].B_fb_c          ;copy feedback/connection values
                mov dl,ah
                and dl,00000001b
                mov S_FBC[si],dl
                mov cl,4
                shl ax,cl
                mov S_fb_val[bx],ax

                mov al,0                    ;copy key scale/total level values
                mov ah,[di].B_mod_KSLTL
                mov dl,ah
                and dl,11000000b
                mov S_KSLTL_0[si],dl
                not ah
                and ah,00111111b
                shl ax,1
                shl ax,1
                mov S_v0_val[bx],ax

                mov al,0                    
                mov ah,[di].B_car_KSLTL
                mov dl,ah
                and dl,11000000b
                mov S_KSLTL_1[si],dl
                not ah
                and ah,00111111b
                shl ax,1
                shl ax,1
                mov S_v1_val[bx],ax

                mov al,[di].B_mod_AVEKM     ;copy AM/Vib/EG/KSR/Multi bits
                mov ah,al
                and al,11110000b            
                mov S_AVEKM_0[si],al
                mov al,0
                mov cl,4
                shl ax,cl
                mov S_m0_val[bx],ax

                mov al,[di].B_car_AVEKM     
                mov ah,al
                and al,11110000b            
                mov S_AVEKM_1[si],al
                mov al,0
                mov cl,4
                shl ax,cl
                mov S_m1_val[bx],ax

                mov al,[di].B_mod_AD        ;copy envelope parms
                mov S_AD_0[si],al           
                mov al,[di].B_mod_SR
                mov S_SR_0[si],al

                mov al,[di].B_car_AD
                mov S_AD_1[si],al
                mov al,[di].B_car_SR
                mov S_SR_1[si],al

                mov al,[di].B_car_WS        ;copy wave select values
                mov ah,[di].B_mod_WS
                mov S_ws_val[bx],ax

                mov al,S_FBC[si]            ;get C-bit (1=additive, 0=FM)
                or al,10b                   ;(2-op carrier always scaled)
                mov S_scale_01[si],al

                mov S_update[si],U_ALL_REGS ;flag all registers "dirty"

                ret
                ENDP
                */

// ;****************************************************************************

/*
note_off        PROC Chan:BYTE,Note:BYTE
                USES ds,si,di           ;Turn MIDI note off

                mov si,-1               ;find all slots in which note is
__next_slot:    mov al,[Note]           ;playing
                mov bl,[Chan]
__find_note:    inc si
                cmp si,NUM_SLOTS
                je __exit
                cmp S_status[si],KEYON  
                jne __find_note
                cmp S_keynum[si],al
                jne __find_note
                cmp S_channel[si],bl
                jne __find_note

                mov bh,0
                cmp MIDI_sus[bx],64
                jge __sustained
                
                cmp S_type[si],OPL3_INST
                je __release_it
                cmp S_type[si],BNK_INST
                jne __TV_note_off
                
__release_it:   call release_voice C,si ;release the slot's voice
                mov S_status[si],FREE

                IFDEF TV_switch_voice   ;if TVFX installed...
                call TV_switch_voice    ;see if a TV slot needs voice
                ENDIF
                jmp __next_slot

__TV_note_off:  mov bx,si
                mov S_duration[bx][si],1    
                jmp __next_slot         ;set duration to last cycle

__sustained:    mov S_sustain[si],1
                jmp __next_slot

__exit:         ret

                ENDP

*/
// ;****************************************************************************
/*
note_on         PROC Chan,Note,Velocity ;Turn MIDI note on
                USES ds,si,di

                mov di,[Chan]     

                mov bh,0
                mov bl,MIDI_timbre[di]  ;get timbre used in channel

                cmp di,9                ;channel under RBS control?
                jne __set_timbre        ;no, BX=timbre cache index

                mov si,[Note]           ;else see if RBS entry is valid for
                mov bl,RBS_timbres[si]  ;this note
                cmp bl,-1
                jne __set_timbre        ;it's valid -- use it

                mov ax,[Note]           ;it's not -- validate RBS entry first
                mov ah,127              ;bank 127 reserved for RBS timbres
                call index_timbre C,ax
                mov bx,ax
                mov RBS_timbres[si],bl

__set_timbre:   cmp bl,-1
                je __exit               ;timbre not loaded, exit
                
                shl bx,1
                lds di,cache_base
                add di,timb_offsets[bx] ;else get address of timbre

                add note_event_l,1      ;update timbre cache LRU counters
                adc note_event_h,0      
                mov ax,note_event_l
                mov dx,note_event_h
                mov timb_hist_l[bx],ax
                mov timb_hist_h[bx],dx
                    
                mov si,0                
__find_slot:    cmp S_status[si],FREE
                je __slot_found
                inc si                  ;find a free virtual voice slot
                cmp si,NUM_SLOTS
                jne __find_slot
                jmp __exit              ;exit if no virtual voice available

__slot_found:   mov ax,[Chan]           ;establish MIDI channel
                mov S_channel[si],al    

                mov dx,[Note]
                mov S_keynum[si],dl     ;save MIDI key #

                mov al,0                ;establish MIDI note/transposition
                mov cl,[di+2]
                mov bx,[Chan]
                cmp bx,9                ;(for all channels except 10)
                je __set_n_txp
                mov al,cl
                mov cl,dl
__set_n_txp:    mov S_note[si],cl
                mov S_transpose[si],al

                IF VEL_SENS             
                mov ax,[Velocity]       ;establish note velocity
                IF NOT VEL_TRUE              
                shr al,1
                shr al,1
                shr al,1
                lea bx,vel_graph        ;scale back velocity sensitivity to 
                xlat cs:[bx]            ;reduce perceived playback noise
                ENDIF
                ELSE
                mov al,127              ;default velocity = 127
                ENDIF
                mov S_velocity[si],al

                mov bx,si               ;copy timbre address
                shl bx,1
                mov S_timbre_off[bx],di               // uint8_t* S_timbre_ptr
                mov S_timbre_seg[bx],ds

                mov S_status[si],KEYON  ;flag note "on" in slot

                mov S_sustain[si],0     ;init sustained flag

                mov ax,[di]             ;derive timbre type based on size
                cmp ax,SIZE OPL3BNK
                je __OPL3BNK
                cmp ax,SIZE BNK
                jne __TVFX_timbre

__BNK:          call BNK_phase C,si     ;set up BNK timbre in slot
                jmp __get_voice

__OPL3BNK:      IFDEF YMF262
                call OPL_phase C,si
                ENDIF
                jmp __get_voice

__TVFX_timbre:  IFDEF TV_phase          ;if TVFX enabled...
                call TV_phase C,si      ;set up TVFX timbre in slot
                ELSE
                jmp __exit
                ENDIF

__get_voice:    mov S_voice[si],-1
                call assign_voice C,si  ;assign hardware voice to slot

__exit:         ret
                ENDP
*/
// ;****************************************************************************
/*
release_sustain PROC Chan:BYTE
                USES ds,si,di

                mov si,0                
__release_sus:  cmp S_status[si],FREE   
                je __next_sus
                mov al,[Chan]
                cmp S_channel[si],al
                jne __next_sus
                cmp S_sustain[si],0
                je __next_sus
                call note_off C,di,WORD PTR S_note[si]
__next_sus:     inc si
                cmp si,NUM_SLOTS
                jne __release_sus

                ret
                ENDP
*/
// ;****************************************************************************
/*
send_MIDI_message PROC Stat:BYTE,D1:BYTE,D2:BYTE       
                USES ds,si,di           ;Send MIDI Channel Voice message

                mov si,WORD PTR [D1]
                and si,0ffh             ;SI=data 1 / controller #
                mov di,WORD PTR [Stat]
                mov ax,di               
                and di,00fh             ;DI=channel
                and ax,0f0h             ;AX=status
                mov ch,0                ;CX=data byte 2
                mov cl,[D2]             

                cmp ax,0b0h             
                je __ctrl_change
                cmp ax,0c0h
                je __program
                cmp ax,0e0h
                je __pitch
                cmp ax,080h
                je __note_off
                cmp ax,090h
                jne __exit

                cmp di,MIN_TRUE_CHAN-1
                jb __exit
                cmp di,MAX_REC_CHAN-1
                ja __exit

                jcxz __note_off         ;implicit Note Off if velocity==0

                call note_on C,di,si,cx
                ret

__note_off:     call note_off C,di,si
__exit:         ret

__pitch:        mov ax,si
                mov MIDI_pitch_l[di],al
                mov MIDI_pitch_h[di],cl
                mov al,U_FREQ
                jmp __flag_updates

__ctrl_change:  cmp si,PATCH_BANK_SEL
                je __t_bank
                cmp si,VOICE_PROTECT
                je __vprot
                cmp si,TIMBRE_PROTECT
                jne __MIDI

                mov bl,MIDI_timbre[di]
                cmp bl,-1
                je __exit
                mov bh,0
                mov al,timb_attribs[bx]
                and al,10111111b
                cmp cl,64
                jl __tprot
                or al,01000000b
__tprot:        mov timb_attribs[bx],al
                jmp __exit

__program:      mov ax,si
                mov MIDI_program[di],al
                mov ah,MIDI_bank[di]
                call index_timbre C,ax
                mov MIDI_timbre[di],al  ;record timbre # used by channel
                jmp __exit              ;(-1 if timbre not in local cache)

__t_bank:       mov MIDI_bank[di],cl
                jmp __exit

__vprot:        mov MIDI_vprot[di],cl
                jmp __exit

__MIDI:         mov al,U_AVEKM          ;Emulate MIDI controllers
                lea bx,MIDI_mod         
                cmp si,MODULATION
                je __MIDI_set

                mov al,U_KSLTL
                lea bx,MIDI_vol
                cmp si,PART_VOLUME
                je __MIDI_set
                lea bx,MIDI_express
                cmp si,EXPRESSION
                je __MIDI_set

                IFDEF YMF262            ;FBC registers control stereo on OPL3
                mov al,U_FBC
                ENDIF

                lea bx,MIDI_pan
                cmp si,PANPOT
                je __MIDI_set

                cmp si,SUSTAIN
                je __MIDI_sus
                cmp si,RESET_ALL_CTRLS
                je __MIDI_rac
                cmp si,ALL_NOTES_OFF
                je __MIDI_ano
                jmp __exit             

__MIDI_set:     mov cs:[bx][di],cl      ;save shadowed controller value
                
__flag_updates: mov bx,di
                mov si,0                ;mark appropriate voice parameters
__flag_slot:    cmp S_status[si],FREE   ;as "changed"
                je __flag_next
                cmp S_channel[si],bl
                jne __flag_next
                or S_update[si],al
                push ax
                push bx
                call update_voice C,si  ;update the hardware registers
                pop bx
                pop ax
__flag_next:    inc si
                cmp si,NUM_SLOTS
                jne __flag_slot
                jmp __exit

__MIDI_sus:     mov MIDI_sus[di],cl     ;log sustain value
                cmp cl,64               ;releasing sustain controller?
                jge __exit
                                          
                call release_sustain C,di 
                jmp __exit              ;yes, turn off any sustained notes 

__MIDI_ano:     mov si,0                ;turn off all notes playing in channel
__chk_note:     cmp S_status[si],KEYON
                jne __next_ano
                mov bx,di
                cmp S_channel[si],bl
                jne __next_ano
                call note_off C,di,WORD PTR S_note[si]
__next_ano:     inc si
                cmp si,NUM_SLOTS
                jne __chk_note
                jmp __exit

__MIDI_rac:     mov MIDI_sus[di],0
                call release_sustain C,di
                mov MIDI_mod[di],0      ;emulate Roland LAPC-1 RAC message
                mov MIDI_express[di],127
                mov MIDI_pitch_l[di],DEF_PITCH_L
                mov MIDI_pitch_h[di],DEF_PITCH_H
                mov al,U_AVEKM OR U_KSLTL OR U_FREQ
                jmp __flag_updates

                ENDP
*/
// ;****************************************************************************
// ;*                                                                          *
// ;*  Miscellaneous public (API-accessible) procedures                        *
// ;*                                                                          *
// ;****************************************************************************
/*
describe_driver PROC H,IntRateProc:FAR PTR    
                USES ds,si,di           ;Return far ptr to DDT
                pushf
                cli

                mov dx,cs
                mov device_name_s,dx
                lea ax,DDT

                POP_F
                ret
                ENDP
*/
// ;****************************************************************************
/*
send_cv_msg     PROC H,Stat,D1,D2       ;Send an explicit Channel Voice msg
                USES ds,si,di
                pushf
                cli

                call send_MIDI_message C,[Stat],[D1],[D2]

                POP_F
                ret
                ENDP
*/
// ;****************************************************************************
// OPL doesn't support sysex messages
void send_sysex_msg(void) {}
// ;****************************************************************************
// OPL doesn't have a display...but might be fun to implement if the game actually sends any of these?
void write_display(const char* string) {

}
