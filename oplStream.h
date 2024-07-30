#pragma once

#include<cstdint>
#include<array>
#include<iostream>
#include<cstring>
#include<SDL2/SDL.h>
#include "opl.h"
#include "yamahaYm3812.h"

class oplStream : public OPLEmul {
    private:
        YamahaYm3812 opl;
        static const int stereoChannels = 2;
        static const int sampleRate = 44100;
        static const int processPerSecond = 100;
        static const int sampleChunkSize = (sampleRate * stereoChannels) / processPerSecond;
        static const int sampleChunkTimeInMs = 1000 / processPerSecond;
        std::array<int16_t,sampleChunkSize> buffer;
        int remainingMilliseconds;
        int sdlDevId;
    public:
        oplStream() : opl(),remainingMilliseconds{0} {
            std::cout<<"Init the stream to "<<stereoChannels<<" channels, "<<sampleRate<<"Hz sample rate\n";
            initSDLAudio();
        }
        ~oplStream() {
            std::cout<<"Deconstruct the stream\n";
        }
        virtual void Reset() override {
            opl.Reset();
            remainingMilliseconds = 0;
            SDL_PauseAudioDevice(sdlDevId, 1);
        }
        void play() {
            SDL_PauseAudioDevice(sdlDevId, 0);
        }
        virtual void WriteReg(int reg, int v) override {
            opl.WriteReg(reg,v);
        }
        void addTime(int milliseconds) {
            remainingMilliseconds += milliseconds;
            while(remainingMilliseconds >= sampleChunkTimeInMs) {
                buffer.fill(0);
                opl.Update(buffer.data(), sampleChunkSize / stereoChannels);
                remainingMilliseconds -= sampleChunkTimeInMs;
                SDL_QueueAudio(sdlDevId, buffer.data(), sampleChunkSize * sizeof(int16_t));

                while(SDL_GetQueuedAudioSize(sdlDevId) > 10 * sampleChunkSize * sizeof(int16_t)) {
                    SDL_Delay(sampleChunkTimeInMs);
                }
            }
        }
        uint32_t getQueuedAudioBytes() {
            return SDL_GetQueuedAudioSize(sdlDevId);
        }

        virtual void Update(float *buffer, int length) override {opl.Update(buffer,length);}
        virtual void Update(int16_t *buffer, int length) override {opl.Update(buffer,length);}
        virtual void SetPanning(int channel, float left, float right) override {opl.SetPanning(channel, left, right);}
        int initSDLAudio() {
            int num_drivers = SDL_GetNumAudioDrivers();
            unsigned int chosen_driver = 255;
            unsigned int driver_desirability = 255;
            std::array<std::array<const char, 20>, 6> desired_drivers = {"directsound", "winmm", "pulse", "pulseaudio", "alsa", "dummy"};
            for(int i=0; i<num_drivers;i++) {
                for(unsigned int j=0;j<desired_drivers.size();j++) {
                    if(strncmp((desired_drivers[j].data()), SDL_GetAudioDriver(i), 20) == 0 && j < driver_desirability) {
                        chosen_driver = i;
                        driver_desirability = j;
                    }
                }
            }

            if(chosen_driver == 255) {
                chosen_driver = 0;
            }
            int failure_code = SDL_AudioInit(SDL_GetAudioDriver(chosen_driver));
            if(failure_code) {
                fprintf(stderr, "Error init'ing driver: %s", SDL_GetError());
            }

            SDL_AudioSpec want;
            want.freq=sampleRate;
            want.format=AUDIO_S16;
            want.channels=stereoChannels;
            want.silence=0;
            want.samples=sampleChunkSize / stereoChannels;
            want.size=0;
            want.callback=NULL;
            want.userdata=NULL;

            SDL_AudioSpec got;
            sdlDevId= SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
            if(!sdlDevId) {
                fprintf(stderr, "Error opening audio device: %s\n", SDL_GetError());
                fprintf(stderr, "No audio device opened.\n");
            }
            else {
                printf("Freq: %d format: %d (%d) ch: %d sil: %d samp: %d size: %d\n", got.freq, got.format, want.format, got.channels, got.silence, got.samples, got.size);
                SDL_PauseAudioDevice(sdlDevId, 0);
            }
            return sdlDevId;
        }
};
