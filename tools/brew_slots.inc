// GERADO por tools/gerar_slots.py a partir dos cabecalhos do SDK.
// NAO EDITAR A MAO: corre o gerador. Um numero escrito a mao ja divergiu
// uma vez, e o custo foi uma ronda inteira a olhar para o sitio errado.
//
// `INHERIT_IBase` ocupa os slots 0 e 1 (AddRef, Release). NAO ha
// QueryInterface em INHERIT_IBase -- foi essa a origem do erro de um.
#pragma once

namespace brew_slots {
// platform/system/inc/AEEIShell.h
//   cabeca: INHERIT_IBase = 2 slots de cabeca
//   50 metodos; slot = 2 + posicao
constexpr unsigned kShell_AddRef = 0;
constexpr unsigned kShell_Release = 1;
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
constexpr unsigned kShell_CancelTimer = 12;
constexpr unsigned kShell_GetTimerExpiration = 13;
constexpr unsigned kShell_CreateDialog = 14;
constexpr unsigned kShell_GetActiveDialog = 15;
constexpr unsigned kShell_EndDialog = 16;
constexpr unsigned kShell_LoadResString = 17;
constexpr unsigned kShell_LoadResData = 18;
constexpr unsigned kShell_LoadResObject = 19;
constexpr unsigned kShell_FreeResData = 20;
constexpr unsigned kShell_SendEvent = 21;
constexpr unsigned kShell_Beep = 22;
constexpr unsigned kShell_GetPrefs = 23;
constexpr unsigned kShell_SetPrefs = 24;
constexpr unsigned kShell_GetItemStyle = 25;
constexpr unsigned kShell_Prompt = 26;
constexpr unsigned kShell_MessageBox = 27;
constexpr unsigned kShell_MessageBoxText = 28;
constexpr unsigned kShell_SetAlarm = 29;
constexpr unsigned kShell_CancelAlarm = 30;
constexpr unsigned kShell_AlarmsActive = 31;
constexpr unsigned kShell_GetHandler = 32;
constexpr unsigned kShell_RegisterHandler = 33;
constexpr unsigned kShell_RegisterNotify = 34;
constexpr unsigned kShell_Notify = 35;
constexpr unsigned kShell_Resume = 36;
constexpr unsigned kShell_ForceExit = 37;
constexpr unsigned kShell_GetPosition = 38;
constexpr unsigned kShell_CheckPrivLevel = 39;
constexpr unsigned kShell_IsValidResource = 40;
constexpr unsigned kShell_LoadResDataEx = 41;
constexpr unsigned kShell_RegisterSystemCallback = 42;
constexpr unsigned kShell_DetectType = 43;
constexpr unsigned kShell_GetDeviceInfoEx = 44;
constexpr unsigned kShell_GetClassItemID = 45;
constexpr unsigned kShell_Obsolete = 46;
constexpr unsigned kShell_GetProperty = 47;
constexpr unsigned kShell_SetProperty = 48;
constexpr unsigned kShell_RegisterEvent = 49;
constexpr unsigned kShell_Reset = 50;
constexpr unsigned kShell_AppIsInGroup = 51;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeShell` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kShellSlots = 52;

inline const char* NomeDeShell(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "CreateInstance", "QueryClass", "GetDeviceInfo", "StartApplet", "CloseApplet", "CanStartApplet", "ActiveApplet", "EnumAppletInit", "EnumNextApplet", "SetTimer", "CancelTimer", "GetTimerExpiration", "CreateDialog", "GetActiveDialog", "EndDialog", "LoadResString", "LoadResData", "LoadResObject", "FreeResData", "SendEvent", "Beep", "GetPrefs", "SetPrefs", "GetItemStyle", "Prompt", "MessageBox", "MessageBoxText", "SetAlarm", "CancelAlarm", "AlarmsActive", "GetHandler", "RegisterHandler", "RegisterNotify", "Notify", "Resume", "ForceExit", "GetPosition", "CheckPrivLevel", "IsValidResource", "LoadResDataEx", "RegisterSystemCallback", "DetectType", "GetDeviceInfoEx", "GetClassItemID", "Obsolete", "GetProperty", "SetProperty", "RegisterEvent", "Reset", "AppIsInGroup",
  };
  return slot < 52 ? k[slot] : "slot_fora_da_tabela";
}

// platform/ui/inc/AEEIDisplay.h
//   cabeca: INHERIT_IBase = 2 slots de cabeca
//   24 metodos; slot = 2 + posicao
constexpr unsigned kDisplay_AddRef = 0;
constexpr unsigned kDisplay_Release = 1;
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
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeDisplay` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kDisplaySlots = 26;

inline const char* NomeDeDisplay(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "GetFontMetrics", "MeasureTextEx", "DrawText", "DrawRect", "BitBlt", "Update", "SetAnnunciators", "Backlight", "SetColor", "GetSymbol", "DrawFrame", "CreateDIBitmap", "SetDestination", "GetDestination", "GetDeviceBitmap", "SetFont", "SetClipRect", "GetClipRect", "Clone", "MakeDefault", "IsEnabled", "NotifyEnable", "CreateDIBitmapEx", "SetPrefs",
  };
  return slot < 26 ? k[slot] : "slot_fora_da_tabela";
}

