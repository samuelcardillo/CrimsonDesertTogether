// vehicle_follow (src/mod/session2/vehicle_follow_logic.h): the smoothing of the controller's ~1 Hz ship samples and
// the sector form of the TransformSync position argument. Values from the 2026-11-13 test (host's ship at Vellua).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "mod/session2/vehicle_follow_logic.h"

using namespace cdmp::session2::vfl;

namespace {
int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++g_failures; } } while (0)

Pose pose(double x, double y, double z, float qy = -0.166f) {
    Pose p;
    p.x = x;
    p.y = y;
    p.z = z;
    p.q[0] = 0;
    p.q[1] = qy;
    p.q[2] = 0;
    p.q[3] = std::sqrt(1.0f - qy * qy);
    return p;
}
bool near(double a, double b, double eps = 1e-3) { return std::fabs(a - b) < eps; }

void test_usable() {
    CHECK(usable(pose(-10384.7, 350.6, -6363.0)));
    CHECK(!usable(pose(0, 350, 0)));  // a vehicle that never got a network pose
    Pose bad = pose(-10384.7, 350.6, -6363.0);
    bad.q[3] = 0.5f;
    CHECK(!usable(bad));
    bad = pose(NAN, 350.6, -6363.0);
    CHECK(!usable(bad));
}

void test_track_empty_and_single() {
    Track t;
    Pose out;
    CHECK(!t.at(5000, &out));
    CHECK(t.push(1000, pose(-10384.7, 350.6, -6363.0)));
    CHECK(!t.push(1500, pose(-10384.7, 350.6, -6363.0)));  // unchanged: not a new sample
    CHECK(t.size() == 1);
    CHECK(t.at(100000, &out) && near(out.x, -10384.7) && near(out.z, -6363.0));
}

void test_track_interpolates_behind() {
    Track t(1200);
    // The host sailing south at 6 m/s, one sample a second.
    for (int i = 0; i < 5; ++i) t.push(10000 + i * 1000, pose(-10380.0, 350.0, -6300.0 - 6.0 * i));
    Pose out;
    // Drawn 1.2 s behind: at 13700 local -> 12500 -> halfway between samples 2 and 3.
    CHECK(t.at(13700, &out));
    CHECK(near(out.z, -6315.0, 1e-6));
    // Before the first sample: the first.
    CHECK(t.at(10500, &out) && near(out.z, -6300.0));
    // Just past the newest (14000 + 1200 + 300): extrapolated 300 ms at 6 m/s.
    CHECK(t.at(15500, &out) && near(out.z, -6325.8, 1e-6));
    // Long past: held at the newest.
    CHECK(t.at(20000, &out) && near(out.z, -6324.0));
}

void test_track_default_delay_extrapolates() {
    Track t;  // 300 ms: about the delay the controller's character is drawn with
    CHECK(t.delay() == 300);
    for (int i = 0; i < 5; ++i) t.push(10000 + i * 1000, pose(-10380.0, 350.0, -6300.0 - 6.0 * i));
    Pose out;
    // 14100 local -> 13800: between the samples of 13000 and 14000.
    CHECK(t.at(14100, &out) && near(out.z, -6322.8, 1e-6));
    // 14500 local -> 14200: 200 ms past the newest, along its velocity (6 m/s).
    CHECK(t.at(14500, &out) && near(out.z, -6325.2, 1e-6));
    // More than a second past the newest (a lost sample, or none sent): held at the newest.
    CHECK(t.at(15400, &out) && near(out.z, -6324.0));
    // The switch to the next sample is continuous when the velocity holds.
    Pose before;
    CHECK(t.at(14800, &before));  // extrapolated to 14500
    t.push(14800, pose(-10380.0, 350.0, -6328.8));  // the same 6 m/s
    CHECK(t.at(14800, &out) && near(out.z, before.z, 1e-3));
    t.set_delay(1200);
    CHECK(t.delay() == 1200);
}

void test_track_teleport_not_interpolated() {
    Track t;
    t.push(1000, pose(-10124.4, 349.7, -7167.7));
    t.push(2000, pose(-10384.7, 350.6, -6363.0));  // 846 m in one step
    Pose out;
    CHECK(t.at(2700, &out) && near(out.z, -6363.0));
}

void test_track_ring_and_clock() {
    Track t;
    for (int i = 0; i < 20; ++i) t.push(1000 + i * 1000, pose(-10000.0 - i, 350.0, -6000.0));
    CHECK(t.size() == Track::kCap);
    // A sample that arrives with the same local time is moved 1 ms later, never before the newest.
    t.push(20000, pose(-10500.0, 350.0, -6000.0));
    Pose out;
    CHECK(t.at(1000000, &out) && near(out.x, -10500.0));
}

// Samples are stamped at arrival: two ~1 Hz poses that land a frame apart (a resent packet, a hitch) must not be read
// as 812 m/s and extrapolated for a second (a review finding of 14-nov; a loopback test never delivers them so).
void test_track_bursty_samples() {
    Track t;
    t.push(10000, pose(-10380.0, 350.0, -6300.0));
    t.push(11000, pose(-10380.0, 350.0, -6313.0));  // 13 m/s
    t.push(11016, pose(-10380.0, 350.0, -6326.0));  // the next pose, 16 ms later
    Pose out;
    // 11816 local -> 11516: 500 ms past the newest. Velocity over the 1016 ms of samples (25.6 m/s): 12.8 m on.
    CHECK(t.at(11816, &out) && out.z < -6326.0 && out.z > -6326.0 - 13.0);
    // Only two samples, 10 ms apart, 50 m: counted over kVelocityBaseMs (100 m/s) and capped at kMaxSpeedMps.
    Track u;
    u.push(10000, pose(-10380.0, 350.0, -6300.0));
    u.push(10010, pose(-10380.0, 350.0, -6350.0));
    CHECK(u.at(10010 + 300 + 1000, &out) && near(out.z, -6350.0 - Track::kMaxSpeedMps, 1e-6));
    // A teleport inside the base time is not part of the velocity.
    Track w;
    w.push(10000, pose(-10380.0, 350.0, -5000.0));
    w.push(10900, pose(-10380.0, 350.0, -6300.0));  // 1300 m: a teleport
    w.push(11000, pose(-10380.0, 350.0, -6301.0));  // 1 m in 100 ms after it
    CHECK(w.at(11000 + 300 + 500, &out) && out.z > -6303.0 && out.z < -6301.0);
}

void test_rotation() {
    float a[4] = {0, -0.166f, 0, 0.98613f}, b[4] = {0, 0.166f, 0, 0.98613f}, out[4];
    nlerp(a, b, 0.5, out);
    CHECK(near(out[1], 0.0, 1e-4) && near(out[3], 1.0, 1e-4));
    // The shorter arc: -q is the same rotation.
    float c[4] = {0, 0.166f, 0, -0.98613f};
    nlerp(a, c, 0.5, out);
    CHECK(near(out[1], -0.166, 1e-3));
}

