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
    amPhase = 0;
    fmPhase = 0;
    for(auto& ch: chan) {
        ch.modOp.phaseInc = 0;
        ch.modOp.phaseCnt = 0;
        ch.modOp.envPhase = adsrPhase::silent;
        ch.modOp.envLevel = 255;
        ch.modOp.amAtten = 0;
        ch.modOp.fmShift = 0;
        ch.modOp.modFB1 = 0;
        ch.modOp.modFB2 = 0;


        ch.carOp.phaseInc = 0;
        ch.carOp.phaseCnt = 0;
        ch.carOp.envPhase = adsrPhase::silent;
        ch.carOp.envLevel = 255;
        ch.carOp.amAtten = 0;
        ch.carOp.fmShift = 0;

        ch.kslIndex = 0;
        ch.fmRow = 0;
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
                op.amActive = static_cast<bool>(val & 0x80);
                if(!op.amActive) op.amAtten = 0;
                else op.amAtten = amTable[amPhase] * tremoloMultiplier;
                op.vibActive = static_cast<bool>(val & 0x40);
                if(!op.vibActive) op.fmShift = 0;
                else op.fmShift = fmTable[chan[chNum].fmRow + fmPhase] * vibratoMultiplier;

                op.sustain = static_cast<bool>(val & 0x20);
                op.keyScaleRate = static_cast<bool>(val & 0x10);
                {
                    int ksrNote = ((chan[chNum].fNum >> 9) + (chan[chNum].octave << 1));
                    op.ksrIndex = op.keyScaleRate ? ksrNote : ksrNote>>2;
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
                percChan[i].carOp->envPhase = release;
                printf("APU::YM3812 perc chan %d (%s) key-off\n", i, rhythmNames[i].c_str());
            }
            else if(newKeyOn) { //keyon event
                if(!percChan[i].keyOn) {
                    if(percChan[i].modOp) percChan[i].modOp->phaseCnt = 0;
                    percChan[i].carOp->phaseCnt = 0;
                }
                if(percChan[i].modOp) {
                    percChan[i].modOp->envPhase = attack;
                }
                percChan[i].carOp->envPhase = attack;
                printf("APU::YM3812 perc   chan %d (%s) attack key-on\n", i, rhythmNames[i].c_str());
            }
            percChan[i].keyOn = newKeyOn;
        }
    }
    else if(reg >= 0xa0 && reg < 0xe0) { // Register writes that affect the channels
        uint8_t chNum = reg & 0x0f;
        if(chNum > 8) return; // invalid channel
        YamahaYm3812::chan_t& channel = chan[chNum];
        YamahaYm3812::op_t& modOp = channel.modOp;
        YamahaYm3812::op_t& carOp = channel.carOp;
        switch(reg & 0xf0) {
            case 0xa0:
                channel.fNum &= 0x300;
                channel.fNum |= val;
                modOp.phaseInc = convertWavelength(((channel.fNum * multVal[modOp.freqMult]) << channel.octave));
                carOp.phaseInc = convertWavelength(((channel.fNum * multVal[carOp.freqMult]) << channel.octave));
                channel.kslIndex = ((channel.fNum >>6 ) + (channel.octave << 4));
                modOp.kslAtten = ((1 << modOp.keyScaleLevel) >> 1) * 8 * kslTable[channel.kslIndex];
                carOp.kslAtten = ((1 << carOp.keyScaleLevel) >> 1) * 8 * kslTable[channel.kslIndex];
                channel.fmRow = (channel.fNum >> 4) & 0b111000;
                break;
            case 0xb0:
                channel.fNum &= 0xff;
                channel.fNum |= ((val&0x03)<<8);
                channel.octave = ((val>>2) & 0x07);
                modOp.phaseInc = convertWavelength(((channel.fNum * multVal[modOp.freqMult]) << channel.octave));
                carOp.phaseInc = convertWavelength(((channel.fNum * multVal[carOp.freqMult]) << channel.octave));
                channel.kslIndex = ((channel.fNum >>6 ) + (channel.octave << 4));
                {
                    int ksrNote = ((channel.fNum >> 9) + (channel.octave << 1));
                    carOp.ksrIndex = carOp.keyScaleRate ? ksrNote : ksrNote>>2;
                    modOp.ksrIndex = modOp.keyScaleRate ? ksrNote : ksrNote>>2;
                }
                modOp.kslAtten = ((1 << modOp.keyScaleLevel) >> 1) * 8 * kslTable[channel.kslIndex];
                carOp.kslAtten = ((1 << carOp.keyScaleLevel) >> 1) * 8 * kslTable[channel.kslIndex];
                channel.fmRow = (channel.fNum >> 4) & 0b111000;
                {
                    bool newKeyOn = static_cast<bool>(val & 0x20);
                    if(channel.keyOn && !newKeyOn) { // keyOff event
                        printf("APU::YM3812 melody chan %d key-off\n", chNum);
                        // std::cout<<"modOp and carOp phases set to \"release\" by keyoff\n";
                        modOp.envPhase = adsrPhase::release;
                        modOp.envAccum = 0;
                        carOp.envPhase = adsrPhase::release;
                        carOp.envAccum = 0;
                    }
                    else if(newKeyOn) { // keyOn event
                        if(!channel.keyOn) { // Key wasn't pressed before 
                            modOp.phaseCnt = 0;
                            carOp.phaseCnt = 0;
                            printf("APU::YM3812 melody chan %d attack key-off->on\n", chNum);
                        }
                        else {
                            printf("APU::YM3812 melody chan %d attack key-on->on\n", chNum);
                        }

                        modOp.envAccum = 0;
                        carOp.envAccum = 0;
                        if(modOp.attackRate == 15) {
                            // std::cout<<"Max vol, set to \"decay\", because attack==15\n";
                            modOp.envLevel = 0;
                            modOp.envPhase = adsrPhase::decay;

                        }
                        else {
                            // std::cout<<"Set to \"attack\" due to key-on\n";
                            modOp.envPhase = adsrPhase::attack;
                        }

                        if(carOp.attackRate == 15) {
                            // std::cout<<"Max vol, set to \"decay\", because attack==15\n";
                            carOp.envLevel = 0;
                            carOp.envPhase = adsrPhase::decay;
                        }
                        else {
                            // std::cout<<"Set to \"attack\" due to key-on\n";
                            carOp.envPhase = adsrPhase::attack;
                        }
                    }
                    channel.keyOn = newKeyOn;
                }
                break;
            case 0xc0:
                carOp.conn = static_cast<op_t::connectionType>(val & 0x01);
                modOp.feedbackLevel = ((val >> 1) & 0x07);
                break;
        }
    }
}

