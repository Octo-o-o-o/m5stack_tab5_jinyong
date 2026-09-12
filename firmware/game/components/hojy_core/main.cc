/*
 * Overlay of HeroesOfJinYong src/main.cc for ESP32-P4.
 * FatFS has no chdir; load config from the absolute SD game root.
 */

#include "core/config.hh"
#include "app/application.hh"
#include "content/loader.hh"
#include "world/strings.hh"

#include <cstdio>
#include <cstdlib>

#ifndef HOJY_TAB5_GAME_ROOT
#define HOJY_TAB5_GAME_ROOT "/sdcard/jinyong"
#endif

using namespace hojy;

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    if (!core::config.load(HOJY_TAB5_GAME_ROOT "/config.toml")) {
        return EXIT_FAILURE;
    }
    const auto optionsFile = core::config.saveFilePath("options.toml");
    if (FILE *opt = std::fopen(optionsFile.c_str(), "r")) {
        std::fclose(opt);
        core::config.load(optionsFile);
    }
    if (!core::config.postLoad()) { return EXIT_FAILURE; }
    if (!::hojy::world::state::gStrings.load("strings.toml")) { return EXIT_FAILURE; }
    core::config.fixOnTextLoaded();
    if (!::hojy::content::loadData()) { return EXIT_FAILURE; }
    app::Application application(core::config.windowWidth(), core::config.windowHeight(),
                                 core::config.animationSpeed());
    return application.run();
}
