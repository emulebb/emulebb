/*CAsyncSocketEx by Tim Kosse (tim.kosse@filezilla-project.org)
			Version 1.3 (2003-04-26)
--------------------------------------------------------

Introduction:
-------------

CAsyncSocketEx is a replacement for the MFC class CAsyncSocket.
This class was written because CAsyncSocket is not the fastest WinSock
wrapper and it's very hard to add new functionality to CAsyncSocket
derived classes. This class offers the same functionality as CAsyncSocket.
Also, CAsyncSocketEx offers some enhancements which were not possible with
CAsyncSocket without some tricks.

How do I use it?
----------------
Basically exactly like CAsyncSocket.
To use CAsyncSocketEx, just replace all occurrences of CAsyncSocket in your
code with CAsyncSocketEx, if you did not enhance CAsyncSocket yourself in
any way, you won't have to change anything else in your code.

Why is CAsyncSocketEx faster?
-----------------------------

CAsyncSocketEx is slightly faster when dispatching notification event messages.
First have a look at the way CAsyncSocket works. For each thread that uses
CAsyncSocket, a window is created. CAsyncSocket calls WSAAsyncSelect with
the handle of that window. Until here, CAsyncSocketEx works the same way.
But CAsyncSocket uses only one window message (WM_SOCKET_NOTIFY) for all
sockets within one thread. When the window receive WM_SOCKET_NOTIFY, wParam
contains the socket handle and the window looks up a CAsyncSocket instance
using a map. CAsyncSocketEx works differently. Its helper window uses a
wide range of different window messages (WM_USER through 0xBFFF) and passes
a different message to WSAAsyncSelect for each socket. When a message in
the specified range is received, CAsyncSocketEx looks up the pointer to a
CAsyncSocketEx instance in an Array using the index of message - WM_USER.
As you can see, CAsyncSocketEx uses the helper window in a more efficient
way, as it don't have to use the slow maps to lookup its own instance.
Still, speed increase is not very much, but it may be noticeable when using
a lot of sockets at the same time.
Please note that the changes do not affect the raw data throughput rate,
CAsyncSocketEx only dispatches the notification messages faster.

What else does CAsyncSocketEx offer?
------------------------------------

CAsyncSocketEx offers a flexible layer system. One example is the proxy layer.
Just create an instance of the proxy layer, configure it and add it to the layer
chain of your CAsyncSocketEx instance. After that, you can connect through
proxies.
Benefit: You don't have to change much to use the layer system.
Another layer that is currently in development is the SSL layer to establish
SSL encrypted connections.

License
-------

Feel free to use this class, as long as you don't claim that you wrote it
and this copyright notice stays intact in the source files.
If you use this class in commercial applications, please send a short message
to tim.kosse@filezilla-project.org
*/

#include "stdafx.h"
#include "AsyncSocketEx.h"

#include "AsyncDnsResolveSeams.h"
#include "AsyncSocketExLayer.h"
#include "AsyncSocketExRuntimeSeams.h"
#include "BindInterfaceSocketSeams.h"
#include "IPv4AddressSeams.h"
#include "Log.h"
#include "Preferences.h"

#include <atomic>
#include <memory>
#include <new>

// This legacy helper-window socket backend intentionally stays on WSAAsyncSelect
// until the broader async-socket transport replacement is scheduled.
#pragma warning(disable:4996)

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

THREADLOCAL CAsyncSocketEx::t_AsyncSocketExThreadData *CAsyncSocketEx::thread_local_data = NULL;
static std::atomic<UINT_PTR> s_uAsyncResolveNextRequestId{0};

/////////////////////////////
//Helper Window class

class CAsyncSocketExHelperWindow
{
public:
	explicit CAsyncSocketExHelperWindow(CAsyncSocketEx::t_AsyncSocketExThreadData *pThreadData)
		: m_nWindowDataSize(512)
		, m_nWindowDataPos()
		, m_nSocketCount()
		, m_pThreadData(pThreadData)
	{
		static LPCTSTR const sHelperWnd = _T("CAsyncSocketEx Helper Window");
		//Initialize data
		m_pAsyncSocketExWindowData = new t_AsyncSocketExWindowData[m_nWindowDataSize]{}; //Reserve space for 512 active sockets

		//Create window
		WNDCLASSEX wndclass{};
		wndclass.cbSize = (UINT)sizeof wndclass;
		wndclass.lpfnWndProc = WindowProc;
		wndclass.hInstance = ::GetModuleHandle(NULL);
		wndclass.lpszClassName = sHelperWnd;
		::RegisterClassEx(&wndclass);

		//have no z-order, cannot be enumerated, and do not receive broadcast messages.
		m_hWnd = ::CreateWindow(sHelperWnd, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL,0);
		if (m_hWnd)
			::SetWindowLongPtr(m_hWnd, GWLP_USERDATA, (LONG_PTR)this);
		else
			ASSERT(0);
	};

	virtual	~CAsyncSocketExHelperWindow()
	{
		//Clean up socket storage
		delete[] m_pAsyncSocketExWindowData;
		m_pAsyncSocketExWindowData = NULL;
		m_nWindowDataSize = 0;
		m_nSocketCount = 0;

		//Destroy window
		if (m_hWnd) {
			DestroyWindow(m_hWnd);
			m_hWnd = 0;
		}
	};

	CAsyncSocketExHelperWindow(const CAsyncSocketExHelperWindow&) = delete;
	CAsyncSocketExHelperWindow& operator=(const CAsyncSocketExHelperWindow&) = delete;

	//Adds a socket to the list of attached sockets
	BOOL AddSocket(CAsyncSocketEx *pSocket, int &nSocketIndex)
	{
		if (!pSocket) {
			ASSERT(0);
			return FALSE;
		}
		if (!m_nWindowDataSize) {
			ASSERT(!m_nSocketCount);
			m_nWindowDataSize = 512;
			m_pAsyncSocketExWindowData = new t_AsyncSocketExWindowData[512]{}; //Reserve space for 512 active sockets
		}

		if (nSocketIndex >= 0) {
			ASSERT(m_pAsyncSocketExWindowData);
			ASSERT(m_nWindowDataSize > nSocketIndex);
			ASSERT(m_pAsyncSocketExWindowData[nSocketIndex].m_pSocket == pSocket);
			ASSERT(m_nSocketCount);
			return m_pAsyncSocketExWindowData != NULL;
		}

		//Increase socket storage if too small
		if (m_nSocketCount >= m_nWindowDataSize - 10) {
			int nOldWindowDataSize = m_nWindowDataSize;
			ASSERT(m_nWindowDataSize < MAX_SOCKETS);
			m_nWindowDataSize += 512;
			if (m_nWindowDataSize > MAX_SOCKETS)
				m_nWindowDataSize = MAX_SOCKETS;
			t_AsyncSocketExWindowData *tmp = m_pAsyncSocketExWindowData;
			m_pAsyncSocketExWindowData = new t_AsyncSocketExWindowData[m_nWindowDataSize];
			memcpy(m_pAsyncSocketExWindowData, tmp, nOldWindowDataSize * sizeof(t_AsyncSocketExWindowData));
			memset(&m_pAsyncSocketExWindowData[nOldWindowDataSize], 0, (m_nWindowDataSize - nOldWindowDataSize) * sizeof(t_AsyncSocketExWindowData));
			delete[] tmp;
		}

		//Search for free slot
		for (int i = m_nWindowDataPos; i < m_nWindowDataSize + m_nWindowDataPos; ++i) {
			int idx = i % m_nWindowDataSize;
			if (!m_pAsyncSocketExWindowData[idx].m_pSocket) {
				m_pAsyncSocketExWindowData[idx].m_pSocket = pSocket;
				nSocketIndex = idx;
				m_nWindowDataPos = (i + 1) % m_nWindowDataSize;
				++m_nSocketCount;
				return TRUE;
			}
		}

		//No slot found, maybe there are too many sockets!
		return FALSE;
	}

