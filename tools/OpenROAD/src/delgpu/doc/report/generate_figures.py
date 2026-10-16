#!/usr/bin/env python3
"""Generate code-native report figures and snapshot the measured evidence."""
from pathlib import Path
import csv, json, os, re, shutil
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle, Circle
from matplotlib.font_manager import FontProperties, fontManager

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[5]
ASSETS=HERE/'assets'; DATA=HERE/'data'
ASSETS.mkdir(exist_ok=True); DATA.mkdir(exist_ok=True)
FONT=os.environ.get('DELGPU_REPORT_CJK_FONT','/mnt/c/Windows/Fonts/msyh.ttc')
fontManager.addfont(FONT)
plt.rcParams.update({'font.family':FontProperties(fname=FONT).get_name(),
 'font.size':12,'axes.unicode_minus':False,'axes.spines.top':False,
 'axes.spines.right':False,'savefig.facecolor':'white','svg.fonttype':'path'})
BLUE='#2563eb'; TEAL='#0d9488'; ORANGE='#ea580c'; DARK='#183153'; MUTED='#64748b'

def save(fig,name):
 fig.savefig(ASSETS/f'{name}.png',dpi=200,bbox_inches='tight',pad_inches=.16)
 fig.savefig(ASSETS/f'{name}.svg',bbox_inches='tight',pad_inches=.16)
 plt.close(fig)
def box(ax,x,y,w,h,text,color=BLUE,fs=12):
 ax.add_patch(FancyBboxPatch((x,y),w,h,boxstyle='round,pad=0.02,rounding_size=.035',
                           facecolor=color+'13',edgecolor=color,lw=1.6))
 ax.text(x+w/2,y+h/2,text,ha='center',va='center',color=DARK,fontsize=fs)
def arrow(ax,a,b,color=MUTED):
 ax.annotate('',b,a,arrowprops={'arrowstyle':'->','lw':1.5,'color':color})

# Freeze the exact input measurements used by every plot.
base=ROOT/'flow/reports/asap7/ibex/base'
for src,dst in [('delgpu_retraction/summary.json','retraction_summary.json'),
                ('delgpu_postprocess/summary.json','postprocess_summary.json'),
                ('delgpu_retraction/cooperative_1.csv','cooperative_1.csv'),
                ('delgpu_retraction/single_1.csv','single_1.csv')]:
 shutil.copyfile(base/src, DATA/dst)
rows=list(csv.DictReader((DATA/'cooperative_1.csv').open()))
metrics={'date':'2026-10-15','design':'ASAP7 / IBEX','nodes':332664,'arcs':437360,
 'dbu_per_micron':1000,'graph_cpu_median_ms':719.708,'graph_gpu_median_ms':447.944,
 'stages':{},'retraction':json.loads((DATA/'retraction_summary.json').read_text())}
for stage in ['initial','continuous','quantized','repaired','retracted']:
 rs=[r for r in rows if r['stage']==stage]
 metrics['stages'][stage]={'conflicts':sum(int(r['conflicts']) for r in rs),
 'ge5nm':sum(int(r['remaining_5nm']) for r in rs),
 'lt5nm':sum(int(r['violations_lt5nm']) for r in rs),
 'extent_um':sum(float(r['extent_dbu']) for r in rs)/1000}
(DATA/'metrics.json').write_text(json.dumps(metrics,ensure_ascii=False,indent=2)+'\n')

fig,ax=plt.subplots(figsize=(12.4,4.1));ax.set(xlim=(0,12.5),ylim=(0,4.2));ax.axis('off')
items=[('OpenDB\n普通布线 / via',BLUE),('CPU 几何\n金属并集 → trim',BLUE),
       ('GPU 建图\n移动域 + A/B/C 边',TEAL),('GPU 连续迭代\n动态学习率 + 跳跃',TEAL),
       ('整数化\n精确 DBU 位移',TEAL),('GPU 冲突修复\n单端点 + 双端点',TEAL),
       ('GPU 安全回缩\n单点 + 整组 + 平移',TEAL),('CPU 精确检查\nCSV / 可选 GUI',ORANGE)]
