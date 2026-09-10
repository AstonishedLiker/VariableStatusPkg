# VariableStatusPkg

<div>
  <img alt="Hit Counter Badge" src="https://hitscounter.dev/api/hit?url=https%3A%2F%2Fgithub.com%2FAstonishedLiker%2FVariableStatusPkg&label=views+%28day+%2F+all+time%29&icon=eye-fill&color=%232362a0&message=&style=for-the-badge&tz=UTC">
  <img alt="CI Badge" src="https://img.shields.io/github/actions/workflow/status/AstonishedLiker/VariableStatusPkg/edkii-package-ci.yml?style=for-the-badge&label=CI">
  <br/>
  <br/>
</div>

**VariableStatusPkg** is a small UEFI Shell application for inspecting the runtime status of an arbitrary UEFI variable.

It reports whether the variable exists, its size and attributes, and whether a variable policy indicates that it is locked. It can be used with variables from any vendor GUID and does not require the variable to be known to the application beforehand.

## Features

* Check whether a UEFI variable exists
* Support arbitrary variable names and vendor GUIDs
* Display the variable's data size
* Decode and display its UEFI variable attributes
* Query `EDKII_VARIABLE_POLICY_PROTOCOL` for lock information (when exposed)
    * Report the lock policy type when a policy is registered
    * Handle `LOCK_ON_VAR_STATE` policies by displaying the variable the policy depends on
    * Fall back to a write probe when Variable Policy is unavailable or cannot answer the query
    * Check variables that do not currently exist, including variables with a registered `LOCK_ON_CREATE` policy

## Usage

### 1. Obtain `VariableStatus.efi`

