#!/usr/bin/env python3
# Isolate the real form tag handlers and attribute decoder from video and page rendering.
from pathlib import Path
import os, subprocess
root = Path(__file__).resolve().parent.parent
os.chdir(root)
build = Path('build/tests'); build.mkdir(parents=True, exist_ok=True)
tags = Path('src/Tags.cpp').read_text()
a = tags.index('static const HTMLTagHandler* tagHandlers[]')
b = tags.index('\n\nconst HTMLTagHandler* DetermineTag', a)
tags = tags[:a] + tags[b:]
a = tags.index('const HTMLTagHandler* DetermineTag')
b = tags.index('\nvoid ', a)
tags = tags[:a] + tags[b:]
(build/'form-tags.cpp').write_text(tags)
p = Path('src/Parser.cpp').read_text()
headers = p[:p.index('HTMLParser::HTMLParser')]
entity = p[p.index('const char* HTMLParser::GetUnicodeString'):p.index('void HTMLParser::Write')]
attrs = p[p.index('char AttributeParser::attributeStringBuffer'):p.index('\n}', p.index('bool AttributeParser::IsWhiteSpace')) + 3]
node = Path('src/Node.cpp').read_text()
dimension = node[node.index('ExplicitDimension ExplicitDimension::Parse'):node.index('const ElementStyle& Node::GetStyle')]
(build/'form-attributes.cpp').write_text(headers + dimension + p[p.index('#define NUM_AMPERSAND'):p.index('};', p.index('#define NUM_AMPERSAND')) + 3] + entity + attrs)
cmd = [os.environ.get('CXX', 'c++'), '-std=c++11', '-g', '-fsanitize=address,undefined', '-fno-sanitize=alignment,vptr', '-fno-omit-frame-pointer', '-ffunction-sections', '-fdata-sections', '-Wno-unknown-pragmas', '-Dstricmp=strcasecmp', '-include', 'strings.h', '-Isrc', 'tests/forms_parser.cpp', str(build/'form-tags.cpp'), str(build/'form-attributes.cpp'), 'src/Nodes/Button.cpp', 'src/Nodes/Field.cpp', 'src/Nodes/Select.cpp', 'src/Nodes/CheckBox.cpp', 'src/Nodes/Break.cpp', 'src/Memory/LinAlloc.cpp', '-Wl,--gc-sections', '-o', str(build/'forms-parser')]
subprocess.run(cmd, check=True)
subprocess.run([str(build/'forms-parser')], check=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'))
