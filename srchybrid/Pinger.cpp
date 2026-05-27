/*---------------------------------------------------------------------
*
*  Some code in this file has been copied from a ping demo that was
*  created by Bob Quinn, 1997          http://www.sockets.com
*
* As some general documentation about how the ping is implemented,
* here is the description Bob Quinn wrote about the ping demo.
*
* <--- Cut --->
*
* Description:
*  This is a ping program that uses Microsoft's ICMP.DLL functions
*  for access to Internet Control Message Protocol.  This is capable
*  of doing "ping or "traceroute", although beware that Microsoft
*  discourages the use of these APIs.
*
*  Tested with MSVC 5 compile with "cl ms_icmp.c /link ws2_32.lib"
*  from the console (if you've run VCVARS32.BAT batch file that
*  ships with MSVC to set the proper environment variables)
*
* NOTES:
* - With both "Don't Fragment" and "Router Alert" set, the
*    IP don't fragment bit is set, but not the router alert option.
* - The ICMP.DLL calls are not redirected to the TCP/IP service
*    provider (what's interesting about this is that the setsockopt()
*    IP_OPTION flag can't do Router Alert, but this API can... hmmm.
* - Although the IcmpSendEcho() docs say it can return multiple
*    responses, if I receive multiple responses (e.g. sending to
*    a limited or subnet broadcast address) IcmpSendEcho() only
*    returns one.
* - using winsock.h  WSOCK32.LIB and version 1.1 works as well as
*    using winsock2.h WS2_32.LIB  and version 2.2
*
* Some Background:
*
* The standard Berkeley Sockets SOCK_RAW socket type, is normally used
* to create ping (echo request/reply), and sometimes traceroute applications
* (the original traceroute application from Van Jacobson used UDP, rather
* than ICMP).
*
* Microsoft has their own API for an ICMP.DLL that their ping and tracert
* applications use (by the way, they are both non-GUI text-based console
* applications. This is a proprietary API, and all function calls that
* involve network functions operate in blocking mode. They still include
* it with WinSock 2 implementations.
*
* There is little documentation available (I first found it in the Win32
* SDK in \MSTOOLS\ICMP, and it exists on the MS Developers' Network
* CD-ROM now, also). Microsoft disclaims this API about as strongly as
* possible.  The README.TXT that accompanies it says:
*
* [DISCLAIMER]
*
* We have had requests in the past to expose the functions exported from
* icmp.dll. The files in this directory are provided for your convenience
* in building applications which make use of ICMPSendEcho(). Notice that
* the functions in icmp.dll are not considered part of the Win32 API and
* will not be supported in future releases. Once we have a more complete
* solution in the operating system, this DLL, and the functions it exports,
* will be dropped.
*
* [DOCUMENTATION]
*
* The ICMPSendEcho() function sends an ICMP echo request to the specified
* destination IP address and returns any replies received within the timeout
* specified. The API is synchronous, requiring the process to spawn a thread
* before calling the API to avoid blocking. An open IcmpHandle is required
* for the request to complete. IcmpCreateFile() and IcmpCloseHandle()
* functions are used to create and destroy the context handle.
*
* <--- End cut --->
*/

/*---------------------------------------------------------------------
* Reworked (tag UDPing) to use UDP packets and ICMP_TTL_EXPIRE responses
* to avoid problems with some ISPs disallowing or limiting ICMP_ECHO
* (ping) requests. This has downside on NT based systems - raw sockets
* can be used only by administrative users; thereby current code has to
* implement both approaches.
*
* Example code and ideas used from (credits go to articles authors):
* https://tangentsoft.net/wskfaq/examples/rawping.html
* http://www.cs.concordia.ca/~teaching/comp445/labs/webpage/ch16.htm
* http://www.networksorcery.com/enp/protocol/icmp.htm
*
* NB! Take care about winsock libraries - look below for IP_TTL!
*
* Partial copyright (c) 2003 by DonQ
*/

