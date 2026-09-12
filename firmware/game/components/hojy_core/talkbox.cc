/*
 * Overlay of HeroesOfJinYong src/scene/talkbox.cc for ESP32-P4.
 * Upstream caches the whole inset window; without ALLOW_ODD_WIDTH that
 * becomes 1024x512 (2MB) and CreateTexture fails after SMP/ALLSIN.
 * Cache only the frame; draw TTF onto the backbuffer so atlas-to-target
 * misses cannot leave an empty box.
 */

#include "talkbox.hh"

#include "texture.hh"
#include "window.hh"
#include "core/config.hh"
#include "util/math.hh"
#include "esp_log.h"
#include "tab5_platform.h"

namespace hojy::scene {

void TalkBox::popup(const std::wstring &text, std::int16_t headId, std::int16_t position) {
    text_.clear();
    size_t idx = 0;
    std::wstring line;
    std::vector<std::wstring> lines;
    size_t tlen = text.length();
    auto windowBorder = core::config.windowBorder();
    while (idx < tlen) {
        auto pos = text.find(L'*', idx);
        if (pos == std::wstring::npos) {
            line.append(text.substr(idx));
            while (!line.empty() && line.back() == 0) {
                line.erase(line.end() - 1);
            }
            if (!line.empty()) {
                lines.emplace_back(line);
                line.clear();
            }
            break;
        }
        auto len = pos - idx;
        auto seg = text.substr(idx, len);
        while (!seg.empty() && seg.back() == 12288/*space*/) {
            seg.erase(seg.end() - 1);
        }
        while (!seg.empty() && seg.front() == 12288/*space*/) {
            seg.erase(seg.begin());
        }
        line.append(seg);
        if (len < 12 || (line.back() == L'．' && *(line.end() - 2) != L'．' && (idx + 2 >= tlen || text[idx + 1] != L'．'))) {
            if (!line.empty()) {
                lines.emplace_back(line);
                line.clear();
            }
        }
        idx = pos + 1;
        while (idx < tlen && text[idx] == 12288/*space*/) {
            ++idx;
        }
    }

    headTex_ = (position != 2 && position != 3 && headId >= 0) ? gWindow->headTexture(headId) : nullptr;
    int headW = 0;
    if (headTex_) {
        headScale_ = util::calcSmallestDivision(gWindow->width(), 320);
        headW = headTex_->width() * headScale_.first / headScale_.second + windowBorder * 2;
    }

    auto *ttf = renderer_->ttf();
    size_t widthMax = width_ - headW - windowBorder * 3;
    for (auto &l: lines) {
        size_t len = l.length();
        if (!len) {
            continue;
        }
        idx = 0;
        size_t w = 0;
        for (size_t i = 0; i < len; ++i) {
            auto &ch = l[i];
            if (ch == L'．') {
                if ((i > 0 && l[i - 1] == L'…') || (i + 1 < len && l[i + 1] == L'．')) {
                    ch = L'…';
                } else {
                    ch = L'。';
                }
            }
            std::uint8_t width;
            std::int8_t y0, y1;
            ttf->charDimension(ch, width, y0, y1);
            w += width;
            if (w > widthMax) {
                text_.emplace_back(l.substr(idx, i - idx));
                idx = i;
                w = width;
            }
        }
        if (idx < len) {
            text_.emplace_back(l.substr(idx));
        }
    }
    index_ = 0;
    dispLines_ = (height_ * 2 / 5 - windowBorder * 2 + TextLineSpacing) / (ttf->fontSize() + TextLineSpacing);
    if (dispLines_ > text_.size()) { dispLines_ = text_.size(); }

    position_ = position;
    unsigned cp0 = 0, cp1 = 0, cp2 = 0;
    unsigned n0 = 0;
    int sw0 = 0;
    if (!text_.empty()) {
        n0 = (unsigned)text_[0].size();
        sw0 = ttf->stringWidth(text_[0]);
        if (n0 > 0) { cp0 = (unsigned)text_[0][0]; }
        if (n0 > 1) { cp1 = (unsigned)text_[0][1]; }
        if (n0 > 2) { cp2 = (unsigned)text_[0][2]; }
    }
    ESP_LOGI(TAB5_TAG, "TalkBox lines=%u inChars=%u head=%d pos=%d line0=%u w=%d cp=%04x %04x %04x",
             (unsigned)text_.size(), (unsigned)tlen, (int)headId, (int)position,
             n0, sw0, cp0, cp1, cp2);
    setDirty();
}

void TalkBox::handleKeyInput(Node::Key key) {
    switch (key) {
    case KeyOK: case KeySpace: case KeyCancel:
        if (index_ + dispLines_ < text_.size()) {
            index_ += dispLines_;
            while (index_ < text_.size() && text_[index_].empty()) {
                ++index_;
            }
            if (index_ < text_.size()) {
                setDirty();
                break;
            }
        }
        gWindow->endPopup();
        break;
    default:
        break;
    }
}

void TalkBox::layoutBoxes(int &headX, int &headY, int &headW, int &headH,
                          int &textX, int &textY, int &textW, int &textH,
                          int &rowHeight, int &lines) const {
    auto windowBorder = core::config.windowBorder();
    headX = headY = headW = headH = 0;
    if (headTex_) {
        headW = headTex_->width() * headScale_.first / headScale_.second + windowBorder * 2;
        headH = headTex_->height() * headScale_.first / headScale_.second + windowBorder * 2;
    }
    auto *ttf = renderer_->ttf();
    rowHeight = ttf->fontSize() + TextLineSpacing;
    if (headTex_) {
        textW = width_ - headW - windowBorder;
    } else {
        textW = width_;
    }
    auto sz = int(text_.size());
    lines = index_ + dispLines_ > sz ? sz - index_ : dispLines_;
    if (lines < 1) {
        lines = 1;
    }
    textH = rowHeight * lines + windowBorder * 2 - TextLineSpacing;
    if (textH < 40) {
        textH = 40;
    }
    if (position_ % 2) {
        if (headTex_) {
            headY = height_ - headH;
        }
        textY = height_ - textH;
    } else {
        if (headTex_) {
            headY = 0;
        }
        textY = 0;
    }
    if (position_ == 1 || position_ == 4) {
        if (headTex_) {
            headX = width_ - headW;
        }
        textX = 0;
    } else {
        if (headTex_) {
            headX = 0;
            textX = headW + windowBorder;
        } else {
            textX = 0;
        }
    }
}

void TalkBox::makeCache() {
    int headX, headY, headW, headH;
    int textX, textY, textW, textH;
    int rowHeight, lines;
    layoutBoxes(headX, headY, headW, headH, textX, textY, textW, textH, rowHeight, lines);
    auto windowBorder = core::config.windowBorder();

    delete cache_;
    cache_ = nullptr;
    int boxX = textX;
    int boxY = textY;
    int boxW = textW;
    int boxH = textH;
    if (headTex_) {
        boxX = headX < textX ? headX : textX;
        boxY = headY < textY ? headY : textY;
        const int boxR = (headX + headW) > (textX + textW) ? (headX + headW) : (textX + textW);
        const int boxB = (headY + headH) > (textY + textH) ? (headY + headH) : (textY + textH);
        boxW = boxR - boxX;
        boxH = boxB - boxY;
    }
    if (boxW < 160) {
        boxW = 160;
    }
    if (boxH < 40) {
        boxH = 40;
    }
    cache_ = Texture::createAsTarget(renderer_, boxW, boxH);
    if (!cache_ || !cache_->data()) {
        ESP_LOGE(TAB5_TAG, "TalkBox cache failed %dx%d", boxW, boxH);
        delete cache_;
        cache_ = nullptr;
        return;
    }
    cache_->enableBlendMode(true);
    renderer_->setTargetTexture(cache_);
    renderer_->clear(0, 0, 0, 0);
    const int ox = -boxX;
    const int oy = -boxY;
    if (headTex_) {
        renderer_->fillRoundedRect(headX + ox, headY + oy, headW, headH, windowBorder, 64, 64, 64, 208);
        renderer_->drawRoundedRect(headX + ox, headY + oy, headW, headH, windowBorder, 224, 224, 224, 255);
        renderer_->renderTexture(headTex_, headX + ox + windowBorder, headY + oy + windowBorder, headScale_, true);
    }
    renderer_->fillRoundedRect(textX + ox, textY + oy, textW, textH, windowBorder, 64, 64, 64, 208);
    renderer_->drawRoundedRect(textX + ox, textY + oy, textW, textH, windowBorder, 224, 224, 224, 255);
    renderer_->setTargetTexture(nullptr);
    ESP_LOGI(TAB5_TAG, "TalkBox cache %dx%d at %d,%d", boxW, boxH, boxX, boxY);
}

void TalkBox::render() {
    int headX, headY, headW, headH;
    int textX, textY, textW, textH;
    int rowHeight, lines;
    layoutBoxes(headX, headY, headW, headH, textX, textY, textW, textH, rowHeight, lines);
    int boxX = textX;
    int boxY = textY;
    if (headTex_) {
        boxX = headX < textX ? headX : textX;
        boxY = headY < textY ? headY : textY;
    }
    if (cache_ && cache_->data()) {
        renderer_->renderTexture(cache_, x_ + boxX, y_ + boxY, 0, 0,
                                 cache_->width(), cache_->height(), true);
    } else {
        auto windowBorder = core::config.windowBorder();
        if (headTex_) {
            renderer_->fillRoundedRect(x_ + headX, y_ + headY, headW, headH, windowBorder, 64, 64, 64, 208);
            renderer_->drawRoundedRect(x_ + headX, y_ + headY, headW, headH, windowBorder, 224, 224, 224, 255);
            renderer_->renderTexture(headTex_, x_ + headX + windowBorder, y_ + headY + windowBorder, headScale_, true);
        }
        renderer_->fillRoundedRect(x_ + textX, y_ + textY, textW, textH, windowBorder, 64, 64, 64, 208);
        renderer_->drawRoundedRect(x_ + textX, y_ + textY, textW, textH, windowBorder, 224, 224, 224, 255);
    }

    auto *ttf = renderer_->ttf();
    auto windowBorder = core::config.windowBorder();
    int x = x_ + textX + windowBorder;
    int y = y_ + textY + windowBorder;
    ttf->setColor(220, 220, 220);
    auto sz = int(text_.size());
    for (size_t i = dispLines_, idx = index_; i && idx < sz; --i, ++idx, y += rowHeight) {
        ttf->render(text_[idx], x, y, true);
    }
}

}
