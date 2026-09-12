/*
 * Heroes of Jin Yong.
 * A reimplementation of the DOS game `The legend of Jin Yong Heroes`.
 * Copyright (C) 2021, Soar Qin<soarchin@gmail.com>

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Overlay: do not construct GlobalMap/SubMap/Warfield before the title
 * screen. GlobalMap's 1919x960 ARGB minimap plus map layers throw
 * std::bad_alloc on ESP32-P4 and abort() the firmware.
 */

#include "window.hh"

#include "colorpalette.hh"
#include "globalmap.hh"
#include "submap.hh"
#include "warfield.hh"
#include "effect.hh"
#include "talkbox.hh"
#include "title.hh"
#include "dead.hh"
#include "endscreen.hh"
#include "menu.hh"
#include "charlistmenu.hh"
#include "itemview.hh"
#include "statusview.hh"

#include "audio/mixer.hh"
#include "content/constants.hh"
#include "content/factors.hh"
#include "content/grpdata.hh"
#include "content/event.hh"
#include "world/strings.hh"
#include "world/savedata.hh"
#include "core/config.hh"
#include "util/conv.hh"
#include "tab5_audio_sync.hh"
#include "tab5_texture_stats.hh"

#include "esp_log.h"
#include "tab5_platform.h"

#include <SDL.h>
#include <fmt/xchar.h>
#include <limits>
#include <algorithm>
#include <new>
#include <thread>
#include <stdexcept>

