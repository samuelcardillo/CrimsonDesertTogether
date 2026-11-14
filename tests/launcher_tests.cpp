// The launcher (tools/launcher): its rules (launcher_logic.h, save_logic.h) and its work on files (launcher_core.cpp,
// save_core.cpp) against fake kit, game, data and save folders in %TEMP%. Not tested here: the window, Steam's
// registry and the https download. Nothing here reads or writes the PC's real save folder.

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/sha256.h"
#include "launcher_core.h"
#include "save_core.h"

using namespace cdmp::launcher;
namespace fs = std::filesystem;

namespace {
int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++g_failures; } } while (0)

constexpr uint32_t kPe = 0x6AF5D198, kOtherPe = 0x6AED2E85;
constexpr uint64_t kFriend = 76561198000000001ull, kFriend2 = 76561198000000002ull;

std::string sha(std::string_view s) {
    return cdmp::sha256::hex(cdmp::sha256::of({reinterpret_cast<const uint8_t*>(s.data()), s.size()}));
}

void put(const fs::path& p, std::string_view text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary | std::ios::trunc).write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string get(const fs::path& p) { return read_text(p).value_or("<missing>"); }

// When a package's list was written decides which of the packages on this PC is the newest.
void set_time(const fs::path& p, int minutes_ago) {
    fs::last_write_time(p, fs::file_time_type::clock::now() - std::chrono::minutes(minutes_ago));
}

// The first 4 KB of a 64-bit exe with that PE timestamp.
std::string fake_exe(uint32_t timestamp, uint32_t image_size = 0x144E9000) {
    std::string b(4096, '\0');
    auto u16 = [&](size_t o, uint32_t v) { b[o] = static_cast<char>(v), b[o + 1] = static_cast<char>(v >> 8); };
    auto u32 = [&](size_t o, uint32_t v) { u16(o, v), u16(o + 2, v >> 16); };
    b[0] = 'M', b[1] = 'Z';
    u32(0x3C, 0x80);
    u32(0x80, 0x00004550);
    u16(0x84, 0x8664);
    u32(0x88, timestamp);
    u16(0x80 + 24, 0x20B);
    u32(0x80 + 24 + 56, image_size);
    return b;
}

std::span<const uint8_t> bytes(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()), s.size()}; }

// ------------------------------------------------------------------ Steam's files
void test_vdf() {
    const auto libs = parse_vdf("\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"C:\\\\Program Files (x86)\\\\Steam\"\n"
                                "\t\t\"apps\"\n\t\t{\n\t\t\t\"228980\"\t\t\"1\"\n\t\t}\n\t}\n"
                                "\t// a comment\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"D:\\\\SteamLibrary\"\n\t}\n}\n");
    CHECK(libs.has_value());
    if (libs) {
        const auto paths = steam_library_paths(*libs);
        CHECK(paths.size() == 2);
        CHECK(paths.size() == 2 && paths[0] == "C:\\Program Files (x86)\\Steam" && paths[1] == "D:\\SteamLibrary");
    }
    const auto old = parse_vdf("\"LibraryFolders\" { \"TimeNextStatsReport\" \"1\" \"1\" \"E:\\\\Games\" }");
    CHECK(old && steam_library_paths(*old) == std::vector<std::string>{"E:\\Games"});
    const auto acf = parse_vdf("\"AppState\"\n{\n\t\"appid\"\t\"3321460\"\n\t\"installdir\"\t\"Crimson Desert\"\n}\n");
    CHECK(acf && app_install_dir(*acf) == "Crimson Desert");
    CHECK(!parse_vdf("\"a\" { \"b\" \"c\"").has_value());   // block not closed
    CHECK(!parse_vdf("\"a\" \"unterminated").has_value());
    CHECK(!parse_vdf("\"a\" }").has_value());
    CHECK(parse_vdf("").has_value());
}

void test_contacts() {
    const auto config = parse_vdf(R"("UserLocalConfigStore"
{
	"friends"
	{
		"PersonaName"		"Me"
		"40000002"
		{
			"name"		"zoe"
			"avatar"		"abc"
		}
		"40000001"
		{
			"name"		"Adam \"The\" Great"
			"NameHistory"
			{
				"0"		"Adam"
			}
		}
		"40000009"
		{
			"name"		"Me"
		}
		"40000003"
		{
			"avatar"		"no name here"
		}
	}
}
)");
    CHECK(config.has_value());
    if (!config) return;
    const auto contacts = contacts_from_localconfig(*config, 40000009);
    CHECK(contacts.size() == 2);
    if (contacts.size() == 2) {
        CHECK(contacts[0].name == "Adam \"The\" Great" && contacts[0].id == steamid_from_account(40000001));
        CHECK(contacts[1].name == "zoe" && contacts[1].id == 76561197960265728ull + 40000002);
    }
    const auto users = parse_vdf("\"users\" { \"76561198000000001\" { \"Timestamp\" \"100\" } "
                                 "\"76561198000000002\" { \"Timestamp\" \"200\" } }");
    CHECK(users && most_recent_login(*users) == kFriend2);
    const auto marked = parse_vdf("\"users\" { \"76561198000000001\" { \"MostRecent\" \"1\" \"Timestamp\" \"1\" } "
                                  "\"76561198000000002\" { \"Timestamp\" \"200\" } }");
    CHECK(marked && most_recent_login(*marked) == kFriend);
}

void test_steamid() {
    CHECK(parse_steamid("76561198000000001") == kFriend);
    CHECK(parse_steamid("  76561198000000001\r\n") == kFriend);
    CHECK(parse_steamid("https://steamcommunity.com/profiles/76561198000000001") == kFriend);
    CHECK(parse_steamid("https://steamcommunity.com/profiles/76561198000000001/friends?x=1") == kFriend);
    CHECK(!parse_steamid("https://steamcommunity.com/id/somebody"));
    CHECK(!parse_steamid("7656119800000000"));     // 16 digits
    CHECK(!parse_steamid("765611980000000012"));   // 18
    CHECK(!parse_steamid("12345678901234567"));    // not an individual account
    CHECK(!parse_steamid("76561197960265728"));    // account 0
    CHECK(!parse_steamid(""));
    CHECK(steamid_from_account(1) == 76561197960265729ull);
}

void test_pe() {
    const std::string exe = fake_exe(kPe);
    const auto id = pe_identity(bytes(exe));
    CHECK(id && id->timestamp == kPe && id->image_size == 0x144E9000);
    CHECK(!pe_identity(bytes(exe).first(0x90)));  // cut before the optional header
    std::string not_64 = exe;
    not_64[0x84] = 0x4C, not_64[0x85] = 0x01;
    CHECK(!pe_identity(bytes(not_64)));
    CHECK(!pe_identity(bytes(std::string(4096, 'x'))));
}

// ------------------------------------------------------------------ packages
const std::string kH1(64, 'a'), kH2(64, 'b'), kH3(64, 'c'), kH4(64, 'd');

std::string package_text(std::string_view id, std::string_view games) {
    return "[package]\nid=" + std::string(id) + "\n" + std::string(games) + "file=winmm.dll " + kH1 +
           "\nfile=cdmp.asi " + kH2 + " https://example.invalid/x/cdmp.asi\nfile=cdmp.ini.host " + kH3 +
           "\nfile=cdmp.ini.guest " + kH4 + "\nfile=README.txt " + kH1 + "\n";
}

void test_packages() {
    const std::string text = "; newest first\n" + package_text("v22", "game=1.0.0.3663 0x6AF5D198\ngame = 1.0.0.3576 0x6aed2e85\n") +
                             package_text("v21", "game=1.0.0.3576 0x6AED2E85\n");
    const PackageList list = parse_packages(text);
    CHECK(list.problems.empty());
    CHECK(list.packages.size() == 2);
    if (list.packages.size() == 2) {
        const Package& p = list.packages[0];
        CHECK(p.id == "v22" && p.games.size() == 2 && p.games[0].version == "1.0.0.3663" && p.games[0].pe == kPe);
        CHECK(p.supports(kPe) && p.supports(kOtherPe) && !p.supports(1));
        CHECK(p.files.size() == 4);  // README.txt is not something the launcher installs
        CHECK(p.file("cdmp.asi") && p.file("cdmp.asi")->where == "https://example.invalid/x/cdmp.asi");
        CHECK(p.file("winmm.dll") && p.file("winmm.dll")->where.empty());
        CHECK(!list.packages[1].supports(kPe));
        // What the launcher writes next to its copies reads back as the same package.
        const PackageList again = parse_packages(format_package(p));
        CHECK(again.packages.size() == 1 && again.problems.empty());
        CHECK(again.packages.size() == 1 && again.packages[0].games.size() == 2 && again.packages[0].supports(kPe) &&
              again.packages[0].file("cdmp.asi")->sha256 == kH2 && again.packages[0].file("cdmp.asi")->where.empty());
    }

    CHECK(parse_packages("[package]\nid=x\nfile=winmm.dll " + kH1 + "\n").packages.empty());           // three missing
    CHECK(parse_packages(package_text("../evil", "")).packages.empty());                               // a path as id
    CHECK(parse_packages(package_text("", "")).packages.empty());
    CHECK(parse_packages(package_text("ok", "game=1.0 nothex\n")).packages.empty());
    std::string bad_hash = package_text("ok", "");
    bad_hash.replace(bad_hash.find(kH2), 64, std::string(64, 'z'));
    CHECK(parse_packages(bad_hash).packages.empty() && !parse_packages(bad_hash).problems.empty());
    CHECK(parse_packages(package_text("ok", "") + "file=cdmp.asi " + kH2 + "\n").packages.empty());    // listed twice
    CHECK(parse_packages("").packages.empty() && parse_packages("").problems.empty());
    CHECK(valid_package_id("20261113-3663-v22") && !valid_package_id(".hidden") && !valid_package_id("a/b") &&
          !valid_package_id("a b") && !valid_package_id(std::string(65, 'a')));
    CHECK(normal_sha256(std::string(64, 'A')) == std::string(64, 'a') && normal_sha256("abc").empty());
}

