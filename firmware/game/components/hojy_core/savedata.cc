/*
 * Heroes of Jin Yong.
 * A reimplementation of the DOS game `The legend of Jin Yong Heroes`.
 * Copyright (C) 2021, Soar Qin<soarchin@gmail.com>

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "savedata.hh"

#include "bag.hh"
#include "content/binary_reader.hh"
#include "content/grpdata.hh"
#include "core/config.hh"
#include "util/file.hh"
#include "esp_log.h"
#include "tab5_platform.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include <sys/stat.h>

namespace hojy::world::state {

namespace {

bool fileExists(const std::string &path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}

/*
 * Move every temporary into place, or leave the slot exactly as it was.
 * FatFs refuses to rename onto a name that already exists, so each destination
 * is moved aside first -- the same order content::AtomicFile uses.
 */
bool commitTemporaries(const std::vector<std::pair<std::string, std::string>> &files) {
    std::vector<std::pair<std::string, std::string>> backups;
    backups.reserve(files.size());
    for (const auto &file: files) {
        if (!fileExists(file.second)) { continue; }
        const auto backup = file.second + ".bak";
        std::remove(backup.c_str());
        if (std::rename(file.second.c_str(), backup.c_str()) != 0) {
            for (const auto &item: backups) {
                std::rename(item.first.c_str(), item.second.c_str());
            }
            return false;
        }
        backups.emplace_back(backup, file.second);
    }
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (std::rename(files[i].first.c_str(), files[i].second.c_str()) == 0) { continue; }
        for (std::size_t j = 0; j < i; ++j) { std::remove(files[j].second.c_str()); }
        for (const auto &item: backups) {
            std::rename(item.first.c_str(), item.second.c_str());
        }
        return false;
    }
    for (const auto &item: backups) { std::remove(item.first.c_str()); }
    return true;
}

/*
 * One .IDX/.GRP pair, written straight to the card.
 *
 * Upstream copies the whole SaveData, turns every record into its own string,
 * then concatenates all of them into a single blob. With this data pack that
 * is 84 x 48KB of layer data three times over -- about 13MB of transient
 * allocation for a 4.5MB save, against 5.7MB free on the device. The copy
 * threw std::bad_alloc out of the menu handler and panicked the board. Here a
 * record goes to the card as soon as it exists and only the offset table
 * (84 x 4 bytes) is kept in memory.
 */
class ArchiveWriter {
public:
    ArchiveWriter() = default;
    ArchiveWriter(const ArchiveWriter &) = delete;
    ~ArchiveWriter() { discard(); }

    bool begin(const std::string &name) {
        grpDest_ = core::config.saveFilePath(name + ".GRP");
        idxDest_ = core::config.saveFilePath(name + ".IDX");
        grpTemp_ = grpDest_ + ".tmp";
        idxTemp_ = idxDest_ + ".tmp";
        std::remove(grpTemp_.c_str());
        std::remove(idxTemp_.c_str());
        group_ = std::fopen(grpTemp_.c_str(), "wb");
        if (group_ == nullptr) {
            ESP_LOGE(TAB5_TAG, "SaveData create %s failed", grpTemp_.c_str());
            return false;
        }
        /* Records are 48KB each; stdio's own buffer would only split them into
         * BUFSIZ-sized writes on the way to FatFs. */
        std::setvbuf(group_, nullptr, _IONBF, 0);
        return true;
    }

    bool append(const void *data, std::size_t size) {
        if (group_ == nullptr) { return false; }
        if (size != 0 && std::fwrite(data, 1, size, group_) != size) {
            ESP_LOGE(TAB5_TAG, "SaveData write %s failed", grpTemp_.c_str());
            return false;
        }
        offset_ += static_cast<std::uint32_t>(size);
        index_.append(reinterpret_cast<const char *>(&offset_), sizeof(offset_));
        return true;
    }

    bool append(Serializable &object) {
        std::string record;
        object.serialize(record);
        return append(record.data(), record.size());
    }

    bool end() {
        if (group_ == nullptr) { return false; }
        const bool flushed = std::fflush(group_) == 0;
        const bool closed = std::fclose(group_) == 0;
        group_ = nullptr;
        if (!flushed || !closed) {
            ESP_LOGE(TAB5_TAG, "SaveData close %s failed", grpTemp_.c_str());
            return false;
        }
        auto *index = std::fopen(idxTemp_.c_str(), "wb");
        if (index == nullptr) {
            ESP_LOGE(TAB5_TAG, "SaveData create %s failed", idxTemp_.c_str());
            return false;
        }
        const bool written = index_.empty()
            || std::fwrite(index_.data(), 1, index_.size(), index) == index_.size();
        const bool indexClosed = std::fclose(index) == 0;
        if (!written || !indexClosed) {
            ESP_LOGE(TAB5_TAG, "SaveData write %s failed", idxTemp_.c_str());
            return false;
        }
        return true;
    }

