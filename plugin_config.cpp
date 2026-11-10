// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
/*
* Copyright (c) 2017-2025, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
*
* SPDX-License-Identifier: GPL-3.0-only
* This file is part of CPMimg and is licensed under GNU GPL v3; see LICENSE.txt.
*/

#include "plugin_config.h"
#include "string_tools.h"

#include "libdsk.h"

#ifdef _WIN32
#include "sysio_winapi.h"
#endif 

#include <algorithm>
#include <memory>
#include <cassert>
#include <optional>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {
    using parse_string_ret_t = std::pair<std::string, std::optional<std::string>>;

    parse_string_ret_t parse_string(std::string arg) { // Note: we need copy here -- let compiler create it for us
        constexpr char separator = '='; // Just for the readability
        constexpr char commenter1 = '#';
        constexpr char commenter2 = '['; // Skip sections for now

        auto comment_pos = arg.find(commenter1);
        if (comment_pos != std::string::npos)
            arg.erase(comment_pos); //! Remove comment
        comment_pos = arg.find(commenter2);
        if (comment_pos != std::string::npos)
            arg.erase(comment_pos); //! Remove sections

        auto sep_pos = arg.find(separator);
        if (sep_pos == std::string::npos) {
            return parse_string_ret_t{ trim(arg), std::nullopt };
        }
        auto left_part = arg.substr(0, sep_pos);
        auto right_part = arg.substr(sep_pos + 1, std::string::npos);
        return parse_string_ret_t{ trim(left_part), trim(right_part) };
    }

    std::optional<std::string> expand_env_vars(const std::string& text) {
#ifdef _WIN32
        // Sized by the API rather than MAX_PATH: a long result must reach the
        // caller's length check instead of being truncated into a wrong path.
        const DWORD needed = ExpandEnvironmentStringsA(text.c_str(), nullptr, 0);
        if (needed == 0)
            return std::nullopt;
        // The ANSI version needs one byte more than it reports (documented).
        const DWORD size = needed + 1;
        std::string result(size, '\0');
        const DWORD written = ExpandEnvironmentStringsA(
            text.c_str(), result.data(), size);
        if (written == 0 || written > size)
            return std::nullopt;
        // The ANSI API may report more than it wrote, so measure the text.
        result.resize(std::strlen(result.c_str()));
        return result;
#else
        return text;
#endif
    }

    // Windows leaves an undefined %NAME% in place without reporting an error.
    bool has_unexpanded_variable(const std::string& text) {
        const auto first = text.find('%');
        if (first == std::string::npos)
            return false;
        const auto second = text.find('%', first + 1);
        return second != std::string::npos && second > first + 1;
    }

    bool is_absolute_path(const std::string& path) {
        return !path.empty() &&
            (path[0] == '\\' || path[0] == '/' ||
                (path.size() >= 2 && path[1] == ':'));
    }

}



int plugin_config_t::file_to_options_map(std::FILE* cf) {
    using namespace std::string_literals;
    int line_size = 1024; // Limit for the string 
    std::unique_ptr<char[]> line_ptr = std::make_unique<char[]>(line_size);
    while ( std::fgets(line_ptr.get(), line_size, cf) ) {
        auto pr = parse_string(line_ptr.get());
        if (pr.first.empty()) {
            if (!pr.second)
                continue;
            else
                return 1;  // throw std::runtime_error{ "Wrong config line -- no option name: "s + line_ptr.get() }; // "=..."
        }
        else if (!pr.second) {
            return 2;      // throw std::runtime_error{ "Wrong config line -- no '=': "s + line_ptr.get() }; // "abc" -- no '='
        }
        else if (pr.second->empty()) {
            return 3;      // throw std::runtime_error{ "Wrong config option value: "s + line_ptr.get() }; // "key="
        }
        if (options_map.count(pr.first)) {
            return 4;      // throw std::runtime_error{ "Duplicated option name: "s + pr.first + " = " //-V112
                           //             + *pr.second + "; prev. val: " + options_map[pr.first] };
        }
        options_map[pr.first] = *pr.second;
    }
    return 0;
}

template<typename T>
T plugin_config_t::get_option_from_map(const std::string& option_name) const {
    if (options_map.count(option_name) == 0) {
        throw std::runtime_error("Option not found: " + option_name); 
    }
    auto elem_itr = options_map.find(option_name);
    if (elem_itr != options_map.end()) {
        return from_str<T>(elem_itr->second);
    }
    else {
        throw std::runtime_error{ "Option " + option_name + " not found" };
    }
}



