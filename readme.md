# Opl2XmiPlayer

This is the code that I originally broke out of my uw-engine (code for an attempted Ultima Underworld engine) repo, combined with a nascent OPLL FM synthesizer implementation from my khedgehog repo (emulator for the 8-bit Sega consoles) to build an OPL2 synth, improve my .xmi playback code, and finish analysis of TVFX "Time-Variant Effects" sound effects present in the UW1+2 OPL2/3 timbre files.

## opl2XmiPlayer

Ties together an XMI file parser and MIDI event container, a GTL (AIL2 Global Timbre Library) parser, an OPL2 driver, and an OPL2 emulator, in order to play back XMI files through an emulated Yamaha FM synthesizer.

## keyboard

Load a timbre library, play the instruments and sound effects that it contains using a computer keyboard. The 12 notes of the scale starting at middle C (and ending at the C an octave up) are triggered by keys: `A W S E D F T G Y H U J K`. So, 8 from the home row, 5 from the row above it, in the semblance of a piano's layout. `Q` quits the program. `+` and `-` go to the next and previous timbre entries. Names for the timbres come from the default Roland MT-32 instrument numbers, and from the sound effect timbre names defined in Ultima Underworld 1.

My initial purpose in making this was to hear the individual timbres defined by different games. It's a great toy for that! But really, I think that its most important purpose currently is to host my implementation of the sound effect interpreter.

## droPlay

Plays back .dro OPL2 captures from Dosbox. Great for comparing my synthesizer's rendering of some music/sound effects to that from a more-mature implementation.

## yamahaYm3812.cpp/.h

This is something I've wanted to write for years, but considered beyond my skills for a long time: My Yamaha Ym3812 (OPL2) FM synthesizer implementation. Test, timer, and interrupt registers don't do anything. It doesn't support the CSM speech synthesis mode. It doesn't obey the waveform disable bit in the test register, or the keyboard split bit in register 8.

Every other function is implemented, although there are some flaws (rhythm mode is only partially functional, and isn't correct; there's not a lot of test material that I could find).

The purpose is ultimately to play back Ultima Underworld music and sound effects, and it does a good job with both. Just don't look to it for a really accuracy-focused emulation of the chip, because honest, it isn't that.

## kail/ale.h kail/yamaha.h

`ale.h` is an approximate C port of `ALE.INC`, as it would've existed in the Ultima Underworld codebase. It contains key code for understanding how the TVFX effects work. `yamaha.h` is an approximate (and *partial*) C port of `YAMAHA.INC` from John Miles' AIL 2.0 code release. It's key to understanding how the original AIL code interfaced with the Yamaha-based synthesizers. In particular, things like pitch bends, and the representation of register data and MIDI tracking state, scheduling of pushing register writes out to the hardware, etc.

## Misc

`experiments` contains random mini-programs that I used to test ideas or visualize data. There are other files that either had data that I wanted to have handy in the repository, or that are incomplete implementations of things that I considered writing before discarding (but that I think would be fun to write at some point in the future). `xmis/demo` has a demo.xmi file and demo.ad file from the AIL 2.0 source code release. `gm.ad` is supposed to be a set of OPL2 General MIDI-compatible patches.