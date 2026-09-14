// GERADO por tools/gerar_slots.py a partir de `AEEGL.h`, argumento a argumento.
// NAO EDITAR A MAO: corre o gerador (`tools/verificar_slots_gl.sh`).
//
// O `AEEGL.h` do Zeebo NAO esta no `inc/` do SDK: vive dentro do instalador
// `OpenGLES_Extension_1.5.3_For_BREW_SDK_4.x.x_General_Installer/Installer.msi`,
// e `tools/achar_aegl.py` extrai-o. Este ficheiro e a unica fonte dos numeros
// de slot do IGL -- copiar a ordem de outro emulador foi o erro do IGLES11 na
// arvore antiga, e um numero escrito a mao ja divergiu uma vez neste trabalho.
//
// CONFERIDO contra conftest.elf: 102 thunks desmontados, 108 slots, zero divergencias com o cabecalho.
// Cada slot tem NOME, porque uma recusa sem nome nao se pode ler (P2/P7).
#pragma once

namespace gl_slots {
// AEEGL.h -- INHERIT_IQueryInterface = 3 slots de cabeca + 77 metodos
constexpr unsigned kIglSlots = 80;
// AEEGL.h -- INHERIT_IQueryInterface = 3 slots de cabeca + 25 metodos
constexpr unsigned kIeglSlots = 28;

constexpr unsigned kIgl_AddRef = 0;
constexpr unsigned kIgl_Release = 1;
constexpr unsigned kIgl_QueryInterface = 2;
constexpr unsigned kIgl_ActiveTexture = 3;
constexpr unsigned kIgl_AlphaFuncx = 4;
constexpr unsigned kIgl_BindTexture = 5;
constexpr unsigned kIgl_BlendFunc = 6;
constexpr unsigned kIgl_Clear = 7;
constexpr unsigned kIgl_ClearColorx = 8;
constexpr unsigned kIgl_ClearDepthx = 9;
constexpr unsigned kIgl_ClearStencil = 10;
constexpr unsigned kIgl_ClientActiveTexture = 11;
constexpr unsigned kIgl_Color4x = 12;
constexpr unsigned kIgl_ColorMask = 13;
constexpr unsigned kIgl_ColorPointer = 14;
constexpr unsigned kIgl_CompressedTexImage2D = 15;
constexpr unsigned kIgl_CompressedTexSubImage2D = 16;
constexpr unsigned kIgl_CopyTexImage2D = 17;
constexpr unsigned kIgl_CopyTexSubImage2D = 18;
constexpr unsigned kIgl_CullFace = 19;
constexpr unsigned kIgl_DeleteTextures = 20;
constexpr unsigned kIgl_DepthFunc = 21;
constexpr unsigned kIgl_DepthMask = 22;
constexpr unsigned kIgl_DepthRangex = 23;
constexpr unsigned kIgl_Disable = 24;
constexpr unsigned kIgl_DisableClientState = 25;
constexpr unsigned kIgl_DrawArrays = 26;
constexpr unsigned kIgl_DrawElements = 27;
constexpr unsigned kIgl_Enable = 28;
constexpr unsigned kIgl_EnableClientState = 29;
constexpr unsigned kIgl_Finish = 30;
constexpr unsigned kIgl_Flush = 31;
constexpr unsigned kIgl_Fogx = 32;
constexpr unsigned kIgl_Fogxv = 33;
constexpr unsigned kIgl_FrontFace = 34;
constexpr unsigned kIgl_Frustumx = 35;
constexpr unsigned kIgl_GenTextures = 36;
constexpr unsigned kIgl_GetError = 37;
constexpr unsigned kIgl_GetIntegerv = 38;
constexpr unsigned kIgl_GetString = 39;
constexpr unsigned kIgl_Hint = 40;
constexpr unsigned kIgl_LightModelx = 41;
constexpr unsigned kIgl_LightModelxv = 42;
constexpr unsigned kIgl_Lightx = 43;
constexpr unsigned kIgl_Lightxv = 44;
constexpr unsigned kIgl_LineWidthx = 45;
constexpr unsigned kIgl_LoadIdentity = 46;
constexpr unsigned kIgl_LoadMatrixx = 47;
constexpr unsigned kIgl_LogicOp = 48;
constexpr unsigned kIgl_Materialx = 49;
constexpr unsigned kIgl_Materialxv = 50;
constexpr unsigned kIgl_MatrixMode = 51;
constexpr unsigned kIgl_MultMatrixx = 52;
constexpr unsigned kIgl_MultiTexCoord4x = 53;
constexpr unsigned kIgl_Normal3x = 54;
constexpr unsigned kIgl_NormalPointer = 55;
constexpr unsigned kIgl_Orthox = 56;
constexpr unsigned kIgl_PixelStorei = 57;
constexpr unsigned kIgl_PointSizex = 58;
constexpr unsigned kIgl_PolygonOffsetx = 59;
constexpr unsigned kIgl_PopMatrix = 60;
constexpr unsigned kIgl_PushMatrix = 61;
constexpr unsigned kIgl_ReadPixels = 62;
constexpr unsigned kIgl_Rotatex = 63;
constexpr unsigned kIgl_SampleCoveragex = 64;
constexpr unsigned kIgl_Scalex = 65;
constexpr unsigned kIgl_Scissor = 66;
constexpr unsigned kIgl_ShadeModel = 67;
constexpr unsigned kIgl_StencilFunc = 68;
constexpr unsigned kIgl_StencilMask = 69;
constexpr unsigned kIgl_StencilOp = 70;
constexpr unsigned kIgl_TexCoordPointer = 71;
constexpr unsigned kIgl_TexEnvx = 72;
constexpr unsigned kIgl_TexEnvxv = 73;
constexpr unsigned kIgl_TexImage2D = 74;
constexpr unsigned kIgl_TexParameterx = 75;
constexpr unsigned kIgl_TexSubImage2D = 76;
constexpr unsigned kIgl_Translatex = 77;
constexpr unsigned kIgl_VertexPointer = 78;
constexpr unsigned kIgl_Viewport = 79;

constexpr unsigned kIegl_AddRef = 0;
constexpr unsigned kIegl_Release = 1;
constexpr unsigned kIegl_QueryInterface = 2;
constexpr unsigned kIegl_GetError = 3;
constexpr unsigned kIegl_GetDisplay = 4;
constexpr unsigned kIegl_Initialize = 5;
constexpr unsigned kIegl_Terminate = 6;
constexpr unsigned kIegl_QueryString = 7;
constexpr unsigned kIegl_GetProcAddress = 8;
constexpr unsigned kIegl_GetConfigs = 9;
constexpr unsigned kIegl_ChooseConfig = 10;
constexpr unsigned kIegl_GetConfigAttrib = 11;
constexpr unsigned kIegl_CreateWindowSurface = 12;
constexpr unsigned kIegl_CreatePixmapSurface = 13;
constexpr unsigned kIegl_CreatePbufferSurface = 14;
constexpr unsigned kIegl_DestroySurface = 15;
constexpr unsigned kIegl_QuerySurface = 16;
constexpr unsigned kIegl_CreateContext = 17;
constexpr unsigned kIegl_DestroyContext = 18;
constexpr unsigned kIegl_MakeCurrent = 19;
constexpr unsigned kIegl_GetCurrentContext = 20;
constexpr unsigned kIegl_GetCurrentSurface = 21;
constexpr unsigned kIegl_GetCurrentDisplay = 22;
constexpr unsigned kIegl_QueryContext = 23;
constexpr unsigned kIegl_WaitGL = 24;
constexpr unsigned kIegl_WaitNative = 25;
constexpr unsigned kIegl_SwapBuffers = 26;
constexpr unsigned kIegl_CopyBuffers = 27;

// O nome de CADA slot, indexado pelo proprio slot. Um despacho que
// devolvesse um sucesso mudo sem nome de metodo seria o defeito do
// `glCullFace` outra vez -- 86 377 chamadas descartadas em silencio.
inline const char* NomeIgl(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "QueryInterface", "glActiveTexture", "glAlphaFuncx", "glBindTexture", "glBlendFunc", "glClear", "glClearColorx", "glClearDepthx", "glClearStencil", "glClientActiveTexture", "glColor4x", "glColorMask", "glColorPointer", "glCompressedTexImage2D", "glCompressedTexSubImage2D", "glCopyTexImage2D", "glCopyTexSubImage2D", "glCullFace", "glDeleteTextures", "glDepthFunc", "glDepthMask", "glDepthRangex", "glDisable", "glDisableClientState", "glDrawArrays", "glDrawElements", "glEnable", "glEnableClientState", "glFinish", "glFlush", "glFogx", "glFogxv", "glFrontFace", "glFrustumx", "glGenTextures", "glGetError", "glGetIntegerv", "glGetString", "glHint", "glLightModelx", "glLightModelxv", "glLightx", "glLightxv", "glLineWidthx", "glLoadIdentity", "glLoadMatrixx", "glLogicOp", "glMaterialx", "glMaterialxv", "glMatrixMode", "glMultMatrixx", "glMultiTexCoord4x", "glNormal3x", "glNormalPointer", "glOrthox", "glPixelStorei", "glPointSizex", "glPolygonOffsetx", "glPopMatrix", "glPushMatrix", "glReadPixels", "glRotatex", "glSampleCoveragex", "glScalex", "glScissor", "glShadeModel", "glStencilFunc", "glStencilMask", "glStencilOp", "glTexCoordPointer", "glTexEnvx", "glTexEnvxv", "glTexImage2D", "glTexParameterx", "glTexSubImage2D", "glTranslatex", "glVertexPointer", "glViewport",
  };
  return slot < kIglSlots ? k[slot] : "slot_fora_da_tabela";
}
inline const char* NomeIegl(unsigned slot) {
  static const char* k[] = {
      "AddRef", "Release", "QueryInterface", "eglGetError", "eglGetDisplay", "eglInitialize", "eglTerminate", "eglQueryString", "eglGetProcAddress", "eglGetConfigs", "eglChooseConfig", "eglGetConfigAttrib", "eglCreateWindowSurface", "eglCreatePixmapSurface", "eglCreatePbufferSurface", "eglDestroySurface", "eglQuerySurface", "eglCreateContext", "eglDestroyContext", "eglMakeCurrent", "eglGetCurrentContext", "eglGetCurrentSurface", "eglGetCurrentDisplay", "eglQueryContext", "eglWaitGL", "eglWaitNative", "eglSwapBuffers", "eglCopyBuffers",
  };
  return slot < kIeglSlots ? k[slot] : "slot_fora_da_tabela";
}

