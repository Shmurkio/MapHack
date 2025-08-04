#include "MapHack.hpp"

IMAGE_INFO gImage;

UINT64 gDxeCoreImageBase = 0, gDxeCoreImageSize = 0;
EFI_HANDLE gDxeCoreImageHandle = nullptr;

PEFI_GCD_MAP_ENTRY gGcdMapEntry = nullptr;
PMEMORY_MAP gMemoryMap = nullptr;
PEFI_MEMORY_TYPE_STATISTICS mMemoryTypeStatistics = nullptr;
EFI_GET_MEMORY_MAP CoreGetMemoryMap = nullptr;

EFI_STATUS
LocateImage(
    IN EFI_GUID Guid,
    OUT EFI_LOADED_IMAGE_PROTOCOL*& LoadedImage,
    OUT EFI_HANDLE& Handle
)
{
    EFI_HANDLE* HandleBuffer = nullptr;
    UINT64 HandleCount = 0;
    EFI_STATUS Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiLoadedImageProtocolGuid, NULL, &HandleCount, &HandleBuffer);
    
    if (EFI_ERROR(Status))
    {
        Serial::Write("Failed to get all handles: %r.\n\r", Status);
        return Status;
    }

    for (UINT64 i = 0; i < HandleCount; i++)
    {
        EFI_LOADED_IMAGE_PROTOCOL* LoadedImage2 = nullptr;
        Status = gBS->HandleProtocol(HandleBuffer[i], &gEfiLoadedImageProtocolGuid, reinterpret_cast<PVOID*>(&LoadedImage2));

        if (EFI_ERROR(Status))
        {
            continue;
        }

        EFI_DEVICE_PATH_PROTOCOL* Path = LoadedImage2->FilePath;

        while (!IsDevicePathEnd(Path))
        {
            MEDIA_FW_VOL_FILEPATH_DEVICE_PATH* FvPath = reinterpret_cast<MEDIA_FW_VOL_FILEPATH_DEVICE_PATH*>(Path);

            if (CompareGuid(&FvPath->FvFileName, &Guid))
            {
                LoadedImage = LoadedImage2;
                Handle = HandleBuffer[i];
                FreePool(HandleBuffer);
                return EFI_SUCCESS;
            }

            Path = NextDevicePathNode(Path);
        }
    }

    FreePool(HandleBuffer);
    return EFI_NOT_FOUND;
}

PCSTR
GetMemoryTypeName(
    IN EFI_MEMORY_TYPE Type
)
{
    switch (Type)
    {
        case 0: return "EfiReservedMemoryType";
        case 1: return "EfiLoaderCode";
        case 2: return "EfiLoaderData";
        case 3: return "EfiBootServicesCode";
        case 4: return "EfiBootServicesData";
        case 5: return "EfiRuntimeServicesCode";
        case 6: return "EfiRuntimeServicesData";
        case 7: return "EfiConventionalMemory";
        case 8: return "EfiUnusableMemory";
        case 9: return "EfiACPIReclaimMemory";
        case 10: return "EfiACPIMemoryNVS";
        case 11: return "EfiMemoryMappedIO";
        case 12: return "EfiMemoryMappedIOPortSpace";
        case 13: return "EfiPalCode";
        case 14: return "EfiPersistentMemory";
        case 15: return "EfiUnacceptedMemoryType";
        case 16: return "EfiMaxMemoryType";
        default: return "Unknown";
    }
}

EFI_STATUS
PrintCurrentMemoryMap(
    VOID
)
{
    EFI_MEMORY_DESCRIPTOR* MemoryMap = nullptr;
    UINT64 MemoryMapSize = 0, MapKey = 0, DescriptorSize = 0;
    UINT32 DescriptorVersion = 0;
    
    EFI_STATUS Status = gBS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);

    if (Status != EFI_BUFFER_TOO_SMALL)
    {
        Serial::Write("-> Unexpected status: %r.\n", Status);
        return EFI_UNSUPPORTED;
    }

    MemoryMapSize += DescriptorSize * 4;
    MemoryMap = reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(AllocatePool(MemoryMapSize));
    
    Status = gBS->GetMemoryMap(&MemoryMapSize, MemoryMap, &MapKey, &DescriptorSize, &DescriptorVersion);

    if (EFI_ERROR(Status))
    {
        Serial::Write("Failed to get memory map: %r.\n", Status);
        return Status;
    }

    EFI_MEMORY_DESCRIPTOR* MemoryMapBackup = MemoryMap;

    for (UINT64 i = 0; i < MemoryMapSize / DescriptorSize; i++)
    {
        EFI_MEMORY_DESCRIPTOR* Descriptor = reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(MemoryMap);
        Serial::Write("-> Type=%a, Start=0x%llX, Pages=%llu, Attr=0x%llX.\n", GetMemoryTypeName(static_cast<EFI_MEMORY_TYPE>(Descriptor->Type)), Descriptor->PhysicalStart, Descriptor->NumberOfPages, Descriptor->Attribute);
        MemoryMap = reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(reinterpret_cast<UINT64>(MemoryMap) + DescriptorSize);
    }

    FreePool(MemoryMapBackup);
    return EFI_SUCCESS;
}

