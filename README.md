# MapHack
MapHack is a PoC for VMware demonstrating how to patch the internal memory map maintained by UEFI's `DxeCore`, causing changes to propagate all the way to what's returned by `gBS->GetMemoryMap()`.

## Overview
UEFI mainly maintains three internal structures that make up the final memory map returned by `gBS->GetMemoryMap()`:
- `gMemoryMap`: a linked list of `MEMORY_MAP` entries.
- `gGcdMapEntry`: a linked list of `EFI_GCD_MAP_ENTRY` entries.
- `mMemoryTypeStatistics`: an array of `EFI_MEMORY_TYPE_STATISTICS` entries.

The function `CoreGetMemoryMap()` uses those three internal structures to build the memory map returned by `gBS->GetMemoryMap()`. This means patching `gMemoryMap` directly patches the actual system memory map, and the changes will be reflected in any memory map consumers.

## CoreGetMemoryMap
In most UEFI implementations, it disassembles into something like this:
```c++
EFI_STATUS __fastcall CoreGetMemoryMap(
        UINTN *MemoryMapSize,
        EFI_MEMORY_DESCRIPTOR *MemoryMap,
        UINTN *MapKey,
        UINTN *DescriptorSize,
        UINT32 *DescriptorVersion)
{
  EFI_STATUS v5; // r14
  EFI_GCD_MAP_ENTRY *GcdMapEntry; // rax
  UINTN NumberOfEntries; // rbx
  EFI_GCD_MEMORY_TYPE GcdMemoryType; // edx
  __int64 v13; // rcx
  LIST_ENTRY *Link; // rsi
  UINTN v15; // rdx
  MEMORY_MAP *Entry; // rbx
  LIST_ENTRY *v18; // r14
  UINT64 BackLink; // rdx
  __int64 MemoryType; // rax
  EFI_MEMORY_TYPE_STATISTICS *MemoryTypeStatistics; // rax
  EFI_MEMORY_TYPE Type; // edx
  EFI_GCD_MAP_ENTRY *ForwardLink; // rbx
  EFI_HANDLE *v1; // rsi
  __int64 v25; // rax
  EFI_MEMORY_DESCRIPTOR *i; // rax
  UINTN BufferSize; // [rsp+28h] [rbp-98h] BYREF
  char MergeGcdMapEntry[24]; // [rsp+30h] [rbp-90h] BYREF
  EFI_PHYSICAL_ADDRESS v30; // [rsp+48h] [rbp-78h]
  EFI_HANDLE v31; // [rsp+50h] [rbp-70h]
  __int64 v32; // [rsp+58h] [rbp-68h]
  __int64 v33; // [rsp+60h] [rbp-60h]
  EFI_HANDLE v34; // [rsp+68h] [rbp-58h]

  v5 = 0x8000000000000002ui64;
  if ( MemoryMapSize )
  {
    sub_7377();
    GcdMapEntry = gGcdMapEntry;
    NumberOfEntries = 0i64;
    while ( GcdMapEntry != &gGcdMapEntry )
    {
      GcdMemoryType = GcdMapEntry->GcdMemoryType;
      if ( GcdMemoryType == EfiGcdMemoryTypeReserved
        || GcdMemoryType == EfiGcdMemoryTypePersistentMemory
        || GcdMemoryType == EfiGcdMemoryTypeMemoryMappedIo && (GcdMapEntry->Attributes & 0x8000000000000000ui64) != 0i64 )
      {
        ++NumberOfEntries;
      }
      GcdMapEntry = GcdMapEntry->Link.ForwardLink;
    }
    if ( DescriptorSize )
      *DescriptorSize = 48i64;
    if ( DescriptorVersion )
      *DescriptorVersion = 1;
    sub_11BC8(&gGcdMapEntry);
    BufferSize = 48 * NumberOfEntries;
    for ( Link = gMemoryMap.ForwardLink; ; Link = Link->ForwardLink )
    {
      v15 = BufferSize;
      if ( Link == &gMemoryMap )
        break;
      BufferSize += 48i64;
    }
    v5 = 0x8000000000000005ui64;
    if ( *MemoryMapSize >= BufferSize )
    {
      v5 = 0x8000000000000002ui64;
      if ( MemoryMap )
      {
        ZeroMem(MemoryMap, BufferSize);
        for ( Entry = Link->ForwardLink; Entry != &gMemoryMap; Entry = Entry->Link.ForwardLink )
        {
          v18 = CONTAINING_RECORD(Entry, LIST_ENTRY, BackLink);
          MemoryMap->Type = Entry->Type;
          MemoryMap->PhysicalStart = Entry->Start;
          MemoryMap->VirtualStart = Entry->VirtualStart;
          MemoryMap->NumberOfPages = RShiftU64(Entry->End + 1 - Entry->Start, 0xCui64);// 12 = EFI_PAGE_SHIFT
          if ( MemoryMap->Type == EfiConventionalMemory )
          {
            MemoryTypeStatistics = mMemoryTypeStatistics;
            for ( Type = EfiReservedMemoryType; Type != 16; ++Type )
            {
              if ( MemoryTypeStatistics->Special
                && MemoryTypeStatistics->NumberOfPages
                && v18[2].ForwardLink >= MemoryTypeStatistics->BaseAddress
                && v18[2].BackLink <= MemoryTypeStatistics->MaximumAddress )
              {
                MemoryMap->Type = Type;
              }
              ++MemoryTypeStatistics;
            }
          }
          BackLink = v18[3].BackLink;
          MemoryMap->Attribute = BackLink;
          MemoryType = MemoryMap->Type;
          if ( MemoryType <= EfiMaxMemoryType && mMemoryTypeStatistics[MemoryType].Runtime )// If that memory type is runtime, attribute of the returned memory descriptor will be set to runtime
            MemoryMap->Attribute = BackLink | 0x8000000000000000ui64;// MemoryMap->Attribute |= EFI_MEMORY_RUNTIME
          MemoryMap = MergeMemoryMapDescriptor(MemoryMap, MemoryMap, 48i64);
        }
        ZeroMem(MergeGcdMapEntry, 0x50ui64);
        ForwardLink = gGcdMapEntry;
        v1 = 0i64;
        while ( 1 )
        {
          if ( ForwardLink != &gGcdMapEntry
            && (v1 = &ForwardLink[-1].DeviceHandle, v32 == ForwardLink->Capabilities)
            && v33 == v1[6]
            && v34 == v1[7] )
          {
            v31 = v1[4];
          }
          else
          {
            if ( v34 == 1 || v34 == 3 && v33 < 0 )
            {
              MemoryMap->PhysicalStart = v30;
              MemoryMap->VirtualStart = 0i64;
              MemoryMap->NumberOfPages = RShiftU64(v31 + 1 - v30, 0xCui64);
              MemoryMap->Attribute = v32 & 0xE701F | v33 & 0xBFFFFFFFFFFFFFFFui64;
              if ( v34 == 1 )
              {
                MemoryMap->Type = 0;
              }
              else if ( v34 == 3 )
              {
                MemoryMap->Type = 12 - ((v33 & 0x4000000000000000i64) == 0);
              }
              MemoryMap = MergeMemoryMapDescriptor(MemoryMap, MemoryMap, 48i64);
            }
            if ( v34 == 4 )
            {
              MemoryMap->PhysicalStart = v30;
              MemoryMap->VirtualStart = 0i64;
              MemoryMap->NumberOfPages = RShiftU64(v31 + 1 - v30, 0xCui64);
              v25 = v33 | v32 & 0xE701F;
              BYTE1(v25) |= 0x80u;
              MemoryMap->Attribute = v25;
              MemoryMap->Type = 14;
              MemoryMap = MergeMemoryMapDescriptor(MemoryMap, MemoryMap, 48i64);
            }
            if ( v34 == 6 )
            {
              MemoryMap->PhysicalStart = v30;
              MemoryMap->VirtualStart = 0i64;
              MemoryMap->NumberOfPages = RShiftU64(v31 + 1 - v30, 0xCui64);
              MemoryMap->Attribute = v33 | v32 & 0x2701F;
              MemoryMap->Type = 15;
              MemoryMap = MergeMemoryMapDescriptor(MemoryMap, MemoryMap, 48i64);
            }
            if ( ForwardLink == &gGcdMapEntry )
            {
              BufferSize = MemoryMap - MemoryMap;
              for ( i = MemoryMap; MemoryMap > i; i = (i + 48) )
                i->Attribute &= 0xFFFFFFFFFFFD9FFFui64;
              sub_3BD6(MemoryMap, &BufferSize, 48i64);
              v5 = 0i64;
              break;
            }
            if ( v1 )
              sub_D6E2(MergeGcdMapEntry, v1, 80i64);
          }
          ForwardLink = ForwardLink->Link.ForwardLink;
        }
      }
    }
    if ( MapKey )
      *MapKey = qword_1C0E8;
    sub_11BDE(v13, v15);
    sub_738D();
    *MemoryMapSize = BufferSize;
    sub_D670();
  }
  return v5;
}
```

## Patching
To demonstrate the concept, we pick two entries in `gMemoryMap`:
```
-> Type=EfiRuntimeServicesData, Start=0x1000, End=0x1FFF, Attr=0xF.
-> Type=EfiConventionalMemory, Start=0x2000, End=0x9FFFF, Attr=0xF.
```
These can be found in the memory map returned by `gBS->GetMemoryMap()`:
```
-> Type=EfiRuntimeServicesData, Start=0x1000, Pages=1, Attr=0x800000000000000F.
-> Type=EfiConventionalMemory, Start=0x2000, Pages=158, Attr=0xF.
```
After swapping the memory types in `gMemoryMap`, the returned memory map reflects our patch:
```
-> Type=EfiConventionalMemory, Start=0x1000, Pages=1, Attr=0xF.
-> Type=EfiRuntimeServicesData, Start=0x2000, Pages=158, Attr=0x800000000000000F.
```
This effectively gives us unrestricted control over the UEFI memory map used by the system and returned to any caller.

## Showcase
[![MapHack PoC Video](https://img.youtube.com/vi/rDRt59TkKCI/0.jpg)](https://youtu.be/rDRt59TkKCI)