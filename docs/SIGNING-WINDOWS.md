# Signing the Windows build (Azure Artifact Signing)

Unsigned builds work, but Windows SmartScreen shows "Windows protected your PC" and the installer shows "Unknown publisher".
This page describes how the release builds are signed with **Azure Artifact Signing** (formerly *Trusted Signing*). Signing needs a
Windows machine (a virtual machine is fine, including Windows 11 ARM); the program itself is built on Linux
(see [BUILD-WINDOWS.md](BUILD-WINDOWS.md)). No secret is stored anywhere: you authenticate with your own `az login`.

Placeholders used below: `<tenant-id>`, `<account-name>`, `<profile-name>`, `<region>` (for example `weu`, `eus`).

## Two stages
The programs must be signed **before** the installer is built (the installer contains them), and then the installer is signed.

1. **On Linux:** `packaging/windows/build-windows.sh x64 <tag>` (and `arm64`) writes, next to the normal unsigned zip and installer,
   `VoyagerCalc-<version>-windows-<arch>-<tag>-staging.zip`: the unsigned program folder `VoyagerCalc\`, `installer.nsi`, the icon,
   `sign-windows.ps1`, `sign-windows.cmd` and a short README. It never contains firmware.
2. **On Windows:** unzip the staging zip and run `sign-windows.cmd` (or `powershell -ExecutionPolicy Bypass -File .\sign-windows.ps1`).
   The script
   * checks that you are logged in to Azure,
   * finds `signtool.exe` and `Azure.CodeSigning.Dlib.dll`,
   * reads `C:\sign\metadata.json` (creates it from the parameters, from a `signing-defaults.json` next to the script, or by asking),
   * signs `VoyagerCalc.exe`, `VoyagerCalc-cli.exe` and any other `VoyagerCalc*.exe` / `.dll`
     (the Qt and runtime DLLs are third-party files and stay as they are),
   * writes a signed portable zip into the `signed` folder,
   * if NSIS (`makensis`) is installed: builds the per-user installer from the signed files, signs it and verifies it.

   Run it again after installing NSIS if the installer was skipped; signing twice is harmless. `-DryRun` shows what would run without
   signing anything; `-SkipInstaller`, `-MetadataPath`, `-SignTool`, `-Dlib`, `-Endpoint`, `-AccountName`, `-ProfileName`,
   `-TimestampUrl`, `-MakeNsis` and `-OutDir` override the defaults.

To give the staging package default signing values without putting them in the repository, set `VC_SIGN_ENDPOINT`,
`VC_SIGN_ACCOUNT`, `VC_SIGN_PROFILE` (and `VC_SIGN_TENANT`) when running `build-windows.sh`; they are written to
`signing-defaults.json` inside the staging zip only.

## One-time setup on the Windows machine
1. **Azure side** (in the Azure portal): an Artifact Signing account in your subscription, a completed identity validation, and a
   certificate profile (for example a *Public Trust* profile). Note the account name, the profile name and the endpoint of the
   account's region (`https://<region>.codesigning.azure.net`).
2. **Role:** your user (or the service principal that signs) needs the role **Artifact Signing Certificate Profile Signer** on the
   certificate profile or the account (Access control (IAM) ▸ Add role assignment). In older portals the role is called
   *Trusted Signing Certificate Profile Signer*.
3. **Tools** (PowerShell; winget ships with Windows 11):

       winget install -e --id Microsoft.AzureCLI
       winget install -e --id Microsoft.Azure.ArtifactSigningClientTools
       winget install -e --id Microsoft.WindowsSDK.10.0.26100
       winget install -e --id NSIS.NSIS

   The client tools bring the signing plug-in (`Azure.CodeSigning.Dlib.dll`, under
   `%LOCALAPPDATA%\Microsoft\...SigningClientTools`) and .NET 8. The Windows SDK brings `signtool.exe`
   (under `C:\Program Files (x86)\Windows Kits\10\bin\<sdk-version>\x64`); a recent SDK (10.0.2261x or newer) is needed.
   NSIS is only needed to build the installer. Open a new terminal window after installing so that `az` is on the path.
   On Windows on ARM the x64 `signtool.exe` and the x64 dlib run under emulation; the script uses that pair.
4. **Login:** `az login --tenant <tenant-id>` (repeat when the login expires).
5. **Metadata:** `C:\sign\metadata.json`, which the script creates if it is missing:

       {
         "Endpoint": "https://<region>.codesigning.azure.net",
         "CodeSigningAccountName": "<account-name>",
         "CertificateProfileName": "<profile-name>"
       }

## What the script runs (to do it by hand)
    & "C:\Program Files (x86)\Windows Kits\10\bin\<sdk-version>\x64\signtool.exe" sign /v /fd SHA256 `
        /tr http://timestamp.acs.microsoft.com /td SHA256 `
        /dlib "$env:LOCALAPPDATA\Microsoft\<ClientTools folder>\Azure.CodeSigning.Dlib.dll" `
        /dmdf C:\sign\metadata.json  VoyagerCalc.exe

then `signtool verify /pa /v VoyagerCalc.exe`. The timestamp matters: the certificates are short-lived, and the timestamp keeps the
signature valid after they expire.

## Checking the result
Right-click a signed file ▸ Properties ▸ Digital Signatures, or `signtool verify /pa /v <file>`. SmartScreen reputation builds up
with downloads, even for signed files; the "Unknown publisher" label is gone at once.

## Troubleshooting
* *"not logged in"*: `az login --tenant <tenant-id>`.
* *403 / "not authorized"*: the role of step 2 is missing on the certificate profile, or the wrong tenant or subscription is active.
* *The dlib cannot be loaded*: .NET 8 is missing, or the dlib and signtool architectures differ (x64 with x64).
* *`signtool` too old*: install the SDK of step 3.
* The NSIS uninstaller inside the installer is not signed separately; the installer's own signature covers the download.
