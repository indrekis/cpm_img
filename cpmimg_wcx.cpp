// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
/*
* CP/M floppy disk images plugin for the Total Commander.
* Copyright (c) 2022-2026, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
*
*/

// https://www.fltk.org/doc-1.3/classFl__Choice.html#a7fd57948259f7f6c17b26d7151b0afef

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>


#include "cpmtools/config.h"
#include "cpmtools/cpmfs.h"
#include "cpmtools/getopt_.h"

#include "sysio_winapi.h"
#include "minimal_fixed_string.h"
#include "plugin_config.h"
#include "cpmimg_plugin_gui.h"
#include "cpmimg_probe_client.h"
#include "cpmimg_disk_info.h"

#include "wcxhead.h"
#include <new>
#include <memory>
#include <exception>
#include <stdexcept>
#include <cstddef>
#include <vector>
#include <algorithm>
#include <optional>
#include <map>
#include <mutex>
#include <cassert>
#include <utility>


using std::nothrow, std::uint8_t;

#ifdef _WIN32
#define WCX_PLUGIN_EXPORTS
#endif 

#ifdef WCX_PLUGIN_EXPORTS
#define DLLEXPORT __declspec(dllexport)
#define STDCALL __stdcall
//! Not enough for the Win32 -- exports would be decorated by: _name@XX.
//! This can help but reverting to the def-file is simpler:
//! #pragma comment(linker, "/EXPORT:" __FUNCTION__ "=" __FUNCDNAME__) 
#else
#define WCX_API
#define STDCALL
#endif 

// For cpmfs.c
char const cmd[] = "cpmimg_wcx";

plugin_config_t plugin_config;

namespace {
// Total Commander may close an archive handle after listing and open
// the same image again for F3/F5. Therefore the selected format must
// be cached by image path even when neither persistence checkbox is
// selected.
std::mutex session_image_format_mutex;
std::map<std::string, minimal_fixed_string_t<33>>
    session_image_formats_by_archive;
minimal_fixed_string_t<33> session_default_image_format;
std::optional<bool> session_format_probing_override;

std::string normalized_archive_key(const char* archive_name)
{
    std::string key = archive_name ? archive_name : "";
    std::transform(
        key.begin(),
        key.end(),
        key.begin(),
        [](unsigned char ch) -> char {
            if (ch == '/')
                return '\\';
            return static_cast<char>(std::tolower(ch));
        });
    return key;
}

minimal_fixed_string_t<33> effective_image_format_for_archive(
    const char* archive_name)
{
    std::lock_guard<std::mutex> lock(session_image_format_mutex);

    const auto key = normalized_archive_key(archive_name);
    const auto found = session_image_formats_by_archive.find(key);
    if (found != session_image_formats_by_archive.end())
        return found->second;

    if (!session_default_image_format.is_empty())
        return session_default_image_format;

    return plugin_config.image_format;
}

void remember_image_format_for_archive(
    const char* archive_name,
    const minimal_fixed_string_t<33>& image_format)
{
    std::lock_guard<std::mutex> lock(session_image_format_mutex);
    session_image_formats_by_archive[
        normalized_archive_key(archive_name)] = image_format;
}

void remember_image_format_for_session(
    const minimal_fixed_string_t<33>& image_format)
{
    std::lock_guard<std::mutex> lock(session_image_format_mutex);
    session_default_image_format = image_format;
}


bool effective_format_probing_enabled()
{
    std::lock_guard<std::mutex> lock(
        session_image_format_mutex);
    return session_format_probing_override.
        value_or(
            plugin_config.enable_format_probing);
}

void set_format_probing_for_session(
    bool enabled)
{
    std::lock_guard<std::mutex> lock(
        session_image_format_mutex);
    session_format_probing_override = enabled;
}

} // namespace


// extern HINSTANCE g_GUI_dlg_hInstance;

// The DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
) {
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	{
		plugin_config.plugin_path = get_plugin_path(hModule);
		plugin_config.set_default_diskdefs_path();
		auto rdconf = plugin_config.read_conf(nullptr, true);
		g_GUI_dlg_hInstance = hModule;
	}
		break; 
	case DLL_PROCESS_DETACH:
		break;
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	}

	return TRUE;
}

//! TODO: How to support F1-F4 attributes
static int cpm_attr_to_tcmd_attr(cpm_attr_t attr) {
	int res = 0;
	if (attr & CPM_ATTR_RO)
		res |= 0x01;
	if (attr & CPM_ATTR_SYS)
		res |= 0x04;
	if (attr & CPM_ATTR_ARCV)
		res |= 0x20;
	return res;
}

bool set_file_attributes_cpm(const char* filename, uint32_t attribute) {
	return set_file_attributes(filename, cpm_attr_to_tcmd_attr(attribute)); // Codes are equal
}

class disk_err_t : public std::runtime_error {
	int err_code = 0;
public: 
	disk_err_t(const char* descr, int err_code_in):
		std::runtime_error{ descr }, err_code{ err_code_in }{
	}
	int get_err_code() const { return err_code; }
};


class device_open_guard_t {
    Device* device_ = nullptr;

public:
    explicit device_open_guard_t(Device& device) noexcept
        : device_(&device)
    {
    }

    device_open_guard_t(const device_open_guard_t&) = delete;
    device_open_guard_t& operator=(const device_open_guard_t&) = delete;

    ~device_open_guard_t() noexcept
    {
        if (device_ && device_->opened) {
            (void)Device_close(device_);
        }
    }

    void release() noexcept
    {
        device_ = nullptr;
    }
};

struct whole_disk_t {
	minimal_fixed_string_t<MAX_PATH> archname; // Should be saved for the TCmd API
	file_handle_t hArchFile = file_handle_t(); //opened file handles
	int openmode_m = PK_OM_LIST;
	bool read_only = true; // Opening to read only or read-write
	bool can_we_write_this_format = true; // If we can write this format
	bool mounted = false;

	minimal_fixed_string_t<MAX_PATH> get_arch_ext() const{
		auto ext_pos = archname.find_last('.');
		if (ext_pos == std::string::npos) {
			return minimal_fixed_string_t<MAX_PATH>{};
		}

		auto extension =
			minimal_fixed_string_t<MAX_PATH>{
				archname.data() + ext_pos + 1};
		for (size_t i = 0; i < extension.size(); ++i) {
			extension[i] = static_cast<char>(
				std::tolower(
					static_cast<unsigned char>(extension[i])));
		}
		return extension;
	}

	size_t image_file_size = 0;	
	size_t image_payload_size = 0;

	tChangeVolProc   pLocChangeVol = nullptr;
	tProcessDataProc pLocProcessData = nullptr;

	cpmSuperBlock super{};
	cpmInode root{};
	minimal_fixed_string_t<33> image_format;
	// std::string format{FORMAT}; //  osb1sssd, osbexec1
	// struct cpmInode root;
	std::string driver_name{}; // devopts; example: driver_name=="imd", "tele" etc.
	std::string actual_libdsk_driver_name{"unknown"};
	std::string actual_libdsk_driver_description{"unknown"};
	std::string actual_libdsk_compression_name{"none"};
	std::string actual_libdsk_compression_description{"Not compressed"};
	bool use_uppercase = true;

	//! TODO: rename 
	char starlit[2] = "*";
	char* const star[1] = { starlit };
	char** gargv = nullptr;
	int gargc = 0;

	uint32_t curren_file_counter = 0;

	std::string disk_info_text;
	std::string format_selection_source{"configured-or-cached"};
	int selected_probe_score = -1;
	std::size_t probe_candidates_tested = 0;
	bool last_header_was_disk_info = false;

	bool disk_info_available() const noexcept {
	    return plugin_config.show_disk_info_file &&
	        !disk_info_text.empty();
	}

	void record_probe_selection(
	    const cpm_safe_probe_report& report) {
	    format_selection_source = "automatic-probe";
	    probe_candidates_tested = report.candidates.size();
	    selected_probe_score = -1;
	    for (const auto& candidate : report.candidates) {
	        if (candidate.format_name == report.selected_format) {
	            selected_probe_score = candidate.score;
	            break;
	        }
	    }
	}


	whole_disk_t(const char* archname_in, size_t vol_size, int openmode, bool read_only_in):
		openmode_m(openmode), image_file_size(vol_size), read_only{ read_only_in }
	{
		archname.push_back(archname_in);
		image_format = effective_image_format_for_archive(archname.data());
		process_image();
	}

	uint32_t users_counter = 0;

	int close_image() noexcept {
		if (!mounted)
			return 0;

		mounted = false;
		const auto result = cpmUmount(&super);
		super = {};
		root = {};
		return result;
	}

	~whole_disk_t() {
		if (hArchFile)
			close_file(hArchFile);
		if (gargv) {
			cpmglobfree(gargv, gargc);
		}
		(void)close_image();
	}
private: 

