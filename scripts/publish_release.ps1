# Makes a release of the mod from this PC: builds and tests the commit that is checked out, makes the kit and puts
# it in a new release of the repository on GitHub. That release is where cdmp_launcher.exe looks for updates
# (docs/LAUNCHER.md): the kit's package.txt and the four files it lists are release files, and players get them the
# next time they press Update. Nothing is built on GitHub.
#
#   .\scripts\publish_release.ps1 -Publish      # build + tests, kit cdmp-coop-<date>-<game build>-b<n>, release
#   .\scripts\publish_release.ps1               # only the kit, from the build already in build\: nothing leaves the PC
#   .\scripts\publish_release.ps1 -Number 7     # that number instead of the one after the newest release's
#
# -Publish refuses unless the release is exactly a pushed commit: no changed tracked files, and the commit on the
# remote. It then runs scripts\build.ps1 itself (which stamps the mod with that commit's build id and stops if a
# test fails) and publishes with the gh that is signed in on this PC.

param(
    [int]$Number = 0,
    [switch]$Publish,
    [string]$Repo = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

if (-not $Repo -and ((& git -C $root remote get-url origin) -match 'github\.com[:/]([^/]+/[^/]+?)(\.git)?\s*$')) { $Repo = $Matches[1] }
$commit = (& git -C $root rev-parse HEAD).Trim()

if ($Publish) {
    if (-not $Repo) { throw 'The repository is not known: this checkout has no origin on github.com. Say it with -Repo owner/name.' }
    # A release says which commit it was built from, so the build has to be that commit and nothing else.
    if (& git -C $root status --porcelain --untracked-files=no) { throw 'Tracked files differ from the commit: commit them (and push) or undo them first.' }
    if (-not (& git -C $root branch -r --contains $commit)) { throw "Commit $commit is not on the remote yet: push it first (a release points at a pushed commit)." }
    & (Join-Path $PSScriptRoot 'build.ps1')
    $buildIdFile = Join-Path $root 'build\generated\build_id.h'
    $buildId = if (Test-Path -LiteralPath $buildIdFile) { Get-Content -Raw -LiteralPath $buildIdFile } else { '' }
    if ($buildId -notmatch ('CDMP_BUILD_ID 0x' + $commit.Substring(0, 8) + 'u') -or $buildId -notmatch 'CDMP_BUILD_DIRTY 0\b') {
        throw "The build in build\ is not a clean build of commit $($commit.Substring(0, 8))."
    }
}

& (Join-Path $PSScriptRoot 'fetch_loader.ps1')

# The kit's name: the game build it is for (the first in kit\coop\game-builds.txt) and the release's number, one
# more than the highest among the repository's releases.
$games = @(Get-Content -LiteralPath (Join-Path $root 'kit\coop\game-builds.txt') |
    ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') })
if (-not $games) { throw 'kit\coop\game-builds.txt names no game build' }
$gameBuild = ($games[0] -split '\s+')[0].Split('.')[-1]
if ($Number -le 0) {
    if (-not $Repo) { throw 'The repository is not known, so the next release number is not either: pass -Number.' }
    $tags = & gh release list --repo $Repo --limit 200 --json tagName --jq '.[].tagName'
    if ($LASTEXITCODE) { throw "Could not list the releases of $Repo with gh: pass -Number." }
    $highest = ($tags | ForEach-Object { if ($_ -match '-b(\d+)$') { [int]$Matches[1] } } | Measure-Object -Maximum).Maximum
    $Number = 1 + [int]$highest
}
$kit = & (Join-Path $PSScriptRoot 'package_coop.ps1') -Suffix "$gameBuild-b$Number" | Select-Object -Last 1
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

$notes = @"
Built from commit $commit, for Crimson Desert $(($games | ForEach-Object { ($_ -split '\s+')[0] }) -join ', ').

**To play:** download ``cdmp_launcher.exe``, start it, press **Update** and then play. Both players need the same build.
If you already have the launcher, just press **Update** in it.

``$id.zip`` is the whole kit, with the instructions (README.txt) for installing by hand. The other files are what the
launcher downloads; ``$id-symbols.zip`` is only for reading crash dumps.
"@
$notesFile = Join-Path ([IO.Path]::GetTempPath()) "cdmp_release_notes_$PID.md"
$notes | Set-Content -Encoding utf8 -LiteralPath $notesFile
try {
    & gh release create $id --repo $Repo --target $commit --title $id --notes-file $notesFile --latest @assets
    if ($LASTEXITCODE) { throw "gh release create failed ($LASTEXITCODE)" }
} finally {
    Remove-Item -LiteralPath $notesFile -ErrorAction SilentlyContinue
}
Write-Host "Published: $id"
