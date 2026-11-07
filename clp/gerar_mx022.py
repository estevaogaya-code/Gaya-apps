# -*- coding: utf-8 -*-
"""
Gera a rotina de dosagem automática do misturador 600MX022 (silo 7) para o
RSLogix 5000 V17 (CLP MOAGEM_ES, 1756-L62), a partir de UMA definição:

  GR9_600MX022_DOSAGEM.L5X      rotina + tags, para Import Routine (programa MOAGEM_LINHA_502)
  GR9_600MX022_DOSAGEM_rungs.txt  os mesmos rungs em texto (alternativa: colar rung a rung)
  MX22_tags.CSV                 os tags, para Tools > Import > Tags (alternativa)

Rodar:  python3 clp/gerar_mx022.py
Nenhum rung existente é alterado. Depois de importar, acrescentar na MainRoutine
de MOAGEM_LINHA_502:  JSR(GR9_600MX022_DOSAGEM,0);
"""
import os
from xml.sax.saxutils import escape

AQUI = os.path.dirname(os.path.abspath(__file__))
CONTROLADOR = 'MOAGEM_ES'
PROGRAMA = 'MOAGEM_LINHA_502'
ROTINA = 'GR9_600MX022_DOSAGEM'

# Cápsulas (índice da lógica -> válvula). Descarga = 600FG051.
CAPSULAS = [(1, '600FG044', '7.1'), (2, '600FG038', '7.2'), (3, '600FG040', '7.3'), (4, '600FG039', '7.4')]

