# Unzips the Windows package into a new folder and starts its window there, with a PATH that holds only Windows' own
# folders. Checks that Windows starts it without a console, that it keeps running, that it loaded Qt and its platform
# plugin from the package, and that every library the package holds and every Visual C++ runtime library came from the
# package. GitHub's Windows runners have the runtime in System32, which Windows searches after the program's folder.
#
# Usage: tools/check-windows-package.ps1 <zip>
param([Parameter(Mandatory)] [string] $Zip)
$ErrorActionPreference = 'Stop'

$folder = (New-Item -ItemType Directory (Join-Path $PWD "kaepora-$([guid]::NewGuid())")).FullName
Expand-Archive $Zip -DestinationPath $folder
$exe = Join-Path $folder 'kaepora.exe'

# The subsystem in the program's PE header: 2 is a window program, 3 a console one.
$bytes = [IO.File]::ReadAllBytes($exe)
$subsystem = [BitConverter]::ToUInt16($bytes, [BitConverter]::ToInt32($bytes, 0x3C) + 0x5C)
if ($subsystem -ne 2) {
    throw "kaepora.exe has subsystem $subsystem, so Windows opens a console for it"
}

$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
$process = Start-Process -FilePath $exe -PassThru
# Long enough for the window to show and its event loop to start.
Start-Sleep -Seconds 5
if ($process.HasExited) {
    throw "The packaged window stopped with exit code $($process.ExitCode)"
}
$loaded = $process.Modules | ForEach-Object FileName | Sort-Object -Unique
Stop-Process $process

$inside = $loaded | Where-Object { $_.StartsWith("$folder\", 'OrdinalIgnoreCase') }
$outside = $loaded | Where-Object { -not $_.StartsWith("$folder\", 'OrdinalIgnoreCase') }
'The window loaded these libraries from the package:'
$inside
'and these from the system:'
$outside
foreach ($name in 'Qt6Widgets.dll', 'platforms\qwindows.dll', 'vcruntime140.dll', 'msvcp140.dll') {
    if ((Join-Path $folder $name) -notin $inside) {
        throw "The window did not load $name from the package"
    }
}
$held = Get-ChildItem $folder -Recurse -Filter *.dll | ForEach-Object Name
foreach ($path in $outside) {
    $name = Split-Path $path -Leaf
    if ($name -in $held -or $name -match '^(concrt|msvcp|vcruntime)\d') {
        throw "$path came from outside the package, which must hold its own copy"
    }
}
