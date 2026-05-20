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
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include "stdafx.h"
#include "ComInitializationSeams.h"
#include "WindowsToastNotifier.h"
#include "Log.h"
#include "UserMsgs.h"
#include <map>
#include <propkey.h>
#include <propvarutil.h>
#include <roapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/base.h>

namespace
{
	constexpr PCWSTR kToastAppId = L"eMule.BB";
	constexpr PCWSTR kToastShortcutName = L"eMule BB.lnk";
	constexpr UINT kMaxTrackedToasts = 64;

	void LogToastFailure(LPCTSTR pszContext, const winrt::hresult_error& error)
	{
		const CString strMessage(error.message().c_str());
		DebugLogError(_T("Windows toast: %s failed (0x%08X: %s)"), pszContext, static_cast<DWORD>(static_cast<HRESULT>(error.code())), (LPCTSTR)strMessage);
	}

	void LogToastFailure(LPCTSTR pszContext)
	{
		DebugLogError(_T("Windows toast: %s failed with an unexpected exception."), pszContext);
	}

	struct ToastPayload
	{
		TbnMsg nMsgType = TBN_NONOTIFY;
		CString strLink;
	};

	struct ToastState
	{
		HWND hWndNotify = NULL;
		CCriticalSection lock;
		std::map<UINT, ToastPayload> payloads;
	};

