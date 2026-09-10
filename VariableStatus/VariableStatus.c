/** @file
  VariableStatus.c

  UEFI application that reports the status of an arbitrary UEFI variable:
  whether it exists, its size and attributes, and whether it is locked.

  Usage:
    VariableStatus.efi <VariableName> <VendorGuid>

  Example:
    VariableStatus.efi PchSetup 4570B7F1-ADE8-4943-8DC3-406472842384

  Lock state is queried via the Variable Policy protocol. If that protocol
  is unavailable, or gives no answer, it falls back to a write probe

  Copyright (c) 2026 Alexis Lecam <alexis.lecam@hexaliker.fr>

  SPDX-License-Identifier: MIT
**/

#include <Uefi.h>

#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>

#include <Protocol/VariablePolicy.h>
#include <Protocol/ShellParameters.h>

STATIC
CONST CHAR16 *
LockTypeToString (
  IN UINT8  LockPolicyType
  )
{
  switch (LockPolicyType) {
    case VARIABLE_POLICY_TYPE_NO_LOCK:
      return L"Not locked";

    case VARIABLE_POLICY_TYPE_LOCK_NOW:
      return L"Locked (LOCK_NOW)";

    case VARIABLE_POLICY_TYPE_LOCK_ON_CREATE:
      return L"Locked (LOCK_ON_CREATE)";

    case VARIABLE_POLICY_TYPE_LOCK_ON_VAR_STATE:
      return L"Locked (LOCK_ON_VAR_STATE)";

    default:
      return NULL;
  }
}

//
// Queries the Variable Policy protocol for a lock on the target
// variable
//
// TRUE is returned if a lock was found, FALSE otherwise
//
STATIC
BOOLEAN
TryVariablePolicyQuery (
  IN CONST CHAR16  *VariableName,
  IN CONST EFI_GUID *VendorGuid
  )
{
  EFI_STATUS                      Status;
  EDKII_VARIABLE_POLICY_PROTOCOL  *VariablePolicy;
  VARIABLE_POLICY_ENTRY           *PolicyInfo;
  UINTN                           LockVarNameSize;
  CHAR16                          *LockVarName;

  VariablePolicy = NULL;
  PolicyInfo     = NULL;
  LockVarName    = NULL;

  Status = gBS->LocateProtocol (
                  &gEdkiiVariablePolicyProtocolGuid,
                  NULL,
                  (VOID **)&VariablePolicy
                  );

  if (EFI_ERROR (Status)) {
    Print (
      L"EDKII_VARIABLE_POLICY_PROTOCOL not present (%r)!!!\n",
      Status
      );
    return FALSE;
  }

  //
  // Get buffer size
  //
  LockVarNameSize = 0;

  Status = VariablePolicy->GetVariablePolicyInfo (
                              VariableName,
                              (EFI_GUID *)VendorGuid,
                              &LockVarNameSize,
                              NULL,
                              NULL
                              );

  if (Status == EFI_NOT_FOUND) {
    Print (L"No variable policy is registered for this variable.\n");
    return TRUE;
  }

  if (Status != EFI_BUFFER_TOO_SMALL) {
    Print (
      L"GetVariablePolicyInfo size query failed: %r\n",
      Status
      );
    return FALSE;
  }

  PolicyInfo = AllocateZeroPool (sizeof (VARIABLE_POLICY_ENTRY));

  if (LockVarNameSize > 0) {
    LockVarName = AllocateZeroPool (LockVarNameSize);
  }

  if ((PolicyInfo == NULL) ||
      ((LockVarNameSize > 0) && (LockVarName == NULL)))
  {
    Print (L"Out of memory!!!\n");

    if (PolicyInfo != NULL) {
      FreePool (PolicyInfo);
    }

    if (LockVarName != NULL) {
      FreePool (LockVarName);
    }

    return FALSE;
  }

  //
  // Retrieve the actual policy entry.
  //
  Status = VariablePolicy->GetVariablePolicyInfo (
                              VariableName,
                              (EFI_GUID *)VendorGuid,
                              &LockVarNameSize,
                              PolicyInfo,
                              LockVarName
                              );

  if (EFI_ERROR (Status)) {
    Print (
      L"GetVariablePolicyInfo failed: %r\n",
      Status
      );

    FreePool (PolicyInfo);

    if (LockVarName != NULL) {
      FreePool (LockVarName);
    }

    return FALSE;
  }

  Print (L"Variable policy found!\n");

  Print (
    L"  Lock type  : %s\n",
    LockTypeToString (PolicyInfo->LockPolicyType)
    );

  if ((PolicyInfo->LockPolicyType ==
       VARIABLE_POLICY_TYPE_LOCK_ON_VAR_STATE) &&
      (LockVarName != NULL) &&
      (LockVarNameSize >= sizeof (CHAR16)))
  {
    Print (
      L"  Depends on : %s\n",
      LockVarName
      );
  }

  FreePool (PolicyInfo);

  if (LockVarName != NULL) {
    FreePool (LockVarName);
  }

  return TRUE;
}