// platform/deprecated/inc/AEEFile.h
//   cabeca: INHERIT_IBase = 2 slots de cabeca
//   19 metodos; slot = 2 + posicao
constexpr unsigned kFileMgr_AddRef = 0;
constexpr unsigned kFileMgr_Release = 1;
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
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeFileMgr` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kFileMgrSlots = 21;

inline const char* NomeDeFileMgr(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "OpenFile", "GetInfo", "Remove", "MkDir", "RmDir", "Test", "GetFreeSpace", "GetLastError", "EnumInit", "EnumNext", "Rename", "EnumNextEx", "SetDescription", "GetInfoEx", "Use", "GetFileUseInfo", "ResolvePath", "CheckPathAccess", "GetFreeSpaceEx",
  };
  return slot < 21 ? k[slot] : "slot_fora_da_tabela";
}

// platform/hardware/inc/AEEIHIDDevice.h
//   cabeca: INHERIT_IQI = 3 slots de cabeca
//   16 metodos; slot = 3 + posicao
constexpr unsigned kHIDDevice_AddRef = 0;
constexpr unsigned kHIDDevice_Release = 1;
constexpr unsigned kHIDDevice_QueryInterface = 2;
constexpr unsigned kHIDDevice_GetDeviceInfo = 3;
constexpr unsigned kHIDDevice_GetDeviceStatus = 4;
constexpr unsigned kHIDDevice_RegisterForStatusChange = 5;
constexpr unsigned kHIDDevice_GetButtonInfo = 6;
constexpr unsigned kHIDDevice_GetNumberOfButtons = 7;
constexpr unsigned kHIDDevice_RegisterForButtonEvent = 8;
constexpr unsigned kHIDDevice_GetNextButtonEvent = 9;
constexpr unsigned kHIDDevice_GetPositionState = 10;
constexpr unsigned kHIDDevice_GetMinPositionInfo = 11;
constexpr unsigned kHIDDevice_GetMaxPositionInfo = 12;
constexpr unsigned kHIDDevice_GetAxesInfo = 13;
constexpr unsigned kHIDDevice_RegisterForPositionChange = 14;
constexpr unsigned kHIDDevice_SetExclusiveLevel = 15;
constexpr unsigned kHIDDevice_GetExclusiveLevel = 16;
constexpr unsigned kHIDDevice_Rumble = 17;
constexpr unsigned kHIDDevice_GetRumbleStatus = 18;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeHIDDevice` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kHIDDeviceSlots = 19;

inline const char* NomeDeHIDDevice(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "QueryInterface", "GetDeviceInfo", "GetDeviceStatus", "RegisterForStatusChange", "GetButtonInfo", "GetNumberOfButtons", "RegisterForButtonEvent", "GetNextButtonEvent", "GetPositionState", "GetMinPositionInfo", "GetMaxPositionInfo", "GetAxesInfo", "RegisterForPositionChange", "SetExclusiveLevel", "GetExclusiveLevel", "Rumble", "GetRumbleStatus",
  };
  return slot < 19 ? k[slot] : "slot_fora_da_tabela";
}

