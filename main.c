#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <windows.h>
#include "structs.c"

#define MFTFileRecordSize 1024
#define $DATA 0x80
#define $LastAttribute 0xFFFFFFFF
#define MAXAttributes 18
#define bootSectorSize 512

FILE* openResultFile(char* resultFileName);
RunHeader* getFirstMFTDataRun(MemoryFile* MFTMemoryFile, DiskInformation* diskInformation);
MemoryFile* MFTDump(char* driveLetter);
NonResidentAttributeHeader* getDataAttribute(unsigned char* MFTFile);
char* GetFullPath(char* resultFileName);
inline void* allocateMemory(uint64_t size);
void writeResultFile(MemoryFile* MFT, FILE* MFTResultFile);
void checkLetter(char* letter);
void openRawDisk(DiskInformation* diskInformation);
void readDriveBootSector(DiskInformation* diskInformation);
void readFistMFTFileRecord(MemoryFile* MFTMemoryFile, DiskInformation* diskInformation);
void readMFTFileData(MemoryFile* MFTMemoryFile, DiskInformation* diskInformation, RunHeader* currentMFTDataRun);
void MFTSanityChecks(MemoryFile* MFT);

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

void MFTTranscode(void* _MFTFilePath, char* resultFile, int inputType, wchar_t* letter) {
	FILE* mft_file_pointer;
	size_t res_size = 0, maxFiles = 0, fsize;
	unsigned char* mft_file_contents = NULL;
	uint64_t fileCount = 0;
	FilesAndFolders* files = NULL; 
	FilesAndFolders* folders = NULL;
	wchar_t* start = NULL;
	paginationStruct* result_pages = NULL;

	// I'm reusing _MFTFilePath to input both filepath or memoryFile
	// Because Dump&Transcode functionality
	if (inputType == 0) { // Read from file to memory
		char* errorMSG = (char*)malloc(1024);
		errno_t err;
		char* MFTFilePath;
		MFTFilePath = (char*)_MFTFilePath;
		err = fopen_s(&mft_file_pointer, MFTFilePath, "rb");
		if (err != 0) {
			strerror_s(errorMSG, 256, err);
			printf("Error reading MFT File: %s\n", errorMSG);
			goto exitMFTTranscode;
		}
		free(errorMSG);
		_fseeki64(mft_file_pointer, 0, SEEK_END);
		fsize = _ftelli64(mft_file_pointer);
		if (fsize == 0) {
			printf("Empty input file.\n");
			goto exitMFTTranscode;
		}
		fseek(mft_file_pointer, 0, SEEK_SET);
		mft_file_contents = (char*) malloc((size_t)fsize + 1);
		if (mft_file_contents == NULL) {
			printf("Not enough RAM to load MFT File.\n");
			goto exitMFTTranscode;
		}
		size_t readItems = fread_s(mft_file_contents, fsize, fsize, 1, mft_file_pointer);
		if (readItems != 1) {
			printf("Failed to read from MFT File.\n");
			goto exitMFTTranscode;
		}
		fclose(mft_file_pointer);
	} else if (inputType == 1) { // Retrieve object from dump
		MemoryFile* file = (MemoryFile*)_MFTFilePath;
		mft_file_contents = file->fileContent;
		fsize = file->size;
	} else {
		printf("Unexpected behaviour.");
		goto exitMFTTranscode;
	}

	maxFiles = (fsize / 1024) * 2; // MFT entries are 1024 bytes in size. One File or Directory per entry.
	size_t mallocSize = maxFiles * sizeof(FilesAndFolders);
	files = (FilesAndFolders*) malloc(mallocSize);
	folders = (FilesAndFolders*) malloc(mallocSize);
	// We are allocating a hundreds of megabytes
	if (files == NULL || folders == NULL || maxFiles < 5) { //5 is the minimum number of elements in a MFT
		printf("Not enough RAM");
		goto exitMFTTranscode;
	}
	memset(files, 0, mallocSize);
	memset(folders, 0, mallocSize);

	// Some sanity checks.
	int magicNumberCheck = memcmp(mft_file_contents, "FILE", 4);
	if (magicNumberCheck != 0 || fsize < 1024) {
		printf("Invalid MFT file");
		goto exitMFTTranscode;
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
	result_pages = (paginationStruct*)malloc(sizeof(paginationStruct) * 1024); //1024 pages will be far far far enough
	if (result_pages == NULL) {
		printf("Error allocating memory");
		goto exitMFTTranscode;
	}
	uint64_t aproxBytes = fileCount * (250 + 168); // I'm assuming an avereage of 250 characters per file/folder path and the rest of the parameters are 168 characters lenght
	unsigned char* page = NULL;
	page = (unsigned char*)malloc(aproxBytes); // because we are going to track the occupied size, trhere is no need to init this space
	if (page == NULL) {
		printf("Not enough memory");
		goto exitMFTTranscode;
	}
	result_pages[currentPage].fileContent = page;
	result_pages[currentPage].totalSpace = aproxBytes;
	result_pages[currentPage].occupiedSpace = 0;

	char* header = "File or Folder|File|$STDINFO Creation Time|$STDINFO Modification Time|$STDINFO Metadata change Time|$STDINFO Access Time|$FILENAME Creation Time|$FILENAME Modification Time|$FILENAME Entry Modified Time|$FILENAME Access Time\n";
	pageSave(result_pages, header, &currentPage, 225);

	
	start = (wchar_t*)malloc(3 * sizeof(wchar_t));
	if (start == NULL) {
		printf("Error allocating memory");
		goto exitMFTTranscode;
	}
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
					free(utf_name);
				}
			}

			char* utf_name = (char*)malloc((size_t)folders[cnt].fileNameLength + 1);;
			size_t resultBytes = WideCharToMultiByte(CP_UTF8, WC_NO_BEST_FIT_CHARS, folders[cnt].filename, folders[cnt].fileNameLength, utf_name, folders[cnt].fileNameLength, NULL, NULL);
			pageSave(result_pages, utf_name, &currentPage, resultBytes);
			pageSave(result_pages, "|", &currentPage, 1);
			free(utf_name);

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
				free(utf_name);
			}
		}

		char* utf_name = (char*)malloc((size_t)files[j].fileNameLength + 1);;
		size_t resultBytes = WideCharToMultiByte(CP_UTF8, WC_NO_BEST_FIT_CHARS, files[j].filename, files[j].fileNameLength, utf_name, files[j].fileNameLength, NULL, NULL);
		pageSave(result_pages, utf_name, &currentPage, resultBytes); 
		pageSave(result_pages, "|", &currentPage, 1);
		free(utf_name);

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

