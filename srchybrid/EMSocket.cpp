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
#ifdef _DEBUG
#include "DebugHelpers.h"
#endif
#include "emule.h"
#include <timeapi.h>
#include "emsocket.h"
#include "AsyncProxySocketLayer.h"
#include "EMSocketSendSeams.h"
#include "Packets.h"
#include "SocketIoSeams.h"
#include "LongPathSeams.h"
#include "OtherFunctions.h"
#include "UploadBandwidthThrottler.h"
#include "Preferences.h"
#include "Log.h"

#include <memory>
#include <new>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


namespace
{
	inline void EMTrace(char *fmt, ...)
	{
#ifdef EMSOCKET_DEBUG
		va_list argptr;
		char bufferline[512];
		va_start(argptr, fmt);
		vsnprintf(bufferline, _countof(bufferline), fmt, argptr);
		va_end(argptr);
		//(Ornis+)
		char osDate[30], osTime[30];
		char temp[1024];
		_strtime(osTime);
		_strdate(osDate);
		int len = snprintf(temp, _countof(temp), "%s %s: %s\r\n", osDate, osTime, bufferline);
		if (len > 0) {
			HANDLE hFile = LongPathSeams::CreateFile(_T("c:\\tmp\\EMSocket.log")	// ensure valid path to a writable location
				, GENERIC_WRITE			// open for writing
				, FILE_SHARE_READ		// share for reading
				, NULL					// no security
				, OPEN_ALWAYS			// open existing or create new
				, FILE_ATTRIBUTE_NORMAL // normal file
				, NULL);				// no template file

			if (hFile != INVALID_HANDLE_VALUE) {
				DWORD nbBytesWritten;
				SetFilePointer(hFile, 0, NULL, FILE_END);
				::WriteFile(hFile	// handle to file
					, temp				// data buffer
					, len				// number of bytes to write
					, &nbBytesWritten	// number of bytes written
					, NULL				// overlapped buffer
				);
				::CloseHandle(hFile);
			}
		}
#else
		//va_list argptr;
		//va_start(argptr, fmt);
		//va_end(argptr);
		UNREFERENCED_PARAMETER(fmt);
#endif //EMSOCKET_DEBUG
	}
}

IMPLEMENT_DYNAMIC(CEMSocket, CEncryptedStreamSocket)

CEMSocket::CEMSocket()
	: m_pProxyLayer()
	, m_uTimeOut(thePrefs.GetConnectionTimeout()) // default timeout for ed2k sockets
	, byConnected(EMS_NOTCONNECTED)
	, m_bProxyConnectFailed()
	, downloadLimitEnable()
	, pendingOnReceive()
	, downloadLimit()
	, pendingPacketSize()
	, pendingPacket()
	, pendingHeaderSize()
	, pendingHeader()
	, sendbuffer()
	, sendblen()
	, sent()
	, m_nQueuedControlPacketBytes()
	, m_nQueuedStandardPacketBytes()
	, m_numberOfSentBytesCompleteFile()
	, m_numberOfSentBytesPartFile()
	//, m_numberOfSentBytesControlPacket()
	, lastFinishedStandard()
	, m_actualPayloadSize()
	, m_actualPayloadSizeSent()
	, m_currentPacket_is_controlpacket()
	, m_currentPackageIsFromPartFile()
	, m_bAccelerateUpload()
	, m_bBusy()
	, m_hasSent()
	, m_bUseBigSendBuffers()
	, m_bUseOverlappedSend(true)
	, m_bPendingSendOv()
	, m_bOverlappedSendBorrowsSendBuffer()
{
	// Keep timeGetTime() intentionally for upload send pacing. Measurement on
	// 2026-05-02 showed 1 ms deltas here, while GetTickCount64 stayed at
	// 15-16 ms on the same host; this code also relies on legacy DWORD delta
	// arithmetic for timer wraparound.
	lastCalledSend = timeGetTime();
	lastSent = lastCalledSend > SEC2MS(1) ? lastCalledSend - SEC2MS(1) : 0;
}

CEMSocket::~CEMSocket()
{
	EMTrace("CEMSocket::~CEMSocket() on %u", (SOCKET)this);

	// need to be locked here to know that the other methods
	// won't be in the middle of things
	sendLocker.Lock();
	byConnected = EMS_DISCONNECTED;
	CleanUpOverlappedSendOperation(true);
	sendLocker.Unlock();

	// now that we know no other method will keep adding to the queue
	// we can remove ourself from the queue
	theApp.uploadBandwidthThrottler->RemoveFromAllQueues(this);

	ClearQueues();
	CEMSocket::RemoveAllLayers(); // deadlake PROXYSUPPORT
	AsyncSelect(0);
}

// deadlake PROXYSUPPORT
// By Maverick: Connection initialization is done by class itself
bool CEMSocket::Connect(const CString &sHostAddress, UINT nHostPort)
{
	InitProxySupport();
	return CEncryptedStreamSocket::Connect(sHostAddress, nHostPort);
}
// end deadlake

// deadlake PROXYSUPPORT
// By Maverick: Connection initialization is done by class itself
BOOL CEMSocket::Connect(const LPSOCKADDR pSockAddr, int iSockAddrLen)
{
	InitProxySupport();
	return CEncryptedStreamSocket::Connect(pSockAddr, iSockAddrLen);
}
// end deadlake

void CEMSocket::InitProxySupport()
{
	m_bProxyConnectFailed = false;

	// Proxy Initialization
	const ProxySettings &settings = thePrefs.GetProxySettings();
	if (settings.bUseProxy && settings.type != PROXYTYPE_NOPROXY) {
		m_bUseOverlappedSend = false;
		Close();

		m_pProxyLayer = new CAsyncProxySocketLayer;
		switch (settings.type) {
		case PROXYTYPE_SOCKS4:
		case PROXYTYPE_SOCKS4A:
			m_pProxyLayer->SetProxy(settings.type, settings.host, settings.port);
			break;
		case PROXYTYPE_SOCKS5:
		case PROXYTYPE_HTTP10:
		case PROXYTYPE_HTTP11:
			if (settings.bEnablePassword)
				m_pProxyLayer->SetProxy(settings.type, settings.host, settings.port, settings.user, settings.password);
			else
				m_pProxyLayer->SetProxy(settings.type, settings.host, settings.port);
			break;
		default:
			ASSERT(0);
		}
		AddLayer(m_pProxyLayer);

		// Connection Initialization
		Create(0, SOCK_STREAM, FD_DEFAULT, thePrefs.GetBindAddr());
		AsyncSelect(FD_DEFAULT);
	}
}

void CEMSocket::ClearQueues()
{
	EMTrace("CEMSocket::ClearQueues on %u", (SOCKET)this);

	sendLocker.Lock();
	while (!controlpacket_queue.IsEmpty())
		delete controlpacket_queue.RemoveHead();
	m_nQueuedControlPacketBytes = 0;

	while (!standardpacket_queue.IsEmpty())
		delete standardpacket_queue.RemoveHead().packet;
	m_nQueuedStandardPacketBytes = 0;
	sendLocker.Unlock();

	// Download (pseudo) rate control
	downloadLimit = 0;
	downloadLimitEnable = false;
	pendingOnReceive = false;

	// Download partial header
	pendingHeaderSize = 0;

	// Download partial packet
	delete pendingPacket;
	pendingPacket = NULL;
	pendingPacketSize = 0;

	// Upload control
	delete[] sendbuffer;
	sendbuffer = NULL;

	sendblen = 0;
	sent = 0;
}

