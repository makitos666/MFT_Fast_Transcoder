#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <windows.h>
#include "mft.h"

#define MAX_RESULT_PAGES 1024
#define RECORD_SIZE 1024
#define MAX_PATH_STACK_DEPTH 512

static int safeAddSize(size_t a, size_t b, size_t* out);
static int safeMulSize(size_t a, size_t b, size_t* out);
static int convertWideToUtf8(const wchar_t* input, uint8_t inputLen, char** output, size_t* outputLen);
static void sanitizePipeField(char* text, size_t len);

static void pageSave(paginationStruct* result_pages, char* whatToSave, int* currentPage, size_t size)
{
	size_t required = 0;
	if (safeAddSize(result_pages[*currentPage].occupiedSpace, size, &required) == 0)
	{
		printf("Output buffer overflow.");
		exit(1);
	}

	if (required > result_pages[*currentPage].totalSpace)
	{
		if (*currentPage >= (MAX_RESULT_PAGES - 1))
		{
			printf("This MFT is ridiculously big or there is any other BUG. Open an issue ^^");
			exit(1);
		}
		*currentPage = (*currentPage + 1);
		unsigned char* page = (unsigned char*)malloc(100000000);
		if (page == NULL)
		{
			printf("Not enough memory");
			exit(1);
		}
		result_pages[*currentPage].fileContent = page;
		result_pages[*currentPage].totalSpace = 100000000;
		result_pages[*currentPage].occupiedSpace = 0;
	}
	size_t available = result_pages[*currentPage].totalSpace - result_pages[*currentPage].occupiedSpace;
	memcpy_s(&result_pages[*currentPage].fileContent[result_pages[*currentPage].occupiedSpace], available, whatToSave, size);
	result_pages[*currentPage].occupiedSpace += size;
}

static int safeAddSize(size_t a, size_t b, size_t* out)
{
	if (a > (SIZE_MAX - b))
	{
		return 0;
	}
	*out = a + b;
	return 1;
}

static int safeMulSize(size_t a, size_t b, size_t* out)
{
	if (a != 0 && b > (SIZE_MAX / a))
	{
		return 0;
	}
	*out = a * b;
	return 1;
}

static int convertWideToUtf8(const wchar_t* input, uint8_t inputLen, char** output, size_t* outputLen)
{
	*output = NULL;
	*outputLen = 0;
	if (input == NULL || inputLen == 0)
	{
		return 0;
	}

	int needed = WideCharToMultiByte(CP_UTF8, WC_NO_BEST_FIT_CHARS, input, (int)inputLen, NULL, 0, NULL, NULL);
	if (needed <= 0)
	{
		return 0;
	}

	*output = (char*)malloc((size_t)needed);
	if (*output == NULL)
	{
		return 0;
	}

	int produced = WideCharToMultiByte(CP_UTF8, WC_NO_BEST_FIT_CHARS, input, (int)inputLen, *output, needed, NULL, NULL);
	if (produced <= 0 || produced > needed)
	{
		free(*output);
		*output = NULL;
		return 0;
	}

	*outputLen = (size_t)produced;
	return 1;
}

static void sanitizePipeField(char* text, size_t len)
{
	for (size_t i = 0; i < len; i++)
	{
		if (text[i] == '|' || text[i] == '\r' || text[i] == '\n')
		{
			text[i] = '_';
		}
	}
}

