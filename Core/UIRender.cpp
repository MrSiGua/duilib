#include "StdAfx.h"

extern "C"
{
    extern unsigned char *stbi_load_from_memory(unsigned char const *buffer, int len, int *x, int *y, \
            int *comp, int req_comp);
    extern void     stbi_image_free(void *retval_from_stbi_load);

};

namespace DuiLib
{

/////////////////////////////////////////////////////////////////////////////////////
//
//

CRenderClip::~CRenderClip()
{
    ASSERT(::GetObjectType(hDC) == OBJ_DC || ::GetObjectType(hDC) == OBJ_MEMDC);
    ASSERT(::GetObjectType(hRgn) == OBJ_REGION);
    ASSERT(::GetObjectType(hOldRgn) == OBJ_REGION);
    ::SelectClipRgn(hDC, hOldRgn);
    ::DeleteObject(hOldRgn);
    ::DeleteObject(hRgn);
}

void CRenderClip::GenerateClip(HDC hDC, RECT rc, CRenderClip & clip)
{
    RECT rcClip = { 0 };
    ::GetClipBox(hDC, &rcClip);
    clip.hOldRgn = ::CreateRectRgnIndirect(&rcClip);
    clip.hRgn = ::CreateRectRgnIndirect(&rc);
    ::ExtSelectClipRgn(hDC, clip.hRgn, RGN_AND);
    clip.hDC = hDC;
    clip.rcItem = rc;
}

void CRenderClip::GenerateRoundClip(HDC hDC, RECT rc, RECT rcItem, int width, int height, CRenderClip & clip)
{
    RECT rcClip = { 0 };
    ::GetClipBox(hDC, &rcClip);
    clip.hOldRgn = ::CreateRectRgnIndirect(&rcClip);
    clip.hRgn = ::CreateRectRgnIndirect(&rc);
    HRGN hRgnItem = ::CreateRoundRectRgn(rcItem.left, rcItem.top, rcItem.right + 1, rcItem.bottom + 1, width, height);
    ::CombineRgn(clip.hRgn, clip.hRgn, hRgnItem, RGN_AND);
    ::ExtSelectClipRgn(hDC, clip.hRgn, RGN_AND);
    clip.hDC = hDC;
    clip.rcItem = rc;
    ::DeleteObject(hRgnItem);
}

void CRenderClip::UseOldClipBegin(HDC hDC, CRenderClip & clip)
{
    ::SelectClipRgn(hDC, clip.hOldRgn);
}

void CRenderClip::UseOldClipEnd(HDC hDC, CRenderClip & clip)
{
    ::SelectClipRgn(hDC, clip.hRgn);
}

/////////////////////////////////////////////////////////////////////////////////////
//
//

static const float OneThird = 1.0f / 3;

static void RGBtoHSL(DWORD ARGB, float *H, float *S, float *L)
{
    const float
    R = (float)GetRValue(ARGB),
    G = (float)GetGValue(ARGB),
    B = (float)GetBValue(ARGB),
    nR = (R < 0 ? 0 : (R > 255 ? 255 : R)) / 255,
    nG = (G < 0 ? 0 : (G > 255 ? 255 : G)) / 255,
    nB = (B < 0 ? 0 : (B > 255 ? 255 : B)) / 255,
    m = min(min(nR, nG), nB),
    M = max(max(nR, nG), nB);
    *L = (m + M) / 2;

    if(M == m)
    {
        *H = *S = 0;
    }
    else
    {
        const float
        f = (nR == m) ? (nG - nB) : ((nG == m) ? (nB - nR) : (nR - nG)),
        i = (nR == m) ? 3.0f : ((nG == m) ? 5.0f : 1.0f);
        *H = (i - f / (M - m));

        if(*H >= 6)
        {
            *H -= 6;
        }

        *H *= 60;
        *S = (2 * (*L) <= 1) ? ((M - m) / (M + m)) : ((M - m) / (2 - M - m));
    }
}

static void HSLtoRGB(DWORD *ARGB, float H, float S, float L)
{
    const float
    q = 2 * L < 1 ? L * (1 + S) : (L + S - L * S),
    p = 2 * L - q,
    h = H / 360,
    tr = h + OneThird,
    tg = h,
    tb = h - OneThird,
    ntr = tr < 0 ? tr + 1 : (tr > 1 ? tr - 1 : tr),
    ntg = tg < 0 ? tg + 1 : (tg > 1 ? tg - 1 : tg),
    ntb = tb < 0 ? tb + 1 : (tb > 1 ? tb - 1 : tb),
    B = 255 * (6 * ntr < 1 ? p + (q - p) * 6 * ntr : (2 * ntr < 1 ? q : (3 * ntr < 2 ? p + (q - p) * 6 * (2.0f * OneThird - ntr) : p))),
    G = 255 * (6 * ntg < 1 ? p + (q - p) * 6 * ntg : (2 * ntg < 1 ? q : (3 * ntg < 2 ? p + (q - p) * 6 * (2.0f * OneThird - ntg) : p))),
    R = 255 * (6 * ntb < 1 ? p + (q - p) * 6 * ntb : (2 * ntb < 1 ? q : (3 * ntb < 2 ? p + (q - p) * 6 * (2.0f * OneThird - ntb) : p)));
    *ARGB &= 0xFF000000;
    *ARGB |= RGB((BYTE)(R < 0 ? 0 : (R > 255 ? 255 : R)), (BYTE)(G < 0 ? 0 : (G > 255 ? 255 : G)), (BYTE)(B < 0 ? 0 : (B > 255 ? 255 : B)));
}

static COLORREF PixelAlpha(COLORREF clrSrc, double src_darken, COLORREF clrDest, double dest_darken)
{
    return RGB(GetRValue(clrSrc) * src_darken + GetRValue(clrDest) * dest_darken,
               GetGValue(clrSrc) * src_darken + GetGValue(clrDest) * dest_darken,
               GetBValue(clrSrc) * src_darken + GetBValue(clrDest) * dest_darken);
}

static BOOL WINAPI AlphaBitBlt(HDC hDC, int nDestX, int nDestY, int dwWidth, int dwHeight, HDC hSrcDC, \
                               int nSrcX, int nSrcY, int wSrc, int hSrc, BLENDFUNCTION ftn)
{
    HDC hTempDC = ::CreateCompatibleDC(hDC);

    if(NULL == hTempDC)
    {
        return FALSE;
    }

    //Creates Source DIB
    LPBITMAPINFO lpbiSrc = NULL;
    // Fill in the BITMAPINFOHEADER
    lpbiSrc = (LPBITMAPINFO) new BYTE[sizeof(BITMAPINFOHEADER)];

    if(lpbiSrc == NULL)
    {
        ::DeleteDC(hTempDC);
        return FALSE;
    }

    lpbiSrc->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    lpbiSrc->bmiHeader.biWidth = dwWidth;
    lpbiSrc->bmiHeader.biHeight = dwHeight;
    lpbiSrc->bmiHeader.biPlanes = 1;
    lpbiSrc->bmiHeader.biBitCount = 32;
    lpbiSrc->bmiHeader.biCompression = BI_RGB;
    lpbiSrc->bmiHeader.biSizeImage = dwWidth * dwHeight;
    lpbiSrc->bmiHeader.biXPelsPerMeter = 0;
    lpbiSrc->bmiHeader.biYPelsPerMeter = 0;
    lpbiSrc->bmiHeader.biClrUsed = 0;
    lpbiSrc->bmiHeader.biClrImportant = 0;
    COLORREF *pSrcBits = NULL;
    HBITMAP hSrcDib = CreateDIBSection(
                          hSrcDC, lpbiSrc, DIB_RGB_COLORS, (void **)&pSrcBits,
                          NULL, NULL);

    if((NULL == hSrcDib) || (NULL == pSrcBits))
    {
        delete [] lpbiSrc;
        ::DeleteDC(hTempDC);
        return FALSE;
    }

    HBITMAP hOldTempBmp = (HBITMAP)::SelectObject(hTempDC, hSrcDib);
    ::StretchBlt(hTempDC, 0, 0, dwWidth, dwHeight, hSrcDC, nSrcX, nSrcY, wSrc, hSrc, SRCCOPY);
    ::SelectObject(hTempDC, hOldTempBmp);
    //Creates Destination DIB
    LPBITMAPINFO lpbiDest = NULL;
    // Fill in the BITMAPINFOHEADER
    lpbiDest = (LPBITMAPINFO) new BYTE[sizeof(BITMAPINFOHEADER)];

    if(lpbiDest == NULL)
    {
        delete [] lpbiSrc;
        ::DeleteObject(hSrcDib);
        ::DeleteDC(hTempDC);
        return FALSE;
    }

    lpbiDest->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    lpbiDest->bmiHeader.biWidth = dwWidth;
    lpbiDest->bmiHeader.biHeight = dwHeight;
    lpbiDest->bmiHeader.biPlanes = 1;
    lpbiDest->bmiHeader.biBitCount = 32;
    lpbiDest->bmiHeader.biCompression = BI_RGB;
    lpbiDest->bmiHeader.biSizeImage = dwWidth * dwHeight;
    lpbiDest->bmiHeader.biXPelsPerMeter = 0;
    lpbiDest->bmiHeader.biYPelsPerMeter = 0;
    lpbiDest->bmiHeader.biClrUsed = 0;
    lpbiDest->bmiHeader.biClrImportant = 0;
    COLORREF *pDestBits = NULL;
    HBITMAP hDestDib = CreateDIBSection(
                           hDC, lpbiDest, DIB_RGB_COLORS, (void **)&pDestBits,
                           NULL, NULL);

    if((NULL == hDestDib) || (NULL == pDestBits))
    {
        delete [] lpbiSrc;
        ::DeleteObject(hSrcDib);
        ::DeleteDC(hTempDC);
        return FALSE;
    }

    ::SelectObject(hTempDC, hDestDib);
    ::BitBlt(hTempDC, 0, 0, dwWidth, dwHeight, hDC, nDestX, nDestY, SRCCOPY);
    ::SelectObject(hTempDC, hOldTempBmp);
    double src_darken;
    BYTE nAlpha;

    for(int pixel = 0; pixel < dwWidth * dwHeight; pixel++, pSrcBits++, pDestBits++)
    {
        nAlpha = LOBYTE(*pSrcBits >> 24);
        src_darken = (double)(nAlpha * ftn.SourceConstantAlpha) / 255.0 / 255.0;

        if(src_darken < 0.0)
        {
            src_darken = 0.0;
        }

        *pDestBits = PixelAlpha(*pSrcBits, src_darken, *pDestBits, 1.0 - src_darken);
    } //for

    ::SelectObject(hTempDC, hDestDib);
    ::BitBlt(hDC, nDestX, nDestY, dwWidth, dwHeight, hTempDC, 0, 0, SRCCOPY);
    ::SelectObject(hTempDC, hOldTempBmp);
    delete [] lpbiDest;
    ::DeleteObject(hDestDib);
    delete [] lpbiSrc;
    ::DeleteObject(hSrcDib);
    ::DeleteDC(hTempDC);
    return TRUE;
}

/////////////////////////////////////////////////////////////////////////////////////
//
//

DWORD CRenderEngine::AdjustColor(DWORD dwColor, short H, short S, short L)
{
    if(H == 180 && S == 100 && L == 100)
    {
        return dwColor;
    }

    float fH, fS, fL;
    float S1 = S / 100.0f;
    float L1 = L / 100.0f;
    RGBtoHSL(dwColor, &fH, &fS, &fL);
    fH += (H - 180);
    fH = fH > 0 ? fH : fH + 360;
    fS *= S1;
    fL *= L1;
    HSLtoRGB(&dwColor, fH, fS, fL);
    return dwColor;
}

void CRenderEngine::DrawImage(HDC hDC, HBITMAP hBitmap, const RECT & rc, const RECT & rcPaint,
                              const RECT & rcBmpPart, const RECT & rcCorners, bool alphaChannel,
                              BYTE uFade, bool hole, bool xtiled, bool ytiled)
{
    ASSERT(::GetObjectType(hDC) == OBJ_DC || ::GetObjectType(hDC) == OBJ_MEMDC);
    typedef BOOL (WINAPI * LPALPHABLEND)(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION);
    static LPALPHABLEND lpAlphaBlend = (LPALPHABLEND) ::GetProcAddress(::GetModuleHandle(_T("msimg32.dll")), "AlphaBlend");

    if(lpAlphaBlend == NULL)
    {
        lpAlphaBlend = AlphaBitBlt;
    }

    if(hBitmap == NULL)
    {
        return;
    }

    HDC hCloneDC = ::CreateCompatibleDC(hDC);
    HBITMAP hOldBitmap = (HBITMAP) ::SelectObject(hCloneDC, hBitmap);
    ::SetStretchBltMode(hDC, HALFTONE);
    RECT rcTemp = {0};
    RECT rcDest = {0};

    if(lpAlphaBlend && (alphaChannel || uFade < 255))
    {
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, uFade, AC_SRC_ALPHA };

        // middle
        if(!hole)
        {
            rcDest.left = rc.left + rcCorners.left;
            rcDest.top = rc.top + rcCorners.top;
            rcDest.right = rc.right - rc.left - rcCorners.left - rcCorners.right;
            rcDest.bottom = rc.bottom - rc.top - rcCorners.top - rcCorners.bottom;
            rcDest.right += rcDest.left;
            rcDest.bottom += rcDest.top;

            if(::IntersectRect(&rcTemp, &rcPaint, &rcDest))
            {
                if(!xtiled && !ytiled)
                {
                    rcDest.right -= rcDest.left;
                    rcDest.bottom -= rcDest.top;
                    lpAlphaBlend(hDC,   // Ŀ��DC
                                 // Ŀ�������
                                 rcDest.left, rcDest.top, // X,Y
                                 rcDest.right, rcDest.bottom,    // Width,Height
                                 hCloneDC,   // ԴDC
                                 // Դ������
                                 rcBmpPart.left + rcCorners.left,    // X
                                 rcBmpPart.top + rcCorners.top,  // Y
                                 rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right,    // Width
                                 rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom,    // Height
                                 // Blend����
                                 bf);
                }
                else if(xtiled && ytiled)
                {
                    LONG lWidth = rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right;
                    LONG lHeight = rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom;
                    int iTimesX = (rcDest.right - rcDest.left + lWidth - 1) / lWidth;
                    int iTimesY = (rcDest.bottom - rcDest.top + lHeight - 1) / lHeight;

                    for(int j = 0; j < iTimesY; ++j)
                    {
                        LONG lDestTop = rcDest.top + lHeight * j;
                        LONG lDestBottom = rcDest.top + lHeight * (j + 1);
                        LONG lDrawHeight = lHeight;

                        if(lDestBottom > rcDest.bottom)
                        {
                            lDrawHeight -= lDestBottom - rcDest.bottom;
                            lDestBottom = rcDest.bottom;
                        }

                        for(int i = 0; i < iTimesX; ++i)
                        {
                            LONG lDestLeft = rcDest.left + lWidth * i;
                            LONG lDestRight = rcDest.left + lWidth * (i + 1);
                            LONG lDrawWidth = lWidth;

                            if(lDestRight > rcDest.right)
                            {
                                lDrawWidth -= lDestRight - rcDest.right;
                                lDestRight = rcDest.right;
                            }

                            lpAlphaBlend(hDC, rcDest.left + lWidth * i, rcDest.top + lHeight * j,
                                         lDestRight - lDestLeft, lDestBottom - lDestTop, hCloneDC,
                                         rcBmpPart.left + rcCorners.left, rcBmpPart.top + rcCorners.top, lDrawWidth, lDrawHeight, bf);
                        }
                    }
                }
                else if(xtiled)
                {
                    LONG lWidth = rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right;
                    int iTimes = (rcDest.right - rcDest.left + lWidth - 1) / lWidth;

                    for(int i = 0; i < iTimes; ++i)
                    {
                        LONG lDestLeft = rcDest.left + lWidth * i;
                        LONG lDestRight = rcDest.left + lWidth * (i + 1);
                        LONG lDrawWidth = lWidth;

                        if(lDestRight > rcDest.right)
                        {
                            lDrawWidth -= lDestRight - rcDest.right;
                            lDestRight = rcDest.right;
                        }

                        lpAlphaBlend(hDC, lDestLeft, rcDest.top, lDestRight - lDestLeft, rcDest.bottom,
                                     hCloneDC, rcBmpPart.left + rcCorners.left, rcBmpPart.top + rcCorners.top, \
                                     lDrawWidth, rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom, bf);
                    }
                }
                else   // ytiled
                {
                    LONG lHeight = rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom;
                    int iTimes = (rcDest.bottom - rcDest.top + lHeight - 1) / lHeight;

                    for(int i = 0; i < iTimes; ++i)
                    {
                        LONG lDestTop = rcDest.top + lHeight * i;
                        LONG lDestBottom = rcDest.top + lHeight * (i + 1);
                        LONG lDrawHeight = lHeight;

                        if(lDestBottom > rcDest.bottom)
                        {
                            lDrawHeight -= lDestBottom - rcDest.bottom;
                            lDestBottom = rcDest.bottom;
                        }

                        lpAlphaBlend(hDC, rcDest.left, rcDest.top + lHeight * i, rcDest.right, lDestBottom - lDestTop,
                                     hCloneDC, rcBmpPart.left + rcCorners.left, rcBmpPart.top + rcCorners.top, \
                                     rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right, lDrawHeight, bf);
                    }
                }
            }
        }

        // left-top
        if(rcCorners.left > 0 && rcCorners.top > 0)
        {
            rcDest.left = rc.left;
            rcDest.top = rc.top;
            rcDest.right = rcCorners.left;
            rcDest.bottom = rcCorners.top;
            rcDest.right += rcDest.left;
            rcDest.bottom += rcDest.top;

            if(::IntersectRect(&rcTemp, &rcPaint, &rcDest))
            {
                rcDest.right -= rcDest.left;
                rcDest.bottom -= rcDest.top;
                lpAlphaBlend(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                             rcBmpPart.left, rcBmpPart.top, rcCorners.left, rcCorners.top, bf);
            }
        }

        // top
        if(rcCorners.top > 0)
        {
            rcDest.left = rc.left + rcCorners.left;
            rcDest.top = rc.top;
            rcDest.right = rc.right - rc.left - rcCorners.left - rcCorners.right;
            rcDest.bottom = rcCorners.top;
            rcDest.right += rcDest.left;
            rcDest.bottom += rcDest.top;

            if(::IntersectRect(&rcTemp, &rcPaint, &rcDest))
            {
                rcDest.right -= rcDest.left;
                rcDest.bottom -= rcDest.top;
                lpAlphaBlend(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                             rcBmpPart.left + rcCorners.left, rcBmpPart.top, rcBmpPart.right - rcBmpPart.left - \
                             rcCorners.left - rcCorners.right, rcCorners.top, bf);
            }
        }

        // right-top
        if(rcCorners.right > 0 && rcCorners.top > 0)
        {
            rcDest.left = rc.right - rcCorners.right;
            rcDest.top = rc.top;
            rcDest.right = rcCorners.right;
            rcDest.bottom = rcCorners.top;
            rcDest.right += rcDest.left;
            rcDest.bottom += rcDest.top;

            if(::IntersectRect(&rcTemp, &rcPaint, &rcDest))
            {
                rcDest.right -= rcDest.left;
                rcDest.bottom -= rcDest.top;
                lpAlphaBlend(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                             rcBmpPart.right - rcCorners.right, rcBmpPart.top, rcCorners.right, rcCorners.top, bf);
            }
        }

        // left
        if(rcCorners.left > 0)
        {
            rcDest.left = rc.left;
            rcDest.top = rc.top + rcCorners.top;
            rcDest.right = rcCorners.left;
            rcDest.bottom = rc.bottom - rc.top - rcCorners.top - rcCorners.bottom;
            rcDest.right += rcDest.left;
            rcDest.bottom += rcDest.top;

            if(::IntersectRect(&rcTemp, &rcPaint, &rcDest))
            {
                rcDest.right -= rcDest.left;
                rcDest.bottom -= rcDest.top;
                lpAlphaBlend(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                             rcBmpPart.left, rcBmpPart.top + rcCorners.top, rcCorners.left, rcBmpPart.bottom - \
                             rcBmpPart.top - rcCorners.top - rcCorners.bottom, bf);
            }
        }

        // right
        if(rcCorners.right > 0)
        {
            rcDest.left = rc.right - rcCorners.right;
            rcDest.top = rc.top + rcCorners.top;
            rcDest.right = rcCorners.right;
            rcDest.bottom = rc.bottom - rc.top - rcCorners.top - rcCorners.bottom;
            rcDest.right += rcDest.left;
            rcDest.bottom += rcDest.top;

            if(::IntersectRect(&rcTemp, &rcPaint, &rcDest))
            {
                rcDest.right -= rcDest.left;
                rcDest.bottom -= rcDest.top;
                lpAlphaBlend(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                             rcBmpPart.right - rcCorners.right, rcBmpPart.top + rcCorners.top, rcCorners.right, \
                             rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom, bf);
            }
        }

        // left-bottom
        if(rcCorners.left > 0 && rcCorners.bottom > 0)
        {
            rcDest.left = rc.left;
            rcDest.top = rc.bottom - rcCorners.bottom;
            rcDest.right = rcCorners.left;
            rcDest.bottom = rcCorners.bottom;
            rcDest.right += rcDest.left;
            rcDest.bottom += rcDest.top;

            if(::IntersectRect(&rcTemp, &rcPaint, &rcDest))
            {
                rcDest.right -= rcDest.left;
                rcDest.bottom -= rcDest.top;
                lpAlphaBlend(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                             rcBmpPart.left, rcBmpPart.bottom - rcCorners.bottom, rcCorners.left, rcCorners.bottom, bf);
            }
        }

        // bottom
        if(rcCorners.bottom > 0)
        {
            rcDest.left = rc.left + rcCorners.left;
            rcDest.top = rc.bottom - rcCorners.bottom;
            rcDest.right = rc.right - rc.left - rcCorners.left - rcCorners.right;
            rcDest.bottom = rcCorners.bottom;
            rcDest.right += rcDest.left;
            rcDest.bottom += rcDest.top;

            if(::IntersectRect(&rcTemp, &rcPaint, &rcDest))
            {
                rcDest.right -= rcDest.left;
                rcDest.bottom -= rcDest.top;
                lpAlphaBlend(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                             rcBmpPart.left + rcCorners.left, rcBmpPart.bottom - rcCorners.bottom, \
                             rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right, rcCorners.bottom, bf);
            }
        }

        // right-bottom
        if(rcCorners.right > 0 && rcCorners.bottom > 0)
        {
            rcDest.left = rc.right - rcCorners.right;
            rcDest.top = rc.bottom - rcCorners.bottom;
            rcDest.right = rcCorners.right;
            rcDest.bottom = rcCorners.bottom;
            rcDest.right += rcDest.left;
            rcDest.bottom += rcDest.top;

            if(::IntersectRect(&rcTemp, &rcPaint, &rcDest))
            {
                rcDest.right -= rcDest.left;
                rcDest.bottom -= rcDest.top;
                lpAlphaBlend(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                             rcBmpPart.right - rcCorners.right, rcBmpPart.bottom - rcCorners.bottom, rcCorners.right, \
                             rcCorners.bottom, bf);
            }
        }
    }
    else
    {
        if(rc.right - rc.left == rcBmpPart.right - rcBmpPart.left \
                && rc.bottom - rc.top == rcBmpPart.bottom - rcBmpPart.top \
                && rcCorners.left == 0 && rcCorners.right == 0 && rcCorners.top == 0 && rcCorners.bottom == 0)
        {
            if(::IntersectRect(&rcTemp, &rcPaint, &rc))
            {
                ::BitBlt(hDC,       // Ŀ��DC
                         rcTemp.left, rcTemp.top,    // Ŀ��x��y
                         rcTemp.right - rcTemp.left, rcTemp.bottom - rcTemp.top,     // Ŀ��Wdith��Height
                         hCloneDC,   // ԴDC
                         rcBmpPart.left + rcTemp.left - rc.left, rcBmpPart.top + rcTemp.top - rc.top,        // Դx��y
                         SRCCOPY);
            }
        }
        else
        {
            // middle
            if(!hole)
            {
                rcDest.left = rc.left + rcCorners.left;
                rcDest.top = rc.top + rcCorners.top;
                rcDest.right = rc.right - rc.left - rcCorners.left - rcCorners.right;
                rcDest.bottom = rc.bottom - rc.top - rcCorners.top - rcCorners.bottom;
                rcDest.right += rcDest.left;
                rcDest.bottom += rcDest.top;

                if(::IntersectRect(&rcTemp, &rcPaint, &rcDest))
                {
                    if(!xtiled && !ytiled)
                    {
                        rcDest.right -= rcDest.left;
                        rcDest.bottom -= rcDest.top;
                        ::StretchBlt(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                                     rcBmpPart.left + rcCorners.left, rcBmpPart.top + rcCorners.top, \
                                     rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right, \
                                     rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom, SRCCOPY);
                    }
                    else if(xtiled && ytiled)
                    {
                        LONG lWidth = rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right;
                        LONG lHeight = rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom;
                        int iTimesX = (rcDest.right - rcDest.left + lWidth - 1) / lWidth;
                        int iTimesY = (rcDest.bottom - rcDest.top + lHeight - 1) / lHeight;

                        for(int j = 0; j < iTimesY; ++j)
                        {
                            LONG lDestTop = rcDest.top + lHeight * j;
                            LONG lDestBottom = rcDest.top + lHeight * (j + 1);
                            LONG lDrawHeight = lHeight;

                            if(lDestBottom > rcDest.bottom)
                            {
                                lDrawHeight -= lDestBottom - rcDest.bottom;
                                lDestBottom = rcDest.bottom;
                            }

                            for(int i = 0; i < iTimesX; ++i)
                            {
                                LONG lDestLeft = rcDest.left + lWidth * i;
                                LONG lDestRight = rcDest.left + lWidth * (i + 1);
                                LONG lDrawWidth = lWidth;

                                if(lDestRight > rcDest.right)
                                {
                                    lDrawWidth -= lDestRight - rcDest.right;
                                    lDestRight = rcDest.right;
                                }

                                ::BitBlt(hDC, rcDest.left + lWidth * i, rcDest.top + lHeight * j, \
                                         lDestRight - lDestLeft, lDestBottom - lDestTop, hCloneDC, \
                                         rcBmpPart.left + rcCorners.left, rcBmpPart.top + rcCorners.top, SRCCOPY);
                            }
                        }
                    }
                    else if(xtiled)
                    {
                        LONG lWidth = rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right;
                        int iTimes = (rcDest.right - rcDest.left + lWidth - 1) / lWidth;

                        for(int i = 0; i < iTimes; ++i)
                        {
                            LONG lDestLeft = rcDest.left + lWidth * i;
                            LONG lDestRight = rcDest.left + lWidth * (i + 1);
                            LONG lDrawWidth = lWidth;

                            if(lDestRight > rcDest.right)
                            {
                                lDrawWidth -= lDestRight - rcDest.right;
                                lDestRight = rcDest.right;
                            }

                            ::StretchBlt(hDC, lDestLeft, rcDest.top, lDestRight - lDestLeft, rcDest.bottom,
                                         hCloneDC, rcBmpPart.left + rcCorners.left, rcBmpPart.top + rcCorners.top, \
                                         lDrawWidth, rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom, SRCCOPY);
                        }
                    }
                    else   // ytiled
                    {
                        LONG lHeight = rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom;
                        int iTimes = (rcDest.bottom - rcDest.top + lHeight - 1) / lHeight;

                        for(int i = 0; i < iTimes; ++i)
                        {
                            LONG lDestTop = rcDest.top + lHeight * i;
                            LONG lDestBottom = rcDest.top + lHeight * (i + 1);
                            LONG lDrawHeight = lHeight;

                            if(lDestBottom > rcDest.bottom)
                            {
                                lDrawHeight -= lDestBottom - rcDest.bottom;
                                lDestBottom = rcDest.bottom;
                            }

                            ::StretchBlt(hDC, rcDest.left, rcDest.top + lHeight * i, rcDest.right, lDestBottom - lDestTop,
                                         hCloneDC, rcBmpPart.left + rcCorners.left, rcBmpPart.top + rcCorners.top, \
                                         rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right, lDrawHeight, SRCCOPY);
                        }
                    }
                }
            }

            // left-top
            if(rcCorners.left > 0 && rcCorners.top > 0)
            {
                rcDest.left = rc.left;
                rcDest.top = rc.top;
                rcDest.right = rcCorners.left;
                rcDest.bottom = rcCorners.top;
                rcDest.right += rcDest.left;
                rcDest.bottom += rcDest.top;

                if(::IntersectRect(&rcTemp, &rcPaint, &rcDest))
                {
                    rcDest.right -= rcDest.left;
                    rcDest.bottom -= rcDest.top;
                    ::StretchBlt(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                                 rcBmpPart.left, rcBmpPart.top, rcCorners.left, rcCorners.top, SRCCOPY);
                }
            }

            // top
            if(rcCorners.top > 0)
            {
                rcDest.left = rc.left + rcCorners.left;
                rcDest.top = rc.top;
                rcDest.right = rc.right - rc.left - rcCorners.left - rcCorners.right;
                rcDest.bottom = rcCorners.top;
                rcDest.right += rcDest.left;
                rcDest.bottom += rcDest.top;

                if(::IntersectRect(&rcTemp, &rcPaint, &rcDest))
                {
                    rcDest.right -= rcDest.left;
                    rcDest.bottom -= rcDest.top;
                    ::StretchBlt(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                                 rcBmpPart.left + rcCorners.left, rcBmpPart.top, rcBmpPart.right - rcBmpPart.left - \
                                 rcCorners.left - rcCorners.right, rcCorners.top, SRCCOPY);
                }
            }

            // right-top
            if(rcCorners.right > 0 && rcCorners.top > 0)
            {
                rcDest.left = rc.right - rcCorners.right;
                rcDest.top = rc.top;
                rcDest.right = rcCorners.right;
                rcDest.bottom = rcCorners.top;
                rcDest.right += rcDest.left;
                rcDest.bottom += rcDest.top;

                if(::IntersectRect(&rcTemp, &rcPaint, &rcDest))
                {
                    rcDest.right -= rcDest.left;
                    rcDest.bottom -= rcDest.top;
                    ::StretchBlt(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                                 rcBmpPart.right - rcCorners.right, rcBmpPart.top, rcCorners.right, rcCorners.top, SRCCOPY);
                }
            }

            // left
            if(rcCorners.left > 0)
            {
                rcDest.left = rc.left;
                rcDest.top = rc.top + rcCorners.top;
                rcDest.right = rcCorners.left;
                rcDest.bottom = rc.bottom - rc.top - rcCorners.top - rcCorners.bottom;
                rcDest.right += rcDest.left;
                rcDest.bottom += rcDest.top;

                if(::IntersectRect(&rcTemp, &rcPaint, &rcDest))
                {
                    rcDest.right -= rcDest.left;
                    rcDest.bottom -= rcDest.top;
                    ::StretchBlt(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                                 rcBmpPart.left, rcBmpPart.top + rcCorners.top, rcCorners.left, rcBmpPart.bottom - \
                                 rcBmpPart.top - rcCorners.top - rcCorners.bottom, SRCCOPY);
                }
            }

            // right
            if(rcCorners.right > 0)
            {
                rcDest.left = rc.right - rcCorners.right;
                rcDest.top = rc.top + rcCorners.top;
                rcDest.right = rcCorners.right;
                rcDest.bottom = rc.bottom - rc.top - rcCorners.top - rcCorners.bottom;
                rcDest.right += rcDest.left;
                rcDest.bottom += rcDest.top;

                if(::IntersectRect(&rcTemp, &rcPaint, &rcDest))
                {
                    rcDest.right -= rcDest.left;
                    rcDest.bottom -= rcDest.top;
                    ::StretchBlt(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                                 rcBmpPart.right - rcCorners.right, rcBmpPart.top + rcCorners.top, rcCorners.right, \
                                 rcBmpPart.bottom - rcBmpPart.top - rcCorners.top - rcCorners.bottom, SRCCOPY);
                }
            }

            // left-bottom
            if(rcCorners.left > 0 && rcCorners.bottom > 0)
            {
                rcDest.left = rc.left;
                rcDest.top = rc.bottom - rcCorners.bottom;
                rcDest.right = rcCorners.left;
                rcDest.bottom = rcCorners.bottom;
                rcDest.right += rcDest.left;
                rcDest.bottom += rcDest.top;

                if(::IntersectRect(&rcTemp, &rcPaint, &rcDest))
                {
                    rcDest.right -= rcDest.left;
                    rcDest.bottom -= rcDest.top;
                    ::StretchBlt(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                                 rcBmpPart.left, rcBmpPart.bottom - rcCorners.bottom, rcCorners.left, rcCorners.bottom, SRCCOPY);
                }
            }

            // bottom
            if(rcCorners.bottom > 0)
            {
                rcDest.left = rc.left + rcCorners.left;
                rcDest.top = rc.bottom - rcCorners.bottom;
                rcDest.right = rc.right - rc.left - rcCorners.left - rcCorners.right;
                rcDest.bottom = rcCorners.bottom;
                rcDest.right += rcDest.left;
                rcDest.bottom += rcDest.top;

                if(::IntersectRect(&rcTemp, &rcPaint, &rcDest))
                {
                    rcDest.right -= rcDest.left;
                    rcDest.bottom -= rcDest.top;
                    ::StretchBlt(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                                 rcBmpPart.left + rcCorners.left, rcBmpPart.bottom - rcCorners.bottom, \
                                 rcBmpPart.right - rcBmpPart.left - rcCorners.left - rcCorners.right, rcCorners.bottom, SRCCOPY);
                }
            }

            // right-bottom
            if(rcCorners.right > 0 && rcCorners.bottom > 0)
            {
                rcDest.left = rc.right - rcCorners.right;
                rcDest.top = rc.bottom - rcCorners.bottom;
                rcDest.right = rcCorners.right;
                rcDest.bottom = rcCorners.bottom;
                rcDest.right += rcDest.left;
                rcDest.bottom += rcDest.top;

                if(::IntersectRect(&rcTemp, &rcPaint, &rcDest))
                {
                    rcDest.right -= rcDest.left;
                    rcDest.bottom -= rcDest.top;
                    ::StretchBlt(hDC, rcDest.left, rcDest.top, rcDest.right, rcDest.bottom, hCloneDC, \
                                 rcBmpPart.right - rcCorners.right, rcBmpPart.bottom - rcCorners.bottom, rcCorners.right, \
                                 rcCorners.bottom, SRCCOPY);
                }
            }
        }
    }