void test_to_sector() {
    float l = 0;
    int16_t s = 0;
    to_sector(-10384.7, -10, &l, &s);
    CHECK(s == -10 && near(l, -384.7, 1e-3));
    to_sector(-10384.7, -11, &l, &s);  // still inside the caller's sector
    CHECK(s == -11 && near(l, 615.3, 1e-3));
    to_sector(-6363.0, -7, &l, &s);
    CHECK(s == -7 && near(l, 637.0, 1e-3));
    to_sector(-8200.0, -6, &l, &s);  // far from the hint: truncated toward zero
    CHECK(s == -8 && near(l, -200.0, 1e-3));
}

// Live values, 2026-11-14 02:40 (host copy, 3663): the ship's vehicle and the wheel's mount hanging from it at
// (0, 2.94, 1.851); the mount's world position read from the wheel's parent frame.
void test_attach() {
    Pose ship = pose(-10372.362, 349.629, -6206.802, 0.326f);
    const float mount[3] = {0.0f, 2.94f, 1.851f}, none[4] = {0, 0, 0, 1};
    const Pose m = attach(ship, mount, none);
    CHECK(near(m.x, -10371.22, 0.08) && near(m.y, 352.57, 0.01) && near(m.z, -6205.34, 0.08));
    CHECK(near(m.q[1], 0.326, 1e-4) && near(m.q[3], ship.q[3], 1e-4));
    // the helmsman 1.17 m behind the mount, on the mount
    const float helm[3] = {0.0f, 0.0f, 1.17f};
    const Pose h = attach(m, helm, none);
    CHECK(near(std::hypot(h.x - m.x, h.z - m.z), 1.17, 1e-3) && near(h.y, m.y, 1e-4));
    // a half turn about the vertical on the parent: the wheel's local rotation (0, 1, 0, 0)
    const float half[4] = {0, 1, 0, 0}, origin[3] = {0, 0, 0};
    const Pose w = attach(pose(0.5, 0, 0.5, 0.0f), origin, half);
    CHECK(near(w.q[1], 1.0, 1e-5) && near(w.q[3], 0.0, 1e-5));
    // rotate: a quarter turn about +y takes +z to +x
    const float quarter[4] = {0, 0.70710678f, 0, 0.70710678f}, z1[3] = {0, 0, 1};
    double out[3];
    rotate(quarter, z1, out);
    CHECK(near(out[0], 1.0, 1e-5) && near(out[1], 0.0, 1e-5) && near(out[2], 0.0, 1e-5));
    float qq[4];
    mul(quarter, quarter, qq);  // two quarter turns: a half turn
    CHECK(near(qq[1], 1.0, 1e-5) && near(qq[3], 0.0, 1e-5));
}
void test_riders() {
    Rider a{0xB01008A8, 0xB01008A6, {1.5f, 0.25f, -3.0f}, {0, 0.70710678f, 0, 0.70710678f}};
    Rider b{0xB01008A9, 0xB01008A6, {-2.0f, 0.25f, 4.0f}, {0, 0, 0, 1}};
    CHECK(rider_ok(a) && rider_ok(b));
    // Not a rider: a player's key, an anchor that is no gimmick, itself as its anchor, a far pose, no rotation.
    Rider bad = a;
    bad.key = 0xA0100001;
    CHECK(!rider_ok(bad));
    bad = a;
    bad.anchor = 0;
    CHECK(!rider_ok(bad));
    bad = a;
    bad.anchor = a.key;
    CHECK(!rider_ok(bad));
    bad = a;
    bad.pos[2] = 501.0f;
    CHECK(!rider_ok(bad));
    bad = a;
    bad.pos[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!rider_ok(bad));
    bad = a;
    bad.q[3] = 0.0f;
    bad.q[1] = 0.0f;
    CHECK(!rider_ok(bad));

    const Rider in[] = {a, bad, b};
    const auto wire = encode_riders(in);
    CHECK(wire.size() == 1 + 2 * kRiderWire && wire[0] == 2);  // the bad one is left out
    Rider out[kMaxRiders];
    CHECK(decode_riders(wire, out) == 2);
    CHECK(out[0].key == a.key && out[0].anchor == a.anchor && out[0].pos[2] == -3.0f && out[0].q[1] == a.q[1]);
    CHECK(out[1].key == b.key && out[1].pos[0] == -2.0f && out[1].q[3] == 1.0f);

    // None: one byte. More than the cap: only the first kMaxRiders go.
    CHECK(encode_riders({}).size() == 1 && decode_riders(encode_riders({}), out) == 0);
    std::vector<Rider> many;
    for (uint32_t i = 0; i < kMaxRiders + 5; ++i) {
        Rider r = a;
        r.key = 0xB0100900 + i;
        many.push_back(r);
    }
    const auto capped = encode_riders(many);
    CHECK(capped[0] == kMaxRiders && capped.size() == 1 + kMaxRiders * kRiderWire);
    CHECK(decode_riders(capped, out) == kMaxRiders && out[kMaxRiders - 1].key == 0xB0100900 + kMaxRiders - 1);

    // A payload of the wrong size, a count over the cap, an empty one: nothing. A bad or repeated rider: skipped.
    auto cut = wire;
    cut.pop_back();
    CHECK(decode_riders(cut, out) == 0);
    auto longer = wire;
    longer.push_back(0);
    CHECK(decode_riders(longer, out) == 0);
    std::vector<uint8_t> over(1 + (kMaxRiders + 1) * kRiderWire, 0);
    over[0] = static_cast<uint8_t>(kMaxRiders + 1);
    CHECK(decode_riders(over, out) == 0);
    CHECK(decode_riders({}, out) == 0);
    auto twice = wire;
    std::memcpy(&twice[1 + kRiderWire], &twice[1], kRiderWire);  // the second is the first again
    CHECK(decode_riders(twice, out) == 1 && out[0].key == a.key);
    auto zeroed = wire;
    std::memset(&zeroed[1 + 20], 0, 16);  // the first one's rotation is all zeros
    CHECK(decode_riders(zeroed, out) == 1 && out[0].key == b.key);
}
void test_vehicle_poses() {
    VehiclePose a{0xB010000A, {-10372.5f, 350.1f, -6207.25f}, {0, 0.70710678f, 0, 0.70710678f}};
    VehiclePose b{0xB010157F, {-9991.17f, 353.17f, -6667.79f}, {0, 0, 0, 1}};
    CHECK(vehicle_pose_ok(a) && vehicle_pose_ok(b));
    VehiclePose bad = a;
    bad.key = 0xA0100001;  // a character
    CHECK(!vehicle_pose_ok(bad));
    bad = a;
    bad.pos[0] = bad.pos[2] = 0;  // never had a pose
    CHECK(!vehicle_pose_ok(bad));
    bad = a;
    bad.pos[1] = std::numeric_limits<float>::infinity();
    CHECK(!vehicle_pose_ok(bad));
    bad = a;
    bad.q[1] = bad.q[3] = 0;
    CHECK(!vehicle_pose_ok(bad));

    const VehiclePose in[] = {a, bad, b};
    const auto wire = encode_vehicles(in);
    CHECK(wire.size() == 1 + 2 * kVehicleWire && wire[0] == 2);
    VehiclePose out[kMaxVehiclePoses];
    CHECK(decode_vehicles(wire, out) == 2);
    CHECK(out[0].key == a.key && out[0].pos[0] == a.pos[0] && out[0].pos[2] == a.pos[2] && out[0].q[1] == a.q[1]);
    CHECK(out[1].key == b.key && out[1].pos[1] == b.pos[1] && out[1].q[3] == 1.0f);
    // The position survives the float32 on the wire to the millimetre at this world's size.
    CHECK(std::fabs(static_cast<double>(out[0].pos[0]) - (-10372.5)) < 0.002);

    CHECK(encode_vehicles({}).size() == 1 && decode_vehicles(encode_vehicles({}), out) == 0);
    std::vector<VehiclePose> many;
    for (uint32_t i = 0; i < kMaxVehiclePoses + 3; ++i) {
        VehiclePose v = a;
        v.key = 0xB0100900 + i;
        many.push_back(v);
    }
    const auto capped = encode_vehicles(many);
    CHECK(capped[0] == kMaxVehiclePoses && decode_vehicles(capped, out) == kMaxVehiclePoses);
    auto cut = wire;
    cut.pop_back();
    CHECK(decode_vehicles(cut, out) == 0);
    std::vector<uint8_t> over(1 + (kMaxVehiclePoses + 1) * kVehicleWire, 0);
    over[0] = static_cast<uint8_t>(kMaxVehiclePoses + 1);
    CHECK(decode_vehicles(over, out) == 0);
    auto twice = wire;
    std::memcpy(&twice[1 + kVehicleWire], &twice[1], kVehicleWire);
    CHECK(decode_vehicles(twice, out) == 1 && out[0].key == a.key);

    // What they are for: a Track fed ten times a second is always between two samples, never guessing.
    Track dense(300);
    for (int i = 0; i <= 20; ++i) dense.push(1000 + i * 100, pose(15.0 * i * 0.1, 0, 5));  // 15 m/s along x
    Pose at;
    CHECK(dense.at(3000, &at));  // drawn 300 ms behind: t = 2700, the ship was at 25.5 m then
    CHECK(near(at.x, 25.5, 0.01));
    // With one sample a second and a 1.7 s gap the same Track stops at its newest sample after 1 s of guessing.
    Track sparse(300);
    sparse.push(1000, pose(0, 0, 5));
    sparse.push(2000, pose(15, 0, 5));
    CHECK(sparse.at(3299, &at) && near(at.x, 15 + 15 * 0.999, 0.05));  // still extrapolating
    CHECK(sparse.at(3400, &at) && near(at.x, 15.0, 1e-9));              // 1.1 s past the newest: back on it
}