#include "stdafx.h"
#include <icmpapi.h>
#include "emule.h"
#include "TimeTick.h"
#include "Pinger.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "BindInterfaceSocketSeams.h"
#include "IPv4AddressSeams.h"
#include "Log.h"
#include "opcodes.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define DEFAULT_LEN	0
#define BUFSIZE		32 //should be >= DEFAULT_LEN + 8
#define TIMEOUT		SEC2MS(3)

namespace
{
	unsigned long GetPingerBindAddress()
	{
		if (thePrefs.GetBindAddr() == NULL)
			return INADDR_ANY;
		uint32_t ulBindAddr = 0;
		return IPv4AddressSeams::TryParseIPv4Address(thePrefs.GetBindAddr(), ulBindAddr) ? ulBindAddr : INADDR_ANY;
	}

	bool ShouldBlockIcmpApiForExplicitBind()
	{
		return !thePrefs.GetActiveBindInterface().IsEmpty()
			&& thePrefs.GetActiveBindAddressResolveResult() == BARR_Resolved;
	}
}

Pinger::Pinger()
	: hICMP(INVALID_HANDLE_VALUE)
	, us(INVALID_SOCKET)
	, is(INVALID_SOCKET)
	, udpStarted()
{
	// udp start
	sockaddr_in sa;		// for UDP and raw sockets

	// ICMP must accept all responses
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = GetPingerBindAddress();
	sa.sin_port = 0;

	// attempt to initialize raw ICMP socket (raw sockets require an Administrator)
	is = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (is != INVALID_SOCKET) {
		int nBindInterfaceError = 0;
		if (!BindInterfaceSocketSeams::ApplyIpv4UnicastInterfaceOption(is
			, AF_INET
			, !thePrefs.GetActiveBindInterface().IsEmpty()
			, thePrefs.GetActiveBindAddressResolveResult() == BARR_Resolved
			, thePrefs.GetActiveBindInterfaceIndex()
			, &nBindInterfaceError)) {
			DebugLogError(_T("Pinger raw socket bind interface enforcement failed: IP_UNICAST_IF could not be applied to %s (ifIndex=%lu, error=%d)")
				, (LPCTSTR)thePrefs.GetActiveBindInterfaceName()
				, thePrefs.GetActiveBindInterfaceIndex()
				, nBindInterfaceError);
			closesocket(is);
			is = INVALID_SOCKET;
		} else if (bind(is, (sockaddr*)&sa, sizeof sa) == SOCKET_ERROR) {
			//WSAGetLastError(); - never using the value
			closesocket(is);	// ignore return value - error close anyway
		} else {
			// attempt to initialize ordinary UDP socket - why should this fail???
			us = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (us == INVALID_SOCKET)
				closesocket(is);		// ignore return value - we need to close it anyway!
			else if (!BindInterfaceSocketSeams::ApplyIpv4UnicastInterfaceOption(us
				, AF_INET
				, !thePrefs.GetActiveBindInterface().IsEmpty()
				, thePrefs.GetActiveBindAddressResolveResult() == BARR_Resolved
				, thePrefs.GetActiveBindInterfaceIndex()
				, &nBindInterfaceError)) {
				DebugLogError(_T("Pinger UDP socket bind interface enforcement failed: IP_UNICAST_IF could not be applied to %s (ifIndex=%lu, error=%d)")
					, (LPCTSTR)thePrefs.GetActiveBindInterfaceName()
					, thePrefs.GetActiveBindInterfaceIndex()
					, nBindInterfaceError);
				closesocket(us);
				closesocket(is);
				us = INVALID_SOCKET;
			} else if (bind(us, (sockaddr*)&sa, sizeof sa) == SOCKET_ERROR) {
				closesocket(us);		// ignore return value - error close anyway
				closesocket(is);		// ignore return value - error close anyway
				us = INVALID_SOCKET;
			} else
				udpStarted = true;
		}
	}
	// udp end

	// Open the ping service
	hICMP = IcmpCreateFile();
	if (hICMP == INVALID_HANDLE_VALUE) {
		DWORD nErr = ::GetLastError();
		CString sErr;
		sErr.Format(_T("IcmpCreateFile() failed, err: %lu "), nErr);
		PIcmpErr(sErr, nErr);
	} else {
		stIPInfo.Tos = 0; // Init IPInfo structure
		stIPInfo.Flags = 0;
		stIPInfo.OptionsSize = 0;
		stIPInfo.OptionsData = NULL;
	}
}

