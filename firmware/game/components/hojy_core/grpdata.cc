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
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "grpdata.hh"

#include "core/config.hh"
#include "content/atomic_file.hh"
#include "content/binary_reader.hh"
#include "util/file.hh"
#include "esp_log.h"
#include "tab5_platform.h"

#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace hojy::content {

namespace {

bool readFile(const std::string &path, std::string &data) {
    util::File file = util::File::open(path);
    if (!file) {
        ESP_LOGE(TAB5_TAG, "GRP open failed %s", path.c_str());
        return false;
    }
    const auto fileSize = file.size();
    if (fileSize > std::numeric_limits<std::size_t>::max()
        || fileSize > std::string().max_size()) {
        ESP_LOGE(TAB5_TAG, "GRP size invalid %s size=%llu", path.c_str(),
                 (unsigned long long)fileSize);
        return false;
    }
    try {
        std::string loaded(static_cast<std::size_t>(fileSize), '\0');
        if (!loaded.empty() && file.read(loaded.data(), loaded.size()) != loaded.size()) {
            ESP_LOGE(TAB5_TAG, "GRP short read %s", path.c_str());
            return false;
        }
        data = std::move(loaded);
        return true;
    } catch (const std::bad_alloc &) {
        ESP_LOGE(TAB5_TAG, "GRP bad_alloc %s size=%llu", path.c_str(),
                 (unsigned long long)fileSize);
        return false;
    }
}

/*
 * GRP records are laid out back to back, so after reading record i the file is
 * already positioned at record i+1. newlib's fseek drops the stdio read buffer,
 * which turned a sequential 4 MB read (SMP: ~2500 records) into ~2500 FatFS
 * round trips. Track the position and seek only when it actually differs.
 */
bool readRecord(util::File &file, std::uint64_t &cursor, std::uint32_t offset,
                std::uint32_t endoffset, std::string &out) {
    const auto size = static_cast<size_t>(endoffset - offset);
    if (size == 0) {
        out.clear();
        return true;
    }
    if (cursor != offset) {
        if (file.seek(static_cast<std::int64_t>(offset), util::File::Beg) != offset) {
            return false;
        }
        cursor = offset;
    }
    try {
        std::string loaded(size, '\0');
        if (file.read(loaded.data(), loaded.size()) != loaded.size()) {
            cursor = UINT64_MAX; /* position unknown after a short read */
            return false;
        }
        cursor += size;
        out = std::move(loaded);
        return true;
    } catch (const std::bad_alloc &) {
        return false;
    }
}

}

bool GrpData::loadData(const std::string &idx, const std::string &grp, GrpData::DataSet &dset, bool isSave) {
    std::string indexData;
    const auto idxPath = isSave ? core::config.saveFilePath(idx) : core::config.dataFilePath(idx);
    const auto grpPath = isSave ? core::config.saveFilePath(grp) : core::config.dataFilePath(grp);
    if (!readFile(idxPath, indexData)) {
        return false;
    }
    util::File groupFile = util::File::open(grpPath);
    if (!groupFile) {
        ESP_LOGE(TAB5_TAG, "GRP open failed %s", grpPath.c_str());
        return false;
    }
    const auto fileSize64 = groupFile.size();
    if (fileSize64 > std::numeric_limits<std::uint32_t>::max()) {
        ESP_LOGE(TAB5_TAG, "GRP too large %s size=%llu", grpPath.c_str(),
                 (unsigned long long)fileSize64);
        return false;
    }
    const auto fileSize = static_cast<std::uint32_t>(fileSize64);
    const auto idxSize = indexData.size();
    if (idxSize % sizeof(std::uint32_t) != 0
        || idxSize / sizeof(std::uint32_t) > std::vector<std::string>().max_size()) {
        return false;
    }
    const auto count = static_cast<size_t>(idxSize / sizeof(std::uint32_t));
    try {
        DataSet loaded(count);
        content::BinaryReader indexReader(indexData);
        std::uint32_t offset = 0;
        std::uint64_t cursor = groupFile.pos();
        bool reachedEnd = false;
        for (size_t i = 0; i < count; ++i) {
            std::uint32_t endoffset = 0;
            if (!indexReader.readPod(endoffset)) {
                ESP_LOGE(TAB5_TAG, "GRP idx short %s i=%u", idx.c_str(), (unsigned)i);
                return false;
            }
            if (endoffset == 0) {
                reachedEnd = true;
                endoffset = fileSize;
            } else if (reachedEnd) {
                ESP_LOGE(TAB5_TAG, "GRP idx after end %s i=%u", idx.c_str(), (unsigned)i);
                return false;
            }
            if (endoffset < offset || endoffset > fileSize) {
                ESP_LOGE(TAB5_TAG, "GRP idx range %s i=%u off=%u end=%u size=%u",
                         idx.c_str(), (unsigned)i, (unsigned)offset,
                         (unsigned)endoffset, (unsigned)fileSize);
                return false;
            }
            if (!readRecord(groupFile, cursor, offset, endoffset, loaded[i])) {
                ESP_LOGE(TAB5_TAG, "GRP record read %s i=%u size=%u",
                         grp.c_str(), (unsigned)i, (unsigned)(endoffset - offset));
                return false;
            }
            offset = endoffset;
        }
        if (offset != fileSize) {
            const auto leftover = static_cast<unsigned>(fileSize - offset);
            ESP_LOGW(TAB5_TAG, "GRP leftover %s used=%u file=%u leftover=%u (ignored if <=16)",
                     grp.c_str(), (unsigned)offset, (unsigned)fileSize, leftover);
            if (leftover > 16) {
                return false;
            }
        }
        dset = std::move(loaded);
        return true;
    } catch (const std::bad_alloc &) {
        ESP_LOGE(TAB5_TAG, "GRP parse bad_alloc %s/%s", idx.c_str(), grp.c_str());
        return false;
    }
}

bool GrpData::loadData(const std::string &name, GrpData::DataSet &dset, bool isSave) {
    return loadData(name + ".IDX", name + ".GRP", dset, isSave);
}

bool GrpData::saveData(const std::string &name, const GrpData::DataSet &dset, bool isSave) {
    try {
        std::uint64_t totalSize = 0;
        for (size_t i = 0; i < dset.size(); ++i) {
            const auto &data = dset[i];
            if (data.size() > std::numeric_limits<std::uint64_t>::max() - totalSize) {
                return false;
            }
            totalSize += data.size();
            if (totalSize > std::numeric_limits<std::uint32_t>::max()) {
                return false;
            }
            if (totalSize == 0 && i + 1 != dset.size()) {
                return false;
            }
        }

        if (dset.size() > std::string().max_size() / sizeof(std::uint32_t)) {
            return false;
        }
        std::string indexData;
        std::string groupData;
        indexData.reserve(dset.size() * sizeof(std::uint32_t));
        groupData.reserve(static_cast<std::size_t>(totalSize));
        std::uint32_t offset = 0;
        for (const auto &data: dset) {
            offset += static_cast<std::uint32_t>(data.size());
            indexData.append(reinterpret_cast<const char *>(&offset), sizeof(offset));
            groupData.append(data);
        }

        const auto indexPath = isSave
            ? core::config.saveFilePath(name + ".IDX")
            : core::config.dataFilePath(name + ".IDX");
        const auto groupPath = isSave
            ? core::config.saveFilePath(name + ".GRP")
            : core::config.dataFilePath(name + ".GRP");
        return content::AtomicFile::writePair({
            {indexPath, std::move(indexData)},
            {groupPath, std::move(groupData)},
        });
    } catch (const std::bad_alloc &) {
        return false;
    }
}

}
