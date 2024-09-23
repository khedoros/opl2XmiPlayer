## Origin Systems Time-Variant Effects

### Intro/Background

Go back to 1992. A typical PC probably has some variety of Sound Blaster card. As a game developer with an interest in including good music in your game, you're looking at orchestrating on Roland synths, like the MT-32 (and expecting players to play with compatible devices like the LAPC-I card or CM-32L module). But you need to cover the more-common customers too. You find this great middleware, Miles Design, Inc.'s Audio Interface Library. Basically feed it MIDI files, and it can output music on what the customer has, whether that's a basic Yamaha FM synth (like on Sound Blasters and clones) or a fancy Roland thing.

It's a game; you want sound effects too, but that's not directly supported in the Miles AIL. On some of the Roland hardware, you can upload custom effects to play, which takes care of your well-equipped customers, but leaves most of your players stuck.

### More to the point

Enter: OSI ALE, which is also sometimes referred to as "TVFX". As far as I can tell, OSI refers to "Origin Systems, Inc.", and TVFX refers to "Time-Variant Effects". I'm not clear on what "ALE" stands for, but I suppose it doesn't really matter. In short, it's a way to use FM synthesis hardware to produce sound effects. Most of the time, you'd use the synthesizer to handle music; write an instrument "timbre" out to the hardware, send key-on and key-off commands, and cool, music.

The key differece between musical notes and sound effects is the "time-variant" aspect. With TVFX, you key on the note, modify the timbre settings while the note is playing to produce sound effects, then key-off at the end of the effect's intended duration. There are 8 modifiable attributes, and each has a list of bytecode "commands" that an interpreter processes to know when to apply each change. These attributes are the (1) frequency of the "note", (2+3) volumes and key scale level for the modulator and carrier, (4+5) frequency multiplier and tremolo+vibrato+sustain for the modulator and carrier, (6) priority of the effect, (7) feedback+connection, and (8) waveform of modulator and carrier.

### Specifics

Each of the 8 parameters has 4 variables tracking its state: (1) a current value, (2) a count of 60Hz ticks before the next iteration of its command list, (3) an incrememnt specifying how much to change the current value by every tick, and (4) an index/pointer to the next command to be executed in its command table (which is located in the timbre data).

#### The sound effect data structure

**Note**: All of the offsets in the struct seem to ignore the "size" field, so a field pointing to offset 0x34 in the raw data is referring to offset 0x36 in the table below.

|Offset|Size|Name|Description|
|:----:|:--:|:--:|:----------|
|0000|2|size|Size of the timbre in bytes|
|0002|1|transpose|semitones to transpose the note (not used for tvfx)|
|0003|1|type|0=OPL2 instrument, 1=TV instrument, 2=TV effect, 3=OPL3 instrument|
|0004|2|duration|Duration of the effect in 60Hz ticks|
|Frequency values|
|:----:|:--:|:--:|:----------|
|0006|2|init_f_val|Value of the frequency at key-on|
|0008|2|keyon_f_offset|Initial command table offset at key-on|
|000A|2|release_f_offset|Initial command table offset for key-off|
|Modulator volume values|
|:----:|:--:|:--:|:----------|
|000C|2|init_v0_val|(The rest follow the same pattern)|
|000E|2| keyon_v0_offset||
|0010|2| release_v0_offset||
|Carrier volume values|
|:----:|:--:|:--:|:----------|
|0012|2|init_v1_val||
|0014|2|keyon_v1_offset||
|0016|2|release_v1_offset||
|Priority values|
|:----:|:--:|:--:|:----------|
|0018|2|init_p_val||
|001A|2|keyon_p_offset||
|001C|2|release_p_offset||
|Feedback values|
|:----:|:--:|:--:|:----------|
|001E|2|init_fb_val||
|0020|2|keyon_fb_offset||
|0022|2|release_fb_offset||
|Modulator freq multiplier values|
|:----:|:--:|:--:|:----------|
|0024|2|init_m0_val||
|0026|2|keyon_m0_offset||
|0028|2|release_m0_offset||
|Carrier freq multiplier values|
|:----:|:--:|:--:|:----------|
|002A|2|init_m1_val||
|002C|2|keyon_m1_offset||
|002E|2|release_m1_offset||
|Waveform select values|
|:----:|:--:|:--:|:----------|
|0030|2|init_ws_val||
|0032|2|keyon_ws_offset||
|0034|2|release_ws_offset||

**Optional block, present in some sound effects, but not others**

**Note**: The raw `keyon_f_offset` will be 0x34 if these values aren't present in the struct, and 0x3c if they are.

|Key-On Envelope values|
|Offset|Size|Name|Description|
|:----:|:--:|:--:|:----------|
|0036|1|keyon_sr_1|Carrier keyon sustain and release|
|0037|1|keyon_ad_1|Carrier keyon attack and decay|
|0038|1|keyon_sr_0|Modulator keyon sustain and release|
|0039|1|keyon_ad_0|Modulator keyon attack and decay|

