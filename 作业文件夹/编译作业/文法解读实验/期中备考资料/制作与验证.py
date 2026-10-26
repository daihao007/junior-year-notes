from pathlib import Path
import shutil, subprocess, tempfile, json, re, html, zipfile
import markdown

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT.parent / '词法分析' / '语法分析'
FILES = ['parser.c','token.c','token.h','syntax_tree.c','syntax_tree.h','error_list.c','error_list.h','config.json']
BASE = (SOURCE/'parser.c').read_text(encoding='utf-8-sig')
CMAKE = '''cmake_minimum_required(VERSION 3.16)
project(midterm C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_executable(compiler parser.c token.c syntax_tree.c error_list.c)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(compiler PRIVATE -Wall -Wextra)
endif()
'''
DO = '''    // Stmt -> 'do' Stmt 'while' '(' Cond ')' ';'
    if (matchToken(parser, "DOTK"))
    {
        if (!parseStmt(parser) ||
            !matchToken(parser, "WHILETK") ||
            !matchToken(parser, "LPARENT") ||
            !parseCond(parser) ||
            !expectToken(parser, "RPARENT", 'j') ||
            !expectToken(parser, "SEMICN", 'i'))
        {
            rollbackParser(parser, start);
            return 0;
        }
        return finishNode(parser, start, "Stmt");
    }

'''
FOR_FUNC = '''// ForStmt -> LVal '=' Exp (no trailing semicolon)
int parseForStmt(Parser *parser)
{
    ParserMark start = markParser(parser);
    if (!parseLVal(parser) ||
        !matchToken(parser, "ASSIGN") ||
        !parseExp(parser))
    {
        rollbackParser(parser, start);
        return 0;
    }
    return finishNode(parser, start, "ForStmt");
}

'''
FOR = '''    // Stmt -> 'for' '(' [ForStmt] ';' [Cond] ';' [ForStmt] ')' Stmt
    if (matchToken(parser, "FORTK"))
    {
        if (!matchToken(parser, "LPARENT"))
        {
            rollbackParser(parser, start);
            return 0;
        }
        if (checkToken(parser, "IDENFR") && !parseForStmt(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }
        if (!expectToken(parser, "SEMICN", 'i'))
        {
            rollbackParser(parser, start);
            return 0;
        }
        if (isExpStart(parser) && !parseCond(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }
        if (!expectToken(parser, "SEMICN", 'i'))
        {
            rollbackParser(parser, start);
            return 0;
        }
        if (checkToken(parser, "IDENFR") && !parseForStmt(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }
        if (!expectToken(parser, "RPARENT", 'j') || !parseStmt(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }
        return finishNode(parser, start, "Stmt");
    }

'''
INC = '''    // UnaryExp -> '++' LVal
    if (matchToken(parser, "INC"))
    {
        if (!parseLVal(parser))
        {
            rollbackParser(parser, start);
            return 0;
        }
        return finishNode(parser, start, "UnaryExp");
    }

'''

def once(s, old, new):
    assert s.count(old)==1, (old,s.count(old))
    return s.replace(old,new,1)

def project(path, code, sample):
    path.mkdir(parents=True,exist_ok=True)
    for f in FILES: shutil.copy2(SOURCE/f,path/f)
    (path/'parser.c').write_text(code,encoding='utf-8')
    (path/'CMakeLists.txt').write_text(CMAKE,encoding='utf-8')
    (path/'testfile.txt').write_text(sample,encoding='utf-8')

