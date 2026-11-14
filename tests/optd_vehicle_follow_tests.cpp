// vehicle_follow (src/mod/session2/vehicle_follow_logic.h): the smoothing of the controller's ~1 Hz ship samples and
// the sector form of the TransformSync position argument. Values from the 2026-11-13 test (host's ship at Vellua).

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
}  // namespace

int main() {
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
    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("vehicle_follow: all passed\n");
    return 0;
}
