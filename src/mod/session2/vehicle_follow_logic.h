#pragma once
// vehicle_follow (vehicle_follow.h): the pure part. Build 1.0.0.3576, docs/ESTADO-3527.md "13-nov: BARCO".
//
// Each client simulates its own copy of a ship. The other player's ship is drawn from its hierarchy parent, the vehicle
// actor (type 5), which every frame gets a TransformSync request of type 7 with the local ship's pose. The vehicle
// actor's last network pose (the controller's, ~1 Hz) sits in its TransformSync at +0x550 (world vec3) / +0x55C (quat).
// vehicle_follow replaces that request's pose with one built from those network samples (interpolated, extrapolated
// past the newest for up to a second), so the ship and everything attached to it is drawn where the controller has it.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "common/clock_mapper.h"

namespace cdmp::session2::vfl {

struct Pose {
    double x = 0, y = 0, z = 0;
    float q[4] = {0, 0, 0, 1};
};

inline bool finite_pose(const Pose& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(p.q[0]) &&
           std::isfinite(p.q[1]) && std::isfinite(p.q[2]) && std::isfinite(p.q[3]);
}
inline bool unit_quat(const float q[4]) {
    const float n = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    return std::isfinite(n) && std::fabs(n - 1.0f) < 0.02f;
}
// A usable network sample: finite, not the zero of a vehicle that never got one, a unit quaternion.
inline bool usable(const Pose& p) {
    return finite_pose(p) && !(p.x == 0 && p.z == 0) && std::fabs(p.x) < 1.0e6 && std::fabs(p.z) < 1.0e6 && unit_quat(p.q);
}

inline double dist_xz(const Pose& a, const Pose& b) { return std::hypot(a.x - b.x, a.z - b.z); }

// Normalised lerp of two quaternions along the shorter arc.
inline void nlerp(const float a[4], const float b[4], double t, float out[4]) {
    const float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    const float s = dot < 0 ? -1.0f : 1.0f;
    float n = 0;
    for (int i = 0; i < 4; ++i) {
        out[i] = static_cast<float>(a[i] + (s * b[i] - a[i]) * t);
        n += out[i] * out[i];
    }
    n = std::sqrt(n);
    if (n > 1e-6f)
        for (int i = 0; i < 4; ++i) out[i] /= n;
}

inline Pose lerp(const Pose& a, const Pose& b, double t) {
    Pose r;
    r.x = a.x + (b.x - a.x) * t;
    r.y = a.y + (b.y - a.y) * t;
    r.z = a.z + (b.z - a.z) * t;
    nlerp(a.q, b.q, t, r.q);
    return r;
}

// q v q^-1 for a unit quaternion (x, y, z, w): a vector of the child's frame in the parent's.
inline void rotate(const float q[4], const float v[3], double out[3]) {
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    const double cx = y * v[2] - z * v[1], cy = z * v[0] - x * v[2], cz = x * v[1] - y * v[0];
    const double dx = y * cz - z * cy, dy = z * cx - x * cz, dz = x * cy - y * cx;
    out[0] = v[0] + 2 * (w * cx + dx);
    out[1] = v[1] + 2 * (w * cy + dy);
    out[2] = v[2] + 2 * (w * cz + dz);
}
// a b: the rotation b followed by a (a child's rotation b seen from the parent's frame a).
inline void mul(const float a[4], const float b[4], float out[4]) {
    const float ax = a[0], ay = a[1], az = a[2], aw = a[3], bx = b[0], by = b[1], bz = b[2], bw = b[3];
    out[0] = aw * bx + ax * bw + ay * bz - az * by;
    out[1] = aw * by - ax * bz + ay * bw + az * bx;
    out[2] = aw * bz + ax * by - ay * bx + az * bw;
    out[3] = aw * bw - ax * bx - ay * by - az * bz;
}
// The world pose of something that sits at (local_pos, local_q) on a parent with the world pose `parent`.
inline Pose attach(const Pose& parent, const float local_pos[3], const float local_q[4]) {
    double w[3];
    rotate(parent.q, local_pos, w);
    Pose r;
    r.x = parent.x + w[0];
    r.y = parent.y + w[1];
    r.z = parent.z + w[2];
    mul(parent.q, local_q, r.q);
    return r;
}

// The network samples of one vehicle, in the order they changed (t = local milliseconds of the change).
class Track {
public:
    static constexpr size_t kCap = 8;
    // Drawn this far behind the samples' arrival. The controller's character is drawn ~200-600 ms behind
    // (remote_interp's players' delay), so the ship uses about the same and extrapolates between its ~1 Hz samples
    // (the controller keeps sending them while the ship is still): at full sail (~9 m/s) a 1.2 s delay left the
    // helmsman ~8 m ahead of the wheel.
    static constexpr uint64_t kDefaultDelayMs = 300;
    static constexpr uint64_t kMaxExtrapMs = 1000;  // past the newest sample, at most this much of its velocity
    static constexpr double kTeleportM = 60.0;   // a bigger step between two samples is not interpolated
    static constexpr uint64_t kVelocityBaseMs = 500;  // the extrapolation's velocity: over at least this much time
    static constexpr double kMaxSpeedMps = 40.0;      // and never faster than this (a ship at full sail: ~14 m/s)

