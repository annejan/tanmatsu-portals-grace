# Apply the data fixes to a chamber file: K under floor button 3, a fizzler in the dock gap.
import sys
src, dst = sys.argv[1], sys.argv[2]
lines = open(src).read().split('\n')
D = 21
def setc(x, y, z, ch):
    i = lines.index('layer %d' % y) + 1 + (D - 1 - z)
    row = lines[i]; lines[i] = row[:x] + ch + row[x+1:]
fixes = sys.argv[3] if len(sys.argv) > 3 else 'AC'
if 'A' in fixes: setc(6, 1, 6, 'K')
if 'C' in fixes:
    for y in (2, 3):
        for x in (1, 2): setc(x, y, 9, 'F')
open(dst, 'w').write('\n'.join(lines))
