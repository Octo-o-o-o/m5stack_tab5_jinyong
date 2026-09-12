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
 * Overlay: no_name_input skips the TTF confirmation (mode 3). Without a
 * loaded font that menu is a tiny empty box that swallows keys; Enter also
 * does nothing because MenuYesNo starts at currIndex_ == -1.
 */

#include "title.hh"

#include "window.hh"
#include "messagebox.hh"
#include "menu.hh"
#include "colorpalette.hh"
#include "world/savedata.hh"
#include "world/action.hh"
#include "world/strings.hh"
#include "content/factors.hh"
#include "content/grpdata.hh"
#include "core/config.hh"
#include "util/random.hh"
#include "util/file.hh"
#include "util/conv.hh"
#include "util/math.hh"
#include "esp_log.h"
#include "tab5_platform.h"
#include <algorithm>
#include <cstring>
#include <new>

namespace hojy::scene {

namespace {

bool gStartingNewGame = false;

/* DOS / desktop HOJY has no loading screen; PC reads ALLSIN+SMP instantly.
 * Tab5 cannot. Reuse the in-game MessageBox and 等待 (already in the TTF). */
MessageBox *showEnterWait(Title *title)
{
    auto *msg = new MessageBox(title, 0, 0, title->width(), title->height());
    auto text = GETTEXT(88);
    text += L"……";
    msg->popup({text}, MessageBox::Normal);
    msg->update();
    if (gWindow && gWindow->renderer()) {
        /* Both DPI buffers, so the one we do not write cannot surface later. */
        gWindow->render();
        gWindow->renderer()->present();
        gWindow->render();
        gWindow->renderer()->present();
    }
    ESP_LOGI(TAB5_TAG, "enter wait ui");
    return msg;
}

/* The wait box has to go away before the attribute screen: it is a child of
 * Title, and leaving it up covered the 是/否 menu -- which looked like "the
 * game started on its own" because Enter then hit the preselected 是. */
void dismissEnterWait(MessageBox *&msg)
{
    if (msg == nullptr) { return; }
    msg->requestDelete();
    msg = nullptr;
}

void loadTitleTextures(Renderer *renderer, TextureMgr &mgr, Texture *&big) {
    mgr.clear();
    delete big;
    big = nullptr;
    renderer->enableLinear(true);
    big = Texture::loadFromRAW(renderer, util::File::getFileContent(core::config.dataFilePath("TITLE.BIG")), 320, 200, gNormalPalette);
    renderer->enableLinear(false);
    std::vector<std::string> dset;
    if (::hojy::content::GrpData::loadData("TITLE", dset)) {
        mgr.loadFromRLE(dset);
    }
}

}

Title::~Title() {
    delete big_;
}

void Title::init() {
    gStartingNewGame = false;
    titleTextureMgr_.setPalette(gNormalPalette);
    titleTextureMgr_.setRenderer(renderer_);
    loadTitleTextures(renderer_, titleTextureMgr_, big_);
    setDirty();
}

namespace {

void applyMainCharName(const std::wstring &mainCharName) {
    auto name = mainCharName;
    auto big5Name = util::big5Conv.fromUnicode(name);
    while (big5Name.length() > 8) {
        name.pop_back();
        big5Name = util::big5Conv.fromUnicode(name);
    }
    auto *charInfo = ::hojy::world::state::gSaveData.charInfo[0];
    auto *subMap = ::hojy::world::state::gSaveData.subMapInfo[
        ::hojy::content::gFactors.initSubMapId];
    if (!charInfo || !subMap) {
        return;
    }
    memset(charInfo->name, 0, 10);
    memcpy(charInfo->name, big5Name.data(), big5Name.length());
    auto tailName = util::big5Conv.fromUnicode(GETTEXT(110));
    memset(subMap->name, 0, 10);
    memcpy(subMap->name, big5Name.data(), big5Name.length());
    const auto tailLength = std::min<std::size_t>(
        tailName.length(), 10 - big5Name.length());
    memcpy(subMap->name + big5Name.length(), tailName.data(), tailLength);
}

}

bool Title::prepareNewGame() {
    const auto t0 = tab5_clock_us();
    ESP_LOGI(TAB5_TAG, "prepareNewGame");
    gWindow->playMusic(-1);
    titleTextureMgr_.clear();
    tab5_log_memory("before_newgame_load");
    try {
        if (!::hojy::world::state::gSaveData.newGame()) {
            ESP_LOGE(TAB5_TAG, "SaveData::newGame failed");
            return false;
        }
        if (!::hojy::world::state::gSaveData.charInfo[0]) {
            ESP_LOGE(TAB5_TAG, "new game missing charInfo[0]");
            return false;
        }
        if (::hojy::content::gFactors.initSubMapId < 0
            || !::hojy::world::state::gSaveData.subMapInfo[
                ::hojy::content::gFactors.initSubMapId]) {
            ESP_LOGE(TAB5_TAG, "new game missing init submap %d",
                     (int)::hojy::content::gFactors.initSubMapId);
            return false;
        }
        doRandomBaseInfo();
        ESP_LOGI(TAB5_TAG, "prepareNewGame ok ms=%u",
                 (unsigned)((tab5_clock_us() - t0) / 1000ULL));
        return true;
    } catch (const std::bad_alloc &) {
        ESP_LOGE(TAB5_TAG, "prepareNewGame bad_alloc");
        return false;
    }
}

void Title::handleKeyInput(Node::Key key) {
    switch (key) {
    case KeyUp:
        if (currSel_-- == 0) { currSel_ = 2; }
        setDirty();
        break;
    case KeyDown:
        if (currSel_++ == 2) { currSel_ = 0; }
        setDirty();
        break;
    case KeyOK: case KeySpace:
        switch (mode_) {
        case 0:
            switch (currSel_) {
            case 0:
                if (core::config.noNameInput()) {
                    if (gStartingNewGame) {
                        break;
                    }
                    gStartingNewGame = true;
                    mainCharName_ = core::config.defaultName();
                    MessageBox *wait = showEnterWait(this);
                    if (!prepareNewGame()) {
                        dismissEnterWait(wait);
                        loadTitleTextures(renderer_, titleTextureMgr_, big_);
                        gWindow->playMusic(16);
                        mainCharName_.clear();
                        auto *msgBox = new MessageBox(this, 0, height_ / 2, width_, height_ / 2);
                        msgBox->popup({GETTEXT(69)}, MessageBox::PressToCloseThis);
                        gStartingNewGame = false;
                        setDirty();
                        break;
                    }
                    dismissEnterWait(wait);
                    /* Upstream keeps the attribute roll/confirm screen even
                     * with no_name_input; only the name prompt is skipped. */
                    mode_ = 3;
                } else {
                    /* There is no IME on this device, so typing can only add
                     * ASCII. Start from default_name (config.toml, [ui]) so a
                     * Chinese name is one Enter away, and Backspace clears it
                     * for anyone who wants to type. */
                    mainCharName_ = core::config.defaultName();
                    mode_ = 2;
                    recalcInputRect();
                    Window::beginInput();
                }
                setDirty();
                break;
            case 1:
                currSel_ = 0;
                mode_ = 1;
                setDirty();
                break;
            case 2:
                gWindow->closePopup();
                gWindow->forceQuit();
                break;
            }
            break;
        case 1: {
            int sel = int(currSel_) + 1;
            showEnterWait(this);
            if (gWindow->loadGame(sel)) {
                gWindow->closePopup();
            } else {
                auto *msgBox = new MessageBox(this, 0, height_ / 2, width_, height_ / 2);
                msgBox->popup({GETTEXT(69)}, MessageBox::PressToCloseThis);
            }
            break;
        }
        case 2:
            if (key == KeyOK) {
                Window::endInput();
                if (mainCharName_.empty()) {
                    /* No IME on Tab5; Enter on an empty box keeps the default. */
                    mainCharName_ = core::config.defaultName();
                }
                MessageBox *wait = showEnterWait(this);
                if (!prepareNewGame()) {
                    dismissEnterWait(wait);
                    /* prepareNewGame() already released the TITLE atlas and
                     * stopped the music; mode 0 needs both back. */
                    loadTitleTextures(renderer_, titleTextureMgr_, big_);
                    gWindow->playMusic(16);
                    currSel_ = 0;
                    mode_ = 0;
                    auto *msgBox = new MessageBox(this, 0, height_ / 2, width_, height_ / 2);
                    msgBox->popup({GETTEXT(69)}, MessageBox::PressToCloseThis);
                    setDirty();
                    break;
                }
                dismissEnterWait(wait);
                mode_ = 3;
                setDirty();
            }
            break;
        }
        break;
    case KeyCancel:
        switch (mode_) {
        case 0:
            gStartingNewGame = false;
            break;
        case 2:
            Window::endInput();
            /* fallthrough */
        case 1:
            currSel_ = 0;
            mode_ = 0;
            setDirty();
            break;
        }
        break;
    case KeyBackspace:
        switch (mode_) {
        case 2:
            if (!mainCharName_.empty()) {
                mainCharName_.pop_back();
                recalcInputRect();
                setDirty();
            }
            break;
        }
        break;
    default:
        break;
    }
}

void Title::handleTextInput(const std::wstring &str) {
    if (mode_ != 2) { return; }
    bool dirty = false;
    for (auto &ch: str) {
        if (mainCharName_.length() < 8 && ch != L' ') {
            mainCharName_ += ch;
            recalcInputRect();
            dirty = true;
        }
    }
    if (dirty) {
        setDirty();
    }
}

void Title::update() {
    /* The TITLE atlas is released inside prepareNewGame() to free the
     * contiguous hole ALLSIN needs. Blocking the whole update here also
     * blocked the attribute confirm screen (empty box, Enter did nothing --
     * playbook P-04). makeCache() guards the atlas per mode instead. */
    NodeWithCache::update();
    if (mode_ == 3 && menu_ == nullptr) {
        ensureConfirmationMenu();
    }
}

void Title::ensureConfirmationMenu() {
    if (menu_ != nullptr || mode_ != 3) { return; }
    auto *ttf = renderer_->ttf();
    const auto windowBorder = core::config.windowBorder();
    const auto lineheight = ttf->fontSize() + TextLineSpacing;
    const int oy = height_ - lineheight * 5;
    const int colwidth = ttf->fontSize() * 21 / 4;
    const int ox = (width_ - colwidth * 4 + 20) / 2;
    const auto askText = L'\2' + mainCharName_ + L"  \1" + GETTEXT(100);
    const int mx = ox + ttf->stringWidth(askText) + windowBorder * 2;
    const int my = oy - windowBorder;
    auto *menu = new MenuYesNo(this, mx, my, gWindow->width() - mx, gWindow->height() - oy);
    menu->enableHorizonal(true);
    /* popupWithYesNo() starts at index -1: nothing is highlighted and Enter is
     * a no-op until an arrow is pressed, which on a handheld reads as a hang
     * (playbook P-04). Preselect 否 rather than 是: this screen is a one-way
     * door, so a stray key must re-roll (visible, harmless) instead of starting
     * the game. Choosing 是 is then a deliberate left-then-Enter. */
    menu->popup({GETTEXT(78), GETTEXT(79)}, 1);
    menu->setHandler([this] {
        auto *charInfo = ::hojy::world::state::gSaveData.charInfo[0];
        auto *subMap = ::hojy::world::state::gSaveData.subMapInfo[
            ::hojy::content::gFactors.initSubMapId];
        if (!charInfo || !subMap) {
            auto *failedMenu = menu_;
            menu_ = nullptr;
            mode_ = 0;
            currSel_ = 0;
            gStartingNewGame = false;
            /* prepareNewGame() released the TITLE atlas to get here. */
            loadTitleTextures(renderer_, titleTextureMgr_, big_);
            gWindow->playMusic(16);
            if (failedMenu) { failedMenu->requestDelete(); }
            auto *msgBox = new MessageBox(this, 0, height_ / 2, width_, height_ / 2);
            msgBox->popup({GETTEXT(69)}, MessageBox::PressToCloseThis);
            setDirty();
            return;
        }
        applyMainCharName(mainCharName_);
        /* No fadeOut: the port skips the first fade (playbook G-13/P-08) and
         * the SMP load right after must not sit behind a black screen. Leave
         * the wait frame on the panel instead. */
        showEnterWait(this);
        gWindow->closePopup();
        gWindow->newGame();
    }, [this] {
        doRandomBaseInfo();
        setDirty();
    });
    menu_ = menu;
}

void Title::makeCache() {
    /* big_ is the TITLE.BIG background and survives prepareNewGame(); the
     * titleTextureMgr_ menu slices do not. Only modes 0/1 use the slices. */
    if (big_ == nullptr) {
        return;
    }
    cacheBegin();
    renderer_->clear(0, 0, 0, 255);

    int w = width_, h = width_ * big_->height() / big_->width();
    if (h > height_) {
        h = height_;
        w = height_ * big_->width() / big_->height();
    }
    int x = (width_ - w) / 2;
    int y = (height_ - h) / 2;
    renderer_->renderTexture(big_, x, y, w, h, 0, 0, big_->width(), big_->height(), false);
    switch (mode_) {
    case 0:
    case 1: {
        if (titleTextureMgr_[0] == nullptr) {
            cacheEnd();
            break;
        }
        auto scale = y == 0 ? util::calcSmallestDivision(height_, 200) : util::calcSmallestDivision(width_, 320);
        const auto *img0 = titleTextureMgr_[0];
        int x0 = (width_ - (img0->width() + img0->originX()) * scale.first / scale.second) / 2;
        int y0 = height_ - 65 * scale.first / scale.second;
        static const std::pair<int, int> offsetY[9] = {
            {y0, 20 * scale.first / scale.second * 3}, {y0, 20 * scale.first / scale.second}, {y0 + 20 * scale.first / scale.second, 20 * scale.first / scale.second}, {y0 + 20 * scale.first / scale.second * 2, 20 * scale.first / scale.second},
            {y0, 20 * scale.first / scale.second * 3}, {y0, 20 * scale.first / scale.second}, {y0 + 20 * scale.first / scale.second, 20 * scale.first / scale.second}, {y0 + 20 * scale.first / scale.second * 2, 20 * scale.first / scale.second}, {y0, 40}
        };
        if (mode_ == 0) {
            renderer_->renderTexture(titleTextureMgr_[0], x0, offsetY[0].first, scale);
            renderer_->renderTexture(titleTextureMgr_[1 + currSel_], x0, offsetY[1 + currSel_].first, scale);
        } else {
            renderer_->renderTexture(titleTextureMgr_[4], x0, offsetY[4].first, scale);
            renderer_->renderTexture(titleTextureMgr_[5 + currSel_], x0, offsetY[5 + currSel_].first, scale);
        }
        cacheEnd();
        break;
    }
    case 2: {
        auto *ttf = renderer_->ttf();
        y = height_ - (ttf->fontSize() + TextLineSpacing) * 5;
        ttf->setColor(236, 236, 236);
        ttf->setAltColor(2, 224, 180, 32);
        ttf->render(GETTEXT(41) + L'\2' + mainCharName_, width_ / 4, y, false);
        cacheEnd();
        break;
    }
    case 3: {
        auto ttf = renderer_->ttf();
        int lineheight = ttf->fontSize() + TextLineSpacing;
        y = height_ - lineheight * 5;
        int hh = lineheight - 2 - TextLineSpacing / 4;
        int colwidth = ttf->fontSize() * 21 / 4;
        x = (width_ - colwidth * 4 + 20) / 2;
        auto askText = L'\2' + mainCharName_ + L"  \1" + GETTEXT(100);
        auto *data = ::hojy::world::state::gSaveData.charInfo[0];
        ttf->setColor(236, 236, 236);
        ttf->setAltColor(2, 224, 180, 32);
        ttf->render(askText, x, y, false);
        y += lineheight * 2;
        drawProperty(GETTEXT(26), data->maxMp, 50, x, y, hh, data->mpType);
        drawProperty(GETTEXT(101), data->attack, 30, x + colwidth, y, hh);
        drawProperty(GETTEXT(9), data->speed, 30, x + colwidth * 2, y, hh);
        drawProperty(GETTEXT(102), data->defence, 30, x + colwidth * 3, y, hh);
        y += lineheight;
        drawProperty(GETTEXT(25), data->maxHp, 50, x, y, hh);
        drawProperty(GETTEXT(103), data->medic, 30, x + colwidth, y, hh);
        drawProperty(GETTEXT(104), data->poison, 30, x + colwidth * 2, y, hh);
        drawProperty(GETTEXT(105), data->depoison, 30, x + colwidth * 3, y, hh);
        y += lineheight;
        drawProperty(GETTEXT(106), data->fist, 30, x, y, hh);
        drawProperty(GETTEXT(107), data->sword, 30, x + colwidth, y, hh);
        drawProperty(GETTEXT(108), data->blade, 30, x + colwidth * 2, y, hh);
        drawProperty(GETTEXT(109), data->special, 30, x + colwidth * 3, y, hh);
        if (core::config.showPotential()) {
            drawProperty(GETTEXT(29), data->potential, 100, x + colwidth * 4, y, hh);
        }
        cacheEnd();
    }
    }
}

void Title::doRandomBaseInfo() {
    (void)this;
    auto *data = ::hojy::world::state::gSaveData.charInfo[0];
    data->maxHp = util::gRandom(25, 50);
    data->hp = data->maxHp;
    data->maxMp = util::gRandom(25, 50);
    data->mp = data->maxMp;
    data->mpType = util::gRandom(0, 1);
    data->hpAddOnLevelUp = util::gRandom(1, 10);
    data->attack = util::gRandom(25, 30);
    data->speed = util::gRandom(25, 30);
    data->defence = util::gRandom(25, 30);
    data->medic = util::gRandom(25, 30);
    data->poison = util::gRandom(25, 30);
    data->depoison = util::gRandom(25, 30);
    data->fist = util::gRandom(25, 30);
    data->sword = util::gRandom(25, 30);
    data->blade = util::gRandom(25, 30);
    data->special = util::gRandom(25, 30);
    data->throwing = util::gRandom(25, 30);
    data->potential = util::gRandom(1, 100);
}

void Title::drawProperty(const std::wstring &name, std::int16_t value, std::int16_t maxValue, int x, int y, int h, int mpType) {
    auto ttf = renderer_->ttf();
    bool shadow = false;
    auto dispString = name + L": " + std::to_wstring(value);
    if (value >= maxValue) {
        renderer_->fillRect(x, y, ttf->stringWidth(dispString) + 2, h, 216, 20, 24, 255);
        shadow = true;
    }
    if (mpType >= 0) {
        std::uint8_t r, g, b;
        std::tie(r, g, b) = ::hojy::world::state::calcColorForMpType(mpType);
        ttf->setColor(r, g, b);
    } else {
        if (value >= maxValue) {
            ttf->setColor(252, 236, 132);
        } else {
            ttf->setColor(216, 20, 24);
        }
    }
    ttf->render(dispString, x, y, shadow);
}

void Title::recalcInputRect() {
    auto *ttf = renderer_->ttf();
    int x = width_ / 4 + ttf->stringWidth(GETTEXT(41) + mainCharName_);
    int y = height_ - ttf->fontSize() * 5 - TextLineSpacing * 4;
    Window::setInputRect(x, y, width_ / 2, ttf->fontSize());
}

}