void test_locations() {
    const std::string web = "https://host.example/mods/package.txt";
    CHECK(resolve_location(web, "", "cdmp.asi") == "https://host.example/mods/cdmp.asi");
    CHECK(resolve_location(web, "v22/cdmp.asi", "cdmp.asi") == "https://host.example/mods/v22/cdmp.asi");
    CHECK(resolve_location(web + "?token=1", "", "cdmp.asi") == "https://host.example/mods/cdmp.asi");
    CHECK(resolve_location(web, "https://cdn.example/a/cdmp.asi", "cdmp.asi") == "https://cdn.example/a/cdmp.asi");
    CHECK(resolve_location("https://host.example", "", "cdmp.asi") == "https://host.example/cdmp.asi");
    CHECK(resolve_location(web, "http://cdn.example/cdmp.asi", "cdmp.asi").empty());    // not https
    CHECK(resolve_location(web, "ftp://cdn.example/cdmp.asi", "cdmp.asi").empty());
    CHECK(resolve_location(web, "C:\\Windows\\cdmp.asi", "cdmp.asi").empty());          // a list on the network, a file here
    CHECK(resolve_location(web, "\\\\pc\\share\\cdmp.asi", "cdmp.asi").empty());
    CHECK(resolve_location(web, "/cdmp.asi", "cdmp.asi").empty());
    CHECK(resolve_location("D:\\kit\\package.txt", "", "cdmp.asi") == "D:\\kit\\cdmp.asi");
    CHECK(resolve_location("\\\\pc\\share\\package.txt", "v22\\cdmp.asi", "cdmp.asi") == "\\\\pc\\share\\v22\\cdmp.asi");
    CHECK(resolve_location("D:\\kit\\package.txt", "E:\\other\\cdmp.asi", "cdmp.asi") == "E:\\other\\cdmp.asi");
    CHECK(resolve_location("D:\\kit\\package.txt", "https://cdn.example/cdmp.asi", "x") == "https://cdn.example/cdmp.asi");
    CHECK(is_https("HTTPS://x.example/") && !is_https("http://x.example/") && !is_https("https://"));
}

// ------------------------------------------------------------------ ini text
void test_ini() {
    const std::string text = "; top\r\n[general]\r\nplayer_name=Player\r\n; note\r\nstartup_delay_ms=0\r\n\r\n"
                             "[Net]\r\nRole = guest\r\n;role=host\r\n\r\n[steam]\r\npeer_steamid=\r\n\r\n"
                             "[net]\r\nrole=second\r\n";
    CHECK(ini_get(text, "general", "player_name") == "Player");
    CHECK(ini_get(text, "GENERAL", "PLAYER_NAME") == "Player");
    CHECK(ini_get(text, "net", "role") == "guest");       // the first [net], and not the commented line
    CHECK(ini_get(text, "steam", "peer_steamid") == "");
    CHECK(!ini_get(text, "steam", "missing"));
    CHECK(!ini_get(text, "nowhere", "role"));

    // An existing key: only its line changes, and it keeps its spelling.
    const std::string renamed = ini_set(text, "general", "player_name", "Sam");
    CHECK(ini_get(renamed, "general", "player_name") == "Sam");
    CHECK(renamed.size() == text.size() - 3);
    CHECK(renamed.find("player_name=Sam\r\n; note\r\nstartup_delay_ms=0\r\n") != std::string::npos);
    const std::string role = ini_set(text, "net", "role", "host");
    CHECK(role.find("Role=host\r\n;role=host\r\n") != std::string::npos);
    CHECK(role.find("[net]\r\nrole=second\r\n") != std::string::npos);  // the second [net] is not read, so not written

    // A new key goes after the section's last line, before the blank line that ends it.
    const std::string added = ini_set(text, "general", "extra", "1");
    CHECK(added.find("startup_delay_ms=0\r\nextra=1\r\n\r\n[Net]") != std::string::npos);
    CHECK(ini_get(added, "general", "extra") == "1");
    // A new section goes at the end.
    const std::string section = ini_set(text, "launcher", "mode", "join");
    CHECK(section.ends_with("role=second\r\n\r\n[launcher]\r\nmode=join\r\n"));
    CHECK(ini_get(section, "launcher", "mode") == "join");
    // Line breaks stay as the file has them, and a last line without one gets one.
    CHECK(ini_set("[a]\nx=1\n", "a", "y", "2") == "[a]\nx=1\ny=2\n");
    CHECK(ini_set("[a]\nx=1", "a", "y", "2") == "[a]\nx=1\ny=2\n");
    CHECK(ini_set("", "a", "x", "1") == "[a]\r\nx=1\r\n");
    CHECK(ini_set("[a]\r\nx=1", "b", "y", "2") == "[a]\r\nx=1\r\n\r\n[b]\r\ny=2\r\n");

    const auto entries = ini_entries("[friends]\r\n1=a\r\n; no\r\n2 = b = c\r\n[other]\r\n3=d\r\n", "friends");
    CHECK(entries.size() == 2 && entries[0].first == "1" && entries[1].first == "2" && entries[1].second == "b = c");
}

void test_names() {
    CHECK(sanitize_player_name("  Sam  ") == "Sam");
    CHECK(sanitize_player_name("") == "Player");
    CHECK(sanitize_player_name("\"\"") == "Player");
    CHECK(sanitize_player_name("Sam\r\n[net]\r\nrole=host") == "Sam[net]role=host");  // one line, whatever is typed
    CHECK(sanitize_player_name("Jos\xC3\xA9") == "Jos");                                // the mod's reader is not UTF-8
    CHECK(sanitize_player_name(std::string(40, 'x')).size() == kMaxNameLen);
    CHECK(clean_label("a\tb\nc") == "abc");
    CHECK(clean_label("\xED\x95\x9C\xEA\xB8\x80", 4) == "\xED\x95\x9C");  // cut between characters, not inside one
}

// The mod's own reader on a file the launcher wrote.
std::string profile(const fs::path& ini, const wchar_t* section, const wchar_t* key) {
    wchar_t buf[256] = {};
    GetPrivateProfileStringW(section, key, L"<none>", buf, 256, ini.c_str());
    return narrow(buf);
}

void test_mod_ini(const fs::path& tmp) {
    const std::string source = CDMP_SOURCE_DIR;
    const std::string host = get(fs::path(source) / "kit" / "coop" / "cdmp.ini.host");
    const std::string guest = get(fs::path(source) / "kit" / "coop" / "cdmp.ini.guest");
    CHECK(host.size() > 1000 && guest.size() > 1000);
    const std::string hh = sha(host), gh = sha(guest);

    CHECK(!mod_ini("", host, hh, Role::Guest, "Sam", 0));  // the host's template is not a guest profile
    const auto fresh = mod_ini("", guest, gh, Role::Guest, "Sam", kFriend);
    CHECK(fresh && fresh->rewritten);
    if (!fresh) return;
    const auto marker = ini_marker(fresh->text);
    CHECK(marker && marker->role == "guest" && marker->template_sha256 == gh);
    CHECK(ini_get(fresh->text, "general", "player_name") == "Sam");
    CHECK(ini_get(fresh->text, "steam", "peer_steamid") == "76561198000000001");
    CHECK(ini_get(fresh->text, "net", "role") == "guest");
    // Apart from its three first lines and the two values, it is the kit's file byte for byte.
    size_t body = 0;
    for (int i = 0; i < 3; ++i) body = fresh->text.find('\n', body) + 1;
    CHECK(fresh->text.substr(body) ==
          ini_set(ini_set(guest, "general", "player_name", "Sam"), "steam", "peer_steamid", "76561198000000001"));

    // The game's reader sees the same values.
    const fs::path ini = tmp / "profile" / "cdmp.ini";
    put(ini, fresh->text);
    CHECK(profile(ini, L"general", L"player_name") == "Sam");
    CHECK(profile(ini, L"steam", L"peer_steamid") == "76561198000000001");
    CHECK(profile(ini, L"net", L"role") == "guest");
    CHECK(profile(ini, L"net", L"transport") == "steam");
    CHECK(profile(ini, L"session2", L"guest_shop_branch") == "5");

    // Same role, same template: the player's own change stays; only the two values move.
    const std::string tweaked = ini_set(fresh->text, "session2", "vehicle_follow", "0");
    const auto again = mod_ini(tweaked, guest, gh, Role::Guest, "Samuel", 0);
    CHECK(again && !again->rewritten);
    CHECK(again && ini_get(again->text, "session2", "vehicle_follow") == "0");
    CHECK(again && ini_get(again->text, "general", "player_name") == "Samuel");
    CHECK(again && ini_get(again->text, "steam", "peer_steamid") == "");
    // Nothing to change: the same text.
    const auto same = mod_ini(fresh->text, guest, gh, Role::Guest, "Sam", kFriend);
    CHECK(same && !same->rewritten && same->text == fresh->text);

    // Another template (a mod update) or another role: the kit's file again, without the change.
    const std::string updated = ini_set(guest, "session2", "new_setting", "1");
    const auto after_update = mod_ini(tweaked, updated, sha(updated), Role::Guest, "Sam", 0);
    CHECK(after_update && after_update->rewritten);
    CHECK(after_update && ini_get(after_update->text, "session2", "vehicle_follow") == "1");
    CHECK(after_update && ini_get(after_update->text, "session2", "new_setting") == "1");
    const auto as_host = mod_ini(tweaked, host, hh, Role::Host, "Sam", 0);
    CHECK(as_host && as_host->rewritten && ini_get(as_host->text, "net", "role") == "host");
    // A file whose role was changed by hand under the launcher's first line is not trusted.
    const auto edited = mod_ini(ini_set(fresh->text, "net", "role", "host"), guest, gh, Role::Guest, "Sam", 0);
    CHECK(edited && edited->rewritten && ini_get(edited->text, "net", "role") == "guest");
    // The player's own file from before the launcher has no first line of ours.
    CHECK(!ini_marker(guest));
    const auto over_own = mod_ini(guest, guest, gh, Role::Guest, "Sam", 0);
    CHECK(over_own && over_own->rewritten);
}

