// The host's sea on the guest (src/mod/session2/sea_sync_logic.h): the state-17 frame, the game's records and the
// guest's copy carried forward in time. Values of the live session of 2026-11-16 (host aground at (-10742, -6275)):
// the longest train's phase grew 0.176 rad/s and the shortest 2.27 rad/s; the host's trains had direction 15.1 and the
// guest's own 39.5.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "mod/session2/sea_sync_logic.h"

using namespace cdmp::session2;

namespace {
int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++g_failures; } } while (0)

bool close_to(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

// The twelve trains t seconds after a start, each at its own speed (0.176 .. 2.27 rad/s).
double speed(size_t i) { return 0.176 + (2.27 - 0.176) * static_cast<double>(i) / 11.0; }
ssl::Sample host_at(uint32_t seq, double t_s, uint64_t clock0_us = 5000000000ull) {
    ssl::Sample s;
    s.seq = seq;
    s.host_us = clock0_us + static_cast<uint64_t>(t_s * 1.0e6);
    s.wind = 0.0334f;
    for (size_t i = 0; i < ssl::kBands; ++i) {
        s.band[i] = ssl::Band{static_cast<float>(10.0 + speed(i) * t_s), 1.0f, 15.1f, -10741.0f, -6273.8f};
    }
    return s;
}

void test_wire_round_trip() {
    const ssl::Sample s = host_at(7, 3.25);
    const auto bytes = ssl::frame(s);
    CHECK(bytes.size() == ssl::kHeader + ssl::kPayloadSize);
    CHECK(bytes.size() == 265);
    CHECK(ssl::is_frame(bytes));
    const auto back = ssl::decode(bytes);
    CHECK(back.has_value());
    CHECK(back && *back == s);
}

void test_wire_refuses_others() {
    const auto bytes = ssl::frame(host_at(1, 0.0));
    auto other_state = bytes;
    other_state[ssl::kHeader] = 16;
    CHECK(!ssl::is_frame(other_state));
    auto other_version = bytes;
    other_version[ssl::kHeader + 1] = 2;
    CHECK(ssl::is_frame(other_version));  // ours: taken out of the stream
    CHECK(!ssl::decode(other_version).has_value());
    auto short_one = bytes;
    short_one.pop_back();
    CHECK(!ssl::is_frame(short_one));
    // Values that are not a sea are never written: a strength of 3, a phase that is not a number, a wind of 2.
    ssl::Sample bad = host_at(1, 0.0);
    bad.band[4].fade = 3.0f;
    CHECK(!ssl::decode(ssl::frame(bad)).has_value());
    bad = host_at(1, 0.0);
    bad.band[0].phase = std::nanf("");
    CHECK(!ssl::decode(ssl::frame(bad)).has_value());
    bad = host_at(1, 0.0);
    bad.wind = 2.0f;
    CHECK(!ssl::decode(ssl::frame(bad)).has_value());
}

void test_entries() {
    // One frame of the game's history: only five words of each record are the train.
    std::vector<uint8_t> raw(ssl::kFrameSize, 0xEE);
    const ssl::Sample s = host_at(1, 2.0);
    ssl::to_entries(s.band, raw.data());
    std::array<ssl::Band, ssl::kBands> back;
    ssl::from_entries(raw.data(), back);
    CHECK(back == s.band);
    for (size_t i = 0; i < ssl::kBands; ++i) {
        const uint8_t* e = raw.data() + i * ssl::kEntrySize;
        CHECK(e[0x0C] == 0xEE && e[0x0F] == 0xEE);  // the fourth word stays the game's
        CHECK(e[0x18] == 0xEE && e[0x1F] == 0xEE);  // and the last two
    }
}

void test_mirror_needs_a_moving_sea() {
    ssl::Mirror m;
    std::array<ssl::Band, ssl::kBands> out;
    float wind = 0;
    const uint64_t g0 = 900000000ull;  // the guest's clock when the first sample arrives
    CHECK(!m.at(g0, out, wind));       // nothing yet
    CHECK(m.on_sample(host_at(1, 0.0), g0));
    CHECK(!m.at(g0 + 1000, out, wind));  // one sample says nothing about how the sea moves
    CHECK(m.on_sample(host_at(2, 0.1), g0 + 100000));
    CHECK(m.moving());
    CHECK(m.at(g0 + 100000, out, wind));
    CHECK(close_to(wind, 0.0334, 1e-6));
    // The host paused: the same values again and again. After three, this client's own sea goes on.
    ssl::Sample still = host_at(2, 0.1);
    for (uint32_t k = 0; k < 3; ++k) {
        still.seq = 3 + k;
        still.host_us += 100000;
        CHECK(m.on_sample(still, g0 + 200000 + k * 100000ull));
    }
    CHECK(!m.moving());
    CHECK(!m.at(g0 + 400000, out, wind));
    // It moves again: written again.
    ssl::Sample again = host_at(6, 0.2);
    again.host_us = still.host_us + 100000;
    CHECK(m.on_sample(again, g0 + 500000));
    CHECK(m.moving());
    CHECK(m.at(g0 + 500000, out, wind));
}

void test_mirror_carries_forward() {
    ssl::Mirror m;
    std::array<ssl::Band, ssl::kBands> out;
    float wind = 0;
    const uint64_t g0 = 900000000ull;
    // Samples every 100 ms, each arriving 30 ms after the host took it (the clocks themselves are far apart).
    for (uint32_t k = 0; k <= 20; ++k) CHECK(m.on_sample(host_at(k + 1, 0.1 * k), g0 + k * 100000ull + 30000));
    // 60 ms after the last sample arrived: the host's sea is 90 ms past it, on the host's clock 2.09 s.
    CHECK(m.at(g0 + 2000000 + 30000 + 60000, out, wind));
    for (size_t i = 0; i < ssl::kBands; ++i) {
        const double want = 10.0 + speed(i) * 2.06;  // the latency (30 ms) is not known to the guest: 2.06, not 2.09
        CHECK(close_to(out[i].phase, want, 0.002));
        CHECK(close_to(out[i].phase, 10.0 + speed(i) * 2.09, speed(i) * 0.03 + 0.002));
        CHECK(out[i].dir == 15.1f && out[i].ox == -10741.0f && out[i].oz == -6273.8f && out[i].fade == 1.0f);
    }
    CHECK(close_to(m.rate(0) * 1.0e6, 0.176, 0.002));
    CHECK(close_to(m.rate(11) * 1.0e6, 2.27, 0.01));
    // No sample for more than 1.5 s: nothing is written any more.
    CHECK(m.at(g0 + 2000000 + 30000 + 1400000, out, wind));
    CHECK(!m.at(g0 + 2000000 + 30000 + 1600000, out, wind));
}

// The live run of 2026-11-16: the guest's copy was behind, its network loop took 0.6 s a pass and the samples came
// in bursts that late. The sea must not be drawn that far behind the host's for it.
void test_mirror_late_bursts() {
    ssl::Mirror m;
    std::array<ssl::Band, ssl::kBands> out;
    float wind = 0;
    const uint64_t g0 = 900000000ull;
    // Ten samples that arrive 20 ms after they were taken, then the loop gets slow: six at a time, the newest of each
    // burst 20 ms old and the oldest 520 ms old.
    for (uint32_t k = 0; k <= 10; ++k) CHECK(m.on_sample(host_at(k + 1, 0.1 * k), g0 + k * 100000ull + 20000));
    for (uint32_t burst = 0; burst < 5; ++burst) {
        const uint64_t arrives = g0 + (16 + burst * 6) * 100000ull + 20000;
        for (uint32_t j = 0; j < 6; ++j) {
            const uint32_t k = 11 + burst * 6 + j;
            CHECK(m.on_sample(host_at(k + 1, 0.1 * k), arrives));
        }
    }
    // The clocks' difference is still the quickest delivery (20 ms), give or take the creep (30 samples x 0.2 ms).
    const double clocks = 900000000.0 - 5000000000.0;
    CHECK(close_to(m.offset_us() - clocks, 20000.0, 6500.0));
    // 300 ms after the last burst: the host's sea is its last sample (4.0 s) carried 300 ms, not one drawn late.
    const uint64_t now = g0 + 40 * 100000ull + 20000 + 300000;
    CHECK(m.at(now, out, wind));
    for (size_t i = 0; i < ssl::kBands; ++i) CHECK(close_to(out[i].phase, 10.0 + speed(i) * 4.3, speed(i) * 0.007 + 0.002));
}

void test_mirror_restart_of_a_train() {
    ssl::Mirror m;
    std::array<ssl::Band, ssl::kBands> out;
    float wind = 0;
    const uint64_t g0 = 900000000ull;
    for (uint32_t k = 0; k <= 10; ++k) CHECK(m.on_sample(host_at(k + 1, 0.1 * k), g0 + k * 100000ull));
    // Train 3 fades out (strength 0.4 -> 0.2 in 100 ms), is started again (phase 0, new direction and origin), fades in.
    ssl::Sample a = host_at(12, 1.1), b = host_at(13, 1.2), c = host_at(14, 1.3);
    a.band[3].fade = 0.4f;
    b.band[3].fade = 0.2f;
    c.band[3] = ssl::Band{0.02f, 0.05f, 13.6f, -10500.0f, -6100.0f};
    CHECK(m.on_sample(a, g0 + 1100000));
    CHECK(m.on_sample(b, g0 + 1200000));
    CHECK(m.at(g0 + 1250000, out, wind));
    CHECK(close_to(out[3].fade, 0.1, 0.01));  // still going down between the samples
    CHECK(m.at(g0 + 1299000, out, wind));
    CHECK(out[3].fade >= 0.0f);           // and never below nothing
    CHECK(m.on_sample(c, g0 + 1300000));
    CHECK(m.at(g0 + 1350000, out, wind));
    CHECK(out[3].dir == 13.6f && out[3].ox == -10500.0f && out[3].oz == -6100.0f);
    CHECK(close_to(out[3].fade, 0.05, 1e-6));  // held: the sample after it says how fast it comes back
    // Its phase starts from the new value and keeps the speed it had (the phase went back, so no speed from it).
    const double r3 = m.rate(3);
    CHECK(close_to(r3 * 1.0e6, speed(3), 0.01));
    CHECK(close_to(out[3].phase, 0.02 + speed(3) * 0.05, 0.002));
    // The others are untouched by it.
    CHECK(close_to(out[0].phase, 10.0 + speed(0) * 1.35, 0.002));
}

void test_mirror_order_and_restart_of_the_host() {
    ssl::Mirror m;
    const uint64_t g0 = 900000000ull;
    CHECK(m.on_sample(host_at(10, 1.0), g0));
    CHECK(m.on_sample(host_at(11, 1.1), g0 + 100000));
    CHECK(!m.on_sample(host_at(11, 1.1), g0 + 110000));  // the same again
    CHECK(!m.on_sample(host_at(9, 0.9), g0 + 120000));   // one that arrived late
    CHECK(m.last().seq == 11);
    // A host that began again counts from 1, on a clock that may be anything.
    CHECK(m.on_sample(host_at(11 + 200000, 5.0), g0 + 200000));
    CHECK(m.on_sample(host_at(1, 0.0, 77000000ull), g0 + 300000));
    CHECK(m.last().seq == 1);
    CHECK(!m.moving());  // and it needs a second sample again
}

void test_apart() {
    const ssl::Sample a = host_at(1, 0.0);
    ssl::Sample b = a;
    b.band[5].phase += 6.2831853f * 3 + 0.5f;  // whole turns do not count
    b.band[7].dir = 39.5f;
    b.band[2].ox += 30.0f;
    b.band[2].oz += 40.0f;
    const ssl::Apart d = ssl::apart(a.band, b.band);
    CHECK(close_to(d.phase, 0.5, 0.001));
    CHECK(close_to(d.dir, 24.4, 0.001));
    CHECK(close_to(d.origin, 50.0, 0.01));
}
}  // namespace

int main() {
    test_wire_round_trip();
    test_wire_refuses_others();
    test_entries();
    test_mirror_needs_a_moving_sea();
    test_mirror_carries_forward();
    test_mirror_late_bursts();
    test_mirror_restart_of_a_train();
    test_mirror_order_and_restart_of_the_host();
    test_apart();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("optd_sea_sync: ok\n");
    return 0;
}
