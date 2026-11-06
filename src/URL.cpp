#include "URL.h"

static bool IsFileScheme(const char* url, const char* scheme)
{
	return scheme && scheme - url == 4 &&
		(url[0] == 'f' || url[0] == 'F') && (url[1] == 'i' || url[1] == 'I') &&
		(url[2] == 'l' || url[2] == 'L') && (url[3] == 'e' || url[3] == 'E');
}

void URL::CleanUp()
{
	// Query and fragment bytes are opaque, not path components or HTML.
	for (char* p = url; *p && *p != '?' && *p != '#'; ++p)
		if (*p == '\\') *p = '/';

	char* scheme = strstr(url, "://");
	bool local = !scheme || IsFileScheme(url, scheme);
	char* start = scheme ? scheme + 3 : url;
	if (!local) start += strcspn(start, "/?#");
	else if (*start && start[1] == ':') start += 2;
	bool rooted = *start == '/';
	char* read = start;
	char* write = start;
	while (*read && *read != '?' && *read != '#')
	{
		if (write == start && local && !strncmp(read, "./", 2)) { read += 2; continue; }
		if (!strncmp(read, "/./", 3)) { read += 2; continue; }
		bool parent = !strncmp(read, "/../", 4) ||
			(!strncmp(read, "/..", 3) && (!read[3] || read[3] == '?' || read[3] == '#'));
		if (parent)
		{
			char* previous = write;
			while (previous > start && previous[-1] != '/') --previous;
			bool previousIsParent = write - previous == 2 && previous[0] == '.' && previous[1] == '.';
			if (rooted || (previous < write && !previousIsParent))
			{
				write = previous > start ? previous - 1 : start;
				read += 3;
				if (write == start && local && !rooted && *read == '/') ++read;
				if (!*read || *read == '?' || *read == '#')
				{
					if (rooted || write > start) *write++ = '/';
					break;
				}
				continue;
			}
		}
		if (!strncmp(read, "/.", 2) && (!read[2] || read[2] == '?' || read[2] == '#'))
		{
			read += 2;
			if (rooted || write > start) *write++ = '/';
			break;
		}
		*write++ = *read++;
	}
	memmove(write, read, strlen(read) + 1);
}

const URL& URL::GenerateFromRelative(const char* baseURL, const char* relativeURL)
{
	static URL result;
	// Stage separately because callers may pass the preceding result as base.
	// Static storage keeps this buffer off the 2 KB DOS call stack.
	static URL next;
	next.url[0] = '\0';
	if (!baseURL || !relativeURL || strlen(baseURL) >= MAX_URL_LENGTH || strlen(relativeURL) >= MAX_URL_LENGTH)
	{
		result.url[0] = '\0';
		return result;
	}

	const char* scheme = strstr(baseURL, "://");
	bool local = !scheme || IsFileScheme(baseURL, scheme);
	const char* basePath = scheme ? scheme + 3 : baseURL;
	if (!local) basePath += strcspn(basePath, "/?#");
	const char* prefixSource = baseURL;
	size_t prefix = 0;
	bool slash = false;
	const char* targetScheme = relativeURL + strcspn(relativeURL, ":/\\?#");
	if (*targetScheme == ':') { /* Absolute URI or DOS drive path. */ }
	else if (relativeURL[0] == '/' && relativeURL[1] == '/')
	{
		if (scheme && !local) prefix = scheme - baseURL + 1;
		else { prefixSource = "http:"; prefix = 5; }
	}
	else if (relativeURL[0] == '/' || relativeURL[0] == '\\')
	{
		prefix = basePath - baseURL;
		if (local && basePath[0] && basePath[1] == ':') prefix += 2;
	}
	else if (relativeURL[0] == '#') prefix = strcspn(baseURL, "#");
	else if (relativeURL[0] == '?') prefix = strcspn(baseURL, "?#");
	else if (!relativeURL[0]) prefix = strcspn(baseURL, "#");
	else
	{
		const char* end = baseURL + strcspn(baseURL, "?#");
		const char* last = end;
		while (last > basePath && last[-1] != '/' && last[-1] != '\\') --last;
		prefix = last - baseURL;
		if (last == basePath && !local) slash = true;
	}

	size_t length = strlen(relativeURL);
	if (prefix + length + (slash ? 1 : 0) < MAX_URL_LENGTH)
	{
		memcpy(next.url, prefixSource, prefix);
		if (slash) next.url[prefix++] = '/';
		memcpy(next.url + prefix, relativeURL, length + 1);
		next.CleanUp();
	}
	result = next;
	return result;
}
