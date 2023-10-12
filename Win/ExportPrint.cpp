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
#include "ExportPrint.h"

#define IS_STL ((epd.fileType == FILE_TYPE_ASCII_STL)||(epd.fileType == FILE_TYPE_BINARY_MAGICS_STL)||(epd.fileType == FILE_TYPE_BINARY_VISCAM_STL))

static int prevPhysMaterial;
static int curPhysMaterial;
static HINSTANCE g_hInst;

// OBJ, OBJ, USD, MAGICS STL, VISCAM STL, ASCII STL, VRML2, SCHEMATIC
#define EP_TOOLTIP_COUNT 56
TooltipDefinition g_epTT[EP_TOOLTIP_COUNT] = {
    { IDC_WORLD_MIN_X,      {1,1,1,1,1,1,1,1}, L"Western edge of volume exported", L""},
    { IDC_WORLD_MIN_Y,      {1,1,1,1,1,1,1,1}, L"Lower bound of volume exported", L""},
    { IDC_WORLD_MIN_Z,      {1,1,1,1,1,1,1,1}, L"Northern edge of volume exported", L""},
    { IDC_WORLD_MAX_X,      {1,1,1,1,1,1,1,1}, L"Eastern edge of volume exported", L""},
    { IDC_WORLD_MAX_Y,      {1,1,1,1,1,1,1,1}, L"Upper bound of volume exported", L""},
    { IDC_WORLD_MAX_Z,      {1,1,1,1,1,1,1,1}, L"Southern edge of volume exported", L""},
    { IDC_CREATE_ZIP,       {1,1,1,1,1,1,1,1}, L"Put all exported files in a corresponding ZIP file", L""},
    { IDC_CREATE_FILES,     {1,1,1,1,1,1,1,1}, L"If unchecked, files are deleted after being put in a ZIP file", L""},
    { IDC_RADIO_EXPORT_NO_MATERIALS,        {1,1,0,1,1,1,1,0}, L"No materials are exported", L""},
    { IDC_RADIO_EXPORT_MTL_COLORS_ONLY,     {1,1,0,2,2,0,1,0}, L"Solid colors are exported, with no textures", L"Solid colors are exported"},
    { IDC_RADIO_EXPORT_SOLID_TEXTURES,      {1,1,1,0,0,0,1,0}, L"Solid 'noisy' textures are exported", L""},
    { IDC_RADIO_EXPORT_MOSAIC_TEXTURES,     {1,1,2,0,0,0,1,0}, L"Three large, mosaic textures of all blocks are exported; useful for 3D printing, not great for rendering", L"For USD, one large, mosaic texture of all blocks is exported"},
    { IDC_RADIO_EXPORT_SEPARATE_TILES,      {1,1,1,0,0,0,0,0}, L"Separate textures are exported for each block face, as needed", L""},
    { IDC_TILE_DIR,         {1,1,2,0,0,0,1,0}, L"Textures are put in a subdirectory called this (delete the name for no subdirectory, putting the textures in the same directory)", L"In the '*_materials' subdirectory, textures are put in a subdirectory called this (delete the name for no subdirectory, putting the textures in the same directory)"},
    { IDC_TEXTURE_RGB,      {1,1,1,0,0,0,1,0}, L"If you previously exported textures for this model, you can save time by unchecking these", L""},
    { IDC_TEXTURE_A,        {1,1,1,0,0,0,1,0}, L"If you previously exported textures for this model, you can save time by unchecking these", L""},
    { IDC_TEXTURE_RGBA,     {1,1,1,0,0,0,1,0}, L"If you previously exported textures for this model, you can save time by unchecking these", L""},
    { IDC_SCALE_LIGHTS,     {0,0,1,0,0,0,0,0}, L"The relative brightness of the sun and dome lights", L""},
    { IDC_SCALE_EMITTERS,   {0,0,1,0,0,0,0,0}, L"The relative brightness of emissive blocks such as torches and lava", L""},
    { IDC_SEPARATE_TYPES,   {1,1,0,0,0,0,0,0}, L"Each type of block - stone, logs, fences, and so on - are put in a separate group", L""},
    { IDC_INDIVIDUAL_BLOCKS,{1,1,1,0,0,0,0,0}, L"All faces of every block are output. Useful if you are animating blocks; you may also then want to 'Make groups objects', below.", L""},
    { IDC_MATERIAL_PER_BLOCK_FAMILY,{1,1,0,0,0,0,0,0}, L"For mosaic texures only: if unchecked, a single material is shared by **all** blocks. Rarely a good idea.", L""},
    { IDC_SPLIT_BY_BLOCK_TYPE,  {1,1,0,0,0,0,0,0}, L"Checked, every type of block has a separate material. Unchecked, blocks in a 'family' (such as 'planks') share a single material.", L""},
    { IDC_MAKE_GROUPS_OBJECTS,  {1,1,0,0,0,0,0,0}, L"Checked, there is one object. Unchecked, each OBJ group is a separate object; useful for animation.", L""},
    { IDC_G3D_MATERIAL,     {1,1,2,0,0,0,0,0}, L"Output extended PBR materials and textures, such as roughness, normals, and emission, as available", L"Use custom 'blocky' shaders for MDL. Uncheck if your textures are high resolution."},
    { IDC_EXPORT_MDL,       {0,0,1,0,0,0,0,0}, L"Export MDL shaders. Unchecked means export only UsdPreviewSurface materials.", L""},
    { IDC_MAKE_Z_UP,        {1,1,1,1,1,1,1,1}, L"The Y axis is up by default; check to instead use Z as the up direction", L""},
    { IDC_SIMPLIFY_MESH,    {1,1,1,0,0,0,0,0}, L"Check to reduce the polygon count, as possible. Downside is that textures are not randomly rotated on grass, etc., to break up pattern repetition.", L""},
    { IDC_CENTER_MODEL,     {1,1,1,1,1,1,1,1}, L"Checked means model is roughly centered around (0,0,0); unchecked means use the world's coordinates", L""},
    { IDC_BIOME,            {1,1,1,1,1,1,1,0}, L"The biome at the center of the model is applied to the whole model, likely changing its coloration", L""},
    { IDC_BLOCKS_AT_BORDERS,{1,1,1,1,1,1,1,0}, L"Unchecked means the bottoms and sides of blocks at the edge of the volume selected are not exported, reducing polygon count", L""},
    { IDC_TREE_LEAVES_SOLID,{1,1,1,0,0,0,1,0}, L"Checked means use solid, non-transparent textures for leaves, so reducing polygon count", L""},
    { IDC_RADIO_SCALE_TO_HEIGHT,    {1,1,1,1,1,1,1,0}, L"Normally for 3D printing, specify the height of the model", L""},
    { IDC_MODEL_HEIGHT,     {1,1,1,1,1,1,1,0}, L"Normally for 3D printing, specify the height of the model", L""},
    { IDC_RADIO_SCALE_TO_MATERIAL,  {1,1,1,1,1,1,1,0}, L"For 3D printing, absolutely minimize the size of the model for the material", L""},
    { IDC_RADIO_SCALE_BY_BLOCK, {1,1,1,1,1,1,1,0}, L"For rendering and 3D printing, change the size of a block", L""},
    { IDC_BLOCK_SIZE,       {1,1,1,1,1,1,1,0}, L"For rendering and 3D printing, change the size of a block", L""},
    { IDC_RADIO_SCALE_BY_COST,  {1,1,1,1,1,1,1,0}, L"For 3D printing, aim for a (very) approximate cost", L""},
    { IDC_COST,             {1,1,1,1,1,1,1,0}, L"For 3D printing, aim for a (very) approximate cost", L""},
    { IDC_FILL_BUBBLES,   {1,1,1,1,1,1,1,0}, L"Any hollow volume is filled with solid material", L""},
    { IDC_SEAL_ENTRANCES, {1,1,1,1,1,1,1,0}, L"Suboption to fill in the insides of buildings", L""},
    { IDC_SEAL_SIDE_TUNNELS,  {1,1,1,1,1,1,1,0}, L"Suboption to fill in isolated tunnels", L""},
    { IDC_CONNECT_PARTS,  {1,1,1,1,1,1,1,0}, L"For 3D printing, connect neighboring blocks if needed", L""},
    { IDC_CONNECT_CORNER_TIPS,{1,1,1,1,1,1,1,0}, L"Suboption to connect separate objects touching at just a point", L""},
    { IDC_CONNECT_ALL_EDGES,  {1,1,1,1,1,1,1,0}, L"Suboption to connect all shared edges", L""},
    { IDC_DELETE_FLOATERS,{1,1,1,1,1,1,1,0}, L"Delete small objects floating in space, unconnected to the main model", L""},
    { IDC_FLOAT_COUNT,    {1,1,1,1,1,1,1,0}, L"Size of small objects in blocks", L""},
    { IDC_HOLLOW,         {1,1,1,1,1,1,1,0}, L"For 3D printing, hollow out the bottom of the model to save material", L""},
    { IDC_HOLLOW_THICKNESS,   {1,1,1,1,1,1,1,0}, L"For 3D printing, how thick to make walls when hollowing", L""},
    { IDC_SUPER_HOLLOW,   {1,1,1,1,1,1,1,0}, L"Be more aggressive in hollowing out volumes", L""},
    { IDC_MELT_SNOW,      {1,1,1,1,1,1,1,0}, L"For 3D printing, melt snow blocks (for sealing entrances)", L""},
    { IDC_EXPORT_ALL,     {1,1,1,1,1,1,1,0}, L"For 3D printing, export more precise versions of partial blocks", L""},
    { IDC_FATTEN,         {1,1,1,1,1,1,1,0}, L"Suboption to make the partial blocks thicker, to print better", L""},
    { IDC_COMPOSITE_OVERLAY,{1,1,1,0,0,0,1,0}, L"If checked, vines, ladders, rails, etc. are composited onto the underlying texture, creating a new texture. Mostly needed for 3D printing.", L""},
    { IDC_SHOW_PARTS,     {1,1,1,1,1,0,1,0}, L"For 3D printing, show separated parts in different colors", L""},
    { IDC_SHOW_WELDS,     {1,1,1,1,1,0,1,0}, L"For 3D printing, show blocks Mineways adds to connect objects", L""},
};



ExportPrint::ExportPrint(void)
{
}


ExportPrint::~ExportPrint(void)
{
}


void getExportPrintData(ExportFileData* pEpd)
{
    *pEpd = epd;
}

void setExportPrintData(ExportFileData* pEpd)
{
    epd = *pEpd;
    // Anything with an indeterminate state on exit needs to get set back to a real state,
    // whatever it started with, unless some new setting has forced it to be different.
    // Currently used just for OBJ export options; indeterminates are considered unchecked otherwise.
    // Since we can switch between file formats, we need to preserve the OBJ settings and not
    // have them destroyed by exporting to VRML, for example.
    origEpd = epd;
}