void test_plan() {
    const std::string asi = kH2, loader = kH1, old_loader = kH3, foreign = kH4;
    FolderState empty;
    SyncPlan p = plan_sync(true, empty, asi, loader, "");
    CHECK(p.copy_asi && p.copy_loader && !p.remove_asi && !p.remove_loader && !p.foreign_loader);

    FolderState installed{true, asi, true, loader, false};
    p = plan_sync(true, installed, asi, loader, "");
    CHECK(!p.changes() && !p.foreign_loader);
    p = plan_sync(false, installed, asi, loader, "");
    CHECK(p.remove_asi && p.remove_loader && !p.loader_kept);
    p = plan_sync(false, installed, "", "", loader);  // no package at hand: the recorded loader is still ours
    CHECK(p.remove_asi && p.remove_loader);
    p = plan_sync(false, installed, "", "", "");      // nothing says the loader is ours
    CHECK(p.remove_asi && !p.remove_loader && p.foreign_loader);

    FolderState old{true, kH4, true, old_loader, false};
    p = plan_sync(true, old, asi, loader, old_loader);  // an earlier package's files: both replaced
    CHECK(p.copy_asi && p.copy_loader && !p.foreign_loader);

    FolderState other{true, asi, true, foreign, false};
    p = plan_sync(true, other, asi, loader, old_loader);  // somebody else's winmm.dll
    CHECK(!p.copy_asi && !p.copy_loader && p.foreign_loader);
    p = plan_sync(false, other, asi, loader, old_loader);
    CHECK(p.remove_asi && !p.remove_loader && p.foreign_loader);

    FolderState shared{true, asi, true, loader, true};  // another mod's .asi needs the loader
    p = plan_sync(false, shared, asi, loader, "");
    CHECK(p.remove_asi && !p.remove_loader && p.loader_kept);

    FolderState nothing_of_ours{false, "", false, "", true};
    p = plan_sync(false, nothing_of_ours, asi, loader, "");
    CHECK(!p.changes());
}

void test_settings() {
    Settings s;
    s.game_dir = "D:\\Games\\Crimson Desert\\bin64";
    s.player_name = "Sam";
    s.mode = Mode::Host;
    s.join_host = kFriend;
    s.host_accept = kFriend2;
    s.update_address = "https://host.example/mods/";
    s.installed_loader_sha256 = kH1;
    s.friends = {{kFriend, "Adam = the first"}, {kFriend2, "\xED\x95\x9C\xEA\xB8\x80"}};
    const Settings r = parse_settings(format_settings(s));
    CHECK(r.game_dir == s.game_dir && r.player_name == "Sam" && r.mode == Mode::Host);
    CHECK(r.join_host == kFriend && r.host_accept == kFriend2 && r.update_address == s.update_address);
    CHECK(r.installed_loader_sha256 == kH1);
    CHECK(r.friends.size() == 2 && r.friends[0].name == "Adam = the first" && r.friends[1].id == kFriend2 &&
          r.friends[1].name == s.friends[1].name);
    const Settings d = parse_settings("garbage\n[launcher]\nmode=whatever\njoin_host=12\n[friends]\nabc=x\n12=y\n");
    CHECK(d.mode == Mode::Join && d.join_host == 0 && d.friends.empty() && d.player_name == "Player");
}

// ------------------------------------------------------------------ files
struct Kit {
    fs::path dir;
    std::string asi, loader;
};

// A kit folder as scripts/package_coop.ps1 makes it: the four files, and package.txt unless `listed` is false.
Kit make_kit(const fs::path& dir, std::string_view id, uint32_t pe, std::string_view tag, bool listed = true) {
    const std::string source = CDMP_SOURCE_DIR;
    Kit kit{dir, "asi of " + std::string(tag), "loader of " + std::string(tag)};
    const std::string host = get(fs::path(source) / "kit" / "coop" / "cdmp.ini.host");
    const std::string guest = get(fs::path(source) / "kit" / "coop" / "cdmp.ini.guest");
    put(dir / "cdmp.asi", kit.asi);
    put(dir / "winmm.dll", kit.loader);
    put(dir / "cdmp.ini.host", host);
    put(dir / "cdmp.ini.guest", guest);
    if (listed) {
        char game[64];
        std::snprintf(game, sizeof game, "game=1.0.0.9999 0x%08X\r\n", pe);
        put(dir / "package.txt", "[package]\r\nid=" + std::string(id) + "\r\n" + game + "file=winmm.dll " + sha(kit.loader) +
                                     "\r\nfile=cdmp.asi " + sha(kit.asi) + "\r\nfile=cdmp.ini.host " + sha(host) +
                                     "\r\nfile=cdmp.ini.guest " + sha(guest) + "\r\n");
    }
    return kit;
}

struct World {
    Paths paths;
    Settings settings;
    Catalog catalog;
    Choice choice;

    void look() {
        catalog = load_catalog(paths, update_address(paths, settings), {});
        choice = choose(catalog, read_game(paths.bin64).pe.timestamp);
    }
    bool have() const { return choice.source && on_this_pc(catalog, *choice.source); }
    ReadyResult ready(Mode mode, uint64_t peer = 0, bool running = false) {
        ReadyRequest request;
        request.mode = mode;
        request.player_name = "Sam";
        request.peer = peer;
        request.source = choice.source;
        request.recorded_loader = settings.installed_loader_sha256;
        request.game_is_running = [running] { return running; };
        ReadyResult result = make_ready(paths, request, {});
        if (result.ok) settings.installed_loader_sha256 = result.recorded_loader;
        return result;
    }
};

World make_world(const fs::path& root) {
    World w;
    w.paths.bin64 = root / "game" / "bin64";
    w.paths.data = root / "data";
    w.paths.bundle = root / "kit";
    put(w.paths.bin64 / "CrimsonDesert.exe", fake_exe(kPe));
    return w;
}

void test_game_folder(const fs::path& tmp) {
    World w = make_world(tmp / "game_folder");
    const GameInfo game = read_game(w.paths.bin64);
    CHECK(game.found && game.pe.timestamp == kPe && game.version.empty());
    CHECK(!read_game(tmp / "nowhere").found);
    CHECK(normalize_game_dir(w.paths.bin64) == w.paths.bin64);
    CHECK(normalize_game_dir(w.paths.bin64.parent_path()) == w.paths.bin64);
    CHECK(normalize_game_dir(tmp).empty());
    put(w.paths.bin64 / "not_a_game.exe", "x");
    CHECK(!folder_state(w.paths.bin64).asi && !folder_state(w.paths.bin64).other_asi);
    put(w.paths.bin64 / "plugins" / "Other.ASI", "x");
    CHECK(folder_state(w.paths.bin64).other_asi);
}