void CEMSocket::OnClose(int nErrorCode)
{
	// need to be locked here to know that the other methods
	// won't be in the middle of things
	SetConState(EMS_DISCONNECTED);

	// now that we know no other method will keep adding to the queue
	// we can remove ourself from the queue
	theApp.uploadBandwidthThrottler->RemoveFromAllQueues(this);

	CEncryptedStreamSocket::OnClose(nErrorCode); // deadlake changed socket to PROXYSUPPORT ( AsyncSocketEx )
	RemoveAllLayers(); // deadlake PROXYSUPPORT
	ClearQueues();
}

BOOL CEMSocket::AsyncSelect(long lEvent)
{
#ifdef EMSOCKET_DEBUG
	if (lEvent & FD_READ)
		EMTrace("  FD_READ");
	if (lEvent & FD_CLOSE)
		EMTrace("  FD_CLOSE");
	if (lEvent & FD_WRITE)
		EMTrace("  FD_WRITE");
#endif
	// deadlake changed to AsyncSocketEx PROXYSUPPORT
	return (m_SocketData.hSocket == INVALID_SOCKET) || CEncryptedStreamSocket::AsyncSelect(lEvent);
}

void CEMSocket::OnReceive(int nErrorCode)
{
	// the 2 meg size was taken from another place
	static char GlobalReadBuffer[2000000];

	// Check for an error code
	if (nErrorCode != 0 && nErrorCode != WSAESHUTDOWN) {
		OnError(nErrorCode);
		return;
	}

	// Check current connection state
	if (byConnected == EMS_DISCONNECTED)
		return;

	byConnected = EMS_CONNECTED; // EMS_DISCONNECTED, EMS_NOTCONNECTED, EMS_CONNECTED

	// CPU load improvement
	if (downloadLimitEnable && downloadLimit == 0 && nErrorCode != WSAESHUTDOWN) {
		EMTrace("CEMSocket::OnReceive blocked by limit");
		pendingOnReceive = true;
		return;
	}

	// Remark: an overflow can not occur here
	size_t readMax = sizeof GlobalReadBuffer - pendingHeaderSize;
	if (downloadLimitEnable && readMax > downloadLimit && nErrorCode != WSAESHUTDOWN)
		readMax = downloadLimit;

	// We attempt to read up to 2 megs at a time (minus whatever is in our internal read buffer)
	int ret = Receive(GlobalReadBuffer + pendingHeaderSize, (int)readMax);
	if (ret == SOCKET_ERROR || byConnected == EMS_DISCONNECTED)
		return;
	if (!HasValidSocketReceiveResult(ret, readMax)) {
		OnError(ERR_WRONGHEADER);
		return;
	}

	// Bandwidth control
	if (downloadLimitEnable)
		// Update limit
		downloadLimit = ClampSocketReceiveBudget(downloadLimit, GetRealReceivedBytes());

	// CPU load improvement
	// Detect if the socket's buffer is empty (or the size did match...)
	pendingOnReceive = m_bFullReceive;

	if (ret == 0)
		return;

	// Copy back the partial header into the global read buffer for processing
	if (pendingHeaderSize > 0) {
		memcpy(GlobalReadBuffer, pendingHeader, pendingHeaderSize);
		ret += (int)pendingHeaderSize;
		pendingHeaderSize = 0;
	}

	if (IsRawDataMode()) {
		DataReceived((BYTE*)GlobalReadBuffer, ret);
		return;
	}

	char *rptr = GlobalReadBuffer; // floating index initialized with the buffer base
	const char *rend = GlobalReadBuffer + ret; // end of buffer
	// Loop, processing packets until we run out of them
	while (rend >= rptr + PACKET_HEADER_SIZE || (pendingPacket != NULL && rend > rptr)) {
		// Two possibilities here:
		//
		// 1. There is no pending incoming packet
		// 2. There is already a partial pending incoming packet
		//
		// It's important to remember that emule exchanges two kinds of packets
		// - The control packet
		// - The data packet for the transport of the block
		//
		// The biggest part of the traffic is done with the data packets.
		// The default size of one block is 10240 bytes (or less if compressed), but the
		// maximum size for one packet on the network is 1300 bytes.
		// It's the reason why most of the blocks were split before being sent.
		//
		// Conclusion: When the download limit is disabled, this method may be called
		// 8 times (10240/1300) by the lower layer before the split packet was
		// rebuilt and transferred to the above layer for processing.
		//
		// The purpose of this algorithm is to limit the amount of data exchanged between buffers

		if (pendingPacket == NULL) {
			// Bugfix We still need to check for a valid protocol
			// Remark: the default eMule v0.26b had removed this test...
			switch (reinterpret_cast<Header_Struct*>(rptr)->eDonkeyID) {
			case OP_EDONKEYPROT:
			case OP_PACKEDPROT:
			case OP_EMULEPROT:
				break;
			default:
				EMTrace("CEMSocket::OnReceive ERROR Wrong header");
				OnError(ERR_WRONGHEADER);
				return;
			}

			const Header_Struct *pHeader = reinterpret_cast<Header_Struct*>(rptr);

			/** Reject malformed or oversized packet lengths before deriving the payload size. */
			if (pHeader->packetlength == 0) {
				OnError(ERR_WRONGHEADER);
				return;
			}

			// Security: Check for buffer overflow (2MB)
			if (pHeader->packetlength - 1 > sizeof GlobalReadBuffer) {
				OnError(ERR_TOOBIG);
				return;
			}
			// Init data buffer
			pendingPacket = new Packet(rptr);	// Create new packet container.
			rptr += PACKET_HEADER_SIZE;			// Only the header is initialized so far
			size_t nPacketAllocationSize = 0;
			if (!TryGetIncomingPacketAllocationSize(pendingPacket->size, sizeof GlobalReadBuffer, &nPacketAllocationSize)) {
				delete pendingPacket;
				pendingPacket = NULL;
				OnError(ERR_TOOBIG);
				return;
			}
			try {
				// WHY: the payload length is peer-controlled but protocol-capped. A
				// valid-length packet can still fail allocation under memory pressure;
				// treat that as a socket failure so the receive callback unwinds in the
				// normal connection-error path instead of throwing through MFC socket code.
				pendingPacket->pBuffer = new char[nPacketAllocationSize];
			} catch (CMemoryException *ex) {
				if (ex != NULL)
					ex->Delete();
				delete pendingPacket;
				pendingPacket = NULL;
				OnError(WSAENOBUFS);
				return;
			} catch (const std::bad_alloc &) {
				delete pendingPacket;
				pendingPacket = NULL;
				OnError(WSAENOBUFS);
				return;
			} catch (...) {
				delete pendingPacket;
				pendingPacket = NULL;
				OnError(WSAENOBUFS);
				return;
			}
			pendingPacketSize = 0;
		}

		// Bytes ready to be copied into packet's internal buffer
		ASSERT(rptr <= rend);
		uint32 toCopy = min(pendingPacket->size - pendingPacketSize, (uint32)(rend - rptr));

		// Copy bytes from Global buffer to packet's internal buffer
		memcpy(&pendingPacket->pBuffer[pendingPacketSize], rptr, toCopy);
		pendingPacketSize += toCopy;
		rptr += toCopy;

		// Check if packet is complete
		ASSERT(pendingPacket->size >= pendingPacketSize);
		if (pendingPacket->size == pendingPacketSize) {
#ifdef EMSOCKET_DEBUG
			EMTrace("CEMSocket::PacketReceived on %u, opcode=%X, realSize=%d"
				, (SOCKET)this, pendingPacket->opcode, pendingPacket->GetRealPacketSize());
#endif
			// Process packet
			bool bPacketResult = PacketReceived(pendingPacket);
			delete pendingPacket;
			pendingPacket = NULL;
			pendingPacketSize = 0;

			if (!bPacketResult)
				return;
		}
	}

	// Finally, if there is any data left over, save it for next time
	ASSERT(rptr <= rend);
	ASSERT(rend - rptr < PACKET_HEADER_SIZE);
	if (rptr < rend) {
		// Keep the partial head
		pendingHeaderSize = rend - rptr;
		memcpy(pendingHeader, rptr, pendingHeaderSize);
	}
}

