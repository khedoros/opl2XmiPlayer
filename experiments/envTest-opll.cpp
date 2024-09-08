#include<iostream>
#include<cstdint>
#include<string>
#include<array>
#include<cmath>

std::array<float, 64> expectedAttack {
0, 0, 0, 0, 
0.14, 0.18, 0.22, 0.28, 
0.3, 0.34, 0.42, 0.5, 
0.54, 0.6, 0.7, 0.84, 
0.97, 1.13, 1.37, 1.69, 
1.93, 2.25, 2.74, 3.3, 
3.86, 4.51, 5.47, 6.76, 
7.72, 9.01, 10.94, 13.52, 
15.45, 18.02, 21, 27.03, 
30.9, 36.04, 43.77, 54.87, 
61.79, 72.89, 87.54, 108.13, 
123.5, 144.48, 175.07, 216.27, 
247.16, 280.36, 358.15, 432.54, 
494.33, 576.72, 780.3, 865.88, 
988.66, 1153.43, 1400.6, 1730.15,
0,0,0,0
};

std::array<float, 64> expectedDecay {
1.27, 1.27, 1.27, 1.27, 
    1.47, 1.71, 2.05, 2.55, 
    2.94, 3.42, 4.1, 5.11, 
    5.87, 6.84, 8.21, 10.22, 
    11.75, 13.6, 16.41, 20.44, 
    23.49, 27.36, 32.03, 40.07, 
    46.99, 54.71, 65.65, 81.74, 
    93.97, 109.42, 131.31, 163.49, 
    187.95, 218.84, 262.61, 326.98, 
    375.98, 437.69, 525.22, 653.95, 
    751.79, 875.37, 1050.45, 1307.91, 
    1503.58, 1750.75, 2180.89, 2615.82, 
    3007.16, 3501.49, 4201.79, 5231.64, 
    6014.32, 7002.98, 8403.58, 10463.3, 
    12078.7, 14606.8, 16807.2, 20926.6,
        0,0,0,0
};

#define NTSC_COLOR_BURST 315000000.0 / 88.0
#define RATE NTSC_COLOR_BURST / 72.0

static const int envAccumRate = 1'000'000 / (RATE);

enum adsrPhase {
    silent,         //Note hit envelope==48dB
    dampen,         //End of previous note, behaves like a base decay rate of 12, adjusted by rate_key_scale
    attack,         //New note rising after key-on
    decay,          //Initial fade to sustain level after reaching max volume
    sustain,        //Level to hold at until key-off, or level at which to transition from decay to release phase
    release         //key-off
};

std::array<std::string, 6> phaseNames {
    "silent",
    "dampen",
    "attack",
    "decay",
    "sustain",
    "release"
};

adsrPhase envPhase = adsrPhase::attack;
int envLevel = 127; // 0 - 127. 0.375dB steps (add envLevel * 0x10)
long long counter = 0;

//reg base 60
unsigned attackRate = 0;
unsigned decayRate = 0;

//reg 80
unsigned sustainLevel = 0;
unsigned releaseRate = 0;

int envAccum = 0;

const std::array<int,64> attackTable {
    0, 0, 0, 0,
    22254, 17739, 14836, 12578, 
    11127, 8869, 7418, 6289, 
    5563, 4435, 3709, 3145, 
    2782, 2217, 1854, 1572, 
    1391, 1109, 927, 786, 
    695, 554, 464, 393, 
    348, 277, 232, 197, 
    174, 139, 116, 98, 
    87, 69, 58, 49, 
    43, 35, 29, 25, 
    22, 17, 14, 12, 
    11, 9, 7, 6, 
    6, 5, 4, 3, 
    3, 2, 2, 2, 
    2, 2, 2, 2
};

const std::array<int,64> decayTable {
    0, 0, 0, 0,
    309296, 247373, 206090, 176741, 
    154648, 123686, 103045, 88370, 
    77324, 61843, 51523, 44185, 
    38662, 30922, 25761, 22093, 
    19331, 15461, 12881, 11046, 
    9666, 7730, 6440, 5523, 
    4833, 3865, 3220, 2762, 
    2416, 1933, 1610, 1381, 
    1208, 966, 805, 690, 
    604, 483, 403, 345, 
    302, 242, 201, 173, 
    151, 121, 101, 86, 
    76, 60, 50, 43, 
    38, 30, 25, 22, 
    19, 19, 19, 19,
};

