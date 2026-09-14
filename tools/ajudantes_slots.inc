// GERADO por tools/nomear_ajudantes.py. NAO EDITAR A MAO.
//
// FONTE: platform/system/inc/AEEStdLib.h
// sha256 do cabecalho: b9492fe63407fdcfd49473f790328f7a84b73e968012debf5b24dcac7dced1e7
//
// A ORDEM E A DO CABECALHO. O `AEEHelperFuncs` e um struct de 117
// ponteiros de funcao de 32 bits e o offset do campo `k` e `4 * k`.
//
// AS QUATRO QUE ENGANAM (medidas neste cabecalho):
//   0x020 sprintf      0x13c vsprintf
//   0x0d8 strstr       0x0e8 stristr
//   0x040 strtowstr    0x0a0 wstrcompress
//   0x090 atoi         0x0a8 aee_GetRand
//
// Um numero escrito a mao ja divergiu nesta tabela: o despacho ligou
// `strtowstr` ao 0x0a0 e `aee_GetRand` ao 0x090. Ver
// docs/LEDGER-ZEEBULATOR.md e tools/ajudantes_hook.patch.
#pragma once

namespace brew_ajudantes {

constexpr unsigned kPassoBytes = 4;
constexpr unsigned kQuantos = 117;

constexpr unsigned kAjudante_memmove = 0x000;
constexpr unsigned kAjudante_memset = 0x004;
constexpr unsigned kAjudante_strcpy = 0x008;
constexpr unsigned kAjudante_strcat = 0x00C;
constexpr unsigned kAjudante_strcmp = 0x010;
constexpr unsigned kAjudante_strlen = 0x014;
constexpr unsigned kAjudante_strchr = 0x018;
constexpr unsigned kAjudante_strrchr = 0x01C;
constexpr unsigned kAjudante_sprintf = 0x020;
constexpr unsigned kAjudante_wstrcpy = 0x024;
constexpr unsigned kAjudante_wstrcat = 0x028;
constexpr unsigned kAjudante_wstrcmp = 0x02C;
constexpr unsigned kAjudante_wstrlen = 0x030;
constexpr unsigned kAjudante_wstrchr = 0x034;
constexpr unsigned kAjudante_wstrrchr = 0x038;
constexpr unsigned kAjudante_wsprintf = 0x03C;
constexpr unsigned kAjudante_strtowstr = 0x040;
constexpr unsigned kAjudante_wstrtostr = 0x044;
constexpr unsigned kAjudante_wstrtofloat = 0x048;
constexpr unsigned kAjudante_floattowstr = 0x04C;
constexpr unsigned kAjudante_utf8towstr = 0x050;
constexpr unsigned kAjudante_wstrtoutf8 = 0x054;
constexpr unsigned kAjudante_wstrlower = 0x058;
constexpr unsigned kAjudante_wstrupper = 0x05C;
constexpr unsigned kAjudante_chartype = 0x060;
constexpr unsigned kAjudante_SetupNativeImage = 0x064;
constexpr unsigned kAjudante_malloc = 0x068;
constexpr unsigned kAjudante_free = 0x06C;
constexpr unsigned kAjudante_wstrdup = 0x070;
constexpr unsigned kAjudante_realloc = 0x074;
constexpr unsigned kAjudante_wwritelongex = 0x078;
constexpr unsigned kAjudante_wstrsize = 0x07C;
constexpr unsigned kAjudante_wstrncopyn = 0x080;
constexpr unsigned kAjudante_OEMStrLen = 0x084;
constexpr unsigned kAjudante_OEMStrSize = 0x088;
constexpr unsigned kAjudante_GetAEEVersion = 0x08C;
constexpr unsigned kAjudante_atoi = 0x090;
constexpr unsigned kAjudante_f_op = 0x094;
constexpr unsigned kAjudante_f_cmp = 0x098;
constexpr unsigned kAjudante_dbgprintf = 0x09C;
constexpr unsigned kAjudante_wstrcompress = 0x0A0;
constexpr unsigned kAjudante_aee_LocalTimeOffset = 0x0A4;
constexpr unsigned kAjudante_aee_GetRand = 0x0A8;
constexpr unsigned kAjudante_aee_GetTimeMS = 0x0AC;
constexpr unsigned kAjudante_aee_GetUpTimeMS = 0x0B0;
constexpr unsigned kAjudante_aee_GetSeconds = 0x0B4;
constexpr unsigned kAjudante_aee_GetJulianDate = 0x0B8;
constexpr unsigned kAjudante_sysfree = 0x0BC;
constexpr unsigned kAjudante_GetAppInstance = 0x0C0;
constexpr unsigned kAjudante_strtoul = 0x0C4;
constexpr unsigned kAjudante_strncpy = 0x0C8;
constexpr unsigned kAjudante_strncmp = 0x0CC;
constexpr unsigned kAjudante_stricmp = 0x0D0;
constexpr unsigned kAjudante_strnicmp = 0x0D4;
constexpr unsigned kAjudante_strstr = 0x0D8;
constexpr unsigned kAjudante_memcmp = 0x0DC;
constexpr unsigned kAjudante_memchr = 0x0E0;
constexpr unsigned kAjudante_strexpand = 0x0E4;
constexpr unsigned kAjudante_stristr = 0x0E8;
constexpr unsigned kAjudante_memstr = 0x0EC;
constexpr unsigned kAjudante_wstrncmp = 0x0F0;
constexpr unsigned kAjudante_strdup = 0x0F4;
constexpr unsigned kAjudante_strbegins = 0x0F8;
constexpr unsigned kAjudante_strends = 0x0FC;
constexpr unsigned kAjudante_strchrend = 0x100;
constexpr unsigned kAjudante_strchrsend = 0x104;
constexpr unsigned kAjudante_memrchr = 0x108;
constexpr unsigned kAjudante_memchrend = 0x10C;
constexpr unsigned kAjudante_memrchrbegin = 0x110;
constexpr unsigned kAjudante_strlower = 0x114;
constexpr unsigned kAjudante_strupper = 0x118;
constexpr unsigned kAjudante_wstricmp = 0x11C;
constexpr unsigned kAjudante_wstrnicmp = 0x120;
constexpr unsigned kAjudante_inet_aton = 0x124;
constexpr unsigned kAjudante_inet_ntoa = 0x128;
constexpr unsigned kAjudante_swapl = 0x12C;
constexpr unsigned kAjudante_swaps = 0x130;
constexpr unsigned kAjudante_GetFSFree = 0x134;
constexpr unsigned kAjudante_GetRAMFree = 0x138;
constexpr unsigned kAjudante_vsprintf = 0x13C;
constexpr unsigned kAjudante_vsnprintf = 0x140;
constexpr unsigned kAjudante_snprintf = 0x144;
constexpr unsigned kAjudante_aee_JulianToSeconds = 0x148;
constexpr unsigned kAjudante_strlcpy = 0x14C;
constexpr unsigned kAjudante_strlcat = 0x150;
constexpr unsigned kAjudante_wstrlcpy = 0x154;
constexpr unsigned kAjudante_wstrlcat = 0x158;
constexpr unsigned kAjudante_setstaticptr = 0x15C;
constexpr unsigned kAjudante_f_assignstr = 0x160;
constexpr unsigned kAjudante_f_assignint = 0x164;
constexpr unsigned kAjudante_wwritelong = 0x168;
constexpr unsigned kAjudante_dbgheapmark = 0x16C;
constexpr unsigned kAjudante_lockmem = 0x170;
constexpr unsigned kAjudante_unlockmem = 0x174;
constexpr unsigned kAjudante_dumpheap = 0x178;
constexpr unsigned kAjudante_strtod = 0x17C;
constexpr unsigned kAjudante_f_calc = 0x180;
constexpr unsigned kAjudante_sleep = 0x184;
constexpr unsigned kAjudante_getlasterror = 0x188;
constexpr unsigned kAjudante_wgs84_to_degrees = 0x18C;
constexpr unsigned kAjudante_dbgevent = 0x190;
constexpr unsigned kAjudante_aee_IsBadPtr = 0x194;
constexpr unsigned kAjudante_aee_basename = 0x198;
constexpr unsigned kAjudante_aee_makepath = 0x19C;
constexpr unsigned kAjudante_aee_splitpath = 0x1A0;
constexpr unsigned kAjudante_aee_stribegins = 0x1A4;
constexpr unsigned kAjudante_aee_GetUTCSeconds = 0x1A8;
constexpr unsigned kAjudante_f_toint = 0x1AC;
constexpr unsigned kAjudante_f_get = 0x1B0;
constexpr unsigned kAjudante_qsort = 0x1B4;
constexpr unsigned kAjudante_trunc = 0x1B8;
constexpr unsigned kAjudante_utrunc = 0x1BC;
constexpr unsigned kAjudante_err_realloc = 0x1C0;
constexpr unsigned kAjudante_err_strdup = 0x1C4;
constexpr unsigned kAjudante_inet_pton = 0x1C8;
constexpr unsigned kAjudante_inet_ntop = 0x1CC;
constexpr unsigned kAjudante_GetALSContext = 0x1D0;

// A ASSINATURA de cada campo, LIDA do cabecalho: `assinatura` e o texto
// da declaracao, e `linha` diz onde ele esta para a medicao ser
// localizavel sem contar campos a mao.
struct Declaracao {
  unsigned offset;
  const char* nome;
  const char* assinatura;
  unsigned linha;
};

inline constexpr Declaracao kDeclaracoes[kQuantos] = {
    {0x000, "memmove", "void *(*memmove)(void *pd, const void *ps, size_t s)", 49},
    {0x004, "memset", "void *(*memset)(void *pd, int c, size_t s)", 50},
    {0x008, "strcpy", "char *(*strcpy)(char *dst, const char *src)", 53},
    {0x00C, "strcat", "char *(*strcat)(char *dst, const char *src)", 54},
    {0x010, "strcmp", "int (*strcmp)(const char *s1, const char *s2)", 55},
    {0x014, "strlen", "size_t (*strlen)(const char *s)", 56},
    {0x018, "strchr", "char *(*strchr)(const char *s1, int ch)", 57},
    {0x01C, "strrchr", "char *(*strrchr)(const char *s1, int ch)", 58},
    {0x020, "sprintf", "int (*sprintf)(char *pszDest,const char *pszFormat, ...)", 59},
    {0x024, "wstrcpy", "AECHAR *(*wstrcpy)(AECHAR *pDest, const AECHAR *pSrc)", 63},
    {0x028, "wstrcat", "AECHAR *(*wstrcat)(AECHAR *pDest, const AECHAR *pSrc)", 64},
    {0x02C, "wstrcmp", "int (*wstrcmp)(const AECHAR *s1, const AECHAR *s2)", 65},
    {0x030, "wstrlen", "int (*wstrlen)(const AECHAR *p)", 66},
    {0x034, "wstrchr", "AECHAR *(*wstrchr)(const AECHAR *s1, AECHAR ch)", 67},
    {0x038, "wstrrchr", "AECHAR *(*wstrrchr)(const AECHAR *s1, AECHAR ch)", 68},
    {0x03C, "wsprintf", "void (*wsprintf)(AECHAR *pDest, int nSize, const AECHAR *pFormat, ...)", 69},
    {0x040, "strtowstr", "AECHAR *(*strtowstr)(const char *pszIn, AECHAR *pDest, int nSize)", 74},
    {0x044, "wstrtostr", "char *(*wstrtostr)(const AECHAR *pIn, char *pszDest, int nSize)", 75},
    {0x048, "wstrtofloat", "double (*wstrtofloat)(const AECHAR *pszNum) AEEINTERFACE_SOFTFP", 77},
    {0x04C, "floattowstr", "boolean (*floattowstr)(double val, AECHAR *psz, int nSize) AEEINTERFACE_SOFTFP", 78},
    {0x050, "utf8towstr", "boolean (*utf8towstr)(const byte *pszIn, int nLen, AECHAR *pszDest, int nSizeDestBytes)", 80},
    {0x054, "wstrtoutf8", "boolean (*wstrtoutf8)(const AECHAR *pszIn, int nLen, byte *pszDest, int nSizeDestBytes)", 82},
    {0x058, "wstrlower", "void (*wstrlower)(AECHAR *pszDest)", 84},
    {0x05C, "wstrupper", "void (*wstrupper)(AECHAR *pszDest)", 85},
    {0x060, "chartype", "TChType (*chartype)(AECHAR ch)", 86},
    {0x064, "SetupNativeImage", "void *(*SetupNativeImage)(AEECLSID cls, void *pBuffer, AEEImageInfo *pii, boolean *pbRealloc)", 90},
    {0x068, "malloc", "void *(*malloc)(uint32 dwSize)", 95},
    {0x06C, "free", "void (*free)(void *pObj)", 96},
    {0x070, "wstrdup", "AECHAR *(*wstrdup)(const AECHAR *pIn)", 97},
    {0x074, "realloc", "void *(*realloc)(void *pSrc, uint32 dwSize)", 98},
    {0x078, "wwritelongex", "AECHAR *(*wwritelongex)(AECHAR *pszBuf, long n, int nPad, int *pnRemaining)", 102},
    {0x07C, "wstrsize", "int (*wstrsize)(const AECHAR *p)", 104},
    {0x080, "wstrncopyn", "int (*wstrncopyn)(AECHAR *pszDest, int cbDest, const AECHAR *pszSource, int lenSource)", 105},
    {0x084, "OEMStrLen", "int (*OEMStrLen)(const byte *p)", 110},
    {0x088, "OEMStrSize", "int (*OEMStrSize)(const byte *p)", 111},
    {0x08C, "GetAEEVersion", "uint32 (*GetAEEVersion)(byte *pszFormatted, int nSize, uint16 wFlags)", 115},
    {0x090, "atoi", "int (*atoi)(const char *psz)", 120},
    {0x094, "f_op", "double (*f_op)(double v1, double v2,int nType) AEEINTERFACE_SOFTFP", 125},
    {0x098, "f_cmp", "boolean (*f_cmp)(double v1, double v2,int nType) AEEINTERFACE_SOFTFP", 126},
    {0x09C, "dbgprintf", "void (*dbgprintf)(const char *psz,...)", 129},
    {0x0A0, "wstrcompress", "void (*wstrcompress)(const AECHAR *pIn, int nChars, byte *pDest,int nSize)", 130},
    {0x0A4, "aee_LocalTimeOffset", "int32 (*aee_LocalTimeOffset)(boolean *pbDaylightSavings)", 134},
    {0x0A8, "aee_GetRand", "void (*aee_GetRand)(byte *pDest, int nSize)", 135},
    {0x0AC, "aee_GetTimeMS", "uint32 (*aee_GetTimeMS)(void)", 136},
    {0x0B0, "aee_GetUpTimeMS", "uint32 (*aee_GetUpTimeMS)(void)", 137},
    {0x0B4, "aee_GetSeconds", "uint32 (*aee_GetSeconds)(void)", 138},
    {0x0B8, "aee_GetJulianDate", "void (*aee_GetJulianDate)(uint32 dwSecs, JulianType *pDate)", 139},
    {0x0BC, "sysfree", "void (*sysfree)(void *pb)", 141},
    {0x0C0, "GetAppInstance", "IApplet *(*GetAppInstance)(void)", 145},
    {0x0C4, "strtoul", "uint32 (*strtoul)(const char *nptr, char **endptr, int base )", 147},
    {0x0C8, "strncpy", "char *(*strncpy)(char *strDest, const char *strSource,size_t count )", 148},
    {0x0CC, "strncmp", "int (*strncmp)(const char *a, const char *b, size_t length )", 149},
    {0x0D0, "stricmp", "int (*stricmp)(const char *a, const char *b)", 150},
    {0x0D4, "strnicmp", "int (*strnicmp)(const char *a, const char *b, size_t length)", 151},
    {0x0D8, "strstr", "char *(*strstr)(const char *haystack, const char *needle)", 152},
    {0x0DC, "memcmp", "int (*memcmp)(const void *a, const void *b, size_t length )", 153},
    {0x0E0, "memchr", "void *(*memchr)(const void *src, int c, size_t length )", 154},
    {0x0E4, "strexpand", "void (*strexpand)(const byte *pSrc, int nCount, AECHAR *pDest,int nSize)", 155},
    {0x0E8, "stristr", "char *(*stristr)(const char *cpszHaystack, const char *cpszNeedle)", 156},
    {0x0EC, "memstr", "char *(*memstr)(const char *cpHaystack, const char *cpszNeedle, size_t nHaystackLen)", 157},
    {0x0F0, "wstrncmp", "int (*wstrncmp)(const AECHAR *s1, const AECHAR *s2, size_t nLen)", 158},
    {0x0F4, "strdup", "char *(*strdup)(const char *psz)", 159},
    {0x0F8, "strbegins", "boolean (*strbegins)(const char *cpszPrefix, const char *psz)", 160},
    {0x0FC, "strends", "boolean (*strends)(const char *cpszSuffic, const char *psz)", 161},
    {0x100, "strchrend", "char *(*strchrend)(const char *pszSrc, char c)", 162},
    {0x104, "strchrsend", "char *(*strchrsend)(const char *pszSrc, const char *pszChars)", 163},
    {0x108, "memrchr", "char *(*memrchr)(const char *pcSrch, int c, size_t nLen)", 164},
    {0x10C, "memchrend", "char *(*memchrend)(const char *pcSrch, int c, size_t nLen)", 165},
    {0x110, "memrchrbegin", "char *(*memrchrbegin)(const char *pcSrch, int c, size_t nLen)", 166},
    {0x114, "strlower", "char *(*strlower)(char *psz)", 167},
    {0x118, "strupper", "char *(*strupper)(char *psz)", 168},
    {0x11C, "wstricmp", "int (*wstricmp)(const AECHAR * p1, const AECHAR * p2)", 169},
    {0x120, "wstrnicmp", "int (*wstrnicmp)(const AECHAR * p1, const AECHAR * p2, size_t length)", 170},
    {0x124, "inet_aton", "boolean (*inet_aton)(const char *pc, uint32 *pulAddr)", 171},
    {0x128, "inet_ntoa", "void (*inet_ntoa)(uint32 ulAddr, char *pc, size_t nLen)", 172},
    {0x12C, "swapl", "uint32 (*swapl)(uint32 ul)", 173},
    {0x130, "swaps", "uint16 (*swaps)(uint16 us)", 174},
    {0x134, "GetFSFree", "uint32 (*GetFSFree)(uint32 * pdwTotal)", 178},
    {0x138, "GetRAMFree", "uint32 (*GetRAMFree)(uint32 * pdwTotal,uint32 * pdwLargest)", 179},
    {0x13C, "vsprintf", "int (*vsprintf)(char * pszDest,const char * pszFormat, AEEOldVaList arg)", 181},
    {0x140, "vsnprintf", "int32 (*vsnprintf)(char *buf, uint32 f, const char *format, AEEOldVaList list)", 182},
    {0x144, "snprintf", "int32 (*snprintf)(char *buf, uint32 f, const char *format, ...)", 183},
    {0x148, "aee_JulianToSeconds", "uint32 (*aee_JulianToSeconds)(JulianType* pDate)", 186},
    {0x14C, "strlcpy", "size_t (*strlcpy)(char * dst, const char * src, size_t nSize)", 187},
    {0x150, "strlcat", "size_t (*strlcat)(char * dst, const char * src, size_t nSize)", 188},
    {0x154, "wstrlcpy", "size_t (*wstrlcpy)(AECHAR * dst, const AECHAR * src, size_t nSize)", 189},
    {0x158, "wstrlcat", "size_t (*wstrlcat)(AECHAR * dst, const AECHAR * src, size_t nSize)", 190},
    {0x15C, "setstaticptr", "void *(*setstaticptr)(int nSPId, void *pNew)", 191},
    {0x160, "f_assignstr", "double (*f_assignstr)(const char *pszFloat) AEEINTERFACE_SOFTFP", 194},
    {0x164, "f_assignint", "double (*f_assignint)(int32 val) AEEINTERFACE_SOFTFP", 195},
    {0x168, "wwritelong", "AECHAR * (*wwritelong)(AECHAR *pszBuf, long n)", 197},
    {0x16C, "dbgheapmark", "void *(*dbgheapmark)(void *ptr, const char * pszFile, int nLine)", 198},
    {0x170, "lockmem", "int (*lockmem)(void ** ppHandle)", 199},
    {0x174, "unlockmem", "int (*unlockmem)(void ** ppHandle)", 200},
    {0x178, "dumpheap", "void (*dumpheap)(void)", 201},
    {0x17C, "strtod", "double (*strtod)(const char *pszFloat, char ** ppszEnd) AEEINTERFACE_SOFTFP", 203},
    {0x180, "f_calc", "double (*f_calc)(double x, int calcType) AEEINTERFACE_SOFTFP", 204},
    {0x184, "sleep", "void (*sleep)(uint32 msecs)", 206},
    {0x188, "getlasterror", "int (*getlasterror)(int errRegion)", 207},
    {0x18C, "wgs84_to_degrees", "double (*wgs84_to_degrees)(int32 latlon) AEEINTERFACE_SOFTFP", 209},
    {0x190, "dbgevent", "void (*dbgevent)(AEEEvent evt, AEECLSID cls, uint32 pl)", 211},
    {0x194, "aee_IsBadPtr", "boolean (*aee_IsBadPtr)(int chkType, void *pBuf, uint32 len)", 212},
    {0x198, "aee_basename", "char *(*aee_basename)(const char *cpszPath)", 213},
    {0x19C, "aee_makepath", "int (*aee_makepath)(const char *cpszDir, const char *cpszFile, char *pszOut, int *pnOutLen)", 214},
    {0x1A0, "aee_splitpath", "char *(*aee_splitpath)(const char *cpszPath, const char *cpszDir)", 216},
    {0x1A4, "aee_stribegins", "boolean (*aee_stribegins)(const char *cpszPrefix, const char *psz)", 217},
    {0x1A8, "aee_GetUTCSeconds", "uint32 (*aee_GetUTCSeconds)(void)", 218},
    {0x1AC, "f_toint", "int (*f_toint)(double val) AEEINTERFACE_SOFTFP", 220},
    {0x1B0, "f_get", "double (*f_get)(uint32 fgetType) AEEINTERFACE_SOFTFP", 221},
    {0x1B4, "qsort", "void (*qsort)(void *base, size_t nmemb, size_t size, PFNQSORTCOMPARE pfn)", 223},
    {0x1B8, "trunc", "int32 (*trunc)(double x) AEEINTERFACE_SOFTFP", 225},
    {0x1BC, "utrunc", "uint32 (*utrunc)(double x) AEEINTERFACE_SOFTFP", 226},
    {0x1C0, "err_realloc", "int (*err_realloc)(uint32 uSize, void **pp)", 228},
    {0x1C4, "err_strdup", "int (*err_strdup)(const char *pszSrc, char **pp)", 229},
    {0x1C8, "inet_pton", "int (*inet_pton)(int af, const char *src, void *dst)", 232},
    {0x1CC, "inet_ntop", "const char *(*inet_ntop)(int af, const void *src, char *dst, size_t size)", 233},
    {0x1D0, "GetALSContext", "IQueryInterface *(*GetALSContext)(void)", 234},
};

}  // namespace brew_ajudantes
