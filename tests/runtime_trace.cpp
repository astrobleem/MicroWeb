#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
static FILE* Trace_Stream=stderr;
static char* Trace_LogFile;
#include "runtime-trace.inc"
int main() {
    int errorDescriptor=fileno(stderr);
    Trace_beginTracing(); Trace_endTracing(); Trace_endTracing();
    assert(Trace_Stream==stderr && fcntl(errorDescriptor,F_GETFD)!=-1);
    char path[]="/tmp/microweb-trace-XXXXXX";
    int descriptor=mkstemp(path); assert(descriptor>=0); close(descriptor);
    Trace_LogFile=path;
    for (unsigned n=0;n<2;++n) {
        Trace_beginTracing(); assert(Trace_Stream!=stderr);
        fputs("trace\n",Trace_Stream);
        Trace_endTracing(); Trace_endTracing();
        assert(Trace_Stream==stderr && fcntl(errorDescriptor,F_GETFD)!=-1);
    }
    FILE* file=fopen(path,"rb"); assert(file); char contents[16]={0};
    assert(fread(contents,1,sizeof(contents)-1,file)==12 && !strcmp(contents,"trace\ntrace\n"));
    fclose(file); remove(path);
    puts("PASS: trace cleanup closes only owned files, preserves stderr, and supports repeated start/stop");
}
