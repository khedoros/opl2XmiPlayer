//Read XMI, .AD/.OPL files, convert the MIDI commands to something that the OPL emulator can use.
//I probably want to create a class to handle sound production, though. Either as a wrapper around the OPL,
//or just as a separate class that I can feed data to.

#include<iostream>
#include<memory>
#include "uwPatch.h"
#include "opl.h"
#include "yamahaYm3812.h"
#include "xmi.h"
#include "midiEvent.h"
#include<vector>
#include<list>
#include<tuple>
#include<cmath>
#include<thread>
#include "oplStream.h"
using namespace std;

array<bool,16> seenNotes;
array<vector<tuple<uint8_t, uint8_t>>, 16> seenPatches;

//Store midi note number mappings to OPL block number and OPL F-num values
vector<tuple<uint8_t,    uint8_t,     uint16_t>> freqs;
std::unique_ptr<oplStream> opl;
uw_patch_file uwpf;
uint8_t timbre_bank[256];

//XMI uses a standard 120Hz clock
const uint32_t TICK_RATE = 120;
const float MIDI_MAX_VAL = 127.0;

//Data and methods having to do with current use of OPL channels, voice assignments, etc
const int OPL_VOICE_COUNT = 9;

//Which channel and note each of the 9 voices in an OPL2 chip is set to play. -1 means "none".
std::array<int8_t, OPL_VOICE_COUNT> opl_channel_assignment {-1,-1,-1,-1,-1,-1,-1,-1,-1};
std::array<int8_t, OPL_VOICE_COUNT> opl_note_assignment    {-1,-1,-1,-1,-1,-1,-1,-1,-1};
std::array<int8_t, OPL_VOICE_COUNT> opl_note_velocity      {-1,-1,-1,-1,-1,-1,-1,-1,-1};
std::array<int8_t, OPL_VOICE_COUNT> opl_patch_transpose        {-1,-1,-1,-1,-1,-1,-1,-1,-1};

//Which patch and bank is each MIDI channel currently set to play
//Initialize them to 0:0
const int MIDI_CHANNEL_COUNT = 16;
std::array<uint8_t, MIDI_CHANNEL_COUNT> bank_assignment    {  0,  0,  0,  0,  0,  0,  0,  0,  0,127,  0,  0,  0,  0,  0,  0};
std::array<uint8_t, MIDI_CHANNEL_COUNT> patch_assignment   {  0, 68, 48, 95, 78, 41,  3,110,122, 36,  0,  0,  0,  0,  0,  0};
std::array<uint8_t, MIDI_CHANNEL_COUNT> channel_modulation {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0};
std::array<uint8_t, MIDI_CHANNEL_COUNT> channel_volume     {127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127};
std::array<uint8_t, MIDI_CHANNEL_COUNT> channel_expression {127,127,127,127,127,127,127,127,127,127,127,127,127,127,127,127};

const std::array<uint8_t, 16> velocity_translation { 0x52, 0x55, 0x58, 0x5b, 0x5e, 0x61, 0x64, 0x67, 0x6a, 0x6d, 0x70, 0x73, 0x76, 0x79, 0x7c, 0x7f };

const std::array<uint16_t, OPL_VOICE_COUNT> voice_base   {    0,     1,     2,    8,    9,  0xa, 0x10, 0x11, 0x12};
const std::array<uint16_t, OPL_VOICE_COUNT> voice_base2  {    0,     1,     2,    3,    4,    5,    6,    7,    8};

enum OPL_addresses {
    TEST       = 0x01, //Set to 0
    TIMER1     = 0x02, //'      '
    TIMER2     = 0x03, //'      '
    TIMER_CTRL = 0x04, //'      '
    NOTE_SEL   = 0x05, 
    FLAGS_MULT = 0x20,
    VOL_KS     = 0x40,
    DEC_ATT    = 0x60,
    REL_SUS    = 0x80,
    F_NUM_L    = 0xa0,
    ON_BLK_NUM = 0xb0,
    TREM_VIB   = 0xbd, //Set to 0xc0
    FEED_CON   = 0xc0,
    WAVEFORM   = 0xe0
};