	//Removes a socket from the socket storage
	BOOL RemoveSocket(const CAsyncSocketEx *pSocket, int &nSocketIndex)
	{
		if (!pSocket) {
			ASSERT(0);
			return FALSE;
		}
		if (nSocketIndex >= 0) {
			// Remove additional messages from queue
				MSG msg;
			while (::PeekMessage(&msg, m_hWnd, WM_SOCKETEX_NOTIFY + nSocketIndex, WM_SOCKETEX_NOTIFY + nSocketIndex, PM_REMOVE));

			ASSERT(m_pAsyncSocketExWindowData);
			ASSERT(m_nWindowDataSize > 0);
			ASSERT(m_nSocketCount > 0);
			ASSERT(m_pAsyncSocketExWindowData[nSocketIndex].m_pSocket == pSocket);
			m_pAsyncSocketExWindowData[nSocketIndex].m_pSocket = NULL;
			nSocketIndex = -1;
			--m_nSocketCount;
		}
		return TRUE;
	}

	bool IsCurrentSocket(const CAsyncSocketEx *pSocket, int nSocketIndex, SOCKET hSocket) const
	{
		return pSocket != NULL
			&& hSocket != INVALID_SOCKET
			&& m_pAsyncSocketExWindowData != NULL
			&& nSocketIndex >= 0
			&& nSocketIndex < m_nWindowDataSize
			&& m_pAsyncSocketExWindowData[nSocketIndex].m_pSocket == pSocket
			&& pSocket->m_SocketData.hSocket == hSocket;
	}

	void RemoveLayers(const CAsyncSocketEx *pOrigSocket)
	{
		// Remove all layer messages from old socket
		std::vector<MSG> msgList;

		for (MSG msg; ::PeekMessage(&msg, m_hWnd, WM_SOCKETEX_TRIGGER, WM_SOCKETEX_TRIGGER, PM_REMOVE);) {
			//Verify parameters, lookup socket and notification message
			if (msg.wParam >= static_cast<WPARAM>(m_nWindowDataSize)) //Index is within socket storage
				continue;

			const CAsyncSocketEx *pSocket = m_pAsyncSocketExWindowData[msg.wParam].m_pSocket;
			CAsyncSocketExLayer::t_LayerNotifyMsg *pMsg = reinterpret_cast<CAsyncSocketExLayer::t_LayerNotifyMsg*>(msg.lParam);
			if (!pMsg || !pSocket || pSocket->m_SocketData.hSocket == INVALID_SOCKET || pSocket == pOrigSocket || pSocket->m_SocketData.hSocket != pMsg->hSocket)
				delete pMsg;
			else
				msgList.push_back(msg);
		}

		for (std::vector<MSG>::const_iterator iter = msgList.begin(); iter != msgList.end(); ++iter)
			if (!::PostMessage(m_hWnd, iter->message, iter->wParam, iter->lParam))
				delete reinterpret_cast<CAsyncSocketExLayer::t_LayerNotifyMsg*>(iter->lParam);
	}

