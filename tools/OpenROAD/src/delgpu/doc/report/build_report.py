#!/usr/bin/env python3
"""Build an illustrated Chinese PDF and a self-contained HTML from Markdown.
Requires pandoc, matplotlib, Pillow, reportlab and PyMuPDF for verification.
"""
from pathlib import Path
import hashlib, html, json, os, re, subprocess, textwrap
from xml.sax.saxutils import escape
from PIL import Image as PILImage
import matplotlib
matplotlib.use('Agg')
from matplotlib.mathtext import math_to_image
from matplotlib.font_manager import FontProperties
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph,
 Image, Spacer, PageBreak, CondPageBreak, LongTable, TableStyle, KeepTogether,
 XPreformatted, Flowable)
from reportlab.platypus.tableofcontents import TableOfContents

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[5]
MD=HERE/'delgpu_full_report.md'
W,H=A4; PAGE_WIDTH=W-34*mm
CJK=os.environ.get('DELGPU_REPORT_CJK_FONT','/mnt/c/Windows/Fonts/msyh.ttc')
BOLD=os.environ.get('DELGPU_REPORT_CJK_BOLD_FONT','/mnt/c/Windows/Fonts/msyhbd.ttc')
MONO=str(Path(matplotlib.get_data_path())/'fonts/ttf/DejaVuSansMono.ttf')
for name,file in [('CJK',CJK),('CJKBold',BOLD),('CodeFont',MONO)]:
 pdfmetrics.registerFont(TTFont(name,file,subfontIndex=0))
pdfmetrics.registerFontFamily('CJK',normal='CJK',bold='CJKBold',italic='CJK',boldItalic='CJKBold')
NAVY=colors.HexColor('#183153'); TEAL=colors.HexColor('#0d9488'); MUTED=colors.HexColor('#64748b')
STYLES={
 'body':ParagraphStyle('body',fontName='CJK',fontSize=10.2,leading=17,spaceAfter=8,
                       wordWrap='CJK',autoLeading='max',textColor=colors.HexColor('#25364a')),
 'caption':ParagraphStyle('caption',fontName='CJK',fontSize=9,leading=14,alignment=TA_CENTER,
                          textColor=MUTED,spaceAfter=13,wordWrap='CJK'),
 'h1':ParagraphStyle('h1',fontName='CJKBold',fontSize=17,leading=24,spaceBefore=15,spaceAfter=12,
                     textColor=NAVY,keepWithNext=True,wordWrap='CJK'),
 'h2':ParagraphStyle('h2',fontName='CJKBold',fontSize=12,leading=19,spaceBefore=10,spaceAfter=7,
                     textColor=TEAL,keepWithNext=True,wordWrap='CJK'),
 'cell':ParagraphStyle('cell',fontName='CJK',fontSize=9,leading=14,wordWrap='CJK',autoLeading='max'),
 'cellhead':ParagraphStyle('cellhead',fontName='CJKBold',fontSize=9,leading=14,wordWrap='CJK',
                           textColor=colors.white,autoLeading='max'),
 'code':ParagraphStyle('code',fontName='CodeFont',fontSize=8.1,leading=11.5,spaceAfter=3,
                       textColor=colors.HexColor('#183153')),
}
FORMULAS=HERE/'assets/formulas';FORMULAS.mkdir(exist_ok=True)
formula_count=0

def math_image(tex,display=False):
 global formula_count
 key=hashlib.sha256((str(display)+tex).encode()).hexdigest()[:18]
 target=FORMULAS/f'{key}.png'
 dpi=240; size=13 if display else 10.6
 if not target.exists():
  math_to_image('$'+tex.replace(r'\iff', r'\Longleftrightarrow')+'$',target,prop=FontProperties(size=size),dpi=dpi,color='#183153')
 width,height=PILImage.open(target).size
 formula_count+=1
 return target,width*72/dpi,height*72/dpi