namespace hojy::scene {

Window *gWindow = nullptr;

#if !defined(HOJY_VERSION)
#define HOJY_VERSION "development"
#endif

static const char *GameWindowTitle = "Heroes of Jin Yong " HOJY_VERSION;

namespace {

int gPendingEnterMusic = -1;

int startingSubMapMusic(std::int16_t subMapId)
{
    const auto *smi = ::hojy::world::state::gSaveData.subMapInfo[subMapId];
    int music = -1;
    if (smi) {
        music = smi->enterMusic;
        if (music < 0) {
            music = smi->exitMusic;
        }
    }
    if (music < 0) {
        music = 16;
    }
    return music;
}

void playPendingEnterMusic(Window *window)
{
    if (gPendingEnterMusic < 0) {
        return;
    }
    const int music = gPendingEnterMusic;
    gPendingEnterMusic = -1;
    try {
        window->playMusic(music);
        ESP_LOGI(TAB5_TAG, "enter music=%d after first frame", music);
    } catch (const std::bad_alloc &) {
        ESP_LOGE(TAB5_TAG, "enter music bad_alloc");
    }
}

std::uint64_t wallTimeMicros() {
    const auto frequency = SDL_GetPerformanceFrequency();
    const auto counter = SDL_GetPerformanceCounter();
    if (frequency == 0) {
        return static_cast<std::uint64_t>(SDL_GetTicks64()) * 1000ULL;
    }
    if (counter > std::numeric_limits<std::uint64_t>::max() / 1000000ULL) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return counter * 1000000ULL / static_cast<std::uint64_t>(frequency);
}

bool loadItemAtlas(Renderer *renderer, MapWithEvent *globalMap,
                   Texture *&itemTexture, int &itemTexW, int &itemTexH,
                   int &itemWCount, int &itemHCount)
{
    if (itemTexture != nullptr) {
        return true;
    }
    const auto &blob = globalMap->texData(::hojy::content::ItemTexIdStart);
    if (blob.size() < sizeof(std::int16_t) * 2) {
        ESP_LOGE(TAB5_TAG, "item tex header missing");
        return false;
    }
    const auto *arr = reinterpret_cast<const int16_t *>(blob.data());
    itemTexW = arr[0];
    itemTexH = arr[1];
    if (itemTexW <= 0 || itemTexH <= 0) {
        ESP_LOGE(TAB5_TAG, "item tex size %dx%d", itemTexW, itemTexH);
        return false;
    }
    itemWCount = 1024 / itemTexW;
    itemHCount = (::hojy::content::BagItemCount + itemWCount - 1) / itemWCount;
    const int height = itemTexH * itemHCount;
    itemTexture = Texture::create(renderer, itemTexW * itemWCount, height);
    if (itemTexture == nullptr) {
        ESP_LOGE(TAB5_TAG, "item atlas create failed");
        return false;
    }
    itemTexture->enableBlendMode(true);
    int pitch = 0;
    const auto *colors = gNormalPalette.colors();
    auto *pixels = itemTexture->lock(pitch);
    if (pixels == nullptr) {
        ESP_LOGE(TAB5_TAG, "item atlas lock failed");
        return false;
    }
    for (int i = 0; i < ::hojy::content::BagItemCount; ++i) {
        Texture::renderRLE(globalMap->texData(::hojy::content::ItemTexIdStart + i),
                           colors,
                           pixels,
                           pitch,
                           height,
                           itemTexW * (i % itemWCount),
                           itemTexH * (i / itemWCount));
    }
    itemTexture->unlock();
    return true;
}

bool ensureSubMap(Renderer *renderer, int w, int h, MapWithEvent *&subMap)
{
    try {
        if (subMap == nullptr) {
            ESP_LOGI(TAB5_TAG, "construct SubMap");
            tab5_log_memory("before_submap");
            subMap = new SubMap(renderer, 0, 0, w, h, core::config.scale());
            tab5_log_memory("after_submap");
        }
        return true;
    } catch (const std::bad_alloc &) {
        ESP_LOGE(TAB5_TAG, "submap bad_alloc");
        return false;
    } catch (const std::exception &ex) {
        ESP_LOGE(TAB5_TAG, "submap exception %s", ex.what());
        return false;
    }
}

bool ensureGlobalMap(Renderer *renderer, int w, int h,
                     MapWithEvent *&globalMap, Texture *&itemTexture,
                     int &itemTexW, int &itemTexH, int &itemWCount, int &itemHCount,
                     const char **reason = nullptr)
{
    if (reason != nullptr) { *reason = "?"; }
    try {
        if (globalMap == nullptr) {
            /* Measured on the host harness against this data set: the build
             * wants ~6.3 MB, of which cellInfo_ alone is one contiguous 2.2 MB
             * block. A BGM decode in flight holds ~4 MB more, and the same door
             * step that gets here also changes the music, so let it settle. */
            waitMusicLoaderIdle(3000);
            ESP_LOGI(TAB5_TAG, "construct GlobalMap");
            tab5_log_memory("before_globalmap");
            ESP_LOGI(TAB5_TAG, "textures before globalmap: %u KB",
                     (unsigned)(textureBytesTotal() / 1024u));
            globalMap = new GlobalMap(renderer, 0, 0, w, h, core::config.scale());
            dynamic_cast<GlobalMap *>(globalMap)->load();
            tab5_log_memory("after_globalmap");
            ESP_LOGI(TAB5_TAG, "textures after globalmap:  %u KB",
                     (unsigned)(textureBytesTotal() / 1024u));
        }
        /* Item icons are cosmetic: a failed 1.2 MB atlas must not be the reason
         * the player cannot leave a submap. */
        if (!loadItemAtlas(renderer, globalMap, itemTexture, itemTexW, itemTexH,
                           itemWCount, itemHCount)) {
            ESP_LOGW(TAB5_TAG, "item atlas unavailable; continuing without item icons");
        }
        return true;
    } catch (const std::bad_alloc &) {
        ESP_LOGE(TAB5_TAG, "globalmap bad_alloc");
        tab5_log_memory("globalmap_oom");
        if (reason != nullptr) { *reason = "oom"; }
        return false;
    } catch (const std::exception &ex) {
        ESP_LOGE(TAB5_TAG, "globalmap exception %s", ex.what());
        tab5_log_memory("globalmap_fail");
        /* "MMAP header" means GrpData::loadData("MMAP") came back empty, which
         * is a different problem from a plain allocation failure. */
        if (reason != nullptr) { *reason = ex.what(); }
        return false;
    }
}

bool ensureWorldMaps(Renderer *renderer, int w, int h,
                     MapWithEvent *&globalMap, MapWithEvent *&subMap,
                     Texture *&itemTexture, int &itemTexW, int &itemTexH,
                     int &itemWCount, int &itemHCount)
{
    return ensureSubMap(renderer, w, h, subMap)
        && ensureGlobalMap(renderer, w, h, globalMap, itemTexture,
                           itemTexW, itemTexH, itemWCount, itemHCount);
}

/*
 * Put one frame with 等待…… on the panel before anything that takes long
 * enough to look like a freeze, the same way the title screen does.
 *
 * Safe to call from inside a node dispatch: doUpdate()/doRender() iterate a
 * copy of children_ and doHandleKeyInput() takes its child pointer before
 * recursing, so appending and then deleting a freshly made node that nothing
 * else references cannot disturb a walk in progress. The box shrinks itself to
 * the text (MessageBox::layoutText), so its cache is ~25 KB, not a screenful.
 */
void showBusy(Node *parent)
{
    if (parent == nullptr) { return; }
    auto *msg = new MessageBox(parent, 0, 0, gWindow->width(), gWindow->height());
    auto text = GETTEXT(88);
    text += L"……";
    msg->popup({text}, MessageBox::Normal);
    msg->update();
    if (gWindow->renderer()) {
        /* Twice: the panel has two frame buffers and present() flips between
         * them. Writing only one leaves the other holding a stale frame, which
         * is what surfaced as a flash part way through a load. */
        gWindow->render();
        gWindow->renderer()->present();
        gWindow->render();
        gWindow->renderer()->present();
    }
    /* The frame is already on the panel; the node is not needed and must not
     * linger as children_.back(), where it would eat every key. Transitions
     * run outside the node dispatch, so deleting directly is safe. */
    delete msg;
}

bool ensureWarfield(Renderer *renderer, int w, int h, Map *&warfield)
{
    try {
        if (warfield == nullptr) {
            ESP_LOGI(TAB5_TAG, "construct Warfield");
            warfield = new Warfield(renderer, 0, 0, w, h, core::config.scale());
        }
        return true;
    } catch (const std::bad_alloc &) {
        ESP_LOGE(TAB5_TAG, "warfield bad_alloc");
        return false;
    }
}

}

Window::Window(int w, int h) : width_(w), height_(h), currTime_(wallTimeMicros()) {
    if (gWindow) {
        throw std::runtime_error("Duplicate window creation");
    }
    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        SDL_Init(SDL_INIT_VIDEO);
    }
    if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER)) {
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
    SDL_GameControllerEventState(SDL_ENABLE);
    auto *win = SDL_CreateWindow(GameWindowTitle,
                                 SDL_WINDOWPOS_CENTERED,
                                 SDL_WINDOWPOS_CENTERED,
                                 w,
                                 h,
                                 SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
#ifdef _WIN32
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
#endif
    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "1");
    win_ = win;
    gWindow = this;

    renderer_ = new Renderer(win_, w, h);
    renderer_->enableLinear(false);

    if (!gNormalPalette.load("MMAP") || !gEndPalette.load("ENDCOL")) {
        ready_ = false;
        return;
    }
    {
        std::array<std::uint32_t, 256> n{};
        n.fill(0xFFFFFFFFu);
        n[0] = 0;
        gMaskPalette.create(n);
    }

    headTextureMgr_.setPalette(gNormalPalette);
    headTextureMgr_.setRenderer(renderer_);
    ::hojy::content::GrpData::DataSet dset;
    renderer_->enableLinear(true);
    if (::hojy::content::GrpData::loadData("HDGRP", dset)) {
        headTextureMgr_.loadFromRLE(dset);
    }
    renderer_->enableLinear(false);
    if (!gEffect.load("EFT")) {
        ready_ = false;
        return;
    }

    SDL_ShowWindow(win);
    if (audio::gMixer.init(3)) {
        audio::gMixer.pause(false);
    }
    ESP_LOGI(TAB5_TAG, "window ready for title (world maps deferred)");
    ESP_LOGI(TAB5_TAG, "ui no_name_input=%d", (int)core::config.noNameInput());
    title();
}

