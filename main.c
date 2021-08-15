#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <windows.h>

// Some structs come from: https://handmade.network/wiki/7002-tutorial_parsing_the_mft
#pragma pack(push,1)
typedef struct {
	uint32_t    magic;
	uint16_t    updateSequenceOffset;
	uint16_t    updateSequenceSize;
	uint64_t    logSequence;
	uint16_t    sequenceNumber;
	uint16_t    hardLinkCount;
	uint16_t    firstAttributeOffset;
	uint16_t    inUse : 1;
	uint16_t    isDirectory : 1;
	uint32_t    usedSize;
	uint32_t    allocatedSize;
	uint64_t    fileReference;
	uint16_t    nextAttributeID;
	uint16_t    unused;
	uint32_t    recordNumber;
} FileRecordHeader;
typedef struct {
	uint32_t    attributeType;
	uint32_t    length;
	uint8_t     nonResident;
	uint8_t     nameLength;
	uint16_t    nameOffset;
	uint16_t    flags;
	uint16_t    attributeID;
} AttributeHeader;
typedef struct {
	AttributeHeader attributeHeader;
	uint64_t    firstCluster;
	uint64_t    lastCluster;
	uint16_t    dataRunsOffset;
	uint16_t    compressionUnit;
	uint32_t    unused;
	uint64_t    attributeAllocated;
	uint64_t    attributeSize;
	uint64_t    streamDataSize;
} NonResidentAttributeHeader;
typedef struct {
	AttributeHeader attributeHeader;
	uint32_t    attributeLength;
	uint16_t    attributeOffset;
	uint8_t     indexed;
	uint8_t     unused;
} ResidentAttributeHeader;
typedef struct {
	uint8_t     lengthFieldBytes : 4;
	uint8_t     offsetFieldBytes : 4;
} RunHeader;
typedef struct {
	ResidentAttributeHeader residentAttributeHeader;
	uint64_t    parentRecordNumber : 48;  
	uint64_t    sequenceNumber : 16;
	uint64_t    creationTime;
	uint64_t    modificationTime;
	uint64_t    metadataModificationTime;  // refered to the MFT entry
	uint64_t    readTime; // same as last acces time
	uint64_t    allocatedSize;
	uint64_t    realSize;
	uint32_t    flags;
	uint32_t    repase;
	uint8_t     fileNameLength;        
	uint8_t     namespaceType;     
	wchar_t     fileName[1]; 
} FileNameAttributeHeader;
typedef struct {
	uint64_t	recordNumber;
	uint64_t	parentRecordNumber;
	wchar_t*	filename;
	uint8_t     fileNameLength;
	uint64_t	isDirectory;
	uint64_t    creationTime;
	uint64_t    modificationTime;
	uint64_t    metadataTime;
	uint64_t    accesTime;
	uint64_t    FNcreationTime;
	uint64_t    FNmodificationTime;
	uint64_t    FNmetadataTime;
	uint64_t    FNaccesTime;
} FilesAndFolders;
typedef struct {
	ResidentAttributeHeader residentAttributeHeader;
	uint64_t	fileCreationTime;
	uint64_t	fileAlteredTime;
	uint64_t	changedTime; // refered to the MFT entry
	uint64_t	fileReadTime; // same as last acces time
	uint32_t	permissions;
} StandardInformation;
typedef struct {
	uint8_t     jump[3];
	char        name[8];
	uint16_t    bytesPerSector;
	uint8_t     sectorsPerCluster;
	uint16_t    reservedSectors;
	uint8_t     unused0[3];
	uint16_t    unused1;
	uint8_t     media;
	uint16_t    unused2;
	uint16_t    sectorsPerTrack;
	uint16_t    headsPerCylinder;
	uint32_t    hiddenSectors;
	uint32_t    unused3;
	uint32_t    unused4;
	uint64_t    totalSectors;
	uint64_t    mftStart;
	uint64_t    mftMirrorStart;
	uint32_t    clustersPerFileRecord;
	uint32_t    clustersPerIndexBlock;
	uint64_t    serialNumber;
	uint32_t    checksum;
	uint8_t     bootloader[426];
	uint16_t    bootSignature;
} BootSector;
#pragma pack(pop)