// platform/system/inc/OEM/AEEIHeap1.h
//   cabeca: INHERIT_IRealloc = 7 slots de cabeca
//   9 metodos; slot = 7 + posicao
constexpr unsigned kHeap1_AddRef = 0;
constexpr unsigned kHeap1_Release = 1;
constexpr unsigned kHeap1_QueryInterface = 2;
constexpr unsigned kHeap1_ErrRealloc = 3;
constexpr unsigned kHeap1_ErrReallocName = 4;
constexpr unsigned kHeap1_ErrReallocNoZI = 5;
constexpr unsigned kHeap1_ErrReallocNameNoZI = 6;
constexpr unsigned kHeap1_Lock = 7;
constexpr unsigned kHeap1_Unlock = 8;
constexpr unsigned kHeap1_Sentinel = 9;
constexpr unsigned kHeap1_NodeInfo = 10;
constexpr unsigned kHeap1_Walk = 11;
constexpr unsigned kHeap1_Flags = 12;
constexpr unsigned kHeap1_Compress = 13;
constexpr unsigned kHeap1_Tag = 14;
constexpr unsigned kHeap1_Statistics = 15;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeHeap1` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kHeap1Slots = 16;

inline const char* NomeDeHeap1(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "QueryInterface", "ErrRealloc", "ErrReallocName", "ErrReallocNoZI", "ErrReallocNameNoZI", "Lock", "Unlock", "Sentinel", "NodeInfo", "Walk", "Flags", "Compress", "Tag", "Statistics",
  };
  return slot < 16 ? k[slot] : "slot_fora_da_tabela";
}

// platform/deprecated/inc/AEESQL.h
//   cabeca: AEEINTERFACE(ISQLMgr): INHERIT_IQueryInterface = 3
//   2 metodos; slot = 3 + posicao
constexpr unsigned kSQLMgr_AddRef = 0;
constexpr unsigned kSQLMgr_Release = 1;
constexpr unsigned kSQLMgr_QueryInterface = 2;
constexpr unsigned kSQLMgr_Open = 3;
constexpr unsigned kSQLMgr_Remove = 4;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeSQLMgr` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kSQLMgrSlots = 5;

inline const char* NomeDeSQLMgr(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "QueryInterface", "Open", "Remove",
  };
  return slot < 5 ? k[slot] : "slot_fora_da_tabela";
}

// platform/system/inc/AEEIAStream.h
//   cabeca: INHERIT_IBase = 2 slots de cabeca
//   3 metodos; slot = 2 + posicao
constexpr unsigned kIAStream_AddRef = 0;
constexpr unsigned kIAStream_Release = 1;
constexpr unsigned kIAStream_Readable = 2;
constexpr unsigned kIAStream_Read = 3;
constexpr unsigned kIAStream_Cancel = 4;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeIAStream` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kIAStreamSlots = 5;

inline const char* NomeDeIAStream(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "Readable", "Read", "Cancel",
  };
  return slot < 5 ? k[slot] : "slot_fora_da_tabela";
}

// platform/deprecated/inc/AEEFile.h
//   cabeca: INHERIT_IAStream = 5 slots de cabeca
//   7 metodos; slot = 5 + posicao
constexpr unsigned kIFile_AddRef = 0;
constexpr unsigned kIFile_Release = 1;
constexpr unsigned kIFile_Readable = 2;
constexpr unsigned kIFile_Read = 3;
constexpr unsigned kIFile_Cancel = 4;
constexpr unsigned kIFile_Write = 5;
constexpr unsigned kIFile_GetInfo = 6;
constexpr unsigned kIFile_Seek = 7;
constexpr unsigned kIFile_Truncate = 8;
constexpr unsigned kIFile_GetInfoEx = 9;
constexpr unsigned kIFile_SetCacheSize = 10;
constexpr unsigned kIFile_Map = 11;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeIFile` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kIFileSlots = 12;

