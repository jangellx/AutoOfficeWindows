//
// AutoOfficeWindows.cpp : Application to respond to wake/sleep commands from AutoOffice.
//  Does not issue wake/sleep commands itself; it is solely a listener.
//

//
// Basic HTTP server code is from a Microsoft example that can be found here:
//  https://docs.microsoft.com/en-us/windows/win32/http/http-server-sample-application
//
// The host is using static IPs as defined by my router, as the URLs to be handled
//  by the server must be fully qualified.  While I could look up the IPs and return
//  them that way, this is simpler and I need the static IP anyway.  localhost can
//  be used for testing, but won't work on the LAN.
//
// Other machines will not be able to access the server until Windows ACL restrictons
//  are lifted on that port.  This is described here:
//   https://stackoverflow.com/questions/3313616/how-to-enable-external-request-in-iis-express
//  And invovles the following command:
//   netsh http add urlacl url=http://192.168.1.233:8182/ user=everyone
//
// A rule needs to be added to the Windows firewall to allow for the incoming connection.
//  I made a global rule for that port, but a safer way would be to make one for just this
//  application.
//
// This is not a robust program.  It does exactly what it needs to do.  It does not try to
//  actually parse the JSON, and it crudely tests the URLs for matching routes.  But it gets
//  the job done, and that's all I need.
//
// The program must be run with admin rights to host a server.  This is annoying, but that's
//  how Windows works.  Running the program with runas.exe can be used to promote it to admin
//  and only enter the admin password once.  For example:
//
//  %windir%\System32\runas.exe /savecred /user:administrator C:\JoesSource\Projects\AutoOfficeWindows\Debug\AutoOfficeWindows.exe
//
// Note that this first requires initializing the administrator account, as decsribed here:
//  
//
//
// When debugging VS must also be run as admin (RMB, choose Run As Administrator).
//

#include "stdafx.h"

#ifndef UNICODE
#define UNICODE
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <http.h>
#include <stdio.h>

#pragma comment(lib, "httpapi.lib")

//#include "precomp.h"

//
// Macros.
//
#define INITIALIZE_HTTP_RESPONSE( resp, status, reason )    \
    do {                                                    \
        RtlZeroMemory( (resp), sizeof(*(resp)) );           \
        (resp)->StatusCode = (status);                      \
        (resp)->pReason = (reason);                         \
        (resp)->ReasonLength = (USHORT) strlen(reason);     \
    } while (FALSE)

#define ADD_KNOWN_HEADER(Response, HeaderId, RawValue)               \
    do {                                                             \
        (Response).Headers.KnownHeaders[(HeaderId)].pRawValue =      \
                                                          (RawValue);\
        (Response).Headers.KnownHeaders[(HeaderId)].RawValueLength = \
            (USHORT) strlen(RawValue);                               \
    } while(FALSE)

#define ALLOC_MEM(cb) HeapAlloc(GetProcessHeap(), 0, (cb))

#define FREE_MEM(ptr) HeapFree(GetProcessHeap(), 0, (ptr))

//
// Prototypes.
//
DWORD DoReceiveRequests(HANDLE hReqQueue);

DWORD
SendHttpResponse(
	IN HANDLE        hReqQueue,
	IN PHTTP_REQUEST pRequest,
	IN USHORT        StatusCode,
	IN PSTR          pReason,
	IN PSTR          pEntity
);

DWORD
SendHttpPostResponse(
	IN HANDLE        hReqQueue,
	IN PHTTP_REQUEST pRequest
);


