# Pure-python stats: mean, stddev, 95% CI, Welch's t-test + t-dist p-value,
# paired t-test, Cohen's d. No scipy dependency.
import math

def _betacf(a,b,x):
    MAXIT=200; EPS=3e-12; FPMIN=1e-300
    qab=a+b; qap=a+1; qam=a-1
    c=1.0; d=1.0-qab*x/qap
    if abs(d)<FPMIN: d=FPMIN
    d=1.0/d; h=d
    for m in range(1,MAXIT+1):
        m2=2*m
        aa=m*(b-m)*x/((qam+m2)*(a+m2))
        d=1.0+aa*d
        if abs(d)<FPMIN: d=FPMIN
        c=1.0+aa/c
        if abs(c)<FPMIN: c=FPMIN
        d=1.0/d; h*=d*c
        aa=-(a+m)*(qab+m)*x/((a+m2)*(qap+m2))
        d=1.0+aa*d
        if abs(d)<FPMIN: d=FPMIN
        c=1.0+aa/c
        if abs(c)<FPMIN: c=FPMIN
        d=1.0/d; de=d*c; h*=de
        if abs(de-1.0)<EPS: break
    return h

def betai(a,b,x):
    if x<=0: return 0.0
    if x>=1: return 1.0
    lbeta=math.lgamma(a+b)-math.lgamma(a)-math.lgamma(b)
    bt=math.exp(lbeta+a*math.log(x)+b*math.log(1-x))
    if x<(a+1)/(a+b+2):
        return bt*_betacf(a,b,x)/a
    else:
        return 1.0-bt*_betacf(b,a,1-x)/b

def t_sf(t,df):
    # two-sided p-value for t statistic
    x=df/(df+t*t)
    return betai(df/2.0,0.5,x)  # two-sided already (symmetric use)

def desc(xs):
    n=len(xs); m=sum(xs)/n
    v=sum((x-m)**2 for x in xs)/(n-1)
    sd=math.sqrt(v)
    se=sd/math.sqrt(n)
    ci=1.96*se
    return dict(n=n,mean=m,sd=sd,se=se,ci95=ci)

def welch(a,b):
    da,db=desc(a),desc(b)
    t=(da['mean']-db['mean'])/math.sqrt(da['se']**2+db['se']**2)
    num=(da['se']**2+db['se']**2)**2
    den=(da['se']**4/(da['n']-1))+(db['se']**4/(db['n']-1))
    df=num/den
    p=t_sf(abs(t),df)
    # pooled sd for Cohen's d
    sp=math.sqrt(((da['n']-1)*da['sd']**2+(db['n']-1)*db['sd']**2)/(da['n']+db['n']-2))
    d=(db['mean']-da['mean'])/sp
    return dict(t=t,df=df,p=p,cohend=d)

def paired(a,b):
    # a,b paired lists; test mean of diffs != 0
    diffs=[y-x for x,y in zip(a,b)]
    dd=desc(diffs)
    t=dd['mean']/dd['se']
    df=dd['n']-1
    p=t_sf(abs(t),df)
    return dict(t=t,df=df,p=p,mean_diff=dd['mean'],ci95=dd['ci95'])
