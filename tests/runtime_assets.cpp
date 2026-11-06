#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
static std::vector<std::string> paths;
static const char* wanted;
static FILE* ProbeOpen(const char* path, const char* mode) {
    assert(!strcmp(mode,"rb")); paths.push_back(path);
    return !strcmp(path,wanted) ? stdout : NULL;
}
#define fopen ProbeOpen
#include "../src/DataPack.cpp"
#undef fopen
int main() {
    wanted="CGA.DAT"; assert(OpenDataPack("CGA.DAT","C:\\APPS\\MICROWEB.EXE")==stdout); assert(paths.size()==1);
    paths.clear(); wanted="C:\\APPS\\CGA.DAT";
    assert(OpenDataPack("CGA.DAT","C:\\APPS\\MICROWEB.EXE")==stdout); assert(paths.size()==2);
    paths.clear(); wanted="/apps/EGA.DAT";
    assert(OpenDataPack("EGA.DAT","/apps/MicroWeb")==stdout); assert(paths.size()==2);
    paths.clear(); wanted="C:CGA.DAT";
    assert(OpenDataPack("CGA.DAT","C:MICROWEB.EXE")==stdout); assert(paths.size()==2);
    for (const char* path : {"D:\\X\\CGA.DAT","sub/CGA.DAT","sub\\CGA.DAT"}) {
        paths.clear(); assert(!OpenDataPack(path,"C:\\APP\\MW.EXE")); assert(paths.size()==1);
    }
    paths.clear(); assert(!OpenDataPack("CGA.DAT",NULL)); assert(paths.size()==1);
    paths.clear(); assert(!OpenDataPack("CGA.DAT","MW.EXE")); assert(paths.size()==1);
    std::string longPath(259,'X'); longPath += "/MW.EXE";
    paths.clear(); assert(!OpenDataPack("CGA.DAT",longPath.c_str())); assert(paths.size()==1);
    puts("PASS: local assets, executable fallback, DOS/POSIX separators, explicit paths, missing path and overflow bounds");
}
