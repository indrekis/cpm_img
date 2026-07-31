#ifndef CPMIMG_DISK_INFO_H_INCLUDED
#define CPMIMG_DISK_INFO_H_INCLUDED

#include "cpmtools/cpmdir.h"
#include "cpmtools/cpmfs.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <vector>

inline constexpr const char* CPMIMG_DISK_INFO_FILENAME =
    "__CPM_DISK_INFO__.TXT";

inline std::string cpmimg_info_basename(const std::string& path)
{
    const auto pos = path.find_last_of("\\/");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

inline std::string cpmimg_info_extension(const std::string& path)
{
    const auto base = cpmimg_info_basename(path);
    const auto pos = base.find_last_of('.');
    if (pos == std::string::npos || pos + 1 >= base.size())
        return {};

    std::string ext = base.substr(pos + 1);
    std::transform(
        ext.begin(),
        ext.end(),
        ext.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
    return ext;
}

inline bool cpmimg_is_disk_info_name(const char* path) noexcept
{
    if (!path)
        return false;

    std::string base = cpmimg_info_basename(path);
    std::transform(
        base.begin(),
        base.end(),
        base.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
    return base == CPMIMG_DISK_INFO_FILENAME;
}

inline const char* cpmimg_yes_no(bool value) noexcept
{
    return value ? "yes" : "no";
}

inline std::string cpmimg_info_safe_text(const std::string& value)
{
    std::ostringstream out;
    for (const unsigned char c : value) {
        if (c == '\\')
            out << "\\\\";
        else if (c == '\r')
            out << "\\r";
        else if (c == '\n')
            out << "\\n";
        else if (c == '\t')
            out << "\\t";
        else if (c >= 0x20 && c < 0x7f)
            out << static_cast<char>(c);
        else
            out << "\\x"
                << std::uppercase
                << std::hex
                << std::setw(2)
                << std::setfill('0')
                << static_cast<unsigned int>(c)
                << std::dec;
    }
    return out.str();
}

inline std::string cpmimg_info_field(
    const char* data,
    std::size_t size)
{
    std::string result;
    result.reserve(size);

    for (std::size_t i = 0; i < size; ++i) {
        unsigned char c =
            static_cast<unsigned char>(data[i]) & 0x7f;
        result.push_back(
            static_cast<char>(c == 0 ? ' ' : c));
    }

    while (!result.empty() &&
           (result.back() == ' ' || result.back() == '\0')) {
        result.pop_back();
    }
    return result;
}

inline std::string cpmimg_info_entry_name(
    const PhysDirectoryEntry& entry)
{
    const auto name = cpmimg_info_field(entry.name, 8);
    const auto ext = cpmimg_info_field(entry.ext, 3);
    return ext.empty() ? name : name + "." + ext;
}

inline std::string cpmimg_info_decode_password(
    const PhysDirectoryEntry& entry)
{
    const unsigned char key =
        static_cast<unsigned char>(entry.lrc);
    std::string password;
    password.reserve(8);

    for (int i = 0; i < 8; ++i) {
        const unsigned char stored =
            static_cast<unsigned char>(entry.pointers[7 - i]);
        password.push_back(
            static_cast<char>(stored ^ key));
    }

    while (!password.empty() &&
           (password.back() == ' ' || password.back() == '\0')) {
        password.pop_back();
    }
    return password;
}

inline int cpmimg_info_bcd(unsigned char value) noexcept
{
    return ((value >> 4) & 0x0f) * 10 + (value & 0x0f);
}

inline bool cpmimg_info_leap(int year) noexcept
{
    return year % 4 == 0 &&
        (year % 100 != 0 || year % 400 == 0);
}

inline std::string cpmimg_info_timestamp(const char* data)
{
    unsigned int days =
        static_cast<unsigned char>(data[0]) |
        (static_cast<unsigned int>(
            static_cast<unsigned char>(data[1])) << 8);

    if (days == 0)
        return "not-set";

    int year = 1978;
    --days;

    while (true) {
        const unsigned int length =
            cpmimg_info_leap(year) ? 366u : 365u;
        if (days < length)
            break;
        days -= length;
        ++year;
    }

    static constexpr int month_days[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    int month = 1;
    for (int i = 0; i < 12; ++i) {
        int length = month_days[i];
        if (i == 1 && cpmimg_info_leap(year))
            ++length;
        if (days < static_cast<unsigned int>(length)) {
            month = i + 1;
            break;
        }
        days -= static_cast<unsigned int>(length);
    }

    std::ostringstream out;
    out << std::setfill('0')
        << std::setw(4) << year << "-"
        << std::setw(2) << month << "-"
        << std::setw(2) << (days + 1) << " "
        << std::setw(2)
        << cpmimg_info_bcd(
            static_cast<unsigned char>(data[2]))
        << ":"
        << std::setw(2)
        << cpmimg_info_bcd(
            static_cast<unsigned char>(data[3]));
    return out.str();
}

inline std::string cpmimg_info_password_mode(unsigned char mode)
{
    std::vector<std::string> values;
    if (mode & 0x80)
        values.emplace_back("read");
    if (mode & 0x40)
        values.emplace_back("write");
    if (mode & 0x20)
        values.emplace_back("delete");

    if (values.empty())
        return "none";

    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i)
            out << ",";
        out << values[i];
    }
    return out.str();
}

inline std::string cpmimg_info_label_time_mode(unsigned char mode)
{
    std::vector<std::string> values;
    if (mode & 0x40)
        values.emplace_back("access");
    if (mode & 0x20)
        values.emplace_back("modification");
    if (mode & 0x10)
        values.emplace_back("creation");

    if (values.empty())
        return "none";

    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i)
            out << ",";
        out << values[i];
    }
    return out.str();
}