void CEMSocket::SetDownloadLimit(uint32 limit)
{
	downloadLimit = limit;
	downloadLimitEnable = true;

	// CPU load improvement
	if (limit > 0 && pendingOnReceive)
		OnReceive(0);
}

void CEMSocket::DisableDownloadLimit()
{
	downloadLimitEnable = false;

	// CPU load improvement
	if (pendingOnReceive)
		OnReceive(0);
}

/**
 * Queues up the packet to be sent. Another thread will actually send the packet.
 *
 * TCP queues are bounded as a per-peer memory backstop. The caller transfers
 * packet ownership to this method, so packets rejected by the budget are deleted
 * here instead of being returned to old call sites that cannot observe a result.
 *
 * @param packet address to the packet that should be added to the queue
 *
 * @param controlpacket the packet is a controlpacket
 *
 * @param bForceImmediateSend drains a queued control packet immediately after
 *        successful queueing. It does not bypass the memory budget because
 *        peer-driven control packet storms must stay bounded.
 */
void CEMSocket::SendPacket(Packet *packet, bool controlpacket, uint32 actualPayloadSize, bool bForceImmediateSend)
{
	//EMTrace("CEMSocket::OnSenPacked1 controlcount %i, standardcount %i, isbusy: %i", controlpacket_queue.GetCount(), standardpacket_queue.GetCount(), IsBusy());

	//if(m_startSendTick > 0) {
	//	m_lastSendLatency = timeGetTime() - m_startSendTick;
	//}
	std::unique_ptr<Packet> ownedPacket(packet);
	bool bQueueControlPacket = false;
	bool bCloseForQueueLimit = false;
	unsigned uQueuedPacketsAtLimit = 0;
	uint64 uQueuedBytesAtLimit = 0;

	{
		CSingleLock lockSend(&sendLocker, TRUE);
		if (byConnected == EMS_DISCONNECTED) {
			// WHY: upload/read helper threads can race a disconnect between
			// their call into SendPacket and the queue mutation below. The
			// disconnected check must be protected by the same lock as
			// ClearQueues/OnClose so caller-transferred Packet ownership is
			// either queued on a live socket or destroyed here.
			return;
		}

		const uint32 nPacketBytes = ownedPacket->GetRealPacketSize();
		if (controlpacket) {
			if (!CanQueueEMSocketControlPacket(
					static_cast<size_t>(controlpacket_queue.GetCount()),
					m_nQueuedControlPacketBytes,
					nPacketBytes)) {
				uQueuedPacketsAtLimit = static_cast<unsigned>(controlpacket_queue.GetCount());
				uQueuedBytesAtLimit = m_nQueuedControlPacketBytes;
				bCloseForQueueLimit = true;
			} else {
				// WHY: CTypedPtrList::AddTail may allocate while sendLocker is
				// held. RAII unlocks on MFC memory exceptions, and ownedPacket
				// keeps caller-transferred ownership until the queue link exists.
				controlpacket_queue.AddTail(ownedPacket.get());
				ownedPacket.release();
				m_nQueuedControlPacketBytes += nPacketBytes;
				bQueueControlPacket = true;
			}
		} else {
			if (!CanQueueEMSocketStandardPacket(
					static_cast<size_t>(standardpacket_queue.GetCount()),
					m_nQueuedStandardPacketBytes,
					nPacketBytes)) {
				uQueuedPacketsAtLimit = static_cast<unsigned>(standardpacket_queue.GetCount());
				uQueuedBytesAtLimit = m_nQueuedStandardPacketBytes;
				bCloseForQueueLimit = true;
			} else {
				const bool first = !((sendbuffer && !m_currentPacket_is_controlpacket) || !standardpacket_queue.IsEmpty());
				// WHY: CList::AddTail can allocate. Publish Packet ownership
				// only after the list node is linked so low-memory failure
				// cannot leak the packet or leave sendLocker held.
				standardpacket_queue.AddTail(StandardPacketQueueEntry{ownedPacket.get(), actualPayloadSize});
				ownedPacket.release();
				m_nQueuedStandardPacketBytes += nPacketBytes;

				// reset timeout for the first time
				if (first) {
					lastFinishedStandard = timeGetTime();
					m_bAccelerateUpload = true;	// Always accelerate first packet in a block
				}
			}
		}
#ifdef _DEBUG
		if (bForceImmediateSend && !bCloseForQueueLimit) {
			// WHY: the immediate-send request is only meaningful for a control
			// packet that was just queued. Check that invariant while sendLocker
			// still protects the control queue; after unlock the throttler can
			// drain or requeue control work and make queue-count assertions racy.
			ASSERT(controlpacket);
			ASSERT(!controlpacket_queue.IsEmpty());
		}
#endif
	}

	if (bCloseForQueueLimit) {
		DebugLogWarning(controlpacket
			? _T("EMSocket: closing peer after rejecting outgoing control packet because the queue is full (%u packets, %I64u bytes)")
			: _T("EMSocket: closing peer after rejecting outgoing standard packet because the queue is full (%u packets, %I64u bytes)"),
			uQueuedPacketsAtLimit,
			uQueuedBytesAtLimit);
		// WHY: callers transfer packet ownership here and cannot observe a
		// false return. Closing the peer makes queue exhaustion explicit instead
		// of silently losing protocol/control data.
		OnError(WSAENOBUFS);
		return;
	}

	if (bQueueControlPacket)
		theApp.uploadBandwidthThrottler->QueueForSendingControlPacket(this, m_hasSent);

	if (bForceImmediateSend) {
		SendEM(1024, 0, true);
	}
}

uint32 CEMSocket::GetSentPayloadSinceLastCall(bool bReset)
{
	return bReset ? (uint32)::InterlockedExchange((LONG*)&m_actualPayloadSizeSent, 0) : m_actualPayloadSizeSent;
}

void CEMSocket::OnSend(int nErrorCode)
{
	//onSendWillBeCalledOuter = false;

	if (nErrorCode) {
		OnError(nErrorCode);
		return;
	}

	//EMTrace("CEMSocket::OnSend controlcount %i, standardcount %i, isbusy: %i", controlpacket_queue.GetCount(), standardpacket_queue.GetCount(), IsBusy());
	CEncryptedStreamSocket::OnSend(0);

	m_bBusy = false;

	// stopped sending here.
	//StoppedSendSoUpdateStats();

	if (byConnected == EMS_DISCONNECTED)
		return;

	byConnected = EMS_CONNECTED;

	if (m_currentPacket_is_controlpacket) {
		// queue up for control packet
		theApp.uploadBandwidthThrottler->QueueForSendingControlPacket(this, m_hasSent);
	}

	if (!m_bUseOverlappedSend && (!standardpacket_queue.IsEmpty() || sendbuffer != NULL))
		theApp.uploadBandwidthThrottler->SocketAvailable();
}

