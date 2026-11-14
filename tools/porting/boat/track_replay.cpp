// track_replay.cpp: what the follower's Track (src/mod/session2/vehicle_follow_logic.h) draws for a vehicle, fed two
// ways with the same real motion: (A) the game's own position updates as the guest received them (about one a
// second: the output of movedump.py for that vehicle on the guest's capture), and (B) the mod's own channel
// (proto::EventKind::Vehicles): the same path sampled ten times a second, arriving 20 ms later with +-10 ms of jitter.
// The "true" path between two updates is not in any capture: it is taken as a smooth curve through them (a cubic
// with finite-difference tangents), so (B) is a model; (A) is the real arrival times and positions.
// Every 1/60 s it compares what is drawn with the true pose one delay (300 ms) earlier, and counts the frames in
// which the drawn ship goes BACK along its course or stands still while the real one moves.
//   track_replay moves.txt [delay_ms]
// Build (x64 Native Tools prompt of VS 2022, from the repository's root):
//   cl /nologo /std:c++20 /EHsc /O2 /W4 /Isrc tools\porting\boat\track_replay.cpp /Fe:build\track_replay.exe
#define _CRT_SECURE_NO_WARNINGS  // sscanf on lines this tool's own companion script wrote
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "mod/session2/vehicle_follow_logic.h"

using namespace cdmp::session2::vfl;