    ::SelectObject(hCloneDC, hOldBitmap);
    ::DeleteDC(hCloneDC);
}


bool DrawImage(HDC hDC, CWindowUI *pManager, const RECT & rc, const RECT & rcPaint, const CDuiString & sImageName, \
               const CDuiString & sImageResType, RECT rcItem, RECT rcBmpPart, RECT rcCorner, DWORD dwMask, BYTE bFade, \
               bool bHole, bool bTiledX, bool bTiledY)
{
    if(sImageName.empty())
    {
        return false;
    }

    TImageData *data = NULL;

    if(sImageResType.empty())
    {
        data = GetResEngine()->GetImage(sImageName.c_str());
        //data = CUIEngine::GetInstance()->GetImageEx((LPCTSTR)sImageName.c_str(), NULL, dwMask);
    }

    //else
    //{
    //    data = CUIEngine::GetInstance()->GetImageEx((LPCTSTR)sImageName.c_str(), (LPCTSTR)sImageResType.c_str(), dwMask);
    //}

    if(!data)
    {
        return false;
    }

	// GC��ǽ׶�
	++(data->nCounter);

    if(rcBmpPart.left == 0 && rcBmpPart.right == 0 && rcBmpPart.top == 0 && rcBmpPart.bottom == 0)
    {
        rcBmpPart.right = data->nX;
        rcBmpPart.bottom = data->nY;
    }

    if(rcBmpPart.right > data->nX)
    {
        rcBmpPart.right = data->nX;
    }

    if(rcBmpPart.bottom > data->nY)
    {
        rcBmpPart.bottom = data->nY;
    }

    RECT rcTemp;

    if(!::IntersectRect(&rcTemp, &rcItem, &rc))
    {
        return true;
    }

    if(!::IntersectRect(&rcTemp, &rcItem, &rcPaint))
    {
        return true;
    }

    CRenderEngine::DrawImage(hDC, data->hBitmap, rcItem, rcPaint, rcBmpPart, rcCorner, data->alphaChannel, bFade, bHole, bTiledX, bTiledY);
    return true;
}

