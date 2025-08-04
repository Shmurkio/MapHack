#pragma once

extern "C"
{
    #include <Uefi.h>
    #include <Library/UefiLib.h>
    #include <Library/UefiBootServicesTableLib.h>
    #include <Library/UefiApplicationEntryPoint.h>
    #include <Library/UefiRuntimeServicesTableLib.h>
    #include <Library/SerialPortLib.h>
    #include <Library/DebugLib.h>
    #include <Library/IoLib.h>
    #include <Library/PrintLib.h>
    #include <Library/MemoryAllocationLib.h>
    #include <Library/BaseMemoryLib.h>
    #include <Protocol/DevicePathToText.h>
    #include <Pi/PiFirmwareFile.h>
    #include <Protocol/FirmwareVolume2.h>
    #include <Protocol/FormBrowser2.h>
    #include <Library/Tpm2CommandLib.h>
    #include <IndustryStandard/Tpm20.h>
    #include <Guid/MemoryTypeInformation.h>
    #include <Library/DevicePathLib.h>
    #include <Guid/FileInfo.h>

    #include <sal.h>
}

#include <Library/SerialLib.hpp>
#include <Library/EntryLib.hpp>
#include <Library/UtilLib.hpp>
#include <Library/HookLib.hpp>

#include <cstdint>
#include <type_traits>

constexpr EFI_GUID gDxeCoreGuid = { 0xD6A2CB7F, 0x6A18, 0x4E2F, { 0xB4, 0x3B, 0x99, 0x20, 0xA7, 0x33, 0x70, 0x0A } };

struct IMAGE_INFO
{
    UINT64 ImageBase;
    UINT64 ImageSize;
    EFI_HANDLE ImageHandle;
    UINT32 ImageVersionHigh;
    UINT32 ImageVersionLow;
    PCSTR ImageName;
    PCSTR Developer;
};

using PIMAGE_INFO = IMAGE_INFO*;

struct MEMORY_MAP
{
    LIST_ENTRY Link;
    BOOLEAN FromPages;
    EFI_MEMORY_TYPE Type;
    UINT64 Start;
    UINT64 End;
    UINT64 VirtualStart;
    UINT64 Attribute;
};

using PMEMORY_MAP = MEMORY_MAP*;

struct EFI_MEMORY_TYPE_STATISTICS
{
    EFI_PHYSICAL_ADDRESS BaseAddress;
    EFI_PHYSICAL_ADDRESS MaximumAddress;
    UINT64 CurrentNumberOfPages;
    UINT64 NumberOfPages;
    UINTN InformationIndex;
    BOOLEAN Special;
    BOOLEAN Runtime;
};

using PEFI_MEMORY_TYPE_STATISTICS = EFI_MEMORY_TYPE_STATISTICS*;

enum EFI_GCD_MEMORY_TYPE
{
    EfiGcdMemoryTypeNonExistent,
    EfiGcdMemoryTypeReserved,
    EfiGcdMemoryTypeSystemMemory,
    EfiGcdMemoryTypeMemoryMappedIo,
    EfiGcdMemoryTypePersistent,
    EfiGcdMemoryTypePersistentMemory = EfiGcdMemoryTypePersistent,
    EfiGcdMemoryTypeMoreReliable,
    EfiGcdMemoryTypeUnaccepted,
    EfiGcdMemoryTypeMaximum = 7
};

enum EFI_GCD_IO_TYPE
{
    EfiGcdIoTypeNonExistent,
    EfiGcdIoTypeReserved,
    EfiGcdIoTypeIo,
    EfiGcdIoTypeMaximum
};

struct EFI_GCD_MAP_ENTRY
{
    LIST_ENTRY Link;
    EFI_PHYSICAL_ADDRESS BaseAddress;
    UINT64 EndAddress;
    UINT64 Capabilities;
    UINT64 Attributes;
    EFI_GCD_MEMORY_TYPE GcdMemoryType;
    EFI_GCD_IO_TYPE GcdIoType;
    EFI_HANDLE ImageHandle;
    EFI_HANDLE DeviceHandle;
};

using PEFI_GCD_MAP_ENTRY = EFI_GCD_MAP_ENTRY*;

// Metadata for the current image.
extern IMAGE_INFO gImage;