//Find the voice playing the given note on the given channel
//-1 means "not found"
int8_t find_playing(uint8_t channel, uint8_t note) {
    for(int i=0;i<OPL_VOICE_COUNT;++i) {
        if(opl_channel_assignment[i] == channel && opl_note_assignment[i] == note)
            return i;
    }
    return -1;
}

//Find the first voice that's currently empty
//-1 means 'all in use'
int8_t find_unused() {
    for(int i=0;i<OPL_VOICE_COUNT;++i) {
        if(opl_channel_assignment[i] == -1)
            return i;
    }
    cout<<"Couldn't find a free OPL voice."<<endl;
    return -1;
}

//Mathematically calculate the best OPL settings to match Midi frequencies
//Outputs the best matches into the freqs vector of 3-tuples.
void calc_freqs() {
    double base_freq = 440.0;
    uint8_t base_mid_num = 69;
    for(uint16_t mid_num = 0; mid_num < 128; ++mid_num) {
        double midi_freq = base_freq * pow(2.0, (mid_num - base_mid_num)/12.0);
        //cout<<"MIDI Number: "<<mid_num<<" Frequency: "<<midi_freq;
        double diff = 9999999999.0;
        uint8_t blk = 0;
        uint16_t f_num = 0;
        double OPL_freq = 0.0;
        for(uint32_t block = 0; block < 8; ++block) {
            for(uint32_t f_number = 0; f_number < 1024; ++f_number) {
                double opl_freq = double(f_number * /*49716*/ OPL_SAMPLE_RATE ) / pow(2.0, 20 - double(block));
                if(abs(opl_freq - midi_freq) < diff) {
                    diff = abs(opl_freq - midi_freq);
                    f_num = f_number;
                    blk = block;
                    OPL_freq = opl_freq;
                }
            }
        }
        if(diff < 10) {
            //cout<<" OPL_Blk: "<<uint16_t(blk)<<" F-Num: "<<f_num<<" OPL Freq: "<<OPL_freq<<endl;
            freqs.push_back(make_tuple(mid_num,blk,f_num));
        }
        else {
            //cout<<" OPL: Out of Range"<<endl;
        }
    }
}

//Methods to do sets of register writes to the OPL synth emulator.

//Write values to the OPL2 emulator to initialize it to defaults
void init_opl2() {
    opl->Reset();
    for(int i=0;i<OPL_VOICE_COUNT;++i)
        opl->SetPanning(i, 0.5,0.5);
     const uint8_t init_array1[] =
     {0,0x20,   0,   0,0x60,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   //00-0f Turn on waveform select, mask timer interrupts
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   //10-1f 
      1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   //20-2f Frequency mult: 1, voices 0-8
      1,   1,   1,   1,   1,   1,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   //30-3f '                           '
     63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,  63,   //40-4f Volume attenuation to full
     63,  63,  63,  63,  63,  63,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   //50-5f '                        '
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,   //60-6f Full attack and decay rates
    255, 255, 255, 255, 255, 255,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   //70-7f '                         '
     15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,  15,   //80-8f Low sustain level, high release rate
     15,  15,  15,  15,  15,  15,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   //90-9f '                                  '
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   //a0-af F-Num, lower 8 bits to 0
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,0xc0,   0,   0,   //b0-bf 0 out notes, turn on tremolo and vibrato, turn off rhythm
   0xf0,0xf0,0xf0,0xf0,0xf0,0xf0,0xf0,0xf0,0xf0,   0,   0,   0,   0,   0,   0,   0,   //c0-cf Turn on output to both speakers
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   //d0-df
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   //e0-ef Set waveforms to sine
      0,   0,   0,   0,   0,   0};                                                    //f0-f5 '                   '

    for(size_t reg = 0; reg < 0xf6; ++reg) {
        opl->WriteReg(reg,init_array1[reg]);
    }
}

//Set the notes of the OPL3 to silent
void pause_sound() {
    opl->pause();
}

//Set the notes of the OPL3 to play again, if previously silenced
void unpause_sound() {
    opl->play();
}

