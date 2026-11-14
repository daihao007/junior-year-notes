from pathlib import Path
import re, subprocess, tempfile, json, hashlib

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT.parent/'词法分析'
OUT=ROOT/'交付';OUT.mkdir(parents=True,exist_ok=True)
def loc(file, name):
    lines=(SRC/file).read_text(encoding='utf-8-sig').splitlines()
    for i,s in enumerate(lines,1):
        if re.match(r'^(?:static )?.*\b'+re.escape(name)+r'\s*\(',s) and (file.endswith('.h') or not s.rstrip().endswith(';')):
            return i
    raise ValueError(name)
def ref(file,name):
    return f'[{file} → {name}]({(SRC/file).as_posix()}:{loc(file,name)})'

script='''我的语义分析是在已有词法、语法分析之后，单独遍历语法树实现的。主函数在语法分析成功后调用 analyzeSemantics，初始化全局作用域并注册库函数，再按语法树的源程序顺序分析。

第一部分是符号表。我将数据类型和符号种类分开记录：类型区分 int、char、数组和 void，种类区分变量、常量、静态变量和函数。作用域保存编号和父编号。同层声明查重，使用名字时从当前作用域向父作用域查找，支持遮蔽。退出作用域只恢复父编号，保留记录用于最后输出。

第二部分是类型检查。analyzeExpression 递归返回表达式类型，上层据此检查赋值、实参、返回值和 printf。函数符号保存返回类型与有序形参类型表，先检查参数个数，再检查类型。子表达式出错返回 TYPE_ERROR，上层跳过依赖该类型的判断，减少连带报错。

第三部分是上下文检查。SemanticContext 保存当前函数返回类型、while 深度和 switch 深度，用于判断 return、break、continue。每个 switch 独立记录 case 值和 default 是否出现，避免嵌套时相互干扰。

右侧例子中，全局 x 在作用域 1，形参 p 和局部 x 在作用域 2，内层字符 x 在作用域 3。使用 x 时就近查找，函数体最后的 return x 返回的是作用域 2 的 int，所以类型正确。函数名 f 属于全局作用域。

最后，各阶段错误写入同一个 ErrorList 并按行号排序；无错误时输出符号表，main 和库函数不输出。当前返回检查遵循作业要求的末条顶层 return 规则，没有实现控制流全路径分析。'''
(OUT/'两分钟讲解稿.txt').write_text(script,encoding='utf-8')