    void collect(std::vector<std::pair<std::string, std::string>> &files) const {
        files.emplace_back(idxTemp_, idxDest_);
        files.emplace_back(grpTemp_, grpDest_);
    }

    /* Leftovers only: after a successful commit the temporaries no longer
     * exist under these names and the removes are no-ops. */
    void discard() {
        if (group_ != nullptr) {
            std::fclose(group_);
            group_ = nullptr;
        }
        std::remove(grpTemp_.c_str());
        std::remove(idxTemp_.c_str());
    }

private:
    std::string grpDest_, idxDest_, grpTemp_, idxTemp_, index_;
    std::FILE *group_ = nullptr;
    std::uint32_t offset_ = 0;
};

bool readIndexedFile(const std::string &path, std::string &data) {
    util::File file = util::File::open(path);
    if (!file) {
        ESP_LOGE(TAB5_TAG, "SaveData open failed %s", path.c_str());
        return false;
    }
    const auto fileSize = file.size();
    try {
        std::string loaded(static_cast<std::size_t>(fileSize), '\0');
        if (!loaded.empty() && file.read(loaded.data(), loaded.size()) != loaded.size()) {
            ESP_LOGE(TAB5_TAG, "SaveData short read %s", path.c_str());
            return false;
        }
        data = std::move(loaded);
        return true;
    } catch (const std::bad_alloc &) {
        ESP_LOGE(TAB5_TAG, "SaveData bad_alloc %s size=%llu", path.c_str(),
                 (unsigned long long)fileSize);
        return false;
    }
}

/* One record at a time. ALLSIN is 100 x 49KB; a single 4.9MB string OOM. */
bool streamFixedRecords(const std::string &idxName, const std::string &grpName, bool isSave,
                        size_t needed,
                        const std::function<bool(size_t, std::string &&)> &consume) {
    const auto idxPath = isSave ? core::config.saveFilePath(idxName)
                                : core::config.dataFilePath(idxName);
    const auto grpPath = isSave ? core::config.saveFilePath(grpName)
                                : core::config.dataFilePath(grpName);
    std::string indexData;
    if (!readIndexedFile(idxPath, indexData)) {
        return false;
    }
    if (indexData.size() % sizeof(std::uint32_t) != 0) {
        return false;
    }
    const auto count = indexData.size() / sizeof(std::uint32_t);
    if (count < needed) {
        ESP_LOGE(TAB5_TAG, "SaveData %s records=%u need=%u",
                 idxName.c_str(), (unsigned)count, (unsigned)needed);
        return false;
    }
    util::File groupFile = util::File::open(grpPath);
    if (!groupFile) {
        ESP_LOGE(TAB5_TAG, "SaveData open failed %s", grpPath.c_str());
        return false;
    }
    const auto fileSize64 = groupFile.size();
    if (fileSize64 > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    const auto fileSize = static_cast<std::uint32_t>(fileSize64);
    /* Small archives (the event tables, ~440KB) are worth slurping whole. The
     * layer archive is 4.1MB in one piece and must not be: the records are read
     * strictly in order, so streaming costs no seeks at all, while the blob
     * would need a second contiguous 4.1MB block at exactly the moment the old
     * and the new SaveData are both alive. */
    std::string blob;
    const char *blobData = nullptr;
    if (fileSize > 0 && fileSize <= 1048576u) {
        try {
            blob.assign(static_cast<std::size_t>(fileSize), '\0');
            if (groupFile.read(blob.data(), blob.size()) == blob.size()) {
                blobData = blob.data();
                ESP_LOGI(TAB5_TAG, "SaveData %s oneshot %u",
                         grpName.c_str(), (unsigned)fileSize);
            } else {
                blob.clear();
                blob.shrink_to_fit();
                (void)groupFile.seek(0, util::File::Beg);
            }
        } catch (const std::bad_alloc &) {
            blob.clear();
            blob.shrink_to_fit();
            ESP_LOGW(TAB5_TAG, "SaveData %s oneshot bad_alloc, stream",
                     grpName.c_str());
        }
    }
    content::BinaryReader indexReader(indexData);
    std::uint32_t offset = 0;
    std::uint32_t filePos = blobData ? fileSize : 0;
    bool reachedEnd = false;
    for (size_t i = 0; i < count; ++i) {
        std::uint32_t endoffset = 0;
        if (!indexReader.readPod(endoffset)) {
            return false;
        }
        if (endoffset == 0) {
            reachedEnd = true;
            endoffset = fileSize;
        } else if (reachedEnd) {
            return false;
        }
        if (endoffset < offset || endoffset > fileSize) {
            return false;
        }
        if (i < needed) {
            const auto size = static_cast<size_t>(endoffset - offset);
            std::string record;
            if (size > 0) {
                try {
                    if (blobData) {
                        record.assign(blobData + offset, size);
                    } else {
                        if (filePos != offset) {
                            if (groupFile.seek(static_cast<std::int64_t>(offset),
                                               util::File::Beg) != offset) {
                                return false;
                            }
                            filePos = offset;
                        }
                        record.assign(size, '\0');
                        if (groupFile.read(record.data(), record.size()) != record.size()) {
                            return false;
                        }
                        filePos += static_cast<std::uint32_t>(size);
                    }
                } catch (const std::bad_alloc &) {
                    ESP_LOGE(TAB5_TAG, "SaveData record bad_alloc %s i=%u size=%u",
                             grpName.c_str(), (unsigned)i, (unsigned)size);
                    return false;
                }
            }
            if (!consume(i, std::move(record))) {
                return false;
            }
        }
        offset = endoffset;
    }
    if (count != needed) {
        ESP_LOGW(TAB5_TAG, "SaveData extra slots %s maps=%u records=%u (ignored)",
                 grpName.c_str(), (unsigned)needed, (unsigned)count);
    }
    return true;
}

bool copyLayerRecord(SubMapLayerInfo &layer, const std::string &record) {
    auto *dst = layer.operator->();
    if (record.size() != sizeof(*dst)) {
        ESP_LOGE(TAB5_TAG, "SaveData layer size=%u expect=%u",
                 (unsigned)record.size(), (unsigned)sizeof(*dst));
        return false;
    }
    /* Deserialize uses a 49KB stack candidate; the game task stack is 64KB. */
    std::memcpy(dst, record.data(), sizeof(*dst));
    return true;
}

}