Window::~Window() {
    if (gWindow == this) {
        gWindow = nullptr;
    }
    closePopup();
    headTextureMgr_.clear();
    gEffect.clear();
    delete itemTexture_;
    delete talkBox_;
    delete globalMap_;
    delete subMap_;
    delete warfield_;
    delete renderer_;
    SDL_DestroyWindow(static_cast<SDL_Window *>(win_));
}

const Texture *Window::smpTexture(std::int16_t id) const {
    if (!subMap_) { return nullptr; }
    return subMap_->getOrLoadTexture(id);
}

void Window::renderItemTexture(std::int16_t id, int x, int y, int w, int h) {
    if (itemTexture_ == nullptr) {
        return;
    }
    renderer_->renderTexture(itemTexture_, x, y, w, h,
                             itemTexW_ * (id % itemWCount_), itemTexH_ * (id / itemWCount_),
                             itemTexW_, itemTexH_, true);
}

void Window::updateFixed() {
    audio::gMixer.service();
    const bool wasProcessing = processingStage_;
    processingStage_ = true;
    if (map_) {
        map_->doUpdate();
    }
    if (popup_) {
        popup_->doUpdate();
    }
    processingStage_ = wasProcessing;
    if (!wasProcessing) {
        applyDeferredNodes();
        applyDeferredCommands();
    }
}

