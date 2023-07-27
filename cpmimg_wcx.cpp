// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
/*
* CP/M floppy disk images plugin for the Total Commander.
* Copyright (c) 2022-2023, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
*
*/


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

#include "wcxhead.h"
#include <new>
#include <memory>
#include <cstddef>
#include <vector>
#include <algorithm>
#include <optional>
#include <map>
#include <cassert>

// #define FLTK_ENABLED_EXPERIMENTAL  // Here for the quick tests -- should be defined by the build system

#ifdef FLTK_ENABLED_EXPERIMENTAL
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/fl_ask.H>
#endif

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


// The DLL entry point
BOOL APIENTRY DllMain(HANDLE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
) {
	return TRUE;
}

bool set_file_attributes_ex(const char* filename, uint32_t attribute) {
	// TODO: Fix
	/*
	DWORD winattr = FILE_ATTRIBUTE_NORMAL;
	if (attribute.is_readonly()) winattr |= FILE_ATTRIBUTE_READONLY;
	if (attribute.is_archive() ) winattr |= FILE_ATTRIBUTE_ARCHIVE;
	if (attribute.is_hidden()  ) winattr |= FILE_ATTRIBUTE_HIDDEN;
	if (attribute.is_system()  ) winattr |= FILE_ATTRIBUTE_SYSTEM;
	*/
	return set_file_attributes(filename, attribute); // Codes are equal
}

plugin_config_t plugin_config; 

struct whole_disk_t {
	minimal_fixed_string_t<MAX_PATH> archname; // Should be saved for the TCmd API
	file_handle_t hArchFile = file_handle_t(); //opened file handles
	int openmode_m = PK_OM_LIST;
	size_t image_file_size = 0;	

	tChangeVolProc   pLocChangeVol = nullptr;
	tProcessDataProc pLocProcessData = nullptr;

	cpmSuperBlock super;
	cpmInode root;
	std::string format{FORMAT}; //  osb1sssd, osbexec1
	// struct cpmInode root;
	std::string driver_name{}; // devopts; example: driver_name=="imd", "tele" etc.
	bool use_uppercase = true;

	//! TODO: rename 
	char starlit[2] = "*";
	char* const star[1] = { starlit };
	char** gargv = nullptr;
	int gargc;

	uint32_t curren_file_counter = 0;

	whole_disk_t(const char* archname_in, size_t vol_size, file_handle_t fh, int openmode):
		hArchFile{ fh }, openmode_m(openmode), image_file_size(vol_size)
	{
		archname.push_back(archname_in);
	}

	uint32_t users_counter = 0;

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

	~whole_disk_t() {
		if (gargv) {
			cpmglobfree(gargv, gargc);
			// TODO: fix call of cpmUmount
			cpmUmount(&super);
//			Device_close(&super.dev);
		}
		if (hArchFile)
			close_file(hArchFile);
	}

};
#if 0
int FAT_image_t::extract_to_file(file_handle_t hUnpFile, uint32_t idx) {
	try { // For bad allocation
		const auto& cur_entry = arc_dir_entries[idx];
		uint32_t nextclus = cur_entry.FirstClus;
		size_t remaining = cur_entry.FileSize;
		std::vector<char> buff(get_cluster_size());
		while (remaining > 0)
		{
			if ( (nextclus <= 1) || (nextclus >= min_end_of_chain_FAT()) )
			{
				plugin_config.log_print_dbg("Error# Wrong cluster number in chain: %d in file: %s", 
							nextclus, cur_entry.PathName.data());
				close_file(hUnpFile);
				return E_UNKNOWN_FORMAT;
			}
			set_file_pointer(get_archive_handler(), cluster_to_image_off(nextclus));
			size_t towrite = std::min<size_t>(get_cluster_size(), remaining);
			size_t result = read_file(get_archive_handler(), buff.data(), towrite);
			if (result != towrite)
			{
				close_file(hUnpFile);
				return E_EREAD;
			}
			result = write_file(hUnpFile, buff.data(), towrite);
			if (result != towrite)
			{
				close_file(hUnpFile);
				return E_EWRITE;
			}
			if (remaining > get_cluster_size()) { remaining -= get_cluster_size(); } //-V104 //-V101
			else { remaining = 0; }

			nextclus = next_cluster_FAT(nextclus);
		}
		return 0;
	}
	catch (std::bad_alloc&) {
		return E_NO_MEMORY;
	}
}

