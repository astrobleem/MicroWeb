#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "HTTP.h"

// Resolve redirects into the existing request-path buffer, not another 512-byte
// allocation per request. Reject oversized destinations rather than truncate them.
static bool ResolveRedirect(char* out, const char* base, const char* target)
{
	const char* authority = strstr(base, "://");
	if (!authority) return false;
	authority += 3;
	const char* basePath = authority + strcspn(authority, "/?#");
	size_t prefix = 0;
	bool slash = false;
	const char* schemeEnd = target + strcspn(target, ":/?#");
	if (*schemeEnd == ':')
	{
		if (strnicmp(target, "http://", 7) && strnicmp(target, "https://", 8)) return false;
	}
	else if (target[0] == '/' && target[1] == '/') prefix = authority - base - 2; // Keep http: or https:
	else if (target[0] == '/') prefix = basePath - base;
	else if (target[0] == '#') prefix = strcspn(base, "#");
	else if (target[0] == '?') prefix = strcspn(base, "?#");
	else
	{
		const char* end = base + strcspn(base, "?#");
		const char* last = end;
		while (last > basePath && last[-1] != '/') --last;
		prefix = last - base;
		if (last == basePath) slash = true;
	}
	size_t length = strlen(target);
	if (!length || prefix + length + (slash ? 1 : 0) >= PATH_LEN) return false;
	memcpy(out, base, prefix);
	if (slash) out[prefix++] = '/';
	memcpy(out + prefix, target, length + 1);

	// RFC-style dot segments apply to the path only, never the query/fragment.
	char* start = strstr(out, "://") + 3;
	start += strcspn(start, "/?#");
	char* read = start;
	char* write = start;
	while (*read && *read != '?' && *read != '#')
	{
		if (!strncmp(read, "/./", 3)) { read += 2; continue; }
		if (!strncmp(read, "/../", 4))
		{
			read += 3;
			while (write > start && *--write != '/') {}
			continue;
		}
		if ((!strncmp(read, "/.", 2) && (!read[2] || read[2] == '?' || read[2] == '#')) ||
			(!strncmp(read, "/..", 3) && (!read[3] || read[3] == '?' || read[3] == '#')))
		{
			if (read[2] == '.') { while (write > start && *--write != '/') {} read += 3; }
			else read += 2;
			*write++ = '/';
			break;
		}
		*write++ = *read++;
	}
	memmove(write, read, strlen(read) + 1);
	return true;
}

// IPv4/DNS endpoint only. Both origins and gateways are checked before DNS,
// including the origin port when the connection itself goes to a proxy.
static bool ParseEndpoint(const char* value, size_t length, size_t& hostLength, uint16_t& port)
{
	hostLength = 0;
	if (!length || length >= HOSTNAME_LEN) return false;
	while (hostLength < length && value[hostLength] != ':')
	{
		unsigned char c = value[hostLength];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_')) return false;
		++hostLength;
	}
	if (!hostLength) return false;
	if (hostLength < length)
	{
		unsigned long number = 0;
		if (hostLength + 1 == length) return false;
		for (size_t i = hostLength + 1; i < length; ++i)
		{
			if (value[i] < '0' || value[i] > '9') return false;
			number = number * 10 + (value[i] - '0');
			if (number > 65535UL) return false;
		}
		if (!number) return false;
		port = (uint16_t)number;
	}
	return true;
}

HTTPRequest::HTTPRequest() : status(HTTPRequest::Stopped), internalStatus(HTTPRequest::InvalidState), sock(NULL), requestOptions(NULL), requestType(HTTPRequest::Get), redirectCount(0), redirectPending(false)
{
	contentType[0] = '\0';
}

void HTTPRequest::Reset()
{
	lineBufferSize = 0;
	lineBufferSendPos = -1;
	contentType[0] = '\0';
	contentSize = 0;
	contentSendPos = 0;
	contentRemaining = -1;
	chunkSizeRemaining = 0;
	usingChunkedTransfer = false;
	redirectPending = false;
}

