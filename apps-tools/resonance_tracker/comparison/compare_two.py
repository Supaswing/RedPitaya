import os
os.makedirs("output", exist_ok=True)
import runpy,numpy as np,pandas as pd,matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
v=runpy.run_path('analyze.py')
f=v['f']; methods=v['methods']; df=pd.read_csv('output/results.csv')
pb,ps=methods['Raw IQ projection'];mb,ms=methods['Optimized Mobius']
for name, b,s in [('projection',pb,ps),('mobius',mb,ms)]:
 df[name+'_belt']=b;df[name+'_sensor']=s;df[name+'_delta']=s-b
 coef=np.polyfit(f-35,b,2);trend=np.polyval(coef,f-35)
 df[name+'_belt_detrended']=b-trend;df[name+'_sensor_detrended']=s-trend
fig,axs=plt.subplots(2,2,figsize=(13,8),sharex=True)
for row,(name,b,s) in enumerate([('Projection complexe PCA',pb,ps),('Mobius optimisee',mb,ms)]):
 coef=np.polyfit(f-35,b,2);trend=np.polyval(coef,f-35)
 axs[row,0].plot(f,b-trend,label='Belt seul',lw=1.6)
 axs[row,0].plot(f,s-trend,label='Belt + 2 capteurs',lw=1.6)
 axs[row,1].plot(f,s-b,label='2 capteurs - belt seul',lw=1.7)
 for ax in axs[row]:
  ax.axvline(34,color='gray',ls=':',alpha=.8);ax.axvline(35.4,color='gray',ls=':',alpha=.8)
  ax.grid(alpha=.2);ax.set_ylabel('Unites arbitraires')
 axs[row,0].set_title(name+' : meme detrending belt seul')
 axs[row,1].set_title(name+' : difference capteurs - belt')
 axs[row,0].legend(loc='best',fontsize=9)
for ax in axs[-1]:ax.set_xlabel('Frequence (MHz)')
fig.tight_layout();fig.savefig('output/projection_vs_mobius.png',dpi=160)
df.to_csv('output/results_extended.csv',index=False)
print('Saved',len(df),'rows',list(df.columns)[-10:])