	void process_image() {
		bool geometry_reliable = false;
		bool raw_size_is_authoritative = false;
		// td0 is read-only
		if(get_arch_ext() == "td0"){
			can_we_write_this_format = false;
		}
		else
		{
			can_we_write_this_format = true;
		}

		hArchFile = open_file_shared_read(archname.data());
		if (hArchFile == file_open_error_v)
		{
			hArchFile = file_handle_t();
			throw disk_err_t{ "Error opening archive file.", E_EOPEN };
		}
		image_file_size = get_file_size(hArchFile);
		if (image_file_size == static_cast<size_t>(-1))
			image_file_size = 0;
		image_payload_size = image_file_size;
		if (get_arch_ext() == "logdisk" && image_payload_size >= 128) {
			image_payload_size -= 128;
			raw_size_is_authoritative = true;
		}
		close_file(hArchFile);
		hArchFile = file_handle_t();
		//=================================================================
		DSK_PDRIVER driver = nullptr;
		DSK_GEOMETRY geom{};
		dsk_err_t err;
		auto disks_set = parse_diskdefs_c(plugin_config.diskdefs_file_path.data());
		decltype(disks_set) possible_fmts;

		err = dsk_open(&driver, archname.data(), nullptr, nullptr);
		if (err) {
			throw disk_err_t{ "Error opening image archive file.", E_EOPEN };
		}

		if (const char* value = dsk_drvname(driver))
		    actual_libdsk_driver_name = value;
		if (const char* value = dsk_drvdesc(driver))
		    actual_libdsk_driver_description = value;
		if (const char* value = dsk_compname(driver))
		    actual_libdsk_compression_name = value;
		else
		    actual_libdsk_compression_name = "none";
		if (const char* value = dsk_compdesc(driver))
		    actual_libdsk_compression_description = value;

		err = dsk_getgeom(driver, &geom);
		dsk_close(&driver);

		const bool geometry_sane =
			!err &&
			geom.dg_secsize > 0 &&
			geom.dg_sectors > 0 &&
			geom.dg_cylinders > 0 &&
			geom.dg_heads > 0;

		if (err) {
			plugin_config.log_print(
				"\n\nError# dsk_getgeom failed with: %d", err
			);
		}
		else if (geometry_sane) {
			const auto geometry_size =
				static_cast<std::uint64_t>(geom.dg_secsize) *
				static_cast<std::uint64_t>(geom.dg_sectors) *
				static_cast<std::uint64_t>(geom.dg_cylinders) *
				static_cast<std::uint64_t>(geom.dg_heads);

			geometry_reliable =
				!raw_size_is_authoritative ||
				image_payload_size == 0 ||
				geometry_size ==
					static_cast<std::uint64_t>(image_payload_size);

			if (!geometry_reliable) {
				plugin_config.log_print(
					"\n\nInfo# Ignoring preliminary LibDsk geometry: "
					"raw image size mismatch"
				);
			}
		}

		const int enough_score = 2;
		for (const auto& dsk : disks_set) {
			int match_score = 0;

			if (geom.dg_secsize == dsk.secLength)
				++match_score;

			const int geom_total_tracks =
				geom.dg_cylinders * geom.dg_heads;
			if (geom_total_tracks == dsk.tracks)
				++match_score;

			if (geom.dg_sectors == dsk.sectrk)
				++match_score;

			const auto expected_size =
				cpm_disk_expected_raw_size(dsk);
			const bool size_match =
				image_payload_size != 0 &&
				expected_size != 0 &&
				expected_size ==
					static_cast<std::uint64_t>(image_payload_size);

			if ((geometry_reliable && match_score >= enough_score) ||
				size_match) {
				possible_fmts.push_back(dsk);
			}
		}
		//=================================================================
		device_open_guard_t device_guard{super.dev};

		// cpmReadSuper() mutates cpmSuperBlock and Device geometry before it
		// knows whether the selected format is valid. Therefore every format
		// attempt must start from a fully discarded superblock and a newly
		// opened LibDsk device.
		auto mount_with_format = [&](const char* format) -> int {
			cpmDiscardSuper(&super);
			root = {};

			const char* open_error = Device_open(
				&super.dev,
				archname.data(),
				read_only ? O_RDONLY : O_RDWR,
				driver_name.empty() ? nullptr : driver_name.c_str());

			if (open_error) {
				plugin_config.log_print(
					"\n\nError# Failed opening file: %s",
					open_error);
				throw disk_err_t{
					"Error in Device_open.",
					E_EOPEN
				};
			}

			return cpmReadSuper(
				&super,
				&root,
				format,
				use_uppercase);
		};

		int erri = mount_with_format(
			image_format.is_empty() ? nullptr : image_format.data());

        if (erri == -1)
        {
            plugin_config.log_print(
                "\n\nError# Failed reading superblock: %s",
                boo ? boo : "unknown error");

            const auto& probe_formats = disks_set;
            const auto& initial_dialog_formats =
                possible_fmts.empty()
                    ? disks_set
                    : possible_fmts;

            cpm_safe_probe_report probe_report;
            bool have_probe_report = false;

            auto log_probe_report = [&]() {
                plugin_config.log_print(
                    "\nInfo# %s",
                    probe_report.message.c_str());

                for (const auto& candidate :
                    probe_report.candidates) {
                    plugin_config.log_print(
                        "\nInfo# Probe %s: "
                        "%d/100; %s",
                        candidate.format_name.c_str(),
                        candidate.score,
                        candidate.summary.c_str());
                }
            };

            auto run_probe = [&]() -> bool {
                probe_report =
                    cpm_run_safe_format_probes(
                        archname.data(),
                        plugin_config.
                            diskdefs_file_path.data(),
                        driver_name.empty()
                            ? nullptr
                            : driver_name.c_str(),
                        probe_formats,
                        static_cast<std::uint64_t>(
                            image_payload_size),
                        geom,
                        geometry_reliable,
                        use_uppercase);

                have_probe_report = true;
                log_probe_report();

                if (!probe_report.unique_match)
                    return false;

                erri = mount_with_format(
                    probe_report.
                        selected_format.c_str());

                if (erri != -1) {
                    image_format =
                        probe_report.
                            selected_format.c_str();
                    record_probe_selection(probe_report);
                    remember_image_format_for_archive(
                        archname.data(),
                        image_format);
                    return true;
                }

                probe_report.unique_match = false;
                probe_report.message =
                    "Isolated probe passed, but "
                    "the final mount failed. "
                    "Select manually.";
                return false;
            };

            if (effective_format_probing_enabled())
                (void)run_probe();

            while (erri == -1) {
                const auto dialog_formats =
                    have_probe_report
                        ? cpm_rank_formats_by_probe(
                            probe_formats,
                            probe_report)
                        : initial_dialog_formats;

                auto img_type_sel_GUI =
                    std::make_unique<
                        img_type_sel_GUI_t>(
                            dialog_formats,
                            geom,
                            true,
                            geometry_reliable,
                            image_payload_size,
                            have_probe_report
                                ? &probe_report
                                : nullptr,
                            effective_format_probing_enabled());

                const auto dialog_action =
                    img_type_sel_GUI->action();

                if (dialog_action ==
                    format_dialog_action_t::cancel) {
                    break;
                }

                const bool enable_future_probing =
                    img_type_sel_GUI->
                        automatic_probing_enabled();

                set_format_probing_for_session(
                    enable_future_probing);

                if (img_type_sel_GUI->
                    save_probing_preference()) {
                    plugin_config.
                        enable_format_probing =
                            enable_future_probing;
                    plugin_config.write_conf();
                }

                if (dialog_action ==
                    format_dialog_action_t::probe_now) {
                    (void)run_probe();
                    continue;
                }

                const auto selected_image_format =
                    img_type_sel_GUI->
                        get_image_type();

                erri = mount_with_format(
                    selected_image_format.data());

                if (erri == -1) {
                    plugin_config.log_print(
                        "\n\nError# Failed reading "
                        "superblock with format "
                        "%s: %s",
                        selected_image_format.data(),
                        boo
                            ? boo
                            : "unknown error");
                    continue;
                }

                image_format =
                    selected_image_format;
                format_selection_source = "manual";
                selected_probe_score = -1;
                remember_image_format_for_archive(
                    archname.data(),
                    selected_image_format);

                if (
                    img_type_sel_GUI->
                        save_disk_type_for_cur() ||
                    img_type_sel_GUI->
                        save_disk_type()) {
                    remember_image_format_for_session(
                        selected_image_format);
                }

                if (img_type_sel_GUI->
                    save_disk_type()) {
                    plugin_config.image_format =
                        selected_image_format;
                    plugin_config.write_conf();
                }

                break;
            }

            if (erri == -1) {
                cpmDiscardSuper(&super);
                root = {};
                throw disk_err_t{
                    "Error in cpmReadSuper.",
                    E_EOPEN
                };
            }
        }

		device_guard.release();
		mounted = true;
	}
};
//------- whole_disk_t implementation --------------------------
using archive_HANDLE = whole_disk_t*;

