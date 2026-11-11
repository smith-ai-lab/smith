# Claim (label `claimed` on the ticket) to PR opened to merged (#3082).
import json,re,glob,os,statistics as st
from datetime import datetime as D
S='.'  # run from the data folder
def t(s): return D.fromisoformat(s.replace('Z','+00:00')) if s else None
def h(a,b): return (b-a).total_seconds()/3600 if a and b else None
rows=json.load(open(f'{S}/rows.json'))
prs={p['number']:p for p in json.load(open(f'{S}/prs.json'))}
ev={int(os.path.basename(f)[:-5]):json.load(open(f)) for f in glob.glob(f'{S}/cache/iev/*.json') if os.path.getsize(f)}
def q(xs,f):
    xs=sorted(x for x in xs if x is not None); return xs[min(len(xs)-1,int(f*len(xs)))] if xs else None
out=[]
for r in rows:
    for k in ('opened','merged','closed','fc','firstq','green','am'): r[k]=t(r[k]) if r[k] not in (None,'None') else None
    m=re.search(r'(?i)\b(?:closes|fixes|resolves|part of)\s+#(\d+)',prs[r['n']].get('body') or '')
    r['claim']=None
    if m and int(m.group(1)) in ev:
        cl=[t(e['created_at']) for e in ev[int(m.group(1))] if e['label']=='claimed' and e['event']=='labeled' and t(e['created_at'])<=r['opened']]
        if cl: r['claim']=max(cl)   # the latest claim before this PR opened
    out.append(r)
DAYS=sorted({r['merged'].strftime('%m-%d') for r in out if r['merged']})
print('| day merged | n | with claim | claim→PR opened med (p90) | PR opened→merged med (p90) | claim→merged med (p90) | waiting share of claim→merged (sum) |')
print('|---|---|---|---|---|---|---|')
for day in DAYS:
    R=[r for r in out if r['merged'] and r['merged'].strftime('%m-%d')==day]
    C=[r for r in R if r['claim'] and h(r['claim'],r['opened'])<48]
    a=[h(r['claim'],r['opened']) for r in C]; b=[h(r['opened'],r['merged']) for r in R]; c=[h(r['claim'],r['merged']) for r in C]
    share=sum(h(r['opened'],r['merged']) for r in C)/max(1e-9,sum(c))
    f=lambda x:f'{x:.2f}' if x is not None else '-'
    print(f'| {day} | {len(R)} | {len(C)} | {f(q(a,.5))} ({f(q(a,.9))}) | {f(q(b,.5))} ({f(q(b,.9))}) | {f(q(c,.5))} ({f(q(c,.9))}) | {100*share:.0f}% |')
json.dump(out,open(f'{S}/rows2.json','w'),default=str)
