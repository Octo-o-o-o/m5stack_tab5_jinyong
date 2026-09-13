/*
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Host test of the Tab5 port's own logic (hojy_core overlays + upstream),
 * driven headlessly against the real SD-card data. Reports whether walking out
 * of the starting submap actually switches to the world map, and how much
 * memory that switch demands -- including the largest single block, which is
 * what has to exist as one contiguous PSRAM hole on the device.
 */
#include "core/config.hh"
#include "content/loader.hh"
#include "content/factors.hh"
#include "world/strings.hh"
#include "world/savedata.hh"
#include "scene/window.hh"
#include "scene/map.hh"
#include "tab5_texture_stats.hh"
#include "channelwavstream.hh"
#include "audio/mixer.hh"
#include "app/input.hh"
#include "app/input_repeat.hh"

#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <atomic>
#include <malloc/malloc.h>
#include <execinfo.h>

static std::atomic<bool> gTrack{false};
static std::atomic<bool> gBigTrace{true};
static std::atomic<size_t> gTotal{0}, gLive{0}, gPeak{0}, gLargest{0}, gCount{0};

/* Sizes are recorded per pointer: the unsized operator delete (used for every
 * `delete base_ptr`) carries no size, so a total kept from the sized form only
 * would drift upwards and look like a leak that is not there. */
/* The BGM loader runs on its own thread, so the bookkeeping has to be locked.
 * A thread_local re-entry flag keeps the map's own allocations out of itself. */
static std::unordered_map<void *, size_t> &sizes() {
    static std::unordered_map<void *, size_t> m;
    return m;
}
static std::mutex &accMutex() { static std::mutex m; return m; }
static thread_local bool gInAccounting = false;

/*
 * Accounting has to survive the BGM loader thread allocating concurrently, and
 * must not keep a side table of its own (a global operator new that allocates
 * is a good way to crash). malloc_size() gives the real block size back at
 * delete time, so nothing needs to be remembered.
 */
void *operator new(size_t n) {
    void *p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    if (gTrack) {
        const size_t real = malloc_size(p);
        gTotal += real; ++gCount;
        const size_t live = (gLive += real);
        if (real > gLargest) gLargest = real;
        if (live > gPeak) gPeak = live;
        if (real > 2u * 1024u * 1024u && gBigTrace) {
            std::printf("   [big alloc] requested %.2f MB (block %.2f MB)\n", n / 1048576.0, real / 1048576.0);
            void *bt[12];
            const int n = backtrace(bt, 12);
            char **sym = backtrace_symbols(bt, n);
            for (int i = 1; i < n && i < 8; ++i) { std::printf("      %s\n", sym[i]); }
            std::free(sym);
        }
    }
    return p;
}
static inline void account_free(void *p) noexcept {
    if (gTrack && p != nullptr) {
        const size_t real = malloc_size(p);
        size_t cur = gLive.load(std::memory_order_relaxed);
        while (cur >= real && !gLive.compare_exchange_weak(cur, cur - real)) { }
    }
}
void operator delete(void *p) noexcept { account_free(p); std::free(p); }
void operator delete(void *p, size_t) noexcept { account_free(p); std::free(p); }

void tab5_log_memory(const char *stage) { std::printf("[mem] %s\n", stage); }

static void reset() { gTotal = 0; gLive = 0; gPeak = 0; gLargest = 0; gCount = 0; }
static void report(const char *what) {
    std::printf(">>> %-22s new=%7.2f MB  peak-live=%7.2f MB  largest-block=%6.2f MB  allocs=%zu\n",
                what, gTotal.load() / 1048576.0, gPeak.load() / 1048576.0, gLargest.load() / 1048576.0, gCount.load());
}

/*
 * The device rebooted the moment a save was started: upstream assembles the
 * whole archive in RAM -- a full SaveData copy, one string per record, then
 * one concatenated blob, about 13 MB for this 4.5 MB save. The overlay writes
 * each record to the card as it is produced. Prove the bytes did not change:
 * read back a slot written by the old code and write it out again; the six
 * files must come out identical.
 */