namespace {

void forget_image_format_for_archive(const char* archive_name)
{
	std::lock_guard<std::mutex> lock(session_image_format_mutex);
	session_image_formats_by_archive.erase(
		normalized_archive_key(archive_name));
}

class transaction_image_guard_t {
	std::string path_;
	bool delete_on_destroy_ = true;

public:
	explicit transaction_image_guard_t(std::string path)
		: path_(std::move(path))
	{
	}

	transaction_image_guard_t(const transaction_image_guard_t&) = delete;
	transaction_image_guard_t& operator=(
		const transaction_image_guard_t&) = delete;

	~transaction_image_guard_t()
	{
		forget_image_format_for_archive(path_.c_str());
		if (delete_on_destroy_ && !path_.empty())
			(void)DeleteFileA(path_.c_str());
	}

	void release() noexcept
	{
		forget_image_format_for_archive(path_.c_str());
		delete_on_destroy_ = false;
	}

	void preserve() noexcept
	{
		forget_image_format_for_archive(path_.c_str());
		delete_on_destroy_ = false;
	}
};

class local_file_handle_guard_t {
	file_handle_t handle_ = file_open_error_v;

public:
	explicit local_file_handle_guard_t(file_handle_t handle) noexcept
		: handle_(handle)
	{
	}

	local_file_handle_guard_t(const local_file_handle_guard_t&) = delete;
	local_file_handle_guard_t& operator=(
		const local_file_handle_guard_t&) = delete;

	~local_file_handle_guard_t()
	{
		if (handle_ != file_open_error_v &&
			handle_ != file_handle_t()) {
			(void)close_file(handle_);
		}
	}

