#include "save_core.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

#include "common/sha256.h"

namespace cdmp::launcher {
namespace fs = std::filesystem;

namespace {

constexpr size_t kMaxText = size_t{4} << 20;
constexpr size_t kMaxJournals = 400;    // the mod keeps the newest 200
constexpr size_t kMaxGenerations = 64;  // the mod keeps the newest 10 of each host
constexpr uint64_t kUnixEpoch = 116444736000000000ull;  // 1970-01-01 as a FILETIME
constexpr wchar_t kLobby[] = L"lobby.save";
constexpr wchar_t kSave[] = L"save.save";

// ---- the file system. A link (junction, symbolic link) is never followed: it could lead anywhere.
DWORD attributes(const fs::path& p) { return GetFileAttributesW(p.c_str()); }

bool present(const fs::path& p) { return attributes(p) != INVALID_FILE_ATTRIBUTES; }

bool is_link(const fs::path& p) {
    const DWORD a = attributes(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
}

bool is_folder(const fs::path& p) {
    const DWORD a = attributes(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
}

uint64_t u64(FILETIME t) { return (static_cast<uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; }

struct Entry {
    std::wstring name;
    bool folder = false, link = false;
    uint64_t size = 0, written = 0;
};

// nullopt: the folder could not be listed to its end.
std::optional<std::vector<Entry>> list_folder(const fs::path& dir) {
    std::vector<Entry> out;
    WIN32_FIND_DATAW fd;
    const HANDLE h = FindFirstFileExW((dir / L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return std::nullopt;
    do {
        const std::wstring_view name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        Entry e;
        e.name = name;
        e.folder = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.link = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        e.size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        e.written = u64(fd.ftLastWriteTime);
        out.push_back(std::move(e));
    } while (FindNextFileW(h, &fd));
    const bool whole = GetLastError() == ERROR_NO_MORE_FILES;
    FindClose(h);
    if (!whole) return std::nullopt;
    return out;
}

// A folder this launcher made (staging, an unfinished backup, an old backup). Never through a link.
bool remove_own(const fs::path& dir) {
    if (!present(dir)) return true;
    if (is_link(dir)) return false;
    std::error_code ec;
    fs::remove_all(dir, ec);
    return !ec && !present(dir);
}

std::span<const uint8_t> bytes_of(std::string_view s) {
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

// ---- time
SYSTEMTIME local_now() {
    SYSTEMTIME t{};
    GetLocalTime(&t);
    return t;
}

std::string stamp(const SYSTEMTIME& t) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return buf;
}

std::string clock_text(const SYSTEMTIME& t) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04u-%02u-%02u %02u:%02u:%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                  t.wSecond);
    return buf;
}

std::string utc_text() {
    SYSTEMTIME t{};
    GetSystemTime(&t);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04u-%02u-%02uT%02u:%02u:%02uZ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                  t.wSecond);
    return buf;
}

// "14 Nov 16:50", in this PC's time.
std::string when_text(uint64_t filetime) {
    const FILETIME ft{static_cast<DWORD>(filetime), static_cast<DWORD>(filetime >> 32)};
    SYSTEMTIME utc{}, local{};
    if (!FileTimeToSystemTime(&ft, &utc) || !SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local) ||
        local.wMonth < 1 || local.wMonth > 12) {
        return "at an unknown time";
    }
    static constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char buf[32];
    std::snprintf(buf, sizeof buf, "%u %s %02u:%02u", local.wDay, kMonths[local.wMonth - 1], local.wHour, local.wMinute);
    return buf;
}

std::optional<uint64_t> to_filetime(const LocalTime& t) {
    SYSTEMTIME local{};
    local.wYear = t.year, local.wMonth = t.month, local.wDay = t.day;
    local.wHour = t.hour, local.wMinute = t.minute, local.wSecond = t.second, local.wMilliseconds = t.millisecond;
    SYSTEMTIME utc{};
    FILETIME ft{};
    if (!TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc) || !SystemTimeToFileTime(&utc, &ft)) return std::nullopt;
    return u64(ft);
}

// ---- the mod's files
// A folder's identity as the mod writes it in a run journal (host_save_export.cpp dir_id): it stays the same when
// the folder is renamed, and a new folder with the same name has another.
std::string folder_id(const fs::path& p) {
    const HANDLE h = CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    BY_HANDLE_FILE_INFORMATION info{};
    const BOOL ok = GetFileInformationByHandle(h, &info);
    CloseHandle(h);
    if (!ok) return {};
    char buf[40];
    std::snprintf(buf, sizeof buf, "%08lX:%08lX%08lX", info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow);
    return buf;
}

// The journal of the run whose sandbox this is; nullopt = none (the sandbox was not made by a hosting game).
std::optional<hsv::Journal> find_journal(const fs::path& runs, const std::string& sandbox_id) {
    if (!is_folder(runs)) return std::nullopt;
    auto list = list_folder(runs);
    if (!list) return std::nullopt;
    std::erase_if(*list, [](const Entry& e) { return e.folder || e.link; });
    std::sort(list->begin(), list->end(), [](const Entry& a, const Entry& b) { return a.written > b.written; });
    if (list->size() > kMaxJournals) list->resize(kMaxJournals);
    for (const Entry& e : *list) {
        const auto text = read_text(runs / e.name, kMaxText);
        if (!text) continue;
        auto journal = hsv::parse_journal(*text);
        if (journal && journal->header.sandbox_id == sandbox_id) return journal;
    }
    return std::nullopt;
}

struct Stat {
    bool present = false;
    uint64_t size = 0, written = 0;
    bool operator==(const Stat&) const = default;
};

Stat stat_of(const fs::path& p) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a) ||
        (a.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
        return {};
    }
    return {true, (static_cast<uint64_t>(a.nFileSizeHigh) << 32) | a.nFileSizeLow, u64(a.ftLastWriteTime)};
}