for i,(t,c) in enumerate(items):
 row,col=divmod(i,4); x=.12+col*3.12;y=2.6 if row==0 else .65
 box(ax,x,y,2.85,1.02,t,c,12)
 if col:arrow(ax,(x-.27,y+.51),(x-.03,y+.51))
arrow(ax,(11,2.55),(11,1.9));arrow(ax,(11,1.9),(1.55,1.9));arrow(ax,(1.55,1.9),(1.55,1.7))
ax.text(.12,4.05,'CPU：几何、索引、控制与验证      GPU：高并行计算      GUI：可完全关闭',color=MUTED,fontsize=11)
save(fig,'01_pipeline')

fig,axes=plt.subplots(2,1,figsize=(11.5,5.3))
for ax in axes:
 ax.set(xlim=(-2,7),ylim=(-1.6,2.1));ax.set_yticks([]);ax.spines[['left','right','top']].set_visible(False)
 ax.set_xlabel('沿布线方向的位置（示意，W=1）')
 ax.add_patch(Rectangle((-2,-.5),2,1,facecolor=BLUE,alpha=.7));ax.text(-1,0,'原有金属',ha='center',va='center',color='white')
 ax.add_patch(Rectangle((0,-1),1,2,facecolor='#facc15',alpha=.6,edgecolor=ORANGE))
 ax.plot(.5,0,'x',color=DARK,ms=9);ax.text(.5,1.2,'trim：长 W，宽 2W',ha='center',color=DARK)
axes[0].add_patch(Rectangle((5,-.6),1,1.2,facecolor=MUTED,alpha=.8))
axes[0].text(5.5,1,'前方障碍',ha='center',color=DARK)
axes[0].annotate('',(4.5,-1.22),(.5,-1.22),arrowprops={'arrowstyle':'<->','lw':2,'color':TEAL})
axes[0].text(2.6,-1.05,'中心可移动区间 [0.5, 4.5]',ha='center',color=TEAL)
axes[0].set_title('边界接触允许；最近障碍决定停止位置',loc='left',fontweight='bold')
axes[1].add_patch(Rectangle((.65,-.6),1.2,1.2,facecolor=ORANGE,alpha=.7))
axes[1].text(3.6,0,'初态内部重叠 → blocked → 位移固定为 0',color=ORANGE,ha='center')
axes[1].set_title('横向不重叠的金属不阻挡；内部重叠则禁止移动',loc='left',fontweight='bold')
fig.tight_layout(h_pad=2.0);save(fig,'02_trim_domain')

fig,ax=plt.subplots(figsize=(11.5,3.5));ax.set(xlim=(-7,9),ylim=(-.5,2.7));ax.set_yticks([2,1,0],['A：必须分离','B：合并或分离','C：对齐或分离']);ax.set_xlabel('有向中心距离 d / W')
for y in [0,1,2]:ax.axhline(y,color='#cbd5e1',lw=2,zorder=0)
ax.plot([5,8.8],[2,2],color=TEAL,lw=9,solid_capstyle='butt')
ax.plot([5,8.8],[1,1],color=TEAL,lw=9,solid_capstyle='butt');ax.plot(0,1,'o',color=TEAL,ms=11)
ax.plot([-6.9,-3],[0,0],color=TEAL,lw=9,solid_capstyle='butt');ax.plot([3,8.8],[0,0],color=TEAL,lw=9,solid_capstyle='butt');ax.plot(0,0,'o',color=TEAL,ms=11)
for x,y,t in [(5,2,'D=5W'),(5,1,'Dt=5W'),(-3,0,'−Du'),(3,0,'Du=3W'),(0,1,'合并点 0')]:ax.text(x,y+.18,t,ha='center',fontsize=10,color=DARK)
ax.set_xticks([-6,-3,0,3,5,8]);ax.grid(axis='x',alpha=.2);fig.tight_layout();save(fig,'03_legal_intervals')

