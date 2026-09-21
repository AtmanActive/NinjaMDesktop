// Minimal stand-in for MFC's afxres.h so ReaNINJAM's res.rc compiles without MFC.
#include <winresrc.h>
#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif
