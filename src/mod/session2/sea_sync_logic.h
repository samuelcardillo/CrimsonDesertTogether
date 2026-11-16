#pragma once
// [session2] sea_sync = 1 (default) | 0, both PCs, build 1.0.0.3663 (session2/sea_sync.h). Pure logic and wire format,
// no game access.
//
// Why (docs/research/mar-3663.md): the swell of the sea is twelve wave trains (lengths 280 m to 21 m). Each has a
// phase that grows with time, a direction (the wind's, taken when the train was last started again), an origin (where
// the camera was then) and a strength that fades out and back in around each restart; one train is restarted at a
// time, all twelve in about two minutes. All of that is kept by each client on its own, so two clients never have the
// same sea, while a ship's pose is the host's: on the guest the host's ship rides waves that are not there and its own
// waves go through the deck. So the host tells the guest its twelve trains, and the guest draws those.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace cdmp::session2::ssl {

inline constexpr uint16_t kControlOpcode = 1;  // troc_frame.h kControlOpcode
inline constexpr uint8_t kControlSea = 17;     // troc_frame.h: 1-16 taken
inline constexpr uint8_t kVersion = 1;
inline constexpr size_t kHeader = 5;           // u16 opcode | i8 slot | u16 len
inline constexpr size_t kBands = 12;
inline constexpr size_t kBandWords = 5;
// u8 state | u8 version | u16 0 | u32 seq | u64 host_us | f32 wind | 12 x (phase, fade, dir, origin x, origin z)
inline constexpr size_t kPayloadSize = 4 + 4 + 8 + 4 + kBands * kBandWords * 4;

// The game's record of one train (0x20 bytes; 12 of them make one frame of the sea's history).
inline constexpr size_t kEntrySize = 0x20, kFrameSize = kBands * kEntrySize;
inline constexpr size_t kEntryPhase = 0x00, kEntryFade = 0x04, kEntryDir = 0x08, kEntryOx = 0x10, kEntryOz = 0x14;

inline constexpr uint64_t kSendEveryUs = 100000;  // the host: 10 Hz, and only a frame it has not sent
inline constexpr uint64_t kStaleUs = 1500000;     // the guest: no sample for this long, its own sea goes on
inline constexpr double kMaxAheadUs = 1500000.0;  // never carried further than this past a sample
inline constexpr double kMaxRate = 20.0e-6;       // phase per microsecond (the fastest train: 2.3 / s)
inline constexpr double kMaxFadeRate = 5.0e-6;    // strength per microsecond (the shortest fade: about 1 / s)
inline constexpr uint32_t kStillSamples = 3;      // the host's sea stands still after this many unchanged samples
inline constexpr double kOffsetRiseUs = 200.0;    // the clocks' difference may grow this much per sample (2 ms/s)

struct Band {
    float phase = 0, fade = 0, dir = 0, ox = 0, oz = 0;
    constexpr bool operator==(const Band&) const = default;
};

struct Sample {
    uint32_t seq = 0;
    uint64_t host_us = 0;  // the host's clock when its frame had these values
    float wind = 0;        // the wind's strength for the swell, 0..1
    std::array<Band, kBands> band{};
    constexpr bool operator==(const Sample&) const = default;
};

inline bool finite_in(float v, float lo, float hi) noexcept { return std::isfinite(v) && v >= lo && v <= hi; }

inline bool plausible(const Band& b) noexcept {
    return finite_in(b.phase, -1.0e7f, 1.0e7f) && finite_in(b.fade, -0.01f, 1.01f) && finite_in(b.dir, -1.0e4f, 1.0e4f) &&
           finite_in(b.ox, -1.0e6f, 1.0e6f) && finite_in(b.oz, -1.0e6f, 1.0e6f);
}
inline bool plausible(const Sample& s) noexcept {
    if (!finite_in(s.wind, 0.0f, 1.0001f)) return false;
    for (const Band& b : s.band) {
        if (!plausible(b)) return false;
    }
    return true;
}

