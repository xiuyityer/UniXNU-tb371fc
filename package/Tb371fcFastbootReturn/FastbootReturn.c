// SPDX-License-Identifier: BSD-2-Clause-Patent
// Qualcomm RESET_PARAM ABI used by the pinned edk2-msm SimpleInit reboot helper.
// Refuse to reset unless gRT->ResetSystem belongs to the loaded Qualcomm module.
// No partition, block device, GPT, direct MMIO or firmware image is accessed.
#include <Uefi.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/DevicePath.h>

typedef struct {
  CHAR16 Signature[12];
  UINT8 Reason;
} TB371FC_RESET_DATA;

STATIC CONST TB371FC_RESET_DATA mFastboot = { L"RESET_PARAM", 2 };
STATIC EFI_GUID mLoadedImageGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
STATIC CONST EFI_GUID mVendorResetGuid =
  { 0x3ae17db7, 0x3cc5, 0x4b89, { 0x93, 0x04, 0x95, 0x49, 0x21, 0x10, 0x57, 0xef } };

STATIC BOOLEAN SameGuid (CONST EFI_GUID *A, CONST EFI_GUID *B)
{
  UINTN Index;
  for (Index = 0; Index < sizeof (EFI_GUID); Index++) {
    if (((CONST UINT8 *)A)[Index] != ((CONST UINT8 *)B)[Index]) { return FALSE; }
  }
  return TRUE;
}

STATIC BOOLEAN VendorFilePath (CONST EFI_DEVICE_PATH_PROTOCOL *Path)
{
  UINTN Index;
  UINTN Total;
  UINTN Length;
  CONST UINT8 *Raw;
  BOOLEAN Vendor;

  Total = 0;
  Vendor = FALSE;
  if (Path == NULL) { return FALSE; }
  // Loaded-image device paths are firmware-owned. Bound traversal rather than
  // assuming an unterminated/malformed path is a valid provider identity.
  for (Index = 0; Index < 64; Index++) {
    Raw = (CONST UINT8 *)Path;
    Length = Raw[2] | ((UINTN)Raw[3] << 8);
    if ((Length < sizeof (*Path)) || (Length > 4096 - Total)) { return FALSE; }
    if ((Path->Type == MEDIA_DEVICE_PATH) && (Path->SubType == MEDIA_PIWG_FW_FILE_DP)) {
      if (Length != sizeof (MEDIA_FW_VOL_FILEPATH_DEVICE_PATH)) { return FALSE; }
      Vendor = SameGuid (&((CONST MEDIA_FW_VOL_FILEPATH_DEVICE_PATH *)Path)->FvFileName, &mVendorResetGuid);
    }
    if (Path->Type == END_DEVICE_PATH_TYPE) {
      return (Path->SubType == END_ENTIRE_DEVICE_PATH_SUBTYPE) &&
             (Length == sizeof (*Path)) && Vendor;
    }
    Total += Length;
    if (Total > 4096 - sizeof (*Path)) { return FALSE; }
    Path = (CONST EFI_DEVICE_PATH_PROTOCOL *)(Raw + Length);
  }
  return FALSE;
}

STATIC BOOLEAN VendorResetActive (EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_HANDLE *Handles;
  EFI_LOADED_IMAGE_PROTOCOL *Loaded;
  UINTN Count;
  UINTN Index;
  UINTN Function;
  UINTN Base;
  BOOLEAN Vendor;

  if ((SystemTable->BootServices == NULL) ||
      (SystemTable->BootServices->LocateHandleBuffer == NULL) ||
      (SystemTable->BootServices->HandleProtocol == NULL) ||
      (SystemTable->BootServices->FreePool == NULL)) { return FALSE; }
  Handles = NULL;
  Count = 0;
  Vendor = FALSE;
  Function = (UINTN)SystemTable->RuntimeServices->ResetSystem;
  Status = SystemTable->BootServices->LocateHandleBuffer (ByProtocol, &mLoadedImageGuid, NULL, &Count, &Handles);
  if (EFI_ERROR (Status)) { return FALSE; }
  if ((Handles == NULL) || (Count > 4096)) {
    if (Handles != NULL) { SystemTable->BootServices->FreePool (Handles); }
    return FALSE;
  }
  for (Index = 0; Index < Count; Index++) {
    Loaded = NULL;
    Status = SystemTable->BootServices->HandleProtocol (Handles[Index], &mLoadedImageGuid, (VOID **)&Loaded);
    if (EFI_ERROR (Status) || (Loaded == NULL) || (Loaded->ImageBase == NULL)) { continue; }
    Base = (UINTN)Loaded->ImageBase;
    if ((Function >= Base) && ((UINT64)(Function - Base) < Loaded->ImageSize) &&
        VendorFilePath (Loaded->FilePath)) {
      Vendor = TRUE;
      break;
    }
  }
  SystemTable->BootServices->FreePool (Handles);
  return Vendor;
}

STATIC VOID Message (EFI_SYSTEM_TABLE *SystemTable, CHAR16 *Text)
{
  if ((SystemTable->ConOut != NULL) && (SystemTable->ConOut->OutputString != NULL)) {
    SystemTable->ConOut->OutputString (SystemTable->ConOut, Text);
  }
}

EFI_STATUS
EFIAPI
UefiMain (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  (VOID)ImageHandle;
  if ((SystemTable == NULL) || (SystemTable->RuntimeServices == NULL) ||
      (SystemTable->RuntimeServices->ResetSystem == NULL)) { return EFI_UNSUPPORTED; }
  if (!VendorResetActive (SystemTable)) {
    Message (SystemTable, L"TB371FC: reset provider is not the verified Qualcomm module; reset REFUSED.\r\n");
    return EFI_UNSUPPORTED;
  }
  Message (SystemTable, L"TB371FC: reset provider = Qualcomm ResetRuntimeDxe (verified loaded image).\r\n");
  Message (SystemTable, L"TB371FC: request original bootloader fastboot (RESET_PARAM reason 2).\r\n");
  if (SystemTable->BootServices->Stall != NULL) { SystemTable->BootServices->Stall (2000000); }
  SystemTable->RuntimeServices->ResetSystem (EfiResetCold, EFI_SUCCESS,
                                            sizeof (mFastboot), (VOID *)&mFastboot);
  Message (SystemTable, L"TB371FC: reset service returned; fastboot return NOT verified.\r\n");
  return EFI_DEVICE_ERROR;
}