void test_install_and_remove(const fs::path& tmp) {
    World w = make_world(tmp / "install");
    const Kit kit = make_kit(w.paths.bundle, "v1", kPe, "v1");
    w.look();
    CHECK(w.catalog.notes.empty());
    CHECK(w.choice.source && w.choice.stated && w.choice.source->kind == Source::Kind::Kit);
    if (!w.choice.source) return;

    // The game is running: nothing at all is written.
    ReadyResult r = w.ready(Mode::Join, kFriend, true);
    CHECK(!r.ok && !r.error.empty());
    CHECK(!fs::exists(w.paths.bin64 / "cdmp.asi") && !fs::exists(w.paths.data));

    // Join: the mod goes in and cdmp.ini is the guest's, with the name and the host.
    r = w.ready(Mode::Join, kFriend);
    CHECK(r.ok && r.error.empty() && r.notes.empty());
    CHECK(get(w.paths.bin64 / "cdmp.asi") == kit.asi && get(w.paths.bin64 / "winmm.dll") == kit.loader);
    const fs::path ini = w.paths.bin64 / "cdmp.ini";
    CHECK(ini_get(get(ini), "net", "role") == "guest" && ini_get(get(ini), "general", "player_name") == "Sam");
    CHECK(ini_get(get(ini), "steam", "peer_steamid") == "76561198000000001");
    CHECK(w.settings.installed_loader_sha256 == sha(kit.loader));
    CHECK(get(w.paths.data / "packages" / "v1" / "cdmp.asi") == kit.asi);
    CHECK(fs::exists(w.paths.data / "packages" / "v1" / "package.txt"));
    CHECK(!fs::exists(w.paths.bin64 / "cdmp.asi.cdmp-new") && !fs::exists(w.paths.bin64 / "cdmp.ini.bak"));

    // The player changes a setting; playing again as a guest keeps it.
    put(ini, ini_set(get(ini), "session2", "vehicle_follow", "0"));
    r = w.ready(Mode::Join, 0);
    CHECK(r.ok && ini_get(get(ini), "session2", "vehicle_follow") == "0" && ini_get(get(ini), "steam", "peer_steamid") == "");
    CHECK(!fs::exists(w.paths.bin64 / "cdmp.ini.bak"));

    // Hosting: the host's profile; the guest's file with that change is kept as cdmp.ini.bak.
    r = w.ready(Mode::Host, kFriend2);
    CHECK(r.ok && r.notes.size() == 1);
    CHECK(ini_get(get(ini), "net", "role") == "host" && ini_get(get(ini), "steam", "peer_steamid") == "76561198000000002");
    CHECK(ini_get(get(w.paths.bin64 / "cdmp.ini.bak"), "session2", "vehicle_follow") == "0");

    // Single-player: the game folder has nothing of the mod but its settings.
    r = w.ready(Mode::Single);
    CHECK(r.ok && r.notes.empty());
    CHECK(!fs::exists(w.paths.bin64 / "cdmp.asi") && !fs::exists(w.paths.bin64 / "winmm.dll") && fs::exists(ini));
    CHECK(fs::exists(w.paths.bin64 / "CrimsonDesert.exe"));
    r = w.ready(Mode::Single);  // and again: nothing to do
    CHECK(r.ok);

    // Without the kit folder the launcher's own copy is enough to play again.
    w.paths.bundle = tmp / "install" / "kit_was_deleted";
    w.look();
    CHECK(w.choice.source && w.choice.source->kind == Source::Kind::Cache && w.choice.stated);
    r = w.ready(Mode::Join);
    CHECK(r.ok && get(w.paths.bin64 / "cdmp.asi") == kit.asi && get(w.paths.bin64 / "winmm.dll") == kit.loader);
}

void test_other_peoples_files(const fs::path& tmp) {
    // Somebody else's winmm.dll is never replaced nor removed.
    World w = make_world(tmp / "foreign");
    const Kit kit = make_kit(w.paths.bundle, "v1", kPe, "v1");
    put(w.paths.bin64 / "winmm.dll", "another mod's loader");
    w.look();
    ReadyResult r = w.ready(Mode::Host);
    CHECK(r.ok && r.notes.size() == 1);
    CHECK(get(w.paths.bin64 / "winmm.dll") == "another mod's loader" && get(w.paths.bin64 / "cdmp.asi") == kit.asi);
    CHECK(w.settings.installed_loader_sha256.empty());
    r = w.ready(Mode::Single);
    CHECK(r.ok && get(w.paths.bin64 / "winmm.dll") == "another mod's loader" && !fs::exists(w.paths.bin64 / "cdmp.asi"));

    // Our loader stays while another mod's .asi needs it.
    World s = make_world(tmp / "shared");
    const Kit kit2 = make_kit(s.paths.bundle, "v1", kPe, "v1");
    put(s.paths.bin64 / "othermod.asi", "x");
    s.look();
    CHECK(s.ready(Mode::Join).ok);
    r = s.ready(Mode::Single);
    CHECK(r.ok && r.notes.size() == 1);
    CHECK(!fs::exists(s.paths.bin64 / "cdmp.asi") && get(s.paths.bin64 / "winmm.dll") == kit2.loader);
    CHECK(get(s.paths.bin64 / "othermod.asi") == "x");

    // The player's own cdmp.ini from before the launcher is kept once, and never overwritten afterwards.
    World o = make_world(tmp / "own_ini");
    make_kit(o.paths.bundle, "v1", kPe, "v1");
    put(o.paths.bin64 / "cdmp.ini", "[general]\r\nplayer_name=Mine\r\n[net]\r\nrole=guest\r\n");
    o.look();
    r = o.ready(Mode::Join);
    CHECK(r.ok && r.notes.size() == 1);
    CHECK(ini_get(get(o.paths.bin64 / "cdmp.ini.before-launcher"), "general", "player_name") == "Mine");
    CHECK(ini_marker(get(o.paths.bin64 / "cdmp.ini")).has_value());
    r = o.ready(Mode::Host);
    CHECK(r.ok && fs::exists(o.paths.bin64 / "cdmp.ini.bak"));
    CHECK(ini_get(get(o.paths.bin64 / "cdmp.ini.before-launcher"), "general", "player_name") == "Mine");
}

void test_bad_packages(const fs::path& tmp) {
    // A file that is not what the package says is not installed.
    World w = make_world(tmp / "tampered");
    make_kit(w.paths.bundle, "v1", kPe, "v1");
    put(w.paths.bundle / "cdmp.asi", "something else");
    w.look();
    CHECK(w.choice.source != nullptr);
    ReadyResult r = w.ready(Mode::Join);
    CHECK(!r.ok && r.error.find("cdmp.asi") != std::string::npos);
    CHECK(!fs::exists(w.paths.bin64 / "cdmp.asi") && !fs::exists(w.paths.bin64 / "winmm.dll") &&
          !fs::exists(w.paths.bin64 / "cdmp.ini"));
    CHECK(!fs::exists(w.paths.data / "packages" / "v1" / "cdmp.asi"));

    // A kit for another game build: no package to play with; single-player still works.
    World o = make_world(tmp / "other_build");
    make_kit(o.paths.bundle, "v0", kOtherPe, "v0");
    o.look();
    CHECK(!o.choice.source && o.catalog.sources.size() == 1);
    r = o.ready(Mode::Join);
    CHECK(!r.ok && !fs::exists(o.paths.bin64 / "cdmp.asi"));
    CHECK(o.ready(Mode::Single).ok);

    // A kit folder from before the launcher (no package.txt): usable, without a word on the game build.
    World u = make_world(tmp / "unlisted");
    const Kit kit = make_kit(u.paths.bundle, "", 0, "old", false);
    u.look();
    CHECK(u.choice.source && !u.choice.stated && u.choice.source->package.id.starts_with("kit-"));
    r = u.ready(Mode::Join);
    CHECK(r.ok && get(u.paths.bin64 / "cdmp.asi") == kit.asi);

    // No kit at all.
    World n = make_world(tmp / "no_kit");
    n.look();
    CHECK(!n.choice.source && n.catalog.sources.empty());
    CHECK(!n.ready(Mode::Host).ok && n.ready(Mode::Single).ok);
}

void test_update_address(const fs::path& tmp) {
    // The kit holds v1; the update address (a shared folder here) lists v2 for this game build: v2 is installed, and
    // then the earlier loader, which this launcher installed, is replaced too.
    World w = make_world(tmp / "update");
    const Kit v1 = make_kit(w.paths.bundle, "v1", kPe, "v1");
    set_time(w.paths.bundle / "package.txt", 60);  // the kit was made an hour ago
    w.look();
    CHECK(w.ready(Mode::Join).ok && get(w.paths.bin64 / "cdmp.asi") == v1.asi);

    const fs::path share = tmp / "update" / "share";
    const Kit v2 = make_kit(share, "v2", kPe, "v2");
    w.settings.update_address = path_text(share);  // a folder: its package.txt
    w.look();
    CHECK(w.catalog.update_ok && w.catalog.notes.empty());
    CHECK(w.choice.source && w.choice.source->kind == Source::Kind::Update && w.choice.source->package.id == "v2");
    ReadyResult r = w.ready(Mode::Join);
    CHECK(r.ok && get(w.paths.bin64 / "cdmp.asi") == v2.asi && get(w.paths.bin64 / "winmm.dll") == v2.loader);
    CHECK(w.settings.installed_loader_sha256 == sha(v2.loader));

    // The address does not answer: a note, and the update fetched before stays (the older kit folder does not undo it).
    w.settings.update_address = path_text(tmp / "update" / "gone" / "package.txt");
    w.look();
    CHECK(!w.catalog.update_ok && w.catalog.notes.size() == 1);
    CHECK(w.choice.source && w.choice.source->kind == Source::Kind::Cache && w.choice.source->package.id == "v2");
    r = w.ready(Mode::Join);
    CHECK(r.ok && get(w.paths.bin64 / "cdmp.asi") == v2.asi);

    // The kit can carry the address for players who set none; the player's own address comes before it, the
    // launcher's built-in one after it, and "off" in either stops the check.
    const std::string kit_address = path_text(share / "package.txt");
    put(w.paths.bundle / "cdmp_launcher.ini", "[update]\r\naddress=" + kit_address + "\r\n");
    w.settings.update_address.clear();
    w.look();
    CHECK(w.catalog.update_ok && w.choice.source && w.choice.source->package.id == "v2");
    CHECK(update_address(w.paths, w.settings, "https://built.in/package.txt") == kit_address);
    w.settings.update_address = "https://mine.example/";
    CHECK(update_address(w.paths, w.settings, "https://built.in/package.txt") == "https://mine.example/");
    w.settings.update_address = "OFF";
    CHECK(update_address(w.paths, w.settings, "https://built.in/package.txt").empty());
    w.settings.update_address.clear();
    put(w.paths.bundle / "cdmp_launcher.ini", "[update]\r\naddress=\r\n");
    CHECK(update_address(w.paths, w.settings, "https://built.in/package.txt") == "https://built.in/package.txt");
    CHECK(update_address(w.paths, w.settings).empty());
    put(w.paths.bundle / "cdmp_launcher.ini", "[update]\r\naddress=off\r\n");
    CHECK(update_address(w.paths, w.settings, "https://built.in/package.txt").empty());
    w.look();
    CHECK(w.catalog.update_address.empty() && !w.catalog.update_ok && w.catalog.notes.empty());

    // Only https is ever downloaded.
    CHECK(!http_get("http://example.invalid/package.txt", 1024, {}).ok);
    w.settings.update_address = "ftp://example.invalid/package.txt";
    w.look();
    CHECK(!w.catalog.update_ok && w.catalog.notes.size() == 1);

    Settings saved = w.settings;
    saved.friends = {{kFriend, "Adam"}};
    CHECK(save_settings(w.paths, saved));
    CHECK(load_settings(w.paths).friends.size() == 1 && load_settings(w.paths).update_address == saved.update_address);
}