bool CRenderEngine::DrawImageString(HDC hDC, CWindowUI *pManager, const RECT & rc, const RECT & rcPaint,
                                    LPCTSTR pStrImage, LPCTSTR pStrModify)
{
    if((pManager == NULL) || (hDC == NULL))
    {
        return false;
    }

    // 1��aaa.jpg
    // 2��file='aaa.jpg' res='' restype='0' dest='0,0,0,0' source='0,0,0,0' corner='0,0,0,0'
    // mask='#FF0000' fade='255' hole='false' xtiled='false' ytiled='false'
    CDuiString sImageName = pStrImage;
    CDuiString sImageResType;
    RECT rcItem = rc;
    RECT rcBmpPart = {0};
    RECT rcCorner = {0};
    DWORD dwMask = 0;
    BYTE bFade = 0xFF;
    bool bHole = false;
    bool bTiledX = false;
    bool bTiledY = false;
    int image_count = 0;
    CDuiString sItem;
    CDuiString sValue;
    LPTSTR pstr = NULL;

    for(int i = 0; i < 2; ++i, image_count = 0)
    {
        if(i == 1)
        {
            pStrImage = pStrModify;
        }

        if(!pStrImage)
        {
            continue;
        }

        while(*pStrImage != _T('\0'))
        {
            sItem.clear();
            sValue.clear();

            while(*pStrImage > _T('\0') && *pStrImage <= _T(' '))
            {
                pStrImage = ::CharNext(pStrImage);
            }

            while(*pStrImage != _T('\0') && *pStrImage != _T('=') && *pStrImage > _T(' '))
            {
                LPTSTR pstrTemp = ::CharNext(pStrImage);

                while(pStrImage < pstrTemp)
                {
                    sItem += *pStrImage++;
                }
            }

            while(*pStrImage > _T('\0') && *pStrImage <= _T(' '))
            {
                pStrImage = ::CharNext(pStrImage);
            }

            if(*pStrImage++ != _T('='))
            {
                break;
            }

            while(*pStrImage > _T('\0') && *pStrImage <= _T(' '))
            {
                pStrImage = ::CharNext(pStrImage);
            }

            if(*pStrImage++ != _T('\''))
            {
                break;
            }

            while(*pStrImage != _T('\0') && *pStrImage != _T('\''))
            {
                LPTSTR pstrTemp = ::CharNext(pStrImage);

                while(pStrImage < pstrTemp)
                {
                    sValue += *pStrImage++;
                }
            }

            if(*pStrImage++ != _T('\''))
            {
                break;
            }

            if(!sValue.empty())
            {
                if(sItem.compare(_T("file")) == 0 || sItem.compare(_T("res")) ==  0)
                {
                    if(image_count > 0)
                        DuiLib::DrawImage(hDC, pManager, rc, rcPaint, sImageName, sImageResType,
                                          rcItem, rcBmpPart, rcCorner, dwMask, bFade, bHole, bTiledX, bTiledY);

                    sImageName = sValue;

                    if(sItem.compare(_T("file")) == 0)
                    {
                        ++image_count;
                    }
                }
                else if(sItem.compare(_T("restype")) == 0)
                {
                    if(image_count > 0)
                        DuiLib::DrawImage(hDC, pManager, rc, rcPaint, sImageName, sImageResType,
                                          rcItem, rcBmpPart, rcCorner, dwMask, bFade, bHole, bTiledX, bTiledY);

                    sImageResType = sValue;
                    ++image_count;
                }
                else if(sItem.compare(_T("dest")) == 0)
                {
                    rcItem.left = rc.left + _tcstol(sValue.c_str(), &pstr, 10);
                    ASSERT(pstr);
                    rcItem.top = rc.top + _tcstol(pstr + 1, &pstr, 10);
                    ASSERT(pstr);
                    rcItem.right = rc.left + _tcstol(pstr + 1, &pstr, 10);
                    ASSERT(pstr);

                    if(rcItem.right > rc.right)
                    {
                        rcItem.right = rc.right;
                    }

                    rcItem.bottom = rc.top + _tcstol(pstr + 1, &pstr, 10);
                    ASSERT(pstr);

                    if(rcItem.bottom > rc.bottom)
                    {
                        rcItem.bottom = rc.bottom;
                    }
                }
                else if(sItem.compare(_T("source")) == 0)
                {
                    rcBmpPart.left = _tcstol(sValue.c_str(), &pstr, 10);
                    ASSERT(pstr);
                    rcBmpPart.top = _tcstol(pstr + 1, &pstr, 10);
                    ASSERT(pstr);
                    rcBmpPart.right = _tcstol(pstr + 1, &pstr, 10);
                    ASSERT(pstr);
                    rcBmpPart.bottom = _tcstol(pstr + 1, &pstr, 10);
                    ASSERT(pstr);
                }
                else if(sItem.compare(_T("corner")) == 0)
                {
                    rcCorner.left = _tcstol(sValue.c_str(), &pstr, 10);
                    ASSERT(pstr);
                    rcCorner.top = _tcstol(pstr + 1, &pstr, 10);
                    ASSERT(pstr);
                    rcCorner.right = _tcstol(pstr + 1, &pstr, 10);
                    ASSERT(pstr);
                    rcCorner.bottom = _tcstol(pstr + 1, &pstr, 10);
                    ASSERT(pstr);
                }
                else if(sItem.compare(_T("mask")) == 0)
                {
                    if(sValue[0] == _T('#'))
                    {
                        dwMask = _tcstoul(sValue.c_str() + 1, &pstr, 16);
                    }
                    else
                    {
                        dwMask = _tcstoul(sValue.c_str(), &pstr, 16);
                    }
                }
                else if(sItem.compare(_T("fade")) == 0)
                {
                    bFade = (BYTE)_tcstoul(sValue.c_str(), &pstr, 10);
                }
                else if(sItem.compare(_T("hole")) == 0)
                {
                    bHole = (_tcscmp(sValue.c_str(), _T("true")) == 0);
                }
                else if(sItem.compare(_T("xtiled")) == 0)
                {
                    bTiledX = (_tcscmp(sValue.c_str(), _T("true")) == 0);
                }
                else if(sItem.compare(_T("ytiled")) == 0)
                {
                    bTiledY = (_tcscmp(sValue.c_str(), _T("true")) == 0);
                }
            }

            if(*pStrImage++ != _T(' '))
            {
                break;
            }
        }
    }

    DuiLib::DrawImage(hDC, pManager, rc, rcPaint, sImageName, sImageResType,
                      rcItem, rcBmpPart, rcCorner, dwMask, bFade, bHole, bTiledX, bTiledY);
    return true;
}

