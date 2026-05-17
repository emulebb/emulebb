//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include "StdAfx.h"
#include <atlimage.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include "CaptchaGenerator.h"
#include "OtherFunctions.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define LETTERSIZE  32
#define CROWDEDSIZE 23

// fairly simple captcha generator, might be improved if spammers think it's really worth it to solve captchas in eMule

static TCHAR const sCaptchaCharSet[] = _T("ABCDEFGHIJKLMNPQRSTUVWXYZ123456789");

namespace
{
	/**
	 * Deletes a GDI object automatically unless ownership was explicitly handed
	 * off to another container.
	 */
	class CScopedDeleteGdiObject
	{
	public:
		explicit CScopedDeleteGdiObject(HGDIOBJ hObject = NULL)
			: m_hObject(hObject)
		{
		}

		~CScopedDeleteGdiObject()
		{
			if (m_hObject != NULL)
				::DeleteObject(m_hObject);
		}

		HGDIOBJ Get() const
		{
			return m_hObject;
		}

		HGDIOBJ Release()
		{
			HGDIOBJ hObject = m_hObject;
			m_hObject = NULL;
			return hObject;
		}

	private:
		HGDIOBJ m_hObject;
	};

	/**
	 * Deletes a scratch memory DC automatically on every return path.
	 */
	class CScopedDeleteDc
	{
	public:
		explicit CScopedDeleteDc(HDC hDC = NULL)
			: m_hDC(hDC)
		{
		}

		~CScopedDeleteDc()
		{
			if (m_hDC != NULL)
				::DeleteDC(m_hDC);
		}

		HDC Get() const
		{
			return m_hDC;
		}

	private:
		HDC m_hDC;
	};

	/**
	 * Restores the previously selected GDI object automatically so exception
	 * unwinding cannot strand temporary fonts or bitmaps in a DC.
	 */
	class CScopedSelectObject
	{
	public:
		CScopedSelectObject(HDC hDC, HGDIOBJ hObject)
			: m_hDC(hDC)
			, m_hPrevious(m_hDC != NULL && hObject != NULL ? ::SelectObject(m_hDC, hObject) : NULL)
		{
		}

		~CScopedSelectObject()
		{
			if (m_hDC != NULL && m_hPrevious != NULL && m_hPrevious != HGDI_ERROR)
				::SelectObject(m_hDC, m_hPrevious);
		}

		bool IsValid() const
		{
			return m_hPrevious != NULL && m_hPrevious != HGDI_ERROR;
		}

	private:
		HDC m_hDC;
		HGDIOBJ m_hPrevious;
	};
}

CCaptchaGenerator::CCaptchaGenerator(uint32 nLetterCount)
	: m_hbmpCaptcha()
{
	ReGenerateCaptcha(nLetterCount);
}

