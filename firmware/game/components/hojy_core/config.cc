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
 * Overlay of HeroesOfJinYong src/core/config.cc for ESP32-P4.
 * If config.toml has no pre_path, prefix every game path with the SD root.
 * Gameplay config keys are unchanged.
 */

#include "config.hh"

#include "world/strings.hh"
#include "resourcemgr.hh"
#include "util/conv.hh"
#include "util/file.hh"
#include "util/math.hh"
#include "esp_log.h"
#include "tab5_platform.h"
#include <algorithm>
#include <external/toml.hpp>
#include <fmt/format.h>
#include <fstream>

namespace hojy::core {

Config config;

bool Config::load(const std::string &filename) {
    auto file = util::File::open(filename);
    if (!file) {
        fmt::print(stderr, "Unable to open config file: {}\n", filename);
        return false;
    }
    std::string content(static_cast<size_t>(file.size()), '\0');
    if (!content.empty() && file.read(content.data(), content.size()) != content.size()) {
        fmt::print(stderr, "Unable to read config file: {}\n", filename);
        return false;
    }
    toml::table tbl;
    try {
        tbl = toml::parse(content);
    } catch (const toml::parse_error &err) {
        fmt::print("Parsing failed: {}\n", err.description());
        return false;
    }
    auto main = tbl["main"];
    if (main) {
        prePath_ = main["pre_path"].value_or(std::move(prePath_));
        if (prePath_.empty()) {
            prePath_ = TAB5_SD_GAME_ROOT "/";
        }
        auto dpath = main["data_path"];
        if (dpath.is_string()) {
            dataPath_.resize(1);
            dataPath_[0] = dpath.value_or<std::string>(".");
        } else if (dpath.is_array()) {
            dataPath_.clear();
            for (auto &p : *dpath.as_array()) {
                dataPath_.emplace_back(p.value_or<std::string>("."));
            }
        }
        if (!dataPath_.empty()) {
            dataPath_[0] = prePath_ + dataPath_[0];
        }
        musicPath_ = prePath_ + main["music_path"].value_or(std::move(musicPath_));
        soundPath_ = prePath_ + main["sound_path"].value_or(std::move(soundPath_));
        savePath_ = prePath_ + main["save_path"].value_or(std::move(savePath_));
        auto fonts = main["fonts"];
        if (fonts.is_string()) {
            fonts_ = {fonts.value_or<std::string>("")};
        } else if (fonts.is_array()) {
            fonts_.clear();
            for (auto &p : *fonts.as_array()) {
                fonts_.emplace_back(p.value_or<std::string>(""));
            }
        }
        if (!fonts_.empty()) {
            fonts_[0] = prePath_ + fonts_[0];
        }
        {
            std::vector<std::string> fontCandidates;
            if (!fonts_.empty()) {
                fontCandidates.push_back(fonts_[0]);
            }
            fontCandidates.push_back(prePath_ + "fonts/chinese.otf");
            fontCandidates.push_back(prePath_ + "data/font/chinese.otf");
            fontCandidates.push_back(prePath_ + "data/fonts/chinese.otf");
            std::string chosen;
            for (const auto &candidate : fontCandidates) {
                if (candidate.empty()) {
                    continue;
                }
                auto file = util::File::open(candidate);
                if (file) {
                    chosen = candidate;
                    break;
                }
            }
            if (!chosen.empty()) {
                fonts_ = {chosen};
                ESP_LOGI(TAB5_TAG, "Using font %s", chosen.c_str());
            } else {
                ESP_LOGE(TAB5_TAG, "No readable font file");
                for (const auto &candidate : fontCandidates) {
                    ESP_LOGE(TAB5_TAG, "  tried %s", candidate.c_str());
                }
            }
        }
        shipLogicEnabled_ = main["ship_logic_enabled"].value_or<bool>(std::forward<bool>(shipLogicEnabled_));
    }
    auto window = tbl["window"];
    if (window) {
        windowWidth_ = window["width"].value_or<int>(std::forward<int>(windowWidth_));
        windowHeight_ = window["height"].value_or<int>(std::forward<int>(windowHeight_));
        showFPS_ = window["show_fps"].value_or<bool>(std::forward<bool>(showFPS_));
        limitFPS_ = window["limit_fps"].value_or<int>(std::forward<int>(limitFPS_));
    }
    auto ui = tbl["ui"];
    if (ui) {
        simplifiedChinese_ = ui["simplified_chinese"].value_or<bool>(std::forward<bool>(simplifiedChinese_));
        showPotential_ = ui["show_potential"].value_or<bool>(std::forward<bool>(showPotential_));
        showMapMiniPanel_ = ui["show_map_mini_panel"].value_or<bool>(std::forward<bool>(showMapMiniPanel_));
        showMinimap_ = ui["show_minimap"].value_or<bool>(std::forward<bool>(showMinimap_));
        auto scale = ui["scale"].value<double>();
        if (scale) {
            scale_ = util::calcSmallestDivision(*scale);
        }
        animationSpeed_ = ui["animation_speed"].value_or<float>(std::forward<float>(animationSpeed_));
        fadeSpeed_ = ui["fade_speed"].value_or<float>(std::forward<float>(fadeSpeed_));
        windowBorder_ = ui["window_border"].value_or<int>(std::forward<int>(windowBorder_));
        noNameInput_ = ui["no_name_input"].value_or<bool>(std::forward<bool>(noNameInput_));
        /* Tab5 has no IME, so a Chinese main-character name cannot be typed.
         * Let it be set here instead; the name prompt starts pre-filled with it
         * and plain Enter accepts it. Glyphs must exist in the subset font. */
        if (auto name = ui["default_name"].value<std::string>()) {
            const auto wide = util::Utf8Conv::toUnicode(*name);
            if (!wide.empty()) {
                defaultName_ = wide;
                ESP_LOGI(TAB5_TAG, "default_name set from config (%u chars)",
                         (unsigned)wide.size());
            }
        }
    }
    auto audio = tbl["audio"];
    if (audio) {
        auto emu = audio["opl_emulator"].value<std::string>();
        if (emu) {
            oplEmulator_ = emu.value();
        }
        sampleRate_ = audio["sample_rate"].value_or<int>(std::forward<int>(sampleRate_));
        auto formatStr = audio["sample_format"].value<std::string>();
        if (formatStr) {
            sampleFormat_ = formatStr == "I32" ? 1 : (formatStr == "F32" ? 2 : 0);
        }
        musicVolume_ = audio["music_volume"].value_or<int>(std::forward<int>(musicVolume_));
        soundVolume_ = audio["sound_volume"].value_or<int>(std::forward<int>(soundVolume_));
    }

    auto fixPath = [](std::string &path) {
        if (!path.empty() && path.back() != '/') { path += '/'; }
    };
    fixPath(musicPath_);
    fixPath(soundPath_);
    fixPath(savePath_);
    for (auto &path : dataPath_) {
        fixPath(path);
    }
    return true;
}

bool Config::saveOptions(const std::string &filename) const {
    auto tbl = toml::table{{
        {
            "ui",
            toml::table{{
                {"show_map_mini_panel", showMapMiniPanel_},
                {"show_minimap", showMinimap_},
            }},
        },
        {
            "audio",
            toml::table{{
                {"music_volume", musicVolume_},
                {"sound_volume", soundVolume_},
            }},
        },
    }};
    std::ofstream fs(filename);
    if (!fs.is_open()) { return false; }
    fs << tbl;
    fs.close();
    return true;
}

bool Config::postLoad() {
    if (dataPath_.empty() || fonts_.empty()) {
        fmt::print(stderr, "Config must define non-empty main.data_path and main.fonts values.\n");
        return false;
    }
    if (limitFPS_ == 0) { limitFPS_ = 60; }
    musicVolume_ = std::clamp(musicVolume_, 0, 8);
    soundVolume_ = std::clamp(soundVolume_, 0, 8);

    gResourceMgr.init();
    const auto &missingFiles = gResourceMgr.missingFiles();
    if (!missingFiles.empty()) {
        fmt::print(stderr, "Missing resource files:\n");
        for (auto &fn : missingFiles) {
            fmt::print(stderr, "  {}\n", fn);
        }
        fflush(stderr);
        return false;
    }
    return true;
}

void Config::fixOnTextLoaded() {
    if (defaultName_.empty()) {
        defaultName_ = GETTEXT(0);
    }
}

std::string Config::dataFilePath(const std::string &filename) const {
    auto fn = gResourceMgr.getFilePath(filename);
    if (!fn.empty()) { return fn; }
    if (dataPath_.empty()) {
        return filename;
    }
    return dataPath_[0] + filename;
}

std::string Config::musicFilePath(const std::string &filename) const {
    auto fn = gResourceMgr.getFilePath(filename);
    if (!fn.empty()) { return fn; }
    return musicPath_.empty() ? dataFilePath(filename) : musicPath_ + filename;
}

std::string Config::soundFilePath(const std::string &filename) const {
    auto fn = gResourceMgr.getFilePath(filename);
    if (!fn.empty()) { return fn; }
    return soundPath_.empty() ? dataFilePath(filename) : soundPath_ + filename;
}

std::string Config::saveFilePath(const std::string &filename) const {
    auto fn = gResourceMgr.getFilePath(filename);
    if (!fn.empty()) { return fn; }
    return savePath_.empty() ? dataFilePath(filename) : savePath_ + filename;
}

}// namespace hojy::core
