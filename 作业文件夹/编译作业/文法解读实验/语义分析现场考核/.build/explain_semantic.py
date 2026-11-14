from pathlib import Path
import re
from datetime import date

root=Path(__file__).resolve().parents[1]
source=root.parent/'词法分析/semantic.c'
code=source.read_text(encoding='utf-8-sig')
pattern=re.compile(r'^(?:static\s+)?(?:void|int|DataType|long long|const Symbol\s*\*|const SyntaxNode\s*\*|const Token\s*\*)\s*(\w+)\s*\([^;{}]*\)\s*\{',re.M)
functions={m.group(1):(code.count('\n',0,m.start())+1,code[m.start():m.end()-1].strip()) for m in pattern.finditer(code)}
entries=[]
def add(name,brief,inputs,result,body):
    assert name in functions,name
    entries.append((name,brief,inputs,result,body))

add('initSemanticContext','初始化本次语义分析的共享状态。','context 是要初始化的对象；symbols、errors 是外部已经初始化的符号表和错误表。','void；直接修改 context。',r'''
把两个表的地址保存起来，将 currentReturnType 初始化为 TYPE_VOID，将 loopDepth、switchDepth、memoryError 全部设为 0。

它不负责分配符号表，也不创建全局作用域。实际全局作用域由 analyzeSemantics 创建。保存指针意味着语义分析中修改的是 main 所持有的同一份表。
''')
add('isNode','判断节点名称是否等于指定名称。','node 是待判断节点；name 如 "Stmt"、"IDENFR"。','相同返回 1；空节点或不同返回 0。',r'''
实现为 `node != NULL && strcmp(node->name, name) == 0`。`&&` 的短路性质使空节点不会继续访问 name。

既能判断非终结符名称，也能判断终结符类别。这里比较的是节点名，不是源程序中的名字；例如 IDENFR 是节点类别，变量 x 则保存在 token->value 中。
''')
add('findChild','寻找指定名称的第一个直接子节点。','node 是父节点；name 是目标子节点名。','命中返回 const SyntaxNode *；找不到返回 NULL。',r'''
按 children 的顺序检查，每个孩子用 isNode 判断。**只检查一层，不递归搜索后代。**

这一点对语义分派很重要：判断 Stmt 是否直接包含 RETURNTK，只会识别当前 return 语句，不会把内部嵌套的 return 当作当前语句。

只返回第一个匹配项。if 有两条 Stmt、调用有多个 Exp 时，需要自行遍历 children，不能反复调用 findChild 来取得后续项。
''')
add('reportSemanticError','将指定行号和错误码加入统一错误表。','context 提供错误表；line 是已经确定的源程序行号；code 如 e、c。','void；可能增加错误表记录或设置 memoryError。',r'''
已有 memoryError 时直接返回；否则调用 addError。若错误记录所需内存分配失败，将 memoryError 置 1。

发现普通语义错误与分析器无法继续工作是两件事：前者记录后继续，后者用 memoryError 停止后续分析。这个函数不直接写 error.txt，最终写文件由 main 调用错误模块完成。
''')
add('resolveIdentifier','按作用域解析一个名字，并在找不到时报告未定义错误。','identifier 应当是带 token 的标识符节点；context 提供符号表和错误表。','找到返回符号表中的 const Symbol *；失败返回 NULL。',r'''
先取 token->value 作为名字，调用 symbol.c 的 findSymbol。查找从当前作用域沿父链进行；未命中时在标识符所在行报告 c。

这里还不判断名字是变量还是函数，调用者再检查 kind。例如找到了变量 a，但写 a()，应由 getCallType 报类型使用错误 e。

返回的是符号表内部记录的指针，不需要 free；符号数组 realloc 后地址可能变化，不应跨越后续插入长期保存。
''')
add('getBaseType','从 BType 或 FuncType 中提取基本类型。','node 必须是 BType 或 FuncType。','TYPE_INT / TYPE_CHAR；FuncType 也可能返回 TYPE_VOID；不符合结构返回 TYPE_ERROR。',r'''
通过直接子节点 INTTK、CHARTK、VOIDTK 判断。void 只允许从 FuncType 得到，BType 不接受 void。

它不判断数组。比如 `int a[3]` 的 BType 仍先得到 TYPE_INT，再由 getDefinitionType 根据定义中的方括号得到 TYPE_INT_ARRAY。
''')
add('getDefinitionType','把基本类型细化为标量或数组类型。','definition 是 ConstDef、VarDef 或 FuncFParam 等定义节点；baseType 是 int 或 char。','TYPE_INT / TYPE_CHAR / TYPE_INT_ARRAY / TYPE_CHAR_ARRAY；无效输入返回 TYPE_ERROR。',r'''
定义节点直接包含 LBRACK 时，int 对应 IntArray，char 对应 CharArray；没有方括号则保留基本类型。

这也是形参 `char s[]` 能登记为字符数组的原因。它不计算数组长度，也不保存维度或容量，当前类型系统区分的是基本元素类型和是否为数组。
''')
add('getNumberType','判断 Number 节点是整数还是字符常量。','node 应为 Number。','INTCON 返回 TYPE_INT，CHARCON 返回 TYPE_CHAR；其他结构返回 TYPE_ERROR。',r'''
这里只看 token 类别，不计算字面量的值。例如 `123` 的类型是 int，`'a'` 的类型是 char。

它与 getNumberValue 分工不同：一个取得类型，另一个将数字/字符标签转换成可比较的数值。
''')
add('isScalarType','判断是否为可参与当前标量运算的类型。','type 是 DataType 枚举值。','int 或 char 返回 1；数组、void、error 返回 0。',r'''
常用于二元运算、一元运算、赋值目标和条件检查。数组本身不是标量，但数组元素经 getLValType 处理后可以成为标量。
''')
add('getBinaryResultType','检查二元运算的两侧类型并返回运算结果类型。','leftType、rightType 来自左右子表达式；operatorNode 提供运算符类别和行号。','合法算术返回操作数类型；关系/逻辑运算返回 TYPE_INT；错误返回 TYPE_ERROR。',r'''
处理顺序：

1. 内部状态或运算符无效，返回 TYPE_ERROR。
2. 任意子表达式已经 TYPE_ERROR，直接传播，不再补报 e。
3. 两侧必须是**同类型标量**，否则在运算符行报告 e。
4. `+ - * / %` 保留两侧共同的类型。
5. `< <= > >= == != && ||` 返回 int。

例如 `'a' + 'b'` 在当前实现中得到 char，`1 + 'a'` 报 e。这里采用本作业的类型规则，不能用标准 C 的自动整型提升解释。

函数只判断类型，不做四则运算求值、除零检查或逻辑短路执行。
''')
add('getFirstToken','取得一棵子树按孩子顺序遇到的第一个真实 token。','node 可以是终结符或非终结符，也可以为空。','找到返回 const Token *；空树或没有 token 返回 NULL。',r'''
终结符直接返回 node->token；非终结符按 children 顺序递归，遇到第一个非空结果立即返回。

非终结符自己通常没有源程序行号，借这个函数可以为表达式节点定位报错行。它和 findChild 不同：getFirstToken 会递归向下寻找。
''')
add('reportNodeError','根据一个节点定位行号，然后记录错误。','node 表示需要定位的语法成分；code 是错误码；context 提供错误表。','void。',r'''
先调用 getFirstToken，再调用 reportSemanticError；没有真实 token 时不记录。

例如赋值类型错误传入右侧 Exp，报在右侧表达式的首 token 行；g 错误传入函数右花括号节点，报在右花括号行。错误在哪一行，取决于调用者传入哪个节点。
''')
add('getLValType','检查变量或数组访问，并给出左值表达式类型。','node 是 LVal；indexType 是调用者提前分析下标所得类型。无下标时传入的 TYPE_ERROR 不会用于检查。','变量类型或数组元素类型；不合法返回 TYPE_ERROR。',r'''
先 resolveIdentifier；未定义由它报告 c。查到函数却当变量用，报告 e。

没有下标时直接返回 symbol->type，所以数组名 a 可以保留 TYPE_INT_ARRAY，用于数组形参匹配。有下标时要求名字本身是数组，且下标类型是 TYPE_INT；合法后 IntArray 转为 Int，CharArray 转为 Char。

`a['x']` 的下标不是 int，报在下标首 token；对标量写 `x[0]`，报在名字处。不检查下标是否越界，也不求出实际下标数值。
''')
add('getCallType','将实参类型列表与查到的函数签名匹配。','node 是函数调用形式的 UnaryExp；argumentTypes 是有序实参类型数组；argumentCount 是实参数量。','检查通过返回函数返回类型，失败返回 TYPE_ERROR。',r'''
先 resolveIdentifier 找函数名；名字不存在报 c。找到但 kind 不是 SYMBOL_FUNC，报 e。然后依次检查：

1. 个数不同，在函数名所在行报 d，停止签名匹配。
2. 个数一致，按位置比较形参与实参类型；首个明确不符报 e。
3. 遇到 TYPE_ERROR 的实参，标记已出错并跳过该项的比较；其他有效项仍可继续比较。
4. 全部匹配且没有实参类型错误，返回 function->type。

getCallType 不遍历实参语法树，这项工作由 analyzeCall 完成。
''')
add('analyzeCall','遍历实参表达式，收集类型，再调用 getCallType。','node 是调用形式的 UnaryExp；context 提供符号表和错误状态。','整个函数调用表达式的 DataType。',r'''
找到 FuncRParams，先数直接 Exp 孩子的数量。零参数直接以 NULL、0 调 getCallType。

有参数时分配 DataType 数组，逐个调用 analyzeExpression 保存类型，再匹配签名，最后 free 临时数组。内存分配失败置 memoryError；实参分析遇到内部内存错误时也先释放数组再返回。

先分析实参再匹配签名，意味着即使参数数量不对，实参内部的未定义名字等错误也有机会被发现。
''')
add('analyzeExpression','递归计算表达式的类型，是类型检查的核心。','node 是受支持的表达式节点；context 允许递归过程中查符号、报错。','DataType；无法得到有效类型返回 TYPE_ERROR。',r'''
这里的“计算”主要指类型推导，不是执行表达式。按节点分类处理：

| 节点 | 处理 |
|---|---|
| Number | getNumberType |
| LVal | 先分析下标，再交 getLValType |
| UnaryExp 直接含 IDENFR | 函数调用，交 analyzeCall |
| UnaryExp 含 BType | 显式转换；操作数须为标量，返回目标类型 |
| UnaryExp 含 UnaryOp | 操作数须为标量；! 返回 int，单目 +/- 保留原类型 |
| 普通 UnaryExp | 递归 PrimaryExp |
| 六类二元层节点 | 一个孩子则透传；三个孩子则分析左右，调用 getBinaryResultType |
| PrimaryExp / Exp / ConstExp / Cond | 找第一个非终结符孩子，递归透传 |

例如 `a + 1`：递归查 a 得到 int，读 1 得到 int，由 getBinaryResultType 确认 int+int 并返回 int。

若 a 未定义，左边先报 c 并返回 TYPE_ERROR，上层传播错误类型，通常不会再为同一原因额外报“加法类型错误”“赋值类型错误”。独立的右子树仍会被分析。

注意 ConstExp 在这里同样走表达式类型推导，**不能据此声称已经检查所有常量表达式约束或完成常量折叠**。
''')
add('registerDefinition','将一项变量、常量或形参定义登记入符号表。','definition 提供标识符和数组标记；baseType 是 int/char；kind 区分变量、常量、static。','void；新增符号，或报告 b，或设置 memoryError。',r'''
取直接 IDENFR，getDefinitionType 得到具体类型，再构造临时 Symbol，填写名字、类型、种类和声明行号。

调用 addSymbol：返回 1 表示成功；0 表示当前作用域同名，报告 b；负数表示内部失败，设置 memoryError。scopeId 和动态字段的独立复制由 symbol.c 中的 addSymbol 负责。

它不检查初值。声明处理会继续调用 analyzeDefinitionValues。
''')
add('analyzeDefinitionValues','检查数组长度和初始化表达式的类型。','definition 是一项定义；baseType 是基本元素类型。','void；必要时报告 e。',r'''
先找定义直接包含的 ConstExp 作为数组长度；若推导类型不是 int 且不是 TYPE_ERROR，报告 e。

再找 InitVal 或 ConstInitVal：

- 无初值则结束。
- 字符串初值只接受字符数组，其他定义类型报告 e。
- 花括号初值的每项与 baseType 比较，即数组元素应是什么类型。
- 单个表达式与 definitionType 比较。

reportedMismatch 控制一次初始化的直接类型不匹配最多记录一次，但仍继续分析后面的项，发现其中独立的表达式错误。

例如 `int a[2] = {1, 'x'};` 第二项类型不符。该函数不求数组长度数值，不检查初始化项数是否超过数组长度。
''')
add('analyzeDeclaration','分析整个 ConstDecl 或 VarDecl。','declaration 应为常量或变量声明节点。','void。',r'''
ConstDecl 使用 SYMBOL_CONST 和 ConstDef；VarDecl 根据是否有 STATICTK 选择 SYMBOL_STATIC_VAR 或 SYMBOL_VAR，并处理 VarDef。

从 BType 得到 baseType，再按孩子顺序对每项定义执行 `registerDefinition → analyzeDefinitionValues`。因此 `int a=1, b=a;` 处理 b 时，a 已经在符号表中。

每一项都是先登记自己再分析初值，这是当前代码的实际顺序。没有实现“读到未初始化变量就报错”的确定赋值分析。static 在本阶段作为种类信息记录，不负责安排运行时存储。
''')
add('analyzeBlock','按顺序分析 Block，并根据参数决定是否创建作用域。','block 是 Block 节点；createScope=1 新建作用域，0 使用当前作用域。','void。',r'''
需要新建时先 enterScope，失败设置 memoryError。遍历块内孩子，逐个 analyzeNode；结束时如果自己创建过作用域，则 leaveScope。

普通嵌套块由 analyzeNode 传 1。函数外层块由 analyzeFunction 传 0，因为函数已经建立了形参与局部变量共用的作用域。否则参数 `int x` 和外层局部 `int x` 会被错误地当作不同层。

遍历中即使因 memoryError 提前停止，也会执行后面的作用域退出清理。
''')
add('registerParameters','把每个形参作为函数内部的变量登记。','parameters 是 FuncFParams；没有形参时可为 NULL。','void。',r'''
遍历直接 FuncFParam，读其 BType，然后调用 registerDefinition(..., SYMBOL_VAR)。数组形参由 getDefinitionType 转换为数组类型。

调用时必须已经进入函数作用域。形参同名也会经过 addSymbol 报 b。它登记的是可在函数体里查找的名字；函数签名中的类型数组则由 collectParameterTypes 负责，两者用途不同。
''')
add('collectParameterTypes','提取有序形参类型列表，建立函数签名。','parameters 是形参树；function 是待填写 paramTypes、paramCount 的临时函数符号。','成功返回 1，内部内存失败或已有 memoryError 返回 0。',r'''
先将参数指针设 NULL、数量设 0。无形参直接成功；否则第一遍数 FuncFParam，分配类型数组，第二遍按源顺序读每个形参的基础类型和数组属性。

数组交给 function 暂时持有。analyzeFunction 调 addSymbol 深复制后，会 free 这份临时数组。因此签名在函数体分析前即可使用，并支持递归调用的类型检查。
''')
add('analyzeAssignment','检查赋值两侧类型与常量可修改性。','statement 是直接含 ASSIGN 的 Stmt。','void；可能报告 h、e 或由子表达式报告其他错误。',r'''
先分别分析左 LVal 和右 Exp，再查左边标识符的 Symbol。

左类型有效且种类为 SYMBOL_CONST，报告 h 并结束本次赋值检查。否则左右类型均有效时，要求左边为标量，且左右类型相等；不符则在右表达式的首 token 行报 e。

即使最后发现左边是常量，右侧也已经被遍历，所以右侧独立错误仍可能出现。数组元素的类型由 getLValType 处理，常量数组元素仍能根据原符号 kind 检查修改限制。
''')
add('analyzeReturn','检查单条 return 语句与当前函数返回类型是否一致。','statement 是 return Stmt；context->currentReturnType 保存函数声明类型。','void；可能报告 e 或 f。',r'''
有 Exp 时调用 analyzeExpression；没有 Exp 时将 actualType 视为 TYPE_VOID。

- 当前函数为 void，return 带任何表达式就报告 f，即使该表达式自身返回 void。
- 当前函数为 int/char，actualType 有效但与声明不同，报告 e；裸 return 因 TYPE_VOID 不匹配也会报 e。
- 表达式为 TYPE_ERROR，则跳过依赖类型的比较，减少连带报错。

它检查的是“这条 return 写得对不对”，不是“函数末尾有没有 return”；后者由 checkFunctionReturn 检查。
''')
add('getFormatType','将 printf 的格式字符映射成期望实参类型。','specifier 是百分号后一个字符，如 d、c、s。','d→TYPE_INT，c→TYPE_CHAR，s→TYPE_CHAR_ARRAY；其他返回 TYPE_ERROR。',r'''
本函数只做一个字符的映射，不扫描完整字符串，也不直接记录错误。analyzePrintf 用它来计数占位符以及匹配类型。
''')
add('analyzePrintf','检查 printf 的占位符数量和对应实参类型。','statement 是 printf Stmt，含格式 STRCON 和若干直接 Exp。','void；数量不符报告 l，匹配位置的类型不符报告 e。',r'''
第一遍扫描格式字符串，只有 getFormatType 能识别的 `%d/%c/%s` 才计数。再统计直接 Exp 实参数量。

数量不同，在 printf 关键字行报 l；随后仍递归分析所有实参内部的错误，但跳过占位符类型匹配。数量一致，则用 position 顺序找到对应格式项，把每个实参类型与期望比较。

reportedMismatch 让该 printf 自己的类型不匹配最多报一次 e；各实参内部的独立错误仍可报告。此函数针对当前作业的格式规则，不是完整标准 C printf 格式解析器。
''')
add('checkFunctionReturn','检查非 void 函数是否以直接的顶层 return 结束。','body 是函数最外层 Block；当前返回类型在 context 中。','void；不满足末条规则时，在函数右花括号行报告 g。',r'''
void 函数跳过。扫描 body 的直接孩子，保存最后一个 BlockItem，再找该项直接包含的 Stmt，检查 Stmt 的直接孩子是否有 RETURNTK。

```c
int f(int x) {
    if (x) return 1;
    else return 2;
}
```

即使两个分支都有 return，最后一条顶层语句仍是 if；当前实现会按末条规则报告 g。它没有做控制流全路径分析。

`int f(){ return; }` 有末条 return，因此该函数不报 g；返回类型不符由 analyzeReturn 报 e。这两个检查互相补充。
''')
add('analyzeCondition','检查条件表达式是否为标量类型。','condition 是 Cond 等条件节点。','void；已知类型为数组或 void 时报告 e。',r'''
先调用 analyzeExpression，然后在类型有效且不是 int/char 时，通过 reportNodeError 报 e。

不计算条件真假，也不按真假决定是否分析分支。TYPE_ERROR 会直接跳过本层的再次报错。
''')
add('analyzeWhile','分析 while 条件，并维护循环嵌套深度。','statement 是直接包含 WHILETK 的 Stmt。','void；通过条件和循环体触发相应检查。',r'''
调用顺序为 `analyzeCondition → loopDepth++ → analyzeNode(循环体) → loopDepth--`。

无论循环条件是常量真还是假，仍分析循环体。嵌套时使用计数而非布尔值，退出内层后外层循环仍然有效。若循环体是 Block，由 analyzeNode/analyzeBlock 处理其作用域。
''')
add('analyzeIf','检查 if 条件并分析 then/else 分支。','statement 是 if Stmt。','void。',r'''
先 analyzeCondition，再遍历该语句直接包含的全部 Stmt。没有 else 时通常一个，存在 else 时通常两个。

这里需要遍历全部直接 Stmt，而不是 findChild 一次，否则会遗漏 else。分支是否运行由未来执行阶段决定，语义分析会检查两边。
''')
add('analyzeJump','检查 break、continue 是否处在允许的上下文。','statement 是 break 或 continue Stmt；context 保存两种深度。','void；不合法时在关键字行报告 m。',r'''
| 语句 | 合法条件 |
|---|---|
| break | loopDepth>0 或 switchDepth>0 |
| continue | loopDepth>0 |

单独的 switch 中 continue 非法；while 内嵌套 switch 时 continue 合法，因为可以继续外层循环。

函数本身不计算跳转目标，也不生成指令，只依据当前遍历状态判断语义是否合法。
''')
add('getNumberValue','把 case 的 Number 字面量转换成整数值。','number 是已由语法阶段保证结构有效的 Number。','long long，供重复标签比较使用。',r'''
INTCON 用 strtoll(...,10) 按十进制读取。CHARCON 普通字符取对应字节值；转义字符处理 `\n`、`\t`、`\r`、`\0`、反斜杠和单引号；default 分支返回转义后字符本身的字节值。

例如两个拼写不同但代表相同数值的有效标签，可按值比较。这里假设存在合法 INTCON 或 CHARCON，不是面向任意损坏树的通用转换函数。

它只处理 Number，不递归计算 `1+2`，不能替代完整的常量表达式求值。
''')
add('analyzeSwitch','分析选择表达式、case/default 标签和各分支。','statement 是 switch Stmt。','void；可能报告 e、n，分支内也能报告其他错误。',r'''
先分析 selector，要求类型为 int/char。再统计 CaseStmt 数量，分配本次 switch 独立使用的 CaseValue 数组。

CaseValue 保存 `type + value`；另有 seenDefault 标志。switchDepth 增加后逐个处理标签：

1. default 已出现过，再出现报告 n。
2. case 类型与有效 selector 类型不一致，报告 e。
3. 否则解析数值，在已保存的同类型值中查重；重复报告 n，未出现则保存。
4. 标签即使出错，仍分析该分支所有直接 Stmt。

当前代码对类型已不符的 case 不再进入同一条 else-if 下的重复值检查。各 switch 的 seen 数组是函数局部数据，嵌套 switch 不相互污染。

最后 switchDepth-- 并 free(seen)。switch 的花括号在当前文法中不等同于 Block，这里没有自动 enterScope；分支中的真正 Block 才会建作用域。
''')
add('analyzeFunction','将函数登记、作用域、形参、函数体和返回检查组合起来。','node 应为 FuncDef 或 MainFuncDef。','void；可能报告 b，或由函数体产生其他语义错误。',r'''
按以下顺序理解整个函数：

1. 取得 Block、FuncFParams，普通函数从 FuncType 读返回类型，main 固定 int。
2. 普通函数先构造 SYMBOL_FUNC，调用 collectParameterTypes 保存签名。
3. 仍在全局作用域时 addSymbol；同名报 b。addSymbol 深复制参数表后，释放临时参数数组。
4. main 不插入符号表，但后续函数体检查仍会执行。
5. enterScope，建立形参和最外层局部变量共享的函数作用域。
6. 保存 previousReturnType/LoopDepth/SwitchDepth，设置本函数返回类型并把两种深度归零。
7. registerParameters → analyzeBlock(body,0) → checkFunctionReturn。
8. leaveScope，并恢复进入前的三个上下文值。

为什么先登记函数再分析函数体？函数内部递归调用自己时，已经能查到函数签名。为什么 analyzeBlock 传 0？避免再多建一层，形参应与函数最外层局部变量同层查重。

普通重定义错误 b 不会直接让整个函数体被忽略，只有内部无法继续等情形才提前退出。当前按源顺序遍历，没有先给所有后定义函数做声明，所以不应声称支持任意前向调用。
''')
add('analyzeNode','根据节点类别分派任务，是语义树遍历的总调度函数。','node 可以是任意遍历到的语法树节点。','void；将工作交给专门函数。',r'''
先排除空节点、memoryError 和终结符，然后处理：声明 → 函数 → Block → while/if/switch → break/continue → printf → 赋值 → return → 表达式。

具体类别交给对应函数后立即 return。原因是专门函数已经负责分析相应的子树；如果再执行末尾的通用递归，会重复登记符号或重复报错。

CompUnit、Decl、BlockItem 等包装节点没有专门分支时，才按 children 顺序继续 analyzeNode。普通语法树节点本身不一定要做语义检查，它们可以只是到达内部声明和语句的路径。

findChild 只检查直接孩子，因此外层 if 不会因为子句含 ASSIGN 而被误识别成赋值语句。
''')
add('registerBuiltin','登记一个运行库函数签名。','name 是库函数名；returnType、paramTypes、paramCount 描述签名。','void；插入失败时设置 memoryError。',r'''
构造临时 Symbol，设置 kind=SYMBOL_FUNC、isBuiltin=1，再 addSymbol。

它只用于初始化空的全局环境，按设计应一次成功，因此非成功返回统一按内部错误处理，不向用户报告 b。

addSymbol 深复制名字和参数表，所以 registerBuiltins 中的局部类型数组在该函数返回后仍不会造成悬空指针。isBuiltin 供符号输出阶段过滤，不影响正常查找和调用检查。
''')
add('registerBuiltins','一次注册当前程序允许调用的运行库函数。','context 应已处于新建的全局作用域。','void。',r'''
| 函数名 | 返回类型 | 形参类型 |
|---|---|---|
| get_int | int | 无 |
| get_char | char | 无 |
| get_string | void | char[]、int |
| put_int | void | int |
| put_char | void | char |
| put_str | void | char[] |
| put_string | void | char[] |

代码同时登记 put_str 和 put_string，注释解释为原文法名称栏和原型的命名差异。printf 是 parser 中的专用语句，不在这里注册为普通函数。

这些函数在符号表里可查，但 symbol.c 的 writeSymbolOutput 会跳过 isBuiltin 的符号。
''')
add('analyzeSemantics','从 CompUnit 根节点启动完整语义分析。','context 已初始化；root 应为 CompUnit。','正常完成返回 1；根节点无效、已有 memoryError 或作用域分配失败等返回 0。',r'''
流程：校验根节点 → enterScope 创建全局 1 → registerBuiltins → analyzeNode(root) → leaveScope → 返回 !memoryError。

**返回 1 表示分析过程完成，不表示源程序没有语义错误。** 普通错误保存在 errors 中，main 还要检查 errors.size 是否为 0，才决定是否输出正式符号表。

它只调用符号表和错误表接口，不直接写 symbol.txt/error.txt，也不释放整个语法树；这些收尾工作由 main 和相应模块处理。
''')