	//Processes event notifications sent by the sockets or the layers
	static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message >= WM_SOCKETEX_NOTIFY) {
			//Verify parameters
			ASSERT(hWnd);
			CAsyncSocketExHelperWindow *pWnd = reinterpret_cast<CAsyncSocketExHelperWindow*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
			if (!pWnd) {
				ASSERT(0);
				return 0;
			}

			if (message < static_cast<UINT>(WM_SOCKETEX_NOTIFY + pWnd->m_nWindowDataSize)) { //Index is within socket storage
				//Lookup socket and verify if it is valid
				const int nSocketIndex = message - WM_SOCKETEX_NOTIFY;
				CAsyncSocketEx *pSocket = pWnd->m_pAsyncSocketExWindowData[nSocketIndex].m_pSocket;
				if (!pSocket)
					return 0;
				SOCKET hSocket = wParam;
				if (hSocket == INVALID_SOCKET || pSocket->m_SocketData.hSocket != hSocket)
					return 0;

				int nEvent = (int)WSAGETSELECTEVENT(lParam);
				int nErrorCode = (int)WSAGETSELECTERROR(lParam);

				//Dispatch notification
				if (!pSocket->m_pFirstLayer) {
					//Dispatch to CAsyncSocketEx instance
					switch (nEvent) {
					case FD_READ:
					case FD_FORCEREAD: //Forceread does not check if there's data waiting
						if (pSocket->GetState() == connecting && !nErrorCode) {
							pSocket->m_nPendingEvents |= nEvent;
							break;
						}
						if (pSocket->GetState() == attached)
							pSocket->SetState(connected);
						if (pSocket->GetState() != connected)
							break;

						// Ignore further FD_READ events after FD_CLOSE has been received
						if (pSocket->m_SocketData.bIsClosing && nEvent != FD_FORCEREAD)
							break;
						if (nErrorCode)
							pSocket->SetState(aborted);
						if (pSocket->m_lEvent & FD_READ)
							pSocket->OnReceive(nErrorCode);
						break;
					case FD_WRITE:
						if (pSocket->GetState() == connecting && !nErrorCode) {
							pSocket->m_nPendingEvents |= FD_WRITE;
							break;
						}
						if (pSocket->GetState() == attached && !nErrorCode)
							pSocket->SetState(connected);
						if (pSocket->GetState() != connected)
							break;
						if (nErrorCode)
							pSocket->SetState(aborted);
						if (pSocket->m_lEvent & FD_WRITE)
							pSocket->OnSend(nErrorCode);
						break;
					case FD_CONNECT:
						if (pSocket->GetState() == connecting) {
							if (nErrorCode && pSocket->m_SocketData.nextAddr && pSocket->TryNextProtocol())
								break;

							pSocket->SetState(connected);
						} else if (pSocket->GetState() == attached && !nErrorCode)
							pSocket->SetState(connected);
						if (pSocket->m_lEvent & FD_CONNECT) {
							pSocket->OnConnect(nErrorCode);
							// WHY: server connect failures can synchronously
							// delete the socket from OnConnect(). The helper
							// window slot is the only valid owner check before
							// touching pending state or replaying callbacks.
							if (!pWnd->IsCurrentSocket(pSocket, nSocketIndex, hSocket))
								break;
						}

						if (!nErrorCode && pWnd->IsCurrentSocket(pSocket, nSocketIndex, hSocket)) {
							if ((pSocket->m_nPendingEvents & (FD_READ | FD_FORCEREAD)) && pSocket->GetState() == connected) {
								pSocket->OnReceive(0);
								if (!pWnd->IsCurrentSocket(pSocket, nSocketIndex, hSocket))
									break;
							}
							if ((pSocket->m_nPendingEvents & FD_WRITE) && pSocket->GetState() == connected) {
								pSocket->OnSend(0);
								if (!pWnd->IsCurrentSocket(pSocket, nSocketIndex, hSocket))
									break;
							}
						}
						if (pWnd->IsCurrentSocket(pSocket, nSocketIndex, hSocket))
							pSocket->m_nPendingEvents = 0;
						break;
					case FD_ACCEPT:
						if (pSocket->GetState() != listening && pSocket->GetState() != attached)
							break;
						if (pSocket->m_lEvent & FD_ACCEPT)
							pSocket->OnAccept(nErrorCode);
						break;
					case FD_CLOSE:
						if (pSocket->GetState() != connected && pSocket->GetState() != attached)
							break;

						// If there are still bytes left to read, call OnReceive instead of
						// OnClose and trigger a new FD_CLOSE
						DWORD nBytes;
						if (!nErrorCode && pSocket->IOCtl(FIONREAD, &nBytes) && nBytes > 0) {
							// Just repeat message.
							pSocket->ResendCloseNotify();
							pSocket->m_SocketData.bIsClosing = true;
							pSocket->OnReceive(WSAESHUTDOWN);
							break;
						}

						pSocket->SetState(nErrorCode ? aborted : closed);
						pSocket->OnClose(nErrorCode);
						break;
					}
				} else { //Dispatch notification to the lower layer
					if (nEvent == FD_READ) {
						// Ignore further FD_READ events after FD_CLOSE has been received
						if (pSocket->m_SocketData.bIsClosing)
							return 0;

						DWORD nBytes;
						if (!pSocket->IOCtl(FIONREAD, &nBytes))
							nErrorCode = WSAGetLastError();
					} else if (nEvent == FD_CLOSE) {
						// If there are still bytes left to read, call OnReceive instead of
						// OnClose and trigger a new FD_CLOSE
						DWORD nBytes;
						if (!nErrorCode && pSocket->IOCtl(FIONREAD, &nBytes) && nBytes > 0) {
							// Just repeat message.
							pSocket->ResendCloseNotify();
							nEvent = FD_READ;
						} else
							pSocket->m_SocketData.bIsClosing = true;
					}
					if (pSocket->m_pLastLayer)
						pSocket->m_pLastLayer->CallEvent(nEvent, nErrorCode);
				}
			}
			return 0;
		}
		switch (message) {
		case WM_SOCKETEX_TRIGGER: //Notification event sent by a layer
			{
				//Verify parameters, lookup socket and notification message
				ASSERT(hWnd);
				CAsyncSocketExHelperWindow *pWnd = reinterpret_cast<CAsyncSocketExHelperWindow*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
				ASSERT(pWnd);

				if (!pWnd || wParam >= static_cast<WPARAM>(pWnd->m_nWindowDataSize)) //Index is within socket storage
					return 0;

				const int nSocketIndex = static_cast<int>(wParam);
				CAsyncSocketEx *pSocket = pWnd->m_pAsyncSocketExWindowData[nSocketIndex].m_pSocket;
				CAsyncSocketExLayer::t_LayerNotifyMsg *pMsg = reinterpret_cast<CAsyncSocketExLayer::t_LayerNotifyMsg*>(lParam);
				if (!pMsg || !pSocket || pSocket->m_SocketData.hSocket == INVALID_SOCKET || pSocket->m_SocketData.hSocket != pMsg->hSocket) {
					delete pMsg;
					return 0;
				}
				int nEvent = WSAGETSELECTEVENT(pMsg->lEvent);
				int nErrorCode = WSAGETSELECTERROR(pMsg->lEvent);

				//Dispatch to layer
				if (pMsg->pLayer)
					pMsg->pLayer->CallEvent(nEvent, nErrorCode);
				else {
					//Dispatch to CAsyncSocketEx instance
					switch (nEvent) {
					case FD_READ:
					case FD_FORCEREAD: //Forceread does not check if there's data waiting
						if (pSocket->GetState() == connecting && !nErrorCode) {
							pSocket->m_nPendingEvents |= nEvent;
							break;
						}
						if (pSocket->GetState() == attached && !nErrorCode)
							pSocket->SetState(connected);
						if (pSocket->GetState() != connected)
							break;
						if (nErrorCode)
							pSocket->SetState(aborted);
						if (pSocket->m_lEvent & FD_READ)
							pSocket->OnReceive(nErrorCode);
						break;
					case FD_WRITE:
						if (pSocket->GetState() == connecting && !nErrorCode) {
							pSocket->m_nPendingEvents |= FD_WRITE;
							break;
						}
						if (pSocket->GetState() == attached && !nErrorCode)
							pSocket->SetState(connected);
						if (pSocket->GetState() != connected)
							break;
						if (nErrorCode)
							pSocket->SetState(aborted);
						if (pSocket->m_lEvent & FD_WRITE)
							pSocket->OnSend(nErrorCode);
						break;
					case FD_CONNECT:
						if (pSocket->GetState() == connecting)
							pSocket->SetState(connected);
						else if (pSocket->GetState() == attached && !nErrorCode)
							pSocket->SetState(connected);
						if (pSocket->m_lEvent & FD_CONNECT) {
							pSocket->OnConnect(nErrorCode);
							// WHY: OnConnect handlers may close and delete
							// their socket synchronously. Layer-triggered
							// connect completion must revalidate the helper
							// window slot before replaying pending events.
							if (!pWnd->IsCurrentSocket(pSocket, nSocketIndex, pMsg->hSocket))
								break;
						}

						if (!nErrorCode && pWnd->IsCurrentSocket(pSocket, nSocketIndex, pMsg->hSocket) && pSocket->GetState() == connected) {
							if ((pSocket->m_nPendingEvents & FD_READ) && pSocket->m_lEvent & FD_READ) {
								pSocket->OnReceive(0);
								if (!pWnd->IsCurrentSocket(pSocket, nSocketIndex, pMsg->hSocket))
									break;
							}
							if ((pSocket->m_nPendingEvents & FD_FORCEREAD) && pSocket->m_lEvent & FD_READ) {
								pSocket->OnReceive(0);
								if (!pWnd->IsCurrentSocket(pSocket, nSocketIndex, pMsg->hSocket))
									break;
							}
							if (pSocket->m_nPendingEvents & FD_WRITE && pSocket->m_lEvent & FD_WRITE) {
								pSocket->OnSend(0);
								if (!pWnd->IsCurrentSocket(pSocket, nSocketIndex, pMsg->hSocket))
									break;
							}
						}
						if (pWnd->IsCurrentSocket(pSocket, nSocketIndex, pMsg->hSocket))
							pSocket->m_nPendingEvents = 0;
						break;
					case FD_ACCEPT:
						if ((pSocket->GetState() == listening || pSocket->GetState() == attached) && (pSocket->m_lEvent & FD_ACCEPT))
						{
							pSocket->OnAccept(nErrorCode);
						}
						break;
					case FD_CLOSE:
						if ((pSocket->GetState() == connected || pSocket->GetState() == attached) && (pSocket->m_lEvent & FD_CLOSE))
						{
							pSocket->SetState(nErrorCode ? aborted : closed);
							pSocket->OnClose(nErrorCode);
						}
						break;
					}
				}
				delete pMsg;
				return 0;
			}
		case WM_TIMER:
			{
				if (wParam != 1)
					return 0;

				ASSERT(hWnd);
				CAsyncSocketExHelperWindow *pWnd = reinterpret_cast<CAsyncSocketExHelperWindow*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
				if (!pWnd || !pWnd->m_pThreadData) {
					ASSERT(0);
					return 0;
				}

				if (pWnd->m_pThreadData->layerCloseNotify.empty()) {
					::KillTimer(hWnd, 1);
					return 0;
				}

				const CAsyncSocketEx *socket = pWnd->m_pThreadData->layerCloseNotify.front();
				pWnd->m_pThreadData->layerCloseNotify.pop_front();
				if (pWnd->m_pThreadData->layerCloseNotify.empty())
					::KillTimer(hWnd, 1);
				if (socket)
					::PostMessage(hWnd, WM_SOCKETEX_NOTIFY + socket->m_SocketData.nSocketIndex, socket->m_SocketData.hSocket, FD_CLOSE);
			}
			return 0;
		case WM_SOCKETEX_GETHOST: // hostname resolver worker reply
			{
				// Verify parameters
				ASSERT(hWnd);
				CAsyncSocketExHelperWindow *pWnd = reinterpret_cast<CAsyncSocketExHelperWindow*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
				ASSERT(pWnd);
				std::unique_ptr<AsyncDnsResolveSeams::SHostnameResolveResult> pResult(reinterpret_cast<AsyncDnsResolveSeams::SHostnameResolveResult*>(lParam));
				if (!pWnd || !pResult)
					return 0;

				const int nSocketIndex = static_cast<int>(wParam);
				if (nSocketIndex < 0 || nSocketIndex >= pWnd->m_nWindowDataSize)
					return 0;

				CAsyncSocketEx *pSocket = pWnd->m_pAsyncSocketExWindowData[nSocketIndex].m_pSocket;
				if (pSocket == NULL || pSocket->m_uAsyncResolveRequestId != pResult->uRequestId)
					return 0;

				pSocket->m_uAsyncResolveRequestId = 0;
				const int nResolveError = AsyncDnsResolveSeams::GetLegacyHostResolveError(*pResult);
				if (nResolveError != 0) {
					pSocket->OnConnect(nResolveError);
					return 0;
				}

				SOCKADDR_IN sockAddr = pResult->sockAddr;
				if (!pSocket->OnHostNameResolved(&sockAddr))
					return 0;

				BOOL res = pSocket->Connect((LPSOCKADDR)& sockAddr, sizeof sockAddr);

				if (!res && GetLastError() != WSAEWOULDBLOCK)
					pSocket->OnConnect(GetLastError());
			}
			return 0;
		case WM_SOCKETEX_CALLBACK:
			{
				//Verify parameters, lookup socket and notification message
				if (!hWnd)
					return 0;

				CAsyncSocketExHelperWindow *pWnd = reinterpret_cast<CAsyncSocketExHelperWindow*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));

				if (!pWnd || wParam >= static_cast<WPARAM>(pWnd->m_nWindowDataSize)) //Index is within socket storage
					return 0;

				CAsyncSocketEx *pSocket = pWnd->m_pAsyncSocketExWindowData[wParam].m_pSocket;
				if (!pSocket)
					return 0;

				// Process pending callbacks
				std::vector<t_callbackMsg> tmp;
				tmp.swap(pSocket->m_pendingCallbacks);
				pSocket->OnLayerCallback(tmp);
			}
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	HWND GetHwnd() const
	{
		return m_hWnd;
	}

