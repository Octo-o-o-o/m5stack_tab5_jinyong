/*
 * SPDX-FileCopyrightText: 2021 Soar Qin <soarchin@gmail.com>
 * SPDX-FileCopyrightText: 2026 tab5_jinyong contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

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
 * Overlay of HeroesOfJinYong src/util/conv.cc for ESP32-P4.
 *
 * Upstream keeps the 107KB Big5 table in .data and builds the OpenCC
 * traditional-to-simplified dictionary in Trad2SimpConv's constructor.
 * On Tab5 those run during C++ static init, when only the ~137KB low
 * internal heap exists (high DRAM is still the startup stack). That
 * corrupts TLSF; the first heap walk in reserve_dma_pool then
 * Load-access-faults (seen at MEPC tlsf block_size / MTVAL 0x9fe67bf0).
 *
 * Keep the Big5 source table in .rodata (XIP), sort a heap copy, and
 * delay the OpenCC maps until the first convert() after app_main.
 * Gameplay conversion is unchanged. Do not edit the submodule copy.
 */

#include "conv.hh"

#include <algorithm>
#include <cstring>
#include <utility>

namespace hojy::util {

Big5Conv big5Conv;
Trad2SimpConv trad2SimpConv;

struct PairCompare {
    bool operator()(const Conv::Pair &a1, const Conv::Pair &a2) {
        return a1.from == a2.from ? a1.to < a2.to : a1.from < a2.from;
    }
    bool operator()(std::uint32_t a1, const Conv::Pair &a2) {
        return a1 == a2.from ? 0 < a2.to : a1 < a2.from;
    }
    bool operator()(const Conv::Pair &a1, std::uint32_t a2) {
        return a1.from == a2 ? a1.to < 0 : a1.from < a2;
    }
};

std::wstring Conv::toUnicode(std::string_view str) {
    size_t len = str.length();
    const char *cstr = str.data();
    const char *cstrEnd = cstr + len;
    std::wstring result;
    result.reserve(len);
    while (cstr < cstrEnd) {
        auto c = std::uint8_t(*cstr);
        if (c == 0) { break; }
        if (c < 0x80) {
            result += wchar_t(c);
            ++cstr;
            continue;
        }
        if (cstr + 1 >= cstrEnd) {
            break;
        }
        std::uint32_t charcode = (std::uint16_t(c) << 8) | std::uint8_t(*(cstr + 1));
        auto ite = std::lower_bound(table_, table_ + size_, charcode, PairCompare());
        if (ite >= table_ + size_ || ite->from != charcode) {
            result.append(L"  ");
        } else {
            result += wchar_t(ite->to);
        }
        cstr += 2;
    }
    return result;
}

std::string Conv::fromUnicode(std::wstring_view wstr) {
    size_t len = wstr.length();
    const wchar_t *cstr = wstr.data();
    const wchar_t *cstrEnd = cstr + len;
    std::string result;
    result.reserve(len * 2);
    while (cstr < cstrEnd) {
        auto c = std::uint32_t(*cstr);
        if (c < 0x80) {
            result += char(c);
            ++cstr;
            continue;
        }
        auto ite = std::lower_bound(tableRev_, tableRev_ + size_, c, PairCompare());
        if (ite >= tableRev_ + size_ || ite->from != c) {
            result.append("  ");
        } else {
            result += char(ite->to >> 8);
            result += char(ite->to & 0xFF);
        }
        ++cstr;
    }
    return result;
}

void Conv::postInit() {
    tableRev_ = new Pair[size_];
    for (size_t i = 0; i < size_; ++i) {
        auto &p = table_[i];
        tableRev_[i] = { p.to, p.from };
    }
    std::sort(table_, table_ + size_, PairCompare());
    std::sort(tableRev_, tableRev_ + size_, PairCompare());
}

Big5Conv::Big5Conv() noexcept {
    static const Pair kTable[] =
#include "big5table.inl"
    size_ = sizeof(kTable) / sizeof(Pair);
    table_ = new Pair[size_];
    std::memcpy(table_, kTable, sizeof(Pair) * size_);
    postInit();
}

std::wstring Utf8Conv::toUnicode(std::string_view str) {
    size_t sz = str.size();
    const auto *n = reinterpret_cast<const std::uint8_t*>(str.data());
    size_t i = 0;
    std::wstring res;
    while (i < sz) {
        if (n[i] < 0x80) {
            res += wchar_t(n[i]);
            ++i; continue;
        }
        if (n[i] < 0xE0) {
            if (i + 2 > sz) { break; }
            res += wchar_t((std::uint32_t(n[i] & 0x1F) << 6) | std::uint32_t(n[i + 1] & 0x3F));
            i += 2; continue;
        }
        if (n[i] < 0xF0) {
            if (i + 3 > sz) { break; }
            res += wchar_t((std::uint32_t(n[i] & 0x0F) << 12) | ((std::uint32_t(n[i + 1] & 0x3F) << 6))
                           | (std::uint32_t(n[i + 2] & 0x3F)));
            i += 3; continue;
        }
        if (n[i] < 0xF8) {
            if (i + 4 > sz) { break; }
            if constexpr (sizeof(wchar_t) > 2) {
                res += wchar_t((std::uint32_t(n[i] & 0x07) << 18) | ((std::uint32_t(n[i + 1] & 0x3F) << 12))
                               | ((std::uint32_t(n[i + 2] & 0x3F) << 6)) | (std::uint32_t(n[i + 3] & 0x3F)));
            }
            i += 4; continue;
        }
        if (n[i] < 0xFC) {
            if (i + 5 > sz) { break; }
            if constexpr (sizeof(wchar_t) > 2) {
                res += wchar_t((std::uint32_t(n[i] & 0x03) << 24) | ((std::uint32_t(n[i + 1] & 0x3F) << 18))
                               | ((std::uint32_t(n[i + 2] & 0x3F) << 12)) | ((std::uint32_t(n[i + 3] & 0x3F) << 6))
                               | (std::uint32_t(n[i + 4] & 0x3F)));
            }
            i += 5; continue;
        }
        if (i + 6 > sz) { break; }
        if constexpr (sizeof(wchar_t) > 2) {
            res += wchar_t((std::uint32_t(n[i] & 0x01) << 30) | ((std::uint32_t(n[i + 1] & 0x3F) << 24))
                           | ((std::uint32_t(n[i + 2] & 0x3F) << 18)) | ((std::uint32_t(n[i + 3] & 0x3F) << 12))
                           | ((std::uint32_t(n[i + 4] & 0x3F) << 6)) | (std::uint32_t(n[i + 5] & 0x3F)));
        }
        i += 6;
    }
    return res;
}

std::string Utf8Conv::fromUnicode(std::wstring_view wstr) {
    std::string res;
    for (auto &ch: wstr) {
        if (ch < 0x80) {
            res += char(ch);
            continue;
        }
        if (ch < 0x800) {
            res += char(0xC0 | (ch >> 6));
            res += char(0x80 | (ch & 0x3F));
            continue;
        }
        if constexpr (sizeof(wchar_t) > 2) {
            if (ch < 0x10000) {
                res += char(0xE0 | (ch >> 12));
                res += char(0x80 | ((ch >> 6) & 0x3F));
                res += char(0x80 | (ch & 0x3F));
                continue;
            }
            if (ch < 0x200000) {
                res += char(0xF0 | (ch >> 18));
                res += char(0x80 | ((ch >> 12) & 0x3F));
                res += char(0x80 | ((ch >> 6) & 0x3F));
                res += char(0x80 | (ch & 0x3F));
                continue;
            }
            if (ch < 0x4000000) {
                res += char(0xF8 | (ch >> 24));
                res += char(0x80 | ((ch >> 18) & 0x3F));
                res += char(0x80 | ((ch >> 12) & 0x3F));
                res += char(0x80 | ((ch >> 6) & 0x3F));
                res += char(0x80 | (ch & 0x3F));
                continue;
            }
            res += char(0xFC | (ch >> 30));
            res += char(0x80 | ((ch >> 24) & 0x3F));
            res += char(0x80 | ((ch >> 18) & 0x3F));
            res += char(0x80 | ((ch >> 12) & 0x3F));
            res += char(0x80 | ((ch >> 6) & 0x3F));
            res += char(0x80 | (ch & 0x3F));
        }
    }
    return res;
}

Trad2SimpConv::Trad2SimpConv() noexcept {}

/*
 * 4014 pairs, 32 KB. Written as an initialiser list inside convert() the
 * backing array is a stack temporary, and together with the phrase table below
 * that made a 45.6 KB frame -- on a 64 KB game task stack, with the first call
 * happening wherever the first line of text gets converted. In .rodata it is
 * XIP'd from flash and costs no stack and no heap copy.
 */
const std::pair<std::uint32_t, std::uint32_t> kTsChars[] = {
#include "tschars.inl"
};

std::wstring Trad2SimpConv::convert(const std::wstring &str) {
    static bool ready = false;
    if (!ready) {
        ready = true;
        charTable_.reserve(sizeof(kTsChars) / sizeof(kTsChars[0]));
        for (const auto &p: kTsChars) {
            charTable_.emplace(p.first, p.second);
        }
        /* The phrase table stays a local initialiser list. Hoisting it to a
         * static object puts it in .bss and builds it during C++ static init,
         * which on this chip runs before the high DRAM heaps exist -- the very
         * thing the note at the top of this file is about. 13 KB of stack in a
         * function called once from Strings::load is the safer half. */
        std::vector<std::pair<std::vector<std::uint32_t>, std::vector<std::uint32_t>>> wordTable = {
#include "tswords.inl"
        };
        for (auto &p: wordTable) {
            auto *node = &root_;
            for (auto c: p.first) {
                node = &node->nodes[c];
            }
            node->word = std::move(p.second);
        }
    }
    std::wstring res;
    size_t sz = str.size();
    res.reserve(sz);
    for (size_t i = 0; i < sz;) {
        {
            auto *node = &root_;
            size_t j = i;
            bool notfound = false;
            while (j < sz) {
                auto c = str[j++];
                auto ite = node->nodes.find(c);
                if (ite == node->nodes.end()) {
                    notfound = true;
                    break;
                }
                node = &ite->second;
            }
            if (!notfound && !node->word.empty()) {
                res.insert(res.end(), node->word.begin(), node->word.end());
                i = j;
                continue;
            }
        }
        auto c = str[i];
        auto ite = charTable_.find(c);
        if (ite == charTable_.end()) res += c;
        else res += wchar_t(ite->second);
        ++i;
    }
    return res;
}

}
