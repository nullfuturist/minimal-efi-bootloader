#include "bootloader.h"

static EFI_GUID BlockIoProtocolGUID = BLOCK_IO_PROTOCOL;
static EFI_GUID SimpleFileSystemGUID = SIMPLE_FILE_SYSTEM_PROTOCOL;
static EFI_GUID LoadedImageProtocolGUID = LOADED_IMAGE_PROTOCOL;
static EFI_GUID LoadFile2ProtocolGUID = LOAD_FILE2_PROTOCOL;
static EFI_GUID DevicePathProtocolGUID = DEVICE_PATH_PROTOCOL;
static EFI_GUID LinuxInitrdMediaGUID = LINUX_EFI_INITRD_MEDIA_GUID;

UINT8 *g_initramfs = NULL;
UINTN g_initramfs_size = 0;

static EFI_LOAD_FILE2_PROTOCOL initrd_lf2 = {
  InitrdLoadFile2
};

void printInt(SIMPLE_TEXT_OUTPUT_INTERFACE *conOut, int value) {
  CHAR16 out[32];
  CHAR16 *ptr = out;
  if (value == 0) {
    conOut->OutputString(conOut, L"0");
    return;
  }
  ptr += 31;
  *--ptr = 0;
  int tmp = value;
  while (tmp) {
    *--ptr = '0' + tmp % 10;
    tmp /= 10;
  }
  if (value < 0) *--ptr = '-';
  conOut->OutputString(conOut, ptr);
}

void printHex(SIMPLE_TEXT_OUTPUT_INTERFACE *conOut, UINT64 value, int width) {
  CHAR16 out[17];
  int i;
  for (i = 0; i < width && i < 16; i++) {
    UINT8 nibble = (value >> ((width - 1 - i) * 4)) & 0xF;
    out[i] = nibble + (nibble < 10 ? '0' : '7');
  }
  out[i] = 0;
  conOut->OutputString(conOut, out);
}

UINTN strlen_a(const char *s) {
  UINTN len = 0;
  while (*s++) len++;
  return len;
}

void ascii_to_ucs2(CHAR16 *dest, const char *src) {
  while (*src) {
    *dest++ = (CHAR16)*src++;
  }
  *dest = 0;
}

EFI_STATUS EFIAPI
InitrdLoadFile2(EFI_LOAD_FILE2_PROTOCOL *This,
                EFI_DEVICE_PATH *DevicePath,
                BOOLEAN BootPolicy,
                UINTN *BufferSize,
                VOID *Buffer)
{
  if (This != &initrd_lf2 || BufferSize == NULL)
    return EFI_INVALID_PARAMETER;

  if (DevicePath->Type != END_DEVICE_PATH_TYPE ||
      DevicePath->SubType != END_ENTIRE_DEVICE_PATH_SUBTYPE)
    return EFI_NOT_FOUND;

  if (BootPolicy)
    return EFI_UNSUPPORTED;

  if (Buffer == NULL || *BufferSize < g_initramfs_size) {
    *BufferSize = g_initramfs_size;
    return EFI_BUFFER_TOO_SMALL;
  }

  UINT8 *src = g_initramfs;
  UINT8 *dst = Buffer;
  for (UINTN i = 0; i < g_initramfs_size; i++) {
    dst[i] = src[i];
  }

  return EFI_SUCCESS;
}