void plugin_config_t::set_default_diskdefs_path()
{
    diskdefs_path_problem.clear();
    resolve_configured_path(
        "diskdefs_file_path",
        diskdefs_file_setting,
        plugin_path.data(),
        diskdefs_file_path,
        diskdefs_path_problem);
    // cpmtools reads this global; set it wherever the path is resolved, as a
    // read_conf() that fails early would otherwise leave it NULL, and
    // fopen(NULL) crashes TCmd.
    diskdefs_path = diskdefs_file_path.data();
}

// Also kept in `problem`, because the log may be off or be the broken part,
// and describe_path_problems() needs the reason for the user.
void plugin_config_t::report_config_problem(
    const std::string& message, std::string& problem)
{
    debug_print("Warning# {}", message);
    log_print("Warning# %s", message.c_str());
    if (!problem.empty())
        problem += "\n";
    problem += message;
}

std::string plugin_config_t::describe_path_problems(bool* cannot_mount) const
{
    std::string text;

    // Checked once, so the text and the caller's decision cannot disagree.
    std::FILE* diskdefs = diskdefs_file_path.is_empty()
        ? nullptr
        : std::fopen(diskdefs_file_path.data(), "r");
    if (diskdefs)
        std::fclose(diskdefs);
    if (cannot_mount)
        *cannot_mount = diskdefs == nullptr;

    if (!diskdefs) {
        text += "The disk definitions file cannot be opened, so disk images "
            "cannot be mounted.\n";
        text += "diskdefs_file_path=" + diskdefs_file_setting + "\n";
        text += "Resolved path: ";
        text += diskdefs_file_path.is_empty() ? "(none)" : diskdefs_file_path.data();
        text += "\n";
        if (!diskdefs_path_problem.empty())
            text += diskdefs_path_problem + "\n";
    }

    if (!log_path_problem.empty()) {
        if (!text.empty())
            text += "\n";
        text += "Logging is off because the log file cannot be opened.\n";
        text += "log_file_path=" + log_file_setting + "\n";
        text += log_path_problem + "\n";
    }

    if (!image_format_problem.empty()) {
        if (!text.empty())
            text += "\n";
        text += "The image_format setting was ignored.\n";
        text += image_format_problem + "\n";
        text += "The format ";
        text += image_format.data();
        text += " is used instead.\n";
    }

    if (!text.empty()) {
        text += "\nPlease check these settings in ";
        text += config_file_path.data();
        text += ".";
    }
    return text;
}

// On failure `resolved` is left empty, so the file simply fails to open
// instead of a truncated path pointing at some other file.
bool plugin_config_t::resolve_configured_path(
    const char* option_name,
    const std::string& setting,
    const char* base_dir,
    minimal_fixed_string_t<MAX_PATH>& resolved,
    std::string& problem)
{
    resolved.clear();

    const auto expanded = expand_env_vars(setting);
    if (!expanded) {
        report_config_problem(std::string{ option_name } +
            ": cannot expand environment variables in: " + setting, problem);
        return false;
    }
    if (has_unexpanded_variable(*expanded)) {
        report_config_problem(std::string{ option_name } +
            ": possibly undefined environment variable in: " + *expanded, problem);
    }

    std::string full_path;
    if (!is_absolute_path(*expanded)) {
        full_path = base_dir;
        if (!full_path.empty() && full_path.back() != get_path_separator())
            full_path.push_back(get_path_separator());
    }
    full_path += *expanded;

    if (full_path.size() > resolved.capacity()) {
        report_config_problem(std::string{ option_name } +
            ": path is too long: " + full_path, problem);
        return false;
    }
    resolved.push_back(full_path.c_str());
    return true;
}

void plugin_config_t::open_log_file()
{
    log_open_attempted = true;
    opened_log_setting = log_file_setting;

    log_path_problem.clear();
    const bool resolved = resolve_configured_path(
        "log_file_path",
        log_file_setting,
        config_dir.data(),
        log_file_path,
        log_path_problem);
    if (resolved)
        log_file = open_file_overwrite(log_file_path.data());
    log_open_failed = !resolved || log_file == file_open_error_v;
    if (log_open_failed) {
        allow_txt_log = false;
        report_config_problem(
            std::string{ "log_file_path: cannot open the log file: " } +
                (resolved ? log_file_path.data() : log_file_setting.c_str()),
            log_path_problem);
    }
    else {
        // A "possibly undefined variable" note is only a guess (a file name
        // may contain '%'); once the file opened, it must not claim that
        // logging is off.
        log_path_problem.clear();
    }
}