// One of a slot's two files, read once: what it is and whether the game would load it.
struct SaveFile {
    bool present = false;
    uint64_t size = 0, written = 0;
    std::string sha256;  // empty = it could not be read
    gsv::HeaderCheck check = gsv::HeaderCheck::TooShort;
};

SaveFile inspect(const fs::path& p) {
    SaveFile f;
    const Stat s = stat_of(p);
    if (!s.present) return f;
    f.present = true;
    f.size = s.size;
    f.written = s.written;
    const auto data = read_file(p, static_cast<size_t>(gsv::kMaxFileSize));
    if (!data) {
        f.check = gsv::HeaderCheck::TooBig;  // or unreadable: not a save to keep either way
        return f;
    }
    f.size = data->size();
    f.sha256 = sha256::hex(sha256::of(*data));
    f.check = gsv::check_header(std::span<const uint8_t>(*data).first(std::min(data->size(), gsv::kHeaderSize)),
                                data->size());
    return f;
}

std::wstring slot_folder(uint32_t slot) { return L"slot" + std::to_wstring(slot); }

// The slots with something the game left half done next to them (slotN_new, slotN_backup...): its own recovery
// uses those the next time it starts, so the slot is not touched.
std::set<uint32_t> interrupted(const std::vector<Entry>& account) {
    std::set<uint32_t> out;
    for (const Entry& e : account) {
        const auto folder = hsv::parse_slot_folder(e.name);
        if (folder && folder->kind != hsv::FolderKind::Slot) out.insert(folder->number);
    }
    return out;
}

bool has_subfolders(const fs::path& slot) {
    if (!present(slot)) return false;
    const auto inside = list_folder(slot);
    return !inside || std::any_of(inside->begin(), inside->end(), [](const Entry& e) { return e.folder || e.link; });
}

// Why save\<account>\slotN cannot be replaced now; nullopt = it can.
std::optional<std::string> target_problem(const fs::path& account_dir, uint32_t slot) {
    const auto account = list_folder(account_dir);
    if (!account) return "your save folder could not be read";
    if (interrupted(*account).contains(slot)) {
        return "the game was interrupted while saving " + slot_label(slot) +
               "; load that save once without the mod so the game repairs it";
    }
    const fs::path target = account_dir / slot_folder(slot);
    if (present(target) && !is_folder(target)) return slot_label(slot) + " is not a folder";
    if (has_subfolders(target)) return slot_label(slot) + " is not a save slot as the game makes them";
    return std::nullopt;
}

