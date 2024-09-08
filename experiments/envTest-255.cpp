#include<iostream>
#include<cstdint>
#include<string>
#include<array>
#include<cmath>

std::array<float, 60> expectedAttack {
       0.0,     0.0,     0.0,   0.0,
       0.2,     0.24,    0.3,   0.38,
       0.42,    0.46,    0.58,  0.7,
       0.8,     0.92,    1.12,  1.4,
       1.56,    1.84,    2.2,   2.76,
       3.12,    3.68,    4.4,   5.52,
       6.24,    7.36,    8.8,  11.04,
      12.48,   14.72,   17.6,  22.08,
      24.96,   29.44,   35.2,  44.16,
      49.92,   58.88,   70.4,  88.32,
      99.84,  117.76,  140.8,  176.64,
     199.68,  235.52,  281.6,  353.28,
     399.36,  471.04,  563.2,  706.56,
     798.72,  942.08, 1126.4, 1413.12,
    1597.44, 1884.16, 2252.8, 2826.24
};

std::array<float, 60> expectedDecay {
        2.4,      2.4,      2.4,      2.4,
        2.74,     3.2,      3.84,     4.8,
        5.48,     6.4,      7.68,     9.6,
       10.96,    12.8,     15.36,    19.20,
       21.92,    25.56,    30.68,    38.36,
       43.84,    51.12,    61.36,    76.72,
       87.68,   102.24,   122.72,   153.44,
      175.36,   204.48,   245.44,   306.88,
      350.72,   408.96,   490.88,   613.76,
      701.44,   817.92,   981.76,  1227.52,
     1402.88,  1635.84,  1963.52,  2455.04,
     2805.76,  3271.68,  3927.04,  4910.08,
     5611.52,  6543.36,  7854.08,  9820.16,
    11223.04, 13086.72, 15708.16, 19640.32,
    22446.08, 26173.44, 31416.32, 39280.64
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
int envLevel = 255; // 0 - 127. 0.375dB steps (add envLevel * 0x10)
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
    if(envPhase == adsrPhase::dampen && envLevel >= 251) { // Dampened previous note, start attack of new note
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
    else if((envPhase == adsrPhase::sustain || envPhase == adsrPhase::release) && envLevel >= 251) {
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
            targetValue = attackTable[index] / 2;
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
            targetValue = decayTable[index] / 2;
            if(envAccum >= targetValue) {
                envLevel += (envAccum / targetValue);
                envAccum -= (envAccum / targetValue) * targetValue;
            }
        }
    }

    if(envLevel < 0) envLevel = 0; // assume wrap-around
    else if(envLevel > 255) envLevel = 255; //assume it just overflowed the 8-bit value
}

int main() {
    std::cout<<"Attack table in uS: ";
    for(int i=0;i<60;i++) {
        std::cout<<int(std::round((expectedAttack[59-i] * 1000) / 255))<<", ";
        if((i+1)%4==0) std::cout<<'\n';
    }
    std::cout<<"\n\n Decay table in uS: ";
        for(int i=0;i<60;i++) {
        std::cout<<int(std::round((expectedDecay[59-i] * 1000)/255))<<", ";
        if((i+1)%4==0) std::cout<<'\n';
    }

    std::cout<<"Audio samples per envelope level for each AR value: \n";
    for(int i=0;i<60;i+=4) {
        std::cout<<"AR: "<<15 - i/4<<": "<<(RATE * (expectedAttack[i]/1000)) / 255.0 <<" ("<<RATE*expectedAttack[i]/1000<<" samples for the whole transition)\n";
    }
    std::cout<<"Audio samples per envelope level for each DR value: \n";
    for(int i=0;i<60;i+=4) {
        std::cout<<"DR: "<<15 - i/4<<": "<<(RATE * (expectedDecay[i]/1000)) / 255.0 <<" ("<<RATE*expectedDecay[i]/1000<<" samples for the whole transition)\n";
    }

    std::cout<<"\n\n";

    for(int attack = 1; attack < 15; attack++) {
        for(int ksr = 0; ksr < 4; ksr++) {
            counter = 0;
            envLevel = 255;
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
