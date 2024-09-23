# Origin Systems Time-Variant Effects

## Intro/Background

Go back to 1992. A typical PC probably has some variety of Sound Blaster card. As a game developer with an interest in including good music in your game, you're looking at orchestrating on Roland synths, like the MT-32 (and expecting players to play with compatible devices like the LAPC-I card or CM-32L module). But you need to cover the more-common customers too. You find this great middleware, Miles Design, Inc.'s Audio Interface Library. Basically feed it MIDI files, and it can output music on what the customer has, whether that's a basic Yamaha FM synth (like on Sound Blasters and clones) or a fancy Roland thing.

It's a game; you want sound effects too, but that's not directly supported in the Miles AIL. On some of the Roland hardware, you can upload custom effects to play, which takes care of your well-equipped customers, but leaves most of your players stuck.

## More to the point

Enter: OSI ALE, which is also sometimes referred to as "TVFX". As far as I can tell, OSI refers to "Origin Systems, Inc.", and TVFX refers to "Time-Variant Effects". I'm not clear on what "ALE" stands for, but I suppose it doesn't really matter. In short, it's a way to use FM synthesis hardware to produce sound effects. Most of the time, you'd use the synthesizer to handle music; write an instrument "timbre" out to the hardware, send key-on and key-off commands, and cool, music.

The key differece between musical notes and sound effects is the "time-variant" aspect. With TVFX, you key on the note, modify the timbre settings while the note is playing to produce sound effects, then key-off at the end of the effect's intended duration. There are 8 modifiable attributes, and each has a list of bytecode "commands" that an interpreter processes to know when to apply each change. These attributes are the (1) frequency of the "note", (2+3) volumes and key scale level for the modulator and carrier, (4+5) frequency multiplier and tremolo+vibrato+sustain for the modulator and carrier, (6) priority of the effect, (7) feedback+connection, and (8) waveform of modulator and carrier.

## Specifics

Each of the 8 parameters has 4 variables tracking its state: (1) a current value, (2) a count of 60Hz ticks before the next iteration of its command list, (3) an incrememnt specifying how much to change the current value by every tick, and (4) an index/pointer to the next command to be executed in its command table (which is located in the timbre data).

### Audience

The initial background and explanation is about the least technical that this info goes. From here, I describe the arrangement of relevant data structures, and have prose and/or code from my implementation to describe how to process the sound effect data, and how it gets applied to an actual OPL2 or OPL3 FM synthesis chip. So, hex values, register numbers, etc. I kind of assume that people reading past this point are either deeply curious about implementation details of decades-old games and/or interested in writing their own code for playing back Origin Systems Time-Variant Sound Effects in their own implementation of the game engine, or something.

### The sound effect data structure

In Ultima Underworld 1+2, the sound effect timbre data for OPL cards is stored in `uw.ad` or `uw.opl` files in the `sound` directory of the game. These are Audio Interface Library Global Timbre Library files, documented in John Miles' release of the source for AIL 2.0. At least for now, I'm skipping the documentation of that format, because it's covered there. So I'm limiting myself to descriptions of the TVFX timbres extracted from the UU games' GTL files.

**Note**: All of the offsets in the struct seem to ignore the "size" field, so a field pointing to offset 0x34 in the raw data is referring to offset 0x36 in the table below.

|Offset|Size|Name|Description|
|:----:|:--:|:--:|:----------|
|0000|2|size|Size of the timbre in bytes|
|0002|1|transpose|semitones to transpose the note (not used for tvfx)|
|0003|1|type|0=OPL2 instrument, 1=TV instrument, 2=TV effect, 3=OPL3 instrument|
|0004|2|duration|Duration of the effect in 60Hz ticks|

**Frequency values**
|Offset|Size|Name|Description|
|:----:|:--:|:--:|:----------|
|0006|2|init_f_val|Value of the frequency at key-on|
|0008|2|keyon_f_offset|Initial command table offset at key-on|
|000A|2|release_f_offset|Initial command table offset for key-off|

**Modulator volume values**
|Offset|Size|Name|Description|
|:----:|:--:|:--:|:----------|
|000C|2|init_v0_val|(The rest follow the same pattern)|
|000E|2| keyon_v0_offset||
|0010|2| release_v0_offset||

**Carrier volume values**
|Offset|Size|Name|Description|
|:----:|:--:|:--:|:----------|
|0012|2|init_v1_val||
|0014|2|keyon_v1_offset||
|0016|2|release_v1_offset||

