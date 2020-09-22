/*
Copyright (c) 2011, Eric Haines
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

#pragma once

#include "stdafx.h"
#include <string>
#include <sstream>
#include <Windowsx.h>
#include "Resource.h"
#include "ExportPrint.h"
#include "sketchfabUploader.h"

static PublishSkfbData skfbPbdata;
static std::pair<bool, std::string> lastResponse;
static HWND uploadWindow;
static int goPublish;

void deleteFile();
void getPublishSkfbData(PublishSkfbData *pEpd);
void setPublishSkfbData(PublishSkfbData *pEpd);
int uploadToSketchfab(HINSTANCE hInst,HWND hWnd);
int doPublishSkfb(HINSTANCE hInst,HWND hWnd);
INT_PTR CALLBACK managePublishWindow(HWND hDlg,UINT message,WPARAM wParam,LPARAM lParam);
INT_PTR CALLBACK manageUploadWindow(HWND hDlg,UINT message,WPARAM wParam,LPARAM lParam);
HANDLE uploadThreadHandle = NULL;
SketchfabV2Uploader* uploader;

LPCWSTR stringToLpwstr(std::string input)
{
    wchar_t* wideString = new wchar_t[4096];
    MultiByteToWideChar(CP_ACP, 0, input.c_str(), (int)(input.size() + 1), wideString, 4096);
    return wideString;
}

void deleteFile()
{
	DeleteFile(stringToLpwstr(skfbPbdata.skfbFilePath));
}

void getPublishSkfbData(PublishSkfbData *pSkfbpd)
{
    *pSkfbpd = skfbPbdata;
}

void setPublishSkfbData(PublishSkfbData *pSkfbpd)
{
    skfbPbdata = *pSkfbpd;
}

int uploadToSketchfab(HINSTANCE hInst,HWND hWnd)
{
    // Success or failure are handled in the callback function
    DialogBox(hInst,MAKEINTRESOURCE(IDD_UPLOAD_SKFB),hWnd, manageUploadWindow);
    return 0;
}


int doPublishSkfb(HINSTANCE hInst,HWND hWnd)
{
    goPublish = 0;
    DialogBox(hInst,MAKEINTRESOURCE(IDD_PUBLISH_SKFB),hWnd, managePublishWindow);
    // did we hit cancel?
    return goPublish;
}

DWORD WINAPI thread_func(LPVOID lpParameter)
{
    UNREFERENCED_PARAMETER(lpParameter);
    uploader = new SketchfabV2Uploader();
    lastResponse = uploader->upload(uploadWindow, skfbPbdata.skfbApiToken, skfbPbdata.skfbFilePath, skfbPbdata.skfbName, skfbPbdata.skfbDescription, skfbPbdata.skfbTags, skfbPbdata.skfbDraft, skfbPbdata.skfbPrivate, skfbPbdata.skfbPassword);
    SendMessage(uploadWindow, SIGNAL_UPLOAD_FINISHED, 100, 0);
    return 0;
}

INT_PTR CALLBACK manageUploadWindow(HWND hDlg,UINT message,WPARAM wParam,LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
        {
            uploadWindow = hDlg;
            uploadThreadHandle = CreateThread(NULL, 0, thread_func, NULL, 0, 0);
            break;
        }
    case SIGNAL_UPLOAD_FINISHED:
    {
        if(lastResponse.first){ // Upload succeeded
            int retcode= MessageBox(NULL,
                _T("Your model has been uploaded. Click OK to view it on Sketchfab"),
                _T("Upload successful"),
                MB_OKCANCEL | MB_ICONINFORMATION | MB_TOPMOST);

            if(retcode == IDOK)
            {
                std::string modelUrl = lastResponse.second;
                wchar_t* wcharModelUrl = new wchar_t[4096];
                MultiByteToWideChar(CP_ACP, 0, modelUrl.c_str(), (int)(modelUrl.size() + 1), wcharModelUrl, 4096);
                ShellExecute(NULL, L"open", wcharModelUrl, NULL, NULL, SW_SHOWNORMAL);
            }
        }
        else { // Upload failed
            std::vector<wchar_t> errorMessage(MultiByteToWideChar(CP_ACP, 0, lastResponse.second.c_str(), (int)(lastResponse.second.size() + 1), 0, 0));
            MultiByteToWideChar(CP_ACP, 0, lastResponse.second.c_str(), (int)(lastResponse.second.size() + 1), &errorMessage[0], (int)(errorMessage.size()));
            std::wstring errorMessageStr(&errorMessage[0]);
            MessageBox(NULL,
                errorMessageStr.c_str(),
                L"Upload failed",
                MB_OKCANCEL | MB_ICONERROR | MB_TOPMOST);

            delete uploader;
        }

        EndDialog(hDlg, (INT_PTR)TRUE);
        return (INT_PTR)TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
            case IDC_SKFB_UPLOAD_CANCEL:
                // Ask curl to stop upload
                uploader->abort();
            }
            break;
    }
    return (INT_PTR)FALSE;
}


INT_PTR CALLBACK managePublishWindow(HWND hDlg,UINT message,WPARAM wParam,LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        {
            // to be decoded from UTF-8
            wchar_t tempName[SKFB_NAME_LIMIT + 1];
            wchar_t tempPassword[SKFB_PASSWORD_LIMIT + 1];

            // UTF-8 decoding
            MultiByteToWideChar(CP_UTF8, NULL, skfbPbdata.skfbName, -1, tempName, SKFB_NAME_LIMIT + 1);
            MultiByteToWideChar(CP_UTF8, NULL, skfbPbdata.skfbPassword, -1, tempPassword, SKFB_PASSWORD_LIMIT + 1);

            SetDlgItemTextA(hDlg,IDC_API_TOKEN,skfbPbdata.skfbApiToken);
            SetDlgItemTextW(hDlg,IDC_SKFB_NAME,tempName);
            SetDlgItemTextW(hDlg,IDC_SKFB_PASSWORDFIELD,tempPassword);

            CheckDlgButton(hDlg,IDC_SKFB_DRAFT,(skfbPbdata.skfbDraft == false));
            CheckDlgButton(hDlg,IDC_SKFB_PRIVATE,skfbPbdata.skfbPrivate);
            Edit_LimitText(GetDlgItem(hDlg, IDC_SKFB_NAME), SKFB_NAME_LIMIT);
            Edit_LimitText(GetDlgItem(hDlg, IDC_API_TOKEN), SKFB_TOKEN_LIMIT);
            Edit_LimitText(GetDlgItem(hDlg, IDC_SKFB_DESC), SKFB_DESC_LIMIT);
            Edit_LimitText(GetDlgItem(hDlg, IDC_SKFB_PASSWORDFIELD), SKFB_PASSWORD_LIMIT);
            EnableWindow(GetDlgItem(hDlg, IDC_SKFB_PASSWORDFIELD), IsDlgButtonChecked(hDlg, IDC_SKFB_PRIVATE));
        }
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
            case IDC_SKFB_CLAIM_TOKEN:
            {
                ShellExecute(NULL, L"open", L"http://sketchfab.com/settings/password",
                    NULL, NULL, SW_SHOWNORMAL);
                break;
            }
            case IDC_SKFB_PRIVATE:
            {
                EnableWindow(GetDlgItem(hDlg, IDC_SKFB_PASSWORDFIELD), IsDlgButtonChecked(hDlg, IDC_SKFB_PRIVATE));
                break;
            }
            case ID_SKFB_PUBLISH:
            {
                PublishSkfbData lepd = skfbPbdata;
                // We should not have non UTF8 characters here
                GetDlgItemTextA(hDlg, IDC_API_TOKEN, lepd.skfbApiToken, SKFB_TOKEN_LIMIT + 1);

                // Temporary wide char to be converted to UTF8
                wchar_t tempName[SKFB_NAME_LIMIT + 1];
                wchar_t tempDescription[SKFB_DESC_LIMIT + 1];
                wchar_t tempTags[SKFB_TAG_LIMIT + 1];

                // Get text from controls
                GetDlgItemTextW(hDlg, IDC_SKFB_NAME, tempName, SKFB_NAME_LIMIT + 1);
                GetDlgItemTextW(hDlg, IDC_SKFB_DESC, tempDescription, SKFB_DESC_LIMIT + 1);
                GetDlgItemTextW(hDlg, IDC_SKFB_TAG, tempTags, SKFB_TAG_LIMIT + 1);

                // UTF8 conversion
                WideCharToMultiByte(CP_UTF8, NULL, tempName, -1, lepd.skfbName, SKFB_NAME_LIMIT + 1, NULL, NULL);
                WideCharToMultiByte(CP_UTF8, NULL, tempDescription, -1, lepd.skfbDescription, SKFB_DESC_LIMIT + 1, NULL, NULL);
                WideCharToMultiByte(CP_UTF8, NULL, tempTags, -1, lepd.skfbTags, SKFB_TAG_LIMIT + 1, NULL, NULL);

                if(std::string(lepd.skfbApiToken).size() != 32)
                {
                    MessageBox(NULL,
                        _T("Please check your API token"), _T("Invalid API token"), MB_OK|MB_ICONERROR);
                    return (INT_PTR)FALSE;
                }
                lepd.skfbDraft = (IsDlgButtonChecked(hDlg, IDC_SKFB_DRAFT) == 0);
                lepd.skfbPrivate = (IsDlgButtonChecked(hDlg, IDC_SKFB_PRIVATE) == 1);

                if (lepd.skfbPrivate){
                    wchar_t tempPassword[SKFB_PASSWORD_LIMIT + 1];
                    GetDlgItemTextW(hDlg, IDC_SKFB_PASSWORDFIELD, tempPassword, SKFB_PASSWORD_LIMIT + 1);
                    WideCharToMultiByte(CP_UTF8, NULL, tempPassword, -1, skfbPbdata.skfbPassword, SKFB_PASSWORD_LIMIT + 1, NULL, NULL);
                }
                goPublish = 1;

                // Set it in the main skfbdata object
                skfbPbdata = lepd;
            }
            case IDCANCEL:
                EndDialog(hDlg, LOWORD(wParam));
                return (INT_PTR)TRUE;
            }
            break;
    }
    return (INT_PTR)FALSE;
}