inline const char* NomeDeIFile(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "Readable", "Read", "Cancel", "Write", "GetInfo", "Seek", "Truncate", "GetInfoEx", "SetCacheSize", "Map",
  };
  return slot < 12 ? k[slot] : "slot_fora_da_tabela";
}

// platform/hardware/inc/AEEIHID.h
//   cabeca: INHERIT_IQI = 3 slots de cabeca
//   5 metodos; slot = 3 + posicao
constexpr unsigned kIHID_AddRef = 0;
constexpr unsigned kIHID_Release = 1;
constexpr unsigned kIHID_QueryInterface = 2;
constexpr unsigned kIHID_CreateDevice = 3;
constexpr unsigned kIHID_GetDeviceInfo = 4;
constexpr unsigned kIHID_GetNextConnectEvent = 5;
constexpr unsigned kIHID_RegisterForConnectEvents = 6;
constexpr unsigned kIHID_GetConnectedDevices = 7;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeIHID` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kIHIDSlots = 8;

inline const char* NomeDeIHID(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "QueryInterface", "CreateDevice", "GetDeviceInfo", "GetNextConnectEvent", "RegisterForConnectEvents", "GetConnectedDevices",
  };
  return slot < 8 ? k[slot] : "slot_fora_da_tabela";
}

// platform/system/inc/AEEISignal.h
//   cabeca: INHERIT_IQI = 3 slots de cabeca
//   1 metodos; slot = 3 + posicao
constexpr unsigned kISignal_AddRef = 0;
constexpr unsigned kISignal_Release = 1;
constexpr unsigned kISignal_QueryInterface = 2;
constexpr unsigned kISignal_Set = 3;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeISignal` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kISignalSlots = 4;

inline const char* NomeDeISignal(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "QueryInterface", "Set",
  };
  return slot < 4 ? k[slot] : "slot_fora_da_tabela";
}

// platform/system/inc/AEEISignalCtl.h
//   cabeca: INHERIT_ISignal = 4 slots de cabeca
//   2 metodos; slot = 4 + posicao
constexpr unsigned kISignalCtl_AddRef = 0;
constexpr unsigned kISignalCtl_Release = 1;
constexpr unsigned kISignalCtl_QueryInterface = 2;
constexpr unsigned kISignalCtl_Set = 3;
constexpr unsigned kISignalCtl_Detach = 4;
constexpr unsigned kISignalCtl_Enable = 5;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeISignalCtl` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kISignalCtlSlots = 6;

inline const char* NomeDeISignalCtl(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "QueryInterface", "Set", "Detach", "Enable",
  };
  return slot < 6 ? k[slot] : "slot_fora_da_tabela";
}

// platform/system/inc/AEEISignalCBFactory.h
//   cabeca: INHERIT_IQI = 3 slots de cabeca
//   1 metodos; slot = 3 + posicao
constexpr unsigned kISignalCBFactory_AddRef = 0;
constexpr unsigned kISignalCBFactory_Release = 1;
constexpr unsigned kISignalCBFactory_QueryInterface = 2;
constexpr unsigned kISignalCBFactory_CreateSignal = 3;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeISignalCBFactory` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kISignalCBFactorySlots = 4;

inline const char* NomeDeISignalCBFactory(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "QueryInterface", "CreateSignal",
  };
  return slot < 4 ? k[slot] : "slot_fora_da_tabela";
}