EFI_STATUS
PrintMemoryTypeStatistics(
    VOID
)
{
    if (!mMemoryTypeStatistics)
    {
        Serial::Write("-> mMemoryTypeStatistics not set.\n");
        return EFI_NOT_READY;
    }

    for (UINTN i = 0; i <= EfiMaxMemoryType; ++i)
    {
        PEFI_MEMORY_TYPE_STATISTICS Statistic = &mMemoryTypeStatistics[i];
        Serial::Write("-> Type=%a, Special=%a, Runtime=%a\n", GetMemoryTypeName(static_cast<EFI_MEMORY_TYPE>(i)), Statistic->Special ? "Yes" : "No", Statistic->Runtime ? "Yes" : "No");
    }

    return EFI_SUCCESS;
}

EFI_STATUS
PrintInternalMemoryMap(
    VOID
)
{
    if (!gMemoryMap)
    {
        Serial::Write("-> gMemoryMap not set.\n");
        return EFI_NOT_READY;
    }

    PMEMORY_MAP Entry = reinterpret_cast<PMEMORY_MAP>(gMemoryMap->Link.ForwardLink);

    while (Entry != gMemoryMap)
    {
        Serial::Write("-> Type=%a, Start=0x%llX, End=0x%llX, Attr=0x%llX.\n", GetMemoryTypeName(Entry->Type), Entry->Start, Entry->End, Entry->Attribute);
        Entry = reinterpret_cast<PMEMORY_MAP>(Entry->Link.ForwardLink);
    }

    return EFI_SUCCESS;
}

EFI_STATUS
InitImageInfo(
    IN UINT32 ImageVersionHigh,
    IN UINT32 ImageVersionLow,
    IN PCSTR ImageName,
    IN PCSTR Developer
)
{
    if (!ImageName)
    {
        return EFI_INVALID_PARAMETER;
    }

    gImage.ImageBase = reinterpret_cast<UINT64>(&__ImageBase);
    gImage.ImageSize = Util::GetCurrentImageSize();
    gImageHandle = gImageHandle;
    gImage.ImageVersionHigh = ImageVersionHigh;
    gImage.ImageVersionLow = ImageVersionLow;
    gImage.ImageName = ImageName;
    gImage.Developer = Developer;
    return EFI_SUCCESS;
}

