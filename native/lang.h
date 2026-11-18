#pragma once
// The language on screen: English by default, Dutch as an option (Settings > Language).
// TR("English", "Nederlands") picks the text for the current language. The choice is the line
// "taal=en" or "taal=nl" in /data/PS4Tegelmaker/settings.ini, which EFUpdater reads too.
// (The log stays in Dutch: it is for developers.)

#include <stdio.h>
#include <string.h>

namespace lang {

static bool dutch = false;

// format_arg: the compiler checks a format string through TR like a literal (the English one; the
// Dutch one has the same placeholders, checked by tools/check_lang.py).
static inline __attribute__((format_arg(1))) const char *T(const char *english, const char *nederlands)
{
    return dutch ? nederlands : english;
}

// Read the language from the settings file (EmuForwarder's online::load_settings also sets it).
static void load(const char *settings_file = "/data/PS4Tegelmaker/settings.ini")
{
    FILE *file = fopen(settings_file, "r");
    if (!file)
        return;
    char line[256];
    while (fgets(line, sizeof(line), file))
        if (!strncmp(line, "taal=", 5))
            dutch = !strncmp(line + 5, "nl", 2);
    fclose(file);
}

}  // namespace lang

#define TR(english, nederlands) lang::T(english, nederlands)
