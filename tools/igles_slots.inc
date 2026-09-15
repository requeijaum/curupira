// GERADO por tools/nomear_igles.py. NAO EDITAR A MAO.
// Corre a guarda: tools/verificar_slots_igles.sh
//
// FONTE: BREW-4.0.2-SP19/sdk/inc/AEEGLES10.h, AEEGLES11.h e AEEGLES11Ext.h
//   sha256 b13b68f53580b294aa42f11a087a1dc3a445d387ab21492cbfec14e4612758be  AEEGLES10.h
//   sha256 a2e76290c0c65ee993c0a5383f730011f59c54a0b5181480503bd44066f8d885  AEEGLES11.h
//   sha256 b78be2c849221572c4c4c42cdcb7660fbbf8d31b0288fa1f17660273b0b7565e  AEEGLES11Ext.h
//
// A CONTA: INHERIT_IQueryInterface(3) + IGLES10(106) + IGLES11(39) = 148,
// e 148 e o `kIglesSlots` que `core/brew/classes.h` ja tinha. Duas fontes,
// e elas concordam.
//
// O `IGLES11Ext` (AEEIID_GLES11EXT = 0x0103d8eb) e OUTRA interface e OUTRO
// objecto: 3 + 12 = 15 slots. E dela que vem o `glDrawTexivOES`.
//
// ANCORAS CONFERIDAS A MAO antes deste gerador existir (o gerador FALHA se
// alguma divergir):
//   IGLES11    slot 33  BindTexture
//   IGLES11    slot 64  GenTextures
//   IGLES11    slot 65  GetError
//   IGLES11    slot 67  GetString
//   IGLES11    slot 74  LoadIdentity
//   IGLES11    slot 79  MatrixMode
//   IGLES11    slot 104 TexParameterx
//   IGLES11    slot 108 Viewport
//   IGLES11Ext slot 3   CurrentPaletteMatrixOES
//   IGLES11Ext slot 8   DrawTexiOES
//   IGLES11Ext slot 11  DrawTexivOES
//   IGLES11Ext slot 14  DrawTexfvOES
#pragma once

