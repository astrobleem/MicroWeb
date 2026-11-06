#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/App.h"

NetworkDriver* Platform::network = NULL;
size_t HTTPRequest::ReadData(char*, size_t) { assert(false); return 0; }
void HTTPRequest::Stop() { assert(false); }

int main()
{
    for (int size = 0; size < 20; ++size)
    {
        LoadTask task;
        task.fs = tmpfile(); assert(task.fs);
        for (int n = 0; n < size; ++n) fputc('a' + n, task.fs);
        rewind(task.fs);
        char buffer[4]; unsigned total = 0;
        while (task.HasContent()) total += (unsigned)task.GetContent(buffer, sizeof(buffer));
        assert(total == (unsigned)size);
        // A caller may ask again inside its same timeslice after the final
        // short read. That must close the stream and clear its owning pointer.
        assert(task.GetContent(buffer, sizeof(buffer)) == 0);
        assert(task.fs == NULL);
        FILE* other = tmpfile(); assert(other);
        assert(!task.HasContent() && !task.IsBusy());
        assert(task.GetContent(buffer, sizeof(buffer)) == 0);
        task.Stop(); task.Stop();
        fputs("another live stream", other);
        assert(!ferror(other)); fclose(other);
    }
    puts("PASS: 20 production local-file EOF/stream-reuse cases; repeat reads and Stop remain safe");
}