void HTTPRequest::WriteLine(const char* fmt, ...)
{
	if (status == Error) return;
	va_list ap;
	va_start(ap, fmt);
	int maxWriteSize = LINE_BUFFER_SIZE - lineBufferSize - 2;
	int vsrc = vsnprintf(lineBuffer + lineBufferSize, maxWriteSize, fmt, ap);
	va_end(ap);

	if ((vsrc < 0) || (vsrc >= maxWriteSize))
	{
		MarkError(WriteLineError);
		lineBufferSendPos = -1;
	}
	else
	{
		if (lineBufferSendPos == -1)
		{
			lineBufferSendPos = 0;
		}
		lineBufferSize += vsrc;
		lineBuffer[lineBufferSize++] = '\r';
		lineBuffer[lineBufferSize++] = '\n';
	}
}

bool HTTPRequest::SendPendingWrites()
{
	if (sock)
	{
		if (lineBufferSendPos < 0)
		{
			return false;
		}

		int rc = sock->Send((uint8_t*)(lineBuffer + lineBufferSendPos), lineBufferSize - lineBufferSendPos);
		if (rc > 0)
		{
			ResetTimeOutTimer();
			lineBufferSendPos += rc;
			if (lineBufferSendPos >= lineBufferSize)
			{
				lineBufferSendPos = -1;
				lineBufferSize = 0;
			}
		}
		else if (rc < 0)
		{
			MarkError(WriteLineError);
			lineBufferSendPos = -1;
		}
		return lineBufferSendPos >= 0;
	}
	return false;
}

void HTTPRequest::ResetTimeOutTimer()
{
	timeout = clock();
}

void HTTPRequest::Open(RequestType type, char* inURL, HTTPOptions* options)
{
	redirectCount = 0;
	headerRetryCount = 0;
	OpenInternal(type, inURL, options);
}

void HTTPRequest::OpenInternal(RequestType type, const char* inURL, HTTPOptions* options)
{
	Stop();
	Reset();
	if (!inURL || strlen(inURL) >= MAX_URL_LENGTH || strpbrk(inURL, "\r\n\t "))
	{
		MarkError(InvalidURL);
		return;
	}
	// inURL can be our path buffer during a redirect or url during a retry.
	memmove(url.url, inURL, strlen(inURL) + 1);
	requestType = type;
	requestOptions = options;

	bool https = !strnicmp(url.url, "https://", 8);
	if (!https && strnicmp(url.url, "http://", 7))
	{
		MarkError(InvalidProtocol);
		return;
	}

	const char* origin = url.url + (https ? 8 : 7);
	const char* pathStart = origin + strcspn(origin, "/?#");
	size_t hostLength;
	serverPort = https ? 443 : 80;
	if (!ParseEndpoint(origin, pathStart - origin, hostLength, serverPort))
	{
		MarkError(InvalidURL);
		return;
	}

	// HTTPS is never sent directly or silently changed to HTTP. This separate
	// opt-in names a trusted plaintext TLS gateway, not a CONNECT proxy.
	const char* proxy = getenv(https ? "MICROWEB_HTTPS_PROXY" : "HTTP_PROXY");
	if (https && (!proxy || !*proxy))
	{
		status = HTTPRequest::UnsupportedHTTPS;
		return;
	}
	if (proxy)
	{
		serverPort = 80;
		if (!ParseEndpoint(proxy, strlen(proxy), hostLength, serverPort))
		{
			MarkError(InvalidProxy);
			return;
		}
		memcpy(hostname, proxy, hostLength);
		hostname[hostLength] = 0;
		strcpy(path, url.url); // Absolute-form target, including https://.
	}
	else
	{
		memcpy(hostname, origin, hostLength);
		hostname[hostLength] = 0;
		if (*pathStart == '/') strcpy(path, pathStart);
		else
		{
			path[0] = '/';
			strcpy(path + 1, pathStart);
		}
	}

	// Fragments belong to the browser and are never sent to either server.
	char* fragment = strchr(path, '#');
	if (fragment) *fragment = 0;

	status = HTTPRequest::Connecting;
	internalStatus = QueuedDNSRequest;
	ResetTimeOutTimer();
}

