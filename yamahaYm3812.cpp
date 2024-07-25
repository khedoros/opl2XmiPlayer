#include<iostream>
#include<cmath>
#include<memory>
#include "yamahaYm3812.h"
#include "util.h"

OPLEmul *YamahaYm3812Create(bool stereo)
{
    /* emulator create */
    return new YamahaYm3812;
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
        chan[ch].modOp.vibPhase = 0;
        chan[ch].modOp.modFB1 = 0;
        chan[ch].modOp.modFB2 = 0;

        chan[ch].carOp.phaseInc = 0;
        chan[ch].carOp.phaseCnt = 0;
        chan[ch].carOp.envPhase = adsrPhase::silent;
        chan[ch].carOp.envLevel = 127;
        chan[ch].carOp.amPhase = 0;
        chan[ch].carOp.vibPhase = 0;
    }
}

YamahaYm3812::YamahaYm3812() : curReg(0), statusVal(0), envCounter(0) {
    initTables();
    Reset();
}

void YamahaYm3812::WriteReg(int reg, int val) {
    val &= 0xff;
    reg &= 0xff;

    if((reg >= 0x20 && reg < 0xa0) || reg >= 0xe0) { // Register writes that affect the slots
        const int8_t regChanNumber[32] {  0, 1, 2, 0, 1, 2, -1, -1,
                                     3, 4, 5, 3, 4, 5, -1, -1,
                                     6, 7, 8, 6, 7, 8, -1, -1,
                                    -1,-1,-1,-1,-1,-1, -1, -1};
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
                op.amActive = static_cast<bool>(val & 0x80);
                op.vibActive = static_cast<bool>(val & 0x40);
                op.sustain = static_cast<bool>(val & 0x20);
                op.keyScaleRate = static_cast<bool>(val & 0x10);
                op.freqMult = (val & 0x0f);
                op.phaseInc = convertWavelength(((chan[chNum].fNum * multVal[op.freqMult]) << chan[chNum].octave));
                break;
            case 0x40:
                op.keyScaleLevel = (val >> 6);
                op.totalLevel = (val & 0x3f);
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
        deepTremolo = static_cast<bool>(val & 0x80);
        deepVibrato = static_cast<bool>(val & 0x40);
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
                break;
            case 0xb0:
                chan[chNum].fNum &= 0xff;
                chan[chNum].fNum |= ((val&0x03)<<8);
                chan[chNum].octave = ((val>>2) & 0x07);
                chan[chNum].modOp.phaseInc = convertWavelength(((chan[chNum].fNum * multVal[chan[chNum].modOp.freqMult]) << chan[chNum].octave));
                chan[chNum].carOp.phaseInc = convertWavelength(((chan[chNum].fNum * multVal[chan[chNum].carOp.freqMult]) << chan[chNum].octave));
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
    for(int i=0;i<sampleCnt*2;i+=2) {
        envCounter+=2;

        int chanMax = (rhythmMode)?6:9;
        for(int ch=0;ch<chanMax;ch++) {
            op_t& modOp = chan[ch].modOp;
            op_t& carOp = chan[ch].carOp;
            modOp.updateEnvelope(envCounter);
            carOp.updateEnvelope(envCounter);

            if(carOp.envPhase != adsrPhase::silent) {
                modOp.phaseCnt += modOp.phaseInc;
                int feedback = (chan[ch].feedbackLevel) ? ((modOp.modFB1 + modOp.modFB2) >> (8 - chan[ch].feedbackLevel)) : 0;
                carOp.phaseCnt += carOp.phaseInc;

                int modSin = lookupSin((modOp.phaseCnt / 1024) - 1 +                              // phase
                                       (modOp.vibPhase) +                                       // modification for vibrato
                                       (feedback),                                               // modification for feedback
                                       modOp.waveform);

                int modOut = lookupExp((modSin) +                                                // sine input
                                       (modOp.amPhase * 0x10) +                                 // AM volume attenuation (tremolo)
                                       (modOp.envLevel * 0x10) +                                // Envelope
                                       //TODO: KSL
                                       (modOp.totalLevel * 0x20));                         // Modulator volume
                modOp.modFB1 = modOp.modFB2;
                modOp.modFB2 = modOut;

                if(chan[ch].conn == connectionType::fm) {
                    int carSin = lookupSin((carOp.phaseCnt  / 1024) +                                 // phase
                                        (carOp.vibPhase) +                                    // modification for vibrato
                                        (modOut),                                             // fm modulation
                                        carOp.waveform);

                    buffer[i]+=lookupExp((carSin) +                                                  // sine input
                                        (carOp.amPhase * 0x10) +                                   // AM volume attenuation (tremolo)
                                        (carOp.envLevel * 0x10) +                                  // Envelope
                                        //TODO: KSL
                                        (carOp.totalLevel * 0x20));                         // Channel volume
                }
                else {
                    int carSin = lookupSin((carOp.phaseCnt / 1024) +                                 // phase
                                        (carOp.vibPhase),                                        // modification for vibrato
                                        carOp.waveform);

                    buffer[i]+=lookupExp((carSin) +                                                  // sine input
                                        (carOp.amPhase * 0x10) +                                   // AM volume attenuation (tremolo)
                                        (carOp.envLevel * 0x10) +                                  // Envelope
                                        //TODO: KSL
                                        (carOp.totalLevel * 0x20));                                // Channel volume
                    buffer[i] += modOut;
                }
            }
        }
        buffer[i+1] = buffer[i];
        if(rhythmMode) { // TODO: handle the 5 rhythm instruments (correctly)
            for(int ch = 0; ch < 5; ch++) {
                op_t* modOp = percChan[ch].modOp;
                op_t* carOp = percChan[ch].carOp;
                if(modOp) {
                    modOp->updateEnvelope(envCounter);
                }
                carOp->updateEnvelope(envCounter);
                if(percChan[ch].carOp->envPhase != silent) {
                    int modOut = 0;
                    carOp->phaseCnt += carOp->phaseInc;
                    if(modOp) {
                        int feedback = (percChan[ch].chan->feedbackLevel) ? ((modOp->modFB1 + modOp->modFB2) >> (8 - percChan[ch].chan->feedbackLevel)) : 0;
                        modOp->phaseCnt += modOp->phaseInc;
                        int modSin = lookupSin((modOp->phaseCnt / 1024) - 1 +                         // phase
                                               (modOp->vibPhase) +                                   // modification for vibrato
                                               (feedback),                                           // modification for feedback
                                               modOp->waveform);

                        modOut = lookupExp((modSin) +                                                // sine input
                                           (modOp->amPhase * 0x10) +                                 // AM volume attenuation (tremolo)
                                           (modOp->envLevel * 0x10) +                                // Envelope
                                           //TODO: KSL
                                           (modOp->totalLevel * 0x20));                              // Modulator volume
                        modOp->modFB1 = modOp->modFB2;
                        modOp->modFB2 = modOut;

                    }
                    int carSin = lookupSin((carOp->phaseCnt / 1024) +                                 // phase
                                           (2 * modOut) +                                            // fm modulation
                                           (carOp->vibPhase),                                        // modification for vibrato
                                           carOp->waveform);

                    buffer[i]+= lookupExp((carSin) +                                                 // sine input
                                         (carOp->amPhase * 0x10) +                                   // AM volume attenuation (tremolo)
                                         (carOp->envLevel * 0x10) +                                  // Envelope
                                         //TODO: KSL
                                         (percChan[ch].carOp->totalLevel * 0x20));                   // Channel volume
                }
            }
            buffer[i+1] = buffer[i];
        }
    }
}

void YamahaYm3812::initTables() {
    for (int i = 0; i < 256; ++i) {
        logsinTable[i] = round(-log2(sin((double(i) + 0.5) * M_PI_2 / 256.0)) * 256.0);
        expTable[i] = round(exp2(double(i) / 256.0) * 1024.0) - 1024.0;
    }
}

int YamahaYm3812::lookupSin(int val, int wf) {
    bool sign   = val & 512;
    bool mirror = val & 256;
    val &= 255;
    int result = logsinTable[mirror ? val ^ 255 : val];
    switch(wf) {
        case 0: // full sine wave
            if(sign) result |= 0x8000;
            break;
        case 1: // half sine wave (positive half, set negative half to 0)
            if(sign) {
                result = 0;
                result |= 0x8000; // vague memory that it uses -0, not +0
            }
            break;
        case 2: // rectified sine wave (double-bumps)
            // Just don't set the sign negative for anything
            break;
        case 3: // rectified sine wave, rises only
            if(mirror) {
                result = 0;
                result |= 0x8000; // vague memory that it uses -0, not +0
            }
            break;
    }
    return result;
}

int YamahaYm3812::lookupExp(int val) {
    bool sign = val & 0x8000;
    // int t = (expTable[(val & 255) ^ 255] << 1) | 0x800;
    int t = (expTable[(val & 255) ^ 255] | 1024) << 1;
    int result = (t >> ((val & 0x7F00) >> 8)) >> 2;
    if (sign) result = ~result;
    // std::cout<<"EXPOUT "<<result<<'\n';
    return result;
}

int YamahaYm3812::convertWavelength(int wavelength) {
    return (wavelength * OPL_SAMPLE_RATE) / NATIVE_SAMPLE_RATE;
}

int YamahaYm3812::logsinTable[256];
int YamahaYm3812::expTable[256];
const int YamahaYm3812::NATIVE_SAMPLE_RATE = 49716;

const std::string YamahaYm3812::rhythmNames[] {"Bass Drum", "High Hat", 
                                               "Snare Drum", "Tom-tom", "Top Cymbal"};

void YamahaYm3812::op_t::updateEnvelope(unsigned int counter) {
    if(envPhase == adsrPhase::dampen && envLevel >= 123) { // Dampened previous note, start attack of new note
        envPhase = adsrPhase::attack;
    }
    else if(envPhase == adsrPhase::attack && (envLevel > 130)) { 
        envPhase = adsrPhase::decay;
    }
    else if(envPhase == adsrPhase::decay && envLevel >= sustainLevel * 8) {
            envPhase = adsrPhase::sustainRelease;
            envLevel = sustainLevel * 8;
    }
    else if((envPhase == adsrPhase::sustainRelease || envPhase == adsrPhase::release) && envLevel >= 123) {
        envPhase = adsrPhase::silent;
    }

    int activeRate = 0;
    switch(envPhase) {
        case adsrPhase::silent: activeRate = 0; break;
        case adsrPhase::dampen: activeRate = 12; break;
        case adsrPhase::attack: activeRate = -attackRate; break;
        case adsrPhase::decay:  activeRate = decayRate; break;
        case adsrPhase::sustain: activeRate = 0; break;
        case adsrPhase::sustainRelease: activeRate = releaseRate; break;
        case adsrPhase::release:
            if(releaseSustain) activeRate = 5;
            else        activeRate = 7;
            break;
        default: activeRate = 0;
            std::cout<<"Unhandled envPhase: "<<envPhase<<"\n";
            break;
    }

    int changeAmount = 1;
    if(envPhase == adsrPhase::attack) {
        if(activeRate == -15) {
            envLevel = 0;
        }
        else {
            envLevel += activeRate;
        }
    }
    else if(activeRate == 0) changeAmount = 0;
    else if(activeRate == 15) {
        changeAmount = 2;
    }

              //    0      1      2     3     4     5     6     7    8   9   a   b   c  d  e  f
    int checks[] {65536, 32768, 16384, 8192, 4096, 2048, 1024, 236, 128, 64, 32, 16, 8, 4, 2, 1};

    if(!(counter & (checks[activeRate] - 1))) {
        if(envPhase != adsrPhase::attack) {
            envLevel += changeAmount;
        }
    }

    //if(envLevel > 127) std::cout<<std::dec<<"Env at "<<envLevel<<"\n";
    if(envLevel > 130) envLevel = 0; // assume wrap-around
    else if(envLevel > 127) envLevel = 127; //assume it just overflowed the 7-bit value
}

int YamahaYm3812::op_t::lfsrStepGalois() {
    bool output = galoisState & 1;
    galoisState >>= 1;
    if (output) galoisState ^= 0x400181;
    return output;
} 