void test_update_button(const fs::path& tmp) {
    // The launcher on its own (no kit folder), the newest build at the update address: nothing is on this PC yet.
    World w = make_world(tmp / "button");
    const fs::path share = tmp / "button" / "releases";
    const Kit b1 = make_kit(share, "b1", kPe, "b1");
    w.settings.update_address = path_text(share);
    w.look();
    CHECK(w.choice.source && w.choice.source->kind == Source::Kind::Update && !w.have());

    // Update without the mod in use (single-player): the files come to the launcher's folder, the game folder is
    // not touched, and from then on nothing has to be downloaded.
    std::string error, last;
    CHECK(fetch_package(w.paths, *w.choice.source, [&](const std::string& s) { last = s; }, {}, &error));
    CHECK(error.empty() && !last.empty());
    CHECK(get(w.paths.data / "packages" / "b1" / "cdmp.asi") == b1.asi);
    CHECK(!fs::exists(w.paths.bin64 / "cdmp.asi") && !fs::exists(w.paths.bin64 / "winmm.dll") &&
          !fs::exists(w.paths.bin64 / "cdmp.ini"));
    w.look();
    CHECK(w.choice.source && w.choice.source->kind == Source::Kind::Update && w.have());
    CHECK(w.ready(Mode::Join).ok && get(w.paths.bin64 / "cdmp.asi") == b1.asi);

    // A newer build is published: it is the one chosen, it is not here yet, and the installed one differs.
    fs::remove_all(share);
    const Kit b2 = make_kit(share, "b2", kPe, "b2");
    w.look();
    CHECK(w.choice.source && w.choice.source->package.id == "b2" && !w.have());
    CHECK(folder_state(w.paths.bin64).asi_sha256 != w.choice.source->package.file(kFileAsi)->sha256);
    // The download is refused when a file is not what the list says, and nothing of it is kept.
    put(share / "cdmp.asi", "tampered on the way");
    CHECK(!fetch_package(w.paths, *w.choice.source, {}, {}, &error) && error.find("cdmp.asi") != std::string::npos);
    CHECK(!fs::exists(w.paths.data / "packages" / "b2" / "cdmp.asi") && get(w.paths.bin64 / "cdmp.asi") == b1.asi);
    put(share / "cdmp.asi", b2.asi);
    CHECK(fetch_package(w.paths, *w.choice.source, {}, {}, &error));
    CHECK(w.ready(Mode::Join).ok && get(w.paths.bin64 / "cdmp.asi") == b2.asi && get(w.paths.bin64 / "winmm.dll") == b2.loader);

    // The update address is for a game build this PC does not have: the newest that is here for this build stays.
    fs::remove_all(share);
    make_kit(share, "b3", kOtherPe, "b3");
    set_time(w.paths.data / "packages" / "b1" / "package.txt", 30);
    w.look();
    CHECK(w.catalog.update_ok && w.choice.source && w.choice.source->kind == Source::Kind::Cache &&
          w.choice.source->package.id == "b2" && w.have());

    // The kit folder holds the very package the update address lists: it is taken from the kit, with no download.
    World k = make_world(tmp / "button_kit");
    make_kit(k.paths.bundle, "b1", kPe, "b1");
    make_kit(tmp / "button_kit" / "releases", "b1", kPe, "b1");
    k.settings.update_address = path_text(tmp / "button_kit" / "releases");
    k.look();
    CHECK(k.catalog.update_ok && k.choice.source && k.choice.source->kind == Source::Kind::Kit && k.have());

    // What is on this PC, newest first: a kit unzipped after the last copy was made is used; an older kit folder
    // is not. (No update address here.)
    World o = make_world(tmp / "button_order");
    make_kit(o.paths.bundle, "v1", kPe, "v1");
    o.look();
    CHECK(o.ready(Mode::Join).ok);  // v1 is now also a copy
    const fs::path copies = o.paths.data / "packages";
    make_kit(copies / "b5", "b5", kPe, "b5");
    set_time(o.paths.bundle / "package.txt", 60);
    set_time(copies / "b5" / "package.txt", 30);
    o.look();
    CHECK(o.catalog.sources.size() == 2);  // the kit's own copy is not listed twice
    CHECK(o.choice.source && o.choice.source->kind == Source::Kind::Cache && o.choice.source->package.id == "b5");
    set_time(o.paths.bundle / "package.txt", 10);
    o.look();
    CHECK(o.choice.source && o.choice.source->kind == Source::Kind::Kit && o.choice.source->package.id == "v1");
}

// ------------------------------------------------------------------ the saves (save_logic.h, save_core.cpp)
// A file the game would load: the 0x80-byte header and `data` bytes after it.
std::string fake_save(std::string_view tag, size_t data = 400) {
    std::string b(0x80 + data, '\0');
    auto u16 = [&](size_t o, uint32_t v) { b[o] = static_cast<char>(v), b[o + 1] = static_cast<char>(v >> 8); };
    auto u32 = [&](size_t o, uint32_t v) { u16(o, v), u16(o + 2, v >> 16); };
    b[0] = 'S', b[1] = 'A', b[2] = 'V', b[3] = 'E';
    u16(4, 2);
    u16(6, 0x80);
    u32(0x12, static_cast<uint32_t>(data * 3));
    u32(0x16, static_cast<uint32_t>(data));
    for (size_t i = 0; i < data; ++i) b[0x80 + i] = tag[i % tag.size()];
    return b;
}

std::string lobby_of(std::string_view tag) { return fake_save("lobby of " + std::string(tag), 60); }
std::string save_of(std::string_view tag) { return fake_save("save of " + std::string(tag)); }

void put_slot(const fs::path& dir, std::string_view tag, int minutes_ago) {
    put(dir / "lobby.save", lobby_of(tag));
    put(dir / "save.save", save_of(tag));
    set_time(dir / "lobby.save", minutes_ago);
    set_time(dir / "save.save", minutes_ago);
}

bool slot_is(const fs::path& dir, std::string_view tag) {
    return get(dir / "lobby.save") == lobby_of(tag) && get(dir / "save.save") == save_of(tag);
}

// "2026-11-14 16:58:25.479", this PC's time that many minutes ago: a run journal's start.
std::string local_time_text(int minutes_ago) {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER t;
    t.LowPart = now.dwLowDateTime, t.HighPart = now.dwHighDateTime;
    t.QuadPart -= static_cast<uint64_t>(minutes_ago) * 60 * 10000000ull;
    const FILETIME then{t.LowPart, t.HighPart};
    SYSTEMTIME utc{}, local{};
    FileTimeToSystemTime(&then, &utc);
    SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local);
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04u-%02u-%02u %02u:%02u:%02u.%03u", local.wYear, local.wMonth, local.wDay,
                  local.wHour, local.wMinute, local.wSecond, local.wMilliseconds);
    return buf;
}

// A folder's identity as the mod writes it in a run journal.
std::string folder_id(const fs::path& p) {
    const HANDLE h = CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    BY_HANDLE_FILE_INFORMATION info{};
    if (h != INVALID_HANDLE_VALUE) {
        GetFileInformationByHandle(h, &info);
        CloseHandle(h);
    }
    char buf[40];
    std::snprintf(buf, sizeof buf, "%08lX:%08lX%08lX", info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow);
    return buf;
}

// The journal lines of one save as the mod writes them: the Save, and the "flush timer" that writes it to disk.
std::string journal_save(uint32_t slot, gsv::Owner owner = gsv::Owner::Host, bool positive = true, int32_t result = 0) {
    hsv::SaveEvent s;
    s.seq = 1;
    s.time = "t";
    s.owner = owner;
    s.positive_host = positive;
    s.caller = "autosave";
    s.asked = s.written = slot;
    s.action = "pass";
    s.result_read = true;
    s.result = result;
    hsv::FlushEvent f;
    f.seq = 2;
    f.time = "t";
    f.owner = owner;
    f.positive_host = positive;
    f.caller = "flush timer";
    f.pending_read = true;
    f.pending = static_cast<int32_t>(slot);
    f.result_read = true;
    f.result = 0;
    return hsv::journal_save_line(s) + hsv::journal_flush_line(f);
}

hsv::Journal journal_of(const std::string& events, bool hooks = true) {
    const std::string text = hsv::journal_header_text({"run", "2026-11-14 16:31:42.137", "id", "0x1", "probe"}) +
                             (hooks ? hsv::journal_hooks_line(true, true) : std::string()) + events;
    return hsv::parse_journal(text).value_or(hsv::Journal{});
}

