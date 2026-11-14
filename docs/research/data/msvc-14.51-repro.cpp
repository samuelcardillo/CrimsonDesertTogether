// Wrong code from MSVC 14.51.36231 (cl / c2.dll 19.51.36260, x64, Visual Studio 2026 18.11) at /O2.
// docs/research/msvc-14.51-miscompila.md. From a "x64 Native Tools" prompt of the compiler to check:
//
//   cl /nologo /std:c++20 /EHsc /O2 msvc-14.51-repro.cpp && msvc-14.51-repro
//
// 19.51.36260 prints "FAIL ... MISCOMPILED" with /O2, /O2 /Ob1 and /Ox, and "ok" with /Od, /O1, /O2 /Ob0 and
// /O2 /d2SSAOptimizer-. MSVC 14.44.35207 (cl 19.44.35229, Visual Studio 2022) prints "ok" with all of them.
//
// parse_slot_dir is src/mod/session2/guest_save_wire.h's (there `inline`; `constexpr` here only for the
// static_assert) and the CHECK is tests/optd_guest_save_b_tests.cpp:708. The static_assert is the same expression:
// the compiler's own constant evaluation, which rejects undefined behaviour, finds it true. In test() the second
// inlined copy reads name[0] ('s') where the source reads name[4] ('1'), takes the "not a digit" path and compares
// the `kind` byte of an optional that was never constructed.
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>
#include <vector>

enum class DirKind : uint8_t { Plain, New, Backup };
struct SlotDir {
    uint32_t slot = 0;
    DirKind kind = DirKind::Plain;
};
constexpr std::optional<SlotDir> parse_slot_dir(std::wstring_view name) {
    if (name.size() < 5 || name.substr(0, 4) != L"slot") return std::nullopt;
    size_t i = 4;
    uint64_t n = 0;
    while (i < name.size() && name[i] >= L'0' && name[i] <= L'9' && i < 4 + 4) n = n * 10 + (name[i++] - L'0');
    if (i == 4 || (name[4] == L'0' && i > 5)) return std::nullopt;
    const std::wstring_view rest = name.substr(i);
    if (rest.empty()) return SlotDir{static_cast<uint32_t>(n), DirKind::Plain};
    if (rest == L"_new") return SlotDir{static_cast<uint32_t>(n), DirKind::New};
    if (rest == L"_backup") return SlotDir{static_cast<uint32_t>(n), DirKind::Backup};
    return std::nullopt;
}
static_assert(parse_slot_dir(L"slot1")->slot == 1 && parse_slot_dir(L"slot1")->kind == DirKind::Plain);

// Leaves 0xA5 where test()'s temporaries will be, so the wrong path shows whatever the stack held before.
__declspec(noinline) void dirty_stack() {
    volatile unsigned char junk[4096];
    for (size_t i = 0; i < sizeof(junk); ++i) junk[i] = 0xA5;
}

int g_failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

// The shape matters: without the vector (any local with a destructor), or with the result kept in a `bool` and
// returned instead of this CHECK, 19.51.36260 generates the right code.
__declspec(noinline) void test() {
    CHECK(parse_slot_dir(L"slot1")->slot == 1 && parse_slot_dir(L"slot1")->kind == DirKind::Plain);
    std::vector<int> v = {1, 2, 3};
    std::printf("%d\n", v[1]);
}

int main() {
    dirty_stack();
    test();
    std::puts(g_failures ? "MISCOMPILED" : "ok");
    return g_failures != 0;
}