|Key-off envelope values|
|:----:|:--:|:--:|:----------|
|003A|1|release_sr_1|Carrier keyon sustain and release|
|003B|1|release_ad_1|Carrier keyon attack and decay|
|003C|1|release_sr_0|Modulator keyon sustain and release|
|003D|1|release_ad_0|Modulator keyon attack and decay|
|Frequency values|
|:----:|:--:|:--:|:----------|
|0036/003E|Variable|CommandList|Start of a variable-length list of U16 values|

#### Initialization (key-on)

#### Uninitialization (key-off)

#### Parameter value iteration

For each parameter:

1. Every 1/60th of a second, the increment is applied to the current value.

2. If the value was changed (i.e. the increment was non-zero), the new value is pushed out to the audio hardware. 

3. Regardless, the count is decremented. 

4. If the count becomes 0, the next item in the command list is processed.

#### Command-list processing

#### Applying the values to hardware

// Applying current state to the hardware
void tvfx_update_voice(oplStream& opl, int voice) {
    if(tvfx_update[voice] & U_MULT0) {
        uint16_t m0_val = tvfxElements[voice][mult0].value;
        uint8_t AVEKM_0 = S_avekm_0[voice];
        m0_val >>= 12; // take the top 4 bits
        opl.WriteReg(voice_base_mod[voice]+AVEKM, m0_val | AVEKM_0);
        tvfx_update[voice] &= (~U_MULT0);
    }
    if(tvfx_update[voice] & U_MULT1) {
        uint16_t m1_val = tvfxElements[voice][mult1].value;
        uint8_t AVEKM_1 = S_avekm_1[voice];
        m1_val >>= 12; // take the top 4 bits
        opl.WriteReg(voice_base_car[voice] + AVEKM, m1_val | AVEKM_1);
        tvfx_update[voice] &= (~U_MULT1);
    }
    if(tvfx_update[voice] & U_LEVEL0) {
        uint8_t KSLTL_0 = S_ksltl_0[voice];
        uint16_t v0_val = tvfxElements[voice][level0].value;
        v0_val >>= 10; // take the top 6 bits
        v0_val = (~v0_val) & 0x3f;
        v0_val |= KSLTL_0;
        opl.WriteReg(voice_base_mod[voice] + KSL_TL, v0_val);
        tvfx_update[voice] &= (~U_LEVEL0);
    }
    if(tvfx_update[voice] & U_LEVEL1) {
        uint8_t KSLTL_1 = S_ksltl_1[voice];
        uint16_t v1_val = tvfxElements[voice][level1].value;
        v1_val >>= 10; // take the top 6 bits
        v1_val = (~v1_val) & 0x3f;
        v1_val |= KSLTL_1;
        opl.WriteReg(voice_base_car[voice] + KSL_TL, v1_val);
        tvfx_update[voice] &= (~U_LEVEL1);
    }
    if(tvfx_update[voice] & U_WAVEFORM) {            //   __WS:   mov al,BYTE PTR ws_val
        uint16_t ws_val = tvfxElements[voice][waveform].value;
        opl.WriteReg(voice_base_mod[voice] + WS, ws_val >> 8);
        opl.WriteReg(voice_base_car[voice] + WS, (ws_val & 0xff));
        tvfx_update[voice] &= (~U_WAVEFORM);         //           call write_register C,voice0,0e0h,ax
    }
    if(tvfx_update[voice] & U_FEEDBACK) {
        uint16_t fb_val = tvfxElements[voice][feedback].value;
        uint8_t FBC = S_fbc[voice];
        fb_val >>= 12; // Take the top 4 bits
        fb_val &= 0b1110;
        int fbc = FBC & 1;
        opl.WriteReg(voice_base2[voice] + FB_C, fb_val | fbc);
        tvfx_update[voice] &= (~U_FEEDBACK);
    }
    if(tvfx_update[voice] & U_FREQ) {
        uint16_t f_num = (tvfxElements[voice][freq].value >> 6);
        opl.WriteReg(voice_base2[voice] + F_NUM_L, f_num & 0xff);

        S_kbf_shadow[voice] = ((f_num >> 8) | S_block[voice]);
        opl.WriteReg(voice_base2[voice] + ON_BLK_NUM, S_kbf_shadow[voice]);
        tvfx_update[voice] &= (~U_FREQ);
    }   
}

// Processing the commands
    uw_patch_file::patchdat* patchDat = voice_patch[voice];
    auto& offset = tvfxElements[voice][element].offset;
    auto& value = tvfxElements[voice][element].value;
    auto& increment = tvfxElements[voice][element].increment;
    auto& counter = tvfxElements[voice][element].counter;

    bool valChanged = false;
    //std::cout<<"iterateTvfxCommandList()\n";
    for(int iter = 0; iter < 10; iter++) {
        uint16_t command = patchDat->tv_patchdatastruct.update_data[offset+0];
        uint16_t data = patchDat->tv_patchdatastruct.update_data[offset+1];
        //std::printf("iteration: %d command: %04x data: %04x\n", iter, command, data);
        if(command == 0) {
            offset += (data / 2);
        }
        else {
            offset += 2;
            if(command == 0xffff) {
                value = data;
                valChanged = true;
                tvfx_update[voice] |= (1 << element);
            }
            else if(command == 0xfffe) {
                valChanged = true;
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
            }
            else {
                counter = command;
                increment = data;
                return valChanged;
            }
        }
    }