// Write proper volume for the given voice, taking into account the patch's TL, note velocity, channel volume and expression.
void writeVolume(uint8_t voice_num) {
    uint8_t channel = opl_channel_assignment[voice_num];
    uint8_t patchNum = patch_assignment[channel];
    uint8_t bankNum = bank_assignment[channel];
    uint8_t velocity = opl_note_velocity[voice_num];
    uint8_t mod_tl, car_tl, connection, fb, mod_ksl, car_ksl;
    for(auto& patch: uwpf.bank_data) {
        if(patch.patch == patchNum && patch.bank == bankNum) {
            mod_tl = patch.ad_patchdatastruct.mod_out_lvl;
            car_tl = patch.ad_patchdatastruct.car_out_lvl;
            mod_tl = (~mod_tl) & 0x3f;
            car_tl = (~car_tl) & 0x3f;
            connection = patch.ad_patchdatastruct.connection;
            fb = patch.ad_patchdatastruct.feedback;
            mod_ksl = patch.ad_patchdatastruct.mod_key_scale;
            car_ksl = patch.ad_patchdatastruct.car_key_scale;
        }
    }

    uint16_t vol = (uint16_t(channel_volume[channel]) * channel_expression[channel])>>7;
    vol *= velocity; vol >>= 7;
    if(connection) { // additive operator, so scale modulating operator too
        uint16_t mod_vol = ~((vol * mod_tl) / 127);
        opl->WriteReg(voice_base[voice_num]+0x40,(mod_vol & 0x3f) +
                                            ((mod_ksl & 0x3)<<(6)));
    }
    uint16_t car_vol = ~((vol * car_tl) / 127);
    opl->WriteReg(voice_base[voice_num]+0x43,((car_vol & 0x3f) +
                                         ((car_ksl & 0x3)<<(6))));
}

//Copies the given patch data into the given voice slot
                //OPL voice #, bank #,       instrument patch #
bool copy_patch(uint8_t voice, uint8_t bank, uint8_t patch) {
    for(auto it = uwpf.bank_data.begin(); it != uwpf.bank_data.end(); ++it) {
        if(it->bank == bank && it->patch == patch) {
            uw_patch_file::opl2_patch pat = it->ad_patchdatastruct;
            opl_patch_transpose[voice] = pat.transpose;
            //Write the values to the modulator:
            opl->WriteReg(voice_base[voice]+0x20,(pat.mod_freq_mult&0x0f) +
                                                 ((pat.mod_env_scaling&1)<<(4)) +
                                                 ((pat.mod_sustain_sound&1)<<(5)) +
                                                 ((pat.mod_ampl_vibrato&1)<<(6)) +
                                                 ((pat.mod_freq_vibrato&1)<<(7)));
            if(pat.connection == 0) { 
                opl->WriteReg(voice_base[voice]+0x40,(pat.mod_out_lvl&0x3f) +
                                                     ((pat.mod_key_scale&0x3)<<(6)));
            }
            opl->WriteReg(voice_base[voice]+0x60,(pat.mod_decay&0xf) +
                                                 ((pat.mod_attack&0xf)<<(4)));
            opl->WriteReg(voice_base[voice]+0x80,(pat.mod_release&0xf) +
                                                 ((pat.mod_sustain_lvl&0xf)<<(4)));
            opl->WriteReg(voice_base[voice]+0xc0,(pat.connection&1) +
                                                 ((pat.feedback&7)<<(1)));
            opl->WriteReg(voice_base[voice]+0xe0,(pat.mod_waveform&3));

            //Write the values to the carrier:
            opl->WriteReg(voice_base[voice]+0x23,(pat.car_freq_mult&0x0f) +
                                                 ((pat.car_env_scaling&1)<<(4)) +
                                                 ((pat.car_sustain_sound&1)<<(5)) +
                                                 ((pat.car_ampl_vibrato&1)<<(6)) +
                                                 ((pat.car_freq_vibrato&1)<<(7)));
            /*
            opl->WriteReg(voice_base[voice]+0x43,((pat.car_out_lvl&0x3f) +
                                                 ((pat.car_key_scale&0x3)<<(6))));
            */
            opl->WriteReg(voice_base[voice]+0x63,(pat.car_decay&0xf) +
                                                 ((pat.car_attack&0xf)<<(4)));
            opl->WriteReg(voice_base[voice]+0x83,(pat.car_release&0xf) +
                                                 ((pat.car_sustain_lvl&0xf)<<(4)));
            opl->WriteReg(voice_base[voice]+0xc3,(pat.connection&0x7)+
                                                 ((pat.feedback&0x7)<<(1)));
            opl->WriteReg(voice_base[voice]+0xe3,(pat.car_waveform&3));
            writeVolume(voice);
            return true;
        }
    }
    cout<<"Bank: "<<int(bank)<<" Patch: "<<int(patch)<<endl;
    return false;        
}