void YamahaYm3812::SetPanning(int channel, float left, float right) {}
void YamahaYm3812::Update(float* buffer, int sampleCnt) {}

void YamahaYm3812::Update(int16_t* buffer, int sampleCnt) {
    std::lock_guard<std::mutex> guard(regMutex);
    for(int i=0;i<sampleCnt*audioChannels;i+=audioChannels) {
        envCounter++;
        updatePhases();
        updateEnvelopes();

        int16_t sample = 0;

        int chanMax = (rhythmMode)?6:9;
        for(int ch=0;ch<chanMax;ch++) {
            op_t& modOp = chan[ch].modOp;
            op_t& carOp = chan[ch].carOp;

            if(carOp.envPhase != adsrPhase::silent) {

                modOp.phaseCnt += modOp.phaseInc;
                int feedback = (modOp.feedbackLevel) ? ((modOp.modFB1 + modOp.modFB2) >> (8 - modOp.feedbackLevel)) : 0;
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

                if(carOp.conn == op_t::connectionType::fm) {
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
                        int feedback = (modOp->feedbackLevel) ? ((modOp->modFB1 + modOp->modFB2) >> (8 - modOp->feedbackLevel)) : 0;
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
}

int YamahaYm3812::convertWavelength(int wavelength) {
    return (static_cast<int64_t>(wavelength) * static_cast<int64_t>(NATIVE_SAMPLE_RATE)) / static_cast<int64_t>(OPL_SAMPLE_RATE);
}

void YamahaYm3812::updatePhases() {
    if(envCounter % amPhaseSampleLength == 0) {
        amPhase++;
        amPhase %= amTable.size();
        int amAtten = amTable[amPhase] * tremoloMultiplier;
        for(int ch = 0; ch < 9; ch++) {
            if(chan[ch].modOp.amActive) chan[ch].modOp.amAtten = amAtten;
            if(chan[ch].carOp.amActive) chan[ch].carOp.amAtten = amAtten;
        }
    }

    if(envCounter % fmPhaseSampleLength == 0) {
        fmPhase++;
        fmPhase &= 7;
        for(auto& ch: chan) {
            if(ch.modOp.vibActive) ch.modOp.fmShift = fmTable[ch.fmRow + fmPhase] * vibratoMultiplier;
            if(ch.carOp.vibActive) ch.carOp.fmShift = fmTable[ch.fmRow + fmPhase] * vibratoMultiplier;
        }
    }

}

void YamahaYm3812::updateEnvelopes() {
    for(auto& ch: chan) {
        if(ch.modOp.envPhase != adsrPhase::silent) {
            // std::cout<<"Modulator: ";
            ch.modOp.updateEnvelope(envCounter);
        }
        if(ch.carOp.envPhase != adsrPhase::silent) {
            // std::cout<<"Carrier: ";
            ch.carOp.updateEnvelope(envCounter);
        }
    }
}

void YamahaYm3812::op_t::updateEnvelope(unsigned int counter) {
    if(envPhase != adsrPhase::silent) {
        // std::cout<<adsrPhaseNames[envPhase]<<": envLevel: "<<std::dec<<envLevel<<" envAccum: "<<envAccum<<'\n';
    }

    if(envPhase == adsrPhase::attack && (envLevel <= 0)) { // Transition from attack to decay when at max volume
        // std::cout<<"Set to \"decay\" because previous was attack and envLevel: "<<envLevel<<'\n';
        envPhase = adsrPhase::decay;
        envAccum = 0;
        envLevel = 0;
    }
    else if(envPhase == adsrPhase::decay && envLevel >= ((sustainLevel == 15)? 247 : (sustainLevel * 8))) { // Transition from decay to either sustain or release when hit sustain level
        if(sustain) {
            // std::cout<<"Set to sustain, because previous was decay, and we're at level "<<envLevel<<" (sustain level is "<<((sustainLevel == 15)? 247 : (sustainLevel * 8))<<")\n";
            envPhase = adsrPhase::sustain;
            envLevel = ((sustainLevel == 15)? 247 : (sustainLevel * 8));
            envAccum = 0;
        }
        else {
            // std::cout<<"Set to release, because prev was decay and we're at level "<<envLevel<<" (sustain level is "<<((sustainLevel == 15)? 247 : (sustainLevel * 8))<<")\n";
            envPhase = adsrPhase::release;
            envAccum = 0;
        }
    }
    else if(envPhase == adsrPhase::release && envLevel >= 247) {
        // std::cout<<"Set to silent, because prev was release, and we've hit envLevel: "<<envLevel<<'\n';
        envPhase = adsrPhase::silent;
        envAccum = 0;
        envLevel = 255;
    }

    int activeRate = 0;
    bool attack = false;
    switch(envPhase) {
        case adsrPhase::silent: activeRate = 0; break;
        case adsrPhase::attack: activeRate = attackRate; attack = true; break;
        case adsrPhase::decay:  activeRate = decayRate; break;
        case adsrPhase::sustain: activeRate = 0; break;
        case adsrPhase::release: activeRate = releaseRate; break;
        default: activeRate = 0;
            std::cout<<"Unhandled envPhase: "<<envPhase<<"\n";
            break;
    }

    if(activeRate != 0 && (!attack || activeRate != 15)) { // Skips the rate==0 row, which is invalid, and silent+sustain states, where envLevel doesn't change
        // std::cout<<"envAccum is "<<envAccum<<", and we add "<<envAccumRate<<" to it\n";
        envAccum += envAccumRate;
        int targetValue = 0;
        int levelsToChange = 0;
        if(attack && activeRate != 15) {
            int index = std::min(63, activeRate * 4 + ksrIndex);
            targetValue = attackTable[index];
            levelsToChange = envAccum / targetValue;
            envAccum = envAccum % targetValue;
            envLevel -= levelsToChange;
            // std::cout<<"Attack, activerate: "<<activeRate<<" ksr: "<<ksrIndex<<" targetValue: "<<targetValue<<" levelsToChange: "<<levelsToChange<<" envAccum: "<<envAccum<<'\n';
        }
        else if(!attack) {
            int index = std::min(63, activeRate * 4 + ksrIndex);
            targetValue = decayTable[index];
            levelsToChange = envAccum / targetValue;
            envAccum = envAccum % targetValue;
            envLevel += levelsToChange;
            // std::cout<<adsrPhaseNames[envPhase]<<", activerate: "<<activeRate<<" ksr: "<<ksrIndex<<" targetValue: "<<targetValue<<" levelsToChange: "<<levelsToChange<<" envAccum: "<<envAccum<<'\n';
        }
    }

    if(envLevel < 0) envLevel = 0; // assume wrap-around
    else if(envLevel > 255) envLevel = 255; //assume it just overflowed the 8-bit value
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

const std::array<float,4> YamahaYm3812::attackTableBase {
    2826.24, 2252.8, 1884.16, 1597.44
};

const std::array<float, 4> YamahaYm3812::decayTableBase {
    39280.64, 31416.32, 26173.44, 22446.08
};

// These are measured in microseconds per change of level
std::array<int,64> YamahaYm3812::attackTable {
    0, 0, 0, 0, // Skip level 0, because 0 means "no attack"
    11083, 8835, 7389, 6264,
    5542, 4417, 3694, 3132,
    2771, 2209, 1847, 1566,
    1385, 1104, 924, 783,
    693, 552, 462, 392,
    346, 276, 231, 196,
    173, 138, 115, 98,
    87, 69, 58, 49,
    43, 35, 29, 24,
    22, 17, 14, 12,
    11, 9, 7, 6,
    5, 4, 4, 3,
    3, 2, 2, 2,
    1, 1, 1, 1,
    1, 1, 1, 1
};

// These are measured in microseconds per change of level
std::array<int,64> YamahaYm3812::decayTable {
    0, 0, 0, 0, // Skip level 0, because 0 means "no decay"
    154042, 123201, 102641, 88024,
    77021, 61601, 51320, 44012,
    38510, 30800, 25660, 22006,
    19255, 15400, 12830, 11003,
    9628, 7700, 6415, 5501,
    4814, 3850, 3208, 2751,
    2407, 1925, 1604, 1375,
    1203, 963, 802, 688,
    602, 481, 401, 344,
    301, 241, 200, 172,
    150, 120, 100, 86,
    75, 60, 50, 43,
    38, 30, 25, 21,
    19, 15, 13, 11,
    9, 9, 9, 9
};

std::array<int,1024*4> YamahaYm3812::logsinTable;
std::array<int,256> YamahaYm3812::expTable;
const std::array<uint8_t,16> YamahaYm3812::multVal {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};
const int YamahaYm3812::NATIVE_SAMPLE_RATE;
const std::array<std::string,5> YamahaYm3812::rhythmNames {"Bass Drum", "High Hat", 
                                               "Snare Drum", "Tom-tom", "Top Cymbal"};

const std::array<std::string,5> YamahaYm3812::adsrPhaseNames {
    "silent",
    "attack",
    "decay",
    "sustain",
    "release"
};