	file_handle_t get() const noexcept
	{
		return handle_;
	}
};

struct pending_pack_file_t {
	std::string cpm_name;
	std::vector<char> data;
};

std::string make_transaction_candidate(
	const char* packed_file,
	unsigned int attempt)
{
	const std::string target =
		packed_file ? packed_file : "";

	const auto separator = target.find_last_of("\\/");
	const auto dot = target.find_last_of('.');
	const bool has_extension =
		dot != std::string::npos &&
		(separator == std::string::npos || dot > separator);

	const std::string directory =
		separator == std::string::npos
			? std::string{}
			: target.substr(0, separator + 1);
	const std::string extension =
		has_extension ? target.substr(dot) : std::string{};

	char temporary_name[96]{};
	const auto name_length = snprintf(
		temporary_name,
		sizeof(temporary_name),
		"~cpmimg-%08lX-%08lX-%03u%s",
		static_cast<unsigned long>(GetCurrentProcessId()),
		static_cast<unsigned long>(GetTickCount()),
		attempt,
		extension.c_str());

	if (name_length < 0 ||
		static_cast<size_t>(name_length) >=
			sizeof(temporary_name)) {
		return {};
	}

	std::string candidate = directory + temporary_name;
	if (candidate.size() >= MAX_PATH)
		return {};

	return candidate;
}

int prepare_transaction_image(
	const char* packed_file,
	bool packed_file_exists,
	std::string& transaction_path)
{
	DWORD last_error = ERROR_SUCCESS;

	for (unsigned int attempt = 0; attempt < 1000; ++attempt) {
		auto candidate =
			make_transaction_candidate(packed_file, attempt);
		if (candidate.empty()) {
			plugin_config.log_print(
				"\n\nError# Cannot create a transaction-image "
				"path for %s",
				packed_file);
			return E_ECREATE;
		}

		if (packed_file_exists) {
			if (CopyFileA(
					packed_file,
					candidate.c_str(),
					TRUE)) {
				transaction_path = std::move(candidate);
				return 0;
			}
		}
		else {
			const auto handle = CreateFileA(
				candidate.c_str(),
				GENERIC_READ | GENERIC_WRITE,
				FILE_SHARE_READ,
				nullptr,
				CREATE_NEW,
				FILE_ATTRIBUTE_NORMAL,
				nullptr);

			if (handle != INVALID_HANDLE_VALUE) {
				(void)CloseHandle(handle);
				transaction_path = std::move(candidate);
				return 0;
			}
		}

		last_error = GetLastError();
		if (last_error == ERROR_FILE_EXISTS ||
			last_error == ERROR_ALREADY_EXISTS) {
			continue;
		}

		plugin_config.log_print(
			"\n\nError# Failed preparing transaction image "
			"for %s (Windows error %lu)",
			packed_file,
			static_cast<unsigned long>(last_error));
		return E_ECREATE;
	}

	plugin_config.log_print(
		"\n\nError# Failed finding a free transaction-image "
		"name for %s (last Windows error %lu)",
		packed_file,
		static_cast<unsigned long>(last_error));
	return E_ECREATE;
}

int load_pending_pack_file(
	const std::string& source_path,
	const std::string& cpm_name,
	pending_pack_file_t& pending)
{
	const auto raw_handle =
		open_file_shared_read(source_path.c_str());
	if (raw_handle == file_open_error_v) {
		plugin_config.log_print(
			"\n\nError# Failed opening source file %s",
			source_path.c_str());
		return E_EOPEN;
	}

	local_file_handle_guard_t source_handle{raw_handle};
	const auto file_size = get_file_size(source_handle.get());

	if (file_size == static_cast<size_t>(-1)) {
		plugin_config.log_print(
			"\n\nError# Failed obtaining size of source file %s",
			source_path.c_str());
		return E_EREAD;
	}

	if (file_size > static_cast<size_t>(0xffffffffu) ||
		file_size > static_cast<size_t>(-1) - 127) {
		plugin_config.log_print(
			"\n\nError# Source file %s is too large",
			source_path.c_str());
		return E_BAD_DATA;
	}

	const auto padded_size =
		((file_size + 127) / 128) * 128;

	try {
		pending.cpm_name = cpm_name;
		pending.data.assign(
			padded_size,
			static_cast<char>(0x1a));
	}
	catch (const std::bad_alloc&) {
		return E_NO_MEMORY;
	}

	if (file_size != 0) {
		const auto read_size = read_file(
			source_handle.get(),
			pending.data.data(),
			file_size);

		if (read_size != file_size) {
			plugin_config.log_print(
				"\n\nError# Failed reading source file %s: "
				"expected %zu bytes, read %zu bytes",
				source_path.c_str(),
				file_size,
				read_size);
			return E_EREAD;
		}
	}

	return 0;
}

int create_empty_transaction_image(
	const std::string& transaction_path,
	const minimal_fixed_string_t<33>& image_format)
{
	if (image_format.is_empty()) {
		plugin_config.log_print(
			"\n\nError# Cannot create image %s: "
			"no CP/M format is selected",
			transaction_path.c_str());
		return E_UNKNOWN_FORMAT;
	}

	cpmSuperBlock super{};
	cpmInode root{};
	super.dev.opened = 0;
	constexpr bool use_uppercase = true;

	if (cpmReadSuper(
			&super,
			&root,
			image_format.data(),
			use_uppercase) == -1) {
		plugin_config.log_print(
			"\n\nError# Failed preparing format %s for "
			"new image %s: %s",
			image_format.data(),
			transaction_path.c_str(),
			boo ? boo : "unknown error");
		cpmDiscardSuper(&super);
		return E_BAD_DATA;
	}

	const auto boot_track_size_64 =
		static_cast<std::uint64_t>(super.boottrk) *
		static_cast<std::uint64_t>(super.secLength) *
		static_cast<std::uint64_t>(super.sectrk);

	if (boot_track_size_64 >
		static_cast<std::uint64_t>(
			static_cast<size_t>(-1))) {
		cpmDiscardSuper(&super);
		return E_NO_MEMORY;
	}

	std::vector<char> boot_tracks;
	try {
		boot_tracks.assign(
			static_cast<size_t>(boot_track_size_64),
			static_cast<char>(0xe5));
	}
	catch (const std::bad_alloc&) {
		cpmDiscardSuper(&super);
		return E_NO_MEMORY;
	}

	const auto create_result = mkfs(
		&super,
		transaction_path.c_str(),
		image_format.data(),
		"unlabeled",
		boot_tracks.data(),
		false,
		use_uppercase);

	if (create_result == -1) {
		plugin_config.log_print(
			"\n\nError# Failed creating transaction image %s "
			"with error: %s",
			transaction_path.c_str(),
			boo ? boo : "unknown error");
	}

	cpmDiscardSuper(&super);
	return create_result == -1 ? E_ECREATE : 0;
}

int verify_transaction_image(
	const std::string& transaction_path,
	const minimal_fixed_string_t<33>& image_format,
	const std::vector<pending_pack_file_t>& pending_files)
{
	cpmSuperBlock super{};
	cpmInode root{};

	const char* open_error = Device_open(
		&super.dev,
		transaction_path.c_str(),
		O_RDONLY,
		nullptr);
	if (open_error) {
		plugin_config.log_print(
			"\n\nError# Failed opening transaction image %s "
			"for verification: %s",
			transaction_path.c_str(),
			open_error);
		return E_EOPEN;
	}

	if (cpmReadSuper(
			&super,
			&root,
			image_format.data(),
			true) == -1) {
		plugin_config.log_print(
			"\n\nError# Failed mounting transaction image %s "
			"for verification: %s",
			transaction_path.c_str(),
			boo ? boo : "unknown error");
		cpmDiscardSuper(&super);
		return E_BAD_DATA;
	}

	int result = 0;

	for (const auto& pending : pending_files) {
		cpmInode inode{};
		if (cpmNamei(
				&root,
				pending.cpm_name.c_str(),
				&inode) == -1) {
			plugin_config.log_print(
				"\n\nError# Verification failed: file %s "
				"is absent from transaction image %s",
				pending.cpm_name.c_str(),
				transaction_path.c_str());
			result = E_BAD_DATA;
			break;
		}

		if (static_cast<size_t>(inode.size) !=
			pending.data.size()) {
			plugin_config.log_print(
				"\n\nError# Verification failed for %s in %s: "
				"expected size %zu, image reports %lld",
				pending.cpm_name.c_str(),
				transaction_path.c_str(),
				pending.data.size(),
				static_cast<long long>(inode.size));
			result = E_BAD_DATA;
			break;
		}

		cpmFile file{};
		if (cpmOpen(&inode, &file, O_RDONLY) == -1) {
			plugin_config.log_print(
				"\n\nError# Verification failed opening %s "
				"in transaction image %s: %s",
				pending.cpm_name.c_str(),
				transaction_path.c_str(),
				boo ? boo : "unknown error");
			result = E_BAD_DATA;
			break;
		}

		std::vector<char> read_back;
		try {
			read_back.resize(pending.data.size());
		}
		catch (const std::bad_alloc&) {
			result = E_NO_MEMORY;
			(void)cpmClose(&file);
			break;
		}

		if (!read_back.empty()) {
			const auto read_size = cpmRead(
				&file,
				read_back.data(),
				read_back.size());

			if (read_size != read_back.size() ||
				!std::equal(
					read_back.begin(),
					read_back.end(),
					pending.data.begin())) {
				plugin_config.log_print(
					"\n\nError# Verification failed: data "
					"mismatch for %s in transaction image %s",
					pending.cpm_name.c_str(),
					transaction_path.c_str());
				result = E_BAD_DATA;
			}
		}

		if (cpmClose(&file) != 0 && result == 0) {
			plugin_config.log_print(
				"\n\nError# Verification failed closing %s "
				"in transaction image %s: %s",
				pending.cpm_name.c_str(),
				transaction_path.c_str(),
				boo ? boo : "unknown error");
			result = E_ECLOSE;
		}

		if (result != 0)
			break;
	}

	if (cpmUmount(&super) == -1 && result == 0) {
		plugin_config.log_print(
			"\n\nError# Failed closing transaction image %s "
			"after verification: %s",
			transaction_path.c_str(),
			boo ? boo : "unknown error");
		result = E_ECLOSE;
	}

	return result;
}

bool commit_transaction_image(
	const char* packed_file,
	const std::string& transaction_path,
	bool packed_file_exists)
{
	if (packed_file_exists) {
		return ReplaceFileA(
			packed_file,
			transaction_path.c_str(),
			nullptr,
			0,
			nullptr,
			nullptr) != FALSE;
	}

	return MoveFileExA(
		transaction_path.c_str(),
		packed_file,
		MOVEFILE_WRITE_THROUGH) != FALSE;
}

void log_cpm_name_resolution_failure(
    const whole_disk_t& archive,
    const char* stage,
    const char* requested_name,
    int gargv_index)
{
    const std::string cpm_error =
        boo ? boo : "unspecified cpmtools error";
    const char* safe_name =
        requested_name ? requested_name : "(null)";

    plugin_config.log_print(
        "\n\nError# cpmNamei diagnostics: stage=%s; "
        "archive=%s; requested='%s'; requested_length=%zu; "
        "gargv_index=%d; gargc=%d; current_file_counter=%u; "
        "format=%s; requested_driver=%s; actual_driver=%s; "
        "cpmtools_error=%s",
        stage ? stage : "(unknown)",
        archive.archname.data(),
        safe_name,
        strlen(safe_name),
        gargv_index,
        archive.gargc,
        static_cast<unsigned int>(
            archive.curren_file_counter),
        archive.image_format.data(),
        archive.driver_name.empty()
            ? "(auto)"
            : archive.driver_name.c_str(),
        archive.actual_libdsk_driver_name.empty()
            ? "(unknown)"
            : archive.actual_libdsk_driver_name.c_str(),
        cpm_error.c_str());

    if (archive.gargv && archive.gargc > 0) {
        const int first =
            gargv_index > 2 ? gargv_index - 2 : 0;
        const int last =
            gargv_index + 2 < archive.gargc
                ? gargv_index + 2
                : archive.gargc - 1;

        for (int i = first; i <= last; ++i) {
            const char* value =
                archive.gargv[i]
                    ? archive.gargv[i]
                    : "(null)";
            plugin_config.log_print(
                "\nInfo# cpmNamei gargv context: "
                "index=%d%s; value='%s'; length=%zu",
                i,
                i == gargv_index ? " [selected]" : "",
                value,
                strlen(value));
        }
    }

    const char* requested_base = safe_name;
    if (requested_base[0] >= '0' &&
        requested_base[0] <= '9' &&
        requested_base[1] >= '0' &&
        requested_base[1] <= '9') {
        requested_base += 2;
    }

    size_t requested_base_length = 0;
    while (requested_base[requested_base_length] &&
           requested_base[requested_base_length] != '.' &&
           requested_base_length < 8) {
        ++requested_base_length;
    }

    bool found_candidate = false;
    const bool supports_high_user_areas =
        (archive.super.type & CPMFS_HI_USER) != 0 &&
        (archive.super.type & CPMFS_CPM3_OTHER) == 0;
    const unsigned int regular_user_limit =
        supports_high_user_areas ? 31u : 15u;

    for (int entry_index = 0;
         entry_index < archive.super.maxdir;
         ++entry_index) {
        const auto& entry =
            archive.super.dir[entry_index];
        const unsigned int status =
            static_cast<unsigned char>(entry.status);

        if (status > regular_user_limit)
            continue;

        size_t entry_name_length = 8;
        while (entry_name_length > 0 &&
               ((static_cast<unsigned char>(
                    entry.name[entry_name_length - 1]) &
                 0x7f) == ' ')) {
            --entry_name_length;
        }

        if (entry_name_length != requested_base_length)
            continue;

        bool same_base = true;
        for (size_t i = 0;
             i < requested_base_length;
             ++i) {
            const char catalog_char =
                static_cast<char>(
                    static_cast<unsigned char>(
                        entry.name[i]) &
                    0x7f);
            if (catalog_char != requested_base[i]) {
                same_base = false;
                break;
            }
        }
        if (!same_base)
            continue;

        found_candidate = true;

        char catalog_name[9]{};
        char catalog_ext[4]{};

        for (size_t i = 0; i < 8; ++i) {
            catalog_name[i] =
                static_cast<char>(
                    static_cast<unsigned char>(
                        entry.name[i]) &
                    0x7f);
        }
        for (size_t i = 0; i < 3; ++i) {
            catalog_ext[i] =
                static_cast<char>(
                    static_cast<unsigned char>(
                        entry.ext[i]) &
                    0x7f);
        }

        for (int i = 7;
             i >= 0 && catalog_name[i] == ' ';
             --i) {
            catalog_name[i] = '\0';
        }
        for (int i = 2;
             i >= 0 && catalog_ext[i] == ' ';
             --i) {
            catalog_ext[i] = '\0';
        }

        const unsigned int extent_number =
            (static_cast<unsigned char>(
                 entry.extnol) &
             0x1f) |
            ((static_cast<unsigned int>(
                  static_cast<unsigned char>(
                      entry.extnoh)) &
              0x3f)
             << 5);

        plugin_config.log_print(
            "\nInfo# cpmNamei catalog candidate: "
            "entry=%d; user=%u; name='%s'; ext='%s'; "
            "extent=%u; lrc=%u; records=%u; "
            "pointers="
            "%02X %02X %02X %02X "
            "%02X %02X %02X %02X "
            "%02X %02X %02X %02X "
            "%02X %02X %02X %02X",
            entry_index,
            status,
            catalog_name,
            catalog_ext,
            extent_number,
            static_cast<unsigned int>(
                static_cast<unsigned char>(
                    entry.lrc)),
            static_cast<unsigned int>(
                static_cast<unsigned char>(
                    entry.blkcnt)),
            static_cast<unsigned int>(
                static_cast<unsigned char>(
                    entry.pointers[0])),
            static_cast<unsigned int>(
                static_cast<unsigned char>(
                    entry.pointers[1])),
            static_cast<unsigned int>(
                static_cast<unsigned char>(
                    entry.pointers[2])),
            static_cast<unsigned int>(
                static_cast<unsigned char>(
                    entry.pointers[3])),
            static_cast<unsigned int>(
                static_cast<unsigned char>(
                    entry.pointers[4])),
            static_cast<unsigned int>(
                static_cast<unsigned char>(
                    entry.pointers[5])),
            static_cast<unsigned int>(
                static_cast<unsigned char>(
                    entry.pointers[6])),
            static_cast<unsigned int>(
                static_cast<unsigned char>(
                    entry.pointers[7])),
            static_cast<unsigned int>(
                static_cast<unsigned char>(
                    entry.pointers[8])),
            static_cast<unsigned int>(
                static_cast<unsigned char>(
                    entry.pointers[9])),
            static_cast<unsigned int>(
                static_cast<unsigned char>(
                    entry.pointers[10])),
            static_cast<unsigned int>(
                static_cast<unsigned char>(
                    entry.pointers[11])),
            static_cast<unsigned int>(
                static_cast<unsigned char>(
                    entry.pointers[12])),
            static_cast<unsigned int>(
                static_cast<unsigned char>(
                    entry.pointers[13])),
            static_cast<unsigned int>(
                static_cast<unsigned char>(
                    entry.pointers[14])),
            static_cast<unsigned int>(
                static_cast<unsigned char>(
                    entry.pointers[15])));
    }

    if (!found_candidate) {
        plugin_config.log_print(
            "\nInfo# cpmNamei diagnostics: "
            "no regular directory entry has basename '%.*s'",
            static_cast<int>(requested_base_length),
            requested_base);
    }
}

} // namespace