// ---- finding what is waiting
// A host's own saves of the last session, still in the sandbox.
void scan_host(const SavePaths& p, const std::vector<std::string>& accounts, SaveScan& scan) {
    if (p.exports.empty() || !is_folder(p.sandbox)) return;
    const std::string id = folder_id(p.sandbox);
    if (id.empty()) return;
    const auto journal = find_journal(p.exports / L"runs", id);
    if (!journal) return;
    std::optional<uint64_t> start;
    if (const auto t = parse_local_time(journal->header.start)) start = to_filetime(*t);

    for (const std::string& account : accounts) {
        const fs::path boxed = p.sandbox / widen(account), real = p.save / widen(account);
        if (!is_folder(boxed)) continue;
        const auto in_box = list_folder(boxed);
        const auto in_real = list_folder(real);
        if (!in_box || !in_real) continue;
        std::set<uint32_t> cut = interrupted(*in_box);
        cut.merge(interrupted(*in_real));
        for (const Entry& e : *in_box) {
            const auto folder = hsv::parse_slot_folder(e.name);
            if (!folder || folder->kind != hsv::FolderKind::Slot || !hsv::slot_whitelisted(folder->number)) continue;
            if (!e.folder || e.link) continue;
            const fs::path from = boxed / e.name, to = real / e.name;
            // The copy the mod made when the game started has the real files' sizes and times: nothing new there.
            const Stat real_lobby = stat_of(to / kLobby), real_save = stat_of(to / kSave);
            if (real_lobby.present && real_save.present && stat_of(from / kLobby) == real_lobby &&
                stat_of(from / kSave) == real_save) {
                continue;
            }
            const SaveFile lobby = inspect(from / kLobby), save = inspect(from / kSave);
            HostSlotFacts f;
            f.slot = folder->number;
            f.lobby = lobby.present;
            f.save = save.present;
            f.lobby_check = lobby.check;
            f.save_check = save.check;
            f.changed = !(lobby.present && save.present && !lobby.sha256.empty() && !save.sha256.empty() &&
                          sha256_of_file(to / kLobby) == lobby.sha256 && sha256_of_file(to / kSave) == save.sha256);
            f.subfolders = has_subfolders(from) || has_subfolders(to);
            f.leftovers = cut.contains(f.slot);
            f.start_known = start.has_value();
            f.real_newer = start && ((real_lobby.present && real_lobby.written > *start) ||
                                     (real_save.present && real_save.written > *start));
            if (lobby.present && save.present) {
                const uint64_t gap = lobby.written > save.written ? lobby.written - save.written
                                                                  : save.written - lobby.written;
                f.pair_gap_ms = gap / 10000;
            }
            const HostDecision decision = judge_host_slot(f, *journal);
            if (decision.verdict == HostVerdict::Keep) {
                SlotChange c;
                c.account = account;
                c.slot = f.slot;
                c.source = from;
                c.lobby_sha256 = lobby.sha256;
                c.save_sha256 = save.sha256;
                c.saved = save.written;
                c.when = when_text(save.written);
                scan.pending.push_back(std::move(c));
            } else if (decision.verdict == HostVerdict::LeftOut) {
                scan.notes.push_back("Your last hosted session changed " + slot_label(f.slot) +
                                     ", but it was left out of your save: " + decision.why + ".");
            }
        }
    }
}