def plain(inlines):
 result=[]
 for x in inlines:
  t=x['t'];c=x.get('c')
  if t=='Str':result.append(c)
  elif t in ('Space','SoftBreak','LineBreak'):result.append(' ')
  elif t=='Code':result.append(c[1])
  elif t=='Math':result.append(c[1])
  elif t in ('Strong','Emph','Strikeout'):result.append(plain(c))
  elif t in ('Link','Image'):result.append(plain(c[1]))
  elif t=='Span':result.append(plain(c[1]))
 return ''.join(result)

def inline(inlines):
 result=[]
 for x in inlines:
  t=x['t'];c=x.get('c')
  if t=='Str':result.append(escape(c))
  elif t in ('Space','SoftBreak'):result.append(' ')
  elif t=='LineBreak':result.append('<br/>')
  elif t=='Strong':result.append('<b>'+inline(c)+'</b>')
  elif t=='Emph':result.append('<i>'+inline(c)+'</i>')
  elif t=='Code':result.append('<font color="#0d766e">'+escape(c[1])+'</font>')
  elif t=='Math':
   path,w,h=math_image(c[1]);result.append(f'<img src="{path}" width="{w:.2f}" height="{h:.2f}" valign="-3"/>')
  elif t=='Link':result.append(inline(c[1]))
  elif t=='Span':result.append(inline(c[1]))
  elif t in ('Quoted','Cite'):result.append(inline(c[1]))
  else:raise ValueError('Unsupported inline: '+t)
 return ''.join(result)

class Heading(Paragraph):
 def __init__(self,markup,level,title):
  super().__init__(markup,STYLES['h1' if level==1 else 'h2'])
  self.level=level;self.title=title

class Report(BaseDocTemplate):
 def afterFlowable(self,flow):
  if isinstance(flow,Heading):
   bookmark='sec-'+hashlib.sha1(flow.title.encode()).hexdigest()[:12]
   self.canv.bookmarkPage(bookmark)
   self.canv.addOutlineEntry(flow.title,bookmark,level=flow.level-1,closed=flow.level>1)
   if flow.level==1:self.notify('TOCEntry',(0,flow.title,self.page,bookmark))

def page(canvas,doc):
 canvas.saveState()
 canvas.setTitle('DELGPU 全流程设计与实现汇报')
 canvas.setAuthor('OpenROAD-flow-scripts / delgpu')
 if doc.page>1:
  canvas.setStrokeColor(colors.HexColor('#dbe3eb'));canvas.line(17*mm,H-12.5*mm,W-17*mm,H-12.5*mm)
  canvas.setFillColor(MUTED);canvas.setFont('CJK',8)
  canvas.drawString(17*mm,H-10.3*mm,'DELGPU  /  全流程设计与实现')
  canvas.drawRightString(W-17*mm,H-10.3*mm,'2026-10-15')
 canvas.setFillColor(MUTED);canvas.setFont('CJK',8)
 canvas.drawString(17*mm,10*mm,'数据：ASAP7 / IBEX     ·     当前结果为 trim 模型位移')
 canvas.drawRightString(W-17*mm,10*mm,str(doc.page))
 canvas.restoreState()

story=[]
cover_style=ParagraphStyle('cover',fontName='CJKBold',fontSize=28,leading=40,textColor=NAVY)
sub_style=ParagraphStyle('sub',fontName='CJK',fontSize=14,leading=24,textColor=TEAL)
story += [Spacer(1,25*mm),Paragraph('DELGPU<br/>全流程设计与实现汇报',cover_style),Spacer(1,8*mm),
 Paragraph('从 trim 约束图到 GPU 迭代、整数修复与协同回缩',sub_style),Spacer(1,11*mm),
 Paragraph('OpenROAD-flow-scripts · delgpu 模块<br/>2026 年 10 月 15 日',STYLES['body']),Spacer(1,8*mm)]