void CRenderEngine::DrawColor(HDC hDC, const RECT & rc, DWORD color)
{
    if(color <= 0x00FFFFFF)
    {
        return;
    }

    if(color >= 0xFF000000)
    {
        ::SetBkColor(hDC, RGB(GetBValue(color), GetGValue(color), GetRValue(color)));
        ::ExtTextOut(hDC, 0, 0, ETO_OPAQUE, &rc, NULL, 0, NULL);
    }
    else
    {
        // Create a new 32bpp bitmap with room for an alpha channel
        BITMAPINFO bmi = { 0 };
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = 1;
        bmi.bmiHeader.biHeight = 1;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        bmi.bmiHeader.biSizeImage = 1 * 1 * sizeof(DWORD);
        LPDWORD pDest = NULL;
        HBITMAP hBitmap = ::CreateDIBSection(hDC, &bmi, DIB_RGB_COLORS, (LPVOID *) &pDest, NULL, 0);

        if(!hBitmap)
        {
            return;
        }

        *pDest = color;
        RECT rcBmpPart = {0, 0, 1, 1};
        RECT rcCorners = {0};
        DrawImage(hDC, hBitmap, rc, rc, rcBmpPart, rcCorners, true, 255);
        ::DeleteObject(hBitmap);
    }
}

void CRenderEngine::DrawGradient(HDC hDC, const RECT & rc, DWORD dwFirst, DWORD dwSecond, bool bVertical, int nSteps)
{
    typedef BOOL (WINAPI * LPALPHABLEND)(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION);
    static LPALPHABLEND lpAlphaBlend = (LPALPHABLEND) ::GetProcAddress(::GetModuleHandle(_T("msimg32.dll")), "AlphaBlend");

    if(lpAlphaBlend == NULL)
    {
        lpAlphaBlend = AlphaBitBlt;
    }

    typedef BOOL (WINAPI * PGradientFill)(HDC, PTRIVERTEX, ULONG, PVOID, ULONG, ULONG);
    static PGradientFill lpGradientFill = (PGradientFill) ::GetProcAddress(::GetModuleHandle(_T("msimg32.dll")), "GradientFill");
    BYTE bAlpha = (BYTE)(((dwFirst >> 24) + (dwSecond >> 24)) >> 1);

    if(bAlpha == 0)
    {
        return;
    }

    int cx = rc.right - rc.left;
    int cy = rc.bottom - rc.top;
    RECT rcPaint = rc;
    HDC hPaintDC = hDC;
    HBITMAP hPaintBitmap = NULL;
    HBITMAP hOldPaintBitmap = NULL;

    if(bAlpha < 255)
    {
        rcPaint.left = rcPaint.top = 0;
        rcPaint.right = cx;
        rcPaint.bottom = cy;
        hPaintDC = ::CreateCompatibleDC(hDC);
        hPaintBitmap = ::CreateCompatibleBitmap(hDC, cx, cy);
        ASSERT(hPaintDC);
        ASSERT(hPaintBitmap);
        hOldPaintBitmap = (HBITMAP) ::SelectObject(hPaintDC, hPaintBitmap);
    }

    if(lpGradientFill != NULL)
    {
        TRIVERTEX triv[2] =
        {
            { rcPaint.left, rcPaint.top, GetBValue(dwFirst) << 8, GetGValue(dwFirst) << 8, GetRValue(dwFirst) << 8, 0xFF00 },
            { rcPaint.right, rcPaint.bottom, GetBValue(dwSecond) << 8, GetGValue(dwSecond) << 8, GetRValue(dwSecond) << 8, 0xFF00 }
        };
        GRADIENT_RECT grc = { 0, 1 };
        lpGradientFill(hPaintDC, triv, 2, &grc, 1, bVertical ? GRADIENT_FILL_RECT_V : GRADIENT_FILL_RECT_H);
    }
    else
    {
        // Determine how many shades
        int nShift = 1;

        if(nSteps >= 64)
        {
            nShift = 6;
        }
        else if(nSteps >= 32)
        {
            nShift = 5;
        }
        else if(nSteps >= 16)
        {
            nShift = 4;
        }
        else if(nSteps >= 8)
        {
            nShift = 3;
        }
        else if(nSteps >= 4)
        {
            nShift = 2;
        }

        int nLines = 1 << nShift;

        for(int i = 0; i < nLines; i++)
        {
            // Do a little alpha blending
            BYTE bR = (BYTE)((GetBValue(dwSecond) * (nLines - i) + GetBValue(dwFirst) * i) >> nShift);
            BYTE bG = (BYTE)((GetGValue(dwSecond) * (nLines - i) + GetGValue(dwFirst) * i) >> nShift);
            BYTE bB = (BYTE)((GetRValue(dwSecond) * (nLines - i) + GetRValue(dwFirst) * i) >> nShift);
            // ... then paint with the resulting color
            HBRUSH hBrush = ::CreateSolidBrush(RGB(bR, bG, bB));
            RECT r2 = rcPaint;

            if(bVertical)
            {
                r2.bottom = rc.bottom - ((i * (rc.bottom - rc.top)) >> nShift);
                r2.top = rc.bottom - (((i + 1) * (rc.bottom - rc.top)) >> nShift);

                if((r2.bottom - r2.top) > 0)
                {
                    ::FillRect(hDC, &r2, hBrush);
                }
            }
            else
            {
                r2.left = rc.right - (((i + 1) * (rc.right - rc.left)) >> nShift);
                r2.right = rc.right - ((i * (rc.right - rc.left)) >> nShift);

                if((r2.right - r2.left) > 0)
                {
                    ::FillRect(hPaintDC, &r2, hBrush);
                }
            }

            ::DeleteObject(hBrush);
        }
    }

    if(bAlpha < 255)
    {
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, bAlpha, AC_SRC_ALPHA };
        lpAlphaBlend(hDC, rc.left, rc.top, cx, cy, hPaintDC, 0, 0, cx, cy, bf);
        ::SelectObject(hPaintDC, hOldPaintBitmap);
        ::DeleteObject(hPaintBitmap);
        ::DeleteDC(hPaintDC);
    }
}