Pinger::~Pinger()
{
// UDPing reworked cleanup -->
	if (udpStarted) {
		closesocket(is);		// close raw ICMP socket
		closesocket(us);		// close UDP socket
	}
// UDPing reworked cleanup end <--

	// Close the ICMP handle
	if (hICMP != INVALID_HANDLE_VALUE && !IcmpCloseHandle(hICMP)) {
		DWORD nErr = ::GetLastError();
		CString sErr;
		sErr.Format(_T("Closing ICMP handle failed, err: %lu "), nErr);
		PIcmpErr(sErr, nErr);
	}
	// Shut down...
}

PingStatus Pinger::Ping(uint32 lAddr, uint32 ttl, bool doLog, bool useUdp)
{
	if (useUdp && udpStarted)
		return PingUDP(lAddr, ttl, doLog);

	if (ShouldBlockIcmpApiForExplicitBind()) {
		// WHY: IcmpSendEcho has no socket handle, so it cannot be bound through
		// BindAddr/IP_UNICAST_IF. Falling back to it after an explicit resolved
		// interface bind would send diagnostic traffic on an arbitrary route.
		if (doLog)
			theApp.QueueDebugLogLine(false
				, _T("Pinger: skipped ICMP API fallback because bind interface %s (ifIndex=%lu) is active")
				, (LPCTSTR)thePrefs.GetActiveBindInterfaceName()
				, thePrefs.GetActiveBindInterfaceIndex());
		PingStatus returnValue = {};
		returnValue.fDelay = TIMEOUT;
		returnValue.status = IP_GENERAL_FAILURE;
		returnValue.error = ERROR_NOT_SUPPORTED;
		returnValue.bSuccess = false;
		return returnValue;
	}

	return PingICMP(lAddr, ttl, doLog);
}