//------------------------------------------------------------
//-----------------------=[ DLL exports ]=--------------------

extern "C" {
	// OpenArchive should perform all necessary operations when an archive is to be opened
	DLLEXPORT archive_HANDLE STDCALL OpenArchive(tOpenArchiveData* ArchiveData)
	{
		auto rdconf = plugin_config.read_conf(nullptr, true); // Reread confuguration
		plugin_config.log_print("\n\nInfo# Opening file: %s", ArchiveData->ArcName);

		std::unique_ptr<whole_disk_t> arch; // TCmd API expects HANDLE/raw pointer,
										// so smart pointer is used to manage cleanup on errors 
										// only inside this function
		//! Not used by TCmd yet.
		ArchiveData->CmtBuf = 0;
		ArchiveData->CmtBufSize = 0;
		ArchiveData->CmtSize = 0; 
		ArchiveData->CmtState = 0;

		try {
			arch = std::make_unique<whole_disk_t>(ArchiveData->ArcName, 0, ArchiveData->OpenMode, true);
		}
		catch (disk_err_t& err) {
			ArchiveData->OpenResult = err.get_err_code();
			return nullptr;
		}
		catch (std::bad_alloc&) {
			ArchiveData->OpenResult = E_NO_MEMORY;
			return nullptr;
		}

		// TODO: Make cpmglob() return an explicit status, check
		// cpmOpendir()/cpmReaddir() failures inside it, and reject
		// partial directory results instead of relying on global boo.
		cpmglob(0, 1, arch->star, &arch->root, &arch->gargc, &arch->gargv);
		for (int i = 0; i < arch->gargc; ++i) {
			if ( arch->gargv[i][0] != '.' && 
				(arch->gargv[i][0] != '0' || arch->gargv[i][1] != '0') 
			   ) {
				++arch->users_counter;
			}
		}

		if (plugin_config.show_disk_info_file) {
            arch->disk_info_text = cpmimg_build_disk_info(
                arch->archname.data(),
                static_cast<std::uint64_t>(arch->image_file_size),
                arch->driver_name.c_str(),
                arch->actual_libdsk_driver_name.c_str(),
                arch->actual_libdsk_driver_description.c_str(),
                arch->actual_libdsk_compression_name.c_str(),
                arch->actual_libdsk_compression_description.c_str(),
                arch->image_format.data(),
                plugin_config.diskdefs_file_path.data(),
                arch->format_selection_source.c_str(),
                arch->selected_probe_score,
                arch->probe_candidates_tested,
                arch->can_we_write_this_format,
                arch->super,
                arch->root);
        }

        return arch.release(); // Returns raw ptr and releases ownership 
	}

	// TCmd calls ReadHeader to find out what files are in the archive
	DLLEXPORT int STDCALL ReadHeader(
        archive_HANDLE hArcData,
        tHeaderData* HeaderData)
    {
        hArcData->last_header_was_disk_info = false;

        while (hArcData->curren_file_counter <
               static_cast<uint32_t>(hArcData->gargc)) {
            auto root_ino = &hArcData->root;
            cpmInode file_ino{};
            auto dirent_raw_ptr =
                hArcData->gargv[hArcData->curren_file_counter];

            if (strcmp(dirent_raw_ptr, "..") == 0) {
                ++hArcData->curren_file_counter;
                continue;
            }

            const auto name_result =
                cpmNamei(
                    root_ino,
                    dirent_raw_ptr,
                    &file_ino);
            if (name_result == -1) {
                log_cpm_name_resolution_failure(
                    *hArcData,
                    "ReadHeader",
                    dirent_raw_ptr,
                    static_cast<int>(
                        hArcData->curren_file_counter));
                ++hArcData->curren_file_counter;
                continue;
            }
            strcpy(HeaderData->ArcName, hArcData->archname.data());

            if (hArcData->users_counter == 0) {
                strcpy(HeaderData->FileName, dirent_raw_ptr + 2);
            }
            else {
                minimal_fixed_string_t<MAX_PATH> path{
                    dirent_raw_ptr, 2
                };
                path.push_back("\\");
                path.push_back(dirent_raw_ptr + 2);
                strcpy(HeaderData->FileName, path.data());
            }

            HeaderData->FileAttr =
                cpm_attr_to_tcmd_attr(file_ino.attr);
            HeaderData->FileTime = file_ino.mtime;
            HeaderData->PackSize = file_ino.size;
            HeaderData->UnpSize = file_ino.size;
            HeaderData->CmtBuf = nullptr;
            HeaderData->CmtBufSize = 0;
            HeaderData->CmtSize = 0;
            HeaderData->CmtState = 0;
            HeaderData->UnpVer = 0;
            HeaderData->Method = 0;
            HeaderData->FileCRC = 0;

            ++hArcData->curren_file_counter;
            return 0;
        }

        if (hArcData->disk_info_available() &&
            hArcData->curren_file_counter ==
                static_cast<uint32_t>(hArcData->gargc)) {
            strcpy(HeaderData->ArcName, hArcData->archname.data());
            strcpy(HeaderData->FileName, CPMIMG_DISK_INFO_FILENAME);
            HeaderData->FileAttr = 0x01;
            HeaderData->FileTime = 0;
            HeaderData->PackSize =
                static_cast<int>(hArcData->disk_info_text.size());
            HeaderData->UnpSize = HeaderData->PackSize;
            HeaderData->CmtBuf = nullptr;
            HeaderData->CmtBufSize = 0;
            HeaderData->CmtSize = 0;
            HeaderData->CmtState = 0;
            HeaderData->UnpVer = 0;
            HeaderData->Method = 0;
            HeaderData->FileCRC = 0;

            hArcData->last_header_was_disk_info = true;
            ++hArcData->curren_file_counter;
            return 0;
        }

        hArcData->curren_file_counter = 0;
        hArcData->last_header_was_disk_info = false;
        return E_END_ARCHIVE;
    }

	// ProcessFile should unpack the specified file or test the integrity of the archive
	DLLEXPORT int STDCALL ProcessFile(
        archive_HANDLE hArcData,
        int Operation,
        char* DestPath,
        char* DestName)
    {
        char dest[MAX_PATH] = "";
        file_handle_t hUnpFile;

        if (Operation == PK_SKIP)
            return 0;
        if (hArcData->curren_file_counter == 0)
            return E_END_ARCHIVE;

        if (Operation == PK_TEST) {
            if (!get_temp_filename(dest, "FIM"))
                return E_ECREATE;
        }
        else {
            if (DestPath)
                strcpy(dest, DestPath);
            if (DestName)
                strcat(dest, DestName);
        }

        const auto preserve_failed_output =
            [&](const char* archived_name) {
                if (Operation == PK_TEST) {
                    delete_file(dest);
                    return;
                }

                char partial_path[MAX_PATH]{};

                for (unsigned int suffix = 0;
                     suffix < 1000;
                     ++suffix) {
                    const auto path_length =
                        suffix == 0
                            ? snprintf(
                                partial_path,
                                sizeof(partial_path),
                                "%s.partial",
                                dest)
                            : snprintf(
                                partial_path,
                                sizeof(partial_path),
                                "%s.partial.%u",
                                dest,
                                suffix);

                    if (path_length < 0 ||
                        static_cast<size_t>(path_length) >=
                            sizeof(partial_path)) {
                        plugin_config.log_print(
                            "\n\nError# Cannot create a partial-file "
                            "name for %s; incomplete output remains as %s",
                            archived_name,
                            dest);
                        return;
                    }

                    if (MoveFileA(dest, partial_path)) {
                        plugin_config.log_print(
                            "\n\nWarning# Incomplete output for %s "
                            "was preserved as %s",
                            archived_name,
                            partial_path);
                        return;
                    }

                    const auto move_error = GetLastError();
                    if (move_error != ERROR_ALREADY_EXISTS &&
                        move_error != ERROR_FILE_EXISTS) {
                        plugin_config.log_print(
                            "\n\nError# Failed renaming incomplete "
                            "output for %s from %s to %s "
                            "(Windows error %lu); output remains "
                            "under its original name",
                            archived_name,
                            dest,
                            partial_path,
                            static_cast<unsigned long>(move_error));
                        return;
                    }
                }

                plugin_config.log_print(
                    "\n\nError# No free partial-file name found "
                    "for %s; incomplete output remains as %s",
                    archived_name,
                    dest);
            };

        if (hArcData->last_header_was_disk_info) {
            hUnpFile = Operation == PK_TEST
                ? open_file_overwrite(dest)
                : open_file_write(dest);

            if (hUnpFile == file_open_error_v)
                return E_ECREATE;

            const auto write_result = write_file(
                hUnpFile,
                hArcData->disk_info_text.data(),
                hArcData->disk_info_text.size());

            if (write_result !=
                hArcData->disk_info_text.size()) {
                plugin_config.log_print(
                    "\n\nError# Failed writing virtual file %s "
                    "from archive %s in ProcessFile/write_file",
                    CPMIMG_DISK_INFO_FILENAME,
                    hArcData->archname.data());
                close_file(hUnpFile);
                preserve_failed_output(
                    CPMIMG_DISK_INFO_FILENAME);
                return E_EWRITE;
            }

            close_file(hUnpFile);

            if (Operation == PK_TEST)
                delete_file(dest);
            else
                (void)set_file_attributes(dest, 0x01);

            return 0;
        }

        if (hArcData->curren_file_counter >
            static_cast<uint32_t>(hArcData->gargc)) {
            if (Operation == PK_TEST)
                delete_file(dest);
            return E_NO_MEMORY;
        }

        struct cpmFile file{};
        auto root_ino = &hArcData->root;
        cpmInode file_ino{};
        auto dirent_raw_ptr =
            hArcData->gargv[hArcData->curren_file_counter - 1];
        const auto nres =
            cpmNamei(root_ino, dirent_raw_ptr, &file_ino);

        if (nres == -1) {
            log_cpm_name_resolution_failure(
                *hArcData,
                "ProcessFile",
                dirent_raw_ptr,
                static_cast<int>(
                    hArcData->curren_file_counter - 1));
            if (Operation == PK_TEST)
                delete_file(dest);
            return E_BAD_DATA;
        }

        hUnpFile = Operation == PK_TEST
            ? open_file_overwrite(dest)
            : open_file_write(dest);

        if (hUnpFile == file_open_error_v)
            return E_ECREATE;

        if (file_ino.size != 0) {
            const auto file_size =
                static_cast<size_t>(file_ino.size);
            auto buf = std::make_unique<char[]>(file_size);

            if (cpmOpen(&file_ino, &file, O_RDONLY) == -1) {
                plugin_config.log_print(
                    "\n\nError# Failed opening file %s in archive %s "
                    "in ProcessFile/cpmOpen",
                    dirent_raw_ptr,
                    hArcData->archname.data());
                close_file(hUnpFile);
                delete_file(dest);
                return E_BAD_DATA;
            }

            const auto rres =
                cpmRead(&file, buf.get(), file_size);

			if (rres > file_size) { // Somehow ssize_t in cpmfs.h is defined as unsigned... So returned -1 is a very large number. 
                plugin_config.log_print(
                    "\n\nError# Failed reading file %s from archive %s "
                    "in ProcessFile/cpmRead",
                    dirent_raw_ptr,
                    hArcData->archname.data());
                close_file(hUnpFile);
                preserve_failed_output(dirent_raw_ptr);
                return E_BAD_DATA;
            }

            const auto bytes_read =
                static_cast<size_t>(rres);

            if (bytes_read != 0) {
                const auto write_result =
                    write_file(
                        hUnpFile,
                        buf.get(),
                        bytes_read);

                if (write_result != bytes_read) {
                    plugin_config.log_print(
                        "\n\nError# Failed writing file %s "
                        "from archive %s "
                        "in ProcessFile/write_file",
                        dirent_raw_ptr,
                        hArcData->archname.data());
                    close_file(hUnpFile);
                    preserve_failed_output(
                        dirent_raw_ptr);
                    return E_EWRITE;
                }
            }

            if (bytes_read != file_size) {
                plugin_config.log_print(
                    "\n\nError# Short read for file %s from archive %s: "
                    "expected %zu bytes, read %zu bytes",
                    dirent_raw_ptr,
                    hArcData->archname.data(),
                    file_size,
                    bytes_read);
                close_file(hUnpFile);
                preserve_failed_output(dirent_raw_ptr);
                return E_BAD_DATA;
            }
        }

        if (file_ino.mtime != 0)
            set_file_datetime(hUnpFile, file_ino.mtime);

        close_file(hUnpFile);
        set_file_attributes_cpm(dest, file_ino.attr);

        if (Operation == PK_TEST)
            delete_file(dest);

        return 0;
    }

	// CloseArchive should perform all necessary operations when an archive is about to be closed
	DLLEXPORT int STDCALL CloseArchive(archive_HANDLE hArcData)
	{
		delete hArcData;
		hArcData = nullptr;
		return 0; // OK
	}

	// This function allows you to notify user about changing a volume when packing files
	DLLEXPORT void STDCALL SetChangeVolProc(archive_HANDLE hArcData, tChangeVolProc pChangeVolProc)
	{
		if( hArcData != reinterpret_cast<archive_HANDLE>(static_cast<size_t>(-1)) )
			hArcData->pLocChangeVol = pChangeVolProc;
	}

	// This function allows you to notify user about the progress when you un/pack files
	DLLEXPORT void STDCALL SetProcessDataProc(archive_HANDLE hArcData, tProcessDataProc pProcessDataProc)
	{
		if ( hArcData != reinterpret_cast<archive_HANDLE>( static_cast<size_t>(- 1)) )
			hArcData->pLocProcessData = pProcessDataProc;
	}

	// PackSetDefaultParams is called immediately after loading the DLL, before any other function. 
	// This function is new in version 2.1. 
	// It requires Total Commander >=5.51, but is ignored by older versions.
	DLLEXPORT void STDCALL PackSetDefaultParams(PackDefaultParamStruct* dps) { //-V2009
		auto res = plugin_config.read_conf(dps, false);
		if (!res) { // Create default configuration if conf file is absent
			plugin_config.write_conf();
		}
	}

	// GetBackgroundFlags is called to determine whether a plugin supports background packing or unpacking.
	// BACKGROUND_UNPACK == 1 Calls to OpenArchive, ReadHeader(Ex), ProcessFile and CloseArchive are thread-safe 
#ifdef _WIN64
	DLLEXPORT int STDCALL GetBackgroundFlags(PackDefaultParamStruct* dps) {
		return 0; //  BACKGROUND_UNPACK; // TODO: Currently code is deeply not thread-safe, including boo global variable of the libdsk library. 
	}
#endif 
	DLLEXPORT int STDCALL CanYouHandleThisFile(char* FileName) { // BOOL == int 
		// Caching results here would complicate code too much as for now
		std::unique_ptr<whole_disk_t> loc_arch;

		try {
			loc_arch = std::make_unique<whole_disk_t>(FileName, 0, PK_OM_LIST, true);
		}
		catch (disk_err_t& err) {
			plugin_config.log_print("\n\nError# Failed opening file: %s in CanYouHandleThisFile with code %i", 
				FileName, err.get_err_code());
			return 0;
		}
		catch (std::bad_alloc&) {
			plugin_config.log_print("\n\nError# Failed opening file: %s in CanYouHandleThisFile -- bad_alloc",
				FileName );
			return 0;
		}
		return 1; 
	}

	DLLEXPORT int STDCALL DeleteFiles(char* PackedFile, char* DeleteList) {
		// Caching results here would complicate code too much as for now
		std::unique_ptr<whole_disk_t> loc_arch;

		try {
			loc_arch = std::make_unique<whole_disk_t>(PackedFile, 0, PK_OM_LIST, false);
		}
		catch (disk_err_t& err) {
			plugin_config.log_print("\n\nError# Failed opening file: %s in DeleteFiles with code %i",
				PackedFile, err.get_err_code());
			return err.get_err_code();
		}
		catch (std::bad_alloc&) {
			plugin_config.log_print("\n\nError# Failed opening file: %s in DeleteFiles -- bad_alloc",
				PackedFile);
			return E_NO_MEMORY;
		}

		if (!loc_arch->can_we_write_this_format) {
			return E_NOT_SUPPORTED; 
		}

		// std::vector<const char*> file_list;
		const char* cur_ptr = DeleteList;
		size_t sl = strlen(cur_ptr);
		while( true ){
			// Erase
			sl = strlen(cur_ptr);
			if (sl == 0)
				break;
			std::string ps{cur_ptr};
			if (cpmimg_is_disk_info_name(ps.c_str())) {
				plugin_config.log_print("\nInfo# Ignoring virtual file %s", cur_ptr);
				cur_ptr += sl + 1;
				continue;
			}
			auto user_pos = ps.find('\\');
			if (user_pos == 2) {
				ps.erase(2, 1);
			}
			else if (user_pos == std::string::npos) {
				ps = "00" + ps;
			}
			else {
				plugin_config.log_print("\n\nError# Wrong file name %s for archive %s in DeleteFiles.", 
					cur_ptr, PackedFile);
			}
			if (cpmUnlink(&loc_arch->root, ps.c_str()) == -1)
			{
				plugin_config.log_print("\n\nError# Failed deleting file %s in archive %s with error: %s.", 
					ps.c_str(), PackedFile, boo); // Иииии! Обробка помилок...
				return E_NOT_SUPPORTED;
			}
			cur_ptr += sl + 1;
		}
		return 0;
	}

#if 0
	DLLEXPORT int STDCALL PackFiles(char* PackedFile, char* SubPath, char* SrcPath, char* AddList, int Flags) {
		const auto image_format = effective_image_format_for_archive(PackedFile);
		// PK_PACK_MOVE_FILES         1 Delete original after packing
		// PK_PACK_SAVE_PATHS         2 Save path names of files
		// PK_PACK_ENCRYPT            4 Ask user for password, then encrypt file with that password

		std::string SubPathS{SubPath ? SubPath : ""};
		if (!SubPathS.empty() && SubPathS.size() != 2) {
			// Impossible path
			plugin_config.log_print("\n\nError# Impossible in-image path: %s for %s archive in PackFiles\n",
				SubPath, PackedFile);
			return E_EOPEN;
		}

		//! Creat file
		//! TODO: add boot blocks support
		if (access(PackedFile, F_OK) != 0) {
			cpmSuperBlock super;
			memset(&super, 0, sizeof(super));
			cpmInode root;
			// file doesn't exist
			super.dev.opened = 0;
			bool use_uppercase = true;
			cpmReadSuper(&super, &root, image_format.data(),
				use_uppercase);
			size_t bootTrackSize = super.boottrk * super.secLength * super.sectrk;
			char* bootTracks = new char[bootTrackSize];
			// if ((bootTracks = malloc[bootTrackSize]) == (void*)0)
			const char* label = "unlabeled";
			bool use_timeStamps = false;
			memset(bootTracks, 0xe5, bootTrackSize);
			if (mkfs(&super, PackedFile, image_format.data(),
				label, bootTracks, use_timeStamps, use_uppercase) == -1)
			{
				plugin_config.log_print("\n\nError# Failed creating file: %s in PackFiles with error: %s",
					PackedFile, boo);
				return E_ECREATE;
			}
		}
		//! 
		std::unique_ptr<whole_disk_t> loc_arch;

		try {
			loc_arch = std::make_unique<whole_disk_t>(PackedFile, 0, PK_OM_LIST, false);
		}
		catch (disk_err_t& err) {
			plugin_config.log_print("\n\nError# Failed opening file: %s in DeleteFiles with code %i",
				PackedFile, err.get_err_code());
			return err.get_err_code();
		}
		catch (std::bad_alloc&) {
			plugin_config.log_print("\n\nError# Failed opening file: %s in DeleteFiles -- bad_alloc",
				PackedFile);
			return E_NO_MEMORY;
		}

		if (!loc_arch->can_we_write_this_format) {
			return E_NOT_SUPPORTED;
		}

		std::string SrcPathS{SrcPath ? SrcPath : ""};

		// std::vector<const char*> file_list;
		const char* cur_ptr = AddList;
		size_t sl = strlen(cur_ptr);
		while (true) {
			// Erase
			sl = strlen(cur_ptr);
			if (sl == 0)
				break;
			std::string ps{cur_ptr};
			if (cpmimg_is_disk_info_name(ps.c_str())) {
				plugin_config.log_print("\nInfo# Ignoring virtual file %s", cur_ptr);
				cur_ptr += sl + 1;
				continue;
			}
			auto user_pos = ps.find('\\');
			if ( user_pos == ps.size() - 1 ) {
				cur_ptr += sl + 1;
				continue; // We do not creat folders by themself 
			}
			if (user_pos == 2 && SubPathS.empty() ) {
				ps.erase(2, 1);
			}
			else if (user_pos == std::string::npos) {
				if(SubPathS.empty())
					ps = "00" + ps;
				else 
					ps = SubPathS + ps;
			}
			else {
				plugin_config.log_print("\n\nError# Wrong file name %s for archive %s in DeleteFiles.",
					cur_ptr, PackedFile);
			}
			// TODO: Process text files?
			auto src_file_path = SrcPathS + cur_ptr; // cur_ptr -- original name
			auto filehdr = open_file_shared_read(src_file_path.c_str());
			if (filehdr == file_open_error_v)
			{
				plugin_config.log_print("\n\nError# Failed opening file %s", src_file_path.c_str());
				return E_EOPEN;
			}
			
			auto file_size = get_file_size(filehdr);
			
			char* buf = new char[ file_size+128 ];

			auto read_size = read_file(filehdr, buf, file_size);
			// TODO: check errors in read_file
			// TODO: check correctness 
			// The end of an ASCII file is denoted by a CTRL-Z character (1AH) 
			// or a real end-of-file returned by the CP/M read operation. 
			// CTRL-Z characters embedded within machine code files (for example, 
			// COM files) are ignored and the end-of-file condition returned 
			// by CP/M is used to terminate read operations.
			// http://www.gaby.de/cpm/manuals/archive/cpm22htm/ch5.htm
			if (file_size % 128 != 0) {
				auto cur_idx = file_size;
				buf[cur_idx++] = 0x1A;
				while (cur_idx % 128) {
					buf[cur_idx++] = 0x1A;
				}
			}

			struct cpmFile file;
			struct cpmInode ino;

			//! TODO: Fix this hack 
			auto temp_err = cpmUnlink(&loc_arch->root, ps.c_str());
			
			if (cpmCreat(&loc_arch->root, ps.c_str(), &ino, 0666) == -1)
			{
				plugin_config.log_print("\n\nError# Failed creating file %s in archive %s with error: %s.",
					ps.c_str(), PackedFile, boo);
			}

			cpmOpen(&ino, &file, O_WRONLY);

			auto written_size = cpmWrite(&file, buf, ((file_size + 127)/128)*128);

			if (cpmClose(&file) != 0)
			{
				plugin_config.log_print("\n\nError# Failed closing file %s in archive %s with error: %s.",
					ps.c_str(), PackedFile, boo);
			}

			delete[] buf;
			close_file(filehdr);
			

			if (false)
			{
				plugin_config.log_print("\n\nError# Failed deleting file %s in archive %s with error: %s.",
					ps.c_str(), PackedFile, boo); // Иииии! обробка помилок...
				return E_NOT_SUPPORTED;
			}
			cur_ptr += sl + 1;
		}
		return 0;
	}

#endif

	DLLEXPORT int STDCALL PackFiles(char* PackedFile, char* SubPath, char* SrcPath, char* AddList, int Flags) {

		(void)Flags;

		const auto image_format =
			effective_image_format_for_archive(PackedFile);
		// PK_PACK_MOVE_FILES         1 Delete original after packing
		// PK_PACK_SAVE_PATHS         2 Save path names of files
		// PK_PACK_ENCRYPT            4 Ask user for password, then encrypt file with that password

		std::string SubPathS{SubPath ? SubPath : ""};
		if (!SubPathS.empty() && SubPathS.size() != 2) {
			plugin_config.log_print(
				"\n\nError# Impossible in-image path: %s "
				"for %s archive in PackFiles",
				SubPath,
				PackedFile);
			return E_EOPEN;
		}

		std::string SrcPathS{SrcPath ? SrcPath : ""};
		std::vector<pending_pack_file_t> pending_files;

		const char* cur_ptr = AddList;
		while (true) {
			const auto list_entry_size = strlen(cur_ptr);
			if (list_entry_size == 0)
				break;

			std::string cpm_name{cur_ptr};
			if (cpmimg_is_disk_info_name(cpm_name.c_str())) {
				plugin_config.log_print(
					"\nInfo# Ignoring virtual file %s",
					cur_ptr);
				cur_ptr += list_entry_size + 1;
				continue;
			}

			const auto user_pos = cpm_name.find('\\');
			if (user_pos == cpm_name.size() - 1) {
				cur_ptr += list_entry_size + 1;
				continue;
			}

			if (user_pos == 2 && SubPathS.empty()) {
				cpm_name.erase(2, 1);
			}
			else if (user_pos == std::string::npos) {
				cpm_name =
					SubPathS.empty()
						? "00" + cpm_name
						: SubPathS + cpm_name;
			}
			else {
				plugin_config.log_print(
					"\n\nError# Wrong file name %s for "
					"archive %s in PackFiles",
					cur_ptr,
					PackedFile);
				return E_BAD_DATA;
			}

			const auto duplicate = std::find_if(
				pending_files.begin(),
				pending_files.end(),
				[&](const pending_pack_file_t& pending) {
					return _stricmp(
						pending.cpm_name.c_str(),
						cpm_name.c_str()) == 0;
				});
			if (duplicate != pending_files.end()) {
				plugin_config.log_print(
					"\n\nError# More than one source file maps "
					"to CP/M name %s in PackFiles",
					cpm_name.c_str());
				return E_BAD_DATA;
			}

			pending_pack_file_t pending;
			const auto source_path = SrcPathS + cur_ptr;
			const auto load_result = load_pending_pack_file(
				source_path,
				cpm_name,
				pending);
			if (load_result != 0)
				return load_result;

			pending_files.push_back(std::move(pending));
			cur_ptr += list_entry_size + 1;
		}

		const bool packed_file_exists =
			access(PackedFile, F_OK) == 0;

		if (pending_files.empty() && packed_file_exists)
			return 0;

		std::string transaction_path;
		const auto prepare_result = prepare_transaction_image(
			PackedFile,
			packed_file_exists,
			transaction_path);
		if (prepare_result != 0)
			return prepare_result;

		transaction_image_guard_t transaction_guard{
			transaction_path
		};
		remember_image_format_for_archive(
			transaction_path.c_str(),
			image_format);

		if (!packed_file_exists) {
			const auto create_result =
				create_empty_transaction_image(
					transaction_path,
					image_format);
			if (create_result != 0)
				return create_result;
		}

		std::unique_ptr<whole_disk_t> loc_arch;
		try {
			loc_arch = std::make_unique<whole_disk_t>(
				transaction_path.c_str(),
				0,
				PK_OM_LIST,
				false);
		}
		catch (disk_err_t& err) {
			plugin_config.log_print(
				"\n\nError# Failed opening transaction image "
				"%s in PackFiles with code %i",
				transaction_path.c_str(),
				err.get_err_code());
			return err.get_err_code();
		}
		catch (std::bad_alloc&) {
			return E_NO_MEMORY;
		}

		if (!loc_arch->can_we_write_this_format)
			return E_NOT_SUPPORTED;

		for (const auto& pending : pending_files) {
			cpmInode previous_inode{};
			const bool replacing_existing =
				cpmNamei(
					&loc_arch->root,
					pending.cpm_name.c_str(),
					&previous_inode) != -1;
			const auto previous_attributes =
				replacing_existing
					? previous_inode.attr
					: cpm_attr_t{};

			if (replacing_existing &&
				cpmUnlink(
					&loc_arch->root,
					pending.cpm_name.c_str()) == -1) {
				plugin_config.log_print(
					"\n\nError# Failed removing old file %s "
					"from transaction image %s: %s",
					pending.cpm_name.c_str(),
					transaction_path.c_str(),
					boo ? boo : "unknown error");
				return E_EWRITE;
			}

			cpmInode inode{};
			if (cpmCreat(
					&loc_arch->root,
					pending.cpm_name.c_str(),
					&inode,
					0666) == -1) {
				plugin_config.log_print(
					"\n\nError# Failed creating file %s in "
					"transaction image %s: %s",
					pending.cpm_name.c_str(),
					transaction_path.c_str(),
					boo ? boo : "unknown error");
				return E_EWRITE;
			}

			cpmFile file{};
			if (cpmOpen(&inode, &file, O_WRONLY) == -1) {
				plugin_config.log_print(
					"\n\nError# Failed opening file %s for "
					"writing in transaction image %s: %s",
					pending.cpm_name.c_str(),
					transaction_path.c_str(),
					boo ? boo : "unknown error");
				return E_EOPEN;
			}

			if (!pending.data.empty()) {
				const auto written_size = cpmWrite(
					&file,
					pending.data.data(),
					pending.data.size());

				if (written_size != pending.data.size()) {
					(void)cpmClose(&file);
					plugin_config.log_print(
						"\n\nError# Incomplete write of %s "
						"to transaction image %s: expected "
						"%zu bytes, wrote %zu bytes",
						pending.cpm_name.c_str(),
						transaction_path.c_str(),
						pending.data.size(),
						static_cast<size_t>(written_size));
					return E_EWRITE;
				}
			}

			if (cpmClose(&file) != 0) {
				plugin_config.log_print(
					"\n\nError# Failed closing file %s in "
					"transaction image %s: %s",
					pending.cpm_name.c_str(),
					transaction_path.c_str(),
					boo ? boo : "unknown error");
				return E_ECLOSE;
			}

			if (replacing_existing &&
				cpmAttrSet(
					&inode,
					previous_attributes) == -1) {
				plugin_config.log_print(
					"\n\nError# Failed restoring attributes "
					"of %s in transaction image %s: %s",
					pending.cpm_name.c_str(),
					transaction_path.c_str(),
					boo ? boo : "unknown error");
				return E_EWRITE;
			}
		}

		const auto close_result = loc_arch->close_image();
		loc_arch.reset();
		if (close_result == -1) {
			plugin_config.log_print(
				"\n\nError# Failed synchronizing or closing "
				"transaction image %s: %s",
				transaction_path.c_str(),
				boo ? boo : "unknown error");
			return E_EWRITE;
		}

		const auto verify_result = verify_transaction_image(
			transaction_path,
			image_format,
			pending_files);
		if (verify_result != 0)
			return verify_result;

		if (!commit_transaction_image(
				PackedFile,
				transaction_path,
				packed_file_exists)) {
			const auto replace_error = GetLastError();
			transaction_guard.preserve();
			plugin_config.log_print(
				"\n\nError# Transaction image %s was valid, "
				"but replacing %s failed (Windows error %lu). "
				"The transaction image was preserved.",
				transaction_path.c_str(),
				PackedFile,
				static_cast<unsigned long>(replace_error));
			return E_EWRITE;
		}

		transaction_guard.release();
		remember_image_format_for_archive(
			PackedFile,
			image_format);
		return 0;
	}


	DLLEXPORT void STDCALL ConfigurePacker(
	    HWND Parent,
	    HINSTANCE DllInstance)
	{
	    (void)Parent;
	    (void)DllInstance;

	    auto disks_set = parse_diskdefs_c(
	        plugin_config.diskdefs_file_path.data());

	    img_type_sel_GUI_t img_type_sel_GUI(
	        disks_set,
	        {},
	        false,
	        false,
	        0,
	        nullptr,
	        effective_format_probing_enabled());

	    if (img_type_sel_GUI.action() !=
	        format_dialog_action_t::mount_selected) {
	        return;
	    }

	    plugin_config.image_format =
	        img_type_sel_GUI.get_image_type();

	    const bool enable_probing =
	        img_type_sel_GUI.
	            automatic_probing_enabled();

	    set_format_probing_for_session(
	        enable_probing);

	    if (img_type_sel_GUI.
	        save_probing_preference()) {
	        plugin_config.enable_format_probing =
	            enable_probing;
	    }

	    plugin_config.write_conf();
	} //-V773

	DLLEXPORT int STDCALL GetPackerCaps() { // Remove PK_CAPS_BY_CONTENT? 
		return PK_CAPS_BY_CONTENT | PK_CAPS_SEARCHTEXT | PK_CAPS_DELETE | 
			   PK_CAPS_MODIFY | PK_CAPS_NEW | PK_CAPS_MULTIPLE | PK_CAPS_OPTIONS;
		// PK_CAPS_DELETE // PK_CAPS_MODIFY //PK_CAPS_ENCRYPT // PK_CAPS_NEW 
	}
}