void Window::compatibilityUpdate() {
    const bool wasProcessing = processingStage_;
    processingStage_ = true;
    if (map_) {
        map_->advanceCompatibilityFrame();
    }
    processingStage_ = wasProcessing;
    if (!wasProcessing) {
        applyDeferredNodes();
        applyDeferredCommands();
    }
}

void Window::update() {
    updateFixed();
}

void Window::render() {
    const bool wasProcessing = processingStage_;
    processingStage_ = true;
    if (map_) {
        map_->doRender();
    }
    if (popup_) {
        popup_->doRender();
    }
    processingStage_ = wasProcessing;
}

bool Window::flush() {
    /* Application::run already asked renderer_->canRender() before rendering;
     * asking again here would advance the frame schedule twice per loop. */
    renderer_->present();
    tab5_prof_end_frame();
    playPendingEnterMusic(this);
    if (core::config.showFPS()) {
        static float lastFPS = 0.f;
        float fps = renderer_->fps();
        if (lastFPS != fps) {
            SDL_SetWindowTitle(static_cast<SDL_Window *>(win_),
                               fmt::format("{}     FPS: {}", GameWindowTitle, fps).c_str());
        }
    }
    SDL_Delay(1);
    return true;
}

void Window::defer(std::function<void()> command) {
    if (!command) { return; }
    if (processingStage_) {
        deferredCommands_.emplace_back(std::move(command));
        return;
    }
    command();
}

void Window::applyDeferredCommands() {
    if (processingStage_ || applyingDeferred_) { return; }
    applyingDeferred_ = true;
    while (!deferredCommands_.empty()) {
        auto commands = std::move(deferredCommands_);
        deferredCommands_.clear();
        for (auto &command : commands) {
            if (command) { command(); }
        }
    }
    applyingDeferred_ = false;
}

void Window::applyDeferredNodes() {
    if (map_) {
        map_->applyDeferredDeletes();
        // Persistent maps are owned by Window and are never deleted as a
        // consequence of a child callback. Consume an accidental root request
        // and keep the owner pointer valid until an explicit scene transition.
        (void)map_->consumeDeleteRequest();
    }
    if (!popup_) { return; }
    popup_->applyDeferredDeletes();
    if (!popup_->deleteRequested()) { return; }
    auto *victim = popup_;
    popup_ = nullptr;
    const bool owned = freeOnClose_;
    freeOnClose_ = false;
    (void)victim->consumeDeleteRequest();
    if (owned) {
        delete victim;
    } else {
        victim->close();
    }
}

void Window::title() {
    if (processingStage_) {
        defer([this] { title(); });
        return;
    }
    try {
        playMusic(16);
        auto *title = new Title(renderer_, 0, 0, width_, height_);
        title->init();
        freeOnClose_ = true;
        popup_ = title;
        ESP_LOGI(TAB5_TAG, "title screen constructed");
    } catch (const std::bad_alloc &) {
        ESP_LOGE(TAB5_TAG, "title bad_alloc");
    }
}

void Window::endscreen() {
    if (processingStage_) {
        defer([this] { endscreen(); });
        return;
    }
    if (subMap_) { subMap_->cleanupEvents(); }
    map_ = nullptr;
    auto *endScreen = new EndScreen(renderer_, 0, 0, width_, height_);
    endScreen->init();
    freeOnClose_ = true;
    popup_ = endScreen;
}

