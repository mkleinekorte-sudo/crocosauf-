#!/usr/bin/env python3
"""Render the checked-in German guide as a standalone PDF (ReportLab)."""
from pathlib import Path
import re
import html
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, HRFlowable, KeepTogether

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'firmware/UPDATE_v6_6_40LED.md'
OUTPUT = ROOT / 'output/pdf/Anleitung_Crocosauf_40LED_v6_6.pdf'
FONT = Path('/usr/share/fonts/truetype/dejavu')
for name, filename in [('DejaVu', 'DejaVuSans.ttf'), ('DejaVu-Bold', 'DejaVuSans-Bold.ttf'), ('DejaVu-Mono', 'DejaVuSansMono.ttf')]:
    pdfmetrics.registerFont(TTFont(name, str(FONT / filename)))
pdfmetrics.registerFontFamily('DejaVu', normal='DejaVu', bold='DejaVu-Bold', italic='DejaVu', boldItalic='DejaVu-Bold')
ink = colors.HexColor('#142e23')
green = colors.HexColor('#256846')
muted = colors.HexColor('#53695d')
styles = getSampleStyleSheet()
styles.add(ParagraphStyle('GuideTitle', fontName='DejaVu-Bold', fontSize=25, leading=31, textColor=ink, spaceAfter=13))
styles.add(ParagraphStyle('GuideH2', fontName='DejaVu-Bold', fontSize=14, leading=19, textColor=green, spaceBefore=14, spaceAfter=7, keepWithNext=True))
styles.add(ParagraphStyle('GuideBody', fontName='DejaVu', fontSize=10.0, leading=14.2, textColor=ink, spaceAfter=7, splitLongWords=True, allowWidows=0, allowOrphans=0))
styles.add(ParagraphStyle('GuideList', parent=styles['GuideBody'], leftIndent=13, firstLineIndent=-10, spaceAfter=7))
styles.add(ParagraphStyle('GuideCell', fontName='DejaVu', fontSize=9, leading=12.5, textColor=ink, splitLongWords=True))
styles.add(ParagraphStyle('GuideCellHead', parent=styles['GuideCell'], fontName='DejaVu-Bold', textColor=colors.white))

def inline(text):
    text = text.replace('\u2011', '-').replace('\u2013', '-').replace('\u2014', '-')
    tokens = []
    def code(m):
        tokens.append('<font name="DejaVu-Mono" size="8.7">'+html.escape(m.group(1))+'</font>')
        return f'@@TOKEN{len(tokens)-1}@@'
    text = re.sub(r'`([^`]+)`', code, text)
    def link(m):
        tokens.append('<a color="#256846" href="'+html.escape(m.group(2), quote=True)+'"><u>'+html.escape(m.group(1))+'</u></a>')
        return f'@@TOKEN{len(tokens)-1}@@'
    text = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', link, text)
    text = html.escape(text)
    text = re.sub(r'\*\*(.+?)\*\*', r'<b>\1</b>', text)
    for i, value in enumerate(tokens): text = text.replace(f'@@TOKEN{i}@@', value)
    return text

def p(text, style='GuideBody'):
    return Paragraph(inline(text), styles[style])

def footer(canvas, doc):
    canvas.saveState()
    w, h = A4
    canvas.setStrokeColor(colors.HexColor('#ccdbd0'))
    canvas.setLineWidth(.5)
    canvas.line(43, 40, w-43, 40)
    canvas.setFont('DejaVu', 8)
    canvas.setFillColor(muted)
    canvas.drawString(43, 27, 'CROCOSAUF  |  40 LEDs  |  Firmware v6.6 / App 1.1')
    canvas.drawRightString(w-43, 27, str(doc.page))
    if doc.page > 1:
        canvas.setFont('DejaVu-Bold', 8)
        canvas.drawString(43, h-29, 'SPIEL- UND INSTALLATIONSANLEITUNG')
    canvas.restoreState()

story=[]
lines=SOURCE.read_text().splitlines()
i=0
width=A4[0]-86
while i < len(lines):
    line=lines[i].strip()
    if not line: i+=1; continue
    if line.startswith('# '):
        story.append(p('CROCOSAUF\n40 LEDs'.replace('\n',' '), 'GuideTitle'))
        story.append(p('Spiel- und Installationsanleitung'))
        story.append(HRFlowable(width='100%', thickness=2, color=green, spaceAfter=10))
        i+=1;continue
    if line.startswith('## '):
        story.append(p(line[3:], 'GuideH2'));i+=1;continue
    if line.startswith('|'):
        rows=[]
        while i<len(lines) and lines[i].strip().startswith('|'):
            row=[x.strip() for x in lines[i].strip().strip('|').split('|')]
            if not all(re.fullmatch(r'[:\- ]+', c) for c in row):rows.append(row)
            i+=1
        widths = [width*.21,width*.49,width*.30] if rows[0][0]=='Gruppe' else [width*.18,width*.52,width*.30]
        table=Table([[p(c, 'GuideCellHead' if n==0 else 'GuideCell') for c in row] for n,row in enumerate(rows)],colWidths=widths,repeatRows=1,hAlign='LEFT')
        table.setStyle(TableStyle([('BACKGROUND',(0,0),(-1,0),green),('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.HexColor('#eef5ef'),colors.white]),('VALIGN',(0,0),(-1,-1),'TOP'),('LEFTPADDING',(0,0),(-1,-1),9),('RIGHTPADDING',(0,0),(-1,-1),9),('TOPPADDING',(0,0),(-1,-1),6),('BOTTOMPADDING',(0,0),(-1,-1),6),('LINEBELOW',(0,-1),(-1,-1),.5,colors.HexColor('#c8d7cb'))]))
        story.extend([KeepTogether([table]),Spacer(1,10)]);continue
    if line.startswith('- ') or re.match(r'^\d+\. ',line):
        if line.startswith('- '):line='• '+line[2:]
        story.append(p(line,'GuideList'));i+=1;continue
    paragraph=[line];i+=1
    while i<len(lines) and lines[i].strip() and not lines[i].startswith(('#','|','- ')) and not re.match(r'^\d+\. ',lines[i]):
        paragraph.append(lines[i].strip());i+=1
    item=p(' '.join(paragraph))
    following=next((ln.strip() for ln in lines[i:] if ln.strip()), '')
    if following.startswith('|'): item.keepWithNext=True
    story.append(item)
OUTPUT.parent.mkdir(parents=True,exist_ok=True)
doc=SimpleDocTemplate(str(OUTPUT),pagesize=A4,rightMargin=43,leftMargin=43,topMargin=49,bottomMargin=54,title='Crocosauf 40 LEDs - Spiel- und Installationsanleitung',author='Crocosauf',subject='Firmware v6.6-BLE-40LED-APPONLY und Android-App 1.1')
doc.build(story,onFirstPage=footer,onLaterPages=footer)
print(OUTPUT)
