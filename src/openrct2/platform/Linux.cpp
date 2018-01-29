#pragma region Copyright (c) 2014-2017 OpenRCT2 Developers
/*****************************************************************************
 * OpenRCT2, an open source clone of Roller Coaster Tycoon 2.
 *
 * OpenRCT2 is the work of many authors, a full list can be found in contributors.md
 * For more information, visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * A full copy of the GNU General Public License can be found in licence.txt
 *****************************************************************************/
#pragma endregion

#include "../common.h"

// Despite the name, this file contains support for more OSs besides Linux, provided the necessary ifdefs remain small.
// Otherwise, they should be spun off into their own files.
#if (defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__)) && !defined(__ANDROID__)

#ifdef __FreeBSD__
#include <sys/sysctl.h>
#endif

#define OPENRCT2_MAX_COMMAND_LENGTH (2 * MAX_PATH)

#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#ifndef NO_TTF
#include <fontconfig/fontconfig.h>
#endif // NO_TTF
#include <fnmatch.h>
#include <locale.h>
#include <pwd.h>

#include "../config/Config.h"
#include "../localisation/Language.h"
#include "../localisation/StringIds.h"
#include "../util/Util.h"
#include "platform.h"

void platform_get_exe_path(utf8 *outPath, size_t outSize)
{
    char exePath[MAX_PATH];
#ifdef __linux__
    ssize_t bytesRead;
    bytesRead = readlink("/proc/self/exe", exePath, MAX_PATH);
    if (bytesRead == -1) {
        log_fatal("failed to read /proc/self/exe");
    }
    exePath[bytesRead - 1] = '\0';
#elif defined(__FreeBSD__)
    size_t exeLen = sizeof(exePath);
    const sint32 mib[] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
    if (sysctl(mib, 4, exePath, &exeLen, NULL, 0) == -1) {
        log_fatal("failed to get process path");

    }
#elif defined(__OpenBSD__)
    // There is no way to get the path name of a running executable.
    // If you are not using the port or package, you may have to change this line!
    strlcpy(exePath, "/usr/local/bin/", sizeof(exePath));
#else
#error "Platform does not support full path exe retrieval"
#endif
    char *exeDelimiter = strrchr(exePath, *PATH_SEPARATOR);
    if (exeDelimiter == nullptr)
    {
        log_error("should never happen here");
        outPath[0] = '\0';
        return;
    }
    *exeDelimiter = '\0';

    safe_strcpy(outPath, exePath, outSize);
}

/**
 * Default directory fallback is:
 *   - (command line argument)
 *   - <exePath>/data
 *   - /usr/local/share/openrct2
 *   - /var/lib/openrct2
 *   - /usr/share/openrct2
 */
void platform_posix_sub_resolve_openrct_data_path(utf8 *out, size_t size) {
    static const utf8 *searchLocations[] = {
        "../share/openrct2",
#ifdef ORCT2_RESOURCE_DIR
        // defined in CMakeLists.txt
        ORCT2_RESOURCE_DIR,
#endif // ORCT2_RESOURCE_DIR
        "/usr/local/share/openrct2",
        "/var/lib/openrct2",
        "/usr/share/openrct2",
    };
    for (auto searchLocation : searchLocations)
    {
        log_verbose("Looking for OpenRCT2 data in %s", searchLocation);
        if (platform_directory_exists(searchLocation))
        {
            safe_strcpy(out, searchLocation, size);
            return;
        }
    }
}

uint16 platform_get_locale_language(){
    const char *langString = setlocale(LC_MESSAGES, "");
    if(langString != nullptr){
        // The locale has the following form:
        // language[_territory[.codeset]][@modifier]
        // (see https://www.gnu.org/software/libc/manual/html_node/Locale-Names.html)
        // longest on my system is 29 with codeset and modifier, so 32 for the pattern should be more than enough
        char pattern[32];
        //strip the codeset and modifier part
        sint32 length = strlen(langString);
        {
            for(sint32 i = 0; i < length; ++i){
                if(langString[i] == '.' || langString[i] == '@'){
                    length = i;
                    break;
                }
            }
        } //end strip
        memcpy(pattern, langString, length); //copy all until first '.' or '@'
        pattern[length] = '\0';
        //find _ if present
        const char *strip = strchr(pattern, '_');
        if(strip != nullptr){
            // could also use '-', but '?' is more flexible. Maybe LanguagesDescriptors will change.
            // pattern is now "language?territory"
            pattern[strip - pattern] = '?';
        }

        // Iterate through all available languages
        for(sint32 i = 1; i < LANGUAGE_COUNT; ++i){
            if(!fnmatch(pattern, LanguagesDescriptors[i].locale, 0)){
                return i;
            }
        }

        //special cases :(
        if(!fnmatch(pattern, "en_CA", 0)){
            return LANGUAGE_ENGLISH_US;
        }
        else if (!fnmatch(pattern, "zh_CN", 0)){
            return LANGUAGE_CHINESE_SIMPLIFIED;
        }
        else if (!fnmatch(pattern, "zh_TW", 0)){
            return LANGUAGE_CHINESE_TRADITIONAL;
        }

        //no exact match found trying only language part
        if(strip != nullptr){
            pattern[strip - pattern] = '*';
            pattern[strip - pattern +1] = '\0'; // pattern is now "language*"
            for(sint32 i = 1; i < LANGUAGE_COUNT; ++i){
                if(!fnmatch(pattern, LanguagesDescriptors[i].locale, 0)){
                    return i;
                }
            }
        }
    }
    return LANGUAGE_ENGLISH_UK;
}

