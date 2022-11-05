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

#pragma once

#include <string>
#include <vector>

#include "../vendor/json.hpp"

using json = nlohmann::json;

struct Dependency {
    std::string jobLabel;
    bool isRequired = true;
    bool startBefore = false;
    bool startAfter = false;
    bool stopOnFailure = false;

    Dependency(std::string label) : jobLabel(std::move(label)) {
        validateLogic();
    }

    Dependency(std::string label, bool required, bool before, bool after, bool stop_on_failure) :
        jobLabel(std::move(label)),
        isRequired(required),
        startBefore(before),
        startAfter(after),
        stopOnFailure(stop_on_failure) {
        validateLogic();
    }

    explicit Dependency(const json& obj);

private:
    void validateLogic();
};

struct DependencyList {
    DependencyList() = default;

    explicit DependencyList(const json& obj);

    [[nodiscard]] const std::unordered_map<std::string, Dependency> &getItemsByLabel() const {
        return itemsByLabel;
    }

private:
    std::unordered_map<std::string, Dependency> itemsByLabel;
};
