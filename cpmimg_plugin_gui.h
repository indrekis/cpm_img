#pragma once 
#ifndef CPMIMG_PLUGIN_GUI_INC
#define CPMIMG_PLUGIN_GUI_INC

#ifndef GUI_PLUGIN_IMPORT
#define DLLEXPORTGUI __declspec(dllexport)
#else
#define DLLEXPORTGUI __declspec(dllimport)
#endif 

#ifdef _WIN32
#define STDCALL __stdcall
#else 
#define STDCALL 
#endif 

#include "cpmtools/config.h"
#include "cpmtools/cpmfs.h"
#include "cpmtools/getopt_.h"

#include "sysio_winapi.h"
#include "minimal_fixed_string.h"
#include "plugin_config.h"


#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <vector>
#include <string>
#include <cstdio>

#include "cpmimg_plugin_gui_resources.h"


extern HINSTANCE g_GUI_dlg_hInstance;

class img_type_sel_GUI_t {
private:
    HWND hDlg;
    HWND hComboFormat;
    HWND hEditSecLength;
    HWND hEditTracks;
    HWND hEditSectrk;
    HWND hEditBlksiz;
    HWND hEditMaxdir;
    HWND hEditDirblks;
    HWND hEditBoottrk;
    HWND hEditProbability;
    HWND hCheckSaveType;

    const std::vector<cpm_disk_descr_t>& possible_fmts;
    DSK_GEOMETRY img_geom;
    bool ui_retry = false;
    minimal_fixed_string_t<33> image_type;
    bool show_probab = false;

    static HFONT hBoldFont;
    static HFONT hNormalFont;
    
    void update_info_fields(int idx) {
        if (idx < 0 || idx >= static_cast<int>(possible_fmts.size()))
            return;

        const auto& dsk = possible_fmts[idx];
        int match_score = 0;

        if (show_probab) {
            if (img_geom.dg_secsize == dsk.secLength) {
                SendMessage(hEditSecLength, WM_SETFONT, (WPARAM)hBoldFont, TRUE);
                ++match_score;
            }
            else {
                SendMessage(hEditSecLength, WM_SETFONT, (WPARAM)hNormalFont, TRUE);
            }

            int geom_total_tracks = img_geom.dg_cylinders * img_geom.dg_heads;
            if (geom_total_tracks == dsk.tracks) {
                ++match_score;
                SendMessage(hEditTracks, WM_SETFONT, (WPARAM)hBoldFont, TRUE);
            }
            else {
                SendMessage(hEditTracks, WM_SETFONT, (WPARAM)hNormalFont, TRUE);
            }

            if (img_geom.dg_sectors == dsk.sectrk) {
                ++match_score;
                SendMessage(hEditSectrk, WM_SETFONT, (WPARAM)hBoldFont, TRUE);
            }
            else {
                SendMessage(hEditSectrk, WM_SETFONT, (WPARAM)hNormalFont, TRUE);
            }
        }

        char buf[128];
        sprintf(buf, "%d", dsk.secLength); SetWindowText(hEditSecLength, buf);
        sprintf(buf, "%d", dsk.tracks); SetWindowText(hEditTracks, buf);
        sprintf(buf, "%d", dsk.sectrk); SetWindowText(hEditSectrk, buf);
        sprintf(buf, "%d", dsk.blksiz); SetWindowText(hEditBlksiz, buf);
        sprintf(buf, "%d", dsk.maxdir); SetWindowText(hEditMaxdir, buf);
        sprintf(buf, "%d", dsk.dirblks); SetWindowText(hEditDirblks, buf);
        sprintf(buf, "%d", dsk.boottrk); SetWindowText(hEditBoottrk, buf);

        if (show_probab) {
            SendMessage(hEditProbability, WM_SETFONT, (WPARAM)hBoldFont, TRUE);
            switch (match_score) {
            case 0:
                SetWindowText(hEditProbability, "No");
                break;
            case 1:
                SetWindowText(hEditProbability, "Low probability");
                break;
            case 2:
                SetWindowText(hEditProbability, "Could be");
                break;
            case 3:
                SetWindowText(hEditProbability, "Yes");
                break;
            default:
                SetWindowText(hEditProbability, "Unknown");
            }
        }
    }

    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
        img_type_sel_GUI_t* pThis = reinterpret_cast<img_type_sel_GUI_t*>(GetWindowLongPtr(hDlg, GWLP_USERDATA));

        switch (message) {
        case WM_INITDIALOG:
        {
            pThis = reinterpret_cast<img_type_sel_GUI_t*>(lParam);
            SetWindowLongPtr(hDlg, GWLP_USERDATA, lParam);
            pThis->hDlg = hDlg;
            return pThis->OnInitDialog();
        }

        case WM_COMMAND:
            if (pThis) {
                return pThis->OnCommand(LOWORD(wParam), HIWORD(wParam));
            }
            break;

        case WM_KEYDOWN:
            if (pThis && GetFocus() == pThis->hComboFormat) {
                return pThis->OnComboKeyDown(wParam);
            }
            break;

        case WM_CLOSE:
            if (pThis) {
                pThis->ui_retry = false;
                EndDialog(hDlg, IDCANCEL);
            }
            return TRUE;
        }

