#include<iostream>
#include<cmath>
#include<memory>
#include "yamahaYm3812.h"
#include "util.h"

OPLEmul *YamahaYm3812Create(bool stereo)
{
    /* emulator create */
    return new YamahaYm3812(stereo);
}

void YamahaYm3812::Reset() {
    for(int addr=0;addr<255;addr++) {
        if(addr >= 0x40 && addr < 0x60) {
            WriteReg(addr,0x3f);
        }
        else {
            WriteReg(addr, 0);
        }
    }
    for(int ch = 0; ch < 9; ch++) {
        chan[ch].modOp.phaseInc = 0;
        chan[ch].modOp.phaseCnt = 0;
        chan[ch].modOp.envPhase = adsrPhase::silent;
        chan[ch].modOp.envLevel = 127;
        chan[ch].modOp.amPhase = 0;
        chan[ch].modOp.amAtten = 0;
        chan[ch].modOp.fmPhase = 0;
        chan[ch].modOp.fmRow = 0;
        chan[ch].modOp.fmShift = 0;
        chan[ch].modOp.modFB1 = 0;
        chan[ch].modOp.modFB2 = 0;


        chan[ch].carOp.phaseInc = 0;
        chan[ch].carOp.phaseCnt = 0;
        chan[ch].carOp.envPhase = adsrPhase::silent;
        chan[ch].carOp.envLevel = 127;
        chan[ch].carOp.amPhase = 0;
        chan[ch].carOp.amAtten = 0;
        chan[ch].carOp.fmPhase = 0;
        chan[ch].carOp.fmRow = 0;
        chan[ch].carOp.fmShift = 0;

        chan[ch].kslIndex = 0;
    }
}

YamahaYm3812::YamahaYm3812() : curReg(0), statusVal(0), envCounter(0), audioChannels(2) {
    initTables();
    Reset();
}

YamahaYm3812::YamahaYm3812(bool stereo) : curReg(0), statusVal(0), envCounter(0), audioChannels(stereo?2:1) {
    initTables();
    Reset();
}