// How unevenly a Track draws a ship that really goes at a steady 10 m/s: the largest departure of the drawn speed,
// taken over a quarter of a second, from the real one (0 = perfectly even). The sender's game moves the ship once
// every frame_ms; its mod sends the newest pose every 100 ms when it changed, with how long ago it appeared; each
// event takes 20 ms +-jitter_ms to arrive; the sender's clock is 700 s ahead of the receiver's. `timed`: the receiver
// places each sample with SampleClock, as vehicle_follow_on_peer_vehicles does for VehiclesTimed; else at its arrival.
double unevenness(double frame_ms, double jitter_ms, bool timed) {
    Track track(300);
    SampleClock clock;
    struct Arrival {
        double at;
        uint64_t sender_ms;
        uint16_t age_ms;
        double x;
    };
    std::vector<Arrival> feed;
    uint32_t seed = 4242;
    double sent_frame = -1;
    for (double tick = 1000; tick < 14000; tick += 100) {
        const double frame = 1000 + std::floor((tick - 1000) / frame_ms) * frame_ms;
        if (frame == sent_frame) continue;
        sent_frame = frame;
        seed = seed * 1664525u + 1013904223u;
        const double transit = std::max(1.0, 20.0 + (((seed >> 8) & 0xFFFF) / 65535.0 * 2 - 1) * jitter_ms);
        feed.push_back({tick + transit, static_cast<uint64_t>(tick) + 700000, static_cast<uint16_t>(tick - frame),
                        10.0 * (frame - 1000) / 1000.0});
    }
    size_t next = 0;
    std::vector<double> xs;
    for (double t = 1000; t < 13000; t += 1000.0 / 60.0) {
        while (next < feed.size() && feed[next].at <= t) {
            const auto now = static_cast<uint64_t>(feed[next].at);
            if (timed) clock.on_event(now, feed[next].sender_ms);
            track.push(timed ? clock.at(now, feed[next].sender_ms, feed[next].age_ms) : now, pose(feed[next].x, 0, 5));
            ++next;
        }
        Pose at;
        if (t >= 5000 && track.at(static_cast<uint64_t>(t), &at)) xs.push_back(at.x);  // the first seconds: the clock settles
    }
    double worst = 0;
    for (size_t i = 15; i < xs.size(); ++i) {
        const double speed = (xs[i] - xs[i - 15]) / 0.25;
        worst = std::max(worst, std::fabs(speed - 10.0) / 10.0);
    }
    return worst;
}

// The receiver's clock for the timed poses: where it puts samples, and what it does when the other clock changes.
void test_sample_clock() {
    // The sender's clock is 123 456 ms behind this one; its events take 20 ms +-30 (never less than 1).
    SampleClock clock;
    uint32_t seed = 99;
    int64_t worst = 0;
    uint64_t last_stamp = 0;
    bool went_back = false;
    for (uint64_t sent = 10000; sent < 40000; sent += 100) {  // the sender's clock
        seed = seed * 1664525u + 1013904223u;
        const auto transit = static_cast<uint64_t>(std::max(1.0, 20.0 + (((seed >> 8) & 0xFFFF) / 65535.0 * 2 - 1) * 30.0));
        const uint64_t now = sent + 123456 + transit;
        clock.on_event(now, sent);
        // A pose the sender saw 40 ms before it sent the event: on this clock that was at sent - 40 + 123456.
        const uint64_t stamp = clock.at(now, sent, 40);
        CHECK(stamp <= now);
        if (sent >= 15000) {  // settled: the fastest transits of the last seconds are 1 to a few ms
            const int64_t off = static_cast<int64_t>(stamp) - static_cast<int64_t>(sent - 40 + 123456);
            worst = std::max<int64_t>(worst, off < 0 ? -off : off);
            went_back |= stamp < last_stamp;
        }
        last_stamp = stamp;
    }
    CHECK(worst <= 12);  // against +-30 ms for a sample stamped at its arrival
    CHECK(!went_back);
    CHECK(clock.resets() == 0);
    // The other game is started again half a second later: its clock starts near zero. The first event already lands
    // at its arrival (never a second back, never ahead), and the samples are placed by the new clock from then on.
    uint64_t now = 39900 + 123456 + 500;
    clock.on_event(now, 2000);
    CHECK(clock.resets() == 1);
    CHECK(clock.at(now, 2000, 0) == now);
    CHECK(clock.at(now, 2000, 60) == now - 60);
    now += 100;
    clock.on_event(now + 7, 2100);  // 7 ms slower than the first: placed 7 ms before its arrival
    CHECK(clock.resets() == 1 && clock.at(now + 7, 2100, 0) == now);
    // The first events of a session arrive late (a hitch) and later ones do not: the offset was too large by 200 ms,
    // an event's send time lands "after" its arrival, and the clock is taken anew instead of crawling there.
    SampleClock late;
    late.on_event(50200, 1000);  // 200 ms in transit
    late.on_event(50300, 1100);
    CHECK(late.resets() == 0);
    late.on_event(50320, 1300);  // 20 ms in transit: its send time would be 179 ms after its arrival
    CHECK(late.resets() == 1 && late.at(50320, 1300, 0) == 50320);
    // No event yet: a sample is placed at its arrival.
    SampleClock none;
    CHECK(none.at(777, 5, 3) == 777);
}

