#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "mft.h"

FILE* openResultFile(char* resultFileName)
{
	FILE* MFTFile;

	if (fopen_s(&MFTFile, resultFileName, "w+b") != 0)
	{
		printf("Error: Unexpected error opening result file: %s\r\n", resultFileName);
		exit(1);
	}

	return MFTFile;
}

char* GetFullPath(char* resultFileName)
{
	char* resultFullPath;
	DWORD resultFullPathLenght;

	resultFullPathLenght = GetFullPathNameA(resultFileName, 0, NULL, NULL);
	if (resultFullPathLenght == 0)
	{
		printf("Error: Unexpected pathname.\n");
		exit(1);
	}
	resultFullPath = (char*)malloc(resultFullPathLenght);
	if (resultFullPath == NULL)
	{
		printf("Error allocating memory\n");
		exit(1);
	}

	GetFullPathNameA(resultFileName, resultFullPathLenght, resultFullPath, NULL);
	return resultFullPath;
}

void writeResultFile(MemoryFile* MFT, FILE* MFTResultFile)
{
	if (fwrite(MFT->fileContent, 1, MFT->size, MFTResultFile) != MFT->size)
	{
		printf("Error: Error writing to output file.\r\n");
		exit(1);
	}
}

void MFTSanityChecks(MemoryFile* MFT)
{
	int magicNumberCheck = memcmp(MFT->fileContent, "FILE", 4);
	if (MFT->size < (1024 * 5) || MFT->size % 1024 != 0 || magicNumberCheck != 0)
	{
		printf("Error: Invalid MFT.\r\n");
		exit(1);
	}
}
