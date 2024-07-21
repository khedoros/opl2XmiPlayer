#pragma once
#include<utility>
#include<string>
#include "opl.h"

OPLEmul* YamahaYm3812Create(bool stereo);

class YamahaYm3812: public OPLEmul {
public:
    YamahaYm3812();

    virtual void Reset() override;
    virtual void WriteReg(int reg, int v) override;
    virtual void Update(float *buffer, int length) override;
    virtual void Update(int16_t *buffer, int length) override;
    virtual void SetPanning(int channel, float left, float right) override;

private:
    class inst_t;
    uint8_t curReg;
    uint8_t statusVal;
    float leftPan;
    float rightPan;

    bool deepTremolo;
    bool deepVibrato;

    unsigned int envCounter; // Global counter for advancing envelope state

    enum adsrPhase {
        silent,         //Note hit envelope==48dB
        dampen,         //End of previous note, behaves like a base decay rate of 12, adjusted by rate_key_scale
        attack,         //New note rising after key-on
        decay,          //Initial fade to sustain level after reaching max volume
        sustain,        //Level to hold at until key-off, or level at which to transition from decay to sustainRelease phase
        sustainRelease, //sustain for percussive notes
        release         //key-off
    };

    struct op_t {
        void updateEnvelope(unsigned int envCounter);
        unsigned phaseInc:20;    // Basically the frequency, generated from the instrument's mult, and the fNum and octave/block for the channel
        unsigned phaseCnt:20;    // Current place in the sine phase. 10.9 fixed-point number, where the whole selects the sine sample to use

        // TODO: AM/tremolo state. amPhase is a placeholder.
        unsigned amPhase:4;

        // TODO: FM/vibrato state. vibPhase is a placeholder.
        unsigned vibPhase:4;

        // TODO: Modulator feedback state.
        unsigned int modFB1;
        unsigned int modFB2;

        bool releaseSustain;   //1=key-off has release-rate at 5, 0=key-off has release rate at 7 (both with KSR adjustment)

        adsrPhase envPhase;
        unsigned int envLevel; // 0 - 127. 0.375dB steps (add envLevel * 0x10)

        //reg base 20
        bool amActive;           //tremolo (amplitude variance) @ 3.7Hz
        bool vibActive;          //frequency variance @ 6.4Hz
        bool sustain;         //1=sustained tone, 0=no sustain period
        bool keyScaleRate;     //KSR: modify ADSR rate based on frequency
        unsigned freqMult:4;   //frequency multiplier, 1 of 3 elements that define the frequency

        //reg base 40
        unsigned keyScaleLevel:2; //KSL: modify volume based on frequency
        unsigned totalLevel:6;    // level for the operator, 0.75dB steps (add totalLevel * 0x20 to output value)

        //reg base 60
        unsigned attackRate:4;
        unsigned decayRate:4;

        //reg 80
        unsigned sustainLevel:4;
        unsigned releaseRate:4;

        //reg e0
        unsigned wave:2;
    };

    enum connectionType {
        fm,
        additive
    };

    struct chan_t {
        unsigned int fNum; // 2nd of 3 elements that define the frequency
        bool keyOn; //on-off state of the key
        unsigned int octave; //3rd element that defines the frequency
        unsigned feedbackLevel: 3; // feedback level of first slot
        connectionType conn;

        op_t modOp;
        op_t carOp;
    } chan[9];

    bool rhythmMode;          // Rhythm mode enabled

    struct percChan_t {
        bool keyOn;
        chan_t* chan;
        op_t* modOp; // nullptr for everything but bass drum
        op_t* carOp;
    };

    percChan_t percChan[5] = { {false, &chan[6], &chan[6].modOp, &chan[6].carOp},  // Bass Drum
                               {false, &chan[7], nullptr,        &chan[7].modOp}, // High Hat
                               {false, &chan[7], nullptr,        &chan[7].carOp},  // Snare Drum
                               {false, &chan[8], nullptr,        &chan[8].modOp}, // Tom-tom
                               {false, &chan[8], nullptr,        &chan[8].carOp}}; // Top Cymbal

    enum rhythmInsts {
        bassDrum,
        highHat,
        snareDrum,
        tomTom,
        topCymbal
    };

    static constexpr uint8_t multVal[16] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};

    static const std::string rhythmNames[];

    static int logsinTable[256];
    static int expTable[256];

    void initTables();
    int lookupSin(int val, int waveForm);
    int lookupExp(int val);
};