PingStatus Pinger::PingUDP(uint32 lAddr, DWORD ttl, bool doLog)
{
	// UDPing reworked ping sequence -->
	sockaddr_in sa;
	int nAddrLen = sizeof(sockaddr_in);
	char bufICMP[1500]; // allow full MTU

	// clear ICMP socket before sending UDP - not best solution, but may be needed to exclude late responses etc
	u_long bytes2read = 0;
	while (!ioctlsocket(is, FIONREAD, &bytes2read) && bytes2read) {
		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = INADDR_ANY;
		sa.sin_port = 0;

		recvfrom(is, (LPSTR)bufICMP, 1500, 0, (sockaddr*)&sa, &nAddrLen);
	}

	// set TTL value for UDP packet - should succeed with winsock 2
	// NB! take care about IP_TTL value - it's redefined in Ws2tcpip.h!
	// TODO: solve next problem correctly:
	// eMule is linking sockets functions using wsock32.lib (IP_TTL=7)
	// to use IP_TTL define, we must enforce linker to bind this function
	// to ws2_32.lib (IP_TTL=4) (linker options: ignore wsock32.lib)
	int nRet = setsockopt(us, IPPROTO_IP, IP_TTL, (const char*)&ttl, sizeof ttl);
	if (nRet == SOCKET_ERROR) {
		PingStatus returnValue;
		returnValue.fDelay = TIMEOUT;
		returnValue.bSuccess = false;
		returnValue.error = WSAGetLastError();
		return returnValue;
	}

	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = lAddr;
	sa.sin_port = htons(UDP_PORT);

	// send lone UDP packet with minimal content (0 bytes is allowed, but no data will be sent in that case)
	nRet = sendto(us, (const char*)&ttl, 4, 0, (sockaddr*)&sa, sizeof sa);  // send four bytes - TTL :)
	if (nRet == SOCKET_ERROR) {
		PingStatus returnValue;
		returnValue.error = WSAGetLastError();
		returnValue.bSuccess = false;
		return returnValue;
	}

	IPHeader *reply = reinterpret_cast<IPHeader*>(bufICMP);

	bytes2read = 0;
	int timeoutOpt = TIMEOUT;
	bool noRcvTimeOut = false;
	nRet = setsockopt(is, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeoutOpt, sizeof timeoutOpt);
	if (nRet == SOCKET_ERROR)
		noRcvTimeOut = true;

	float usResTime = 0.0f;
	CTimeTick c_time;
	c_time.Start();
	while ((usResTime += c_time.Tick()) < TIMEOUT) {
		if (noRcvTimeOut) {
			nRet = ioctlsocket(is, FIONREAD, &bytes2read);
			if (nRet != 0) {
				PingStatus returnValue;
				returnValue.fDelay = TIMEOUT;
				returnValue.error = WSAGetLastError();
				returnValue.bSuccess = false;
				return returnValue;
			}
			if (bytes2read <= 0) {
				::Sleep(1);		// share time with other threads
				continue;
			}
		}
		// read and filter incoming ICMP
		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = INADDR_ANY;
		sa.sin_port = 0;
		nRet = recvfrom(is, bufICMP, 1500, 0, (sockaddr*)&sa, &nAddrLen);

		usResTime += c_time.Tick();
		if (nRet == SOCKET_ERROR) {
			PingStatus returnValue;
			returnValue.fDelay = TIMEOUT;
			returnValue.error = WSAGetLastError();
			returnValue.bSuccess = false;
			return returnValue;
		}

		unsigned short header_len = reply->h_len * 4;
		ICMPHeader *icmphdr = reinterpret_cast<ICMPHeader*>(bufICMP + header_len);

		if ((icmphdr->type == ICMP_T_TTL_EXPIRE || icmphdr->type == ICMP_T_DEST_UNREACH)
			&& icmphdr->UDP.dest_port == htons(UDP_PORT)
			&& icmphdr->hdrsent.dest_ip == lAddr)
		{
			PingStatus returnValue;
			returnValue.fDelay = usResTime;
			returnValue.destinationAddress = reply->source_ip;
			if (icmphdr->type == ICMP_T_TTL_EXPIRE) {
				returnValue.status = IP_TTL_EXPIRED_TRANSIT;
				returnValue.ttl = (UCHAR)ttl;
			} else {
				returnValue.status = IP_DEST_HOST_UNREACHABLE;
				returnValue.ttl = 64 - (reply->ttl & 0x3f); //63
			}
			returnValue.error = 0;
			returnValue.bSuccess = true;

			if (doLog)
				theApp.QueueDebugLogLine(false
					, _T("Reply (UDP-pinger) from %s: bytes=%d time=%3.2fms TTL=%hhu")
					, (LPCTSTR)ipstr(reply->source_ip)
					, nRet
					, usResTime
					, returnValue.ttl);

			return returnValue;
		}
		// verbose log filtered packets info (not seen yet...)

		if (doLog)
			theApp.QueueDebugLogLine(false
				, _T("Filtered reply (UDP-pinger) from %s: bytes=%d time=%3.2fms TTL=%i type=%i")
				, (LPCTSTR)ipstr(reply->source_ip)
				, nRet
				, usResTime
				, 64 - (reply->ttl & 0x3f) //63
				, icmphdr->type);
	}
	// UDPing reworked ping sequence end <--

	PingStatus returnValue;
	returnValue.fDelay = TIMEOUT;
	returnValue.error = IP_REQ_TIMED_OUT;
	returnValue.bSuccess = false;

	return returnValue;
}