namespace igles_slots {

constexpr unsigned kQuantos = 148;

constexpr unsigned kIgles_AddRef = 0;
constexpr unsigned kIgles_Release = 1;
constexpr unsigned kIgles_QueryInterface = 2;
constexpr unsigned kIgles_AlphaFunc = 3;
constexpr unsigned kIgles_ClearColor = 4;
constexpr unsigned kIgles_ClearDepthf = 5;
constexpr unsigned kIgles_Color4f = 6;
constexpr unsigned kIgles_DepthRangef = 7;
constexpr unsigned kIgles_Fogf = 8;
constexpr unsigned kIgles_Fogfv = 9;
constexpr unsigned kIgles_Frustumf = 10;
constexpr unsigned kIgles_LightModelf = 11;
constexpr unsigned kIgles_LightModelfv = 12;
constexpr unsigned kIgles_Lightf = 13;
constexpr unsigned kIgles_Lightfv = 14;
constexpr unsigned kIgles_LineWidth = 15;
constexpr unsigned kIgles_LoadMatrixf = 16;
constexpr unsigned kIgles_Materialf = 17;
constexpr unsigned kIgles_Materialfv = 18;
constexpr unsigned kIgles_MultMatrixf = 19;
constexpr unsigned kIgles_MultiTexCoord4f = 20;
constexpr unsigned kIgles_Normal3f = 21;
constexpr unsigned kIgles_Orthof = 22;
constexpr unsigned kIgles_PointSize = 23;
constexpr unsigned kIgles_PolygonOffset = 24;
constexpr unsigned kIgles_Rotatef = 25;
constexpr unsigned kIgles_Scalef = 26;
constexpr unsigned kIgles_TexEnvf = 27;
constexpr unsigned kIgles_TexEnvfv = 28;
constexpr unsigned kIgles_TexParameterf = 29;
constexpr unsigned kIgles_Translatef = 30;
constexpr unsigned kIgles_ActiveTexture = 31;
constexpr unsigned kIgles_AlphaFuncx = 32;
constexpr unsigned kIgles_BindTexture = 33;
constexpr unsigned kIgles_BlendFunc = 34;
constexpr unsigned kIgles_Clear = 35;
constexpr unsigned kIgles_ClearColorx = 36;
constexpr unsigned kIgles_ClearDepthx = 37;
constexpr unsigned kIgles_ClearStencil = 38;
constexpr unsigned kIgles_ClientActiveTexture = 39;
constexpr unsigned kIgles_Color4x = 40;
constexpr unsigned kIgles_ColorMask = 41;
constexpr unsigned kIgles_ColorPointer = 42;
constexpr unsigned kIgles_CompressedTexImage2D = 43;
constexpr unsigned kIgles_CompressedTexSubImage2D = 44;
constexpr unsigned kIgles_CopyTexImage2D = 45;
constexpr unsigned kIgles_CopyTexSubImage2D = 46;
constexpr unsigned kIgles_CullFace = 47;
constexpr unsigned kIgles_DeleteTextures = 48;
constexpr unsigned kIgles_DepthFunc = 49;
constexpr unsigned kIgles_DepthMask = 50;
constexpr unsigned kIgles_DepthRangex = 51;
constexpr unsigned kIgles_Disable = 52;
constexpr unsigned kIgles_DisableClientState = 53;
constexpr unsigned kIgles_DrawArrays = 54;
constexpr unsigned kIgles_DrawElements = 55;
constexpr unsigned kIgles_Enable = 56;
constexpr unsigned kIgles_EnableClientState = 57;
constexpr unsigned kIgles_Finish = 58;
constexpr unsigned kIgles_Flush = 59;
constexpr unsigned kIgles_Fogx = 60;
constexpr unsigned kIgles_Fogxv = 61;
constexpr unsigned kIgles_FrontFace = 62;
constexpr unsigned kIgles_Frustumx = 63;
constexpr unsigned kIgles_GenTextures = 64;
constexpr unsigned kIgles_GetError = 65;
constexpr unsigned kIgles_GetIntegerv = 66;
constexpr unsigned kIgles_GetString = 67;
constexpr unsigned kIgles_Hint = 68;
constexpr unsigned kIgles_LightModelx = 69;
constexpr unsigned kIgles_LightModelxv = 70;
constexpr unsigned kIgles_Lightx = 71;
constexpr unsigned kIgles_Lightxv = 72;
constexpr unsigned kIgles_LineWidthx = 73;
constexpr unsigned kIgles_LoadIdentity = 74;
constexpr unsigned kIgles_LoadMatrixx = 75;
constexpr unsigned kIgles_LogicOp = 76;
constexpr unsigned kIgles_Materialx = 77;
constexpr unsigned kIgles_Materialxv = 78;
constexpr unsigned kIgles_MatrixMode = 79;
constexpr unsigned kIgles_MultMatrixx = 80;
constexpr unsigned kIgles_MultiTexCoord4x = 81;
constexpr unsigned kIgles_Normal3x = 82;
constexpr unsigned kIgles_NormalPointer = 83;
constexpr unsigned kIgles_Orthox = 84;
constexpr unsigned kIgles_PixelStorei = 85;
constexpr unsigned kIgles_PointSizex = 86;
constexpr unsigned kIgles_PolygonOffsetx = 87;
constexpr unsigned kIgles_PopMatrix = 88;
constexpr unsigned kIgles_PushMatrix = 89;
constexpr unsigned kIgles_ReadPixels = 90;
constexpr unsigned kIgles_Rotatex = 91;
constexpr unsigned kIgles_SampleCoverage = 92;
constexpr unsigned kIgles_SampleCoveragex = 93;
constexpr unsigned kIgles_Scalex = 94;
constexpr unsigned kIgles_Scissor = 95;
constexpr unsigned kIgles_ShadeModel = 96;
constexpr unsigned kIgles_StencilFunc = 97;
constexpr unsigned kIgles_StencilMask = 98;
constexpr unsigned kIgles_StencilOp = 99;
constexpr unsigned kIgles_TexCoordPointer = 100;
constexpr unsigned kIgles_TexEnvx = 101;
constexpr unsigned kIgles_TexEnvxv = 102;
constexpr unsigned kIgles_TexImage2D = 103;
constexpr unsigned kIgles_TexParameterx = 104;
constexpr unsigned kIgles_TexSubImage2D = 105;
constexpr unsigned kIgles_Translatex = 106;
constexpr unsigned kIgles_VertexPointer = 107;
constexpr unsigned kIgles_Viewport = 108;
constexpr unsigned kIgles_ClipPlanef = 109;
constexpr unsigned kIgles_GetClipPlanef = 110;
constexpr unsigned kIgles_GetFloatv = 111;
constexpr unsigned kIgles_GetLightfv = 112;
constexpr unsigned kIgles_GetMaterialfv = 113;
constexpr unsigned kIgles_GetTexEnvfv = 114;
constexpr unsigned kIgles_GetTexParameterfv = 115;
constexpr unsigned kIgles_PointParameterf = 116;
constexpr unsigned kIgles_PointParameterfv = 117;
constexpr unsigned kIgles_TexParameterfv = 118;
constexpr unsigned kIgles_BindBuffer = 119;
constexpr unsigned kIgles_BufferData = 120;
constexpr unsigned kIgles_BufferSubData = 121;
constexpr unsigned kIgles_ClipPlanex = 122;
constexpr unsigned kIgles_Color4ub = 123;
constexpr unsigned kIgles_DeleteBuffers = 124;
constexpr unsigned kIgles_GetBooleanv = 125;
constexpr unsigned kIgles_GetBufferParameteriv = 126;
constexpr unsigned kIgles_GetClipPlanex = 127;
constexpr unsigned kIgles_GenBuffers = 128;
constexpr unsigned kIgles_GetFixedv = 129;
constexpr unsigned kIgles_GetLightxv = 130;
constexpr unsigned kIgles_GetMaterialxv = 131;
constexpr unsigned kIgles_GetPointerv = 132;
constexpr unsigned kIgles_GetTexEnviv = 133;
constexpr unsigned kIgles_GetTexEnvxv = 134;
constexpr unsigned kIgles_GetTexParameteriv = 135;
constexpr unsigned kIgles_GetTexParameterxv = 136;
constexpr unsigned kIgles_IsBuffer = 137;
constexpr unsigned kIgles_IsEnabled = 138;
constexpr unsigned kIgles_IsTexture = 139;
constexpr unsigned kIgles_PointParameterx = 140;
constexpr unsigned kIgles_PointParameterxv = 141;
constexpr unsigned kIgles_TexEnvi = 142;
constexpr unsigned kIgles_TexEnviv = 143;
constexpr unsigned kIgles_TexParameteri = 144;
constexpr unsigned kIgles_TexParameteriv = 145;
constexpr unsigned kIgles_TexParameterxv = 146;
constexpr unsigned kIgles_PointSizePointerOES = 147;

// O NOME de cada slot, pela ordem da vtable. Uma recusa sem nome nao se
// pode ler: `IGLES11::slot67` nao diz nada, `IGLES11::GetString` diz tudo.
inline constexpr const char* kNomes[kQuantos] = {
    "AddRef", "Release", "QueryInterface", "AlphaFunc", "ClearColor", "ClearDepthf", "Color4f",
    "DepthRangef", "Fogf", "Fogfv", "Frustumf", "LightModelf", "LightModelfv", "Lightf",
    "Lightfv", "LineWidth", "LoadMatrixf", "Materialf", "Materialfv", "MultMatrixf",
    "MultiTexCoord4f", "Normal3f", "Orthof", "PointSize", "PolygonOffset", "Rotatef", "Scalef",
    "TexEnvf", "TexEnvfv", "TexParameterf", "Translatef", "ActiveTexture", "AlphaFuncx",
    "BindTexture", "BlendFunc", "Clear", "ClearColorx", "ClearDepthx", "ClearStencil",
    "ClientActiveTexture", "Color4x", "ColorMask", "ColorPointer", "CompressedTexImage2D",
    "CompressedTexSubImage2D", "CopyTexImage2D", "CopyTexSubImage2D", "CullFace",
    "DeleteTextures", "DepthFunc", "DepthMask", "DepthRangex", "Disable", "DisableClientState",
    "DrawArrays", "DrawElements", "Enable", "EnableClientState", "Finish", "Flush", "Fogx",
    "Fogxv", "FrontFace", "Frustumx", "GenTextures", "GetError", "GetIntegerv", "GetString",
    "Hint", "LightModelx", "LightModelxv", "Lightx", "Lightxv", "LineWidthx", "LoadIdentity",
    "LoadMatrixx", "LogicOp", "Materialx", "Materialxv", "MatrixMode", "MultMatrixx",
    "MultiTexCoord4x", "Normal3x", "NormalPointer", "Orthox", "PixelStorei", "PointSizex",
    "PolygonOffsetx", "PopMatrix", "PushMatrix", "ReadPixels", "Rotatex", "SampleCoverage",
    "SampleCoveragex", "Scalex", "Scissor", "ShadeModel", "StencilFunc", "StencilMask",
    "StencilOp", "TexCoordPointer", "TexEnvx", "TexEnvxv", "TexImage2D", "TexParameterx",
    "TexSubImage2D", "Translatex", "VertexPointer", "Viewport", "ClipPlanef", "GetClipPlanef",
    "GetFloatv", "GetLightfv", "GetMaterialfv", "GetTexEnvfv", "GetTexParameterfv",
    "PointParameterf", "PointParameterfv", "TexParameterfv", "BindBuffer", "BufferData",
    "BufferSubData", "ClipPlanex", "Color4ub", "DeleteBuffers", "GetBooleanv",
    "GetBufferParameteriv", "GetClipPlanex", "GenBuffers", "GetFixedv", "GetLightxv",
    "GetMaterialxv", "GetPointerv", "GetTexEnviv", "GetTexEnvxv", "GetTexParameteriv",
    "GetTexParameterxv", "IsBuffer", "IsEnabled", "IsTexture", "PointParameterx",
    "PointParameterxv", "TexEnvi", "TexEnviv", "TexParameteri", "TexParameteriv",
    "TexParameterxv", "PointSizePointerOES",
};

// A ASSINATURA lida do cabecalho, para o argumento de uma recusa se poder
// ler sem ir contar campos a mao.
struct Declaracao {
  unsigned slot;
  const char* nome;
  const char* assinatura;
  const char* cabecalho;
  unsigned linha;
};

inline constexpr Declaracao kDeclaracoes[kQuantos] = {
    {0, "AddRef", "INHERIT_IQueryInterface", "AEEQueryInterface.h", 0},
    {1, "Release", "INHERIT_IQueryInterface", "AEEQueryInterface.h", 0},
    {2, "QueryInterface", "INHERIT_IQueryInterface", "AEEQueryInterface.h", 0},
    {3, "AlphaFunc", "int (*AlphaFunc) (iname *pMe, AEEGLenum func, AEEGLclampf ref);", "AEEGLES10.h", 27},
    {4, "ClearColor", "int (*ClearColor) (iname *pMe, AEEGLclampf red, AEEGLclampf green, AEEGLclampf blue, AEEGLclampf alpha);", "AEEGLES10.h", 28},
    {5, "ClearDepthf", "int (*ClearDepthf) (iname *pMe, AEEGLclampf depth);", "AEEGLES10.h", 29},
    {6, "Color4f", "int (*Color4f) (iname *pMe, AEEGLfloat red, AEEGLfloat green, AEEGLfloat blue, AEEGLfloat alpha);", "AEEGLES10.h", 30},
    {7, "DepthRangef", "int (*DepthRangef) (iname *pMe, AEEGLclampf zNear, AEEGLclampf zFar);", "AEEGLES10.h", 31},
    {8, "Fogf", "int (*Fogf) (iname *pMe, AEEGLenum pname, AEEGLfloat param);", "AEEGLES10.h", 32},
    {9, "Fogfv", "int (*Fogfv) (iname *pMe, AEEGLenum pname, const AEEGLfloat *params);", "AEEGLES10.h", 33},
    {10, "Frustumf", "int (*Frustumf) (iname *pMe, AEEGLfloat left, AEEGLfloat right, AEEGLfloat bottom, AEEGLfloat top, AEEGLfloat zNear, AEEGLfloat zFar);", "AEEGLES10.h", 34},
    {11, "LightModelf", "int (*LightModelf) (iname *pMe, AEEGLenum pname, AEEGLfloat param);", "AEEGLES10.h", 35},
    {12, "LightModelfv", "int (*LightModelfv) (iname *pMe, AEEGLenum pname, const AEEGLfloat *params);", "AEEGLES10.h", 36},
    {13, "Lightf", "int (*Lightf) (iname *pMe, AEEGLenum light, AEEGLenum pname, AEEGLfloat param);", "AEEGLES10.h", 37},
    {14, "Lightfv", "int (*Lightfv) (iname *pMe, AEEGLenum light, AEEGLenum pname, const AEEGLfloat *params);", "AEEGLES10.h", 38},
    {15, "LineWidth", "int (*LineWidth) (iname *pMe, AEEGLfloat width);", "AEEGLES10.h", 39},
    {16, "LoadMatrixf", "int (*LoadMatrixf) (iname *pMe, const AEEGLfloat *m);", "AEEGLES10.h", 40},
    {17, "Materialf", "int (*Materialf) (iname *pMe, AEEGLenum face, AEEGLenum pname, AEEGLfloat param);", "AEEGLES10.h", 41},
    {18, "Materialfv", "int (*Materialfv) (iname *pMe, AEEGLenum face, AEEGLenum pname, const AEEGLfloat *params);", "AEEGLES10.h", 42},
    {19, "MultMatrixf", "int (*MultMatrixf) (iname *pMe, const AEEGLfloat *m);", "AEEGLES10.h", 43},
    {20, "MultiTexCoord4f", "int (*MultiTexCoord4f) (iname *pMe, AEEGLenum target, AEEGLfloat s, AEEGLfloat t, AEEGLfloat r, AEEGLfloat q);", "AEEGLES10.h", 44},
    {21, "Normal3f", "int (*Normal3f) (iname *pMe, AEEGLfloat nx, AEEGLfloat ny, AEEGLfloat nz);", "AEEGLES10.h", 45},
    {22, "Orthof", "int (*Orthof) (iname *pMe, AEEGLfloat left, AEEGLfloat right, AEEGLfloat bottom, AEEGLfloat top, AEEGLfloat zNear, AEEGLfloat zFar);", "AEEGLES10.h", 46},
    {23, "PointSize", "int (*PointSize) (iname *pMe, AEEGLfloat size);", "AEEGLES10.h", 47},
    {24, "PolygonOffset", "int (*PolygonOffset) (iname *pMe, AEEGLfloat factor, AEEGLfloat units);", "AEEGLES10.h", 48},
    {25, "Rotatef", "int (*Rotatef) (iname *pMe, AEEGLfloat angle, AEEGLfloat x, AEEGLfloat y, AEEGLfloat z);", "AEEGLES10.h", 49},
    {26, "Scalef", "int (*Scalef) (iname *pMe, AEEGLfloat x, AEEGLfloat y, AEEGLfloat z);", "AEEGLES10.h", 50},
    {27, "TexEnvf", "int (*TexEnvf) (iname *pMe, AEEGLenum target, AEEGLenum pname, AEEGLfloat param);", "AEEGLES10.h", 51},
    {28, "TexEnvfv", "int (*TexEnvfv) (iname *pMe, AEEGLenum target, AEEGLenum pname, const AEEGLfloat *params);", "AEEGLES10.h", 52},
    {29, "TexParameterf", "int (*TexParameterf) (iname *pMe, AEEGLenum target, AEEGLenum pname, AEEGLfloat param);", "AEEGLES10.h", 53},
    {30, "Translatef", "int (*Translatef) (iname *pMe, AEEGLfloat x, AEEGLfloat y, AEEGLfloat z);", "AEEGLES10.h", 54},
    {31, "ActiveTexture", "int (*ActiveTexture) (iname *pMe, AEEGLenum texture);", "AEEGLES10.h", 55},
    {32, "AlphaFuncx", "int (*AlphaFuncx) (iname *pMe, AEEGLenum func, AEEGLclampx ref);", "AEEGLES10.h", 56},
    {33, "BindTexture", "int (*BindTexture) (iname *pMe, AEEGLenum target, AEEGLuint texture);", "AEEGLES10.h", 57},
    {34, "BlendFunc", "int (*BlendFunc) (iname *pMe, AEEGLenum sfactor, AEEGLenum dfactor);", "AEEGLES10.h", 58},
    {35, "Clear", "int (*Clear) (iname *pMe, AEEGLbitfield mask);", "AEEGLES10.h", 59},
    {36, "ClearColorx", "int (*ClearColorx) (iname *pMe, AEEGLclampx red, AEEGLclampx green, AEEGLclampx blue, AEEGLclampx alpha);", "AEEGLES10.h", 60},
    {37, "ClearDepthx", "int (*ClearDepthx) (iname *pMe, AEEGLclampx depth);", "AEEGLES10.h", 61},
    {38, "ClearStencil", "int (*ClearStencil) (iname *pMe, AEEGLint s);", "AEEGLES10.h", 62},
    {39, "ClientActiveTexture", "int (*ClientActiveTexture) (iname *pMe, AEEGLenum texture);", "AEEGLES10.h", 63},
    {40, "Color4x", "int (*Color4x) (iname *pMe, AEEGLfixed red, AEEGLfixed green, AEEGLfixed blue, AEEGLfixed alpha);", "AEEGLES10.h", 64},
    {41, "ColorMask", "int (*ColorMask) (iname *pMe, AEEGLboolean red, AEEGLboolean green, AEEGLboolean blue, AEEGLboolean alpha);", "AEEGLES10.h", 65},
    {42, "ColorPointer", "int (*ColorPointer) (iname *pMe, AEEGLint size, AEEGLenum type, AEEGLsizei stride, const AEEGLvoid *pointer);", "AEEGLES10.h", 66},
    {43, "CompressedTexImage2D", "int (*CompressedTexImage2D) (iname *pMe, AEEGLenum target, AEEGLint level, AEEGLenum internalformat, AEEGLsizei width, AEEGLsizei height, AEEGLint border, AEEGLsizei imageSize, const AEEGLvoid *data);", "AEEGLES10.h", 67},
    {44, "CompressedTexSubImage2D", "int (*CompressedTexSubImage2D) (iname *pMe, AEEGLenum target, AEEGLint level, AEEGLint xoffset, AEEGLint yoffset, AEEGLsizei width, AEEGLsizei height, AEEGLenum format, AEEGLsizei imageSize, const AEEGLvoid *data);", "AEEGLES10.h", 68},
    {45, "CopyTexImage2D", "int (*CopyTexImage2D) (iname *pMe, AEEGLenum target, AEEGLint level, AEEGLenum internalformat, AEEGLint x, AEEGLint y, AEEGLsizei width, AEEGLsizei height, AEEGLint border);", "AEEGLES10.h", 69},
    {46, "CopyTexSubImage2D", "int (*CopyTexSubImage2D) (iname *pMe, AEEGLenum target, AEEGLint level, AEEGLint xoffset, AEEGLint yoffset, AEEGLint x, AEEGLint y, AEEGLsizei width, AEEGLsizei height);", "AEEGLES10.h", 70},
    {47, "CullFace", "int (*CullFace) (iname *pMe, AEEGLenum mode);", "AEEGLES10.h", 71},
    {48, "DeleteTextures", "int (*DeleteTextures) (iname *pMe, AEEGLsizei n, const AEEGLuint *textures);", "AEEGLES10.h", 72},
    {49, "DepthFunc", "int (*DepthFunc) (iname *pMe, AEEGLenum func);", "AEEGLES10.h", 73},
    {50, "DepthMask", "int (*DepthMask) (iname *pMe, AEEGLboolean flag);", "AEEGLES10.h", 74},
    {51, "DepthRangex", "int (*DepthRangex) (iname *pMe, AEEGLclampx zNear, AEEGLclampx zFar);", "AEEGLES10.h", 75},
    {52, "Disable", "int (*Disable) (iname *pMe, AEEGLenum cap);", "AEEGLES10.h", 76},
    {53, "DisableClientState", "int (*DisableClientState) (iname *pMe, AEEGLenum array);", "AEEGLES10.h", 77},
    {54, "DrawArrays", "int (*DrawArrays) (iname *pMe, AEEGLenum mode, AEEGLint first, AEEGLsizei count);", "AEEGLES10.h", 78},
    {55, "DrawElements", "int (*DrawElements) (iname *pMe, AEEGLenum mode, AEEGLsizei count, AEEGLenum type, const AEEGLvoid *indices);", "AEEGLES10.h", 79},
    {56, "Enable", "int (*Enable) (iname *pMe, AEEGLenum cap);", "AEEGLES10.h", 80},
    {57, "EnableClientState", "int (*EnableClientState) (iname *pMe, AEEGLenum array);", "AEEGLES10.h", 81},
    {58, "Finish", "int (*Finish) (iname *pMe);", "AEEGLES10.h", 82},
    {59, "Flush", "int (*Flush) (iname *pMe);", "AEEGLES10.h", 83},
    {60, "Fogx", "int (*Fogx) (iname *pMe, AEEGLenum pname, AEEGLfixed param);", "AEEGLES10.h", 84},
    {61, "Fogxv", "int (*Fogxv) (iname *pMe, AEEGLenum pname, const AEEGLfixed *params);", "AEEGLES10.h", 85},
    {62, "FrontFace", "int (*FrontFace) (iname *pMe, AEEGLenum mode);", "AEEGLES10.h", 86},
    {63, "Frustumx", "int (*Frustumx) (iname *pMe, AEEGLfixed left, AEEGLfixed right, AEEGLfixed bottom, AEEGLfixed top, AEEGLfixed zNear, AEEGLfixed zFar);", "AEEGLES10.h", 87},
    {64, "GenTextures", "int (*GenTextures) (iname *pMe, AEEGLsizei n, AEEGLuint *textures);", "AEEGLES10.h", 88},
    {65, "GetError", "int (*GetError) (iname *pMe, AEEGLenum *ret);", "AEEGLES10.h", 89},
    {66, "GetIntegerv", "int (*GetIntegerv) (iname *pMe, AEEGLenum pname, AEEGLint *params);", "AEEGLES10.h", 90},
    {67, "GetString", "int (*GetString) (iname *pMe, AEEGLenum name, AEEGLubyte const **ret);", "AEEGLES10.h", 91},
    {68, "Hint", "int (*Hint) (iname *pMe, AEEGLenum target, AEEGLenum mode);", "AEEGLES10.h", 92},
    {69, "LightModelx", "int (*LightModelx) (iname *pMe, AEEGLenum pname, AEEGLfixed param);", "AEEGLES10.h", 93},
    {70, "LightModelxv", "int (*LightModelxv) (iname *pMe, AEEGLenum pname, const AEEGLfixed *params);", "AEEGLES10.h", 94},
    {71, "Lightx", "int (*Lightx) (iname *pMe, AEEGLenum light, AEEGLenum pname, AEEGLfixed param);", "AEEGLES10.h", 95},
    {72, "Lightxv", "int (*Lightxv) (iname *pMe, AEEGLenum light, AEEGLenum pname, const AEEGLfixed *params);", "AEEGLES10.h", 96},
    {73, "LineWidthx", "int (*LineWidthx) (iname *pMe, AEEGLfixed width);", "AEEGLES10.h", 97},
    {74, "LoadIdentity", "int (*LoadIdentity) (iname *pMe);", "AEEGLES10.h", 98},
    {75, "LoadMatrixx", "int (*LoadMatrixx) (iname *pMe, const AEEGLfixed *m);", "AEEGLES10.h", 99},
    {76, "LogicOp", "int (*LogicOp) (iname *pMe, AEEGLenum opcode);", "AEEGLES10.h", 100},
    {77, "Materialx", "int (*Materialx) (iname *pMe, AEEGLenum face, AEEGLenum pname, AEEGLfixed param);", "AEEGLES10.h", 101},
    {78, "Materialxv", "int (*Materialxv) (iname *pMe, AEEGLenum face, AEEGLenum pname, const AEEGLfixed *params);", "AEEGLES10.h", 102},
    {79, "MatrixMode", "int (*MatrixMode) (iname *pMe, AEEGLenum mode);", "AEEGLES10.h", 103},
    {80, "MultMatrixx", "int (*MultMatrixx) (iname *pMe, const AEEGLfixed *m);", "AEEGLES10.h", 104},
    {81, "MultiTexCoord4x", "int (*MultiTexCoord4x) (iname *pMe, AEEGLenum target, AEEGLfixed s, AEEGLfixed t, AEEGLfixed r, AEEGLfixed q);", "AEEGLES10.h", 105},
    {82, "Normal3x", "int (*Normal3x) (iname *pMe, AEEGLfixed nx, AEEGLfixed ny, AEEGLfixed nz);", "AEEGLES10.h", 106},
    {83, "NormalPointer", "int (*NormalPointer) (iname *pMe, AEEGLenum type, AEEGLsizei stride, const AEEGLvoid *pointer);", "AEEGLES10.h", 107},
    {84, "Orthox", "int (*Orthox) (iname *pMe, AEEGLfixed left, AEEGLfixed right, AEEGLfixed bottom, AEEGLfixed top, AEEGLfixed zNear, AEEGLfixed zFar);", "AEEGLES10.h", 108},
    {85, "PixelStorei", "int (*PixelStorei) (iname *pMe, AEEGLenum pname, AEEGLint param);", "AEEGLES10.h", 109},
    {86, "PointSizex", "int (*PointSizex) (iname *pMe, AEEGLfixed size);", "AEEGLES10.h", 110},
    {87, "PolygonOffsetx", "int (*PolygonOffsetx) (iname *pMe, AEEGLfixed factor, AEEGLfixed units);", "AEEGLES10.h", 111},
    {88, "PopMatrix", "int (*PopMatrix) (iname *pMe);", "AEEGLES10.h", 112},
    {89, "PushMatrix", "int (*PushMatrix) (iname *pMe);", "AEEGLES10.h", 113},
    {90, "ReadPixels", "int (*ReadPixels) (iname *pMe, AEEGLint x, AEEGLint y, AEEGLsizei width, AEEGLsizei height, AEEGLenum format, AEEGLenum type, AEEGLvoid *pixels);", "AEEGLES10.h", 114},
    {91, "Rotatex", "int (*Rotatex) (iname *pMe, AEEGLfixed angle, AEEGLfixed x, AEEGLfixed y, AEEGLfixed z);", "AEEGLES10.h", 115},
    {92, "SampleCoverage", "int (*SampleCoverage) (iname *pMe, AEEGLclampf value, AEEGLboolean invert);", "AEEGLES10.h", 116},
    {93, "SampleCoveragex", "int (*SampleCoveragex) (iname *pMe, AEEGLclampx value, AEEGLboolean invert);", "AEEGLES10.h", 117},
    {94, "Scalex", "int (*Scalex) (iname *pMe, AEEGLfixed x, AEEGLfixed y, AEEGLfixed z);", "AEEGLES10.h", 118},
    {95, "Scissor", "int (*Scissor) (iname *pMe, AEEGLint x, AEEGLint y, AEEGLsizei width, AEEGLsizei height);", "AEEGLES10.h", 119},
    {96, "ShadeModel", "int (*ShadeModel) (iname *pMe, AEEGLenum mode);", "AEEGLES10.h", 120},
    {97, "StencilFunc", "int (*StencilFunc) (iname *pMe, AEEGLenum func, AEEGLint ref, AEEGLuint mask);", "AEEGLES10.h", 121},
    {98, "StencilMask", "int (*StencilMask) (iname *pMe, AEEGLuint mask);", "AEEGLES10.h", 122},
    {99, "StencilOp", "int (*StencilOp) (iname *pMe, AEEGLenum fail, AEEGLenum zfail, AEEGLenum zpass);", "AEEGLES10.h", 123},
    {100, "TexCoordPointer", "int (*TexCoordPointer) (iname *pMe, AEEGLint size, AEEGLenum type, AEEGLsizei stride, const AEEGLvoid *pointer);", "AEEGLES10.h", 124},
    {101, "TexEnvx", "int (*TexEnvx) (iname *pMe, AEEGLenum target, AEEGLenum pname, AEEGLfixed param);", "AEEGLES10.h", 125},
    {102, "TexEnvxv", "int (*TexEnvxv) (iname *pMe, AEEGLenum target, AEEGLenum pname, const AEEGLfixed *params);", "AEEGLES10.h", 126},
    {103, "TexImage2D", "int (*TexImage2D) (iname *pMe, AEEGLenum target, AEEGLint level, AEEGLint internalformat, AEEGLsizei width, AEEGLsizei height, AEEGLint border, AEEGLenum format, AEEGLenum type, const AEEGLvoid *pixels);", "AEEGLES10.h", 127},
    {104, "TexParameterx", "int (*TexParameterx) (iname *pMe, AEEGLenum target, AEEGLenum pname, AEEGLfixed param);", "AEEGLES10.h", 128},
    {105, "TexSubImage2D", "int (*TexSubImage2D) (iname *pMe, AEEGLenum target, AEEGLint level, AEEGLint xoffset, AEEGLint yoffset, AEEGLsizei width, AEEGLsizei height, AEEGLenum format, AEEGLenum type, const AEEGLvoid *pixels);", "AEEGLES10.h", 129},
    {106, "Translatex", "int (*Translatex) (iname *pMe, AEEGLfixed x, AEEGLfixed y, AEEGLfixed z);", "AEEGLES10.h", 130},
    {107, "VertexPointer", "int (*VertexPointer) (iname *pMe, AEEGLint size, AEEGLenum type, AEEGLsizei stride, const AEEGLvoid *pointer);", "AEEGLES10.h", 131},
    {108, "Viewport", "int (*Viewport) (iname *pMe, AEEGLint x, AEEGLint y, AEEGLsizei width, AEEGLsizei height)", "AEEGLES10.h", 132},
    {109, "ClipPlanef", "int (*ClipPlanef) (iname *pMe, AEEGLenum plane, const AEEGLfloat *equation);", "AEEGLES11.h", 25},
    {110, "GetClipPlanef", "int (*GetClipPlanef) (iname *pMe, AEEGLenum pname, AEEGLfloat eqn[4]);", "AEEGLES11.h", 26},
    {111, "GetFloatv", "int (*GetFloatv) (iname *pMe, AEEGLenum pname, AEEGLfloat *params);", "AEEGLES11.h", 27},
    {112, "GetLightfv", "int (*GetLightfv) (iname *pMe, AEEGLenum light, AEEGLenum pname, AEEGLfloat *params);", "AEEGLES11.h", 28},
    {113, "GetMaterialfv", "int (*GetMaterialfv) (iname *pMe, AEEGLenum face, AEEGLenum pname, AEEGLfloat *params);", "AEEGLES11.h", 29},
    {114, "GetTexEnvfv", "int (*GetTexEnvfv) (iname *pMe, AEEGLenum env, AEEGLenum pname, AEEGLfloat *params);", "AEEGLES11.h", 30},
    {115, "GetTexParameterfv", "int (*GetTexParameterfv) (iname *pMe, AEEGLenum target, AEEGLenum pname, AEEGLfloat *params);", "AEEGLES11.h", 31},
    {116, "PointParameterf", "int (*PointParameterf) (iname *pMe, AEEGLenum pname, AEEGLfloat param);", "AEEGLES11.h", 32},
    {117, "PointParameterfv", "int (*PointParameterfv) (iname *pMe, AEEGLenum pname, const AEEGLfloat *params);", "AEEGLES11.h", 33},
    {118, "TexParameterfv", "int (*TexParameterfv) (iname *pMe, AEEGLenum target, AEEGLenum pname, const AEEGLfloat *params);", "AEEGLES11.h", 34},
    {119, "BindBuffer", "int (*BindBuffer) (iname *pMe, AEEGLenum target, AEEGLuint buffer);", "AEEGLES11.h", 35},
    {120, "BufferData", "int (*BufferData) (iname *pMe, AEEGLenum target, AEEGLsizeiptr size, const AEEGLvoid *data, AEEGLenum usage);", "AEEGLES11.h", 36},
    {121, "BufferSubData", "int (*BufferSubData) (iname *pMe, AEEGLenum target, AEEGLintptr offset, AEEGLsizeiptr size, const AEEGLvoid *data);", "AEEGLES11.h", 37},
    {122, "ClipPlanex", "int (*ClipPlanex) (iname *pMe, AEEGLenum plane, const AEEGLfixed *equation);", "AEEGLES11.h", 38},
    {123, "Color4ub", "int (*Color4ub) (iname *pMe, AEEGLubyte red, AEEGLubyte green, AEEGLubyte blue, AEEGLubyte alpha);", "AEEGLES11.h", 39},
    {124, "DeleteBuffers", "int (*DeleteBuffers) (iname *pMe, AEEGLsizei n, const AEEGLuint *buffers);", "AEEGLES11.h", 40},
    {125, "GetBooleanv", "int (*GetBooleanv) (iname *pMe, AEEGLenum pname, AEEGLboolean *params);", "AEEGLES11.h", 41},
    {126, "GetBufferParameteriv", "int (*GetBufferParameteriv) (iname *pMe, AEEGLenum target, AEEGLenum pname, AEEGLint *params);", "AEEGLES11.h", 42},
    {127, "GetClipPlanex", "int (*GetClipPlanex) (iname *pMe, AEEGLenum pname, AEEGLfixed eqn[4]);", "AEEGLES11.h", 43},
    {128, "GenBuffers", "int (*GenBuffers) (iname *pMe, AEEGLsizei n, AEEGLuint *buffers);", "AEEGLES11.h", 44},
    {129, "GetFixedv", "int (*GetFixedv) (iname *pMe, AEEGLenum pname, AEEGLfixed *params);", "AEEGLES11.h", 45},
    {130, "GetLightxv", "int (*GetLightxv) (iname *pMe, AEEGLenum light, AEEGLenum pname, AEEGLfixed *params);", "AEEGLES11.h", 46},
    {131, "GetMaterialxv", "int (*GetMaterialxv) (iname *pMe, AEEGLenum face, AEEGLenum pname, AEEGLfixed *params);", "AEEGLES11.h", 47},
    {132, "GetPointerv", "int (*GetPointerv) (iname *pMe, AEEGLenum pname, void **params);", "AEEGLES11.h", 48},
    {133, "GetTexEnviv", "int (*GetTexEnviv) (iname *pMe, AEEGLenum env, AEEGLenum pname, AEEGLint *params);", "AEEGLES11.h", 49},
    {134, "GetTexEnvxv", "int (*GetTexEnvxv) (iname *pMe, AEEGLenum env, AEEGLenum pname, AEEGLfixed *params);", "AEEGLES11.h", 50},
    {135, "GetTexParameteriv", "int (*GetTexParameteriv) (iname *pMe, AEEGLenum target, AEEGLenum pname, AEEGLint *params);", "AEEGLES11.h", 51},
    {136, "GetTexParameterxv", "int (*GetTexParameterxv) (iname *pMe, AEEGLenum target, AEEGLenum pname, AEEGLfixed *params);", "AEEGLES11.h", 52},
    {137, "IsBuffer", "int (*IsBuffer) (iname *pMe, AEEGLuint buffer, AEEGLboolean *ret);", "AEEGLES11.h", 53},
    {138, "IsEnabled", "int (*IsEnabled) (iname *pMe, AEEGLenum cap, AEEGLboolean *ret);", "AEEGLES11.h", 54},
    {139, "IsTexture", "int (*IsTexture) (iname *pMe, AEEGLuint texture, AEEGLboolean *ret);", "AEEGLES11.h", 55},
    {140, "PointParameterx", "int (*PointParameterx) (iname *pMe, AEEGLenum pname, AEEGLfixed param);", "AEEGLES11.h", 56},
    {141, "PointParameterxv", "int (*PointParameterxv) (iname *pMe, AEEGLenum pname, const AEEGLfixed *params);", "AEEGLES11.h", 57},
    {142, "TexEnvi", "int (*TexEnvi) (iname *pMe, AEEGLenum target, AEEGLenum pname, AEEGLint param);", "AEEGLES11.h", 58},
    {143, "TexEnviv", "int (*TexEnviv) (iname *pMe, AEEGLenum target, AEEGLenum pname, const AEEGLint *params);", "AEEGLES11.h", 59},
    {144, "TexParameteri", "int (*TexParameteri) (iname *pMe, AEEGLenum target, AEEGLenum pname, AEEGLint param);", "AEEGLES11.h", 60},
    {145, "TexParameteriv", "int (*TexParameteriv) (iname *pMe, AEEGLenum target, AEEGLenum pname, const AEEGLint *params);", "AEEGLES11.h", 61},
    {146, "TexParameterxv", "int (*TexParameterxv) (iname *pMe, AEEGLenum target, AEEGLenum pname, const AEEGLfixed *params);", "AEEGLES11.h", 62},
    {147, "PointSizePointerOES", "int (*PointSizePointerOES) (iname *pMe, AEEGLenum type, AEEGLsizei stride, const AEEGLvoid *pointer)", "AEEGLES11.h", 63},
};

}  // namespace igles_slots

