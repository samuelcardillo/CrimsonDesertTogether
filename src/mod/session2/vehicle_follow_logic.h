#pragma once
// vehicle_follow (vehicle_follow.h): the pure part. Build 1.0.0.3576, docs/ESTADO-3527.md "13-nov: BARCO".
//
// Each client simulates its own copy of a ship. The other player's ship is drawn from its hierarchy parent, the vehicle
// actor (type 5), which every frame gets a TransformSync request of type 7 with the local ship's pose. The vehicle
// actor's last network pose (the controller's, ~1 Hz) sits in its TransformSync at +0x550 (world vec3) / +0x55C (quat).
// vehicle_follow replaces that request's pose with one built from those network samples (interpolated, extrapolated
// past the newest for up to a second), so the ship and everything attached to it is drawn where the controller has it.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

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

    // A sample read now; kept only when it differs from the newest one. True when added.
    bool push(uint64_t t_ms, const Pose& p) {
        if (!usable(p)) return false;
        if (n_) {
            const Pose& last = s_[(head_ + n_ - 1) % kCap].p;
            const bool same = std::fabs(last.x - p.x) < 1e-3 && std::fabs(last.y - p.y) < 1e-3 &&
                              std::fabs(last.z - p.z) < 1e-3 && std::fabs(last.q[0] - p.q[0]) < 1e-5f &&
                              std::fabs(last.q[1] - p.q[1]) < 1e-5f && std::fabs(last.q[2] - p.q[2]) < 1e-5f &&
                              std::fabs(last.q[3] - p.q[3]) < 1e-5f;
            if (same) return false;
            if (t_ms <= s_[(head_ + n_ - 1) % kCap].t) t_ms = s_[(head_ + n_ - 1) % kCap].t + 1;
        }
        if (n_ == kCap) {
            head_ = (head_ + 1) % kCap;
            --n_;
        }
        s_[(head_ + n_) % kCap] = {t_ms, p};
        ++n_;
        return true;
    }
    size_t size() const { return n_; }
    void clear() { n_ = head_ = 0; }

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
    std::array<Sample, kCap> s_{};
    size_t n_ = 0, head_ = 0;
    uint64_t delay_ms_ = kDefaultDelayMs;
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

}  // namespace cdmp::session2::vfl