/*******************************************************************++

Routine Description:
    main routine

Arguments:
    argc - # of command line arguments.
    argv - Arguments.

Return Value:
    Success/Failure

--*******************************************************************/
int __cdecl wmain(
	int argc,
	wchar_t * argv[]
)
{
	ULONG           retCode;
	HANDLE          hReqQueue = NULL;
	int             UrlAdded = 0;
	HTTPAPI_VERSION HttpApiVersion = HTTPAPI_VERSION_1;

	//
	// Initialize HTTP Server APIs
	//
	retCode = HttpInitialize(
		HttpApiVersion,
		HTTP_INITIALIZE_SERVER,    // Flags
		NULL                       // Reserved
	);

	if (retCode != NO_ERROR) {
		wprintf(L"HttpInitialize failed with %lu \n", retCode);
		return retCode;
	}

	//
	// Create a Request Queue Handle
	//
	retCode = HttpCreateHttpHandle(
		&hReqQueue,        // Req Queue
		0                  // Reserved
	);

	if (retCode != NO_ERROR) {
		wprintf(L"HttpCreateHttpHandle failed with %lu \n", retCode);
		goto CleanUp;
	}

	//
	// The command line arguments represent URIs that to 
	// listen on. Call HttpAddUrl for each URI.
	//
	// The URI is a fully qualified URI and must include the
	// terminating (/) character.
	//
	wchar_t *urls[] = { L"http://192.168.1.233:8182/wake/",		// Get
			    L"http://192.168.1.233:8182/sleep/",	// Get
			    L"http://192.168.1.233:8182/status/",	// Put
			    L"http://192.168.1.233:8182/do/",		// Put
			    L"http://192.168.1.233:8182/do/wake/",	// Put
			    L"http://192.168.1.233:8182/do/sleep/",	// Put
			    NULL };

	for (int i = 0; urls[i] != NULL; i++) {
		wprintf(L"listening for requests on: %s\n", urls[i]);

		retCode = HttpAddUrl(
			hReqQueue,  // Req Queue
			urls[i],    // Fully qualified URL
			NULL        // Reserved
		);

		if (retCode != NO_ERROR) {
			wprintf(L"HttpAddUrl failed with %lu \n", retCode);
			if( retCode == 1214 )
				wprintf(L"- Invalid net name.  Local IP must exactly match the one in the app (currently 192.168.1.233)\n");
			goto CleanUp;

		} else {
			//
			// Track the currently added URLs.
			//
			UrlAdded++;
		}
	}

	DoReceiveRequests(hReqQueue);

  CleanUp:

	//
	// Call HttpRemoveUrl for all added URLs.
	//
	for (int i = 0; urls[i] != NULL; i++) {
		HttpRemoveUrl(
			hReqQueue,     // Req Queue
			urls[i]        // Fully qualified URL
		);
	}

	//
	// Close the Request Queue handle.
	//
	if (hReqQueue)
		CloseHandle(hReqQueue);

	// 
	// Call HttpTerminate.
	//
	HttpTerminate(HTTP_INITIALIZE_SERVER, NULL);

	return retCode;
}

// Utility to wake the display.  SC_MONITORPOWER with -1 should work, but seems to have broken since Windows 8.  MOving the mouse hacks around that.
void triggerWake(void) {
	SendNotifyMessage(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, (LPARAM)-1);	// -1: on
	mouse_event( 0x01 /* MOUSEEVENT_MOVE */, 0, 1, 0, NULL );
}