private:
	HWND m_hWnd;
	struct t_AsyncSocketExWindowData
	{
		CAsyncSocketEx *m_pSocket;
	} *m_pAsyncSocketExWindowData;
	int m_nWindowDataSize; //number of socket pointers in array m_pAsyncSocketExWindowData[]
	int m_nWindowDataPos;
	int m_nSocketCount;
	CAsyncSocketEx::t_AsyncSocketExThreadData *m_pThreadData;
};

//////////////////////////////////////////////////////////////////////
// Konstruktion/Destruktion
//////////////////////////////////////////////////////////////////////

IMPLEMENT_DYNAMIC(CAsyncSocketEx, CObject)

CAsyncSocketEx::CAsyncSocketEx()
	: m_pLocalAsyncSocketExThreadData()
	, m_uAsyncResolveRequestId()
	, m_nState(notsock)
	, m_nPendingEvents()
	, m_pFirstLayer()
	, m_pLastLayer()
	, m_nSocketPort()
	, m_lEvent()
{
	m_SocketData.addrInfo = NULL;
	m_SocketData.nextAddr = NULL;
	m_SocketData.hSocket = INVALID_SOCKET;
	m_SocketData.nSocketIndex = -1;
	m_SocketData.nFamily = AF_UNSPEC;
	m_SocketData.bIsClosing = false;
}

CAsyncSocketEx::~CAsyncSocketEx()
{
	CAsyncSocketEx::Close();
	FreeAsyncSocketExInstance();
}

bool CAsyncSocketEx::Create(UINT nSocketPort /*=0*/, int nSocketType /*=SOCK_STREAM*/, long lEvent /*=FD_DEFAULT*/, const CString &sSocketAddress /*=CString()*/, ADDRESS_FAMILY nFamily /*=AF_INET*/, bool reusable /*=false*/)
{
	//Close the socket, although this should not happen
	if (GetSocketHandle() != INVALID_SOCKET) {
		ASSERT(0);
		WSASetLastError(WSAEALREADY);
		return false;
	}

	if (!InitAsyncSocketExInstance()) {
		ASSERT(0);
		WSASetLastError(WSANOTINITIALISED);
		return false;
	}

	m_SocketData.nFamily = nFamily;

	if (m_pFirstLayer) {
		bool res = m_pFirstLayer->Create(nSocketPort, nSocketType, lEvent, sSocketAddress, nFamily, reusable);
		if (res)
			SetState(unconnected);
		return res;
	}

	if (m_SocketData.nFamily == AF_UNSPEC) {
		SetState(unconnected);
		m_lEvent = lEvent;
		m_nSocketPort = nSocketPort;
		m_sSocketAddress = sSocketAddress;
		return true;
	}

	SOCKET hSocket = socket(m_SocketData.nFamily, nSocketType, 0);
	if (hSocket == INVALID_SOCKET)
		return false;
	m_SocketData.hSocket = hSocket;
	if (!AttachHandle()) {
		closesocket(m_SocketData.hSocket);
		m_SocketData.hSocket = INVALID_SOCKET;
		return false;
	}

	if (m_pFirstLayer) {
		m_lEvent = lEvent;
		if (WSAAsyncSelect(m_SocketData.hSocket, GetHelperWindowHandle(), WM_SOCKETEX_NOTIFY + m_SocketData.nSocketIndex, FD_DEFAULT)) {
			Close();
			return false;
		}
	} else if (!AsyncSelect(lEvent)) {
		Close();
		return false;
	}

	if (reusable && nSocketPort != 0) {
		BOOL value = TRUE;
		SetSockOpt(SO_REUSEADDR, reinterpret_cast<const void*>(&value), sizeof value);
	}

	if (!Bind(nSocketPort, sSocketAddress)) {
		Close();
		return false;
	}
	if (!ApplyConfiguredIpv4UnicastInterface()) {
		Close();
		return false;
	}

	SetState(unconnected);

	return true;
}

bool CAsyncSocketEx::OnHostNameResolved(const SOCKADDR_IN * /*pSockAddr*/)
{
	return true;
}