uint8 platform_get_locale_currency(){
    char *langstring = setlocale(LC_MONETARY, "");

    if (langstring == nullptr) {
        return platform_get_currency_value(NULL);
    }

    struct lconv *lc = localeconv();

    return platform_get_currency_value(lc->int_curr_symbol);
}

uint8 platform_get_locale_measurement_format(){
    // LC_MEASUREMENT is GNU specific.
    #ifdef LC_MEASUREMENT
    const char *langstring = setlocale(LC_MEASUREMENT, "");
    #else
    const char *langstring = setlocale(LC_ALL, "");
    #endif

    if(langstring != nullptr){
        //using https://en.wikipedia.org/wiki/Metrication#Chronology_and_status_of_conversion_by_country as reference
        if(!fnmatch("*_US*", langstring, 0) || !fnmatch("*_MM*", langstring, 0) || !fnmatch("*_LR*", langstring, 0)){
            return MEASUREMENT_FORMAT_IMPERIAL;
        }
    }
    return MEASUREMENT_FORMAT_METRIC;
}

bool platform_get_steam_path(utf8 * outPath, size_t outSize)
{
    const char * steamRoot = getenv("STEAMROOT");
    if (steamRoot != nullptr)
    {
        safe_strcpy(outPath, steamRoot, outSize);
        safe_strcat_path(outPath, "steamapps/common", outSize);
        return true;
    }

    char steamPath[1024] = { 0 };
    const char * localSharePath = getenv("XDG_DATA_HOME");
    if (localSharePath != nullptr)
    {
        safe_strcpy(steamPath, localSharePath, sizeof(steamPath));
        safe_strcat_path(steamPath, "Steam/steamapps/common", sizeof(steamPath));
        if (platform_directory_exists(steamPath))
        {
            safe_strcpy(outPath, steamPath, outSize);
            return true;
        }
    }

    const char * homeDir = getpwuid(getuid())->pw_dir;
    if (homeDir != nullptr)
    {
        safe_strcpy(steamPath, homeDir, sizeof(steamPath));
        safe_strcat_path(steamPath, ".local/share/Steam/steamapps/common", sizeof(steamPath));
        if (platform_directory_exists(steamPath))
        {
            safe_strcpy(outPath, steamPath, outSize);
            return true;
        }

        memset(steamPath, 0, sizeof(steamPath));
        safe_strcpy(steamPath, homeDir, sizeof(steamPath));
        safe_strcat_path(steamPath, ".steam/steam/steamapps/common", sizeof(steamPath));
        if (platform_directory_exists(steamPath))
        {
            safe_strcpy(outPath, steamPath, outSize);
            return true;
        }
    }
    return false;
}

#ifndef NO_TTF
bool platform_get_font_path(TTFFontDescriptor *font, utf8 *buffer, size_t size)
{
    assert(buffer != nullptr);
    assert(font != nullptr);

    log_verbose("Looking for font %s with FontConfig.", font->font_name);
    FcConfig* config = FcInitLoadConfigAndFonts();
    if (!config)
    {
        log_error("Failed to initialize FontConfig library");
        FcFini();
        return false;
    }

    FcPattern* pat = FcNameParse((const FcChar8*) font->font_name);

    FcConfigSubstitute(config, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);

    bool found = false;
    FcResult result = FcResultNoMatch;
    FcPattern* match = FcFontMatch(config, pat, &result);

    if (match)
    {
        bool is_substitute = false;

        // FontConfig implicitly falls back to any default font it is configured to handle.
        // In our implementation, this cannot account for supported character sets, leading
        // to unrendered characters (tofu) when trying to render e.g. CJK characters using a
        // Western (sans-)serif font. We therefore ignore substitutions FontConfig provides,
        // and instead rely on exact matches on the fonts predefined for each font family.
        FcChar8* matched_font_face = nullptr;
        if (FcPatternGetString(match, FC_FULLNAME, 0, &matched_font_face) == FcResultMatch &&
            strcmp(font->font_name, (const char *) matched_font_face) != 0)
        {
            log_verbose("FontConfig provided substitute font %s -- disregarding.", matched_font_face);
            is_substitute = true;
        }

        FcChar8* filename = nullptr;
        if (!is_substitute && FcPatternGetString(match, FC_FILE, 0, &filename) == FcResultMatch)
        {
            found = true;
            safe_strcpy(buffer, (utf8*) filename, size);
            log_verbose("FontConfig provided font %s", filename);
        }

        FcPatternDestroy(match);
    }
    else
    {
        log_warning("Failed to find required font.");
    }

    FcPatternDestroy(pat);
    FcConfigDestroy(config);
    FcFini();
    return found;
}
#endif // NO_TTF

#endif