# ── Tags (todos no escopo do controlador; o servidor lê/escreve pelo nome) ──
# (nome, tipo, dimensão, valor inicial L5K, descrição)
TAGS = [
    ('MX22_AUTO', 'BOOL', 0, '0', 'Dosagem automatica ativa (= seletora ED_600MX022_DOSAGEM)'),
    ('MX22_PASSO', 'DINT', 0, '0', 'Passo: 0 parado, 10 aguarda, 20 produto, 22 capsula, 25 dosando, 30 estabiliza, 40 descarga, 45 registra, 90 pausa'),
    ('MX22_PROD', 'DINT', 0, '0', 'Produto em dosagem (1..4)'),
    ('MX22_CAP', 'DINT', 0, '0', 'Capsula aberta (1=7.1 2=7.2 3=7.3 4=7.4)'),
    ('MX22_PESO', 'REAL', 0, '0.0', 'Peso da balanca 600MX022 (kg)'),
    ('MX22_LOTE', 'REAL', 0, '1000.0', 'Peso da batelada (kg)'),
    ('MX22_VAZIO', 'REAL', 0, '100.0', 'Balanca vazia / descarga completa abaixo de (kg)'),
    ('MX22_P0', 'REAL', 0, '0.0', 'Peso no inicio do produto (kg)'),
    ('MX22_PREF', 'REAL', 0, '0.0', 'Peso de referencia da janela de capsula vazia'),
    ('MX22_DELTA', 'REAL', 0, '0.0', 'Ganho de peso na janela de 20 s'),
    ('MX22_NOMINAL', 'REAL', 0, '0.0', 'Alvo nominal do produto (pct x lote)'),
    ('MX22_ALVO', 'REAL', 0, '0.0', 'Alvo do produto com compensacao (kg)'),
    ('MX22_LIM', 'REAL', 0, '0.0', 'Tolerancia 10% do nominal (kg)'),
    ('MX22_DOSADO', 'REAL', 0, '0.0', 'Peso dosado do produto ate agora (kg)'),
    ('MX22_DOS_FECH', 'REAL', 0, '0.0', 'Peso dosado no instante do fechamento'),
    ('MX22_SOMA', 'REAL', 0, '0.0', 'Soma dos percentuais ativos'),
    ('MX22_PCT', 'REAL', 8, None, 'Receita [1..4] fracao de cada produto - escrita pelo servidor'),
    ('MX22_CAP_PROD', 'DINT', 8, None, 'Capsula [1..4] -> produto (0 = nao usada) - escrita pelo servidor'),
    ('MX22_RCP_SEQ', 'DINT', 0, '0', 'Numero da receita escrita pelo servidor'),
    ('MX22_PCT_ATIVO', 'REAL', 8, None, 'Receita em uso (copiada no inicio da batelada)'),
    ('MX22_CAP_PROD_AT', 'DINT', 8, None, 'Capsula -> produto em uso'),
    ('MX22_RCP_SEQ_AT', 'DINT', 0, '0', 'Numero da receita em uso'),
    ('MX22_QUEDA', 'REAL', 8, None, 'Queda aprendida por produto (kg que caem apos fechar)'),
    ('MX22_COMP', 'REAL', 8, None, 'Compensacao para a proxima batelada por produto (kg)'),
    ('MX22_REAL', 'REAL', 8, None, 'Peso real de cada produto na batelada em curso (kg)'),
    ('MX22_BAT_KG', 'REAL', 8, None, 'Peso real de cada produto na ultima batelada (kg)'),
    ('MX22_BAT_TOTAL', 'REAL', 0, '0.0', 'Total da ultima batelada (kg)'),
    ('MX22_BAT_SEQ', 'DINT', 0, '0', 'Contador de bateladas concluidas'),
    ('MX22_BAT_RCP', 'DINT', 0, '0', 'Receita usada na ultima batelada'),
    ('MX22_ACUM_KG', 'DINT', 8, None, 'Acumulado de kg por produto (nunca zera; servidor calcula diferencas)'),
    ('MX22_ALM', 'DINT', 0, '0', 'Alarmes: .0 tolerancia .1 capsula vazia .2 sem capsula .3 descarga demorada .4 sem receita .5 misturador desligado'),
    ('MX22_VAZIA', 'DINT', 0, '0', 'Capsulas vazias: bit 1..4'),
    ('MX22_ALM_TOL_PROD', 'DINT', 0, '0', 'Produtos fora da tolerancia: bit 1..4'),
    ('MX22_ABRE', 'DINT', 0, '0', 'Comando interno: bit 1..4 capsulas, bit 5 descarga FG051'),
    ('MX22_ONS', 'DINT', 0, '0', 'Bits de ONS da rotina'),
    ('MX22_RESET', 'BOOL', 0, '0', 'Reconhecer alarmes / retomar (escrito pelo servidor)'),
    ('MX22_TMR_VAZIA', 'TIMER', 0, '[0,20000,0]', 'Janela de 20 s da capsula vazia'),
    ('MX22_TMR_EST', 'TIMER', 0, '[0,6000,0]', 'Estabilizacao apos fechar (6 s)'),
    ('MX22_TMR_DESC', 'TIMER', 0, '[0,5000,0]', 'Balanca vazia por 5 s = descarga completa'),
    ('MX22_TMR_DESC_MAX', 'TIMER', 0, '[0,600000,0]', 'Descarga demorada (10 min)'),
]

# ── Rungs: (comentário, texto neutro) ─────────────────────────────────────────
R = []
def rung(com, txt): R.append((com, txt))

rung('DOSAGEM AUTOMATICA MISTURADOR 600MX022 - SILO 7 (gerado por clp/gerar_mx022.py, repositorio Gaya-apps).\n'
     'Seletora ED_600MX022_DOSAGEM habilita. Valvulas das capsulas e FG051 comandadas pelo bit .4 de V600FGxxx (igual a IHM).',
     'XIC(ED_600MX022_DOSAGEM)OTE(MX22_AUTO);')
rung('Peso da balanca (kg)', 'MOV(EA_600WF023_WT_U01_AJUSTE[9],MX22_PESO);')
rung('Valores padrao se os tags vierem zerados (criados por CSV)',
     '[LEQ(MX22_LOTE,0.0) MOV(1000.0,MX22_LOTE) ,LEQ(MX22_VAZIO,0.0) MOV(100.0,MX22_VAZIO) ];')