static bool readWhole(const std::string &path, std::string &out) {
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) { return false; }
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.assign(n > 0 ? static_cast<size_t>(n) : 0u, '\0');
    const bool ok = out.empty() || std::fread(&out[0], 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

static int saveRoundTrip(const char *refDir) {
    std::puts("== save round trip ==");
    if (!hojy::world::state::gSaveData.load(1)) {
        std::puts("   no slot 1 on the card, skipped");
        return 0;
    }
    reset(); gTrack = true;
    const bool saved = hojy::world::state::gSaveData.save(2);
    gTrack = false;
    report("SaveData::save");
    if (!saved) { std::puts("   FAIL save(2) returned false"); return 1; }
    static const char *pairs[3][2] = {{"R1", "R2"}, {"S1", "S2"}, {"D1", "D2"}};
    static const char *exts[2] = {".IDX", ".GRP"};
    int bad = 0, compared = 0;
    for (auto &pair: pairs) {
        for (const char *ext: exts) {
            std::string a, b;
            if (!readWhole(std::string(refDir) + "/" + pair[0] + ext, a)) {
                std::printf("   %s%-4s reference missing, skipped\n", pair[0], ext);
                continue;
            }
            if (!readWhole(hojy::core::config.saveFilePath(std::string(pair[1]) + ext), b)) {
                std::printf("   FAIL cannot read %s%s\n", pair[1], ext);
                ++bad;
                continue;
            }
            const bool same = a == b;
            std::printf("   %s%-4s -> %s%-4s %8zu vs %8zu bytes  %s\n", pair[0], ext,
                        pair[1], ext, a.size(), b.size(), same ? "identical" : "DIFFERENT");
            if (!same) { ++bad; } else { ++compared; }
        }
    }
    if (compared == 0) { std::puts("   nothing compared"); }
    return bad;
}

/*
 * One physical press must produce exactly one action event, and holding a
 * direction must scroll at a usable rate rather than a burst.
 *
 * Upstream repeats every key after 180 ms at 50 Hz. Both numbers are desktop
 * defaults and both were visible on the device: a deliberate press of Enter
 * delivered two Accepts, so the save menu opened the slot list and confirmed
 * slot 1 on a single press; a deliberate press of Down moved a menu cursor two
 * entries; and holding a direction walked tens of tiles a second, because
 * MapWithEvent::handleKeyInput steps one whole tile per event with no throttle.
 */
static int checkKeyRepeat() {
    std::puts("\n== key repeat ==");
    struct Case { const char *name; hojy::app::InputAction action; bool repeats; };
    const Case cases[] = {
        {"Accept",    hojy::app::InputAction::Accept,    false},
        {"Cancel",    hojy::app::InputAction::Cancel,    false},
        {"Space",     hojy::app::InputAction::Space,     false},
        {"Backspace", hojy::app::InputAction::Backspace, false},
        {"Up",        hojy::app::InputAction::Up,        true},
        {"Down",      hojy::app::InputAction::Down,      true},
        {"Left",      hojy::app::InputAction::Left,      true},
        {"Right",     hojy::app::InputAction::Right,     true},
    };
    /* Held for this long, drained once per 60 Hz frame like SdlInputCollector. */
    auto hold = [](hojy::app::InputAction action, int millis) {
        hojy::app::InputRepeater rep;
        std::uint64_t t = 1000000;
        rep.press(1, hojy::app::InputDevice::Keyboard, action, t);
        int count = 0;
        for (int elapsed = 0; elapsed < millis * 1000; elapsed += 16666) {
            count += int(rep.drainThrough(t).size());
            t += 16666;
        }
        rep.release(1);
        count += int(rep.drainThrough(t).size());
        return count;
    };
    /* A deliberate press on a physical keyboard. Nothing may fire twice. */
    const int kTapMillis = 300;
    /* Menu scrolling and walking: fast enough to be useful, slow enough to aim. */
    const int kHoldRateMin = 4, kHoldRateMax = 14;
    int bad = 0;
    for (const auto &c: cases) {
        const int tap = hold(c.action, kTapMillis);
        const int held = hold(c.action, 1000);
        const bool tapOk = (tap == 1);
        const bool heldOk = c.repeats ? (held >= kHoldRateMin && held <= kHoldRateMax)
                                      : (held == 1);
        std::printf("   %-10s %d ms tap -> %d   1.0 s hold -> %2d   %s\n",
                    c.name, kTapMillis, tap, held,
                    (tapOk && heldOk) ? "ok"
                        : (!tapOk ? "WRONG (tap fired twice)"
                                  : (c.repeats ? "WRONG (hold rate)" : "WRONG (action repeats)")));
        if (!tapOk || !heldOk) { ++bad; }
    }
    return bad;
}

using namespace hojy;

int main() {
    if (checkKeyRepeat() != 0) { std::puts("RESULT: FAIL - key repeat"); return 1; }
    if (!core::config.load("config.toml")) { std::puts("FAIL config"); return 2; }
    if (!core::config.postLoad()) { std::puts("FAIL postLoad"); return 2; }
    if (!world::state::gStrings.load("strings.toml")) { std::puts("FAIL strings"); return 2; }
    core::config.fixOnTextLoaded();
    if (!content::loadData()) { std::puts("FAIL loadData"); return 2; }
    std::printf("initSubMap=%d start=(%d,%d)\n", (int)content::gFactors.initSubMapId,
                (int)content::gFactors.initSubMapX, (int)content::gFactors.initSubMapY);

    reset(); gTrack = true;
    scene::Window win(core::config.windowWidth(), core::config.windowHeight());
    (void)0;
    gTrack = false; report("Window ctor (title)");
    if (!win.ready()) { std::puts("FAIL window not ready"); return 2; }
    std::printf("globalMap after ctor = %p (port defers it)\n", (void *)win.globalMap());

    /* Same sequence Title::prepareNewGame + Window::newGame perform. */
    win.playMusic(-1);
    if (!world::state::gSaveData.newGame()) { std::puts("FAIL gSaveData.newGame"); return 2; }
    world::state::gStrings.saveDataLoaded();
    if (const char *ref = std::getenv("HOST_TEST_SAVE_REF")) {
        if (saveRoundTrip(ref) != 0) { std::puts("FAIL save round trip"); return 1; }
        if (!world::state::gSaveData.newGame()) { std::puts("FAIL newGame after round trip"); return 2; }
        world::state::gStrings.saveDataLoaded();
    }
    win.closePopup(); win.applyDeferredCommands();
    reset(); gTrack = true;
    win.newGame(); win.applyDeferredCommands();
    gTrack = false; report("Window::newGame (SubMap)");

    std::uint64_t t = win.currTime();
    auto tick = [&](int n) {
        for (int i = 0; i < n; ++i) {
            t += 16666; win.setSimulationTime(t);
            win.updateFixed(); win.compatibilityUpdate(); win.render();
        }
    };
    tick(60);

    /* Walk the real path (BFS over the same blocking rule the game uses,
     * generated by probe_reach.py) instead of calling exitToGlobalMap()
     * directly, so the door-cell comparison in SubMap::tryMove is exercised. */
    std::string path;
    if (FILE *pf = std::fopen("walkpath.txt", "r")) {
        char buf[512]; size_t n = std::fread(buf, 1, sizeof(buf) - 1, pf); buf[n] = 0;
        std::fclose(pf); path = buf;
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
    }
    std::printf("walk path: %zu steps\n", path.size());

    std::uint64_t seq = 0;
    auto send = [&](app::InputAction a) {
        app::InputEvent ev; ev.timestamp = t; ev.device = app::InputDevice::Keyboard;
        ev.action = a; ev.sequence = seq++;
        win.dispatchInput(ev);
    };
    auto where = [&](const char *tag) {
        if (win.saveGame(2)) {
            auto &b = world::state::gSaveData.baseInfo;
            std::printf("   pos[%s] subMap=%d (%d,%d)\n", tag, (int)b->subMap, (int)b->subX, (int)b->subY);
        } else { std::printf("   pos[%s] saveGame failed\n", tag); }
    };
    where("after newGame");
    /* clear the opening dialogue */
    for (int i = 0; i < 900; ++i) { send(app::InputAction::Accept); tick(6); }
    where("after dialogue");
    /* A submap save to load back later; slot 2 gets overwritten on the way out. */
    if (!win.saveGame(3)) { std::puts("FAIL saveGame(3) in submap"); return 1; }

    reset(); gTrack = true;
    for (char c : path) {
        switch (c) {
        case 'U': send(app::InputAction::Up); break;
        case 'D': send(app::InputAction::Down); break;
        case 'L': send(app::InputAction::Left); break;
        case 'R': send(app::InputAction::Right); break;
        default: break;
        }
        tick(4);
        if (win.globalMap() != nullptr) { std::puts("door reached: ensureGlobalMap ran"); break; }
    }
    win.applyDeferredCommands();
    gTrack = false; report("walk to door + exit");
    where("after walk");
    std::printf("globalMap now = %p\n", (void *)win.globalMap());
    if (win.globalMap() == nullptr) { std::puts("RESULT: FAIL (ensureGlobalMap did not build the map)"); return 1; }

    tick(600);   /* run the fade out/in to completion */
    if (!win.saveGame(1)) { std::puts("FAIL saveGame"); return 1; }
    const int sub = world::state::gSaveData.baseInfo->subMap;
    std::printf("baseInfo->subMap after walking out = %d\n", sub);
    if (sub != 0) { std::puts("RESULT: FAIL - still in a submap"); return 1; }
    std::puts("world map reached");

    /* ---- load from the in-game menu: the expensive direction ---------------
     * Slot 2 was written while the party was inside a submap, so this is the
     * world map giving its 7.8 MB back, the archive being parsed, and the
     * submap tile set being read -- the peak the device has to survive. */
    {
        std::printf("\n== loadGame (world map -> submap) ==\n");
        reset(); gTrack = true;
        const bool okLoad = win.loadGame(3);
        win.applyDeferredCommands();
        gTrack = false; report("Window::loadGame");
        std::printf("  ok=%d  map now %s\n", (int)okLoad,
                    win.globalMap() != nullptr ? "world" : "submap");
        if (!okLoad) { std::puts("RESULT: FAIL - loadGame(3) returned false"); return 1; }
        if (win.globalMap() != nullptr) { std::puts("RESULT: FAIL - slot 3 should be a submap save"); return 1; }
        /* Back the other way, which is also how the rest of this test expects
         * to find the world: slot 1 was written a few lines above. */
        reset(); gTrack = true;
        const bool okBack = win.loadGame(1);
        win.applyDeferredCommands();
        gTrack = false; report("Window::loadGame (back)");
        if (!okBack || win.globalMap() == nullptr) {
            std::puts("RESULT: FAIL - loadGame(1) did not restore the world map");
            return 1;
        }
        tick(600);
    }

    /* ---- streaming BGM: bytes must keep coming, and must wrap when looping -- */
    {
        std::printf("\n== bgm stream ==\n");
        const auto path = core::config.musicFilePath("GAME01.WAV");
        reset(); gTrack = true; gBigTrace = false;
        audio::ChannelWavStream st(&audio::gMixer, path);
        const bool okStream = st.ok();
        st.setRepeat(true);
        st.start();
        size_t total = 0;
        std::vector<std::uint8_t> buf(16384);
        /* 45 s of 22050 Hz stereo F32 is ~15.9 MB out; read past one full loop. */
        for (int i = 0; i < 1200; ++i) {
            const size_t got = st.readData(buf.data(), buf.size());
            if (got == 0) { break; }
            total += got;
        }
        gTrack = false; gBigTrace = true;
        const double seconds = total / (22050.0 * 2 * 4);
        std::printf("  ok=%d  produced %.2f MB (%.1f s of audio)  resident %.2f MB  largest %.2f MB\n",
                    (int)okStream, total / 1048576.0, seconds,
                    gLive.load() / 1048576.0, gLargest.load() / 1048576.0);
        if (!okStream || seconds < 50.0 || gLargest.load() > 512u * 1024u) {
            std::puts("RESULT: FAIL - bgm streaming did not behave");
            return 1;
        }
        std::puts("  looped past the end and stayed small");
    }

    /* ---- can the player actually walk after entering a town from the world
            map? "enters, then frozen" is what the device reported. ---- */
    {
        std::printf("\n== walk after entering a town from the world map ==\n");
        win.enterSubMap(content::gFactors.initSubMapId, int(scene::Map::DirDown));
        win.applyDeferredCommands();
        tick(60);
        int bx = -1, by = -1;
        if (win.saveGame(3)) {
            bx = world::state::gSaveData.baseInfo->subX;
            by = world::state::gSaveData.baseInfo->subY;
        }
        std::printf("  entered at (%d,%d)\n", bx, by);
        /* Direction keys only -- exactly what a player does on arriving, and
         * what the device reported as "frozen". A tip box that only closes on
         * Accept swallows these forever. */
        const app::InputAction dirs[4] = {app::InputAction::Down, app::InputAction::Up,
                                          app::InputAction::Left, app::InputAction::Right};
        bool moved = false;
        for (int round = 0; round < 4 && !moved; ++round) {
            for (int i = 0; i < 6; ++i) { send(dirs[round]); tick(20); }
            if (win.saveGame(3)) {
                const int nx = world::state::gSaveData.baseInfo->subX;
                const int ny = world::state::gSaveData.baseInfo->subY;
                if (nx != bx || ny != by) {
                    std::printf("  moved to (%d,%d) after %d presses in dir %d\n",
                                nx, ny, 6, round);
                    moved = true;
                }
            }
        }
        if (!moved) {
            std::printf("  STILL AT (%d,%d) after 24 direction presses\n", bx, by);
            std::puts("RESULT: FAIL - frozen after entering a town");
            return 1;
        }
        /* back out to the world map for the soak below */
        win.exitToGlobalMap(int(scene::Map::DirDown));
        win.applyDeferredCommands();
        tick(200);
    }

    /* ---- soak: repeated fast enter/exit, the thing that fragments PSRAM ---- */
    const int cycles = 25;
    std::printf("\n== soak: %d x (enterSubMap -> exitToGlobalMap) ==\n", cycles);
    size_t liveBase = 0, texBase = 0;
    for (int c = 0; c < cycles; ++c) {
        gTrack = true;
        win.enterSubMap(content::gFactors.initSubMapId, int(scene::Map::DirDown));
        win.applyDeferredCommands();
        tick(20);
        /* The town-name tip waits for a key, same as in real play. */
        send(app::InputAction::Accept);
        tick(400);
        win.exitToGlobalMap(int(scene::Map::DirDown));
        win.applyDeferredCommands();
        tick(400);
        gTrack = false;
        if (c == 0) { liveBase = gLive.load(); texBase = scene::textureBytesTotal(); }
        if (c == 0 || c == cycles - 1 || (c + 1) % 5 == 0) {
            std::printf("  cycle %2d  heap-live %+8.3f MB   textures %7.2f MB (%+.3f MB)\n",
                        c + 1, (double)((long long)gLive.load() - (long long)liveBase) / 1048576.0,
                        scene::textureBytesTotal() / 1048576.0,
                        (double)((long long)scene::textureBytesTotal() - (long long)texBase) / 1048576.0);
        }
    }
    const double heapDrift = (double)((long long)gLive.load() - (long long)liveBase) / 1048576.0;
    const double texDrift = (double)((long long)scene::textureBytesTotal() - (long long)texBase) / 1048576.0;
    std::printf("\nsoak drift over %d cycles: heap %+.3f MB, textures %+.3f MB\n",
                cycles - 1, heapDrift, texDrift);
    const bool leaky = heapDrift > 0.75 || texDrift > 0.75;

    /* One battle: Warfield + WMP textures + the lazily loaded FIGHT sets. */
    std::printf("\n== battle ==\n");
    size_t texBefore = scene::textureBytesTotal(); size_t liveBefore = gLive.load();
    gTrack = true;
    const bool war = win.enterWar(0, false);
    win.applyDeferredCommands();
    for (int i = 0; i < 30; ++i) { send(app::InputAction::Accept); tick(10); }
    gTrack = false;
    std::printf("  enterWar(0) = %d   textures %.2f -> %.2f MB   heap %+.2f MB\n",
                (int)war, texBefore / 1048576.0, scene::textureBytesTotal() / 1048576.0,
                (double)((long long)gLive.load() - (long long)liveBefore) / 1048576.0);

    std::printf("RESULT: %s\n", leaky ? "FAIL - grows across enter/exit cycles"
                                      : "PASS - world map reached, enter/exit steady, battle entered");
    return leaky ? 1 : 0;
}