SocketSentBytes CEMSocket::SendEM(uint32 maxNumberOfBytesToSend, uint32 minFragSize, bool onlyAllowedToSendControlPacket)
{
	if (byConnected == EMS_DISCONNECTED)
		return SocketSentBytes{};
	if (m_bUseOverlappedSend)
		return SendOv(maxNumberOfBytesToSend, minFragSize, onlyAllowedToSendControlPacket);
	return SendStd(maxNumberOfBytesToSend, minFragSize, onlyAllowedToSendControlPacket);
}

/**
 * Try to put queued up data on the socket.
 *
 * Control packets have higher priority, and will be sent first, if possible.
 * Standard packets can be split up in several package containers. In that case
 * all the parts of a split package must be sent in a row, without any control packet
 * in between.
 *
 * @param maxNumberOfBytesToSend This is the maximum number of bytes that is allowed to be put on the socket
 *								 this call. The actual number of sent bytes will be returned from the method.
 *
 * @param onlyAllowedToSendControlPacket This call we only try to put control packets on the sockets.
 *										 If there's a standard packet "in the way", and we think that this socket
 *										 is no longer an upload slot, then it is OK to send the standard packet to
 *										 get it out of the way. But it is not allowed to pick a new standard packet
 *										 from the queue during this call. Several split packets are counted as one
 *										 standard packet though, so it is OK to finish them all off if necessary.
 *
 * @return the actual number of bytes that were put on the socket.
 */
SocketSentBytes CEMSocket::SendStd(uint32 maxNumberOfBytesToSend, uint32 minFragSize, bool onlyAllowedToSendControlPacket)
{
	//EMTrace("CEMSocket::Send controlcount %i, standardcount %i, isbusy: %i", controlpacket_queue.GetCount(), standardpacket_queue.GetCount(), IsBusy());
	SocketSentBytes ret{0, 0, true};

	sendLocker.Lock();
	if (byConnected == EMS_CONNECTED && IsEncryptionLayerReady()) {
		if (minFragSize < 1)
			minFragSize = 1;

		maxNumberOfBytesToSend = GetNextFragSize(maxNumberOfBytesToSend, minFragSize);

		lastCalledSend = timeGetTime();
		bool bWasLongTimeSinceSend = (lastCalledSend >= lastSent + SEC2MS(1));

		uint32 sentBytes = 0;
		while (sentBytes < maxNumberOfBytesToSend // don't send more than allowed
			&& ret.success // there should have been no error in earlier loop
			&& (sendbuffer != NULL || !controlpacket_queue.IsEmpty() || (!standardpacket_queue.IsEmpty() && !onlyAllowedToSendControlPacket)) // there must exist something to send
			&& (   !onlyAllowedToSendControlPacket // this means we are allowed to send both types of packets, so proceed
				|| (sendbuffer != NULL && m_currentPacket_is_controlpacket) // We are in the process of sending a control packet. We are always allowed to send those
				|| (sentBytes > 0 && sentBytes % minFragSize != 0) // Once we've started, continue to send until an even minFragsize to minimize packet overhead
				|| (sendbuffer == NULL && !controlpacket_queue.IsEmpty()) // There's a control packet in queue, and we are not currently sending anything, so we will handle the control packet next
				|| (sendbuffer != NULL && !m_currentPacket_is_controlpacket && bWasLongTimeSinceSend && !controlpacket_queue.IsEmpty() && sentBytes < minFragSize) // We have waited too long to clean the current packet (which may be a standard packet that is in the way). Proceed no matter what the value of onlyAllowedToSendControlPacket.
			   )
			)
		{ // If we are not sending a packet currently, we will need to find one to send
			if (sendbuffer == NULL) {
				Packet *curPacket = NULL;
				m_currentPacket_is_controlpacket = !controlpacket_queue.IsEmpty();
				if (m_currentPacket_is_controlpacket) {
					// There's a control packet to send
					curPacket = controlpacket_queue.RemoveHead();
					const uint32 nPacketBytes = curPacket->GetRealPacketSize();
					m_nQueuedControlPacketBytes = (m_nQueuedControlPacketBytes > nPacketBytes) ? m_nQueuedControlPacketBytes - nPacketBytes : 0;
				} else {
					if (standardpacket_queue.IsEmpty()) {
						// Just to be safe. Shouldn't happen?
						sendLocker.Unlock();

						// if we reach this point, then there's something wrong with the while condition above!
						ASSERT(0);
						theApp.QueueDebugLogLine(true, _T("EMSocket: Couldn't get a new packet! There's an error in the first while condition in EMSocket::Send()"));

						return ret;
					}
					// There's a standard packet to send
					StandardPacketQueueEntry queueEntry = standardpacket_queue.RemoveHead();
					curPacket = queueEntry.packet;
					const uint32 nPacketBytes = curPacket->GetRealPacketSize();
					m_nQueuedStandardPacketBytes = (m_nQueuedStandardPacketBytes > nPacketBytes) ? m_nQueuedStandardPacketBytes - nPacketBytes : 0;
					m_actualPayloadSize = queueEntry.actualPayloadSize;

					// remember this for statistics purposes.
					m_currentPackageIsFromPartFile = curPacket->IsFromPF();
				}

				// We found a package to send. Get the data to send from the
				// package container and dispose of the container.
				sendblen = curPacket->GetRealPacketSize();
				sendbuffer = curPacket->DetachPacket();
				sent = 0;
				delete curPacket;

				// encrypting which cannot be done transparent by base class
				CryptPrepareSendData((uchar*)sendbuffer, sendblen);
			}

			// At this point we've got a packet to send in sendbuffer. Try to send it. Loop until entire packet
			// is sent, or until we reach maximum bytes to send for this call, or until we get an error.
			// NOTE! If send would block (returns WSAEWOULDBLOCK), we will return from this method INSIDE this loop.
			while (sent < sendblen
				&& sentBytes < maxNumberOfBytesToSend
				&& (   !onlyAllowedToSendControlPacket // this means we are allowed to send both types of packets, so proceed
					|| m_currentPacket_is_controlpacket
					|| (bWasLongTimeSinceSend && sentBytes < minFragSize)
					|| sentBytes % minFragSize != 0
				   )
				&& ret.success)
			{
				uint32 tosend = sendblen - sent;
				if (!onlyAllowedToSendControlPacket || m_currentPacket_is_controlpacket) {
					if (tosend > maxNumberOfBytesToSend - sentBytes)
						tosend = maxNumberOfBytesToSend - sentBytes;
				} else if (bWasLongTimeSinceSend && minFragSize > sentBytes) {
					if (tosend > minFragSize - sentBytes)
						tosend = minFragSize - sentBytes;
				} else {
					uint32 nextFragMaxBytesToSent = GetNextFragSize(sentBytes, minFragSize);
					if (nextFragMaxBytesToSent >= sentBytes && tosend > nextFragMaxBytesToSent - sentBytes)
						tosend = nextFragMaxBytesToSent - sentBytes;
				}
				ASSERT(tosend != 0 && tosend <= sendblen - sent);

				lastSent = timeGetTime();

				uint32 result = CEncryptedStreamSocket::Send(sendbuffer + sent, tosend); // deadlake PROXYSUPPORT - changed to AsyncSocketEx
				if (result == (uint32)SOCKET_ERROR) {
					uint32 error = (uint32)CAsyncSocket::GetLastError();
					if (error == WSAEWOULDBLOCK) {
						m_bBusy = true;

						//m_wasBlocked = true;
						sendLocker.Unlock();

						// Send() blocked, onsend will be called when ready to send again
						return ret;
					}
					// Send() gave an error
					ret.success = false;
					//DEBUG_ONLY( AddDebugLogLine(true,"EMSocket: An error has occurred: %i", error) );
				} else {
					// we managed to send some bytes. Perform bookkeeping.
					m_bBusy = false;
					m_hasSent = true;

					uint32 nUpdatedSent = 0;
					if (!TryAccumulateSocketSendProgress(sent, tosend, sendblen, result, &nUpdatedSent)) {
						ret.success = false;
						theApp.QueueDebugLogLineEx(ERROR, _T("EMSocket::Send returned an invalid byte count: sent=%u request=%u result=%u buffer=%u"), sent, tosend, result, sendblen);
						break;
					}

					sent = nUpdatedSent;
					sentBytes = result;
					// Log send bytes in correct class
					if (!m_currentPacket_is_controlpacket) {
						ret.sentBytesStandardPackets += result;

						if (m_currentPackageIsFromPartFile)
							::InterlockedAdd64((LONG64*)&m_numberOfSentBytesPartFile, result);
						else
							::InterlockedAdd64((LONG64*)&m_numberOfSentBytesCompleteFile, result);
					} else {
						ret.sentBytesControlPackets += result;
						//::InterlockedAdd64((LONG64*)&m_numberOfSentBytesControlPacket, result);
					}
				}
			}

			if (sent == sendblen) {
				// we are done sending the current package. Delete it and set
				// sendbuffer to NULL so a new packet can be fetched.
				delete[] sendbuffer;
				sendbuffer = NULL;
				sendblen = 0;

				if (!m_currentPacket_is_controlpacket) {
					::InterlockedAdd((LONG*)&m_actualPayloadSizeSent, m_actualPayloadSize);
					m_actualPayloadSize = 0;

					lastFinishedStandard = timeGetTime(); // reset timeout
					m_bAccelerateUpload = false; // Safe until told otherwise
				}

				sent = 0;
			}
		}
	}

	if (onlyAllowedToSendControlPacket && (!controlpacket_queue.IsEmpty() || (sendbuffer != NULL && m_currentPacket_is_controlpacket))) {
		// enter control packet send queue
		// we might enter control packet queue several times for the same package,
		// but that costs very little overhead. Less overhead than trying to make sure
		// that we only enter the queue once.
		theApp.uploadBandwidthThrottler->QueueForSendingControlPacket(this, m_hasSent);
	}

	sendLocker.Unlock();
	return ret;
}