GuestGeneration generation(uint32_t number, uint32_t slot, uint64_t time, std::string_view tag, std::string_view root,
                           std::string_view base) {
    GuestGeneration g;
    g.number = number;
    g.manifest.gen = number;
    g.manifest.host_id = kFriend;
    g.manifest.slot = slot;
    g.manifest.reason = 3;
    g.manifest.time_unix = time;
    g.manifest.received_utc = "2026-11-14T02:44:23Z";
    const std::string lobby = lobby_of(tag), save = save_of(tag);
    g.manifest.files[0] = {static_cast<uint32_t>(lobby.size()), cdmp::sha256::of(bytes(lobby))};
    g.manifest.files[1] = {static_cast<uint32_t>(save.size()), cdmp::sha256::of(bytes(save))};
    if (!root.empty()) g.manifest.root = cdmp::sha256::of(bytes(save_of(root)));
    if (!base.empty()) g.manifest.base = cdmp::sha256::of(bytes(save_of(base)));
    return g;
}

void test_save_rules() {
    // A guest's generations continue ONE save. "a" was uploaded in the first session, which made g1; the second
    // session uploaded g1's save and made g2 and g3.
    const std::vector<GuestGeneration> gens = {generation(1, 2, 100, "g1", "a", "a"), generation(2, 2, 200, "g2", "a", "g1"),
                                               generation(3, 2, 300, "g3", "a", "g1")};
    const auto in_slot = [](std::string_view tag) { return std::optional<std::string>(sha(save_of(tag))); };
    CHECK(line_problem(gens[2], gens, {}, in_slot("a")) == LineProblem::None);        // the save it all came from
    CHECK(line_problem(gens[2], gens, {}, in_slot("g1")) == LineProblem::None);       // an older generation of it
    CHECK(line_problem(gens[2], gens, {}, in_slot("g2")) == LineProblem::None);
    CHECK(line_problem(gens[2], gens, {}, std::nullopt) == LineProblem::None);        // no save in the slot
    CHECK(line_problem(gens[2], gens, {}, in_slot("g3")) == LineProblem::AlreadyThere);
    CHECK(line_problem(gens[1], gens, {}, in_slot("g3")) == LineProblem::AlreadyThere);  // never backwards
    CHECK(line_problem(gens[0], gens, {}, in_slot("g1")) == LineProblem::AlreadyThere);
    CHECK(line_problem(gens[2], gens, {}, in_slot("mine")) == LineProblem::OtherSave);   // played on alone since
    CHECK(line_problem(gens[2], gens, {}, std::optional<std::string>("")) == LineProblem::OtherSave);  // unreadable
    CHECK(line_problem(generation(4, 2, 400, "g4", "", ""), gens, {}, in_slot("a")) == LineProblem::NoLine);
    // The slot holds a generation that is no longer on disk, but the record of what was applied names it.
    const std::vector<GuestGeneration> later = {gens[2]};
    CHECK(line_problem(gens[2], later, {}, in_slot("g0")) == LineProblem::OtherSave);
    const AppliedRecord record{7, sha(save_of("a")), sha(save_of("g0"))};
    CHECK(line_problem(gens[2], later, record, in_slot("g0")) == LineProblem::None);
    // A generation older than the ones made from the save in the slot.
    const std::vector<GuestGeneration> mixed = {generation(1, 2, 100, "g1", "a", "x"), generation(2, 2, 200, "g2", "a", "a")};
    CHECK(line_problem(mixed[0], mixed, {}, in_slot("a")) == LineProblem::Older);
    CHECK(line_problem(mixed[1], mixed, {}, in_slot("a")) == LineProblem::None);

    std::vector<GuestGeneration> order = {gens[0], gens[2], gens[1], generation(9, 2, 200, "g9", "a", "a")};
    newest_first(order);
    CHECK(order[0].number == 3 && order[1].number == 9 && order[2].number == 2 && order[3].number == 1);

    // applied.txt: what the launcher writes is what it (and the mod) reads.
    const auto applied = parse_applied(format_applied(gens[1], "2026-11-14T10:00:00Z", "C:\\from", "C:\\backup"));
    CHECK(applied && applied->generation == 2 && applied->root == sha(save_of("a")) && applied->save == sha(save_of("g2")));
    CHECK(!parse_applied("slot=2\r\n"));
    const auto manifest = gsw::parse_incoming_manifest(gsw::incoming_manifest_text(gens[1].manifest));
    GuestGeneration read_back;
    if (manifest) read_back.manifest = *manifest;
    CHECK(manifest && manifest->slot == 2 && read_back.base() == sha(save_of("g1")) && read_back.root() == sha(save_of("a")));

    // A host's slot: kept only when every save that went to it was the host's own and worked.
    HostSlotFacts f;
    f.slot = 0;
    f.lobby = f.save = f.changed = f.start_known = true;
    const hsv::Journal own = journal_of(journal_save(0) + journal_save(204, gsv::Owner::User2, false));
    CHECK(judge_host_slot(f, own).verdict == HostVerdict::Keep);
    const auto left_out = [&](const HostSlotFacts& facts, const hsv::Journal& j) {
        const HostDecision d = judge_host_slot(facts, j);
        return d.verdict == HostVerdict::LeftOut && !d.why.empty();
    };
    CHECK(left_out(f, journal_of(journal_save(0) + journal_save(0, gsv::Owner::User2, false))));  // the guest's too
    CHECK(left_out(f, journal_of(journal_save(0, gsv::Owner::Unidentified, false))));
    CHECK(left_out(f, journal_of(journal_save(0, gsv::Owner::Host, false))));   // "host" only for lack of a guest
    CHECK(left_out(f, journal_of(journal_save(0, gsv::Owner::Host, true, 5)))); // the save failed
    CHECK(left_out(f, journal_of(journal_save(1))));                            // changed, but no save to it was seen
    CHECK(left_out(f, journal_of(journal_save(0), false)));                     // nobody watched the saves
    CHECK(left_out(f, journal_of(journal_save(0) + "save|3|cut")));             // the journal ends in a cut line
    HostSlotFacts g = f;
    g.changed = false;
    CHECK(judge_host_slot(g, own).verdict == HostVerdict::Unchanged);
    g = f, g.slot = 204;  // the guest's slot on the host: never the host's
    CHECK(judge_host_slot(g, journal_of(journal_save(204))).verdict == HostVerdict::Unchanged);
    g = f, g.slot = 3;    // CrossSave
    CHECK(judge_host_slot(g, journal_of(journal_save(3))).verdict == HostVerdict::Unchanged);
    g = f, g.save = false;
    CHECK(left_out(g, own));
    g = f, g.save_check = gsv::HeaderCheck::FileSize;
    CHECK(left_out(g, own));
    g = f, g.leftovers = true;
    CHECK(left_out(g, own));
    g = f, g.subfolders = true;
    CHECK(left_out(g, own));
    g = f, g.real_newer = true;
    CHECK(left_out(g, own));
    g = f, g.start_known = false;
    CHECK(left_out(g, own));
    g = f, g.pair_gap_ms = 11000;
    CHECK(left_out(g, own));
    g = f, g.slot = 100;  // a manual save
    CHECK(judge_host_slot(g, journal_of(journal_save(100))).verdict == HostVerdict::Keep);

    const auto t = parse_local_time("2026-11-14 16:58:25.479");
    CHECK(t && t->year == 2026 && t->month == 11 && t->day == 14 && t->hour == 16 && t->minute == 58 && t->second == 25 &&
          t->millisecond == 479);
    CHECK(parse_local_time("2026-11-14 16:58:25") && !parse_local_time("2026-11-14") && !parse_local_time("") &&
          !parse_local_time("2026-13-14 16:58:25") && !parse_local_time("2026-11-14T16:58:25Z"));

    // Backups: only the launcher's own names, the newest ten stay.
    CHECK(backup_name("20261114-165825") && backup_name("20261114-165825-2") && !backup_name("20261114-165825.partial") &&
          !backup_name("save") && !backup_name("20261114-165825-0") && !backup_name("2026111-4165825"));
    std::vector<std::string> names = {"other", "20261114-165825-2", "20261114-165825"};
    for (int day = 1; day <= 12; ++day) names.push_back("202610" + std::string(day < 10 ? "0" : "") + std::to_string(day) + "-120000");
    const auto ordered = newest_backups_first(names);
    CHECK(ordered.size() == 14 && ordered[0] == "20261114-165825-2" && ordered[1] == "20261114-165825" &&
          ordered.back() == "20261001-120000");
    const auto gone = backups_to_remove(names);
    CHECK(gone.size() == 4 && gone[0] == "20261004-120000" && gone[3] == "20261001-120000");
    CHECK(backups_to_remove({"20261114-165825", "x"}).empty());
    const std::vector<hsv::TreeFile> tree = {{"1\\slot0\\save.save", 528, kH1}, {"user_input.save", 9, kH2}};
    const auto listed = parse_backup_list(format_backup_list("2026-11-14 18:00:00", tree));
    CHECK(listed && hsv::same_tree(*listed, tree));
    std::string unfinished = format_backup_list("x", tree);
    unfinished.resize(unfinished.find("state="));
    CHECK(!parse_backup_list(unfinished) && !parse_backup_list("") && !parse_backup_list("something else\r\nstate=complete\r\n"));
}

constexpr char kAccount[] = "40000007";  // a made-up account folder: the game names it after the account's number

