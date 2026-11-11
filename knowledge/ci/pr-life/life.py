# Life of a pull request from GitHub's REST records (#3082). Inputs cached by the shell steps in the ticket.
import json,re,glob,os,sys,statistics as st
from datetime import datetime as D, timedelta
S='.'  # run from the data folder
def t(s): return D.fromisoformat(s.replace('Z','+00:00')) if s else None
def h(a,b): return (b-a).total_seconds()/3600 if a and b else None
prs=json.load(open(f'{S}/prs.json'))
runs=[json.loads(l) for f in glob.glob(f'{S}/cache/runs2/*.json') for l in open(f) if l.strip()]
byb={}
for r in runs:
    if r['event'] in('pull_request','workflow_dispatch'): byb.setdefault(r['head_branch'],[]).append(r)
iss={}
for f in glob.glob(f'{S}/cache/iss/*.json'):
    try: j=json.load(open(f)); iss[j['number']]=j
    except: pass
rows=[]
for p in prs:
    n=p['number']; tl=json.load(open(f'{S}/cache/tl/{n}.json'))
    com=[e for e in tl if e['event']=='committed']
    fc=min((t(e['committer']['date']) for e in com),default=None)
    opened=t(p['created_at']); merged=t(p['merged_at']); closed=t(p['closed_at'])
    q=[t(e['created_at']) for e in tl if e['event']=='added_to_merge_queue']
    rq=[t(e['created_at']) for e in tl if e['event']=='removed_from_merge_queue']
    am=[t(e['created_at']) for e in tl if e['event']=='auto_merge_enabled']
    br=p['head']['ref']
    rs=sorted([r for r in byb.get(br,[]) if t(r['created_at'])>=opened-timedelta(minutes=5) and (not closed or t(r['created_at'])<=closed)],key=lambda r:r['created_at'])
    shas=[]
    for r in rs:
        if r['head_sha'] not in shas: shas.append(r['head_sha'])
    green=[t(r['updated_at']) for r in rs if r['conclusion']=='success']
    red=[r for r in rs if r['conclusion']=='failure']
    tk=None
    m=re.search(r'(?i)\b(?:closes|fixes|resolves|part of)\s+#(\d+)',p.get('body') or '')
    if m and int(m.group(1)) in iss: tk=t(iss[int(m.group(1))]['created_at'])
    ejected=max(0,len(rq)-(1 if merged else 0))
    rows.append(dict(n=n,opened=opened,merged=merged,closed=closed,fc=fc,tk=tk,firstq=min(q) if q else None,am=min(am) if am else None,
      green=min(green) if green else None,nq=len(q),eject=ejected,pushes=len(shas),checks=len(rs),red=len(red),redids=[r['id'] for r in red],
      ncommits=len(com),files=None,user=p['user']['login'],title=p['title'],branch=br,draft=p.get('draft')))
json.dump(rows,open(f'{S}/rows.json','w'),default=str)
DAYS=sorted({r['merged'].strftime('%m-%d') for r in rows if r['merged']})
def q(xs,f):
    xs=sorted(x for x in xs if x is not None)
    if not xs: return '-'
    return xs[min(len(xs)-1,int(f*len(xs)))]
def fmt(x): return '-' if x=='-' else (f'{x:.1f}' if isinstance(x,float) else str(x))
print('| day merged | merged | ticket→1st commit | 1st commit→open | open→1st green | green→queued | 1st queued→merged | 1st commit→merged (p90) | pushes med (p90) | thrown out of queue (PRs) | red PR checks /PR |')
print('|'+'---|'*11)
alld={}
for day in DAYS:
    R=[r for r in rows if r['merged'] and r['merged'].strftime('%m-%d')==day]
    alld[day]=R
    if not R: continue
    A=lambda k1,k2:[h(r[k1],r[k2]) for r in R]
    life=A('fc','merged')
    print(f"| {day} | {len(R)} | {fmt(q(A('tk','fc'),.5))} | {fmt(q(A('fc','opened'),.5))} | {fmt(q(A('opened','green'),.5))} | {fmt(q([h(r['green'],r['firstq']) for r in R if r['green'] and r['firstq'] and r['firstq']>r['green']],.5))} | {fmt(q(A('firstq','merged'),.5))} | {fmt(q(life,.5))} ({fmt(q(life,.9))}) | {q([r['pushes'] for r in R],.5)} ({q([r['pushes'] for r in R],.9)}) | {sum(1 for r in R if r['eject'])} ({sum(r['eject'] for r in R)} times) | {sum(r['red'] for r in R)/len(R):.2f} |")
# share of waiting
for lab,days in [('11-03..05',['11-03','11-04','11-05']),('11-06..10',['11-06','11-07','11-08','11-09','11-10'])]:
    R=[r for d in days for r in alld.get(d,[]) if r['fc'] and r['opened']]
    if not R: continue
    tot=sum(h(r['fc'],r['merged']) for r in R); wait=sum(h(max(r['opened'],r['fc']),r['merged']) for r in R)
    med=st.median([h(max(r['opened'],r['fc']),r['merged'])/max(h(r['fc'],r['merged']),1e-6) for r in R])
    print(f'{lab}: {len(R)} PRs, open→merged is {100*wait/tot:.0f}% of summed 1st commit→merged hours; median per-PR share {100*med:.0f}%')
cl=[r for r in rows if r['closed'] and not r['merged'] and r['opened']>=t('2026-11-03T00:00:00Z')]
op=[r for r in rows if r['opened']>=t('2026-11-03T00:00:00Z')]
print('opened since 11-03',len(op),'closed unmerged',len(cl),'open now',sum(1 for r in op if not r['closed']))