**Priority values**
|Offset|Size|Name|Description|
|:----:|:--:|:--:|:----------|
|0018|2|init_p_val||
|001A|2|keyon_p_offset||
|001C|2|release_p_offset||

**Feedback values**
|Offset|Size|Name|Description|
|:----:|:--:|:--:|:----------|
|001E|2|init_fb_val||
|0020|2|keyon_fb_offset||
|0022|2|release_fb_offset||

**Modulator freq multiplier values**
|Offset|Size|Name|Description|
|:----:|:--:|:--:|:----------|
|0024|2|init_m0_val||
|0026|2|keyon_m0_offset||
|0028|2|release_m0_offset||

**Carrier freq multiplier values**
|Offset|Size|Name|Description|
|:----:|:--:|:--:|:----------|
|002A|2|init_m1_val||
|002C|2|keyon_m1_offset||
|002E|2|release_m1_offset||

**Waveform select values**
|Offset|Size|Name|Description|
|:----:|:--:|:--:|:----------|
|0030|2|init_ws_val||
|0032|2|keyon_ws_offset||
|0034|2|release_ws_offset||

**Optional block, present in some sound effects, but not others**

**Note**: The raw `keyon_f_offset` will be 0x34 if these values aren't present in the struct, and 0x3c if they are.

**Key-On Envelope values|**
|Offset|Size|Name|Description|
|:----:|:--:|:--:|:----------|
|0036|1|keyon_sr_1|Carrier keyon sustain and release|
|0037|1|keyon_ad_1|Carrier keyon attack and decay|
|0038|1|keyon_sr_0|Modulator keyon sustain and release|
|0039|1|keyon_ad_0|Modulator keyon attack and decay|

**Key-off envelope values**
|Offset|Size|Name|Description|
|:----:|:--:|:--:|:----------|
|003A|1|release_sr_1|Carrier keyon sustain and release|
|003B|1|release_ad_1|Carrier keyon attack and decay|
|003C|1|release_sr_0|Modulator keyon sustain and release|
|003D|1|release_ad_0|Modulator keyon attack and decay|

**Command List**
|Offset|Size|Name|Description|
|:----:|:--:|:--:|:----------|
|0036/003E|Variable|CommandList|Start of a variable-length list of U16 values|

### Initialization (key-on)

#### Init the register shadows for the non-variant parts of the voice's modulator and carrier operators

**register 0x20-0x35**

    S_avekm_0[voice] = 0x20; // SUSTAIN=1, AM=0, FM=0, Mult=0 (Mult will typically come from TV part)
    S_avekm_1[voice] = 0x20; // SUSTAIN=1, AM=0, FM=0, Mult=0

**register 0xc0-0xc8**

    S_fbc[voice] = 0;        // FM synth, no feedback; feedback comes from time-variant part)

**register 0x40-0x55**

    S_ksltl_0[voice] = 0;    // volume=zero, ksl=0 (TL/volume will typically come from the time-variant part)
    S_ksltl_1[voice] = 0;    // volume=zero, ksl=0

**register 0xb0-0xb8**

    S_block[voice] = 0x20;

#### Init the ADSR values, either from the provided ones or from defaults

**registers 0x60-0x75 and 0x80-0x95**

**Default values, if the specific effect doesn't contain its own optional overrides**

|Operator|Element|value|
|:------:|:-----:|:---:|
|carrier|attack|0xf|
|carrier|decay|0xf|
|carrier|sustain|0x0|
|carrier|release|0xf|
|modulator|attack|0xf|
|modulator|decay|0xf|
|modulator|sustain|0x0|
|modulator|release|0xf|

**If default values were provided, then the keyon ones are applied during keyon**

#### Duration in 60Hz units (incremented by 1)

    tvfx_duration[voice] = pat.init.duration + 1;

#### Set the offsets and current values to their KeyOn initial values

    tvfxElements[voice][freq].offset = (pat.init.keyon_f_offset - pat.init.keyon_f_offset) / 2;
    tvfxElements[voice][level0].offset = (pat.init.keyon_v0_offset - pat.init.keyon_f_offset) / 2;
    tvfxElements[voice][level1].offset = (pat.init.keyon_v1_offset - pat.init.keyon_f_offset) / 2;
    tvfxElements[voice][priority].offset = (pat.init.keyon_p_offset - pat.init.keyon_f_offset) / 2;
    tvfxElements[voice][feedback].offset = (pat.init.keyon_fb_offset - pat.init.keyon_f_offset) / 2;
    tvfxElements[voice][mult0].offset = (pat.init.keyon_m0_offset - pat.init.keyon_f_offset) / 2;
    tvfxElements[voice][mult1].offset = (pat.init.keyon_m1_offset - pat.init.keyon_f_offset) / 2;
    tvfxElements[voice][waveform].offset = (pat.init.keyon_ws_offset - pat.init.keyon_f_offset) / 2;

    tvfxElements[voice][freq].value = pat.init.init_f_val;
    tvfxElements[voice][level0].value = pat.init.init_v0_val;
    tvfxElements[voice][level1].value = pat.init.init_v1_val;
    tvfxElements[voice][priority].value = pat.init.init_p_val;
    tvfxElements[voice][feedback].value = pat.init.init_fb_val;
    tvfxElements[voice][mult0].value = pat.init.init_m0_val;
    tvfxElements[voice][mult1].value = pat.init.init_m1_val;
    tvfxElements[voice][waveform].value = pat.init.init_ws_val;