        return FALSE;
    }

    BOOL OnInitDialog() {
        // Center the dialog on screen
        RECT rcDlg, rcDesktop;
        GetWindowRect(hDlg, &rcDlg);
        GetWindowRect(GetDesktopWindow(), &rcDesktop);

        int x = (rcDesktop.right - rcDesktop.left - (rcDlg.right - rcDlg.left)) / 2;
        int y = (rcDesktop.bottom - rcDesktop.top - (rcDlg.bottom - rcDlg.top)) / 2;
        SetWindowPos(hDlg, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        // Get control handles
        hComboFormat = GetDlgItem(hDlg, IDC_COMBO_FORMAT);
        hEditSecLength = GetDlgItem(hDlg, IDC_EDIT_SECLENGTH);
        hEditTracks = GetDlgItem(hDlg, IDC_EDIT_TRACKS);
        hEditSectrk = GetDlgItem(hDlg, IDC_EDIT_SECTRK);
        hEditBlksiz = GetDlgItem(hDlg, IDC_EDIT_BLKSIZ);
        hEditMaxdir = GetDlgItem(hDlg, IDC_EDIT_MAXDIR);
        hEditDirblks = GetDlgItem(hDlg, IDC_EDIT_DIRBLKS);
        hEditBoottrk = GetDlgItem(hDlg, IDC_EDIT_BOOTTRK);
        hEditProbability = GetDlgItem(hDlg, IDC_EDIT_PROBABILITY);
        hCheckSaveType = GetDlgItem(hDlg, IDC_CHECK_SAVE_TYPE);

        // Debug: Check if controls were found
        if (!hComboFormat) {
            MessageBox(hDlg, "ComboBox control not found!", "Error", MB_OK);
            return FALSE;
        }

        // Create fonts
        if (!hBoldFont) {
            LOGFONT lf;
            GetObject(GetStockObject(DEFAULT_GUI_FONT), sizeof(LOGFONT), &lf);
            lf.lfWeight = FW_BOLD;
            hBoldFont = CreateFontIndirect(&lf);
            hNormalFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        }

        // Populate combo box
        for (const auto& dsk : possible_fmts) {
            ComboBox_AddString(hComboFormat, dsk.fmt_name);
        }
        ComboBox_SetCurSel(hComboFormat, 0);

        // Set checkbox state
        if (!show_probab) {
            Button_SetCheck(hCheckSaveType, BST_CHECKED);
        }

        // Hide probability field if not needed
        if (!show_probab) {
            ShowWindow(hEditProbability, SW_HIDE);
            ShowWindow(GetDlgItem(hDlg, IDC_STATIC_PROBABILITY), SW_HIDE);
        }

        // Update info fields
        update_info_fields(0);

        return TRUE;
    }

    BOOL OnCommand(WORD wID, WORD wNotifyCode) {
        switch (wID) {
        case IDC_COMBO_FORMAT:
            if (wNotifyCode == CBN_SELCHANGE) {
                int idx = ComboBox_GetCurSel(hComboFormat);
                update_info_fields(idx);
            }
            break;

        case IDOK:
        {
            int idx = ComboBox_GetCurSel(hComboFormat);
            if (idx >= 0 && idx < static_cast<int>(possible_fmts.size())) {
                image_type = possible_fmts[idx].fmt_name;
                ui_retry = true;
                EndDialog(hDlg, IDOK);
            }
        }
        break;

        case IDCANCEL:
            ui_retry = false;
            EndDialog(hDlg, IDCANCEL);
            break;
        }
        return FALSE;
    }

    BOOL OnComboKeyDown(WPARAM wParam) {
        int current = ComboBox_GetCurSel(hComboFormat);
        int count = ComboBox_GetCount(hComboFormat);

        if (wParam == VK_UP && current > 0) {
            ComboBox_SetCurSel(hComboFormat, current - 1);
            update_info_fields(current - 1);
            return TRUE;
        }
        else if (wParam == VK_DOWN && current < count - 1) {
            ComboBox_SetCurSel(hComboFormat, current + 1);
            update_info_fields(current + 1);
            return TRUE;
        }

        return FALSE;
    }

public:

    static bool  TestResourceLoading() {
        HRSRC hResource = FindResource(g_GUI_dlg_hInstance, MAKEINTRESOURCE(IDD_FORMAT_SELECT), RT_DIALOG);

        if (!hResource) {
            DWORD error = GetLastError();
            char errorMsg[256];
            sprintf(errorMsg, "Dialog resource not found! Error: %lu", error);
            MessageBox(nullptr, errorMsg, "Error", MB_OK | MB_ICONERROR); // TODO: Check if dialogs are allowed
            return false;
        }
        return true; 
    }


    img_type_sel_GUI_t(const std::vector<cpm_disk_descr_t>& possible_fmts_in,
        const DSK_GEOMETRY& geom_in,
        bool show_probab_in) :
        possible_fmts(possible_fmts_in),
        img_geom(geom_in),
        show_probab(show_probab_in),
        hDlg(nullptr)
    {
        auto can_load_res = TestResourceLoading();

        HWND hParent = GetActiveWindow(); // TODO: check. For Options dialog -- use parent sent by TCmd

        // Show modal dialog
        INT_PTR result = DialogBoxParam(g_GUI_dlg_hInstance,  // GetModuleHandle(nullptr),
            MAKEINTRESOURCE(IDD_FORMAT_SELECT),
            hParent, // TODO: set to parent sent by TCmd 
            DlgProc,
            reinterpret_cast<LPARAM>(this));

        // Debug: Check if dialog creation failed
        if (result == -1) {
            DWORD error = GetLastError();
            char errorMsg[256];
            sprintf(errorMsg, "DialogBoxParam failed with error: %lu", error);
            MessageBox(nullptr, errorMsg, "Error", MB_OK | MB_ICONERROR); // TODO: Check if dialogs are allowed
            ui_retry = false; // Should already be false
        }

    }

    bool attempt_new_read() {
        return ui_retry;
    }

    bool save_disk_type() const {
        return Button_GetCheck(hCheckSaveType) == BST_CHECKED;
    }

    auto get_image_type() const {
        return image_type;
    }
};



#endif