namespace igles_ext_slots {

constexpr unsigned kQuantos = 15;

constexpr unsigned kIglesExt_AddRef = 0;
constexpr unsigned kIglesExt_Release = 1;
constexpr unsigned kIglesExt_QueryInterface = 2;
constexpr unsigned kIglesExt_CurrentPaletteMatrixOES = 3;
constexpr unsigned kIglesExt_LoadPaletteFromModelViewMatrixOES = 4;
constexpr unsigned kIglesExt_MatrixIndexPointerOES = 5;
constexpr unsigned kIglesExt_WeightPointerOES = 6;
constexpr unsigned kIglesExt_DrawTexsOES = 7;
constexpr unsigned kIglesExt_DrawTexiOES = 8;
constexpr unsigned kIglesExt_DrawTexxOES = 9;
constexpr unsigned kIglesExt_DrawTexsvOES = 10;
constexpr unsigned kIglesExt_DrawTexivOES = 11;
constexpr unsigned kIglesExt_DrawTexxvOES = 12;
constexpr unsigned kIglesExt_DrawTexfOES = 13;
constexpr unsigned kIglesExt_DrawTexfvOES = 14;

// O NOME de cada slot, pela ordem da vtable. Uma recusa sem nome nao se
// pode ler: `IGLES11::slot67` nao diz nada, `IGLES11::GetString` diz tudo.
inline constexpr const char* kNomes[kQuantos] = {
    "AddRef", "Release", "QueryInterface", "CurrentPaletteMatrixOES",
    "LoadPaletteFromModelViewMatrixOES", "MatrixIndexPointerOES", "WeightPointerOES",
    "DrawTexsOES", "DrawTexiOES", "DrawTexxOES", "DrawTexsvOES", "DrawTexivOES", "DrawTexxvOES",
    "DrawTexfOES", "DrawTexfvOES",
};

// A ASSINATURA lida do cabecalho, para o argumento de uma recusa se poder
// ler sem ir contar campos a mao.
struct Declaracao {
  unsigned slot;
  const char* nome;
  const char* assinatura;
  const char* cabecalho;
  unsigned linha;
};

inline constexpr Declaracao kDeclaracoes[kQuantos] = {
    {0, "AddRef", "INHERIT_IQueryInterface", "AEEQueryInterface.h", 0},
    {1, "Release", "INHERIT_IQueryInterface", "AEEQueryInterface.h", 0},
    {2, "QueryInterface", "INHERIT_IQueryInterface", "AEEQueryInterface.h", 0},
    {3, "CurrentPaletteMatrixOES", "int (*CurrentPaletteMatrixOES) (iname *pMe, AEEGLuint matrixpaletteindex);", "AEEGLES11Ext.h", 26},
    {4, "LoadPaletteFromModelViewMatrixOES", "int (*LoadPaletteFromModelViewMatrixOES) (iname *pMe);", "AEEGLES11Ext.h", 27},
    {5, "MatrixIndexPointerOES", "int (*MatrixIndexPointerOES) (iname *pMe, AEEGLint size, AEEGLenum type, AEEGLsizei stride, const AEEGLvoid *pointer);", "AEEGLES11Ext.h", 28},
    {6, "WeightPointerOES", "int (*WeightPointerOES) (iname *pMe, AEEGLint size, AEEGLenum type, AEEGLsizei stride, const AEEGLvoid *pointer);", "AEEGLES11Ext.h", 29},
    {7, "DrawTexsOES", "int (*DrawTexsOES) (iname *pMe, AEEGLshort x, AEEGLshort y, AEEGLshort z, AEEGLshort width, AEEGLshort height);", "AEEGLES11Ext.h", 30},
    {8, "DrawTexiOES", "int (*DrawTexiOES) (iname *pMe, AEEGLint x, AEEGLint y, AEEGLint z, AEEGLint width, AEEGLint height);", "AEEGLES11Ext.h", 31},
    {9, "DrawTexxOES", "int (*DrawTexxOES) (iname *pMe, AEEGLfixed x, AEEGLfixed y, AEEGLfixed z, AEEGLfixed width, AEEGLfixed height);", "AEEGLES11Ext.h", 32},
    {10, "DrawTexsvOES", "int (*DrawTexsvOES) (iname *pMe, const AEEGLshort *coords);", "AEEGLES11Ext.h", 33},
    {11, "DrawTexivOES", "int (*DrawTexivOES) (iname *pMe, const AEEGLint *coords);", "AEEGLES11Ext.h", 34},
    {12, "DrawTexxvOES", "int (*DrawTexxvOES) (iname *pMe, const AEEGLfixed *coords);", "AEEGLES11Ext.h", 35},
    {13, "DrawTexfOES", "int (*DrawTexfOES) (iname *pMe, AEEGLfloat x, AEEGLfloat y, AEEGLfloat z, AEEGLfloat width, AEEGLfloat height);", "AEEGLES11Ext.h", 36},
    {14, "DrawTexfvOES", "int (*DrawTexfvOES) (iname *pMe, const AEEGLfloat *coords)", "AEEGLES11Ext.h", 37},
};

}  // namespace igles_ext_slots