fig,axes=plt.subplots(1,3,figsize=(12,3.1))
a=np.linspace(3.5,6,400);axes[0].plot(a,np.minimum(0,a-5)**2,color=BLUE,lw=2.4);axes[0].set_title('A：分离不足的二次惩罚')
b=np.linspace(0,5,400);axes[1].plot(b,16*(b/5)**2*(1-b/5)**2,color=TEAL,lw=2.4);axes[1].axvline(2.5,ls='--',color=ORANGE);axes[1].set_title('B：合并与分离之间的势垒')
c=np.linspace(-3,3,400);t=np.abs(c)/3;axes[2].plot(c,16*t*t*(1-t)**2,color=TEAL,lw=2.4);axes[2].axvline(-1.5,ls='--',color=ORANGE);axes[2].axvline(1.5,ls='--',color=ORANGE);axes[2].set_title('C：左右分支均可跳跃')
for ax in axes:ax.set_xlabel('d / W');ax.set_ylabel('规则势能');ax.grid(alpha=.18)
fig.tight_layout();save(fig,'04_potentials')

fig,axes=plt.subplots(1,2,figsize=(11.8,3.4))
ax=axes[0];ax.axis('off');ax.set(xlim=(-.5,3.5),ylim=(-.8,1.3))
for i in range(3):ax.plot([i,i+1],[.4,.4],color=MUTED,lw=2)
for i in range(4):
 color=BLUE if i%2==0 else ORANGE;ax.add_patch(Circle((i,.4),.19,facecolor=color));ax.text(i,.4,'ABCD'[i],ha='center',va='center',color='white',fontsize=14)
ax.text(1.5,1.08,'同色节点之间没有直接约束',ha='center',color=DARK)
ax.text(1.5,-.3,'先并行更新 A、C；再并行更新 B、D',ha='center',color=DARK)
ax=axes[1];ax.axis('off');ax.set(xlim=(-2,12),ylim=(-1,2))
for y,xa,xb,label in [(1,0,8,'原间距 8'),(0,-1,9,'两端各移 1 → 间距 10')]:
 ax.plot([xa,xb],[y,y],lw=2,color=TEAL);ax.scatter([xa,xb],[y,y],s=120,c=[BLUE,ORANGE]);ax.text((xa+xb)/2,y+.25,label,ha='center',color=DARK)
ax.text(5,-.65,'联合候选检查两端全部关联边的并集',ha='center',color=DARK)
fig.tight_layout();save(fig,'05_parallel_repair')

fig,ax=plt.subplots(figsize=(10,3.6));x=np.arange(3);w=.32
ax.bar(x-w/2,[20,20,-30],w,label='当前位移：总量 70',color=BLUE)
ax.bar(x+w/2,[0,0,-50],w,label='整组左移 20：总量 50',color=TEAL)
ax.axhline(0,color=MUTED,lw=1);ax.set_xticks(x,['端点 A','端点 B','端点 C']);ax.set_ylabel('相对原始位置的位移 / DBU');ax.legend(loc='lower left',fontsize=10)
ax.text(2,14,'C 伸长，但全组总量下降',ha='center',color=ORANGE,fontsize=11);ax.grid(axis='y',alpha=.15);fig.tight_layout();save(fig,'06_group_retraction')

stages=['continuous','quantized','repaired','retracted'];labels=['连续末态','整数化','冲突修复','协同回缩']
fig,axes=plt.subplots(1,2,figsize=(11.8,3.6))
vals=[metrics['stages'][s]['ge5nm'] for s in stages];bars=axes[0].bar(labels,vals,color=[BLUE,ORANGE,TEAL,TEAL]);axes[0].bar_label(bars,padding=4);axes[0].set_ylim(0,115);axes[0].set_ylabel('残差 ≥5nm 的约束数量')
v=[metrics['stages'][s]['extent_um'] for s in stages];axes[1].plot(labels,v,'o-',color=TEAL,lw=2.2,ms=7);axes[1].set_ylabel('trim 延长量 / µm');axes[1].set_ylim(2550,2810)
for i,n in enumerate(v):axes[1].annotate(f'{n:.3f}',(i,n),xytext=(0,10),textcoords='offset points',ha='center',fontsize=10)
for ax in axes:ax.grid(axis='y',alpha=.18)
fig.tight_layout();save(fig,'07_stage_metrics')

