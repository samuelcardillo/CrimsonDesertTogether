# Makes the kit and, with -Publish, puts it in a new release of the repository. That release is where
# cdmp_launcher.exe looks for updates (docs/LAUNCHER.md): the kit's package.txt and the four files it lists are
# release files. It runs after scripts\build.ps1, in one of two places:
#
#   On GitHub (.github\workflows\release.yml):  ci_release.ps1 -Run <run number> -Attempt <attempt> -Publish
#       The kit is cdmp-coop-<date>-<game build>-b<run number>; GH_TOKEN and GITHUB_* are set by GitHub.
#   On a PC, when GitHub's build cannot run:    .\scripts\build.ps1; .\scripts\ci_release.ps1 -Run 2 -Publish
#       The kit is ...-pc2 (so it never takes a name GitHub's build will want). It publishes with the gh already
#       signed in, and only if the build in build\ is exactly a commit that is already pushed: no changed
#       tracked files, the same commit in the mod's build id, and that commit on the remote.
#
#   .\scripts\ci_release.ps1 -Run 12             # only makes the kit in dist\: nothing leaves the PC

param(
    [Parameter(Mandatory)][int]$Run,
    [int]$Attempt = 1,
    [switch]$Publish,
    [string]$Repo = $env:GITHUB_REPOSITORY,
    [string]$Commit = $env:GITHUB_SHA
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$onGitHub = $env:GITHUB_ACTIONS -eq 'true'

if ($Publish -and -not $onGitHub) {
    # A release says which commit it was built from: on a PC that has to be checked, on GitHub it is the checkout.
    $head = (& git -C $root rev-parse HEAD).Trim()
    if (-not $Commit) { $Commit = $head }
    if (-not $Repo -and ((& git -C $root remote get-url origin) -match 'github\.com[:/]([^/]+/[^/]+?)(\.git)?\s*$')) { $Repo = $Matches[1] }
    if ($Commit -ne $head) { throw "The checkout is at $head, not at ${Commit}: a release from a PC is of the commit that is checked out." }
    if (& git -C $root status --porcelain --untracked-files=no) { throw 'Tracked files differ from the commit: commit them (and push) or undo them first.' }
    if (-not (& git -C $root branch -r --contains $Commit)) { throw "Commit $Commit is not on the remote yet: push it first (a release points at a pushed commit)." }
    $buildIdFile = Join-Path $root 'build\generated\build_id.h'
    $buildId = if (Test-Path -LiteralPath $buildIdFile) { Get-Content -Raw -LiteralPath $buildIdFile } else { '' }
    if ($buildId -notmatch ('CDMP_BUILD_ID 0x' + $Commit.Substring(0, 8) + 'u') -or $buildId -notmatch 'CDMP_BUILD_DIRTY 0\b') {
        throw "The build in build\ is not a clean build of commit $($Commit.Substring(0, 8)): run .\scripts\build.ps1 first."
    }
}
if ($Publish -and (-not $Repo -or -not $Commit)) { throw '-Publish needs the repository and the commit (GITHUB_REPOSITORY and GITHUB_SHA, or a checkout whose origin is on github.com)' }

& (Join-Path $PSScriptRoot 'fetch_loader.ps1')

# The kit's name: the game build it is for (the first in kit\coop\game-builds.txt) and this build's number.
$games = @(Get-Content -LiteralPath (Join-Path $root 'kit\coop\game-builds.txt') |
    ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') })
if (-not $games) { throw 'kit\coop\game-builds.txt names no game build' }
$gameBuild = ($games[0] -split '\s+')[0].Split('.')[-1]
$suffix = "$gameBuild-" + $(if ($onGitHub) { 'b' } else { 'pc' }) + $Run + $(if ($Attempt -gt 1) { ".$Attempt" } else { '' })
$kit = & (Join-Path $PSScriptRoot 'package_coop.ps1') -Suffix $suffix | Select-Object -Last 1
if (-not $kit -or -not (Test-Path -LiteralPath (Join-Path $kit 'package.txt'))) { throw 'package_coop.ps1 made no kit' }
$id = Split-Path $kit -Leaf

if (-not $Publish) {
    Write-Host "Kit made, not published: $kit"
    return
}

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

$builtBy = if ($onGitHub) { 'Built by GitHub' } else { "Built on a developer's PC (not by the GitHub workflow)" }
$notes = @"
$builtBy from commit $Commit, for Crimson Desert $(($games | ForEach-Object { ($_ -split '\s+')[0] }) -join ', ').

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
