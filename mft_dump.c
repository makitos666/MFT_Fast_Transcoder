#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <windows.h>
#include "mft.h"

#define MFTFileRecordSize 1024
#define $DATA 0x80
#define $LastAttribute 0xFFFFFFFF
#define MAXAttributes 18
#define bootSectorSize 512

static void* allocateMemory(uint64_t size);
static void checkLetter(char* letter);
static void openRawDisk(DiskInformation* diskInformation);
static void readDriveBootSector(DiskInformation* diskInformation);
static void readFistMFTFileRecord(MemoryFile* MFTMemoryFile, DiskInformation* diskInformation);
static NonResidentAttributeHeader* getDataAttribute(unsigned char* MFTFile);
static RunHeader* getFirstMFTDataRun(MemoryFile* MFTMemoryFile, DiskInformation* diskInformation);
static void readMFTFileData(MemoryFile* MFTMemoryFile, DiskInformation* diskInformation, RunHeader* currentMFTDataRun);

MemoryFile* MFTDump(char* driveLetter)
{
	DiskInformation* diskInformation;
	MemoryFile* MFTMemoryFile;
	RunHeader* currentMFTDataRun;

	checkLetter(driveLetter);

	diskInformation = (DiskInformation*)allocateMemory(sizeof(DiskInformation));
	diskInformation->driveLetter = driveLetter;
	diskInformation->MFTSize = 0;

	MFTMemoryFile = (MemoryFile*)allocateMemory(sizeof(MemoryFile));
	MFTMemoryFile->fileContent = (unsigned char*)allocateMemory(MFTFileRecordSize);
	MFTMemoryFile->size = 0;

	openRawDisk(diskInformation);
	readDriveBootSector(diskInformation);
	readFistMFTFileRecord(MFTMemoryFile, diskInformation);
	currentMFTDataRun = getFirstMFTDataRun(MFTMemoryFile, diskInformation);
	readMFTFileData(MFTMemoryFile, diskInformation, currentMFTDataRun);

	CloseHandle(diskInformation->handler);
	free(diskInformation->bootSector);
	free(diskInformation);
	return MFTMemoryFile;
}

static void* allocateMemory(uint64_t size)
{
	void* var = malloc((size_t)size);
	if (var == NULL)
	{
		printf("Error allocating memory\n");
		exit(1);
	}
	return var;
}

static void checkLetter(char* letter)
{
	int isValid = 0;
	unsigned char* letterChar;

	if (strlen(letter) != 1)
	{
		printf("Error: Invalid input letter\r\n");
		exit(1);
	}

	letterChar = (unsigned char*)letter;
	if (*letterChar >= 'A' && *letterChar <= 'Z')
	{
		isValid = 1;
	}
	else if (*letterChar >= 'a' && *letterChar <= 'z')
	{
		*letterChar = *letterChar - 32;
		isValid = 1;
	}
	else
	{
		isValid = 0;
	}

	if (!isValid)
	{
		printf("Error: Invalid input letter\r\n");
		exit(1);
	}
}

