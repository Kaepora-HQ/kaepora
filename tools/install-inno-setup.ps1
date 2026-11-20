# Installs the Inno Setup compiler that makes the Windows installer, from upstream's release on GitHub, into the job's
# temporary folder, and puts it first on PATH, ahead of the older Inno Setup that GitHub's Windows runners carry.
# Portable mode writes no uninstaller, shortcuts or file associations.
#
# Usage: tools/install-inno-setup.ps1
# Runs in a GitHub Actions job on Windows.
$ErrorActionPreference = 'Stop'

$version = '7.1.1'
$sha256 = '8A5A6711C3766A6E5FB74F280BA0E3551E719CE94538D901FDBD1F8F9920B3F0'

$installer = Join-Path $env:RUNNER_TEMP "innosetup-$version-x64.exe"
Invoke-WebRequest -OutFile $installer `
    "https://github.com/jrsoftware/issrc/releases/download/is-$($version.Replace('.', '_'))/innosetup-$version-x64.exe"
if ((Get-FileHash -Algorithm SHA256 $installer).Hash -ne $sha256) {
    throw "Inno Setup $version's installer does not have the expected SHA-256"
}
$root = Join-Path $env:RUNNER_TEMP 'innosetup'
$setup = Start-Process -FilePath $installer -Wait -PassThru `
    -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/PORTABLE=1', "/DIR=$root"
if ($setup.ExitCode -ne 0) {
    throw "Inno Setup $version's installer stopped with exit code $($setup.ExitCode)"
}
$root | Out-File -Append -Encoding utf8 $env:GITHUB_PATH