void YamahaYm3812::WriteReg(int reg, int val) {
    std::lock_guard<std::mutex> guard(regMutex);
    val &= 0xff;
    reg &= 0xff;

    if((reg >= 0x20 && reg < 0xa0) || reg >= 0xe0) { // Register writes that affect the slots
        const int8_t regChanNumber[32] {  0, 1, 2, 0, 1, 2, -1, -1,
                                     3, 4, 5, 3, 4, 5, -1, -1,
                                     6, 7, 8, 6, 7, 8, -1, -1,
                                    -1,-1,-1,-1,-1,-1, -1, -1};
        if(regChanNumber[reg & 0x1f] == -1) return;
        enum slotType {
            MOD, CAR, INV
        };

        const slotType regOpType[32]   { MOD, MOD, MOD, CAR, CAR, CAR, INV, INV,
                                   MOD, MOD, MOD, CAR, CAR, CAR, INV, INV,
                                   MOD, MOD, MOD, CAR, CAR, CAR, INV, INV,
                                   INV, INV, INV, INV, INV, INV, INV, INV };
        uint8_t chNum = regChanNumber[reg & 0x1f];
        slotType type = regOpType[reg & 0x1f];

        if(chNum == -1) return; // invalid channel number; ignore the write

        op_t& op = (type == slotType::MOD) ? chan[chNum].modOp : chan[chNum].carOp;
        
        switch(reg & 0xe0) {
            case 0x20:
                if(op.amActive != static_cast<bool>(val & 0x80)) { // If turning off, going to phase 0 effectively disables it.
                                                                   // If turning on, we want to start from the beginning of the phase.
                    op.amPhase = 0;
                    op.amAtten = 0;
                }
                op.amActive = static_cast<bool>(val & 0x80);

                if(op.vibActive != static_cast<bool>(val & 0x40)) { // Similar reasons to the AM one above
                    op.fmPhase = 0;
                }
                op.vibActive = static_cast<bool>(val & 0x40);

                op.sustain = static_cast<bool>(val & 0x20);
                op.keyScaleRate = static_cast<bool>(val & 0x10);
                {
                    int ksrNote = ((chan[chNum].fNum >> 9) + (chan[chNum].octave << 1));
                    op.ksrIndex = chan[chNum].carOp.keyScaleRate ? ksrNote : ksrNote>>2;
                }
                op.freqMult = (val & 0x0f);
                op.phaseInc = convertWavelength(((chan[chNum].fNum * multVal[op.freqMult]) << chan[chNum].octave));
                break;
            case 0x40:
                op.keyScaleLevel = (val >> 6);
                op.totalLevel = (val & 0x3f);
                op.kslAtten = ((1 << op.keyScaleLevel) >> 1) * 8 * kslTable[chan[chNum].kslIndex];
                break;
            case 0x60:
                op.attackRate = (val >> 4);
                op.decayRate = (val & 0x0f);
                break;
            case 0x80:
                op.sustainLevel = (val >> 4);
                op.releaseRate = (val & 0x0f);
                break;
            case 0xe0:
                op.waveform = (val & 0x03);
                break;
        }
    }
    else if(reg == 0xbd) { // tremolo, vibrato, rhythm
        bool deepTremolo = static_cast<bool>(val & 0x80);
        if(deepTremolo) tremoloMultiplier = 8;
        else tremoloMultiplier = 2;

        bool deepVibrato = static_cast<bool>(val & 0x40);
        if(deepVibrato) vibratoMultiplier = 2;
        else vibratoMultiplier = 1;

        rhythmMode = static_cast<bool>(val & 0x20);

        const int key[] = { 0x10, 0x01, 0x08, 0x04, 0x02};
        printf("APU::YM3812 Rhythm: %02x\n", rhythmMode);
        for(int i=0;i<5;i++) {
            bool newKeyOn = (val & key[i]);
            if(percChan[i].keyOn && !newKeyOn) { //keyoff event
                if(percChan[i].modOp) {
                    percChan[i].modOp->envPhase = release;
                }
                printf("APU::YM3812 perc   chan %d (%s) key-off\n", i, rhythmNames[i].c_str());
                percChan[i].carOp->envPhase = release;
            }
            else if(!percChan[i].keyOn && newKeyOn) { //keyon event
                if(percChan[i].modOp) {
                    if(percChan[i].modOp->envPhase == silent) {
                        percChan[i].modOp->envPhase = attack;
                    }
                    else {
                        percChan[i].modOp->envPhase = dampen;
                    }
                }
                if(percChan[i].carOp->envPhase != silent) {
                    percChan[i].carOp->envPhase = dampen;
                    printf("APU::YM3812 perc   chan %d (%s) dampen key-on\n", i, rhythmNames[i].c_str());
                }
                else {
                    percChan[i].carOp->envPhase = attack;
                    printf("APU::YM3812 perc   chan %d (%s) attack key-on\n", i, rhythmNames[i].c_str());
                }
            }
            percChan[i].keyOn = newKeyOn;
        }
    }
    else if(reg >= 0xa0 && reg < 0xe0) { // Register writes that affect the channels
        uint8_t chNum = reg & 0x0f;
        if(chNum > 8) return; // invalid channel
        switch(reg & 0xf0) {
            case 0xa0:
                chan[chNum].fNum &= 0x300;
                chan[chNum].fNum |= val;
                chan[chNum].modOp.phaseInc = convertWavelength(((chan[chNum].fNum * multVal[chan[chNum].modOp.freqMult]) << chan[chNum].octave));
                chan[chNum].carOp.phaseInc = convertWavelength(((chan[chNum].fNum * multVal[chan[chNum].carOp.freqMult]) << chan[chNum].octave));
                chan[chNum].kslIndex = ((chan[chNum].fNum >>6 ) + (chan[chNum].octave << 4));
                chan[chNum].modOp.kslAtten = ((1 << chan[chNum].modOp.keyScaleLevel) >> 1) * 8 * kslTable[chan[chNum].kslIndex];
                chan[chNum].carOp.kslAtten = ((1 << chan[chNum].carOp.keyScaleLevel) >> 1) * 8 * kslTable[chan[chNum].kslIndex];
                chan[chNum].carOp.fmRow = (chan[chNum].fNum >> 4) & 0b111000;
                chan[chNum].modOp.fmRow = (chan[chNum].fNum >> 4) & 0b111000;
                break;
            case 0xb0:
                chan[chNum].fNum &= 0xff;
                chan[chNum].fNum |= ((val&0x03)<<8);
                chan[chNum].octave = ((val>>2) & 0x07);
                chan[chNum].modOp.phaseInc = convertWavelength(((chan[chNum].fNum * multVal[chan[chNum].modOp.freqMult]) << chan[chNum].octave));
                chan[chNum].carOp.phaseInc = convertWavelength(((chan[chNum].fNum * multVal[chan[chNum].carOp.freqMult]) << chan[chNum].octave));
                chan[chNum].kslIndex = ((chan[chNum].fNum >>6 ) + (chan[chNum].octave << 4));
                {
                    int ksrNote = ((chan[chNum].fNum >> 9) + (chan[chNum].octave << 1));
                    chan[chNum].carOp.ksrIndex = chan[chNum].carOp.keyScaleRate ? ksrNote : ksrNote>>2;
                    chan[chNum].modOp.ksrIndex = chan[chNum].modOp.keyScaleRate ? ksrNote : ksrNote>>2;
                }
                chan[chNum].modOp.kslAtten = ((1 << chan[chNum].modOp.keyScaleLevel) >> 1) * 8 * kslTable[chan[chNum].kslIndex];
                chan[chNum].carOp.kslAtten = ((1 << chan[chNum].carOp.keyScaleLevel) >> 1) * 8 * kslTable[chan[chNum].kslIndex];
                chan[chNum].carOp.fmRow = (chan[chNum].fNum >> 4) & 0b111000;
                chan[chNum].modOp.fmRow = (chan[chNum].fNum >> 4) & 0b111000;
                {
                    bool newKeyOn = static_cast<bool>(val & 0x20);
                    if(chan[chNum].keyOn && !newKeyOn) { // keyOff event
                        printf("APU::YM3812 melody chan %d key-off\n", chNum);
                        chan[chNum].modOp.envPhase = adsrPhase::release;
                        chan[chNum].carOp.envPhase = adsrPhase::release;
                    }
                    else if(!chan[chNum].keyOn && newKeyOn) { // keyOn event
                        // Modulator Operator
                        if(chan[chNum].modOp.envPhase == adsrPhase::silent) {
                            chan[chNum].modOp.envPhase = adsrPhase::attack;
                        }
                        else {
                            chan[chNum].modOp.envPhase = adsrPhase::dampen;
                        }

                        // Carrier Operator
                        if(chan[chNum].carOp.envPhase == adsrPhase::silent) {
                            chan[chNum].carOp.envPhase = adsrPhase::attack;
                            printf("APU::YM3812 melody chan %d attack key-on\n", chNum);
                        }
                        else {
                            chan[chNum].carOp.envPhase = adsrPhase::dampen;
                            printf("APU::YM3812 melody chan %d dampen key-on\n", chNum);
                        }
                    }
                    chan[chNum].keyOn = newKeyOn;
                }
                break;
            case 0xc0:
                chan[chNum].conn = static_cast<connectionType>(val & 0x01);
                chan[chNum].feedbackLevel = ((val >> 1) & 0x07);
                break;
        }
    }
}