void MFTTranscode(void* _MFTFilePath, char* resultFile, int inputType, wchar_t* letter)
{
	FILE* mft_file_pointer = NULL;
	size_t res_size = 0, maxFiles = 0, fsize;
	unsigned char* mft_file_contents = NULL;
	uint64_t fileCount = 0;
	FilesAndFolders* files = NULL;
	FilesAndFolders* folders = NULL;
	wchar_t* start = NULL;
	paginationStruct* result_pages = NULL;
	int currentPage = -1;
	int ownsMFTBuffer = 0;

	if (inputType == 0)
	{
		char* errorMSG = (char*)malloc(1024);
		errno_t err;
		char* MFTFilePath;
		MFTFilePath = (char*)_MFTFilePath;
		err = fopen_s(&mft_file_pointer, MFTFilePath, "rb");
		if (err != 0)
		{
			strerror_s(errorMSG, 256, err);
			printf("Error reading MFT File: %s\n", errorMSG);
			goto exitMFTTranscode;
		}
		free(errorMSG);
		_fseeki64(mft_file_pointer, 0, SEEK_END);
		fsize = _ftelli64(mft_file_pointer);
		if (fsize == 0)
		{
			printf("Empty input file.\n");
			goto exitMFTTranscode;
		}
		fseek(mft_file_pointer, 0, SEEK_SET);
		mft_file_contents = (char*)malloc((size_t)fsize + 1);
		if (mft_file_contents == NULL)
		{
			printf("Not enough RAM to load MFT File.\n");
			goto exitMFTTranscode;
		}
		ownsMFTBuffer = 1;
		size_t readItems = fread_s(mft_file_contents, fsize, fsize, 1, mft_file_pointer);
		if (readItems != 1)
		{
			printf("Failed to read from MFT File.\n");
			goto exitMFTTranscode;
		}
		fclose(mft_file_pointer);
		mft_file_pointer = NULL;
	}
	else if (inputType == 1)
	{
		MemoryFile* file = (MemoryFile*)_MFTFilePath;
		mft_file_contents = file->fileContent;
		fsize = file->size;
	}
	else
	{
		printf("Unexpected behaviour.");
		goto exitMFTTranscode;
	}

	if (fsize < RECORD_SIZE || (fsize % RECORD_SIZE) != 0)
	{
		printf("Invalid MFT file size.\n");
		goto exitMFTTranscode;
	}

	maxFiles = (fsize / RECORD_SIZE) * 2;
	size_t mallocSize = 0;
	if (safeMulSize(maxFiles, sizeof(FilesAndFolders), &mallocSize) == 0)
	{
		printf("Not enough RAM");
		goto exitMFTTranscode;
	}
	files = (FilesAndFolders*)malloc(mallocSize);
	folders = (FilesAndFolders*)malloc(mallocSize);
	if (files == NULL || folders == NULL || maxFiles < 5)
	{
		printf("Not enough RAM");
		goto exitMFTTranscode;
	}
	memset(files, 0, mallocSize);
	memset(folders, 0, mallocSize);

	int magicNumberCheck = memcmp(mft_file_contents, "FILE", 4);
	if (magicNumberCheck != 0 || fsize < RECORD_SIZE)
	{
		printf("Invalid MFT file");
		goto exitMFTTranscode;
	}
	MemoryFile sanityMFT = { mft_file_contents, fsize };
	MFTSanityChecks(&sanityMFT);

	for (size_t o = RECORD_SIZE; o < fsize; o += RECORD_SIZE)
	{
		FileRecordHeader* fileRecord = (FileRecordHeader*)(&mft_file_contents[o]);

		if (!fileRecord->inUse)
		{
			continue;
		}
		if (fileRecord->firstAttributeOffset >= RECORD_SIZE)
		{
			continue;
		}

		uint64_t seq_number = *(uint16_t*)((uint8_t*)fileRecord + 48);
		if (seq_number == *(uint16_t*)((uint64_t)fileRecord + 510) && seq_number == *(uint16_t*)((uint64_t)fileRecord + 1022))
		{
			uint16_t seq_attr1, seq_attr2 = 0;
			if (fileRecord->updateSequenceOffset == 42)
			{
				seq_attr1 = *(uint16_t*)((uint8_t*)fileRecord + 44);
				seq_attr2 = *(uint16_t*)((uint8_t*)fileRecord + 46);
			}
			else
			{
				seq_attr1 = *(uint16_t*)((uint8_t*)fileRecord + 50);
				seq_attr2 = *(uint16_t*)((uint8_t*)fileRecord + 52);
			}
			memcpy_s((void*)((uint8_t*)fileRecord + 510), 2, &seq_attr1, 2);
			memcpy_s((void*)((uint8_t*)fileRecord + 1022), 2, &seq_attr2, 2);
		}

		StandardInformation standardInfoZeroed = { 0 };
		StandardInformation* standardInformation = &standardInfoZeroed;
		uint64_t fileReference = fileRecord->fileReference;
		int linkedRecordLimit = 0;
		while (fileReference != 0 && linkedRecordLimit < 64)
		{
			size_t linkedOffset = (size_t)((fileReference & 0x0000FFFFFFFFFFFF) * RECORD_SIZE);
			if (linkedOffset > (fsize - RECORD_SIZE))
			{
				break;
			}
			FileRecordHeader* fileRecord2 = (FileRecordHeader*)(&mft_file_contents[linkedOffset]);
			if (fileRecord2->firstAttributeOffset >= RECORD_SIZE)
			{
				break;
			}
			AttributeHeader* attribute2 = (AttributeHeader*)((uint8_t*)fileRecord2 + fileRecord2->firstAttributeOffset);
			while (((size_t)((uint8_t*)attribute2 - (uint8_t*)fileRecord2) + sizeof(AttributeHeader)) <= RECORD_SIZE)
			{
				if (attribute2->attributeType == 0x10)
				{
					standardInformation = (StandardInformation*)attribute2;
					break;
				}
				else if (attribute2->attributeType == 0xFFFFFFFF)
				{
					break;
				}
				if (attribute2->length < sizeof(AttributeHeader))
				{
					break;
				}
				size_t nextOffset = (size_t)((uint8_t*)attribute2 - (uint8_t*)fileRecord2) + attribute2->length;
				if (nextOffset > RECORD_SIZE)
				{
					break;
				}
				attribute2 = (AttributeHeader*)((uint8_t*)fileRecord2 + nextOffset);
			}
			fileReference = fileRecord2->fileReference;
			linkedRecordLimit++;
		}
		AttributeHeader* attribute = (AttributeHeader*)((uint8_t*)fileRecord + fileRecord->firstAttributeOffset);

		while (((size_t)((uint8_t*)attribute - (uint8_t*)fileRecord) + sizeof(AttributeHeader)) <= RECORD_SIZE)
		{
			if (attribute->attributeType == 0x30)
			{
				if (attribute->length < sizeof(FileNameAttributeHeader))
				{
					break;
				}
				FileNameAttributeHeader* fileNameAttribute = (FileNameAttributeHeader*)attribute;
				if (!fileNameAttribute->residentAttributeHeader.attributeHeader.nonResident)
				{
					size_t fnHeaderOffset = (size_t)((uint8_t*)fileNameAttribute - (uint8_t*)fileRecord);
					size_t fnDataLen = (size_t)fileNameAttribute->fileNameLength * sizeof(wchar_t);
					size_t fnTotalLen = 0;
					if (safeAddSize(sizeof(FileNameAttributeHeader) - sizeof(wchar_t), fnDataLen, &fnTotalLen) == 0)
					{
						break;
					}
					if (safeAddSize(fnHeaderOffset, fnTotalLen, &fnTotalLen) == 0 || fnTotalLen > RECORD_SIZE)
					{
						break;
					}
					if ((size_t)fileRecord->recordNumber >= maxFiles)
					{
						break;
					}
					if (fileRecord->isDirectory)
					{
						folders[fileRecord->recordNumber].isDirectory = fileRecord->isDirectory;
						folders[fileRecord->recordNumber].filename = fileNameAttribute->fileName;
						folders[fileRecord->recordNumber].recordNumber = fileRecord->recordNumber;
						folders[fileRecord->recordNumber].parentRecordNumber = fileNameAttribute->parentRecordNumber;
						folders[fileRecord->recordNumber].fileNameLength = fileNameAttribute->fileNameLength;
						folders[fileRecord->recordNumber].creationTime = standardInformation->fileCreationTime;
						folders[fileRecord->recordNumber].modificationTime = standardInformation->fileAlteredTime;
						folders[fileRecord->recordNumber].metadataTime = standardInformation->changedTime;
						folders[fileRecord->recordNumber].accesTime = standardInformation->fileReadTime;
						folders[fileRecord->recordNumber].FNcreationTime = fileNameAttribute->creationTime;
						folders[fileRecord->recordNumber].FNmodificationTime = fileNameAttribute->modificationTime;
						folders[fileRecord->recordNumber].FNmetadataTime = fileNameAttribute->metadataModificationTime;
						folders[fileRecord->recordNumber].FNaccesTime = fileNameAttribute->readTime;
					}
					else
					{
						if (fileNameAttribute->namespaceType != 2)
						{
							if (fileCount >= maxFiles)
							{
								break;
							}
							files[fileCount].isDirectory = fileRecord->isDirectory;
							files[fileCount].filename = fileNameAttribute->fileName;
							files[fileCount].recordNumber = fileRecord->recordNumber;
							files[fileCount].parentRecordNumber = fileNameAttribute->parentRecordNumber;
							files[fileCount].fileNameLength = fileNameAttribute->fileNameLength;
							files[fileCount].creationTime = standardInformation->fileCreationTime;
							files[fileCount].modificationTime = standardInformation->fileAlteredTime;
							files[fileCount].metadataTime = standardInformation->changedTime;
							files[fileCount].accesTime = standardInformation->fileReadTime;
							files[fileCount].FNcreationTime = fileNameAttribute->creationTime;
							files[fileCount].FNmodificationTime = fileNameAttribute->modificationTime;
							files[fileCount].FNmetadataTime = fileNameAttribute->metadataModificationTime;
							files[fileCount].FNaccesTime = fileNameAttribute->readTime;
							fileCount++;
						}
					}
				}
			}
			else if (attribute->attributeType == 0x10)
			{
				standardInformation = (StandardInformation*)attribute;
			}
			else if (attribute->attributeType == 0xFFFFFFFF)
			{
				break;
			}
			if (attribute->length < sizeof(AttributeHeader))
			{
				break;
			}
			size_t nextOffset = (size_t)((uint8_t*)attribute - (uint8_t*)fileRecord) + attribute->length;
			if (nextOffset > RECORD_SIZE)
			{
				break;
			}
			attribute = (AttributeHeader*)((uint8_t*)fileRecord + nextOffset);
		}
	}

	currentPage = 0;
	result_pages = (paginationStruct*)malloc(sizeof(paginationStruct) * MAX_RESULT_PAGES);
	if (result_pages == NULL)
	{
		printf("Error allocating memory");
		goto exitMFTTranscode;
	}
	memset(result_pages, 0, sizeof(paginationStruct) * MAX_RESULT_PAGES);
	size_t aproxBytes = 0;
	if (safeMulSize((size_t)fileCount, (250 + 168), &aproxBytes) == 0 || aproxBytes < 1024)
	{
		aproxBytes = 1024;
	}
	unsigned char* page = (unsigned char*)malloc(aproxBytes);
	if (page == NULL)
	{
		printf("Not enough memory");
		goto exitMFTTranscode;
	}
	result_pages[currentPage].fileContent = page;
	result_pages[currentPage].totalSpace = aproxBytes;
	result_pages[currentPage].occupiedSpace = 0;

	char* header = "File or Folder|File|$STDINFO Creation Time|$STDINFO Modification Time|$STDINFO Metadata change Time|$STDINFO Access Time|$FILENAME Creation Time|$FILENAME Modification Time|$FILENAME Entry Modified Time|$FILENAME Access Time\n";
	pageSave(result_pages, header, &currentPage, strlen(header));

	start = (wchar_t*)malloc(3 * sizeof(wchar_t));
	if (start == NULL)
	{
		printf("Error allocating memory");
		goto exitMFTTranscode;
	}
	memcpy_s(start, 2, letter, 2);
	start[1] = L':';
	start[2] = L'\0';
	folders[5].filename = start;
	folders[5].fileNameLength = 2;

	size_t cnt = 0;
	while (cnt < maxFiles)
	{
		if (folders[cnt].filename != NULL)
		{
			pageSave(result_pages, "Folder|", &currentPage, 7);
			uint64_t stack[MAX_PATH_STACK_DEPTH];
			int top = -1;
			uint64_t rnumber, pnumber;
			int invalidPath = 0;
			int hops = 0;
			rnumber = folders[cnt].recordNumber;
			pnumber = folders[cnt].parentRecordNumber;
			if ((size_t)rnumber >= maxFiles || (size_t)pnumber >= maxFiles)
			{
				invalidPath = 1;
			}
			if (!invalidPath && top + 2 < MAX_PATH_STACK_DEPTH)
			{
				top = top + 1;
				stack[top] = rnumber;
				top = top + 1;
				stack[top] = pnumber;
			}
			else
			{
				invalidPath = 1;
			}
			while (!invalidPath && rnumber != pnumber && hops < MAX_PATH_STACK_DEPTH)
			{
				if ((size_t)pnumber >= maxFiles || folders[pnumber].filename == NULL)
				{
					invalidPath = 1;
					break;
				}
				rnumber = folders[pnumber].recordNumber;
				pnumber = folders[pnumber].parentRecordNumber;
				if ((size_t)pnumber >= maxFiles || top + 2 >= MAX_PATH_STACK_DEPTH)
				{
					invalidPath = 1;
					break;
				}
				top = top + 1;
				stack[top] = rnumber;
				top = top + 1;
				stack[top] = pnumber;
				hops++;
			}
			while (!invalidPath && top > 0)
			{
				pnumber = stack[top];
				top = top - 1;
				rnumber = stack[top];
				top = top - 1;
				if (rnumber != 5)
				{
					if ((size_t)pnumber >= maxFiles || folders[pnumber].filename == NULL)
					{
						invalidPath = 1;
						break;
					}
					char* utf_name = NULL;
					size_t resultBytes = 0;
					if (!convertWideToUtf8(folders[pnumber].filename, folders[pnumber].fileNameLength, &utf_name, &resultBytes))
					{
						invalidPath = 1;
						break;
					}
					sanitizePipeField(utf_name, resultBytes);
					pageSave(result_pages, utf_name, &currentPage, resultBytes);
					pageSave(result_pages, "\\", &currentPage, 1);
					free(utf_name);
				}
			}

			char* utf_name = NULL;
			size_t resultBytes = 0;
			if (convertWideToUtf8(folders[cnt].filename, folders[cnt].fileNameLength, &utf_name, &resultBytes))
			{
				sanitizePipeField(utf_name, resultBytes);
				pageSave(result_pages, utf_name, &currentPage, resultBytes);
				free(utf_name);
			}
			pageSave(result_pages, "|", &currentPage, 1);
			FILETIME time;
			SYSTEMTIME timeHuman;
			char timeString[20];
			time.dwLowDateTime = (DWORD)folders[cnt].creationTime;
			time.dwHighDateTime = (DWORD)(folders[cnt].creationTime >> 32);
			FileTimeToSystemTime(&time, &timeHuman);
			sprintf_s(timeString, 20, "%04d-%02d-%02d %02d:%02d:%02d", timeHuman.wYear, timeHuman.wMonth, timeHuman.wDay, timeHuman.wHour, timeHuman.wMinute, timeHuman.wSecond);
			pageSave(result_pages, timeString, &currentPage, 19);
			pageSave(result_pages, "|", &currentPage, 1);
			time.dwLowDateTime = (DWORD)folders[cnt].modificationTime;
			time.dwHighDateTime = (DWORD)(folders[cnt].modificationTime >> 32);
			FileTimeToSystemTime(&time, &timeHuman);
			sprintf_s(timeString, 20, "%04d-%02d-%02d %02d:%02d:%02d", timeHuman.wYear, timeHuman.wMonth, timeHuman.wDay, timeHuman.wHour, timeHuman.wMinute, timeHuman.wSecond);
			pageSave(result_pages, timeString, &currentPage, 19);
			pageSave(result_pages, "|", &currentPage, 1);
			time.dwLowDateTime = (DWORD)folders[cnt].metadataTime;
			time.dwHighDateTime = (DWORD)(folders[cnt].metadataTime >> 32);
			FileTimeToSystemTime(&time, &timeHuman);
			sprintf_s(timeString, 20, "%04d-%02d-%02d %02d:%02d:%02d", timeHuman.wYear, timeHuman.wMonth, timeHuman.wDay, timeHuman.wHour, timeHuman.wMinute, timeHuman.wSecond);
			pageSave(result_pages, timeString, &currentPage, 19);
			pageSave(result_pages, "|", &currentPage, 1);
			time.dwLowDateTime = (DWORD)folders[cnt].accesTime;
			time.dwHighDateTime = (DWORD)(folders[cnt].accesTime >> 32);
			FileTimeToSystemTime(&time, &timeHuman);
			sprintf_s(timeString, 20, "%04d-%02d-%02d %02d:%02d:%02d", timeHuman.wYear, timeHuman.wMonth, timeHuman.wDay, timeHuman.wHour, timeHuman.wMinute, timeHuman.wSecond);
			pageSave(result_pages, timeString, &currentPage, 19);
			pageSave(result_pages, "|", &currentPage, 1);
			time.dwLowDateTime = (DWORD)folders[cnt].FNcreationTime;
			time.dwHighDateTime = (DWORD)(folders[cnt].FNcreationTime >> 32);
			FileTimeToSystemTime(&time, &timeHuman);
			sprintf_s(timeString, 20, "%04d-%02d-%02d %02d:%02d:%02d", timeHuman.wYear, timeHuman.wMonth, timeHuman.wDay, timeHuman.wHour, timeHuman.wMinute, timeHuman.wSecond);
			pageSave(result_pages, timeString, &currentPage, 19);
			pageSave(result_pages, "|", &currentPage, 1);
			time.dwLowDateTime = (DWORD)folders[cnt].FNmodificationTime;
			time.dwHighDateTime = (DWORD)(folders[cnt].FNmodificationTime >> 32);
			FileTimeToSystemTime(&time, &timeHuman);
			sprintf_s(timeString, 20, "%04d-%02d-%02d %02d:%02d:%02d", timeHuman.wYear, timeHuman.wMonth, timeHuman.wDay, timeHuman.wHour, timeHuman.wMinute, timeHuman.wSecond);
			pageSave(result_pages, timeString, &currentPage, 19);
			pageSave(result_pages, "|", &currentPage, 1);
			time.dwLowDateTime = (DWORD)folders[cnt].FNmetadataTime;
			time.dwHighDateTime = (DWORD)(folders[cnt].FNmetadataTime >> 32);
			FileTimeToSystemTime(&time, &timeHuman);
			sprintf_s(timeString, 20, "%04d-%02d-%02d %02d:%02d:%02d", timeHuman.wYear, timeHuman.wMonth, timeHuman.wDay, timeHuman.wHour, timeHuman.wMinute, timeHuman.wSecond);
			pageSave(result_pages, timeString, &currentPage, 19);
			pageSave(result_pages, "|", &currentPage, 1);
			time.dwLowDateTime = (DWORD)folders[cnt].FNaccesTime;
			time.dwHighDateTime = (DWORD)(folders[cnt].FNaccesTime >> 32);
			FileTimeToSystemTime(&time, &timeHuman);
			sprintf_s(timeString, 20, "%04d-%02d-%02d %02d:%02d:%02d", timeHuman.wYear, timeHuman.wMonth, timeHuman.wDay, timeHuman.wHour, timeHuman.wMinute, timeHuman.wSecond);
			pageSave(result_pages, timeString, &currentPage, 19);
			pageSave(result_pages, "\n", &currentPage, 1);
		}
		cnt += 1;
	}

	for (size_t j = 0; j < fileCount; j++)
	{
		pageSave(result_pages, "File|", &currentPage, 5);
		uint64_t stack[MAX_PATH_STACK_DEPTH];
		int top = -1;
		uint64_t rnumber, pnumber;
		int invalidPath = 0;
		int hops = 0;
		rnumber = files[j].recordNumber;
		pnumber = files[j].parentRecordNumber;
		if ((size_t)rnumber >= maxFiles || (size_t)pnumber >= maxFiles)
		{
			invalidPath = 1;
		}
		if (!invalidPath && top + 2 < MAX_PATH_STACK_DEPTH)
		{
			top = top + 1;
			stack[top] = rnumber;
			top = top + 1;
			stack[top] = pnumber;
		}
		else
		{
			invalidPath = 1;
		}
		while (!invalidPath && rnumber != pnumber && hops < MAX_PATH_STACK_DEPTH)
		{
			if ((size_t)pnumber >= maxFiles || folders[pnumber].filename == NULL)
			{
				invalidPath = 1;
				break;
			}
			rnumber = folders[pnumber].recordNumber;
			pnumber = folders[pnumber].parentRecordNumber;
			if ((size_t)pnumber >= maxFiles || top + 2 >= MAX_PATH_STACK_DEPTH)
			{
				invalidPath = 1;
				break;
			}
			top = top + 1;
			stack[top] = rnumber;
			top = top + 1;
			stack[top] = pnumber;
			hops++;
		}
		while (!invalidPath && top > 0)
		{
			pnumber = stack[top];
			top = top - 1;
			rnumber = stack[top];
			top = top - 1;
			if (rnumber != 5)
			{
				if ((size_t)pnumber >= maxFiles || folders[pnumber].filename == NULL)
				{
					invalidPath = 1;
					break;
				}
				char* utf_name = NULL;
				size_t resultBytes = 0;
				if (!convertWideToUtf8(folders[pnumber].filename, folders[pnumber].fileNameLength, &utf_name, &resultBytes))
				{
					invalidPath = 1;
					break;
				}
				sanitizePipeField(utf_name, resultBytes);
				pageSave(result_pages, utf_name, &currentPage, resultBytes);
				pageSave(result_pages, "\\", &currentPage, 1);
				free(utf_name);
			}
		}

		char* utf_name = NULL;
		size_t resultBytes = 0;
		if (convertWideToUtf8(files[j].filename, files[j].fileNameLength, &utf_name, &resultBytes))
		{
			sanitizePipeField(utf_name, resultBytes);
			pageSave(result_pages, utf_name, &currentPage, resultBytes);
			free(utf_name);
		}
		pageSave(result_pages, "|", &currentPage, 1);

		FILETIME time;
		SYSTEMTIME timeHuman;
		char timeString[20];
		time.dwLowDateTime = (DWORD)files[j].creationTime;
		time.dwHighDateTime = (DWORD)(files[j].creationTime >> 32);
		FileTimeToSystemTime(&time, &timeHuman);
		sprintf_s(timeString, 20, "%04d-%02d-%02d %02d:%02d:%02d", timeHuman.wYear, timeHuman.wMonth, timeHuman.wDay, timeHuman.wHour, timeHuman.wMinute, timeHuman.wSecond);
		pageSave(result_pages, timeString, &currentPage, 19);
		pageSave(result_pages, "|", &currentPage, 1);
		time.dwLowDateTime = (DWORD)files[j].modificationTime;
		time.dwHighDateTime = (DWORD)(files[j].modificationTime >> 32);
		FileTimeToSystemTime(&time, &timeHuman);
		sprintf_s(timeString, 20, "%04d-%02d-%02d %02d:%02d:%02d", timeHuman.wYear, timeHuman.wMonth, timeHuman.wDay, timeHuman.wHour, timeHuman.wMinute, timeHuman.wSecond);
		pageSave(result_pages, timeString, &currentPage, 19);
		pageSave(result_pages, "|", &currentPage, 1);
		time.dwLowDateTime = (DWORD)files[j].metadataTime;
		time.dwHighDateTime = (DWORD)(files[j].metadataTime >> 32);
		FileTimeToSystemTime(&time, &timeHuman);
		sprintf_s(timeString, 20, "%04d-%02d-%02d %02d:%02d:%02d", timeHuman.wYear, timeHuman.wMonth, timeHuman.wDay, timeHuman.wHour, timeHuman.wMinute, timeHuman.wSecond);
		pageSave(result_pages, timeString, &currentPage, 19);
		pageSave(result_pages, "|", &currentPage, 1);
		time.dwLowDateTime = (DWORD)files[j].accesTime;
		time.dwHighDateTime = (DWORD)(files[j].accesTime >> 32);
		FileTimeToSystemTime(&time, &timeHuman);
		sprintf_s(timeString, 20, "%04d-%02d-%02d %02d:%02d:%02d", timeHuman.wYear, timeHuman.wMonth, timeHuman.wDay, timeHuman.wHour, timeHuman.wMinute, timeHuman.wSecond);
		pageSave(result_pages, timeString, &currentPage, 19);
		pageSave(result_pages, "|", &currentPage, 1);
		time.dwLowDateTime = (DWORD)files[j].FNcreationTime;
		time.dwHighDateTime = (DWORD)(files[j].FNcreationTime >> 32);
		FileTimeToSystemTime(&time, &timeHuman);
		sprintf_s(timeString, 20, "%04d-%02d-%02d %02d:%02d:%02d", timeHuman.wYear, timeHuman.wMonth, timeHuman.wDay, timeHuman.wHour, timeHuman.wMinute, timeHuman.wSecond);
		pageSave(result_pages, timeString, &currentPage, 19);
		pageSave(result_pages, "|", &currentPage, 1);
		time.dwLowDateTime = (DWORD)files[j].FNmodificationTime;
		time.dwHighDateTime = (DWORD)(files[j].FNmodificationTime >> 32);
		FileTimeToSystemTime(&time, &timeHuman);
		sprintf_s(timeString, 20, "%04d-%02d-%02d %02d:%02d:%02d", timeHuman.wYear, timeHuman.wMonth, timeHuman.wDay, timeHuman.wHour, timeHuman.wMinute, timeHuman.wSecond);
		pageSave(result_pages, timeString, &currentPage, 19);
		pageSave(result_pages, "|", &currentPage, 1);
		time.dwLowDateTime = (DWORD)files[j].FNmetadataTime;
		time.dwHighDateTime = (DWORD)(files[j].FNmetadataTime >> 32);
		FileTimeToSystemTime(&time, &timeHuman);
		sprintf_s(timeString, 20, "%04d-%02d-%02d %02d:%02d:%02d", timeHuman.wYear, timeHuman.wMonth, timeHuman.wDay, timeHuman.wHour, timeHuman.wMinute, timeHuman.wSecond);
		pageSave(result_pages, timeString, &currentPage, 19);
		pageSave(result_pages, "|", &currentPage, 1);
		time.dwLowDateTime = (DWORD)files[j].FNaccesTime;
		time.dwHighDateTime = (DWORD)(files[j].FNaccesTime >> 32);
		FileTimeToSystemTime(&time, &timeHuman);
		sprintf_s(timeString, 20, "%04d-%02d-%02d %02d:%02d:%02d", timeHuman.wYear, timeHuman.wMonth, timeHuman.wDay, timeHuman.wHour, timeHuman.wMinute, timeHuman.wSecond);
		pageSave(result_pages, timeString, &currentPage, 19);
		pageSave(result_pages, "\n", &currentPage, 1);
	}

	FILE* mft_res_file_pointer;
	errno_t err2 = fopen_s(&mft_res_file_pointer, resultFile, "w+b");
	if (err2 == 0 && mft_res_file_pointer != 0)
	{
		for (int pageNumber = 0; pageNumber <= currentPage; pageNumber++)
		{
			fwrite(result_pages[pageNumber].fileContent, 1, result_pages[pageNumber].occupiedSpace, mft_res_file_pointer);
		}
		fclose(mft_res_file_pointer);
	}

exitMFTTranscode:
	if (mft_file_pointer != NULL)
	{
		fclose(mft_file_pointer);
	}
	if (ownsMFTBuffer)
	{
		free(mft_file_contents);
	}
	free(files);
	free(folders);
	if (result_pages != NULL && currentPage >= 0)
	{
		for (int pageNumber = 0; pageNumber <= currentPage; pageNumber++)
		{
			free(result_pages[pageNumber].fileContent);
		}
	}
	free(result_pages);
	free(start);
}