project(ROOT/'starter',BASE,'int main() {\n    return 0;\n}\n')
do=once(BASE,'        {"while", "WHILETK"},','        {"do", "DOTK"},\n        {"while", "WHILETK"},')
do=once(do,'    // while \'(\' Cond \')\' Stmt',DO+'    // while \'(\' Cond \')\' Stmt')
fr=once(BASE,'        {"while", "WHILETK"},','        {"for", "FORTK"},\n        {"while", "WHILETK"},')
fr=once(fr,'int parseStmt(Parser *parser)\n{',FOR_FUNC+'int parseStmt(Parser *parser)\n{')
fr=once(fr,'    // while \'(\' Cond \')\' Stmt',FOR+'    // while \'(\' Cond \')\' Stmt')
plus=once(BASE,'        case \'=\':\n            return "EQL";','        case \'+\':\n            return "PLUSASSIGN";\n        case \'=\':\n            return "EQL";')
plus=once(plus,"            ch == '>' || ch == '&' || ch == '|')","            ch == '>' || ch == '&' || ch == '|' || ch == '+')")
plus=once(plus,'        if (matchToken(parser, "ASSIGN"))','        if (matchToken(parser, "ASSIGN") ||\n            matchToken(parser, "PLUSASSIGN"))')
inc=once(BASE,'    // UnaryOp UnaryExp',INC+'    // UnaryOp UnaryExp')
inc=once(inc,'           checkToken(parser, "NOT");','           checkToken(parser, "NOT") ||\n           checkToken(parser, "INC");')
inc=once(inc,'    if (second == \'=\')','    if (first == \'+\' && second == \'+\')\n    {\n        return "INC";\n    }\n\n    if (second == \'=\')')
inc=once(inc,"            ch == '>' || ch == '&' || ch == '|')","            ch == '>' || ch == '&' || ch == '|' || ch == '+')")
variants={'01_do_while':do,'02_for':fr,'03_plus_assign':plus,'04_prefix_inc':inc}
samples={
'01_do_while':'int main() {\n    int a = 0;\n    do { a = a + 1; } while (a < 3);\n    return 0;\n}\n',
'02_for':'int main() {\n    int i;\n    for (i = 0; i < 3; i = i + 1) { ; }\n    for (;;) ;\n    return 0;\n}\n',
'03_plus_assign':'int main() {\n    int a = 0;\n    a += 2;\n    return a;\n}\n',
'04_prefix_inc':'int main() {\n    int a = 0;\n    return ++a;\n}\n'}
for n,c in variants.items(): project(ROOT/'examples'/n,c,samples[n])

