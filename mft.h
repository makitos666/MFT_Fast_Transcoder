#ifndef MFT_H
#define MFT_H

#include <stdio.h>
#include "structs.h"

FILE* openResultFile(char* resultFileName);
char* GetFullPath(char* resultFileName);
void writeResultFile(MemoryFile* MFT, FILE* MFTResultFile);
void MFTSanityChecks(MemoryFile* MFT);

MemoryFile* MFTDump(char* driveLetter);
void MFTTranscode(void* _MFTFilePath, char* resultFile, int inputType, wchar_t* letter);

#endif