Download the [latest release](https://github.com/AstonishedLiker/VariableStatusPkg/releases/latest) and copy `VariableStatus.efi` to a filesystem accessible from the UEFI Shell.

### 2. Boot into a UEFI Shell

If your firmware does not provide a built-in UEFI Shell, you can build one using [TianoCore EDK II](https://github.com/tianocore/edk2).

The following example assumes Debian 13 (Trixie):

```sh
# Clone EDK II
git clone https://github.com/tianocore/edk2.git
cd edk2
git submodule update --init --recursive --depth 1

# Install build dependencies
sudo apt-get update
sudo apt-get install -y build-essential uuid-dev iasl nasm python3 python3-setuptools

# Build BaseTools and initialize the environment
make -C BaseTools
source edksetup.sh BaseTools

# Build the UEFI Shell
# Replace <ARCH> with IA32, X64, EBC, AARCH64, RISCV64, or LOONGARCH64.
build ShellPkg/ShellPkg.dsc -a <ARCH> -t GCC -b RELEASE
```

The resulting Shell EFI binary can then be placed on a FAT-formatted device and booted through your firmware's UEFI boot manager.

### 3. Locate the application

Once inside the UEFI Shell, use `map` to identify the filesystem containing `VariableStatus.efi`. It will commonly be available as `fs0:`, although the mapping can vary between systems.

For example:

```text
UEFI Interactive Shell v2.2
EDK II
UEFI v2.40 (Lenovo, 0x00001403)

...

Shell> fs0:
FS0:\> ls

Directory of FS0:\

09/05/2026  17:04 <DIR>         8,192  EFI
09/05/2026  17:04              20,480  VariableStatus.efi

               1 File(s)     20,480 bytes
               1 Dir(s)
```

### 4. Inspect a variable

Run the application with the variable name followed by its vendor GUID:

```text
FS0:\> VariableStatus.efi <VariableName> <VendorGuid>
```

For example, to inspect the `PchSetup` variable:

```text
FS0:\> VariableStatus.efi PchSetup 4570B7F1-ADE8-4943-8DC3-406472842384
```

The GUID must be provided in the conventional UEFI GUID format.

## Example output

For an existing variable, the application reports information similar to:

```text
Checking variable PchSetup (GUID 4570B7F1-ADE8-4943-8DC3-406472842384)

Pre UEFI spec 2.8 firmware detected: Attributes wasn't populated with EFI_BUFFER_TOO_SMALL.
Variable exists.
  Size       : 1394 bytes
  Attributes : 0x00000003
    - NON_VOLATILE
    - BOOTSERVICE_ACCESS

EDKII_VARIABLE_POLICY_PROTOCOL not present (Not Found)!!!

Falling back to write probe...
Write probe: variable is LOCKED #2 (EFI_WRITE_PROTECTED)
```

The exact output depends on the variable and the firmware implementation.

## Lock detection

VariableStatusPkg uses two mechanisms to determine whether a variable is locked.

### Variable Policy

The application first attempts to locate the EDK II `EDKII_VARIABLE_POLICY_PROTOCOL`.

When available, it queries the policy database for the specified variable and reports the registered lock policy. Supported policy types include:

* `NO_LOCK`
* `LOCK_NOW`
* `LOCK_ON_CREATE`
* `LOCK_ON_VAR_STATE`

For `LOCK_ON_VAR_STATE`, the application also reports the variable whose state controls when the target variable becomes locked.

A variable does not necessarily need to exist for a policy to be found. This is specifically relevant for `LOCK_ON_CREATE`, where a policy may already be registered for a variable _that has not yet been created_.

### Write probe fallback

If Variable Policy is unavailable or cannot answer the query, the application falls back to a write probe

The probe:

1. Reads the variable's current contents
2. Attempts to write the same contents back using `SetVariable()`
3. Interprets the resulting EFI status

In particular:

* `EFI_SECURITY_VIOLATION` indicates that the write was rejected as protected
* `EFI_WRITE_PROTECTED` indicates that the variable service rejected the write as write-protected
* `EFI_SUCCESS` means the write was accepted and the variable **appears to be unlocked**

The write probe is less precise than querying Variable Policy. A successful `SetVariable()` call does not necessarily prove that a variable has no policy restrictions, it only indicates that the particular write was accepted, or that the firmware reported it as accepted.

## Variable attributes

For an existing variable, VariableStatusPkg displays its raw attribute bitmask and decodes the attributes currently set.

Supported attributes include:

* `EFI_VARIABLE_NON_VOLATILE`
* `EFI_VARIABLE_BOOTSERVICE_ACCESS`
* `EFI_VARIABLE_RUNTIME_ACCESS`
* `EFI_VARIABLE_HARDWARE_ERROR_RECORD`
* `EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS`
* `EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS`
* `EFI_VARIABLE_APPEND_WRITE`
* `EFI_VARIABLE_ENHANCED_AUTHENTICATED_ACCESS` (Disabled, uncomment in source to enable)

## Compatibility

VariableStatusPkg is designed to run as a UEFI application from the UEFI Shell.

The application is compatible with older firmware implementations that do not populate the variable attributes during the initial `GetVariable()` size query; when this behavior is detected, it performs an additional `GetVariable()` call to retrieve the attributes.

The Variable Policy functionality is dependent on the firmware exposing `EDKII_VARIABLE_POLICY_PROTOCOL`. If that protocol is not available, the tool will use the write-probe fallback.

## Building

This project is built using the [TianoCore EDK II](https://github.com/tianocore/edk2) build system.

After setting up an EDK II build environment, build the package with:

```sh
build VariableStatusPkg/VariableStatusPkg.dsc -a <ARCH> -t GCC -b RELEASE
```

Replace `<ARCH>` with the target architecture, such as `X64` or `IA32`.

The resulting `VariableStatus.efi` can be copied to a FAT-formatted USB drive or another filesystem accessible from the UEFI Shell.

## Contributing

Contributions are welcome!

By contributing to this repository, you agree that your contributions will be licensed under the repository's [MIT License](./License).

## License

VariableStatusPkg is licensed under the [MIT License](./License).

The project uses components from [TianoCore EDK II](https://github.com/tianocore/edk2), which are licensed under the [BSD-2-Clause-Patent License](./License.edk2).
