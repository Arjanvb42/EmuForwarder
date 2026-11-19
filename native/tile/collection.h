#pragma once

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace collection {

constexpr int MaxEntries = 160;

struct Entry {
    std::string system, title, path;
};

struct List {
    std::string id, name;
    std::vector<Entry> entries;
};

static bool field(const std::string &text)
{
    return !text.empty() && text.find_first_of("\t\r\n") == std::string::npos;
}

static bool valid_id(const std::string &id)
{
    return id.size() == 9 && id.compare(0, 4, "EFCL") == 0 &&
           id.find_first_not_of("0123456789", 4) == std::string::npos;
}

static std::string serialize(const List &list)
{
    std::string text = list.id + "\n" + list.name + "\n";
    for (const Entry &e : list.entries)
        text += e.system + "\t" + e.title + "\t" + e.path + "\n";
    return text;
}

static List read(const char *path)
{
    List list;
    FILE *file = fopen(path, "r");
    if (!file)
        return list;

    char line[1024];
    bool truncated = false;
    auto next = [&] {
        if (!fgets(line, sizeof(line), file))
            return false;
        char *newline = std::strchr(line, '\n');
        if (!newline) {
            truncated = true;
            return false;
        }
        *newline = 0;
        return true;
    };
    if (next())
        list.id = line;
    if (next())
        list.name = line;
    bool valid = valid_id(list.id) && field(list.name) && list.name.size() <= 127;
    while (valid && next()) {
        const std::string row(line);
        const size_t a = row.find('\t');
        const size_t b = a == std::string::npos ? a : row.find('\t', a + 1);
        if (b == std::string::npos || list.entries.size() == MaxEntries) {
            valid = false;
            break;
        }
        Entry entry{row.substr(0, a), row.substr(a + 1, b - a - 1), row.substr(b + 1)};
        valid = field(entry.system) && field(entry.title) && field(entry.path) && entry.path.front() == '/';
        if (valid)
            list.entries.push_back(std::move(entry));
    }
    valid = valid && !truncated && feof(file) && !ferror(file) && !list.entries.empty();
    fclose(file);
    return valid ? std::move(list) : List{};
}

}  // namespace collection