/**
 * Try to put queued up data on the socket with Overlapped methods.
 *
 * Control packets have higher priority, and will be sent first, if possible.
 *
 * @param maxNumberOfBytesToSend This is the maximum number of bytes that is allowed to be put on the socket
 *								 this call. The actual number of sent bytes will be returned from the method.
 *
 * @param onlyAllowedToSendControlPacket This call we only try to put control packets on the sockets.
 *
 * @return the actual number of bytes that were put on the socket.
 */
SocketSentBytes CEMSocket::SendOv(uint32 maxNumberOfBytesToSend, uint32 minFragSize, bool onlyAllowedToSendControlPacket)
{
	//EMTrace("CEMSocket::Send controlcount %i, standardcount %i, isbusy: %i", controlpacket_queue.GetCount(), standardpacket_queue.GetCount(), IsBusy());
	ASSERT(m_pProxyLayer == NULL);
	SocketSentBytes ret = {0, 0, true};
	bool bQueueControlPacket = false;
	bool bSignalSocketError = false;

	{
		CSingleLock lockSend(&sendLocker, TRUE);
		try {
			if (byConnected == EMS_CONNECTED && IsEncryptionLayerReady() && !IsBusyExtensiveCheck() && maxNumberOfBytesToSend > 0) {
				if (minFragSize < 1)
					minFragSize = 1;

				maxNumberOfBytesToSend = GetNextFragSize(maxNumberOfBytesToSend, minFragSize);
				lastCalledSend = timeGetTime();
				ASSERT(!m_bPendingSendOv && m_aBufferSend.IsEmpty());
				if (sendbuffer != NULL || !controlpacket_queue.IsEmpty() || (!standardpacket_queue.IsEmpty() && !onlyAllowedToSendControlPacket)) {
					// WSASend takes multiple buffers which is quite nice for our case, as we have to call send
					// only once regardless how many packets we want to ship without moving memory.
					// But before we can do this, collect all buffers we want to send in this call

					sint32 nBytesLeft = maxNumberOfBytesToSend;
					// first send the existing sendbuffer (already started packet)
					if (sendbuffer != NULL) {
						WSABUF pCurBuf = {};
						pCurBuf.len = min(sendblen - sent, (uint32)nBytesLeft);
						bool bBorrowedSendBuffer = false;
						std::unique_ptr<CHAR[]> copiedSlice;
						// Borrowing avoids an extra allocation/copy for a partial TCP
						// packet. The borrowed slice is safe only because sendbuffer is
						// kept alive until CleanUpOverlappedSendOperation observes the
						// overlapped send completion.
						if (CanBorrowOverlappedSendBufferSlice(sent, pCurBuf.len, sendblen)) {
							pCurBuf.buf = sendbuffer + sent;
							bBorrowedSendBuffer = true;
						} else {
							copiedSlice.reset(new CHAR[pCurBuf.len]);
							memcpy(copiedSlice.get(), sendbuffer + sent, pCurBuf.len);
							pCurBuf.buf = copiedSlice.get();
						}
						// WHY: CArray::Add can allocate. Keep copiedSlice owned
						// until the WSABUF is actually linked; for borrowed slices,
						// do not publish the borrow flag until cleanup can see the
						// linked entry.
						m_aBufferSend.Add(pCurBuf);
						if (!bBorrowedSendBuffer)
							copiedSlice.release();
						else
							m_bOverlappedSendBorrowsSendBuffer = true;
						sent += pCurBuf.len;
						nBytesLeft -= pCurBuf.len;
						if (sent == sendblen) { //finished the buffer
							if (bBorrowedSendBuffer) {
								// Do not append more WSABUF entries after borrowing a
								// just-finished sendbuffer; sendbuffer must remain the
								// stable owner until overlapped completion cleanup.
								nBytesLeft = 0;
							} else {
								delete[] sendbuffer;
								sendbuffer = NULL;
								sendblen = 0;
							}
						}
						ret.sentBytesStandardPackets += pCurBuf.len; // Sendbuffer is always a standard packet in this method
						lastFinishedStandard = timeGetTime();
						m_bAccelerateUpload = false;
						::InterlockedAdd((LONG*)&m_actualPayloadSizeSent, m_actualPayloadSize);
						m_actualPayloadSize = 0;
						if (m_currentPackageIsFromPartFile)
							::InterlockedAdd64((LONG64*)&m_numberOfSentBytesPartFile, pCurBuf.len);
						else
							::InterlockedAdd64((LONG64*)&m_numberOfSentBytesCompleteFile, pCurBuf.len);
					}

					// next send all control packets if there are any and we have bytes left
					while (!controlpacket_queue.IsEmpty() && nBytesLeft > 0) {
						// never split control packets, ignore going over the limit by a few bytes
						WSABUF pCurBuf = {};
						std::unique_ptr<Packet> curPacket(controlpacket_queue.RemoveHead());
						const uint32 nQueuedPacketBytes = curPacket->GetRealPacketSize();
						m_nQueuedControlPacketBytes = (m_nQueuedControlPacketBytes > nQueuedPacketBytes) ? m_nQueuedControlPacketBytes - nQueuedPacketBytes : 0;
						pCurBuf.len = curPacket->GetRealPacketSize();
						std::unique_ptr<char[]> detachedBuffer(curPacket->DetachPacket());
						pCurBuf.buf = detachedBuffer.get();
						curPacket.reset();
						// encrypting which cannot be done transparently in the base class
						CryptPrepareSendData((uchar*)pCurBuf.buf, pCurBuf.len);
						// WHY: after DetachPacket, this stack frame owns the raw
						// packet bytes until the WSABUF is linked into
						// m_aBufferSend. If Add throws, detachedBuffer releases
						// the bytes and the socket is failed below instead of
						// leaking a buffer from a removed queue entry.
						m_aBufferSend.Add(pCurBuf);
						detachedBuffer.release();
						nBytesLeft -= pCurBuf.len;
						ret.sentBytesControlPackets += pCurBuf.len;
					}

					// and now finally the standard packets if there are any, and we have bytes left, and we are allowed to
					if (!onlyAllowedToSendControlPacket)
						while (!standardpacket_queue.IsEmpty() && nBytesLeft > 0) {
							StandardPacketQueueEntry queueEntry = standardpacket_queue.RemoveHead();
							WSABUF pCurBuf = {};
							std::unique_ptr<Packet> curPacket(queueEntry.packet);
							const uint32 nQueuedPacketBytes = curPacket->GetRealPacketSize();
							m_nQueuedStandardPacketBytes = (m_nQueuedStandardPacketBytes > nQueuedPacketBytes) ? m_nQueuedStandardPacketBytes - nQueuedPacketBytes : 0;
							m_currentPackageIsFromPartFile = curPacket->IsFromPF();

							// can we send it right away or only a part of it?
							if (queueEntry.packet->GetRealPacketSize() <= (uint32)nBytesLeft) {
								// yay
								pCurBuf.len = curPacket->GetRealPacketSize();
								std::unique_ptr<char[]> detachedBuffer(curPacket->DetachPacket());
								pCurBuf.buf = detachedBuffer.get();
								CryptPrepareSendData((uchar*)pCurBuf.buf, pCurBuf.len);// encryption cannot be done transparently in the base class
								// WHY: m_aBufferSend becomes the owner boundary
								// for detached packet bytes. Keep the buffer in
								// a local owner until CArray::Add succeeds.
								m_aBufferSend.Add(pCurBuf);
								detachedBuffer.release();
								curPacket.reset();
								::InterlockedAdd((LONG*)&m_actualPayloadSizeSent, queueEntry.actualPayloadSize);
								lastFinishedStandard = timeGetTime();
								m_bAccelerateUpload = false;
							} else {	// aww, well first stuff everything into the sendbuffer and then send what we can of it
								ASSERT(sendbuffer == NULL);
								const uint32 nPacketBytes = curPacket->GetRealPacketSize();
								std::unique_ptr<char[]> detachedPacket(curPacket->DetachPacket());
								CryptPrepareSendData((uchar*)detachedPacket.get(), nPacketBytes); //  encryption cannot be done transparently in the base class
								pCurBuf.len = min(nPacketBytes, (uint32)nBytesLeft);
								bool bBorrowedSendBuffer = false;
								std::unique_ptr<CHAR[]> copiedSlice;
								// See the sendbuffer slice comment above. This avoids
								// copying the first overlapped fragment of a standard
								// packet while preserving the packet buffer until the
								// overlapped operation is cleaned up.
								if (CanBorrowOverlappedSendBufferSlice(0, pCurBuf.len, nPacketBytes)) {
									pCurBuf.buf = detachedPacket.get();
									bBorrowedSendBuffer = true;
								} else {
									copiedSlice.reset(new CHAR[pCurBuf.len]);
									memcpy(copiedSlice.get(), detachedPacket.get(), pCurBuf.len);
									pCurBuf.buf = copiedSlice.get();
								}
								// WHY: partial standard packets publish a new
								// sendbuffer owner only after the first WSABUF is
								// linked. If Add throws, detachedPacket/copy
								// clean up without leaving a half-published
								// sendbuffer behind a locked socket.
								m_aBufferSend.Add(pCurBuf);
								if (!bBorrowedSendBuffer)
									copiedSlice.release();
								else
									m_bOverlappedSendBorrowsSendBuffer = true;
								sendbuffer = detachedPacket.release();
								sendblen = nPacketBytes;
								sent = pCurBuf.len;
								ASSERT(sent < sendblen);
								m_actualPayloadSize = queueEntry.actualPayloadSize;
								m_currentPacket_is_controlpacket = false;
								curPacket.reset();
							}
							nBytesLeft -= pCurBuf.len;
							ret.sentBytesStandardPackets += pCurBuf.len;
							if (m_currentPackageIsFromPartFile)
								::InterlockedAdd64((LONG64*)&m_numberOfSentBytesPartFile, pCurBuf.len);
							else
								::InterlockedAdd64((LONG64*)&m_numberOfSentBytesCompleteFile, pCurBuf.len);
						}

					if (m_aBufferSend.GetCount() > 0) {
						// all right, prepare to send our collected buffers
						memset(&m_PendingSendOperation, 0, sizeof WSAOVERLAPPED);
						m_PendingSendOperation.hEvent = theApp.uploadBandwidthThrottler->GetSocketAvailableEvent();
						m_bPendingSendOv = true;
						if (CEncryptedStreamSocket::SendOv(m_aBufferSend, &m_PendingSendOperation) == 0)
							CleanUpOverlappedSendOperation(false);
						else {
							int nError = WSAGetLastError();
							if (nError != WSA_IO_PENDING) {
								ret.success = false;
								theApp.QueueDebugLogLineEx(ERROR, _T("WSASend() Error: %u, %s"), nError, (LPCTSTR)GetErrorMessage(nError));
								CleanUpOverlappedSendOperation(false);
							}
						}
					}
				}
			}

			if (onlyAllowedToSendControlPacket && !controlpacket_queue.IsEmpty()) {
				// enter control packet send queue
				// we might enter control packet queue several times for the same package,
				// but that costs very little overhead. Less overhead than trying to make sure
				// that we only enter the queue once.
				bQueueControlPacket = true;
			}
		} catch (CException *pException) {
			if (pException != NULL)
				pException->Delete();
			ret.success = false;
			bSignalSocketError = true;
			m_bPendingSendOv = false;
			DiscardPreparedOverlappedSendBuffers();
			// WHY: preparation failed before WSASend accepted ownership. Drop
			// any partially-published sendbuffer while the socket lock is still
			// held; the caller below closes the socket so stream semantics are
			// retried through normal peer teardown instead of resuming mid-frame.
			delete[] sendbuffer;
			sendbuffer = NULL;
			sendblen = 0;
			sent = 0;
		} catch (const std::bad_alloc&) {
			ret.success = false;
			bSignalSocketError = true;
			m_bPendingSendOv = false;
			DiscardPreparedOverlappedSendBuffers();
			delete[] sendbuffer;
			sendbuffer = NULL;
			sendblen = 0;
			sent = 0;
		}
	}

	if (bSignalSocketError)
		OnError(WSAENOBUFS);
	else if (bQueueControlPacket)
		theApp.uploadBandwidthThrottler->QueueForSendingControlPacket(this, m_hasSent);

	return ret;
}