rung('Seletora ligada: comeca do inicio (aguarda balanca vazia)',
     'XIC(MX22_AUTO)ONS(MX22_ONS.0)[MOV(10,MX22_PASSO) ,MOV(0,MX22_PROD) ,MOV(0,MX22_CAP) ,MOV(0,MX22_ALM) ,MOV(0,MX22_VAZIA) ,MOV(0,MX22_ALM_TOL_PROD) ];')
rung('Seletora desligada: fecha todas as valvulas (capsulas e descarga). O operador decide pelo comando manual.',
     'XIO(MX22_AUTO)ONS(MX22_ONS.1)[OTU(V600FG044.4) ,OTU(V600FG038.4) ,OTU(V600FG040.4) ,OTU(V600FG039.4) ,OTU(V600FG051.4) ,MOV(0,MX22_ABRE) ,MOV(0,MX22_PASSO) ,MOV(0,MX22_CAP) ];')
rung('Reconhecer alarmes (servidor escreve MX22_RESET=1). Em pausa por falta de capsula: retoma o produto.',
     'XIC(MX22_RESET)EQU(MX22_PASSO,90)MOV(22,MX22_PASSO);')
rung('', 'XIC(MX22_RESET)[MOV(0,MX22_ALM) ,MOV(0,MX22_VAZIA) ,MOV(0,MX22_ALM_TOL_PROD) ,OTU(MX22_RESET) ];')
rung('Aviso: dosagem habilitada com o misturador desligado (as capsulas nao abrem)',
     'XIC(MX22_AUTO)XIO(M600MX022.6)OTE(MX22_ALM.5);')

# Passo 10 — aguarda condições e aplica receita nova
rung('PASSO 10 - receita nova escrita pelo servidor entra so no inicio da batelada (zera a compensacao)',
     'EQU(MX22_PASSO,10)NEQ(MX22_RCP_SEQ,MX22_RCP_SEQ_AT)[COP(MX22_PCT[0],MX22_PCT_ATIVO[0],8) ,COP(MX22_CAP_PROD[0],MX22_CAP_PROD_AT[0],8) ,FLL(0.0,MX22_COMP[0],8) ,MOV(MX22_RCP_SEQ,MX22_RCP_SEQ_AT) ];')
rung('', 'CPT(MX22_SOMA,MX22_PCT_ATIVO[1]+MX22_PCT_ATIVO[2]+MX22_PCT_ATIVO[3]+MX22_PCT_ATIVO[4]);')
rung('Sem receita valida', 'XIC(MX22_AUTO)LEQ(MX22_SOMA,0.0)OTE(MX22_ALM.4);')
rung('PASSO 10 -> 20: misturador ligado, balanca vazia, FG051 fechada, receita valida',
     'EQU(MX22_PASSO,10)XIC(M600MX022.6)LES(MX22_PESO,MX22_VAZIO)XIC(V600FG051.0)XIO(V600FG051.4)GRT(MX22_SOMA,0.0)[FLL(0.0,MX22_REAL[0],8) ,MOV(0,MX22_PROD) ,MOV(0,MX22_CAP) ,MOV(20,MX22_PASSO) ];')

# Passo 20 — próximo produto
rung('PASSO 20 - proximo produto da receita', 'EQU(MX22_PASSO,20)ADD(MX22_PROD,1,MX22_PROD);')
rung('Acabaram os produtos: descarga', 'EQU(MX22_PASSO,20)GRT(MX22_PROD,4)MOV(40,MX22_PASSO);')
rung('Produto com percentual: alvo = pct x lote + compensacao da batelada anterior',
     'EQU(MX22_PASSO,20)LEQ(MX22_PROD,4)GRT(MX22_PCT_ATIVO[MX22_PROD],0.0)[CPT(MX22_NOMINAL,MX22_PCT_ATIVO[MX22_PROD]*MX22_LOTE) ,CPT(MX22_LIM,MX22_NOMINAL*0.1) ,CPT(MX22_ALVO,MX22_NOMINAL+MX22_COMP[MX22_PROD]) ,MOV(MX22_PESO,MX22_P0) ,MOV(0,MX22_CAP) ,MOV(22,MX22_PASSO) ];')