// A guest's character as the host's game saved it, sent back by the host.
void scan_guest(const SavePaths& p, const std::vector<std::string>& accounts, SaveScan& scan) {
    if (!is_folder(p.progress)) return;
    const auto hosts = list_folder(p.progress);
    if (!hosts) return;
    bool several_said = false;
    for (const Entry& h : *hosts) {
        if (!h.folder || h.link) continue;
        // A host is a Steam friend. Any other name ("unknown-host") is from the developers' tests without Steam.
        const auto host_id = parse_u64(narrow(h.name));
        if (!host_id || !valid_steamid(*host_id)) continue;
        const fs::path host_dir = p.progress / h.name, incoming = host_dir / L"incoming";
        if (!is_folder(incoming)) continue;
        const auto folders = list_folder(incoming);
        if (!folders) continue;
        std::vector<GuestGeneration> gens;
        for (const Entry& g : *folders) {
            if (!g.folder || g.link || gens.size() >= kMaxGenerations) continue;
            const std::string name = narrow(g.name);
            if (name.size() < 4 || !name.starts_with("gen") || name[3] == '0') continue;
            const auto number = parse_u64(std::string_view(name).substr(3));
            if (!number || *number > 0xFFFFFFFFull) continue;
            // The manifest is written last: a generation without it did not arrive whole.
            const auto text = read_text(incoming / g.name / L"manifest.txt", kMaxText);
            auto manifest = text ? gsw::parse_incoming_manifest(*text) : std::nullopt;
            if (!manifest) continue;
            gens.push_back({static_cast<uint32_t>(*number), std::move(*manifest)});
        }
        if (gens.empty()) continue;
        if (accounts.size() != 1) {
            if (!several_said) {
                scan.notes.push_back("A friend's game sent back your co-op progress, but this PC has the saves of " +
                                     std::to_string(accounts.size()) + " accounts and it does not say whose it is, "
                                     "so it was left out.");
            }
            several_said = true;
            continue;
        }
        newest_first(gens);
        std::optional<AppliedRecord> applied;
        if (const auto text = read_text(host_dir / L"applied.txt", kMaxText)) applied = parse_applied(*text);
        const fs::path account_dir = p.save / widen(accounts[0]);
        std::map<uint32_t, std::optional<std::string>> in_slot;  // the save.save each slot holds now
        for (const GuestGeneration& g : gens) {
            const uint32_t slot = g.manifest.slot;
            if (!in_slot.contains(slot)) {
                const fs::path file = account_dir / slot_folder(slot) / kSave;
                in_slot[slot] = present(file) ? std::optional<std::string>(sha256_of_file(file)) : std::nullopt;
            }
            if (line_problem(g, gens, applied, in_slot[slot]) != LineProblem::None) continue;
            // The newest that fits its slot. Its files have to be what the host announced, and loadable.
            const fs::path dir = incoming / (L"gen" + std::to_wstring(g.number));
            const SaveFile lobby = inspect(dir / kLobby), save = inspect(dir / kSave);
            const bool whole = lobby.present && save.present && lobby.size == g.manifest.files[0].size &&
                               save.size == g.manifest.files[1].size && lobby.sha256 == g.lobby_sha256() &&
                               save.sha256 == g.save_sha256() && lobby.check == gsv::HeaderCheck::Ok &&
                               save.check == gsv::HeaderCheck::Ok;
            if (!whole) continue;  // damaged on the way or on disk: an older one may still fit
            if (const auto problem = target_problem(account_dir, slot)) {
                scan.notes.push_back("Your progress from a friend's game was left out of your save: " + *problem + ".");
                break;
            }
            SlotChange c;
            c.from_guest = true;
            c.account = accounts[0];
            c.slot = slot;
            c.source = dir;
            c.lobby_sha256 = lobby.sha256;
            c.save_sha256 = save.sha256;
            c.saved = kUnixEpoch + g.manifest.time_unix * 10000000ull;
            c.when = when_text(c.saved);
            c.host_dir = host_dir;
            c.generation = g;
            scan.pending.push_back(std::move(c));
            break;
        }
    }
}

// ---- changing the save folder
bool copy_file(const fs::path& from, const fs::path& to, std::string& why) {
    if (CopyFileW(from.c_str(), to.c_str(), TRUE)) return true;  // with its write time
    why = "could not copy " + path_text(from.filename()) + " (" + error_text(GetLastError()) + ")";
    return false;
}

bool move_folder(const fs::path& from, const fs::path& to, std::string& why) {
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH)) return true;
    why = error_text(GetLastError());
    return false;
}

