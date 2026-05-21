#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "mft.h"

int main(int argc, char* argv[])
{
	if (argc == 1)
	{
		printf("usage:\n\tmft.exe dump [Drive letter] [MFT Dump] \n\tDumps the MFT of the selected drive into the described file \n\n\ttranscode [MFT Dump] [CSV Result] \n\tProceses the MFT of the selected drive into a CSV to the described file\n\n\tDT [Drive letter][CSV Result] \n\tThe two other options together");
		exit(1);
	}

	if (strcmp("dump", argv[1]) == 0 && argc == 4)
	{
		MemoryFile* MFT;
		FILE* MFTResultFile;
		char* resultFileName = argv[3];
		char* driveLetter = argv[2];

		MFTResultFile = openResultFile(resultFileName);
		MFT = MFTDump(driveLetter);
		writeResultFile(MFT, MFTResultFile);

		char* fullPath = GetFullPath(resultFileName);
		printf("INFO: Result file successfully written to: %s\r\n", fullPath);
		free(fullPath);

		fclose(MFTResultFile);
		free(MFT->fileContent);
		free(MFT);
	}
	else if (strcmp("transcode", argv[1]) == 0 && argc == 4)
	{
		MFTTranscode(argv[2], argv[3], 0, (wchar_t*)L"?");
	}
	else if (strcmp("DT", argv[1]) == 0 && argc == 4)
	{
		if (strlen(argv[2]) == 1)
		{
			int letter = toupper(*argv[2]);
			if (letter >= 65 && letter <= 90)
			{
				wchar_t* letterW = malloc(4);
				if (letterW)
				{
					letterW[0] = (wchar_t)letter;
					letterW[1] = 0;
					MemoryFile* MFT = MFTDump(argv[2]);
					MFTTranscode(MFT, argv[3], 1, letterW);
					free(MFT->fileContent);
					free(MFT);
					free(letterW);
				}
				else
				{
					printf("Unexpected allocation error.");
				}
			}
			else
			{
				printf("Not a valid disk letter.");
			}
		}
		else
		{
			printf("Not a valid disk letter.");
		}
	}
	else
	{
		printf("usage:\n\tmft.exe dump [Drive letter] [MFT Dump] \n\tDumps the MFT of the selected drive into the described file \n\n\ttranscode [MFT Dump] [CSV Result] \n\tProceses the MFT of the selected drive into a CSV to the described file\n\n\tDT [Drive letter][CSV Result] \n\tThe two other options together");
		exit(1);
	}
}