// platform/media/inc/AEEIMedia.h
//   cabeca: INHERIT_IQI = 3 slots de cabeca
//   11 metodos; slot = 3 + posicao
constexpr unsigned kMedia_AddRef = 0;
constexpr unsigned kMedia_Release = 1;
constexpr unsigned kMedia_QueryInterface = 2;
constexpr unsigned kMedia_RegisterNotify = 3;
constexpr unsigned kMedia_SetMediaParm = 4;
constexpr unsigned kMedia_GetMediaParm = 5;
constexpr unsigned kMedia_Play = 6;
constexpr unsigned kMedia_Record = 7;
constexpr unsigned kMedia_Stop = 8;
constexpr unsigned kMedia_Seek = 9;
constexpr unsigned kMedia_Pause = 10;
constexpr unsigned kMedia_Resume = 11;
constexpr unsigned kMedia_GetTotalTime = 12;
constexpr unsigned kMedia_GetState = 13;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeMedia` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kMediaSlots = 14;

inline const char* NomeDeMedia(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "QueryInterface", "RegisterNotify", "SetMediaParm", "GetMediaParm", "Play", "Record", "Stop", "Seek", "Pause", "Resume", "GetTotalTime", "GetState",
  };
  return slot < 14 ? k[slot] : "slot_fora_da_tabela";
}

// platform/system/inc/AEEIAppHistory.h
//   cabeca: INHERIT_IQI = 3 slots de cabeca
//   13 metodos; slot = 3 + posicao
constexpr unsigned kAppHistory_AddRef = 0;
constexpr unsigned kAppHistory_Release = 1;
constexpr unsigned kAppHistory_QueryInterface = 2;
constexpr unsigned kAppHistory_Forward = 3;
constexpr unsigned kAppHistory_Back = 4;
constexpr unsigned kAppHistory_Top = 5;
constexpr unsigned kAppHistory_Insert = 6;
constexpr unsigned kAppHistory_GetClass = 7;
constexpr unsigned kAppHistory_GetArgs = 8;
constexpr unsigned kAppHistory_GetData = 9;
constexpr unsigned kAppHistory_SetData = 10;
constexpr unsigned kAppHistory_Current = 11;
constexpr unsigned kAppHistory_Remove = 12;
constexpr unsigned kAppHistory_Move = 13;
constexpr unsigned kAppHistory_GetReason = 14;
constexpr unsigned kAppHistory_SetReason = 15;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeAppHistory` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kAppHistorySlots = 16;

inline const char* NomeDeAppHistory(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "QueryInterface", "Forward", "Back", "Top", "Insert", "GetClass", "GetArgs", "GetData", "SetData", "Current", "Remove", "Move", "GetReason", "SetReason",
  };
  return slot < 16 ? k[slot] : "slot_fora_da_tabela";
}

// platform/ui/inc/AEEIValueModel.h
//   cabeca: INHERIT_IModel = 5 slots de cabeca
//   4 metodos; slot = 5 + posicao
constexpr unsigned kValueModel_AddRef = 0;
constexpr unsigned kValueModel_Release = 1;
constexpr unsigned kValueModel_QueryInterface = 2;
constexpr unsigned kValueModel_AddListener = 3;
constexpr unsigned kValueModel_Notify = 4;
constexpr unsigned kValueModel_SetValue = 5;
constexpr unsigned kValueModel_GetValue = 6;
constexpr unsigned kValueModel_AdaptGet = 7;
constexpr unsigned kValueModel_AdaptSet = 8;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeValueModel` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kValueModelSlots = 9;

inline const char* NomeDeValueModel(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "QueryInterface", "AddListener", "Notify", "SetValue", "GetValue", "AdaptGet", "AdaptSet",
  };
  return slot < 9 ? k[slot] : "slot_fora_da_tabela";
}

// platform/deprecated/inc/AEEText.h
//   cabeca: QINTERFACE + DECLARE_IBASE = 2, DECLARE_ICONTROL = 9
//   17 metodos; slot = 11 + posicao
constexpr unsigned kTextCtl_AddRef = 0;
constexpr unsigned kTextCtl_Release = 1;
constexpr unsigned kTextCtl_HandleEvent = 2;
constexpr unsigned kTextCtl_Redraw = 3;
constexpr unsigned kTextCtl_SetActive = 4;
constexpr unsigned kTextCtl_IsActive = 5;
constexpr unsigned kTextCtl_SetRect = 6;
constexpr unsigned kTextCtl_GetRect = 7;
constexpr unsigned kTextCtl_SetProperties = 8;
constexpr unsigned kTextCtl_GetProperties = 9;
constexpr unsigned kTextCtl_Reset = 10;
constexpr unsigned kTextCtl_SetTitle = 11;
constexpr unsigned kTextCtl_SetText = 12;
constexpr unsigned kTextCtl_GetText = 13;
constexpr unsigned kTextCtl_GetTextPtr = 14;
constexpr unsigned kTextCtl_EnableCommand = 15;
constexpr unsigned kTextCtl_SetMaxSize = 16;
constexpr unsigned kTextCtl_SetSoftKeyMenu = 17;
constexpr unsigned kTextCtl_SetInputMode = 18;
constexpr unsigned kTextCtl_GetCursorPos = 19;
constexpr unsigned kTextCtl_SetCursorPos = 20;
constexpr unsigned kTextCtl_GetInputMode = 21;
constexpr unsigned kTextCtl_EnumModeInit = 22;
constexpr unsigned kTextCtl_EnumNextMode = 23;
constexpr unsigned kTextCtl_SetSelection = 24;
constexpr unsigned kTextCtl_GetSelection = 25;
constexpr unsigned kTextCtl_SetPropertiesEx = 26;
constexpr unsigned kTextCtl_GetPropertiesEx = 27;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeTextCtl` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kTextCtlSlots = 28;