// Puts the change's two files into save\<account>\slotN, keeping the slot's other files. The new slot is made whole
// in the staging folder and checked there; then the old one is renamed out and the new one renamed in, and what is
// in place is read back. False = the slot is as it was, unless `why` says the put-back failed too (it then names
// where the slot's files are). Nothing named slotN_new or slotN_backup ever exists in the save folder: the game
// reads those as its own interrupted save.
bool apply_slot(const SavePaths& p, const SlotChange& c, const std::string& stamp_text, std::string& why) {
    const fs::path account_dir = p.save / widen(c.account);
    const std::wstring slot = slot_folder(c.slot);
    const fs::path target = account_dir / slot;
    if (!is_folder(p.save) || !is_folder(account_dir)) {
        why = "your save folder is not where it was";
        return false;
    }
    if (const auto problem = target_problem(account_dir, c.slot)) {
        why = *problem;
        return false;
    }
    std::error_code ec;
    fs::create_directories(p.staging, ec);
    const fs::path staged = p.staging / slot;
    if (!is_folder(p.staging) || !remove_own(staged) || !CreateDirectoryW(staged.c_str(), nullptr)) {
        why = "the folder to prepare it in (" + path_text(p.staging) + ") could not be used";
        return false;
    }
    const auto abandon = [&] {
        remove_own(staged);
        return false;
    };
    const bool had = present(target);
    if (had) {
        const auto files = list_folder(target);
        if (!files) {
            why = slot_label(c.slot) + " could not be read";
            return abandon();
        }
        for (const Entry& e : *files) {
            if (_wcsicmp(e.name.c_str(), kLobby) == 0 || _wcsicmp(e.name.c_str(), kSave) == 0) continue;
            if (!copy_file(target / e.name, staged / e.name, why)) return abandon();
        }
    }
    const std::pair<const wchar_t*, const std::string*> wanted[] = {{kLobby, &c.lobby_sha256}, {kSave, &c.save_sha256}};
    for (const auto& [name, sha] : wanted) {
        if (!copy_file(c.source / name, staged / name, why)) return abandon();
        if (sha->empty() || sha256_of_file(staged / name) != *sha) {
            why = "the copy of " + narrow(name) + " is not the file that was checked";
            return abandon();
        }
    }

    const fs::path old = p.staging / (slot + L"_old_" + widen(stamp_text));
    if (had && !move_folder(target, old, why)) {
        why = slot_label(c.slot) + " is in use (" + why + ")";
        return abandon();
    }
    std::string problem;
    bool ok = move_folder(staged, target, problem);
    for (const auto& [name, sha] : wanted) {
        if (ok && sha256_of_file(target / name) != *sha) {
            ok = false;
            problem = narrow(name) + " does not read back as it was written";
        }
    }
    if (!ok) {
        std::string undo;
        bool undone = true;
        if (present(target)) undone = move_folder(target, p.staging / (slot + L"_failed_" + widen(stamp_text)), undo);
        if (undone && had) undone = move_folder(old, target, undo);
        why = problem;
        if (!undone) {
            why += "; and " + slot_label(c.slot) + " could not be put back (" + undo + "): its files are in " +
                   path_text(had ? old : staged);
        }
        remove_own(staged);
        return false;
    }
    if (had) remove_own(old);
    return true;
}

void log_line(const SavePaths& p, const std::string& text) {
    if (p.log.empty()) return;
    std::error_code ec;
    fs::create_directories(p.log.parent_path(), ec);
    const HANDLE h = CreateFileW(p.log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    const std::string line = clock_text(local_now()) + " " + text + "\r\n";
    DWORD wrote = 0;
    WriteFile(h, line.data(), static_cast<DWORD>(line.size()), &wrote, nullptr);
    CloseHandle(h);
}

// Every file under `root` (never through a link), with its size and SHA-256.
bool walk(const fs::path& root, const std::wstring& rel, std::vector<hsv::TreeFile>& out, std::string& error) {
    const fs::path dir = rel.empty() ? root : root / rel;
    const auto list = list_folder(dir);
    if (!list) {
        error = path_text(dir) + " could not be read";
        return false;
    }
    for (const Entry& e : *list) {
        const std::wstring child = rel.empty() ? e.name : rel + L"\\" + e.name;
        if (e.link) {
            error = path_text(root / child) + " is a link";
            return false;
        }
        if (e.folder) {
            if (!walk(root, child, out, error)) return false;
            continue;
        }
        std::string sha = sha256_of_file(root / child);
        if (sha.empty()) {
            error = path_text(root / child) + " could not be read";
            return false;
        }
        out.push_back({narrow(child), e.size, std::move(sha)});
    }
    return true;
}

// The backup still has every file its list names, at the listed size.
bool backup_intact(const fs::path& dir, const std::vector<hsv::TreeFile>& listed) {
    return std::all_of(listed.begin(), listed.end(), [&](const hsv::TreeFile& f) {
        const Stat s = stat_of(dir / L"save" / widen(f.path));
        return s.present && s.size == f.size;
    });
}

}  // namespace