uint32 CEMSocket::GetNextFragSize(uint32 current, uint32 minFragSize)
{
	return (min(_I32_MAX, current + minFragSize - 1) / minFragSize) * minFragSize;
}

/**
 * Decides the (minimum) amount the socket needs to send to prevent timeout.
 *
 * @author SlugFiller
 */
uint32 CEMSocket::GetNeededBytes()
{
	sendLocker.Lock();
	if (byConnected == EMS_DISCONNECTED) {
		sendLocker.Unlock();
		return 0;
	}

	bool isControlpacket = (sendbuffer == NULL) || m_currentPacket_is_controlpacket;
	if (isControlpacket && standardpacket_queue.IsEmpty()) {
		// No standard packet to send. Even if data needs to be sent to prevent timeout, there's nothing to send.
		sendLocker.Unlock();
		return 0;
	}
	if (!isControlpacket && !controlpacket_queue.IsEmpty())
		m_bAccelerateUpload = true;	// We might be trying to send a block request, accelerate packet

	DWORD sendgap = timeGetTime();
	DWORD timeleft = sendgap - lastFinishedStandard;
	sendgap -= lastCalledSend;
	DWORD timetotal = SEC2MS(m_bAccelerateUpload ? 45 : 90);
	uint64 sizeleft, sizetotal;
	if (!isControlpacket) {
		sizeleft = sendblen - sent;
		sizetotal = sendblen;
	} else
		sizeleft = sizetotal = standardpacket_queue.GetHead().packet->GetRealPacketSize();
	sendLocker.Unlock();

	if (timeleft >= timetotal)
		return (uint32)sizeleft;
	timeleft = timetotal - timeleft;
	// don't use 'GetTimeOut' here in case the timeout value is high,
	if (timeleft * sizetotal >= timetotal * sizeleft) {
		// Don't let the socket itself to time out - Might happen when switching from spread (non-focus) slot to trickle slot
		return static_cast<uint32>(sendgap >= SEC2MS(20));
	}
	uint64 decval = timeleft * sizetotal / timetotal;
	if (!decval)
		return (uint32)sizeleft;
	if (decval < sizeleft)
		return (uint32)(sizeleft - decval + 1);	// Round up
	return 1;
}

