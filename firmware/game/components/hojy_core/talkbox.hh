#pragma once

#include "nodewithcache.hh"

#include <cstdint>
#include <string>
#include <vector>

namespace hojy::scene {

class TalkBox: public NodeWithCache {
public:
    using NodeWithCache::NodeWithCache;

    void popup(const std::wstring &text, std::int16_t headId, std::int16_t position);

    void handleKeyInput(Key key) override;
    void render() override;

private:
    void makeCache() override;
    void layoutBoxes(int &headX, int &headY, int &headW, int &headH,
                     int &textX, int &textY, int &textW, int &textH,
                     int &rowHeight, int &lines) const;

private:
    std::vector<std::wstring> text_;
    const Texture *headTex_ = nullptr;
    std::pair<int, int> headScale_ = {2, 1};
    std::int16_t position_ = 0;
    int index_ = 0, dispLines_ = 0;
};

}
