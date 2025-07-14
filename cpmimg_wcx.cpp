// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
/*
* CP/M floppy disk images plugin for the Total Commander.
* Copyright (c) 2022-2025, Oleg Farenyuk aka Indrekis ( indrekis@gmail.com )
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
#include <cassert>


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

// extern HINSTANCE g_GUI_dlg_hInstance;

// The DLL entry point
BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
) {
	// For some reason is called many-many times. 
	plugin_config.plugin_path = get_plugin_path(hModule);
	auto rdconf = plugin_config.read_conf(nullptr, true);
	g_GUI_dlg_hInstance = hModule;

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


struct whole_disk_t {
	minimal_fixed_string_t<MAX_PATH> archname; // Should be saved for the TCmd API
	file_handle_t hArchFile = file_handle_t(); //opened file handles
	int openmode_m = PK_OM_LIST;
	bool read_only = true;
	size_t image_file_size = 0;	

	tChangeVolProc   pLocChangeVol = nullptr;
	tProcessDataProc pLocProcessData = nullptr;

	cpmSuperBlock super;
	cpmInode root;
	// std::string format{FORMAT}; //  osb1sssd, osbexec1
	// struct cpmInode root;
	std::string driver_name{}; // devopts; example: driver_name=="imd", "tele" etc.
	bool use_uppercase = true;

	//! TODO: rename 
	char starlit[2] = "*";
	char* const star[1] = { starlit };
	char** gargv = nullptr;
	int gargc;

	uint32_t curren_file_counter = 0;

	whole_disk_t(const char* archname_in, size_t vol_size, int openmode, bool read_only_in):
		openmode_m(openmode), image_file_size(vol_size), read_only{ read_only_in }
	{
		archname.push_back(archname_in);
		process_image(read_only);
	}

	uint32_t users_counter = 0;

	~whole_disk_t() {
		if (hArchFile)
			close_file(hArchFile);
		if (gargv) {
			cpmglobfree(gargv, gargc);
		}
		cpmUmount(&super);
	}
private: 

	void process_image(bool read_only_in) {
		hArchFile = open_file_shared_read(archname.data());
		if (hArchFile == file_open_error_v)
		{
			hArchFile = file_handle_t();
			throw disk_err_t{ "Error opening archive file.", E_EOPEN };
		}
		close_file(hArchFile);
		hArchFile = file_handle_t();
		//=================================================================
		DSK_PDRIVER driver = nullptr;
		DSK_GEOMETRY geom{};
		dsk_err_t err;
		err = dsk_open(&driver, archname.data(), nullptr, nullptr);
		if (err) {
			throw disk_err_t{ "Error opening image archive file.", E_EOPEN };
		}

		err = dsk_getgeom(driver, &geom);
		if (err) {
			dsk_close(&driver);
			throw disk_err_t{ "Error reading image geometry.", E_EOPEN };
		}
		dsk_close(&driver);
		//=================================================================
		auto disks_set = parse_diskdefs_c(plugin_config.diskdefs_file_path.data());
		decltype(disks_set) possible_fmts;
		// TODO: Here is some duplication with img_type_sel_GUI_t
		const int enough_score = 2;
		for (const auto& dsk : disks_set) {
			int match_score = 0;
			if (geom.dg_secsize == dsk.secLength)
				++match_score;
			int geom_total_tracks = geom.dg_cylinders * geom.dg_heads;
			if (geom_total_tracks == dsk.tracks) 
				++match_score;
			if (geom.dg_sectors == dsk.sectrk) 
				++match_score;

			if (match_score >= enough_score ) {
				possible_fmts.push_back(dsk);
			}
		}
		//=================================================================

		const char* errs = Device_open(&super.dev, archname.data(), read_only_in ? O_RDONLY : O_RDWR,
			driver_name.empty() ? nullptr : driver_name.c_str());

		if (errs) // Pointer to error string 
		{
			close_file(hArchFile);
			plugin_config.log_print("\n\nError# Failed opening file: %s", errs);
			throw disk_err_t{ "Error in Device_open.", E_EOPEN };
		}
		int erri = cpmReadSuper(&super, &root,
			plugin_config.image_format.is_empty() ? nullptr : plugin_config.image_format.data(),
			use_uppercase);
		if (erri == -1)
		{
			close_file(hArchFile);
			plugin_config.log_print("\n\nError# Failed reading superblock.");
			while (true) {
				img_type_sel_GUI_t img_type_sel_GUI(possible_fmts, geom, true);				

				if(!img_type_sel_GUI.attempt_new_read())
					break;

				if (img_type_sel_GUI.save_disk_type()) {
					plugin_config.image_format = img_type_sel_GUI.get_image_type();
				}
				
				erri = cpmReadSuper(&super, &root,
					img_type_sel_GUI.get_image_type().data(),
					use_uppercase);
				if (erri == -1)
				{
					plugin_config.log_print("\n\nError# Failed reading superblock.");
				}
				else
					break;

			} //-V773
			if (erri == -1)
				throw disk_err_t{ "Error in cpmReadSuper.", E_EOPEN };
		}
	}
};
//------- whole_disk_t implementation --------------------------
using archive_HANDLE = whole_disk_t*;

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

			// TODO: support for the [passwd] [label] names 
			// TODO: cpmNamei sometimes returns error code. Example: COB1A.IMD/SQUARO -- some extents error.
			//       then the struct is filled with void values.
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
			HeaderData->FileAttr = cpm_attr_to_tcmd_attr(file_ino.attr);
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
		auto nres = cpmNamei(root_ino, dirent_raw_ptr, &file_ino);
		// If we got this far, for errors different than E_ECREATE, TCmd expects file shold exist
		if (Operation == PK_TEST) {
			hUnpFile = open_file_overwrite(dest);
		}
		else {
			hUnpFile = open_file_write(dest);
		}
		if (hUnpFile == file_open_error_v)
			return E_ECREATE;

		if (nres == -1) { // In fact, already dangerous...
			// TCmd крашитьс€ тут, €кщо повторно, не виход€чи з арх≥ва, знову спробувати прочитати файл. 
			// але достатньо вийти-зайти -- очищати кеш, заход€чи в ≥нший арх≥в, не потр≥бно...
			plugin_config.log_print("\n\nError# Failed opening file %s in archive %s in ProcessFile/cpmNamei",
				dirent_raw_ptr, hArcData->archname);
			close_file(hUnpFile);
			return E_BAD_DATA;
		}
        auto buf = std::make_unique<char[]>(file_ino.size);
		// std::unique_ptr<char[]> buf{ new char[file_ino.size] };
		auto ores = cpmOpen(&file_ino, &file, O_RDONLY);
		if (ores == -1) {
			plugin_config.log_print("\n\nError# Failed opening file %s in archive %s in ProcessFile/cpmOpen",
				dirent_raw_ptr, hArcData->archname);
			close_file(hUnpFile);
			return E_BAD_DATA;
		}
		auto rres = cpmRead(&file, buf.get(), file_ino.size);

		write_file(hUnpFile, buf.get(), file_ino.size);

		if(file_ino.mtime != 0 )
			set_file_datetime(hUnpFile, file_ino.mtime); // TODO: Check and fix
		close_file(hUnpFile);
		set_file_attributes_cpm(dest, file_ino.attr );

		if (Operation == PK_TEST) {
			delete_file(dest);
		}
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
		return BACKGROUND_UNPACK;
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

		// std::vector<const char*> file_list;
		const char* cur_ptr = DeleteList;
		size_t sl = strlen(cur_ptr);
		while( true ){
			// Erase
			sl = strlen(cur_ptr);
			if (sl == 0)
				break;
			std::string ps{cur_ptr};
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
					ps.c_str(), PackedFile, boo); // »ииии! ќбробка помилок...
				return E_NOT_SUPPORTED;
			}
			cur_ptr += sl + 1;
		}
		return 0;
	}

	DLLEXPORT int STDCALL PackFiles(char* PackedFile, char* SubPath, char* SrcPath, char* AddList, int Flags) {
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
			cpmReadSuper(&super, &root, plugin_config.image_format.data(),
				use_uppercase);
			size_t bootTrackSize = super.boottrk * super.secLength * super.sectrk;
			char* bootTracks = new char[bootTrackSize];
			// if ((bootTracks = malloc[bootTrackSize]) == (void*)0)
			const char* label = "unlabeled";
			bool use_timeStamps = false;
			memset(bootTracks, 0xe5, bootTrackSize);
			if (mkfs(&super, PackedFile, plugin_config.image_format.data(),
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
					ps.c_str(), PackedFile, boo); // »ииии! ќбробка помилок...
				return E_NOT_SUPPORTED;
			}
			cur_ptr += sl + 1;
		}
		return 0;
	}

	DLLEXPORT void STDCALL ConfigurePacker(HWND Parent, HINSTANCE DllInstance) {
		auto disks_set = parse_diskdefs_c(plugin_config.diskdefs_file_path.data());
		img_type_sel_GUI_t img_type_sel_GUI(disks_set, {}, false);
		plugin_config.image_format = img_type_sel_GUI.get_image_type();
	} //-V773

	DLLEXPORT int STDCALL GetPackerCaps() { // Remove PK_CAPS_BY_CONTENT? 
		return PK_CAPS_BY_CONTENT | PK_CAPS_SEARCHTEXT | PK_CAPS_DELETE | 
			   PK_CAPS_MODIFY | PK_CAPS_NEW | PK_CAPS_MULTIPLE | PK_CAPS_OPTIONS;
		// PK_CAPS_DELETE // PK_CAPS_MODIFY //PK_CAPS_ENCRYPT // PK_CAPS_NEW 
	}
}

