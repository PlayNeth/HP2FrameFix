// ---------------------------------------------------------------------------
// Game addresses (NFSHP2.exe, image base 0x400000, no relocations)
// ---------------------------------------------------------------------------
#pragma once
#include "common.h"

namespace hp2 {
    // Globals
    const DWORD pApp            = 0x83C024;  // App*; +0x48 options, +0x50 audio subsystem
    const DWORD optAudioModePtr = 0x19154;   // options + this -> int* (option 0x12 = audio mode)
    const DWORD bEaxActive      = 0x84ECE1;  // BYTE, set by sound lib when EAX listener is live
    const DWORD bSoundReady     = 0x7930D4;  // DWORD, non-zero once the sound system is up
    const int   kAudioModeEAX   = 3;

    // Functions
    const DWORD fnRestartAudio  = 0x446360;  // void(), same as changing audio mode in the menu
    const DWORD fnProgAdvance   = 0x479C70;  // Progress::Advance (thiscall)
    const DWORD callLoadRender  = 0x479CCE;  // E8 call -> fnRenderScreen inside Advance
    const DWORD fnRenderScreen  = 0x598920;  // int __cdecl(Screen*)

    // 120 Hz simulation ticker
    const DWORD fnTickCallback  = 0x5DBA10;  // TIMECALLBACK, reschedules itself via timeSetEvent
    const DWORD pTickCounter    = 0x84C38C;  // DWORD, incremented once per sim tick
    const DWORD pTickRate       = 0x84C388;  // int, ticks per second (120)