assert set(functions)=={x[0] for x in entries},set(functions)-{x[0] for x in entries}
header=r'''# semantic.c 逐函数讲解

根据 **2026-11-14 当前工作区的 `词法分析/semantic.c`** 整理，共 **COUNT 个函数定义**。文件开头的 analyzeNode、analyzeExpression 是前置声明，后面才是定义，不应重复计数。行号对应整理时的版本；代码修改后优先搜索函数名。

## 一、先理解整体分工

semantic.c 在已有语法树上进行语义检查。它通过 symbol.c 管理名字和作用域，通过 error_list.c 保存错误，供 main 最后输出。

```text
main（parser.c）
  ├─ initSemanticContext
  └─ analyzeSemantics
       ├─ 创建全局作用域 + registerBuiltins
       └─ analyzeNode（按源程序顺序分派）
            ├─ analyzeDeclaration → 登记名字 + 检查初值
            ├─ analyzeFunction → 登记签名 + 形参 + 函数体
            ├─ analyzeBlock → 管理嵌套作用域
            ├─ analyzeAssignment / analyzeReturn / analyzePrintf
            ├─ analyzeWhile / analyzeIf / analyzeSwitch / analyzeJump
            └─ analyzeExpression → 递归推导类型
```

这里是语义检查器，不执行 SysY 程序。例如 if 的两条分支都会检查，while 不会按条件反复执行；类型检查也不等于已经算出表达式的数值。

### 1. 经常出现的参数

| 名称 | 含义 |
|---|---|
| context | 一次分析共享的上下文，携带符号表、错误表及控制状态 |
| node | 当前要分析的语法树节点 |
| declaration / definition | 整条声明 / 其中一个名字的定义，例如 int a,b 有一个声明、两个定义 |
| statement / body | 当前语句 / 函数最外层 Block |
| parameters | 形参或实参列表；具体看 FuncFParams、FuncRParams |

### 2. SemanticContext 的字段

| 字段 | 用途 |
|---|---|
| symbols | 指向 SymbolTable，登记名字、解析可见符号 |
| errors | 指向统一 ErrorList，保存行号和类别码 |
| currentReturnType | 当前函数声明的返回类型 |
| loopDepth | 当前嵌套的 while 层数 |
| switchDepth | 当前嵌套的 switch 层数 |
| memoryError | 内存分配等内部失败，无法继续分析时置 1 |

### 3. 三个常见误解

- `TYPE_ERROR`：类型推导失败的哨兵，用于传播并减少连带报错。普通语义错误后通常仍继续分析其他部分。
- `memoryError`：分析器内部无法继续。它与一个表达式的普通类型错误不同。
- 大多数函数带 `static`：只在 semantic.c 内可见。对外接口是 initSemanticContext 和 analyzeSemantics。

## 二、函数速查

| 序号 | 函数 | 起始行 | 一句话用途 |
|---|---|---|---|
'''.replace('COUNT',str(len(entries)))
for i,(name,brief,*_) in enumerate(entries,1):
    header+=f'| {i} | [{name}](#f{i:02}) | {functions[name][0]} | {brief} |\n'
