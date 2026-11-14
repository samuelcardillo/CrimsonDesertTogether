#pragma once
// The launcher's rules for the player's saves, with no Windows calls (tests/launcher_tests.cpp); the files are in
// save_core.cpp. docs/LAUNCHER.md, "Las partidas".
//
// While the mod is on, the game saves into a sandbox and the real save folder is never written
// (save_guard=redirect, src/mod/session2/save_hooks.h). What a player did in a co-op session is therefore somewhere
// else when the game closes:
//   - a HOST's saves are in the sandbox (%LOCALAPPDATA%\Pearl Abyss\CD\cdmp_save_sandbox), which the next start with
//     the mod replaces with a new copy of the real folder;
//   - a GUEST's character is saved by the host's game and comes back through the tunnel as generations in
//     %LOCALAPPDATA%\cdmp\guest_progress\<host SteamID64>\incoming\gen<n>\.
// Players used to put either into their real save by hand, with the game closed (the kit's README, section 6:
// apply-guest-progress.ps1 and host_save_restore.ps1). The launcher does it now; here is what it may keep and what
// it leaves out. The text formats (the guest's manifest, the host's run journal, the save header) are read with the
// mod's own headers, so the launcher and the mod cannot disagree on them.

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/sha256.h"
#include "launcher_logic.h"
#include "mod/session2/guest_save_wire.h"
#include "mod/session2/host_save_logic.h"

namespace cdmp::launcher {

namespace gsv = session2::gsv;
namespace gsw = session2::gsw;
namespace hsv = session2::hsv;

inline constexpr size_t kBackupsKept = 10;

inline std::string slot_label(uint32_t n) { return "slot " + std::to_string(n); }

// ------------------------------------------------------------------ a guest's generations
struct GuestGeneration {
    uint32_t number = 0;  // its folder: gen<number>
    gsw::IncomingManifest manifest;