// root passed by copy to avoid problems while relocating vector
int FAT_image_t::load_file_list_recursively(minimal_fixed_string_t<MAX_PATH> root, uint32_t firstclus, uint32_t depth) //-V813
{
	if (root.is_empty()) { // Initial reading
		counter = 0;
		arc_dir_entries.clear();
	}

	if (firstclus == 0 && FAT_type == FAT32_type) {
		// For exotic implementations, if BS_RootFirstClus == 0, will behave as expected
		firstclus = bootsec.EBPB_FAT32.BS_RootFirstClus; 
	}

	size_t portion_size = 0;
	if (firstclus == 0)
	{   // Read whole FAT12/16 dir at once
		set_file_pointer(get_archive_handler(), get_root_area_offset());
		portion_size = get_root_dir_size();
	}
	else {
		set_file_pointer(get_archive_handler(), cluster_to_image_off(firstclus));
		portion_size = static_cast<size_t>(get_cluster_size());
	}
	size_t records_number = portion_size / sizeof(FATxx_dir_entry_t);
	std::unique_ptr<FATxx_dir_entry_t[]> sector;
	try {
		sector = std::make_unique<FATxx_dir_entry_t[]>(records_number);
	}
	catch (std::bad_alloc&) {
		return E_NO_MEMORY;
	}

	if (firstclus >= max_normal_cluster_FAT()) {
		plugin_config.log_print_dbg("Warning# Unusual first "
			"clusters number:  %d of %d", firstclus, max_normal_cluster_FAT());
	}
	if ( (firstclus == 1) || (firstclus >= max_cluster_FAT()) ) {
		plugin_config.log_print_dbg("Error# Wrong first "
			"clusters number: %d of 2-%d", firstclus, max_cluster_FAT());
		return E_UNKNOWN_FORMAT;
	}
	size_t result = read_file(get_archive_handler(), sector.get(), portion_size);
	if (result != portion_size) {
		return E_EREAD;
	} 

	LFN_accumulator_t current_LFN;
	do {
		size_t entry_in_cluster = 0;
		while ((entry_in_cluster < records_number) && (!sector[entry_in_cluster].is_dir_record_free()))
		{
			if (sector[entry_in_cluster].is_dir_record_longname_part()) {
				if (plugin_config.use_VFAT) {
					current_LFN.process_LFN_record(&sector[entry_in_cluster]);
				}
				entry_in_cluster++;
				continue;				
			}

			if (sector[entry_in_cluster].is_dir_record_volumeID()) 
			{
				minimal_fixed_string_t<12> voll;
				sector[entry_in_cluster].dir_entry_name_to_str(voll);
				plugin_config.log_print_dbg("Info# Volume label: %s", voll.data());
			}

			if (sector[entry_in_cluster].is_dir_record_deleted() ||
				sector[entry_in_cluster].is_dir_record_unknown() ||
				sector[entry_in_cluster].is_dir_record_volumeID() ||
				sector[entry_in_cluster].is_dir_record_invalid_attr()
				)
			{
				if(current_LFN.are_processing())
					current_LFN.abort_processing();
				entry_in_cluster++;
				continue;
			}

			arc_dir_entries.emplace_back();
			auto& newentryref = arc_dir_entries.back();
			newentryref.FileAttr = sector[entry_in_cluster].DIR_Attr;
			newentryref.PathName.push_back(root); // Empty root is OK, "\\" OK too
			if (plugin_config.use_VFAT && current_LFN.are_processing()) {
				if (current_LFN.cur_LFN_CRC == VFAT_LFN_dir_entry_t::LFN_checksum(sector[entry_in_cluster].DIR_Name)) {
					newentryref.PathName.push_back(current_LFN.cur_LFN_name);
				}
				else {
					auto res = sector[entry_in_cluster].process_E5();
					if(!res)
						plugin_config.log_print_dbg("Warning# E5 occurred at first symbol.");
					auto invalid_chars = sector[entry_in_cluster].dir_entry_name_to_str(newentryref.PathName);					
					// No OS/2 EA on FAT32
				}
				current_LFN.abort_processing();
			}
			else {
				auto res = sector[entry_in_cluster].process_E5(); 
				if (!res)
					plugin_config.log_print_dbg("Warning# E5 occurred at first symbol.");
				auto invalid_chars = sector[entry_in_cluster].dir_entry_name_to_str(newentryref.PathName);
				if (invalid_chars == FATxx_dir_entry_t::LLDE_OS2_EA) {
					plugin_config.log_print_dbg("Info# OS/2 Extended attributes found.");
					has_OS2_EA = true;
				}
			}

			newentryref.FileTime = sector[entry_in_cluster].get_file_datetime();
			newentryref.FileSize = sector[entry_in_cluster].DIR_FileSize;
			newentryref.FirstClus = get_first_cluster(sector[entry_in_cluster]);

			if (sector[entry_in_cluster].is_dir_record_dir()) {
				newentryref.PathName.push_back('\\'); // Neccessery for empty dirs to be "enterable"
			}
			if (depth > 100) {
				plugin_config.log_print_dbg("Too many nested directories: %d.", depth);
			}
			if (sector[entry_in_cluster].is_dir_record_dir() &&
				(newentryref.FirstClus < max_cluster_FAT()) && (newentryref.FirstClus > 0x1)
				&& (depth <= 100))
			{
				load_file_list_recursively(newentryref.PathName, newentryref.FirstClus, depth + 1);
			}
			++entry_in_cluster;
		}
		if (entry_in_cluster < records_number) { return 0; }
		if (firstclus == 0)
		{
			break; // We already processed FAT12/16 root dir
		}
		else {
			firstclus = next_cluster_FAT(firstclus);
			if (firstclus >= max_normal_cluster_FAT() && !is_end_of_chain_FAT(firstclus)) {
				plugin_config.log_print_dbg("Warning# Unusual next "
					"clusters number: %d of %d", firstclus, max_normal_cluster_FAT());
			}
			if ((firstclus <= 1) || ( (firstclus >= max_cluster_FAT()) && !is_end_of_chain_FAT(firstclus)) ) {
				plugin_config.log_print_dbg("Error# Wrong next "
					"clusters number: %d of 2-%d", firstclus, max_cluster_FAT());
			}
			if (is_end_of_chain_FAT(firstclus)) {
				break;
			}

			set_file_pointer(get_archive_handler(), cluster_to_image_off(firstclus)); //-V104
		}
		result = read_file(get_archive_handler(), sector.get(), portion_size);
		if (result != portion_size) {
			return E_EREAD;
		}
	} while (true);

	if (root.is_empty()) { // Initial finished
		arc_dir_entries.shrink_to_fit();
	}
	return 0;
}
#endif