SaveData gSaveData;

static void buildSaveFilename(int num, std::string &rangerFile, std::string &sinFile, std::string &defFile) {
    if (num == 0) {
        rangerFile = "RANGER";
        sinFile = "ALLSIN";
        defFile = "ALLDEF";
    } else {
        rangerFile = "R" + std::to_string(num);
        sinFile = "S" + std::to_string(num);
        defFile = "D" + std::to_string(num);
    }
}

bool SaveData::newGame() {
    return load(0);
}

bool SaveData::load(int num) {
    std::string rangerFile, sinFile, defFile;
    buildSaveFilename(num, rangerFile, sinFile, defFile);
    ESP_LOGI(TAB5_TAG, "SaveData::load slot=%d %s/%s/%s", num,
             rangerFile.c_str(), sinFile.c_str(), defFile.c_str());
    tab5_log_memory("save_load");
    ::hojy::content::GrpData::DataSet rangerData;
    if (!::hojy::content::GrpData::loadData(rangerFile, rangerData, num > 0)) {
        ESP_LOGE(TAB5_TAG, "SaveData load %s failed", rangerFile.c_str());
        return false;
    }
    if (rangerData.size() < 6) {
        ESP_LOGE(TAB5_TAG, "SaveData %s entries=%u need>=6",
                 rangerFile.c_str(), (unsigned)rangerData.size());
        return false;
    }

    SaveData loaded;
    if (!loaded.baseInfo.deserialize(rangerData[0])) {
        ESP_LOGE(TAB5_TAG, "SaveData baseInfo deserialize failed size=%u",
                 (unsigned)rangerData[0].size());
        return false;
    }
    if (!loaded.charInfo.deserialize(rangerData[1])) {
        ESP_LOGE(TAB5_TAG, "SaveData charInfo deserialize failed size=%u",
                 (unsigned)rangerData[1].size());
        return false;
    }
    if (!loaded.itemInfo.deserialize(rangerData[2])) {
        ESP_LOGE(TAB5_TAG, "SaveData itemInfo deserialize failed size=%u",
                 (unsigned)rangerData[2].size());
        return false;
    }
    if (!loaded.subMapInfo.deserialize(rangerData[3])) {
        ESP_LOGE(TAB5_TAG, "SaveData subMapInfo deserialize failed size=%u",
                 (unsigned)rangerData[3].size());
        return false;
    }
    if (!loaded.skillInfo.deserialize(rangerData[4])) {
        ESP_LOGE(TAB5_TAG, "SaveData skillInfo deserialize failed size=%u",
                 (unsigned)rangerData[4].size());
        return false;
    }
    if (!loaded.shopInfo.deserialize(rangerData[5])) {
        ESP_LOGE(TAB5_TAG, "SaveData shopInfo deserialize failed size=%u",
                 (unsigned)rangerData[5].size());
        return false;
    }
    rangerData.clear();
    rangerData.shrink_to_fit();

    const auto subMapCount = loaded.subMapInfo.size();
    ESP_LOGI(TAB5_TAG, "SaveData maps=%u stream %s/%s",
             (unsigned)subMapCount, sinFile.c_str(), defFile.c_str());
    tab5_log_memory("save_before_layers");
    try {
        loaded.subMapLayerInfo.resize(subMapCount);
        loaded.subMapEventInfo.resize(subMapCount);
    } catch (const std::bad_alloc &) {
        ESP_LOGE(TAB5_TAG, "SaveData layer resize bad_alloc maps=%u",
                 (unsigned)subMapCount);
        return false;
    }
    tab5_log_memory("save_after_layer_resize");

    if (!streamFixedRecords(sinFile + ".IDX", sinFile + ".GRP", num > 0, subMapCount,
                            [&](size_t i, std::string &&record) {
                                if (!copyLayerRecord(loaded.subMapLayerInfo[i], record)) {
                                    ESP_LOGE(TAB5_TAG, "SaveData layer[%u] failed",
                                             (unsigned)i);
                                    return false;
                                }
                                return true;
                            })) {
        ESP_LOGE(TAB5_TAG, "SaveData load %s failed", sinFile.c_str());
        return false;
    }
    if (!streamFixedRecords(defFile + ".IDX", defFile + ".GRP", num > 0, subMapCount,
                            [&](size_t i, std::string &&record) {
                                if (!loaded.subMapEventInfo[i].deserialize(record)) {
                                    ESP_LOGE(TAB5_TAG, "SaveData event[%u] failed size=%u",
                                             (unsigned)i, (unsigned)record.size());
                                    return false;
                                }
                                return true;
                            })) {
        ESP_LOGE(TAB5_TAG, "SaveData load %s failed", defFile.c_str());
        return false;
    }

    *this = std::move(loaded);
    gBag.syncFromSave();
    ESP_LOGI(TAB5_TAG, "SaveData::load ok slot=%d maps=%u", num, (unsigned)subMapCount);
    tab5_log_memory("save_load_ok");
    return true;
}