kpis=[['332,664','437,360','36 / 48'],['trim 节点','A/B/C 约束边','≥5nm / <5nm 最终违例']]
kp=LongTable([[Paragraph(str(v),ParagraphStyle('k'+str(i)+str(j),parent=STYLES['body'],fontName='CJKBold' if i==0 else 'CJK',fontSize=20 if i==0 else 9,leading=30 if i==0 else 15,alignment=TA_CENTER,textColor=NAVY if i==0 else MUTED)) for j,v in enumerate(row)] for i,row in enumerate(kpis)],colWidths=[PAGE_WIDTH/3]*3)
kp.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,-1),colors.HexColor('#eef6fa')),('TOPPADDING',(0,0),(-1,0),12),('BOTTOMPADDING',(0,-1),(-1,-1),13)]));story += [kp,Spacer(1,10*mm)]
img=HERE/'assets/01_pipeline.png';iw,ih=PILImage.open(img).size
story += [Image(str(img),PAGE_WIDTH,PAGE_WIDTH*ih/iw),Spacer(1,7*mm),
 Paragraph('完整链路 · 数学建模 · 算法伪代码 · 真实 GUI · 可复现实验',STYLES['caption']),PageBreak()]
story += [Paragraph('目录与阅读指南',STYLES['h1']),Paragraph('建议先阅读第 1、11、12 章了解结果和演示；第 2–10 章给出完整模型与算法；第 13 章及附录记录保证、边界和证据来源。',STYLES['body'])]
toc=TableOfContents();toc.levelStyles=[ParagraphStyle('toc',fontName='CJK',fontSize=10.5,leading=20,leftIndent=0,firstLineIndent=0,textColor=NAVY)]
story += [toc,PageBreak()]

ast=json.loads(subprocess.check_output(['pandoc',str(MD),'-t','json'],cwd=HERE))

def convert_table(c):
 # Pandoc <=2.9 uses the 5-field table; >=2.10 uses the 6-field table.
 if len(c)==5:
  header,rows=c[3],c[4]
 else:
  def cells(row):return [cell[4] for cell in row[1]]
  header=cells(c[3][1][0]) if c[3][1] else []
  rows=[cells(row) for body in c[4] for row in body[3]]
 def cell(blocks,head=False):
  text='<br/>'.join(inline(b['c']) for b in blocks if b['t'] in ('Plain','Para'))
  return Paragraph(text,STYLES['cellhead' if head else 'cell'])
 values=[[cell(b,True) for b in header]]+[[cell(b) for b in row] for row in rows]
 n=len(header)
 widths={2:[.31,.69],3:[.20,.33,.47],4:[.31,.23,.23,.23],5:[.27,.14,.18,.17,.24]}.get(n,[1/n]*n)
 table=LongTable(values,colWidths=[PAGE_WIDTH*x for x in widths],repeatRows=1,hAlign='LEFT')
 table.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),NAVY),('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.white,colors.HexColor('#f3f7fa')]),
  ('LINEBELOW',(0,0),(-1,0),.5,NAVY),('LINEBELOW',(0,1),(-1,-1),.3,colors.HexColor('#dbe4ec')),
  ('VALIGN',(0,0),(-1,-1),'TOP'),('LEFTPADDING',(0,0),(-1,-1),7),('RIGHTPADDING',(0,0),(-1,-1),7),
  ('TOPPADDING',(0,0),(-1,-1),7),('BOTTOMPADDING',(0,0),(-1,-1),7)]))
 return [table,Spacer(1,10)]

