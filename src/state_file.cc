/*
 * Copyright (c) 2022 Mark Heily <mark@heily.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <filesystem>
#include <unistd.h>
#include <fstream>
#include "state_file.hpp"

using json = nlohmann::json;

StateFile::StateFile(std::string path, const json &default_value) : dataPath(std::move(path)) {
    // FIXME: this could race with another process
    // add some extra handling for that
    if (std::filesystem::exists(path)) {
        std::ifstream ifs{dataPath};
        currentValue = json::parse(ifs);
    } else {
        currentValue = default_value;
        std::ofstream ofs{dataPath};
        ofs << default_value;
        ofs.close();
    }
}

void StateFile::flushUpdate() {
    if (needUpdating) {
        //FIXME: randomize this filename
        std::string tmpfilepath = dataPath + ".tmp" + std::to_string(getpid());
        std::ofstream ofs{tmpfilepath};
        ofs << currentValue;
        ofs.close();
        rename(tmpfilepath.c_str(), dataPath.c_str());
        // FIXME: cleanup tmpfile if an error occurs.
        needUpdating = false;
    }
}

json &StateFile::value_for_update() {
    needUpdating = true;
    return currentValue;
}

const json &StateFile::value_for_read() const {
    return currentValue;
}

StateFile::~StateFile() {
    if (needUpdating) {
        flushUpdate();
    }
}