	bool IsCurrentProcessElevated()
	{
		HANDLE hToken = NULL;
		if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &hToken))
			return false;

		TOKEN_ELEVATION elevation = {};
		DWORD dwLength = 0;
		const BOOL bResult = ::GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwLength);
		::CloseHandle(hToken);
		return bResult && elevation.TokenIsElevated != 0;
	}

	CStringW EscapeToastXml(CStringW strValue)
	{
		strValue.Replace(L"&", L"&amp;");
		strValue.Replace(L"<", L"&lt;");
		strValue.Replace(L">", L"&gt;");
		strValue.Replace(L"\"", L"&quot;");
		strValue.Replace(L"'", L"&apos;");
		return strValue;
	}

	CStringW GetToastFallbackTitle(TbnMsg nMsgType)
	{
		switch (nMsgType) {
		case TBN_CHAT:
			return L"Chat";
		case TBN_DOWNLOADFINISHED:
			return L"Download finished";
		case TBN_DOWNLOADADDED:
			return L"Download added";
		case TBN_LOG:
			return L"Log";
		case TBN_IMPORTANTEVENT:
			return L"Important event";
		case TBN_NEWVERSION:
			return L"New version";
		default:
			return L"eMule BB";
		}
	}

	void SplitToastText(LPCTSTR pszText, TbnMsg nMsgType, CStringW &strTitle, CStringW &strBody)
	{
		CStringW strText(pszText != NULL ? pszText : L"");
		strText.Replace(L"\r\n", L"\n");
		strText.Replace(L"\r", L"\n");
		strText.Trim();

		const int iBreak = strText.Find(L'\n');
		if (iBreak >= 0) {
			strTitle = strText.Left(iBreak);
			strBody = strText.Mid(iBreak + 1);
		} else {
			strTitle = GetToastFallbackTitle(nMsgType);
			strBody = strText;
		}

		strTitle.Trim();
		strBody.Trim();
		if (strTitle.IsEmpty())
			strTitle = GetToastFallbackTitle(nMsgType);
		if (strBody.IsEmpty())
			strBody = strTitle;
	}

	CStringW BuildToastXml(UINT uToastId, LPCTSTR pszText, TbnMsg nMsgType)
	{
		CStringW strTitle;
		CStringW strBody;
		SplitToastText(pszText, nMsgType, strTitle, strBody);

		CStringW strXml;
		strXml.Format(
			L"<toast launch=\"emule-toast:%u\">"
			L"<visual><binding template=\"ToastGeneric\">"
			L"<text>%s</text><text>%s</text>"
			L"</binding></visual>"
			L"</toast>",
			uToastId,
			static_cast<LPCWSTR>(EscapeToastXml(strTitle)),
			static_cast<LPCWSTR>(EscapeToastXml(strBody)));
		return strXml;
	}

	bool EnsureToastShortcut()
	{
		ComInitializationSeams::CScopedCoTaskMem<WCHAR> startMenuPath;
		if (FAILED(::SHGetKnownFolderPath(FOLDERID_StartMenu, KF_FLAG_CREATE, NULL, startMenuPath.GetAddressOf())))
			return false;

		CStringW strShortcutPath(startMenuPath.Get());
		strShortcutPath += L"\\Programs\\";
		strShortcutPath += kToastShortcutName;

		WCHAR szModulePath[MAX_PATH] = {};
		if (::GetModuleFileNameW(NULL, szModulePath, _countof(szModulePath)) == 0)
			return false;

		const ComInitializationSeams::CScopedComInitialize coInitialize(COINIT_APARTMENTTHREADED);
		if (!coInitialize.IsUsable())
			return false;

		CComPtr<IShellLinkW> pShellLink;
		if (FAILED(pShellLink.CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER)) || pShellLink == NULL)
			return false;
		pShellLink->SetPath(szModulePath);
		pShellLink->SetIconLocation(szModulePath, 0);
		pShellLink->SetDescription(L"eMule BB");

		CComPtr<IPropertyStore> pPropertyStore;
		if (FAILED(pShellLink.QueryInterface(&pPropertyStore)) || pPropertyStore == NULL)
			return false;

		ComInitializationSeams::CScopedPropVariant appId;
		if (FAILED(::InitPropVariantFromString(kToastAppId, appId.GetAddressOf()))
			|| FAILED(pPropertyStore->SetValue(PKEY_AppUserModel_ID, appId.Get()))
			|| FAILED(pPropertyStore->Commit()))
		{
			return false;
		}

		CComPtr<IPersistFile> pPersistFile;
		if (FAILED(pShellLink.QueryInterface(&pPersistFile)) || pPersistFile == NULL)
			return false;

		return SUCCEEDED(pPersistFile->Save(strShortcutPath, TRUE));
	}

	void PruneToastState(const std::shared_ptr<ToastState> &state)
	{
		CSingleLock lock(&state->lock, TRUE);
		while (state->payloads.size() > kMaxTrackedToasts)
			state->payloads.erase(state->payloads.begin());
	}

	void PostToastClicked(const std::shared_ptr<ToastState> &state, UINT uToastId)
	{
		ToastPayload payload;
		HWND hWndNotify = NULL;
		{
			CSingleLock lock(&state->lock, TRUE);
			const auto itPayload = state->payloads.find(uToastId);
			if (itPayload == state->payloads.end())
				return;

			payload = itPayload->second;
			state->payloads.erase(itPayload);
			hWndNotify = state->hWndNotify;
		}

		if (!::IsWindow(hWndNotify))
			return;

		LPTSTR pszLink = NULL;
		if (!payload.strLink.IsEmpty())
			pszLink = _tcsdup(payload.strLink);

		if (!::PostMessage(hWndNotify, UM_WINDOWS_TOAST_CLICKED, static_cast<WPARAM>(payload.nMsgType), reinterpret_cast<LPARAM>(pszLink)) && pszLink != NULL)
			free(pszLink);
	}
}

struct CWindowsToastNotifier::Impl
{
	std::shared_ptr<ToastState> state = std::make_shared<ToastState>();
	bool bInitializationAttempted = false;
	bool bInitialized = false;
	bool bRoInitialized = false;
	UINT uNextToastId = 1;
	winrt::Windows::UI::Notifications::ToastNotifier notifier{ nullptr };
	std::map<UINT, winrt::Windows::UI::Notifications::ToastNotification> activeToasts;