doc=r'''# 新增文法：按修改位置逐步完成

## 1. 拿到试卷先做一张修改清单

抄出新增的产生式、关键词/运算符类别码、需要输出的非终结符、错误码及行号规则。然后确定：新词法元素是什么、从哪个 parse 函数能进入、哪些旧分支具有相同开头、是否有可选成分、如何恢复错误。

| 文法变化 | 要检查的位置 |
|---|---|
| 新关键词 | getWordType 关键字表 + 使用它的 parse 分支 |
| 新单字符 | getSingleCharType + 对应语法层 |
| 新双字符 | getDoubleCharType + main 的双字符候选集合 + 语法层 |
| 新语句 | parseStmt 中赋值/表达式尝试之前 |
| 新声明前缀/类型 | parseDecl、parseBType、parseVarDecl 等 + parseBlockItem/isGlobalDeclStart/isFuncDefStart 的入口判断 |
| 新表达式开头 | 对应表达式函数 + isExpStart |
| 新二元运算层 | 新 parse 函数 + 上下层调用关系 + 输出节点名 |
| 数组维度增加 | parseLVal、ConstDef、VarDef、FuncFParam 对应位置，按试卷确定哪些支持 |
| 新错误 | 检测点、addError 或 expectToken、行号、消费/保留 token 的策略 |

每完成一项先编译，再测最小输入。不要到最后才发现关键字拼写不一致。

## 2. 将文法翻译成代码的规则

`A B C`：依次调用识别 A、B、C 的函数；任何失败则回退。`A | B`：开头不同优先看 token 分派；开头相同则前看或带完整回退试探。

`[A]`：当 A 的起始 token 出现时才尝试，尝试失败不能当作“A 不存在”。例如已有实参起始却解析失败，应返回失败。

`{A}`：循环读取 A；每轮成功应消费至少一个 token，并有明确结束条件及 EOF 检查。

`Ident { ',' Ident }`：先读一个 Ident，再循环消费逗号和下一个 Ident；不能接受尾随逗号，除非文法允许。

一个独立 parse 函数通常：保存 mark → 匹配/调用子函数 → 失败 rollback 并 return 0 → 成功 finishNode。若返回的只是一个辅助判断而非文法节点，不要额外输出虚构的节点名。

## 3. 演练一：do-while（完整代码）

训练文法：`Stmt → 'do' Stmt 'while' '(' Cond ')' ';'`。练习中约定新增类别码 DOTK，缺 `)`/`;` 沿用 j/i。实际试卷若不同，必须调整。

步骤一：在 getWordType 的 keywords 数组加入 `{"do", "DOTK"},`。无需修改 token.h。

步骤二：在 parseStmt 的 while 分支之前插入以下代码。它使用 parseStmt 已有的 start，不再重复定义。

```c
@@DO@@```

循环体可以是一条赋值、空语句或 Block，所以调用 parseStmt，不能写死为 parseBlock。条件使用 parseCond，支持比较和逻辑运算。

步骤三：用 examples/01_do_while 中的 testfile.txt 运行。lexer.txt 应出现 `DOTK do`，parser.txt 应完整结束于 `<CompUnit>`。

错误练习：删除 while 后面的右括号，应报告该条件最后一个 token 所在行的 j；删除结尾分号，报告前一个真实 token 所在行的 i。保留下一条 return，观察恢复是否允许整个程序继续分析。

这里未新增 DoStmt，所以最终仍包装 Stmt。文件对比时，只应看到关键字表和 parseStmt 的改动。

## 4. 演练二：for 与可选部分（完整代码）

本练习明确采用以下文法：

```text
Stmt → 'for' '(' [ForStmt] ';' [Cond] ';' [ForStmt] ')' Stmt
ForStmt → LVal '=' Exp
```

它不支持 `for (int i=0; ... )`、`i++` 或逗号赋值列表。那些是否允许，由实际文法决定。此练习要求输出 `<ForStmt>`，类别码用 FORTK，缺分号/右括号沿用 i/j。

步骤一：关键字表加入 `{"for", "FORTK"},`。

步骤二：在 parseStmt 定义之前增加下面函数。它的位置在 parseLVal、parseExp 定义之后，因此调用时已有声明/定义。

```c
@@FOR_FUNC@@```

ForStmt 只吃赋值，不吃分号！for 括号里的两个分号由外层 Stmt 负责；若直接调用普通 parseStmt，会误消费分隔符并产生错误的 `<Stmt>` 输出。

步骤三：在 parseStmt 的 while 分支之前插入：

```c
@@FOR@@```

第一、三段 ForStmt 只可能以 IDENFR 开头；第二段 Cond 用 isExpStart 判断。`for (;;) ;` 的三个可选部分都不存在，两个分号、右括号和循环体空语句仍必须消费。

可选段缺失与分隔符缺失的组合可能有歧义，例如第三段也是一个标识符起始的循环体。此实现处理下面验证样例的规定缺符号情况；如果试卷另有错误恢复约定或更宽文法，应按其规则调整前看，不能声称能恢复任意错误输入。

建议逐项验证：完整三段、全部省略、只保留条件、数组左值、嵌套循环、第一/第二分号缺失、右括号缺失、数组下标缺右方括号。不要把逻辑上的循环条件真/假当作前端测试的执行问题：这里不会运行循环。

## 5. 演练三：复合赋值 +=

训练文法：`Stmt → LVal '+=' Exp ';'`。约定类别 PLUSASSIGN。它只是一种语句，不能拿来当普通表达式中的运算符。

步骤一：getDoubleCharType 中 `if (second == '=')` 的 switch 增加：

```c
case '+':
    return "PLUSASSIGN";
```

步骤二：main 里触发双字符扫描的判断完整改成：

```c
if (ch == '=' || ch == '!' || ch == '<' ||
    ch == '>' || ch == '&' || ch == '|' || ch == '+')
```

步骤三：parseStmt 末尾“尝试赋值语句”分支中的等号匹配改成：

```c
if (matchToken(parser, "ASSIGN") ||
    matchToken(parser, "PLUSASSIGN"))
```

其余 parseExp、expectToken、finishNode 沿用原分支。`||` 短路意味着 ASSIGN 成功后不再匹配 PLUSASSIGN，不会多消费。

测试 `a += 2;` 应产生一个 `PLUSASSIGN +=`。测试 `a = a + 2;` 确保单独加号仍正确；测试 `a + = 2;` 应当无法按新文法成功解析，因为中间空白意味着两个 token。没有为这种结构错误规定类别码时，不要凭空输出 i 或 a。

关键点：最长匹配先读 +=，失败后才按 + 处理，且把未使用的第二个字符 ungetc 回去。

## 6. 演练四：前缀 ++ 与表达式起始集合

训练文法：`UnaryExp → '++' LVal`，类别码 INC。此练习只允许前缀 ++，不允许后缀 `a++`，也不允许 `++(a+1)`。

步骤一：getDoubleCharType 开头加：

```c
if (first == '+' && second == '+') {
    return "INC";
}
```

步骤二：和 += 练习一样，把 `+` 加入 main 的双字符扫描候选。

步骤三：在 parseUnaryExp 保存 start 后、一元运算分支之前插入：

```c
@@INC@@```

步骤四：isExpStart 的最后一项改为：

```c
checkToken(parser, "NOT") || checkToken(parser, "INC");
```

为什么必须改第四步？`return [Exp] ';'` 和函数调用的可选实参，都先用 isExpStart 判断。如果没加 INC，`++a;` 可能能解析，`return ++a;` 或 `f(++a)` 却被当作没有表达式，最终失败。这是新增文法常见的“部分样例通过”。

## 7. 新二元优先级怎样接入

假设题目新增 `PowExp → UnaryExp { '**' UnaryExp }`，并把 MulExp 的子成分由 UnaryExp 改成 PowExp。这只是左结合训练文法；数学幂常用右结合，必须看题目定义。

修改步骤：增加 ** 的词法识别及 `*` 的双字符候选；增加 parsePowExp，仿照 parseMulExp 的“先读一个、循环、每轮包装”结构；将 parseMulExp 中首次读取和循环右操作数读取两处 parseUnaryExp 都替换为 parsePowExp；新增函数必须在调用前声明/定义；核对是否输出 `<PowExp>`。

若题目是 `PowExp → UnaryExp [ '**' PowExp ]`，右操作数递归调用 parsePowExp，才形成右结合 `a ** (b ** c)`。不能机械复制 while 循环。

## 8. 多维数组怎样改

目前 parseLVal 中下标是可选一次的 `if`，若新增规则是 `LVal → Ident { '[' Exp ']' }`，改为循环读取每个维度；每轮仍用 expectToken(..., "RBRACK", 'k') 恢复缺右方括号。一个 LVal 内先读完全部下标，最后只包装一次 LVal，除非新文法规定嵌套结构。

不要只改 LVal：若定义、常量定义、形参的维数也扩展，分别检查 parseVarDef、parseConstDef、parseFuncFParam。定义的长度可能是 ConstExp，访问下标是 Exp，形参第一维可能不写长度，各位置不能混用。

## 9. 一道新规则的最低检查清单

1. 最小合法输入：lexer 类别正确，parser 完整且节点名称、数量正确。
2. 正常嵌套：放进 if、while、Block 或函数实参中（以文法允许为准）。
3. 可选分支：每一段存在/缺失都覆盖。
4. 缺符号：每一种规定错误分别测，核对行号和后面的语句能否继续。
5. 跨行：把缺失符号前后的 token 放在两行，验证“前一个真实 token 行号”。
6. 旧功能：原赋值、函数调用、数组、运算符、注释仍能通过。
7. 输出：没有调试字符、额外节点、漏 token 或重复错误。

示例目录已附完整正确/错误输入及实际输出，见每个项目 cases 文件夹。重新验证整个资料包可在本机运行 `python 制作与验证.py`，需要 Python 和 markdown 包；考场仅编译示例 C 文件时不需要 Python。
'''
for k,v in {'DO':DO,'FOR_FUNC':FOR_FUNC,'FOR':FOR,'INC':INC}.items(): doc=doc.replace('@@'+k+'@@',v)
(ROOT/'03_新增文法实战.md').write_text(doc,encoding='utf-8')