bool plugin_config_t::read_conf(const PackDefaultParamStruct* dps, bool reread)
{
    using namespace std::literals::string_literals; 
    if (!reread) {
        config_file_path.push_back(dps->DefaultIniName);
        auto slash_idx = config_file_path.find_last(get_path_separator());
        if (slash_idx == config_file_path.npos)
            slash_idx = 0;
        config_file_path.shrink_to(slash_idx);
        config_dir = config_file_path;
        config_file_path.push_back(inifilename);

        plugin_interface_version_hi = dps->PluginInterfaceVersionHi;
        plugin_interface_version_lo = dps->PluginInterfaceVersionLow;
    }

    std::FILE* cf = std::fopen( config_file_path.data(), "r");
    if (!cf) {
        set_default_diskdefs_path();
        return false; // Use default configuration
    }

    // A failed earlier read can leave options behind; they would then count
    // as duplicates and make every later read fail.
    options_map = options_map_t{};
    auto res = file_to_options_map(cf);
    if (res) {
        std::fclose(cf);
        return false; // Wrong configuration would be overwritten by default configuration.
    }
    try {
        allow_txt_log = get_option_from_map<decltype(allow_txt_log)>("allow_txt_log"s);
        debug_level = get_option_from_map<decltype(debug_level)>("debug_level"s);

        // Read even with logging off, or the next save would lose this value.
        try {
            log_file_setting = get_option_from_map<std::string>("log_file_path"s);
        }
        catch (const std::exception&) { //-V565
            // Keep the current value, initially the default.
        }

        // A failed open is retried on each reread, and a changed path is
        // reopened, so that editing the INI takes effect without restarting
        // TCmd; otherwise a working log is opened only once.
        if (log_open_attempted &&
            (log_open_failed || log_file_setting != opened_log_setting)) {
            if (!log_open_failed && log_file != file_handle_t())
                close_file(log_file);
            log_file = file_handle_t();
            log_open_attempted = false;
        }
        if (!allow_txt_log) {
            log_path_problem.clear();
            log_open_failed = false;
        }

        // Before the other paths, so that their problems reach the log.
        if (allow_txt_log && !log_open_attempted) {
            open_log_file();
        }

        diskdefs_file_setting = get_option_from_map<std::string>("diskdefs_file_path"s);
		set_default_diskdefs_path();

        // Optional like the options below: a missing line keeps the current
        // format instead of invalidating the whole INI, and an over-long name
        // is reported and ignored instead of overflowing the fixed buffer.
        image_format_problem.clear();
        try {
            auto tf = get_option_from_map<std::string>("image_format"s);
            if (tf.size() > image_format.capacity()) {
                report_config_problem(
                    "image_format: longer than " +
                        std::to_string(image_format.capacity()) +
                        " characters, ignored: " + tf,
                    image_format_problem);
            }
            else {
                image_format.clear();
                image_format.push_back(tf.c_str());
            }
        }
        catch (const std::exception&) { //-V565
            // Keep the current format.
        }

        // Optional in older configuration files.
        try {
            show_disk_info_file = get_option_from_map<
                decltype(show_disk_info_file)>(
                    "show_disk_info_file"s);
        }
        catch (const std::exception&) { //-V565
            // Keep the default: enabled. 
        }


        // Backward compatible with older INI files.
        try {
            enable_format_probing = get_option_from_map<
                decltype(enable_format_probing)>(
                    "enable_format_probing"s);
        }
        catch (const std::exception&) { //-V565
            // Keep the in-class default (enabled).
        }
    }
    catch (std::exception& ex) {
        (void)ex;
        std::fclose(cf);
        return false; 
    }
       
    std::fclose(cf);
    options_map = options_map_t{}; // Optimization -- options_map.clear() can leave allocated internal structures

    return validate_conf();
}

bool plugin_config_t::validate_conf() {
    if (debug_level > 2)
        return false;
    return true;
}

bool plugin_config_t::write_conf()
{
    assert(validate_conf() && "Inconsisten fatdiskimg plugin configuration");

    std::FILE* cf = std::fopen(config_file_path.data(), "w");

    //std::ofstream cf{ config_file_path.data() };
    if ( !cf ) {
        return false;
    }
    fprintf(cf, "[CPM_disk_img_plugin]\n");

    // A log that failed to open is off for this session only; saving must
    // keep the user's choice, or one bad path would disable logging for good.
    fprintf(cf, "allow_txt_log=%x\n", allow_txt_log || log_open_failed);
    fprintf(cf, "enable_format_probing=%x\n", enable_format_probing);
    fprintf(cf, "show_disk_info_file=%x\n", show_disk_info_file);
    // log_file_path.clear();
    // log_file_path.push_back("D:\\Temp\\cpmimg.txt");
    fprintf(cf, "log_file_path=%s\n", log_file_setting.c_str());
    fprintf(cf, "debug_level=%x\n\n", debug_level);
    fprintf(cf, "image_format=%s\n", image_format.is_empty() ? "osbexec1" : image_format.data());
    // fprintf(cf, "diskdefs_file_path=%s\n", (diskdefs_file_path+"\\diskdefs").data());
    fprintf(cf, "diskdefs_file_path=%s\n", diskdefs_file_setting.c_str());

    std::fclose(cf);
    return true;
}