    std::string lobby_sha256() const { return sha256::hex(manifest.files[0].sha); }
    std::string save_sha256() const { return sha256::hex(manifest.files[1].sha); }
    // The save it continues (the SHA-256 of the save.save its first session uploaded); empty = not stated.
    std::string root() const { return gsw::digest_zero(manifest.root) ? std::string() : sha256::hex(manifest.root); }
    // The save.save uploaded in its own session; empty = not stated.
    std::string base() const { return gsw::digest_zero(manifest.base) ? std::string() : sha256::hex(manifest.base); }
};

// By the host's time in the manifest, not by number: a host whose mailbox was emptied numbers from 1 again.
inline void newest_first(std::vector<GuestGeneration>& gens) {
    std::sort(gens.begin(), gens.end(), [](const GuestGeneration& a, const GuestGeneration& b) {
        if (a.manifest.time_unix != b.manifest.time_unix) return a.manifest.time_unix > b.manifest.time_unix;
        return a.number > b.number;
    });
}

// guest_progress\<host>\applied.txt: the last generation that was put into a slot. The mod reads `generation` (it
// tells the host, which then sends nothing older); the launcher reads the rest as one more save of that line.
struct AppliedRecord {
    uint32_t generation = 0;
    std::string root, save;  // 64 hex digits, or empty
};

inline std::optional<AppliedRecord> parse_applied(std::string_view text) {
    const gsw::KeyValues kv = gsw::parse_key_values(text);
    const auto gen = gsw::kv_u64(kv, "generation");
    if (!gen || *gen > 0xFFFFFFFFull) return std::nullopt;
    AppliedRecord r;
    r.generation = static_cast<uint32_t>(*gen);
    r.root = normal_sha256(gsw::kv_get(kv, "lineage_root").value_or(""));
    r.save = normal_sha256(gsw::kv_get(kv, "save_sha256").value_or(""));
    return r;
}

// The same keys apply-guest-progress.ps1 writes.
inline std::string format_applied(const GuestGeneration& g, std::string_view applied_utc, std::string_view source,
                                  std::string_view backup) {
    std::string s;
    auto line = [&](std::string_view key, std::string_view value) {
        s.append(key).append("=").append(value).append("\r\n");
    };
    line("generation", std::to_string(g.number));
    line("slot", std::to_string(g.manifest.slot));
    line("applied_utc", applied_utc);
    line("source", source);
    line("backup", backup);
    line("lineage_root", g.root());
    line("lobby_sha256", g.lobby_sha256());
    line("save_sha256", g.save_sha256());
    return s;
}

// A generation continues ONE save: its line. It goes only into a slot that holds a save of that line (the root, a
// generation, or the save a generation was made from) or no save at all, and never backwards within the line. So
// the progress of one save never replaces another save, and a slot the player has played on alone since is left as
// it is. (The rule of apply-guest-progress.ps1, step 4.)
enum class LineProblem {
    None,          // it may go into the slot
    NoLine,        // it does not say which save it continues (a mod older than 12 October 2026)
    OtherSave,     // the slot holds a save that is not of its line
    AlreadyThere,  // the slot holds it, or an equal or newer generation of the line
    Older,         // the slot holds the save that newer generations were made from
};

// `all`: the generations of the same host (c among them). `slot_save`: the SHA-256 of the save.save in c's slot;
// nullopt = the slot has no save.
inline LineProblem line_problem(const GuestGeneration& c, std::span<const GuestGeneration> all,
                                const std::optional<AppliedRecord>& applied,
                                const std::optional<std::string>& slot_save) {
    const std::string root = c.root();
    if (root.empty()) return LineProblem::NoLine;
    if (!slot_save) return LineProblem::None;
    const std::string& current = *slot_save;
    bool member = current == root;
    for (const GuestGeneration& o : all) {
        if (o.root() == root && (o.save_sha256() == current || o.base() == current)) member = true;
    }
    if (applied && applied->root == root && applied->save == current) member = true;
    if (!member) return LineProblem::OtherSave;
    if (c.save_sha256() == current) return LineProblem::AlreadyThere;
    const uint64_t time = c.manifest.time_unix;
    std::optional<uint64_t> first_from_here;  // the oldest generation made from the save in the slot
    for (const GuestGeneration& o : all) {
        if (o.root() != root) continue;
        if (o.save_sha256() == current && o.manifest.time_unix >= time) return LineProblem::AlreadyThere;
        if (o.base() == current && (!first_from_here || o.manifest.time_unix < *first_from_here)) {
            first_from_here = o.manifest.time_unix;
        }
    }
    if (first_from_here && c.base() != current && time < *first_from_here) return LineProblem::Older;
    return LineProblem::None;
}

// ------------------------------------------------------------------ a host's sandbox
// What is known about one slot of the sandbox after a hosted session.
struct HostSlotFacts {
    uint32_t slot = 0;
    bool lobby = false, save = false;  // the two files are there
    gsv::HeaderCheck lobby_check = gsv::HeaderCheck::Ok, save_check = gsv::HeaderCheck::Ok;
    bool changed = false;      // its lobby.save or save.save is not the real folder's
    bool subfolders = false;   // not a slot as the game makes them
    bool leftovers = false;    // slotN_new / slotN_backup... next to it, in the sandbox or in the real folder
    bool start_known = false;  // when that session began (its journal's start) could be read
    bool real_newer = false;   // the real folder's slot was written after that session began
    uint64_t pair_gap_ms = 0;  // between the write times of its two files
};

enum class HostVerdict {
    Unchanged,  // nothing to do: not a slot of the host's, or the same as the real folder's
    Keep,       // the host's own save of that session: it goes into the real folder
    LeftOut,    // changed, but not surely the host's own complete save: it stays in the sandbox, and `why` says so
};

struct HostDecision {
    HostVerdict verdict = HostVerdict::Unchanged;
    std::string why;
};

// `journal`: the record the mod kept of that session's saves (host_save=probe: every SavePlayerToSlot and
// FlushPendingSaveData with who it was for). A slot is kept only when every save that went to it was the host's own
// and worked. The mod's own verdict (hsv::classify_slot) also wants no delayed write at all, which is how every
// autosave is written (the "flush timer" five seconds later), so it never says "candidate" in normal play; here a
// delayed write is accepted when it was the host's.
inline HostDecision judge_host_slot(const HostSlotFacts& f, const hsv::Journal& journal) {
    const auto left_out = [](std::string why) { return HostDecision{HostVerdict::LeftOut, std::move(why)}; };
    if (!hsv::slot_whitelisted(f.slot) || !f.changed) return {};
    if (f.subfolders) return left_out("it is not a save slot as the game makes them");
    if (!f.lobby || !f.save) return left_out("one of its two files is missing");
    if (f.lobby_check != gsv::HeaderCheck::Ok || f.save_check != gsv::HeaderCheck::Ok) {
        return left_out("its files are not a complete save (the game may have closed while saving)");
    }
    if (f.leftovers) {
        return left_out("the game was interrupted while saving it; load that save once without the mod so the game "
                        "repairs it");
    }
    const hsv::SlotObservation o = hsv::observe_slot(&journal, f.slot);
    if (!o.hooks) return left_out("that session kept no record of who saved");
    if (o.truncated) return left_out("that session's record of saves is incomplete (the game did not close normally)");
    if (o.other_owner) return left_out("the other player's character was saved into it");
    for (const hsv::FlushEvent& e : journal.flushes) {
        if (!e.pending_read) return left_out("a delayed save of that session could not be followed");
        if (e.pending >= 0 && static_cast<uint32_t>(e.pending) == f.slot &&
            (e.owner != gsv::Owner::Host || !e.positive_host)) {
            return left_out("a delayed save that was not surely yours was written to it");
        }
    }
    if (o.nested) return left_out("a save to it ran inside another save");
    if (o.host_failed) return left_out("one of your saves to it failed");
    if (o.redirect_failed) return left_out("some save files of that session could not be written where they belong");
    if (!o.host_positive_ok) return left_out("no save of yours to it was seen in that session");
    if (f.pair_gap_ms > hsv::kPairGapMs) return left_out("its two files were not written together");
    if (!f.start_known) return left_out("when that session began is not known");
    if (f.real_newer) {
        return left_out("your real save in that slot is newer than that session (you played without the mod since)");
    }
    return {HostVerdict::Keep, {}};
}

// The journal's "start=2026-11-14 16:58:25.479" (local time; the milliseconds may be missing).
struct LocalTime {
    uint16_t year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0, millisecond = 0;
};

inline std::optional<LocalTime> parse_local_time(std::string_view s) {
    s = trim(s);
    auto number = [&](size_t at, size_t len) -> std::optional<uint16_t> {
        if (at + len > s.size()) return std::nullopt;
        const auto v = parse_u64(s.substr(at, len));
        return v ? std::optional<uint16_t>(static_cast<uint16_t>(*v)) : std::nullopt;
    };
    if (s.size() < 19 || s[4] != '-' || s[7] != '-' || s[10] != ' ' || s[13] != ':' || s[16] != ':') return std::nullopt;
    const auto year = number(0, 4), month = number(5, 2), day = number(8, 2);
    const auto hour = number(11, 2), minute = number(14, 2), second = number(17, 2);
    if (!year || !month || !day || !hour || !minute || !second) return std::nullopt;
    if (*year < 2000 || *month < 1 || *month > 12 || *day < 1 || *day > 31 || *hour > 23 || *minute > 59 || *second > 59) {
        return std::nullopt;
    }
    LocalTime t{*year, *month, *day, *hour, *minute, *second, 0};
    if (s.size() > 19) {
        if (s[19] != '.' || s.size() != 23) return std::nullopt;
        const auto ms = number(20, 3);
        if (!ms) return std::nullopt;
        t.millisecond = *ms;
    }
    return t;
}

// ------------------------------------------------------------------ the backups
// A backup is the whole save folder as it was, in <data>\save_backups\<yyyymmdd-hhmmss>[-n]\save, with backup.txt
// (every file's size and SHA-256) next to it. One is made before the launcher changes the real folder and before it
// starts the game with the mod; a folder equal to the newest backup is not copied again.
inline constexpr std::string_view kBackupHeader = "cdmp_launcher save backup";
inline constexpr std::string_view kBackupList = "backup.txt";

inline bool backup_name(std::string_view name) {
    if (name.size() < 15) return false;
    for (size_t i = 0; i < 15; ++i) {
        const bool digit = name[i] >= '0' && name[i] <= '9';
        if (i == 8 ? name[i] != '-' : !digit) return false;
    }
    if (name.size() == 15) return true;
    return name[15] == '-' && name.size() <= 19 && all_digits(name.substr(16)) && name[16] != '0';
}

// Newest first. The names are local times, so they sort as text; "-2" (the same second again) comes after its base.
inline std::vector<std::string> newest_backups_first(std::vector<std::string> names) {
    std::erase_if(names, [](const std::string& n) { return !backup_name(n); });
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        const std::string_view sa = std::string_view(a).substr(0, 15), sb = std::string_view(b).substr(0, 15);
        if (sa != sb) return sa > sb;
        const uint64_t na = a.size() > 16 ? parse_u64(std::string_view(a).substr(16)).value_or(1) : 1;
        const uint64_t nb = b.size() > 16 ? parse_u64(std::string_view(b).substr(16)).value_or(1) : 1;
        return na > nb;
    });
    return names;
}