#### Init the counters all to 1, and the increment values to 0

    for(int element = 0; element < TVFX_ELEMENT_COUNT; element++) {
        tvfxElements[voice][element].counter = 1;
        tvfxElements[voice][element].increment = 0;
    }

#### Tag all 8 values as needing to be pushed out to hardware

    tvfx_update[voice] = U_ALL;

### Uninitialization (key-off)

(It's similar to the initialization in many respects)

#### Init the register shadows for the non-variant parts of the voice's modulator and carrier operators

**register 0x20-0x35**

    S_avekm_0[voice] = 0x20; // SUSTAIN=1, AM=0, FM=0, Mult=0 (Mult will typically come from TV part)
    S_avekm_1[voice] = 0x20; // SUSTAIN=1, AM=0, FM=0, Mult=0

**register 0xc0-0xc8**

    S_fbc[voice] = 0;        // FM synth, no feedback; feedback comes from time-variant part)

**register 0x40-0x55**

    S_ksltl_0[voice] = 0;    // volume=zero, ksl=0 (TL/volume will typically come from the time-variant part)
    S_ksltl_1[voice] = 0;    // volume=zero, ksl=0

**register 0xb0-0xb8**

    S_block[voice] = 0x20;

#### Update the ADSR values, if optional ones were provided

**registers 0x60-0x75 and 0x80-0x95**

If defaults were used, they stay the same. If optional values were used, then you apply the keyoff/release values at this point.

#### Set the offsets (but not current values) to their KeyOff/Release initial values

    tvfxElements[voice][freq].offset = (pat.init.release_f_offset - pat.init.keyon_f_offset) / 2;
    tvfxElements[voice][level0].offset = (pat.init.release_v0_offset - pat.init.keyon_f_offset) / 2;
    tvfxElements[voice][level1].offset = (pat.init.release_v1_offset - pat.init.keyon_f_offset) / 2;
    tvfxElements[voice][priority].offset = (pat.init.release_p_offset - pat.init.keyon_f_offset) / 2;
    tvfxElements[voice][feedback].offset = (pat.init.release_fb_offset - pat.init.keyon_f_offset) / 2;
    tvfxElements[voice][mult0].offset = (pat.init.release_m0_offset - pat.init.keyon_f_offset) / 2;
    tvfxElements[voice][mult1].offset = (pat.init.release_m1_offset - pat.init.keyon_f_offset) / 2;
    tvfxElements[voice][waveform].offset = (pat.init.release_ws_offset - pat.init.keyon_f_offset) / 2;

#### Init the counters all to 1, and the increment values to 0

    for(int element = 0; element < TVFX_ELEMENT_COUNT; element++) {
        tvfxElements[voice][element].counter = 1;
        tvfxElements[voice][element].increment = 0;
    }

#### Tag all 8 values as needing to be pushed out to hardware

    tvfx_update[voice] = U_ALL;

### Parameter value iteration

For each parameter:

1. Every 1/60th of a second, apply the increment to the current value.

2. If the value was changed (i.e. the increment was non-zero), flag the parameter to have the hardware value updated

3. Regardless, the count is decremented. 

4. If the count becomes 0, process the next item in the command list (which might trigger the need for a hardware update even if the increment was zero, this iteration)


### Command-list processing

The command lists at the end of the timbre data are made of pairs of 16-bit words. I call the first the "command" and the second the "data", but that's not strictly accurate; depending on the value of the "command", it can be used to update a data value too.

When processing a single iteration of commands, the processor is hard-coded to do a max of 10 commands. In UW1 and UW2 though, I think the effects are all authored such that they never hit that limit anyhow.

We process the commands for one element at a time, using the Offset value as an index into the command data list.

For a max of 10 iterations:

1. Read the first 16-bit word that the offset points to as the "command"
2. Read the second word as the "data".
3. Has several options:

- Command == 0: Increment the offset by `data` bytes.

(For the other options, increment just one command ahead, so 4 bytes)

- Command == 0xffff (or -1): Current element's `value` is set to `data`, element should be tagged as needing to be pushed to hardware

- Command == 0xfffe (or -2): Does an element-specific update to the non-time-variant parts of the registers.

    if(element == freq) {
        S_block[voice] = (data >> 8);
        tvfx_update[voice] |= U_FREQ;
    }
    else if(element == level0) {
        S_ksltl_0[voice] = data & 0xff;
        tvfx_update[voice] |= U_LEVEL0;
    }
    else if(element == level1) {
        S_ksltl_1[voice] = data & 0xff;
        tvfx_update[voice] |= U_LEVEL1;
    }
    else if(element == feedback) {
        S_fbc[voice] = (data >> 8);
        tvfx_update[voice] |= U_FEEDBACK;
    }
    else if(element == mult0) {
        S_avekm_0[voice] = (data & 0xff);
        tvfx_update[voice] |= U_MULT0;
    }
    else if(element == mult1) {
        S_avekm_1[voice] = (data & 0xff);
        tvfx_update[voice] |= U_MULT1;
    }

- Command is in the range of 1 to 0xfffd:

    counter = command;
    increment = data;
    return from the function


### Applying the values to hardware

For each of these, an update should only be applied if the relevant values have changed.

**Modulator Mult and A-V-E-K-M (registers 0x20-0x35)**

    if(tvfx_update[voice] & U_MULT0) {
        uint16_t m0_val = tvfxElements[voice][mult0].value;
        uint8_t AVEKM_0 = S_avekm_0[voice];
        m0_val >>= 12; // take the top 4 bits
        opl.WriteReg(voice_base_mod[voice]+AVEKM, m0_val | AVEKM_0);
        tvfx_update[voice] &= (~U_MULT0);
    }

**Carrier Mult and AVEKM (registers 0x20-0x35)**

Same as the modulator version, just targeting the carrier registers.

**Modulator Total Level and Key Scale Level (registers 0x40-0x55)**

    if(tvfx_update[voice] & U_LEVEL0) {
        uint8_t KSLTL_0 = S_ksltl_0[voice];
        uint16_t v0_val = tvfxElements[voice][level0].value;
        v0_val >>= 10; // take the top 6 bits
        v0_val = (~v0_val) & 0x3f;
        v0_val |= KSLTL_0;
        opl.WriteReg(voice_base_mod[voice] + KSL_TL, v0_val);
        tvfx_update[voice] &= (~U_LEVEL0);
    }

**Carrier Total Level and Key Scale Level (registers 0x40-0x55)**

Same as the modulator version, just targeting the carrier registers.

**Modulator and Carrier waveform selection (registers 0xe0-0xf5)**

    if(tvfx_update[voice] & U_WAVEFORM) {            //   __WS:   mov al,BYTE PTR ws_val
        uint16_t ws_val = tvfxElements[voice][waveform].value;
        opl.WriteReg(voice_base_mod[voice] + WS, ws_val >> 8);
        opl.WriteReg(voice_base_car[voice] + WS, (ws_val & 0xff));
        tvfx_update[voice] &= (~U_WAVEFORM);         //           call write_register C,voice0,0e0h,ax
    }

**Feedback and Operator Connection (registers 0xc0-0xc8)**

    if(tvfx_update[voice] & U_FEEDBACK) {
        uint16_t fb_val = tvfxElements[voice][feedback].value;
        uint8_t FBC = S_fbc[voice];
        fb_val >>= 12; // Take the top 4 bits
        fb_val &= 0b1110;
        int fbc = FBC & 1;
        opl.WriteReg(voice_base2[voice] + FB_C, fb_val | fbc);
        tvfx_update[voice] &= (~U_FEEDBACK);
    }

**Frequency, Octave, and Key-On (registers 0xa0-0xa8 and 0xb0-0xb8)**

    if(tvfx_update[voice] & U_FREQ) {
        uint16_t f_num = (tvfxElements[voice][freq].value >> 6);
        opl.WriteReg(voice_base2[voice] + F_NUM_L, f_num & 0xff);

        S_kbf_shadow[voice] = ((f_num >> 8) | S_block[voice]);
        opl.WriteReg(voice_base2[voice] + ON_BLK_NUM, S_kbf_shadow[voice]);
        tvfx_update[voice] &= (~U_FREQ);
    }   
}