# Passo 22 — escolhe a cápsula do produto (pula as vazias)
rung('PASSO 22 - escolhe a capsula do produto que nao esta vazia (7.1 e 7.2 / 7.3 e 7.4 com o mesmo produto)',
     'EQU(MX22_PASSO,22)MOV(0,MX22_CAP);')
for i, fg, cid in CAPSULAS:
    rung('' if i > 1 else '',
         f'EQU(MX22_PASSO,22)EQU(MX22_CAP,0)EQU(MX22_CAP_PROD_AT[{i}],MX22_PROD)XIO(MX22_VAZIA.{i})MOV({i},MX22_CAP);')
rung('Capsula encontrada: comeca a dosar', 'EQU(MX22_PASSO,22)NEQ(MX22_CAP,0)[MOV(MX22_PESO,MX22_PREF) ,MOV(25,MX22_PASSO) ];')
rung('Nenhuma capsula com o produto: pausa e alarma (operador abastece e reconhece)',
     'EQU(MX22_PASSO,22)EQU(MX22_CAP,0)[OTL(MX22_ALM.2) ,MOV(90,MX22_PASSO) ];')

# Passo 25 — dosando
rung('PASSO 25 - dosando', 'EQU(MX22_PASSO,25)CPT(MX22_DOSADO,MX22_PESO-MX22_P0);')
rung('Chegou no alvo menos a queda aprendida: fecha', 'EQU(MX22_PASSO,25)CMP(MX22_DOSADO>=MX22_ALVO-MX22_QUEDA[MX22_PROD])[MOV(MX22_DOSADO,MX22_DOS_FECH) ,MOV(30,MX22_PASSO) ];')
rung('Capsula vazia: janela de 20 s com o misturador ligado (o rele de campo alterna 5 s fluidizando / 5 s aberta)',
     'EQU(MX22_PASSO,25)XIC(M600MX022.6)XIO(MX22_TMR_VAZIA.DN)TON(MX22_TMR_VAZIA,20000,0);')
rung('', 'EQU(MX22_PASSO,25)XIC(MX22_TMR_VAZIA.DN)[CPT(MX22_DELTA,MX22_PESO-MX22_PREF) ,MOV(MX22_PESO,MX22_PREF) ];')
rung('Ganhou menos de 10 kg em 20 s: marca vazia, alarma e passa para a outra capsula do mesmo produto',
     'EQU(MX22_PASSO,25)XIC(MX22_TMR_VAZIA.DN)LES(MX22_DELTA,10.0)[OTL(MX22_VAZIA.[MX22_CAP]) ,OTL(MX22_ALM.1) ,MOV(22,MX22_PASSO) ];')

# Passo 30 — estabiliza e registra
rung('PASSO 30 - valvula fechada, espera estabilizar 6 s', 'EQU(MX22_PASSO,30)TON(MX22_TMR_EST,6000,0);')
rung('Real do produto; queda aprendida (media); compensacao para a proxima batelada = alvo - real',
     'EQU(MX22_PASSO,30)XIC(MX22_TMR_EST.DN)[CPT(MX22_REAL[MX22_PROD],MX22_PESO-MX22_P0) ,CPT(MX22_QUEDA[MX22_PROD],(MX22_QUEDA[MX22_PROD]+MX22_REAL[MX22_PROD]-MX22_DOS_FECH)/2.0) ,CPT(MX22_COMP[MX22_PROD],MX22_ALVO-MX22_REAL[MX22_PROD]) ];')
rung('Limites: queda 0..200 kg, compensacao +/-10% do nominal',
     'EQU(MX22_PASSO,30)XIC(MX22_TMR_EST.DN)[LES(MX22_QUEDA[MX22_PROD],0.0) MOV(0.0,MX22_QUEDA[MX22_PROD]) ,GRT(MX22_QUEDA[MX22_PROD],200.0) MOV(200.0,MX22_QUEDA[MX22_PROD]) ,GRT(MX22_COMP[MX22_PROD],MX22_LIM) MOV(MX22_LIM,MX22_COMP[MX22_PROD]) ,CMP(MX22_COMP[MX22_PROD]<0.0-MX22_LIM) CPT(MX22_COMP[MX22_PROD],0.0-MX22_LIM) ];')