std::vector<cpm_disk_descr_t> parse_diskdefs_c(const char* filename) {

    // Only for private use
    std::vector<cpm_disk_descr_t> res;

    FILE* file = fopen(filename, "r");
    if (!file) 
		return res; // TODO: throw exception 
    constexpr int line_size = 1024; // Limit for the string 
    char line[line_size];

    cpm_disk_descr_t superblock = {};

    const auto parse_offset = [](
        const char* text,
        const cpm_disk_descr_t& format,
        off_t& result) -> bool {
        if (!text || !*text)
            return false;

        errno = 0;
        char* end = nullptr;
        const long long value = std::strtoll(text, &end, 10);
        if (errno == ERANGE || end == text || value < 0)
            return false;

        unsigned long long multiplier = 1;
        if (*end != '\0') {
            if (end[1] != '\0')
                return false;

            switch (std::toupper(
                static_cast<unsigned char>(*end))) {
            case 'K':
                multiplier = 1024;
                break;
            case 'M':
                multiplier = 1024ull * 1024ull;
                break;
            case 'T':
                if (format.sectrk <= 0 ||
                    format.secLength <= 0) {
                    return false;
                }
                multiplier =
                    static_cast<unsigned long long>(
                        format.sectrk) *
                    static_cast<unsigned long long>(
                        format.secLength);
                break;
            case 'S':
                if (format.secLength <= 0)
                    return false;
                multiplier =
                    static_cast<unsigned long long>(
                        format.secLength);
                break;
            default:
                return false;
            }
        }

        const auto unsigned_value =
            static_cast<unsigned long long>(value);
        if (multiplier == 0 ||
            unsigned_value >
                (std::numeric_limits<
                    unsigned long long>::max)() /
                    multiplier) {
            return false;
        }

        const auto byte_offset =
            unsigned_value * multiplier;
        if (byte_offset >
            static_cast<unsigned long long>(
                (std::numeric_limits<off_t>::max)())) {
            return false;
        }

        result = static_cast<off_t>(byte_offset);
        return true;
    };

    while (fgets(line, line_size, file)) {
        // Remove newline and trim comments
        char* comment = strchr(line, '#');
        if (comment) *comment = '\0';

        // Skip leading whitespace
        char* token = line;
        while (*token && isspace((unsigned char)*token)) token++;

        if (*token == '\0') continue;

        char key[65] = { 0 }, value[65] = { 0 };
        if (sscanf(token, "%63s %63s", key, value) < 1) continue;

        if (strcmp(key, "diskdef") == 0) {
			strcpy(superblock.fmt_name, value);
        }
        
        if (strcmp(key, "end") == 0) 
        {
			res.push_back(superblock);
            memset(&superblock, 0, sizeof(cpm_disk_descr_t));
            continue;
        }

             if (strcmp(key, "seclen") == 0)    superblock.secLength = atoi(value);
        else if (strcmp(key, "sectrk") == 0)    superblock.sectrk = atoi(value);
        else if (strcmp(key, "tracks") == 0)    superblock.tracks = atoi(value); 
        else if (strcmp(key, "skew") == 0)      superblock.skew = atoi(value);
        else if (strcmp(key, "blocksize") == 0) superblock.blksiz = atoi(value);
        else if (strcmp(key, "maxdir") == 0)    superblock.maxdir = atoi(value);
        else if (strcmp(key, "dirblks") == 0)  superblock.dirblks = atoi(value);
        else if (strcmp(key, "boottrk") == 0)   superblock.boottrk = atoi(value);
        else if (strcmp(key, "bootsec") == 0)   superblock.bootsec = atoi(value);
        else if (strcmp(key, "offset") == 0) {
            off_t parsed_offset = 0;
            if (parse_offset(
                    value,
                    superblock,
                    parsed_offset)) {
                superblock.offset = parsed_offset;
            }
        }
        else if (strcmp(key, "os") == 0) {
                  if (strcmp(value, "2.2") == 0)    superblock.type |= CPMFS_DR22;
             else if (strcmp(value, "3") == 0)      superblock.type |= CPMFS_DR3;
             else if (strcmp(value, "isx") == 0)    superblock.type |= CPMFS_ISX;
             else if (strcmp(value, "p2dos") == 0)  superblock.type |= CPMFS_P2DOS;
             else if (strcmp(value, "zsys") == 0)   superblock.type |= CPMFS_ZSYS;
             else
             {
				 // unknown OS type, skip it
             }
         }
             auto tt = errno;
             // Add reading skewtab, malloc(sizeof(int)*sectors);
    }
        
    fclose(file);
    return res;
}