inline const char* NomeDeTextCtl(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "HandleEvent", "Redraw", "SetActive", "IsActive", "SetRect", "GetRect", "SetProperties", "GetProperties", "Reset", "SetTitle", "SetText", "GetText", "GetTextPtr", "EnableCommand", "SetMaxSize", "SetSoftKeyMenu", "SetInputMode", "GetCursorPos", "SetCursorPos", "GetInputMode", "EnumModeInit", "EnumNextMode", "SetSelection", "GetSelection", "SetPropertiesEx", "GetPropertiesEx",
  };
  return slot < 28 ? k[slot] : "slot_fora_da_tabela";
}

// platform/ui/inc/AEEIHandler.h
//   cabeca: INHERIT_IQI = 3 slots de cabeca
//   2 metodos; slot = 3 + posicao
constexpr unsigned kIHandler_AddRef = 0;
constexpr unsigned kIHandler_Release = 1;
constexpr unsigned kIHandler_QueryInterface = 2;
constexpr unsigned kIHandler_HandleEvent = 3;
constexpr unsigned kIHandler_SetHandler = 4;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeIHandler` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kIHandlerSlots = 5;

inline const char* NomeDeIHandler(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "QueryInterface", "HandleEvent", "SetHandler",
  };
  return slot < 5 ? k[slot] : "slot_fora_da_tabela";
}

// platform/ui/inc/AEEIForm.h
//   cabeca: INHERIT_IHandler = 5 slots de cabeca
//   0 metodos; slot = 5 + posicao
constexpr unsigned kIForm_AddRef = 0;
constexpr unsigned kIForm_Release = 1;
constexpr unsigned kIForm_QueryInterface = 2;
constexpr unsigned kIForm_HandleEvent = 3;
constexpr unsigned kIForm_SetHandler = 4;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeIForm` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kIFormSlots = 5;

inline const char* NomeDeIForm(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "QueryInterface", "HandleEvent", "SetHandler",
  };
  return slot < 5 ? k[slot] : "slot_fora_da_tabela";
}

