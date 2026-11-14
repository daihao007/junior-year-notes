from pathlib import Path
import tempfile, subprocess, shutil, re, runpy

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT.parent/'词法分析'
# Refresh the base explanations against the current source before adding examples.
runpy.run_path(str(ROOT/'.build/explain_semantic.py'))
target=ROOT/'交付/semantic.c逐函数讲解.md'
base=target.read_text(encoding='utf-8')
samples={
'minimal':'int main() {\n    return 0;\n}\n',
'assign':'int main() {\n    int a = 1;\n    a = a + 2;\n    return a;\n}\n',
'undefined':'int main() {\n    a = 1;\n    return 0;\n}\n',
'array':'int main() {\n    int a[3] = {1, 2, 3};\n    a[1] = 2;\n    return a[1];\n}\n',
'function':"int f(int x, char s[]) {\n    return x;\n}\nint main() {\n    char buf[3];\n    return f(1, buf);\n}\n",
'chars':"char f() {\n    return 'a';\n}\nint main() {\n    return 0;\n}\n",
'nested':'int f(int x) {\n    int a = 1;\n    {\n        char a;\n    }\n    return x;\n}\nint main() { return 0; }\n',
'const':'const int c = 1;\nint main() {\n    c = 2;\n    return 0;\n}\n',
'printf':"int main() {\n    char c = 'a';\n    printf(\"%d %c\", 1, c);\n    return 0;\n}\n",
'condition':'int main() {\n    int a = 1;\n    if (a < 3) a = 2; else a = 4;\n    return a;\n}\n',
'while':'int main() {\n    while (1) {\n        break;\n    }\n    return 0;\n}\n',
'switch':"int main() {\n    switch ('a') {\n        case 'a': break;\n        case 'a': break;\n        default: ;\n    }\n    return 0;\n}\n",
'builtin':'int main() {\n    int a = get_int();\n    return a;\n}\n',
'init_error':"int main() {\n    int a[2] = {1, 'x'};\n    return 0;\n}\n",
'return_error':'int f(int x) {\n    if (x) return 1; else return 2;\n}\nint main() { return 0; }\n',
'cast':"int main() {\n    char c = 'a';\n    return (int)c;\n}\n",
}

# Instrument only an isolated temporary copy: capture the full syntax tree before
# semantic checking, including examples intentionally containing semantic errors.
dump=r'''
static void documentTree(FILE *out, const SyntaxNode *node, int depth)
{
    if (node == NULL) return;
    for (int i = 0; i < depth; i++) fputc('\t', out);
    fprintf(out, "%s", node->name);
    if (node->token != NULL)
        fprintf(out, "\t%d\t%s", node->token->line, node->token->value);
    fputc('\n', out);
    for (size_t i = 0; i < node->childCount; i++)
        documentTree(out, node->children[i], depth + 1);
}
'''
trees={}
with tempfile.TemporaryDirectory(prefix='semantic_tree_docs_') as td:
    td=Path(td)
    for f in SRC.glob('*.h'):shutil.copy2(f,td/f.name)
    names=['parser.c','token.c','syntax_tree.c','error_list.c','symbol.c','semantic.c']
    for f in names:shutil.copy2(SRC/f,td/f)
    text=(td/'semantic.c').read_text(encoding='utf-8-sig')
    text='#include <stdio.h>\n'+text
    at=text.index('int analyzeSemantics(')
    text=text[:at]+dump+'\n'+text[at:]
    at=text.index('{',text.index('int analyzeSemantics('))+1
    text=text[:at]+'\n    FILE *docFile = fopen("syntax_document.txt", "w");\n    if (docFile != NULL) { documentTree(docFile, root, 0); fclose(docFile); }\n'+text[at:]
    (td/'semantic.c').write_text(text,encoding='utf-8')
    exe=td/'compiler.exe'
    r=subprocess.run(['gcc','-Wall','-Wextra','-Werror','-std=c11',*[str(td/f) for f in names],'-o',str(exe)],capture_output=True,text=True)
    assert r.returncode==0,r.stderr
    for key,program in samples.items():
        wd=td/key;wd.mkdir();(wd/'testfile.txt').write_text(program,encoding='utf-8')
        r=subprocess.run([str(exe)],cwd=wd,capture_output=True,text=True,timeout=6)
        assert r.returncode==0 and not r.stderr,(key,r)
        lines=(wd/'syntax_document.txt').read_text(encoding='utf-8').splitlines()
        stack=[];root=None
        for s in lines:
            depth=len(s)-len(s.lstrip('\t'));parts=s.lstrip('\t').split('\t',2)
            node={'name':parts[0],'children':[]}
            if len(parts)>1:node.update(line=int(parts[1]),value=parts[2])
            if depth==0:root=node
            else:stack[depth-1]['children'].append(node)
            stack=stack[:depth]+[node]
        trees[key]=root