void Window::newGame() {
    if (processingStage_) {
        defer([this] { newGame(); });
        return;
    }
    ESP_LOGI(TAB5_TAG, "newGame begin");
    tab5_log_memory("newGame");
    const auto t0 = tab5_clock_us();
    try {
        if (!ensureSubMap(renderer_, width_, height_, subMap_)) {
            ESP_LOGE(TAB5_TAG, "newGame maps failed, return to title");
            title();
            return;
        }
        ::hojy::world::state::gStrings.saveDataLoaded();
        map_ = subMap_;
        const auto subMapId = ::hojy::content::gFactors.initSubMapId;
        ESP_LOGI(TAB5_TAG, "newGame load SubMap id=%d", (int)subMapId);
        if (!dynamic_cast<SubMap *>(subMap_)->load(subMapId)) {
            ESP_LOGE(TAB5_TAG, "SubMap::load(%d) failed, return to title", (int)subMapId);
            map_ = nullptr;
            title();
            return;
        }
        /* Instant first enter: Mask fade starts at alpha=255 (one black
         * present) and each step tears on the single DPI FB. Keep the last
         * title frame on-panel until this full map is presented. BGM waits
         * until after that present so WAV decode is not on the black wait. */
        dynamic_cast<SubMap *>(subMap_)->setPosition(
            ::hojy::content::gFactors.initSubMapX,
            ::hojy::content::gFactors.initSubMapY);
        dynamic_cast<SubMap *>(subMap_)->forceMainCharTexture(
            ::hojy::content::gFactors.initMainCharTex / 2);
        map_->resetFrame();
        gPendingEnterMusic = startingSubMapMusic(subMapId);
        ESP_LOGI(TAB5_TAG, "newGame ready ms=%u music=%d",
                 (unsigned)((tab5_clock_us() - t0) / 1000ULL),
                 gPendingEnterMusic);
    } catch (const std::bad_alloc &) {
        ESP_LOGE(TAB5_TAG, "newGame bad_alloc");
        map_ = nullptr;
        title();
    } catch (const std::exception &ex) {
        ESP_LOGE(TAB5_TAG, "newGame exception %s", ex.what());
        map_ = nullptr;
        title();
    }
}

bool Window::loadGame(int slot) {
    if (!ensureSubMap(renderer_, width_, height_, subMap_)) {
        return false;
    }
    /* Build whichever side the save data in memory says the party is on. Used
     * both after a successful read and to undo the release below when the read
     * failed -- gSaveData is only replaced once the whole archive is in, so on
     * failure it still describes the game that was running. */
    auto enterFromSave = [this]() -> bool {
        auto &binfo = ::hojy::world::state::gSaveData.baseInfo;
        if (binfo->subMap > 0) {
            if (globalMap_ != nullptr) {
                delete globalMap_;
                globalMap_ = nullptr;
            }
            map_ = subMap_;
            if (!dynamic_cast<SubMap *>(subMap_)->load(binfo->subMap - 1)) {
                ESP_LOGE(TAB5_TAG, "loadGame submap %d load failed",
                         (int)(binfo->subMap - 1));
                return false;
            }
            subMap_->setDirection(Map::Direction(binfo->direction));
            subMap_->setPosition(binfo->subX, binfo->subY);
            map_->resetFrame();
            /* Same revival point as newGame: the title theme would otherwise
             * keep playing until the next scene change. Decoding is deferred
             * to after the first map frame is on the panel. */
            gPendingEnterMusic = startingSubMapMusic(std::int16_t(binfo->subMap - 1));
            return true;
        }
        if (auto *sub = dynamic_cast<SubMap *>(subMap_)) {
            sub->releaseTiles();
        }
        if (!ensureGlobalMap(renderer_, width_, height_, globalMap_, itemTexture_,
                             itemTexW_, itemTexH_, itemWCount_, itemHCount_)) {
            map_ = nullptr;
            return false;
        }
        globalMap_->setPosition(binfo->mainX, binfo->mainY);
        globalMap_->setDirection(Map::Direction(binfo->direction));
        map_ = globalMap_;
        map_->resetFrame();
        return true;
    };

    /*
     * Give the maps back *before* reading, not after. Parsing the archive needs
     * the outgoing and the incoming SaveData alive at the same time, about 9 MB;
     * asking for that on top of a resident world map (7.8 MB) or submap tile set
     * (4.7 MB) is what a load from the in-game menu used to do, and it does not
     * fit. Reading the card takes seconds, so put a frame up first.
     */
    showBusy(popup_ ? popup_ : static_cast<Node *>(map_));
    map_ = nullptr;
    if (globalMap_ != nullptr) {
        delete globalMap_;
        globalMap_ = nullptr;
    }
    if (auto *sub = dynamic_cast<SubMap *>(subMap_)) {
        sub->releaseTiles();
    }
    tab5_log_memory("loadgame_released");

    if (!::hojy::world::state::gSaveData.load(slot)) {
        ESP_LOGE(TAB5_TAG, "loadGame slot=%d failed, restoring", slot);
        (void)enterFromSave();
        return false;
    }
    ::hojy::world::state::gStrings.saveDataLoaded();
    return enterFromSave();
}