//************************************
// ��������: DrawLine
// ��������: void
// ������Ϣ: HDC hDC
// ������Ϣ: const RECT & rc
// ������Ϣ: int nSize
// ������Ϣ: DWORD dwPenColor
// ������Ϣ: int nStyle
// ����˵��:
//************************************
void CRenderEngine::DrawLine(HDC hDC, const RECT & rc, int nSize, DWORD dwPenColor, int nStyle /*= PS_SOLID*/)
{
    ASSERT(::GetObjectType(hDC) == OBJ_DC || ::GetObjectType(hDC) == OBJ_MEMDC);
    LOGPEN lg;
    lg.lopnColor = RGB(GetBValue(dwPenColor), GetGValue(dwPenColor), GetRValue(dwPenColor));
    lg.lopnStyle = nStyle;
    lg.lopnWidth.x = nSize;
    HPEN hPen = CreatePenIndirect(&lg);
    HPEN hOldPen = (HPEN)::SelectObject(hDC, hPen);
    POINT ptTemp = { 0 };
    ::MoveToEx(hDC, rc.left, rc.top, &ptTemp);
    ::LineTo(hDC, rc.right, rc.bottom);
    ::SelectObject(hDC, hOldPen);
    ::DeleteObject(hPen);
}

void CRenderEngine::DrawRect(HDC hDC, const RECT & rc, int nSize, DWORD dwPenColor)
{
    ASSERT(::GetObjectType(hDC) == OBJ_DC || ::GetObjectType(hDC) == OBJ_MEMDC);
    HPEN hPen = ::CreatePen(PS_SOLID | PS_INSIDEFRAME, nSize, RGB(GetBValue(dwPenColor), GetGValue(dwPenColor), GetRValue(dwPenColor)));
    HPEN hOldPen = (HPEN)::SelectObject(hDC, hPen);
    ::SelectObject(hDC, ::GetStockObject(HOLLOW_BRUSH));
    ::Rectangle(hDC, rc.left, rc.top, rc.right, rc.bottom);
    ::SelectObject(hDC, hOldPen);
    ::DeleteObject(hPen);
}

void CRenderEngine::DrawRoundRect(HDC hDC, const RECT & rc, int nSize, int width, int height, DWORD dwPenColor)
{
    ASSERT(::GetObjectType(hDC) == OBJ_DC || ::GetObjectType(hDC) == OBJ_MEMDC);
    HPEN hPen = ::CreatePen(PS_SOLID | PS_INSIDEFRAME, nSize, RGB(GetBValue(dwPenColor), GetGValue(dwPenColor), GetRValue(dwPenColor)));
    HPEN hOldPen = (HPEN)::SelectObject(hDC, hPen);
    ::SelectObject(hDC, ::GetStockObject(HOLLOW_BRUSH));
    ::RoundRect(hDC, rc.left, rc.top, rc.right, rc.bottom, width, height);
    ::SelectObject(hDC, hOldPen);
    ::DeleteObject(hPen);
}

void CRenderEngine::DrawText(HDC hDC, CWindowUI *pManager, RECT & rc, LPCTSTR pstrText, DWORD dwTextColor, LPCTSTR pstrFontID, UINT uStyle)
{
    ASSERT(::GetObjectType(hDC) == OBJ_DC || ::GetObjectType(hDC) == OBJ_MEMDC);

    if(pstrText == NULL || pManager == NULL)
    {
        return;
    }

    if(GetBValue(dwTextColor) == 0
            && GetGValue(dwTextColor) == 0
            && GetRValue(dwTextColor) == 0)
    {
        dwTextColor = COLOR_ARGB(GetAValue(dwTextColor), 1, 1, 1);
    }

	CDuiString strText;
	CResEngine::GetInstance()->ProcessString(strText,pstrText);

    ::SetBkMode(hDC, TRANSPARENT);
    ::SetTextColor(hDC, RGB(GetBValue(dwTextColor), GetGValue(dwTextColor), GetRValue(dwTextColor)));
    HFONT hOldFont = (HFONT)::SelectObject(hDC, GetResEngine()->GetFont(pstrFontID)->GetFont());
    ::DrawText(hDC, strText.c_str(), -1, &rc, uStyle | DT_NOPREFIX);
    ::SelectObject(hDC, hOldFont);
}