bool SaveData::save(int num) {
    if (subMapLayerInfo.size() != subMapInfo.size()
        || subMapEventInfo.size() != subMapInfo.size()) {
        return false;
    }
    std::string rangerFile, sinFile, defFile;
    buildSaveFilename(num, rangerFile, sinFile, defFile);
    tab5_log_memory("save_write");

    /* Only baseInfo is touched by the bag sync, so only baseInfo is copied.
     * Upstream duplicates the entire SaveData here just for this. */
    BaseInfo base = baseInfo;
    gBag.syncTo(*base.operator->());

    try {
        ArchiveWriter ranger, layers, events;
        if (!ranger.begin(rangerFile)
            || !ranger.append(base)
            || !ranger.append(charInfo)
            || !ranger.append(itemInfo)
            || !ranger.append(subMapInfo)
            || !ranger.append(skillInfo)
            || !ranger.append(shopInfo)
            || !ranger.end()) {
            ESP_LOGE(TAB5_TAG, "SaveData write %s failed", rangerFile.c_str());
            return false;
        }
        if (!layers.begin(sinFile)) { return false; }
        for (auto &layer: subMapLayerInfo) {
            if (!layers.append(layer.operator->(), sizeof(SubMapLayerData))) {
                return false;
            }
        }
        if (!layers.end()) { return false; }
        if (!events.begin(defFile)) { return false; }
        for (auto &event: subMapEventInfo) {
            if (!events.append(event.operator->(), sizeof(SubMapEventData))) {
                return false;
            }
        }
        if (!events.end()) { return false; }

        std::vector<std::pair<std::string, std::string>> files;
        files.reserve(6);
        ranger.collect(files);
        layers.collect(files);
        events.collect(files);
        if (!commitTemporaries(files)) {
            ESP_LOGE(TAB5_TAG, "SaveData commit slot=%d failed", num);
            return false;
        }
    } catch (const std::bad_alloc &) {
        ESP_LOGE(TAB5_TAG, "SaveData save bad_alloc slot=%d", num);
        return false;
    }
    gBag.syncToSave();
    ESP_LOGI(TAB5_TAG, "SaveData::save ok slot=%d maps=%u", num,
             (unsigned)subMapInfo.size());
    return true;
}

}
