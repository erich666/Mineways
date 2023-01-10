#pragma once

static int gOK;

static	ExportFileData epd;
// this one has to be static to make the compiler happy
static	ExportFileData origEpd;


class ExportPrint
{
public:
    ExportPrint(void);
    ~ExportPrint(void);
};

void getExportPrintData(ExportFileData* pEpd);
void setExportPrintData(ExportFileData* pEpd);
int doExportPrint(HINSTANCE hInst, HWND hWnd);
HWND CreateToolTip(int toolID, HINSTANCE hInst, HWND hDlg, PTSTR pszText);

// we assume we need no more than 2 different messages (typically OBJ vs. USD) for any tool tip
typedef struct TooltipDefinition {
    const int id;
    const int fileTypeMsg[FILE_TYPE_TOTAL];
    const PTSTR name1;
    const PTSTR name2;
} TooltipDefinition;
