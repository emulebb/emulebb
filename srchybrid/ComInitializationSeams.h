//this file is part of eMule
//Copyright (C)2002-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.

#pragma once

#include <objbase.h>

namespace ComInitializationSeams
{
	/**
	 * @brief Reports whether COM is usable on a thread after an initialization attempt.
	 */
	inline bool IsInitializationUsable(const HRESULT hr)
	{
		return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
	}

	/**
	 * @brief Reports whether a successful initialization must be balanced with CoUninitialize.
	 */
	inline bool ShouldUninitializeAfterInitialization(const HRESULT hr)
	{
		return SUCCEEDED(hr);
	}

	/**
	 * @brief Owns one thread-local COM initialization attempt and balances successful calls.
	 */
	class CScopedComInitialize
	{
	public:
		CScopedComInitialize()
			: m_hr(::CoInitialize(NULL))
			, m_bUninitialize(ShouldUninitializeAfterInitialization(m_hr))
		{
		}

		explicit CScopedComInitialize(const DWORD dwCoInit)
			: m_hr(::CoInitializeEx(NULL, dwCoInit))
			, m_bUninitialize(ShouldUninitializeAfterInitialization(m_hr))
		{
		}

		~CScopedComInitialize()
		{
			if (m_bUninitialize)
				::CoUninitialize();
		}

		CScopedComInitialize(const CScopedComInitialize&) = delete;
		CScopedComInitialize& operator=(const CScopedComInitialize&) = delete;

		/**
		 * @brief Reports whether COM APIs can be used on this thread.
		 */
		bool IsUsable() const
		{
			return IsInitializationUsable(m_hr);
		}

		/**
		 * @brief Reports whether this instance owns a successful COM initialization.
		 */
		bool IsInitialized() const
		{
			return ShouldUninitializeAfterInitialization(m_hr);
		}

		/**
		 * @brief Reports the raw initialization result for diagnostics and compatibility checks.
		 */
		HRESULT GetResult() const
		{
			return m_hr;
		}

	private:
		HRESULT m_hr;
		bool m_bUninitialize;
	};
}