    explicit Track(uint64_t delay_ms = kDefaultDelayMs) : delay_ms_(delay_ms) {}
    void set_delay(uint64_t delay_ms) { delay_ms_ = delay_ms; }
    uint64_t delay() const { return delay_ms_; }

    // Further than kTeleportM from each other, in the plane or in height.
    static bool apart(const Pose& a, const Pose& b) { return dist_xz(a, b) > kTeleportM || std::fabs(a.y - b.y) > kTeleportM; }

    // A sample read now; kept only when it differs from the newest one. True when added.
    // ONE sample far from the newest does not move the vehicle: it waits, and goes in (at its own time) only when the
    // next sample is in that new place too; if the next one is back by the track, the far one is forgotten. Live,
    // 2026-11-16, twice in one day on the guest (13:40:57 and 15:22:36): one sample of the host's ship 769 m away and
    // 1592 m below its place, and one 318 m away and 43890 m below, each followed by a right one. The ship was drawn
    // there for a frame, the deck was gone from under everyone standing on it, and the guest's character fell into
    // the water under the hull (the second time it drowned there). A real jump (a ship fetched back to its mooring) is
    // drawn one sample later than before.
    bool push(uint64_t t_ms, const Pose& p) {
        if (!usable(p)) return false;
        if (n_ && apart(s_[(head_ + n_ - 1) % kCap].p, p)) {
            if (!pending_ || apart(pending_p_.p, p)) {
                pending_ = true;
                pending_p_ = {t_ms, p};
                ++lone_;
                return false;
            }
            --lone_;  // the second one there: the first was the vehicle after all
            pending_ = false;
            append(pending_p_.t, pending_p_.p);
        } else {
            pending_ = false;  // back by the track (or the first sample of all)
        }
        if (n_) {
            const Pose& last = s_[(head_ + n_ - 1) % kCap].p;
            const bool same = std::fabs(last.x - p.x) < 1e-3 && std::fabs(last.y - p.y) < 1e-3 &&
                              std::fabs(last.z - p.z) < 1e-3 && std::fabs(last.q[0] - p.q[0]) < 1e-5f &&
                              std::fabs(last.q[1] - p.q[1]) < 1e-5f && std::fabs(last.q[2] - p.q[2]) < 1e-5f &&
                              std::fabs(last.q[3] - p.q[3]) < 1e-5f;
            if (same) return false;
        }
        append(t_ms, p);
        return true;
    }
    size_t size() const { return n_; }
    void clear() {
        n_ = head_ = 0;
        pending_ = false;
    }
    // Far samples that no second one confirmed (the one waiting now included).
    uint64_t lone_jumps() const { return lone_; }