// ------------------------------------------------------------------ where things are
SavePaths save_paths(const Paths& paths, const fs::path& local_dir) {
    SavePaths p;
    fs::path local = local_dir;
    if (local.empty()) {
        wchar_t buf[MAX_PATH] = {};
        const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) local = buf;
    }
    if (!local.empty()) {
        const fs::path cd = local / L"Pearl Abyss" / L"CD";
        p.save = cd / L"save";
        p.sandbox = cd / L"cdmp_save_sandbox";
        p.staging = cd / L"cdmp_launcher_staging";
        p.progress = local / L"cdmp" / L"guest_progress";
    }
    if (!paths.bin64.empty()) {
        // As the mod resolves it (host_save_export.cpp): empty = next to the game's exe, a relative path under it.
        std::string stated;
        if (const auto ini = read_text(paths.bin64 / L"cdmp.ini", kMaxText)) {
            stated = ini_get(*ini, "session2", "host_save_dir").value_or("");
        }
        while (!stated.empty() && (stated.back() == '"' || stated.back() == ' ' || stated.back() == '\t')) stated.pop_back();
        while (!stated.empty() && (stated.front() == '"' || stated.front() == ' ')) stated.erase(stated.begin());
        const fs::path dir(widen(stated));
        p.exports = stated.empty() ? paths.bin64 / L"cdmp_save_exports" : dir.is_absolute() ? dir : paths.bin64 / dir;
    }
    if (!paths.data.empty()) {
        p.backups = paths.data / L"save_backups";
        p.log = paths.data / L"saves.log";
    }
    return p;
}

std::string SaveScan::signature() const {
    std::string s;
    for (const SlotChange& c : pending) s += c.account + "/" + std::to_string(c.slot) + "=" + c.save_sha256 + ";";
    return s;
}

SaveScan scan_saves(const SavePaths& p) {
    SaveScan scan;
    if (p.save.empty() || !is_folder(p.save)) return scan;
    const auto top = list_folder(p.save);
    if (!top) return scan;
    std::vector<std::string> accounts;
    for (const Entry& e : *top) {
        if (e.folder && !e.link && hsv::account_folder(e.name)) accounts.push_back(narrow(e.name));
    }
    if (accounts.empty()) return scan;
    scan.found = true;
    scan_host(p, accounts, scan);
    scan_guest(p, accounts, scan);
    // Two things waiting for one slot (a session as a guest, then one as the host): only the later one.
    std::vector<SlotChange> kept;
    for (const SlotChange& c : scan.pending) {
        const bool later_one = std::any_of(scan.pending.begin(), scan.pending.end(), [&](const SlotChange& o) {
            return &o != &c && o.account == c.account && o.slot == c.slot &&
                   (o.saved > c.saved || (o.saved == c.saved && &o < &c));
        });
        if (!later_one) kept.push_back(c);
    }
    scan.pending = std::move(kept);
    std::sort(scan.pending.begin(), scan.pending.end(), [](const SlotChange& a, const SlotChange& b) {
        return a.account != b.account ? a.account < b.account : a.slot < b.slot;
    });
    return scan;
}