void YamahaYm3812::SetPanning(int channel, float left, float right) {}
void YamahaYm3812::Update(float* buffer, int sampleCnt) {}

void YamahaYm3812::Update(int16_t* buffer, int sampleCnt) {
    for(int i=0;i<sampleCnt*audioChannels;i+=audioChannels) {
        std::lock_guard<std::mutex> guard(regMutex);
        envCounter++;
        for(int ch = 0; ch < 9; ch++) {
            chan[ch].modOp.updateEnvelope(envCounter, tremoloMultiplier, vibratoMultiplier);
            chan[ch].carOp.updateEnvelope(envCounter, tremoloMultiplier, vibratoMultiplier);
        }

        int16_t sample = 0;

        int chanMax = (rhythmMode)?6:9;
        for(int ch=0;ch<chanMax;ch++) {
            op_t& modOp = chan[ch].modOp;
            op_t& carOp = chan[ch].carOp;

            if(carOp.envPhase != adsrPhase::silent) {

                modOp.phaseCnt += modOp.phaseInc;
                int feedback = (chan[ch].feedbackLevel) ? ((modOp.modFB1 + modOp.modFB2) >> (8 - chan[ch].feedbackLevel)) : 0;
                carOp.phaseCnt += carOp.phaseInc;

                int modSin = lookupSin((modOp.phaseCnt / 1024) - 1 +                              // phase
                                       modOp.fmShift +                                            // modification for vibrato
                                       (feedback),                                               // modification for feedback
                                       modOp.waveform);

                int modOut = lookupExp((modSin) +                                                // sine input
                                       modOp.amAtten +                                       // AM volume attenuation (tremolo)
                                       (modOp.envLevel * 0x10) +                                // Envelope
                                       modOp.kslAtten +                                         // Key Scale Level
                                       (modOp.totalLevel * 0x20));                         // Modulator volume
                modOp.modFB1 = modOp.modFB2;
                modOp.modFB2 = modOut;

                if(chan[ch].conn == connectionType::fm) {
                    int carSin = lookupSin((carOp.phaseCnt  / 1024) +                                 // phase
                                        carOp.fmShift +                                        // modification for vibrato
                                        (modOut),                                             // fm modulation
                                        carOp.waveform);

                    sample+=lookupExp((carSin) +                                                  // sine input
                                        carOp.amAtten +                                           // AM volume attenuation (tremolo)
                                        (carOp.envLevel * 0x10) +                                  // Envelope
                                        carOp.kslAtten +                                         // Key Scale Level
                                        (carOp.totalLevel * 0x20));                         // Channel volume
                }
                else {
                    int carSin = lookupSin((carOp.phaseCnt / 1024) +                                 // phase
                                        carOp.fmShift,                                            // modification for vibrato
                                        carOp.waveform);

                    sample+=lookupExp((carSin) +                                                  // sine input
                                        carOp.amAtten +                                           // AM volume attenuation (tremolo)
                                        (carOp.envLevel * 0x10) +                                  // Envelope
                                        carOp.kslAtten +                                         // Key Scale Level
                                        (carOp.totalLevel * 0x20));                                // Channel volume
                    sample += modOut;
                }
            }
        }
        if(rhythmMode) { // TODO: handle the 5 rhythm instruments (correctly)
            for(int ch = 0; ch < 5; ch++) {
                op_t* modOp = percChan[ch].modOp;
                op_t* carOp = percChan[ch].carOp;

                if(percChan[ch].carOp->envPhase != silent) {
                    int modOut = 0;
                    carOp->phaseCnt += carOp->phaseInc;
                    if(modOp) {
                        int feedback = (percChan[ch].chan->feedbackLevel) ? ((modOp->modFB1 + modOp->modFB2) >> (8 - percChan[ch].chan->feedbackLevel)) : 0;
                        modOp->phaseCnt += modOp->phaseInc;
                        int modSin = lookupSin((modOp->phaseCnt / 1024) - 1 +                         // phase
                                               modOp->fmShift +                                      // modification for vibrato
                                               (feedback),                                           // modification for feedback
                                               modOp->waveform);

                        modOut = lookupExp((modSin) +                                                // sine input
                                           modOp->amAtten +                                          // AM volume attenuation (tremolo)
                                           (modOp->envLevel * 0x10) +                                // Envelope
                                           modOp->kslAtten +
                                           (modOp->totalLevel * 0x20));                              // Modulator volume
                        modOp->modFB1 = modOp->modFB2;
                        modOp->modFB2 = modOut;

                    }
                    int carSin = lookupSin((carOp->phaseCnt / 1024) +                                 // phase
                                           (2 * modOut) +                                            // fm modulation
                                           carOp->fmShift,                                        // modification for vibrato
                                           carOp->waveform);

                    sample+=    lookupExp((carSin) +                                                 // sine input
                                         carOp->amAtten +                                            // AM volume attenuation (tremolo)
                                         (carOp->envLevel * 0x10) +                                  // Envelope
                                         carOp->kslAtten +
                                         (percChan[ch].carOp->totalLevel * 0x20));                   // Channel volume
                }
            }
        }
        buffer[i] = sample << 2;
        if(audioChannels == 2) {
            buffer[i+1] = buffer[i];
        }
    }
}