EFI_STATUS
efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *systemTable)
{
  EFI_BOOT_SERVICES *bs = systemTable->BootServices;
  SIMPLE_TEXT_OUTPUT_INTERFACE *conOut = systemTable->ConOut;
  EFI_EVENT event = systemTable->ConIn->WaitForKey;
  UINTN index;
  EFI_HANDLE handles[100];
  EFI_FILE_IO_INTERFACE *fs;
  EFI_FILE *root, *file;
  UINTN bufferSize = 100 * sizeof(EFI_HANDLE);
  EFI_STATUS status;
  UINT8 *kernel, *initramfs;
  UINTN kernelSize, initramfsSize;
  EFI_HANDLE kernelHandle;
  EFI_LOADED_IMAGE *loadedImage;
  EFI_HANDLE initrdHandle = NULL;

  status = bs->LocateHandle(ByProtocol, &BlockIoProtocolGUID, NULL, &bufferSize, handles);
  if (EFI_ERROR(status)) {
    conOut->OutputString(conOut, L"LocateHandle failed\r\n");
    return status;
  }

  status = bs->HandleProtocol(handles[0], &SimpleFileSystemGUID, (void **)&fs);
  if (EFI_ERROR(status)) {
    conOut->OutputString(conOut, L"No filesystem on handle 0: 0x");
    printHex(conOut, status, 8);
    conOut->OutputString(conOut, L"\r\n");
    bs->WaitForEvent(1, &event, &index);
    return status;
  }

  status = fs->OpenVolume(fs, &root);
  if (EFI_ERROR(status)) {
    conOut->OutputString(conOut, L"OpenVolume failed: 0x");
    printHex(conOut, status, 8);
    conOut->OutputString(conOut, L"\r\n");
    bs->WaitForEvent(1, &event, &index);
    return status;
  }

  status = root->Open(root, &file, L"vmlinuz", EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(status)) {
    conOut->OutputString(conOut, L"Failed to open vmlinuz: 0x");
    printHex(conOut, status, 8);
    conOut->OutputString(conOut, L"\r\n");
    root->Close(root);
    bs->WaitForEvent(1, &event, &index);
    return status;
  }

  conOut->OutputString(conOut, L"vmlinuz opened\r\n");

  UINT8 infoBuffer[512];
  UINTN infoSize = sizeof(infoBuffer);
  EFI_GUID fileInfoGuid = EFI_FILE_INFO_ID;
  
  status = file->GetInfo(file, &fileInfoGuid, &infoSize, infoBuffer);
  if (EFI_ERROR(status)) {
    conOut->OutputString(conOut, L"GetInfo failed: 0x");
    printHex(conOut, status, 8);
    conOut->OutputString(conOut, L"\r\n");
    file->Close(file);
    root->Close(root);
    bs->WaitForEvent(1, &event, &index);
    return status;
  }

  EFI_FILE_INFO *fileInfo = (EFI_FILE_INFO *)infoBuffer;
  kernelSize = fileInfo->FileSize;
  
  conOut->OutputString(conOut, L"Kernel size: ");
  printInt(conOut, (int)kernelSize);
  conOut->OutputString(conOut, L" bytes\r\n");

  status = bs->AllocatePool(EfiLoaderData, kernelSize, (void **)&kernel);
  if (EFI_ERROR(status)) {
    conOut->OutputString(conOut, L"AllocatePool failed: 0x");
    printHex(conOut, status, 8);
    conOut->OutputString(conOut, L"\r\n");
    file->Close(file);
    root->Close(root);
    bs->WaitForEvent(1, &event, &index);
    return status;
  }

  status = file->Read(file, &kernelSize, kernel);
  if (EFI_ERROR(status)) {
    conOut->OutputString(conOut, L"Read kernel failed: 0x");
    printHex(conOut, status, 8);
    conOut->OutputString(conOut, L"\r\n");
    bs->FreePool(kernel);
    file->Close(file);
    root->Close(root);
    bs->WaitForEvent(1, &event, &index);
    return status;
  }

  conOut->OutputString(conOut, L"Kernel loaded at 0x");
  printHex(conOut, (UINT64)kernel, 16);
  conOut->OutputString(conOut, L"\r\n");

  file->Close(file);

  status = root->Open(root, &file, L"initramfs", EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(status)) {
    conOut->OutputString(conOut, L"Failed to open initramfs: 0x");
    printHex(conOut, status, 8);
    conOut->OutputString(conOut, L"\r\n");
    bs->FreePool(kernel);
    root->Close(root);
    bs->WaitForEvent(1, &event, &index);
    return status;
  }

  conOut->OutputString(conOut, L"initramfs opened\r\n");

  infoSize = sizeof(infoBuffer);
  status = file->GetInfo(file, &fileInfoGuid, &infoSize, infoBuffer);
  if (EFI_ERROR(status)) {
    conOut->OutputString(conOut, L"GetInfo initramfs failed: 0x");
    printHex(conOut, status, 8);
    conOut->OutputString(conOut, L"\r\n");
    file->Close(file);
    bs->FreePool(kernel);
    root->Close(root);
    bs->WaitForEvent(1, &event, &index);
    return status;
  }

  fileInfo = (EFI_FILE_INFO *)infoBuffer;
  initramfsSize = fileInfo->FileSize;
  
  conOut->OutputString(conOut, L"Initramfs size: ");
  printInt(conOut, (int)initramfsSize);
  conOut->OutputString(conOut, L" bytes\r\n");

  status = bs->AllocatePool(EfiLoaderData, initramfsSize, (void **)&initramfs);
  if (EFI_ERROR(status)) {
    conOut->OutputString(conOut, L"AllocatePool initramfs failed: 0x");
    printHex(conOut, status, 8);
    conOut->OutputString(conOut, L"\r\n");
    file->Close(file);
    bs->FreePool(kernel);
    root->Close(root);
    bs->WaitForEvent(1, &event, &index);
    return status;
  }

  status = file->Read(file, &initramfsSize, initramfs);
  if (EFI_ERROR(status)) {
    conOut->OutputString(conOut, L"Read initramfs failed: 0x");
    printHex(conOut, status, 8);
    conOut->OutputString(conOut, L"\r\n");
    bs->FreePool(initramfs);
    bs->FreePool(kernel);
    file->Close(file);
    root->Close(root);
    bs->WaitForEvent(1, &event, &index);
    return status;
  }

  conOut->OutputString(conOut, L"Initramfs loaded at 0x");
  printHex(conOut, (UINT64)initramfs, 16);
  conOut->OutputString(conOut, L"\r\n");

  file->Close(file);
  root->Close(root);

  g_initramfs = initramfs;
  g_initramfs_size = initramfsSize;

  INITRD_DEVICE_PATH initrdPath;
  initrdPath.vendor.Header.Type = MEDIA_DEVICE_PATH;
  initrdPath.vendor.Header.SubType = MEDIA_VENDOR_DP;
  initrdPath.vendor.Header.Length[0] = sizeof(EFI_VENDOR_DEVICE_PATH) & 0xFF;
  initrdPath.vendor.Header.Length[1] = (sizeof(EFI_VENDOR_DEVICE_PATH) >> 8) & 0xFF;

  UINT8 *guidSrc = (UINT8 *)&LinuxInitrdMediaGUID;
  UINT8 *guidDst = (UINT8 *)&initrdPath.vendor.Guid;
  for (int i = 0; i < sizeof(EFI_GUID); i++) {
    guidDst[i] = guidSrc[i];
  }

  initrdPath.end.Header.Type = END_DEVICE_PATH_TYPE;
  initrdPath.end.Header.SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  initrdPath.end.Header.Length[0] = sizeof(EFI_END_DEVICE_PATH);
  initrdPath.end.Header.Length[1] = 0;

  conOut->OutputString(conOut, L"Installing LoadFile2 protocol...\r\n");
  status = bs->InstallMultipleProtocolInterfaces(&initrdHandle,
                                                  &LoadFile2ProtocolGUID, &initrd_lf2,
                                                  &DevicePathProtocolGUID, &initrdPath,
                                                  NULL);
  if (EFI_ERROR(status)) {
    conOut->OutputString(conOut, L"InstallMultipleProtocolInterfaces failed: 0x");
    printHex(conOut, status, 8);
    conOut->OutputString(conOut, L"\r\n");
    bs->FreePool(initramfs);
    bs->FreePool(kernel);
    bs->WaitForEvent(1, &event, &index);
    return status;
  }

  conOut->OutputString(conOut, L"LoadFile2 protocol installed\r\n");

  struct {
    EFI_MEMORY_MAPPED_DEVICE_PATH mempath;
    EFI_END_DEVICE_PATH end;
  } devicePath;

  devicePath.mempath.Header.Type = HARDWARE_DEVICE_PATH;
  devicePath.mempath.Header.SubType = HW_MEMMAP_DP;
  devicePath.mempath.Header.Length[0] = sizeof(EFI_MEMORY_MAPPED_DEVICE_PATH) & 0xFF;
  devicePath.mempath.Header.Length[1] = (sizeof(EFI_MEMORY_MAPPED_DEVICE_PATH) >> 8) & 0xFF;
  devicePath.mempath.MemoryType = EfiLoaderData;
  devicePath.mempath.StartAddress = (EFI_PHYSICAL_ADDRESS)kernel;
  devicePath.mempath.EndAddress = (EFI_PHYSICAL_ADDRESS)kernel + kernelSize;

  devicePath.end.Header.Type = END_DEVICE_PATH_TYPE;
  devicePath.end.Header.SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
  devicePath.end.Header.Length[0] = sizeof(EFI_END_DEVICE_PATH);
  devicePath.end.Header.Length[1] = 0;

  conOut->OutputString(conOut, L"Calling LoadImage...\r\n");
  status = bs->LoadImage(FALSE, image, (EFI_DEVICE_PATH*)&devicePath, kernel, kernelSize, &kernelHandle);
  if (EFI_ERROR(status)) {
    conOut->OutputString(conOut, L"LoadImage failed: 0x");
    printHex(conOut, status, 8);
    conOut->OutputString(conOut, L"\r\n");
    bs->UninstallMultipleProtocolInterfaces(initrdHandle,
                                             &LoadFile2ProtocolGUID, &initrd_lf2,
                                             &DevicePathProtocolGUID, &initrdPath,
                                             NULL);
    bs->FreePool(initramfs);
    bs->FreePool(kernel);
    bs->WaitForEvent(1, &event, &index);
    return status;
  }

  conOut->OutputString(conOut, L"LoadImage succeeded\r\n");

  status = bs->HandleProtocol(kernelHandle, &LoadedImageProtocolGUID, (void**)&loadedImage);
  if (EFI_ERROR(status)) {
    conOut->OutputString(conOut, L"HandleProtocol LoadedImage failed: 0x");
    printHex(conOut, status, 8);
    conOut->OutputString(conOut, L"\r\n");
    bs->UninstallMultipleProtocolInterfaces(initrdHandle,
                                             &LoadFile2ProtocolGUID, &initrd_lf2,
                                             &DevicePathProtocolGUID, &initrdPath,
                                             NULL);
    bs->FreePool(initramfs);
    bs->FreePool(kernel);
    bs->WaitForEvent(1, &event, &index);
    return status;
  }

  const char *cmdline = "console=ttyS0,115200 console=tty0 modules=loop,squashfs,sd-mod,usb-storage alpine_dev=sda modloop=/modloop-lts";

  UINTN cmdlineLen = strlen_a(cmdline);
  CHAR16 *cmdlineUcs2;
  
  status = bs->AllocatePool(EfiLoaderData, (cmdlineLen + 1) * sizeof(CHAR16), (void**)&cmdlineUcs2);
  if (EFI_ERROR(status)) {
    conOut->OutputString(conOut, L"AllocatePool cmdline failed: 0x");
    printHex(conOut, status, 8);
    conOut->OutputString(conOut, L"\r\n");
    bs->UninstallMultipleProtocolInterfaces(initrdHandle,
                                             &LoadFile2ProtocolGUID, &initrd_lf2,
                                             &DevicePathProtocolGUID, &initrdPath,
                                             NULL);
    bs->FreePool(initramfs);
    bs->FreePool(kernel);
    bs->WaitForEvent(1, &event, &index);
    return status;
  }

  ascii_to_ucs2(cmdlineUcs2, cmdline);
  loadedImage->LoadOptions = cmdlineUcs2;
  loadedImage->LoadOptionsSize = (cmdlineLen + 1) * sizeof(CHAR16);

  conOut->OutputString(conOut, L"Command line set, calling StartImage...\r\n");
  
  UINTN exitDataSize = 0;
  CHAR16 *exitData = NULL;
  status = bs->StartImage(kernelHandle, &exitDataSize, &exitData);
  
  conOut->OutputString(conOut, L"StartImage returned: 0x");
  printHex(conOut, status, 8);
  conOut->OutputString(conOut, L"\r\n");
  
  if (exitData) {
    conOut->OutputString(conOut, L"Exit data: ");
    conOut->OutputString(conOut, exitData);
    conOut->OutputString(conOut, L"\r\n");
  }


  bs->UninstallMultipleProtocolInterfaces(initrdHandle,
                                           &LoadFile2ProtocolGUID, &initrd_lf2,
                                           &DevicePathProtocolGUID, &initrdPath,
                                           NULL);
  bs->FreePool(cmdlineUcs2);
  bs->FreePool(initramfs);
  bs->FreePool(kernel);
  bs->WaitForEvent(1, &event, &index);
  return EFI_SUCCESS;
}
