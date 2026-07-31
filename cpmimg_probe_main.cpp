/*
 * Isolated single-format CP/M mount probe.
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "cpmtools/config.h"
#include "cpmtools/cpmdir.h"
#include "cpmtools/cpmfs.h"
#include "cpmtools/device.h"

#include <fcntl.h>

#include <cstdlib>
#include <fstream>
#include <set>
#include <string>

const char cmd[] = "cpmimg_probe";

namespace {

struct options {
    std::string image;
    std::string diskdefs;
    std::string format;
    std::string driver;
    std::string output;
    int uppercase = 0;
};

bool parse_options(int argc, char** argv, options& result)
{
    for (int index = 1; index < argc; ++index) {
        const std::string key = argv[index];
        if (index + 1 >= argc)
            return false;
        const std::string value = argv[++index];

        if (key == "--image")
            result.image = value;
        else if (key == "--diskdefs")
            result.diskdefs = value;
        else if (key == "--format")
            result.format = value;
        else if (key == "--driver")
            result.driver = value;
        else if (key == "--output")
            result.output = value;
        else if (key == "--uppercase")
            result.uppercase = std::atoi(value.c_str());
        else
            return false;
    }

    return !result.image.empty() &&
        !result.diskdefs.empty() &&
        !result.format.empty() &&
        !result.output.empty();
}

std::string file_key(const PhysDirectoryEntry& entry)
{
    std::string key;
    key.reserve(14);

    const unsigned int user = static_cast<unsigned char>(entry.status);
    key.push_back(static_cast<char>('0' + ((user / 10) % 10)));
    key.push_back(static_cast<char>('0' + (user % 10)));

    for (char ch : entry.name)
        key.push_back(static_cast<char>(static_cast<unsigned char>(ch) & 0x7f));
    key.push_back('.');
    for (char ch : entry.ext)
        key.push_back(static_cast<char>(static_cast<unsigned char>(ch) & 0x7f));

    return key;
}

bool regular_entry(const cpmSuperBlock& super, unsigned int status) noexcept
{
    const bool supports_high_user_areas =
        (super.type & CPMFS_HI_USER) != 0 &&
        (super.type & CPMFS_CPM3_OTHER) == 0;
    const unsigned int user_limit =
        supports_high_user_areas ? 31u : 15u;
    return status <= user_limit;
}

void analyse(
    const cpmSuperBlock& super,
    std::size_t& files,
    std::size_t& extents,
    std::size_t& blocks)
{
    std::set<std::string> unique_files;
    std::set<unsigned int> used_blocks;

    for (int index = 0; index < super.maxdir; ++index) {
        const auto& entry = super.dir[index];
        const unsigned int status = static_cast<unsigned char>(entry.status);

        if (status == 0xe5 || !regular_entry(super, status))
            continue;

        ++extents;
        unique_files.insert(file_key(entry));

        if (super.extents <= 0)
            continue;

        const unsigned int extent =
            static_cast<unsigned char>(entry.extnol) |
            (static_cast<unsigned int>(
                static_cast<unsigned char>(entry.extnoh)) << 5);

        const int first = static_cast<int>(
            (extent % super.extents) * 16 / super.extents);
        const int last = static_cast<int>(
            ((extent % super.extents) + 1) * 16 / super.extents);

        for (int pointer = first; pointer < last;) {
            unsigned int block =
                static_cast<unsigned char>(entry.pointers[pointer]);

            if (super.size > 256) {
                if (pointer + 1 >= last)
                    break;
                block |= static_cast<unsigned int>(
                    static_cast<unsigned char>(entry.pointers[pointer + 1])) << 8;
                pointer += 2;
            }
            else {
                ++pointer;
            }

            if (block != 0)
                used_blocks.insert(block);
        }
    }

    files = unique_files.size();
    blocks = used_blocks.size();
}

bool write_result(
    const std::string& output,
    bool mounted,
    std::size_t files,
    std::size_t extents,
    std::size_t blocks)
{
    std::ofstream stream(output, std::ios::out | std::ios::trunc);
    if (!stream)
        return false;

    stream << "CPMIMG_PROBE_V1\t"
           << (mounted ? 1 : 0) << '\t'
           << files << '\t'
           << extents << '\t'
           << blocks << '\n';
    stream.flush();
    return stream.good();
}

} // namespace

int main(int argc, char** argv)
{
    options parsed;
    if (!parse_options(argc, argv, parsed))
        return 2;

    diskdefs_path = parsed.diskdefs.data();

    cpmSuperBlock super{};
    cpmInode root{};

    const char* open_error = Device_open(
        &super.dev,
        parsed.image.c_str(),
        O_RDONLY,
        parsed.driver.empty() ? nullptr : parsed.driver.c_str());

    if (open_error) {
        write_result(parsed.output, false, 0, 0, 0);
        cpmDiscardSuper(&super);
        return 0;
    }

    const int mount_result = cpmReadSuper(
        &super,
        &root,
        parsed.format.c_str(),
        parsed.uppercase);

    if (mount_result != 0) {
        write_result(parsed.output, false, 0, 0, 0);
        cpmDiscardSuper(&super);
        return 0;
    }

    std::size_t files = 0;
    std::size_t extents = 0;
    std::size_t blocks = 0;
    analyse(super, files, extents, blocks);

    const bool written = write_result(
        parsed.output,
        true,
        files,
        extents,
        blocks);

    cpmDiscardSuper(&super);
    return written ? 0 : 3;
}