// ------------------------------------------------------------------ the backup
BackupResult backup_saves(const SavePaths& p) {
    BackupResult r;
    if (p.save.empty() || !present(p.save)) {
        r.ok = true;
        return r;
    }
    if (!is_folder(p.save)) {
        r.error = path_text(p.save) + " is not a plain folder";
        return r;
    }
    if (p.backups.empty()) {
        r.error = "there is no folder for backups";
        return r;
    }
    std::vector<hsv::TreeFile> tree;
    if (!walk(p.save, {}, tree, r.error)) return r;
    if (tree.empty()) {
        r.ok = true;
        return r;
    }
    std::error_code ec;
    fs::create_directories(p.backups, ec);
    if (!is_folder(p.backups)) {
        r.error = path_text(p.backups) + " could not be made";
        return r;
    }
    std::vector<std::string> names;
    if (const auto have = list_folder(p.backups)) {
        for (const Entry& e : *have) {
            if (e.folder && !e.link) names.push_back(narrow(e.name));
        }
    }
    names = newest_backups_first(std::move(names));
    // The newest finished backup already holds the folder as it is now: no second copy.
    for (const std::string& name : names) {
        const fs::path dir = p.backups / widen(name);
        const auto text = read_text(dir / widen(kBackupList), kMaxText);
        const auto listed = text ? parse_backup_list(*text) : std::nullopt;
        if (!listed) continue;
        if (hsv::same_tree(*listed, tree) && backup_intact(dir, *listed)) {
            r.ok = true;
            r.dir = dir;
            return r;
        }
        break;
    }

    const SYSTEMTIME now = local_now();
    std::string name = stamp(now);
    for (int n = 2; present(p.backups / widen(name)) || present(p.backups / widen(name + ".partial")); ++n) {
        if (n > 999) {
            r.error = "too many backups were made in one second";
            return r;
        }
        name = stamp(now) + "-" + std::to_string(n);
    }
    const fs::path partial = p.backups / widen(name + ".partial"), done = p.backups / widen(name);
    const auto fail = [&](std::string why) {
        remove_own(partial);
        r.error = std::move(why);
        return r;
    };
    for (const hsv::TreeFile& f : tree) {
        const fs::path from = p.save / widen(f.path), to = partial / L"save" / widen(f.path);
        fs::create_directories(to.parent_path(), ec);
        if (!CopyFileW(from.c_str(), to.c_str(), TRUE)) {
            return fail("could not copy " + f.path + " (" + error_text(GetLastError()) + ")");
        }
        if (sha256_of_file(to) != f.sha256) return fail("the copy of " + f.path + " is not the same as the file");
    }
    std::string why;
    if (!write_file(partial / widen(kBackupList), bytes_of(format_backup_list(clock_text(now), tree)), &why)) {
        return fail("could not write the backup's list (" + why + ")");
    }
    if (!MoveFileExW(partial.c_str(), done.c_str(), MOVEFILE_WRITE_THROUGH)) {
        return fail("could not finish the backup (" + error_text(GetLastError()) + ")");
    }
    r.ok = r.made = true;
    r.dir = done;
    names.push_back(name);
    for (const std::string& old : backups_to_remove(std::move(names))) remove_own(p.backups / widen(old));
    log_line(p, "backup " + path_text(done) + " (" + std::to_string(tree.size()) + " files)");
    return r;
}

// ------------------------------------------------------------------ keeping the progress
KeepResult keep_progress(const SavePaths& p, const std::function<bool()>& game_is_running,
                         const std::function<void(const std::string&)>& status) {
    KeepResult r;
    const auto running = [&] { return game_is_running && game_is_running(); };
    if (running()) {
        r.error = "Crimson Desert is running. Your progress goes into your save once the game is closed.";
        return r;
    }
    if (status) status("Looking at your saves...");
    const SaveScan scan = scan_saves(p);
    r.notes = scan.notes;
    if (scan.pending.empty()) {
        r.ok = true;
        return r;
    }
    if (status) status("Backing up your saves...");
    const BackupResult backup = backup_saves(p);
    if (!backup.ok || backup.dir.empty()) {
        r.error = "Your saves could not be backed up (" + (backup.error.empty() ? "nothing to copy" : backup.error) +
                  "), so your save was not changed.";
        log_line(p, "keep refused: " + r.error);
        return r;
    }
    r.backup = backup.dir;
    const std::string stamp_text = stamp(local_now());
    for (const SlotChange& c : scan.pending) {
        if (status) status("Putting your progress into " + slot_label(c.slot) + "...");
        std::string why;
        if (running()) {
            why = "Crimson Desert was started";
        } else if (apply_slot(p, c, stamp_text, why)) {
            r.kept.push_back(slot_label(c.slot) + " (saved " + c.when + ")");
            log_line(p, "kept " + slot_label(c.slot) + " of account " + c.account + " from " + path_text(c.source) +
                            " (save.save " + c.save_sha256 + "); the folder as it was: " + path_text(backup.dir));
            if (c.from_guest) {
                // What the mod tells that host next time, so it sends nothing older (as apply-guest-progress.ps1).
                const std::string record =
                    format_applied(c.generation, utc_text(), path_text(c.source), path_text(backup.dir));
                if (!write_file(c.host_dir / L"applied.txt", bytes_of(record), nullptr)) {
                    r.notes.push_back("The note of what was kept could not be written in " + path_text(c.host_dir) + ".");
                }
            }
            continue;
        }
        r.error = "Your progress could not be put into " + slot_label(c.slot) + ": " + why + ". A copy of your saves " +
                  "from before is in " + path_text(backup.dir) + ".";
        log_line(p, "keep FAILED for " + slot_label(c.slot) + " of account " + c.account + ": " + why);
        return r;
    }
    r.ok = true;
    return r;
}

}  // namespace cdmp::launcher
