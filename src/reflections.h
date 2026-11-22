// What the other parts use from reflections.cpp.
#pragma once
#include "common.h"

namespace hp2 {
    struct ReflSig { DWORD addr; const char* bytes; size_t len; };
    const ReflSig kReflSigs[] = {
        { 0x53E143, "\x68\x80\x00\x00\x00\x68\x80\x00\x00\x00", 10 },  // composite target 128x128
        { 0x53E175, "\x68\x80\x00\x00\x00\x68\x80\x00\x00\x00", 10 },  // cube faces 128x128
        { 0x53E12A, "\x8B\x0D\x74\x54\x66\x00", 6 },                  // composite viewport [0x665474]
        { 0x53E169, "\xA1\x74\x54\x66\x00", 5 },                      // cube viewport [0x665474]
        { 0x53EB4E, "\x68\x00\x00\x00\x3F\x68\x00\x00\x00\x3F", 10 },  // composite view scale 0.5
        { 0x53ED46, "\x68\x00\x00\x00\x3F\x68\x00\x00\x00\x3F", 10 },
        { 0x53E06D, "\x06\x00\x00\x00\xC7\x03\xFF\x00\x00\x00\xC7\x07\xFF\x00\x00\x00"
                    "\xC7\x46\x08\xFF\x00\x00\x00", 23 },               // reflection colour 255
        { 0x53E85F, "\x0F\x84\x88\x00\x00\x00", 6 },                  // face parity branch
        { 0x53E8E8, "\xE9\x45\x02\x00\x00", 5 },                      // jmp last face + composite
        { 0x53E8F6, "\x83\xB8\x78\x3D\x01\x00\x02\x0F\x85\x36\x02\x00\x00", 13 }, // detail == 2
        { 0x53EB32, "\x8B\xCE\xE8\xB7\x00\x00\x00", 7 },              // render face
    };
}
void InstallReflections();
