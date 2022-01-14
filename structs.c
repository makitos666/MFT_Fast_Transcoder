#include <inttypes.h>
#include <windows.h>

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
    wchar_t* filename;
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
    uint64_t    MFTFirstClusterLocation;
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
} MemoryFile;

typedef struct {
    unsigned char* fileContent;
    size_t totalSpace;
    size_t occupiedSpace;
} paginationStruct;

typedef struct {
    char* driveLetter;
    HANDLE handler;
    BootSector* bootSector;
    uint64_t bytesPerCluster;
    uint64_t MFTSize;
    NonResidentAttributeHeader* MFTDataAttribute;
} DiskInformation;