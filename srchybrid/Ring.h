//this file is part of eMule
//Copyright (C)2024-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
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
#pragma once

typedef struct {
	uint64	datalen;
	ULONGLONG timestamp;
} TransferredData;

template<class TYPE> class CRing
{
	UINT_PTR m_nCount;		//the number of added items
	UINT_PTR m_nIncrement;	//increase capacity by this number of items
	UINT_PTR m_nSize;		//buffer capacity
	TYPE *m_pData;			//the buffer
	TYPE *m_pEnd;			//after the allocated space
	TYPE *m_pHead;			//the oldest item (to be extracted first)
	TYPE *m_pTail;			//the latest added item

	void SetBuffer(UINT_PTR nSize);
public:
	explicit CRing(UINT_PTR nSize = 128, UINT_PTR nIncrement = 128); //zero values default to 128
	~CRing()								{ delete[] m_pData; }
	CRing(const CRing&) = delete;
	CRing& operator=(const CRing&) = delete;
	const TYPE& operator [](UINT_PTR index) const	{ ASSERT(index < m_nCount); return m_pData[(index + (m_pHead - m_pData)) % m_nSize]; }

	void AddTail(const TYPE &newElement);
	UINT_PTR Capacity() const				{ return m_nSize; }
	UINT_PTR Count() const					{ return m_nCount; }
	const TYPE& Head() const				{ ASSERT(m_nCount > 0); return *m_pHead; }
	const TYPE& Tail() const				{ ASSERT(m_nCount > 0); return *m_pTail; }
	bool IsEmpty() const					{ return !m_nCount; }
	void RemoveAll();
	void RemoveHead();
	void SetCapacity(UINT_PTR nSize);
	void SetIncrement(UINT_PTR nIncrement)	{ m_nIncrement = nIncrement ? nIncrement : 128; }
};

template<class TYPE>
CRing<TYPE>::CRing(UINT_PTR nSize, UINT_PTR nIncrement)
	: m_nCount()
	, m_nIncrement(nIncrement ? nIncrement : 128)
	, m_nSize(nSize ? nSize : 128)
	, m_pData()
{
	SetBuffer(m_nSize);
}

template<class TYPE>
void CRing<TYPE>::AddTail(const TYPE &newElement)
{
	if (m_nCount >= m_nSize)
		SetCapacity(m_nSize + m_nIncrement);
	if (m_nCount == 0) {
		m_pHead = m_pTail = m_pData;
		m_nCount = 1;
		*m_pTail = newElement;
		return;
	}
	++m_nCount;
	if (++m_pTail >= m_pEnd)
		m_pTail = m_pData;
	*m_pTail = newElement;
}

template<class TYPE>
void CRing<TYPE>::RemoveAll()
{
	m_nCount = 0;
	m_pHead = m_pData;
	m_pTail = m_pEnd;
}

template<class TYPE>
void CRing<TYPE>::RemoveHead()
{
	if (m_nCount) {
		--m_nCount;
		if (m_nCount == 0) {
			m_pHead = m_pData;
			m_pTail = m_pEnd;
		} else if (++m_pHead >= m_pEnd)
			m_pHead = m_pData;
	}
}

template<class TYPE>
void CRing<TYPE>::SetBuffer(UINT_PTR nSize)
{
	TYPE *dst = new TYPE[nSize];
	if (m_nCount)
		if (m_pHead > m_pTail) {
			const UINT_PTR nHeadCount = static_cast<UINT_PTR>(m_pEnd - m_pHead);
			const UINT_PTR nTailCount = m_nCount - nHeadCount;
			memcpy(dst, m_pHead, nHeadCount * sizeof TYPE);
			memcpy(&dst[nHeadCount], m_pData, nTailCount * sizeof TYPE);
		} else
			memcpy(dst, m_pHead, m_nCount * sizeof TYPE);
	delete[] m_pData;
	m_nSize = nSize;
	m_pHead = m_pData = dst;
	m_pEnd = &dst[nSize];
	m_pTail = m_nCount ? &dst[m_nCount - 1] : m_pEnd;
}

template<class TYPE>
void CRing<TYPE>::SetCapacity(UINT_PTR nSize)
{
	if (nSize > m_nSize)
		SetBuffer(nSize);
}