namespace {
struct Upd {
    double t, x, y, z;
};
std::vector<Upd> g_u;
std::vector<double> g_mx, g_my, g_mz;  // tangents (per second)

// The smooth path through the updates at time t (seconds, the capture's clock).
void truth(double t, double* x, double* y, double* z) {
    if (t <= g_u.front().t) {
        *x = g_u.front().x, *y = g_u.front().y, *z = g_u.front().z;
        return;
    }
    if (t >= g_u.back().t) {
        *x = g_u.back().x, *y = g_u.back().y, *z = g_u.back().z;
        return;
    }
    size_t i = static_cast<size_t>(std::upper_bound(g_u.begin(), g_u.end(), t, [](double v, const Upd& u) { return v < u.t; }) -
                                   g_u.begin()) - 1;
    const Upd& a = g_u[i];
    const Upd& b = g_u[i + 1];
    const double h = b.t - a.t, s = (t - a.t) / h;
    const double h00 = 2 * s * s * s - 3 * s * s + 1, h10 = s * s * s - 2 * s * s + s, h01 = -2 * s * s * s + 3 * s * s,
                 h11 = s * s * s - s * s;
    *x = h00 * a.x + h10 * h * g_mx[i] + h01 * b.x + h11 * h * g_mx[i + 1];
    *y = h00 * a.y + h10 * h * g_my[i] + h01 * b.y + h11 * h * g_my[i + 1];
    *z = h00 * a.z + h10 * h * g_mz[i] + h01 * b.z + h11 * h * g_mz[i + 1];
}
Pose pose_at(double x, double y, double z) {
    Pose p;
    p.x = x, p.y = y, p.z = z;
    return p;
}
struct Result {
    size_t frames = 0, back = 0, still = 0;
    double err_sum = 0, err_max = 0, back_max = 0, step_max_ratio = 0;
    std::vector<double> errs;
};
// feed: (arrival time ms, pose) in order.
Result run(const std::vector<std::pair<uint64_t, Pose>>& feed, uint64_t delay_ms, double t0, double t1) {
    Track track(delay_ms);
    Result r;
    size_t next = 0;
    Pose prev{};
    bool have_prev = false;
    const double dt = 1.0 / 60.0;
    for (double t = t0; t < t1; t += dt) {
        const auto now = static_cast<uint64_t>(t * 1000.0);
        while (next < feed.size() && feed[next].first <= now) {
            track.push(feed[next].first, feed[next].second);
            ++next;
        }
        Pose out;
        if (!track.at(now, &out)) continue;
        double tx, ty, tz, px, py, pz;
        truth(t - delay_ms / 1000.0, &tx, &ty, &tz);
        truth(t - delay_ms / 1000.0 - dt, &px, &py, &pz);
        const double true_step = std::hypot(tx - px, tz - pz);
        if (true_step / dt < 2.0) {  // only while the real ship moves at 2 m/s or more
            have_prev = false;
            continue;
        }
        const double err = std::hypot(out.x - tx, out.z - tz);
        ++r.frames;
        r.err_sum += err;
        r.err_max = std::max(r.err_max, err);
        r.errs.push_back(err);
        if (have_prev) {
            // The drawn step, measured along the real course.
            const double ux = (tx - px) / true_step, uz = (tz - pz) / true_step;
            const double along = (out.x - prev.x) * ux + (out.z - prev.z) * uz;
            if (along < -0.05) {
                ++r.back;
                r.back_max = std::max(r.back_max, -along);
            } else if (along < 0.1 * true_step) {
                ++r.still;
            }
            r.step_max_ratio = std::max(r.step_max_ratio, along / true_step);
        }
        prev = out;
        have_prev = true;
    }
    return r;
}
void print(const char* name, Result& r) {
    if (!r.frames) {
        std::printf("%s: the vehicle never moved at 2 m/s or more\n", name);
        return;
    }
    std::sort(r.errs.begin(), r.errs.end());
    std::printf("%s: %zu frames at 60 fps while it moved (%.0f s): off its true place by %.2f m on average, %.2f m "
                "at the 95th percentile, %.2f m at most | went BACK along its course in %zu frames (%.1f a minute, "
                "up to %.2f m in one frame) | stood still in %zu frames (%.1f s) | largest forward step %.1f times "
                "the real one\n",
                name, r.frames, r.frames / 60.0, r.err_sum / r.frames, r.errs[r.errs.size() * 95 / 100], r.err_max,
                r.back, r.back * 3600.0 / r.frames, r.back_max, r.still, r.still / 60.0, r.step_max_ratio);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const uint64_t delay = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : Track::kDefaultDelayMs;
    std::ifstream in(argv[1]);
    std::string line;
    while (std::getline(in, line)) {
        // t=  1765.215 B010157F parent     none local ( -9991.17,  353.17, -6667.79) world (...)
        Upd u{};
        const size_t l = line.find("local (");
        if (line.rfind("t=", 0) != 0 || l == std::string::npos) continue;
        if (std::sscanf(line.c_str(), "t=%lf", &u.t) != 1) continue;
        if (std::sscanf(line.c_str() + l, "local (%lf,%lf,%lf)", &u.x, &u.y, &u.z) != 3) continue;
        if (u.x == 0 && u.z == 0) continue;
        if (!g_u.empty() && u.t <= g_u.back().t) continue;
        g_u.push_back(u);
    }
    if (g_u.size() < 4) {
        std::printf("%s: fewer than four position updates\n", argv[1]);
        return 1;
    }
    const size_t n = g_u.size();
    g_mx.resize(n), g_my.resize(n), g_mz.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const Upd& a = g_u[i ? i - 1 : 0];
        const Upd& b = g_u[i + 1 < n ? i + 1 : n - 1];
        const double h = std::max(b.t - a.t, 1e-3);
        g_mx[i] = (b.x - a.x) / h, g_my[i] = (b.y - a.y) / h, g_mz[i] = (b.z - a.z) / h;
    }
    size_t gaps = 0;
    for (size_t i = 1; i < n; ++i) gaps += g_u[i].t - g_u[i - 1].t > 1.5;
    std::printf("%s: %zu position updates over %.0f s; %zu of the %zu gaps between them are over 1.5 s; delay %llu ms\n",
                argv[1], n, g_u.back().t - g_u.front().t, gaps, n - 1, static_cast<unsigned long long>(delay));

    // (A) the game's updates, as they arrived.
    std::vector<std::pair<uint64_t, Pose>> a;
    for (const Upd& u : g_u) a.emplace_back(static_cast<uint64_t>(u.t * 1000.0), pose_at(u.x, u.y, u.z));
    // (B) the mod's channel: the path ten times a second, 20 ms later, +-10 ms of jitter (a fixed pattern).
    std::vector<std::pair<uint64_t, Pose>> b;
    uint32_t seed = 12345;
    for (double t = g_u.front().t; t < g_u.back().t; t += 0.1) {
        seed = seed * 1664525u + 1013904223u;
        const double jitter = ((seed >> 16) % 21) - 10.0;
        double x, y, z;
        truth(t, &x, &y, &z);
        b.emplace_back(static_cast<uint64_t>(t * 1000.0 + 20.0 + jitter), pose_at(x, y, z));
    }
    std::sort(b.begin(), b.end(), [](const auto& p, const auto& q) { return p.first < q.first; });
    Result ra = run(a, delay, g_u.front().t + 2.0, g_u.back().t);
    Result rb = run(b, delay, g_u.front().t + 2.0, g_u.back().t);
    print("  (A) the game's updates (what b1 and the build of 16:33 draw)", ra);
    print("  (B) the mod's ten a second (release b2)                      ", rb);
    return 0;
}