int main(int argc, char* argv[]) {
    xmi xmifile;

    if(argc == 3) {
        //Load the patch data
        bool success = uwpf.load(argv[1]);
        if(!success) {
            cout<<"Couldn't load the patch file. Aborting.\n";
        }
        //Load the music itself
        success = xmifile.load(argv[2]);
        if(!success) {
            cout<<"Couldn't load the xmi file. Aborting.\n";
        }
    }
    else {
        std::cout<<"Provide the instrument library and XMI file to load.\n";
        return 1;
    }

    string output_file = "";

    if(argc == 4) {
        output_file = argv[3];
    }
    //Instantiate the OPL emulator
    opl = std::make_unique<oplStream>();
    init_opl2();

    calc_freqs();//Populate the frequency conversion table

    //Look up the timbre->bank map in the xmi file
    pair<uint8_t,uint8_t> * p = xmifile.next_timbre();

    while(p != nullptr) {
        //Store the expected bank for this timbre
        timbre_bank[p->second] = p->first;
        cout<<"Timbre: Bank: "<<int(p->first)<<" Patch: "<<int(p->second)<<endl;
        p = xmifile.next_timbre();
    }

    uint32_t curTime = 0;
    uint8_t meta = 0;
    uint8_t channel = 0;
    int8_t voice_num = 0;
    uint16_t f_num = 0;
    uint8_t block = 0;
    uint8_t midi_num = 0;
    uint8_t * v;
    uint8_t potential_instrument_num = 0;
    uint32_t tick_count = xmifile.tick_count();

    uint8_t for_counter[4] = {0};
    int for_nesting = 0;;

    float lmaxval = 0, lminval = 0, rmaxval = 0, rminval = 0;

    opl->play();

    //Start processing MIDI events from the XMI file
    midi_event* e = xmifile.next_event();
    while(e != nullptr) {
        uint32_t tickCount = e->get_time() - curTime;
        if(tickCount != 0) {
            // std::cout<<"TickCount: "<<tickCount<<'\n';
            // std::cout<<"Should pause for "<<((tickCount * 1000) / TICK_RATE)<<" milliseconds\n";
            opl->addTime((tickCount * 1000) / TICK_RATE );
        }
        v = e->get_data();
        channel = e->get_channel();
        curTime = e->get_time();
        bool retval = true;
        switch(e->get_command()) {
        case midi_event::NOTE_OFF: //0x80
            //Look up the voice playing the note, and the block+f_num values
            midi_num = v[1];
            block = get<1>(freqs[midi_num]);
            f_num = get<2>(freqs[midi_num]);
            voice_num = find_playing(channel, midi_num);
            if(voice_num == -1) {
                cout<<"Couldn't find a voice playing "<<int(midi_num)<<" for channel "<<int(channel)<<endl;
                break;
            }
            //Clear the note tracking, write the note-off register commands.
            opl_channel_assignment[voice_num] = -1;
            opl_note_assignment[voice_num] = -1;
            opl->WriteReg(voice_base2[voice_num]+0xb0, (block<<(2)) + ((f_num&0xff00)>>(8)));
            break;
        case midi_event::NOTE_ON: //0x90
            //Find an empty voice, copy the patch currently assigned to this command's channel to that voice
            voice_num = find_unused();
            if(voice_num == -1) {
                cout<<"No free voice, dropping a note."<<endl;
                break;
            }
            seenNotes[channel]=true;

            // Need to know the note number, if the instrument is rhythm
            /*
            if(bank_assignment[channel] == 127) {
                uint8_t patchNum = v[1];
                retval = copy_patch(voice_num, bank_assignment[channel], patchNum);
                std::cout<<"Channel "<<int(channel)<<": Bank 127, patch "<<int(patchNum)<<" (channel assignment "<<patch_assignment[channel]<<") "; 
                midi_num = opl_patch_transpose[voice_num];
                std::cout<<" note "<<int(midi_num)<<'\n';
                opl_note_assignment[voice_num] = patchNum; 

            }
            else {*/
                retval = copy_patch(voice_num, bank_assignment[channel], patch_assignment[channel]);
                midi_num = v[1];
                opl_note_assignment[voice_num] = midi_num; 
                opl_note_velocity[voice_num] = velocity_translation[v[2]>>4];
            //}

            if(!retval) { cout<<"Had trouble copying "<<int(bank_assignment[channel])<<":"<<int(patch_assignment[channel])<<" to channel "<<int(channel)<<". Dropping the note."<<endl;
                break;
            }
            //Look up the note info, store in note tracking, write the note-on register commands

            block = get<1>(freqs[midi_num]);
            f_num = get<2>(freqs[midi_num]);
            opl_channel_assignment[voice_num] = channel;

            opl->WriteReg(voice_base2[voice_num]+0xa0, (f_num&0xff));
            opl->WriteReg(voice_base2[voice_num]+0xb0, 0x20 + (block<<(2)) + ((f_num&0xff00)>>(8)));
            writeVolume(voice_num);
            break;
        case midi_event::PROGRAM_CHANGE: //0xc0
            patch_assignment[channel] = v[1];
            // bank_assignment[channel] = timbre_bank[v[1]];
            /*
            for(int i=0;i<OPL_VOICE_COUNT;++i) {
                if(opl_channel_assignment[i] == channel) {
                    retval = copy_patch(i, bank_assignment[channel], patch_assignment[channel]);
                    if(!retval) {
                        cout<<"Had trouble copying "<<int(bank_assignment[channel])<<":"<<int(patch_assignment[channel])<<" to channel "<<int(channel)<<endl;
                        break;
                    }
                }
            }*/
            cout<<dec<<"Program change: Channel "<<int(channel)<<"->"<<int(bank_assignment[channel])<<":"<<int(patch_assignment[channel])<<endl;
            break;
        case midi_event::CONTROL_CHANGE: //0xb0
            cout<<"CC: "<<hex<<int(v[0])<<" "<<int(v[1])<<" "<<int(v[2])<<endl;
            meta = e->get_meta();
            if(meta == 0x01) { //Modulation change (set vibrato if over 64)
                channel_modulation[channel] = v[2];
                for(int i=0;i<OPL_VOICE_COUNT;++i) {
                    uint8_t patchNum = patch_assignment[channel];
                    uint8_t bankNum = bank_assignment[channel];
                    if(opl_channel_assignment[i] == channel) {
                        for(auto& patch: uwpf.bank_data) {
                            if(patch.patch == patchNum && patch.bank == bankNum) {
                                opl->WriteReg(voice_base[i]+0x23,(patch.ad_patchdatastruct.car_freq_mult&0x0f) +
                                                                ((patch.ad_patchdatastruct.car_env_scaling&1)<<(4)) +
                                                                ((patch.ad_patchdatastruct.car_sustain_sound&1)<<(5)) +
                                                                ((patch.ad_patchdatastruct.car_ampl_vibrato&1)<<(6)) +
                                                                ((v[2] > 64) ? 128 : 0)); // freq vibrato
                            }
                        }
                    }
                }
            }
            else if(meta == 0x07) { //Volume change
                channel_volume[channel] = v[2];
                for(int i=0;i<OPL_VOICE_COUNT;++i) {
                    if(opl_channel_assignment[i] == channel) {
                        writeVolume(i);
                        // opl->WriteReg(voice_base[i] + 0x40 + 3, 63 - (channel_volume[channel] >> 1));
                    }
                }
            }
            else if(meta == 0x0a) { //Panning control
                for(int i=0;i<OPL_VOICE_COUNT;++i) {
                    if(opl_channel_assignment[i] == channel)
                        opl->SetPanning(channel, 0.5 * (1.0 - float(v[2])/MIDI_MAX_VAL), 0.5*(float(v[2])/MIDI_MAX_VAL));
                }
            }
            else if(meta == 0x0b) { //Expression change (also influences volume)
                channel_expression[channel] = v[2];
                for(int i=0;i<OPL_VOICE_COUNT;++i) {
                    if(opl_channel_assignment[i] == channel) {
                        writeVolume(i);
                        // opl->WriteReg(voice_base[i] + 0x40 + 3, 63 - (channel_volume[channel] >> 1));
                    }
                }
            }
            else if(meta == 0x72) { //Bank change
                bank_assignment[channel] = v[2];
                /*
                for(int i=0;i<OPL_VOICE_COUNT;++i) {
                    if(opl_channel_assignment[i] == channel) {
                        copy_patch(i, bank_assignment[channel], patch_assignment[channel]);
                    }
                }*/
                cout<<dec<<"Bank change: Channel "<<int(channel)<<"->"<<int(bank_assignment[channel])<<":"<<int(patch_assignment[channel])<<endl;
            }           
            else if(meta == 0x6e) cout<<"Channel lock (not implemented)"<<endl;
            else if(meta == 0x6f) cout<<"Channel lock protect (not implemented)"<<endl;
            else if(meta == 0x70) cout<<"Voice protect (not implemented)"<<endl;
            else if(meta == 0x71) cout<<"Timbre protect (not implemented)"<<endl;
            else if(meta == 0x73) cout<<"Indirect controller prefix (not implemented)"<<endl;
            else if(meta == 0x74) {
                cout<<"For loop controller (not implemented) data: ";
                for(int i=0;i<e->get_data_size();++i) {
                    cout<<hex<<int(v[i])<<" ";
                }
                cout<<endl;
            }
            else if(meta == 0x75) {
                cout<<"Next/Break loop controller (not implemented) data: ";
                for(int i=0;i<e->get_data_size();++i) {
                    cout<<hex<<int(v[i])<<" ";
                }
                cout<<endl;
            }
            else if(meta == 0x76) cout<<"Clear beat/bar count (not implemented)"<<endl;
            else if(meta == 0x77) cout<<"Callback trigger (not implemented)"<<endl;
            else if(meta == 0x78) cout<<"Sequence branch index (not implemented)"<<endl;
            else 
                cout<<"Other unimplemented control change: "<<int(v[1])<<" = "<<int(v[2])<<endl;
            break;
        case midi_event::META: //0xff
            if(e->get_command() != 0xf0)
                cout<<"Unexpected command coming into META: "<<int(e->get_command())<<endl;
            meta = e->get_meta();
            if(meta == 0x2f) {
                cout<<"End of track."<<endl;

                if(argc == 3) { // TODO: Move this functionality into oplStream?
                    //bool worked = sb.saveToFile(output_file);
                    bool worked = false;
                    std::cout<<"Output to file currently disabled.\n";
                    if(!worked) cout<<"Couldn't output to the file '"<<output_file<<"'. Sorry."<<endl;
                    else cout<<"Output a rendering of the music to '"<<output_file<<"'."<<endl;
                }
                else {
                    while(opl->getQueuedAudioBytes() > 0) {
                        SDL_Delay(100);
                    }
                    cout<<"Done playing."<<endl;
                    for(int i=0;i<MIDI_CHANNEL_COUNT;i++) {
                        std::cout<<"Channel "<<i+1<<": "<<seenNotes[i]<<'\n';
                    }
                }
                return 0;
            }
            else {
                cout<<"Ignoring meta command"<<std::hex<<" 0x"<<int(meta)<<endl;
            }
            break;
        case midi_event::PITCH_WHEEL: //0xe0
            cout<<"Don't want to do the pitch wheel, so I didn't. (value: "<<std::hex<<int(v[0])<<" "<<int(v[1])<<" "<<int(v[2])<<")"<<endl;
            break;
        default:
            cout<<"Not implemented, yet: ";
            cout<<hex<<int(v[0])<<" "<<int(v[1])<<" "<<int(v[2])<<endl;
        }

        e = xmifile.next_event();
    }
    cout<<"I reached the end of the track without seeing the appropriate command. Weird."<<endl;
    return 1;
}