//------- whole_disk_t implementation --------------------------
using archive_HANDLE = whole_disk_t*;

//-----------------------=[ DLL exports ]=--------------------

extern "C" {
	// OpenArchive should perform all necessary operations when an archive is to be opened
	DLLEXPORT archive_HANDLE STDCALL OpenArchive(tOpenArchiveData* ArchiveData)
	{
		plugin_config.log_print("\n\nInfo# Opening file: %s", ArchiveData->ArcName);
		auto rdconf = plugin_config.read_conf(nullptr, true); // Reread confuguration

		std::unique_ptr<whole_disk_t> arch; // TCmd API expects HANDLE/raw pointer,
										// so smart pointer is used to manage cleanup on errors 
										// only inside this function
		//! Not used by TCmd yet.
		ArchiveData->CmtBuf = 0;
		ArchiveData->CmtBufSize = 0;
		ArchiveData->CmtSize = 0;
		ArchiveData->CmtState = 0;

		auto hArchFile = open_file_shared_read(ArchiveData->ArcName);
		if (hArchFile == file_open_error_v)
		{
			ArchiveData->OpenResult = E_EOPEN;
			return nullptr;
		}
		try {
			arch = std::make_unique<whole_disk_t>(ArchiveData->ArcName, 0,
				hArchFile, ArchiveData->OpenMode);
		}
		catch (std::bad_alloc&) {
			ArchiveData->OpenResult = E_NO_MEMORY;
			return nullptr;
		}

		const char* errs = Device_open(&(arch->super.dev), ArchiveData->ArcName, O_RDONLY,
			arch->driver_name.empty() ? nullptr : arch->driver_name.c_str());

		if (errs) // Pointer to error string 
		{
			plugin_config.log_print("\n\nError# Failed opening file: %s", errs);
			ArchiveData->OpenResult = E_EOPEN;
			return nullptr;
		}
		int erri = cpmReadSuper(&arch->super, &arch->root, 
			arch->format.empty() ? nullptr : arch->format.c_str(), 
			arch->use_uppercase);
		if (erri == -1) 
		{
			plugin_config.log_print("\n\nError# Failed reading superblock.");
			ArchiveData->OpenResult = E_EOPEN;
			return nullptr;
		}

		cpmglob(0, 1, arch->star, &arch->root, &arch->gargc, &arch->gargv);
		for (int i = 0; i < arch->gargc; ++i) {
			if ( arch->gargv[i][0] != '.' && 
				(arch->gargv[i][0] != '0' || arch->gargv[i][1] != '0') 
			   ) {
				++arch->users_counter;
			}
		}

		return arch.release(); // Returns raw ptr and releases ownership 
	}

	// TCmd calls ReadHeader to find out what files are in the archive
	DLLEXPORT int STDCALL ReadHeader(archive_HANDLE hArcData, tHeaderData* HeaderData)
	{
		if (hArcData->curren_file_counter < hArcData->gargc)
		{
			auto root_ino = &hArcData->root;
			cpmInode file_ino;
			auto dirent_raw_ptr = hArcData->gargv[hArcData->curren_file_counter];

			if (strcmp(dirent_raw_ptr, "..") == 0) { // Skip
				++hArcData->curren_file_counter;
				if( hArcData->curren_file_counter < hArcData->gargc )
					dirent_raw_ptr = hArcData->gargv[hArcData->curren_file_counter];
				else 
					return E_END_ARCHIVE;
			}

			cpmNamei(root_ino, dirent_raw_ptr, &file_ino);

			strcpy(HeaderData->ArcName, hArcData->archname.data());

			if (hArcData->users_counter == 0)
			{
				
				strcpy(HeaderData->FileName, dirent_raw_ptr + 2);
			}
			else {
				minimal_fixed_string_t<MAX_PATH> ts{dirent_raw_ptr, 2};
				ts.push_back("\\");
				ts.push_back(dirent_raw_ptr + 2);
				strcpy(HeaderData->FileName, ts.data());
			}
			HeaderData->FileAttr = hArcData->cpm_attr_to_tcmd_attr(file_ino.attr);
			// TODO: check and fix time
			HeaderData->FileTime = file_ino.mtime;
			HeaderData->PackSize = file_ino.size; // (statbuf.size+127)/128
			HeaderData->UnpSize = HeaderData->PackSize;
			HeaderData->CmtBuf = 0;
			HeaderData->CmtBufSize = 0;
			HeaderData->CmtSize = 0;
			HeaderData->CmtState = 0;
			HeaderData->UnpVer = 0;
			HeaderData->Method = 0;
			HeaderData->FileCRC = 0;
			++hArcData->curren_file_counter;
			return 0;
		}
		
		hArcData->curren_file_counter = 0;
		return E_END_ARCHIVE;
	}

	// ProcessFile should unpack the specified file or test the integrity of the archive
	DLLEXPORT int STDCALL ProcessFile(archive_HANDLE hArcData, int Operation, char* DestPath, char* DestName) //-V2009
	{
		char dest[MAX_PATH] = "";
		file_handle_t hUnpFile;

		if (Operation == PK_SKIP) return 0;

		if (hArcData->curren_file_counter > hArcData->gargc)
			return E_NO_MEMORY; // Logic error

		if ( hArcData->curren_file_counter == 0 )
			return E_END_ARCHIVE;
		// if (newentry->FileAttr & ATTR_DIRECTORY) return 0;

		if (Operation == PK_TEST) {
			auto res = get_temp_filename(dest, "FIM");
			if (!res) {
				return E_ECREATE;
			}
		}
		else {
			if (DestPath) strcpy(dest, DestPath);
			if (DestName) strcat(dest, DestName);
		}

		struct cpmFile file;
		auto root_ino = &hArcData->root;
		cpmInode file_ino;
		auto dirent_raw_ptr = hArcData->gargv[hArcData->curren_file_counter - 1];
		cpmNamei(root_ino, dirent_raw_ptr, &file_ino);

		std::unique_ptr<char[]> buf{ new char[file_ino.size] };
		cpmOpen(&file_ino, &file, O_RDONLY);
		auto rres = cpmRead(&file, buf.get(), file_ino.size);
		if (Operation == PK_TEST) {
			hUnpFile = open_file_overwrite(dest);
		}
		else {
			hUnpFile = open_file_write(dest);
		}
		if (hUnpFile == file_open_error_v)
			return E_ECREATE;
		write_file(hUnpFile, buf.get(), file_ino.size);

		if(file_ino.mtime != 0 )
			set_file_datetime(hUnpFile, file_ino.mtime); // TODO: Check and fix
		close_file(hUnpFile);
		set_file_attributes_ex(dest, hArcData->cpm_attr_to_tcmd_attr(file_ino.attr) );

		if (Operation == PK_TEST) {
			delete_file(dest);
		}
		return 0;
	}

	// CloseArchive should perform all necessary operations when an archive is about to be closed
	DLLEXPORT int STDCALL CloseArchive(archive_HANDLE hArcData)
	{
		delete hArcData;
		return 0; // OK
	}

	// This function allows you to notify user about changing a volume when packing files
	DLLEXPORT void STDCALL SetChangeVolProc(archive_HANDLE hArcData, tChangeVolProc pChangeVolProc)
	{
		hArcData->pLocChangeVol = pChangeVolProc;
	}

	// This function allows you to notify user about the progress when you un/pack files
	DLLEXPORT void STDCALL SetProcessDataProc(archive_HANDLE hArcData, tProcessDataProc pProcessDataProc)
	{
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
		return BACKGROUND_UNPACK;
	}
#endif 
	DLLEXPORT int STDCALL CanYouHandleThisFile(char* FileName) { // BOOL == int 
		auto hArchFile = open_file_shared_read(FileName);
		if (hArchFile == file_open_error_v)
		{
			return 0;
		} else {
			close_file(hArchFile);
		}

		// Caching results here would complicate code too much as for now
		cpmSuperBlock super;
		cpmInode root;
		//! TODO: Read from config
		std::string format{FORMAT}; //  osb1sssd, osbexec1
		// struct cpmInode root;
		std::string driver_name{}; // devopts; example: driver_name=="imd", "tele" etc.
		//! TODO: parse extension and use it as a possible driver name.
		bool use_uppercase = true;

		const char* errs = Device_open(&(super.dev), FileName, O_RDONLY,
			driver_name.empty() ? nullptr : driver_name.c_str());

		if (errs) // Pointer to error string 
		{
			plugin_config.log_print("\n\nError# Failed opening file: %s in CanYouHandleThisFile\n", errs);
			return 0;
		}
		int erri = cpmReadSuper(&super, &root,
			format.empty() ? nullptr : format.c_str(),
			use_uppercase);
		if (erri == -1)
		{
			plugin_config.log_print("\n\nError# Failed reading superblock of %s in CanYouHandleThisFile.", FileName);
			return 0;
		}
		cpmUmount(&super);

		return 1;
//		whole_disk_t arch{ FileName, image_file_size,
	//			hArchFile, PK_OM_LIST };

//		auto err_code = arch.process_volumes();
//		int is_OK = (err_code != 0);
//		return is_OK;
	}

	DLLEXPORT int STDCALL GetPackerCaps() {
		return PK_CAPS_BY_CONTENT | PK_CAPS_SEARCHTEXT;
	}
}

#if 0 // FLTK Dialogs:
Fl_Window* w = new Fl_Window(400, 300);
w->set_modal();
w->show();
while (w->shown()) Fl::wait();

https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setparent -- 
Changes the parent window of the specified child window.
HWND SetParent(
	[in]           HWND hWndChild,
	[in, optional] HWND hWndNewParent
);


FLTK windows as children of system windows: "replaces WS_POPUP with WS_CHILD flags"
https ://fltk.easysw.narkive.com/FJwMvdmO/windows-as-children-of-system-windows
HWND hWnd = (HWND)fl_xid(dlgWnd_);
SetWindowLong(hWnd, GWL_STYLE, (GetWindowLong(hWnd, GWL_STYLE) & ~WS_POPUP) | WS_CHILD);
SetParent(hWnd, parentWindow);

https://www.fltk.org/doc-1.3/osissues.html#osissues_win32
See "Handling Other WIN32 Messages"

Fl_Window* fl_find(HWND xid)
Returns the Fl_Window that corresponds to the given window handle, or NULL if not found.
This function uses a cache so it is slightly faster than iterating through the windows yourself.
#endif
