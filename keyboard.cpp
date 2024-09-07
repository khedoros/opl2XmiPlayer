#include<iostream>
#include<tuple>
#include<vector>
#include<array>
#include<cstdint>
#include<memory>
#include<unordered_map>
#include<SDL2/SDL.h>
#include "opl.h"
#include "oplStream.h"
#include "uwPatch.h"

std::vector<std::tuple<uint8_t,uint8_t,uint16_t>> freqs;

//Mathematically calculate the best OPL settings to match Midi frequencies
//Outputs the best matches into the freqs vector of 3-tuples.
void calc_freqs() {
    double base_freq = 440.0;
    uint8_t base_mid_num = 69;
    for(uint16_t mid_num = 0; mid_num < 128; ++mid_num) {
        double midi_freq = base_freq * pow(2.0, (mid_num - base_mid_num)/12.0);
        //cout<<"MIDI Number: "<<mid_num<<" Frequency: "<<midi_freq<<'\n';
        double diff = 9999999999.0;
        uint8_t blk = 0;
        uint16_t f_num = 0;
        double OPL_freq = 0.0;
        for(uint32_t block = 0; block < 8; ++block) {
            for(uint32_t f_number = 0; f_number < 1024; ++f_number) {
                double opl_freq = double(f_number * /*49716*/ NATIVE_OPL_SAMPLE_RATE ) / pow(2.0, 20 - double(block));
                if(abs(opl_freq - midi_freq) < diff) {
                    diff = abs(opl_freq - midi_freq);
                    f_num = f_number;
                    blk = block;
                    OPL_freq = opl_freq;
                }
            }
        }
        if(diff < 10) {
            //cout<<" OPL_Blk: "<<uint16_t(blk)<<" F-Num: "<<f_num<<" OPL Freq: "<<OPL_freq<<'\n';
            freqs.push_back(std::make_tuple(mid_num,blk,f_num));
        }
        else {
            //cout<<" OPL: Out of Range\n";
        }
    }
}