rung('Tolerancia de 10%: alarma e segue',
     'EQU(MX22_PASSO,30)XIC(MX22_TMR_EST.DN)CMP(ABS(MX22_REAL[MX22_PROD]-MX22_ALVO)>MX22_LIM)[OTL(MX22_ALM.0) ,OTL(MX22_ALM_TOL_PROD.[MX22_PROD]) ];')
rung('Proximo produto', 'EQU(MX22_PASSO,30)XIC(MX22_TMR_EST.DN)MOV(20,MX22_PASSO);')

# Passo 40/45 — descarga e registro
rung('PASSO 40 - descarga automatica pela FG051 ate a balanca ficar abaixo de MX22_VAZIO por 5 s',
     'EQU(MX22_PASSO,40)LES(MX22_PESO,MX22_VAZIO)TON(MX22_TMR_DESC,5000,0);')
rung('', 'EQU(MX22_PASSO,40)XIC(MX22_TMR_DESC.DN)MOV(45,MX22_PASSO);')
rung('Descarga demorando mais de 10 min (ex.: tromba nao pronta): alarma e continua esperando',
     'EQU(MX22_PASSO,40)TON(MX22_TMR_DESC_MAX,600000,0)XIC(MX22_TMR_DESC_MAX.DN)OTL(MX22_ALM.3);')
rung('PASSO 45 - registra a batelada (o servidor le MX22_BAT_SEQ e MX22_ACUM_KG) e recomeca',
     'EQU(MX22_PASSO,45)[COP(MX22_REAL[0],MX22_BAT_KG[0],8) ,CPT(MX22_BAT_TOTAL,MX22_REAL[1]+MX22_REAL[2]+MX22_REAL[3]+MX22_REAL[4]) ,ADD(MX22_ACUM_KG[1],MX22_REAL[1],MX22_ACUM_KG[1]) ,ADD(MX22_ACUM_KG[2],MX22_REAL[2],MX22_ACUM_KG[2]) ,ADD(MX22_ACUM_KG[3],MX22_REAL[3],MX22_ACUM_KG[3]) ,ADD(MX22_ACUM_KG[4],MX22_REAL[4],MX22_ACUM_KG[4]) ,MOV(MX22_RCP_SEQ_AT,MX22_BAT_RCP) ,ADD(MX22_BAT_SEQ,1,MX22_BAT_SEQ) ,MOV(10,MX22_PASSO) ];')

# Saídas para as válvulas (sempre no fim)
rung('COMANDO DAS VALVULAS - so com a dosagem automatica ativa. Abre = V600FGxxx.4 (mesmo bit da IHM).', 'MOV(0,MX22_ABRE);')
for i, fg, cid in CAPSULAS:
    rung(f'Capsula {cid} - {fg}', f'XIC(MX22_AUTO)EQU(MX22_PASSO,25)EQU(MX22_CAP,{i})OTL(MX22_ABRE.{i});')
    rung('', f'XIC(MX22_AUTO)[XIC(MX22_ABRE.{i}) OTL(V{fg}.4) OTU(V{fg}.6) ,XIO(MX22_ABRE.{i}) OTU(V{fg}.4) ];')
rung('Descarga 600FG051 (continua valendo a logica atual da GR9_600FG051: tromba 600TC002)',
     'XIC(MX22_AUTO)EQU(MX22_PASSO,40)OTL(MX22_ABRE.5);')
rung('', 'XIC(MX22_AUTO)[XIC(MX22_ABRE.5) OTL(V600FG051.4) OTU(V600FG051.6) ,XIO(MX22_ABRE.5) OTU(V600FG051.4) ];')


def valor_inicial(tipo, dim, ini):
    if tipo == 'TIMER': return ini
    zero = '0.00000000e+000' if tipo == 'REAL' else '0'
    if dim: return '[' + ','.join([zero] * dim) + ']'
    if tipo == 'REAL': return '%.8e' % float(ini)
    return ini


