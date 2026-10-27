// Mem Reduct
// Copyright (c) 2011-2026 Henry++

#pragma once

#include "routine.h"

#include "resource.h"
#include "app.h"

DEFINE_GUID (GUID_TrayIcon, 0xAE9053F0, 0x8D59, 0x4803, 0x9A, 0xBB, 0x74, 0xAF, 0xE6, 0x6B, 0x5F, 0xD2);

// the sdk only exposes DT_UNDERLINE when a minimum windows version is declared, this project declares none
#ifndef DT_UNDERLINE
#define DT_UNDERLINE 0x00010000
#endif


#define TIMER 1000
#define UID 1337

// position of the language submenu inside the flat main menu
#define LANG_MENU 11

#define TRAY_SUBMENU_1 4
#define TRAY_SUBMENU_2 5
#define TRAY_SUBMENU_3 6

// main window metrics (96 dpi, scaled at runtime)
#define UI_WIDTH 400
#define UI_HEIGHT 360
#define UI_PAD 16
#define UI_HEADER_TOP 12
#define UI_HEADER_HEIGHT 28
#define UI_OVERFLOW_SIZE 28
#define UI_CARD_TOP 52
#define UI_CARD_BOTTOM 266
#define UI_ACTIONS_TOP 280
#define UI_ACTIONS_HEIGHT 40
#define UI_SPLIT_WIDTH 44
#define UI_FOOTER_TOP 330
#define UI_FOOTER_HEIGHT 16
#define UI_CARD_RADIUS 8
#define UI_CONTROL_RADIUS 6
#define UI_BAR_HEIGHT 6
#define UI_ROW_HEIGHT 56

#define DEFAULT_AUTOREDUCT_VAL 90
#define DEFAULT_AUTOREDUCTINTERVAL_VAL 30

// minimum pause (in seconds) between usage-based automatic cleanups
#define AUTOREDUCT_COOLDOWN 30

// routine never trims the debug log, so truncate it once it grows past this size (bytes)
#define LOG_MAXIMUM_SIZE 0x100000

// the sdk builds the project url from the upstream owner, so it is declared here instead
#define APP_WEBSITE_URL L"https://github.com/ninokiru/Mem-Reduct"

// the sdk footer links the license text through its own repository; this fork links its copy of the license
#define APP_ABOUT_LICENSE L"This program is free software; you can redistribute it and/or modify it under the terms of " \
	L"the <a href=\"https://github.com/ninokiru/Mem-Reduct/blob/main/LICENSE\">GNU General Public License 3</a> " \
	L"as published by the Free Software Foundation."

// update channel (this fork)
#define UPDATE_MANIFEST_URL L"https://raw.githubusercontent.com/ninokiru/Mem-Reduct/main/VERSION"
#define UPDATE_RELEASE_URL_PREFIX L"https://github.com/ninokiru/Mem-Reduct/releases/download/"

// hours between automatic update checks
#define UPDATE_CHECK_PERIOD 6

#define UPDATE_VERSION_MAX_LENGTH 32

// setup.exe is ~2MB; reject anything smaller (error page / truncated download)
#define UPDATE_MINIMUM_FILE_SIZE 0x40000

// the installer is downloaded to disk before it is checked, so a hostile answer must not fill the drive
#define UPDATE_MAXIMUM_FILE_SIZE 0x2000000

// required third field of the manifest: sha256 of the installer, printed by the build workflow
#define UPDATE_HASH_ALGORITHM L"SHA256"
#define UPDATE_HASH_HEX_LENGTH 64

// the VERSION file is a few lines of text; a bigger answer is either broken or a denial of service attempt
#define UPDATE_MAXIMUM_MANIFEST_SIZE 0x8000

#define DEFAULT_DANGER_LEVEL 90
#define DEFAULT_WARNING_LEVEL 70

// colors
#define TRAY_COLOR_BLACK RGB(0x00, 0x00, 0x00)
#define TRAY_COLOR_WHITE RGB(0xFF, 0xFF, 0xFF)
#define TRAY_COLOR_TEXT RGB(0xFF, 0xFF, 0xFF)
#define TRAY_COLOR_BG RGB(0x00, 0x80, 0x40)
#define TRAY_COLOR_WARNING RGB(0xFF, 0x80, 0x40)
#define TRAY_COLOR_DANGER RGB(0xEC, 0x1C, 0x24)

// memory cleaning mask
#define REDUCT_WORKINGSET 0x01
#define REDUCT_SYSTEMFILECACHE 0x02
#define REDUCT_STANDBYPRIORITY0LIST 0x04
#define REDUCT_STANDBYLIST 0x08
#define REDUCT_MODIFIEDLIST 0x10
#define REDUCT_COMBINEMEMORYLISTS 0x20
#define REDUCT_REGISTRYCACHE 0x40
#define REDUCT_MODIFIEDFILECACHE 0x80

// memory cleaning values
#define REDUCT_MASK_ALL (REDUCT_WORKINGSET | \
REDUCT_SYSTEMFILECACHE | \
REDUCT_STANDBYPRIORITY0LIST | \
REDUCT_STANDBYLIST | \
REDUCT_MODIFIEDLIST | \
REDUCT_COMBINEMEMORYLISTS | \
REDUCT_REGISTRYCACHE | \
REDUCT_MODIFIEDFILECACHE)

#define REDUCT_MASK_DEFAULT (REDUCT_WORKINGSET | REDUCT_SYSTEMFILECACHE | REDUCT_STANDBYPRIORITY0LIST | REDUCT_REGISTRYCACHE | REDUCT_COMBINEMEMORYLISTS | REDUCT_MODIFIEDFILECACHE)
#define REDUCT_MASK_FREEZES (REDUCT_STANDBYLIST | REDUCT_MODIFIEDLIST)

typedef struct _APP_UPDATE_TASK
{
	HWND hwnd;
	BOOLEAN is_silent;
} APP_UPDATE_TASK, *PAPP_UPDATE_TASK;

typedef struct _STATIC_DATA
{
	HDC hdc;
	HDC hdc_mask;
	HBITMAP hbitmap;
	HBITMAP hbitmap_mask;
	HFONT hfont;
	HICON hicon;
	RECT icon_size;
	ULONG ms_prev;

	// last strings pushed to the tray tooltip, used to skip unchanged updates
	WCHAR tooltip_prev[0x80];

	// main window fonts, recreated on dpi change
	HFONT hfont_ui_hero;
	HFONT hfont_ui_title;
	HFONT hfont_ui_body;
	HFONT hfont_ui_bodybold;
	HFONT hfont_ui_small;
	HFONT hfont_ui_smallbold;

	// main window values, cached so the paint code does no formatting
	WCHAR ui_hero_percent[0x08];
	WCHAR ui_hero_used[0x20];
	WCHAR ui_hero_free[0x20];
	WCHAR ui_metric_percent[0x02][0x08];
	WCHAR ui_metric_meta[0x02][0x40];
	ULONG ui_percent[0x03];

	HWND hwnd_ui_hover;
	BOOLEAN ui_hasdata;
} STATIC_DATA, *PSTATIC_DATA;

typedef enum _CLEANUP_SOURCE_ENUM
{
	SOURCE_AUTO,
	SOURCE_MANUAL,
	SOURCE_HOTKEY,
	SOURCE_CMDLINE,
	SOURCE_MANUAL_REGION
} CLEANUP_SOURCE_ENUM, *PCLEANUP_SOURCE_ENUM;