qa=[
('语义分析和语法分析有什么区别？','语法阶段判断 token 能否符合产生式并构造树；语义阶段在这棵树上检查名字、类型和上下文。比如 x=1; 在语法上可以成立，x 未声明时由语义报 c。',[('parser.c','main'),('semantic.c','analyzeSemantics')]),
('从哪一个函数开始看？','main 中 parseCompUnit 成功后初始化 SymbolTable 与 SemanticContext，再调用 analyzeSemantics。入口创建全局作用域 1，注册库函数，调用 analyzeNode 分派；普通包装节点递归展开，具体节点交给专门函数。',[('semantic.c','analyzeSemantics'),('semantic.c','analyzeNode')]),
('为什么不把所有检查都写进 parser？','当前实现复用现有树，减少语法识别与语义规则之间的耦合；新增语义规则主要改 semantic.c。代价是要保存整棵树，并进行额外遍历。parser 负责结构恢复，semantic 主要在结构解析成功后工作。',[('semantic.c','analyzeNode'),('parser.c','main')]),
('符号表保存哪些东西？','Symbol 保存 name、type、kind、scopeId、line；函数额外保存 paramTypes、paramCount；isBuiltin 标记运行库。type 与 kind 分开，让 int 常量与 int 变量共享 TYPE_INT，同时用 kind 判断是否可修改。',[('symbol.h','initSymbolTable'),('symbol.c','getSymbolTypeName')]),
('作用域怎么进入、退出？编号为什么减 1？','enterScope 新建 id=scopeCount+1，parentId 指向进入前的 currentScopeId。leaveScope 把 currentScopeId 恢复为 parentId。编号从 1 开始，数组从 0 开始，所以 scopes[id-1]。退出不删符号，编号不复用，空 Block 也占编号。',[('symbol.c','enterScope'),('symbol.c','leaveScope')]),
('重定义与遮蔽怎么区别？','addSymbol 只调用 findSymbolInCurrentScope，当前层同名报 b。findSymbol 从当前层向父链查找，第一个命中即返回，因此不同作用域同名合法，内层遮蔽外层，兄弟作用域不能相互访问。',[('symbol.c','addSymbol'),('symbol.c','findSymbol')]),
('为什么形参与函数体不分两层？createScope 有什么用？','analyzeFunction 已 enterScope，然后 registerParameters；调用 analyzeBlock(body,0) 复用这层，保证形参与函数最外层局部变量同层查重。普通嵌套 Block 从 analyzeNode 调 analyzeBlock(...,1)，才新建作用域。',[('semantic.c','analyzeFunction'),('semantic.c','analyzeBlock')]),
('递归调用为什么能找到函数？是否能调用后面才定义的函数？','analyzeFunction 先收集完整形参类型表，将函数登记到全局，再分析函数体，因此支持自递归。程序按源顺序处理，没有提前预登记所有函数；不能宣称支持任意前向调用或相互递归。',[('semantic.c','collectParameterTypes'),('semantic.c','analyzeFunction')]),
('函数参数怎么匹配？','analyzeCall 先分析每个实际参数，得到有序 DataType 数组。getCallType 解析名字并确认它是函数；个数不同报 d，个数一致后逐项比较类型，不同报 e，正确则返回函数返回类型。即使参数数量错误，前面的实参遍历也能发现实参中的未定义名字。',[('semantic.c','analyzeCall'),('semantic.c','getCallType')]),
('数组与数组元素如何区分？','不带下标返回符号的数组类型，可与数组形参匹配；带下标要求符号本身是数组且下标为 int，最终把 IntArray/CharArray 转为 Int/Char。函数名当变量、标量加下标等报 e。',[('semantic.c','getLValType')]),
('int 和 char 能自动转换吗？','本作业实现严格类型匹配：二元运算要求两侧同类型标量；算术保留类型，关系和逻辑返回 int。显式 (int)/(char) 转换在 UnaryExp 分支处理。不要套用标准 C 的自动整型提升规则。',[('semantic.c','analyzeExpression'),('semantic.c','getBinaryResultType')]),
('TYPE_ERROR 和 memoryError 是一个概念吗？','TYPE_ERROR 是表达式类型无法确定的哨兵，常用于未定义名字或已发现类型错误后抑制连带诊断；memoryError 是内部无法继续的状态。普通语义错误加入 ErrorList 后尽量继续。analyzeSemantics 返回 1 表示遍历完成，不表示零语义错误，是否可输出还看 errors.size。',[('semantic.c','resolveIdentifier'),('semantic.c','getBinaryResultType'),('semantic.c','analyzeSemantics')]),
('常量不能修改在哪检查？','analyzeAssignment 先分析左右表达式，再找左边符号；左类型有效且 kind==SYMBOL_CONST 时报告 h；其余赋值比较标量和类型。不只看名字是否带 const 字符串。',[('semantic.c','analyzeAssignment')]),
('return 的 e、f、g 怎么区别？','analyzeReturn：void 函数带表达式 return 报 f；非 void 返回类型不符，包括裸 return，报 e。checkFunctionReturn：非 void 函数最后一个顶层 BlockItem 不是直接 return，报右花括号行 g。if 两支都 return 也不等同于这里的末条顶层 return。',[('semantic.c','analyzeReturn'),('semantic.c','checkFunctionReturn')]),
('为什么 loopDepth 和 switchDepth 要分开？','break 要求 loopDepth>0 或 switchDepth>0；continue 只要求 loopDepth>0。只有 switch 没有 while 时，continue 非法；while 里嵌套 switch 时，continue 可继续外层循环。计数器进入增加，退出减少，支持嵌套；函数边界保存/重置/恢复上下文。',[('semantic.c','analyzeWhile'),('semantic.c','analyzeJump'),('semantic.c','analyzeFunction')]),
('switch 怎样发现重复？','每次 analyzeSwitch 都有独立 seen 数组和 seenDefault。case 先校验与 selector 类型一致，再按类型和解析后的数值比较；default 重复也报 n。即使标签有错误仍遍历分支里的语句。结束后释放数组并恢复 switchDepth。',[('semantic.c','getNumberValue'),('semantic.c','analyzeSwitch')]),
('printf 怎么检查？','printf 在 parser 中是专用语句。analyzePrintf 数 %d/%c/%s 占位符与 Exp 实参：个数不符报 l；一致时按 int/char/char[] 匹配，类型不符报 e。数量不符时仍检查每个实参内部错误。',[('semantic.c','getFormatType'),('semantic.c','analyzePrintf')]),
('怎样确定错误行号并统一输出？','语义规则选择指定 token：b/c 通常标识符，调用 d/e 使用函数名，二元类型错误用运算符，g 用函数右花括号；reportNodeError 默认取节点第一个真实 token。词法 a 与语法 i/j/k 也在同一个 ErrorList 中，最终稳定按行排序。行号不能笼统说全取当前行。',[('semantic.c','reportNodeError'),('semantic.c','reportSemanticError'),('error_list.c','sortErrors')]),
('符号输出的顺序怎么保证？','writeSymbolOutput 外层按 scopes 的创建顺序枚举，内层按符号插入顺序筛选该层，所以作用域编号升序、同层声明有序。main 不插入；库函数先注册供查找，但 isBuiltin 过滤输出。错误输入清空 symbol.txt，避免上次成功结果残留。',[('symbol.c','writeSymbolOutput'),('semantic.c','registerBuiltins'),('parser.c','main')]),
('为什么结构体复制后还复制 name 与 paramTypes？','结构体赋值对指针是浅复制；addSymbol 再为名字和参数数组分配独立内存，避免临时数组释放后符号表留下悬空指针。freeSymbolTable 逐项释放再释放容器。动态数组扩容可能移动地址，不应长期保存 Symbol 指针跨越 addSymbol。',[('symbol.c','addSymbol'),('symbol.c','freeSymbolTable')]),
('查找复杂度与改进方向？','当前每沿父链走一层都线性扫描所有符号，单次名字查找最坏 O(D×S)，D 是嵌套深度、S 是符号总数；插入查重 O(S)，符号输出 O(K×S)，K 是作用域数。可改成每层哈希表并保留独立声明顺序表；当前实现不是哈希表。',[('symbol.c','findSymbol'),('symbol.c','findSymbolInCurrentScope'),('symbol.c','writeSymbolOutput')]),
('实现有哪些边界？','这里只讨论语义分析：未实现完整常量表达式求值、数组越界与全路径返回证明，也没有为后续代码生成设计完地址/存储布局。switch 数字标签求值不代表具备通用常量折叠能力；static 目前以符号种类记录。新增语法后还应补语义分派、类型和上下文规则。',[('semantic.c','getNumberValue'),('semantic.c','checkFunctionReturn'),('symbol.h','initSymbolTable')]),
]
doc='# 语义分析现场讲解与代码追问\n\n依据：当前工作目录中的实际 C 源码。PPT 只有一页，以下材料用于考前准备和现场定位。\n\n## 两分钟讲解稿\n\n'+script+'\n\n## 代码追问：先口述结论，再打开函数\n\n'
for i,(q,a,refs) in enumerate(qa,1):
    doc+=f'### {i}. {q}\n\n{a}\n\n定位：'+ '；'.join(ref(f,n) for f,n in refs)+'。\n\n'