EFI_STATUS
Main(
    MAYBE_UNUSED IN UINT64 ArgC,
    MAYBE_UNUSED IN PCHAR ArgV[]
)
{
    // Set serial port.
    Serial::SetPort(SERIAL_PORT_1);

    // Set current image's metadata.
    InitImageInfo(1, 0, "MapHack", "Shmurkio");

    // Find DxeCore.
    Serial::Write("DxeCore:\n");
    EFI_LOADED_IMAGE_PROTOCOL* LoadedImage = nullptr;
    EFI_STATUS Status = LocateImage(gDxeCoreGuid, LoadedImage, gDxeCoreImageHandle);

    if (EFI_ERROR(Status))
    {
        return Status;
    }

    gDxeCoreImageBase = reinterpret_cast<UINT64>(LoadedImage->ImageBase);
    gDxeCoreImageSize = LoadedImage->ImageSize;
    Serial::Write("-> ImageBase=0x%llX.\n", gDxeCoreImageBase);
    Serial::Write("-> ImageSize=0x%llX.\n", gDxeCoreImageSize);
    Serial::Write("-> ImageHandle=0x%llX.\n", gDxeCoreImageHandle);
    Serial::Write("\n");

    // Find CoreGetMemoryMap.
    Serial::Write("CoreGetMemoryMap:\n");
    Status = Util::FindPattern(gDxeCoreImageBase, gDxeCoreImageSize, "55 48 89 E5 41 57 41 56 41 55 41 54 57 56 53 48 81 EC 88 00 00 00 48 8B 75", CoreGetMemoryMap);

    if (EFI_ERROR(Status))
    {
        Serial::Write("-> Failed to find: %r.\n", Status);
        return Status;
    }

    Serial::Write("-> Address=0x%llX.\n", CoreGetMemoryMap);
    Serial::Write("\n");

    // Find gGcdMapEntry.
    // The pointer to gGcdMapEntry is stored at ImageBase + 0x1A040:
    // .data:000000000001A040 ; EFI_GCD_MAP_ENTRY *gGcdMapEntry
    // .data:000000000001A040 gGcdMapEntry    dq offset gGcdMapEntry  ; DATA XREF: sub_4D0E+24B↑r
    // .data:000000000001A040                                         ; sub_4D0E+252↑o ...
    Serial::Write("gGcdMapEntry:\n");
    gGcdMapEntry = *reinterpret_cast<PEFI_GCD_MAP_ENTRY*>(gDxeCoreImageBase + 0x1A040);
    Serial::Write("-> Address=0x%llX.\n", gGcdMapEntry);
    Serial::Write("\n");

    // Find mMemoryTypeStatistics.
    // mMemoryTypeStatistics is stored at gDxeCoreImageBase + 0x1BC00:
    // .data:000000000001BC00 mMemoryTypeStatistics db 8 dup(0), 8 dup(0FFh), 10h dup(0), 10h, 7 dup(0), 1
    // .data:000000000001BC00                                         ; DATA XREF: CoreGetMemoryMap+F3↑o
    // .data:000000000001BC00                                         ; CoreGetMemoryMap:loc_1203A↑o ...
    Serial::Write("mMemoryTypeStatistics:\n");
    mMemoryTypeStatistics = reinterpret_cast<PEFI_MEMORY_TYPE_STATISTICS>(gDxeCoreImageBase + 0x1BC00);
    Serial::Write("-> Address=0x%llX.\n", mMemoryTypeStatistics);
    PrintMemoryTypeStatistics();
    Serial::Write("\n");
    
    // Find gMemoryMap.
    // The pointer to gMemoryMap is stored at gDxeCoreImageBase + 0x1C0F0:
    // .data:000000000001C0F0 gMemoryMap      LIST_ENTRY <offset gMemoryMap, offset gMemoryMap>
    // .data:000000000001C0F0                                         ; DATA XREF: sub_11C34+59↑r
    // .data:000000000001C0F0                                         ; sub_11C34:loc_11C94↑o ...
    Serial::Write("gMemoryMap:\n");
    gMemoryMap = *reinterpret_cast<PMEMORY_MAP*>(gDxeCoreImageBase + 0x1C0F0);
    Serial::Write("-> Address=0x%llX.\n", gMemoryMap);
    PrintInternalMemoryMap();
    Serial::Write("\n");

    // Print regular MemoryMap.
    Serial::Write("MemoryMap:\n");
    PrintCurrentMemoryMap();
    Serial::Write("\n");

    // Patch internal memory map.
    // gMemoryMap shows:
    // -> Type=EfiRuntimeServicesData, Start=0x1000, End=0x1FFF, Attr=0xF.
    // -> Type=EfiConventionalMemory, Start=0x2000, End=0x9FFFF, Attr=0xF.
    // Those entries will be mapped to:
    // -> Type=EfiRuntimeServicesData, Start=0x1000, Pages=1, Attr=0x800000000000000F.
    // -> Type=EfiConventionalMemory, Start=0x2000, Pages=158, Attr=0xF.
    // in the memory map returned by CoreGetMemoryMap.
    Serial::Write("Patching gMemoryMap entry:\n");
    PMEMORY_MAP Entry = reinterpret_cast<PMEMORY_MAP>(gMemoryMap->Link.ForwardLink);
    BOOLEAN PatchedOne = FALSE;

    while (Entry != gMemoryMap)
    {
        if (Entry->Start == 0x1000 && Entry->End == 0x1FFF)
        {
            Serial::Write("-> Setting type for entry from 0x%llX to 0x%llX from %a to %a.\n", Entry->Start, Entry->End, GetMemoryTypeName(Entry->Type), GetMemoryTypeName(EfiConventionalMemory));
            Entry->Type = EfiConventionalMemory;

            if (!PatchedOne)
            {
                PatchedOne = TRUE;
            }
            else
            {
                break;
            }
        }
        else if (Entry->Start == 0x2000 && Entry->End == 0x9FFFF)
        {
            Serial::Write("-> Setting type for entry from 0x%llX to 0x%llX from %a to %a.\n", Entry->Start, Entry->End, GetMemoryTypeName(Entry->Type), GetMemoryTypeName(EfiRuntimeServicesData));
            Entry->Type = EfiRuntimeServicesData;

            if (!PatchedOne)
            {
                PatchedOne = TRUE;
            }
            else
            {
                break;
            }
        }

        Entry = reinterpret_cast<PMEMORY_MAP>(Entry->Link.ForwardLink);
    }

    Serial::Write("\n");

    // Print memory map again to confirm changes.
    Serial::Write("Memory map:\n");
    PrintCurrentMemoryMap();
    
    return EFI_SUCCESS;
}