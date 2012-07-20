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

#include "stdafx.h"
#include <CommDlg.h>
#include <stdio.h>
#include <assert.h>
#include "Resource.h"
//#include "../MinewaysMap/blockInfo.h"
#include "ExportPrint.h"


static int prevPhysMaterial;
static int curPhysMaterial;

ExportPrint::ExportPrint(void)
{
}


ExportPrint::~ExportPrint(void)
{
}


void getExportPrintData(ExportFileData *pEpd)
{
    *pEpd = epd;
}

void setExportPrintData(ExportFileData *pEpd)
{
    epd = *pEpd;
}

INT_PTR CALLBACK ExportPrint(HWND hDlg,UINT message,WPARAM wParam,LPARAM lParam);

int doExportPrint(HINSTANCE hInst,HWND hWnd)
{
    gOK = 0;
    DialogBox(hInst,MAKEINTRESOURCE(IDD_EXPT_VIEW),hWnd,ExportPrint);
    // did we hit cancel?
    return gOK;
}

INT_PTR CALLBACK ExportPrint(HWND hDlg,UINT message,WPARAM wParam,LPARAM lParam)
{
    char changeString[EP_FIELD_LENGTH];
    //char oldString[EP_FIELD_LENGTH];
    //char currentString[EP_FIELD_LENGTH];
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
        {
            // set them up
            sprintf_s(epd.minxString,EP_FIELD_LENGTH,"%d",epd.minxVal);
            sprintf_s(epd.minyString,EP_FIELD_LENGTH,"%d",epd.minyVal);
            sprintf_s(epd.minzString,EP_FIELD_LENGTH,"%d",epd.minzVal);
            sprintf_s(epd.maxxString,EP_FIELD_LENGTH,"%d",epd.maxxVal);
            sprintf_s(epd.maxyString,EP_FIELD_LENGTH,"%d",epd.maxyVal);
            sprintf_s(epd.maxzString,EP_FIELD_LENGTH,"%d",epd.maxzVal);

            sprintf_s(epd.modelHeightString,EP_FIELD_LENGTH,"%g",epd.modelHeightVal);
            sprintf_s(epd.blockSizeString,EP_FIELD_LENGTH,"%g",epd.blockSizeVal[epd.fileType]);
            sprintf_s(epd.costString,EP_FIELD_LENGTH,"%0.2f",epd.costVal);

            sprintf_s(epd.floaterCountString,EP_FIELD_LENGTH,"%d",epd.floaterCountVal);
            sprintf_s(epd.hollowThicknessString,EP_FIELD_LENGTH,"%g",epd.hollowThicknessVal[epd.fileType]);

            SetDlgItemTextA(hDlg,IDC_WORLD_MIN_X,epd.minxString);
            SetDlgItemTextA(hDlg,IDC_WORLD_MIN_Y,epd.minyString);
            SetDlgItemTextA(hDlg,IDC_WORLD_MIN_Z,epd.minzString);
            SetDlgItemTextA(hDlg,IDC_WORLD_MAX_X,epd.maxxString);
            SetDlgItemTextA(hDlg,IDC_WORLD_MAX_Y,epd.maxyString);
            SetDlgItemTextA(hDlg,IDC_WORLD_MAX_Z,epd.maxzString);

			CheckDlgButton(hDlg,IDC_CREATE_ZIP,epd.chkCreateZip[epd.fileType]);
			CheckDlgButton(hDlg,IDC_CREATE_FILES,epd.chkCreateModelFiles[epd.fileType]);

            CheckDlgButton(hDlg,IDC_RADIO_EXPORT_NO_MATERIALS,epd.radioExportNoMaterials[epd.fileType]);
            CheckDlgButton(hDlg,IDC_RADIO_EXPORT_MTL_COLORS_ONLY,epd.radioExportMtlColors[epd.fileType]);
            CheckDlgButton(hDlg,IDC_RADIO_EXPORT_SOLID_TEXTURES,epd.radioExportSolidTexture[epd.fileType]);
            CheckDlgButton(hDlg,IDC_RADIO_EXPORT_FULL_TEXTURES,epd.radioExportFullTexture[epd.fileType]);

            CheckDlgButton(hDlg,IDC_MERGE_FLATTOP,epd.chkMergeFlattop);
            CheckDlgButton(hDlg,IDC_EXPORT_ALL,epd.chkExportAll);
            CheckDlgButton(hDlg,IDC_MAKE_Z_UP,epd.chkMakeZUp[epd.fileType]);
			CheckDlgButton(hDlg,IDC_CENTER_MODEL,epd.chkCenterModel);
			CheckDlgButton(hDlg,IDC_INDIVIDUAL_BLOCKS,epd.chkIndividualBlocks);

            CheckDlgButton(hDlg,IDC_RADIO_ROTATE_0,epd.radioRotate0);
            CheckDlgButton(hDlg,IDC_RADIO_ROTATE_90,epd.radioRotate90);
            CheckDlgButton(hDlg,IDC_RADIO_ROTATE_180,epd.radioRotate180);
            CheckDlgButton(hDlg,IDC_RADIO_ROTATE_270,epd.radioRotate270);

            CheckDlgButton(hDlg,IDC_RADIO_SCALE_TO_HEIGHT,epd.radioScaleToHeight);
            CheckDlgButton(hDlg,IDC_RADIO_SCALE_TO_MATERIAL,epd.radioScaleToMaterial);
            CheckDlgButton(hDlg,IDC_RADIO_SCALE_BY_BLOCK,epd.radioScaleByBlock);
            CheckDlgButton(hDlg,IDC_RADIO_SCALE_BY_COST,epd.radioScaleByCost);

            SetDlgItemTextA(hDlg,IDC_MODEL_HEIGHT,epd.modelHeightString);
            SetDlgItemTextA(hDlg,IDC_BLOCK_SIZE,epd.blockSizeString);
            SetDlgItemTextA(hDlg,IDC_COST,epd.costString);

            CheckDlgButton(hDlg,IDC_FILL_BUBBLES,epd.chkFillBubbles);
            CheckDlgButton(hDlg,IDC_SEAL_ENTRANCES,epd.chkFillBubbles?epd.chkSealEntrances:BST_INDETERMINATE);
            CheckDlgButton(hDlg,IDC_SEAL_SIDE_TUNNELS,epd.chkFillBubbles?epd.chkSealSideTunnels:BST_INDETERMINATE);

            CheckDlgButton(hDlg,IDC_CONNECT_PARTS,epd.chkConnectParts);
            CheckDlgButton(hDlg,IDC_CONNECT_CORNER_TIPS,epd.chkConnectParts?epd.chkConnectCornerTips:BST_INDETERMINATE);
            CheckDlgButton(hDlg,IDC_CONNECT_ALL_EDGES,epd.chkConnectParts?epd.chkConnectAllEdges:BST_INDETERMINATE);
            CheckDlgButton(hDlg,IDC_DELETE_FLOATERS,epd.chkDeleteFloaters);
            CheckDlgButton(hDlg,IDC_HOLLOW,epd.chkHollow);
            CheckDlgButton(hDlg,IDC_SUPER_HOLLOW,epd.chkHollow?epd.chkSuperHollow:BST_INDETERMINATE);
            CheckDlgButton(hDlg,IDC_MELT_SNOW,epd.chkMeltSnow);

            SetDlgItemTextA(hDlg,IDC_FLOAT_COUNT,epd.floaterCountString);
            SetDlgItemTextA(hDlg,IDC_HOLLOW_THICKNESS,epd.hollowThicknessString);

            BOOL debugAvailable = !epd.radioExportNoMaterials[epd.fileType] && (epd.fileType != FILE_TYPE_ASCII_STL);

            CheckDlgButton(hDlg,IDC_SHOW_PARTS,debugAvailable?epd.chkShowParts:BST_INDETERMINATE);
            CheckDlgButton(hDlg,IDC_SHOW_WELDS,debugAvailable?epd.chkShowWelds:BST_INDETERMINATE);

            // When handling INITDIALOG message, send the combo box a message:
            for ( int i = 0; i < MTL_COST_TABLE_SIZE; i++ )
                SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_ADDSTRING, 0, (LPARAM)mtlCostTable[i].wname); 

            SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, epd.comboPhysicalMaterial[epd.fileType], 0);
            prevPhysMaterial = curPhysMaterial = epd.comboPhysicalMaterial[epd.fileType];

            for ( int i = 0; i < MODELS_UNITS_TABLE_SIZE; i++ )
                SendDlgItemMessage(hDlg, IDC_COMBO_MODELS_UNITS, CB_ADDSTRING, 0, (LPARAM)unitTypeTable[i].wname); 

            SendDlgItemMessage(hDlg, IDC_COMBO_MODELS_UNITS, CB_SETCURSEL, epd.comboModelUnits[epd.fileType], 0);
        }
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_FILL_BUBBLES:
            {
                UINT isFillBubblesChecked = IsDlgButtonChecked(hDlg,IDC_FILL_BUBBLES);
                CheckDlgButton(hDlg,IDC_SEAL_ENTRANCES,isFillBubblesChecked?epd.chkSealEntrances:BST_INDETERMINATE);
                CheckDlgButton(hDlg,IDC_SEAL_SIDE_TUNNELS,isFillBubblesChecked?epd.chkSealEntrances:BST_INDETERMINATE);
            }
            break;
            // all this crazy code says is "if Fill Bubbles is not checked, keep the seal entrances checkbox indeterminate;
            // if it *is* checked, then don't allow seal entrances to become indeterminate."
        case IDC_SEAL_ENTRANCES:
            {
                UINT isChecked = IsDlgButtonChecked(hDlg,IDC_FILL_BUBBLES);
                if ( !isChecked )
                {
                    CheckDlgButton(hDlg,IDC_SEAL_ENTRANCES,BST_INDETERMINATE);
                }
                else
                {
                    UINT isIndeterminate = ( IsDlgButtonChecked(hDlg,IDC_SEAL_ENTRANCES) == BST_INDETERMINATE );
                    if ( isIndeterminate )
                        CheckDlgButton(hDlg,IDC_SEAL_ENTRANCES,BST_UNCHECKED);
                }
            }
            break;
        case IDC_SEAL_SIDE_TUNNELS:
            {
                UINT isChecked = IsDlgButtonChecked(hDlg,IDC_FILL_BUBBLES);
                if ( !isChecked )
                {
                    CheckDlgButton(hDlg,IDC_SEAL_SIDE_TUNNELS,BST_INDETERMINATE);
                }
                else
                {
                    UINT isIndeterminate = ( IsDlgButtonChecked(hDlg,IDC_SEAL_SIDE_TUNNELS) == BST_INDETERMINATE );
                    if ( isIndeterminate )
                        CheckDlgButton(hDlg,IDC_SEAL_SIDE_TUNNELS,BST_UNCHECKED);
                }
            }
            break;

        case IDC_CONNECT_PARTS:
            {
                UINT isConnectPartsChecked = IsDlgButtonChecked(hDlg,IDC_CONNECT_PARTS);
                // if connect parts turned on, then default setting for others are turned on
                CheckDlgButton(hDlg,IDC_CONNECT_CORNER_TIPS,isConnectPartsChecked?epd.chkConnectCornerTips:BST_INDETERMINATE);
                CheckDlgButton(hDlg,IDC_CONNECT_ALL_EDGES,isConnectPartsChecked?epd.chkConnectAllEdges:BST_INDETERMINATE);
            }
        case IDC_CONNECT_CORNER_TIPS:
            {
                UINT isChecked = IsDlgButtonChecked(hDlg,IDC_CONNECT_PARTS);
                if ( !isChecked )
                {
                    CheckDlgButton(hDlg,IDC_CONNECT_CORNER_TIPS,BST_INDETERMINATE);
                }
                else
                {
                    UINT isIndeterminate = ( IsDlgButtonChecked(hDlg,IDC_CONNECT_CORNER_TIPS) == BST_INDETERMINATE );
                    if ( isIndeterminate )
                        CheckDlgButton(hDlg,IDC_CONNECT_CORNER_TIPS,BST_UNCHECKED);
                }
            }
            break;
        case IDC_CONNECT_ALL_EDGES:
            {
                UINT isChecked = IsDlgButtonChecked(hDlg,IDC_CONNECT_PARTS);
                if ( !isChecked )
                {
                    CheckDlgButton(hDlg,IDC_CONNECT_ALL_EDGES,BST_INDETERMINATE);
                }
                else
                {
                    UINT isIndeterminate = ( IsDlgButtonChecked(hDlg,IDC_CONNECT_ALL_EDGES) == BST_INDETERMINATE );
                    if ( isIndeterminate )
                        CheckDlgButton(hDlg,IDC_CONNECT_ALL_EDGES,BST_UNCHECKED);
                }
            }
            break;

        case IDC_HOLLOW:
            {
                UINT isHollowChecked = IsDlgButtonChecked(hDlg,IDC_HOLLOW);
                // if hollow turned on, then default setting for superhollow is on
                CheckDlgButton(hDlg,IDC_SUPER_HOLLOW,isHollowChecked?epd.chkSuperHollow:BST_INDETERMINATE);
            }
            break;
        case IDC_SUPER_HOLLOW:
            {
                UINT isHollowChecked = IsDlgButtonChecked(hDlg,IDC_HOLLOW);
                if ( !isHollowChecked )
                {
                    CheckDlgButton(hDlg,IDC_SUPER_HOLLOW,BST_INDETERMINATE);
                }
                else
                {
                    UINT isSuperHollowIndeterminate = ( IsDlgButtonChecked(hDlg,IDC_SUPER_HOLLOW) == BST_INDETERMINATE );
                    if ( isSuperHollowIndeterminate )
                        CheckDlgButton(hDlg,IDC_SUPER_HOLLOW,BST_UNCHECKED);
                }
            }
            break;

        case IDC_RADIO_EXPORT_NO_MATERIALS:
            // set the combo box material to white (might already be that, which is fine)
            SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_WHITE_STRONG_FLEXIBLE, 0);
            // kinda sleazy: if we go to anything but full textures, turn off exporting all objects
            CheckDlgButton(hDlg,IDC_EXPORT_ALL, BST_UNCHECKED);
            goto ChangeMaterial;

        case IDC_RADIO_EXPORT_MTL_COLORS_ONLY:
            // set the combo box material to color (might already be that, which is fine)
            SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, 0);
            // kinda sleazy: if we go to anything but full textures, turn off exporting all objects
            CheckDlgButton(hDlg,IDC_EXPORT_ALL, BST_UNCHECKED);
            goto ChangeMaterial;

        case IDC_RADIO_EXPORT_SOLID_TEXTURES:
            // set the combo box material to color (might already be that, which is fine)
            SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, 0);
            // kinda sleazy: if we go to anything but full textures, turn off exporting all objects
            CheckDlgButton(hDlg,IDC_EXPORT_ALL, BST_UNCHECKED);
            goto ChangeMaterial;

        case IDC_RADIO_EXPORT_FULL_TEXTURES:
            // set the combo box material to color (might already be that, which is fine)
            SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, 0);
            goto ChangeMaterial;

        case IDC_COMBO_PHYSICAL_MATERIAL:
            ChangeMaterial:
            {
                // combo box selection will change the thickness, if previous value is set to the default
                curPhysMaterial = (int)SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_GETCURSEL, 0, 0);
                if ( prevPhysMaterial != curPhysMaterial )
                {
                    //sprintf_s(oldString,EP_FIELD_LENGTH,"%g",METERS_TO_MM * mtlCostTable[prevPhysMaterial].minWall);
                    sprintf_s(changeString,EP_FIELD_LENGTH,"%g",METERS_TO_MM * mtlCostTable[curPhysMaterial].minWall);

                    // this old code cleverly changed the value only if the user hadn't set it to something else. This
                    // is a little too clever: if the user set the value, then there was no way he could find out what
                    // a material's minimum thickness had to be when he chose the material - he'd have to restart the
                    // program. Better to force the user to set block size again if he changes the material type.
                    //GetDlgItemTextA(hDlg,IDC_BLOCK_SIZE,currentString,EP_FIELD_LENGTH);
                    //if ( strcmp(oldString,currentString) == 0)
                    SetDlgItemTextA(hDlg,IDC_BLOCK_SIZE,changeString);

                    //GetDlgItemTextA(hDlg,IDC_HOLLOW_THICKNESS,currentString,EP_FIELD_LENGTH);
                    //if ( strcmp(oldString,currentString) == 0)
                    SetDlgItemTextA(hDlg,IDC_HOLLOW_THICKNESS,changeString);

                    prevPhysMaterial = curPhysMaterial;
                }

                // if material output turned off, don't allow debug options
                BOOL debugAvailable = !IsDlgButtonChecked(hDlg,IDC_RADIO_EXPORT_NO_MATERIALS) 
                    && (epd.fileType != FILE_TYPE_ASCII_STL);
                if ( debugAvailable )
                {
                    // wipe out any indeterminates
                    if ( IsDlgButtonChecked(hDlg,IDC_SHOW_PARTS) == BST_INDETERMINATE )
                    {
                        // back to state at start
                        CheckDlgButton(hDlg,IDC_SHOW_PARTS,epd.chkShowParts);
                        CheckDlgButton(hDlg,IDC_SHOW_WELDS,epd.chkShowParts);
                    }
                }
                else
                {
                    // shut them down
                    CheckDlgButton(hDlg,IDC_SHOW_PARTS,BST_INDETERMINATE);
                    CheckDlgButton(hDlg,IDC_SHOW_WELDS,BST_INDETERMINATE);
                }
            }
            break;
        case IDC_SHOW_PARTS:
            {
                UINT isInactive = IsDlgButtonChecked(hDlg,IDC_RADIO_EXPORT_NO_MATERIALS) 
                    || (epd.fileType == FILE_TYPE_ASCII_STL);
                if ( isInactive )
                {
                    CheckDlgButton(hDlg,IDC_SHOW_PARTS,BST_INDETERMINATE);
                }
                else
                {
                    UINT isIndeterminate = ( IsDlgButtonChecked(hDlg,IDC_SHOW_PARTS) == BST_INDETERMINATE );
                    if ( isIndeterminate )
                        CheckDlgButton(hDlg,IDC_SHOW_PARTS,BST_UNCHECKED);
                }
            }
            break;
		case IDC_SHOW_WELDS:
			{
				UINT isInactive = IsDlgButtonChecked(hDlg,IDC_RADIO_EXPORT_NO_MATERIALS) 
					|| (epd.fileType == FILE_TYPE_ASCII_STL);
				if ( isInactive )
				{
					CheckDlgButton(hDlg,IDC_SHOW_WELDS,BST_INDETERMINATE);
				}
				else
				{
					UINT isIndeterminate = ( IsDlgButtonChecked(hDlg,IDC_SHOW_WELDS) == BST_INDETERMINATE );
					if ( isIndeterminate )
						CheckDlgButton(hDlg,IDC_SHOW_WELDS,BST_UNCHECKED);
				}
			}
			break;

		case IDC_CREATE_ZIP:
			{
				// if zip off, model file export must be set to on
				if ( !IsDlgButtonChecked(hDlg,IDC_CREATE_ZIP) )
				{
					CheckDlgButton(hDlg,IDC_CREATE_FILES,BST_CHECKED);
				}
			}
			break;

		case IDC_CREATE_FILES:
			{
				// if model off, model file export must be set to on
				if ( !IsDlgButtonChecked(hDlg,IDC_CREATE_FILES) )
				{
					CheckDlgButton(hDlg,IDC_CREATE_ZIP,BST_CHECKED);
				}
			}
			break;


        case IDOK:
            {
                gOK = 1;
                ExportFileData lepd;
                lepd = epd;

                // suck all the data out to a local copy
                GetDlgItemTextA(hDlg,IDC_WORLD_MIN_X,lepd.minxString,EP_FIELD_LENGTH);
                GetDlgItemTextA(hDlg,IDC_WORLD_MIN_Y,lepd.minyString,EP_FIELD_LENGTH);
                GetDlgItemTextA(hDlg,IDC_WORLD_MIN_Z,lepd.minzString,EP_FIELD_LENGTH);
                GetDlgItemTextA(hDlg,IDC_WORLD_MAX_X,lepd.maxxString,EP_FIELD_LENGTH);
                GetDlgItemTextA(hDlg,IDC_WORLD_MAX_Y,lepd.maxyString,EP_FIELD_LENGTH);
                GetDlgItemTextA(hDlg,IDC_WORLD_MAX_Z,lepd.maxzString,EP_FIELD_LENGTH);

				lepd.chkCreateZip[lepd.fileType] = IsDlgButtonChecked(hDlg,IDC_CREATE_ZIP);
				lepd.chkCreateModelFiles[lepd.fileType] = IsDlgButtonChecked(hDlg,IDC_CREATE_FILES);

                lepd.radioExportNoMaterials[lepd.fileType] = IsDlgButtonChecked(hDlg,IDC_RADIO_EXPORT_NO_MATERIALS);
                lepd.radioExportMtlColors[lepd.fileType] = IsDlgButtonChecked(hDlg,IDC_RADIO_EXPORT_MTL_COLORS_ONLY);
                lepd.radioExportSolidTexture[lepd.fileType] = IsDlgButtonChecked(hDlg,IDC_RADIO_EXPORT_SOLID_TEXTURES);
                lepd.radioExportFullTexture[lepd.fileType] = IsDlgButtonChecked(hDlg,IDC_RADIO_EXPORT_FULL_TEXTURES);

                lepd.chkMergeFlattop = IsDlgButtonChecked(hDlg,IDC_MERGE_FLATTOP);
                lepd.chkExportAll = IsDlgButtonChecked(hDlg,IDC_EXPORT_ALL);
                lepd.chkMakeZUp[lepd.fileType] = IsDlgButtonChecked(hDlg,IDC_MAKE_Z_UP);
				lepd.chkCenterModel = IsDlgButtonChecked(hDlg,IDC_CENTER_MODEL);
				lepd.chkIndividualBlocks = IsDlgButtonChecked(hDlg,IDC_INDIVIDUAL_BLOCKS);

                lepd.radioRotate0 = IsDlgButtonChecked(hDlg,IDC_RADIO_ROTATE_0);
                lepd.radioRotate90 = IsDlgButtonChecked(hDlg,IDC_RADIO_ROTATE_90);
                lepd.radioRotate180 = IsDlgButtonChecked(hDlg,IDC_RADIO_ROTATE_180);
                lepd.radioRotate270 = IsDlgButtonChecked(hDlg,IDC_RADIO_ROTATE_270);

                lepd.radioScaleToHeight = IsDlgButtonChecked(hDlg,IDC_RADIO_SCALE_TO_HEIGHT);
                lepd.radioScaleToMaterial = IsDlgButtonChecked(hDlg,IDC_RADIO_SCALE_TO_MATERIAL);
                lepd.radioScaleByBlock = IsDlgButtonChecked(hDlg,IDC_RADIO_SCALE_BY_BLOCK);
                lepd.radioScaleByCost = IsDlgButtonChecked(hDlg,IDC_RADIO_SCALE_BY_COST);

                GetDlgItemTextA(hDlg,IDC_MODEL_HEIGHT,lepd.modelHeightString,EP_FIELD_LENGTH);
                GetDlgItemTextA(hDlg,IDC_BLOCK_SIZE,lepd.blockSizeString,EP_FIELD_LENGTH);
                GetDlgItemTextA(hDlg,IDC_COST,lepd.costString,EP_FIELD_LENGTH);

                lepd.chkFillBubbles = IsDlgButtonChecked(hDlg,IDC_FILL_BUBBLES);
                // if filling bubbles is off, sealing entrances does nothing at all
                lepd.chkSealEntrances = lepd.chkFillBubbles ? IsDlgButtonChecked(hDlg,IDC_SEAL_ENTRANCES) : BST_UNCHECKED;
                lepd.chkSealSideTunnels = lepd.chkFillBubbles ? IsDlgButtonChecked(hDlg,IDC_SEAL_SIDE_TUNNELS) : BST_UNCHECKED;

                lepd.chkConnectParts = IsDlgButtonChecked(hDlg,IDC_CONNECT_PARTS);
                // if connect parts is off, corner tips and edges is off
                lepd.chkConnectCornerTips = lepd.chkConnectParts ? IsDlgButtonChecked(hDlg,IDC_CONNECT_CORNER_TIPS) : BST_UNCHECKED;
                lepd.chkConnectAllEdges = lepd.chkConnectParts ? IsDlgButtonChecked(hDlg,IDC_CONNECT_ALL_EDGES) : BST_UNCHECKED;
            
                lepd.chkDeleteFloaters = IsDlgButtonChecked(hDlg,IDC_DELETE_FLOATERS);
            
                lepd.chkHollow = IsDlgButtonChecked(hDlg,IDC_HOLLOW);
                // if hollow is off, superhollow is off
                lepd.chkSuperHollow = lepd.chkHollow ? IsDlgButtonChecked(hDlg,IDC_SUPER_HOLLOW) : BST_UNCHECKED;

                lepd.chkMeltSnow = IsDlgButtonChecked(hDlg,IDC_MELT_SNOW);

                GetDlgItemTextA(hDlg,IDC_FLOAT_COUNT,lepd.floaterCountString,EP_FIELD_LENGTH);
                GetDlgItemTextA(hDlg,IDC_HOLLOW_THICKNESS,lepd.hollowThicknessString,EP_FIELD_LENGTH);

                BOOL debugAvailable = !lepd.radioExportNoMaterials[lepd.fileType] && (lepd.fileType != FILE_TYPE_ASCII_STL);
                lepd.chkShowParts = debugAvailable ? IsDlgButtonChecked(hDlg,IDC_SHOW_PARTS) : BST_UNCHECKED;
                lepd.chkShowWelds = debugAvailable ? IsDlgButtonChecked(hDlg,IDC_SHOW_WELDS) : BST_UNCHECKED;

                lepd.comboPhysicalMaterial[lepd.fileType] = (int)SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_GETCURSEL, 0, 0);
                lepd.comboModelUnits[lepd.fileType] = (int)SendDlgItemMessage(hDlg, IDC_COMBO_MODELS_UNITS, CB_GETCURSEL, 0, 0);


                int nc;
                nc = sscanf_s(lepd.minxString,"%d",&lepd.minxVal);
                nc &= sscanf_s(lepd.minyString,"%d",&lepd.minyVal);
                nc &= sscanf_s(lepd.minzString,"%d",&lepd.minzVal);
                nc &= sscanf_s(lepd.maxxString,"%d",&lepd.maxxVal);
                nc &= sscanf_s(lepd.maxyString,"%d",&lepd.maxyVal);
                nc &= sscanf_s(lepd.maxzString,"%d",&lepd.maxzVal);

                nc &= sscanf_s(lepd.modelHeightString,"%f",&lepd.modelHeightVal);
                nc &= sscanf_s(lepd.blockSizeString,"%f",&lepd.blockSizeVal[lepd.fileType]);
                nc &= sscanf_s(lepd.costString,"%f",&lepd.costVal);

                nc &= sscanf_s(lepd.floaterCountString,"%d",&lepd.floaterCountVal);
                nc &= sscanf_s(lepd.hollowThicknessString,"%g",&lepd.hollowThicknessVal[epd.fileType]);

                // this is a bit lazy checking all errors here, there's probably a better way
                // to test as we go, but this sort of thing should be rare
                if ( nc == 0 )
                {
                    MessageBox(NULL,
                        _T("Bad (non-numeric) value detected in options dialog;\nYou need to clean up, then hit OK again."), _T("Non-numeric value error"), MB_OK|MB_ICONERROR);
                    return (INT_PTR)FALSE;
                }

                if ( lepd.radioScaleToHeight && lepd.modelHeightVal <= 0.0f )
                {
                    MessageBox(NULL,
                        _T("Model height must be a positive number;\nYou need to fix this, then hit OK again."), _T("Value error"), MB_OK|MB_ICONERROR);
                    return (INT_PTR)FALSE;
                }

                if ( lepd.radioScaleByBlock && lepd.blockSizeVal[lepd.fileType] <= 0.0f )
                {
                    MessageBox(NULL,
                        _T("Block size must be a positive number;\nYou need to fix this, then hit OK again."), _T("Value error"), MB_OK|MB_ICONERROR);
                    return (INT_PTR)FALSE;
                }

                if ( lepd.radioScaleByCost )
                {
                    // white vs. colored stuff: $1.50 vs. $3.00 handling fees, plus some minimum amount of material
                    // We need to find out the minimum amount rules for white material; colored is at
                    // http://www.shapeways.com/design-rules/full_color_sandstone, and we use the
                    // "the dimensions have to add up to 65mm" and assume a 3mm block size to give a 59mm*3mm*3mm volume
                    // minimum, times $0.75/cm^3 gives $0.40.
                    if ( lepd.costVal <= (mtlCostTable[curPhysMaterial].costHandling+mtlCostTable[curPhysMaterial].costMinimum) )
                    {
                        MessageBox(NULL,
                            _T("The cost must be > $1.55 for colorless, > $3.40 for color;\nYou need to fix this, then hit OK again."), _T("Value error"), MB_OK|MB_ICONERROR);
                        return (INT_PTR)FALSE;
                    }
                }

                if ( lepd.chkDeleteFloaters && lepd.floaterCountVal < 0 )
                {
                    MessageBox(NULL,
                        _T("Floating objects deletion value cannot be negative;\nYou need to fix this, then hit OK again."), _T("Value error"), MB_OK|MB_ICONERROR);
                    return (INT_PTR)FALSE;
                }

                if ( lepd.chkHollow && lepd.hollowThicknessVal[epd.fileType] < 0.0 )
                {
                    MessageBox(NULL,
                        _T("Hollow thickness value cannot be negative;\nYou need to fix this, then hit OK again."), _T("Value error"), MB_OK|MB_ICONERROR);
                    return (INT_PTR)FALSE;
                }

                // survived tests, so really use data
                epd = lepd;
            } // yes, we do want to fall through here
       case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