// One frame of the game's history (12 records) <-> the bands.
inline void from_entries(const uint8_t* frame, std::array<Band, kBands>& out) noexcept {
    for (size_t i = 0; i < kBands; ++i) {
        const uint8_t* e = frame + i * kEntrySize;
        std::memcpy(&out[i].phase, e + kEntryPhase, 4);
        std::memcpy(&out[i].fade, e + kEntryFade, 4);
        std::memcpy(&out[i].dir, e + kEntryDir, 4);
        std::memcpy(&out[i].ox, e + kEntryOx, 4);
        std::memcpy(&out[i].oz, e + kEntryOz, 4);
    }
}
// Only the five words that are the train: the rest of a record stays as the game has it.
inline void to_entries(const std::array<Band, kBands>& in, uint8_t* frame) noexcept {
    for (size_t i = 0; i < kBands; ++i) {
        uint8_t* e = frame + i * kEntrySize;
        std::memcpy(e + kEntryPhase, &in[i].phase, 4);
        std::memcpy(e + kEntryFade, &in[i].fade, 4);
        std::memcpy(e + kEntryDir, &in[i].dir, 4);
        std::memcpy(e + kEntryOx, &in[i].ox, 4);
        std::memcpy(e + kEntryOz, &in[i].oz, 4);
    }
}