def convert_blocks(blocks):
 output=[]
 for block in blocks:
  t=block['t'];c=block.get('c')
  if t=='Header':output += [CondPageBreak(95),Heading(inline(c[2]),min(c[0],2),plain(c[2]))]
  elif t in ('Para','Plain'):
   if len(c)==1 and c[0]['t']=='Image':
    image=c[0]['c'];path=HERE/image[2][0];iw,ih=PILImage.open(path).size
    w=PAGE_WIDTH;h=w*ih/iw
    if h>145*mm:w*=145*mm/h;h=145*mm
    output += [KeepTogether([Spacer(1,4),Image(str(path),w,h),Paragraph(inline(image[1]),STYLES['caption'])])]
   elif len(c)==1 and c[0]['t']=='Math' and c[0]['c'][0]['t']=='DisplayMath':
    path,w,h=math_image(c[0]['c'][1],True)
    if w>PAGE_WIDTH:w,h=PAGE_WIDTH,h*PAGE_WIDTH/w
    output += [KeepTogether([Spacer(1,5),Image(str(path),w,h),Spacer(1,10)])]
   else:output += [Paragraph(inline(c),STYLES['body'])]
  elif t=='Table':output += convert_table(c)
  elif t=='CodeBlock':
   text=c[1];style=STYLES['code']
   if re.search('[\u4e00-\u9fff]',text):
    style=ParagraphStyle('cjcode',parent=style,fontName='CJK')
   lines=[]
   for line in text.splitlines():
    lines += textwrap.wrap(line,width=98,replace_whitespace=False,drop_whitespace=False,
                           subsequent_indent='    ',break_long_words=False) or ['']
   text='\n'.join(lines)
   code=XPreformatted(escape(text),style)
   table=LongTable([[code]],colWidths=[PAGE_WIDTH],hAlign='LEFT')
   table.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,-1),colors.HexColor('#eef4f8')),('BOX',(0,0),(-1,-1),.4,colors.HexColor('#cbd9e3')),
    ('LEFTPADDING',(0,0),(-1,-1),10),('RIGHTPADDING',(0,0),(-1,-1),10),('TOPPADDING',(0,0),(-1,-1),10),('BOTTOMPADDING',(0,0),(-1,-1),10)]))
   output += [KeepTogether([Spacer(1,3),table,Spacer(1,11)])]
  elif t in ('BulletList','OrderedList'):
   items=c if t=='BulletList' else c[1]
   for i,item in enumerate(items):
    prefix='• ' if t=='BulletList' else f'{i+1}. '
    for b in item:
     if b['t'] in ('Plain','Para'):output += [Paragraph(prefix+inline(b['c']),STYLES['body'])]
     else:output += convert_blocks([b])
  elif t=='HorizontalRule':output += [Spacer(1,12)]
  elif t=='BlockQuote':output += convert_blocks(c)
  elif t=='Div':output += convert_blocks(c[1])
  else:raise ValueError('Unsupported block '+t)
 return output
story += convert_blocks(ast['blocks'])
output=HERE/'delgpu_full_report.pdf'
doc=Report(str(output),pagesize=A4,leftMargin=17*mm,rightMargin=17*mm,topMargin=17*mm,bottomMargin=17*mm,
           title='DELGPU 全流程设计与实现汇报',author='OpenROAD-flow-scripts / delgpu',pageCompression=1)
frame=Frame(17*mm,17*mm,PAGE_WIDTH,H-34*mm,id='normal',leftPadding=0,rightPadding=0,topPadding=0,bottomPadding=0)
doc.addPageTemplates(PageTemplate(id='main',frames=frame,onPage=page))
doc.multiBuild(story)
# Native MathML is offline and editable text, without a MathJax network dependency.
subprocess.run(['pandoc',str(MD),'-s','--toc','--toc-depth=2','--mathml','--self-contained',
                '--css',str(HERE/'report.css'),'-o',str(HERE/'delgpu_full_report.html')],cwd=HERE,check=True)
# Frozen provenance: source files, raw measured evidence and capture outputs.
paths=list((HERE/'data').glob('*.csv'))+list((HERE/'data').glob('*summary.json'))+[HERE/'data/metrics.json']
module=HERE.parents[1]
paths += [module/p for p in ['include/delgpu/Rules.h','include/delgpu/Parameters.h',
 'include/delgpu/AdaptiveLearningRate.h','src/graph/BuildGraph.cpp','src/graph/TrimGeometry.cpp',
 'src/graph/Domains.cpp','src/graph/Arcs.cpp','src/graph/GraphGpu.cpp','src/graph/GraphCuda.cu',
 'src/optimize/TrimOptimizer.cu','src/optimize/Postprocess.cuh','src/optimize/Postprocess.cpp',
 'src/optimize/RunOptimization.cpp','src/delgpu.tcl','src/GraphGraphics.cpp']]
paths += list((HERE/'assets').glob('gui*.png'))+[MD,HERE/'generate_figures.py',HERE/'build_report.py']
manifest={'report_date':'2026-10-15','git_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
          'note':'Hashes describe local file contents, including uncommitted changes; not just HEAD.',
          'files':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}}
(HERE/'data/source_manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
print(f'Built {output.name}, offline HTML, {formula_count} equation/inline math placements and source manifest.')
