import os
os.makedirs("output", exist_ok=True)
import zipfile,io,numpy as np,pandas as pd
from scipy.optimize import least_squares
import matplotlib;matplotlib.use('Agg')
import matplotlib.pyplot as plt
z=zipfile.ZipFile('raw_iq_sweeps.zip'); names={'belt':'no_sensor','sensor':'2sensor','open':'open','short':'short','load':'50ohm'}
d={}
for k,n in names.items():
 x=pd.read_csv(io.BytesIO(z.read(f'raw_iq_sweep_{n}/raw.csv')));d[k]=x
 print(k,len(x),x.valid.sum(),x.requested_hz.iloc[0],x.requested_hz.iloc[-1])
f=d['belt'].requested_hz.to_numpy()/1e6
r={k:(v.ref_i.to_numpy()+1j*v.ref_q.to_numpy())/(v.inc_i.to_numpy()+1j*v.inc_q.to_numpy()) for k,v in d.items()}
# map OSL ratio to impedance via cross ratio: gamma = (r-L)*(O-S)/... etc
# Möbius gamma(r): gamma(L)=0, gamma(O)=1, gamma(S)=-1
O,S,L=r['open'],r['short'],r['load']
# gamma = k*(r-L)/(r-p); solve pole using O,S
# (O-L)/(O-p) / ((S-L)/(S-p))=-1 => (O-L)*(S-p)+(S-L)*(O-p)=0
p=((O-L)*S+(S-L)*O)/(O+S-2*L)
k=(O-p)/(O-L)
def gamma(x):return k*(x-L)/(x-p)
def impedance(x):
 g=gamma(x);return 50*(1+g)/(1-g)
Zb,Zs=impedance(r['belt']),impedance(r['sensor'])
print('Zbelt',np.real(Zb).min(),np.real(Zb).max(),'delta',np.real(Zs-Zb).min(),np.real(Zs-Zb).max())
# fit linear projection via PCA; belt only centered
x=r['belt'];cov=np.cov(np.stack([x.real,x.imag]));eig,vec=np.linalg.eigh(cov);v=vec[:,0];theta=np.arctan2(-v[1],v[0]);print('PCA theta',theta,'eig',eig)
# optimized Mobius: normalized map T(x)=(x-r0)/(1+q*(x-r0)) ; q complex, T'(r0)=1; choose rotation by PCA for each candidate minimizing real background variance; pole constraint
r0=np.mean(r['belt']); scale=np.std(r['belt']); xb=(r['belt']-r0)/scale; xs=(r['sensor']-r0)/scale
# optimize q complex; projection angle is analytical eigenvector
# cost objective normalized real variance + regularization and pole penalty; prevent trivial near-constant via derivative sensitivity floor and output range cap
# parameter q bounded to .7, |q*x| < .65 typical
maxx=max(np.max(np.abs(xb)),np.max(np.abs(xs)))
def trans(x,q):return x/(1+q*x)
def evaluate(q):
 y=trans(xb,q); C=np.cov(np.stack([y.real,y.imag])); ev,evc=np.linalg.eigh(C); w=evc[:,0]; t=w[0]-1j*w[1];return ev[0],t,y
# belt only fit, penalize q norm and denominator proximity
from scipy.optimize import minimize
def obj(qr):
 q=qr[0]+1j*qr[1];den=1+q*xb
 if np.min(np.abs(den))<.45:return 1e4+(0.45-np.min(np.abs(den)))*1e4
 ev,t,y=evaluate(q);return ev/np.var(xb.real+1j*xb.imag)+.025*abs(q)**2
res=minimize(obj,[0,0],method='Nelder-Mead',options={'maxiter':4000,'xatol':1e-10});q=res.x[0]+1j*res.x[1];ev,t,y=evaluate(q);print('opt',q,ev,res.fun,'den min',np.min(abs(1+q*xb)))
# baseline residual via polynomial detrending separately (fit to belt only) for visual sensor comparison; apply same belt polynomial to sensor, show differential (sensor-belt) and belt residual
from numpy.polynomial.polynomial import polyfit,polyval
methods={
 'Raw IQ projection':(np.real(np.exp(1j*theta)*r['belt']),np.real(np.exp(1j*theta)*r['sensor'])),
 'OSL Re(Z)':(Zb.real,Zs.real),
 'OSL rotated Z':(None,None),
 'Optimized Mobius':(np.real(t*trans(xb,q)),np.real(t*trans(xs,q)))
}
C=np.cov(np.stack([Zb.real,Zb.imag]));ev,V=np.linalg.eigh(C);rot=V[0,0]-1j*V[1,0];methods['OSL rotated Z']=(np.real(rot*Zb),np.real(rot*Zs))
fig,axes=plt.subplots(4,2,figsize=(12,12),sharex=True)
for axrow,(name,(b,s)) in zip(axes,methods.items()):
 xx=f-35;coef=np.polyfit(xx,b,2);trend=np.polyval(coef,xx);resid=b-trend;delt=s-b
 axrow[0].plot(f,resid,label='Belt only detrended');axrow[0].plot(f,s-trend,label='2 sensors, same belt trend',alpha=.8)
 axrow[1].plot(f,delt,label='2 sensors - belt only');
 for ax in axrow:
  for ff in (34,35.4):ax.axvline(ff,ls=':',color='gray')
  ax.grid(alpha=.2);ax.set_xlim(33,37)
 axrow[0].set_title(name+' : belt polynomial detrending');axrow[1].set_title(name+' : sensor-induced difference')
 print(name,'belt residual RMS',np.std(resid),'sensor delta RMS',np.std(delt),'at34',np.interp(34,f,delt),'at35.4',np.interp(35.4,f,delt))
axes[0,0].legend(fontsize=8);axes[0,1].legend(fontsize=8)
fig.tight_layout();fig.savefig('output/comparison.png',dpi=160)
# more physically relevant difference for OSL with linear detrend delta, peaks
fig,ax=plt.subplots(figsize=(10,4)); delta=(Zs-Zb).real
for deg in [0,1,2]:
 detr=delta-np.polyval(np.polyfit(f-35,delta,deg),f-35);ax.plot(f,detr,label=f'delta Re(Z) remove degree {deg}')
for ff in [34,35.4]:ax.axvline(ff,color='gray',ls=':')
ax.grid(alpha=.2);ax.legend();ax.set_xlabel('MHz');ax.set_ylabel('Ohm');fig.tight_layout();fig.savefig('output/deltaZ.png',dpi=160)
pd.DataFrame({'freq_MHz':f,'raw_ratio_belt_re':r['belt'].real,'raw_ratio_belt_im':r['belt'].imag,'raw_ratio_sensor_re':r['sensor'].real,'raw_ratio_sensor_im':r['sensor'].imag,'Z_belt_re':Zb.real,'Z_belt_im':Zb.imag,'Z_sensor_re':Zs.real,'Z_sensor_im':Zs.imag,'delta_R':delta,'pseudo_belt':methods['Optimized Mobius'][0],'pseudo_sensor':methods['Optimized Mobius'][1]}).to_csv('output/results.csv',index=False)