/*******************************************************************++

Routine Description:
    The function to receive a request. This function calls the
    corresponding function to handle the response.

Arguments:
    hReqQueue - Handle to the request queue

Return Value:
    Success/Failure.

--*******************************************************************/
DWORD DoReceiveRequests(
	IN HANDLE hReqQueue
)
{
	ULONG              result;
	HTTP_REQUEST_ID    requestId;
	DWORD              bytesRead;
	PHTTP_REQUEST      pRequest;
	PCHAR              pRequestBuffer;
	ULONG              RequestBufferLength;

	//
	// Allocate a 2 KB buffer. This size should work for most 
	// requests. The buffer size can be increased if required. Space
	// is also required for an HTTP_REQUEST structure.
	//
	RequestBufferLength = sizeof(HTTP_REQUEST) + 2048;
	pRequestBuffer = (PCHAR)ALLOC_MEM(RequestBufferLength);

	if (pRequestBuffer == NULL)
		return ERROR_NOT_ENOUGH_MEMORY;

	pRequest = (PHTTP_REQUEST)pRequestBuffer;

	//
	// Wait for a new request. This is indicated by a NULL 
	// request ID.
	//

	HTTP_SET_NULL_ID(&requestId);

	while (1) {
		RtlZeroMemory(pRequest, RequestBufferLength);

		result = HttpReceiveHttpRequest(
			hReqQueue,          // Req Queue
			requestId,          // Req ID
			0,                  // Flags
			pRequest,           // HTTP request buffer
			RequestBufferLength,// req buffer length
			&bytesRead,         // bytes received
			NULL                // LPOVERLAPPED
		);

		if (NO_ERROR == result) {
			//
			// Worked! 
			// 
			switch (pRequest->Verb) {
			    case HttpVerbGET: {
				// GET
				wprintf(L"Got a GET request for %ws \n", pRequest->CookedUrl.pFullUrl);

				wchar_t *hasDo = wcsstr((wchar_t *)pRequest->CookedUrl.pFullUrl, L"/do");
				if (hasDo != NULL) {
				    // "/do" routes not valid as GET
					wprintf(L"/do unsupported for GET\n");
					result = SendHttpResponse(
						hReqQueue,
						pRequest,
						404,
						"Not Available",
						"GET requests not available at this URL \r\n"
					);
					break;
				}

				wchar_t *route = wcsrchr((wchar_t *)pRequest->CookedUrl.pFullUrl, '/');
				if (route == NULL) {
				    // Invalid route
					wprintf(L"Unknown path\n");
					result = SendHttpResponse(
						hReqQueue,
						pRequest,
						404,
						"Not Available",
						"GET reqeusts not available at this URL \r\n"
					);
					break;

				}
				else if (wcscmp(route, L"/wake") == 0) {
				 // Wake the displays
					wprintf(L"/wake GET URL hit\n");
					triggerWake();
					result = SendHttpResponse(hReqQueue, pRequest, 200, "Wake", "{\"isAwake\":1}");
					break;

				}
				else if (wcscmp(route, L"/sleep") == 0) {
				 // Sleep the displays
					wprintf(L"/sleep GET URL hit\n");
					SendNotifyMessage(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, (LPARAM)2);		// 2: off
					result = SendHttpResponse(hReqQueue, pRequest, 200, "Sleep", "{\"isAwake\":0}");
					break;

				}
				else if (wcscmp(route, L"/status") == 0) {
				 // Return the status as a simple JSON construct
					wprintf(L"/status GET URL hit\n");
					int isPowered = -1;

					POINT p = { 0, 0 };
					HMONITOR monitor = MonitorFromPoint(p, MONITOR_DEFAULTTONULL);
					if (monitor != NULL) {
						BOOL dpsResult;
						if (GetDevicePowerState(monitor, &dpsResult))
							isPowered = (result == TRUE) ? 1 : 0;
					}

					result = SendHttpResponse(hReqQueue, pRequest,
								  isPowered ? 200 : 503, 		// 200: success; 503: service unavailable
								  "Status",
								  isPowered ? "{\"isAwake\":1}" : "{\"isAwake\":0}");

					break;

				}
				else {
				 // Unsupported GET URL
					wprintf(L"Unsupported GET URL hit\n");
					result = SendHttpResponse(
						hReqQueue,
						pRequest,
						404,
						"Not Available",
						"GET requests not available at this URL \r\n"
					);
					break;
				}

			    } break;

			    case HttpVerbPUT: {
				// PUT
				wprintf(L"Got a PUT request for %ws \n", pRequest->CookedUrl.pFullUrl);

				wchar_t *hasDo = wcsstr((wchar_t *)pRequest->CookedUrl.pFullUrl, L"/do");
				if (hasDo == NULL) {
					// Only "/do" routes are valid as PUT
					wprintf(L"Only /do unsupported for PUT\n");
					result = SendHttpResponse(
						hReqQueue,
						pRequest,
						404,
						"Not Available",
						"Only /do supported for PUT \r\n"
					);
					break;
				}

				if (wcsstr((wchar_t *)pRequest->CookedUrl.pFullUrl, L"/do/wake") != NULL) {
					// /do/wake
					wprintf(L"/do/wake PUT URL hit\n");
					triggerWake();
					result = SendHttpResponse(hReqQueue, pRequest, 200, "Wake", "{\"isAwake\":1}");
					break;
				}

				if (wcsstr((wchar_t *)pRequest->CookedUrl.pFullUrl, L"/do/wake") != NULL) {
					// /do/sleep
					wprintf(L"/do/sleep PUT URL hit\n");
					SendNotifyMessage(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, (LPARAM)2);		// 2: off
					result = SendHttpResponse(hReqQueue, pRequest, 200, "Sleep", "{\"isAwake\":0}");
					break;
				}

				ULONG  BytesRead          = 0; 
				ULONG  EntityBufferLength = 2048;
				PUCHAR pEntityBuffer      = (PUCHAR) ALLOC_MEM( EntityBufferLength );
				DWORD  result = HttpReceiveRequestEntityBody(
					hReqQueue,
					pRequest->RequestId,
					0,
					pEntityBuffer,
					EntityBufferLength,
					&BytesRead,
					NULL 
					);

				// Just /do; crudely parse the JSON
				if( bytesRead >= 2047 ) {
					result = SendHttpResponse(
						hReqQueue,
						pRequest,
						413,
						"Too much data",
						NULL
					);
					break;
				}

//				printf( (char *)pEntityBuffer );

				if (strstr((char *)pEntityBuffer, "command") == NULL) {
					result = SendHttpResponse(
						hReqQueue,
						pRequest,
						400,
						"No command found",
						NULL
					);
					break;
				}

				if (strstr((char *)pEntityBuffer, "wake") != NULL) {
					// /do command:wake
					wprintf(L"/do command:wake PUT URL hit\n");
					triggerWake();
					result = SendHttpResponse(hReqQueue, pRequest, 200, "Wake", "{\"isAwake\":1}");
					break;
				}

				if (strstr((char *)pEntityBuffer, "sleep") != NULL) {
					// /do command:sleep
					wprintf(L"/do command:sleep PUT URL hit\n");
					SendNotifyMessage(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, (LPARAM)2);		// 2: off
					result = SendHttpResponse(hReqQueue, pRequest, 200, "Sleep", "{\"isAwake\":0}");
					break;
				}

				result = SendHttpResponse(
					hReqQueue,
					pRequest,
					400,
					"Invalid command",
					NULL
				);

			    } break;

			    default:
				// Unknown request
				wprintf(L"Got a unknown request for %ws \n",
					pRequest->CookedUrl.pFullUrl);

				result = SendHttpResponse(
					hReqQueue,
					pRequest,
					503,
					"Not Implemented",
					NULL
				);
				break;
			}

			if (result != NO_ERROR)
				break;

			//
			// Reset the Request ID to handle the next request.
			//
			HTTP_SET_NULL_ID(&requestId);

		} else if (result == ERROR_MORE_DATA) {
			//
			// The input buffer was too small to hold the request
			// headers. Increase the buffer size and call the 
			// API again. 
			//
			// When calling the API again, handle the request
			// that failed by passing a RequestID.
			//
			// This RequestID is read from the old buffer.
			//
			requestId = pRequest->RequestId;

			//
			// Free the old buffer and allocate a new buffer.
			//
			RequestBufferLength = bytesRead;
			FREE_MEM(pRequestBuffer);
			pRequestBuffer = (PCHAR)ALLOC_MEM(RequestBufferLength);

			if (pRequestBuffer == NULL) {
				result = ERROR_NOT_ENOUGH_MEMORY;
				break;
			}

			pRequest = (PHTTP_REQUEST)pRequestBuffer;

		} else if (ERROR_CONNECTION_INVALID == result && !HTTP_IS_NULL_ID(&requestId)) {
			// The TCP connection was corrupted by the peer when
			// attempting to handle a request with more buffer. 
			// Continue to the next request.
			HTTP_SET_NULL_ID(&requestId);
		} else {
			break;
		}

	}

	if (pRequestBuffer)
		FREE_MEM(pRequestBuffer);

	return result;
}