size_t HTTPRequest::ReadData(char* buffer, size_t count)
{
	if (status == HTTPRequest::Downloading && sock && internalStatus == ReceiveContent)
	{
		if (usingChunkedTransfer && count > chunkSizeRemaining)
		{
			count = chunkSizeRemaining;
		}

		if (!usingChunkedTransfer && contentRemaining >= 0 && (long)count > contentRemaining)
			count = (size_t)contentRemaining;
		if (count > 32767) count = 32767;
		if (!count) return 0;
		int16_t rc = sock->Receive((unsigned char*)buffer, (int)count);
		if (rc < 0)
		{
			MarkError(ContentReceiveError);
		}
		else if (rc == 0 && sock->IsClosed())
		{
			if (!usingChunkedTransfer && contentRemaining < 0) Finish();
			else MarkError(ContentReceiveError);
		}
		else if(rc > 0)
		{
			ResetTimeOutTimer();

			int bytesRead = rc;
			if (!usingChunkedTransfer && contentRemaining > 0)
			{
				contentRemaining -= bytesRead;
				if (contentRemaining <= 0)
				{
					Finish();

					return (size_t)bytesRead;
				}
			}

			if (usingChunkedTransfer)
			{
				chunkSizeRemaining -= bytesRead;
				if (!chunkSizeRemaining)
				{
					internalStatus = ParseChunkHeaderLineBreak;
				}
			}

			return bytesRead;
		}
	}
	return 0;
}

void HTTPRequest::Stop()
{
	if (sock)
	{
		sock->Close();
		Platform::network->DestroySocket(sock);
		sock = NULL;
	}
	status = HTTPRequest::Stopped;
}

void HTTPRequest::Finish()
{
	Stop();
	status = HTTPRequest::Finished;
	internalStatus = InvalidState;
}

void HTTPRequest::MarkError(InternalStatus statusError)
{
	Stop();
	lineBufferSendPos = -1;
	status = HTTPRequest::Error;
	internalStatus = statusError;
}