// platform/ui/inc/AEEIRootForm.h
//   cabeca: INHERIT_IForm = 5 slots de cabeca
//   5 metodos; slot = 5 + posicao
constexpr unsigned kIRootForm_AddRef = 0;
constexpr unsigned kIRootForm_Release = 1;
constexpr unsigned kIRootForm_QueryInterface = 2;
constexpr unsigned kIRootForm_HandleEvent = 3;
constexpr unsigned kIRootForm_SetHandler = 4;
constexpr unsigned kIRootForm_InsertForm = 5;
constexpr unsigned kIRootForm_RemoveForm = 6;
constexpr unsigned kIRootForm_GetForm = 7;
constexpr unsigned kIRootForm_ResolveForm = 8;
constexpr unsigned kIRootForm_GetClientRect = 9;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeIRootForm` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kIRootFormSlots = 10;

inline const char* NomeDeIRootForm(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "QueryInterface", "HandleEvent", "SetHandler", "InsertForm", "RemoveForm", "GetForm", "ResolveForm", "GetClientRect",
  };
  return slot < 10 ? k[slot] : "slot_fora_da_tabela";
}

// platform/ui/inc/AEEIWidget.h
//   cabeca: INHERIT_IHandler = 5 slots de cabeca
//   9 metodos; slot = 5 + posicao
constexpr unsigned kIWidget_AddRef = 0;
constexpr unsigned kIWidget_Release = 1;
constexpr unsigned kIWidget_QueryInterface = 2;
constexpr unsigned kIWidget_HandleEvent = 3;
constexpr unsigned kIWidget_SetHandler = 4;
constexpr unsigned kIWidget_GetPreferredExtent = 5;
constexpr unsigned kIWidget_GetExtent = 6;
constexpr unsigned kIWidget_SetExtent = 7;
constexpr unsigned kIWidget_GetParent = 8;
constexpr unsigned kIWidget_SetParent = 9;
constexpr unsigned kIWidget_Draw = 10;
constexpr unsigned kIWidget_IntersectOpaque = 11;
constexpr unsigned kIWidget_GetModel = 12;
constexpr unsigned kIWidget_SetModel = 13;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeIWidget` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kIWidgetSlots = 14;

inline const char* NomeDeIWidget(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "QueryInterface", "HandleEvent", "SetHandler", "GetPreferredExtent", "GetExtent", "SetExtent", "GetParent", "SetParent", "Draw", "IntersectOpaque", "GetModel", "SetModel",
  };
  return slot < 14 ? k[slot] : "slot_fora_da_tabela";
}