INT_PTR CALLBACK ExportPrint(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

int doExportPrint(HINSTANCE hInst, HWND hWnd)
{
    gOK = 0;
    g_hInst = hInst;
    DialogBox(hInst, MAKEINTRESOURCE(IDD_EXPT_VIEW), hWnd, ExportPrint);
    // did we hit cancel?
    return gOK;
}

INT_PTR CALLBACK ExportPrint(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    char changeString[EP_FIELD_LENGTH];
    //char oldString[EP_FIELD_LENGTH];
    //char currentString[EP_FIELD_LENGTH];
    UNREFERENCED_PARAMETER(lParam);

    static int focus = -1;

    HWND tt;

    switch (message)
    {
    case WM_INITDIALOG:
    {
        focus = -1;

        // set them up
        sprintf_s(epd.minxString, EP_FIELD_LENGTH, "%d", epd.minxVal);
        sprintf_s(epd.minyString, EP_FIELD_LENGTH, "%d", epd.minyVal);
        sprintf_s(epd.minzString, EP_FIELD_LENGTH, "%d", epd.minzVal);
        sprintf_s(epd.maxxString, EP_FIELD_LENGTH, "%d", epd.maxxVal);
        sprintf_s(epd.maxyString, EP_FIELD_LENGTH, "%d", epd.maxyVal);
        sprintf_s(epd.maxzString, EP_FIELD_LENGTH, "%d", epd.maxzVal);

        sprintf_s(epd.modelHeightString, EP_FIELD_LENGTH, "%g", epd.modelHeightVal);
        sprintf_s(epd.blockSizeString, EP_FIELD_LENGTH, "%g", epd.blockSizeVal[epd.fileType]);
        sprintf_s(epd.costString, EP_FIELD_LENGTH, "%0.2f", epd.costVal);

        sprintf_s(epd.floaterCountString, EP_FIELD_LENGTH, "%d", epd.floaterCountVal);
        sprintf_s(epd.hollowThicknessString, EP_FIELD_LENGTH, "%g", epd.hollowThicknessVal[epd.fileType]);

        SetDlgItemTextA(hDlg, IDC_WORLD_MIN_X, epd.minxString);
        SetDlgItemTextA(hDlg, IDC_WORLD_MIN_Y, epd.minyString);
        SetDlgItemTextA(hDlg, IDC_WORLD_MIN_Z, epd.minzString);
        SetDlgItemTextA(hDlg, IDC_WORLD_MAX_X, epd.maxxString);
        SetDlgItemTextA(hDlg, IDC_WORLD_MAX_Y, epd.maxyString);
        SetDlgItemTextA(hDlg, IDC_WORLD_MAX_Z, epd.maxzString);

        CheckDlgButton(hDlg, IDC_CREATE_ZIP, epd.chkCreateZip[epd.fileType]);
        CheckDlgButton(hDlg, IDC_CREATE_FILES, epd.chkCreateModelFiles[epd.fileType]);

        strcpy_s(epd.scaleLightsString, EP_FIELD_LENGTH, "n/a");
        strcpy_s(epd.scaleEmittersString, EP_FIELD_LENGTH, "n/a");

        if (epd.fileType == FILE_TYPE_USD) {
            // only allow separate texture tiles or full textures - TODO could add others,
            // just need to mess with textures then
            CheckDlgButton(hDlg, IDC_RADIO_EXPORT_NO_MATERIALS, 0);
            CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MTL_COLORS_ONLY, 0);
            CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SOLID_TEXTURES, 0);
            CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MOSAIC_TEXTURES, 1);
            CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES, (epd.flags & EXPT_3DPRINT)?0:1);
            if (epd.radioExportNoMaterials[epd.fileType] || epd.radioExportMtlColors[epd.fileType] || epd.radioExportSolidTexture[epd.fileType]) {
                epd.radioExportNoMaterials[epd.fileType] = epd.radioExportMtlColors[epd.fileType] = epd.radioExportSolidTexture[epd.fileType] = epd.radioExportFullTexture[epd.fileType] = 0;
                epd.radioExportTileTextures[epd.fileType] = 1;
            }

            sprintf_s(epd.scaleLightsString, EP_FIELD_LENGTH, "%g", epd.scaleLightsVal);
            sprintf_s(epd.scaleEmittersString, EP_FIELD_LENGTH, "%g", epd.scaleEmittersVal);
        }
        else if (epd.fileType == FILE_TYPE_VRML2) {
            // only allow mosaic textures at best
            CheckDlgButton(hDlg, IDC_RADIO_EXPORT_NO_MATERIALS, 1);
            CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MTL_COLORS_ONLY, 1);
            CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SOLID_TEXTURES, 1);
            CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MOSAIC_TEXTURES, 1);
            CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES, 0);
            if (epd.radioExportTileTextures[epd.fileType]) {
                epd.radioExportTileTextures[epd.fileType] = 0;
                epd.radioExportFullTexture[epd.fileType] = 1;
            }
        }
        else if (IS_STL) {
            // only allow colors, at most
            CheckDlgButton(hDlg, IDC_RADIO_EXPORT_NO_MATERIALS, 1);
            CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MTL_COLORS_ONLY, (epd.fileType == FILE_TYPE_ASCII_STL) ? 0 : 1);
            CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SOLID_TEXTURES, 0);
            CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MOSAIC_TEXTURES, 0);
            CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES, 0);
            if (epd.radioExportSolidTexture[epd.fileType] || epd.radioExportFullTexture[epd.fileType] || epd.radioExportTileTextures[epd.fileType])
            {
                epd.radioExportSolidTexture[epd.fileType] = epd.radioExportFullTexture[epd.fileType] = epd.radioExportTileTextures[epd.fileType] = 0;
                epd.radioExportNoMaterials[epd.fileType] = 1;
            }
        }
        // is there a way to make a dialog item inactive? That would be better. TODO
        SetDlgItemTextA(hDlg, IDC_SCALE_LIGHTS, epd.scaleLightsString);
        SetDlgItemTextA(hDlg, IDC_SCALE_EMITTERS, epd.scaleEmittersString);

        CheckDlgButton(hDlg, IDC_RADIO_EXPORT_NO_MATERIALS, epd.radioExportNoMaterials[epd.fileType]);
        CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MTL_COLORS_ONLY, epd.radioExportMtlColors[epd.fileType]);
        CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SOLID_TEXTURES, epd.radioExportSolidTexture[epd.fileType]);
        CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MOSAIC_TEXTURES, epd.radioExportFullTexture[epd.fileType]);
        CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES, epd.radioExportTileTextures[epd.fileType]);
        if (epd.radioExportTileTextures[epd.fileType] && (epd.flags & EXPT_3DPRINT)) {
            CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MOSAIC_TEXTURES, 1);
            CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES, 0);
        }
        SetDlgItemTextA(hDlg, IDC_TILE_DIR, epd.tileDirString);

        // if 3D printing, A and RGBA are not options
        CheckDlgButton(hDlg, IDC_TEXTURE_RGB, epd.chkTextureRGB);
        CheckDlgButton(hDlg, IDC_TEXTURE_A, (epd.flags & EXPT_3DPRINT) ? BST_INDETERMINATE : epd.chkTextureA);
        CheckDlgButton(hDlg, IDC_TEXTURE_RGBA, (epd.flags & EXPT_3DPRINT) ? BST_INDETERMINATE : epd.chkTextureRGBA);

        // USD options: gray out if USD in use
        if (epd.fileType == FILE_TYPE_USD)
        {
            // billboard button has no effect for USD
            CheckDlgButton(hDlg, IDC_DOUBLED_BILLBOARD, BST_INDETERMINATE);
        }
        else {
            CheckDlgButton(hDlg, IDC_DOUBLED_BILLBOARD, epd.chkDoubledBillboards);
        }

        // OBJ options: enable, or gray out if OBJ not in use
        if (epd.fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ || epd.fileType == FILE_TYPE_WAVEFRONT_REL_OBJ)
        {
            CheckDlgButton(hDlg, IDC_MAKE_GROUPS_OBJECTS, epd.chkMakeGroupsObjects);
            CheckDlgButton(hDlg, IDC_SEPARATE_TYPES, epd.chkSeparateTypes);
            CheckDlgButton(hDlg, IDC_INDIVIDUAL_BLOCKS, (epd.flags & EXPT_3DPRINT) ? BST_INDETERMINATE : epd.chkIndividualBlocks[epd.fileType]);
            // if neither of the two above are checked, this one's indeterminate
            // rather than confusing the logic below any further, if individual textures are on, make this option indeterminate always
            if (epd.radioExportTileTextures[epd.fileType]) {
                CheckDlgButton(hDlg, IDC_MATERIAL_PER_BLOCK_FAMILY, BST_INDETERMINATE);
            }
            else {
                // if separate tiles are in use, indeterminate; else go ahead.
                CheckDlgButton(hDlg, IDC_MATERIAL_PER_BLOCK_FAMILY,
                    epd.radioExportTileTextures[epd.fileType] ? BST_INDETERMINATE :
                        (epd.chkSeparateTypes || ((epd.flags & EXPT_3DPRINT) ? false : epd.chkIndividualBlocks[epd.fileType])) ? epd.chkMaterialPerFamily : BST_INDETERMINATE);
            }
            CheckDlgButton(hDlg, IDC_SPLIT_BY_BLOCK_TYPE, (epd.chkSeparateTypes || ((epd.flags & EXPT_3DPRINT) ? false : epd.chkIndividualBlocks[epd.fileType])) ? epd.chkSplitByBlockType : BST_INDETERMINATE);
            CheckDlgButton(hDlg, IDC_G3D_MATERIAL, epd.chkCustomMaterial[epd.fileType]);
            CheckDlgButton(hDlg, IDC_EXPORT_MDL, BST_INDETERMINATE);
        }
        else
        {
            // other file formats: keep these grayed out and unselectable, except for USD
            CheckDlgButton(hDlg, IDC_MAKE_GROUPS_OBJECTS, BST_INDETERMINATE);
            CheckDlgButton(hDlg, IDC_SEPARATE_TYPES, BST_INDETERMINATE);
            if (epd.fileType == FILE_TYPE_USD)
                CheckDlgButton(hDlg, IDC_INDIVIDUAL_BLOCKS, (epd.flags & EXPT_3DPRINT) ? BST_INDETERMINATE : epd.chkIndividualBlocks[epd.fileType]);
            else
                CheckDlgButton(hDlg, IDC_INDIVIDUAL_BLOCKS, BST_INDETERMINATE);
            CheckDlgButton(hDlg, IDC_MATERIAL_PER_BLOCK_FAMILY, BST_INDETERMINATE);
            CheckDlgButton(hDlg, IDC_SPLIT_BY_BLOCK_TYPE, BST_INDETERMINATE);
            if (epd.fileType == FILE_TYPE_USD) {
                // indeterminate if exportMDL is off, for USD
                CheckDlgButton(hDlg, IDC_G3D_MATERIAL, epd.chkExportMDL ? epd.chkCustomMaterial[epd.fileType] : BST_INDETERMINATE);
                CheckDlgButton(hDlg, IDC_EXPORT_MDL, epd.chkExportMDL);
            }
            else
            {
                CheckDlgButton(hDlg, IDC_G3D_MATERIAL, BST_INDETERMINATE);
                CheckDlgButton(hDlg, IDC_EXPORT_MDL, BST_INDETERMINATE);
            }
        }

        //CheckDlgButton(hDlg,IDC_MERGE_FLATTOP,epd.chkMergeFlattop);
        CheckDlgButton(hDlg, IDC_MAKE_Z_UP, epd.chkMakeZUp[epd.fileType]);
        // under certain conditions we need to make composite overlay uncheckable, i.e. if 3D printing is on, or if detailed output is off for rendering (or, below, if tiling textures are in use)
        // disallow composites if tile texture is on
        CheckDlgButton(hDlg, IDC_COMPOSITE_OVERLAY, ((epd.flags & EXPT_3DPRINT) || !epd.chkExportAll || !epd.radioExportFullTexture[epd.fileType]) ? BST_INDETERMINATE : epd.chkCompositeOverlay);
        CheckDlgButton(hDlg, IDC_SIMPLIFY_MESH, ((epd.flags & EXPT_3DPRINT) || !epd.radioExportTileTextures[epd.fileType]) ? BST_INDETERMINATE : epd.chkDecimate);

        CheckDlgButton(hDlg, IDC_CENTER_MODEL, epd.chkCenterModel);
        // these next two options are only available for rendering
        CheckDlgButton(hDlg, IDC_TREE_LEAVES_SOLID, (epd.flags & EXPT_3DPRINT) ? BST_INDETERMINATE : epd.chkLeavesSolid);
        CheckDlgButton(hDlg, IDC_BLOCKS_AT_BORDERS, (epd.flags & EXPT_3DPRINT) ? BST_INDETERMINATE : epd.chkBlockFacesAtBorders);
        // disallow biome color if full texture and tile texture is off
        CheckDlgButton(hDlg, IDC_BIOME, (epd.radioExportFullTexture[epd.fileType] || epd.radioExportTileTextures[epd.fileType]) ? epd.chkBiome : BST_INDETERMINATE);

        CheckDlgButton(hDlg, IDC_RADIO_ROTATE_0, epd.radioRotate0);
        CheckDlgButton(hDlg, IDC_RADIO_ROTATE_90, epd.radioRotate90);
        CheckDlgButton(hDlg, IDC_RADIO_ROTATE_180, epd.radioRotate180);
        CheckDlgButton(hDlg, IDC_RADIO_ROTATE_270, epd.radioRotate270);

        CheckDlgButton(hDlg, IDC_RADIO_SCALE_TO_HEIGHT, epd.radioScaleToHeight);
        CheckDlgButton(hDlg, IDC_RADIO_SCALE_TO_MATERIAL, epd.radioScaleToMaterial);
        CheckDlgButton(hDlg, IDC_RADIO_SCALE_BY_BLOCK, epd.radioScaleByBlock);
        CheckDlgButton(hDlg, IDC_RADIO_SCALE_BY_COST, epd.radioScaleByCost);

        SetDlgItemTextA(hDlg, IDC_MODEL_HEIGHT, epd.modelHeightString);
        SetDlgItemTextA(hDlg, IDC_BLOCK_SIZE, epd.blockSizeString);
        SetDlgItemTextA(hDlg, IDC_COST, epd.costString);

        CheckDlgButton(hDlg, IDC_FILL_BUBBLES, epd.chkFillBubbles);
        CheckDlgButton(hDlg, IDC_SEAL_ENTRANCES, epd.chkFillBubbles ? epd.chkSealEntrances : BST_INDETERMINATE);
        CheckDlgButton(hDlg, IDC_SEAL_SIDE_TUNNELS, epd.chkFillBubbles ? epd.chkSealSideTunnels : BST_INDETERMINATE);

        CheckDlgButton(hDlg, IDC_CONNECT_PARTS, epd.chkConnectParts);
        CheckDlgButton(hDlg, IDC_CONNECT_CORNER_TIPS, epd.chkConnectParts ? epd.chkConnectCornerTips : BST_INDETERMINATE);
        CheckDlgButton(hDlg, IDC_CONNECT_ALL_EDGES, epd.chkConnectParts ? epd.chkConnectAllEdges : BST_INDETERMINATE);

        CheckDlgButton(hDlg, IDC_DELETE_FLOATERS, epd.chkDeleteFloaters);
        CheckDlgButton(hDlg, IDC_HOLLOW, epd.chkHollow[epd.fileType]);
        CheckDlgButton(hDlg, IDC_SUPER_HOLLOW, epd.chkHollow[epd.fileType] ? epd.chkSuperHollow[epd.fileType] : BST_INDETERMINATE);
        SetDlgItemTextA(hDlg, IDC_FLOAT_COUNT, epd.floaterCountString);
        SetDlgItemTextA(hDlg, IDC_HOLLOW_THICKNESS, epd.hollowThicknessString);

        CheckDlgButton(hDlg, IDC_MELT_SNOW, epd.chkMeltSnow);

        CheckDlgButton(hDlg, IDC_EXPORT_ALL, epd.chkExportAll);
        CheckDlgButton(hDlg, IDC_FATTEN, epd.chkExportAll ? epd.chkFatten : BST_INDETERMINATE);

        BOOL debugAvailable = !epd.radioExportNoMaterials[epd.fileType] && (epd.fileType != FILE_TYPE_ASCII_STL) && (epd.fileType != FILE_TYPE_SCHEMATIC);

        CheckDlgButton(hDlg, IDC_SHOW_PARTS, debugAvailable ? epd.chkShowParts : BST_INDETERMINATE);
        CheckDlgButton(hDlg, IDC_SHOW_WELDS, debugAvailable ? epd.chkShowWelds : BST_INDETERMINATE);

        // When handling INITDIALOG message, send the combo box a message:
        for (int i = 0; i < MTL_COST_TABLE_SIZE; i++)
            SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_ADDSTRING, 0, (LPARAM)gMtlCostTable[i].wname);

        SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, epd.comboPhysicalMaterial[epd.fileType], 0);
        prevPhysMaterial = curPhysMaterial = epd.comboPhysicalMaterial[epd.fileType];

        for (int i = 0; i < MODELS_UNITS_TABLE_SIZE; i++)
            SendDlgItemMessage(hDlg, IDC_COMBO_MODELS_UNITS, CB_ADDSTRING, 0, (LPARAM)gUnitTypeTable[i].wname);

        SendDlgItemMessage(hDlg, IDC_COMBO_MODELS_UNITS, CB_SETCURSEL, epd.comboModelUnits[epd.fileType], 0);

        // tooltips
        for (int itt = 0; itt < EP_TOOLTIP_COUNT; itt++) {
            switch (g_epTT[itt].fileTypeMsg[epd.fileType]) {
            default:
                assert(0);  // wrong value, not between 0-2 inclusive, for fileTypeMsg
            case 0:
                tt = CreateToolTip(g_epTT[itt].id, g_hInst, hDlg, L"Not used for this file format");
                break;
            case 1:
                tt = CreateToolTip(g_epTT[itt].id, g_hInst, hDlg, g_epTT[itt].name1);
                break;
            case 2:
                tt = CreateToolTip(g_epTT[itt].id, g_hInst, hDlg, g_epTT[itt].name2);
                break;
            }
            if (tt != NULL) {
                SendMessage(tt, TTM_ACTIVATE, TRUE, 0);
            }
            else {
                assert(0);
            }
        }
    }
    return (INT_PTR)TRUE;
    case WM_COMMAND:

        switch (LOWORD(wParam))
        {
        case IDC_EXPORT_HELP:
            ShellExecute(NULL, L"open", L"http://mineways.com/mineways.html#options", NULL, NULL, SW_SHOWNORMAL);
            break;
        case IDC_FILL_BUBBLES:
        {
            UINT isFillBubblesChecked = IsDlgButtonChecked(hDlg, IDC_FILL_BUBBLES);
            CheckDlgButton(hDlg, IDC_SEAL_ENTRANCES, isFillBubblesChecked ? epd.chkSealEntrances : BST_INDETERMINATE);
            CheckDlgButton(hDlg, IDC_SEAL_SIDE_TUNNELS, isFillBubblesChecked ? epd.chkSealEntrances : BST_INDETERMINATE);
        }
        break;
        case IDC_SEAL_ENTRANCES:
        {
            // all this crazy code says is "if Fill Bubbles is not checked, keep the seal entrances checkbox indeterminate;
            // if it *is* checked, then don't allow seal entrances to become indeterminate."
            UINT isChecked = IsDlgButtonChecked(hDlg, IDC_FILL_BUBBLES);
            if (!isChecked)
            {
                CheckDlgButton(hDlg, IDC_SEAL_ENTRANCES, BST_INDETERMINATE);
            }
            else
            {
                UINT isIndeterminate = (IsDlgButtonChecked(hDlg, IDC_SEAL_ENTRANCES) == BST_INDETERMINATE);
                if (isIndeterminate)
                    CheckDlgButton(hDlg, IDC_SEAL_ENTRANCES, BST_UNCHECKED);
            }
        }
        break;
        case IDC_SEAL_SIDE_TUNNELS:
        {
            UINT isChecked = IsDlgButtonChecked(hDlg, IDC_FILL_BUBBLES);
            if (!isChecked)
            {
                CheckDlgButton(hDlg, IDC_SEAL_SIDE_TUNNELS, BST_INDETERMINATE);
            }
            else
            {
                UINT isIndeterminate = (IsDlgButtonChecked(hDlg, IDC_SEAL_SIDE_TUNNELS) == BST_INDETERMINATE);
                if (isIndeterminate)
                    CheckDlgButton(hDlg, IDC_SEAL_SIDE_TUNNELS, BST_UNCHECKED);
            }
        }
        break;

        case IDC_CONNECT_PARTS:
        {
            UINT isConnectPartsChecked = IsDlgButtonChecked(hDlg, IDC_CONNECT_PARTS);
            // if connect parts turned on, then default setting for others are turned on
            CheckDlgButton(hDlg, IDC_CONNECT_CORNER_TIPS, isConnectPartsChecked ? epd.chkConnectCornerTips : BST_INDETERMINATE);
            CheckDlgButton(hDlg, IDC_CONNECT_ALL_EDGES, isConnectPartsChecked ? epd.chkConnectAllEdges : BST_INDETERMINATE);
        }
        case IDC_CONNECT_CORNER_TIPS:
        {
            UINT isChecked = IsDlgButtonChecked(hDlg, IDC_CONNECT_PARTS);
            if (!isChecked)
            {
                CheckDlgButton(hDlg, IDC_CONNECT_CORNER_TIPS, BST_INDETERMINATE);
            }
            else
            {
                UINT isIndeterminate = (IsDlgButtonChecked(hDlg, IDC_CONNECT_CORNER_TIPS) == BST_INDETERMINATE);
                if (isIndeterminate)
                    CheckDlgButton(hDlg, IDC_CONNECT_CORNER_TIPS, BST_UNCHECKED);
            }
        }
        break;
        case IDC_CONNECT_ALL_EDGES:
        {
            UINT isChecked = IsDlgButtonChecked(hDlg, IDC_CONNECT_PARTS);
            if (!isChecked)
            {
                CheckDlgButton(hDlg, IDC_CONNECT_ALL_EDGES, BST_INDETERMINATE);
            }
            else
            {
                UINT isIndeterminate = (IsDlgButtonChecked(hDlg, IDC_CONNECT_ALL_EDGES) == BST_INDETERMINATE);
                if (isIndeterminate)
                    CheckDlgButton(hDlg, IDC_CONNECT_ALL_EDGES, BST_UNCHECKED);
            }
        }
        break;

        case IDC_HOLLOW:
        {
            UINT isHollowChecked = IsDlgButtonChecked(hDlg, IDC_HOLLOW);
            // if hollow turned on, then default setting for superhollow is on
            CheckDlgButton(hDlg, IDC_SUPER_HOLLOW, isHollowChecked ? epd.chkSuperHollow[epd.fileType] : BST_INDETERMINATE);
        }
        break;
        case IDC_SUPER_HOLLOW:
        {
            UINT isHollowChecked = IsDlgButtonChecked(hDlg, IDC_HOLLOW);
            if (!isHollowChecked)
            {
                CheckDlgButton(hDlg, IDC_SUPER_HOLLOW, BST_INDETERMINATE);
            }
            else
            {
                UINT isSuperHollowIndeterminate = (IsDlgButtonChecked(hDlg, IDC_SUPER_HOLLOW) == BST_INDETERMINATE);
                if (isSuperHollowIndeterminate)
                    CheckDlgButton(hDlg, IDC_SUPER_HOLLOW, BST_UNCHECKED);
            }
        }
        break;

        case IDC_RADIO_EXPORT_NO_MATERIALS:
            if (epd.fileType == FILE_TYPE_USD) {
                // don't allow anything but tile output
                CheckDlgButton(hDlg, IDC_RADIO_EXPORT_NO_MATERIALS, 0);
                CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MOSAIC_TEXTURES, (epd.flags& EXPT_3DPRINT) ? 1 : 0);
                CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES, (epd.flags & EXPT_3DPRINT) ? 0 : 1);
                SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, 0);
            }
            else {
                // set the combo box material to white (might already be that, which is fine)
                SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_WHITE_STRONG_FLEXIBLE, 0);
                // kinda sleazy: if we go to anything but full textures, turn off exporting all objects
                // - done because full blocks of the lesser objects usually looks dumb
                CheckDlgButton(hDlg, IDC_EXPORT_ALL, BST_UNCHECKED);
            }
            CheckDlgButton(hDlg, IDC_COMPOSITE_OVERLAY, BST_INDETERMINATE);
            CheckDlgButton(hDlg, IDC_SIMPLIFY_MESH, BST_INDETERMINATE);
            goto ChangeMaterial;

        case IDC_RADIO_EXPORT_MTL_COLORS_ONLY:
            if (epd.fileType == FILE_TYPE_USD) {
                // don't allow anything but tile output
                CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MTL_COLORS_ONLY, 0);
                CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MOSAIC_TEXTURES, (epd.flags & EXPT_3DPRINT) ? 1 : 0);
                CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES, (epd.flags & EXPT_3DPRINT) ? 0 : 1);
                SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, 0);
            }
            else if (epd.fileType == FILE_TYPE_ASCII_STL) {
                CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MTL_COLORS_ONLY, 0);
                CheckDlgButton(hDlg, IDC_RADIO_EXPORT_NO_MATERIALS, 1);
                SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, 0);
            }
            else {
                // set the combo box material to color (might already be that, which is fine)
                SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, 0);
                // kinda sleazy: if we go to anything but full textures, turn off exporting all objects
                CheckDlgButton(hDlg, IDC_EXPORT_ALL, BST_UNCHECKED);
            }
            CheckDlgButton(hDlg, IDC_COMPOSITE_OVERLAY, BST_INDETERMINATE);
            CheckDlgButton(hDlg, IDC_SIMPLIFY_MESH, BST_INDETERMINATE);
            goto ChangeMaterial;

        case IDC_RADIO_EXPORT_SOLID_TEXTURES:
            if (epd.fileType == FILE_TYPE_USD) {
                // don't allow anything but tile output
                CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SOLID_TEXTURES, 0);
                CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MOSAIC_TEXTURES, (epd.flags& EXPT_3DPRINT) ? 1 : 0);
                CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES, (epd.flags& EXPT_3DPRINT) ? 0 : 1);
                SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, 0);
            }
            else if (epd.fileType == FILE_TYPE_ASCII_STL) {
                CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SOLID_TEXTURES, 0);
                CheckDlgButton(hDlg, IDC_RADIO_EXPORT_NO_MATERIALS, 1);
                SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, 0);
            }
            else if (epd.fileType == FILE_TYPE_BINARY_MAGICS_STL || epd.fileType == FILE_TYPE_BINARY_VISCAM_STL) {
                CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SOLID_TEXTURES, 0);
                CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MTL_COLORS_ONLY, 1);
                SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, 0);
            }
            else {
                // set the combo box material to color (might already be that, which is fine)
                SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, 0);
                // kinda sleazy: if we go to anything but full textures, turn off exporting all objects
                CheckDlgButton(hDlg, IDC_EXPORT_ALL, BST_UNCHECKED);
            }
            CheckDlgButton(hDlg, IDC_COMPOSITE_OVERLAY, BST_INDETERMINATE);
            CheckDlgButton(hDlg, IDC_SIMPLIFY_MESH, BST_INDETERMINATE);
            goto ChangeMaterial;

        case IDC_RADIO_EXPORT_MOSAIC_TEXTURES:
            if (epd.fileType == FILE_TYPE_ASCII_STL) {
                CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MOSAIC_TEXTURES, 0);
                CheckDlgButton(hDlg, IDC_RADIO_EXPORT_NO_MATERIALS, 1);
                SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_WHITE_STRONG_FLEXIBLE, 0);
            }
            else if (epd.fileType == FILE_TYPE_BINARY_MAGICS_STL || epd.fileType == FILE_TYPE_BINARY_VISCAM_STL) {
                CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MOSAIC_TEXTURES, 0);
                CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MTL_COLORS_ONLY, 1);
                SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, 0);
            }
            else {
                // set the combo box material to color (might already be that, which is fine)
                // if this option is picked, assume colored output material
                SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, 0);
                CheckDlgButton(hDlg, IDC_COMPOSITE_OVERLAY, ((epd.flags & EXPT_3DPRINT) || !epd.chkExportAll) ? BST_INDETERMINATE : epd.chkCompositeOverlay);
                CheckDlgButton(hDlg, IDC_EXPORT_ALL, (epd.flags & EXPT_3DPRINT) ? BST_UNCHECKED : BST_CHECKED);
                CheckDlgButton(hDlg, IDC_MATERIAL_PER_BLOCK_FAMILY, (epd.fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ || epd.fileType == FILE_TYPE_WAVEFRONT_REL_OBJ) &&
                    (IsDlgButtonChecked(hDlg, IDC_SEPARATE_TYPES) == BST_CHECKED) || (IsDlgButtonChecked(hDlg, IDC_INDIVIDUAL_BLOCKS) == BST_CHECKED) ? epd.chkMaterialPerFamily : BST_INDETERMINATE);
                CheckDlgButton(hDlg, IDC_SPLIT_BY_BLOCK_TYPE, (epd.fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ || epd.fileType == FILE_TYPE_WAVEFRONT_REL_OBJ) &&
                    (IsDlgButtonChecked(hDlg, IDC_SEPARATE_TYPES) == BST_CHECKED) || (IsDlgButtonChecked(hDlg, IDC_INDIVIDUAL_BLOCKS) == BST_CHECKED) ? epd.chkSplitByBlockType : BST_INDETERMINATE);
            }
            CheckDlgButton(hDlg, IDC_SIMPLIFY_MESH, BST_INDETERMINATE);
            goto ChangeMaterial;

        case IDC_RADIO_EXPORT_SEPARATE_TILES:
            if (epd.flags & EXPT_3DPRINT) {
                // don't allow tile output for 3d printing except for USD
                if (epd.fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ || epd.fileType == FILE_TYPE_WAVEFRONT_REL_OBJ || 
                    epd.fileType == FILE_TYPE_VRML2 || epd.fileType == FILE_TYPE_USD) {
                    // single texture is only allowed type, since we can't use compositing with separate tiles
                    CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MOSAIC_TEXTURES, 1);
                    CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES, 0);
                    SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, 0);
                    CheckDlgButton(hDlg, IDC_EXPORT_ALL, BST_UNCHECKED);
                }
                else if (epd.fileType == FILE_TYPE_ASCII_STL) {
                    CheckDlgButton(hDlg, IDC_RADIO_EXPORT_NO_MATERIALS, 1);
                    CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES, 0);
                    SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_WHITE_STRONG_FLEXIBLE, 0);
                }
                else if (epd.fileType == FILE_TYPE_BINARY_MAGICS_STL || epd.fileType == FILE_TYPE_BINARY_VISCAM_STL || epd.fileType == FILE_TYPE_USD) {
                    CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MTL_COLORS_ONLY, 1);
                    CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES, 0);
                    SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, 0);
                }
                // for 3d printing, make sure compositing is on when selected - no; now we always show as indeterminate, since it can never be turned off
                //CheckDlgButton(hDlg, IDC_EXPORT_ALL, (epd.flags & EXPT_3DPRINT) ? BST_UNCHECKED : BST_CHECKED);
            }
            else {
                // render
                if (epd.fileType == FILE_TYPE_ASCII_STL) {
                    CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES, 0);
                    CheckDlgButton(hDlg, IDC_RADIO_EXPORT_NO_MATERIALS, 1);
                    SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_WHITE_STRONG_FLEXIBLE, 0);
                }
                else if (epd.fileType == FILE_TYPE_BINARY_MAGICS_STL || epd.fileType == FILE_TYPE_BINARY_VISCAM_STL) {
                    CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES, 0);
                    CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MTL_COLORS_ONLY, 1);
                    SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, 0);
                }
                else if (epd.fileType == FILE_TYPE_VRML2) {
                    CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES, 0);
                    CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MOSAIC_TEXTURES, 1);
                    SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, 0);
                }
                else {
                    // Valid selection (OBJ or USD)
                    // set the combo box material to color (might already be that, which is fine)
                    // not used in rendering, but still set it:
                    SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_SETCURSEL, PRINT_MATERIAL_FULL_COLOR_SANDSTONE, 0);
                    CheckDlgButton(hDlg, IDC_EXPORT_ALL, BST_CHECKED);
                    // if individual blocks is on, this one's indeterminate
                    CheckDlgButton(hDlg, IDC_SIMPLIFY_MESH, (IsDlgButtonChecked(hDlg, IDC_INDIVIDUAL_BLOCKS) == BST_CHECKED) ? BST_INDETERMINATE : epd.chkDecimate);
                }
            }
            // meaningless whenever individual tiles is set
            CheckDlgButton(hDlg, IDC_MATERIAL_PER_BLOCK_FAMILY, BST_INDETERMINATE);
            CheckDlgButton(hDlg, IDC_COMPOSITE_OVERLAY, BST_INDETERMINATE);
            goto ChangeMaterial;

        case IDC_TEXTURE_A:
        {
            if (epd.flags & EXPT_3DPRINT)
            {
                CheckDlgButton(hDlg, IDC_TEXTURE_A, BST_INDETERMINATE);
            }
            else
            {
                UINT isIndeterminate = (IsDlgButtonChecked(hDlg, IDC_TEXTURE_A) == BST_INDETERMINATE);
                if (isIndeterminate)
                    CheckDlgButton(hDlg, IDC_TEXTURE_A, BST_UNCHECKED);
            }
        }
        break;
        case IDC_TEXTURE_RGBA:
        {
            if (epd.flags & EXPT_3DPRINT)
            {
                CheckDlgButton(hDlg, IDC_TEXTURE_RGBA, BST_INDETERMINATE);
            }
            else
            {
                UINT isIndeterminate = (IsDlgButtonChecked(hDlg, IDC_TEXTURE_RGBA) == BST_INDETERMINATE);
                if (isIndeterminate)
                    CheckDlgButton(hDlg, IDC_TEXTURE_RGBA, BST_UNCHECKED);
            }
        }
        break;

        case IDC_COMBO_PHYSICAL_MATERIAL:
        ChangeMaterial:
        {
            // Change the scale only if 3D printing. Otherwise any material change should not change the scale!
            if (epd.flags & EXPT_3DPRINT) {
                // combo box selection will change the thickness, if previous value is set to the default
                curPhysMaterial = (int)SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_GETCURSEL, 0, 0);
                if (prevPhysMaterial != curPhysMaterial)
                {
                    //sprintf_s(oldString,EP_FIELD_LENGTH,"%g",METERS_TO_MM * mtlCostTable[prevPhysMaterial].minWall);
                    sprintf_s(changeString, EP_FIELD_LENGTH, "%g", METERS_TO_MM * gMtlCostTable[curPhysMaterial].minWall);

                    // this old code cleverly changed the value only if the user hadn't set it to something else. This
                    // is a little too clever: if the user set the value, then there was no way he could find out what
                    // a material's minimum thickness had to be when he chose the material - he'd have to restart the
                    // program. Better to force the user to set block size again if he changes the material type.
                    //GetDlgItemTextA(hDlg,IDC_BLOCK_SIZE,currentString,EP_FIELD_LENGTH);
                    //if ( strcmp(oldString,currentString) == 0)
                    SetDlgItemTextA(hDlg, IDC_BLOCK_SIZE, changeString);

                    //GetDlgItemTextA(hDlg,IDC_HOLLOW_THICKNESS,currentString,EP_FIELD_LENGTH);
                    //if ( strcmp(oldString,currentString) == 0)
                    SetDlgItemTextA(hDlg, IDC_HOLLOW_THICKNESS, changeString);

                    prevPhysMaterial = curPhysMaterial;
                }
            }

            // if material output turned off, don't allow debug options
            BOOL colorAvailable = !IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_NO_MATERIALS)
                && (epd.fileType != FILE_TYPE_ASCII_STL);
            if (colorAvailable)
            {
                // wipe out any indeterminates
                if (IsDlgButtonChecked(hDlg, IDC_SHOW_PARTS) == BST_INDETERMINATE)
                {
                    // back to state at start
                    CheckDlgButton(hDlg, IDC_SHOW_PARTS, epd.chkShowParts);
                    CheckDlgButton(hDlg, IDC_SHOW_WELDS, epd.chkShowParts);
                }
            }
            else
            {
                // shut them down
                CheckDlgButton(hDlg, IDC_SHOW_PARTS, BST_INDETERMINATE);
                CheckDlgButton(hDlg, IDC_SHOW_WELDS, BST_INDETERMINATE);
            }
            // disallow biome color if not full texture or tile textures
            CheckDlgButton(hDlg, IDC_BIOME, 
                (IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_MOSAIC_TEXTURES) || IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES) )
                ? epd.chkBiome : BST_INDETERMINATE);
        }
        break;
        case IDC_BIOME:
        {
            UINT isInactive = !(IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_MOSAIC_TEXTURES) || IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES));
            if (isInactive)
            {
                CheckDlgButton(hDlg, IDC_BIOME, BST_INDETERMINATE);
            }
            else
            {
                UINT isIndeterminate = (IsDlgButtonChecked(hDlg, IDC_BIOME) == BST_INDETERMINATE);
                if (isIndeterminate)
                    CheckDlgButton(hDlg, IDC_BIOME, BST_UNCHECKED);
            }
        }
        break;
        case IDC_SHOW_PARTS:
        {
            UINT isInactive = IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_NO_MATERIALS)
                | (epd.fileType == FILE_TYPE_ASCII_STL);
            if (isInactive)
            {
                CheckDlgButton(hDlg, IDC_SHOW_PARTS, BST_INDETERMINATE);
            }
            else
            {
                UINT isIndeterminate = (IsDlgButtonChecked(hDlg, IDC_SHOW_PARTS) == BST_INDETERMINATE);
                if (isIndeterminate)
                    CheckDlgButton(hDlg, IDC_SHOW_PARTS, BST_UNCHECKED);
            }
        }
        break;
        case IDC_SHOW_WELDS:
        {
            UINT isInactive = IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_NO_MATERIALS)
                | (epd.fileType == FILE_TYPE_ASCII_STL);
            if (isInactive)
            {
                CheckDlgButton(hDlg, IDC_SHOW_WELDS, BST_INDETERMINATE);
            }
            else
            {
                UINT isIndeterminate = (IsDlgButtonChecked(hDlg, IDC_SHOW_WELDS) == BST_INDETERMINATE);
                if (isIndeterminate)
                    CheckDlgButton(hDlg, IDC_SHOW_WELDS, BST_UNCHECKED);
            }
        }
        break;

        case IDC_CREATE_ZIP:
        {
            // if zip off, model file export must be set to on
            if (!IsDlgButtonChecked(hDlg, IDC_CREATE_ZIP))
            {
                CheckDlgButton(hDlg, IDC_CREATE_FILES, BST_CHECKED);
            }
        }
        break;

        case IDC_CREATE_FILES:
        {
            // if model off, model file export must be set to on
            if (!IsDlgButtonChecked(hDlg, IDC_CREATE_FILES))
            {
                CheckDlgButton(hDlg, IDC_CREATE_ZIP, BST_CHECKED);
            }
        }
        break;


        case IDC_SEPARATE_TYPES:
            if (epd.fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ || epd.fileType == FILE_TYPE_WAVEFRONT_REL_OBJ)
            {
                // if it was checked, then it goes to indeterminate, but that really means "go to unchecked"
                if (IsDlgButtonChecked(hDlg, IDC_SEPARATE_TYPES) == BST_INDETERMINATE)
                {
                    // uncheck the box
                    // go from the indeterminate tristate to unchecked - indeterminate is not selectable
                    CheckDlgButton(hDlg, IDC_SEPARATE_TYPES, BST_UNCHECKED);
                    // now adjust sub-items. Material per type is indeterminate if multiple objects is unchecked,
                    // AND individual blocks is unchecked.
                    if (IsDlgButtonChecked(hDlg, IDC_INDIVIDUAL_BLOCKS) != BST_CHECKED) {
                        // both separate types and individual blocks are unchecked, so these two are indeterminate (not used):
                        CheckDlgButton(hDlg, IDC_MATERIAL_PER_BLOCK_FAMILY, BST_INDETERMINATE);
                        CheckDlgButton(hDlg, IDC_SPLIT_BY_BLOCK_TYPE, BST_INDETERMINATE);
                    }
                }
                else
                {
                    // check the box and set up state back to the default:
                    if (epd.flags & EXPT_3DPRINT)
                    {
                        // for 3D printing we never allow individual blocks.
                        CheckDlgButton(hDlg, IDC_INDIVIDUAL_BLOCKS, BST_INDETERMINATE);
                    }
                    else
                    {
                        // checked
                        // turn materials and split by block type back on if indeterminate (which happens when both
                        // individual and separate types were off).
                        if (IsDlgButtonChecked(hDlg, IDC_INDIVIDUAL_BLOCKS) == BST_UNCHECKED) {
                            CheckDlgButton(hDlg, IDC_MATERIAL_PER_BLOCK_FAMILY, BST_CHECKED);
                            CheckDlgButton(hDlg, IDC_SPLIT_BY_BLOCK_TYPE, BST_CHECKED);
                        }
                        else {
                            // turn off individual blocks
                            CheckDlgButton(hDlg, IDC_INDIVIDUAL_BLOCKS, BST_UNCHECKED);
                            // and allow simplify again
                            if (IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES)) {
                                CheckDlgButton(hDlg, IDC_SIMPLIFY_MESH, epd.chkDecimate);
                            }
                        }
                    }
                }
            }
            else
            {
                CheckDlgButton(hDlg, IDC_SEPARATE_TYPES, BST_INDETERMINATE);
            }
            // make sure, no matter what, that material block per family stays indeterminate when individual textures is on:
            if (IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES)) {
                CheckDlgButton(hDlg, IDC_MATERIAL_PER_BLOCK_FAMILY, BST_INDETERMINATE);
            }
            break;
        case IDC_INDIVIDUAL_BLOCKS:
            // the indeterminate state is only for when the option is not available (i.e., 3d printing, simplify mesh)
            if (epd.flags & EXPT_3DPRINT) {
                CheckDlgButton(hDlg, IDC_INDIVIDUAL_BLOCKS, BST_INDETERMINATE);
            }
            else
            {
                if (epd.fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ || epd.fileType == FILE_TYPE_WAVEFRONT_REL_OBJ)
                {
                    if (IsDlgButtonChecked(hDlg, IDC_INDIVIDUAL_BLOCKS) == BST_INDETERMINATE)
                    {
                        // go from the indeterminate tristate to unchecked - indeterminate is not selectable
                        CheckDlgButton(hDlg, IDC_INDIVIDUAL_BLOCKS, BST_UNCHECKED);
                        // now adjust sub-items. Material per type is indeterminate if multiple objects is unchecked,
                        // AND individual blocks is unchecked.
                        if (IsDlgButtonChecked(hDlg, IDC_SEPARATE_TYPES) != BST_CHECKED) {
                            // both separate types and individual blocks are unchecked, so these two are indeterminate (not used):
                            CheckDlgButton(hDlg, IDC_MATERIAL_PER_BLOCK_FAMILY, BST_INDETERMINATE);
                            CheckDlgButton(hDlg, IDC_SPLIT_BY_BLOCK_TYPE, BST_INDETERMINATE);
                        }
                        // unchecked, so go back to whatever state we came in with for decimation
                        if (IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES)) {
                            CheckDlgButton(hDlg, IDC_SIMPLIFY_MESH, epd.chkDecimate);
                        }
                    }
                    else
                    {
                        // checked, so the boxes below becomes active
                        // turn separate types off - can't use both.
                        CheckDlgButton(hDlg, IDC_SEPARATE_TYPES, BST_UNCHECKED);
                        // these should both get turned on for this mode by default
                        CheckDlgButton(hDlg, IDC_MATERIAL_PER_BLOCK_FAMILY, BST_CHECKED);
                        CheckDlgButton(hDlg, IDC_SPLIT_BY_BLOCK_TYPE, BST_CHECKED);
                        CheckDlgButton(hDlg, IDC_SIMPLIFY_MESH, BST_INDETERMINATE);
                    }
                }
                else if (epd.fileType == FILE_TYPE_USD)
                {
                    if (IsDlgButtonChecked(hDlg, IDC_INDIVIDUAL_BLOCKS) == BST_INDETERMINATE)
                    {
                        // uncheck the box
                        // go from the indeterminate tristate to unchecked - indeterminate is not selectable
                        CheckDlgButton(hDlg, IDC_INDIVIDUAL_BLOCKS, BST_UNCHECKED);
                        // unchecked, so go back to whatever state we came in with for decimation
                        if (IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES)) {
                            CheckDlgButton(hDlg, IDC_SIMPLIFY_MESH, epd.chkDecimate);
                        }
                    }
                    else {
                        CheckDlgButton(hDlg, IDC_SIMPLIFY_MESH, BST_INDETERMINATE);
                    }
                }
                else
                {
                    CheckDlgButton(hDlg, IDC_INDIVIDUAL_BLOCKS, BST_INDETERMINATE);
                }
            }
            // make sure, no matter what, that material block per family stays indeterminate when individual textures is on:
            if (IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES)) {
                CheckDlgButton(hDlg, IDC_MATERIAL_PER_BLOCK_FAMILY, BST_INDETERMINATE);
            }
            break;
        case IDC_MATERIAL_PER_BLOCK_FAMILY:
            if ((epd.fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ || epd.fileType == FILE_TYPE_WAVEFRONT_REL_OBJ) &&
                (IsDlgButtonChecked(hDlg, IDC_SEPARATE_TYPES) == BST_CHECKED) || (IsDlgButtonChecked(hDlg, IDC_INDIVIDUAL_BLOCKS) == BST_CHECKED))
            {
                // things are unlocked
                if (IsDlgButtonChecked(hDlg, IDC_MATERIAL_PER_BLOCK_FAMILY) == BST_INDETERMINATE)
                {
                    CheckDlgButton(hDlg, IDC_MATERIAL_PER_BLOCK_FAMILY, BST_UNCHECKED);
                }
            }
            else
            {
                // button is not unlocked
                CheckDlgButton(hDlg, IDC_MATERIAL_PER_BLOCK_FAMILY, BST_INDETERMINATE);
            }
            // make sure, no matter what, that material block per family stays indeterminate when individual textures is on:
            if (IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES)) {
                CheckDlgButton(hDlg, IDC_MATERIAL_PER_BLOCK_FAMILY, BST_INDETERMINATE);
            }
            break;
        case IDC_SPLIT_BY_BLOCK_TYPE:
            if ((epd.fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ || epd.fileType == FILE_TYPE_WAVEFRONT_REL_OBJ) && 
                (IsDlgButtonChecked(hDlg, IDC_SEPARATE_TYPES) == BST_CHECKED) || (IsDlgButtonChecked(hDlg, IDC_INDIVIDUAL_BLOCKS) == BST_CHECKED))
            {
                if (IsDlgButtonChecked(hDlg, IDC_SPLIT_BY_BLOCK_TYPE) == BST_INDETERMINATE)
                {
                    CheckDlgButton(hDlg, IDC_SPLIT_BY_BLOCK_TYPE, BST_UNCHECKED);
                }
            }
            else
            {
                CheckDlgButton(hDlg, IDC_SPLIT_BY_BLOCK_TYPE, BST_INDETERMINATE);
            }
            break;

        case IDC_G3D_MATERIAL:
            if (epd.fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ || epd.fileType == FILE_TYPE_WAVEFRONT_REL_OBJ )
            {
                if (IsDlgButtonChecked(hDlg, IDC_G3D_MATERIAL) == BST_INDETERMINATE)
                {
                    CheckDlgButton(hDlg, IDC_G3D_MATERIAL, BST_UNCHECKED);
                }
            }
            // modify state for USD only if MDL export is checked:
            else if (epd.fileType == FILE_TYPE_USD && IsDlgButtonChecked(hDlg, IDC_EXPORT_MDL) == BST_CHECKED) {
                if (IsDlgButtonChecked(hDlg, IDC_G3D_MATERIAL) == BST_INDETERMINATE)
                {
                    CheckDlgButton(hDlg, IDC_G3D_MATERIAL, BST_UNCHECKED);
                }
            }
            else
            {
                // do nothing, not valid
                CheckDlgButton(hDlg, IDC_G3D_MATERIAL, BST_INDETERMINATE);
            }
            break;

        case IDC_EXPORT_MDL:
            if (epd.fileType == FILE_TYPE_USD)
            {
                // This is a tri-state box, cycling through unchecked, checked, and indeterminate.
                // If we detect INDETERMINATE while using USD, that really means UNCHECKED, so go
                // to the next state.
                if (IsDlgButtonChecked(hDlg, IDC_EXPORT_MDL) == BST_INDETERMINATE)
                {
                    CheckDlgButton(hDlg, IDC_EXPORT_MDL, BST_UNCHECKED);
                    // And, if unchecked, it means IDC_G3D_MATERIAL should be unchecked, too.
                    CheckDlgButton(hDlg, IDC_G3D_MATERIAL, BST_INDETERMINATE);
                }
                else {
                    // else it's determinate, so set G3D_MATERIAL back to its original state upon entry.
                    CheckDlgButton(hDlg, IDC_G3D_MATERIAL, epd.chkCustomMaterial[epd.fileType]);
                }
            }
            else
            {
                CheckDlgButton(hDlg, IDC_EXPORT_MDL, BST_INDETERMINATE);
            }
            break;

        case IDC_MAKE_GROUPS_OBJECTS:
            if (epd.fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ || epd.fileType == FILE_TYPE_WAVEFRONT_REL_OBJ)
            {
                if (IsDlgButtonChecked(hDlg, IDC_MAKE_GROUPS_OBJECTS) == BST_INDETERMINATE)
                {
                    CheckDlgButton(hDlg, IDC_MAKE_GROUPS_OBJECTS, BST_UNCHECKED);
                }
            }
            else
            {
                CheckDlgButton(hDlg, IDC_MAKE_GROUPS_OBJECTS, BST_INDETERMINATE);
            }
            break;

        case IDC_EXPORT_ALL:
            // if printing, special warning; this is the only time we do something special for printing vs. rendering export in this code.
            if (epd.flags & EXPT_3DPRINT) {
                if (IsDlgButtonChecked(hDlg, IDC_EXPORT_ALL) == BST_CHECKED)
                {
                    // depending on file format, explain problems and solutions
                    if (epd.fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ || epd.fileType == FILE_TYPE_WAVEFRONT_REL_OBJ)
                        // Sculpteo
                        MessageBox(NULL, _T("Warning: this checkbox allows tiny features to be exported for 3D printing. Some of these small bits - fences, free-standing signs - may snap off during manufacture. Fattened versions of these objects are used by default, but even these can break. Also, the edge connection and floater checkboxes have been unchecked, since these options can cause problems. Finally, the meshes for some objects have elements that can cause Sculpteo's slicer problems - either visually check the uploaded file carefully or run it through a mesh cleanup system such as Netfabb; old free version: https://github.com/3DprintFIT/netfabb-basic-download."),
                            _T("Warning"), MB_OK | MB_ICONWARNING | MB_SYSTEMMODAL);
                    else if (epd.fileType == FILE_TYPE_VRML2)
                        // Shapeways
                        MessageBox(NULL, _T("Warning: this checkbox allows tiny features to be exported for 3D printing. Some of these small bits - fences, free-standing signs - may snap off during manufacture. Fattened versions of these objects are used by default, but even these can break, so Shapeways may refuse to print the model. Also, the edge connection and floater checkboxes have been unchecked, since these options can cause problems. The one bit of good news is that Shapeways' software will clean up the mesh for you, so at least any geometric inconsistencies will not cause you problems. If you are 3D printing otherwise, you may need to clean up the mesh with a system such as Netfabb; old free version: https://github.com/3DprintFIT/netfabb-basic-download."),
                            _T("Warning"), MB_OK | MB_ICONWARNING | MB_SYSTEMMODAL);
                    else
                        MessageBox(NULL, _T("Warning: this checkbox allows tiny features to be exported for 3D printing. Some of these small bits - fences, free-standing signs - may snap off during manufacture. Fattened versions of these objects are used by default, but even these can break. Also, the edge connection and floater checkboxes have been unchecked, since these options can cause problems. Finally, the meshes for some objects have elements that can cause some 3D printer slicers problems; you may need to clean up the mesh with a system such as Netfabb; old free version: https://github.com/3DprintFIT/netfabb-basic-download."),
                            _T("Warning"), MB_OK | MB_ICONWARNING | MB_SYSTEMMODAL);
                    CheckDlgButton(hDlg, IDC_FATTEN, BST_CHECKED);
                    CheckDlgButton(hDlg, IDC_DELETE_FLOATERS, BST_UNCHECKED);
                    CheckDlgButton(hDlg, IDC_CONNECT_PARTS, BST_UNCHECKED);
                    CheckDlgButton(hDlg, IDC_CONNECT_CORNER_TIPS, BST_INDETERMINATE);
                    CheckDlgButton(hDlg, IDC_CONNECT_ALL_EDGES, BST_INDETERMINATE);
                }
                else if (IsDlgButtonChecked(hDlg, IDC_EXPORT_ALL) == BST_UNCHECKED)
                {
                    // if lesser is toggled back off, turn on the defaults
                    if (IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES)) {
                        MessageBox(NULL, _T("Warning: turning details off changes the export mode to \"Export all textures to three large, mosaic images,\" as the \"Export individual blocks\" export mode is incompatible with full block export. New textures are created that are composites, e.g., fern atop a grass block."),
                            _T("Warning"), MB_OK | MB_ICONWARNING | MB_SYSTEMMODAL);
                        CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MOSAIC_TEXTURES, 1);
                        CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES, 0);
                    }
                    CheckDlgButton(hDlg, IDC_DELETE_FLOATERS, BST_CHECKED);
                    CheckDlgButton(hDlg, IDC_CONNECT_PARTS, BST_CHECKED);
                    CheckDlgButton(hDlg, IDC_CONNECT_CORNER_TIPS, BST_CHECKED);
                    CheckDlgButton(hDlg, IDC_CONNECT_ALL_EDGES, BST_UNCHECKED);
                }
            }
            else {
                // for rendering
                if (IsDlgButtonChecked(hDlg, IDC_EXPORT_ALL) == BST_CHECKED)
                {
                    // all objects export (i.e. lesser) is now on, so for mosaics make compositing checkable, but off, which is the default for rendering
                    CheckDlgButton(hDlg, IDC_COMPOSITE_OVERLAY, IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_MOSAIC_TEXTURES) ? BST_UNCHECKED : BST_INDETERMINATE);
                }
                else if (IsDlgButtonChecked(hDlg, IDC_EXPORT_ALL) == BST_UNCHECKED)
                {
                    if (IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES)) {
                        MessageBox(NULL, _T("Warning: turning details off changes the export mode to \"Export all textures to three large, mosaic images,\" as the \"Export individual blocks\" export mode is incompatible with full block export. New textures are created that are composites, e.g., fern atop a grass block."),
                            _T("Warning"), MB_OK | MB_ICONWARNING | MB_SYSTEMMODAL);
                        CheckDlgButton(hDlg, IDC_RADIO_EXPORT_MOSAIC_TEXTURES, 1);
                        CheckDlgButton(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES, 0);
                    }
                    // definitely make compositing uncheckable at this point - full blocks mean that composite overlay must be used, vs. separate objects
                    CheckDlgButton(hDlg, IDC_COMPOSITE_OVERLAY, BST_INDETERMINATE);

                    // just to be safe:
                    CheckDlgButton(hDlg, IDC_DELETE_FLOATERS, BST_UNCHECKED);
                    CheckDlgButton(hDlg, IDC_CONNECT_PARTS, BST_UNCHECKED);
                    CheckDlgButton(hDlg, IDC_CONNECT_CORNER_TIPS, BST_INDETERMINATE);
                    CheckDlgButton(hDlg, IDC_CONNECT_ALL_EDGES, BST_INDETERMINATE);
                }
            }
            // if we're turning it off, set fatten to indeterminate state
            {
                UINT isLesserChecked = IsDlgButtonChecked(hDlg, IDC_EXPORT_ALL);
                if (!isLesserChecked)
                    CheckDlgButton(hDlg, IDC_FATTEN, BST_INDETERMINATE);
            }
            break;

        case IDC_FATTEN:
        {
            UINT isLesserChecked = IsDlgButtonChecked(hDlg, IDC_EXPORT_ALL);
            if (!isLesserChecked)
            {
                CheckDlgButton(hDlg, IDC_FATTEN, BST_INDETERMINATE);
            }
            else
            {
                UINT isFattenIndeterminate = (IsDlgButtonChecked(hDlg, IDC_FATTEN) == BST_INDETERMINATE);
                if (isFattenIndeterminate)
                    CheckDlgButton(hDlg, IDC_FATTEN, BST_UNCHECKED);
            }
        }
        break;

        case IDC_COMPOSITE_OVERLAY:
            // the indeterminate state is only for when the option is not available (i.e., 3d printing - where it's always on - or tile texture output - where it's not)
            if ((epd.flags & EXPT_3DPRINT) || (IsDlgButtonChecked(hDlg, IDC_EXPORT_ALL) == BST_UNCHECKED) || !IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_MOSAIC_TEXTURES))
            {
                CheckDlgButton(hDlg, IDC_COMPOSITE_OVERLAY, BST_INDETERMINATE);
            }
            else
            {
                // always go to the next state, if we'd normally go to the indeterminate (tri-value) state
                UINT isIndeterminate = (IsDlgButtonChecked(hDlg, IDC_COMPOSITE_OVERLAY) == BST_INDETERMINATE);
                if (isIndeterminate)
                    CheckDlgButton(hDlg, IDC_COMPOSITE_OVERLAY, BST_UNCHECKED);
            }
            break;

        case IDC_SIMPLIFY_MESH:
            // the indeterminate state is only for when the option is not available (i.e., 3d printing - where it's always on - or single texture output - where it's not)
            if ((epd.flags & EXPT_3DPRINT) || !IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES) ||
                IsDlgButtonChecked(hDlg, IDC_INDIVIDUAL_BLOCKS) == BST_CHECKED)
            {
                CheckDlgButton(hDlg, IDC_SIMPLIFY_MESH, BST_INDETERMINATE);
            }
            else
            {
                // always go to the next state, if we'd normally go to the indeterminate (tri-value) state
                UINT isIndeterminate = (IsDlgButtonChecked(hDlg, IDC_SIMPLIFY_MESH) == BST_INDETERMINATE);
                if (isIndeterminate)
                    CheckDlgButton(hDlg, IDC_SIMPLIFY_MESH, BST_UNCHECKED);
            }
            break;

        case IDC_DOUBLED_BILLBOARD:
            // the indeterminate state is only for when the option is not available, i.e., USD
            if (epd.fileType == FILE_TYPE_USD)
            {
                CheckDlgButton(hDlg, IDC_DOUBLED_BILLBOARD, BST_INDETERMINATE);
            }
            else
            {
                UINT isIndeterminate = (IsDlgButtonChecked(hDlg, IDC_DOUBLED_BILLBOARD) == BST_INDETERMINATE);
                if (isIndeterminate)
                    CheckDlgButton(hDlg, IDC_DOUBLED_BILLBOARD, BST_UNCHECKED);
            }
            break;

        case IDC_TREE_LEAVES_SOLID:
            // the indeterminate state is only for when the option is not available (i.e., 3d printing)
            if (epd.flags & EXPT_3DPRINT)
            {
                CheckDlgButton(hDlg, IDC_TREE_LEAVES_SOLID, BST_INDETERMINATE);
            }
            else
            {
                UINT isIndeterminate = (IsDlgButtonChecked(hDlg, IDC_TREE_LEAVES_SOLID) == BST_INDETERMINATE);
                if (isIndeterminate)
                    CheckDlgButton(hDlg, IDC_TREE_LEAVES_SOLID, BST_UNCHECKED);
            }
            break;

        case IDC_BLOCKS_AT_BORDERS:
            // the indeterminate state is only for when the option is not available (i.e., 3d printing)
            if (epd.flags & EXPT_3DPRINT)
            {
                CheckDlgButton(hDlg, IDC_BLOCKS_AT_BORDERS, BST_INDETERMINATE);
            }
            else
            {
                UINT isIndeterminate = (IsDlgButtonChecked(hDlg, IDC_BLOCKS_AT_BORDERS) == BST_INDETERMINATE);
                if (isIndeterminate)
                    CheckDlgButton(hDlg, IDC_BLOCKS_AT_BORDERS, BST_UNCHECKED);
            }
            break;

        case IDC_MODEL_HEIGHT:
            // a bit sleazy: if we get focus, then get that the box is changing, change radio button to that choice.
            // There's probably a good way to do this, but I don't know it.
            // The problem is EN_CHANGE happens when IDC_BLOCK_SIZE is first set, and we don't want to do this then
            if (HIWORD(wParam) == EN_SETFOCUS)
            {
                focus = IDC_MODEL_HEIGHT;
            }
            else if ((HIWORD(wParam) == EN_CHANGE) && (focus == IDC_MODEL_HEIGHT))
            {
                epd.radioScaleByBlock = epd.radioScaleToMaterial = epd.radioScaleByCost = 0;
                epd.radioScaleToHeight = 1;
                CheckDlgButton(hDlg, IDC_RADIO_SCALE_TO_HEIGHT, epd.radioScaleToHeight);
                CheckDlgButton(hDlg, IDC_RADIO_SCALE_TO_MATERIAL, epd.radioScaleToMaterial);
                CheckDlgButton(hDlg, IDC_RADIO_SCALE_BY_BLOCK, epd.radioScaleByBlock);
                CheckDlgButton(hDlg, IDC_RADIO_SCALE_BY_COST, epd.radioScaleByCost);
            }
            break;

        case IDC_BLOCK_SIZE:
            // a bit sleazy: if we get focus, then get that the box is changing, change radio button to that choice.
            // There's probably a good way to do this, but I don't know it.
            // The problem is EN_CHANGE happens when IDC_BLOCK_SIZE is first set, and we don't want to do this then
            if (HIWORD(wParam) == EN_SETFOCUS)
            {
                focus = IDC_BLOCK_SIZE;
            }
            else if ((HIWORD(wParam) == EN_CHANGE) && (focus == IDC_BLOCK_SIZE))
            {
                epd.radioScaleToHeight = epd.radioScaleToMaterial = epd.radioScaleByCost = 0;
                epd.radioScaleByBlock = 1;
                CheckDlgButton(hDlg, IDC_RADIO_SCALE_TO_HEIGHT, epd.radioScaleToHeight);
                CheckDlgButton(hDlg, IDC_RADIO_SCALE_TO_MATERIAL, epd.radioScaleToMaterial);
                CheckDlgButton(hDlg, IDC_RADIO_SCALE_BY_BLOCK, epd.radioScaleByBlock);
                CheckDlgButton(hDlg, IDC_RADIO_SCALE_BY_COST, epd.radioScaleByCost);
            }
            break;

        case IDC_SCALE_LIGHTS:
            if (HIWORD(wParam) == EN_SETFOCUS)
            {
                focus = IDC_SCALE_LIGHTS;
            }
            //else if ((HIWORD(wParam) == EN_CHANGE) && (focus == IDC_SCALE_LIGHTS))
            break;

        case IDC_SCALE_EMITTERS:
            if (HIWORD(wParam) == EN_SETFOCUS)
            {
                focus = IDC_SCALE_EMITTERS;
            }
            //else if ((HIWORD(wParam) == EN_CHANGE) && (focus == IDC_SCALE_EMITTERS))
            break;

        case IDC_COST:
            // a bit sleazy: if we get focus, then get that the box is changing, change radio button to that choice.
            // There's probably a good way to do this, but I don't know it.
            // The problem is EN_CHANGE happens when IDC_COST is first set, and we don't want to do this then
            if (HIWORD(wParam) == EN_SETFOCUS)
            {
                focus = IDC_COST;
            }
            else if ((HIWORD(wParam) == EN_CHANGE) && (focus == IDC_COST))
            {
                epd.radioScaleToHeight = epd.radioScaleToMaterial = epd.radioScaleByBlock = 0;
                epd.radioScaleByCost = 1;
                CheckDlgButton(hDlg, IDC_RADIO_SCALE_TO_HEIGHT, epd.radioScaleToHeight);
                CheckDlgButton(hDlg, IDC_RADIO_SCALE_TO_MATERIAL, epd.radioScaleToMaterial);
                CheckDlgButton(hDlg, IDC_RADIO_SCALE_BY_BLOCK, epd.radioScaleByBlock);
                CheckDlgButton(hDlg, IDC_RADIO_SCALE_BY_COST, epd.radioScaleByCost);
            }
            break;

        case IDC_FLOAT_COUNT:
            // a bit sleazy: if we get focus, then get that the box is changing, change check button to that choice.
            // There's probably a good way to do this, but I don't know it.
            // The problem is EN_CHANGE happens when IDC_FLOAT_COUNT is first set, and we don't want to do this then
            if (HIWORD(wParam) == EN_SETFOCUS)
            {
                focus = IDC_FLOAT_COUNT;
            }
            else if ((HIWORD(wParam) == EN_CHANGE) && (focus == IDC_FLOAT_COUNT))
            {
                epd.chkDeleteFloaters = 1;
                CheckDlgButton(hDlg, IDC_DELETE_FLOATERS, epd.chkDeleteFloaters);
            }
            break;

        case IDC_HOLLOW_THICKNESS:
            // a bit sleazy: if we get focus, then get that the box is changing, change check button to that choice.
            // There's probably a good way to do this, but I don't know it.
            // The problem is EN_CHANGE happens when IDC_HOLLOW_THICKNESS is first set, and we don't want to do this then
            if (HIWORD(wParam) == EN_SETFOCUS)
            {
                focus = IDC_HOLLOW_THICKNESS;
            }
            else if ((HIWORD(wParam) == EN_CHANGE) && (focus == IDC_HOLLOW_THICKNESS))
            {
                epd.chkHollow[epd.fileType] = 1;
                CheckDlgButton(hDlg, IDC_HOLLOW, epd.chkHollow[epd.fileType]);
            }
            break;

        case IDOK:
        {
            gOK = 1;
            ExportFileData lepd;
            lepd = epd;

            // suck all the data out to a local copy
            GetDlgItemTextA(hDlg, IDC_WORLD_MIN_X, lepd.minxString, EP_FIELD_LENGTH);
            GetDlgItemTextA(hDlg, IDC_WORLD_MIN_Y, lepd.minyString, EP_FIELD_LENGTH);
            GetDlgItemTextA(hDlg, IDC_WORLD_MIN_Z, lepd.minzString, EP_FIELD_LENGTH);
            GetDlgItemTextA(hDlg, IDC_WORLD_MAX_X, lepd.maxxString, EP_FIELD_LENGTH);
            GetDlgItemTextA(hDlg, IDC_WORLD_MAX_Y, lepd.maxyString, EP_FIELD_LENGTH);
            GetDlgItemTextA(hDlg, IDC_WORLD_MAX_Z, lepd.maxzString, EP_FIELD_LENGTH);

            lepd.chkCreateZip[lepd.fileType] = (IsDlgButtonChecked(hDlg, IDC_CREATE_ZIP) == BST_CHECKED);
            lepd.chkCreateModelFiles[lepd.fileType] = (IsDlgButtonChecked(hDlg, IDC_CREATE_FILES) == BST_CHECKED);

            lepd.radioExportNoMaterials[lepd.fileType] = IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_NO_MATERIALS);
            lepd.radioExportMtlColors[lepd.fileType] = IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_MTL_COLORS_ONLY);
            lepd.radioExportSolidTexture[lepd.fileType] = IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_SOLID_TEXTURES);
            lepd.radioExportFullTexture[lepd.fileType] = IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_MOSAIC_TEXTURES);
            lepd.radioExportTileTextures[lepd.fileType] = IsDlgButtonChecked(hDlg, IDC_RADIO_EXPORT_SEPARATE_TILES);
            GetDlgItemTextA(hDlg, IDC_TILE_DIR, lepd.tileDirString, MAX_PATH);

            lepd.chkTextureRGB = (IsDlgButtonChecked(hDlg, IDC_TEXTURE_RGB) == BST_CHECKED);
            lepd.chkTextureA = (IsDlgButtonChecked(hDlg, IDC_TEXTURE_A) == BST_CHECKED);
            lepd.chkTextureRGBA = (IsDlgButtonChecked(hDlg, IDC_TEXTURE_RGBA) == BST_CHECKED);

            // OBJ options
            if (epd.fileType == FILE_TYPE_WAVEFRONT_ABS_OBJ || epd.fileType == FILE_TYPE_WAVEFRONT_REL_OBJ)
            {
                lepd.chkMakeGroupsObjects = (IsDlgButtonChecked(hDlg, IDC_MAKE_GROUPS_OBJECTS) == BST_CHECKED);
                lepd.chkSeparateTypes = (IsDlgButtonChecked(hDlg, IDC_SEPARATE_TYPES) == BST_CHECKED);
                // leave this option checked, the default, if it's grayed out (unused for individual textures).
                // In this way, if the user switches to mosaics, it will be on.
                lepd.chkMaterialPerFamily = (IsDlgButtonChecked(hDlg, IDC_MATERIAL_PER_BLOCK_FAMILY) != BST_UNCHECKED);
                lepd.chkSplitByBlockType = (IsDlgButtonChecked(hDlg, IDC_SPLIT_BY_BLOCK_TYPE) == BST_CHECKED);
                lepd.chkCustomMaterial[epd.fileType] = (IsDlgButtonChecked(hDlg, IDC_G3D_MATERIAL) == BST_CHECKED);
            }
            else
            {
                // when exporting to another format, for properties only relevant to OBJ, restore state (should not be changed)
                lepd.chkMakeGroupsObjects = origEpd.chkMakeGroupsObjects;
                lepd.chkSeparateTypes = origEpd.chkSeparateTypes;
                lepd.chkMaterialPerFamily = origEpd.chkMaterialPerFamily;
                lepd.chkSplitByBlockType = origEpd.chkSplitByBlockType;
                if (epd.fileType == FILE_TYPE_USD) {
                    lepd.chkExportMDL = (IsDlgButtonChecked(hDlg, IDC_EXPORT_MDL) == BST_CHECKED);
                    lepd.chkCustomMaterial[epd.fileType] = (IsDlgButtonChecked(hDlg, IDC_G3D_MATERIAL) == BST_CHECKED);
                    GetDlgItemTextA(hDlg, IDC_SCALE_LIGHTS, lepd.scaleLightsString, EP_FIELD_LENGTH);
                    GetDlgItemTextA(hDlg, IDC_SCALE_EMITTERS, lepd.scaleEmittersString, EP_FIELD_LENGTH);
                }
                else
                    lepd.chkCustomMaterial[epd.fileType] = origEpd.chkCustomMaterial[epd.fileType];
            }
            // 3D printing should never use this option.
            lepd.chkIndividualBlocks[epd.fileType] = (epd.flags & EXPT_3DPRINT) ? 0 : (IsDlgButtonChecked(hDlg, IDC_INDIVIDUAL_BLOCKS) == BST_CHECKED);

            //lepd.chkMergeFlattop = IsDlgButtonChecked(hDlg,IDC_MERGE_FLATTOP);
            lepd.chkMakeZUp[lepd.fileType] = (IsDlgButtonChecked(hDlg, IDC_MAKE_Z_UP) == BST_CHECKED);
            lepd.chkCenterModel = (IsDlgButtonChecked(hDlg, IDC_CENTER_MODEL) == BST_CHECKED);
            // if 3D printing, or if lesser blocks is off, do composite overlay, where we make a new tile (things break otherwise)
            lepd.chkCompositeOverlay = (epd.flags & EXPT_3DPRINT) ? 1 :
                ((IsDlgButtonChecked(hDlg, IDC_COMPOSITE_OVERLAY) == BST_CHECKED) || (IsDlgButtonChecked(hDlg, IDC_EXPORT_ALL) != BST_CHECKED));
            lepd.chkDecimate = (IsDlgButtonChecked(hDlg, IDC_SIMPLIFY_MESH) == BST_CHECKED);

            // state doesn't really matter for USD, but set it anyway
            lepd.chkDoubledBillboards = (lepd.fileType == FILE_TYPE_USD) ? false : (IsDlgButtonChecked(hDlg, IDC_DOUBLED_BILLBOARD) == BST_CHECKED);
            // solid leaves and faces at borders always true for 3D printing.
            lepd.chkLeavesSolid = (epd.flags & EXPT_3DPRINT) ? 1 : (IsDlgButtonChecked(hDlg, IDC_TREE_LEAVES_SOLID) == BST_CHECKED);
            lepd.chkBlockFacesAtBorders = (epd.flags & EXPT_3DPRINT) ? 1 : (IsDlgButtonChecked(hDlg, IDC_BLOCKS_AT_BORDERS) == BST_CHECKED);
            lepd.chkBiome = (IsDlgButtonChecked(hDlg, IDC_BIOME) == BST_CHECKED);

            lepd.radioRotate0 = IsDlgButtonChecked(hDlg, IDC_RADIO_ROTATE_0);
            lepd.radioRotate90 = IsDlgButtonChecked(hDlg, IDC_RADIO_ROTATE_90);
            lepd.radioRotate180 = IsDlgButtonChecked(hDlg, IDC_RADIO_ROTATE_180);
            lepd.radioRotate270 = IsDlgButtonChecked(hDlg, IDC_RADIO_ROTATE_270);

            lepd.radioScaleToHeight = IsDlgButtonChecked(hDlg, IDC_RADIO_SCALE_TO_HEIGHT);
            lepd.radioScaleToMaterial = IsDlgButtonChecked(hDlg, IDC_RADIO_SCALE_TO_MATERIAL);
            lepd.radioScaleByBlock = IsDlgButtonChecked(hDlg, IDC_RADIO_SCALE_BY_BLOCK);
            lepd.radioScaleByCost = IsDlgButtonChecked(hDlg, IDC_RADIO_SCALE_BY_COST);

            GetDlgItemTextA(hDlg, IDC_MODEL_HEIGHT, lepd.modelHeightString, EP_FIELD_LENGTH);
            GetDlgItemTextA(hDlg, IDC_BLOCK_SIZE, lepd.blockSizeString, EP_FIELD_LENGTH);
            GetDlgItemTextA(hDlg, IDC_COST, lepd.costString, EP_FIELD_LENGTH);

            lepd.chkFillBubbles = (IsDlgButtonChecked(hDlg, IDC_FILL_BUBBLES) == BST_CHECKED);
            // if filling bubbles is off, sealing entrances does nothing at all
            lepd.chkSealEntrances = lepd.chkFillBubbles ? (IsDlgButtonChecked(hDlg, IDC_SEAL_ENTRANCES) == BST_CHECKED) : 0;
            lepd.chkSealSideTunnels = lepd.chkFillBubbles ? (IsDlgButtonChecked(hDlg, IDC_SEAL_SIDE_TUNNELS) == BST_CHECKED) : 0;

            lepd.chkConnectParts = (IsDlgButtonChecked(hDlg, IDC_CONNECT_PARTS) == BST_CHECKED);
            // if connect parts is off, corner tips and edges is off
            lepd.chkConnectCornerTips = lepd.chkConnectParts ? (IsDlgButtonChecked(hDlg, IDC_CONNECT_CORNER_TIPS) == BST_CHECKED) : 0;
            lepd.chkConnectAllEdges = lepd.chkConnectParts ? (IsDlgButtonChecked(hDlg, IDC_CONNECT_ALL_EDGES) == BST_CHECKED) : 0;

            lepd.chkDeleteFloaters = (IsDlgButtonChecked(hDlg, IDC_DELETE_FLOATERS) == BST_CHECKED);

            lepd.chkHollow[epd.fileType] = (IsDlgButtonChecked(hDlg, IDC_HOLLOW) == BST_CHECKED);
            // if hollow is off, superhollow is off
            lepd.chkSuperHollow[epd.fileType] = lepd.chkHollow[epd.fileType] ? (IsDlgButtonChecked(hDlg, IDC_SUPER_HOLLOW) == BST_CHECKED) : 0;

            lepd.chkMeltSnow = (IsDlgButtonChecked(hDlg, IDC_MELT_SNOW) == BST_CHECKED);

            GetDlgItemTextA(hDlg, IDC_FLOAT_COUNT, lepd.floaterCountString, EP_FIELD_LENGTH);
            GetDlgItemTextA(hDlg, IDC_HOLLOW_THICKNESS, lepd.hollowThicknessString, EP_FIELD_LENGTH);

            lepd.chkExportAll = (IsDlgButtonChecked(hDlg, IDC_EXPORT_ALL) == BST_CHECKED);
            lepd.chkFatten = lepd.chkExportAll ? (IsDlgButtonChecked(hDlg, IDC_FATTEN) == BST_CHECKED) : 0;

            BOOL debugAvailable = !lepd.radioExportNoMaterials[lepd.fileType] && (lepd.fileType != FILE_TYPE_ASCII_STL);
            lepd.chkShowParts = debugAvailable ? (IsDlgButtonChecked(hDlg, IDC_SHOW_PARTS) == BST_CHECKED) : 0;
            lepd.chkShowWelds = debugAvailable ? (IsDlgButtonChecked(hDlg, IDC_SHOW_WELDS) == BST_CHECKED) : 0;

            lepd.comboPhysicalMaterial[lepd.fileType] = (int)SendDlgItemMessage(hDlg, IDC_COMBO_PHYSICAL_MATERIAL, CB_GETCURSEL, 0, 0);
            lepd.comboModelUnits[lepd.fileType] = (int)SendDlgItemMessage(hDlg, IDC_COMBO_MODELS_UNITS, CB_GETCURSEL, 0, 0);

            // check for non-file characters - cannot be a path, either
            char badchar[] = "<>|?*:/\\";
            bool badcharFound = false;
            for (int i = 0; i < (int)strlen(badchar); i++) {
                if (strchr(lepd.tileDirString, badchar[i]) != NULL) {
                    badcharFound = true;
                }
            }
            if (badcharFound) {
                MessageBox(NULL,
                    _T("Illegal character <>|?*:/\\ detected in output tile directory name; this name cannot be a path, but just a simple folder name.\nYou must fix this, then hit OK again."), _T("Folder name character error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                return (INT_PTR)FALSE;
            }

            int nc;
            nc = sscanf_s(lepd.minxString, "%d", &lepd.minxVal);
            nc &= sscanf_s(lepd.minyString, "%d", &lepd.minyVal);
            nc &= sscanf_s(lepd.minzString, "%d", &lepd.minzVal);
            nc &= sscanf_s(lepd.maxxString, "%d", &lepd.maxxVal);
            nc &= sscanf_s(lepd.maxyString, "%d", &lepd.maxyVal);
            nc &= sscanf_s(lepd.maxzString, "%d", &lepd.maxzVal);

            nc &= sscanf_s(lepd.modelHeightString, "%f", &lepd.modelHeightVal);
            nc &= sscanf_s(lepd.blockSizeString, "%f", &lepd.blockSizeVal[lepd.fileType]);
            nc &= sscanf_s(lepd.costString, "%f", &lepd.costVal);

            nc &= sscanf_s(lepd.floaterCountString, "%d", &lepd.floaterCountVal);
            nc &= sscanf_s(lepd.hollowThicknessString, "%g", &lepd.hollowThicknessVal[epd.fileType]);

            if (lepd.fileType == FILE_TYPE_USD) {
                nc &= sscanf_s(lepd.scaleLightsString, "%g", &lepd.scaleLightsVal);
                nc &= sscanf_s(lepd.scaleEmittersString, "%g", &lepd.scaleEmittersVal);
            }

            // this is a bit lazy checking all errors here, there's probably a better way
            // to test as we go, but this sort of thing should be rare
            if (nc == 0)
            {
                MessageBox(NULL,
                    _T("Bad (non-numeric) value detected in options dialog;\nYou need to clean up, then hit OK again."), _T("Non-numeric value error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                return (INT_PTR)FALSE;
            }

            if (lepd.radioScaleToHeight && lepd.modelHeightVal <= 0.0f)
            {
                MessageBox(NULL,
                    _T("Model height must be a positive number;\nYou need to fix this, then hit OK again."), _T("Value error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                return (INT_PTR)FALSE;
            }

            if (lepd.radioScaleByBlock && lepd.blockSizeVal[lepd.fileType] <= 0.0f)
            {
                MessageBox(NULL,
                    _T("Block size must be a positive number;\nYou need to fix this, then hit OK again."), _T("Value error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                return (INT_PTR)FALSE;
            }

            if (lepd.fileType == FILE_TYPE_USD && lepd.scaleLightsVal < 0.0f)
            {
                MessageBox(NULL,
                    _T("Light scale must be a non-negative number;\nYou need to fix this, then hit OK again."), _T("Value error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                return (INT_PTR)FALSE;
            }
            if (lepd.fileType == FILE_TYPE_USD && lepd.scaleEmittersVal < 0.0f)
            {
                MessageBox(NULL,
                    _T("Surface emit scale must be a non-negative number;\nYou need to fix this, then hit OK again."), _T("Value error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                return (INT_PTR)FALSE;
            }

            if (lepd.radioScaleByCost)
            {
                // white vs. colored stuff: $1.50 vs. $3.00 handling fees, plus some minimum amount of material
                // We need to find out the minimum amount rules for white material; colored is at
                // http://www.shapeways.com/design-rules/full_color_sandstone, and we use the
                // "the dimensions have to add up to 65mm" and assume a 3mm block size to give a 59mm*3mm*3mm volume
                // minimum, times $0.75/cm^3 gives $0.40.
                if (lepd.costVal <= (gMtlCostTable[curPhysMaterial].costHandling + gMtlCostTable[curPhysMaterial].costMinimum))
                {
                    MessageBox(NULL,
                        _T("The cost must be > $1.55 for colorless, > $3.40 for color;\nYou need to fix this, then hit OK again."), _T("Value error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                    return (INT_PTR)FALSE;
                }
            }

            if (lepd.chkDeleteFloaters && lepd.floaterCountVal < 0)
            {
                MessageBox(NULL,
                    _T("Floating objects deletion value cannot be negative;\nYou need to fix this, then hit OK again."), _T("Value error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
                return (INT_PTR)FALSE;
            }

            if (lepd.chkHollow[epd.fileType] && lepd.hollowThicknessVal[epd.fileType] < 0.0)
            {
                MessageBox(NULL,
                    _T("Hollow thickness value cannot be negative;\nYou need to fix this, then hit OK again."), _T("Value error"), MB_OK | MB_ICONERROR | MB_SYSTEMMODAL);
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

// Description:
//   Creates a tooltip for an item in a dialog box. 
// Parameters:
//   idTool - identifier of an dialog box item.
//   nDlg - window handle of the dialog box.
//   pszText - string to use as the tooltip text.
// Returns:
//   The handle to the tooltip.
//
HWND CreateToolTip(int toolID, HINSTANCE hInst, HWND hDlg, PTSTR pszText)
{
    if (!toolID || !hDlg || !pszText)
    {
        return FALSE;
    }
    // Get the window of the tool.
    HWND hwndTool = GetDlgItem(hDlg, toolID);

    // Create the tooltip.
    HWND hwndTip = CreateWindowEx(NULL, TOOLTIPS_CLASS, NULL,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, // TTS_BALLOON to get balloon look, which I don't like
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        hDlg, NULL,
        hInst, NULL);

    if (!hwndTool || !hwndTip)
    {
        return (HWND)NULL;
    }

    // Associate the tooltip with the tool.
    TOOLINFO toolInfo = { 0 };
    // magic setting that makes the tooltip work!
    toolInfo.cbSize = TTTOOLINFO_V1_SIZE; // sizeof(toolInfo); // why TTTOOLINFO_V1_SIZE see https://social.msdn.microsoft.com/Forums/sqlserver/en-US/4e74bd31-d3bb-4c0a-9f16-5457991b4a0b/win32-how-use-the-tooltip-control?forum=vclanguage
    toolInfo.hwnd = hDlg;
    toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    toolInfo.uId = (UINT_PTR)hwndTool;
    toolInfo.lpszText = pszText;
    SendMessage(hwndTip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);

    return hwndTip;
}
