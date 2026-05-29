#pragma once

#include <atlstr.h>
#include <objbase.h>

#define EMULEBB_CUSTOM_AUTO_COMPLETE_SEAMS_HAS_ENUM_STRING_COPY 1

namespace CustomAutoCompleteSeams
{
typedef LPVOID (STDAPICALLTYPE *EnumStringAllocator)(SIZE_T nBytes);

/**
 * @brief Copies one autocomplete value into COM task memory for IEnumString.
 */
inline HRESULT CopyEnumString(const CString &rValue, LPOLESTR &rpEnumString, EnumStringAllocator pAllocator = ::CoTaskMemAlloc) noexcept
{
	rpEnumString = NULL;
	if (pAllocator == NULL)
		return E_POINTER;

	const CStringW strValue(rValue);
	const SIZE_T nChars = static_cast<SIZE_T>(strValue.GetLength()) + 1u;
	LPWSTR pszCopy = static_cast<LPWSTR>(pAllocator(sizeof(WCHAR) * nChars));
	if (pszCopy == NULL)
		return E_OUTOFMEMORY;

	wcscpy_s(pszCopy, nChars, strValue);
	rpEnumString = pszCopy;
	return S_OK;
}
}