    // The pose to draw at local time now_ms (false: no sample yet).
    bool at(uint64_t now_ms, Pose* out) const {
        if (!n_) return false;
        const uint64_t t = now_ms > delay_ms_ ? now_ms - delay_ms_ : 0;
        const Sample& first = get(0);
        if (n_ == 1 || t <= first.t) {
            *out = n_ == 1 ? first.p : (t <= first.t ? first.p : get(n_ - 1).p);
            return true;
        }
        for (size_t i = 0; i + 1 < n_; ++i) {
            const Sample& a = get(i);
            const Sample& b = get(i + 1);
            if (t >= a.t && t <= b.t) {
                if (dist_xz(a.p, b.p) > kTeleportM) {
                    *out = b.p;
                    return true;
                }
                *out = lerp(a.p, b.p, static_cast<double>(t - a.t) / static_cast<double>(b.t - a.t));
                return true;
            }
        }
        // Past the newest sample: keep moving with the latest velocity for a short while. The samples are stamped when
        // they ARRIVE: two of the ~1 Hz poses can land a frame apart (a resent packet, a hitch), and the last segment
        // alone would then read as hundreds of m/s. So the velocity is taken over at least kVelocityBaseMs of samples
        // (as far back as there are, the time never counted shorter than that) and capped at kMaxSpeedMps.
        const Sample& b = get(n_ - 1);
        const uint64_t over = t - b.t;
        if (over > kMaxExtrapMs || dist_xz(get(n_ - 2).p, b.p) > kTeleportM) {
            *out = b.p;
            return true;
        }
        size_t j = n_ - 2;
        while (j > 0 && b.t - get(j).t < kVelocityBaseMs && dist_xz(get(j - 1).p, get(j).p) <= kTeleportM) --j;
        const Sample& a = get(j);
        const double span_s = static_cast<double>(std::max<uint64_t>(b.t - a.t, kVelocityBaseMs)) / 1000.0;
        double vx = (b.p.x - a.p.x) / span_s, vy = (b.p.y - a.p.y) / span_s, vz = (b.p.z - a.p.z) / span_s;
        const double speed = std::sqrt(vx * vx + vy * vy + vz * vz);
        if (speed > kMaxSpeedMps) {
            const double s = kMaxSpeedMps / speed;
            vx *= s;
            vy *= s;
            vz *= s;
        }
        const double k = static_cast<double>(over) / 1000.0;
        Pose r = b.p;
        r.x += vx * k;
        r.y += vy * k;
        r.z += vz * k;
        *out = r;
        return true;
    }

private:
    struct Sample {
        uint64_t t;
        Pose p;
    };
    const Sample& get(size_t i) const { return s_[(head_ + i) % kCap]; }
    void append(uint64_t t_ms, const Pose& p) {
        if (n_ && t_ms <= s_[(head_ + n_ - 1) % kCap].t) t_ms = s_[(head_ + n_ - 1) % kCap].t + 1;
        if (n_ == kCap) {
            head_ = (head_ + 1) % kCap;
            --n_;
        }
        s_[(head_ + n_) % kCap] = {t_ms, p};
        ++n_;
    }
    std::array<Sample, kCap> s_{};
    size_t n_ = 0, head_ = 0;
    uint64_t delay_ms_ = kDefaultDelayMs;
    bool pending_ = false;
    Sample pending_p_{};
    uint64_t lone_ = 0;
};

// A world coordinate as the game's TransformSync position argument: a float local to a 1000 m sector plus the sector.
// The caller's sector (`hint`) is kept when the local value stays within it; else the sector that truncates toward zero
// (what the game writes: -10384.7 -> sector -10, local -384.7).
// ---------------------------------------------------------------- riders (proto::EventKind::Riders)
// Who rides a ship besides the player's character (its crew): the actor, what it hangs from on the sender's client
// and its pose local to that. Wire: u8 n | n x (u32 key | u32 anchor | f32 x, y, z | f32 qx, qy, qz, qw).
struct Rider {
    uint32_t key = 0, anchor = 0;
    float pos[3]{}, q[4]{0, 0, 0, 1};
};
inline constexpr size_t kRiderWire = 36, kMaxRiders = 16;
// A rider worth sending or using: two 0xB keys (an NPC on a gimmick), a pose a few metres from its anchor, a unit
// rotation.
inline bool rider_ok(const Rider& r) {
    if ((r.key >> 28) != 0xB || (r.anchor >> 28) != 0xB || r.key == r.anchor || !unit_quat(r.q)) return false;
    for (const float v : r.pos)
        if (!std::isfinite(v) || std::fabs(v) > 500.0f) return false;
    return true;
}
// The payload for up to kMaxRiders riders (the rest is left out; the ones that are not rider_ok too).
inline std::vector<uint8_t> encode_riders(std::span<const Rider> riders) {
    std::vector<uint8_t> out(1, 0);
    for (const Rider& r : riders) {
        if (out[0] == kMaxRiders) break;
        if (!rider_ok(r)) continue;
        const size_t at = out.size();
        out.resize(at + kRiderWire);
        std::memcpy(&out[at], &r.key, 4);
        std::memcpy(&out[at + 4], &r.anchor, 4);
        std::memcpy(&out[at + 8], r.pos, 12);
        std::memcpy(&out[at + 20], r.q, 16);
        ++out[0];
    }
    return out;
}
// The riders of a payload, into out[kMaxRiders]; how many. 0 for a payload that is not exactly its count's size or
// names more than kMaxRiders; a rider that is not rider_ok, or a key already taken from this payload, is skipped.
inline size_t decode_riders(std::span<const uint8_t> payload, Rider* out) {
    if (payload.empty() || payload[0] > kMaxRiders || payload.size() != 1 + payload[0] * kRiderWire) return 0;
    size_t n = 0;
    for (size_t i = 0; i < payload[0]; ++i) {
        const uint8_t* p = payload.data() + 1 + i * kRiderWire;
        Rider r;
        std::memcpy(&r.key, p, 4);
        std::memcpy(&r.anchor, p + 4, 4);
        std::memcpy(r.pos, p + 8, 12);
        std::memcpy(r.q, p + 20, 16);
        if (!rider_ok(r)) continue;
        bool twice = false;
        for (size_t j = 0; j < n; ++j) twice |= out[j].key == r.key;
        if (!twice) out[n++] = r;
    }
    return n;
}

// ---------------------------------------------------------------- vehicle poses (proto::EventKind::Vehicles)
// Where a client has a vehicle it controls, for the other client's Track: the game itself tells the other client
// about once a second (1.0, 1.0, 1.7 s for an NPC's ship, which also changes speed all the time), and a Track fed
// with that has to guess for most of every second. Wire: u8 n | n x (u32 key | f32 x, y, z | f32 qx, qy, qz, qw).
struct VehiclePose {
    uint32_t key = 0;
    float pos[3]{}, q[4]{0, 0, 0, 1};
    uint16_t age_ms = 0;  // VehiclesTimed only: how long before the event was sent the sender saw this pose appear
};
inline constexpr size_t kVehicleWire = 32, kMaxVehiclePoses = 16;
inline bool vehicle_pose_ok(const VehiclePose& v) {
    if ((v.key >> 28) != 0xB || !unit_quat(v.q)) return false;
    for (const float c : v.pos)
        if (!std::isfinite(c) || std::fabs(c) > 1.0e6f) return false;
    return !(v.pos[0] == 0 && v.pos[2] == 0);  // the zero of a vehicle that never had a pose
}
inline std::vector<uint8_t> encode_vehicles(std::span<const VehiclePose> poses) {
    std::vector<uint8_t> out(1, 0);
    for (const VehiclePose& v : poses) {
        if (out[0] == kMaxVehiclePoses) break;
        if (!vehicle_pose_ok(v)) continue;
        const size_t at = out.size();
        out.resize(at + kVehicleWire);
        std::memcpy(&out[at], &v.key, 4);
        std::memcpy(&out[at + 4], v.pos, 12);
        std::memcpy(&out[at + 16], v.q, 16);
        ++out[0];
    }
    return out;
}
// The poses of a payload, into out[kMaxVehiclePoses]; how many. 0 for a payload that is not exactly its count's size
// or names more than the cap; a pose that is not vehicle_pose_ok, or a key already taken, is skipped.
inline size_t decode_vehicles(std::span<const uint8_t> payload, VehiclePose* out) {
    if (payload.empty() || payload[0] > kMaxVehiclePoses || payload.size() != 1 + payload[0] * kVehicleWire) return 0;
    size_t n = 0;
    for (size_t i = 0; i < payload[0]; ++i) {
        const uint8_t* p = payload.data() + 1 + i * kVehicleWire;
        VehiclePose v;
        std::memcpy(&v.key, p, 4);
        std::memcpy(v.pos, p + 4, 12);
        std::memcpy(v.q, p + 16, 16);
        if (!vehicle_pose_ok(v)) continue;
        bool twice = false;
        for (size_t j = 0; j < n; ++j) twice |= out[j].key == v.key;
        if (!twice) out[n++] = v;
    }
    return n;
}

// ---------------------------------------------------------------- timed vehicle poses (proto::EventKind::VehiclesTimed)
// The same poses with their time. A sample stamped when it ARRIVES carries two errors that have nothing to do with
// the ship: the sender's game only moves a vehicle once a frame and the sender looks ten times a second (a copy of the
// game running behind another window draws 7 frames a second: its poses are 143 ms apart and arrive 100 or 200 ms
// apart, so the receiver saw the ship at 0.7 and 1.4 times its speed by turns), and the network's own unevenness
// (20 +-30 ms: 0.76 to 1.27 times). So the sender looks at its vehicles every few milliseconds, notes when each pose
// appeared and sends that as an age; the receiver turns it into its own clock with the fastest transit seen
// (common/clock_mapper.h). tools/porting/boat/track_replay.cpp, cases (C) and (D): the drawn speed stays within 3-5%.
// Wire: u8 n | u64 sender's clock now, ms | n x (u32 key | u16 age ms | f32 x, y, z | f32 qx, qy, qz, qw).
inline constexpr size_t kVehicleTimedHead = 9, kVehicleTimedWire = 34;
inline std::vector<uint8_t> encode_vehicles_timed(uint64_t sender_ms, std::span<const VehiclePose> poses) {
    std::vector<uint8_t> out(kVehicleTimedHead, 0);
    std::memcpy(&out[1], &sender_ms, 8);
    for (const VehiclePose& v : poses) {
        if (out[0] == kMaxVehiclePoses) break;
        if (!vehicle_pose_ok(v)) continue;
        const size_t at = out.size();
        out.resize(at + kVehicleTimedWire);
        std::memcpy(&out[at], &v.key, 4);
        std::memcpy(&out[at + 4], &v.age_ms, 2);
        std::memcpy(&out[at + 6], v.pos, 12);
        std::memcpy(&out[at + 18], v.q, 16);
        ++out[0];
    }
    return out;
}
// As decode_vehicles; *sender_ms gets the sender's clock.
inline size_t decode_vehicles_timed(std::span<const uint8_t> payload, uint64_t* sender_ms, VehiclePose* out) {
    if (payload.size() < kVehicleTimedHead || payload[0] > kMaxVehiclePoses ||
        payload.size() != kVehicleTimedHead + payload[0] * kVehicleTimedWire)
        return 0;
    std::memcpy(sender_ms, payload.data() + 1, 8);
    size_t n = 0;
    for (size_t i = 0; i < payload[0]; ++i) {
        const uint8_t* p = payload.data() + kVehicleTimedHead + i * kVehicleTimedWire;
        VehiclePose v;
        std::memcpy(&v.key, p, 4);
        std::memcpy(&v.age_ms, p + 4, 2);
        std::memcpy(v.pos, p + 6, 12);
        std::memcpy(v.q, p + 18, 16);
        if (!vehicle_pose_ok(v)) continue;
        bool twice = false;
        for (size_t j = 0; j < n; ++j) twice |= out[j].key == v.key;
        if (!twice) out[n++] = v;
    }
    return n;
}
// The time, on the receiver's clock, of a sample the sender saw `age_ms` before it sent the event at `sender_ms`:
// `offset` is receiver clock - sender clock (the fastest transit included). Never later than now (a sample cannot
// come from the future) and never more than kMaxSampleAgeMs before it (an offset that went wrong: the arrival then).
inline constexpr uint64_t kMaxSampleAgeMs = 1000;
inline uint64_t sample_time(uint64_t now_ms, uint64_t sender_ms, uint16_t age_ms, int64_t offset) {
    const int64_t t = static_cast<int64_t>(sender_ms) - age_ms + offset;
    const auto now = static_cast<int64_t>(now_ms);
    if (t > now) return now_ms;
    if (now - t > static_cast<int64_t>(kMaxSampleAgeMs)) return now_ms;
    return static_cast<uint64_t>(t);
}
// The other mod's clock on this one's, kept from its timed events. Each event gives (its arrival here - the sender's
// clock when it was sent) = the clocks' difference plus that datagram's transit; ClockMapper keeps the smallest of the
// last seconds and moves towards it slowly. An event whose own send time would land more than kFutureMs after its
// arrival, or more than kMaxSampleAgeMs before it, does not fit the offset in use: the other game was started again
// (another clock), or the first events of this one came late; the offset is then taken anew from that event at once
// (the mapper alone would crawl there a millisecond per event, or hold the old minimum for seconds).
class SampleClock {
public:
    static constexpr int64_t kFutureMs = 50;
    // An event that the sender sent at sender_ms (its clock) and that arrived at now_ms (this clock).
    void on_event(uint64_t now_ms, uint64_t sender_ms) {
        mapper_.add(now_ms, sender_ms, static_cast<uint32_t>(now_ms));
        const int64_t at = static_cast<int64_t>(sender_ms) + mapper_.offset().value_or(0) - static_cast<int64_t>(now_ms);
        if (at > kFutureMs || at < -static_cast<int64_t>(kMaxSampleAgeMs)) {
            mapper_.reset();
            mapper_.add(now_ms, sender_ms, static_cast<uint32_t>(now_ms));
            ++resets_;
        }
    }
    // The time on this clock of a sample of the event just given to on_event (the same now_ms and sender_ms).
    uint64_t at(uint64_t now_ms, uint64_t sender_ms, uint16_t age_ms) const {
        const auto o = mapper_.offset();
        return o ? sample_time(now_ms, sender_ms, age_ms, *o) : now_ms;
    }
    uint64_t resets() const { return resets_; }
    void reset() { mapper_.reset(); }

private:
    ClockMapper mapper_;
    uint64_t resets_ = 0;
};

inline void to_sector(double world, int16_t hint, float* local, int16_t* sector) {
    const double l = world - hint * 1000.0;
    if (std::fabs(l) < 1000.0) {
        *local = static_cast<float>(l);
        *sector = hint;
        return;
    }
    const int s = static_cast<int>(world / 1000.0);
    *sector = static_cast<int16_t>(s);
    *local = static_cast<float>(world - s * 1000.0);
}

// ---------------------------------------------------------------- a passenger in the air keeps up with the ship
// Live, 2026-11-15 (3663, the user's own sail, the guest's copy behind at 5-7 frames a second, ship at 16-18 m/s): a
// character that stands on a deck part stays on its spot frame after frame, but in the frames in which the game has
// taken it off the part (a hop when the deck moves a lot between two frames, a step off a platform, a jump) the game
// carries it with the ship only sometimes: all the way in a copy that draws 30 frames a second (08:23:21), 74 %,
// 36 %, 0 % and 0 % of the ship's way in four frames of 150-230 ms (08:24:52: the guest's character was off the stern
// 1.1 s later). How much it was carried can be read: at its own position request the character's physics position
// is where the game has it BEFORE this frame's own movement, i.e. the request of the frame before plus what the game
// carried it since. So, for the client's OWN character, while it is in the air after leaving a part of a ship that
// another player's mod places here: its request is moved by what that carry LACKED, the difference between where its
// place of the frame before is now, riding the ship, and where its physics position is. Its own movement of the
// frame (the request minus the physics position: a jump, steering in the air, a dash) is not touched, and a frame
// in which the game carried it all the way is not touched either.
// air_carry is one frame's sum; AirRide is the whole of it, request by request (when the character is in the air,
// the ship's speed, what is still owed, when it is over), with no game memory in it so that the tests can run it.
// (Release b4 moved the request to where the take-off spot and the speed over the deck put it, along the straight
// line the spot had travelled: that undid the character's own movement along the ship's course and drifted sideways
// in a turn: 1.2 m in 1.4 s at 14 m/s and 10 degrees a second. Found by an independent review the same day, before
// it had been played.)

// A world position in the frame of `p` (the inverse of attach's position).
inline void to_local(const Pose& p, const double world[3], double out[3]) {
    const float inv[4] = {-p.q[0], -p.q[1], -p.q[2], p.q[3]};
    const float v[3] = {static_cast<float>(world[0] - p.x), static_cast<float>(world[1] - p.y),
                        static_cast<float>(world[2] - p.z)};
    rotate(inv, v, out);
}

inline constexpr double kAirMaxS = 1.5;        // a longer flight is not a hop on the deck: the game's own
// A ship slower than this (at its mooring it bobs a few centimetres a frame; adrift) is not kept up with: without a
// parent the character may as well be WALKING on the quay it stepped onto, and must not be pulled after the ship.
// The ship's speed is taken over kAirSpeedWindowMs at least, not over one frame: the character sees the ship one
// frame late, so one frame's way divided by that frame's length is wrong whenever two frames differ in length.
inline constexpr double kAirMinSpeed = 1.0;    // m/s
inline constexpr uint64_t kAirSpeedWindowMs = 250;
inline constexpr double kAirMaxSpeed = 60.0;   // "faster" than this between two readings: another ship, not a speed
// Lower than this in the ship's frame the character is at the water (the swimmer at the foot of the ladder is at
// -0.58, the lowest deck at 1.8: the records of 15-nov): it went overboard, and the game has it from there.
inline constexpr double kAirWaterlineY = 0.2;
// What the carry lacked is given once it adds up to this (a copy at 144 frames a second lacks millimetres a frame).
inline constexpr double kAirMinFixM = 0.03;
// The ship's way in one frame beyond which it did not sail there (put away and taken out again, a jump of its
// samples): the time in the air is over. 18 m/s at two and a half frames a second is 7.2 m.
// (15 since 2026-11-16: at 18 m/s a copy drawing two frames a second sees 9 m steps, and those are the frames the
// character needs it most. A ship put away and taken out again is hundreds of metres off.)
inline constexpr double kAirMaxStepM = 15.0;
// The carry cannot have lacked more than the ship's way in the frame (a little more: the game sometimes pushes the
// character back). Anything beyond is something else's doing and is not made up.
inline constexpr double kAirSlack = 1.25, kAirSlackM = 0.1;
// Without a parent the character can also be STANDING: on the quay or the shore it stepped onto from a ship under
// way. Its physics position then keeps its height: the same within kAirRestM for kAirRestMs and it is taken to
// stand on something firm, and is left to the game. (A free fall stays within 6 cm for 0.22 s at most, at the top
// of a jump.)
inline constexpr double kAirRestM = 0.03;
inline constexpr uint64_t kAirRestMs = 300;
// But a steady height is also the DECK, when the game has taken the character off its part and it stands there
// without a parent. Seen live on 2026-11-16 06:01 (the guest's copy behind at six frames a second, ship at 7.5 m/s):
// the character was held for one second (5 of 6 frames, 6.8 m of the ship's 7.7 m), two frames in a row had the same
// height, "standing on something firm" ended it and the ship left. Nobody steps onto a quay from a ship doing
// kAirFastSpeed or more: there a steady height does not end anything, and the limit is kAirMaxFastS (at six frames
// a second the game needs more than a second to give the character its part back).
inline constexpr double kAirFastSpeed = 1.5;  // m/s (5.0 at first: at 08:06 the guest was let go at 2.2 m/s, standing on the deck)
inline constexpr double kAirMaxFastS = 4.0;

// The time in the air is over for this module: too long, or down at the water (`asked_y`: the request's height in
// the ship's frame).
inline bool air_over(double t_s, double asked_y) {
    return !(t_s >= 0) || t_s > kAirMaxS || !(asked_y >= kAirWaterlineY);
}

enum class AirStep : uint8_t {
    None,  // nothing lacked in this frame
    Fix,   // `out`: what the game's carry lacked (world x and z)
    Over,  // the ship did not sail there, or a pose is not one: the time in the air is over
};
// One frame in the air. `prev_hull` / `prev_at`: the ship's pose and where the character was left (its request as it
// went to the game) at the frame before; `hull`: the ship's pose now; `body`: the character's physics position now
// (world; before this frame's own movement); `ship_speed`: the ship's speed, m/s (AirRide's, over a window).
inline AirStep air_carry(const Pose& prev_hull, const double prev_at[3], const Pose& hull, const double body[3],
                         double ship_speed, double out[2]) {
    out[0] = out[1] = 0;
    if (!finite_pose(prev_hull) || !finite_pose(hull) || !unit_quat(prev_hull.q) || !unit_quat(hull.q)) return AirStep::Over;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(prev_at[i]) || !std::isfinite(body[i])) return AirStep::Over;
    // Where its place of the frame before is now, riding the ship.
    double l[3];
    to_local(prev_hull, prev_at, l);
    const float lf[3] = {static_cast<float>(l[0]), static_cast<float>(l[1]), static_cast<float>(l[2])};
    const float unit[4] = {0, 0, 0, 1};
    const Pose ideal = attach(hull, lf, unit);
    const double step = std::hypot(ideal.x - prev_at[0], ideal.z - prev_at[2]);
    if (!std::isfinite(step) || step > kAirMaxStepM) return AirStep::Over;
    if (!(ship_speed >= kAirMinSpeed)) return AirStep::None;
    double lx = ideal.x - body[0], lz = ideal.z - body[2];
    const double lack = std::hypot(lx, lz);
    if (!std::isfinite(lack) || lack < 1e-4) return AirStep::None;
    const double most = step * kAirSlack + kAirSlackM;
    if (lack > most) {
        lx *= most / lack;
        lz *= most / lack;
    }
    out[0] = lx;
    out[1] = lz;
    return AirStep::Fix;
}