# Build a verified, searchable map from the actual starter sources.
notes=['# 函数定位表','', '行号对应本资料 starter 原始底稿，编辑后请用函数名搜索。','']
for f in ['parser.c','token.c','syntax_tree.c','error_list.c']:
    notes+=['## '+f,'','| 函数 | 行号 |','|---|---|']
    lines=(ROOT/'starter'/f).read_text(encoding='utf-8-sig').splitlines()
    for i,l in enumerate(lines,1):
        m=re.match(r'^(?:static\s+)?(?:const\s+)?(?:int|void|ParserMark|SyntaxNode\s*\*|char\s*\*|Token\s*\*)\s*(\w+)\s*\(',l)
        if m and not l.rstrip().endswith(';'): notes.append(f'| {m.group(1)} | {i} |')
    notes.append('')
(ROOT/'函数定位表.md').write_text('\n'.join(notes),encoding='utf-8')

cases=[
('minimal','int main(){return 0;}\n',''),
('legacy','int f(int a){return a;}\nint main(){int a[2]; a[0]=f(1+2*3); while(a[0]>0){a[0]=a[0]-1;} return 0;}\n',''),
('missing_semicolon','int main(){\nreturn 0\n}\n','2 i\n'),
('missing_paren','int main( {return 0;}\n','1 j\n'),
('missing_bracket','int main(){int a[2; return 0;}\n','1 k\n'),
('lone_and','int main(){if(1 & 1) ; return 0;}\n','1 a\n'),
]
extras={
'01_do_while':[
('nested','int main(){do do ; while(1); while(0); return 0;}\n',''),
('do_missing_j','int main(){\ndo ; while(1\n; return 0;}\n','2 j\n'),
('do_missing_i','int main(){\ndo ; while(1)\nreturn 0;}\n','2 i\n'),
('keyword_boundary','int main(){int doublex; doublex=1; return 0;}\n','')],
'02_for':[
('for_optional','int main(){for(;;); for(;1;); for(a=0;;); for(;;a=1); return 0;}\n',''),
('for_nested','int main(){for(a[0]=0;a[0]<2;a[0]=a[0]+1) for(;;) ; return 0;}\n',''),
('for_missing_first_i','int main(){\nfor(a=0\na<2;a=a+1); return 0;}\n','2 i\n'),
('for_missing_second_i','int main(){\nfor(a=0;a<2\na=a+1); return 0;}\n','2 i\n'),
('for_missing_j','int main(){\nfor(;;\n{;} return 0;}\n','2 j\n'),
('for_missing_k','int main(){\nfor(a[0=0;;); return 0;}\n','2 k\n')],
'03_plus_assign':[
('plus_single','int main(){int a; a=1+2; a+=3; a[0]+=4; return a;}\n',''),
('plus_missing_i','int main(){\na+=1\nreturn 0;}\n','2 i\n'),
('plus_missing_k','int main(){a[0+=1;return 0;}\n','1 k\n')],
'04_prefix_inc':[
('inc_starts','int f(int a){return a;} int main(){int a; ++a; f(++a); return ++a;}\n',''),
('inc_array','int main(){int a[2]; return ++a[0];}\n',''),
('inc_missing_k','int main(){return ++a[0;}\n','1 k\n'),
('inc_old_plus','int main(){return +1 + +2;}\n','')]
}
results=[]
with tempfile.TemporaryDirectory(prefix='midterm_verify_') as tmp:
    tmp=Path(tmp)
    for name,proj in [('starter',ROOT/'starter')]+[(n,ROOT/'examples'/n) for n in variants]:
        exe=tmp/(name+'.exe')
        cmd=['gcc','-Wall','-Wextra','-Werror','-std=c11']+[str(proj/f) for f in FILES if f.endswith('.c')]+['-o',str(exe)]
        r=subprocess.run(cmd,capture_output=True,text=True)
        assert r.returncode==0,(name,r.stderr)
        run_cases=cases+extras.get(name,[])
        if name in samples: run_cases+=[('provided_sample',samples[name],'')]
        for label,source,errors in run_cases:
            wd=tmp/(name+'_'+label);wd.mkdir()
            (wd/'testfile.txt').write_text(source,encoding='utf-8')
            r=subprocess.run([str(exe)],cwd=wd,capture_output=True,text=True,timeout=5)
            actual=(wd/'error.txt').read_text();parsed=(wd/'parser.txt').read_text()
            assert r.returncode==0 and not r.stdout and not r.stderr,(name,label,r)
            assert actual==errors,(name,label,actual,errors)
            assert (parsed.endswith('<CompUnit>\n') if not errors else parsed==''),(name,label,parsed[-100:])
            lex=(wd/'lexer.txt').read_text()
            if label=='provided_sample':
                token={'01_do_while':'DOTK do','02_for':'FORTK for','03_plus_assign':'PLUSASSIGN +=','04_prefix_inc':'INC ++'}[name]
                assert token in lex
                if name=='02_for': assert parsed.count('<ForStmt>')==2
            case_dir=proj/'cases'/label;case_dir.mkdir(parents=True,exist_ok=True)
            for fn in ['testfile.txt','lexer.txt','parser.txt','error.txt']: shutil.copy2(wd/fn,case_dir/fn)
            results.append({'project':name,'case':label,'passed':True,'expected_errors':errors})
    # Invalid spaced += must not be accepted as one token or a complete program.
    wd=tmp/'spaced';wd.mkdir();(wd/'testfile.txt').write_text('int main(){a + = 2;return 0;}')
    r=subprocess.run([str(tmp/'03_plus_assign.exe')],cwd=wd,capture_output=True,text=True,timeout=5)
    assert (wd/'parser.txt').read_text()==''
    assert 'PLUSASSIGN' not in (wd/'lexer.txt').read_text()
    results.append({'project':'03_plus_assign','case':'spaced_operator_rejected','passed':True})