// pach2:
// written this overriden Receive to handle transparently FIN notifications coming from calls to recv()
// This was maybe(??) the cause of a lot of socket error, notably after a brutal close from peer
// also added trace so that we can debug after the fact.
int CEMSocket::Receive(void *lpBuf, int nBufLen, int nFlags)
{
	//EMTrace("CEMSocket::Receive on %u, maxSize=%d", (SOCKET)this, nBufLen);
	int recvRetCode = CEncryptedStreamSocket::Receive(lpBuf, nBufLen, nFlags); // deadlake PROXYSUPPORT - changed to AsyncSocketEx
	switch (recvRetCode) {
	case 0:
		if (GetRealReceivedBytes() <= 0) { // we received data but it was for the underlying encryption layer - all fine
			//EMTrace("CEMSocket::##Received FIN on %u, maxSize=%d", (SOCKET)this, nBufLen);
			// FIN received on socket // Connection is being closed by peer
			//ASSERT (false);
			if (!AsyncSelect(FD_CLOSE | FD_WRITE)) { // no more READ notifications...
				//int waserr = CAsyncSocket::GetLastError(); // oops, AsyncSelect failed !!!
				ASSERT(0);
			}
		}
		break;
	case SOCKET_ERROR:
		char *p = NULL;
		switch (CAsyncSocket::GetLastError()) {
		case WSANOTINITIALISED:
			ASSERT(0);
			EMTrace("%sA successful AfxSocketInit must occur before using this API.");
			break;
		case WSAENETDOWN:
			ASSERT(true);
			p = "%sThe socket %u received a net down error";
			break;
		case WSAENOTCONN:
			p = "%sThe socket %u is not connected";
			break;
		case WSAEINPROGRESS:	// A blocking Windows Sockets operation is in progress.
			p = "%sThe socket %u is blocked";
			break;
		case WSAEWOULDBLOCK:	// The socket is marked as nonblocking and the Receive operation would block.
			p = "%sThe socket %u would block";
			break;
		case WSAENOTSOCK:		// The descriptor is not a socket.
			p = "%sThe descriptor %u is not a socket (may have been closed or never created)";
			break;
		case WSAEOPNOTSUPP:		// MSG_OOB was specified, but the socket is not of type SOCK_STREAM.
			break;
		case WSAESHUTDOWN:		// The socket has been shut down; it is impossible to call Receive on a socket after ShutDown(0) or ShutDown(2) has been invoked.
			p = "%sThe socket %u has been shut down";
			break;
		case WSAEMSGSIZE:		// The datagram was too large to fit into the specified buffer and was truncated.
			p = "%sThe datagram was too large to fit and was truncated (socket %u)";
			break;
		case WSAEINVAL:			// The socket has not been bound with Bind.
		case WSAECONNABORTED:	// The virtual circuit was aborted due to timeout or other failure.
		case WSAECONNRESET:		// The virtual circuit was reset by the remote side.
			p = "%sThe socket %u has not been bound";
			break;
		default:
			EMTrace("CEMSocket::OnReceive: Unexpected socket error %x on socket %u", CAsyncSocket::GetLastError(), (SOCKET)this);
		}
		if (p)
			EMTrace(p, "CEMSocket::OnReceive: ", (SOCKET)this);
//		break;
//	default:
//		EMTrace("CEMSocket::OnReceive on %u, receivedSize=%d", (SOCKET)this, recvRetCode);
	}
	return recvRetCode;
}

void CEMSocket::RemoveAllLayers()
{
	CEncryptedStreamSocket::RemoveAllLayers();
	delete m_pProxyLayer;
	m_pProxyLayer = NULL;
}

int CEMSocket::OnLayerCallback(std::vector<t_callbackMsg> &callbacks)
{
	for (std::vector<t_callbackMsg>::const_iterator iter = callbacks.begin(); iter != callbacks.end(); ++iter) {
		if (iter->nType == LAYERCALLBACK_LAYERSPECIFIC) {
			if (iter->pLayer == m_pProxyLayer) {
				m_strLastProxyError = GetProxyError((int)iter->wParam);
				switch (iter->wParam) {
				case PROXYERROR_NOCONN:
					// We failed to connect to the proxy.
					m_bProxyConnectFailed = true;
					[[fallthrough]];
				case PROXYERROR_REQUESTFAILED:
					// We are connected to the proxy but it failed to connect to the peer.
					if (thePrefs.GetVerbose() && iter->str && iter->str[0] != '\0')
						m_strLastProxyError.AppendFormat(_T(" - %hs"), iter->str);
				}
				LogWarning(LOG_DEFAULT, _T("Proxy Error: %s"), (LPCTSTR)m_strLastProxyError);
			}
		}
		delete[] iter->str;
	}
	return 0;
}

/**
 * Removes all packets from the standard queue that don't have to be sent for the socket to be able to send a control packet.
 *
 * Before a socket can send a new packet, the current packet has to be finished. If the current packet is part of
 * a split packet, then all parts of that split packet must be sent before the socket can send a control packet.
 *
 * This method keeps in standard queue only those packets that must be sent (rest of split packet), and removes everything
 * after it. The method doesn't touch the control packet queue.
 */
void CEMSocket::TruncateQueues()
{
	sendLocker.Lock();

	// Clear the standard queue totally
	// Please note! There may still be a standardpacket in the sendbuffer variable!
	while (!standardpacket_queue.IsEmpty())
		delete standardpacket_queue.RemoveHead().packet;
	m_nQueuedStandardPacketBytes = 0;

	sendLocker.Unlock();
}

