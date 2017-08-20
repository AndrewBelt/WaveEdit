#include "noc_file_dialog.h"
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <commdlg.h>

const char *noc_file_dialog_open(int flags,
                                 const char *filters,
                                 const char *default_path,
                                 const char *default_name)
{
    OPENFILENAME ofn;       // common dialog box structure
    char szFile[260];       // buffer for file name
    int ret;

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = szFile;
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = filters;
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = default_name;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = default_path;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (flags & NOC_FILE_DIALOG_OPEN)
        ret = GetOpenFileName(&ofn);
    else
        ret = GetSaveFileName(&ofn);

    free(g_noc_file_dialog_ret);
    g_noc_file_dialog_ret = ret ? strdup(szFile) : NULL;
    return g_noc_file_dialog_ret;
}
