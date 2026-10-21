<#
.SYNOPSIS
  Signs the VoyagerCalc programs with Azure Artifact Signing (formerly Trusted Signing), then builds and signs the installer.

.DESCRIPTION
  Run this on a Windows machine, from an unzipped "staging" package made by packaging/windows/build-windows.sh
  (the folder holds VoyagerCalc\ with the program, installer.nsi, voyagercalc.ico, staging.json and this script).

  1. checks that you are logged in to Azure (az login)
  2. finds signtool.exe (Windows SDK, x64) and Azure.CodeSigning.Dlib.dll (Artifact Signing client tools)
  3. reads the signing metadata from C:\sign\metadata.json (creates it from the parameters, the signing-defaults.json next to the script, or a prompt)
  4. signs VoyagerCalc.exe, VoyagerCalc-cli.exe and any other VoyagerCalc*.exe/.dll  (Qt and runtime DLLs are third-party and stay as they are)
  5. makes a signed portable zip
  6. if makensis is installed (winget install NSIS.NSIS): builds the per-user installer from the signed files and signs it

  Nothing secret is stored: authentication is your own `az login`. Every default can be overridden with a parameter.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File .\sign-windows.ps1

.EXAMPLE
  .\sign-windows.ps1 -Endpoint https://<region>.codesigning.azure.net -AccountName MyAccount -ProfileName MyProfile
#>
[CmdletBinding()]
param(
    [string]$StagingDir = $PSScriptRoot,
    [string]$MetadataPath = 'C:\sign\metadata.json',
    [string]$SignTool,
    [string]$Dlib,
    [string]$Endpoint,
    [string]$AccountName,
    [string]$ProfileName,
    [string]$TimestampUrl = 'http://timestamp.acs.microsoft.com',
    [string]$MakeNsis,
    [string]$OutDir,
    [switch]$SkipInstaller,
    [switch]$SkipAzCheck,
    [switch]$DryRun
)
Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'

function Say([string]$m)  { Write-Host $m }
function Step([string]$m) { Write-Host ''; Write-Host "== $m" -ForegroundColor Cyan }
function Fail([string]$m) { Write-Host ''; Write-Host "ERROR: $m" -ForegroundColor Red; exit 1 }

# run a native program; stderr is not an error by itself; returns the exit code (or 0 for a dry run)
function Invoke-Native([string]$exe, [string[]]$arguments, [switch]$Quiet) {
    if ($DryRun) { Say ('  [dry run] "' + $exe + '" ' + ($arguments -join ' ')); return 0 }
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    try {
        if ($Quiet) { & $exe @arguments *> $null } else { & $exe @arguments | Out-Host }
        return $LASTEXITCODE
    } finally { $ErrorActionPreference = $old }
}

if (-not $StagingDir) { $StagingDir = (Get-Location).Path }
$StagingDir = (Resolve-Path -LiteralPath $StagingDir).Path