fig,axes=plt.subplots(1,2,figsize=(12,3.9));layers=['M2','M3','M4','M5','M6','M7']
iterations=sorted({int(r['iteration']) for r in rows if r['stage']=='iteration'})
totals=[sum(int(r['remaining_5nm']) for r in rows if r['stage']=='iteration' and int(r['iteration'])==i) for i in iterations]
axes[0].plot(iterations,totals,color=BLUE,lw=2);axes[0].set_yscale('log');axes[0].set(xlabel='迭代轮次',ylabel='全层 ≥5nm 数量（对数坐标）');axes[0].annotate('第 150 轮：64',(150,64),xytext=(88,150),arrowprops={'arrowstyle':'->','color':MUTED},fontsize=10)
for layer in layers:
 rr=[r for r in rows if r['stage']=='iteration' and r['layer']==layer];axes[1].plot([int(r['iteration']) for r in rr],[float(r['learning_rate']) for r in rr],label=layer,lw=1.7)
axes[1].set(xlabel='迭代轮次',ylabel='实际学习率比例 η',ylim=(.45,1.58));axes[1].legend(ncol=3,fontsize=9)
for ax in axes:ax.grid(alpha=.2)
fig.tight_layout();save(fig,'08_convergence_learning_rate')

fig,axes=plt.subplots(1,2,figsize=(11.7,3.6))
bars=axes[0].bar(['CPU 建图','GPU 建图'],[719.708,447.944],color=[MUTED,TEAL]);axes[0].bar_label(bars,fmt='%.3f ms',padding=4);axes[0].set_ylim(0,850);axes[0].set_ylabel('建图端到端时间 / ms');axes[0].set_title('历史同输入建图：约 1.61×')
s=metrics['retraction'];names=['single','cooperative','cooperative16'];vl=[s[n]['retract_ms'] for n in names];bars=axes[1].bar(['原单点','协同 8 次','协同 16 次'],vl,color=[MUTED,TEAL,BLUE]);axes[1].bar_label(bars,fmt='%.3f ms',padding=4);axes[1].set_ylim(0,35);axes[1].set_ylabel('回缩阶段时间 / ms');axes[1].set_title('新回缩减少延长量，增加约 17.6ms')
for ax in axes:ax.grid(axis='y',alpha=.18)
fig.tight_layout();save(fig,'09_performance')

# Actual OpenROAD output, unaltered panels with report captions.
fig,axes=plt.subplots(1,3,figsize=(12.8,4.9))
for ax,file,title in zip(axes,['gui_initial_detail.png','gui_continuous_detail.png','gui_final_detail.png'],['初始约束图','150 轮连续优化','150 轮 + 完整后处理']):
 ax.imshow(plt.imread(ASSETS/file));ax.set_title(title,color=DARK,fontsize=12,pad=9);ax.axis('off')
fig.text(.5,.04,'相同 M2 窗口：[20.9, 21.6] × [20.9, 21.6] µm；颜色表示边类型，不表示违例状态',ha='center',fontsize=10,color=MUTED)
fig.tight_layout(rect=(0,.08,1,1));save(fig,'10_gui_comparison')
fig,axes=plt.subplots(1,2,figsize=(10.8,5.2))
for ax,file,title in zip(axes,['gui_initial_detail.png','gui_domains_detail.png'],['移动域隐藏','移动域显示（黄色水平线）']):
 ax.imshow(plt.imread(ASSETS/file));ax.set_title(title,color=DARK);ax.axis('off')
fig.tight_layout();save(fig,'11_gui_domains')
print('Generated 11 illustrated figures, SVG/PNG pairs and frozen evidence tables.')