void CAsyncSocketEx::OnReceive(int /*nErrorCode*/)
{
}

void CAsyncSocketEx::OnSend(int /*nErrorCode*/)
{
}

void CAsyncSocketEx::OnConnect(int /*nErrorCode*/)
{
}

void CAsyncSocketEx::OnAccept(int /*nErrorCode*/)
{
}

void CAsyncSocketEx::OnClose(int /*nErrorCode*/)
{
}

bool CAsyncSocketEx::Bind(UINT nSocketPort, const CString &sSocketAddress)
{
	m_sSocketAddress = sSocketAddress;
	m_nSocketPort = nSocketPort;

	if (m_SocketData.nFamily == AF_UNSPEC)
		return true;

	const CStringA sAscii(sSocketAddress);

	if (sAscii.IsEmpty()) {
		if (m_SocketData.nFamily == AF_INET) {
			SOCKADDR_IN sockAddr = {};
			sockAddr.sin_family = AF_INET;
			sockAddr.sin_addr.s_addr = INADDR_ANY;
			sockAddr.sin_port = htons((u_short)nSocketPort);

			return Bind((LPSOCKADDR)&sockAddr, sizeof sockAddr);
		}
		if (m_SocketData.nFamily == AF_INET6) {
			SOCKADDR_IN6 sockAddr6 = {};
			sockAddr6.sin6_family = AF_INET6;
			sockAddr6.sin6_addr = in6addr_any;
			sockAddr6.sin6_port = htons((u_short)nSocketPort);

			return Bind((LPSOCKADDR)&sockAddr6, sizeof sockAddr6);
		}
	} else {
		addrinfo hints = {};
		hints.ai_family = m_SocketData.nFamily;
		hints.ai_socktype = SOCK_STREAM;
		CStringA port;
		port.Format("%u", nSocketPort);
		addrinfo *res0;
		if (getaddrinfo(sAscii, port, &hints, &res0))
			return false;

		bool ret = false;
		for (addrinfo *res = res0; res; res = res->ai_next)
			if (Bind(res->ai_addr, (int)res->ai_addrlen)) {
				ret = true;
				break;
			}

		freeaddrinfo(res0);
		return ret;
	}
	return false;
}

BOOL CAsyncSocketEx::Bind(const LPSOCKADDR lpSockAddr, int nSockAddrLen)
{
	return !bind(m_SocketData.hSocket, lpSockAddr, nSockAddrLen);
}

BOOL CAsyncSocketEx::AttachHandle(/*SOCKET hSocket*/)
{
	ASSERT(m_pLocalAsyncSocketExThreadData);
	try {
		// WHY: release builds compile VERIFY to a plain expression; a failed
		// helper-window slot allocation must not leave the socket attached with
		// nSocketIndex == -1 because async messages and DetachHandle depend on
		// that index. Return failure so callers can close the raw SOCKET before
		// it becomes an unroutable async endpoint.
		if (!m_pLocalAsyncSocketExThreadData->m_pHelperWindow->AddSocket(this, m_SocketData.nSocketIndex)) {
			WSASetLastError(WSAENOBUFS);
			ASSERT(0);
			return FALSE;
		}
	} catch (CMemoryException *ex) {
		if (ex != NULL)
			ex->Delete();
		WSASetLastError(WSAENOBUFS);
		return FALSE;
	} catch (const std::bad_alloc&) {
		WSASetLastError(WSAENOBUFS);
		return FALSE;
	}
	SetState(attached);
	return TRUE;
}

void CAsyncSocketEx::DetachHandle()
{
	m_SocketData.hSocket = INVALID_SOCKET;
	if (!m_pLocalAsyncSocketExThreadData) {
		ASSERT(0);
		return;
	}
	if (!m_pLocalAsyncSocketExThreadData->m_pHelperWindow) {
		ASSERT(0);
		return;
	}
	VERIFY(m_pLocalAsyncSocketExThreadData->m_pHelperWindow->RemoveSocket(this, m_SocketData.nSocketIndex));
	SetState(notsock);
}

void CAsyncSocketEx::Close()
{
	m_nPendingEvents = 0;
	if (m_pFirstLayer)
		m_pFirstLayer->Close();
	if (m_SocketData.hSocket != INVALID_SOCKET) {
		VERIFY(closesocket(m_SocketData.hSocket) != SOCKET_ERROR);
		DetachHandle();
	}
	if (m_SocketData.addrInfo) {
		freeaddrinfo(m_SocketData.addrInfo);
		m_SocketData.addrInfo = NULL;
		m_SocketData.nextAddr = NULL;
	}
	m_SocketData.nFamily = AF_UNSPEC;
	m_sSocketAddress.Empty();
	m_nSocketPort = 0;
	RemoveAllLayers();
	m_uAsyncResolveRequestId = 0;
	m_SocketData.bIsClosing = false;
}

bool CAsyncSocketEx::InitAsyncSocketExInstance()
{
	//Check if already initialized
	if (!m_pLocalAsyncSocketExThreadData) {
		// Get thread specific data
		if (!thread_local_data) {
			try {
				thread_local_data = new t_AsyncSocketExThreadData{};
				thread_local_data->m_pHelperWindow = new CAsyncSocketExHelperWindow(thread_local_data);
			} catch (...) {
				if (thread_local_data) {
					delete thread_local_data;
					thread_local_data = NULL;
				}
				if (AsyncSocketExRuntimeSeams::ShouldLogThreadDataInitFailure())
					DebugLogError(_T("Failed to initialize AsyncSocketEx helper thread data"));
				return false;
			}
		}
		m_pLocalAsyncSocketExThreadData = thread_local_data;
		++m_pLocalAsyncSocketExThreadData->nInstanceCount;
	}
	return true;
}

void CAsyncSocketEx::FreeAsyncSocketExInstance()
{
	//Check if already freed
	if (!m_pLocalAsyncSocketExThreadData)
		return;

	std::list<CAsyncSocketEx*> &socks = m_pLocalAsyncSocketExThreadData->layerCloseNotify;
	std::list<CAsyncSocketEx*>::const_iterator iter = std::find(socks.begin(), socks.end(), this);
	if (iter != socks.end()) {
		socks.erase(iter);
		if (socks.empty())
			::KillTimer(m_pLocalAsyncSocketExThreadData->m_pHelperWindow->GetHwnd(), 1);
	}

	if (!--m_pLocalAsyncSocketExThreadData->nInstanceCount) {
		m_pLocalAsyncSocketExThreadData = NULL;
		delete thread_local_data->m_pHelperWindow;
		delete thread_local_data;
		thread_local_data = NULL;
	}
}

int CAsyncSocketEx::Receive(void *lpBuf, int nBufLen, int nFlags /*=0*/)
{
	if (m_pFirstLayer)
		return m_pFirstLayer->Receive(lpBuf, nBufLen, nFlags);
	return recv(m_SocketData.hSocket, (LPSTR)lpBuf, nBufLen, nFlags);
}

int CAsyncSocketEx::Send(const void *lpBuf, int nBufLen, int nFlags /*=0*/)
{
	if (m_pFirstLayer)
		return m_pFirstLayer->Send(lpBuf, nBufLen, nFlags);
	return send(m_SocketData.hSocket, (LPSTR)lpBuf, nBufLen, nFlags);
}

