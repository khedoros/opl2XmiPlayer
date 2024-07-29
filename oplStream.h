#pragma once

#include<SFML/System.hpp>
#include<SFML/Audio.hpp>
#include<cstdint>
#include<array>
#include "opl.h"
#include "yamahaYm3812.h"
#include<iostream>

#define PROCESS_PER_SECOND 25
#define SAMPLE_CHUNK (44100 / PROCESS_PER_SECOND)

class oplStream : public OPLEmul, public sf::SoundStream {
    private:
    YamahaYm3812 opl;
    std::array<int16_t,SAMPLE_CHUNK * 2> buffer;
    public:
    oplStream() : opl() {
        std::cout<<"Init the stream to 2 channels, 44100Hz sample rate\n";
        initialize(2,44100);
        setProcessingInterval(sf::milliseconds(1000/PROCESS_PER_SECOND));
    }
    ~oplStream() {
        std::cout<<"Deconstruct the stream\n";
    }
    virtual bool onGetData (Chunk &data) override {
        // std::cout<<"\n**************GENERATING AUDIO************\n";
        data.sampleCount = SAMPLE_CHUNK;
        data.samples = buffer.data();
        buffer.fill(0);
        opl.Update(const_cast<int16_t*>(data.samples), SAMPLE_CHUNK);
        return true;
    }
    virtual void onSeek (sf::Time timeOffset) override {}
    virtual void Reset() override {opl.Reset();}
    virtual void WriteReg(int reg, int v) override {opl.WriteReg(reg,v);}
    virtual void Update(float *buffer, int length) override {opl.Update(buffer,length);}
    virtual void Update(int16_t *buffer, int length) override {opl.Update(buffer,length);}
    virtual void SetPanning(int channel, float left, float right) override {opl.SetPanning(channel, left, right);}
};
