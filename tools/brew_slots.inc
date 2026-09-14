// GERADO por tools/gerar_slots.py a partir dos cabecalhos do SDK.
// NAO EDITAR A MAO: corre o gerador. Um numero escrito a mao ja divergiu
// uma vez, e o custo foi uma ronda inteira a olhar para o sitio errado.
//
// `INHERIT_IBase` ocupa os slots 0 e 1 (AddRef, Release). NAO ha
// QueryInterface em INHERIT_IBase -- foi essa a origem do erro de um.
#pragma once

namespace brew_slots {
// platform/system/inc/AEEIShell.h
//   53 metodos; slot = 2 + posicao
constexpr unsigned kShell_CreateInstance = 2;
constexpr unsigned kShell_QueryClass = 3;
constexpr unsigned kShell_GetDeviceInfo = 4;
constexpr unsigned kShell_StartApplet = 5;
constexpr unsigned kShell_CloseApplet = 6;
constexpr unsigned kShell_CanStartApplet = 7;
constexpr unsigned kShell_ActiveApplet = 8;
constexpr unsigned kShell_EnumAppletInit = 9;
constexpr unsigned kShell_EnumNextApplet = 10;
constexpr unsigned kShell_SetTimer = 11;
constexpr unsigned kShell_CancelTimer = 13;
constexpr unsigned kShell_GetTimerExpiration = 15;
constexpr unsigned kShell_CreateDialog = 17;
constexpr unsigned kShell_GetActiveDialog = 18;
constexpr unsigned kShell_EndDialog = 19;
constexpr unsigned kShell_LoadResString = 20;
constexpr unsigned kShell_LoadResData = 21;
constexpr unsigned kShell_LoadResObject = 22;
constexpr unsigned kShell_FreeResData = 23;
constexpr unsigned kShell_SendEvent = 24;
constexpr unsigned kShell_Beep = 25;
constexpr unsigned kShell_GetPrefs = 26;
constexpr unsigned kShell_SetPrefs = 27;
constexpr unsigned kShell_GetItemStyle = 28;
constexpr unsigned kShell_Prompt = 29;
constexpr unsigned kShell_MessageBox = 30;
constexpr unsigned kShell_MessageBoxText = 31;
constexpr unsigned kShell_SetAlarm = 32;
constexpr unsigned kShell_CancelAlarm = 33;
constexpr unsigned kShell_AlarmsActive = 34;
constexpr unsigned kShell_GetHandler = 35;
constexpr unsigned kShell_RegisterHandler = 36;
constexpr unsigned kShell_RegisterNotify = 37;
constexpr unsigned kShell_Notify = 38;
constexpr unsigned kShell_Resume = 39;
constexpr unsigned kShell_ForceExit = 40;
constexpr unsigned kShell_GetPosition = 41;
constexpr unsigned kShell_CheckPrivLevel = 42;
constexpr unsigned kShell_IsValidResource = 43;
constexpr unsigned kShell_LoadResDataEx = 44;
constexpr unsigned kShell_RegisterSystemCallback = 45;
constexpr unsigned kShell_DetectType = 46;
constexpr unsigned kShell_GetDeviceInfoEx = 47;
constexpr unsigned kShell_GetClassItemID = 48;
constexpr unsigned kShell_Obsolete = 49;
constexpr unsigned kShell_GetProperty = 50;
constexpr unsigned kShell_SetProperty = 51;
constexpr unsigned kShell_RegisterEvent = 52;
constexpr unsigned kShell_Reset = 53;
constexpr unsigned kShell_AppIsInGroup = 54;

// platform/ui/inc/AEEIDisplay.h
//   24 metodos; slot = 2 + posicao
constexpr unsigned kDisplay_GetFontMetrics = 2;
constexpr unsigned kDisplay_MeasureTextEx = 3;
constexpr unsigned kDisplay_DrawText = 4;
constexpr unsigned kDisplay_DrawRect = 5;
constexpr unsigned kDisplay_BitBlt = 6;
constexpr unsigned kDisplay_Update = 7;
constexpr unsigned kDisplay_SetAnnunciators = 8;
constexpr unsigned kDisplay_Backlight = 9;
constexpr unsigned kDisplay_SetColor = 10;
constexpr unsigned kDisplay_GetSymbol = 11;
constexpr unsigned kDisplay_DrawFrame = 12;
constexpr unsigned kDisplay_CreateDIBitmap = 13;
constexpr unsigned kDisplay_SetDestination = 14;
constexpr unsigned kDisplay_GetDestination = 15;
constexpr unsigned kDisplay_GetDeviceBitmap = 16;
constexpr unsigned kDisplay_SetFont = 17;
constexpr unsigned kDisplay_SetClipRect = 18;
constexpr unsigned kDisplay_GetClipRect = 19;
constexpr unsigned kDisplay_Clone = 20;
constexpr unsigned kDisplay_MakeDefault = 21;
constexpr unsigned kDisplay_IsEnabled = 22;
constexpr unsigned kDisplay_NotifyEnable = 23;
constexpr unsigned kDisplay_CreateDIBitmapEx = 24;
constexpr unsigned kDisplay_SetPrefs = 25;

// platform/deprecated/inc/AEEFile.h
//   19 metodos; slot = 2 + posicao
constexpr unsigned kFileMgr_OpenFile = 2;
constexpr unsigned kFileMgr_GetInfo = 3;
constexpr unsigned kFileMgr_Remove = 4;
constexpr unsigned kFileMgr_MkDir = 5;
constexpr unsigned kFileMgr_RmDir = 6;
constexpr unsigned kFileMgr_Test = 7;
constexpr unsigned kFileMgr_GetFreeSpace = 8;
constexpr unsigned kFileMgr_GetLastError = 9;
constexpr unsigned kFileMgr_EnumInit = 10;
constexpr unsigned kFileMgr_EnumNext = 11;
constexpr unsigned kFileMgr_Rename = 12;
constexpr unsigned kFileMgr_EnumNextEx = 13;
constexpr unsigned kFileMgr_SetDescription = 14;
constexpr unsigned kFileMgr_GetInfoEx = 15;
constexpr unsigned kFileMgr_Use = 16;
constexpr unsigned kFileMgr_GetFileUseInfo = 17;
constexpr unsigned kFileMgr_ResolvePath = 18;
constexpr unsigned kFileMgr_CheckPathAccess = 19;
constexpr unsigned kFileMgr_GetFreeSpaceEx = 20;

// platform/hardware/inc/AEEIHIDDevice.h
//   16 metodos; slot = 2 + posicao
constexpr unsigned kHIDDevice_GetDeviceInfo = 2;
constexpr unsigned kHIDDevice_GetDeviceStatus = 3;
constexpr unsigned kHIDDevice_RegisterForStatusChange = 4;
constexpr unsigned kHIDDevice_GetButtonInfo = 5;
constexpr unsigned kHIDDevice_GetNumberOfButtons = 6;
constexpr unsigned kHIDDevice_RegisterForButtonEvent = 7;
constexpr unsigned kHIDDevice_GetNextButtonEvent = 8;
constexpr unsigned kHIDDevice_GetPositionState = 9;
constexpr unsigned kHIDDevice_GetMinPositionInfo = 10;
constexpr unsigned kHIDDevice_GetMaxPositionInfo = 11;
constexpr unsigned kHIDDevice_GetAxesInfo = 12;
constexpr unsigned kHIDDevice_RegisterForPositionChange = 13;
constexpr unsigned kHIDDevice_SetExclusiveLevel = 14;
constexpr unsigned kHIDDevice_GetExclusiveLevel = 15;
constexpr unsigned kHIDDevice_Rumble = 16;
constexpr unsigned kHIDDevice_GetRumbleStatus = 17;

// platform/system/inc/OEM/AEEIHeap1.h
//   9 metodos; slot = 2 + posicao
constexpr unsigned kHeap1_Lock = 2;
constexpr unsigned kHeap1_Unlock = 3;
constexpr unsigned kHeap1_Sentinel = 4;
constexpr unsigned kHeap1_NodeInfo = 5;
constexpr unsigned kHeap1_Walk = 6;
constexpr unsigned kHeap1_Flags = 7;
constexpr unsigned kHeap1_Compress = 8;
constexpr unsigned kHeap1_Tag = 9;
constexpr unsigned kHeap1_Statistics = 10;

}  // namespace brew_slots