def gerar_l5x():
    o = []
    o.append('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>')
    o.append(f'<RSLogix5000Content SchemaRevision="1.0" SoftwareRevision="17.00" TargetName="{ROTINA}" TargetType="Routine" TargetSubType="RLL" ContainsContext="true" ExportDate="Sat Nov 07 12:00:00 2026" ExportOptions="References NoRawData L5KData DecoratedData ForceProtectedEncoding AllProjDocTrans">')
    o.append(f'<Controller Use="Context" Name="{CONTROLADOR}">')
    o.append('<DataTypes Use="Context">\n</DataTypes>')
    o.append('<Tags Use="Context">')
    for nome, tipo, dim, ini, desc in TAGS:
        radix = '' if tipo == 'TIMER' else (' Radix="Float"' if tipo == 'REAL' else ' Radix="Decimal"')
        dims = f' Dimensions="{dim}"' if dim else ''
        o.append(f'<Tag Name="{nome}" TagType="Base" DataType="{tipo}"{dims}{radix}>')
        o.append(f'<Description>\n<![CDATA[{desc}]]>\n</Description>')
        o.append(f'<Data Format="L5K">\n<![CDATA[{valor_inicial(tipo, dim, ini)}]]>\n</Data>')
        o.append('</Tag>')
    o.append('</Tags>')
    o.append('<Programs Use="Context">')
    o.append(f'<Program Use="Context" Name="{PROGRAMA}">')
    o.append('<Routines Use="Context">')
    o.append(f'<Routine Use="Target" Name="{ROTINA}" Type="RLL">')
    o.append(f'<Description>\n<![CDATA[Dosagem automatica do misturador 600MX022 (silo 7), habilitada pela seletora ED_600MX022_DOSAGEM]]>\n</Description>')
    o.append('<RLLContent>')
    for n, (com, txt) in enumerate(R):
        o.append(f'<Rung Number="{n}" Type="N">')
        if com: o.append(f'<Comment>\n<![CDATA[{com}]]>\n</Comment>')
        o.append(f'<Text>\n<![CDATA[{txt}]]>\n</Text>')
        o.append('</Rung>')
    o.append('</RLLContent>\n</Routine>\n</Routines>\n</Program>\n</Programs>\n</Controller>\n</RSLogix5000Content>')
    return '\n'.join(o) + '\n'


def gerar_txt():
    o = [f'Rotina {ROTINA} - programa {PROGRAMA} - {len(R)} rungs',
         'Alternativa ao L5X: criar a rotina vazia, e em cada rung novo colar o texto (modo texto do editor ladder).',
         'Depois: JSR(GR9_600MX022_DOSAGEM,0); no fim da MainRoutine de MOAGEM_LINHA_502.', '']
    for n, (com, txt) in enumerate(R):
        if com: o.append(f'// Rung {n}: ' + com.replace('\n', ' '))
        o.append(txt)
        o.append('')
    return '\n'.join(o)


def gerar_csv():
    o = ['remark,"CSV-Import-Export"', 'remark,"Version = RSLogix 5000 v17.00"', '0.3',
         'TYPE,SCOPE,NAME,DESCRIPTION,DATATYPE,SPECIFIER,ATTRIBUTES']
    for nome, tipo, dim, ini, desc in TAGS:
        dt = f'{tipo}[{dim}]' if dim else tipo
        attr = '' if tipo == 'TIMER' else ('(RADIX := Float)' if tipo == 'REAL' else '(RADIX := Decimal)')
        o.append(f'TAG,,{nome},"{desc}",{dt},"","{attr}"')
    return '\r\n'.join(o) + '\r\n'


if __name__ == '__main__':
    for nome, conteudo in [(f'{ROTINA}.L5X', gerar_l5x()), (f'{ROTINA}_rungs.txt', gerar_txt()), ('MX22_tags.CSV', gerar_csv())]:
        with open(os.path.join(AQUI, nome), 'w', encoding='utf-8', newline='') as f:
            f.write(conteudo)
        print('gerado', nome)
    print(len(R), 'rungs,', len(TAGS), 'tags')
