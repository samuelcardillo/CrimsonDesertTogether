// cr_replay.cpp: the real ActorLoadDataAck / RemoveActorAck / MoveFieldAck packets of a guest capture (cr_extract.py)
// through create_retry's logic (src/mod/session2/create_retry_logic.h), handled as create_retry.cpp does, against a
// model of the client: an announced actor is in the table `delay` ms after its record, until it is removed.
//   cr_replay stream delay_ms [on] [fail=K,K..] [lost=K,K..] [budget=N]
// fail: these keys' first creation fails (a failure with no readable key is reported 300 ms after the record; the
// actor never appears from the host's record). lost: these never appear and nothing is reported. on: a record the
// logic gives again creates its actor (delay ms later); without it (probe) nothing is given.
// It prints what the logic kept and gave again, and how long it took. What it cannot say: the model creates everything
// else, so nothing about what the real client fails to create.
// Build (x64 Native Tools prompt of VS 2022, from the repository's root; add /fsanitize=address /Zi for the memory
// check):  cl /nologo /std:c++20 /EHsc /O2 /W4 /Isrc tools\portingoat\cr_replay.cpp /Fe:build\cr_replay.exe
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "mod/session2/create_retry_logic.h"

using namespace cdmp::session2;

namespace {
struct Clock {
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    double us() const {
        return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    }
};
std::unordered_set<uint32_t> keys_of(const char* s) {
    std::unordered_set<uint32_t> out;
    while (s && *s) {
        out.insert(static_cast<uint32_t>(std::strtoul(s, nullptr, 16)));
        s = std::strchr(s, ',');
        if (s) ++s;
    }
    return out;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const uint64_t delay = std::strtoull(argv[2], nullptr, 10);
    bool on = false;
    std::unordered_set<uint32_t> fail, lost;
    uint32_t budget = 0;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "on") on = true;
        else if (a.rfind("fail=", 0) == 0) fail = keys_of(a.c_str() + 5);
        else if (a.rfind("lost=", 0) == 0) lost = keys_of(a.c_str() + 5);
        else if (a.rfind("budget=", 0) == 0) budget = static_cast<uint32_t>(std::strtoul(a.c_str() + 7, nullptr, 10));
    }
    std::FILE* f = nullptr;
    if (fopen_s(&f, argv[1], "rb") || !f) return 3;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> all(static_cast<size_t>(size));
    if (std::fread(all.data(), 1, all.size(), f) != all.size()) return 4;
    std::fclose(f);

    constexpr uint64_t kBase = 10'000'000, kStepMs = 250;
    std::unordered_map<uint32_t, uint64_t> table;  // key -> in the client's table from this time on
    std::unordered_set<uint32_t> failed_once;      // fail= keys whose first record already went by
    std::vector<std::pair<uint64_t, uint32_t>> reports;  // (time, key 0) failures still to report
    crl::Store store;
    crl::Stats st;
    store.set_lost_budget(budget);
    uint64_t now = kBase, last_step = 0;
    const auto in_table = [&](uint32_t k) {
        const auto it = table.find(k);
        return it != table.end() && it->second <= now;
    };
    double in_total = 0, in_max = 0, step_total = 0, step_max = 0;
    size_t packets = 0, records = 0, with_parent = 0, max_kept = 0, max_bytes = 0, steps = 0, lines = 0, emits = 0;
    size_t max_records = 0, max_payload = 0;
    const auto step = [&](uint64_t t) {
        now = t;
        std::vector<uint32_t> failed;
        for (size_t i = 0; i < reports.size();) {
            if (reports[i].first > now) {
                ++i;
                continue;
            }
            failed.push_back(reports[i].second);
            reports.erase(reports.begin() + static_cast<std::ptrdiff_t>(i));
        }
        const Clock c;
        store.step(now, failed, in_table, in_table,
                   [&](const crl::Kept& e) {
                       ++emits;
                       if (lines < 40) {
                           ++lines;
                           std::printf("  t=%8.3f emit 0x%08X parent 0x%08X %s%s%s attempt %u, %llu ms after its record\n",
                                       (now - kBase) / 1000.0, e.key, e.parent,
                                       e.state == crl::State::Part ? "part" : "failed", e.guessed ? " guessed" : "",
                                       e.lost_retry ? " lost" : "", e.attempts,
                                       static_cast<unsigned long long>(now - e.first_ms));
                       }
                       if (!on) return;
                       const auto payload = crl::single_record_payload(e.rec);
                       // The module's own packet passes create_retry_on_in again (deliver_body).
                       if (store.size())
                           fam::scan_2005(payload, [&](const fam::Record& r) { store.record_seen(r.key, now); });
                       table[e.key] = now + delay;  // the retry creates it (a fail= or lost= one too)
                   },
                   st);
        const double us = c.us();
        step_total += us;
        step_max = std::max(step_max, us);
        ++steps;
        max_kept = std::max(max_kept, store.size());
        max_bytes = std::max(max_bytes, store.bytes());
    };

    size_t pos = 0;
    while (pos + 16 <= all.size()) {
        uint64_t t_us;
        uint16_t op;
        uint32_t len;
        std::memcpy(&t_us, &all[pos], 8);
        std::memcpy(&op, &all[pos + 8], 2);
        std::memcpy(&len, &all[pos + 12], 4);
        pos += 16;
        if (pos + len > all.size()) break;
        const std::span<const uint8_t> payload(&all[pos], len);
        pos += len;
        const uint64_t t = kBase + t_us / 1000;
        while (last_step + kStepMs <= t) {
            last_step = last_step ? last_step + kStepMs : t;
            step(last_step);
        }
        now = t;
        ++packets;
        max_payload = std::max<size_t>(max_payload, len);
        const Clock c;
        // create_retry_on_in
        if (op == fam::kOpMoveField) {
            store.clear();
        } else if (op == fam::kOpRemove) {
            for (size_t i = 6; i + 4 <= payload.size(); i += 4) (void)store.drop(fam::le32(&payload[i]));
        } else if (op == fam::kOpAnnounce) {
            if (store.size()) fam::scan_2005(payload, [&](const fam::Record& r) { store.record_seen(r.key, now); });
            crl::scan_orphans(payload, [&](uint32_t parent) { return in_table(parent); },
                              [&](uint32_t parent) { return store.find(parent) != nullptr; },
                              [&](uint32_t key, uint32_t parent, std::span<const uint8_t> rec) {
                                  store.keep(key, parent, rec, now, st);
                              });
        }
        const double us = c.us();
        in_total += us;
        in_max = std::max(in_max, us);
        max_kept = std::max(max_kept, store.size());
        max_bytes = std::max(max_bytes, store.bytes());
        // The client.
        if (op == fam::kOpMoveField) {
            table.clear();
        } else if (op == fam::kOpRemove) {
            for (size_t i = 6; i + 4 <= payload.size(); i += 4) table.erase(fam::le32(&payload[i]));
        } else if (op == fam::kOpAnnounce) {
            const size_t n = fam::scan_2005(payload, [&](const fam::Record& r) {
                if (crl::attach_parent(r)) ++with_parent;
                if (lost.count(r.key)) return;
                if (fail.count(r.key) && failed_once.insert(r.key).second) {
                    reports.emplace_back(now + 300, 0u);
                    return;
                }
                if (fail.count(r.key)) return;  // only a record this module gives again creates it
                if (!table.count(r.key)) table[r.key] = now + delay;
            });
            records += n;
            max_records = std::max(max_records, n);
        }
    }
    std::printf("%s delay %llu ms%s: %zu packets, %zu records (%zu with an attach parent), largest payload %zu bytes / "
                "%zu records\n",
                argv[1], static_cast<unsigned long long>(delay), on ? " ON" : " probe", packets, records, with_parent,
                max_payload, max_records);
    std::printf("  on_in: total %.1f ms, worst packet %.0f us | step: %zu steps, total %.1f ms, worst %.0f us\n",
                in_total / 1000, in_max, steps, step_total / 1000, step_max);
    std::printf("  kept at most %zu records / %zu bytes; at the end %zu / %zu, credits %zu\n", max_kept, max_bytes,
                store.size(), store.bytes(), store.credits());
    std::printf("  kept %llu replaced %llu evicted %llu expired %llu removed(created) %llu | failures %llu (not kept %llu) "
                "named_alive %llu credits %llu | delivered %llu created_after %llu gave_up %llu parts %llu lost %llu "
                "guessed %llu lost_retried %llu | emits %zu\n",
                (unsigned long long)st.kept, (unsigned long long)st.replaced, (unsigned long long)st.evicted,
                (unsigned long long)st.expired, (unsigned long long)st.removed, (unsigned long long)st.failures,
                (unsigned long long)st.failures_not_kept, (unsigned long long)st.named_alive,
                (unsigned long long)st.credits, (unsigned long long)st.delivered, (unsigned long long)st.created_after,
                (unsigned long long)st.gave_up, (unsigned long long)st.parts, (unsigned long long)st.lost,
                (unsigned long long)st.guessed, (unsigned long long)st.lost_retried, emits);
    return 0;
}