/*******************************************************************++

Routine Description:
    The routine sends a HTTP response

Arguments:
    hReqQueue     - Handle to the request queue
    pRequest      - The parsed HTTP request
    StatusCode    - Response Status Code
    pReason       - Response reason phrase
    pEntityString - Response entity body

Return Value:
    Success/Failure.
--*******************************************************************/

DWORD SendHttpResponse(
	IN HANDLE        hReqQueue,
	IN PHTTP_REQUEST pRequest,
	IN USHORT        StatusCode,
	IN PSTR          pReason,
	IN PSTR          pEntityString
)
{
	HTTP_RESPONSE   response;
	HTTP_DATA_CHUNK dataChunk;
	DWORD           result;
	DWORD           bytesSent;

	//
	// Initialize the HTTP response structure.
	//
	INITIALIZE_HTTP_RESPONSE(&response, StatusCode, pReason);

	//
	// Add a known header.
	//
	ADD_KNOWN_HEADER(response, HttpHeaderContentType, "text/html");

	if (pEntityString) {
	    // 
	    // Add an entity chunk.
	    //
		dataChunk.DataChunkType = HttpDataChunkFromMemory;
		dataChunk.FromMemory.pBuffer = pEntityString;
		dataChunk.FromMemory.BufferLength =
			(ULONG)strlen(pEntityString);

		response.EntityChunkCount = 1;
		response.pEntityChunks = &dataChunk;
	}

	// 
	// Because the entity body is sent in one call, it is not
	// required to specify the Content-Length.
	//

	result = HttpSendHttpResponse(
		hReqQueue,           // ReqQueueHandle
		pRequest->RequestId, // Request ID
		0,                   // Flags
		&response,           // HTTP response
		NULL,                // pReserved1
		&bytesSent,          // bytes sent  (OPTIONAL)
		NULL,                // pReserved2  (must be NULL)
		0,                   // Reserved3   (must be 0)
		NULL,                // LPOVERLAPPED(OPTIONAL)
		NULL                 // pReserved4  (must be NULL)
	);

	if (result != NO_ERROR)
		wprintf(L"HttpSendHttpResponse failed with %lu \n", result);

	return result;
}