report={'scope':'Local GCC compilation and representative lexer/parser/error-recovery checks; no platform or lab execution',
        'gcc':subprocess.check_output(['gcc','--version'],text=True).splitlines()[0],
        'cmake':'Configuration provided per lab PDF; cmake/ninja unavailable in current PATH, not executed',
        'projects_compiled':5,'passed':len(results),'cases':results}
(ROOT/'练习验证结果.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
materials=ROOT/'原始材料';materials.mkdir(exist_ok=True)
shutil.copy2(Path('E:/Users/daihao/Downloads/机房环境说明.pdf'),materials/'机房环境说明.pdf')
css='''body{max-width:960px;margin:40px auto;padding:0 28px;color:#172331;font:17px/1.8 "Microsoft YaHei",sans-serif}h1,h2,h3{line-height:1.4;color:#153d62}h2{margin-top:2em;border-bottom:1px solid #ccd7e1;padding-bottom:.3em}pre{background:#f2f5f8;padding:16px;border:1px solid #d7e0e8;white-space:pre-wrap;overflow-wrap:anywhere;font:14px/1.6 Consolas,monospace}code{font-family:Consolas,monospace}table{border-collapse:collapse;width:100%;font-size:15px}th,td{padding:8px;border:1px solid #ccd7e1;text-align:left;vertical-align:top}th{background:#eef3f7}a{color:#175ea0}@media print{body{margin:0;max-width:none;font-size:11pt}pre{font-size:9pt}h1,h2,h3{break-after:avoid}tr{break-inside:avoid}thead{display:table-header-group}}'''
nav='<nav>'+ ' | '.join(f'<a href="{p.stem}.html">{html.escape(p.stem)}</a>' for p in sorted(ROOT.glob('*.md')))+'</nav>'
for p in ROOT.glob('*.md'):
    body=markdown.markdown(p.read_text(encoding='utf-8'),extensions=['tables','fenced_code','toc'])
    p.with_suffix('.html').write_text('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>'+html.escape(p.stem)+'</title><style>'+css+'</style>'+nav+body+'</html>',encoding='utf-8')
archive=ROOT.parent/'期中备考资料.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
    for p in sorted(ROOT.rglob('*')):
        if p.is_file(): z.write(p,p.relative_to(ROOT.parent))
with zipfile.ZipFile(archive) as z: assert z.testzip() is None
print(json.dumps({'archive':str(archive),'checks_passed':len(results),'projects':5},ensure_ascii=False))