bool CAsyncSocketEx::Connect(const CString &sHostAddress, UINT nHostPort)
{
	if (m_pFirstLayer) {
		bool res = m_pFirstLayer->Connect(sHostAddress, nHostPort);
		if (res || GetLastError() == WSAEWOULDBLOCK)
			SetState(connecting);
		return res;
	}

	const CStringA sAscii(sHostAddress);
	ASSERT(!sAscii.IsEmpty());

	if (m_SocketData.nFamily == AF_INET) {
		SOCKADDR_IN sockAddr = {};
		sockAddr.sin_family = AF_INET;

		uint32_t uNetworkOrderAddress = 0;
		if (!IPv4AddressSeams::TryParseIPv4Address(sHostAddress, uNetworkOrderAddress)) {
			const UINT_PTR uRequestId = AsyncDnsResolveSeams::AllocateNonZeroRequestId(s_uAsyncResolveNextRequestId);
			std::unique_ptr<AsyncDnsResolveSeams::SHostnameResolveWork> pWork = AsyncDnsResolveSeams::MakeHostnameResolveWork(
				GetHelperWindowHandle(),
				WM_SOCKETEX_GETHOST,
				static_cast<WPARAM>(m_SocketData.nSocketIndex),
				uRequestId,
				sAscii,
				SOCK_STREAM,
				static_cast<USHORT>(nHostPort));
			m_uAsyncResolveRequestId = uRequestId;

			if (AsyncDnsResolveSeams::StartHostnameResolveThread(pWork)) {
				SetState(connecting);
				WSASetLastError(WSAEWOULDBLOCK);
				return false;
			}

			m_uAsyncResolveRequestId = 0;
			WSASetLastError(WSA_NOT_ENOUGH_MEMORY);
			return false;
		}

		sockAddr.sin_addr.s_addr = uNetworkOrderAddress;
		sockAddr.sin_port = htons((u_short)nHostPort);
		return CAsyncSocketEx::Connect((LPSOCKADDR)&sockAddr, sizeof sockAddr);
	}

	if (m_SocketData.addrInfo) {
		freeaddrinfo(m_SocketData.addrInfo);
		m_SocketData.addrInfo = NULL;
		m_SocketData.nextAddr = NULL;
	}

	addrinfo hints = {};
	hints.ai_family = m_SocketData.nFamily;
	hints.ai_socktype = SOCK_STREAM;
	CStringA port;
	port.Format("%u", nHostPort);
	if (getaddrinfo(sAscii, port, &hints, &m_SocketData.addrInfo))
		return false;

	bool ret = false;
	for (m_SocketData.nextAddr = m_SocketData.addrInfo; m_SocketData.nextAddr; m_SocketData.nextAddr = m_SocketData.nextAddr->ai_next) {
		bool newSocket = (m_SocketData.nFamily == AF_UNSPEC);
		if (newSocket)
			m_SocketData.hSocket = socket(m_SocketData.nextAddr->ai_family, m_SocketData.nextAddr->ai_socktype, m_SocketData.nextAddr->ai_protocol);
		if (m_SocketData.hSocket == INVALID_SOCKET)
			continue;

		if (newSocket) {
			m_SocketData.nFamily = (ADDRESS_FAMILY)m_SocketData.nextAddr->ai_family;
			if (!AttachHandle()) {
				m_SocketData.nFamily = AF_UNSPEC;
				closesocket(m_SocketData.hSocket);
				m_SocketData.hSocket = INVALID_SOCKET;
				continue;
			}
		}

		if (AsyncSelect(m_lEvent) && (!newSocket || Bind(m_nSocketPort, m_sSocketAddress)) && ApplyConfiguredIpv4UnicastInterface()) {
			ret = Connect(m_SocketData.nextAddr->ai_addr, (int)m_SocketData.nextAddr->ai_addrlen);
			if (ret || GetLastError() == WSAEWOULDBLOCK)
				break;
		}

		if (newSocket) {
			m_SocketData.nFamily = AF_UNSPEC;
			closesocket(m_SocketData.hSocket);
			DetachHandle();
		}
	}

	if (m_SocketData.nextAddr)
		m_SocketData.nextAddr = m_SocketData.nextAddr->ai_next;

	if (!m_SocketData.nextAddr) {
		freeaddrinfo(m_SocketData.addrInfo);
		m_SocketData.addrInfo = NULL;
	}

	return ret && m_SocketData.hSocket != INVALID_SOCKET;
}

BOOL CAsyncSocketEx::Connect(const LPSOCKADDR lpSockAddr, int nSockAddrLen)
{
	BOOL res;
	if (m_pFirstLayer)
		res = m_pFirstLayer->Connect(lpSockAddr, nSockAddrLen);
	else
		res = !connect(m_SocketData.hSocket, lpSockAddr, nSockAddrLen);

	if (res || GetLastError() == WSAEWOULDBLOCK)
		SetState(connecting);
	return res;
}

bool CAsyncSocketEx::GetPeerName(CString &rPeerAddress, UINT &rPeerPort)
{
	if (m_pFirstLayer)
		return m_pFirstLayer->GetPeerName(rPeerAddress, rPeerPort);
	if (m_SocketData.nFamily != AF_INET6 && m_SocketData.nFamily != AF_INET)
		return false;

	int nSockAddrLen = (int)((m_SocketData.nFamily == AF_INET6) ? sizeof(SOCKADDR_IN6) : sizeof(SOCKADDR_IN));
	LPSOCKADDR sockAddr = (LPSOCKADDR)new char[nSockAddrLen]();

	bool bResult = GetPeerName(sockAddr, &nSockAddrLen);
	if (bResult)
		if (m_SocketData.nFamily == AF_INET6) {
			rPeerPort = ntohs(((LPSOCKADDR_IN6)sockAddr)->sin6_port);
			rPeerAddress = Inet6AddrToString(((LPSOCKADDR_IN6)sockAddr)->sin6_addr);
		} else {
			rPeerPort = ntohs(((LPSOCKADDR_IN)sockAddr)->sin_port);
			rPeerAddress = IPv4AddressSeams::FormatIPv4Address(((LPSOCKADDR_IN)sockAddr)->sin_addr.s_addr);
		}

	delete[] sockAddr;
	return bResult;
}

BOOL CAsyncSocketEx::GetPeerName(LPSOCKADDR lpSockAddr, int *lpSockAddrLen)
{
	if (m_pFirstLayer)
		return m_pFirstLayer->GetPeerName(lpSockAddr, lpSockAddrLen);
	return !getpeername(m_SocketData.hSocket, lpSockAddr, lpSockAddrLen);
}

bool CAsyncSocketEx::GetSockName(CString &rSocketAddress, UINT &rSocketPort) const
{
	if (m_SocketData.nFamily != AF_INET6 && m_SocketData.nFamily != AF_INET)
		return false;
	int nSockAddrLen = (int)((m_SocketData.nFamily == AF_INET6) ? sizeof(SOCKADDR_IN6) : sizeof(SOCKADDR_IN));
	LPSOCKADDR sockAddr = (LPSOCKADDR)new char[nSockAddrLen]();

	bool bResult = GetSockName(sockAddr, &nSockAddrLen);
	if (bResult)
		if (m_SocketData.nFamily == AF_INET6) {
			rSocketPort = ntohs(((LPSOCKADDR_IN6)sockAddr)->sin6_port);
			rSocketAddress = Inet6AddrToString(((LPSOCKADDR_IN6)sockAddr)->sin6_addr);
		} else {
			rSocketPort = ntohs(((LPSOCKADDR_IN)sockAddr)->sin_port);
			rSocketAddress = IPv4AddressSeams::FormatIPv4Address(((LPSOCKADDR_IN)sockAddr)->sin_addr.s_addr);
		}

	delete[] sockAddr;
	return bResult;
}