// One own position request of the client's own character, as the hook reads it.
struct AirFrame {
    uint64_t ms = 0;
    bool on_part = false;       // its hierarchy parent is a part of something (a 0xB key)
    bool other_parent = false;  // it has a parent that is not a part
    bool followed = false;      // on a part: of a ship another player's mod places here
    uint32_t part = 0, root = 0;  // the part, and the actor at the top of what it stands (or stood) on
    bool hull_ok = false, request_ok = false, body_ok = false;
    Pose hull;                  // that top actor's pose
    double request[3]{};        // the request, world
    double body[3]{};           // its physics position, world
};
enum class AirEnd : uint8_t { Landed, Time, Water, Rest, Parent, Unreadable, ShipJump };
inline const char* air_end_text(AirEnd e) {
    switch (e) {
        case AirEnd::Landed: return "stands on a part again";
        case AirEnd::Time: return "still without a part when the time was up";
        case AirEnd::Water: return "down at the water";
        case AirEnd::Rest: return "standing on something firm that is not the ship";
        case AirEnd::Parent: return "took another parent";
        case AirEnd::Unreadable: return "a pose could not be read";
        case AirEnd::ShipJump: return "the ship was somewhere else altogether";
    }
    return "?";
}
// One time in the air, when it is over.
struct AirDone {
    AirEnd why = AirEnd::Landed;
    uint32_t from = 0, to = 0, root = 0;  // the part left, the part landed on (Landed), the ship
    uint32_t frames = 0, fixed = 0, air_ms = 0;
    float total_fix = 0, max_fix = 0;     // metres the requests were moved (or would have been), in all, in one frame
    float ship_m = -1;                    // the way the ship went meanwhile (-1: landed on something else)
    float off = -1;                       // Landed on the same ship: metres across the deck from where it left it
    bool wrote = false;
};