void test_vehicle_poses_timed() {
    VehiclePose a{0xB010000A, {-10372.5f, 350.1f, -6207.25f}, {0, 0.70710678f, 0, 0.70710678f}};
    a.age_ms = 37;
    VehiclePose b{0xB010157F, {-9991.17f, 353.17f, -6667.79f}, {0, 0, 0, 1}};
    b.age_ms = 0xFFFF;  // a pose that has not changed for a minute: the age saturates
    VehiclePose bad = a;
    bad.pos[0] = bad.pos[2] = 0;
    const VehiclePose in[] = {a, bad, b};
    const uint64_t sender = 0x123456789ABCull;  // a clock past 32 bits (a PC that has been on for months)
    const auto wire = encode_vehicles_timed(sender, in);
    CHECK(wire.size() == kVehicleTimedHead + 2 * kVehicleTimedWire && wire[0] == 2);
    VehiclePose out[kMaxVehiclePoses];
    uint64_t got = 0;
    CHECK(decode_vehicles_timed(wire, &got, out) == 2 && got == sender);
    CHECK(out[0].key == a.key && out[0].age_ms == 37 && out[0].pos[0] == a.pos[0] && out[0].q[1] == a.q[1]);
    CHECK(out[1].key == b.key && out[1].age_ms == 0xFFFF && out[1].pos[2] == b.pos[2]);
    // Neither form is taken for the other, at any count (their sizes differ: 1 + 32 n against 9 + 34 n).
    CHECK(decode_vehicles(wire, out) == 0);
    CHECK(decode_vehicles_timed(encode_vehicles(in), &got, out) == 0);
    auto cut = wire;
    cut.pop_back();
    CHECK(decode_vehicles_timed(cut, &got, out) == 0);
    CHECK(decode_vehicles_timed({}, &got, out) == 0);
    CHECK(encode_vehicles_timed(sender, {}).size() == kVehicleTimedHead);
    CHECK(decode_vehicles_timed(encode_vehicles_timed(sender, {}), &got, out) == 0);
    std::vector<VehiclePose> many(kMaxVehiclePoses + 3, a);
    for (size_t i = 0; i < many.size(); ++i) many[i].key = 0xB0100900 + static_cast<uint32_t>(i);
    CHECK(encode_vehicles_timed(sender, many)[0] == kMaxVehiclePoses);

    // A sample's time on the receiver's clock: the sender's clock, less its age, plus the clocks' difference.
    // The sender's clock reads 50 000 when the receiver's reads 80 020 (difference 30 000 plus 20 ms of transit).
    CHECK(sample_time(80020, 50000, 0, 30000) == 80000);
    CHECK(sample_time(80020, 50000, 120, 30000) == 79880);
    CHECK(sample_time(80020, 50000, 0, 30100) == 80020);       // would be in the future: its arrival
    CHECK(sample_time(80020, 50000, 0xFFFF, 30000) == 80020);  // over a second old: its arrival (the Track drops a
                                                               // pose that did not change anyway)
    CHECK(sample_time(80020, 50000, 0, -40000) == 80020);      // an offset of another clock: its arrival

    // What the times are for, through the receiver's own code (SampleClock). A copy of the game running behind
    // another window draws 7 frames a second: stamped on arrival, the ship is seen at up to a third more or less than
    // its speed by turns; with the poses' own times it is steady.
    CHECK(unevenness(143.0, 0.0, false) > 0.25);
    CHECK(unevenness(143.0, 0.0, true) < 0.03);
    // Two PCs: 60 frames a second and a network that takes 20 ms +-30. On arrival the ship surges by a fifth; with
    // the times, a few per cent (what is left is the offset following the fastest transit of the last seconds).
    CHECK(unevenness(16.7, 30.0, false) > 0.15);
    CHECK(unevenness(16.7, 30.0, true) < 0.06);
    // Both at once.
    CHECK(unevenness(143.0, 30.0, true) < 0.06);
}
}  // namespace

// A passenger in the air keeps up with the ship (air_carry, AirRide). Values from the user's sail of 2026-11-15 (the
// guest's log): the guest stood at (-2.52, 3.14, 5.30) in the ship's frame, the ship moved 2.3-2.9 m a frame
// (~200 ms), and once off the part the game carried the character with the ship in some frames and not in others.
Pose yawed(double x, double y, double z, double yaw_deg) {
    Pose p;
    p.x = x;
    p.y = y;
    p.z = z;
    const double h = yaw_deg * 0.008726646259971648;  // half the angle, in radians
    p.q[0] = 0;
    p.q[1] = static_cast<float>(std::sin(h));
    p.q[2] = 0;
    p.q[3] = static_cast<float>(std::cos(h));
    return p;
}
// A ship that sails `speed` m/s along its bow (-z of its own frame) and turns `turn` degrees a second.
struct Sailing {
    Pose hull = yawed(-9724.43, 351.9, -6538.86, 62.7);
    double yaw = 62.7, speed = 14.0, turn = 0.0;
    void step(double dt) {
        const float bow_local[3] = {0, 0, -1};
        double bow[3];
        rotate(hull.q, bow_local, bow);
        yaw += turn * dt;
        hull = yawed(hull.x + bow[0] * speed * dt, hull.y, hull.z + bow[2] * speed * dt, yaw);
    }
};
// The world place of a point of the ship's frame.
void on_ship(const Pose& hull, double lx, double ly, double lz, double out[3]) {
    const float l[3] = {static_cast<float>(lx), static_cast<float>(ly), static_cast<float>(lz)}, unit[4] = {0, 0, 0, 1};
    const Pose w = attach(hull, l, unit);
    out[0] = w.x;
    out[1] = w.y;
    out[2] = w.z;
}