inline const char* cpmimg_info_variant(int type) noexcept
{
    if (type & CPMFS_CPM3_OTHER)
        return "CP/M 3";
    if (type & CPMFS_EXACT_SIZE)
        return "ISX";
    if (type & CPMFS_CPM3_DATES)
        return "P2DOS or compatible";
    return "CP/M 2.2 or compatible";
}

inline std::string cpmimg_info_users(const std::set<int>& users)
{
    if (users.empty())
        return "none";

    std::ostringstream out;
    bool first = true;
    for (const int user : users) {
        if (!first)
            out << ",";
        first = false;
        out << std::setfill('0') << std::setw(2) << user;
    }
    return out.str();
}

inline std::string cpmimg_build_disk_info(
    const char* archive_name,
    std::uint64_t physical_file_size,
    const char* requested_driver_name,
    const char* actual_driver_name,
    const char* actual_driver_description,
    const char* actual_compression_name,
    const char* actual_compression_description,
    const char* diskdef_name,
    const char* diskdefs_path,
    const char* selection_source,
    int probe_score,
    std::size_t probe_candidates_tested,
    bool write_supported,
    const cpmSuperBlock& super,
    const cpmInode& root)
{
    struct cpmStatFS statfs {};
    cpmStatFS(&root, &statfs);

    const std::uint64_t logical_disk_size =
        static_cast<std::uint64_t>(super.secLength) *
        static_cast<std::uint64_t>(super.sectrk) *
        static_cast<std::uint64_t>(super.tracks);
    const std::uint64_t expected_raw_image_size =
        logical_disk_size +
        static_cast<std::uint64_t>(
            super.offset > 0 ? super.offset : 0);

    const bool supports_high_user_areas =
        (super.type & CPMFS_HI_USER) != 0 &&
        (super.type & CPMFS_CPM3_OTHER) == 0;
    const unsigned int regular_user_limit =
        supports_high_user_areas ? 31u : 15u;

    std::size_t directory_entries_used = 0;
    std::size_t physical_extents = 0;
    std::set<std::string> unique_files;
    std::set<int> user_areas;
    const PhysDirectoryEntry* label_entry = nullptr;
    std::vector<const PhysDirectoryEntry*> password_entries;

    for (int i = 0; i < super.maxdir; ++i) {
        const auto& entry = super.dir[i];
        const unsigned int status =
            static_cast<unsigned char>(entry.status);

        if (status == 0xe5)
            continue;

        ++directory_entries_used;

        if (status <= regular_user_limit) {
            ++physical_extents;
            user_areas.insert(static_cast<int>(status));

            std::ostringstream key;
            key << std::setfill('0')
                << std::setw(2)
                << status
                << ":"
                << cpmimg_info_entry_name(entry);
            unique_files.insert(key.str());
        }
        else if ((super.type & CPMFS_CPM3_OTHER) &&
                 status >= 16 && status <= 31) {
            password_entries.push_back(&entry);
        }
        else if ((super.type & CPMFS_CPM3_OTHER) &&
                 status == 0x20) {
            label_entry = &entry;
        }
    }

    const std::string source_name =
        cpmimg_info_basename(archive_name ? archive_name : "");
    const std::string container =
        cpmimg_info_extension(source_name);
    const std::string diskdefs_file =
        diskdefs_path ? diskdefs_path : "";

    const std::uint64_t total_bytes =
        static_cast<std::uint64_t>(statfs.f_blocks) *
        static_cast<std::uint64_t>(statfs.f_bsize);
    const std::uint64_t used_bytes =
        static_cast<std::uint64_t>(statfs.f_bused) *
        static_cast<std::uint64_t>(statfs.f_bsize);
    const std::uint64_t free_bytes =
        static_cast<std::uint64_t>(statfs.f_bfree) *
        static_cast<std::uint64_t>(statfs.f_bsize);

    std::ostringstream out;

    out << "[metadata]\r\n"
        << "schema = cpmimg.disk-info/1\r\n"
        << "virtual_file = yes\r\n"
        << "plugin = CPMimg\r\n"
        << "this_text_encoding = UTF-8\r\n\r\n";

    out << "[image]\r\n"
        << "source_name = " << cpmimg_info_safe_text(source_name) << "\r\n"
        << "container = "
        << (container.empty() ? "raw-or-unknown" : container) << "\r\n"
        << "libdsk_driver_selection = "
        << ((requested_driver_name && *requested_driver_name)
            ? "explicit"
            : "auto") << "\r\n"
        << "libdsk_driver_requested = "
        << ((requested_driver_name && *requested_driver_name)
            ? cpmimg_info_safe_text(requested_driver_name)
            : "auto") << "\r\n"
        << "libdsk_driver_actual = "
        << ((actual_driver_name && *actual_driver_name)
            ? cpmimg_info_safe_text(actual_driver_name)
            : "unknown") << "\r\n"
        << "libdsk_driver_description = "
        << ((actual_driver_description &&
             *actual_driver_description)
            ? cpmimg_info_safe_text(
                actual_driver_description)
            : "unknown") << "\r\n"
        << "libdsk_compression = "
        << ((actual_compression_name &&
             *actual_compression_name)
            ? cpmimg_info_safe_text(
                actual_compression_name)
            : "none") << "\r\n"
        << "libdsk_compression_description = "
        << ((actual_compression_description &&
             *actual_compression_description)
            ? cpmimg_info_safe_text(
                actual_compression_description)
            : "Not compressed") << "\r\n"
        << "physical_file_size = " << physical_file_size << "\r\n"
        << "logical_disk_size = " << logical_disk_size << "\r\n"
        << "expected_raw_image_size = "
        << expected_raw_image_size << "\r\n"
        << "write_support = " << cpmimg_yes_no(write_supported)
        << "\r\n\r\n";

    out << "[format]\r\n"
        << "diskdef = "
        << cpmimg_info_safe_text(diskdef_name ? diskdef_name : "")
        << "\r\n"
        << "diskdefs_file = "
        << diskdefs_file << "\r\n"
        << "selection_source = "
        << cpmimg_info_safe_text(
            selection_source ? selection_source : "unknown")
        << "\r\n"
        << "probe_score = ";

    if (probe_score >= 0)
        out << probe_score;
    else
        out << "not-applicable";

    out << "\r\n"
        << "probe_candidates_tested = ";

    if (probe_candidates_tested)
        out << probe_candidates_tested;
    else
        out << "not-recorded";

    out << "\r\n\r\n";

    out << "[geometry]\r\n"
        << "sector_length = " << super.secLength << "\r\n"
        << "tracks = " << super.tracks << "\r\n"
        << "sectors_per_track = " << super.sectrk << "\r\n"
        << "block_size = " << super.blksiz << "\r\n"
        << "maximum_directory_entries = " << super.maxdir << "\r\n"
        << "directory_blocks = " << super.dirblks << "\r\n"
        << "boot_tracks = " << super.boottrk << "\r\n"
        << "boot_sectors = " << super.bootsec << "\r\n"
        << "byte_offset = " << super.offset << "\r\n"
        << "skew = " << super.skew << "\r\n"
        << "logical_extents_per_physical_extent = "
        << super.extents << "\r\n"
        << "allocation_blocks = " << super.size << "\r\n"
        << "libdsk_geometry = "
        << cpmimg_info_safe_text(super.libdskGeometry) << "\r\n"
        << "skew_table = ";

    if (super.skewtab && super.sectrk > 0) {
        for (int i = 0; i < super.sectrk; ++i) {
            if (i)
                out << ",";
            out << super.skewtab[i];
        }
    }
    else {
        out << "not-available";
    }

    out << "\r\n\r\n";

    out << "[filesystem]\r\n"
        << "family = CP/M\r\n"
        << "variant = " << cpmimg_info_variant(super.type) << "\r\n"
        << "uppercase_names = "
        << cpmimg_yes_no(super.uppercase != 0) << "\r\n"
        << "exact_file_sizes = "
        << cpmimg_yes_no((super.type & CPMFS_EXACT_SIZE) != 0) << "\r\n"
        << "cpm3_timestamps = "
        << cpmimg_yes_no((super.type & CPMFS_CPM3_DATES) != 0) << "\r\n"
        << "datestamper_timestamps = "
        << cpmimg_yes_no((super.type & CPMFS_DS_DATES) != 0) << "\r\n"
        << "high_user_numbers = "
        << cpmimg_yes_no((super.type & CPMFS_HI_USER) != 0) << "\r\n"
        << "disk_labels = "
        << cpmimg_yes_no((super.type & CPMFS_CPM3_OTHER) != 0) << "\r\n"
        << "password_entries = "
        << cpmimg_yes_no((super.type & CPMFS_CPM3_OTHER) != 0)
        << "\r\n\r\n";

    out << "[allocation]\r\n"
        << "block_size = " << statfs.f_bsize << "\r\n"
        << "total_blocks = " << statfs.f_blocks << "\r\n"
        << "used_blocks = " << statfs.f_bused << "\r\n"
        << "free_blocks = " << statfs.f_bfree << "\r\n"
        << "total_bytes = " << total_bytes << "\r\n"
        << "used_bytes = " << used_bytes << "\r\n"
        << "free_bytes = " << free_bytes << "\r\n\r\n";

    out << "[directory]\r\n"
        << "maximum_entries = " << statfs.f_files << "\r\n"
        << "used_entries = " << directory_entries_used << "\r\n"
        << "free_entries = " << statfs.f_ffree << "\r\n"
        << "files = " << unique_files.size() << "\r\n"
        << "physical_extents = " << physical_extents << "\r\n"
        << "user_areas_present = "
        << cpmimg_info_users(user_areas) << "\r\n\r\n";

    out << "[label]\r\n"
        << "supported = "
        << cpmimg_yes_no((super.type & CPMFS_CPM3_OTHER) != 0)
        << "\r\n";

    if (!label_entry) {
        out << "present = no\r\n";
    }
    else {
        const unsigned char mode =
            static_cast<unsigned char>(label_entry->extnol);

        out << "present = "
            << cpmimg_yes_no((mode & 0x01) != 0) << "\r\n"
            << "name = "
            << cpmimg_info_safe_text(
                cpmimg_info_entry_name(*label_entry)) << "\r\n"
            << "password_protection = "
            << cpmimg_yes_no((mode & 0x80) != 0) << "\r\n"
            << "password = "
            << cpmimg_info_safe_text(
                cpmimg_info_decode_password(*label_entry)) << "\r\n"
            << "timestamp_mode = "
            << cpmimg_info_label_time_mode(mode) << "\r\n"
            << "created = "
            << cpmimg_info_timestamp(label_entry->pointers + 8) << "\r\n"
            << "modified = "
            << cpmimg_info_timestamp(label_entry->pointers + 12) << "\r\n";
    }

    out << "\r\n[file_passwords]\r\n"
        << "supported = "
        << cpmimg_yes_no((super.type & CPMFS_CPM3_OTHER) != 0)
        << "\r\n"
        << "entries = " << password_entries.size() << "\r\n";

    for (std::size_t i = 0; i < password_entries.size(); ++i) {
        const auto& entry = *password_entries[i];
        const unsigned int user =
            static_cast<unsigned char>(entry.status) - 16u;
        const unsigned char mode =
            static_cast<unsigned char>(entry.extnol);

        out << "entry_" << (i + 1) << " = "
            << std::setfill('0') << std::setw(2) << user
            << ":"
            << cpmimg_info_safe_text(
                cpmimg_info_entry_name(entry))
            << " | mode=" << cpmimg_info_password_mode(mode)
            << " | password="
            << cpmimg_info_safe_text(
                cpmimg_info_decode_password(entry))
            << "\r\n";
    }

    out << "\r\n[validation]\r\n"
        << "mount_result = success\r\n"
        << "directory_validation = passed\r\n"
        << "directory_entries_checked = "
        << super.maxdir << "\r\n"
        << "allocation_map_built = yes\r\n"
        << "validation_level = mount-and-directory-validation\r\n"
        << "full_fsck_performed = no\r\n"
        << "warnings_collected = no\r\n\r\n"
        << "[capabilities]\r\n"
        << "pseudo_file_read_only = yes\r\n"
        << "label_editing_support = no\r\n"
        << "password_editing_support = no\r\n";

    return out.str();
}

#endif
