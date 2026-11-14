#pragma once
// The launcher's work on the player's saves, without a window: a backup of the save folder, finding the progress a
// co-op session left outside it, and putting that progress into it with the game closed. The rules are in
// save_logic.h. Everything here can run on a worker thread; nothing here runs while the game does.

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "launcher_core.h"
#include "save_logic.h"

namespace cdmp::launcher {

struct SavePaths {
    std::filesystem::path save;      // the game's own save folder: %LOCALAPPDATA%\Pearl Abyss\CD\save
    std::filesystem::path sandbox;   // where the game saves while the mod is on: ...\CD\cdmp_save_sandbox
    std::filesystem::path progress;  // what hosts sent back to this PC's guest: %LOCALAPPDATA%\cdmp\guest_progress
    std::filesystem::path exports;   // the host's session journals: <bin64>\cdmp_save_exports, or cdmp.ini's
                                     // [session2] host_save_dir; empty = the game folder is not known
    std::filesystem::path staging;   // next to `save`, on its volume: a slot is made there and swapped in
    std::filesystem::path backups;   // <data>\save_backups
    std::filesystem::path log;       // <data>\saves.log: what the launcher did to the saves, one line each
};

// `local` stands in for %LOCALAPPDATA% (the tests and --local-dir); empty = the real one.
SavePaths save_paths(const Paths& paths, const std::filesystem::path& local = {});

// One slot's worth of progress that is not in the real save folder yet.
struct SlotChange {
    bool from_guest = false;       // a generation a host sent back; false = the host's own slot in the sandbox
    std::string account;           // the account folder under `save`
    uint32_t slot = 0;
    std::filesystem::path source;  // the folder with the lobby.save and save.save to keep
    std::string lobby_sha256, save_sha256;
    uint64_t saved = 0;            // when it was saved (FILETIME, UTC)
    std::string when;              // the same for the player: "14 Nov 16:50"
    // from_guest only: where it came from, for applied.txt
    std::filesystem::path host_dir;
    GuestGeneration generation;
};

struct SaveScan {
    bool found = false;               // the save folder is there, with at least one account's saves
    std::vector<SlotChange> pending;  // what keep_progress would put into the real folder
    std::vector<std::string> notes;   // progress that is there but is left out, and why
    // Changes when what is waiting changes (so that a failed attempt is not repeated on its own).
    std::string signature() const;
};

// Reads only.
SaveScan scan_saves(const SavePaths& paths);

struct BackupResult {
    bool ok = false;             // also when there was nothing to back up
    bool made = false;           // false with ok: the folder is the same as the newest backup, or there are no saves
    std::string error;
    std::filesystem::path dir;   // the backup that holds the folder as it is now (empty: no saves)
};

// The whole save folder, copied and checked file by file; the newest kBackupsKept backups stay.
BackupResult backup_saves(const SavePaths& paths);

struct KeepResult {
    bool ok = false;                  // nothing failed (also when nothing was waiting)
    std::string error;
    std::vector<std::string> kept;    // one per slot, for the player: "slot 0 (saved 14 Nov 16:50)"
    std::vector<std::string> notes;   // the scan's notes
    std::filesystem::path backup;     // the save folder as it was before (empty: nothing was changed)
};

// Looks again, backs the save folder up and puts each waiting slot into it: the slot is made complete in the staging
// folder, checked, and swapped in with two renames; a slot that cannot be swapped is left exactly as it was. Refuses
// while the game runs, and writes nothing without a checked backup.
KeepResult keep_progress(const SavePaths& paths, const std::function<bool()>& game_is_running = game_running,
                         const std::function<void(const std::string&)>& status = {});

}  // namespace cdmp::launcher