// How the pretend game carries a character that has no parent, between two frames. None of them but Rigid uses the
// sum the module itself uses, so "it ends on its spot" is not true by construction.
enum class Carry { None, Rigid, Origin, Inertia, Random };
// A character on a ship, request by request, with a pretend game: stand() on a deck part, air() without a parent,
// land() on the part again. `takes`: the game starts each frame from the request as the module left it (true), or
// from its own position whatever the request said (false).
struct Flight {
    Sailing ship;
    AirRide ride;
    uint64_t ms = 500000;
    double spot[3] = {-2.52, 3.14, 5.30};
    uint32_t part = 0xB010081D, root = 0xB010000A;
    double at[3]{}, game[3]{};       // the request as it went to the game; the game's own position
    double total = 0, most = 0;      // the fixes: metres in all, the largest
    int fixes = 0, frames = 0;
    AirDone last;
    bool have_done = false;
    bool write = true;
    // In the air.
    double t_air = 0, y0 = 0, v_take[2]{};
    uint32_t rnd = 12345;

    void collect() {
        AirDone d;
        if (ride.done(&d)) {
            last = d;
            have_done = true;
        }
    }
    AirFrame base(double dt) {
        ms += static_cast<uint64_t>(std::llround(dt * 1000.0));
        AirFrame f;
        f.ms = ms;
        f.root = root;
        f.hull_ok = f.request_ok = f.body_ok = true;
        f.hull = ship.hull;
        return f;
    }
    void stand(int n, double dt, bool followed = true) {
        for (int i = 0; i < n; ++i) {
            ship.step(dt);
            AirFrame f = base(dt);
            f.on_part = true;
            f.followed = followed;
            f.part = part;
            on_ship(ship.hull, spot[0], spot[1], spot[2], f.request);
            std::memcpy(f.body, f.request, sizeof f.body);
            double add[2];
            CHECK(!ride.frame(f, write, add));
            std::memcpy(at, f.request, sizeof at);
            std::memcpy(game, at, sizeof game);
            collect();
        }
        t_air = 0;
        y0 = at[1];
    }
    // `vy`: its speed upwards at take-off, m/s (NaN: it keeps its height: firm ground). `own_x`, `own_z`: its own
    // movement, m/s in the ship's frame. Returns the frames in which a fix came.
    int air(int n, double dt, Carry carry, double share, double vy, double own_x, double own_z, bool takes) {
        int got = 0;
        for (int i = 0; i < n; ++i) {
            const Pose before = ship.hull;
            ship.step(dt);
            if (t_air == 0) {
                v_take[0] = (ship.hull.x - before.x) / dt;
                v_take[1] = (ship.hull.z - before.z) / dt;
            }
            t_air += dt;
            const double* from = takes ? at : game;
            double c[2] = {0, 0};
            if (carry == Carry::Rigid) {
                double l[3], rode[3];
                to_local(before, from, l);
                on_ship(ship.hull, l[0], l[1], l[2], rode);
                c[0] = (rode[0] - from[0]) * share;
                c[1] = (rode[2] - from[2]) * share;
            } else if (carry == Carry::Origin || carry == Carry::Random) {
                double s = share;
                if (carry == Carry::Random) {
                    rnd = rnd * 1664525u + 1013904223u;
                    s = -0.1 + 1.1 * ((rnd >> 8) & 0xFFFF) / 65535.0;
                }
                c[0] = (ship.hull.x - before.x) * s;
                c[1] = (ship.hull.z - before.z) * s;
            } else if (carry == Carry::Inertia) {
                c[0] = v_take[0] * dt * share;
                c[1] = v_take[1] * dt * share;
            }
            AirFrame f = base(dt);
            f.body[0] = from[0] + c[0];
            f.body[1] = std::isnan(vy) ? y0 : y0 + vy * t_air - 4.9 * t_air * t_air;
            f.body[2] = from[2] + c[1];
            double bl[3];
            to_local(ship.hull, f.body, bl);
            on_ship(ship.hull, bl[0] + own_x * dt, bl[1], bl[2] + own_z * dt, f.request);
            f.request[1] = f.body[1];
            std::memcpy(game, f.request, sizeof game);
            double add[2];
            const bool fix = ride.frame(f, write, add);
            std::memcpy(at, f.request, sizeof at);
            if (fix) {
                ++got;
                ++fixes;
                const double m = std::hypot(add[0], add[1]);
                total += m;
                most = std::max(most, m);
                if (write) {
                    at[0] += add[0];
                    at[2] += add[1];
                }
            }
            ++frames;
            collect();
        }
        return got;
    }
    // It stands on the part again, where it is.
    void land(double dt) {
        const Pose before = ship.hull;
        ship.step(dt);
        double l[3];
        to_local(before, at, l);
        AirFrame f = base(dt);
        f.on_part = true;
        f.followed = true;
        f.part = part;
        on_ship(ship.hull, l[0], l[1], l[2], f.request);
        std::memcpy(f.body, f.request, sizeof f.body);
        double add[2];
        CHECK(!ride.frame(f, write, add));
        std::memcpy(at, f.request, sizeof at);
        collect();
    }
    // Where the request is, in the ship's frame.
    void where(double out[3]) const { to_local(ship.hull, at, out); }
};