// The own character on a ship another player's mod places here, request by request: when it is in the air after
// leaving a part, and what its request lacks. No game memory, no clock: the hook gives it what it read.
class AirRide {
public:
    static constexpr uint64_t kStaleMs = 1000;  // no request for this long (a load, the helm): nothing kept holds

    // One request. True: (add[0], add[1]) is to be added to the request's world x and z. `write`: the caller writes
    // it (false: it only counts, and the character is taken to be left where the game asked).
    bool frame(const AirFrame& f, bool write, double add[2]) {
        add[0] = add[1] = 0;
        if (last_ms_ && (f.ms < last_ms_ || f.ms - last_ms_ > kStaleMs)) {
            air_ = false;
            on_have_ = false;
            prev_on_part_ = false;
            speed_have_ = false;
            speed_ = 0;
        }
        last_ms_ = f.ms;
        track_speed(f);
        const bool was_on_part = prev_on_part_;
        prev_on_part_ = f.on_part;
        if (f.on_part) {
            if (air_) end(AirEnd::Landed, f);
            on_have_ = f.followed && f.hull_ok && f.request_ok;
            if (on_have_) {
                on_ms_ = f.ms;
                std::memcpy(on_at_, f.request, sizeof on_at_);
                to_local(f.hull, f.request, on_local_);
                on_hull_ = f.hull;
                on_part_ = f.part;
                on_root_ = f.root;
            }
            return false;
        }
        if (!air_ && was_on_part && on_have_ && f.ms - on_ms_ < kStaleMs) {
            // Taken off the part in this frame: from its last request on it.
            air_ = true;
            air_ms_ = prev_ms_ = on_ms_;
            air_hull_ = prev_hull_ = on_hull_;
            std::memcpy(air_local_, on_local_, sizeof air_local_);
            std::memcpy(prev_at_, on_at_, sizeof prev_at_);
            air_root_ = on_root_;
            owed_[0] = owed_[1] = 0;
            rest_have_ = false;
            cur_ = AirDone{};
            cur_.from = on_part_;
            cur_.root = on_root_;
        }
        on_have_ = false;
        if (!air_) return false;
        if (f.other_parent) return end(AirEnd::Parent, f);
        if (!f.hull_ok || !f.request_ok || !f.body_ok || f.root != air_root_) return end(AirEnd::Unreadable, f);
        double l[3];
        to_local(f.hull, f.request, l);
        const bool fast = speed_ >= kAirFastSpeed;
        if (f.ms - air_ms_ > static_cast<uint64_t>((fast ? kAirMaxFastS : kAirMaxS) * 1000.0)) return end(AirEnd::Time, f);
        if (!(l[1] >= kAirWaterlineY)) return end(AirEnd::Water, f);
        if (!rest_have_ || std::fabs(f.body[1] - rest_y_) > kAirRestM) {
            rest_have_ = true;
            rest_y_ = f.body[1];
            rest_ms_ = f.ms;
        } else if (!fast && f.ms - rest_ms_ >= kAirRestMs) {
            return end(AirEnd::Rest, f);
        }
        double lack[2];
        const AirStep step = air_carry(prev_hull_, prev_at_, f.hull, f.body, speed_, lack);
        if (step == AirStep::Over) return end(AirEnd::ShipJump, f);
        ++cur_.frames;
        double left[3] = {f.request[0], f.request[1], f.request[2]};
        bool fix = false;
        wrote_last_ = false;
        if (step == AirStep::Fix) {
            owed_[0] += lack[0];
            owed_[1] += lack[1];
            const double m = std::hypot(owed_[0], owed_[1]);
            if (m >= kAirMinFixM) {
                fix = true;
                add[0] = owed_[0];
                add[1] = owed_[1];
                owed_[0] = owed_[1] = 0;
                ++cur_.fixed;
                cur_.total_fix += static_cast<float>(m);
                cur_.max_fix = std::max(cur_.max_fix, static_cast<float>(m));
                if (write) {
                    left[0] += add[0];
                    left[2] += add[1];
                    cur_.wrote = true;
                    wrote_last_ = true;
                    last_add_[0] = add[0];
                    last_add_[1] = add[1];
                }
            }
        }
        prev_hull_ = f.hull;
        std::memcpy(prev_at_, left, sizeof prev_at_);
        prev_ms_ = f.ms;
        return fix;
    }
    // The write of the last frame's fix did not happen: the character was left where the game asked.
    void unwritten() {
        if (!wrote_last_) return;
        wrote_last_ = false;
        prev_at_[0] -= last_add_[0];
        prev_at_[2] -= last_add_[1];
    }
    bool in_air() const { return air_; }
    double ship_speed() const { return speed_; }
    // The time in the air that ended in the last frame(), once.
    bool done(AirDone* out) {
        if (!done_have_) return false;
        done_have_ = false;
        *out = done_;
        return true;
    }

private:
    void track_speed(const AirFrame& f) {
        if (!f.hull_ok || !finite_pose(f.hull)) {
            speed_have_ = false;
            speed_ = 0;
            return;
        }
        if (!speed_have_ || f.root != speed_root_) {
            speed_have_ = true;
            speed_root_ = f.root;
            speed_ref_ = f.hull;
            speed_ms_ = f.ms;
            speed_ = 0;
            return;
        }
        if (f.ms - speed_ms_ < kAirSpeedWindowMs) return;
        const double v = std::hypot(f.hull.x - speed_ref_.x, f.hull.z - speed_ref_.z) * 1000.0 /
                         static_cast<double>(f.ms - speed_ms_);
        speed_ = std::isfinite(v) && v <= kAirMaxSpeed ? v : 0;
        speed_ref_ = f.hull;
        speed_ms_ = f.ms;
    }
    bool end(AirEnd why, const AirFrame& f) {
        air_ = false;
        cur_.why = why;
        cur_.air_ms = f.ms > air_ms_ ? static_cast<uint32_t>(std::min<uint64_t>(f.ms - air_ms_, 600000)) : 0;
        if (f.hull_ok && f.root == air_root_) {
            cur_.ship_m = static_cast<float>(std::hypot(f.hull.x - air_hull_.x, f.hull.z - air_hull_.z));
            if (why == AirEnd::Landed && f.request_ok) {
                double l[3];
                to_local(f.hull, f.request, l);
                cur_.off = static_cast<float>(std::hypot(l[0] - air_local_[0], l[2] - air_local_[2]));
            }
        }
        if (why == AirEnd::Landed) cur_.to = f.part;
        done_ = cur_;
        done_have_ = true;
        return false;
    }

    uint64_t last_ms_ = 0;
    bool prev_on_part_ = false;
    // The ship's speed over a window.
    bool speed_have_ = false;
    uint32_t speed_root_ = 0;
    Pose speed_ref_;
    uint64_t speed_ms_ = 0;
    double speed_ = 0;
    // Its last request on a part of a followed ship.
    bool on_have_ = false;
    uint64_t on_ms_ = 0;
    double on_at_[3]{}, on_local_[3]{};
    Pose on_hull_;
    uint32_t on_part_ = 0, on_root_ = 0;
    // In the air.
    bool air_ = false;
    uint64_t air_ms_ = 0, prev_ms_ = 0;
    Pose air_hull_, prev_hull_;
    double air_local_[3]{}, prev_at_[3]{};
    uint32_t air_root_ = 0;
    double owed_[2]{};         // what lacked and has not been given yet (too little so far)
    bool rest_have_ = false;
    double rest_y_ = 0;
    uint64_t rest_ms_ = 0;
    bool wrote_last_ = false;
    double last_add_[2]{};
    AirDone cur_, done_;
    bool done_have_ = false;
};

}  // namespace cdmp::session2::vfl