// The backups to delete so that the newest `keep` stay. Anything not named as a backup is never in the result.
inline std::vector<std::string> backups_to_remove(std::vector<std::string> names, size_t keep = kBackupsKept) {
    names = newest_backups_first(std::move(names));
    if (names.size() <= keep) return {};
    return {names.begin() + static_cast<std::ptrdiff_t>(keep), names.end()};
}

inline std::string format_backup_list(std::string_view made, std::span<const hsv::TreeFile> files) {
    std::string s(kBackupHeader);
    s.append("\r\nmade=").append(made).append("\r\n");
    for (const hsv::TreeFile& f : files) {
        s += "file=" + hsv::field(f.path) + "|" + std::to_string(f.size) + "|" + f.sha256 + "\r\n";
    }
    s += "state=complete\r\n";
    return s;
}

// nullopt: not a complete list (a backup that was not finished is never trusted, nor counted as the newest).
inline std::optional<std::vector<hsv::TreeFile>> parse_backup_list(std::string_view text) {
    std::vector<hsv::TreeFile> files;
    bool first = true, complete = false;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = trim(text.substr(at, end - at));
        at = end + 1;
        if (first) {
            if (line != kBackupHeader) return std::nullopt;
            first = false;
            continue;
        }
        if (line.empty()) continue;
        if (complete) return std::nullopt;  // nothing comes after the last line
        if (line == "state=complete") {
            complete = true;
        } else if (line.starts_with("file=")) {
            const auto p = hsv::split(line.substr(5));
            const auto size = p.size() == 3 ? parse_u64(p[1]) : std::nullopt;
            if (!size || p[0].empty() || normal_sha256(p[2]).empty()) return std::nullopt;
            files.push_back({std::string(p[0]), *size, normal_sha256(p[2])});
        }
    }
    if (!complete) return std::nullopt;
    return files;
}

}  // namespace cdmp::launcher
