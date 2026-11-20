# Starts the window of an unzipped or installed Windows package, with a PATH that holds only Windows' own folders.
# Checks that Windows starts it without a console, that its request to GitHub completes over Schannel, that it loaded
# Qt, its platform plugin and the TLS plugin from the package, and that every library the package holds and every
# Visual C++ runtime library came from the package. GitHub's Windows runners have the runtime in System32, which Windows
# searches after the program's folder. Qt's HTTP/2 log names each response that arrives whole, whatever its status, and
# Qt writes its log to stderr only when told to, since the window has no console.
#
# Usage: tools/check-windows-package.ps1 <package folder>
param([Parameter(Mandatory)] [string] $Package)
$ErrorActionPreference = 'Stop'

$folder = (Resolve-Path $Package).Path
$exe = Join-Path $folder 'kaepora.exe'

# The subsystem in the program's PE header: 2 is a window program, 3 a console one.
$bytes = [IO.File]::ReadAllBytes($exe)
$subsystem = [BitConverter]::ToUInt16($bytes, [BitConverter]::ToInt32($bytes, 0x3C) + 0x5C)
if ($subsystem -ne 2) {
    throw "kaepora.exe has subsystem $subsystem, so Windows opens a console for it"
}

if (Test-Path (Join-Path $folder 'tls\qopensslbackend.dll')) {
    throw 'The package holds the OpenSSL plugin, which Qt would prefer to Schannel'
}

$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
$env:QT_FORCE_STDERR_LOGGING = '1'
$env:QT_LOGGING_RULES = 'qt.network.http2.debug=true'
$log = Join-Path $PWD 'window.log'
$finished = 'qt.network.http2: stream 1 closed'
$process = Start-Process -FilePath $exe -PassThru -RedirectStandardError $log
foreach ($try in 1..150) {
    if ($process.HasExited -or (Select-String -Path $log -SimpleMatch $finished -Quiet)) {
        break
    }
    Start-Sleep -Milliseconds 200
}
if ($process.HasExited) {
    throw "The packaged window stopped with exit code $($process.ExitCode)"
}
if (-not (Select-String -Path $log -SimpleMatch $finished -Quiet)) {
    Get-Content $log
    throw "The packaged window's request to GitHub did not finish"
}
$loaded = $process.Modules | ForEach-Object FileName | Sort-Object -Unique
Stop-Process $process
$process.WaitForExit()

$inside = $loaded | Where-Object { $_.StartsWith("$folder\", 'OrdinalIgnoreCase') }
$outside = $loaded | Where-Object { -not $_.StartsWith("$folder\", 'OrdinalIgnoreCase') }
'The window loaded these libraries from the package:'
$inside
'and these from the system:'
$outside
foreach ($name in 'Qt6Widgets.dll', 'platforms\qwindows.dll', 'Qt6Network.dll', 'tls\qschannelbackend.dll',
                  'vcruntime140.dll', 'msvcp140.dll') {
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
