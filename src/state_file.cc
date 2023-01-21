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

#include "state_file.hpp"
#include <filesystem>
#include <fstream>
#include <unistd.h>

using json = nlohmann::json;

StateFile::StateFile(std::string path, json default_value)
    : dataPath(std::move(path)), defaultValue(std::move(default_value)) {
    if (std::filesystem::exists(dataPath)) {
        std::ifstream ifs{dataPath};
        currentValue = json::parse(ifs);
    } else {
        clear();
    }
}

void StateFile::setValue(const json &new_value) const {
    // TODO: randomize this filename
    std::string tmpfilepath =
        std::string{dataPath}.append(".tmp").append(std::to_string(getpid()));
    std::ofstream ofs{tmpfilepath};
    ofs << new_value;
    ofs.close();
    if (rename(tmpfilepath.c_str(), dataPath.c_str()) != 0) {
        std::filesystem::remove(tmpfilepath);
        throw std::system_error(errno, std::system_category(), "rename()");
    }
    currentValue = new_value;
}

const json &StateFile::getValue() const { return currentValue; }

void StateFile::clear() {
    std::ofstream ofs{dataPath};
    ofs << defaultValue;
    ofs.close();
    currentValue = defaultValue;
}
