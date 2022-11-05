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

#include "dependency.h"


Dependency::Dependency(const json &obj) {
    obj.at("Label").get_to(jobLabel);
    if (obj.contains("Required")) {
        obj.at("Required").get_to(isRequired);
    }
    if (obj.contains("StartBefore")) {
        obj.at("StartBefore").get_to(startBefore);
    }
    if (obj.contains("StartAfter")) {
        obj.at("StartAfter").get_to(startAfter);
    }
    if (obj.contains("StopOnFailure")) {
        obj.at("StopOnFailure").get_to(stopOnFailure);
    }
    validateLogic();
}

void Dependency::validateLogic() {
    if (startBefore && startAfter) {
        throw std::logic_error("StartBefore and StartAfter cannot both be true");
    }
}

DependencyList::DependencyList(const json &obj) {
    if (obj.type() != json::value_t::array) {
        throw std::range_error("unsupported JSON type");
    }
    for (const json &item : obj) {
        std::string label;
        if (item.type() == json::value_t::string) {
            label = item.get<std::string>();
            itemsByLabel.insert({{label, Dependency(label)}});
        } else if (item.type() == json::value_t::object) {
            item.at("Label").get_to(label);
            itemsByLabel.insert({{label, Dependency(item)}});
        } else {
            throw std::range_error("unsupported inner JSON type");
        }
    }
}