def walk(node):
    yield node
    for c in node['children']:yield from walk(c)
def pick(key,name,index=0):
    return [n for n in walk(trees[key]) if n['name']==name][index]
def render(node,maxdepth=4):
    def label(n):
        if 'value' in n:return f'{n["name"]} {n["value"]!r}  [第 {n["line"]} 行]'
        return n['name']
    lines=[label(node)]
    def kids(n,prefix,depth):
        if depth>=maxdepth and n['children']:
            lines.append(prefix+'└─ …（该节点的后代折叠）');return
        for i,c in enumerate(n['children']):
            last=i==len(n['children'])-1
            lines.append(prefix+('└─ ' if last else '├─ ')+label(c))
            kids(c,prefix+('   ' if last else '│  '),depth+1)
    kids(node,'',0)
    return '\n'.join(lines)

examples={}
def ex(name,key,node,trace,depth=4,note=''):
    examples[name]='\n**结合具体语法树：**\n\n源程序：\n\n```c\n'+samples[key].rstrip()+'\n```\n\n'+(note+'\n\n' if note else '')+'树形结构：\n\n```text\n'+render(node,depth)+'\n```\n\n**沿这棵树看函数如何工作：**\n\n'+trace.strip()+'\n'

ex('initSemanticContext','minimal',trees['minimal'],'''
1. 这棵 CompUnit 已由语法分析阶段建好；初始化函数本身不接收树节点。
2. 为后续遍历准备 context：currentReturnType=TYPE_VOID、loopDepth=0、switchDepth=0，绑定两个表。
3. main 随后把同一 context 和 CompUnit 根传给 analyzeSemantics；直到分析 MainFuncDef 时，返回类型才设为 TYPE_INT。
''',3)
ex('isNode','assign',pick('assign','Stmt'),'''
这里选中的是赋值 a=a+2 的 Stmt。

- isNode(当前根, "Stmt") → 1。
- isNode(第一个孩子, "LVal") → 1。
- isNode(ASSIGN 孩子, "ASSIGN") → 1。
- isNode(当前根, "Exp") → 0。

函数看的是树节点 name，不是源代码中的字面值 a。
''',2)
ex('findChild','assign',pick('assign','Stmt'),'''
- findChild(Stmt, "LVal") 返回 children[0]。
- findChild(Stmt, "ASSIGN") 返回 children[1]。
- findChild(Stmt, "Exp") 返回 children[2]。
- findChild(Stmt, "IDENFR") 返回 NULL，因为 a 在 LVal 的下一层，并非 Stmt 的直接孩子。

要取得左侧 a，应先 findChild(Stmt,"LVal")，再 findChild(LVal,"IDENFR")。这正是 analyzeAssignment 的做法。
''',3)
ex('reportSemanticError','undefined',pick('undefined','LVal'),'''
1. resolveIdentifier 从这个 IDENFR 取 value="a"、line=2。
2. 符号表没有 a，调用 reportSemanticError(context,2,'c')。
3. 函数把 (2,c) 放进 ErrorList。它只接收行号和错误码，不接收树；树用于说明行号是如何由调用者取到的。
4. 普通记录成功时继续；若 addError 分配失败才设置 memoryError。
''',2)
ex('resolveIdentifier','undefined',pick('undefined','LVal'),'''
调用者传入树中的 IDENFR 孩子，而不是整个 LVal。函数取 a 作为查找键，findSymbol 从当前 main 作用域向全局查找；均未找到，在第 2 行记 c 并返回 NULL。getLValType 收到 NULL 后返回 TYPE_ERROR。
''',2)
ex('getBaseType','function',pick('function','FuncType'),'''
当前节点是 f 的 FuncType，直接包含 INTTK，因此 getBaseType 返回 TYPE_INT。若输入形参 x 的 BType，同样直接找 INTTK，也返回 TYPE_INT；若是 char s[] 的 BType 则返回 TYPE_CHAR。是否数组不由这一层判断。
''',2)
ex('getDefinitionType','array',pick('array','VarDef'),'''
analyzeDeclaration 已从声明的 BType 得到 TYPE_INT，把此 VarDef 与 baseType=TYPE_INT 传入。函数看到 VarDef 的直接孩子 LBRACK，返回 TYPE_INT_ARRAY。数组长度 3 在 ConstExp 后代里，本函数不会读取它的数值。
''',3)
ex('getNumberType','chars',pick('chars','Number'),'''
传入的是 return 'a' 中的 Number。直接孩子 CHARCON 命中，返回 TYPE_CHAR。仅根据类别判断，不把字符转换成整数值；结果向 PrimaryExp、UnaryExp、MulExp、AddExp、Exp 一层层返回。
''',2)
ex('isScalarType','array',pick('array','LVal'),'''
这棵树是 a[1]，其名字 a 在符号表里为 TYPE_INT_ARRAY，但 getLValType 分析带下标访问后返回 TYPE_INT。

- isScalarType(TYPE_INT_ARRAY) → 0：数组整体不是标量。
- isScalarType(TYPE_INT) → 1：a[1] 的结果是元素，可进行标量赋值和运算。

isScalarType 的参数是类型值，不直接遍历 LVal。
''',3)
ex('getBinaryResultType','assign',pick('assign','AddExp',2),'''
此处选的是 a+2 的最外层 AddExp：children[0] 为左 AddExp，children[1] 为 PLUS，children[2] 为右 MulExp。analyzeExpression 先让两棵操作数子树返回 TYPE_INT，再将 (TYPE_INT,TYPE_INT,PLUS节点) 传入本函数，得到 TYPE_INT。

若左侧返回 TYPE_ERROR，就直接传播，不在 PLUS 行再报由同一未知类型引出的 e。
''',6)
# Choose the binary AddExp explicitly, independent of the number of wrappers.
binary=next(n for n in walk(trees['assign']) if n['name']=='AddExp' and len(n['children'])==3)
examples['getBinaryResultType']=examples['getBinaryResultType'].replace(render(pick('assign','AddExp',2),6),render(binary,6))
ex('getFirstToken','assign',pick('assign','Stmt')['children'][2],'''
传入赋值右侧 Exp（a+2）。函数沿第一个孩子递归：Exp → AddExp → 左 AddExp → MulExp → UnaryExp → PrimaryExp → LVal → IDENFR a，取得 a 的真实 Token，第 3 行。

它不会返回 PLUS 或最右边的 2，因为孩子按源顺序查找，遇到第一个 token 就结束。
''',8)
ex('reportNodeError','init_error',pick('init_error','InitVal'),'''
初始化项 'x' 的类型为 char，与 int 元素不符。analyzeDefinitionValues 将表示 'x' 的 Const/Exp（本例为 Exp）子树传入 reportNodeError，而不是传整个 InitVal。

该子树首 token 是第 2 行的 CHARCON 'x'，所以最终记录 (2,e)。此图保留 InitVal，以便看到函数所接收的那一项位于哪里。
''',7)
ex('getLValType','array',pick('array','LVal'),'''
1. analyzeExpression 先分析下标 Exp(1)，得到 indexType=TYPE_INT。
2. getLValType 找直接 IDENFR a，resolveIdentifier 得到数组符号 TYPE_INT_ARRAY。
3. LBRACK 表示这是下标访问，校验“原符号是数组、下标是 int”。
4. 返回 TYPE_INT，作为 a[1] 这个表达式的类型。

如果这棵 LVal 没有 LBRACK，则会返回整个数组类型；如果原符号是普通 int，则带下标的结构语法成立，但语义报 e。
''',6)
call=next(n for n in walk(trees['function']) if n['name']=='UnaryExp' and any(c['name']=='IDENFR' for c in n['children']))
ex('getCallType','function',call,'''
进入本函数前，实参已得到 [TYPE_INT, TYPE_CHAR_ARRAY]，数量为 2。函数从 UnaryExp 的直接 IDENFR 找到 f，匹配签名 f(int,char[])；数量相同、逐项类型相同，返回 f 的 TYPE_INT。

本函数只用树取调用名字及行号；实参列表的遍历已由 analyzeCall 完成。改为 f(1) 就在数量层报 d，改为 f(1,1) 就在第二个类型层报 e。
''',5)
ex('analyzeCall','function',call,'''
1. findChild(UnaryExp,"FuncRParams") 找到参数列表。
2. 只数直接 Exp，有两个，COMMA 不计数。
3. 第一个 Exp(1) 返回 TYPE_INT；第二个 Exp(buf) 经 LVal 查符号，返回 TYPE_CHAR_ARRAY。
4. 临时数组 types=[TYPE_INT,TYPE_CHAR_ARRAY] 交给 getCallType。
5. 得到返回类型 TYPE_INT 后释放 types，把类型交回上一层表达式。
''',6)
ex('analyzeExpression','assign',pick('assign','Stmt')['children'][2],'''
按递归调用顺序看：

```text
Exp
└─ AddExp(a+2)
   ├─ analyzeExpression(左 AddExp) → … → getLValType(a) → TYPE_INT
   ├─ PLUS：传给 getBinaryResultType，不当作独立表达式递归
   └─ analyzeExpression(右 MulExp) → … → getNumberType(2) → TYPE_INT
   得到：getBinaryResultType(int,int,+) → TYPE_INT
Exp 最终也返回 TYPE_INT
```

注意图中左孩子仍有 AddExp。这是解析器保留左结合层次的结果，不能把实际树误画成只有 a、+、2 三个叶子的平面 AST。
''',10)
ex('registerDefinition','array',pick('array','VarDef'),'''
此函数只从 VarDef 中拿 IDENFR a 及 LBRACK 数组标记，不深入处理 InitVal。

analyzeDeclaration 传 baseType=TYPE_INT、kind=SYMBOL_VAR，函数得到 TYPE_INT_ARRAY，构造 Symbol{name="a",type=TYPE_INT_ARRAY,kind=SYMBOL_VAR,line=2}。addSymbol 负责填当前 main 的 scopeId，并复制名字。如果同层已有 a，报告第 2 行 b。
''',3)
ex('analyzeDefinitionValues','init_error',pick('init_error','VarDef'),'''
1. VarDef 直接 ConstExp 是长度 2，类型为 int，通过长度类型检查。
2. InitVal 含 LBRACE，所以每一项与 baseType=TYPE_INT 比较。
3. 第一个 Exp(1) 返回 int，匹配；第二个 Exp('x') 返回 char，在第 2 行报告 e。
4. 这次初始化的 reportedMismatch 变为 1，但后续独立项仍会被分析。

这里要区分定义直接的 ConstExp（长度）与 InitVal 后代中的表达式（初值）。
''',7)
ex('analyzeDeclaration','array',pick('array','VarDecl'),'''
findChild(VarDecl,"BType") 找到 int；没有 STATICTK，确定 kind=SYMBOL_VAR。遍历直接孩子时只对 VarDef 执行 registerDefinition 与 analyzeDefinitionValues；BType、SEMICN 不当作定义。

若是 int a,b;，这里会有两个并列 VarDef，函数按源顺序逐个处理；数组属性与初值内容由各项的下层函数处理。
''',4)
ex('analyzeBlock','nested',pick('nested','Block',1),'''
此处是嵌套的 { char a; }，不是函数最外层 Block。analyzeNode 传 createScope=1：enterScope → 遍历 BlockItem → analyzeDeclaration 登记 char a → leaveScope。

在整段程序中全局作用域 1，f 的参数 x 和外层 a 位于作用域 2，这个内层 a 位于作用域 3。返回外层后，return x 使用的仍是作用域 2。
''',5)
params=pick('function','FuncFParams')
ex('registerParameters','function',params,'''
函数作用域已创建。先遍历第一个 FuncFParam，BType int + IDENFR x，registerDefinition 将 x 作为普通 Int 变量插入；再处理 BType char + IDENFR s + []，插入 CharArray。

COMMA 是分隔符，跳过。最终函数体中用 x 或 s 都能在当前作用域找到名字。
''',4)
ex('collectParameterTypes','function',params,'''
第一遍数两个直接 FuncFParam，分配两个 DataType。第二遍按顺序：x 的基本类型 int、无 []，记 TYPE_INT；s 的基本类型 char、有 []，记 TYPE_CHAR_ARRAY。

得到 function->paramTypes=[TYPE_INT,TYPE_CHAR_ARRAY]，paramCount=2。此时只建立函数签名，没有把 x、s 作为局部名字插入；那是之后 registerParameters 的工作。
''',4)
ex('analyzeAssignment','const',pick('const','Stmt'),'''
根是 c=2 的 Stmt。取直接 LVal 与 Exp，先分析左右均得到 int；再用 LVal 的直接 IDENFR c 查符号，kind 为 SYMBOL_CONST，报告 c 所在第 3 行 h。

虽然左右类型相同，常量修改仍不合法。图中的 ASSIGN 只是语法标志，不会改变符号表里的常量值。
''',6)
ex('analyzeReturn','cast',pick('cast','Stmt'),'''
本例第一个 Stmt 就是 return (int)c。当前 main 返回类型是 TYPE_INT。

analyzeReturn 找直接 Exp，再由 analyzeExpression 逐层走到含 BType 的 UnaryExp，先查 c 得 char，再确认显式目标 int，得到 TYPE_INT。最后与 currentReturnType 一致，接受。

如果没有 Exp，即 return;，actualType=TYPE_VOID；如果当前函数声明 void 却带 Exp，就在 RETURNTK 行报告 f。
''',8)
ex('getFormatType','printf',pick('printf','Stmt'),'''
analyzePrintf 从直接 STRCON 取字面字符串 "%d %c"，扫描到 % 时分别把后一个字符 'd'、'c' 传给 getFormatType，返回 TYPE_INT、TYPE_CHAR。

getFormatType 不接收整棵树或 STRCON 节点，只处理一个字符。此图说明该字符来自 printf 的哪个节点，而不是一个独立“格式表达式”语法节点。
''',4)
ex('analyzePrintf','printf',pick('printf','Stmt'),'''
findChild 找 STRCON 后识别两个占位符；数 Stmt 的直接 Exp，也有两个。递归分析第一个 Exp(1) 得 int，对上 %d；第二个 Exp(c) 查变量得 char，对上 %c。

如果去掉第二个实参，会先报 l，但剩余实参内部仍会遍历。这里不通过 FuncRParams 收集参数，因为 printf 在当前语法中是专用 Stmt。
''',6)
ex('checkFunctionReturn','return_error',pick('return_error','Block'),'''
扫描 f 的 Block 直接孩子，最后一个 BlockItem 中的直接 Stmt 是 if。该 Stmt 的直接孩子是 IFTK、括号、Cond、两个子 Stmt 和 ELSETK，没有直接 RETURNTK。

两个 return 藏在下一层分支 Stmt 内，findChild 不递归，所以不满足本实现的“末条顶层 return”规则，报告函数 RBRACE 所在第 3 行 g。这里正好说明树层次会影响判断。
''',5)
ex('analyzeCondition','condition',pick('condition','Cond'),'''
沿 Cond → LOrExp → LAndExp → EqExp → RelExp 分析。比较 a<3 时，两边都是 int，getBinaryResultType 返回 TYPE_INT；外层单孩子逐层透传。analyzeCondition 对最终 TYPE_INT 调 isScalarType，通过。

若条件只是数组名，最终得到数组类型，analyzeCondition 会报 e。它不需要求出 a<3 到底是真还是假。
''',8)
ex('analyzeWhile','while',pick('while','Stmt'),'''
直接 Cond 是 1；直接 Stmt 是包含 Block 的循环体。

分析 Cond 后，loopDepth 从 0 变 1，再把直接循环体 Stmt 交给 analyzeNode。遍历到 break 时能看到 loopDepth=1，因而合法。循环体处理完后减回 0，不因条件恒真而无限执行。
''',5)
ex('analyzeIf','condition',pick('condition','Stmt'),'''
先分析唯一的直接 Cond，然后遍历直接 Stmt：then 的 a=2、else 的 a=4 都交给 analyzeNode → analyzeAssignment。ELSETK 只是分隔标记，不是一个需要递归分析的语句。

不管 a<3 的实际真假，两棵分支子树都会检查。
''',4)
ex('analyzeJump','while',pick('while','Stmt',2),'''
该叶级 Stmt 直接包含 BREAKTK 与 SEMICN。它由外层 while 体遍历而来，此时 loopDepth=1、switchDepth=0，所以不报 m。

analyzeJump 不需要从树上向父节点搜索 while，因为外层 analyzeWhile 已经把“处于循环内”的信息写进 context。若从循环外遇到同样的树，两个深度均 0，就会报 m。
''',2)
ex('getNumberValue','switch',pick('switch','Number',1),'''
这是第一个 case 'a' 的 Number（第 3 行）。getNumberType 先得到 TYPE_CHAR；getNumberValue 再从 CHARCON 的 token->value 读取 'a'，返回字符 a 的数值 97（当前 ASCII 环境）。

它传回的是供 CaseValue 保存的数值，不更改树。后一个 case 的独立 Number 也得到同值，从而能发现重复。
''',2)
ex('analyzeSwitch','switch',pick('switch','Stmt'),'''
1. 直接 Exp('a') 得 TYPE_CHAR，作为 selectorType。
2. 直接 CaseStmt 有三项；第一项有 Number('a')，存入 seen={char,97}。
3. 第二项仍是 char,97，已出现，在第 4 行 CASETK 报 n。
4. 第三项有 DEFAULTTK，置 seenDefault=1。
5. 每个 CaseStmt 中的直接 Stmt 都继续分析，break 看到 switchDepth=1，因此合法。

seen 数组属于此次调用，嵌套 switch 会有另一份。结束后恢复深度并释放数组。
''',5)
ex('analyzeFunction','function',pick('function','FuncDef'),'''
沿这棵 FuncDef 先取 FuncType=int、直接 IDENFR=f、FuncFParams 和 Block。collectParameterTypes 读签名后，f 插入全局作用域。

随后 enterScope；registerParameters 登记 x 和 s；analyzeBlock(Block,0) 检查 return x；checkFunctionReturn 确认末项是直接 return；最后退出作用域并恢复上下文。

在 Block 被遍历之前 f 已登记，因此若函数体中调用 f 自己，签名已经可查。main 对应 MainFuncDef，不插名字但仍分析函数体。
''',4)
ex('analyzeNode','assign',trees['assign'],'''
从 CompUnit 开始，根没有专门处理分支，展开到 MainFuncDef，交 analyzeFunction；函数体的 BlockItem 是包装层，继续展开到 Decl 或 Stmt。

- Decl 展开到 VarDecl，交 analyzeDeclaration，立即 return。
- 赋值 Stmt 直接含 ASSIGN，交 analyzeAssignment，立即 return。
- return Stmt 直接含 RETURNTK，交 analyzeReturn，立即 return。

专门函数已分析自己的孩子，因此此处必须 return，避免再用末尾通用循环重复分析同一子树。
''',5)
ex('registerBuiltin','builtin',call if False else next(n for n in walk(trees['builtin']) if n['name']=='UnaryExp' and any(c['name']=='IDENFR' for c in n['children'])),'''
此树展示后续的 get_int() 调用。registerBuiltin 本身不从树取名字，而是在遍历此节点之前按硬编码签名登记 Symbol{name="get_int",type=TYPE_INT,paramCount=0,isBuiltin=1}。

因此 analyzeCall/getCallType 之后看到这个 UnaryExp，能查到 get_int 并返回 int。没有额外的“库函数定义”树节点，isBuiltin 还让最终符号输出过滤这条记录。
''',3)
ex('registerBuiltins','builtin',trees['builtin'],'''
这棵源程序树只含 main 和 get_int() 的使用，不含 get_int 的定义。analyzeSemantics 创建全局作用域后，先调用 registerBuiltins；它在树外登记七个库函数名和签名，再开始遍历 CompUnit。

当遍历到 InitVal 中的 get_int() 时，符号已经存在。代码里使用的 intParam、stringParam 等是 C 程序的临时类型数组，不是 SysY 语法树节点。
''',5)
ex('analyzeSemantics','minimal',trees['minimal'],'''
1. 确认根名是 CompUnit，创建作用域 1，注册库函数。
2. analyzeNode 从 CompUnit 展开到 MainFuncDef；main 建立自己的作用域 2。
3. 顺着 Block → BlockItem → Stmt 检查 return 0，得到 int，与 main 返回类型一致。
4. 函数分析恢复到作用域 1，入口最终 leaveScope 回到 0。
5. 返回 !memoryError。这个例子无错误；若树内有普通语义错误，仍可能返回 1，最终要看 ErrorList 判断输入是否正确。
''',9)