struct SaveWorld {
    Paths paths;
    SavePaths at;
    fs::path real(int slot) const { return at.save / kAccount / ("slot" + std::to_string(slot)); }
    fs::path boxed(int slot) const { return at.sandbox / kAccount / ("slot" + std::to_string(slot)); }
};

SaveWorld make_save_world(const fs::path& root) {
    SaveWorld w;
    w.paths.bin64 = root / "game" / "bin64";
    w.paths.data = root / "data";
    put(w.paths.bin64 / "CrimsonDesert.exe", fake_exe(kPe));
    w.at = save_paths(w.paths, root / "local");
    return w;
}

// What the mod does when the game starts: the sandbox is a new copy of the real folder, with the files' times.
void start_session(const SaveWorld& w, const std::string& events, bool hosting = true, int minutes_ago = 30) {
    fs::remove_all(w.at.sandbox);
    for (const auto& entry : fs::recursive_directory_iterator(w.at.save)) {
        if (!entry.is_regular_file()) continue;
        const fs::path to = w.at.sandbox / fs::relative(entry.path(), w.at.save);
        fs::create_directories(to.parent_path());
        fs::copy_file(entry.path(), to);
        fs::last_write_time(to, fs::last_write_time(entry.path()));
    }
    if (!hosting) return;  // a guest's game keeps no journal
    static int run = 0;
    const std::string name = "20261114-1631" + std::to_string(10 + ++run) + "-p1";
    put(w.at.exports / "runs" / (name + ".txt"),
        hsv::journal_header_text({name, local_time_text(minutes_ago), folder_id(w.at.sandbox), "0x1", "probe"}) +
            hsv::journal_hooks_line(true, true) + events);
}

size_t count_dirs(const fs::path& dir) {
    std::error_code ec;
    size_t n = 0;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) n += it->is_directory() ? 1 : 0;
    return n;
}

void test_backups(const fs::path& tmp) {
    SaveWorld w = make_save_world(tmp / "backups");
    CHECK(w.at.save == tmp / "backups" / "local" / "Pearl Abyss" / "CD" / "save");
    CHECK(w.at.exports == w.paths.bin64 / "cdmp_save_exports" && w.at.backups == w.paths.data / "save_backups");
    // Nothing to back up is not a failure.
    BackupResult b = backup_saves(w.at);
    CHECK(b.ok && !b.made && b.dir.empty() && !fs::exists(w.at.backups));

    put_slot(w.real(0), "zero", 60);
    put_slot(w.real(100), "manual", 90);
    put(w.at.save / "user_input.save", "keys");
    b = backup_saves(w.at);
    CHECK(b.ok && b.made && backup_name(path_text(b.dir.filename())));
    CHECK(slot_is(b.dir / "save" / kAccount / "slot0", "zero") && slot_is(b.dir / "save" / kAccount / "slot100", "manual"));
    CHECK(get(b.dir / "save" / "user_input.save") == "keys" && fs::exists(b.dir / "backup.txt"));
    CHECK(fs::last_write_time(b.dir / "save" / kAccount / "slot0" / "save.save") == fs::last_write_time(w.real(0) / "save.save"));
    const fs::path first = b.dir;

    // The folder is as the newest backup has it: no second copy.
    b = backup_saves(w.at);
    CHECK(b.ok && !b.made && b.dir == first && count_dirs(w.at.backups) == 1);
    // A file changed: a new backup; the earlier one keeps the earlier file.
    put_slot(w.real(0), "zero again", 5);
    b = backup_saves(w.at);
    CHECK(b.ok && b.made && b.dir != first && count_dirs(w.at.backups) == 2);
    CHECK(slot_is(b.dir / "save" / kAccount / "slot0", "zero again") && slot_is(first / "save" / kAccount / "slot0", "zero"));
    // A backup that lost a file is not taken for a copy of the folder.
    fs::remove(b.dir / "save" / kAccount / "slot100" / "save.save");
    const fs::path damaged = b.dir;
    b = backup_saves(w.at);
    CHECK(b.ok && b.made && b.dir != damaged);

    // Only the newest ten stay; a folder that is not a backup is never removed.
    for (int day = 1; day <= 12; ++day) {
        put(w.at.backups / ("202601" + std::string(day < 10 ? "0" : "") + std::to_string(day) + "-120000") / "backup.txt", "old");
    }
    put(w.at.backups / "my own copy" / "x.txt", "mine");
    put_slot(w.real(0), "zero once more", 1);
    b = backup_saves(w.at);
    CHECK(b.ok && b.made && fs::exists(b.dir) && fs::exists(first) && fs::exists(w.at.backups / "my own copy" / "x.txt"));
    CHECK(count_dirs(w.at.backups) == 11);  // ten backups and the player's folder
    CHECK(!fs::exists(w.at.backups / "20260101-120000") && fs::exists(w.at.backups / "20260112-120000"));
}

void test_keep_host(const fs::path& tmp) {
    SaveWorld w = make_save_world(tmp / "host");
    put_slot(w.real(0), "zero", 300);
    put_slot(w.real(1), "one", 200);
    put_slot(w.real(2), "two", 100);
    put(w.at.save / kAccount / "steam_autocloud.vdf", "cloud");
    put(w.at.save / "user_input.save", "keys");

    // A hosted session: the host's autosaves went to slot 0 and slot 1, the guest's character to slot 204, and one
    // save of the guest's landed in slot 2 (it must never be taken for the host's).
    start_session(w, journal_save(0) + journal_save(1) + journal_save(204, gsv::Owner::User2, false) +
                         journal_save(2, gsv::Owner::User2, false));
    put_slot(w.boxed(0), "zero after co-op", 0);
    put_slot(w.boxed(1), "one after co-op", 0);
    put_slot(w.boxed(2), "the guest in two", 0);
    put_slot(w.boxed(204), "the guest", 0);

    SaveScan scan = scan_saves(w.at);
    CHECK(scan.found && scan.pending.size() == 2 && scan.notes.size() == 1);
    CHECK(scan.pending.size() == 2 && scan.pending[0].slot == 0 && scan.pending[1].slot == 1 && !scan.pending[0].from_guest &&
          scan.pending[0].account == kAccount && !scan.pending[0].when.empty());
    CHECK(scan.notes.size() == 1 && scan.notes[0].find("slot 2") != std::string::npos);
    CHECK(slot_is(w.real(0), "zero"));  // looking changes nothing

    // While the game runs nothing is written.
    KeepResult r = keep_progress(w.at, [] { return true; });
    CHECK(!r.ok && !r.error.empty() && slot_is(w.real(0), "zero") && !fs::exists(w.at.backups));

    // A file of the slot is open somewhere (Steam, an antivirus): that slot is left exactly as it was, and the
    // game was not started in between.
    {
        const HANDLE held = CreateFileW((w.real(0) / "save.save").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                        OPEN_EXISTING, 0, nullptr);
        CHECK(held != INVALID_HANDLE_VALUE);
        r = keep_progress(w.at, [] { return false; });
        CloseHandle(held);
        CHECK(!r.ok && r.error.find("slot 0") != std::string::npos && r.kept.empty());
        CHECK(slot_is(w.real(0), "zero") && slot_is(w.real(1), "one"));
        CHECK(count_dirs(w.at.staging) == 0);
    }

    std::string last;
    r = keep_progress(w.at, [] { return false; }, [&](const std::string& s) { last = s; });
    CHECK(r.ok && r.error.empty() && r.kept.size() == 2 && !last.empty());
    CHECK(slot_is(w.real(0), "zero after co-op") && slot_is(w.real(1), "one after co-op"));
    CHECK(slot_is(w.real(2), "two") && !fs::exists(w.real(204)));
    CHECK(get(w.at.save / kAccount / "steam_autocloud.vdf") == "cloud" && get(w.at.save / "user_input.save") == "keys");
    CHECK(fs::last_write_time(w.real(0) / "save.save") == fs::last_write_time(w.boxed(0) / "save.save"));
    // The folder as it was is in the backup, nothing is left in the staging folder, and nothing the game would
    // read as its own interrupted save was ever in the save folder.
    CHECK(slot_is(r.backup / "save" / kAccount / "slot0", "zero") && slot_is(r.backup / "save" / kAccount / "slot1", "one"));
    CHECK(count_dirs(w.at.staging) == 0 && count_dirs(w.at.save / kAccount) == 3);
    CHECK(get(w.paths.data / "saves.log").find("kept slot 0") != std::string::npos);
    // Nothing waits any more; the slot that was left out is still said.
    scan = scan_saves(w.at);
    CHECK(scan.pending.empty() && scan.notes.size() == 1);
    r = keep_progress(w.at, [] { return false; });
    CHECK(r.ok && r.kept.empty() && r.backup.empty());

    // The next session saves to a new slot (a manual save) and to slot 0 again: a new slot is made, an old one replaced.
    start_session(w, journal_save(100) + journal_save(0), true, 0);
    put_slot(w.boxed(100), "manual", 0);
    put_slot(w.boxed(0), "zero, third", 0);
    r = keep_progress(w.at, [] { return false; });
    CHECK(r.ok && r.kept.size() == 2 && slot_is(w.real(100), "manual") && slot_is(w.real(0), "zero, third"));
    CHECK(slot_is(r.backup / "save" / kAccount / "slot0", "zero after co-op") && !fs::exists(r.backup / "save" / kAccount / "slot100"));

    // The player went on alone, without the mod, after a hosted session nobody kept: their newer save stays.
    start_session(w, journal_save(1), true, 30);
    put_slot(w.boxed(1), "one, hosted", 20);
    put_slot(w.real(1), "one, alone afterwards", 5);
    scan = scan_saves(w.at);
    CHECK(scan.pending.empty() && scan.notes.size() == 1 && scan.notes[0].find("newer") != std::string::npos);
    CHECK(keep_progress(w.at, [] { return false; }).ok && slot_is(w.real(1), "one, alone afterwards"));

    // What the game left half done is its own to repair; a save that is not whole is not kept either.
    put_slot(w.real(1), "one", 200);
    start_session(w, journal_save(0) + journal_save(1) + journal_save(2), true, 0);
    put_slot(w.boxed(0), "zero, fourth", 0);
    put_slot(w.boxed(1), "one, fourth", 0);
    put_slot(w.boxed(2), "two, fourth", 0);
    fs::create_directories(w.at.save / kAccount / "slot0_new");
    put(w.boxed(1) / "save.save", save_of("one, fourth").substr(0, 300));  // cut while it was written
    put(w.boxed(2) / "lobby.save", lobby_of("two, fourth"));
    set_time(w.boxed(2) / "lobby.save", 9);  // not written with its save.save
    scan = scan_saves(w.at);
    CHECK(scan.pending.empty() && scan.notes.size() == 3);
    fs::remove(w.at.save / kAccount / "slot0_new");

    // A session as a guest (or any sandbox no hosting game made): nothing of it is taken for the host's progress.
    start_session(w, "", false);
    put_slot(w.boxed(0), "played alone with the mod on", 0);
    scan = scan_saves(w.at);
    CHECK(scan.found && scan.pending.empty() && scan.notes.empty());
    // The journal of another sandbox does not speak for this one.
    put(w.at.exports / "runs" / "20261114-999999-p1.txt",
        hsv::journal_header_text({"x", local_time_text(30), "00000000:0000000000000000", "0x1", "probe"}) +
            hsv::journal_hooks_line(true, true) + journal_save(0));
    CHECK(scan_saves(w.at).pending.empty());

    // cdmp.ini can name another folder for the host's journals, as the mod reads it.
    put(w.paths.bin64 / "cdmp.ini", "[session2]\r\nhost_save_dir=\"D:\\exports\"\r\n");
    CHECK(save_paths(w.paths, tmp).exports == fs::path("D:\\exports"));
    put(w.paths.bin64 / "cdmp.ini", "[session2]\r\nhost_save_dir=kept\\here\r\n");
    CHECK(save_paths(w.paths, tmp).exports == w.paths.bin64 / "kept" / "here");
    CHECK(!scan_saves(save_paths(Paths{}, tmp / "nothing here")).found);
}

