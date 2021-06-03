/*
Copyright (c) 2021, Eric Haines
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "stdafx.h"
#include <CommDlg.h>
#include <stdio.h>
#include <assert.h>
#include "Resource.h"
#include "Location.h"

Location::Location(void)
{
}


Location::~Location(void)
{
}


void getLocationData(int &x, int &z)
{
    x = gX;
    z = gZ;
}

void setLocationData(int x, int z)
{
    gX = x;
    gZ = z;
}

INT_PTR CALLBACK Location(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

int doLocation(HINSTANCE hInst, HWND hWnd)
{
    gLocOK = 0;
    DialogBox(hInst, MAKEINTRESOURCE(IDD_FOCUS_VIEW), hWnd, Location);
    // did we hit cancel?
    return gLocOK;
}

INT_PTR CALLBACK Location(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    char xString[EP_FIELD_LENGTH];
    char zString[EP_FIELD_LENGTH];

    switch (message)
    {
    case WM_INITDIALOG:
        // set them up
        sprintf_s(xString, EP_FIELD_LENGTH, "%d", gX);
        sprintf_s(zString, EP_FIELD_LENGTH, "%d", gZ);
        SetDlgItemTextA(hDlg, IDC_FOCUS_X, xString);
        SetDlgItemTextA(hDlg, IDC_FOCUS_Z, zString);
        return (INT_PTR)TRUE;

    case WM_COMMAND:

        switch (LOWORD(wParam))
        {
            case IDOK:
            {
                gLocOK = 1;

                int x, z;

                // suck all the data out
                GetDlgItemTextA(hDlg, IDC_FOCUS_X, xString, EP_FIELD_LENGTH);
                GetDlgItemTextA(hDlg, IDC_FOCUS_Z, zString, EP_FIELD_LENGTH);

                int nc;
                nc = sscanf_s(xString, "%d", &x);
                nc &= sscanf_s(zString, "%d", &z);
                // this is a bit lazy checking all errors here, there's probably a better way
                // to test as we go, but this sort of thing should be rare
                if (nc == 0)
                {
                    MessageBox(NULL,
                        _T("Bad (non-numeric) value detected in dialog;\nYou need to clean up, then hit OK again."), _T("Non-numeric value error"), MB_OK | MB_ICONERROR);
                    return (INT_PTR)FALSE;
                }

                // survived
                gX = x;
                gZ = z;
            } // yes, we do want to fall through here
            case IDCANCEL:
                EndDialog(hDlg, LOWORD(wParam));
                return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