doc=header+'\n## 三、按源码顺序逐个解释\n\n'
for i,(name,brief,inputs,result,body) in enumerate(entries,1):
    line,sig=functions[name]
    link=source.as_posix()
    doc+=f'<a id="f{i:02}"></a>\n\n### {i}. {name}\n\n**作用：** {brief}\n\n[打开源码，第 {line} 行]({link}:{line})\n\n```c\n{sig};\n```\n\n**参数：** {inputs}\n\n**返回/状态：** {result}\n\n**执行过程与理解要点：**\n'+body.strip()+'\n\n'
doc+=r'''## 四、用三个例子把函数串起来

### 例一：声明与使用名字

```c
int main() {
    int a = 1;
    a = a + 2;
    return a;
}
```

1. analyzeFunction 为 main 建立函数作用域，返回类型设为 int。
2. analyzeBlock(body,0) 使用已建作用域。
3. analyzeDeclaration 将 a 通过 registerDefinition 登记，再检查初值 1 是 int。
4. analyzeAssignment 分析左右：左 LVal 用 resolveIdentifier 查到 a；右 a+2 由 analyzeExpression 递归得到 int。
5. getBinaryResultType 检查 int+int，赋值双方类型一致。
6. analyzeReturn 检查 a 的类型与函数 int 一致；checkFunctionReturn 确认最后一项是 return。

### 例二：函数定义与调用

```c
int f(char c) {
    return (int)c;
}
int main() {
    f(1);
    return 0;
}
```

定义时 collectParameterTypes 得到 `[TYPE_CHAR]`，addSymbol 保存全局 f 的签名，registerParameters 将 c 登记到函数内部。return 中的显式转换由 analyzeExpression 返回 int。

调用时 analyzeCall 分析 1 得到 `[TYPE_INT]`，getCallType 发现个数为 1 对得上，但 int 与形参 char 不同，在函数名 f 所在行报告 e。

### 例三：上下文状态如何随遍历变化

```c
int main() {
    while (1) {
        switch (1) {
            case 1: continue;
        }
        break;
    }
    return 0;
}
```

| 遍历位置 | loopDepth | switchDepth | 结果 |
|---|---|---|---|
| main 函数开始 | 0 | 0 | 初始状态 |
| 进入 while 体 | 1 | 0 | 允许 break/continue |
| 进入 switch 分支 | 1 | 1 | continue 可继续外层 while |
| 退出 switch | 1 | 0 | break 仍位于 while 内 |
| 退出 while | 0 | 0 | 恢复状态 |

这些数值是分析器的遍历状态，不是运行时变量。若删去外层 while，只保留 switch 中的 continue，loopDepth 为 0，就报告 m。

## 五、按错误码反查函数

| 错误码 | 含义 | semantic.c 中主要入口 |
|---|---|---|
| b | 当前作用域重定义 | registerDefinition、analyzeFunction |
| c | 未定义名字 | resolveIdentifier |
| d | 函数实参数量不符 | getCallType |
| e | 类型不匹配 | getBinaryResultType、getLValType、getCallType、analyzeExpression、analyzeDefinitionValues、analyzeAssignment、analyzeReturn、analyzePrintf、analyzeCondition、analyzeSwitch |
| f | void 函数 return 带表达式 | analyzeReturn |
| g | 非 void 函数缺末条顶层 return | checkFunctionReturn |
| h | 修改常量 | analyzeAssignment |
| l | printf 占位符和实参数量不符 | analyzePrintf |
| m | break/continue 上下文非法 | analyzeJump |
| n | 重复 case/default | analyzeSwitch |

a 和 i/j/k 由词法/语法阶段负责，不是 semantic.c 新增检查；它们仍与语义错误使用同一个错误表。

## 六、答辩时优先讲清的六个问题

1. **谁负责遍历，谁负责推导类型？** analyzeNode 调度语句/声明，analyzeExpression 递归返回类型。
2. **为什么 findChild 只查直接孩子？** 防止把嵌套语句中的关键字当成本语句的类别。
3. **为什么形参处理分成两个函数？** collectParameterTypes 建签名，registerParameters 登记函数内部可见名字。
4. **为什么函数体传 createScope=0？** analyzeFunction 已创建作用域，形参与最外层局部变量应在同层。
5. **为什么报错后仍继续？** ErrorList 记录普通错误，TYPE_ERROR 减少连带错误，memoryError 才表示内部失败。
6. **当前实现边界在哪里？** 类型推导、名字解析和上下文检查已实现；不是完整常量求值、数组越界检查、变量确定赋值检查或控制流全路径返回证明。

建议阅读顺序：先 analyzeSemantics → analyzeNode，再读 analyzeFunction / analyzeDeclaration / analyzeExpression，最后对照具体错误阅读专门语句函数；第二遍再从 isNode、findChild 等辅助函数顺着源码读。
'''
target=root/'交付/semantic.c逐函数讲解.md'
target.write_text(doc,encoding='utf-8')
assert all(doc.count('### '+str(i)+'. '+name+'\n')==1 for i,(name,*_) in enumerate(entries,1))
print(f'{len(entries)} functions documented; {len(doc)} characters; {target.resolve()}')