BOOL CAsyncSocketEx::GetSockName(LPSOCKADDR lpSockAddr, int *lpSockAddrLen) const
{
	return !getsockname(m_SocketData.hSocket, lpSockAddr, lpSockAddrLen);
}

BOOL CAsyncSocketEx::ShutDown(int nHow /*=CAsyncSocket::sends*/)
{
	if (m_pFirstLayer)
		return m_pFirstLayer->ShutDown(nHow);
	return !shutdown(m_SocketData.hSocket, nHow);
}

SOCKET CAsyncSocketEx::Detach()
{
	SOCKET socket = m_SocketData.hSocket;
	DetachHandle();
	m_SocketData.nFamily = AF_UNSPEC;
	return socket;
}

BOOL CAsyncSocketEx::Attach(SOCKET hSocket, long lEvent /*= FD_DEFAULT*/)
{
	if (hSocket == INVALID_SOCKET)
		return FALSE;
	if (!InitAsyncSocketExInstance()) {
		// WHY: VERIFY evaluates away to a best-effort boolean in release builds.
		// AttachHandle requires initialized thread-local socket data, so fail
		// before publishing the raw SOCKET when the helper window slot cannot be
		// prepared.
		ASSERT(0);
		WSASetLastError(WSANOTINITIALISED);
		return FALSE;
	}
	m_SocketData.hSocket = hSocket;
	if (!AttachHandle()) {
		closesocket(m_SocketData.hSocket);
		m_SocketData.hSocket = INVALID_SOCKET;
		return FALSE;
	}

	if (m_pFirstLayer) {
		m_lEvent = lEvent;
		return !WSAAsyncSelect(m_SocketData.hSocket, GetHelperWindowHandle(), WM_SOCKETEX_NOTIFY + m_SocketData.nSocketIndex, FD_DEFAULT);
	}
	return AsyncSelect(lEvent);
}

BOOL CAsyncSocketEx::AsyncSelect(long lEvent /*= FD_DEFAULT*/)
{
	ASSERT(m_pLocalAsyncSocketExThreadData);
	m_lEvent = lEvent;
	if (m_pFirstLayer)
		return TRUE;
	if (m_SocketData.hSocket == INVALID_SOCKET && m_SocketData.nFamily == AF_UNSPEC)
		return TRUE;
	return !WSAAsyncSelect(m_SocketData.hSocket, GetHelperWindowHandle(), WM_SOCKETEX_NOTIFY + m_SocketData.nSocketIndex, lEvent);
}

BOOL CAsyncSocketEx::Listen(int nConnectionBacklog /*=5*/)
{
	if (m_pFirstLayer)
		return m_pFirstLayer->Listen(nConnectionBacklog);

	if (!listen(m_SocketData.hSocket, nConnectionBacklog)) {
		SetState(listening);
		return TRUE;
	}
	return FALSE;
}

BOOL CAsyncSocketEx::Accept(CAsyncSocketEx &rConnectedSocket, LPSOCKADDR lpSockAddr /*=NULL*/, int *lpSockAddrLen /*=NULL*/)
{
	ASSERT(rConnectedSocket.m_SocketData.hSocket == INVALID_SOCKET);
	if (m_pFirstLayer)
		return m_pFirstLayer->Accept(rConnectedSocket, lpSockAddr, lpSockAddrLen);

	SOCKET hTemp = accept(m_SocketData.hSocket, lpSockAddr, lpSockAddrLen);
	if (hTemp == INVALID_SOCKET)
		return FALSE;
	if (!rConnectedSocket.InitAsyncSocketExInstance()) {
		// WHY: accepted sockets are owned by this function until AttachHandle
		// publishes them to the async helper window. Release builds must close
		// the raw SOCKET instead of continuing into AttachHandle with missing
		// thread-local socket data.
		ASSERT(0);
		closesocket(hTemp);
		WSASetLastError(WSANOTINITIALISED);
		return FALSE;
	}
	rConnectedSocket.m_SocketData.hSocket = hTemp;
	if (!rConnectedSocket.AttachHandle()) {
		closesocket(hTemp);
		rConnectedSocket.m_SocketData.hSocket = INVALID_SOCKET;
		return FALSE;
	}
	rConnectedSocket.SetFamily(GetFamily());
	rConnectedSocket.SetState(connected);
	return TRUE;
}

BOOL CAsyncSocketEx::IOCtl(long lCommand, DWORD *lpArgument)
{
	return !ioctlsocket(m_SocketData.hSocket, lCommand, lpArgument);
}

BOOL CAsyncSocketEx::TriggerEvent(long lEvent)
{
	if (m_SocketData.hSocket == INVALID_SOCKET)
		return FALSE;

	ASSERT(m_pLocalAsyncSocketExThreadData);
	ASSERT(m_pLocalAsyncSocketExThreadData->m_pHelperWindow);
	ASSERT(m_SocketData.nSocketIndex >= 0);

	if (m_pFirstLayer) {
		CAsyncSocketExLayer::t_LayerNotifyMsg *pMsg = new CAsyncSocketExLayer::t_LayerNotifyMsg;
		pMsg->hSocket = m_SocketData.hSocket;
		pMsg->lEvent = WSAGETSELECTEVENT(lEvent);
		pMsg->pLayer = NULL;
		BOOL res = ::PostMessage(GetHelperWindowHandle(), WM_SOCKETEX_TRIGGER, (WPARAM)m_SocketData.nSocketIndex, (LPARAM)pMsg);
		if (!res)
			delete pMsg;
		return res;
	}
	return ::PostMessage(GetHelperWindowHandle(), WM_SOCKETEX_NOTIFY + m_SocketData.nSocketIndex, m_SocketData.hSocket, WSAGETSELECTEVENT(lEvent));
}

HWND CAsyncSocketEx::GetHelperWindowHandle()
{
	if (!m_pLocalAsyncSocketExThreadData || !m_pLocalAsyncSocketExThreadData->m_pHelperWindow)
		return 0;
	return m_pLocalAsyncSocketExThreadData->m_pHelperWindow->GetHwnd();
}

BOOL CAsyncSocketEx::AddLayer(CAsyncSocketExLayer *pLayer)
{
	ASSERT(pLayer);
	if (m_pFirstLayer) {
		ASSERT(m_pLastLayer);
		m_pLastLayer = m_pLastLayer->AddLayer(pLayer, this);
		return m_pLastLayer != NULL;
	}

	ASSERT(!m_pLastLayer);
	pLayer->Init(NULL, this);
	m_pFirstLayer = pLayer;
	m_pLastLayer = m_pFirstLayer;

	return m_SocketData.hSocket == INVALID_SOCKET
		|| !WSAAsyncSelect(m_SocketData.hSocket, GetHelperWindowHandle(), WM_SOCKETEX_NOTIFY + m_SocketData.nSocketIndex, FD_DEFAULT);
}