doc+=r'''## 现场编译和演示

在 `词法分析` 目录中运行 PowerShell：

```powershell
gcc -Wall -Wextra -std=c11 parser.c token.c syntax_tree.c error_list.c symbol.c semantic.c -o compiler_demo.exe
$LASTEXITCODE
.\compiler_demo.exe
Get-Content .\symbol.txt
Get-Content .\error.txt
```

GCC 编译成功才运行。程序从当前工作目录读取 testfile.txt，运行后写输出，不会执行测试程序。不要使用 gcc *.c，目录里还有其他 main。为保护当前输入，优先在本材料的 demo 子目录运行已经准备的样例，编译命令见其 README。

### 现场操作顺序

1. PPT 先讲主流程，打开 semantic.c 的 analyzeSemantics 与 analyzeNode。
2. 跟着作用域例子，打开 symbol.c 的 enterScope、findSymbol，再打开 analyzeFunction。
3. 助教问表达式时，打开 analyzeExpression 和 getBinaryResultType；问函数调用则打开 getCallType。
4. 演示一个正确输入与一个错误输入，指出 error.txt 的行号对应哪一个 token。
5. 如果追问未来扩展，说明当前实现边界及应增加的数据，不声称已实现中间代码或控制流分析。

## 错误码速查

| 类别 | 规则 | 主要处理点 |
|---|---|---|
| a | 单独 & / 竖线 | parser.c 词法循环 |
| b / c | 同层重定义 / 未定义 | registerDefinition / resolveIdentifier |
| d / e | 参数数量 / 各类类型不匹配 | getCallType / 各类型检查函数 |
| f / g | void 带值 return / 缺末条顶层 return | analyzeReturn / checkFunctionReturn |
| h | 修改常量 | analyzeAssignment |
| i / j / k | 缺 ; / ) / ] | parser.c → expectToken |
| l | printf 占位符和实参数量不符 | analyzePrintf |
| m | break / continue 上下文非法 | analyzeJump |
| n | 重复 case / default | analyzeSwitch |

## 考前自测

先不看答案，能否用例子解释：内层同名为何合法、形参与外层局部变量为何不能同名、递归为何可见、TYPE_ERROR 为什么不会让所有检查停下、switch 内的 continue 什么时候合法？每题尝试用“规则 → 状态/数据结构 → 函数 → 一个例子”的顺序回答。
'''
(OUT/'代码讲解与答辩问答.md').write_text(doc,encoding='utf-8')
import markdown
body=markdown.markdown(doc,extensions=['tables','fenced_code','toc'])
(OUT/'代码讲解与答辩问答.html').write_text('<!doctype html><meta charset="utf-8"><title>语义分析答辩</title><style>body{max-width:980px;margin:40px auto;padding:0 24px;font:17px/1.8 Microsoft YaHei;color:#17304a}pre{background:#eef3f7;padding:18px;white-space:pre-wrap}table{border-collapse:collapse}td,th{border:1px solid #bbc9d5;padding:8px}h2{margin-top:2em}a{color:#1263a0}</style>'+body,encoding='utf-8')

