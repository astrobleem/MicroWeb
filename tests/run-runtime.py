#!/usr/bin/env python3
"""Sanitizer tests of exact production runtime paths with mocked DOS/mTCP I/O."""
from pathlib import Path
import os
import re
import subprocess
root = Path(__file__).resolve().parent.parent
os.chdir(root)
build = root / 'build/tests'
build.mkdir(parents=True, exist_ok=True)
# The DOS wrapper has platform-only headers. Compile its unchanged body against
# the mocks in runtime_network.cpp; keep its real public header and HTTP types.
source = (root / 'src/DOS/DOSNet.cpp').read_text()
(build / 'runtime-dosnet.inc').write_text(re.sub(r'^#include[^\n]*\n', '', source, flags=re.M))
def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'
source = (root / 'lib/mTCP/TCPLIB/TCP.CPP').read_text()
(build / 'runtime-mtcp.inc').write_text('\n'.join(function(source, signature) for signature in (
    'int8_t TcpSocket::setRecvBuffer(', 'void TcpSocket::abortConnection(', 'void TcpSocket::destroy(')))
source = (root / 'src/DOS/Platform.cpp').read_text()
(build / 'runtime-platform.inc').write_text(re.sub(r'^#include[^\n]*\n', '', source, flags=re.M))
source = (root / 'lib/mTCP/TCPLIB/PACKET.CPP').read_text()
(build / 'runtime-packet.inc').write_text('\n'.join(function(source, signature) for signature in (
    'int8_t Buffer_init(', 'void Buffer_startReceiving(', 'void Buffer_stopReceiving(', 'void Buffer_stop(')))
source = (root / 'lib/mTCP/TCPLIB/UTILS.CPP').read_text()
# initStack has mutually exclusive #ifdef branches with duplicate braces.
(build / 'runtime-initstack.inc').write_text(source[source.index('int8_t Utils::initStack('):
    source.index('// Utils::endStack')] + function(source, 'void Utils::endStack2('))
source = (root / 'lib/mTCP/TCPLIB/TRACE.CPP').read_text()
(build / 'runtime-trace.inc').write_text('\n'.join(function(source, signature) for signature in (
    'void Trace_beginTracing(', 'void Trace_endTracing(')))
for name in ('runtime_assets','runtime_network','runtime_platform','runtime_packet','runtime_trace'):
    subprocess.run([os.environ.get('CXX','c++'), '-std=c++11', '-g', '-fsanitize=address,undefined',
        '-fno-sanitize=vptr', '-fno-omit-frame-pointer', '-ffunction-sections', '-fdata-sections',
        '-Wno-unknown-pragmas', '-Dstricmp=strcasecmp', '-include', 'strings.h',
        '-I'+str(build), 'tests/'+name+'.cpp', '-Wl,--gc-sections', '-o', str(build/name)], check=True)
    subprocess.run([str(build/name)], env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0',
        UBSAN_OPTIONS='halt_on_error=1'), check=True)
