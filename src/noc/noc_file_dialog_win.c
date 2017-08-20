#include "noc_file_dialog.h"
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <commdlg.h>


static char *g_noc_file_dialog_ret = NULL;

const char *noc_file_dialog_open(int flags,
                                 const char *filters,
                                 const char *default_path,
                                 const char *default_name)
{
    OPENFILENAME ofn;       // common dialog box structure
    char strFile[260];
    int ret;
    char strInitialDir[260];
    strncpy(strInitialDir, default_path, sizeof(strInitialDir));

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = strFile;
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = sizeof(strFile);
    ofn.lpstrFilter = filters;
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = strInitialDir;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (flags & NOC_FILE_DIALOG_OPEN)
        ret = GetOpenFileName(&ofn);
    else
        ret = GetSaveFileName(&ofn);

    free(g_noc_file_dialog_ret);
    g_noc_file_dialog_ret = ret ? strdup(strFile) : NULL;
    return g_noc_file_dialog_ret;
}