void test_air_ride() {
    // Left where it is in the world (08:24:52), carried part of the way by the hull's own way (not by its turn),
    // by the speed it had at take-off, or by a share that changes in every frame: it comes down where it left the
    // deck, at 5, 60 and 144 frames a second, on a straight course and in a hard turn (20 degrees a second at
    // 14 m/s: release b4 drifted 2.5 m in 1.4 s there).
    for (const double dt : {0.2, 1.0 / 60, 1.0 / 144}) {
        for (const double turn : {0.0, 20.0, -20.0}) {
            for (const Carry carry : {Carry::None, Carry::Origin, Carry::Inertia, Carry::Random}) {
                Flight fl;
                fl.ship.turn = turn;
                fl.stand(static_cast<int>(1.0 / dt), dt);
                fl.air(static_cast<int>(1.2 / dt), dt, carry, 0.5, 6.9, 0, 0, true);
                CHECK(fl.ride.in_air() && !fl.have_done);
                fl.land(dt);
                CHECK(fl.have_done && fl.last.why == AirEnd::Landed && fl.last.to == fl.part && fl.last.wrote);
                CHECK(fl.last.off >= 0 && fl.last.off < 0.06);
                CHECK(fl.last.ship_m > 10.0);
            }
        }
    }
    // Carried all the way by the game (a copy that draws 30 frames a second, 08:23:21): nothing is added.
    {
        Flight fl;
        fl.ship.turn = 20.0;
        fl.stand(30, 1.0 / 30);
        CHECK(fl.air(36, 1.0 / 30, Carry::Rigid, 1.0, 6.9, 0, 0, true) == 0);
        fl.land(1.0 / 30);
        CHECK(fl.last.fixed == 0 && fl.last.total_fix == 0 && fl.last.off < 0.02 && !fl.last.wrote);
    }
    // Its own movement in the air stays its own, along the ship's course too (b4 undid it): 4 m/s towards the bow
    // and 1 m/s to starboard for 0.6 s, whether the game carries it or not.
    for (const Carry carry : {Carry::None, Carry::Rigid}) {
        Flight fl;
        fl.ship.speed = 10.0;
        fl.stand(60, 1.0 / 60);
        fl.air(36, 1.0 / 60, carry, 1.0, 3.0, 1.0, -4.0, true);
        double l[3];
        fl.where(l);
        CHECK(near(l[0], -2.52 + 0.6, 0.04) && near(l[2], 5.30 - 2.4, 0.04));
    }
    // A reversing ship.
    {
        Flight fl;
        fl.ship.speed = -3.0;
        fl.stand(10, 0.2);
        fl.air(5, 0.2, Carry::None, 0, 6.9, 0, 0, true);
        fl.land(0.2);
        CHECK(fl.last.why == AirEnd::Landed && fl.last.off < 0.05);
    }
    // 144 frames a second, a ship at 2 m/s, the game carrying 88 % of the way: 1.7 mm lack in each frame. They add
    // up and are given every so often (the earlier way of doing it never gave them: 0.29 m astern in 1.2 s).
    {
        Flight fl;
        fl.ship.speed = 2.0;
        fl.stand(144, 1.0 / 144);
        const int got = fl.air(172, 1.0 / 144, Carry::Origin, 0.88, 6.9, 0, 0, true);
        CHECK(got >= 5 && got < 40);
        fl.land(1.0 / 144);
        CHECK(fl.last.off < 0.04 && near(fl.total, 0.29, 0.04));
    }
    // A ship that drifts at half a metre a second: left to the game, whatever lacks.
    {
        Flight fl;
        fl.ship.speed = 0.5;
        fl.stand(60, 1.0 / 60);
        CHECK(fl.air(60, 1.0 / 60, Carry::None, 0, 6.9, 0, 0, true) == 0);
        CHECK(fl.ride.ship_speed() < kAirMinSpeed);
    }
    // Off the part a moment after stepping onto the ship (its speed is not known yet): nothing.
    {
        Flight fl;
        fl.stand(2, 0.1);
        CHECK(fl.ride.ship_speed() == 0);
        CHECK(fl.air(1, 0.1, Carry::None, 0, 6.9, 0, 0, true) == 0);
    }
    // It stepped onto a quay from a ship doing 3 m/s and walks away from it: without a parent, but its height does
    // not change. Left to the game after kAirRestMs: dragged 1.1 m at most instead of 4.5 m.
    {
        Flight fl;
        fl.ship.speed = 3.0;
        fl.stand(60, 1.0 / 60);
        fl.air(60, 1.0 / 60, Carry::None, 0, std::numeric_limits<double>::quiet_NaN(), 1.5, 0, true);
        CHECK(fl.have_done && fl.last.why == AirEnd::Rest && !fl.ride.in_air());
        CHECK(fl.last.air_ms <= kAirRestMs + 40 && fl.total < 1.1 && fl.total > 0.7);
        const double before = fl.total;
        CHECK(fl.air(30, 1.0 / 60, Carry::None, 0, std::numeric_limits<double>::quiet_NaN(), 1.5, 0, true) == 0);
        CHECK(fl.total == before);
    }
    // The top of a jump is not standing: 144 frames a second, 3 m/s upwards (0.61 s in the air).
    {
        Flight fl;
        fl.stand(144, 1.0 / 144);
        fl.air(86, 1.0 / 144, Carry::None, 0, 3.0, 0, 0, true);
        CHECK(fl.ride.in_air() && !fl.have_done);
        fl.land(1.0 / 144);
        CHECK(fl.last.why == AirEnd::Landed && fl.last.off < 0.05);
    }
    // Too long in the air: over at kAirMaxS, and nothing after.
    {
        Flight fl;
        fl.stand(60, 1.0 / 60);
        fl.air(100, 1.0 / 60, Carry::None, 0, 9.0, 0, 0, true);
        CHECK(fl.have_done && fl.last.why == AirEnd::Time && !fl.ride.in_air());
        CHECK(fl.last.air_ms > 1500 && fl.last.air_ms < 1560);
        const double before = fl.total;
        fl.air(10, 1.0 / 60, Carry::None, 0, 9.0, 0, 0, true);
        CHECK(fl.total == before);
    }
    // Overboard: down at the water (3 m/s upwards from 3.14 m: under 0.2 m after 1.13 s).
    {
        Flight fl;
        fl.stand(60, 1.0 / 60);
        fl.air(80, 1.0 / 60, Carry::None, 0, 3.0, 2.0, 0, true);
        CHECK(fl.have_done && fl.last.why == AirEnd::Water && fl.last.air_ms > 1100 && fl.last.air_ms < 1200);
    }
    // The game does NOT take the moved request (it starts each frame from its own position): no fix is larger than
    // a little over the ship's way of one frame, and the request stays that close to where the game has it.
    {
        Flight fl;
        fl.stand(5, 0.2);
        fl.air(6, 0.2, Carry::None, 0, 6.9, 0, 0, false);
        CHECK(fl.most <= 14.0 * 0.2 * kAirSlack + kAirSlackM + 1e-6 && fl.most > 2.7);
        CHECK(std::hypot(fl.at[0] - fl.game[0], fl.at[2] - fl.game[2]) <= fl.most + 1e-9);
    }
    // Only worked out (probe): the same sums, nothing taken as moved.
    {
        Flight fl;
        fl.write = false;
        fl.stand(5, 0.2);
        CHECK(fl.air(6, 0.2, Carry::None, 0, 6.9, 0, 0, true) == 6);
        fl.land(0.2);
        CHECK(!fl.last.wrote && fl.last.fixed == 6 && near(fl.last.total_fix, 6 * 2.8, 0.05));
        CHECK(fl.last.off > 16.0);  // and the character is where the game left it
    }
}

void test_air_ride_ends() {
    const auto standing = [](Flight& fl) {
        fl.stand(5, 0.2);
        CHECK(fl.air(1, 0.2, Carry::None, 0, 6.9, 0, 0, true) == 1);
        CHECK(fl.ride.in_air());
    };
    // One frame by hand, for the cases below: without a parent, everything readable, left where it was.
    const auto frame = [](Flight& fl, double dt) {
        fl.ship.step(dt);
        AirFrame f = fl.base(dt);
        std::memcpy(f.request, fl.at, sizeof f.request);
        std::memcpy(f.body, fl.at, sizeof f.body);
        f.request[1] = f.body[1] = fl.at[1] + 0.5;
        return f;
    };
    double add[2];
    // Another parent (a mount, a seat): over, nothing added.
    {
        Flight fl;
        standing(fl);
        AirFrame f = frame(fl, 0.2);
        f.other_parent = true;
        CHECK(!fl.ride.frame(f, true, add) && add[0] == 0 && add[1] == 0);
        fl.collect();
        CHECK(fl.have_done && fl.last.why == AirEnd::Parent && !fl.ride.in_air());
    }
    // The ship, the request or the physics position unreadable; the top actor another one: over.
    for (int what = 0; what < 4; ++what) {
        Flight fl;
        standing(fl);
        AirFrame f = frame(fl, 0.2);
        if (what == 0) f.hull_ok = false;
        if (what == 1) f.request_ok = false;
        if (what == 2) f.body_ok = false;
        if (what == 3) f.root = 0xB0100999;
        CHECK(!fl.ride.frame(f, true, add) && add[0] == 0 && add[1] == 0);
        fl.collect();
        CHECK(fl.have_done && fl.last.why == AirEnd::Unreadable && !fl.ride.in_air());
    }
    // The ship is 40 m away from one frame to the next (put away and taken out again): over, nothing added.
    {
        Flight fl;
        standing(fl);
        AirFrame f = frame(fl, 0.2);
        f.hull.x += 40.0;
        CHECK(!fl.ride.frame(f, true, add) && add[0] == 0 && add[1] == 0);
        fl.collect();
        CHECK(fl.have_done && fl.last.why == AirEnd::ShipJump);
    }
    // Something else moved the character 30 m: no more than a little over the ship's way is added.
    {
        Flight fl;
        standing(fl);
        AirFrame f = frame(fl, 0.2);
        f.body[0] += 30.0;
        f.request[0] += 30.0;
        CHECK(fl.ride.frame(f, true, add));
        CHECK(near(std::hypot(add[0], add[1]), 2.8 * kAirSlack + kAirSlackM, 0.02));
    }
    // A frame in which the ship's pose has not changed (two own requests 4 ms apart were recorded) and one with no
    // time at all: nothing lacks, and the next frame makes up both.
    {
        Flight fl;
        standing(fl);
        AirFrame same = fl.base(0.004);
        std::memcpy(same.request, fl.at, sizeof same.request);
        std::memcpy(same.body, fl.at, sizeof same.body);
        same.request[1] = same.body[1] = fl.at[1] + 0.3;
        CHECK(!fl.ride.frame(same, true, add));
        AirFrame again = fl.base(0.0);
        std::memcpy(again.request, fl.at, sizeof again.request);
        std::memcpy(again.body, fl.at, sizeof again.body);
        again.request[1] = again.body[1] = fl.at[1] + 0.6;
        CHECK(!fl.ride.frame(again, true, add));
        const Pose before = fl.ship.hull;
        AirFrame f = frame(fl, 0.2);
        CHECK(fl.ride.frame(f, true, add));
        double l[3], rode[3];
        to_local(before, fl.at, l);
        on_ship(fl.ship.hull, l[0], l[1], l[2], rode);
        CHECK(near(fl.at[0] + add[0], rode[0], 0.01) && near(fl.at[2] + add[1], rode[2], 0.01));
    }
    // The write did not happen: the character is taken to be where the game asked, and the next frame makes it up.
    {
        Flight fl;
        fl.stand(5, 0.2);
        const Pose take_off = fl.ship.hull;
        double left[3];
        std::memcpy(left, fl.at, sizeof left);
        fl.ship.step(0.2);
        AirFrame f = fl.base(0.2);
        std::memcpy(f.request, left, sizeof f.request);
        std::memcpy(f.body, left, sizeof f.body);
        f.request[1] = f.body[1] = left[1] + 0.5;
        CHECK(fl.ride.frame(f, true, add));
        fl.ride.unwritten();
        fl.ship.step(0.2);
        AirFrame g = fl.base(0.2);
        std::memcpy(g.request, left, sizeof g.request);
        std::memcpy(g.body, left, sizeof g.body);
        g.request[1] = g.body[1] = left[1] + 0.8;
        CHECK(fl.ride.frame(g, true, add));
        // Only this frame's way of the ship (the frame before's is lost: the character was left there).
        CHECK(near(std::hypot(add[0], add[1]), 2.8, 0.02));
        (void)take_off;
    }
    // No request for two seconds (a load, the helm) and then one without a parent: not a take-off.
    {
        Flight fl;
        fl.stand(5, 0.2);
        fl.ms += 2000;
        CHECK(fl.air(3, 0.2, Carry::None, 0, 6.9, 0, 0, true) == 0);
        CHECK(!fl.ride.in_air() && !fl.have_done);
    }
    // On a part of something that no other player's mod places (its own ship, a plank of the quay): not a take-off.
    {
        Flight fl;
        fl.stand(5, 0.2, false);
        CHECK(fl.air(3, 0.2, Carry::None, 0, 6.9, 0, 0, true) == 0);
        CHECK(!fl.ride.in_air() && !fl.have_done);
    }
    // Lands on a part of something else: said so, no distance across a deck that is not the one it left.
    {
        Flight fl;
        standing(fl);
        fl.ship.step(0.2);
        AirFrame f = fl.base(0.2);
        f.on_part = true;
        f.followed = false;
        f.part = 0xB0100510;
        f.root = 0xB0100510;
        std::memcpy(f.request, fl.at, sizeof f.request);
        std::memcpy(f.body, fl.at, sizeof f.body);
        CHECK(!fl.ride.frame(f, true, add));
        fl.collect();
        CHECK(fl.have_done && fl.last.why == AirEnd::Landed && fl.last.to == 0xB0100510 && fl.last.off < 0 && fl.last.ship_m < 0);
    }
}

