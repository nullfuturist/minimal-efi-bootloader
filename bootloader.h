#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include "efibind.h"
#include "efidef.h"
#include "efidevp.h"
#include "eficon.h"
#include "efiapi.h"
#include "efierr.h"
#include "efiprot.h"

#define LOAD_FILE2_PROTOCOL \
  { 0x4006c0c1, 0xfcb3, 0x403e, {0x99, 0x6d, 0x4a, 0x6c, 0x87, 0x24, 0xe0, 0x6d} }

#define LINUX_EFI_INITRD_MEDIA_GUID \
  { 0x5568e427, 0x68fc, 0x4f3d, {0xac, 0x74, 0xca, 0x55, 0x52, 0x31, 0xcc, 0x68} }

#define EFI_FILE_MODE_READ 0x0000000000000001
#define HARDWARE_DEVICE_PATH 0x01
#define HW_MEMMAP_DP 0x03
#define MEDIA_DEVICE_PATH 0x04
#define MEDIA_VENDOR_DP 0x03
#define END_DEVICE_PATH_TYPE 0x7f
#define END_ENTIRE_DEVICE_PATH_SUBTYPE 0xff

typedef struct {
  UINT8 Type;
  UINT8 SubType;
  UINT8 Length[2];
} EFI_DEVICE_PATH_PROTOCOL_HEADER;

typedef struct {
  EFI_DEVICE_PATH_PROTOCOL_HEADER Header;
  UINT32 MemoryType;
  EFI_PHYSICAL_ADDRESS StartAddress;
  EFI_PHYSICAL_ADDRESS EndAddress;
} EFI_MEMORY_MAPPED_DEVICE_PATH;

typedef struct {
  EFI_DEVICE_PATH_PROTOCOL_HEADER Header;
} EFI_END_DEVICE_PATH;

typedef struct {
  EFI_DEVICE_PATH_PROTOCOL_HEADER Header;
  EFI_GUID Guid;
} EFI_VENDOR_DEVICE_PATH;

typedef struct _EFI_LOAD_FILE2_PROTOCOL EFI_LOAD_FILE2_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_LOAD_FILE2)(
  EFI_LOAD_FILE2_PROTOCOL *This,
  EFI_DEVICE_PATH *FilePath,
  BOOLEAN BootPolicy,
  UINTN *BufferSize,
  VOID *Buffer
);

struct _EFI_LOAD_FILE2_PROTOCOL {
  EFI_LOAD_FILE2 LoadFile;
};

typedef struct {
  EFI_VENDOR_DEVICE_PATH vendor;
  EFI_END_DEVICE_PATH end;
} INITRD_DEVICE_PATH;

extern UINT8 *g_initramfs;
extern UINTN g_initramfs_size;

void printInt(SIMPLE_TEXT_OUTPUT_INTERFACE *conOut, int value);
void printHex(SIMPLE_TEXT_OUTPUT_INTERFACE *conOut, UINT64 value, int width);
UINTN strlen_a(const char *s);
void ascii_to_ucs2(CHAR16 *dest, const char *src);
EFI_STATUS EFIAPI InitrdLoadFile2(EFI_LOAD_FILE2_PROTOCOL *This,
                                   EFI_DEVICE_PATH *DevicePath,
                                   BOOLEAN BootPolicy,
                                   UINTN *BufferSize,
                                   VOID *Buffer);

#endif