void updateEnvelope(int ksrIndex) {
    if(envPhase == adsrPhase::dampen && envLevel >= 123) { // Dampened previous note, start attack of new note
        envPhase = adsrPhase::attack;
    }
    else if(envPhase == adsrPhase::attack && (envLevel <= 0)) { 
        envPhase = adsrPhase::decay;
        envLevel = 0;
    }
    else if(envPhase == adsrPhase::decay && envLevel >= sustainLevel * 8) {
        if(sustain) {
            envPhase = adsrPhase::sustain;
            envLevel = sustainLevel * 8;
        }
        else {
            envPhase = adsrPhase::release;
        }
    }
    else if((envPhase == adsrPhase::sustain || envPhase == adsrPhase::release) && envLevel >= 123) {
        envPhase = adsrPhase::silent;
    }

    int activeRate = 0;
    bool attack = false;
    switch(envPhase) {
        case adsrPhase::silent: activeRate = 0; break;
        case adsrPhase::dampen: activeRate = 12; break;
        case adsrPhase::attack: activeRate = attackRate; attack = true; break;
        case adsrPhase::decay:  activeRate = decayRate; break;
        case adsrPhase::sustain: activeRate = 0; break;
        case adsrPhase::release: activeRate = releaseRate; break;
        default: activeRate = 0;
            std::cout<<"Unhandled envPhase: "<<envPhase<<"\n";
            break;
    }

    if(activeRate != 0) {
        envAccum += envAccumRate;
        int targetValue = 0;
        if(attack && activeRate != 0 && activeRate != 15) {
            int index = std::min(63, activeRate * 4 + ksrIndex);
            targetValue = attackTable[index];
            if(envAccum >= targetValue) {
                envLevel -= (envAccum / targetValue);
                envAccum -= (envAccum / targetValue) * targetValue;
            }
        }
        else if(attack && activeRate == 15) {
            envLevel = 0;
        }
        else {
            int index = std::min(63, activeRate * 4 + ksrIndex);
            targetValue = decayTable[index];
            if(envAccum >= targetValue) {
                envLevel += (envAccum / targetValue);
                envAccum -= (envAccum / targetValue) * targetValue;
            }
        }
    }

    if(envLevel < 0) envLevel = 0; // assume wrap-around
    else if(envLevel > 127) envLevel = 127; //assume it just overflowed the 8-bit value
}

int main() {
    std::cout<<"Attack table in uS: ";
    for(int i=0;i<60;i++) {
        std::cout<<int(std::round((expectedAttack[59-i] * 1000) / 127))<<", ";
    }
    std::cout<<"\n\n Decay table in uS: ";
        for(int i=0;i<60;i++) {
        std::cout<<int(std::round((expectedDecay[59-i] * 1000)/127))<<", ";
    }

    std::cout<<"Audio samples per envelope level for each AR value: \n";
    for(int i=0;i<60;i+=4) {
        std::cout<<"AR: "<<15 - i/4<<": "<<(RATE * (expectedAttack[i]/1000)) / 127.0 <<" ("<<RATE*expectedAttack[i]/1000<<" samples for the whole transition)\n";
    }
    std::cout<<"Audio samples per envelope level for each DR value: \n";
    for(int i=0;i<60;i+=4) {
        std::cout<<"DR: "<<15 - i/4<<": "<<(RATE * (expectedDecay[i]/1000)) / 127.0 <<" ("<<RATE*expectedDecay[i]/1000<<" samples for the whole transition)\n";
    }

    std::cout<<"\n\n";

    for(int attack = 1; attack < 15; attack++) {
        for(int ksr = 0; ksr < 4; ksr++) {
            counter = 0;
            envLevel = 127;
            envPhase = adsrPhase::attack;
            attackRate = attack;
            while(envPhase == adsrPhase::attack) {
                updateEnvelope(ksr);
                counter++;
                //std::cout<<"counter: "<<counter<<" envLevel: "<<envLevel<<'\n';
            }
            std::cout<<"Attack "<<attack<<" ksr "<<ksr<<" ran for "<<counter<<" cycles ("<<(counter*1000)/49716<<" ms)\n";
        }
    }
    for(int decay = 1; decay < 16; decay++) {
        for(int ksr = 0; ksr < 4; ksr++) {
            counter = 0;
            envLevel = 0;
            envPhase = adsrPhase::release;
            releaseRate = decay;
            while(envPhase == adsrPhase::release) {
                updateEnvelope(ksr);
                counter++;
            }
            std::cout<<"Release "<<decay<<" ksr "<<ksr<<" ran for "<<counter<<" cycles ("<<(counter*1000)/49716<<" ms)\n";
        }
    }
}