	bool Initialize(HWND hWndNotify)
	{
		{
			CSingleLock lock(&state->lock, TRUE);
			state->hWndNotify = hWndNotify;
		}

		if (bInitialized)
			return true;
		if (bInitializationAttempted || hWndNotify == NULL || IsCurrentProcessElevated())
			return false;

		bInitializationAttempted = true;
		if (FAILED(::SetCurrentProcessExplicitAppUserModelID(kToastAppId)) || !EnsureToastShortcut())
			return false;

		const HRESULT hrRo = ::RoInitialize(RO_INIT_SINGLETHREADED);
		if (hrRo == S_OK || hrRo == S_FALSE)
			bRoInitialized = true;
		else if (hrRo != RPC_E_CHANGED_MODE)
			return false;

		try {
			notifier = winrt::Windows::UI::Notifications::ToastNotificationManager::CreateToastNotifier(kToastAppId);
			bInitialized = true;
		} catch (const winrt::hresult_error& error) {
			LogToastFailure(_T("CreateToastNotifier"), error);
			if (bRoInitialized) {
				::RoUninitialize();
				bRoInitialized = false;
			}
		} catch (...) {
			LogToastFailure(_T("CreateToastNotifier"));
			if (bRoInitialized) {
				::RoUninitialize();
				bRoInitialized = false;
			}
		}
		return bInitialized;
	}
};

CWindowsToastNotifier::CWindowsToastNotifier()
	: m_impl(std::make_unique<Impl>())
{
}

CWindowsToastNotifier::~CWindowsToastNotifier()
{
	Shutdown();
}

bool CWindowsToastNotifier::Show(HWND hWndNotify, LPCTSTR pszText, TbnMsg nMsgType, LPCTSTR pszLink)
{
	if (!m_impl->Initialize(hWndNotify))
		return false;

	const UINT uToastId = m_impl->uNextToastId++;
	const CStringW strXml = BuildToastXml(uToastId, pszText, nMsgType);

	try {
		winrt::Windows::Data::Xml::Dom::XmlDocument xmlDocument;
		xmlDocument.LoadXml(static_cast<LPCWSTR>(strXml));

		winrt::Windows::UI::Notifications::ToastNotification toast(xmlDocument);
		const std::shared_ptr<ToastState> state = m_impl->state;
		toast.Activated([state, uToastId](const auto&, const auto&) {
			PostToastClicked(state, uToastId);
		});

		{
			CSingleLock lock(&m_impl->state->lock, TRUE);
			ToastPayload payload;
			payload.nMsgType = nMsgType;
			payload.strLink = pszLink != NULL ? pszLink : _T("");
			m_impl->state->payloads[uToastId] = payload;
		}
		PruneToastState(m_impl->state);

		m_impl->notifier.Show(toast);
		m_impl->activeToasts.insert_or_assign(uToastId, toast);
		while (m_impl->activeToasts.size() > kMaxTrackedToasts)
			m_impl->activeToasts.erase(m_impl->activeToasts.begin());
		return true;
	} catch (const winrt::hresult_error& error) {
		LogToastFailure(_T("Show"), error);
		CSingleLock lock(&m_impl->state->lock, TRUE);
		m_impl->state->payloads.erase(uToastId);
	} catch (...) {
		LogToastFailure(_T("Show"));
		CSingleLock lock(&m_impl->state->lock, TRUE);
		m_impl->state->payloads.erase(uToastId);
	}
	return false;
}

void CWindowsToastNotifier::Shutdown()
{
	if (m_impl == nullptr)
		return;

	{
		CSingleLock lock(&m_impl->state->lock, TRUE);
		m_impl->state->hWndNotify = NULL;
		m_impl->state->payloads.clear();
	}
	m_impl->activeToasts.clear();
	m_impl->notifier = nullptr;
	m_impl->bInitialized = false;

	if (m_impl->bRoInitialized) {
		::RoUninitialize();
		m_impl->bRoInitialized = false;
	}
}
