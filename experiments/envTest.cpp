#include<iostream>
#include<cstdint>
#include<string>
#include<array>
#include<cmath>

// From 0%-100%, attack time in ms
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

// From 10%-90%, attack time in ms
std::array<float, 60> expectedAttackMid {
      0.0,    0.0,     0.0,     0.0,
      0.11,   0.11,    0.14,    0.19,
      0.22,   0.26,    0.31,    0.37,
      0.43,   0.49,    0.61,    0.73,
      0.85,   0.97,    1.13,    1.45,
      1.70,   1.94,    2.26,    2.90,
      3.39,   3.87,    4.51,    5.79,
      6.78,   7.74,    9.02,   11.58,
     13.57,  15.49,   18.05,   23.17,
     27.14,  30.98,   36.10,   46.34,
     54.27,  61.95,   72.13,   92.67,
    108.54, 123.90,  144.38,  185.34,
    217.09, 247.81,  288.77,  370.69,
    434.18, 495.62,  577.54,  741.38,
    858.35, 991.23, 1155.07, 1482.75
};

// From 0%-100%, decay time in ms
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

// From 10%-90%, decay time in ms
std::array<float, 60> expectedDecayMid {
       0.51,    0.51,    0.51,    0.51,
       0.58,    0.69,    0.81,    1.01,
       1.15,    1.35,    1.62,    2.02,
       2.32,    2.68,    3.22,    4.02,
       4.62,    5.38,    6.42,    8.02,
       9.24,   10.76,   12.84,   16.04,
      18.48,   21.52,   25.68,   32.08,
      36.96,   43.04,   51.36,   64.16,
      73.92,   86.08,  102.72,  128.32,
     147.84,  172.16,  205.44,  256.64,
     295.68,  344.32,  410.88,  513.28,
     591.36,  688.64,  821.76, 1026.56,
    1182.72, 1377.28, 1643.52, 2053.12,
    2365.44, 2754.56, 3287.04, 4106.24,
    4730.88, 5503.12, 6574.08, 8212.48
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



void updateEnvelope(int ksrIndex) {

    //if(envPhase == adsrPhase::dampen && envLevel >= 123) { // Dampened previous note, start attack of new note
    //    envPhase = adsrPhase::attack;
    //}
    /*else*/ if(envPhase == adsrPhase::attack && (envLevel <= 0)) { 
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
        //case adsrPhase::dampen: activeRate = 12; break;
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
            targetValue = expectedAttack[index];
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
            targetValue = expectedDecay[index];
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