//
// Fallback that re-writes the variable with its current contents
//
// EFI_WRITE_PROTECTED means that the variable service rejected the write
// because the variable is protected.
//
// it is important to note that a successful SetVariable call does not necessarily mean that the
// variable has no policy restrictions, it only means that this particular write was accepted,
// or was reported as accepted, but actually skipped
//
STATIC
VOID
WriteProbe (
  IN CONST CHAR16   *VariableName,
  IN CONST EFI_GUID *VendorGuid,
  IN UINT32         Attributes,
  IN UINTN          DataSize
  )
{
  EFI_STATUS  Status;
  VOID        *Buffer;
  UINTN       ReadSize;

  if (DataSize == 0) {
    Print (L"Write probe skipped: variable has zero size.\n");
    return;
  }

  Buffer = AllocateZeroPool (DataSize);
  if (Buffer == NULL) {
    Print (L"Out of memory.\n");
    return;
  }

  //
  // Read the current contents so that the probe can attempt to write the
  // exact same value back.
  //
  ReadSize = DataSize;

  Status = gRT->GetVariable (
                  (CHAR16 *)VariableName,
                  (EFI_GUID *)VendorGuid,
                  NULL,
                  &ReadSize,
                  Buffer
                  );

  if (EFI_ERROR (Status)) {
    Print (
      L"Write probe: read-back failed: %r\n",
      Status
      );

    FreePool (Buffer);
    return;
  }

  Status = gRT->SetVariable (
                  (CHAR16 *)VariableName,
                  (EFI_GUID *)VendorGuid,
                  Attributes,
                  ReadSize,
                  Buffer
                  );

  FreePool (Buffer);

  if (Status == EFI_SECURITY_VIOLATION) {
    Print (
      L"Write probe: variable is LOCKED #1 (EFI_SECURITY_VIOLATION).\n"
      );
  } else if (Status == EFI_WRITE_PROTECTED) {
    Print (
      L"Write probe: variable is LOCKED #2 (EFI_WRITE_PROTECTED).\n"
      );
  } else if (Status == EFI_SUCCESS) {
    Print (
      L"Write probe: variable seems UNLOCKED (EFI_SUCCESS).\n"
      );
  } else {
    Print (
      L"Write probe: unknown status (%r)\n",
      Status
      );
  }
}

