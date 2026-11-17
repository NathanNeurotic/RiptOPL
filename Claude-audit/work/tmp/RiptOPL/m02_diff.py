import difflib, sys, os
A='src/RiptOPL/modules/bdmassault/ilink_bd_assault/src/'; B='src/RiptOPL/modules/ilink/'
fs=['iLinkman/include/iLinkman.h','iLinkman/src/exports.tab','iLinkman/src/imports.lst','iLinkman/src/irx_imports.h','iLinkman/src/iLink.c','iLinkman/src/iLink_DMA.c','iLinkman/src/iLink_PHY.c','iLinkman/src/iLink_TxRx.c','iLinkman/src/iLink_crom.c','iLinkman/src/iLink_internal.c','iLinkman/src/iLink_intr.c','iLinkman/src/include/iLink_internal.h','iLinkman/src/include/iLink_CROM.h']
for f in fs:
    a=open(A+f,encoding='latin1').read().splitlines()
    if not os.path.exists(B+f): print(f,'missing B'); continue
    b=open(B+f,encoding='latin1').read().splitlines()
    d=[l for l in difflib.unified_diff(a,b,lineterm='',n=0) if l[:1] in '+-' and not l.startswith(('+++','---'))]
    print(f,'A=%d B=%d -%d +%d'%(len(a),len(b),sum(1 for l in d if l[0]=='-'),sum(1 for l in d if l[0]=='+')))
