import re, sys, os.path, subprocess, re
from distutils.spawn import find_executable

def AddToStat(stat, offset, name):
    offset = int(offset, 0)
    if name in stat:
        stat[name].append(offset)
    else:
        offsets = []
        offsets.append(offset)
        stat[name] = offsets

def Process(f):
    stat = dict()
    p = re.compile('^kind = ([^;]+);offset = ([^;]+);(\w+ = (.*);)?')
    sample_count = 0
    while True:
        sample_count += 1
        line = f.readline()
        if not line:
            break
        m = p.match(line)
        if not m:
            continue
        AddToStat(stat, m.group(2), m.group(4))
    print('processed %d lines' % sample_count)

    return stat, sample_count

text_section_start_re = re.compile('.*\.text\s+\w+\s+([0-9a-fA-F]+).*')

def FindTextSectionStart(filename):
    global text_section_start_re
    sections = subprocess.check_output(['llvm-readelf', '--section-headers', filename]).decode('utf-8')
    for line in sections.split('\n'):
       m = text_section_start_re.match(line)
       if m:
           return int(m.group(1), 16)
    raise Exception('no .text section!')

line_number_re = re.compile('[^:]+:(\d+):\d+')
def ProcessSamples(stat):
    global line_number_re
    line_info = dict()
    for name, offsets in stat.items():
        if not name:
            continue
        filename = 'out/Release/' + name + '.o'
        if not os.path.isfile(filename):
            continue
        offsets.sort()
        text_section_start = FindTextSectionStart(filename)
        for i in range(len(offsets)):
            offsets[i] += text_section_start
        line_numbers = subprocess.check_output(['llvm-symbolizer', '-e', filename ] + [str(n) for n in offsets]).decode('utf-8')
        line_numbers_count_map = dict()
        sample_count = 0
        for line_number in line_numbers.split('\n'):
            m = line_number_re.match(line_number)
            if not m:
                continue
            n = int(m.group(1))
            sample_count += 1
            if n in line_numbers_count_map:
                line_numbers_count_map[n] += 1
            else:
                line_numbers_count_map[n] = 1
        line_numbers_count_map['sample_count'] = sample_count
        line_info[name] = line_numbers_count_map
    return line_info

def WriteProfFile(f, line_info):
    for funname, line_numbers_count_map in line_info.items():
        f.write('%s:%d\n' % (funname, line_numbers_count_map.pop('sample_count')))
        line_numbers = line_numbers_count_map.items()
        line_numbers = sorted(line_numbers, key = lambda item: item[0])
        for line, sample_count in line_numbers:
            f.write(' %d: %d\n'%(line, sample_count))

def main():
    if len(sys.argv) != 2:
        print >> sys.stderr, 'need a file name'
        sys.exit(1)
    if not find_executable('llvm-symbolizer'):
        print >>sys,stderr, 'need llvm-symbolizer in the PATH'
        sys.exit(1)
    if not find_executable('llvm-readelf'):
        print >>sys,stderr, 'need llvm-readelf in the PATH'
        sys.exit(1)

    with open(sys.argv[1], 'r') as f:
        stat, sample_count = Process(f)
    line_info = ProcessSamples(stat)
    with open('sample.prof', 'w') as f:
        WriteProfFile(f, line_info);

if __name__ == '__main__':
    main()
