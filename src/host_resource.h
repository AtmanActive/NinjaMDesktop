// Resource IDs used by the NinjaMDesktop host (kept clear of ReaNINJAM's resource.h ranges)

#ifndef NJD_HOST_RESOURCE_H
#define NJD_HOST_RESOURCE_H

#define IDD_NJD_AUDIOCFG     300

#define IDC_NJD_BACKEND      3001
#define IDC_NJD_INDEV        3002
#define IDC_NJD_OUTDEV       3003
#define IDC_NJD_SRATE        3004
#define IDC_NJD_BSIZE        3005
#define IDC_NJD_STATUS       3006
#define IDC_NJD_APPLY        3007

#define IDM_NJD_QUIT         41001
#define IDM_NJD_ABOUT        41002
#define IDM_NJD_THEME_SYSTEM 41010 // + THEME_* mode
#define IDM_NJD_THEME_LIGHT  41011
#define IDM_NJD_THEME_DARK   41012
#define IDM_NJD_ZOOM_FIRST   41020 // + index into the zoom list in host_main.cpp
#define IDM_NJD_ZOOM_LAST    41039

#define IDI_NJD_APPICON      400

#endif