# ---- what is being signed ------------------------------------------------------------------------------------------
$infoFile = Join-Path $StagingDir 'staging.json'
if (-not (Test-Path -LiteralPath $infoFile)) { Fail "staging.json not found in $StagingDir. Run this script from the unzipped staging package." }
$info = Get-Content -LiteralPath $infoFile -Raw | ConvertFrom-Json
$version = [string]$info.version; $arch = [string]$info.arch; $tag = [string]$info.tag
$appDir = Join-Path $StagingDir 'VoyagerCalc'
if (-not (Test-Path -LiteralPath (Join-Path $appDir 'VoyagerCalc.exe'))) { Fail "VoyagerCalc\VoyagerCalc.exe not found in $StagingDir." }
$baseName = "VoyagerCalc-$version-windows-$arch" + $(if ($tag) { "-$tag" } else { '' })
if (-not $OutDir) { $OutDir = Join-Path $StagingDir 'signed' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
Say "VoyagerCalc $version ($arch) staging package in $StagingDir"

# ---- 1. Azure login ------------------------------------------------------------------------------------------------
$defaults = $null
$defFile = Join-Path $StagingDir 'signing-defaults.json'
if (Test-Path -LiteralPath $defFile) { $defaults = Get-Content -LiteralPath $defFile -Raw | ConvertFrom-Json }
function Default([string]$name) { if ($defaults -and ($defaults.PSObject.Properties.Name -contains $name)) { return [string]$defaults.$name } return '' }

Step '1. Azure login'
if ($SkipAzCheck) { Say '  skipped (-SkipAzCheck)' }
else {
    $az = Get-Command az -ErrorAction SilentlyContinue
    if (-not $az) { Fail 'Azure CLI (az) not found. Install it:  winget install -e --id Microsoft.AzureCLI   (then open a new window)' }
    $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    $acctJson = & az account show --output json 2>$null
    $azCode = $LASTEXITCODE
    $ErrorActionPreference = $old
    if ($azCode -ne 0 -or -not $acctJson) {
        $tenant = Default 'tenant'
        $hint = if ($tenant) { "az login --tenant $tenant" } else { 'az login --tenant <your-tenant-id>' }
        Fail "You are not logged in to Azure. Run:  $hint   and then run this script again."
    }
    $acct = ($acctJson -join "`n") | ConvertFrom-Json
    Say ("  logged in as {0} (tenant {1})" -f $acct.user.name, $acct.tenantId)
    $tenant = Default 'tenant'
    if ($tenant -and $acct.tenantId -ne $tenant) { Say "  note: the expected tenant is $tenant; if signing fails, run  az login --tenant $tenant" }
}

# ---- 2. signtool and the dlib --------------------------------------------------------------------------------------
Step '2. signtool and the Artifact Signing dlib'
if (-not $SignTool -and $env:SIGNTOOL) { $SignTool = $env:SIGNTOOL }
if (-not $SignTool) {
    $roots = @()
    if (${env:ProgramFiles(x86)}) { $roots += (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin') }
    if ($env:ProgramFiles)        { $roots += (Join-Path $env:ProgramFiles 'Windows Kits\10\bin') }
    $cands = @()
    foreach ($r in $roots) {
        if (Test-Path -LiteralPath $r) {
            foreach ($d in Get-ChildItem -LiteralPath $r -Directory -ErrorAction SilentlyContinue) {
                $p = Join-Path $d.FullName 'x64\signtool.exe'
                if (Test-Path -LiteralPath $p) {
                    $v = [version]'0.0'; [void][version]::TryParse($d.Name, [ref]$v)
                    $cands += [pscustomobject]@{ Version = $v; Path = $p }
                }
            }
        }
    }
    if ($cands.Count -gt 0) { $SignTool = ($cands | Sort-Object Version -Descending | Select-Object -First 1).Path }
    else { $c = Get-Command signtool.exe -ErrorAction SilentlyContinue; if ($c) { $SignTool = $c.Source } }
}
if (-not $SignTool -or -not (Test-Path -LiteralPath $SignTool)) { Fail 'signtool.exe not found. Install the tools:  winget install -e --id Microsoft.Azure.ArtifactSigningClientTools  (it brings the Windows SDK), or pass -SignTool <path>.' }
Say "  signtool: $SignTool"

if (-not $Dlib -and $env:AZURE_CODESIGNING_DLIB) { $Dlib = $env:AZURE_CODESIGNING_DLIB }
if (-not $Dlib) {
    $found = @()
    foreach ($base in @($env:LOCALAPPDATA, $env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if (-not $base) { continue }
        $ms = Join-Path $base 'Microsoft'
        if (Test-Path -LiteralPath $ms) {
            foreach ($d in Get-ChildItem -LiteralPath $ms -Directory -Filter '*SigningClientTools*' -ErrorAction SilentlyContinue) {
                $found += Get-ChildItem -LiteralPath $d.FullName -Recurse -Filter 'Azure.CodeSigning.Dlib.dll' -ErrorAction SilentlyContinue
            }
        }
    }
    # the dlib must match the (x64) signtool: prefer a path with x64 in it, else the one next to the tools
    $pick = $found | Where-Object { $_.FullName -match '[\\/]x64[\\/]' } | Select-Object -First 1
    if (-not $pick) { $pick = $found | Where-Object { $_.FullName -notmatch '[\\/](x86|arm64)[\\/]' } | Select-Object -First 1 }
    if ($pick) { $Dlib = $pick.FullName }
}
if (-not $Dlib -or -not (Test-Path -LiteralPath $Dlib)) { Fail 'Azure.CodeSigning.Dlib.dll not found. Install:  winget install -e --id Microsoft.Azure.ArtifactSigningClientTools   or pass -Dlib <path to the x64 dll>.' }
Say "  dlib:     $Dlib"

# ---- 3. metadata ---------------------------------------------------------------------------------------------------
Step '3. signing metadata'
if (Test-Path -LiteralPath $MetadataPath) {
    $meta = Get-Content -LiteralPath $MetadataPath -Raw | ConvertFrom-Json
    Say "  using $MetadataPath"
} else {
    if (-not $Endpoint)    { $Endpoint    = Default 'endpoint' }
    if (-not $AccountName) { $AccountName = Default 'account' }
    if (-not $ProfileName) { $ProfileName = Default 'profile' }
    if (-not $Endpoint)    { $Endpoint    = Read-Host 'Artifact Signing endpoint (for example https://<region>.codesigning.azure.net)' }
    if (-not $AccountName) { $AccountName = Read-Host 'Artifact Signing account name' }
    if (-not $ProfileName) { $ProfileName = Read-Host 'Certificate profile name' }
    if (-not $Endpoint -or -not $AccountName -or -not $ProfileName) { Fail 'endpoint, account name and certificate profile name are all needed.' }
    $meta = [ordered]@{ Endpoint = $Endpoint; CodeSigningAccountName = $AccountName; CertificateProfileName = $ProfileName }
    if ($DryRun) { Say "  [dry run] would create $MetadataPath" }
    else {
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $MetadataPath) | Out-Null
        ($meta | ConvertTo-Json) | Set-Content -LiteralPath $MetadataPath -Encoding ASCII
        Say "  created $MetadataPath"
    }
}
Say ("  endpoint {0}, account {1}, profile {2}" -f $meta.Endpoint, $meta.CodeSigningAccountName, $meta.CertificateProfileName)

# ---- signing helper ------------------------------------------------------------------------------------------------
function Sign-Files([string[]]$files) {
    foreach ($f in $files) {
        $stArgs = @('sign', '/v', '/fd', 'SHA256', '/tr', $TimestampUrl, '/td', 'SHA256', '/dlib', $Dlib, '/dmdf', $MetadataPath, $f)
        $ok = $false
        for ($try = 1; $try -le 3 -and -not $ok; $try++) {
            $code = Invoke-Native $SignTool $stArgs
            if ($code -eq 0) { $ok = $true } else { Say "  signing $([IO.Path]::GetFileName($f)) failed (exit $code), attempt $try of 3"; Start-Sleep -Seconds (2 * $try) }
        }
        if (-not $ok) { Fail "Could not sign $f. Check: az login, the role 'Artifact Signing Certificate Profile Signer' on the signing account, and the metadata." }
        $v = Invoke-Native $SignTool @('verify', '/pa', $f) -Quiet
        if ($v -ne 0) { Fail "Signature of $f does not verify (signtool verify /pa exit $v)." }
        Say "  signed and verified: $([IO.Path]::GetFileName($f))"
    }
}
function Hashes([string]$file) {
    if ($DryRun -and -not (Test-Path -LiteralPath $file)) { return }
    $md5 = (Get-FileHash -Algorithm MD5 -LiteralPath $file).Hash.ToLower()
    $sha = (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash.ToLower()
    Say ("  {0}  {1:N0} bytes" -f $file, (Get-Item -LiteralPath $file).Length)
    Say "    md5    $md5"
    Say "    sha256 $sha"
    Set-Content -LiteralPath ($file + '.md5') -Value ("$md5  " + [IO.Path]::GetFileName($file)) -Encoding ASCII
}

# ---- 4. sign the programs ------------------------------------------------------------------------------------------
Step '4. signing the VoyagerCalc programs'
$ours = @(Get-ChildItem -LiteralPath $appDir -File | Where-Object { $_.Name -like 'VoyagerCalc*' -and $_.Extension -in '.exe', '.dll' } | ForEach-Object { $_.FullName })
if ($ours.Count -eq 0) { Fail 'no VoyagerCalc*.exe found to sign.' }
Sign-Files $ours

# ---- 5. signed portable zip ----------------------------------------------------------------------------------------
Step '5. signed portable zip'
$zip = Join-Path $OutDir ($baseName + '-signed.zip')
if ($DryRun) { Say "  [dry run] would create $zip" }
else {
    # Entries are written one by one with forward slashes ("VoyagerCalc/platforms/qwindows.dll"). ZipFile.CreateFromDirectory
    # in Windows PowerShell 5.1 (.NET Framework) writes backslashes, which Linux/macOS unzip and some tools treat as part of
    # the file name (0.7.0: the zips had to be re-packed by hand).
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    $root = (Resolve-Path -LiteralPath $appDir).Path.TrimEnd('\', '/')
    $top = Split-Path -Leaf $root
    $fs = [IO.File]::Open($zip, [IO.FileMode]::CreateNew)
    try {
        $za = New-Object IO.Compression.ZipArchive($fs, [IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($f in Get-ChildItem -LiteralPath $root -Recurse -File | Sort-Object FullName) {
                $rel = $f.FullName.Substring($root.Length + 1).Replace('\', '/')
                [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($za, $f.FullName, "$top/$rel", [IO.Compression.CompressionLevel]::Optimal)
            }
        } finally { $za.Dispose() }
    } finally { $fs.Dispose() }
    # check: no entry may contain a backslash
    $chk = [IO.Compression.ZipFile]::OpenRead($zip)
    try { $bad = @($chk.Entries | Where-Object { $_.FullName.Contains('\') }) } finally { $chk.Dispose() }
    if ($bad.Count -gt 0) { Fail "zip has entries with backslashes: $($bad[0].FullName)" }
    Hashes $zip
}

# ---- 6. installer --------------------------------------------------------------------------------------------------
Step '6. installer'
$setup = Join-Path $OutDir ($baseName + '-setup-signed.exe')
if ($SkipInstaller) { Say '  skipped (-SkipInstaller)' }
else {
    if (-not $MakeNsis -and $env:MAKENSIS) { $MakeNsis = $env:MAKENSIS }
    if (-not $MakeNsis) {
        $c = Get-Command makensis.exe -ErrorAction SilentlyContinue
        if ($c) { $MakeNsis = $c.Source }
        else {
            foreach ($b in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
                if ($b -and (Test-Path -LiteralPath (Join-Path $b 'NSIS\makensis.exe'))) { $MakeNsis = Join-Path $b 'NSIS\makensis.exe'; break }
            }
        }
    }
    if (-not $MakeNsis -or -not (Test-Path -LiteralPath $MakeNsis)) {
        Say '  makensis (NSIS) is not installed, so no installer was built. The programs are signed and the signed zip is ready.'
        Say '  To get the installer too:  winget install -e --id NSIS.NSIS   then run this script again (signing twice is harmless).'
    } else {
        Say "  makensis: $MakeNsis"
        $nsi = Join-Path $StagingDir 'installer.nsi'
        $ico = Join-Path $StagingDir 'voyagercalc.ico'
        if (-not (Test-Path -LiteralPath $nsi) -or -not (Test-Path -LiteralPath $ico)) { Fail 'installer.nsi / voyagercalc.ico missing from the staging folder.' }
        $code = Invoke-Native $MakeNsis @('-V2', "-DVERSION=$version", "-DSRCDIR=$appDir", "-DOUTFILE=$setup", "-DARCH=$arch", "-DICON=$ico", $nsi)
        if ($code -ne 0) { Fail "makensis failed (exit $code)." }
        Sign-Files @($setup)
        Hashes $setup
    }
}

Step 'Done'
Say "Signed files are in $OutDir"
if (-not $DryRun) { Get-ChildItem -LiteralPath $OutDir -File | Where-Object { $_.Extension -ne '.md5' } | ForEach-Object { Say ('  ' + $_.Name) } }
