VoyagerCalc Windows staging package (unsigned programs + signing script)
=======================================================================

This folder is made on Linux by packaging/windows/build-windows.sh. The programs inside VoyagerCalc\ are NOT signed yet.
sign-windows.ps1 signs them with Azure Artifact Signing, makes a signed zip, and (if NSIS is installed) builds the
per-user installer from the signed files and signs it too.

On a Windows machine that has the signing tools set up (see docs/SIGNING-WINDOWS.md):

  Work in a folder on the Windows machine's own disk, such as C:\sign or the Desktop. Do not run the script from a
  shared Mac folder (\\Mac\Home\... in Parallels, or a drive mapped to it): it does not run reliably from UNC paths.

  1. Copy this zip to C:\sign (or the Desktop), unzip it there (Extract All...), open the folder.
  2. Make sure you are logged in:   az login --tenant <your-tenant-id>
  3. Double-click sign-windows.cmd   (or:  powershell -ExecutionPolicy Bypass -File .\sign-windows.ps1 )

The result is in the "signed" folder: the signed portable zip and, with NSIS installed (winget install -e --id NSIS.NSIS),
the signed installer. Run the script again after installing NSIS if the installer was skipped; signing twice is harmless.

Useful options (all have defaults): -MetadataPath, -SignTool, -Dlib, -Endpoint, -AccountName, -ProfileName, -TimestampUrl,
-MakeNsis, -OutDir, -SkipInstaller, -SkipAzCheck, -DryRun (shows what would run, signs nothing).

Nothing secret is stored here: authentication is your own az login. No firmware is in this package.