static void openRawDisk(DiskInformation* diskInformation)
{
	char* driveName = (char*)allocateMemory(7);
	sprintf_s(driveName, 7, "\\\\.\\%s:", diskInformation->driveLetter);

	diskInformation->handler = CreateFileA(driveName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (diskInformation->handler == INVALID_HANDLE_VALUE)
	{
		printf("Error: can not open drive. Maybe incorrect letter or maybe you have not sufficient privileges.\r\n");
		exit(1);
	}
	free(driveName);
}

static void readDriveBootSector(DiskInformation* diskInformation)
{
	DWORD bytesAccessed;
	int readFailed;

	diskInformation->bootSector = (BootSector*)allocateMemory(sizeof(BootSector));
	SetFilePointer(diskInformation->handler, 0, 0, FILE_BEGIN);

	readFailed = ReadFile(
		diskInformation->handler,
		diskInformation->bootSector,
		bootSectorSize,
		&bytesAccessed,
		NULL
	);

	if (!readFailed || bytesAccessed != bootSectorSize)
	{
		printf("Error: can not read boot sector from drive.\r\n");
		exit(1);
	}
	if (diskInformation->bootSector->bytesPerSector == 0 || diskInformation->bootSector->sectorsPerCluster == 0)
	{
		printf("Error: invalid NTFS boot sector geometry.\r\n");
		exit(1);
	}

	printf("INFO: Boot sector successfully read.\r\n");
}

static void readFistMFTFileRecord(MemoryFile* MFTMemoryFile, DiskInformation* diskInformation)
{
	int readFailed;
	uint64_t MFTLocation;
	DWORD bytesAccessed;

	diskInformation->bytesPerCluster =
		(uint64_t)(diskInformation->bootSector->bytesPerSector) *
		(uint64_t)(diskInformation->bootSector->sectorsPerCluster);

	MFTLocation =
		(diskInformation->bootSector->MFTFirstClusterLocation) *
		(diskInformation->bytesPerCluster);

	LONG MFTLocation_low = (LONG)(MFTLocation & 0xFFFFFFFF);
	LONG MFTLocation_high = (LONG)(MFTLocation >> 32);
	SetFilePointer(diskInformation->handler, MFTLocation_low, &MFTLocation_high, FILE_BEGIN);

	readFailed = ReadFile(
		diskInformation->handler,
		&MFTMemoryFile->fileContent[MFTMemoryFile->size],
		MFTFileRecordSize,
		&bytesAccessed,
		NULL
	);

	if (!readFailed || bytesAccessed != MFTFileRecordSize)
	{
		printf("Error: can not read from drive.\r\n");
		exit(1);
	}
}

static RunHeader* getFirstMFTDataRun(MemoryFile* MFTMemoryFile, DiskInformation* diskInformation)
{
	RunHeader* firstMFTDataRun;

	diskInformation->MFTDataAttribute = getDataAttribute(MFTMemoryFile->fileContent);
	diskInformation->MFTSize += diskInformation->MFTDataAttribute->attributeAllocated;

	MFTMemoryFile->fileContent =
		(unsigned char*)realloc(MFTMemoryFile->fileContent, diskInformation->MFTSize);

	if (MFTMemoryFile->fileContent == NULL)
	{
		printf("Error: Can not re-allocate memory for MFT File.\n");
		exit(1);
	}

	firstMFTDataRun = (RunHeader*)((uint8_t*)diskInformation->MFTDataAttribute + diskInformation->MFTDataAttribute->dataRunsOffset);
	return firstMFTDataRun;
}

static NonResidentAttributeHeader* getDataAttribute(unsigned char* MFTFile)
{
	FileRecordHeader* MFTFileRecord;
	AttributeHeader* attribute;
	int currentAttribute = 0;
	size_t nextAttributeOffset;
	size_t maxAttributeOffset = (size_t)(MFTFile + MFTFileRecordSize);

	MFTFileRecord = (FileRecordHeader*)MFTFile;
	attribute = (AttributeHeader*)(MFTFile + MFTFileRecord->firstAttributeOffset);

	while (1)
	{
		if (attribute->attributeType == $DATA)
		{
			return (NonResidentAttributeHeader*)attribute;
		}
		else if (attribute->attributeType == $LastAttribute)
		{
			printf("Error: can not parse Data Attribute, $DATA not found\r\n");
			exit(1);
		}

		nextAttributeOffset = (size_t)((uint8_t*)attribute + attribute->length);
		if (nextAttributeOffset > maxAttributeOffset)
		{
			printf("Error: can not parse Data Attribute, inconsistent headers\r\n");
			exit(1);
		}

		attribute = (AttributeHeader*)(nextAttributeOffset);
		currentAttribute++;
		if (currentAttribute > MAXAttributes)
		{
			printf("Error: can not parse Data Attribute, inconsistent headers\r\n");
			exit(1);
		}
	}
}

static void readMFTFileData(MemoryFile* MFTMemoryFile, DiskInformation* diskInformation, RunHeader* currentMFTDataRun)
{
	int read;
	DWORD bytesAccessed;
	LONG MFTLocation_low, MFTLocation_high;
	uint32_t currentDataRunOffset = (uint32_t)((uint8_t*)currentMFTDataRun - (uint8_t*)(diskInformation->MFTDataAttribute));
	uint64_t clusterNumber = 0;

	while (currentDataRunOffset < diskInformation->MFTDataAttribute->attributeHeader.length && currentMFTDataRun->lengthFieldBytes)
	{
		uint64_t length = 0, offset = 0, from, count;

		for (int i = 0; i < currentMFTDataRun->lengthFieldBytes; i++)
		{
			length |= (uint64_t)(((uint8_t*)currentMFTDataRun)[1 + i]) << (i * 8);
		}

		for (int i = 0; i < currentMFTDataRun->offsetFieldBytes; i++)
		{
			offset |= (uint64_t)(((uint8_t*)currentMFTDataRun)[1 + currentMFTDataRun->lengthFieldBytes + i]) << (i * 8);
		}

		if (offset & ((uint64_t)1 << (currentMFTDataRun->offsetFieldBytes * 8 - 1)))
		{
			for (int i = currentMFTDataRun->offsetFieldBytes; i < 8; i++)
			{
				offset |= (uint64_t)0xFF << (i * 8);
			}
		}

		currentMFTDataRun = (RunHeader*)((uint8_t*)currentMFTDataRun + 1 + currentMFTDataRun->lengthFieldBytes +
			currentMFTDataRun->offsetFieldBytes);
		currentDataRunOffset = (uint32_t)((uint8_t*)currentMFTDataRun - (uint8_t*)(diskInformation->MFTDataAttribute));

		clusterNumber += offset;
		from = clusterNumber * (diskInformation->bytesPerCluster);
		count = length * (diskInformation->bytesPerCluster);

		MFTLocation_low = (LONG)(from & 0xFFFFFFFF);
		MFTLocation_high = (LONG)(from >> 32);
		SetFilePointer(diskInformation->handler, MFTLocation_low, &MFTLocation_high, FILE_BEGIN);
		if ((MFTMemoryFile->size + count) > diskInformation->MFTSize)
		{
			printf("Error: Real size seems to be greater than headers defined.\r\n");
			exit(1);
		}

		uint64_t remaining = count;
		while (remaining > 0)
		{
			DWORD chunk = (remaining > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (DWORD)remaining;
			read = ReadFile(diskInformation->handler, &MFTMemoryFile->fileContent[MFTMemoryFile->size + (count - remaining)], chunk, &bytesAccessed, NULL);
			if (read == 0 || bytesAccessed != chunk)
			{
				printf("Error: Cannot read MFT DataRun from drive offset.\r\n");
				exit(1);
			}
			remaining -= chunk;
		}
		MFTMemoryFile->size += count;
	}

	if (MFTMemoryFile->size == diskInformation->MFTSize)
	{
		printf("INFO: MFT File read successfully.\r\n");
	}
	else
	{
		printf("Warning: Stored MFT size and read size differs.\r\n");
	}
}