namespace detail {
inline void put32(uint8_t* p, uint32_t v) noexcept {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
inline uint32_t get32(const uint8_t* p) noexcept {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline void putf(uint8_t* p, float f) noexcept {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    put32(p, v);
}
inline float getf(const uint8_t* p) noexcept {
    const uint32_t v = get32(p);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
}  // namespace detail

inline std::vector<uint8_t> frame(const Sample& s) {
    std::vector<uint8_t> out(kHeader + kPayloadSize, 0);
    out[0] = kControlOpcode & 0xFF;
    out[1] = kControlOpcode >> 8;
    out[2] = 0xFF;
    out[3] = kPayloadSize & 0xFF;
    out[4] = kPayloadSize >> 8;
    uint8_t* p = out.data() + kHeader;
    p[0] = kControlSea;
    p[1] = kVersion;
    detail::put32(p + 4, s.seq);
    detail::put32(p + 8, static_cast<uint32_t>(s.host_us));
    detail::put32(p + 12, static_cast<uint32_t>(s.host_us >> 32));
    detail::putf(p + 16, s.wind);
    for (size_t i = 0; i < kBands; ++i) {
        uint8_t* b = p + 20 + i * kBandWords * 4;
        detail::putf(b + 0, s.band[i].phase);
        detail::putf(b + 4, s.band[i].fade);
        detail::putf(b + 8, s.band[i].dir);
        detail::putf(b + 12, s.band[i].ox);
        detail::putf(b + 16, s.band[i].oz);
    }
    return out;
}

inline bool is_frame(std::span<const uint8_t> f) noexcept {
    return f.size() == kHeader + kPayloadSize && f[0] == (kControlOpcode & 0xFF) && f[1] == (kControlOpcode >> 8) &&
           f[2] == 0xFF && f[3] == (kPayloadSize & 0xFF) && f[4] == (kPayloadSize >> 8) && f[kHeader] == kControlSea;
}

inline std::optional<Sample> decode(std::span<const uint8_t> f) noexcept {
    if (!is_frame(f) || f[kHeader + 1] != kVersion || f[kHeader + 2] != 0 || f[kHeader + 3] != 0) return std::nullopt;
    const uint8_t* p = f.data() + kHeader;
    Sample s;
    s.seq = detail::get32(p + 4);
    s.host_us = detail::get32(p + 8) | (static_cast<uint64_t>(detail::get32(p + 12)) << 32);
    s.wind = detail::getf(p + 16);
    for (size_t i = 0; i < kBands; ++i) {
        const uint8_t* b = p + 20 + i * kBandWords * 4;
        s.band[i] = Band{detail::getf(b + 0), detail::getf(b + 4), detail::getf(b + 8), detail::getf(b + 12),
                         detail::getf(b + 16)};
    }
    if (!plausible(s)) return std::nullopt;
    return s;
}

// The guest's copy of the host's sea: the last sample, how fast each train's phase and strength were changing, and
// how the two PCs' clocks stand to each other. `at` carries the sample forward to the frame being drawn.
class Mirror {
public:
    void reset() noexcept { *this = Mirror{}; }

    // A sample from the host, taken in at recv_us (this PC's clock). False: not kept (older than the one held).
    bool on_sample(const Sample& s, uint64_t recv_us) noexcept {
        if (has_) {
            if (s.seq == cur_.seq) return false;
            // An older one that arrived late; a much smaller number is a host that started again.
            if (s.seq < cur_.seq && cur_.seq - s.seq < 100000) return false;
            if (s.seq < cur_.seq) reset();
        }
        // The two clocks: a sample's "taken in" minus its "taken" is their difference plus the time it travelled. The
        // quickest one seen is the nearest to the difference itself (a guest whose network loop is busy takes samples
        // in half a second late: the sea must not be drawn half a second behind for that), and the estimate may only
        // creep up (clocks drift, a route changes).
        const double d = static_cast<double>(recv_us) - static_cast<double>(s.host_us);
        if (!has_ || d < offset_us_) {
            offset_us_ = d;
        } else {
            offset_us_ += d - offset_us_ < kOffsetRiseUs ? d - offset_us_ : kOffsetRiseUs;
        }
        if (has_ && s.host_us > cur_.host_us && s.host_us - cur_.host_us <= 3000000) {
            const double dt = static_cast<double>(s.host_us - cur_.host_us);
            bool moved = false;
            for (size_t i = 0; i < kBands; ++i) {
                const double dp = static_cast<double>(s.band[i].phase) - static_cast<double>(cur_.band[i].phase);
                if (dp != 0.0) moved = true;
                if (dp >= 0.0 && dp / dt <= kMaxRate) {
                    if (dp > 0.0) {  // a sea that stands still says nothing about its speed
                        const double r = dp / dt;
                        rate_[i] = rate_known_[i] ? rate_[i] + (r - rate_[i]) * 0.25 : r;
                        rate_known_[i] = true;
                    }
                    double fr = (static_cast<double>(s.band[i].fade) - static_cast<double>(cur_.band[i].fade)) / dt;
                    if (fr > kMaxFadeRate) fr = kMaxFadeRate;
                    if (fr < -kMaxFadeRate) fr = -kMaxFadeRate;
                    fade_rate_[i] = fr;
                } else {
                    fade_rate_[i] = 0.0;  // started again between the two samples: its strength is at its lowest
                }
            }
            if (moved) {
                still_ = 0;
                moving_ = true;
            } else if (++still_ >= kStillSamples) {
                moving_ = false;
            }
        } else {
            fade_rate_.fill(0.0);  // the first one, or one after a gap: nothing to carry the strength with
        }
        cur_ = s;
        cur_recv_us_ = recv_us;
        has_ = true;
        return true;
    }

    // The host's trains at now_us (this PC's clock). False: nothing to write now (no sample yet, the last one is too
    // old, or the host's sea stands still: a paused or hidden game), and this client's own sea goes on by itself.
    bool at(uint64_t now_us, std::array<Band, kBands>& out, float& wind) const noexcept {
        if (!has_ || !moving_) return false;
        if (now_us < cur_recv_us_ || now_us - cur_recv_us_ > kStaleUs) return false;
        double ahead = static_cast<double>(now_us) - offset_us_ - static_cast<double>(cur_.host_us);
        if (ahead < 0.0) ahead = 0.0;
        if (ahead > kMaxAheadUs) ahead = kMaxAheadUs;
        for (size_t i = 0; i < kBands; ++i) {
            Band b = cur_.band[i];
            if (rate_known_[i]) b.phase = static_cast<float>(static_cast<double>(b.phase) + rate_[i] * ahead);
            double fade = static_cast<double>(b.fade) + fade_rate_[i] * ahead;
            if (fade < 0.0) fade = 0.0;
            if (fade > 1.0) fade = 1.0;
            b.fade = static_cast<float>(fade);
            out[i] = b;
        }
        wind = cur_.wind;
        return true;
    }

    bool has() const noexcept { return has_; }
    bool moving() const noexcept { return moving_; }
    const Sample& last() const noexcept { return cur_; }
    double offset_us() const noexcept { return offset_us_; }
    double rate(size_t i) const noexcept { return rate_known_[i] ? rate_[i] : 0.0; }

private:
    bool has_ = false, moving_ = false;
    uint32_t still_ = 0;
    Sample cur_{};
    uint64_t cur_recv_us_ = 0;
    double offset_us_ = 0.0;
    std::array<double, kBands> rate_{}, fade_rate_{};
    std::array<bool, kBands> rate_known_{};
};

// How far two seas are apart, for the log: the largest difference of phase (as an angle, 0..pi) and of direction.
struct Apart {
    float phase = 0, dir = 0, origin = 0;
};
inline Apart apart(const std::array<Band, kBands>& a, const std::array<Band, kBands>& b) noexcept {
    Apart out;
    for (size_t i = 0; i < kBands; ++i) {
        const float two_pi = 6.2831853f;
        float dp = std::fmod(std::fabs(a[i].phase - b[i].phase), two_pi);
        if (dp > two_pi / 2) dp = two_pi - dp;
        if (dp > out.phase) out.phase = dp;
        const float dd = std::fabs(a[i].dir - b[i].dir);
        if (dd > out.dir) out.dir = dd;
        const float dx = a[i].ox - b[i].ox, dz = a[i].oz - b[i].oz;
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d > out.origin) out.origin = d;
    }
    return out;
}

}  // namespace cdmp::session2::ssl
