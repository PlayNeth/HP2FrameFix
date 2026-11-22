// What the other parts use from portable.cpp.
#pragma once
#include "common.h"

// Save folder: the profile/save path builder (0x528B9B) asks for My Documents
// (SHGetSpecialFolderPathA CSIDL_PERSONAL at 0x528BD4), appends
// "\EA Games\Need For Speed Hot Pursuit 2\" creating each folder, and copies
// the result into the profile manager (+0xC34). The call is redirected to a
// function that returns our folder, and the append code is jumped over
// (0x528BE4 -> 0x528C9C, same stack: edi and ebx pushed).
namespace hp2 {
    const DWORD callDocsFolder = 0x528BD4;   // call [SHGetSpecialFolderPathA] (6 bytes)
    const DWORD saveAppend     = 0x528BE4;   // mov edx, [0x6A3D08] (6 bytes): start of the append
    const DWORD saveAppendDone = 0x528C9C;   // copy into the profile manager
}
void InstallSaveFolder();
void InstallPortableRegistry();