#ifdef _DEBUG
void CEMSocket::AssertValid() const
{
	CEncryptedStreamSocket::AssertValid();

	const_cast<CEMSocket*>(this)->sendLocker.Lock();

	ASSERT(byConnected == EMS_DISCONNECTED || byConnected == EMS_NOTCONNECTED || byConnected == EMS_CONNECTED);
	CHECK_BOOL(m_bProxyConnectFailed);
	CHECK_PTR(m_pProxyLayer);
	(void)downloadLimit;
	CHECK_BOOL(downloadLimitEnable);
	CHECK_BOOL(pendingOnReceive);
	//char pendingHeader[PACKET_HEADER_SIZE];
	pendingHeaderSize;
	CHECK_PTR(pendingPacket);
	(void)pendingPacketSize;
	CHECK_ARR(sendbuffer, sendblen);
	(void)sent;
	controlpacket_queue.AssertValid();
	standardpacket_queue.AssertValid();
	CHECK_BOOL(m_currentPacket_is_controlpacket);
	//(void)sendLocker;
	(void)m_numberOfSentBytesCompleteFile;
	(void)m_numberOfSentBytesPartFile;
	//(void)m_numberOfSentBytesControlPacket;
	CHECK_BOOL(m_currentPackageIsFromPartFile);
	(void)lastCalledSend;
	(void)m_actualPayloadSize;
	(void)m_actualPayloadSizeSent;

	const_cast<CEMSocket*>(this)->sendLocker.Unlock();
}

void CEMSocket::Dump(CDumpContext &dc) const
{
	CEncryptedStreamSocket::Dump(dc);
}
#endif

void CEMSocket::DataReceived(const BYTE*, UINT)
{
	ASSERT(0);
}

CString CEMSocket::GetFullErrorMessage(DWORD dwError) const
{
	CString strError;

	// Proxy error
	if (!GetLastProxyError().IsEmpty()) {
		strError = GetLastProxyError();
		// If we had a proxy error and the socket error is WSAECONNABORTED, we just 'aborted'
		// the TCP connection ourself - no need to add that self-created error too.
		if (dwError == WSAECONNABORTED)
			return strError;
	}
	// Winsock error
	if (dwError) {
		if (!strError.IsEmpty())
			strError += _T(": ");
		strError += GetErrorMessage(dwError, 1);
	}

	return strError;
}

// increases the send buffer to a bigger size
bool CEMSocket::UseBigSendBuffer()
{
	if (!m_bUseBigSendBuffers) {
		int val = static_cast<int>(kBroadbandTcpUploadSendBufferBytes);
		int oldval;
		int vallen = sizeof oldval;
		if (GetSockOpt(SO_SNDBUF, &oldval, &vallen))
			if (static_cast<int>(kBroadbandTcpUploadSendBufferBytes) > oldval) {
				SetSockOpt(SO_SNDBUF, &val, sizeof val);
				vallen = sizeof val;
				m_bUseBigSendBuffers = (GetSockOpt(SO_SNDBUF, &val, &vallen) && val > oldval);
#if defined(_DEBUG) || defined(_DEVBUILD)
				if (m_bUseBigSendBuffers && val >= static_cast<int>(kBroadbandTcpUploadSendBufferBytes))
					theApp.QueueDebugLogLine(false, _T("Increased Sendbuffer for uploading socket from %u KiB to %u KiB"), oldval / 1024, val / 1024);
				else if (m_bUseBigSendBuffers)
					theApp.QueueDebugLogLine(false, _T("Upload send buffer requested %u KiB, Windows applied %u KiB"), kBroadbandTcpUploadSendBufferBytes / 1024, val / 1024);
				else
					theApp.QueueDebugLogLine(false, _T("Failed to increase Sendbuffer for uploading socket, stays at %u KiB"), oldval / 1024);
#endif
			} else
				m_bUseBigSendBuffers = true;
	}
	return m_bUseBigSendBuffers;
}

bool CEMSocket::IsBusyExtensiveCheck()
{
	if (!m_bUseOverlappedSend)
		return m_bBusy;

	CSingleLock lockSend(&sendLocker, TRUE);
	if (!m_bPendingSendOv)
		return false;
	DWORD dwTransferred, dwFlags;
	if (WSAGetOverlappedResult(GetSocketHandle(), &m_PendingSendOperation, &dwTransferred, FALSE, &dwFlags)) {
		CleanUpOverlappedSendOperation(false);
		OnSend(0);
		return false;
	}
	int nError = WSAGetLastError();
	if (nError == WSA_IO_INCOMPLETE)
		return true;
	CleanUpOverlappedSendOperation(true);
	theApp.QueueDebugLogLineEx(LOG_ERROR, _T("WSAGetOverlappedResult return error: %s"), (LPCTSTR)GetErrorMessage(nError));
	return false;
}

// won't always deliver the proper result (sometimes reports busy even if it isn't any more
// and thread related errors) but doesn't need locks or function calls
bool CEMSocket::IsBusyQuickCheck() const
{
	return m_bUseOverlappedSend ? m_bPendingSendOv : m_bBusy;
}

//sendLock must be locked by the caller!
void CEMSocket::DiscardPreparedOverlappedSendBuffers()
{
	// WHY: SendOv builds m_aBufferSend before WSASend accepts ownership. If
	// allocation fails during that staging phase, m_bPendingSendOv is still
	// false, so CleanUpOverlappedSendOperation intentionally does nothing. This
	// helper releases only the staged WSABUF storage and leaves sendbuffer
	// lifetime decisions to the failing caller.
	for (INT_PTR i = m_aBufferSend.GetCount(); --i >= 0;) {
		if (!IsBorrowedOverlappedSendBufferSlice(m_aBufferSend[i].buf, sendbuffer, sendblen))
			delete[] m_aBufferSend[i].buf;
	}
	m_aBufferSend.RemoveAll();
	m_bOverlappedSendBorrowsSendBuffer = false;
}

//sendLock must be locked by the caller!
void CEMSocket::CleanUpOverlappedSendOperation(bool bCancel)
{
	if (m_bPendingSendOv) {
		m_bPendingSendOv = false;
		if (bCancel && CancelIo((HANDLE)GetSocketHandle()))
			for (int i = 5; --i >= 0;) { //use counter and sleep(), because this may loop forever
				DWORD dwTransferred, dwFlags;
				if (WSAGetOverlappedResult(GetSocketHandle(), &m_PendingSendOperation, &dwTransferred, FALSE, &dwFlags))
					break;
				if (WSAGetLastError() != WSA_IO_INCOMPLETE)
					break;
				::Sleep(20);
			};

		for (INT_PTR i = m_aBufferSend.GetCount(); --i >= 0;) {
			if (!IsBorrowedOverlappedSendBufferSlice(m_aBufferSend[i].buf, sendbuffer, sendblen))
				delete[] m_aBufferSend[i].buf;
		}
		m_aBufferSend.RemoveAll();
		if (m_bOverlappedSendBorrowsSendBuffer && sent == sendblen) {
			delete[] sendbuffer;
			sendbuffer = NULL;
			sendblen = 0;
			sent = 0;
		}
		m_bOverlappedSendBorrowsSendBuffer = false;
	}
}

bool CEMSocket::HasQueues(bool bOnlyStandardPackets) const
{
	// not trustworthy threaded? but it's OK if we don't get the correct result now and then
	return sendbuffer != NULL || !standardpacket_queue.IsEmpty() || (!controlpacket_queue.IsEmpty() && !bOnlyStandardPackets);
}

bool CEMSocket::IsLowOnFileDataQueued(uint32 nMinFilePayloadBytes) const
{
	// check we have at least nMinFilePayloadBytes Payload data in our standardqueue
	for (POSITION pos = standardpacket_queue.GetHeadPosition(); pos != NULL;) {
		uint32 actualsize = standardpacket_queue.GetNext(pos).actualPayloadSize;
		if (actualsize > nMinFilePayloadBytes)
			return false;
		nMinFilePayloadBytes -= actualsize;
	}
	return true;
}
