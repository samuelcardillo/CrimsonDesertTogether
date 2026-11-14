# What the GitHub build (.github\workflows\release.yml) runs after scripts\build.ps1: it gets the ASI loader, makes
# the kit and, with -Publish, puts it in a new release of the repository. That release is where cdmp_launcher.exe
# looks for updates (docs/LAUNCHER.md): the kit's package.txt and the four files it lists are release files.
#
#   .\scripts\ci_release.ps1 -Run 12             # dist\cdmp-coop-<date>-3663-b12, nothing published
#   .\scripts\ci_release.ps1 -Run 12 -Publish    # the same, then "gh release create" (GH_TOKEN, GITHUB_* set by GitHub)
#
# Without -Publish nothing leaves the PC, so the same steps can be tried anywhere.

param(
    [Parameter(Mandatory)][int]$Run,
    [int]$Attempt = 1,
    [switch]$Publish,
    [string]$Repo = $env:GITHUB_REPOSITORY,
    [string]$Commit = $env:GITHUB_SHA
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

& (Join-Path $PSScriptRoot 'fetch_loader.ps1')

# The kit's name: the game build it is for (the first in kit\coop\game-builds.txt) and this build's number.
$games = @(Get-Content -LiteralPath (Join-Path $root 'kit\coop\game-builds.txt') |
    ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') })
if (-not $games) { throw 'kit\coop\game-builds.txt names no game build' }
$gameBuild = ($games[0] -split '\s+')[0].Split('.')[-1]
$suffix = "$gameBuild-b$Run" + $(if ($Attempt -gt 1) { ".$Attempt" } else { '' })
$kit = & (Join-Path $PSScriptRoot 'package_coop.ps1') -Suffix $suffix | Select-Object -Last 1
if (-not $kit -or -not (Test-Path -LiteralPath (Join-Path $kit 'package.txt'))) { throw 'package_coop.ps1 made no kit' }
$id = Split-Path $kit -Leaf

if (-not $Publish) {
    Write-Host "Kit made, not published: $kit"
    return
}
if (-not $Repo -or -not $Commit) { throw '-Publish needs the repository and the commit (GITHUB_REPOSITORY and GITHUB_SHA)' }

# What the launcher downloads, the launcher itself, the whole kit, and the mod's symbols for reading crash dumps.
$assets = @('package.txt', 'winmm.dll', 'cdmp.asi', 'cdmp.ini.host', 'cdmp.ini.guest', 'cdmp_launcher.exe') |
    ForEach-Object { Join-Path $kit $_ }
$assets += "$kit.zip"
$pdb = Join-Path $root 'build\bin\RelWithDebInfo\cdmp.pdb'
if (Test-Path -LiteralPath $pdb) {
    $symbols = Join-Path (Split-Path $kit -Parent) "$id-symbols.zip"
    if (Test-Path -LiteralPath $symbols) { Remove-Item -LiteralPath $symbols }
    Compress-Archive -LiteralPath $pdb -DestinationPath $symbols
    $assets += $symbols
}
foreach ($a in $assets) {
    if (-not (Test-Path -LiteralPath $a)) { throw "Missing from the kit: $a" }
}

$notes = @"
Built by GitHub from commit $Commit, for Crimson Desert $(($games | ForEach-Object { ($_ -split '\s+')[0] }) -join ', ').

**To play:** download ``cdmp_launcher.exe``, start it, press **Update** and then play. Both players need the same build.
If you already have the launcher, just press **Update** in it.

``$id.zip`` is the whole kit, with the instructions (README.txt) for installing by hand. The other files are what the
launcher downloads; ``$id-symbols.zip`` is only for reading crash dumps.
"@
$notesFile = Join-Path ([IO.Path]::GetTempPath()) "cdmp_release_notes_$PID.md"
$notes | Set-Content -Encoding utf8 -LiteralPath $notesFile
try {
    & gh release create $id --repo $Repo --target $Commit --title $id --notes-file $notesFile --latest @assets
    if ($LASTEXITCODE) { throw "gh release create failed ($LASTEXITCODE)" }
} finally {
    Remove-Item -LiteralPath $notesFile -ErrorAction SilentlyContinue
}
Write-Host "Published: $id"
