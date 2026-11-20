# Installs the Windows installer silently, checks that it added Kaepora to the Start menu and to the user's installed
# programs, starts the installed window with tools/check-windows-package.ps1, then uninstalls it silently and checks
# that the folder and both entries are gone and the window's settings stayed.
#
# Usage: tools/check-windows-installer.ps1 <installer>
param([Parameter(Mandatory)] [string] $Installer)
$ErrorActionPreference = 'Stop'

$quiet = '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART'
$shortcut = Join-Path ([Environment]::GetFolderPath('Programs')) 'Kaepora.lnk'
# Inno Setup names the entry after the installer's AppId.
$entry = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Kaepora_is1'

$setup = Start-Process -FilePath (Resolve-Path $Installer) -ArgumentList $quiet -Wait -PassThru
if ($setup.ExitCode -ne 0) {
    throw "The installer stopped with exit code $($setup.ExitCode)"
}
if (-not (Test-Path $entry)) {
    throw "The installer added no entry for this user at $entry"
}
$installed = Get-ItemProperty $entry
$folder = $installed.InstallLocation.TrimEnd('\')
"Installed in $folder"
$target = (New-Object -ComObject WScript.Shell).CreateShortcut($shortcut).TargetPath
if ($target -ne (Join-Path $folder 'kaepora.exe')) {
    throw "The Start menu entry $shortcut opens '$target', not Kaepora"
}

& (Join-Path $PSScriptRoot 'check-windows-package.ps1') $folder

$removal = Start-Process -FilePath $installed.UninstallString.Trim('"') -ArgumentList $quiet -Wait -PassThru
if ($removal.ExitCode -ne 0) {
    throw "The uninstaller stopped with exit code $($removal.ExitCode)"
}
# The uninstaller returns while its copy in TEMP still deletes the folder.
foreach ($try in 1..50) {
    if (-not (Test-Path $folder)) {
        break
    }
    Start-Sleep -Milliseconds 200
}
foreach ($left in $folder, $shortcut, $entry) {
    if (Test-Path $left) {
        throw "$left is still there after uninstalling"
    }
}
if (-not (Test-Path 'HKCU:\Software\kaepora')) {
    throw 'Uninstalling removed the settings the window wrote'
}