// As constantes do GL, lidas de platform/ui/inc/gles/gl.h.
constexpr unsigned GL_POINTS = 0x0000u;
constexpr unsigned GL_LINES = 0x0001u;
constexpr unsigned GL_LINE_LOOP = 0x0002u;
constexpr unsigned GL_LINE_STRIP = 0x0003u;
constexpr unsigned GL_TRIANGLES = 0x0004u;
constexpr unsigned GL_TRIANGLE_STRIP = 0x0005u;
constexpr unsigned GL_TRIANGLE_FAN = 0x0006u;
constexpr unsigned GL_NEVER = 0x0200u;
constexpr unsigned GL_LESS = 0x0201u;
constexpr unsigned GL_EQUAL = 0x0202u;
constexpr unsigned GL_LEQUAL = 0x0203u;
constexpr unsigned GL_GREATER = 0x0204u;
constexpr unsigned GL_NOTEQUAL = 0x0205u;
constexpr unsigned GL_GEQUAL = 0x0206u;
constexpr unsigned GL_ALWAYS = 0x0207u;
constexpr unsigned GL_ZERO = 0u;
constexpr unsigned GL_ONE = 1u;
constexpr unsigned GL_SRC_ALPHA = 0x0302u;
constexpr unsigned GL_ONE_MINUS_SRC_ALPHA = 0x0303u;
constexpr unsigned GL_FRONT = 0x0404u;
constexpr unsigned GL_BACK = 0x0405u;
constexpr unsigned GL_FRONT_AND_BACK = 0x0408u;
constexpr unsigned GL_CW = 0x0900u;
constexpr unsigned GL_CCW = 0x0901u;
constexpr unsigned GL_BYTE = 0x1400u;
constexpr unsigned GL_UNSIGNED_BYTE = 0x1401u;
constexpr unsigned GL_SHORT = 0x1402u;
constexpr unsigned GL_UNSIGNED_SHORT = 0x1403u;
constexpr unsigned GL_FIXED = 0x140Cu;
constexpr unsigned GL_FLOAT = 0x1406u;
constexpr unsigned GL_CULL_FACE = 0x0B44u;
constexpr unsigned GL_LIGHTING = 0x0B50u;
constexpr unsigned GL_TEXTURE_2D = 0x0DE1u;
constexpr unsigned GL_NORMALIZE = 0x0BA1u;
constexpr unsigned GL_ALPHA_TEST = 0x0BC0u;
constexpr unsigned GL_BLEND = 0x0BE2u;
constexpr unsigned GL_DITHER = 0x0BD0u;
constexpr unsigned GL_FOG = 0x0B60u;
constexpr unsigned GL_DEPTH_TEST = 0x0B71u;
constexpr unsigned GL_SCISSOR_TEST = 0x0C11u;
constexpr unsigned GL_POLYGON_OFFSET_FILL = 0x8037u;
constexpr unsigned GL_VERTEX_ARRAY = 0x8074u;
constexpr unsigned GL_NORMAL_ARRAY = 0x8075u;
constexpr unsigned GL_COLOR_ARRAY = 0x8076u;
constexpr unsigned GL_TEXTURE_COORD_ARRAY = 0x8078u;
constexpr unsigned GL_MODELVIEW = 0x1700u;
constexpr unsigned GL_PROJECTION = 0x1701u;
constexpr unsigned GL_TEXTURE = 0x1702u;
constexpr unsigned GL_NO_ERROR = 0u;
constexpr unsigned GL_INVALID_ENUM = 0x0500u;
constexpr unsigned GL_INVALID_VALUE = 0x0501u;
constexpr unsigned GL_INVALID_OPERATION = 0x0502u;
constexpr unsigned GL_STACK_OVERFLOW = 0x0503u;
constexpr unsigned GL_STACK_UNDERFLOW = 0x0504u;
constexpr unsigned GL_OUT_OF_MEMORY = 0x0505u;
constexpr unsigned GL_COLOR_BUFFER_BIT = 0x00004000u;
constexpr unsigned GL_DEPTH_BUFFER_BIT = 0x00000100u;
constexpr unsigned GL_STENCIL_BUFFER_BIT = 0x00000400u;
constexpr unsigned GL_VENDOR = 0x1F00u;
constexpr unsigned GL_RENDERER = 0x1F01u;
constexpr unsigned GL_VERSION = 0x1F02u;
constexpr unsigned GL_EXTENSIONS = 0x1F03u;
constexpr unsigned GL_NEAREST = 0x2600u;
constexpr unsigned GL_LINEAR = 0x2601u;
constexpr unsigned GL_TEXTURE_MAG_FILTER = 0x2800u;
constexpr unsigned GL_TEXTURE_MIN_FILTER = 0x2801u;
constexpr unsigned GL_SMOOTH = 0x1D01u;
constexpr unsigned GL_FLAT = 0x1D00u;
constexpr unsigned GL_FASTEST = 0x1101u;
constexpr unsigned GL_NICEST = 0x1102u;
constexpr unsigned GL_DONT_CARE = 0x1100u;
constexpr unsigned GL_UNPACK_ALIGNMENT = 0x0CF5u;
constexpr unsigned GL_MAX_TEXTURE_SIZE = 0x0D33u;
constexpr unsigned GL_MAX_MODELVIEW_STACK_DEPTH = 0x0D36u;
constexpr unsigned GL_MAX_PROJECTION_STACK_DEPTH = 0x0D38u;
constexpr unsigned GL_MAX_TEXTURE_STACK_DEPTH = 0x0D39u;
constexpr unsigned GL_SUBPIXEL_BITS = 0x0D50u;
constexpr unsigned GL_RED_BITS = 0x0D52u;
constexpr unsigned GL_GREEN_BITS = 0x0D53u;
constexpr unsigned GL_BLUE_BITS = 0x0D54u;
constexpr unsigned GL_ALPHA_BITS = 0x0D55u;
constexpr unsigned GL_DEPTH_BITS = 0x0D56u;
constexpr unsigned GL_STENCIL_BITS = 0x0D57u;
constexpr unsigned GL_RGBA = 0x1908u;
constexpr unsigned GL_RGB = 0x1907u;
constexpr unsigned GL_ALPHA = 0x1906u;
constexpr unsigned GL_LUMINANCE = 0x1909u;
constexpr unsigned GL_LUMINANCE_ALPHA = 0x190Au;
constexpr unsigned GL_POINT_SIZE = 0x0B11u;
constexpr unsigned GL_LINE_WIDTH = 0x0B21u;
constexpr unsigned GL_POLYGON_OFFSET_FACTOR = 0x8038u;
constexpr unsigned GL_POLYGON_OFFSET_UNITS = 0x2A00u;
constexpr unsigned GL_TEXTURE0 = 0x84C0u;

}  // namespace gl_slots