typedef struct {
	unsigned char* fileContent;
	size_t size;
} memoryFile;

typedef struct {
	unsigned char* fileContent;
	size_t totalSpace;
	size_t occupiedSpace;
} paginationStruct;

inline void pageSave(paginationStruct* result_pages, char* whatToSave, int* currentPage, size_t size) {
	if (result_pages[*currentPage].occupiedSpace + size > result_pages[*currentPage].totalSpace) { //no more space in this page, create a new one
		if (*currentPage == 1024) {
			printf("This MFT is ridiculously big or there is any other BUG. Open an issue ^^");
			exit(1);
		}
		*currentPage = (*currentPage + 1);
		unsigned char* page = (unsigned char*)malloc(100000000);
		if (page == NULL) {
			printf("Not enough memory");
			exit(1);
		}
		result_pages[*currentPage].fileContent = page;
		result_pages[*currentPage].totalSpace = 100000000;
		result_pages[*currentPage].occupiedSpace = 0;
	}
	memcpy_s(&result_pages[*currentPage].fileContent[result_pages[*currentPage].occupiedSpace], size, whatToSave, size);
	result_pages[*currentPage].occupiedSpace += size;
}

memoryFile* MFTDump(wchar_t* disk, char* destination, int toFile) {
	HANDLE drive;
	DWORD bytesAccessed;
	int read;
	int MFT_FILE_SIZE = 1024;
	uint64_t MFTTotalSize = MFT_FILE_SIZE, mftfile_size = 0, clusterNumber = 0, recordsProcessed = 0, bytesPerCluster = 0;
	BootSector bootSector;
	FILE* MFTFile;
	LPSTR messageBuffer = NULL;
	size_t writtenBytes;
	LONG lpDistanceToMoveHigh;
	memoryFile* MFTStruct = (memoryFile*)malloc(sizeof(memoryFile));
	wchar_t* wszDrive = (wchar_t*) malloc(14);
	FileRecordHeader* fileRecord = (FileRecordHeader*) malloc(sizeof(FileRecordHeader));
	FileRecordHeader* fileRecordFree = fileRecord;
	AttributeHeader* attribute = (AttributeHeader*) malloc(sizeof(AttributeHeader));
	AttributeHeader* attributeFree = attribute;
	NonResidentAttributeHeader* dataAttribute = (NonResidentAttributeHeader*) malloc(sizeof(NonResidentAttributeHeader));
	NonResidentAttributeHeader* dataAttributeFree = dataAttribute;
	unsigned char* mftFile = (unsigned char*) malloc(MFT_FILE_SIZE);
	RunHeader* dataRun = (RunHeader*) malloc(sizeof(RunHeader));
	RunHeader* dataRunFree = dataRun;

	if (MFTStruct == NULL || wszDrive == NULL || fileRecord == NULL || attribute == NULL || dataAttribute == NULL || mftFile == NULL || dataRun == NULL) {
		printf("Unexpected error allocating memory");
		goto exit;
	}

	// Open RAW device
	swprintf(wszDrive, 7, L"\\\\.\\%ls:", disk);
	drive = CreateFile(wszDrive, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (drive == INVALID_HANDLE_VALUE) {
		printf("Error accessing Drive. Maybe incorrect letter maybe you have not sufficient privileges.\n");
		goto exit;
	}
	
	if (toFile == 1) {
		errno_t err;
		// Open and check permisions for results file
		err = fopen_s(&MFTFile, destination, "w+b");
		if (err != 0) {
			char* errorMSG = (char *) malloc(1024);
			if (errorMSG != NULL) {
				strerror_s(errorMSG, 256, err);
				printf("Error creating output file: %s\n", errorMSG);
				free(errorMSG);
			}
			printf("Unexpected error in error\n");
			goto exit;
		}
	} else {
		MFTFile = NULL;
	}

	// Read boot sector
	SetFilePointer(drive, 0, 0, FILE_BEGIN);
	read = ReadFile(drive, &bootSector, 512, &bytesAccessed, NULL);
	if (read == 0) {
		printf("Error reading from dirve.\n");
		goto exit;
	}

	// Calcualte and read first MFT FILE record
	bytesPerCluster = (uint64_t)bootSector.bytesPerSector * (uint64_t)bootSector.sectorsPerCluster;
	lpDistanceToMoveHigh = (bootSector.mftStart * bytesPerCluster) >> 32;
	SetFilePointer(drive, (bootSector.mftStart * bytesPerCluster) & 0xFFFFFFFF, &lpDistanceToMoveHigh, FILE_BEGIN);
	read = ReadFile(drive, &mftFile[mftfile_size], MFT_FILE_SIZE, &bytesAccessed, NULL);
	mftfile_size += MFT_FILE_SIZE;
	if (read == 0) {
		printf("Error reading from dirve.\n");
		goto exit;
	}
	
	fileRecord = (FileRecordHeader*)mftFile;
	attribute = (AttributeHeader*)(mftFile + fileRecord->firstAttributeOffset);
	
	while (1) {
		if (attribute->attributeType == 0x80) {
			dataAttribute = (NonResidentAttributeHeader*)attribute;
			MFTTotalSize += dataAttribute->attributeAllocated; // This attribute should be the real MFT size
			unsigned char* mftFile2 = realloc(mftFile, MFTTotalSize);
			if (mftFile2 == NULL) {
				printf("Error re-allocating memory for MFT File.\n");
				goto exit;
			} else {
				mftFile = mftFile2;
			}
		} else if (attribute->attributeType == 0xFFFFFFFF) {
			break;
		}
		attribute = (AttributeHeader*)((uint8_t*)attribute + attribute->length);
	}

	dataRun = (RunHeader*)((uint8_t*)dataAttribute + dataAttribute->dataRunsOffset);

	// Calculate and read the rest of the MFT
	// Thanks for this piece of code to: https://handmade.network/wiki/7002-tutorial_parsing_the_mft
	while (((uint8_t*)dataRun - (uint8_t*)dataAttribute) < dataAttribute->attributeHeader.length && dataRun->lengthFieldBytes) {
		uint64_t length = 0, offset = 0, filesRemaining, positionInBlock = 0, from, count;

		for (int i = 0; i < dataRun->lengthFieldBytes; i++) {
			length |= (uint64_t)(((uint8_t*)dataRun)[1 + i]) << (i * 8);
		}
		for (int i = 0; i < dataRun->offsetFieldBytes; i++) {
			offset |= (uint64_t)(((uint8_t*)dataRun)[1 + dataRun->lengthFieldBytes + i]) << (i * 8);
		}
		if (offset & ((uint64_t)1 << (dataRun->offsetFieldBytes * 8 - 1))) {
			for (int i = dataRun->offsetFieldBytes; i < 8; i++) {
				offset |= (uint64_t)0xFF << (i * 8);
			}
		}

		clusterNumber += offset;
		dataRun = (RunHeader*)((uint8_t*)dataRun + 1 + dataRun->lengthFieldBytes + dataRun->offsetFieldBytes);

		filesRemaining = length * bytesPerCluster / MFT_FILE_SIZE;
		from = clusterNumber * bytesPerCluster;
		count = filesRemaining * MFT_FILE_SIZE;

		lpDistanceToMoveHigh = from >> 32;
		SetFilePointer(drive, from & 0xFFFFFFFF, &lpDistanceToMoveHigh, FILE_BEGIN);

		if ((mftfile_size + count) > MFTTotalSize) { // We only allocated MFTTotalSize bytes. If MFT headers are wrong the program will fail
			printf("Unexpected error allocating memory.\n");
			goto exit;
		}
		read = ReadFile(drive, &mftFile[mftfile_size], (DWORD)count, &bytesAccessed, NULL); // ReadFile also checks if output buffer is big enough
		// It's also OK to convert 64 bit count to 32 bit in ReadFile. I think that is impossible to get >4GB chuck of MFT.   
		if (read == 0) {
			printf("Error reading from dirve.\n");
			goto exit;
		}

		mftfile_size += count;
	}

	// Write the MFT to destination file
	if (toFile == 1) {
		if (MFTFile != NULL) {
			writtenBytes = fwrite(mftFile, 1, mftfile_size, MFTFile); // No need to check if mftfile_size is bigger than mftFile allocation because ReadFile has already checked it
			if (writtenBytes != mftfile_size) {
				printf("Error writting to output file.\n");
				fclose(MFTFile);
				goto exit;
			}
			fclose(MFTFile);
		} else {
			printf("Error writting to output file, file handle closed unexpectedly.\n");
			goto exit;
		}
		printf("MFT file dumped correctly to: \"%s\".\n", destination);
	}
	// Return de MFT memory file to transcode if necessary
	MFTStruct->fileContent = mftFile;
	MFTStruct->size = mftfile_size;

	free(wszDrive);
	free(fileRecordFree);
	free(attributeFree);
	free(dataAttributeFree);
	free(dataRunFree);

	return MFTStruct;

exit:
	free(MFTStruct);
	free(wszDrive);
	free(fileRecordFree);
	free(attributeFree);
	free(dataAttributeFree);
	free(mftFile);
	free(dataRunFree);

	exit(1);
}

void MFTTranscode(void* _MFTFilePath, char* resultFile, int inputType, wchar_t* letter) {
	FILE* mft_file_pointer;
	size_t res_size = 0;
	size_t maxFiles = 0;
	size_t fsize;
	unsigned char* mft_file_contents;
	char* errorMSG = (char*)malloc(1024);
	uint64_t fileCount = 0;
	FilesAndFolders* files; 
	FilesAndFolders* folders;

	// I'm reusing _MFTFilePath to input both filepath or memoryFile
	// Because Dump&Transcode functionality
	if (inputType == 0) { // Read from file to memory
		errno_t err;
		char* MFTFilePath;
		MFTFilePath = (char*)_MFTFilePath;
		err = fopen_s(&mft_file_pointer, MFTFilePath, "rb");
		if (err != 0) {
			strerror_s(errorMSG, 256, err);
			printf("Error reading MFT File: %s\n", errorMSG);
			exit(6);
		}
		free(errorMSG);
		fseek(mft_file_pointer, 0, SEEK_END);
		fsize = ftell(mft_file_pointer);
		if (fsize == 0) {
			printf("Empty input file.\n");
			exit(6);
		}
		fseek(mft_file_pointer, 0, SEEK_SET);
		mft_file_contents = (char*) malloc((size_t)fsize + 1);
		if (mft_file_contents == NULL) {
			printf("Not enough RAM to load MFT File.\n");
			exit(6);
		}
		size_t readItems = fread_s(mft_file_contents, fsize, fsize, 1, mft_file_pointer);
		if (readItems != 1) {
			printf("Failed to read from MFT File.\n");
			exit(6);
		}
		fclose(mft_file_pointer);
	} else if (inputType == 1) { // Retrieve object from dump
		memoryFile* file = (memoryFile*)_MFTFilePath;
		mft_file_contents = file->fileContent;
		fsize = file->size;
	} else {
		printf("Unexpected behaviour.");
		exit(12);
	}

	maxFiles = (fsize / 1024) * 2; // MFT entries are 1024 bytes in size. One File or Directory per entry.
	size_t mallocSize = maxFiles * sizeof(FilesAndFolders);
	files = (FilesAndFolders*) malloc(mallocSize);
	folders = (FilesAndFolders*) malloc(mallocSize);
	// We are allocating a hundreds of megabytes
	if (files == NULL || folders == NULL || maxFiles < 5) { //5 is the minimum number of elements in a MFT
		printf("Not enough RAM");
		exit(1);
	}
	memset(files, 0, mallocSize);
	memset(folders, 0, mallocSize);

	// Some sanity checks.
	int magicNumberCheck = memcmp(mft_file_contents, "FILE", 4);
	if (magicNumberCheck != 0 || fsize < 1024) {
		printf("Invalid MFT file");
		exit(1);
	}

	// MFT entries are 1024 bytes in size.
	for (int o = 1024; o < fsize; o += 1024) {
		FileRecordHeader* fileRecord = (FileRecordHeader*)(&mft_file_contents[o]);

		if (!fileRecord->inUse) {
			continue; // Entries are reusable, usually headers are erased.
		}

		// NTFS fixup. Every 512 bytes there is 1 byte checksum (and the original value are before)
		// It seems a technique to know if the entry are OK or corrupted (for example, due a unterminated write)
		// Thanks to: https://github.com/dkovar/analyzeMFT
		uint64_t seq_number = *(uint16_t*)((uint64_t)fileRecord + 48);
		if (seq_number == *(uint16_t*)((uint64_t)fileRecord + 510) && seq_number == *(uint16_t*)((uint64_t)fileRecord + 1022)) {
			uint16_t seq_attr1, seq_attr2 = 0;
			if (fileRecord->updateSequenceOffset == 42) {
				seq_attr1 = *(uint16_t*)((uint64_t)fileRecord + 44);
				seq_attr2 = *(uint16_t*)((uint64_t)fileRecord + 46);
			} else {
				seq_attr1 = *(uint16_t*)((uint64_t)fileRecord + 50);
				seq_attr2 = *(uint16_t*)((uint64_t)fileRecord + 52);
			}
			memcpy_s((void*)((uint64_t)fileRecord + 510), 2, &seq_attr1, 2);
			memcpy_s((void*)((uint64_t)fileRecord + 1022), 2, &seq_attr2, 2);
		}

		StandardInformation* standardInformation = NULL;

		// Sometimes, 1024 bytes are not enough. In this cases the attributes are separated into multiple MFT entries.
		uint64_t fileReference = fileRecord->fileReference;
		while (fileReference != 0) { // It's a loop because it can be linked multiple times as storage needs increases.
			// I'm not sure about the following line. But so far it has worked.
			// fileReference & 0x0000FFFFFFFFFFFF stores the FILE record number to the base FILE record (https://flatcap.org/linux-ntfs/ntfs/concepts/file_record.html)
			// I'm assuming that the FILE record number are consecutive. Then I can access them by multiplying by entry size (1024)
			FileRecordHeader* fileRecord2 = (FileRecordHeader*)(&mft_file_contents[(fileReference & 0x0000FFFFFFFFFFFF) * 1024]);
			AttributeHeader* attribute2 = (AttributeHeader*)((uint64_t)fileRecord2 + fileRecord2->firstAttributeOffset);
			while ((uint8_t*)attribute2 - (uint8_t*)fileRecord2 < 1024) {
				if (attribute2->attributeType == 0x10) {
					standardInformation = (StandardInformation*)attribute2;
					break;
				} else if (attribute2->attributeType == 0xFFFFFFFF) {
					break;
				}
				attribute2 = (AttributeHeader*)((uint64_t)attribute2 + attribute2->length);
			}
			fileReference = fileRecord2->fileReference;
		}
		AttributeHeader* attribute = (AttributeHeader*)((uint64_t)fileRecord + fileRecord->firstAttributeOffset);

		while ((uint8_t*)attribute - (uint8_t*)fileRecord < 1024) {
			// Here we loop between atributes
			// From what I've seen so far, the attibutes are stores with incremental and optional attributeType.
			// That means that if the first attribute is 0x30, there is no 0x10 or 0x20
			if (attribute->attributeType == 0x30) { // 0x30 $FILE_NAME
				if (standardInformation == NULL) { 
					// In some extremely rare cases, the attributeType 0x10 could be missing. 
					// Filling with zeros will prevent the program from crashing, but FileTimes will be incorrect.
					standardInformation = calloc(1, sizeof(StandardInformation));
				}
				FileNameAttributeHeader* fileNameAttribute = (FileNameAttributeHeader*)attribute;
				if (!fileNameAttribute->residentAttributeHeader.attributeHeader.nonResident) {
					if (fileRecord->isDirectory) {
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
					} else {
						if (fileNameAttribute->namespaceType != 2) {
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
			else if (attribute->attributeType == 0x10) {
				standardInformation = (StandardInformation*)attribute;
			}
			else if (attribute->attributeType == 0xFFFFFFFF) {
				break;
			}
			attribute = (AttributeHeader*)((uint64_t)attribute + attribute->length);
		}
	}

	int currentPage = 0;
	paginationStruct* result_pages = (paginationStruct *) malloc(sizeof(paginationStruct)*1024); //1024 pages will be far far far enough
	uint64_t aproxBytes = fileCount * (250 + 168); // I'm assuming an avereage of 250 characters per file/folder path and the rest of the parameters are 168 characters lenght
	unsigned char* page = (unsigned char*)malloc(aproxBytes); // because we are going to track the occupied size, trhere is no need to init this space
	if (page == NULL) {
		printf("Not enough memory");
		exit(1);
	}
	result_pages[currentPage].fileContent = page;
	result_pages[currentPage].totalSpace = aproxBytes;
	result_pages[currentPage].occupiedSpace = 0;

	char* header = "File or Folder|File|$STDINFO Creation Time|$STDINFO Modification Time|$STDINFO Metadata change Time|$STDINFO Access Time|$FILENAME Creation Time|$FILENAME Modification Time|$FILENAME Entry Modified Time|$FILENAME Access Time\n";
	pageSave(result_pages, header, &currentPage, 225);

	wchar_t* start = (wchar_t*)malloc(3 * sizeof(wchar_t));
	memcpy_s(start, 2, letter, 2);
	memcpy_s(&start[1], 2, ":", 2);
	memcpy_s(&start[2], 2, "\x0\x0", 2);
	folders[5].filename = start;
	folders[5].fileNameLength = 2;

	size_t cnt = 0;
	while (cnt <= maxFiles) {
		if (folders[cnt].filename != NULL) {
			pageSave(result_pages, "Folder|", &currentPage, 7);
			uint64_t stack[200];
			int top = -1;
			uint64_t rnumber, pnumber;
			rnumber = folders[cnt].recordNumber;
			pnumber = folders[cnt].parentRecordNumber;
			top = top + 1;
			stack[top] = rnumber;
			top = top + 1;
			stack[top] = pnumber;
			while (rnumber != pnumber) {
				rnumber = folders[pnumber].recordNumber;
				pnumber = folders[pnumber].parentRecordNumber;
				top = top + 1;
				stack[top] = rnumber;
				top = top + 1;
				stack[top] = pnumber;
			}
			while (top != -1) {
				pnumber = stack[top];
				top = top - 1;
				rnumber = stack[top];
				top = top - 1;
				if (rnumber != 5) {
					char* utf_name = (char*)malloc((size_t)folders[pnumber].fileNameLength + 1);
					// If you know better way to convert from UTF16 to UTF8 I'm all ears
					size_t resultBytes = WideCharToMultiByte(CP_UTF8, WC_NO_BEST_FIT_CHARS, folders[pnumber].filename, folders[pnumber].fileNameLength, utf_name, folders[pnumber].fileNameLength, NULL, NULL);
					pageSave(result_pages, utf_name, &currentPage, resultBytes);
					pageSave(result_pages, "\\", &currentPage, 1);
				}
			}

			char* utf_name = (char*)malloc((size_t)folders[cnt].fileNameLength + 1);;
			size_t resultBytes = WideCharToMultiByte(CP_UTF8, WC_NO_BEST_FIT_CHARS, folders[cnt].filename, folders[cnt].fileNameLength, utf_name, folders[cnt].fileNameLength, NULL, NULL);
			pageSave(result_pages, utf_name, &currentPage, resultBytes);
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

	for (int j = 0; j < fileCount; j++) {
		pageSave(result_pages, "File|", &currentPage, 5);

		// Improvised shaky stack to rebuild file path.
		uint64_t stack[200];
		int top = -1;
		uint64_t rnumber, pnumber;
		rnumber = files[j].recordNumber;
		pnumber = files[j].parentRecordNumber;
		top = top + 1;
		stack[top] = rnumber;
		top = top + 1;
		stack[top] = pnumber;
		while (rnumber != pnumber) {
			rnumber = folders[pnumber].recordNumber;
			pnumber = folders[pnumber].parentRecordNumber;
			top = top + 1;
			stack[top] = rnumber;
			top = top + 1;
			stack[top] = pnumber;
		}
		while (top != -1) {
			pnumber = stack[top];
			top = top - 1;
			rnumber = stack[top];
			top = top - 1;
			if (rnumber != 5) {
				char* utf_name = (char*)malloc((size_t)folders[pnumber].fileNameLength + 1);
				// If you know better way to convert from UTF16 to UTF8 I'm all ears
				size_t resultBytes = WideCharToMultiByte(CP_UTF8, WC_NO_BEST_FIT_CHARS, folders[pnumber].filename, folders[pnumber].fileNameLength, utf_name, folders[pnumber].fileNameLength, NULL, NULL);
				pageSave(result_pages, utf_name, &currentPage, resultBytes);
				pageSave(result_pages, "\\", &currentPage, 1);
			}
		}

		char* utf_name = (char*)malloc((size_t)files[j].fileNameLength + 1);;
		size_t resultBytes = WideCharToMultiByte(CP_UTF8, WC_NO_BEST_FIT_CHARS, files[j].filename, files[j].fileNameLength, utf_name, files[j].fileNameLength, NULL, NULL);
		pageSave(result_pages, utf_name, &currentPage, resultBytes); 
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
	if (err2 == 0 && mft_res_file_pointer != 0) {
		for (int pageNumber = 0; pageNumber <= currentPage; pageNumber++) {
			fwrite(result_pages[pageNumber].fileContent, 1, result_pages[pageNumber].occupiedSpace, mft_res_file_pointer);
		}
		fclose(mft_res_file_pointer);
	}
}

int main(int argc, char* argv[])
{
	if (argc == 1) {
		printf("usage:\n\tmft.exe dump [Drive letter] [MFT Dump] \n\tDumps the MFT of the selected drive into the described file \n\n\ttranscode [MFT Dump] [CSV Result] \n\tProceses the MFT of the selected drive into a CSV to the described file\n\n\tDT [Drive letter][CSV Result] \n\tThe two other options together");
		exit(1);
	}

	if (strcmp("dump", argv[1]) == 0 && argc == 4) {
		if (strlen(argv[2]) == 1) {
			int letter = toupper(*argv[2]);
			if (letter >= 65 && letter <= 90) {
				wchar_t* letterW = malloc(4);
				if (letterW) {
					letterW[0] = (wchar_t)letter;
					letterW[1] = 0;
					memoryFile* MFTFile = MFTDump(letterW, argv[3], 1);
					free(MFTFile);
				} else {
					printf("Unexpected allocation error.");
				}
			} else {
				printf("Not a valid disk letter.");
			}
		} else {
			printf("Not a valid disk letter.");
		}
	} else if (strcmp("transcode", argv[1]) == 0 && argc == 4) {
		// I'm  not going to check if input params are a good path
		// because if later fopen_s opens them it's OK for me
		MFTTranscode(argv[2], argv[3], 0, (wchar_t*)L"?");
	} else if (strcmp("DT", argv[1]) == 0 && argc == 4) {
		if (strlen(argv[2]) == 1) {
			int letter = toupper(*argv[2]);
			if (letter >= 65 && letter <= 90) {
				wchar_t* letterW = malloc(4);
				if (letterW) {
					letterW[0] = (wchar_t)letter;
					letterW[1] = 0;
					memoryFile* MFTFile = MFTDump(letterW, NULL, 0);
					MFTTranscode(MFTFile, argv[3], 1, letterW);
					free(MFTFile);
				} else {
					printf("Unexpected allocation error.");
				}
			} else {
				printf("Not a valid disk letter.");
			}
		} else {
			printf("Not a valid disk letter.");
		}
	} else {
		printf("usage:\n\tmft.exe dump [Drive letter] [MFT Dump] \n\tDumps the MFT of the selected drive into the described file \n\n\ttranscode [MFT Dump] [CSV Result] \n\tProceses the MFT of the selected drive into a CSV to the described file\n\n\tDT [Drive letter][CSV Result] \n\tThe two other options together");
		exit(1);
	}
}