void CAsyncSocketEx::RemoveAllLayers()
{
	CAsyncSocketEx::OnLayerCallback(m_pendingCallbacks);

	m_pFirstLayer = NULL;
	m_pLastLayer = NULL;

	if (m_pLocalAsyncSocketExThreadData && m_pLocalAsyncSocketExThreadData->m_pHelperWindow)
		m_pLocalAsyncSocketExThreadData->m_pHelperWindow->RemoveLayers(this);
}

int CAsyncSocketEx::OnLayerCallback(std::vector<t_callbackMsg> &callbacks)
{
	while (!callbacks.empty()) {
		delete[] callbacks.back().str;
		callbacks.pop_back();
	}
	return 0;
}

BOOL CAsyncSocketEx::GetSockOpt(int nOptionName, void *lpOptionValue, int *lpOptionLen, int nLevel /*=SOL_SOCKET*/) const
{
	return !getsockopt(m_SocketData.hSocket, nLevel, nOptionName, (LPSTR)lpOptionValue, lpOptionLen);
}

BOOL CAsyncSocketEx::SetSockOpt(int nOptionName, const void *lpOptionValue, int nOptionLen, int nLevel /*=SOL_SOCKET*/)
{
	return !setsockopt(m_SocketData.hSocket, nLevel, nOptionName, (LPSTR)lpOptionValue, nOptionLen);
}

bool CAsyncSocketEx::ApplyConfiguredIpv4UnicastInterface()
{
	// WHY: the IP_UNICAST_IF pinning below is IPv4-only. When a VPN-guard bind
	// interface is configured and resolved, an IPv6 socket would leave the tunnel
	// unpinned (an IPv6 leak); the bind-address match below cannot catch it because
	// an IPv6 socket is never bound to the IPv4 bind address. Fail closed so the
	// kill-switch stays IPv4-locked. P2P sockets are AF_INET today, so this only
	// blocks a future IPv6 P2P path while bind enforcement is active.
	if (m_SocketData.nFamily == AF_INET6
		&& BindInterfaceSocketSeams::ShouldApplyIpv4UnicastInterfaceOption(
			!thePrefs.GetActiveBindInterface().IsEmpty()
			, thePrefs.GetActiveBindAddressResolveResult() == BARR_Resolved
			, thePrefs.GetActiveBindInterfaceIndex())) {
		DebugLogError(_T("P2P bind interface enforcement: refusing IPv6 socket because IP_UNICAST_IF tunnel pinning is IPv4-only (interface=%s)")
			, (LPCTSTR)thePrefs.GetActiveBindInterfaceName());
		return false;
	}

	if (m_sSocketAddress.IsEmpty())
		return true;
	CString strActiveBindAddress;
	if (thePrefs.GetBindAddr() != NULL)
		strActiveBindAddress = thePrefs.GetBindAddr();
	if (strActiveBindAddress.IsEmpty() || m_sSocketAddress.CompareNoCase(strActiveBindAddress))
		return true;

	int nError = 0;
	if (BindInterfaceSocketSeams::ApplyIpv4UnicastInterfaceOption(m_SocketData.hSocket
		, m_SocketData.nFamily
		, !thePrefs.GetActiveBindInterface().IsEmpty()
		, thePrefs.GetActiveBindAddressResolveResult() == BARR_Resolved
		, thePrefs.GetActiveBindInterfaceIndex()
		, &nError))
		return true;

	DebugLogError(_T("P2P bind interface enforcement failed: IP_UNICAST_IF could not be applied to %s (ifIndex=%lu, error=%d)")
		, (LPCTSTR)thePrefs.GetActiveBindInterfaceName()
		, thePrefs.GetActiveBindInterfaceIndex()
		, nError);
	return false;
}

bool CAsyncSocketEx::SetFamily(ADDRESS_FAMILY nFamily)
{
	if (m_SocketData.nFamily != AF_UNSPEC)
		return false;

	m_SocketData.nFamily = nFamily;
	return true;
}

bool CAsyncSocketEx::TryNextProtocol()
{
	closesocket(m_SocketData.hSocket);
	DetachHandle();

	bool ret = false;
	for (; m_SocketData.nextAddr; m_SocketData.nextAddr = m_SocketData.nextAddr->ai_next) {
		m_SocketData.hSocket = socket(m_SocketData.nextAddr->ai_family, m_SocketData.nextAddr->ai_socktype, m_SocketData.nextAddr->ai_protocol);

		if (m_SocketData.hSocket == INVALID_SOCKET)
			continue;

		m_SocketData.nFamily = (ADDRESS_FAMILY)m_SocketData.nextAddr->ai_family;
		if (!AttachHandle()) {
			closesocket(m_SocketData.hSocket);
			m_SocketData.hSocket = INVALID_SOCKET;
			continue;
		}

		if (AsyncSelect(m_lEvent))
			if (!m_pFirstLayer || !WSAAsyncSelect(m_SocketData.hSocket, GetHelperWindowHandle(), WM_SOCKETEX_NOTIFY + m_SocketData.nSocketIndex, FD_DEFAULT))
				if (Bind(m_nSocketPort, m_sSocketAddress) && ApplyConfiguredIpv4UnicastInterface()) {
					ret = Connect(m_SocketData.nextAddr->ai_addr, (int)m_SocketData.nextAddr->ai_addrlen);
					if (ret || GetLastError() == WSAEWOULDBLOCK)
						break;
				}

		closesocket(m_SocketData.hSocket);
		DetachHandle();
	}

	if (m_SocketData.nextAddr)
		m_SocketData.nextAddr = m_SocketData.nextAddr->ai_next;

	if (!m_SocketData.nextAddr) {
		freeaddrinfo(m_SocketData.addrInfo);
		m_SocketData.addrInfo = NULL;
	}

	return ret && m_SocketData.hSocket != INVALID_SOCKET;
}

void CAsyncSocketEx::AddCallbackNotification(const t_callbackMsg &msg)
{
	m_pendingCallbacks.push_back(msg);

	if (m_pendingCallbacks.size() == 1 && m_SocketData.nSocketIndex >= 0)
		::PostMessage(GetHelperWindowHandle(), WM_SOCKETEX_CALLBACK, (WPARAM)m_SocketData.nSocketIndex, 0);
}

void CAsyncSocketEx::ResendCloseNotify()
{
	std::list<CAsyncSocketEx*> &socks = m_pLocalAsyncSocketExThreadData->layerCloseNotify;
	std::list<CAsyncSocketEx*>::const_iterator iter = std::find(socks.begin(), socks.end(), this);
	if (iter == socks.end()) {
		m_pLocalAsyncSocketExThreadData->layerCloseNotify.push_back(this);
		if (m_pLocalAsyncSocketExThreadData->layerCloseNotify.size() == 1)
			::SetTimer(m_pLocalAsyncSocketExThreadData->m_pHelperWindow->GetHwnd(), 1, 10, NULL);
	}
}

#ifdef _DEBUG
void CAsyncSocketEx::AssertValid() const
{
	CObject::AssertValid();

	(void)m_SocketData;
	(void)m_lEvent;
	(void)m_uAsyncResolveRequestId;
	(void)m_nSocketPort;
	(void)m_pendingCallbacks;
	(void)m_pFirstLayer;
	(void)m_pLastLayer;
}

void CAsyncSocketEx::Dump(CDumpContext &dc) const
{
	CObject::Dump(dc);
}
#endif