void CRenderEngine::DrawHtmlText(HDC hDC, CWindowUI *pManager, RECT & rc, LPCTSTR pstrText, DWORD dwTextColor, RECT *prcLinks, CDuiString *sLinks, int & nLinkRects, UINT uStyle)
{
    // ���ǵ���xml�༭����ʹ��<>���Ų����㣬����ʹ��{}���Ŵ���
    // ֧�ֱ�ǩǶ�ף���<l><b>text</b></l>�������ǽ���Ƕ����Ӧ�ñ���ģ���<l><b>text</l></b>��
    // The string formatter supports a kind of "mini-html" that consists of various short tags:
    //
    //   Bold:             <b>text</b>
    //   Color:            <c #xxxxxx>text</c>  where x = RGB in hex
    //   Font:             <f x>text</f>        where x = font id
    //   Italic:           <i>text</i>
    //   Image:            <i x y z>            where x = image name and y = imagelist num and z(optional) = imagelist id
    //   Link:             <a x>text</a>        where x(optional) = link content, normal like app:notepad or http:www.xxx.com
    //   NewLine           <n>
    //   Paragraph:        <p x>text</p>        where x = extra pixels indent in p
    //   Raw Text:         <r>text</r>
    //   Selected:         <s>text</s>
    //   Underline:        <u>text</u>
    //   X Indent:         <x i>                where i = hor indent in pixels
    //   Y Indent:         <y i>                where i = ver indent in pixels
    ASSERT(::GetObjectType(hDC) == OBJ_DC || ::GetObjectType(hDC) == OBJ_MEMDC);

    if(pstrText == NULL || pManager == NULL)
    {
        return;
    }

    if(::IsRectEmpty(&rc))
    {
        return;
    }

    bool bDraw = (uStyle & DT_CALCRECT) == 0;
    CStdPtrArray aFontArray(10);
    CStdPtrArray aColorArray(10);
    CStdPtrArray aPIndentArray(10);
    RECT rcClip = { 0 };
    ::GetClipBox(hDC, &rcClip);
    HRGN hOldRgn = ::CreateRectRgnIndirect(&rcClip);
    HRGN hRgn = ::CreateRectRgnIndirect(&rc);

    if(bDraw)
    {
        ::ExtSelectClipRgn(hDC, hRgn, RGN_AND);
    }

    FontObject *pDefaultFont = GetResEngine()->GetDefaultFont();
    TEXTMETRIC *pTm = &pDefaultFont->GetTextMetric();
    HFONT hOldFont = (HFONT) ::SelectObject(hDC, pDefaultFont->GetFont());
    ::SetBkMode(hDC, TRANSPARENT);
    ::SetTextColor(hDC, RGB(GetBValue(dwTextColor), GetGValue(dwTextColor), GetRValue(dwTextColor)));
    DWORD dwBkColor = GetResEngine()->GetDefaultColor(_T("SelectedBkColor"));
    ::SetBkColor(hDC, RGB(GetBValue(dwBkColor), GetGValue(dwBkColor), GetRValue(dwBkColor)));

    // If the drawstyle include a alignment, we'll need to first determine the text-size so
    // we can draw it at the correct position...
    if(((uStyle & DT_CENTER) != 0 || (uStyle & DT_RIGHT) != 0 || (uStyle & DT_VCENTER) != 0 || (uStyle & DT_BOTTOM) != 0) && (uStyle & DT_CALCRECT) == 0)
    {
        RECT rcText = { 0, 0, 9999, 100 };
        int nLinks = 0;
        DrawHtmlText(hDC, pManager, rcText, pstrText, dwTextColor, NULL, NULL, nLinks, uStyle | DT_CALCRECT);

        if((uStyle & DT_SINGLELINE) != 0)
        {
            if((uStyle & DT_CENTER) != 0)
            {
                rc.left = rc.left + ((rc.right - rc.left) / 2) - ((rcText.right - rcText.left) / 2);
                rc.right = rc.left + (rcText.right - rcText.left);
            }

            if((uStyle & DT_RIGHT) != 0)
            {
                rc.left = rc.right - (rcText.right - rcText.left);
            }
        }

        if((uStyle & DT_VCENTER) != 0)
        {
            rc.top = rc.top + ((rc.bottom - rc.top) / 2) - ((rcText.bottom - rcText.top) / 2);
            rc.bottom = rc.top + (rcText.bottom - rcText.top);
        }

        if((uStyle & DT_BOTTOM) != 0)
        {
            rc.top = rc.bottom - (rcText.bottom - rcText.top);
        }
    }

    bool bHoverLink = false;
    CDuiString sHoverLink;
    POINT ptMouse = pManager->GetMousePos();

    for(int i = 0; !bHoverLink && i < nLinkRects; i++)
    {
        if(::PtInRect(prcLinks + i, ptMouse))
        {
            sHoverLink = *(CDuiString *)(sLinks + i);
            bHoverLink = true;
        }
    }

    POINT pt = { rc.left, rc.top };
    int iLinkIndex = 0;
    int cyLine = pTm->tmHeight + pTm->tmExternalLeading + (int)aPIndentArray.GetAt(aPIndentArray.GetSize() - 1);
    int cyMinHeight = 0;
    int cxMaxWidth = 0;
    POINT ptLinkStart = { 0 };
    bool bLineEnd = false;
    bool bInRaw = false;
    bool bInLink = false;
    bool bInSelected = false;
    int iLineLinkIndex = 0;
    // �Ű�ϰ����ͼ�ĵײ����룬����ÿ�л��ƶ�Ҫ���������ȼ���߶ȣ��ٻ���
    CStdPtrArray aLineFontArray;
    CStdPtrArray aLineColorArray;
    CStdPtrArray aLinePIndentArray;
    LPCTSTR pstrLineBegin = pstrText;
    bool bLineInRaw = false;
    bool bLineInLink = false;
    bool bLineInSelected = false;
    int cyLineHeight = 0;
    bool bLineDraw = false; // �еĵڶ��׶Σ�����
    //while(*pstrText != _T('\0'))
    //{
    //    if(pt.x >= rc.right || *pstrText == _T('\n') || bLineEnd)
    //    {
    //        if(*pstrText == _T('\n'))
    //        {
    //            pstrText++;
    //        }
    //        if(bLineEnd)
    //        {
    //            bLineEnd = false;
    //        }
    //        if(!bLineDraw)
    //        {
    //            if(bInLink && iLinkIndex < nLinkRects)
    //            {
    //                ::SetRect(&prcLinks[iLinkIndex++], ptLinkStart.x, ptLinkStart.y, MIN(pt.x, rc.right), pt.y + cyLine);
    //                CDuiString *pStr1 = (CDuiString *)(sLinks + iLinkIndex - 1);
    //                CDuiString *pStr2 = (CDuiString *)(sLinks + iLinkIndex);
    //                *pStr2 = *pStr1;
    //            }
    //            for(int i = iLineLinkIndex; i < iLinkIndex; i++)
    //            {
    //                prcLinks[i].bottom = pt.y + cyLine;
    //            }
    //            if(bDraw)
    //            {
    //                bInLink = bLineInLink;
    //                iLinkIndex = iLineLinkIndex;
    //            }
    //        }
    //        else
    //        {
    //            if(bInLink && iLinkIndex < nLinkRects)
    //            {
    //                iLinkIndex++;
    //            }
    //            bLineInLink = bInLink;
    //            iLineLinkIndex = iLinkIndex;
    //        }
    //        if((uStyle & DT_SINGLELINE) != 0 && (!bDraw || bLineDraw))
    //        {
    //            break;
    //        }
    //        if(bDraw)
    //        {
    //            bLineDraw = !bLineDraw;    // !
    //        }
    //        pt.x = rc.left;
    //        if(!bLineDraw)
    //        {
    //            pt.y += cyLine;
    //        }
    //        if(pt.y > rc.bottom && bDraw)
    //        {
    //            break;
    //        }
    //        ptLinkStart = pt;
    //        cyLine = pTm->tmHeight + pTm->tmExternalLeading + (int)aPIndentArray.GetAt(aPIndentArray.GetSize() - 1);
    //        if(pt.x >= rc.right)
    //        {
    //            break;
    //        }
    //    }
    //    else if(!bInRaw && (*pstrText == _T('<') || *pstrText == _T('{'))
    //            && (pstrText[1] >= _T('a') && pstrText[1] <= _T('z'))
    //            && (pstrText[2] == _T(' ') || pstrText[2] == _T('>') || pstrText[2] == _T('}')))
    //    {
    //        pstrText++;
    //        LPCTSTR pstrNextStart = NULL;
    //        switch(*pstrText)
    //        {
    //            case _T('a'):  // Link
    //                {
    //                    pstrText++;
    //                    while(*pstrText > _T('\0') && *pstrText <= _T(' '))
    //                    {
    //                        pstrText = ::CharNext(pstrText);
    //                    }
    //                    if(iLinkIndex < nLinkRects && !bLineDraw)
    //                    {
    //                        CDuiString *pStr = (CDuiString *)(sLinks + iLinkIndex);
    //                        pStr->clear();
    //                        while(*pstrText != _T('\0') && *pstrText != _T('>') && *pstrText != _T('}'))
    //                        {
    //                            LPCTSTR pstrTemp = ::CharNext(pstrText);
    //                            while(pstrText < pstrTemp)
    //                            {
    //                                *pStr += *pstrText++;
    //                            }
    //                        }
    //                    }
    //                    DWORD clrColor =  GetResEngine()->GetDefaultColor(_T("LinkFontColor"));
    //                    if(bHoverLink && iLinkIndex < nLinkRects)
    //                    {
    //                        CDuiString *pStr = (CDuiString *)(sLinks + iLinkIndex);
    //                        if(sHoverLink.compare(pStr->c_str()) == 0)
    //                        {
    //                            clrColor = GetResEngine()->GetDefaultColor(_T("LinkHoverFontColor"));
    //                        }
    //                    }
    //                    else if(prcLinks == NULL)
    //                    {
    //                        if(::PtInRect(&rc, ptMouse))
    //                        {
    //                            clrColor = GetResEngine()->GetDefaultColor(_T("LinkHoverFontColor"));
    //                        }
    //                    }
    //                    aColorArray.Add((LPVOID)clrColor);
    //                    ::SetTextColor(hDC,  RGB(GetBValue(clrColor), GetGValue(clrColor), GetRValue(clrColor)));
    //      FontObject* pFont = GetResEngine()->GetDefaultFont();
    //                    TFontInfo *pFontInfo = CUIEngine::GetInstance()->GetDefaultFontInfo();
    //                    if(aFontArray.GetSize() > 0)
    //                    {
    //                        pFontInfo = (TFontInfo *)aFontArray.GetAt(aFontArray.GetSize() - 1);
    //                    }
    //                    if(pFontInfo->bUnderline == false)
    //                    {
    //                        HFONT hFont = CUIEngine::GetInstance()->GetFont(pFontInfo->strFaceName.c_str(), pFontInfo->iSize, pFontInfo->bBold, true, pFontInfo->bItalic);
    //                        if(hFont == NULL)
    //                        {
    //                            hFont = CUIEngine::GetInstance()->AddFont(pFontInfo->strFaceName.c_str(), pFontInfo->iSize, pFontInfo->bBold, true, pFontInfo->bItalic);
    //                        }
    //                        pFontInfo = CUIEngine::GetInstance()->GetFontInfo(hFont);
    //                        aFontArray.Add(pFontInfo);
    //                        pTm = &pFontInfo->tm;
    //                        ::SelectObject(hDC, pFontInfo->hFont);
    //                        cyLine = MAX(cyLine, pTm->tmHeight + pTm->tmExternalLeading + (int)aPIndentArray.GetAt(aPIndentArray.GetSize() - 1));
    //                    }
    //                    ptLinkStart = pt;
    //                    bInLink = true;
    //                }
    //                break;
    //            case _T('b'):  // Bold
    //                {
    //                    pstrText++;
    //                    TFontInfo *pFontInfo = CUIEngine::GetInstance()->GetDefaultFontInfo();
    //                    if(aFontArray.GetSize() > 0)
    //                    {
    //                        pFontInfo = (TFontInfo *)aFontArray.GetAt(aFontArray.GetSize() - 1);
    //                    }
    //                    if(pFontInfo->bBold == false)
    //                    {
    //                        HFONT hFont = CUIEngine::GetInstance()->GetFont(pFontInfo->strFaceName.c_str(), pFontInfo->iSize, true, pFontInfo->bUnderline, pFontInfo->bItalic);
    //                        if(hFont == NULL)
    //                        {
    //                            hFont = CUIEngine::GetInstance()->AddFont(pFontInfo->strFaceName.c_str(), pFontInfo->iSize, true, pFontInfo->bUnderline, pFontInfo->bItalic);
    //                        }
    //                        pFontInfo = CUIEngine::GetInstance()->GetFontInfo(hFont);
    //                        aFontArray.Add(pFontInfo);
    //                        pTm = &pFontInfo->tm;
    //                        ::SelectObject(hDC, pFontInfo->hFont);
    //                        cyLine = MAX(cyLine, pTm->tmHeight + pTm->tmExternalLeading + (int)aPIndentArray.GetAt(aPIndentArray.GetSize() - 1));
    //                    }
    //                }
    //                break;
    //            case _T('c'):  // Color
    //                {
    //                    pstrText++;
    //                    while(*pstrText > _T('\0') && *pstrText <= _T(' '))
    //                    {
    //                        pstrText = ::CharNext(pstrText);
    //                    }
    //                    if(*pstrText == _T('#'))
    //                    {
    //                        pstrText++;
    //                    }
    //                    DWORD clrColor = _tcstol(pstrText, const_cast<LPTSTR *>(&pstrText), 16);
    //                    aColorArray.Add((LPVOID)clrColor);
    //                    ::SetTextColor(hDC, RGB(GetBValue(clrColor), GetGValue(clrColor), GetRValue(clrColor)));
    //                }
    //                break;
    //            case _T('f'):  // Font
    //                {
    //                    pstrText++;
    //                    while(*pstrText > _T('\0') && *pstrText <= _T(' '))
    //                    {
    //                        pstrText = ::CharNext(pstrText);
    //                    }
    //                    LPCTSTR pstrTemp = pstrText;
    //                    int iFont = (int) _tcstol(pstrText, const_cast<LPTSTR *>(&pstrText), 10);
    //                    //if( isdigit(*pstrText) ) { // debug�汾�������쳣
    //                    if(pstrTemp != pstrText)
    //                    {
    //                        TFontInfo *pFontInfo = CUIEngine::GetInstance()->GetFontInfo(iFont);
    //                        aFontArray.Add(pFontInfo);
    //                        pTm = &pFontInfo->tm;
    //                        ::SelectObject(hDC, pFontInfo->hFont);
    //                    }
    //                    else
    //                    {
    //                        CDuiString sFontName;
    //                        int iFontSize = 10;
    //                        CDuiString sFontAttr;
    //                        bool bBold = false;
    //                        bool bUnderline = false;
    //                        bool bItalic = false;
    //                        while(*pstrText != _T('\0') && *pstrText != _T('>') && *pstrText != _T('}') && *pstrText != _T(' '))
    //                        {
    //                            pstrTemp = ::CharNext(pstrText);
    //                            while(pstrText < pstrTemp)
    //                            {
    //                                sFontName += *pstrText++;
    //                            }
    //                        }
    //                        while(*pstrText > _T('\0') && *pstrText <= _T(' '))
    //                        {
    //                            pstrText = ::CharNext(pstrText);
    //                        }
    //                        if(isdigit(*pstrText))
    //                        {
    //                            iFontSize = (int) _tcstol(pstrText, const_cast<LPTSTR *>(&pstrText), 10);
    //                        }
    //                        while(*pstrText > _T('\0') && *pstrText <= _T(' '))
    //                        {
    //                            pstrText = ::CharNext(pstrText);
    //                        }
    //                        while(*pstrText != _T('\0') && *pstrText != _T('>') && *pstrText != _T('}'))
    //                        {
    //                            pstrTemp = ::CharNext(pstrText);
    //                            while(pstrText < pstrTemp)
    //                            {
    //                                sFontAttr += *pstrText++;
    //                            }
    //                        }
    //                        CDuiStringOperation::lower(sFontAttr);
    //                        if(sFontAttr.find(_T("bold")) >= 0)
    //                        {
    //                            bBold = true;
    //                        }
    //                        if(sFontAttr.find(_T("underline")) >= 0)
    //                        {
    //                            bUnderline = true;
    //                        }
    //                        if(sFontAttr.find(_T("italic")) >= 0)
    //                        {
    //                            bItalic = true;
    //                        }
    //                        HFONT hFont = CUIEngine::GetInstance()->GetFont(sFontName.c_str(), iFontSize, bBold, bUnderline, bItalic);
    //                        if(hFont == NULL)
    //                        {
    //                            hFont = CUIEngine::GetInstance()->AddFont(sFontName.c_str(), iFontSize, bBold, bUnderline, bItalic);
    //                        }
    //                        TFontInfo *pFontInfo = CUIEngine::GetInstance()->GetFontInfo(hFont);
    //                        aFontArray.Add(pFontInfo);
    //                        pTm = &pFontInfo->tm;
    //                        ::SelectObject(hDC, pFontInfo->hFont);
    //                    }
    //                    cyLine = MAX(cyLine, pTm->tmHeight + pTm->tmExternalLeading + (int)aPIndentArray.GetAt(aPIndentArray.GetSize() - 1));
    //                }
    //                break;
    //            case _T('i'):  // Italic or Image
    //                {
    //                    pstrNextStart = pstrText - 1;
    //                    pstrText++;
    //                    CDuiString sImageString = pstrText;
    //                    int iWidth = 0;
    //                    int iHeight = 0;
    //                    while(*pstrText > _T('\0') && *pstrText <= _T(' '))
    //                    {
    //                        pstrText = ::CharNext(pstrText);
    //                    }
    //                    const TImageData *pImageInfo = NULL;
    //                    CDuiString sName;
    //                    while(*pstrText != _T('\0') && *pstrText != _T('>') && *pstrText != _T('}') && *pstrText != _T(' '))
    //                    {
    //                        LPCTSTR pstrTemp = ::CharNext(pstrText);
    //                        while(pstrText < pstrTemp)
    //                        {
    //                            sName += *pstrText++;
    //                        }
    //                    }
    //                    if(sName.empty())     // Italic
    //                    {
    //                        pstrNextStart = NULL;
    //                        TFontInfo *pFontInfo = CUIEngine::GetInstance()->GetDefaultFontInfo();
    //                        if(aFontArray.GetSize() > 0)
    //                        {
    //                            pFontInfo = (TFontInfo *)aFontArray.GetAt(aFontArray.GetSize() - 1);
    //                        }
    //                        if(pFontInfo->bItalic == false)
    //                        {
    //                            HFONT hFont = CUIEngine::GetInstance()->GetFont(pFontInfo->strFaceName.c_str(), pFontInfo->iSize, pFontInfo->bBold, pFontInfo->bUnderline, true);
    //                            if(hFont == NULL)
    //                            {
    //                                hFont = CUIEngine::GetInstance()->AddFont(pFontInfo->strFaceName.c_str(), pFontInfo->iSize, pFontInfo->bBold, pFontInfo->bUnderline, true);
    //                            }
    //                            pFontInfo = CUIEngine::GetInstance()->GetFontInfo(hFont);
    //                            aFontArray.Add(pFontInfo);
    //                            pTm = &pFontInfo->tm;
    //                            ::SelectObject(hDC, pFontInfo->hFont);
    //                            cyLine = MAX(cyLine, pTm->tmHeight + pTm->tmExternalLeading + (int)aPIndentArray.GetAt(aPIndentArray.GetSize() - 1));
    //                        }
    //                    }
    //                    else
    //                    {
    //                        while(*pstrText > _T('\0') && *pstrText <= _T(' '))
    //                        {
    //                            pstrText = ::CharNext(pstrText);
    //                        }
    //                        int iImageListNum = (int) _tcstol(pstrText, const_cast<LPTSTR *>(&pstrText), 10);
    //                        if(iImageListNum <= 0)
    //                        {
    //                            iImageListNum = 1;
    //                        }
    //                        while(*pstrText > _T('\0') && *pstrText <= _T(' '))
    //                        {
    //                            pstrText = ::CharNext(pstrText);
    //                        }
    //                        int iImageListIndex = (int) _tcstol(pstrText, const_cast<LPTSTR *>(&pstrText), 10);
    //                        if(iImageListIndex < 0 || iImageListIndex >= iImageListNum)
    //                        {
    //                            iImageListIndex = 0;
    //                        }
    //                        if(_tcsstr(sImageString.c_str(), _T("file=\'")) != NULL || _tcsstr(sImageString.c_str(), _T("res=\'")) != NULL)
    //                        {
    //                            CDuiString sImageResType;
    //                            CDuiString sImageName;
    //                            LPCTSTR pStrImage = sImageString.c_str();
    //                            CDuiString sItem;
    //                            CDuiString sValue;
    //                            while(*pStrImage != _T('\0'))
    //                            {
    //                                sItem.clear();
    //                                sValue.clear();
    //                                while(*pStrImage > _T('\0') && *pStrImage <= _T(' '))
    //                                {
    //                                    pStrImage = ::CharNext(pStrImage);
    //                                }
    //                                while(*pStrImage != _T('\0') && *pStrImage != _T('=') && *pStrImage > _T(' '))
    //                                {
    //                                    LPTSTR pstrTemp = ::CharNext(pStrImage);
    //                                    while(pStrImage < pstrTemp)
    //                                    {
    //                                        sItem += *pStrImage++;
    //                                    }
    //                                }
    //                                while(*pStrImage > _T('\0') && *pStrImage <= _T(' '))
    //                                {
    //                                    pStrImage = ::CharNext(pStrImage);
    //                                }
    //                                if(*pStrImage++ != _T('='))
    //                                {
    //                                    break;
    //                                }
    //                                while(*pStrImage > _T('\0') && *pStrImage <= _T(' '))
    //                                {
    //                                    pStrImage = ::CharNext(pStrImage);
    //                                }
    //                                if(*pStrImage++ != _T('\''))
    //                                {
    //                                    break;
    //                                }
    //                                while(*pStrImage != _T('\0') && *pStrImage != _T('\''))
    //                                {
    //                                    LPTSTR pstrTemp = ::CharNext(pStrImage);
    //                                    while(pStrImage < pstrTemp)
    //                                    {
    //                                        sValue += *pStrImage++;
    //                                    }
    //                                }
    //                                if(*pStrImage++ != _T('\''))
    //                                {
    //                                    break;
    //                                }
    //                                if(!sValue.empty())
    //                                {
    //                                    if(sItem.compare(_T("file")) == 0 || sItem.compare(_T("res")) ==  0)
    //                                    {
    //                                        sImageName = sValue;
    //                                    }
    //                                    else if(sItem.compare(_T("restype")) == 0)
    //                                    {
    //                                        sImageResType = sValue;
    //                                    }
    //                                }
    //                                if(*pStrImage++ != _T(' '))
    //                                {
    //                                    break;
    //                                }
    //                            }
    //                            pImageInfo = CUIEngine::GetInstance()->GetImageEx((LPCTSTR)sImageName.c_str(), sImageResType.c_str());
    //                        }
    //                        else
    //                        {
    //                            pImageInfo = CUIEngine::GetInstance()->GetImageEx((LPCTSTR)sName.c_str());
    //                        }
    //                        if(pImageInfo)
    //                        {
    //                            iWidth = pImageInfo->nX;
    //                            iHeight = pImageInfo->nY;
    //                            if(iImageListNum > 1)
    //                            {
    //                                iWidth /= iImageListNum;
    //                            }
    //                            if(pt.x + iWidth > rc.right && pt.x > rc.left && (uStyle & DT_SINGLELINE) == 0)
    //                            {
    //                                bLineEnd = true;
    //                            }
    //                            else
    //                            {
    //                                pstrNextStart = NULL;
    //                                if(bDraw && bLineDraw)
    //                                {
    //                                    CDuiRect rcImage(pt.x, pt.y + cyLineHeight - iHeight, pt.x + iWidth, pt.y + cyLineHeight);
    //                                    if(iHeight < cyLineHeight)
    //                                    {
    //                                        rcImage.bottom -= (cyLineHeight - iHeight) / 2;
    //                                        rcImage.top = rcImage.bottom -  iHeight;
    //                                    }
    //                                    CDuiRect rcBmpPart(0, 0, iWidth, iHeight);
    //                                    rcBmpPart.left = iWidth * iImageListIndex;
    //                                    rcBmpPart.right = iWidth * (iImageListIndex + 1);
    //                                    CDuiRect rcCorner(0, 0, 0, 0);
    //                                    DrawImage(hDC, pImageInfo->hBitmap, rcImage, rcImage, rcBmpPart, rcCorner, \
    //                                              pImageInfo->alphaChannel, 255);
    //                                }
    //                                cyLine = MAX(iHeight, cyLine);
    //                                pt.x += iWidth;
    //                                cyMinHeight = pt.y + iHeight;
    //                                cxMaxWidth = MAX(cxMaxWidth, pt.x);
    //                            }
    //                        }
    //                        else
    //                        {
    //                            pstrNextStart = NULL;
    //                        }
    //                    }
    //                }
    //                break;
    //            case _T('n'):  // Newline
    //                {
    //                    pstrText++;
    //                    if((uStyle & DT_SINGLELINE) != 0)
    //                    {
    //                        break;
    //                    }
    //                    bLineEnd = true;
    //                }
    //                break;
    //            case _T('p'):  // Paragraph
    //                {
    //                    pstrText++;
    //                    if(pt.x > rc.left)
    //                    {
    //                        bLineEnd = true;
    //                    }
    //                    while(*pstrText > _T('\0') && *pstrText <= _T(' '))
    //                    {
    //                        pstrText = ::CharNext(pstrText);
    //                    }
    //                    int cyLineExtra = (int)_tcstol(pstrText, const_cast<LPTSTR *>(&pstrText), 10);
    //                    aPIndentArray.Add((LPVOID)cyLineExtra);
    //                    cyLine = MAX(cyLine, pTm->tmHeight + pTm->tmExternalLeading + cyLineExtra);
    //                }
    //                break;
    //            case _T('r'):  // Raw Text
    //                {
    //                    pstrText++;
    //                    bInRaw = true;
    //                }
    //                break;
    //            case _T('s'):  // Selected text background color
    //                {
    //                    pstrText++;
    //                    bInSelected = !bInSelected;
    //                    if(bDraw && bLineDraw)
    //                    {
    //                        if(bInSelected)
    //                        {
    //                            ::SetBkMode(hDC, OPAQUE);
    //                        }
    //                        else
    //                        {
    //                            ::SetBkMode(hDC, TRANSPARENT);
    //                        }
    //                    }
    //                }
    //                break;
    //            case _T('u'):  // Underline text
    //                {
    //                    pstrText++;
    //                    TFontInfo *pFontInfo = CUIEngine::GetInstance()->GetDefaultFontInfo();
    //                    if(aFontArray.GetSize() > 0)
    //                    {
    //                        pFontInfo = (TFontInfo *)aFontArray.GetAt(aFontArray.GetSize() - 1);
    //                    }
    //                    if(pFontInfo->bUnderline == false)
    //                    {
    //                        HFONT hFont = CUIEngine::GetInstance()->GetFont(pFontInfo->strFaceName.c_str(), pFontInfo->iSize, pFontInfo->bBold, true, pFontInfo->bItalic);
    //                        if(hFont == NULL)
    //                        {
    //                            hFont = CUIEngine::GetInstance()->AddFont(pFontInfo->strFaceName.c_str(), pFontInfo->iSize, pFontInfo->bBold, true, pFontInfo->bItalic);
    //                        }
    //                        pFontInfo = CUIEngine::GetInstance()->GetFontInfo(hFont);
    //                        aFontArray.Add(pFontInfo);
    //                        pTm = &pFontInfo->tm;
    //                        ::SelectObject(hDC, pFontInfo->hFont);
    //                        cyLine = MAX(cyLine, pTm->tmHeight + pTm->tmExternalLeading + (int)aPIndentArray.GetAt(aPIndentArray.GetSize() - 1));
    //                    }
    //                }
    //                break;
    //            case _T('x'):  // X Indent
    //                {
    //                    pstrText++;
    //                    while(*pstrText > _T('\0') && *pstrText <= _T(' '))
    //                    {
    //                        pstrText = ::CharNext(pstrText);
    //                    }
    //                    int iWidth = (int) _tcstol(pstrText, const_cast<LPTSTR *>(&pstrText), 10);
    //                    pt.x += iWidth;
    //                    cxMaxWidth = MAX(cxMaxWidth, pt.x);
    //                }
    //                break;
    //            case _T('y'):  // Y Indent
    //                {
    //                    pstrText++;
    //                    while(*pstrText > _T('\0') && *pstrText <= _T(' '))
    //                    {
    //                        pstrText = ::CharNext(pstrText);
    //                    }
    //                    cyLine = (int) _tcstol(pstrText, const_cast<LPTSTR *>(&pstrText), 10);
    //                }
    //                break;
    //        }
    /* if(pstrNextStart != NULL)
     {
         pstrText = pstrNextStart;
     }
     else
     {
         while(*pstrText != _T('\0') && *pstrText != _T('>') && *pstrText != _T('}'))
         {
             pstrText = ::CharNext(pstrText);
         }

         pstrText = ::CharNext(pstrText);
     }*/
    //    }
    //    else if(!bInRaw && (*pstrText == _T('<') || *pstrText == _T('{')) && pstrText[1] == _T('/'))
    //    {
    //        pstrText++;
    //        pstrText++;
    //        switch(*pstrText)
    //        {
    //            case _T('c'):
    //                {
    //                    pstrText++;
    //                    aColorArray.Remove(aColorArray.GetSize() - 1);
    //                    DWORD clrColor = dwTextColor;
    //                    if(aColorArray.GetSize() > 0)
    //                    {
    //                        clrColor = (int)aColorArray.GetAt(aColorArray.GetSize() - 1);
    //                    }
    //                    ::SetTextColor(hDC, RGB(GetBValue(clrColor), GetGValue(clrColor), GetRValue(clrColor)));
    //                }
    //                break;
    //            case _T('p'):
    //                pstrText++;
    //                if(pt.x > rc.left)
    //                {
    //                    bLineEnd = true;
    //                }
    //                aPIndentArray.Remove(aPIndentArray.GetSize() - 1);
    //                cyLine = MAX(cyLine, pTm->tmHeight + pTm->tmExternalLeading + (int)aPIndentArray.GetAt(aPIndentArray.GetSize() - 1));
    //                break;
    //            case _T('s'):
    //                {
    //                    pstrText++;
    //                    bInSelected = !bInSelected;
    //                    if(bDraw && bLineDraw)
    //                    {
    //                        if(bInSelected)
    //                        {
    //                            ::SetBkMode(hDC, OPAQUE);
    //                        }
    //                        else
    //                        {
    //                            ::SetBkMode(hDC, TRANSPARENT);
    //                        }
    //                    }
    //                }
    //                break;
    //            case _T('a'):
    //                {
    //                    if(iLinkIndex < nLinkRects)
    //                    {
    //                        if(!bLineDraw)
    //                        {
    //                            ::SetRect(&prcLinks[iLinkIndex], ptLinkStart.x, ptLinkStart.y, MIN(pt.x, rc.right), pt.y + pTm->tmHeight + pTm->tmExternalLeading);
    //                        }
    //                        iLinkIndex++;
    //                    }
    //                    aColorArray.Remove(aColorArray.GetSize() - 1);
    //                    DWORD clrColor = dwTextColor;
    //                    if(aColorArray.GetSize() > 0)
    //                    {
    //                        clrColor = (int)aColorArray.GetAt(aColorArray.GetSize() - 1);
    //                    }
    //                    ::SetTextColor(hDC, RGB(GetBValue(clrColor), GetGValue(clrColor), GetRValue(clrColor)));
    //                    bInLink = false;
    //                }
    //            case _T('b'):
    //            case _T('f'):
    //            case _T('i'):
    //            case _T('u'):
    //                {
    //                    pstrText++;
    //                    aFontArray.Remove(aFontArray.GetSize() - 1);
    //                    TFontInfo *pFontInfo = (TFontInfo *)aFontArray.GetAt(aFontArray.GetSize() - 1);
    //                    if(pFontInfo == NULL)
    //                    {
    //                        pFontInfo = CUIEngine::GetInstance()->GetDefaultFontInfo();
    //                    }
    //                    if(pTm->tmItalic && pFontInfo->bItalic == false)
    //                    {
    //                        ABC abc;
    //                        ::GetCharABCWidths(hDC, _T(' '), _T(' '), &abc);
    //                        pt.x += abc.abcC / 2; // ������һ��б����ŵ�����, ��ȷ����Ӧ����http://support.microsoft.com/kb/244798/en-us
    //                    }
    //                    pTm = &pFontInfo->tm;
    //                    ::SelectObject(hDC, pFontInfo->hFont);
    //                    cyLine = MAX(cyLine, pTm->tmHeight + pTm->tmExternalLeading + (int)aPIndentArray.GetAt(aPIndentArray.GetSize() - 1));
    //                }
    //                break;
    //        }
    //        while(*pstrText != _T('\0') && *pstrText != _T('>') && *pstrText != _T('}'))
    //        {
    //            pstrText = ::CharNext(pstrText);
    //        }
    //        pstrText = ::CharNext(pstrText);
    //    }
    //    else if(!bInRaw &&  *pstrText == _T('<') && pstrText[2] == _T('>') && (pstrText[1] == _T('{')  || pstrText[1] == _T('}')))
    //    {
    //        SIZE szSpace = { 0 };
    //        ::GetTextExtentPoint32(hDC, &pstrText[1], 1, &szSpace);
    //        if(bDraw && bLineDraw)
    //        {
    //            ::TextOut(hDC, pt.x, pt.y + cyLineHeight - pTm->tmHeight - pTm->tmExternalLeading, &pstrText[1], 1);
    //        }
    //        pt.x += szSpace.cx;
    //        cxMaxWidth = MAX(cxMaxWidth, pt.x);
    //        pstrText++;
    //        pstrText++;
    //        pstrText++;
    //    }
    //    else if(!bInRaw &&  *pstrText == _T('{') && pstrText[2] == _T('}') && (pstrText[1] == _T('<')  || pstrText[1] == _T('>')))
    //    {
    //        SIZE szSpace = { 0 };
    //        ::GetTextExtentPoint32(hDC, &pstrText[1], 1, &szSpace);
    //        if(bDraw && bLineDraw)
    //        {
    //            ::TextOut(hDC, pt.x,  pt.y + cyLineHeight - pTm->tmHeight - pTm->tmExternalLeading, &pstrText[1], 1);
    //        }
    //        pt.x += szSpace.cx;
    //        cxMaxWidth = MAX(cxMaxWidth, pt.x);
    //        pstrText++;
    //        pstrText++;
    //        pstrText++;
    //    }
    //    else if(!bInRaw &&  *pstrText == _T(' '))
    //    {
    //        SIZE szSpace = { 0 };
    //        ::GetTextExtentPoint32(hDC, _T(" "), 1, &szSpace);
    //        // Still need to paint the space because the font might have
    //        // underline formatting.
    //        if(bDraw && bLineDraw)
    //        {
    //            ::TextOut(hDC, pt.x,  pt.y + cyLineHeight - pTm->tmHeight - pTm->tmExternalLeading, _T(" "), 1);
    //        }
    //        pt.x += szSpace.cx;
    //        cxMaxWidth = MAX(cxMaxWidth, pt.x);
    //        pstrText++;
    //    }
    //    else
    //    {
    //        POINT ptPos = pt;
    //        int cchChars = 0;
    //        int cchSize = 0;
    //        int cchLastGoodWord = 0;
    //        int cchLastGoodSize = 0;
    //        LPCTSTR p = pstrText;
    //        LPCTSTR pstrNext;
    //        SIZE szText = { 0 };
    //        if(!bInRaw && *p == _T('<') || *p == _T('{'))
    //        {
    //            p++, cchChars++, cchSize++;
    //        }
    //        while(*p != _T('\0') && *p != _T('\n'))
    //        {
    //            // This part makes sure that we're word-wrapping if needed or providing support
    //            // for DT_END_ELLIPSIS. Unfortunately the GetTextExtentPoint32() call is pretty
    //            // slow when repeated so often.
    //            // TODO: Rewrite and use GetTextExtentExPoint() instead!
    //            if(bInRaw)
    //            {
    //                if((*p == _T('<') || *p == _T('{')) && p[1] == _T('/')
    //                        && p[2] == _T('r') && (p[3] == _T('>') || p[3] == _T('}')))
    //                {
    //                    p += 4;
    //                    bInRaw = false;
    //                    break;
    //                }
    //            }
    //            else
    //            {
    //                if(*p == _T('<') || *p == _T('{'))
    //                {
    //                    break;
    //                }
    //            }
    //            pstrNext = ::CharNext(p);
    //            cchChars++;
    //            cchSize += (int)(pstrNext - p);
    //            szText.cx = cchChars * pTm->tmMaxCharWidth;
    //            if(pt.x + szText.cx >= rc.right)
    //            {
    //                ::GetTextExtentPoint32(hDC, pstrText, cchSize, &szText);
    //            }
    //            if(pt.x + szText.cx > rc.right)
    //            {
    //                if(pt.x + szText.cx > rc.right && pt.x != rc.left)
    //                {
    //                    cchChars--;
    //                    cchSize -= (int)(pstrNext - p);
    //                }
    //                if((uStyle & DT_WORDBREAK) != 0 && cchLastGoodWord > 0)
    //                {
    //                    cchChars = cchLastGoodWord;
    //                    cchSize = cchLastGoodSize;
    //                }
    //                if((uStyle & DT_END_ELLIPSIS) != 0 && cchChars > 0)
    //                {
    //                    cchChars -= 1;
    //                    LPCTSTR pstrPrev = ::CharPrev(pstrText, p);
    //                    if(cchChars > 0)
    //                    {
    //                        cchChars -= 1;
    //                        pstrPrev = ::CharPrev(pstrText, pstrPrev);
    //                        cchSize -= (int)(p - pstrPrev);
    //                    }
    //                    else
    //                    {
    //                        cchSize -= (int)(p - pstrPrev);
    //                    }
    //                    pt.x = rc.right;
    //                }
    //                bLineEnd = true;
    //                cxMaxWidth = MAX(cxMaxWidth, pt.x);
    //                break;
    //            }
    //            if(!((p[0] >= _T('a') && p[0] <= _T('z')) || (p[0] >= _T('A') && p[0] <= _T('Z'))))
    //            {
    //                cchLastGoodWord = cchChars;
    //                cchLastGoodSize = cchSize;
    //            }
    //            if(*p == _T(' '))
    //            {
    //                cchLastGoodWord = cchChars;
    //                cchLastGoodSize = cchSize;
    //            }
    //            p = ::CharNext(p);
    //        }
    //        ::GetTextExtentPoint32(hDC, pstrText, cchSize, &szText);
    //        if(bDraw && bLineDraw)
    //        {
    //            if((uStyle & DT_SINGLELINE) == 0 && (uStyle & DT_CENTER) != 0)
    //            {
    //                ptPos.x += (rc.right - rc.left - szText.cx) / 2;
    //            }
    //            else if((uStyle & DT_SINGLELINE) == 0 && (uStyle & DT_RIGHT) != 0)
    //            {
    //                ptPos.x += (rc.right - rc.left - szText.cx);
    //            }
    //            ::TextOut(hDC, ptPos.x, ptPos.y + cyLineHeight - pTm->tmHeight - pTm->tmExternalLeading, pstrText, cchSize);
    //            if(pt.x >= rc.right && (uStyle & DT_END_ELLIPSIS) != 0)
    //            {
    //                ::TextOut(hDC, ptPos.x + szText.cx, ptPos.y, _T("..."), 3);
    //            }
    //        }
    //        pt.x += szText.cx;
    //        cxMaxWidth = MAX(cxMaxWidth, pt.x);
    //        pstrText += cchSize;
    //    }
    //    if(pt.x >= rc.right || *pstrText == _T('\n') || *pstrText == _T('\0'))
    //    {
    //        bLineEnd = true;
    //    }
    //    if(bDraw && bLineEnd)
    //    {
    //        if(!bLineDraw)
    //        {
    //            aFontArray.Resize(aLineFontArray.GetSize());
    //            ::CopyMemory(aFontArray.GetData(), aLineFontArray.GetData(), aLineFontArray.GetSize() * sizeof(LPVOID));
    //            aColorArray.Resize(aLineColorArray.GetSize());
    //            ::CopyMemory(aColorArray.GetData(), aLineColorArray.GetData(), aLineColorArray.GetSize() * sizeof(LPVOID));
    //            aPIndentArray.Resize(aLinePIndentArray.GetSize());
    //            ::CopyMemory(aPIndentArray.GetData(), aLinePIndentArray.GetData(), aLinePIndentArray.GetSize() * sizeof(LPVOID));
    //            cyLineHeight = cyLine;
    //            pstrText = pstrLineBegin;
    //            bInRaw = bLineInRaw;
    //            bInSelected = bLineInSelected;
    //            DWORD clrColor = dwTextColor;
    //            if(aColorArray.GetSize() > 0)
    //            {
    //                clrColor = (int)aColorArray.GetAt(aColorArray.GetSize() - 1);
    //            }
    //            ::SetTextColor(hDC, RGB(GetBValue(clrColor), GetGValue(clrColor), GetRValue(clrColor)));
    //            TFontInfo *pFontInfo = (TFontInfo *)aFontArray.GetAt(aFontArray.GetSize() - 1);
    //            if(pFontInfo == NULL)
    //            {
    //                pFontInfo = CUIEngine::GetInstance()->GetDefaultFontInfo();
    //            }
    //            pTm = &pFontInfo->tm;
    //            ::SelectObject(hDC, pFontInfo->hFont);
    //            if(bInSelected)
    //            {
    //                ::SetBkMode(hDC, OPAQUE);
    //            }
    //        }
    //        else
    //        {
    //            aLineFontArray.Resize(aFontArray.GetSize());
    //            ::CopyMemory(aLineFontArray.GetData(), aFontArray.GetData(), aFontArray.GetSize() * sizeof(LPVOID));
    //            aLineColorArray.Resize(aColorArray.GetSize());
    //            ::CopyMemory(aLineColorArray.GetData(), aColorArray.GetData(), aColorArray.GetSize() * sizeof(LPVOID));
    //            aLinePIndentArray.Resize(aPIndentArray.GetSize());
    //            ::CopyMemory(aLinePIndentArray.GetData(), aPIndentArray.GetData(), aPIndentArray.GetSize() * sizeof(LPVOID));
    //            pstrLineBegin = pstrText;
    //            bLineInSelected = bInSelected;
    //            bLineInRaw = bInRaw;
    //        }
    //    }
    //    ASSERT(iLinkIndex <= nLinkRects);
    //}
    nLinkRects = iLinkIndex;

    // Return size of text when requested
    if((uStyle & DT_CALCRECT) != 0)
    {
        rc.bottom = MAX(cyMinHeight, pt.y + cyLine);
        rc.right = MIN(rc.right, cxMaxWidth);
    }

    if(bDraw)
    {
        ::SelectClipRgn(hDC, hOldRgn);
    }

    ::DeleteObject(hOldRgn);
    ::DeleteObject(hRgn);
    ::SelectObject(hDC, hOldFont);
}

