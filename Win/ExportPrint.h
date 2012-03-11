#pragma once

static int gOK;

static     ExportFileData epd;

class ExportPrint
{
public:
    ExportPrint(void);
    ~ExportPrint(void);
};

void getExportPrintData(ExportFileData *pEpd);
void setExportPrintData(ExportFileData *pEpd);
int doExportPrint(HINSTANCE hInst,HWND hWnd);