samples={
'01_scope':('int x;\nint f(int p) {\n    int x;\n    { char x; }\n    return x;\n}\nint main() { return 0; }\n','', '1 x Int\n1 f IntFunc\n2 p Int\n2 x Int\n3 x Char\n'),
'02_errors':('const int c = 1;\nint main() {\n    c = 2;\n    x = 1;\n    return 0;\n}\n','3 h\n4 c\n', ''),
'03_context':('int main() {\n    switch (1) {\n        case 1: continue;\n    }\n    while (1) { switch (1) { case 1: continue; } break; }\n    return 0;\n}\n','3 m\n',''),
'04_types':("int f(char c) { return (int)c; }\nint main() {\n    f(1);\n    f('a', 'b');\n    return 0;\n}\n",'3 e\n4 d\n','')}
report=[]
with tempfile.TemporaryDirectory(prefix='semantic_defense_') as tmp:
    exe=Path(tmp)/'compiler.exe'
    files=['parser.c','token.c','syntax_tree.c','error_list.c','symbol.c','semantic.c']
    r=subprocess.run(['gcc','-Wall','-Wextra','-Werror','-std=c11',*[str(SRC/f) for f in files],'-o',str(exe)],capture_output=True,text=True)
    assert r.returncode==0,r.stderr
    for n,(code,errors,symbols) in samples.items():
        p=OUT/'demo'/n;p.mkdir(parents=True,exist_ok=True)
        (p/'testfile.txt').write_text(code,encoding='utf-8')
        r=subprocess.run([str(exe)],cwd=p,capture_output=True,text=True,timeout=5)
        assert r.returncode==0 and not r.stdout and not r.stderr,(n,r)
        assert (p/'error.txt').read_text()==errors,n
        assert (p/'symbol.txt').read_text()==symbols,(n,(p/'symbol.txt').read_text())
        (p/'预期error.txt').write_text(errors,encoding='utf-8')
        (p/'预期symbol.txt').write_text(symbols,encoding='utf-8')
        report.append({'case':n,'passed':True})
(ROOT/'.build/demo_checks.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
source=' '.join('"'+(SRC/f).as_posix()+'"' for f in files)
(OUT/'demo/README.md').write_text('# 演示运行\n\n在 PowerShell 中进入某个样例目录（如 01_scope），运行：\n\n```powershell\ngcc -Wall -Wextra -std=c11 '+source+' -o compiler_demo.exe\n$LASTEXITCODE\n.\\compiler_demo.exe\nGet-Content .\\symbol.txt\nGet-Content .\\error.txt\n```\n\n编译成功再运行。上述源码绝对路径对应当前工作区；移动到其他电脑时替换成实际路径。每个样例自带 testfile.txt、实际输出和预期结果。程序分析 SysY，不会执行其中循环。\n',encoding='utf-8')
(ROOT/'.build/source_hashes.json').write_text(json.dumps({f:hashlib.sha256((SRC/f).read_bytes()).hexdigest() for f in files},indent=2),encoding='utf-8')
print('Prepared notes and four verified demos')
