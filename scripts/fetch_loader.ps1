# Puts Ultimate ASI Loader v9.7.4's dinput8.dll where the packaging scripts read it from
# (third_party\ultimate-asi-loader\v9.7.4, a path git ignores; see the README next to it). A fresh clone needs it
# once; scripts\publish_release.ps1 runs it, and on a PC that already has the file it does nothing.
#
# The zip and the dll inside it must have the SHA-256 written here: anything else is refused, and a different
# dinput8.dll already at that path is left alone.

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$root = Split-Path $PSScriptRoot -Parent
$dir = Join-Path $root 'third_party\ultimate-asi-loader\v9.7.4'
$dll = Join-Path $dir 'dinput8.dll'
$url = 'https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v9.7.4/Ultimate-ASI-Loader-NoPDB_x64.zip'
$zipSha = 'e5860e7d9a1805267535b65749575b5e406cc6ea3325c7392189c578815045d1'
$dllSha = '031a3e5576d91dce1e438d36b9a3d462c7334ab4791990a8ff1e3ddc0e132daf'

function Get-Sha256([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower() }

if (Test-Path -LiteralPath $dll) {
    if ((Get-Sha256 $dll) -eq $dllSha) {
        Write-Host "ASI loader already in place: $dll"
        return
    }
    throw "$dll is not Ultimate ASI Loader v9.7.4's dinput8.dll (SHA-256 $(Get-Sha256 $dll)). Remove it and run this again."
}

# The download is unpacked in a folder of its own, and only the one file with the expected hash leaves it.
$work = Join-Path ([IO.Path]::GetTempPath()) ('cdmp_loader_' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $work | Out-Null
try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $zip = Join-Path $work 'loader.zip'
    Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
    if ((Get-Sha256 $zip) -ne $zipSha) { throw "The downloaded zip is not the v9.7.4 release (SHA-256 $(Get-Sha256 $zip)): $url" }
    $unpacked = Join-Path $work 'unpacked'
    Expand-Archive -LiteralPath $zip -DestinationPath $unpacked
    $found = Get-ChildItem -LiteralPath $unpacked -Recurse -File -Filter 'dinput8.dll' | Select-Object -First 1
    if (-not $found) { throw "dinput8.dll is not in the zip: $url" }
    if ((Get-Sha256 $found.FullName) -ne $dllSha) { throw "dinput8.dll in the zip is not the expected file (SHA-256 $(Get-Sha256 $found.FullName))" }
    New-Item -ItemType Directory -Force $dir | Out-Null
    Copy-Item -LiteralPath $found.FullName -Destination $dll
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host "ASI loader downloaded: $dll"