bool Window::saveGame(int slot) {
    auto &binfo = ::hojy::world::state::gSaveData.baseInfo;
    if (auto *world = dynamic_cast<GlobalMap *>(globalMap_)) {
        binfo->onShip = world->onShip();
        binfo->mainX = world->currX();
        binfo->mainY = world->currY();
    }
    binfo->subMap = map_->subMapId() + 1;
    if (binfo->subMap > 0) {
        binfo->subX = dynamic_cast<SubMap *>(subMap_)->currX();
        binfo->subY = dynamic_cast<SubMap *>(subMap_)->currY();
    }
    binfo->direction = std::int16_t(dynamic_cast<MapWithEvent *>(map_)->direction());
    /* A save is ~4.5 MB of submap layer data written to the card. */
    showBusy(popup_ ? popup_ : static_cast<Node *>(map_));
    if (::hojy::world::state::gSaveData.save(slot)) {
        return true;
    }
    /* window_menu.cc reports "saved" whatever this returns, so say otherwise
     * here -- a save that quietly did nothing is the worst outcome of all.
     * The box stays underneath that one and is read when it is dismissed. */
    ESP_LOGE(TAB5_TAG, "saveGame slot=%d failed", slot);
    popupMessageBox({L"\u5b58\u6a94\u5931\u6557"}, MessageBox::PressToCloseTop);
    return false;
}

void Window::forceQuit() {
    quitRequested_ = true;
}

void Window::exitToGlobalMap(int direction) {
    if (processingStage_) {
        defer([this, direction] { exitToGlobalMap(direction); });
        return;
    }
    /*
     * Measured on the device: with the SDX/SMP tile set resident there is about
     * 5.7 MB of PSRAM left, and building the world map peaks around 8.9 MB.
     * The two simply do not fit together on 32 MB, so they take turns -- the
     * submap gives its tiles back here, and enterSubMap() gives the world map
     * back on the way in. Each direction pays one load; that is the price of
     * the world map existing at all.
     */
    int resumeMusic = playingMusic_ - 1;
    playMusic(-1);
    showBusy(popup_ ? popup_ : static_cast<Node *>(map_));
    if (auto *sub = dynamic_cast<SubMap *>(subMap_)) {
        sub->releaseTiles();
    }
    const char *reason = "?";
    if (!ensureGlobalMap(renderer_, width_, height_, globalMap_, itemTexture_,
                         itemTexW_, itemTexH_, itemWCount_, itemHCount_, &reason)) {
        /* Silently doing nothing here is what "walking to the door does not
         * leave the map" looks like. Say so on the panel. */
        ESP_LOGE(TAB5_TAG, "exitToGlobalMap failed (%s)", reason);
        if (resumeMusic >= 0) { gPendingEnterMusic = resumeMusic; }
        /* GETTEXT(69) is "load failed", which is simply the wrong sentence and
         * sent the user looking for a save-file problem. Say what it is, and
         * put the numbers on the panel so no serial cable is needed.
         * Only characters present in the subset font may be used here. */
        size_t freeBytes = 0, largest = 0;
        tab5_mem_psram(&freeBytes, &largest);
        std::wstring why;
        for (const char *c = reason; c != nullptr && *c != '\0'; ++c) {
            why += static_cast<wchar_t>(*c);   /* reason is ASCII by construction */
        }
        popupMessageBox({L"\u7a7a\u9593\u4e0d\u8db3  "
                         + std::to_wstring((unsigned long)(freeBytes / 1024u)) + L"K / "
                         + std::to_wstring((unsigned long)(largest / 1024u)) + L"K  "
                         + why},
                        MessageBox::PressToCloseTop);
        return;
    }
    if (resumeMusic >= 0) { gPendingEnterMusic = resumeMusic; }
    globalMap_->setPosition(::hojy::world::state::gSaveData.baseInfo->mainX,
                            ::hojy::world::state::gSaveData.baseInfo->mainY);
    /* Nothing to fade *out* of -- the submap gave its tiles back before the load
     * -- but the world map can still fade in, so it arrives the same way a town
     * does instead of snapping onto the busy frame. */
    map_ = globalMap_;
    map_->resetFrame();
    dynamic_cast<MapWithEvent *>(map_)->setDirection(Map::Direction(direction));
    map_->fadeIn([this] { map_->resetFrame(); });
}