exitMFTTranscode:
	free(mft_file_contents);
	free(files);
	free(folders);
	if (result_pages != NULL) {
		for (int pageNumber = 0; pageNumber <= currentPage; pageNumber++) {
			free(result_pages[pageNumber].fileContent);
		}
	}
	free(result_pages);
	free(start);
}

int main(int argc, char* argv[])
{
	if (argc == 1) {
		printf("usage:\n\tmft.exe dump [Drive letter] [MFT Dump] \n\tDumps the MFT of the selected drive into the described file \n\n\ttranscode [MFT Dump] [CSV Result] \n\tProceses the MFT of the selected drive into a CSV to the described file\n\n\tDT [Drive letter][CSV Result] \n\tThe two other options together");
		exit(1);
	}

	if (strcmp("dump", argv[1]) == 0 && argc == 4) {
		MemoryFile* MFT;
		FILE* MFTResultFile;
		char* resultFileName = argv[3];
		char* driveLetter = argv[2];

		MFTResultFile = openResultFile(resultFileName);
		MFT = MFTDump(driveLetter);

		writeResultFile(MFT, MFTResultFile);

		printf("INFO: Result file successfully written to: %s\r\n", GetFullPath(resultFileName));

		fclose(MFTResultFile);
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

					MemoryFile* MFT;
					FILE* MFTResultFile;
					char* resultFileName = argv[3];
					char* driveLetter = argv[2];

					MFT = MFTDump(driveLetter);
					MFTTranscode(MFT, argv[3], 1, letterW);


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


FILE* openResultFile(char* resultFileName)
{
	FILE* MFTFile;
	char* resultFullPath = NULL;

	if (fopen_s(&MFTFile, resultFileName, "w+b") != 0)
	{
		printf("Error: Unexpected error opening result file: %s\r\n", resultFullPath);
		exit(1);
	}

	return MFTFile;
}

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
	return MFTMemoryFile;
}

void checkLetter(char* letter)
{
	int isValid = 1;
	unsigned char* letterChar;

	if (strlen(letter) != 1)
	{
		isValid = 0;
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

inline void* allocateMemory(uint64_t size)
{
	void* var = malloc(size);

	if (var == NULL)
	{
		printf("Error allocating memory\n");
		exit(1);
	}

	return var;
}

void openRawDisk(DiskInformation* diskInformation)
{
	char* driveName;

	driveName = (char*)allocateMemory(7);

	sprintf_s(driveName, 7, "\\\\.\\%s:", diskInformation->driveLetter);

	diskInformation->handler = CreateFileA(driveName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (diskInformation->handler == INVALID_HANDLE_VALUE)
	{
		printf("Error: can not open drive. Maybe incorrect letter or maybe you have not sufficient privileges.\r\n");
		exit(1);
	}

	free(driveName);

}

void readDriveBootSector(DiskInformation* diskInformation)
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

	printf("INFO: Boot sector successfully read.\r\n");
}

void readFistMFTFileRecord(MemoryFile* MFTMemoryFile, DiskInformation* diskInformation)
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

RunHeader* getFirstMFTDataRun(MemoryFile* MFTMemoryFile, DiskInformation* diskInformation)
{
	RunHeader* firstMFTDataRun;

	diskInformation->MFTDataAttribute = getDataAttribute(MFTMemoryFile->fileContent);
	diskInformation->MFTSize += diskInformation->MFTDataAttribute->attributeAllocated;

	MFTMemoryFile->fileContent =
		(unsigned char*)realloc(MFTMemoryFile->fileContent, diskInformation->MFTSize);

	if (MFTMemoryFile->fileContent == NULL) {
		printf("Error: Can not re-allocate memory for MFT File.\n");
		exit(1);
	}

	firstMFTDataRun = (RunHeader*)((uint8_t*)diskInformation->MFTDataAttribute + diskInformation->MFTDataAttribute->dataRunsOffset);
	return firstMFTDataRun;
}

NonResidentAttributeHeader* getDataAttribute(unsigned char* MFTFile)
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

		/* This check will disallow random memory access */
		if (nextAttributeOffset > maxAttributeOffset)
		{
			printf("Error: can not parse Data Attribute, inconsistent headers\r\n");
			exit(1);
		}

		attribute = (AttributeHeader*)(nextAttributeOffset);
		currentAttribute++;

		/* The following check is necessary to avoid an infinite loop */
		if (currentAttribute > MAXAttributes)
		{
			printf("Error: can not parse Data Attribute, inconsistent headers\r\n");
			exit(1);
		}
	}
}

void readMFTFileData(MemoryFile* MFTMemoryFile, DiskInformation* diskInformation, RunHeader* currentMFTDataRun)
{
	int read;
	DWORD bytesAccessed;
	LONG MFTLocation_low, MFTLocation_high;
	uint32_t currentDataRunOffset = (uint32_t)((uint8_t*)currentMFTDataRun - (uint8_t*)(diskInformation->MFTDataAttribute));
	uint64_t clusterNumber = 0;

	// Last DataRun header is defined by its Header == 0x00 
	while (currentDataRunOffset < diskInformation->MFTDataAttribute->attributeHeader.length && currentMFTDataRun->lengthFieldBytes)
	{
		uint64_t length = 0, offset = 0, from, count;

		// The first 4 bits of data run indicates de size of the length field 
		// The next 4 bits of data run indicates de size of the offset field 
		// This two field are before the first byte 
		// Thanks for this piece of code to: https://handmade.network/wiki/7002-tutorial_parsing_the_mft
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

		// DataRuns are stored one after the other 
		currentMFTDataRun = (RunHeader*)((uint8_t*)currentMFTDataRun + 1 + currentMFTDataRun->lengthFieldBytes +
			currentMFTDataRun->offsetFieldBytes);
		currentDataRunOffset = (uint32_t)((uint8_t*)currentMFTDataRun - (uint8_t*)(diskInformation->MFTDataAttribute));

		// The offset filed is relative to the previous DataRun offset 
		clusterNumber += offset;
		// The offset is defined as LCN (Logical Cluster Number), so we need to calculate de byte offset 
		from = clusterNumber * (diskInformation->bytesPerCluster);
		// Length also in clusters 
		count = length * (diskInformation->bytesPerCluster);

		MFTLocation_low = (LONG)(from & 0xFFFFFFFF);
		MFTLocation_high = (LONG)(from >> 32);
		SetFilePointer(diskInformation->handler, MFTLocation_low, &MFTLocation_high, FILE_BEGIN);
		if ((MFTMemoryFile->size + count) > diskInformation->MFTSize)
		{
			printf("Error: Real size seems to be greater than headers defined.\r\n");
			exit(1);
		}

		read = ReadFile(diskInformation->handler, &MFTMemoryFile->fileContent[MFTMemoryFile->size], (DWORD)count, &bytesAccessed, NULL);
		if (read == 0)
		{
			printf("Error: Cannot read MFT DataRun from drive offset.\r\n");
			exit(1);
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
	resultFullPath = (char*)allocateMemory(resultFullPathLenght);

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
		//Valid MFT will have at least 5 Files.
		//MFT is made by Files of 1024 bytes.
		printf("Error: Invalid MFT.\r\n");
		exit(1);
	}
	/* TODO:
			Better sanity checks
			Analyse anyway, incomplete MFT reconstruction
	*/
}