PingStatus Pinger::PingICMP(uint32 lAddr, DWORD ttl, bool doLog)
{
	if (hICMP == INVALID_HANDLE_VALUE) {
		// WHY: IcmpCreateFile can fail on locked-down hosts. Passing the
		// sentinel handle to IcmpSendEcho just turns that setup failure into a
		// later API misuse; report the unavailable backend at the call boundary.
		if (doLog)
			theApp.QueueDebugLogLine(false, _T("Pinger: skipped ICMP API ping because IcmpCreateFile did not return a valid handle"));
		PingStatus returnValue = {};
		returnValue.fDelay = TIMEOUT;
		returnValue.destinationAddress = lAddr;
		returnValue.status = IP_GENERAL_FAILURE;
		returnValue.error = ERROR_INVALID_HANDLE;
		returnValue.ttl = (UCHAR)ttl;
		returnValue.bSuccess = false;
		return returnValue;
	}

	char achRepData[sizeof(ICMP_ECHO_REPLY) + BUFSIZE];

	// Address is assumed to be OK
	IN_ADDR stDestAddr;
	stDestAddr.s_addr = lAddr;
	stIPInfo.Ttl = (UCHAR)ttl;

	CTimeTick c_time;
	c_time.Start();
	// Send the ICMP Echo Request and read the Reply
	DWORD dwReplyCount = IcmpSendEcho(hICMP
		, stDestAddr.s_addr
		, NULL // data buffer
		, DEFAULT_LEN // DataLen, length of data buffer. Currently 0
		, &stIPInfo
		, achRepData
		, sizeof achRepData
		, TIMEOUT);
	float usResTime = c_time.Tick();

	PingStatus returnValue;
	if (dwReplyCount > 0) {
		const ICMP_ECHO_REPLY &Reply = *reinterpret_cast<PICMP_ECHO_REPLY>(achRepData);
		long pingTime = Reply.RoundTripTime;
		const float fPingTime = static_cast<float>(pingTime);
		const bool bUseMeasuredElapsed =
			c_time.isPerformanceCounter()
			&& (pingTime <= 20 || pingTime % 10 == 0)
			&& (fPingTime + 10.0f > usResTime && usResTime + 10.0f > fPingTime);

		returnValue.fDelay = bUseMeasuredElapsed ? usResTime : fPingTime;
		returnValue.destinationAddress = Reply.Address;
		returnValue.status = Reply.Status;
		returnValue.error = 0;
		returnValue.ttl = (Reply.Status == IP_SUCCESS) ? Reply.Options.Ttl : (UCHAR)ttl;
		returnValue.bSuccess = true;

		if (doLog)
			theApp.QueueDebugLogLine(false, _T("Reply (ICMP-pinger) from %s: bytes=%hu time=%3.2fms (%3.2fms %lums) TTL=%hhu")
				, (LPCTSTR)ipstr(Reply.Address)
				, Reply.DataSize
				, returnValue.fDelay
				, c_time.isPerformanceCounter() ? usResTime : -1.0f
				, Reply.RoundTripTime
				, returnValue.ttl);

	} else {
		returnValue.error = ::GetLastError();
		returnValue.bSuccess = false;
		if (doLog)
			theApp.QueueDebugLogLine(false , _T("Error from %s: Error=%u")
				, (LPCTSTR)ipstr(stDestAddr)
				, returnValue.error);
	}

	return returnValue;
}

void Pinger::PIcmpErr(LPCTSTR pszMsg, DWORD nICMPErr)
{
	DWORD dwSize = 511;
	CStringW sErr;
	bool b = (GetIpErrorString(nICMPErr, sErr.GetBuffer(dwSize), &dwSize) == NO_ERROR);
	sErr.ReleaseBuffer(); //the string has trailing spaces!
	theApp.QueueDebugLogLine(false, _T("%sPinger: %s")
		, pszMsg ? pszMsg : _T("")
		, (LPCTSTR)(b ? (CString)sErr.Trim() : GetErrorMessage(nICMPErr, 1)));
}