    struct Sig { DWORD addr; const char* bytes; int len; };
    const Sig kSigs[] = {
        { fnRestartAudio, "\x83\x3D\xE0\x0B\x7A\x00\xFC", 7 },
        { 0x5F2E36,       "\xC6\x05\xE1\xEC\x84\x00\x01", 7 },             // EAX flag set
        { 0x536270,       "\x55\x8B\xEC\x8B\x45\x08\x8B\x8C\x81\x0C\x91\x01\x00", 13 }, // GetOption
        { fnProgAdvance,  "\x8B\x11\x56\x8B\x71\x04\x46\x8B", 8 },
        { callLoadRender, "\xE8\x4D\xEC\x11\x00", 5 },
        { fnTickCallback, "\xA1\x44\xCC\x84\x00\x8B\x0D\x50\xCC\x84\x00", 11 },
        { 0x5DBE22,       "\xA1\x8C\xC3\x84\x00\x40\xA3\x8C\xC3\x84\x00", 11 }, // tick++
        { 0x42E7DC,       "\x83\xC2\x03", 3 },                         // steer: every 4 steps
        { 0x42E7F9,       "\xD8\x05\x24\xD8\x65\x00", 6 },            // fadd [0.1]
        { 0x42E810,       "\xD8\x25\x24\xD8\x65\x00", 6 },            // fsub [0.1]
        { 0x545030,       "\x55\x8B\xEC\x51\x8A\x41\x08\x53\x32\xDB", 10 }, // AnimChannelSet::Advance
        { 0x54504E,       "\x8B\x15\xDC\x21\x72\x00", 6 },            // mov edx,[stepCounter]
        { 0x4D00A9,       "\x8A\x5D\x08\xB1\x01\x22\xD9", 7 },         // bl = step & 1
        { 0x4D0217,       "\xE8\x34\xA6\xFF\xFF", 5 },                 // call 0x4CA850
        { 0x4D021C,       "\x84\xDB\x74\x35", 4 },                      // test bl,bl / je skip
        { 0x42BEEE,       "\xE8\xBD\x27\x00\x00", 5 },                 // call selector (in CarModel update)
        { 0x42E713,       "\x0F\xB6\x81\x30\x06\x00\x00", 7 },       // gear = byte [car+0x630]
        { 0x42E71F,       "\x89\x46\x1C", 3 },                           // last animated gear [sel+0x1C]
        { 0x42E6FA,       "\x8A\x47\x3E", 3 },                           // selector reads looped (+0x34+0xA)
        { 0x42E761,       "\xE8\xDA\x3F\xFD\xFF", 5 },                 // selector PlayAnim calls (6)
        { 0x42E775,       "\xE8\xC6\x3F\xFD\xFF", 5 },
        { 0x42E795,       "\xE8\xA6\x3F\xFD\xFF", 5 },
        { 0x42E7A9,       "\xE8\x92\x3F\xFD\xFF", 5 },
        { 0x42E7BD,       "\xE8\x7E\x3F\xFD\xFF", 5 },
        { 0x42E83A,       "\xE8\x01\x3F\xFD\xFF", 5 },
        { 0x406FB0,       "\x8B\x49\x5C\x85\xC9\x74\x05", 7 },         // ActorList::UpdateAll wrapper
        { 0x41D7E1,       "\x8B\xD1\xD1\xFA\x89\x15\xFC\x33\x6D\x00\xD1\xF9", 12 }, // props: half steps
        { 0x41D80B,       "\xD8\x0D\x94\xD6\x65\x00", 6 },            // particles: fmul [0.5]
        { 0x4D53B8,       "\x85\xDB\x74\x09", 4 },                      // simple integrator odd-only gate
        { 0x4D8850,       "\xA1\xA8\x9A\x6E\x00\x57\x33\xFF", 8 },     // sync flying props loop
        { 0x444F80,       "\x55\x8B\xEC\x83\xEC\x18\x56\x8B\x35\xC8\x1A\x70\x00", 13 }, // re-file pending objects
        { 0x444440,       "\x55\x8B\xEC\x83\xEC\x48\x53\x8B\x5D\x08", 10 }, // simple integrator
        { 0x444495,       "\xD8\x0D\x90\xD6\x65\x00", 6 },            // dt 1/30 sites (6)
        { 0x4444A9,       "\xD8\x0D\x90\xD6\x65\x00", 6 },
        { 0x4444B6,       "\xD8\x0D\x90\xD6\x65\x00", 6 },
        { 0x4444DE,       "\xD8\x0D\x90\xD6\x65\x00", 6 },
        { 0x4444F9,       "\xD8\x0D\x90\xD6\x65\x00", 6 },
        { 0x444517,       "\xD8\x0D\x90\xD6\x65\x00", 6 },
        // interpolation
        { 0x4D0370,       "\xE8\x2B\xFD\xFF\xFF", 5 },                 // call WorldStep
        { 0x4CA968,       "\xE8\x13\x0F\xF6\xFF", 5 },                 // call CarModel::SetOwner
        { 0x4CA978,       "\xE8\xC3\x0F\xF6\xFF", 5 },                 // call CarModel::SetTransform
        { 0x4CA8D7,       "\x8B\x88\x70\x01\x00\x00", 6 },            // vehicle = [x+0x170]
        { 0x4CA8E3,       "\x8B\x90\xA8\x02\x00\x00", 6 },            // body pos at +0x2A8
        { 0x4CA960,       "\x8B\xB1\x2C\x0D\x00\x00", 6 },            // CarModel = [veh+0xD2C]
        { 0x4D7734,       "\xE8\xE7\x5E\xF3\xFF", 5 },                 // call BuildView (main)
        { 0x4D7813,       "\xE8\x08\x5E\xF3\xFF", 5 },                 // call BuildView (2nd)
        { 0x40D626,       "\x8B\x81\x64\x01\x00\x00", 6 },            // active camera slot
        { 0x40C269,       "\x39\x7B\x68", 3 },                           // camera id at +0x68
        { 0x40C756,       "\x8D\x41\x04", 3 },                           // eye at cam+8
        { 0x4D7985,       "\xE8\x56\x6E\x06\x00", 5 },                 // call reflection cubemap update
        { 0x4D795A,       "\x8B\x88\xA8\x02\x00\x00", 6 },            // probe pos = body +0x2A8
        { 0x4CAA7F,       "\xE8\x8C\x15\xF6\xFF", 5 },                 // car draw, list B
        { 0x44D6D0,       "\xE8\x3B\xE9\xFD\xFF", 5 },                 // car draw, sim-placed cars
    };
}