# Make sure all selected nodes really have the intended shape, so examples do not
# silently depend on a guessed tree occurrence number.
assert pick('assign','Stmt')['children'][0]['name']=='LVal'
assert pick('while','Stmt',2)['children'][0]['name']=='BREAKTK'
assert pick('switch','Number',1)['children'][0]['line']==3
assert pick('cast','Stmt')['children'][0]['name']=='RETURNTK'
assert pick('nested','Block',1)['children'][1]['name']=='BlockItem'
names=re.findall(r'^### \d+\. ([A-Za-z_]\w*)$',base,re.M)
assert set(examples)==set(names),(set(names)-set(examples),set(examples)-set(names))

intro='''## 语法树示例的阅读方法

下面每个函数都配有具体源程序和树形结构。树来自当前 parser.c 实际生成的 SyntaxNode，示例包含有意设置的语义错误，但语法结构均可解析。

- 终结符显示“类别、字面值、行号”；非终结符只显示节点名。
- 树中的 `…（该节点的后代折叠）` 表示该节点存在更多真实后代，不是直接相连的省略边。
- 图是相应完整程序树中的根或局部子树；是否只展示局部会由根名看出，例如 LVal、VarDef、Stmt。
- `Decl`、`BType`、`BlockItem` 在正式 parser.txt 中可能不输出，但实际内存树里仍然存在；下图保留这些节点。
- 语法树只表示源程序结构；符号表、DataType 返回值、ErrorList 与 context 深度是分析过程中额外维护的数据，不是树的新孩子。
- initSemanticContext、isScalarType、getFormatType、registerBuiltin 等函数并不直接接收语法树；它们的图用于展示调用来源或后续使用位置，不会虚构这些函数执行树遍历。

先看“源程序”，再确定“本函数收到哪个节点/参数”，最后对照执行步骤追踪“返回类型、符号表和 context 的变化”。

'''
base=base.replace('## 三、按源码顺序逐个解释',intro+'## 三、按源码顺序逐个解释')
for name in names:
    m=re.search(r'^### \d+\. '+re.escape(name)+r'\n',base,re.M)
    next_heading=re.search(r'^### \d+\.|^## 四、',base[m.end():],re.M)
    end=m.end()+next_heading.start() if next_heading else len(base)
    # Anchor for the next function belongs to that next function, not this section.
    anchor=base.rfind('<a id=',m.end(),end)
    if anchor!=-1:end=anchor
    base=base[:end]+examples[name]+'\n'+base[end:]
target.write_text(base,encoding='utf-8')
assert base.count('**结合具体语法树：**')==38
assert base.count('**沿这棵树看函数如何工作：**')==38
assert base.count('```')%2==0
print(f'Updated {target.resolve()}\n38 function examples; {len(samples)} parsed source programs; {len(base)} characters')
