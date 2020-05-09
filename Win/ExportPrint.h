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