void Window::enterSubMap(std::int16_t subMapId, int direction) {
    if (processingStage_) {
        defer([this, subMapId, direction] { enterSubMap(subMapId, direction); });
        return;
    }
    if (subMapId < 0
        || static_cast<std::size_t>(subMapId) >= ::hojy::world::state::gSaveData.subMapInfo.size()
        || ::hojy::world::state::gSaveData.subMapInfo[subMapId] == nullptr) {
        /* Event scripts and switchSubMap fields are data; a bad id here is a
         * null deref inside the fade callback, i.e. a reboot mid-transition. */
        ESP_LOGE(TAB5_TAG, "enterSubMap bad id=%d", (int)subMapId);
        return;
    }
    const bool fromGlobalMap = (globalMap_ != nullptr && map_ == globalMap_);
    if (fromGlobalMap) {
        /* Other half of the swap made in exitToGlobalMap(): the world map has
         * to give its ~7.8 MB back before the SDX/SMP tile set can be read in.
         * No fade -- the map we would fade out of is about to be deleted. */
        showBusy(popup_ ? popup_ : static_cast<Node *>(map_));
        auto &binfo = ::hojy::world::state::gSaveData.baseInfo;
        if (auto *world = dynamic_cast<GlobalMap *>(globalMap_)) {
            /* saveGame() reads these from globalMap_; nobody else keeps the
             * world position, so persist it before the object goes away. */
            binfo->onShip = world->onShip();
            binfo->mainX = world->currX();
            binfo->mainY = world->currY();
        }
        map_ = subMap_;
        delete globalMap_;
        globalMap_ = nullptr;
        ESP_LOGI(TAB5_TAG, "world map released for submap %d", (int)subMapId);
        tab5_log_memory("after_globalmap_release");

        const auto *smi = ::hojy::world::state::gSaveData.subMapInfo[subMapId];
        /* No second busy frame here: the submap has no tiles yet, so it would
         * paint one black frame between the world map and the town. The frame
         * presented above stays on the panel for the whole load. */
        if (!dynamic_cast<SubMap *>(map_)->load(subMapId)) {
            ESP_LOGE(TAB5_TAG, "enterSubMap load failed id=%d", (int)subMapId);
            popupMessageBox({GETTEXT(69)}, MessageBox::PressToCloseTop);
            return;
        }
        subMap_->setDirection(Map::Direction(direction));
        const std::int16_t ex = smi->enterX, ey = smi->enterY;
        /* Same tail as the submap-to-submap path: place without firing the
         * enter event, show the town name, and let the fade-in dismiss the tip
         * and then run the event. A tip that needs a keypress is worse than no
         * tip -- MessageBox only closes on OK/Space/Cancel, so arrow keys got
         * swallowed and arriving in a town looked like a freeze. */
        dynamic_cast<MapWithEvent *>(map_)->setPosition(ex, ey, false);
        auto *tips = new MessageBox(map_, 0, 0, width_, height_ * 4 / 5);
        tips->popup({GETSUBMAPNAME(subMapId)}, MessageBox::Normal);
        map_->fadeIn([this, tips, ex, ey] {
            tips->requestDelete();
            dynamic_cast<MapWithEvent *>(map_)->setPosition(ex, ey);
            map_->resetFrame();
        });
        return;
    }

    bool switching = map_->subMapId() >= 0;
    map_->fadeOut([this, subMapId, direction, switching]() {
        if (!switching) {
            map_ = subMap_;
        }
        const auto *smi = ::hojy::world::state::gSaveData.subMapInfo[subMapId];
        if (smi == nullptr || !dynamic_cast<SubMap *>(map_)->load(subMapId)) {
            ESP_LOGE(TAB5_TAG, "enterSubMap load failed id=%d", (int)subMapId);
            if (!switching) { map_ = globalMap_ ? globalMap_ : map_; }
            popupMessageBox({GETTEXT(69)}, MessageBox::PressToCloseTop);
            return;
        }
        if (!switching) {
            subMap_->setDirection(Map::Direction(direction));
        }
        std::int16_t x, y;
        if (switching && smi->subMapEnterX) {
            x = smi->subMapEnterX;
            y = smi->subMapEnterY;
        } else {
            x = smi->enterX;
            y = smi->enterY;
        }
        dynamic_cast<MapWithEvent *>(map_)->setPosition(x, y, false);
        auto *tips = new MessageBox(map_, 0, 0, width_, height_ * 4 / 5);
        tips->popup({GETSUBMAPNAME(subMapId)}, MessageBox::Normal);
        map_->fadeIn([this, tips, x, y] {
            tips->requestDelete();
            dynamic_cast<MapWithEvent *>(map_)->setPosition(x, y);
            map_->resetFrame();
        });
    });
}