void HTTPRequest::Update()
{
	if (status != HTTPRequest::Connecting && status != HTTPRequest::Downloading)
	{
		return;
	}

	if ((status == HTTPRequest::Connecting || status == HTTPRequest::Downloading) &&
		(unsigned long)clock() - (unsigned long)timeout > (unsigned long)HTTP_RESPONSE_TIMEOUT)
	{
		// Only replay a plain GET before any response bytes arrived, once.
		// Never retry a POST or a caller-supplied header/body request.
		if (!headerRetryCount && requestType == Get && !requestOptions &&
			internalStatus == ReceiveHeaderResponse && !lineBufferSize && lineBufferSendPos < 0)
		{
			++headerRetryCount;
			OpenInternal(Get, url.url, NULL);
		}
		else MarkError(TimedOut);
		return;
	}

	if (SendPendingWrites())
	{
		return;
	}

	switch (status)
	{
		case HTTPRequest::Connecting:
		{
			switch (internalStatus)
			{
				case QueuedDNSRequest:
				{
					int rc = Platform::network->ResolveAddress(hostname, hostAddr, true);
					if (rc > 0)
					{
						internalStatus = WaitingDNSResolve;
					}
					else if (rc == 0)
					{
						internalStatus = OpeningSocket;
					}
					else
					{
						MarkError(HostNameResolveError);
					}
				}
				break;
				case WaitingDNSResolve:
				{
					int8_t rc = Platform::network->ResolveAddress(hostname, hostAddr, false);
					if (rc == 0)
					{
						internalStatus = OpeningSocket;
					}
					else if (rc < 0)
					{
						MarkError(HostNameResolveError);
					}
				}
				break;
				case OpeningSocket:
				{
					sock = Platform::network->CreateSocket();
					if (!sock)
					{
						MarkError(SocketCreationError);
						break;
					}

					if (sock->Connect(hostAddr, serverPort))
					{
						MarkError(SocketCreationError);
						break;
					}
					internalStatus = ConnectingSocket;
					ResetTimeOutTimer();
				}
				break;
				case ConnectingSocket:
				{
					if (sock->IsConnectComplete())
					{
						internalStatus = SendHeaders;
						ResetTimeOutTimer();
						break;
					}
					else if (sock->IsClosed())
					{
						MarkError(SocketConnectionError);
						break;
					}
				}
				break;
				case SendHeaders:
				{
					switch (requestType)
					{
					case HTTPRequest::Post:
						WriteLine("POST %s HTTP/1.1", path);
						break;
					case HTTPRequest::Get:
						WriteLine("GET %s HTTP/1.1", path);
						break;
					}
					// Preserve the optional suffix supported by earlier MicroWeb builds.
					// Ignore invalid configuration instead of allowing header injection.
					const char* agent = getenv("HTTP_USER_AGENT");
					bool validAgent = agent && *agent && strlen(agent) <= 120;
					if (validAgent)
					{
						for (const unsigned char* p = (const unsigned char*)agent; *p; ++p)
							if (*p < 32 || *p == 127) { validAgent = false; break; }
					}
					if (validAgent) WriteLine("User-Agent: MicroWeb 2.1 %s", agent);
					else WriteLine("User-Agent: MicroWeb 2.1");
					// Host names the origin (including its port), not an HTTP proxy.
					const char* origin = strstr(url.url, "://") + 3;
					WriteLine("Host: %.*s", (int)strcspn(origin, "/?#"), origin);
					WriteLine("Accept-Encoding: identity");
			
					if(requestOptions && requestOptions->keepAlive)
					{
						WriteLine("Connection: keep-alive");
					}
					else
					{
						WriteLine("Connection: close");
					}
			
					if(requestOptions && requestOptions->headerParams)
					{
						WriteLine("%s", requestOptions->headerParams);
					}
			
					if(requestType == Post && requestOptions && requestOptions->postContentType && requestOptions->contentData)
					{
						WriteLine("Content-Type: %s", requestOptions->postContentType);
						WriteLine("Content-Length: %d", strlen(requestOptions->contentData));
						WriteLine("");
					}
					else
					{
						WriteLine("");
					}
			
					if(status != HTTPRequest::Error)
					{
						internalStatus = requestType == Post && requestOptions && requestOptions->postContentType && requestOptions->contentData ? SendContent : ReceiveHeaderResponse;
					}
				}
				break;
				case SendContent:
				{
					size_t remaining = strlen(requestOptions->contentData) - contentSendPos;
					if (remaining)
					{
						int rc = sock->Send((uint8_t*)requestOptions->contentData + contentSendPos,
							(int)(remaining > 32767 ? 32767 : remaining));
						if (rc < 0) { MarkError(WriteLineError); break; }
						if (rc > 0) { contentSendPos += rc; ResetTimeOutTimer(); }
						if ((size_t)rc < remaining) break;
					}
					internalStatus = ReceiveHeaderResponse;
					ResetTimeOutTimer();
				}
				break;
				case ReceiveHeaderResponse:
				{
					if (ReadLine())
					{
						if ((strncmp(lineBuffer, "HTTP/1.0", 8) != 0) && (strncmp(lineBuffer, "HTTP/1.1", 8) != 0)) {
							MarkError(UnsupportedHTTPError);
							return;
						}

						// Skip past HTTP version number
						char* s = lineBuffer + 8;
						char* s2 = s;

						// Skip past whitespace
						while (*s) {
							if (*s != ' ' && *s != '\t') break;
							s++;
						}

						if ((s == s2) || (*s == 0) || (sscanf(s, "%3d", &responseCode) != 1)) {
							MarkError(MalformedHTTPVersionLineError);
							return;
						}

						//printf("Response code: %d", responseCode);
						//getchar();
						internalStatus = ReceiveHeaderContent;

						contentRemaining = -1;
						usingChunkedTransfer = false;
						contentType[0] = '\0';
					}
				}
				break;
				case ReceiveHeaderContent:
				{
					if (ReadLine())
					{
						if (lineBuffer[0] == '\0')
						{
							if (redirectPending)
							{
								if (redirectCount >= MAX_HTTP_REDIRECTS) { MarkError(TooManyRedirects); break; }
								++redirectCount;
								RequestType nextType = requestType;
								HTTPOptions* nextOptions = requestOptions;
								if (responseCode == RESPONSE_SEE_OTHER ||
									(requestType == Post && (responseCode == 301 || responseCode == 302)))
								{ nextType = Get; nextOptions = NULL; }
								OpenInternal(nextType, path, nextOptions);
							}
							else if (responseCode == 204 || responseCode == 304 || (!usingChunkedTransfer && contentRemaining == 0))
							{
								// A zero-length response is complete, not an error.
								Finish();
							}
							else
							{
								// Header has finished
								if (usingChunkedTransfer)
								{
									internalStatus = ParseChunkHeader;
								}
								else
								{
									status = Downloading;
									internalStatus = ReceiveContent;
								}
							}
							break;
						}
						else if (!strnicmp(lineBuffer, "Location:", 9))
						{
							if (responseCode == 301 || responseCode == 302 || responseCode == 303 || responseCode == 307 || responseCode == 308)
							{
								char* location = lineBuffer + 9;
								while (*location == ' ' || *location == '\t') ++location;
								char* end = location + strlen(location);
								while (end > location && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
								if (redirectPending || !ResolveRedirect(path, url.url, location)) MarkError(InvalidURL);
								else if (!strnicmp(url.url, "https://", 8) && !strnicmp(path, "http://", 7))
									MarkError(HTTPSDowngrade);
								else
								{
									const char* oldHost = strstr(url.url, "://") + 3;
									const char* newHost = strstr(path, "://") + 3;
									size_t oldLength = strcspn(oldHost, "/?#");
									if (requestOptions && requestOptions->headerParams &&
										(oldLength != strcspn(newHost, "/?#") || strnicmp(oldHost, newHost, oldLength)))
										MarkError(InvalidURL);
									else redirectPending = true;
								}
							}
						}
						else if (!strnicmp(lineBuffer, "Content-Length:", 15))
						{
							char* end;
							const char* value = lineBuffer + 15;
							while (*value == ' ' || *value == '\t') ++value;
							errno = 0;
							long length = strtol(value, &end, 10);
							while (*end == ' ' || *end == '\t') ++end;
							if (*value < '0' || *value > '9' || *end || errno == ERANGE || length < 0 ||
								(contentRemaining >= 0 && contentRemaining != length))
								MarkError(ContentReceiveError);
							else contentSize = contentRemaining = length;
						}
						else if (!strnicmp(lineBuffer, "Transfer-Encoding:", 18))
						{
							char* value = lineBuffer + 18;
							while (*value == ' ' || *value == '\t') ++value;
							char* end = value + strlen(value);
							while (end > value && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
							if (stricmp(value, "chunked")) MarkError(ContentReceiveError);
							else usingChunkedTransfer = true;
						}
						else if (!strnicmp(lineBuffer, "Content-Type:", 13))
						{
							const char* value = lineBuffer + 13;
							while (*value == ' ' || *value == '\t') ++value;
							strncpy(contentType, value, MAX_CONTENT_TYPE_LENGTH - 1);
							contentType[MAX_CONTENT_TYPE_LENGTH - 1] = 0;
						}

						//printf("Header: %s  -- \n", lineBuffer);
						//getchar();
					}
				}
				break;
			}
		}
		break;

		case HTTPRequest::Downloading:
		{
		}
		break;
	}

	if (internalStatus == ParseChunkHeaderLineBreak)
	{
		if (ReadLine())
		{
			if (lineBuffer[0]) MarkError(ContentReceiveError);
			else internalStatus = ParseChunkHeader;
		}
	}
	if (internalStatus == ParseChunkHeader)
	{
		if (ReadLine())
		{
			char* end;
			errno = 0;
			chunkSizeRemaining = strtol(lineBuffer, &end, 16);
			if (!lineBuffer[0] || lineBuffer[0] == '-' || lineBuffer[0] == '+' ||
				end == lineBuffer || (*end && *end != ';') || errno == ERANGE || chunkSizeRemaining < 0)
			{
				MarkError(ContentReceiveError);
				return;
			}

			if (chunkSizeRemaining)
			{
				status = Downloading;
				internalStatus = ReceiveContent;
			}
			else
			{
				internalStatus = ParseChunkTrailers;
			}
		}
	}
	if (internalStatus == ParseChunkTrailers && ReadLine() && !lineBuffer[0]) Finish();
}

bool HTTPRequest::ReadLine()
{
	if (!sock)
		return false;

	while (1)
	{
		int rc = sock->Receive((unsigned char*)lineBuffer + lineBufferSize, 1);
		if (rc == 0)
		{
			if (sock->IsClosed()) MarkError(ContentReceiveError);
			// A temporarily empty socket is not EOF.
			return false;
		}
		else if (rc < 0)
		{
			printf("Receive error\n");
			MarkError(ContentReceiveError);
			return false;
		}

		ResetTimeOutTimer();

		if (lineBufferSize >= LINE_BUFFER_SIZE)
		{
			// Line was too long
			lineBuffer[LINE_BUFFER_SIZE - 1] = '\0';
			MarkError(ContentReceiveError);
			return false;
		}

		if (lineBuffer[lineBufferSize] == '\n')
		{
			lineBuffer[lineBufferSize] = '\0';
			if (lineBufferSize >= 1)
			{
				if (lineBuffer[lineBufferSize - 1] == '\r')
				{
					lineBuffer[lineBufferSize - 1] = '\0';
				}
			}

			lineBufferSize = 0;
			return true;
		}

		if (lineBuffer[lineBufferSize] == '\0' || lineBufferSize >= LINE_BUFFER_SIZE - 1)
		{
			// Never turn a truncated Location (or other header) into a valid line.
			MarkError(ContentReceiveError);
			return false;
		}
		++lineBufferSize;
	}
}

const char* HTTPRequest::GetStatusString()
{
	if ((status == Connecting || status == Downloading || status == Finished) &&
		!strnicmp(url.url, "https://", 8)) return HTTPS_PROXY_WARNING;

	switch (status)
	{
	case HTTPRequest::Error:
		switch (internalStatus)
		{
		case InvalidURL:
			return "Invalid or oversized URL";
		case InvalidProxy:
			return "Invalid proxy: use hostname:port";
		case HTTPSDowngrade:
			return "Blocked HTTPS to HTTP redirect";
		case TooManyRedirects:
			return "Too many redirects";
		case InvalidPort:
			return "Invalid port";
		case InvalidProtocol:
			return "Invalid protocol";
		case SocketCreationError:
			return "Socket creation error";
		case SocketConnectionError:
			return "Socket connection error";
		case HeaderSendError:
			return "Error sending HTTP header";
		case ContentReceiveError:
			return "Error receiving HTTP content";
		case UnsupportedHTTPError:
			return "Unsupported HTTP version";
		case MalformedHTTPVersionLineError:
			return "Malformed HTTP version line";
		case WriteLineError:
			return "Error writing headers";
		case HostNameResolveError:
			return "Error resolving host name";
		case TimedOut:
			return "Connection timed out";
		}
		break;

	case HTTPRequest::Connecting:
		switch (internalStatus)
		{
		case QueuedDNSRequest:
		case WaitingDNSResolve:
			return "Resolving host name via DNS";
		case OpeningSocket:
			return "Connecting to server";
		case ConnectingSocket:
		case SendHeaders:
		case SendContent:
			return "Sending headers";
		case ReceiveHeaderResponse:
		case ReceiveHeaderContent:
			return "Receiving headers";
		case ReceiveContent:
			return "Receiving content";
		}
		break;

	case HTTPRequest::Stopped:
		return "Stopped";

	case HTTPRequest::Downloading:
		if (internalStatus == ReceiveContent)
		{
			return "Receiving content";
		}
		return "Downloading?";
	case HTTPRequest::Finished:
		return "Finished";
	case HTTPRequest::UnsupportedHTTPS:
		return "HTTPS needs MICROWEB_HTTPS_PROXY";
	default:
		break;
	}
	return "Unknown state";
}