void put_generation(const fs::path& host_dir, const GuestGeneration& g, std::string_view tag) {
    const fs::path dir = host_dir / "incoming" / ("gen" + std::to_string(g.number));
    put(dir / "lobby.save", lobby_of(tag));
    put(dir / "save.save", save_of(tag));
    put(dir / "manifest.txt", gsw::incoming_manifest_text(g.manifest));
}

void test_keep_guest(const fs::path& tmp) {
    SaveWorld w = make_save_world(tmp / "guest");
    put_slot(w.real(2), "mine", 300);
    put_slot(w.real(0), "another character", 400);
    const fs::path host = w.at.progress / std::to_string(kFriend);
    const uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                   std::chrono::system_clock::now().time_since_epoch()).count());
    // One session with a friend: the host's game saved the guest's character twice and sent both back.
    put_generation(host, generation(1, 2, now - 600, "after an hour", "mine", "mine"), "after an hour");
    put_generation(host, generation(2, 2, now - 60, "at the end", "mine", "mine"), "at the end");
    // The developers' own tests without Steam leave such a folder: never used.
    put_generation(w.at.progress / "unknown-host", generation(9, 2, now, "a test", "mine", "mine"), "a test");

    SaveScan scan = scan_saves(w.at);
    CHECK(scan.found && scan.pending.size() == 1 && scan.notes.empty());
    CHECK(scan.pending.size() == 1 && scan.pending[0].from_guest && scan.pending[0].slot == 2 &&
          scan.pending[0].generation.number == 2 && scan.pending[0].save_sha256 == sha(save_of("at the end")));
    KeepResult r = keep_progress(w.at, [] { return false; });
    CHECK(r.ok && r.kept.size() == 1 && slot_is(w.real(2), "at the end") && slot_is(w.real(0), "another character"));
    CHECK(slot_is(r.backup / "save" / kAccount / "slot2", "mine") && count_dirs(w.at.staging) == 0);
    // The record the mod reads at the next join, so that the host sends nothing older.
    const auto applied = parse_applied(get(host / "applied.txt"));
    CHECK(applied && applied->generation == 2 && applied->root == sha(save_of("mine")) &&
          applied->save == sha(save_of("at the end")));
    CHECK(scan_saves(w.at).pending.empty());

    // The next session continues that save; its newest generation arrived damaged, so the one before it is kept.
    put_generation(host, generation(3, 2, now - 30, "second session", "mine", "at the end"), "second session");
    put_generation(host, generation(4, 2, now - 10, "cut on the way", "mine", "at the end"), "something else");
    scan = scan_saves(w.at);
    CHECK(scan.pending.size() == 1 && scan.pending[0].generation.number == 3);
    r = keep_progress(w.at, [] { return false; });
    CHECK(r.ok && slot_is(w.real(2), "second session"));
    // A generation without its manifest did not arrive whole.
    put(host / "incoming" / "gen5" / "save.save", save_of("no manifest"));
    CHECK(scan_saves(w.at).pending.empty());

    // The player went on alone in that slot: what a host sends back for the old save never replaces it.
    put_slot(w.real(2), "alone since", 1);
    put_generation(host, generation(6, 2, now - 20, "late", "mine", "second session"), "late");
    scan = scan_saves(w.at);
    CHECK(scan.pending.empty() && scan.notes.empty());
    CHECK(keep_progress(w.at, [] { return false; }).ok && slot_is(w.real(2), "alone since"));

    // A slot with no save takes the generation; a second host's generations for another slot are kept as well.
    fs::remove_all(w.real(2));
    const fs::path host2 = w.at.progress / std::to_string(kFriend2);
    put_generation(host2, generation(1, 0, now, "with another friend", "another character", "another character"),
                   "with another friend");
    scan = scan_saves(w.at);
    CHECK(scan.pending.size() == 2 && scan.pending[0].slot == 0 && scan.pending[1].slot == 2);
    r = keep_progress(w.at, [] { return false; });
    CHECK(r.ok && r.kept.size() == 2 && slot_is(w.real(2), "late") && slot_is(w.real(0), "with another friend"));
    CHECK(parse_applied(get(host2 / "applied.txt")).has_value());

    // The game's own interrupted save next to the slot: left to the game, and said.
    put_generation(host, generation(7, 2, now - 5, "latest", "mine", "late"), "latest");
    fs::create_directories(w.at.save / kAccount / "slot2_backup");
    scan = scan_saves(w.at);
    CHECK(scan.pending.empty() && scan.notes.size() == 1);
    fs::remove(w.at.save / kAccount / "slot2_backup");
    // Two accounts' saves on one PC: a generation does not say whose it is.
    put_slot(w.at.save / "777" / "slot0", "somebody else", 50);
    scan = scan_saves(w.at);
    CHECK(scan.pending.empty() && scan.notes.size() == 1);
    fs::remove_all(w.at.save / "777");
    CHECK(scan_saves(w.at).pending.size() == 1);

    // A session as a guest, then one as the host that saved to the same slot: the later one is what is kept.
    start_session(w, journal_save(2), true, 0);
    put_slot(w.boxed(2), "hosted afterwards", 0);
    scan = scan_saves(w.at);
    CHECK(scan.pending.size() == 1 && !scan.pending[0].from_guest);
    r = keep_progress(w.at, [] { return false; });
    CHECK(r.ok && slot_is(w.real(2), "hosted afterwards") && scan_saves(w.at).pending.empty());
}

// cdmp_tests_launcher --fetch <https address>: downloads it with the launcher's own code and prints its size and
// SHA-256. Not part of the test run (it needs the network); for trying a release address by hand.
int fetch_by_hand(const char* url) {
    const Download d = http_get(url, size_t{128} << 20, {});
    if (!d.ok) {
        std::printf("failed: %s\n", d.error.c_str());
        return 1;
    }
    std::printf("%zu bytes, sha256 %s\n", d.body.size(), cdmp::sha256::hex(cdmp::sha256::of(d.body)).c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--fetch") return fetch_by_hand(argv[2]);

    // Only this folder, made here, is removed at the end.
    wchar_t temp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, temp);
    const fs::path tmp = fs::path(temp) / (L"cdmp_launcher_tests_" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp);

    test_vdf();
    test_contacts();
    test_steamid();
    test_pe();
    test_packages();
    test_locations();
    test_ini();
    test_names();
    test_mod_ini(tmp);
    test_plan();
    test_settings();
    test_game_folder(tmp);
    test_install_and_remove(tmp);
    test_other_peoples_files(tmp);
    test_bad_packages(tmp);
    test_update_address(tmp);
    test_update_button(tmp);
    test_save_rules();
    test_backups(tmp);
    test_keep_host(tmp);
    test_keep_guest(tmp);

    fs::remove_all(tmp, ec);
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("launcher: ok\n");
    return 0;
}
