# Ejections, red-check round trips, sizes and file overlap (#3082).
import json,glob,os,re,collections as C,statistics as st
from datetime import datetime as D, timedelta
def t(s): return D.fromisoformat(s.replace('Z','+00:00')) if s and s!='None' else None
rows=json.load(open('rows2.json'))
for r in rows:
    for k in ('opened','merged','closed','fc','firstq','green','am','claim'): r[k]=t(r[k])
M=[r for r in rows if r['merged'] and r['merged']>=t('2026-11-03T00:00:00Z')]
h=lambda a,b:(b-a).total_seconds()/3600
def med(x): x=sorted(x); return x[len(x)//2] if x else None
def p90(x): x=sorted(x); return x[int(.9*len(x))] if x else None
e=[h(r['firstq'],r['merged']) for r in M if r['eject'] and r['firstq']]; ne=[h(r['firstq'],r['merged']) for r in M if not r['eject'] and r['firstq']]
print(f'queued→merged: thrown out at least once {len(e)} PRs med {med(e):.2f} h p90 {p90(e):.2f}; never thrown out {len(ne)} med {med(ne):.2f} p90 {p90(ne):.2f}')
print(f'hours queued→merged summed: ejected {sum(e):.0f}, not {sum(ne):.0f}; excess over the non-ejected median: {sum(x-med(ne) for x in e):.0f} h')
print('| day merged | removed PRs | queued→merged med (p90) | never removed | queued→merged med (p90) |'); print('|---|---|---|---|---|')
for day in sorted({r['merged'].strftime('%m-%d') for r in M}):
    R=[r for r in M if r['merged'].strftime('%m-%d')==day and r['firstq']]
    a=[h(r['firstq'],r['merged']) for r in R if r['eject']]; b=[h(r['firstq'],r['merged']) for r in R if not r['eject']]
    f=lambda x:f'{med(x):.2f} ({p90(x):.2f})' if x else '-'
    print(f'| {day} | {len(a)} | {f(a)} | {len(b)} | {f(b)} |')
rd=[r for r in M if r['red']]; nr=[r for r in M if not r['red']]
f=lambda R:[h(r['opened'],r['merged']) for r in R]
print(f'opened→merged: PRs with a red PR check {len(rd)} med {med(f(rd)):.2f} h; without {len(nr)} med {med(f(nr)):.2f} h')
# sizes by day
files={int(os.path.basename(x)[:-5]):json.load(open(x)) for x in glob.glob('cache/files/*.json') if os.path.getsize(x)}
DAYS=sorted({r['merged'].strftime('%m-%d') for r in M})
print('| day | merged | files med (p90) | lines med (p90) |'); print('|---|---|---|---|')
for day in DAYS:
    R=[r for r in M if r['merged'].strftime('%m-%d')==day and r['n'] in files]
    fs=[len(files[r['n']]) for r in R]; ls=[sum(x['additions']+x['deletions'] for x in files[r['n']]) for r in R]
    print(f'| {day} | {len(R)} | {med(fs)} ({p90(fs)}) | {med(ls)} ({p90(ls)}) |')
# overlap: merged PR shares a file with another PR open at the same time
iv=[(r['n'],r['opened'],r['merged'] or r['closed'] or t('2026-11-11T04:00:00Z'),set(x['filename'] for x in files.get(r['n'],[]))) for r in rows]
hot=C.Counter(); ov=0
for r in M:
    a=next(x for x in iv if x[0]==r['n'])
    shared=set()
    for b in iv:
        if b[0]!=a[0] and b[1]<a[2] and a[1]<b[2]: shared|=(a[3]&b[3])
    if shared: ov+=1; hot.update(shared)
print(f'merged PRs sharing a file with a PR open at the same time: {ov}/{len(M)} ({100*ov/len(M):.0f}%)')
print('most shared:',hot.most_common(12))
# merges of main into a PR branch (commit message "Merge ... main")
mm=0;mmpr=0
for r in M:
    tl=json.load(open(f'cache/tl/{r["n"]}.json'))
    k=sum(1 for e in tl if e['event']=='committed' and re.match(r"Merge (remote-tracking )?branch '?(origin/)?main",e.get('message','')))
    mm+=k; mmpr+=bool(k)
print(f'merge-main commits: {mm} in {mmpr} merged PRs')
cl=[r for r in rows if r['closed'] and not r['merged'] and r['opened']>=t('2026-11-03T00:00:00Z')]
print('closed unmerged', len(cl), 'lines', sum(sum(x['additions']+x['deletions'] for x in files.get(r['n'],[])) for r in cl), [r['n'] for r in cl])