void YamahaYm3812::initTables() {
    for (int i = 0; i < 256; ++i) {
        logsinTable[i] = round(-log2(sin((double(i) + 0.5) * M_PI_2 / 256.0)) * 256.0);
        logsinTable[511 - i] = logsinTable[i];
        logsinTable[512 + i] = 0x8000 | logsinTable[i];
        logsinTable[1023 - i] = logsinTable[512+i];
        // expTable[i] = round(exp2(double(i) / 256.0) * 1024.0) - 1024.0;
        expTable[255-i] = int(round(exp2(double(i) / 256.0) * 1024.0)) << 1;
    }
    for(int i = 0; i < 1024; ++i) {
        bool sign = i & 512;
        bool mirror = i & 256;
        for(int wf=0;wf<4;wf++) {
            switch(wf) {
                case 0: break; // full sine wave; already covered.
                case 1: // half sine wave (positive half, set negative half to 0)
                    if(!sign) logsinTable[wf*1024+i] = logsinTable[i];
                    else      logsinTable[wf*1024+i] = 0x8000; // constant for -0 value
                    break;
                case 2: // rectified sine wave (double-bumps)
                    logsinTable[wf*1024+i] = logsinTable[i & 511];
                    break;
                case 3: // pseudo-saw (only the 1st+3rd quarters of the wave is defined, and are both positive)
                    if(!mirror) logsinTable[wf*1024+i] = logsinTable[i&255];
                    else        logsinTable[wf*1024+i] = 0x8000;
            }
        }

    }
}