//Data and methods having to do with current use of OPL channels, voice assignments, etc
const int OPL_VOICE_COUNT = 9;
std::array<int8_t, OPL_VOICE_COUNT> voice_midi_num { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
std::array<int8_t, OPL_VOICE_COUNT> voice_velocity {127, 127, 127, 127, 127, 127, 127, 127, 127 };
std::array<int8_t, OPL_VOICE_COUNT> voice_bank_num    { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
std::array<int8_t, OPL_VOICE_COUNT> voice_patch_num   { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
std::array<uw_patch_file::patchdat*, OPL_VOICE_COUNT> voice_patch;
std::array<int8_t, OPL_VOICE_COUNT> voice_modulation {  0,  0,  0,  0,  0,  0,  0,  0,  0 };
std::array<int8_t, OPL_VOICE_COUNT> voice_volume     {127,127,127,127,127,127,127,127,127};
std::array<int8_t, OPL_VOICE_COUNT> voice_expression {127,127,127,127,127,127,127,127,127};

const std::array<uint8_t, OPL_VOICE_COUNT> voice_base_mod {  0,  1,  2,  8,  9,0xa,0x10,0x11,0x12};
const std::array<uint8_t, OPL_VOICE_COUNT> voice_base_car {  3,  4,  5,0xb,0xc,0xd,0x13,0x14,0x15};
const std::array<uint8_t, OPL_VOICE_COUNT> voice_base2    {  0,  1,  2,  3,  4,  5,   6,   7,   8};

//Which patch and bank is each MIDI channel currently set to play
//Initialize them to 0:0
const int MIDI_CHANNEL_COUNT = 16;


const std::array<int8_t, 16> velocity_translation { 0x52, 0x55, 0x58, 0x5b, 0x5e, 0x61, 0x64, 0x67, 0x6a, 0x6d, 0x70, 0x73, 0x76, 0x79, 0x7c, 0x7f };

enum OPL_addresses {
    TEST       = 0x01, //Set to 0
    TIMER1     = 0x02, //'      '
    TIMER2     = 0x03, //'      '
    TIMER_CTRL = 0x04, //'      '
    NOTE_SEL   = 0x05, 
    AVEKM = 0x20,
    KSL_TL     = 0x40,
    AD    = 0x60,
    SR    = 0x80,
    F_NUM_L    = 0xa0,
    ON_BLK_NUM = 0xb0,
    TREM_VIB   = 0xbd, //Set to 0xc0
    FB_C   = 0xc0,
    WS   = 0xe0
};

//Find the note entry for the given channel and note#
//-1 means "note not found"
int8_t find_playing_note(int8_t note) {
    for(int i=0;i<OPL_VOICE_COUNT;++i) {
        if(voice_midi_num[i] == note)
            return i;
    }
    return -1;
}

//Find the first voice that's currently empty
//-1 means 'all in use'
int8_t find_unused_voice() {
    for(int i=0;i<OPL_VOICE_COUNT;++i) {
        if(voice_midi_num[i] == -1)
            return i;
    }
    return -1;
}

// Write proper volume for the given voice, taking into account the patch's TL, note velocity, channel volume and expression.
void writeVolume(oplStream& opl, int voice_num) {
    auto patch = voice_patch[voice_num];
    int8_t velocity = voice_velocity[voice_num];

    uint16_t vol = (uint16_t(voice_volume[voice_num]) * voice_expression[voice_num])>>7;
    vol *= velocity; vol >>= 7;

    uint8_t connection = patch->ad_patchdatastruct.connection;
    if(connection) { // additive operator, so scale modulating operator too
        uint8_t mod_tl = patch->ad_patchdatastruct.mod_out_lvl;
        mod_tl = (~mod_tl) & 0x3f;
        uint8_t mod_ksl = patch->ad_patchdatastruct.mod_key_scale;
        uint16_t mod_vol = ~((vol * mod_tl) / 127);

        opl.WriteReg(voice_base_mod[voice_num]+KSL_TL,(mod_vol & 0x3f) +
                                                      ((mod_ksl & 0x3)<<(6)));
    }

    uint8_t car_tl = patch->ad_patchdatastruct.car_out_lvl;
    car_tl = (~car_tl) & 0x3f;
    uint8_t car_ksl = patch->ad_patchdatastruct.car_key_scale;
    uint16_t car_vol = ~((vol * car_tl) / 127);

    opl.WriteReg(voice_base_car[voice_num]+KSL_TL,((car_vol & 0x3f) +
                                                   ((car_ksl & 0x3)<<(6))));
}

//Copies the given patch data into the given voice slot
                //OPL voice #, bank #,       instrument patch #
bool copy_patch(oplStream& opl, int voice) {
    if(voice == -1 || voice >= OPL_VOICE_COUNT) {
        std::cerr<<"Invalid voice\n";
        return false;
    }

    std::vector<uint8_t>& pat = voice_patch[voice]->ad_patchdata;
    bool am = voice_patch[voice]->ad_patchdatastruct.connection;

    //Write the values to the modulator:
    uint8_t mod_avekm = pat[uw_patch_file::patchIndices::mod_avekm];
    mod_avekm &= 0b10111111;
    mod_avekm |= ((voice_modulation[voice] > 64) ? 0b01000000 : 0);
    opl.WriteReg(voice_base_mod[voice]+AVEKM, mod_avekm);
    if(!am) { // For FM patch connection, modulator volume comes straight from the patch data
        opl.WriteReg(voice_base_mod[voice]+KSL_TL,pat[uw_patch_file::patchIndices::mod_ksl_tl]);
    }
    opl.WriteReg(voice_base_mod[voice]+AD,pat[uw_patch_file::patchIndices::mod_ad]);
    opl.WriteReg(voice_base_mod[voice]+SR,pat[uw_patch_file::patchIndices::mod_sr]);
    opl.WriteReg(voice_base_mod[voice]+WS,pat[uw_patch_file::patchIndices::mod_ws]);

    //Write the values to the carrier:
    uint8_t car_avekm = pat[uw_patch_file::patchIndices::car_avekm];
    car_avekm &= 0b10111111;
    car_avekm |= ((voice_modulation[voice] > 64) ? 0b01000000 : 0);
    opl.WriteReg(voice_base_car[voice]+AVEKM, car_avekm);
    // opl->WriteReg(voice_base_car[voice]+KSL_TL,pat[uw_patch_file::patchIndices::car_ksl_tl]);
    opl.WriteReg(voice_base_car[voice]+AD,pat[uw_patch_file::patchIndices::car_ad]);
    opl.WriteReg(voice_base_car[voice]+SR,pat[uw_patch_file::patchIndices::car_sr]);
    opl.WriteReg(voice_base_car[voice]+WS,pat[uw_patch_file::patchIndices::car_ws]);

    //Write connection and feedback:
    opl.WriteReg(voice_base2[voice]+FB_C,pat[uw_patch_file::patchIndices::fb_c]);

    //Calculate and write volume levels:
    writeVolume(opl, voice);
    return true;
}

void note_off(oplStream& opl, int midi_num) {
    //Look up the voice playing the note, and the block+f_num values
    int voice_index = find_playing_note(midi_num);
    if(voice_index == -1) return;

    voice_midi_num[voice_index] = -1;

    int block = std::get<1>(freqs[midi_num]);
    int f_num = std::get<2>(freqs[midi_num]);
    opl.WriteReg(voice_base2[voice_index]+ON_BLK_NUM, (block<<(2)) + ((f_num&0xff00)>>(8)));
}

void note_on(oplStream& opl, int midi_num, int velocity) {
    if(midi_num < 12 || midi_num > 108) return; // unsupported note range
    int voice_num = find_unused_voice();
    if(voice_num == -1) {
        std::cout<<"No free voice, dropping a note.\n";
        return;
    }
    voice_midi_num[voice_num] = midi_num;
    voice_velocity[voice_num] = velocity_translation[velocity>>3];

    int retval = copy_patch(opl, voice_num);
    if(!retval) {
        std::cout<<"Had trouble copying "<<int(voice_bank_num[voice_num])<<":"<<int(voice_patch_num[voice_num])
                    <<" to voice "<<int(voice_num)<<". Dropping the note.\n";
        return;
    }

    int block = std::get<1>(freqs[midi_num]);
    int f_num = std::get<2>(freqs[midi_num]);

    opl.WriteReg(voice_base2[voice_num]+0xa0, (f_num&0xff));
    opl.WriteReg(voice_base2[voice_num]+0xb0, 0x20 + (block<<(2)) + ((f_num&0xff00)>>(8)));
    writeVolume(opl, voice_num);
}

std::unordered_map<int, int> keyMap {
    {SDLK_a, 48},
    {SDLK_w, 49},
    {SDLK_s, 50},
    {SDLK_e, 51},
    {SDLK_d, 52},
    {SDLK_f, 53},
    {SDLK_t, 54},
    {SDLK_g, 55},
    {SDLK_y, 56},
    {SDLK_h, 57},
    {SDLK_u, 58},
    {SDLK_j, 59},
    {SDLK_k, 60},
};

bool write_patches(uw_patch_file& uwpf,int bankNum, int patchNum) {
    for(auto& patch: uwpf.bank_data) {
        if(patch.bank == bankNum && patch.patch == patchNum) {
            std::cout<<"Copying "<<bankNum<<":"<<patchNum<<" ("<<patch.name<<") to voice slots\n";
            for(int i = 0; i < OPL_VOICE_COUNT; i++) {
                voice_patch[i] = &patch;
            }
            return true;
        }
    }
    std::cout<<"Patch "<<bankNum<<":"<<patchNum<<" not found.\n";
    return false;
}

bool write_patches(uw_patch_file& uwpf,int patchIndex) {
    auto& patch = uwpf.bank_data[patchIndex % uwpf.bank_data.size()];
    std::cout<<"Copying "<<int(patch.bank)<<":"<<int(patch.patch)<<"("<<patch.name<<") to voice slots\n";
    for(int i = 0; i < OPL_VOICE_COUNT; i++) {
        voice_patch[i] = &patch;
    }
    return true;
}

int main(int argc, char* argv[]) {
    if(argc < 2) {
        std::cout<<"Provide a timbre library as the first argument.\n";
        return 1;
    }
    std::unique_ptr<oplStream> opl = std::make_unique<oplStream>(false); //std::make_unique<oplStream>(false);
    uw_patch_file uwpf;
    bool success = uwpf.load(argv[1]);
    if(!success) {
        std::cerr<<"Couldn't load the patch file. Aborting.\n";
        return 1;
    }

    int patchNum = 0;
    int bankNum = 0;
    if(argc == 4) {
        bankNum = std::atoi(argv[2]);
        patchNum = std::atoi(argv[3]);
    }

    if(!write_patches(uwpf,bankNum,patchNum)) {
        std::cout<<"Patch "<<bankNum<<":"<<patchNum<<" couldn't be loaded. Loading first entry, "<<uwpf.bank_data[0].bank<<":"<<uwpf.bank_data[1].patch<<" instead.\n";
        write_patches(uwpf, uwpf.bank_data[0].bank, uwpf.bank_data[0].patch);
    }

    calc_freqs();

    int failure_code = SDL_Init(SDL_INIT_VIDEO);
    if(failure_code) {
        fprintf(stderr, "Error init'ing video subsystem: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("OPL Keyboard", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 100,100,0);
    SDL_Renderer* r = SDL_CreateRenderer(window, 0, 0);

    opl->play();
    bool quit = false;
    while(!quit) {
        SDL_Event event;
        int note = 0;
        while(SDL_PollEvent(&event)) {
            switch(event.type) {
            case SDL_KEYDOWN:
                if(!event.key.repeat) {
                    // std::cout<<"KEYDOWN: "<<event.key.keysym.sym<<'\n';
                    note = keyMap[SDL_GetKeyFromScancode(event.key.keysym.scancode)];
                    if(note > 0 && note < 100) {
                        note_on(*opl, note, 127);
                        break;
                    }
                    switch(SDL_GetKeyFromScancode(event.key.keysym.scancode)) {
                        case SDLK_q:
                        case SDLK_ESCAPE:
                            quit = true;
                            break;
                        case SDLK_EQUALS:
                            patchNum++;
                            write_patches(uwpf, patchNum);
                            break;
                        case SDLK_MINUS:
                            patchNum--;
                            write_patches(uwpf, patchNum);
                            break;
                    }
                }
                break;
            case SDL_KEYUP:
                // std::cout<<"KEYUP: "<<event.key.keysym.sym<<'\n';
                note = keyMap[SDL_GetKeyFromScancode(event.key.keysym.scancode)];
                if(note > 0 && note < 100) {
                    note_off(*opl, note);
                    break;
                }
                break;
            case SDL_QUIT:
                quit = true;
                break;
            }
        }
        SDL_RenderPresent(r);
        // std::cout<<"Render present...";
        opl->addTime(25);
        // std::cout<<"Loop...";
    }

}