void CCaptchaGenerator::ReGenerateCaptcha(uint32 nLetterCount)
{
	Clear();
	//CUpDownClient::ProcessCaptchaRequest verifies that height is between 11 and 49, width between 11 and 149
	int nWidth = nLetterCount > 1 ? ((LETTERSIZE)+nLetterCount * (CROWDEDSIZE)) : (LETTERSIZE);
	int nHeight = 48;
	struct {
		BITMAPINFOHEADER bmiHeader;
		RGBQUAD bmiColors[2];
	} bmiMono = { {0}, { {255, 255, 255} } };
	bmiMono.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmiMono.bmiHeader.biWidth = nWidth;
	bmiMono.bmiHeader.biHeight = nHeight;
	bmiMono.bmiHeader.biPlanes = 1;
	bmiMono.bmiHeader.biBitCount = 1;
	bmiMono.bmiHeader.biCompression = BI_RGB;
	void *pv;
	CScopedDeleteGdiObject hbmpCaptcha(::CreateDIBSection(NULL, (BITMAPINFO*)&bmiMono, DIB_RGB_COLORS, &pv, NULL, 0));
	bmiMono.bmiHeader.biWidth = LETTERSIZE;
	CScopedDeleteGdiObject hBitMem(::CreateDIBSection(NULL, (BITMAPINFO*)&bmiMono, DIB_RGB_COLORS, &pv, NULL, 0));

	int nFontSize = 40;
	LOGFONT m_LF = { 0 };
	m_LF.lfHeight = nFontSize;
	m_LF.lfWeight = FW_HEAVY;
	_tcsncpy(m_LF.lfFaceName, _T("Arial"), LF_FACESIZE - 1);	// For UNICODE support
	CScopedDeleteGdiObject hFont(CreateFontIndirect(&m_LF));

	CScopedDeleteDc hdc(::CreateCompatibleDC(NULL));
	CScopedDeleteDc hdcMem(::CreateCompatibleDC(NULL));
	ASSERT(hdc.Get() && hdcMem.Get() && hbmpCaptcha.Get() && hBitMem.Get() && hFont.Get());
	if (!hdc.Get() || !hdcMem.Get() || !hbmpCaptcha.Get() || !hBitMem.Get() || !hFont.Get())
		return;

	CScopedSelectObject selectCaptchaBitmap(hdc.Get(), hbmpCaptcha.Get());
	CScopedSelectObject selectMemBitmap(hdcMem.Get(), hBitMem.Get());
	CScopedSelectObject selectMemFont(hdcMem.Get(), hFont.Get());
	if (!selectCaptchaBitmap.IsValid() || !selectMemBitmap.IsValid() || !selectMemFont.IsValid())
		return;

	WCHAR wT[2] = { 0 };
	int xOff = (CROWDEDSIZE) / 2;
	for (uint32 n = 0; n < nLetterCount; ++n) {
		*wT = sCaptchaCharSet[rand() % (_countof(sCaptchaCharSet) - 1)];
		m_strCaptchaText += *wT;
		RECT r{0, 0, (LETTERSIZE), (LETTERSIZE) };
		::DrawText(hdcMem.Get(), wT, 1, &r, DT_TOP | DT_LEFT | DT_CALCRECT);
		::DrawText(hdcMem.Get(), wT, 1, &r, DT_TOP | DT_LEFT);
		const float scale = static_cast<float>(nFontSize - (rand() % 10)) / static_cast<float>(nFontSize);
		const float angle = static_cast<float>(35 - (rand() % 71)) * static_cast<float>(M_PI) / 180.0f;
		float co = cosf(angle);
		float si = sinf(angle);
		LONG x2 = (r.right - r.left) / 2;
		LONG y2 = (r.bottom - r.top) / 2;
		RECT r2{ r.left - x2, r.top - y2, r.right - x2, r.bottom - y2 };
		POINT ap[3];
		x2 += xOff;
		y2 += rand() & 8;
		const float fX2 = static_cast<float>(x2);
		const float fY2 = static_cast<float>(y2);
		ap[0].x = static_cast<LONG>(fX2 + scale * (static_cast<float>(r2.left) * co - static_cast<float>(r2.top) * si));
		ap[0].y = static_cast<LONG>(fY2 + scale * (static_cast<float>(r2.left) * si + static_cast<float>(r2.top) * co));
		ap[1].x = static_cast<LONG>(fX2 + scale * (static_cast<float>(r2.right) * co - static_cast<float>(r2.top) * si));
		ap[1].y = static_cast<LONG>(fY2 + scale * (static_cast<float>(r2.right) * si + static_cast<float>(r2.top) * co));
		ap[2].x = static_cast<LONG>(fX2 + scale * (static_cast<float>(r2.left) * co - static_cast<float>(r2.bottom) * si));
		ap[2].y = static_cast<LONG>(fY2 + scale * (static_cast<float>(r2.left) * si + static_cast<float>(r2.bottom) * co));
		::PlgBlt(hdc.Get(), ap, hdcMem.Get(), 0, 0, r.right - r.left, r.bottom - r.top, NULL, 0, 0);
		xOff += CROWDEDSIZE;
	}
	for (int j = nWidth * nHeight / 4; --j > 0;) //add noise
		::SetPixel(hdc.Get(), rand() % nWidth, rand() % nHeight, RGB(0, 0, 0));

	m_hbmpCaptcha = (HBITMAP)hbmpCaptcha.Release();
#if TEST_FRAMEGRABBER // local diagnostic image dump
	CImage captcha;
	captcha.Attach(m_hbmpCaptcha);
	captcha.Save(_T("\\tmp\\CaptchaTest.bmp"), Gdiplus::ImageFormatBMP);
	captcha.Detach();
#endif
}

void CCaptchaGenerator::Clear()
{
	if (m_hbmpCaptcha) {
		::DeleteObject(m_hbmpCaptcha);
		m_hbmpCaptcha = 0;
	}
	m_strCaptchaText.Empty();
}

bool CCaptchaGenerator::WriteCaptchaImage(CFileDataIO &file) const
{
	if (m_hbmpCaptcha) {
		size_t size;
		byte *buf = bmp2mem(m_hbmpCaptcha, size, Gdiplus::ImageFormatBMP);
		if (buf) {
			file.Write(buf, (UINT)size);
			delete[] buf;
			return true;
		}
	}
	return false;
}