// platform/ui/inc/AEEIDecorator.h
//   cabeca: INHERIT_IWidget = 14 slots de cabeca
//   2 metodos; slot = 14 + posicao
constexpr unsigned kIDecorator_AddRef = 0;
constexpr unsigned kIDecorator_Release = 1;
constexpr unsigned kIDecorator_QueryInterface = 2;
constexpr unsigned kIDecorator_HandleEvent = 3;
constexpr unsigned kIDecorator_SetHandler = 4;
constexpr unsigned kIDecorator_GetPreferredExtent = 5;
constexpr unsigned kIDecorator_GetExtent = 6;
constexpr unsigned kIDecorator_SetExtent = 7;
constexpr unsigned kIDecorator_GetParent = 8;
constexpr unsigned kIDecorator_SetParent = 9;
constexpr unsigned kIDecorator_Draw = 10;
constexpr unsigned kIDecorator_IntersectOpaque = 11;
constexpr unsigned kIDecorator_GetModel = 12;
constexpr unsigned kIDecorator_SetModel = 13;
constexpr unsigned kIDecorator_SetWidget = 14;
constexpr unsigned kIDecorator_GetWidget = 15;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeIDecorator` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kIDecoratorSlots = 16;

inline const char* NomeDeIDecorator(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "QueryInterface", "HandleEvent", "SetHandler", "GetPreferredExtent", "GetExtent", "SetExtent", "GetParent", "SetParent", "Draw", "IntersectOpaque", "GetModel", "SetModel", "SetWidget", "GetWidget",
  };
  return slot < 16 ? k[slot] : "slot_fora_da_tabela";
}

// platform/ui/inc/AEEIDrawDecorator.h
//   cabeca: INHERIT_IDecorator = 16 slots de cabeca
//   1 metodos; slot = 16 + posicao
constexpr unsigned kIDrawDecorator_AddRef = 0;
constexpr unsigned kIDrawDecorator_Release = 1;
constexpr unsigned kIDrawDecorator_QueryInterface = 2;
constexpr unsigned kIDrawDecorator_HandleEvent = 3;
constexpr unsigned kIDrawDecorator_SetHandler = 4;
constexpr unsigned kIDrawDecorator_GetPreferredExtent = 5;
constexpr unsigned kIDrawDecorator_GetExtent = 6;
constexpr unsigned kIDrawDecorator_SetExtent = 7;
constexpr unsigned kIDrawDecorator_GetParent = 8;
constexpr unsigned kIDrawDecorator_SetParent = 9;
constexpr unsigned kIDrawDecorator_Draw = 10;
constexpr unsigned kIDrawDecorator_IntersectOpaque = 11;
constexpr unsigned kIDrawDecorator_GetModel = 12;
constexpr unsigned kIDrawDecorator_SetModel = 13;
constexpr unsigned kIDrawDecorator_SetWidget = 14;
constexpr unsigned kIDrawDecorator_GetWidget = 15;
constexpr unsigned kIDrawDecorator_SetDraw = 16;
// Quantos slots esta interface TEM, lido do cabecalho. E o limite da
// cablagem e o do `NomeDeIDrawDecorator` -- uma so constante, para os dois nao
// poderem divergir.
constexpr unsigned kIDrawDecoratorSlots = 17;

inline const char* NomeDeIDrawDecorator(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "QueryInterface", "HandleEvent", "SetHandler", "GetPreferredExtent", "GetExtent", "SetExtent", "GetParent", "SetParent", "Draw", "IntersectOpaque", "GetModel", "SetModel", "SetWidget", "GetWidget", "SetDraw",
  };
  return slot < 17 ? k[slot] : "slot_fora_da_tabela";
}

// platform/ui/inc/AEEIForm.h
constexpr unsigned PROP_FORM = 20480u;
constexpr unsigned WID_FORM = 20480u;
constexpr unsigned WID_TITLE = 20481u;
constexpr unsigned WID_SOFTKEYS = 20482u;
constexpr unsigned WID_BACKGROUND = 20483u;
constexpr unsigned WID_CONTAINER = 20488u;
constexpr unsigned WID_DECORATOR = 20484u;
constexpr unsigned WID_BACKDROP = 20489u;
constexpr unsigned WID_STATIC = 20490u;
constexpr unsigned FID_ACTIVE = 20580u;
constexpr unsigned FID_ROOT = 20581u;
constexpr unsigned FID_THEME = 20582u;
constexpr unsigned FID_THEME_FNAME = 20584u;
constexpr unsigned FID_TITLE = 20586u;
constexpr unsigned FID_BACKGROUND = 20588u;
constexpr unsigned FID_VISIBLE = 20603u;
constexpr unsigned FID_DISPLAY = 20601u;
constexpr unsigned FID_THEME_BASENAME = 20583u;

// platform/ui/inc/AEEWidgetProperties.h
constexpr unsigned PROP_BGCOLOR = 304u;
constexpr unsigned PROP_ACTIVE_BGCOLOR = 305u;
constexpr unsigned PROP_INACTIVE_BGCOLOR = 306u;
constexpr unsigned PROP_FGCOLOR = 320u;
constexpr unsigned PROP_ACTIVE_FGCOLOR = 321u;
constexpr unsigned PROP_INACTIVE_FGCOLOR = 322u;
constexpr unsigned PROP_BORDERCOLOR = 272u;
constexpr unsigned PROP_ACTIVE_BORDERCOLOR = 273u;
constexpr unsigned PROP_INACTIVE_BORDERCOLOR = 274u;
constexpr unsigned PROP_BORDERSTYLE = 281u;

// platform/ui/inc/AEEIWidget.h
constexpr unsigned EVT_WDG_SETPROPERTY = 2049u;
constexpr unsigned EVT_WDG_GETPROPERTY = 2048u;
constexpr unsigned EVT_WDG_SETFOCUS = 1792u;
constexpr unsigned EVT_WDG_HASFOCUS = 1793u;
constexpr unsigned EVT_WDG_CANTAKEFOCUS = 1794u;
constexpr unsigned EVT_WDG_SETLAYOUT = 2050u;

// platform/ui/inc/AEEIHandler.h
constexpr unsigned AEEIID_IHandler = 16865622u;

// platform/ui/inc/AEEIForm.h
constexpr unsigned AEEIID_IForm = 16856579u;

// platform/ui/inc/AEEIRootForm.h
constexpr unsigned AEEIID_IRootForm = 16856580u;

// platform/ui/inc/AEEIWidget.h
constexpr unsigned AEEIID_IWidget = 16865618u;

// platform/ui/inc/AEEIDecorator.h
constexpr unsigned AEEIID_IDecorator = 16865588u;

}  // namespace brew_slots
