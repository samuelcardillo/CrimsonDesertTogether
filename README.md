# Crimson Desert Together

A two-player co-op mod for **Crimson Desert** (PC, Steam). One player hosts their world; a friend joins it
with their own character and their own save. Unofficial, experimental, and written from scratch in C++20.
Inside the code and the game folder it goes by its original name, **cdmp** (Crimson Desert Multiplayer).

It does not copy characters between two separate games. Crimson Desert is built like an online game on the
inside: the client talks to a server that runs in the same process. The mod lets the **host's internal server
accept a second player**, and connects the guest's game client to it through Steam.

> **Status (14 November 2026).** Works on game version **1.0.0.3663** only (the mod turns itself off on any
> other version; a game update needs a new port). Two players: one host, one guest. Almost everything has been
> checked with two copies of the game on one PC; only one short session has been played between two real PCs,
> on an older game version. Expect rough edges, keep backups of your saves, and read
> [Known problems](#known-problems).

## Playing

Both players need Crimson Desert on Steam at the same version, the same build of the mod, and to be Steam
friends. No IP addresses and no ports: the connection goes through Steam.

1. Download **[cdmp_launcher.exe](https://github.com/samuelcardillo/CrimsonDesertTogether/releases/latest/download/cdmp_launcher.exe)**
   (from the [latest release](https://github.com/samuelcardillo/CrimsonDesertTogether/releases/latest)) into a
   folder of its own and start it. Windows may warn about an unknown publisher: it is not signed.
2. Press **Update**. It downloads the newest build made for your game version, checks every file and keeps it.
3. Choose **Host a game** or **Join a friend**, type your name and press the big button. The launcher copies
   the mod into the game folder, writes its settings and starts the game through Steam. **Single-player** takes
   the mod out again.
4. The host loads their save until they are in the world. Then the guest starts the game and presses Play.
   Each player appears where their own save left them: to meet, both fast travel to the same Abyss Nexus.

A panel at the top left says who you are and the state of the session (F8 hides it).

**Your saves.** While the mod is loaded the game saves into a separate folder (`cdmp_save_sandbox`), not into
your real save. The guest plays their own save, uploaded to the host when joining, and the host's game sends
the guest's progress back. After a session, with the game closed, the launcher puts each player's own progress
into their real save, and backs up the whole save folder first (launchers from the releases after `b2`; tried
on copies of save folders, not yet on a real one). With an older launcher, or without one, that is done with
two scripts and you back up `%LOCALAPPDATA%\Pearl Abyss\CD\save` yourself. A host's save made with a guest in
the world is the least tested part.

**Achievements.** A guest earns no Steam achievements while in a host's world. This is deliberate: there the
guest's game holds the host's progress, so the mod blocks achievements for the guest until the game is
restarted.

The full instructions (installing by hand, mounts, dying, saving progress, leaving in the right order, what to
send when something goes wrong) are in **[kit/coop/README.txt](kit/coop/README.txt)**, which is also inside
every release zip.

## What works

"Played" means someone played it and looked; the game version is the last one it was checked on. Things checked
on an older version were ported to 3663 but not all were tried again.

| Area | State | Last checked |
|---|---|---|
| Joining the host's world; leaving to the title and joining again | Played | 3663 |
| Seeing each other and moving, smoothed against network hiccups | Played | 3663 |
| Climbing: each player sees the other climb | Played | 3527 |
| Mounts: calling, riding and dismounting, both players | Played | 3549 |
| Fighting: the guest hurts and kills enemies, enemies can kill the guest, dying and reviving | Played | 3527 |
| Same time of day; the guest shows the host's weather | Played | 3663 (weather) |
| Shopkeepers (buy, sell) and market stalls, with the guest's own money and inventory | Played | 3663 |
| The host's house as the guest sees it: walls, door, shutters, furniture, pets, live building changes | Played | 3663 (house and pets), 3576 (building changes) |
| Fast travel for the guest inside the host's world | Played | 3663 |
| The guest plays their own save and takes the progress back | Played | 3527 |
| Ships: the guest boards the host's ship and rides along; each sees the other on deck; the wheel | Played | 3663 |
| Ships: sails, the ship's crew at their posts, the stern lantern | Played | 3663 |
| Other ships (pirates, merchants) drawn where the host has them | Big ships played; small boats and the smoother ten-a-second positions are new and not played yet | 3663 |
| Two real PCs over Steam: connecting, the guest on their own save, moving, fast travel | One short session | 3549 |

## Known problems

- Quests belong to the host's world. What the guest kills or interacts with can advance the host's quest
  objectives as if the host had done it. The guest also sees and can use the host's discovered Abyss Nexus
  points.
- If the host leaves first, or the guest's game crashes, the guest loses that session's progress. Leave in
  order: the guest through the menu, then the host.
- Merchants, houses and most of the living world exist only around the host; far from the host the guest's
  world is thin.
- Switching character works, with a crash guard that has not been seen acting; riding as the new character is
  not synced. Rope does not work on the other player's copy.
- On ships: the other player's name tag can sit a few metres off; boarding a ship that is already under way is
  not checked; the guest's own ship as the host sees it is not tested.
- The panel shows "Stamina 0/0" on 3663.
- Loot from enemies killed by the guest is not tested.

Each feature can be switched off in `cdmp.ini`; the kit's README says which setting does what.

## How it works

**Loading.** [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader), installed as
`winmm.dll` in the game's `bin64` folder, loads `cdmp.asi` into the game. The mod identifies the game by the
timestamp of its executable and carries a table of addresses and layouts for each version it knows; on an
unknown version it does nothing. All its work runs on its own threads and through inline hooks
([safetyhook](https://github.com/cursey/safetyhook)); every read of game memory is guarded.

**One world, two clients.** The game's client and its in-process server exchange about 1,180 kinds of internal
messages (called "Troc" in the game). On the host, the mod makes that server log in a second user. On the
guest, the mod takes the client's connection away from its own local server and sends it, through a tunnel, to
the host's server instead. From then on the guest's client is a real client of the host's world: its requests
are handled by the host's server, and it receives what that server sends about the world, like the host's own
client does. The message numbers change with every game version, so both ends agree on the game build and
translate through a common numbering before any traffic flows.

**The tunnel.** Steam's peer-to-peer messaging (`ISteamNetworkingMessages`) between two Steam friends; plain
UDP is used for two copies on one PC and for tests. On top of it the mod runs its own small protocol
(`src/common/protocol.h`): a reliable, ordered, compressed channel that carries the game's messages in
batches, and unreliable messages for things that go stale quickly (each player's state twenty times a second,
positions of ships and their crews ten times a second).

**Filling in what the game does not do.** The game was written for one player per world, and much of the mod
is about the places where that shows:

- *Saves.* File access of the save system is redirected to a sandbox folder, so nothing a session writes
  reaches the real save. The guest's own save is uploaded to the host at the join so the guest plays their
  character, and is handed back at the end.
- *Who simulates what.* Each client simulates some things by itself: ships, their crews, sails, doors. The
  server tells a second client little or nothing about them (for a character standing on a ship's deck it
  sends empty position updates). For those the two mods talk to each other directly: the host's mod says where
  its ship and the ships around it are, where each crew member stands and when a sail changes, and the guest's
  mod places them on its own copy, frame by frame.
- *Things only one client is told.* Shops, market stalls, house building, mounts, weather and cut-scenes each
  needed their own handling so the second player gets the right messages and neither client takes over the
  other's camera or scene.
- *Guards.* Requests the guest's client would send about things it does not own are held back; a few
  situations that used to close the game (a rider with no mount during a load, a scene with no target) are
  caught; Steam achievements are blocked for a guest.

**The panel.** A Dear ImGui overlay drawn through hooks on the game's DirectX 12 presentation, installed only
once the world is up.

**Distribution.** Nothing is built on GitHub. A release is built and tested on the development PC
(`scripts/publish_release.ps1`) and uploaded with a `package.txt` that lists each file and its SHA-256; the
launcher reads that file from the latest release, downloads what it names, checks it and installs it
([docs/LAUNCHER.md](docs/LAUNCHER.md)).

## Building from source

You need Windows 10/11 x64 and **Visual Studio 2022** or its Build Tools with "Desktop development with C++".
The compiler of Visual Studio 2026 (MSVC 14.51) generates wrong code for this project and is refused
([why](docs/research/msvc-14.51-miscompila.md)). CMake downloads Dear ImGui and safetyhook.

```powershell
.\scripts\build.ps1              # configure, build (RelWithDebInfo), run the 105 unit tests
.\scripts\build.ps1 -Clean       # the same, compiling everything again
.\scripts\deploy.ps1             # copy cdmp.asi and the loader into the game's bin64 (-Remove uninstalls)
.\scripts\launch-2inst.ps1       # start two copies of the game on this PC, one host and one guest
```

Always build through `build.ps1`: it also repairs a trap of the build system that once shipped a release that
crashed at startup ([details](docs/research/objetos-obsoletos-msbuild.md)). The output is in
`build\bin\RelWithDebInfo\`: `cdmp.asi`, `cdmp_launcher.exe`, the test and helper programs. The mod logs to
`bin64\cdmp.log` and is configured by `bin64\cdmp.ini`; the profiles the kit is made from are in
`tools/porting/configs/`.

To try the mod without the game, `cdmp_host.exe` loads `cdmp.asi` by itself and `cdmp_peer.exe` plays the other
end. With the game, `tools/porting/livetest/` drives two copies unattended (join, travel, board a ship, measure).

## Repository layout

| Folder | Contents |
|---|---|
| `src/common/` | The protocol between the two mods, the reliable channel, the session, the tunnel, transports, save guards |
| `src/mod/` | The mod: start-up and configuration, `game/` (reading the game), `net/` (capture, sending), `render/` (the panel), `session2/` (everything about sharing one world: about 280 files, one feature each) |
| `tools/launcher/` | `cdmp_launcher.exe`, what players use |
| `tools/host`, `tools/peer_sim`, `tools/probe`, `tools/dx12_test` | Programs for testing without the game or from outside it |
| `tools/porting/` | Scripts for porting to a new game version, live two-copy tests (`livetest/`), ship measurements (`boat/`), the kit's profiles (`configs/`) |
| `tests/` | Unit and integration tests, run by `ctest` |
| `scripts/` | Build, install, two-copy launch, packaging and release |
| `kit/coop/` | What goes into a release: settings for host and guest, the players' README, save scripts |
| `docs/` | Status, roadmap, design and research notes |
| `third_party/` | A safetyhook patch (Boost Software License 1.0) and notes on the ASI loader |

## Documentation

Most notes are in Spanish; what a player sees is in English.

- [docs/ESTADO-3527.md](docs/ESTADO-3527.md): what works today, newest first.
- [docs/research/port-3663.md](docs/research/port-3663.md): the port to the current game version and the ship
  work, with measurements.
- [docs/OPCION-D-EMULADOR.md](docs/OPCION-D-EMULADOR.md): the design, and what is known about the game's
  internals.
- [docs/LAUNCHER.md](docs/LAUNCHER.md): the launcher and how releases are made.
- [docs/PRUEBA-DOS-PC.md](docs/PRUEBA-DOS-PC.md): testing between two PCs.
- [docs/ROADMAP.md](docs/ROADMAP.md): how the project got here.

## Legal

This is an unofficial fan project, not affiliated with or endorsed by Pearl Abyss. Crimson Desert is a
trademark of Pearl Abyss Corp. You need a legitimately purchased copy of the game. The repository contains no
decompiled game code, game binaries, extracted game files or save data. The source and research notes refer to
addresses, function names and short byte signatures of the game's executable, as needed to hook it.

## License and credits

GPL v3: see [LICENSE](LICENSE). `third_party/safetyhook-patch` keeps its own license (BSL-1.0).

The project was started by **ByJunter** as
[CrimsonDesertMultiplayer-NativeServer](https://github.com/ByJunter/CrimsonDesertMultiplayer-NativeServer), the
successor to [CrimsonDesertMultiplayer](https://github.com/ByJunter/CrimsonDesertMultiplayer). It continues here,
with the full commit history, from the fork
[samuelcardillo/CrimsonDesertMultiplayer-NativeServer](https://github.com/samuelcardillo/CrimsonDesertMultiplayer-NativeServer).
The tag `kit-3663-v23` marks the last kit distributed as a zip before the launcher and the releases.