bool Window::enterWar(std::int16_t warId, bool getExpOnLose, bool deadOnLose) {
    /* The first battle constructs Warfield and every battle reads ~1.6 MB of
     * battlefield tiles plus the participants' animation sets off the card. */
    showBusy(popup_ ? popup_ : static_cast<Node *>(map_));
    if (!ensureSubMap(renderer_, width_, height_, subMap_)
        || !ensureWarfield(renderer_, width_, height_, warfield_)) {
        return false;
    }
    auto *wf = dynamic_cast<Warfield *>(warfield_);
    if (!wf) { return false; }
    if (!wf->load(warId)) {
        return false;
    }
    wf->setGetExpOnLose(getExpOnLose);
    wf->setDeadOnLose(deadOnLose);
    std::set<std::int16_t> defaultChars;
    if (wf->getDefaultChars(defaultChars)) {
        auto *clm = new CharListMenu(renderer_, 0, 0, gWindow->width(), gWindow->height());
        clm->enableCheckBox(true, [defaultChars](std::int16_t charId) -> bool {
            return defaultChars.find(charId) == defaultChars.end();
        });
        clm->initWithTeamMembers({GETTEXT(70)}, {CharListMenu::LEVEL}, [this, clm](std::int16_t) {
            const auto selectedChars = clm->getSelectedCharIds();
            closePopup();
            auto *wf = dynamic_cast<Warfield *>(warfield_);
            if (!wf) { return; }
            map_ = warfield_;
            if (!wf->putChars(selectedChars)) {
                map_ = subMap_;
                return;
            }
            if (map_ == warfield_) {
                map_->fadeIn();
            }
        }, []() -> bool { return false; });
        for (size_t i = 0; i < clm->charCount(); ++i) {
            if (defaultChars.find(clm->charId(i)) != defaultChars.end()) {
                clm->checkItem(i, true);
            }
        }
        clm->makeCenter(gWindow->width(), gWindow->height() * 4 / 5, 0, 0);
        popup_ = clm;
        freeOnClose_ = true;
    } else {
        map_ = warfield_;
        if (!wf->putChars({})) {
            map_ = subMap_;
            return false;
        }
        if (map_ == warfield_) {
            map_->fadeIn();
        }
    }
    return true;
}

void Window::endWar(bool won, bool instantDie) {
    if (processingStage_) {
        defer([this, won, instantDie] { endWar(won, instantDie); });
        return;
    }
    if (instantDie) {
        playerDie();
        return;
    }
    map_ = subMap_;
    subMap_->fadeIn([this, won]() {
        subMap_->continueEvents(won);
        auto *subMapInfo = ::hojy::world::state::gSaveData.subMapInfo[subMap_->subMapId()];
        if (subMapInfo) {
            auto music = subMapInfo->enterMusic;
            if (music < 0) {
                music = subMapInfo->exitMusic;
            }
            if (music >= 0) {
                gWindow->playMusic(music);
            }
        }
    });
}

void Window::playerDie() {
    if (processingStage_) {
        defer([this] { playerDie(); });
        return;
    }
    if (subMap_) { subMap_->cleanupEvents(); }
    map_ = nullptr;
    auto *dead = new Dead(renderer_, 0, 0, width_, height_);
    dead->init();
    freeOnClose_ = true;
    popup_ = dead;
}

void Window::useQuestItem(std::int16_t itemId) {
    if (processingStage_) {
        defer([this, itemId] { useQuestItem(itemId); });
        return;
    }
    auto *mapev = dynamic_cast<MapWithEvent *>(map_);
    if (mapev) mapev->onUseItem(itemId);
}

void Window::forceEvent(std::int16_t eventId) {
    if (processingStage_) {
        defer([this, eventId] { forceEvent(eventId); });
        return;
    }
    auto *mapev = dynamic_cast<MapWithEvent *>(map_);
    if (mapev) mapev->runEvent(eventId);
    else if (subMap_) subMap_->runEvent(eventId);
}

void Window::closePopup() {
    if (processingStage_) {
        defer([this] { closePopup(); });
        return;
    }
    if (!popup_) { return; }
    if (freeOnClose_) {
        delete popup_;
    } else {
        popup_->close();
    }
    popup_ = nullptr;
}

void Window::endPopup(bool close, bool result) {
    if (processingStage_) {
        defer([this, close, result] { endPopup(close, result); });
        return;
    }
    if (close) {
        closePopup();
    }
    auto *mapev = dynamic_cast<MapWithEvent *>(map_);
    if (mapev) mapev->continueEvents(result);
}

}