int YamahaYm3812::lookupSin(int val, int wf) {
    val &= 1023;
    return logsinTable[1024 * wf + val];
}

int YamahaYm3812::lookupExp(int val) {
    bool sign = val & 0x8000;
    int t = expTable[(val & 255)];
    int result = (t >> ((val & 0x7F00) >> 8)) >> 2;
    if (sign) result = ~result;
    return result;
/*
    bool sign = val & 0x8000;
    int t = (expTable[(val & 255) ^ 255] | 1024) << 1;
    int result = (t >> ((val & 0x7F00) >> 8)) >> 2;
    if (sign) result = ~result;
    return result;
*/
}

int YamahaYm3812::convertWavelength(int wavelength) {
    return (wavelength * OPL_SAMPLE_RATE) / NATIVE_SAMPLE_RATE;
}

void YamahaYm3812::op_t::updateEnvelope(unsigned int counter, unsigned int tremoloMultiplier, unsigned int vibratoMultiplier) {
    if(amActive && counter % amPhaseSampleLength == 0) {
        amPhase++;
        if(amPhase == amTable.size()) {
            amPhase = 0;
        }
        amAtten = amTable[amPhase] * tremoloMultiplier;
    }

    if(vibActive && counter % fmPhaseSampleLength == 0) {
        fmPhase++;
        if(fmPhase == 8) {
            fmPhase = 0;
        }
        fmShift = fmTable[fmRow + fmPhase] * vibratoMultiplier;
    }
    
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
    else if(envLevel > 127) envLevel = 127; //assume it just overflowed the 7-bit value
}

int YamahaYm3812::op_t::lfsrStepGalois() {
    bool output = galoisState & 1;
    galoisState >>= 1;
    if (output) galoisState ^= 0x400181;
    return output;
} 

const std::array<int,210> YamahaYm3812::amTable {
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4,
    4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 9, 9, 9, 
    9,10,10,10,10,11,11,11,11,12,12,12,12,13,13,13,13,14,14,14,14,
    15,15,15,15,16,16,16,16,17,17,17,17,18,18,18,18,19,19,19,19,20,
    20,20,20,21,21,21,21,22,22,22,22,23,23,23,23,24,24,24,24,25,25,
    25,25,26,26,26,25,25,25,25,24,24,24,24,23,23,23,23,22,22,22,22,
    21,21,21,21,20,20,20,20,19,19,19,19,18,18,18,18,17,17,17,17,16,
    16,16,16,15,15,15,15,14,14,14,14,13,13,13,13,12,12,12,12,11,11,
    11,11,10,10,10,10, 9, 9, 9, 9, 8, 8, 8, 8, 7, 7, 7, 7, 6, 6, 6,
    6, 5, 5, 5, 5, 4, 4, 4, 4, 3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1
    };

const std::array<int,64> YamahaYm3812::fmTable {{
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0,-1, 0,
    0, 1, 2, 1, 0,-1,-2,-1,
    0, 1, 3, 1, 0,-1,-3,-1,
    0, 2, 4, 2, 0,-2,-4,-2,
    0, 2, 5, 2, 0,-2,-5,-2,
    0, 3, 6, 3, 0,-3,-6,-3,
    0, 3, 7, 3, 0,-3,-7,-3
}};

const std::array<int,128> YamahaYm3812::kslTable {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 4, 5, 6, 7, 8,
    0, 0, 0, 0, 0, 3, 5, 7, 8,10,11,12,13,14,15,16,
    0, 0, 0, 5, 8,11,13,15,16,18,19,20,21,22,23,24,
    0, 0, 8,13,16,19,21,23,24,26,27,28,29,30,31,32,
    0, 8,16,21,24,27,29,31,32,34,35,36,37,38,39,40,
    0,16,24,29,32,35,37,39,40,42,43,44,45,46,47,48,
    0,24,32,37,40,43,45,47,48,50,51,52,53,54,55,56
    };

const std::array<int,64> YamahaYm3812::attackTable {
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

const std::array<int,64> YamahaYm3812::decayTable {
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

std::array<int,1024*4> YamahaYm3812::logsinTable;
std::array<int,256> YamahaYm3812::expTable;
const std::array<uint8_t,16> YamahaYm3812::multVal {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};
const int YamahaYm3812::NATIVE_SAMPLE_RATE;
const std::array<std::string,5> YamahaYm3812::rhythmNames {"Bass Drum", "High Hat", 
                                               "Snare Drum", "Tom-tom", "Top Cymbal"};