HBITMAP CRenderEngine::GenerateBitmap(CWindowUI *pManager, CControlUI *pControl, RECT rc)
{
    int cx = rc.right - rc.left;
    int cy = rc.bottom - rc.top;
    HDC hPaintDC = ::CreateCompatibleDC(pManager->GetPaintDC());
    HBITMAP hPaintBitmap = ::CreateCompatibleBitmap(pManager->GetPaintDC(), rc.right, rc.bottom);
    ASSERT(hPaintDC);
    ASSERT(hPaintBitmap);
    HBITMAP hOldPaintBitmap = (HBITMAP) ::SelectObject(hPaintDC, hPaintBitmap);
    pControl->DoPaint(hPaintDC, rc);
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = cx;
    bmi.bmiHeader.biHeight = cy;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage = cx * cy * sizeof(DWORD);
    LPDWORD pDest = NULL;
    HDC hCloneDC = ::CreateCompatibleDC(pManager->GetPaintDC());
    HBITMAP hBitmap = ::CreateDIBSection(pManager->GetPaintDC(), &bmi, DIB_RGB_COLORS, (LPVOID *) &pDest, NULL, 0);
    ASSERT(hCloneDC);
    ASSERT(hBitmap);

    if(hBitmap != NULL)
    {
        HBITMAP hOldBitmap = (HBITMAP) ::SelectObject(hCloneDC, hBitmap);
        ::BitBlt(hCloneDC, 0, 0, cx, cy, hPaintDC, rc.left, rc.top, SRCCOPY);
        ::SelectObject(hCloneDC, hOldBitmap);
        ::DeleteDC(hCloneDC);
        ::GdiFlush();
    }

    // Cleanup
    ::SelectObject(hPaintDC, hOldPaintBitmap);
    ::DeleteObject(hPaintBitmap);
    ::DeleteDC(hPaintDC);
    return hBitmap;
}

SIZE CRenderEngine::GetTextSize(HDC hDC, CWindowUI *pManager , LPCTSTR pstrText, LPCTSTR pstrFontID, UINT uStyle)
{
    SIZE size = {0, 0};
    ASSERT(::GetObjectType(hDC) == OBJ_DC || ::GetObjectType(hDC) == OBJ_MEMDC);

    if(pstrText == NULL || pManager == NULL)
    {
        return size;
    }

    ::SetBkMode(hDC, TRANSPARENT);
    HFONT hOldFont = (HFONT)::SelectObject(hDC, GetResEngine()->GetFont(pstrFontID)->GetFont());
    GetTextExtentPoint32(hDC, pstrText, _tcslen(pstrText) , &size);
    ::SelectObject(hDC, hOldFont);
    return size;
}

} // namespace DuiLib