void test_air_carry() {
    const Pose then = yawed(-9724.43, 351.9, -6538.86, 62.7);
    Sailing one;
    one.hull = then;
    one.step(0.2);
    double was[3], rode[3], fix[2];
    on_ship(then, -2.52, 3.14, 5.30, was);
    on_ship(one.hull, -2.52, 3.14, 5.30, rode);
    // Left where it was: the whole way of its spot. Carried all the way: nothing.
    CHECK(air_carry(then, was, one.hull, was, 14.0, fix) == AirStep::Fix);
    CHECK(near(std::hypot(fix[0], fix[1]), 2.8, 0.01) && near(was[0] + fix[0], rode[0], 0.01) && near(was[2] + fix[1], rode[2], 0.01));
    CHECK(air_carry(then, was, one.hull, rode, 14.0, fix) == AirStep::None && fix[0] == 0 && fix[1] == 0);
    // A slow ship, or one whose speed is not known: nothing.
    CHECK(air_carry(then, was, one.hull, was, 0.99, fix) == AirStep::None);
    CHECK(air_carry(then, was, one.hull, was, 0.0, fix) == AirStep::None);
    CHECK(air_carry(then, was, one.hull, was, std::numeric_limits<double>::quiet_NaN(), fix) == AirStep::None);
    // Something else moved the character 30 m (not the ship's doing): no more than a little over the ship's way.
    {
        const double far[3] = {was[0] + 30.0, was[1], was[2]};
        CHECK(air_carry(then, was, one.hull, far, 14.0, fix) == AirStep::Fix);
        CHECK(near(std::hypot(fix[0], fix[1]), 2.8 * kAirSlack + kAirSlackM, 0.01));
    }
    // The ship is somewhere else altogether, or a pose or a position is not one: over.
    {
        Pose gone = then;
        gone.x += 40.0;
        CHECK(air_carry(then, was, gone, was, 14.0, fix) == AirStep::Over);
        Pose bad = one.hull;
        bad.x = std::numeric_limits<double>::infinity();
        CHECK(air_carry(then, was, bad, was, 14.0, fix) == AirStep::Over);
        Pose zero = one.hull;
        zero.q[0] = zero.q[1] = zero.q[2] = zero.q[3] = 0;
        CHECK(air_carry(then, was, zero, was, 14.0, fix) == AirStep::Over);
        const double nan[3] = {std::numeric_limits<double>::quiet_NaN(), was[1], was[2]};
        CHECK(air_carry(then, was, one.hull, nan, 14.0, fix) == AirStep::Over);
    }
    // A ship that rolls between two frames: the spot's own way is made up, not the hull's origin's.
    {
        Pose tilted = one.hull;
        const float roll[4] = {0.0f, 0.0f, 0.0523f, 0.9986f};  // 6 degrees about the ship's length
        float q[4];
        mul(one.hull.q, roll, q);
        std::memcpy(tilted.q, q, sizeof q);
        double rode_tilted[3];
        on_ship(tilted, -2.52, 3.14, 5.30, rode_tilted);
        CHECK(air_carry(then, was, tilted, was, 14.0, fix) == AirStep::Fix);
        CHECK(near(was[0] + fix[0], rode_tilted[0], 0.01) && near(was[2] + fix[1], rode_tilted[2], 0.01));
        CHECK(std::hypot(rode_tilted[0] - rode[0], rode_tilted[2] - rode[2]) > 0.2);
    }
    // to_local undoes attach; over: too long in the air, or down at the water (the lowest deck is at 1.8 in the
    // ship's frame, the swimmer at the foot of the ladder at -0.58).
    {
        double back[3];
        to_local(then, was, back);
        CHECK(near(back[0], -2.52, 1e-3) && near(back[1], 3.14, 1e-3) && near(back[2], 5.30, 1e-3));
    }
    CHECK(!air_over(0.0, 1.87) && !air_over(kAirMaxS, 3.14) && air_over(kAirMaxS + 0.01, 3.14));
    CHECK(air_over(0.3, -0.58) && air_over(0.3, 0.19) && !air_over(0.3, 0.21));
    CHECK(air_over(-1.0, 3.0) && air_over(0.3, std::numeric_limits<double>::quiet_NaN()));
}

// The frames of 08:24:52 (15-nov, the guest's log: the guest went off the stern), through AirRide as the game alone
// made them (nothing written: `probe`): what the game's carry lacked in each. Each frame: the ship's way, the
// physics position and the request in the ship's frame. The ship is put on a straight course (the record of that
// build has no turn; the ship did turn, which is where the recorded sideways shifts come from), so only the part
// along the ship is looked at.
void test_air_ride_recorded() {
    struct Frame {
        uint32_t ms;
        double ship;
        double body[3], asked[3];
    };
    const double last[3] = {-2.81, 3.54, 5.51};  // its last request on the part
    const Frame frames[] = {{140, 2.92, {-1.77, 3.77, 8.43}, {-1.96, 4.34, 8.61}},
                            {233, 1.85, {-2.05, 4.60, 9.10}, {-2.31, 4.42, 9.34}},
                            {157, 2.88, {-3.80, 4.40, 11.17}, {-3.98, 4.34, 11.36}},
                            {207, 1.96, {-3.22, 4.37, 13.47}, {-3.25, 4.25, 13.51}},
                            {4, 2.59, {-2.24, 4.25, 16.24}, {-2.39, 4.17, 16.43}},
                            {149, 0.00, {-2.40, 4.22, 16.43}, {-2.55, 4.15, 16.62}},
                            {165, 1.89, {-1.88, 4.22, 18.57}, {-1.99, 4.05, 18.74}}};
    Sailing ship;
    ship.speed = 1.0;  // step(metres): the frame's way
    AirRide ride;
    uint64_t ms = 700000;
    double add[2];
    // Standing on the part while the ship does 2.8 m every 200 ms, the last request the recorded one.
    for (int i = 0; i < 5; ++i) {
        ship.step(2.8);
        ms += 200;
        AirFrame f;
        f.ms = ms;
        f.on_part = f.followed = true;
        f.part = 0xB010081D;
        f.root = 0xB010000A;
        f.hull_ok = f.request_ok = f.body_ok = true;
        f.hull = ship.hull;
        on_ship(ship.hull, last[0], last[1], last[2], f.request);
        std::memcpy(f.body, f.request, sizeof f.body);
        CHECK(!ride.frame(f, false, add));
    }
    CHECK(near(ride.ship_speed(), 14.0, 0.1));
    double lacked = 0, first_five = 0;
    int n = 0;
    for (const Frame& fr : frames) {
        ship.step(fr.ship);
        ms += fr.ms;
        AirFrame f;
        f.ms = ms;
        f.root = 0xB010000A;
        f.hull_ok = f.request_ok = f.body_ok = true;
        f.hull = ship.hull;
        on_ship(ship.hull, fr.body[0], fr.body[1], fr.body[2], f.body);
        on_ship(ship.hull, fr.asked[0], fr.asked[1], fr.asked[2], f.request);
        if (ride.frame(f, false, add)) {
            // Along the ship (its -z is the bow): how far the fix would take the character towards the bow.
            const double moved[3] = {f.request[0] + add[0], f.request[1], f.request[2] + add[1]};
            double l[3];
            to_local(ship.hull, moved, l);
            const double along = fr.asked[2] - l[2];
            CHECK(along <= fr.ship * kAirSlack + kAirSlackM + 1e-3);
            lacked += along;
            if (n < 5) first_five += along;
        }
        ++n;
    }
    // In the first five frames the carry lacked 2.92 + 0.49 + 1.83 + 2.11 + 2.73 m of the ship's 12.2 m. (The last
    // two are not asserted one by one: the character's height is within 3 cm for 314 ms there, right at the limit
    // of "standing on something firm".) The character went 13.2 m aft in all; what is not lack (1.2 m) was its own
    // movement, which is left alone.
    CHECK(near(first_five, 10.08, 0.15));
    CHECK(lacked >= first_five && lacked < 12.2);
}

int main() {
    test_air_carry();
    test_air_ride();
    test_air_ride_ends();
    test_air_ride_recorded();
    test_usable();
    test_track_empty_and_single();
    test_track_interpolates_behind();
    test_track_default_delay_extrapolates();
    test_track_teleport_not_interpolated();
    test_track_ring_and_clock();
    test_track_bursty_samples();
    test_rotation();
    test_to_sector();
    test_attach();
    test_riders();
    test_vehicle_poses();
    test_vehicle_poses_timed();
    test_sample_clock();
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("vehicle_follow: all passed\n");
    return 0;
}