STATIC
VOID
PrintVariableAttributes (
  IN UINT32  Attributes
  )
{
  Print (
    L"  Attributes : 0x%08x\n",
    Attributes
    );

  if ((Attributes & EFI_VARIABLE_NON_VOLATILE) != 0) {
    Print (L"    - NON_VOLATILE\n");
  }

  if ((Attributes & EFI_VARIABLE_BOOTSERVICE_ACCESS) != 0) {
    Print (L"    - BOOTSERVICE_ACCESS\n");
  }

  if ((Attributes & EFI_VARIABLE_RUNTIME_ACCESS) != 0) {
    Print (L"    - RUNTIME_ACCESS\n");
  }

  if ((Attributes & EFI_VARIABLE_HARDWARE_ERROR_RECORD) != 0) {
    Print (L"    - HARDWARE_ERROR_RECORD\n");
  }

  if ((Attributes & EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS) != 0) {
    Print (L"    - AUTHENTICATED_WRITE_ACCESS\n");
  }

  if ((Attributes & EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS) != 0) {
    Print (L"    - TIME_BASED_AUTHENTICATED_WRITE_ACCESS\n");
  }

  if ((Attributes & EFI_VARIABLE_APPEND_WRITE) != 0) {
    Print (L"    - APPEND_WRITE\n");
  }

  // if ((Attributes & EFI_VARIABLE_ENHANCED_AUTHENTICATED_ACCESS) != 0) {
  //   Print (L"    - ENHANCED_AUTHENTICATED_ACCESS\n");
  // }
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                    Status;
  EFI_SHELL_PARAMETERS_PROTOCOL *ShellParameters;
  CHAR16                        *VariableName;
  EFI_GUID                      VendorGuid;
  UINTN                         DataSize;
  UINT32                        Attributes;
  BOOLEAN                       PolicyAnswered;
  VOID                          *TemporaryVariableData;

  ShellParameters = NULL;
  VariableName    = NULL;
  DataSize        = 0;
  Attributes      = 0;

  //
  // Obtain shell arguments
  //
  Status = gBS->OpenProtocol (
                  ImageHandle,
                  &gEfiShellParametersProtocolGuid,
                  (VOID **)&ShellParameters,
                  ImageHandle,
                  NULL,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );

  if (EFI_ERROR (Status) || (ShellParameters == NULL)) {
    Print (L"Unable to obtain shell parameters: %r\n", Status);
    goto Usage;
    return EFI_INVALID_PARAMETER;
  }

  if (ShellParameters->Argc != 3) {
    Print (L"Invalid number of arguments.\n\n");
    goto Usage;

    return EFI_INVALID_PARAMETER;
  }

  VariableName = ShellParameters->Argv[1];

  if ((VariableName == NULL) || (VariableName[0] == L'\0')) {
    Print (L"Variable name cannot be empty.\n");
    return EFI_INVALID_PARAMETER;
  }

  //
  // Convert the second argument from the conventional GUID string format
  //
  Status = StrToGuid (
             ShellParameters->Argv[2],
             &VendorGuid
             );

  if (EFI_ERROR (Status)) {
    Print (
      L"Invalid Vendor GUID \"%s\": %r\n",
      ShellParameters->Argv[2],
      Status
      );
    return EFI_INVALID_PARAMETER;
  }

  Print (
    L"Checking variable %s (GUID %g)\n\n",
    VariableName,
    &VendorGuid
    );

  //
  // Existence, size, and attributes
  //
  Status = gRT->GetVariable (
                  VariableName,
                  &VendorGuid,
                  &Attributes,
                  &DataSize,
                  NULL
                  );

  if (Status == EFI_NOT_FOUND) {
    Print (L"Variable does not exist on this system!!!\n");

    //
    // Even if the variable does not currently exist, Variable Policy may
    // have a LOCK_ON_CREATE policy registered for it. Therefore, still
    // query the policy database.
    //
    Print (L"\nChecking Variable Policy...\n");

    PolicyAnswered = TryVariablePolicyQuery (
                       VariableName,
                       &VendorGuid
                       );

    if (!PolicyAnswered) {
      Print (L"No Variable Policy seems to be registered for this variable.\n");
    }

    return EFI_NOT_FOUND;
  }

  if (Status != EFI_BUFFER_TOO_SMALL) {
    Print (
      L"GetVariable probe failed: %r\n",
      Status
      );
    return Status;
  }

  if (Attributes == 0x0) {
    Print (L"Pre UEFI spec 2.8 firmware detected: Attributes wasn't populated with EFI_BUFFER_TOO_SMALL.\n");
    TemporaryVariableData = AllocatePool(DataSize);
    if (TemporaryVariableData == NULL) {
      Print (
        L"AllocatePool failed: %r\n",
        Status
        );
      return Status;
    }

    Status = gRT->GetVariable (
                  VariableName,
                  &VendorGuid,
                  &Attributes,
                  &DataSize,
                  TemporaryVariableData
                  );

    FreePool(TemporaryVariableData);
    if (EFI_ERROR(Status)) {
      Print (
        L"GetVariable probe failed: %r\n",
        Status
        );
      return Status;
    }
  }

  Print (L"Variable exists.\n");
  Print (L"  Size       : %u bytes\n", (UINT32)DataSize);

  PrintVariableAttributes (Attributes);

  Print (L"\n");

  PolicyAnswered = TryVariablePolicyQuery (
                     VariableName,
                     &VendorGuid
                     );

  //
  // If Variable Policy is unavailable or could not answer the query,
  // perform the write probe
  //
  if (!PolicyAnswered) {
    Print (L"\nFalling back to write probe...\n");

    WriteProbe (
      VariableName,
      &VendorGuid,
      Attributes,
      DataSize
      );
  }

  return EFI_SUCCESS;

Usage:
  Print (L"Usage:\n");
  Print (L"  VariableStatus.efi <VariableName> <VendorGuid>\n");

  Print (L"\nExample:\n");
  Print (
    L"  VariableStatus.efi PchSetup 4570B7F1-ADE8-4943-8DC3-406472842384\n"
    );

  return EFI_INVALID_PARAMETER;
}