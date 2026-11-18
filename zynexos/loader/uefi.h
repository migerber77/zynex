#pragma once
/* Minimale UEFI-Definitionen fuer den Zynex-Lader (x86_64, Microsoft-Aufrufkonvention) */
typedef unsigned long long UINT64, UINTN, EFI_STATUS, EFI_PHYSICAL_ADDRESS;
typedef unsigned int UINT32; typedef unsigned short CHAR16, UINT16; typedef unsigned char UINT8; typedef void* EFI_HANDLE;
typedef struct { UINT32 a; UINT16 b, c; UINT8 d[8]; } EFI_GUID;
#define EFIAPI __attribute__((ms_abi))
enum { AllocateAnyPages, AllocateMaxAddress, AllocateAddress };
enum { EfiReservedMemoryType, EfiLoaderCode, EfiLoaderData };
typedef struct { UINT32 Type, Pad; UINT64 PhysicalStart, VirtualStart, NumberOfPages, Attribute; } EFI_MEMORY_DESCRIPTOR;
typedef struct { UINT64 sig; UINT32 rev, size, crc, res; } EFI_TABLE_HEADER;

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
struct EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *Open)(EFI_FILE_PROTOCOL*, EFI_FILE_PROTOCOL**, CHAR16*, UINT64, UINT64);
    EFI_STATUS (EFIAPI *Close)(EFI_FILE_PROTOCOL*);
    void* Delete;
    EFI_STATUS (EFIAPI *Read)(EFI_FILE_PROTOCOL*, UINTN*, void*);
    void* Write;
    EFI_STATUS (EFIAPI *GetPosition)(EFI_FILE_PROTOCOL*, UINT64*);
    EFI_STATUS (EFIAPI *SetPosition)(EFI_FILE_PROTOCOL*, UINT64);
};
typedef struct EFI_SIMPLE_FILE_SYSTEM { UINT64 Revision; EFI_STATUS (EFIAPI *OpenVolume)(struct EFI_SIMPLE_FILE_SYSTEM*, EFI_FILE_PROTOCOL**); } EFI_SIMPLE_FILE_SYSTEM;
typedef struct { UINT32 Revision; EFI_HANDLE ParentHandle; void* SystemTable; EFI_HANDLE DeviceHandle; } EFI_LOADED_IMAGE;

typedef struct { UINT32 Version, HorizontalResolution, VerticalResolution, PixelFormat; UINT32 Mask[4]; UINT32 PixelsPerScanLine; } EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;
typedef struct { UINT32 MaxMode, Mode; EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info; UINTN SizeOfInfo; UINT64 FrameBufferBase, FrameBufferSize; } EFI_GRAPHICS_OUTPUT_MODE;
typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *QueryMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL*, UINT32, UINTN*, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION**);
    EFI_STATUS (EFIAPI *SetMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL*, UINT32);
    void* Blt;
    EFI_GRAPHICS_OUTPUT_MODE* Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void* Reset;
    EFI_STATUS (EFIAPI *OutputString)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, CHAR16*);
    void* TestString, *QueryMode, *SetMode, *SetAttribute;
    EFI_STATUS (EFIAPI *ClearScreen)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*);
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void* RaiseTPL; void* RestoreTPL;
    EFI_STATUS (EFIAPI *AllocatePages)(UINT32, UINT32, UINTN, EFI_PHYSICAL_ADDRESS*);
    void* FreePages;
    EFI_STATUS (EFIAPI *GetMemoryMap)(UINTN*, EFI_MEMORY_DESCRIPTOR*, UINTN*, UINTN*, UINT32*);
    void* AllocatePool; void* FreePool;
    void* CreateEvent; void* SetTimer; void* WaitForEvent; void* SignalEvent; void* CloseEvent; void* CheckEvent;
    void* InstallProtocolInterface; void* ReinstallProtocolInterface; void* UninstallProtocolInterface;
    EFI_STATUS (EFIAPI *HandleProtocol)(EFI_HANDLE, EFI_GUID*, void**);
    void* Reserved; void* RegisterProtocolNotify; void* LocateHandle; void* LocateDevicePath; void* InstallConfigurationTable;
    void* LoadImage; void* StartImage; void* Exit; void* UnloadImage;
    EFI_STATUS (EFIAPI *ExitBootServices)(EFI_HANDLE, UINTN);
    void* GetNextMonotonicCount; void* Stall;
    EFI_STATUS (EFIAPI *SetWatchdogTimer)(UINTN, UINT64, UINTN, CHAR16*);
    void* ConnectController; void* DisconnectController;
    void* OpenProtocol; void* CloseProtocol; void* OpenProtocolInformation;
    void* ProtocolsPerHandle; void* LocateHandleBuffer;
    EFI_STATUS (EFIAPI *LocateProtocol)(EFI_GUID*, void*, void**);
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr; CHAR16* FirmwareVendor; UINT32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle; void* ConIn; EFI_HANDLE ConsoleOutHandle; EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    EFI_HANDLE StandardErrorHandle; void* StdErr; void* RuntimeServices; EFI_BOOT_SERVICES* BootServices;
} EFI_SYSTEM_TABLE;
