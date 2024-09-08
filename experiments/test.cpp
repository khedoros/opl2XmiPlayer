#include<cmath>
#include<cstdint>
#include<iostream>
#include "opl.h"
#define MIDI_MAX_VAL 127.0


//Mathematically calculate the best OPL settings to match Midi frequencies
//Outputs the best matches into the freqs vector of 3-tuples.
void calc_freqs() {
    double base_freq = 440.0;
    uint8_t base_mid_num = 69;
    for(uint16_t mid_num = 0; mid_num < 128; ++mid_num) {
        double midi_freq = base_freq * pow(2.0, (mid_num - base_mid_num)/12.0);
        std::cout<<"MIDI Number: "<<mid_num<<" Frequency: "<<midi_freq;
        double diff = 9999999999.0;
        uint8_t blk = 0;
        uint16_t f_num = 0;
        double OPL_freq = 0.0;
        for(uint32_t block = 0; block < 8; ++block) {
            for(uint32_t f_number = 0; f_number < 1024; ++f_number) {
                double opl_freq = double(f_number * /*49716*/ NATIVE_OPL_SAMPLE_RATE ) / pow(2.0, 20.0 - double(block));
                if(abs(opl_freq - midi_freq) < diff) {
                    diff = abs(opl_freq - midi_freq);
                    f_num = f_number;
                    blk = block;
                    OPL_freq = opl_freq;
                }
            }
        }
        if(diff < 10) {
            std::cout<<" OPL_Blk: "<<uint16_t(blk)<<" F-Num: "<<f_num<<"("<<std::hex<<f_num<<std::dec<<") OPL Freq: "<<OPL_freq<<'\n';
            // freqs.push_back(make_tuple(mid_num,blk,f_num));
        }
        else {
            std::cout<<" OPL: Out of Range\n";
        }
    }
}

/*
auto midi2opl(unsigned char vol) -> uint8_t {
    // return 63 - (uint8_t((float(channel_volume[channel])/MIDI_MAX_VAL) * (float(vol) / 2.0)));
    return 63 - (uint8_t((float(vol)/(MIDI_MAX_VAL+1)) * 64.0));
}
*/

int main() {/*
    for(int i = 0; i < 256; i++) {
        std::cout<<"MIDI vol: "<<i<<"\tOPL vol: "<<int(midi2opl(i))<<'\n';
    }*/
    calc_freqs();
}
