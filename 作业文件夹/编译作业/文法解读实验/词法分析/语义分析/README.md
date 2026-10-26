# 2026 语义分析实验

本目录中的 C 源码可独立编译。`semantic.zip` 是提交用源码包，ZIP 根目录只包含编译所需的源码、头文件和 `config.json`。

## 编译与运行

在本目录打开 PowerShell，执行：

```powershell
gcc -Wall -Wextra -Werror -std=c11 parser.c token.c syntax_tree.c error_list.c symbol.c semantic.c -o semantic.exe
.\semantic.exe
```

运行前，把待分析的 SysY 源程序保存为当前目录中的 `testfile.txt`。

- 正确程序：`symbol.txt` 每行输出 `作用域序号 名字 类型名称`。
- 错误程序：`error.txt` 按行号排序输出 `行号 错误类别码`，同时清空 `symbol.txt` 和 `parser.txt`，避免残留前一次结果。
- 默认不打印调试信息。`lexer.txt`、`parser.txt` 是现有前端保留的中间输出，不属于提交时的预期答案文件。
- 当前设计中，退出码 `0` 表示分析和文件输出正常完成；源程序错误记录在 `error.txt`。内存不足、解析无法完成或输出失败时返回非零退出码。

## 文件职责

| 文件 | 职责 |
| --- | --- |
| `parser.c` | 单词识别、递归下降解析、主程序和各阶段连接 |
| `token.c/.h` | 单词列表及行号 |
| `syntax_tree.c/.h` | 语法树及语法分析输出 |
| `error_list.c/.h` | 错误列表、排序与输出 |
| `symbol.c/.h` | 符号记录、作用域、查找、内存管理及符号输出 |
| `semantic.c/.h` | 类型推导、函数签名和语义检查 |
| `config.json` | 课程提交配置，C / MIPS |

## 已实现规则

- `a`：词法阶段处理单独的 `&`、`|`。
- `b/c/d`：名字重定义、未定义名字、函数参数个数不匹配。
- `e`：表达式、赋值、初值、函数参数和返回值的类型匹配；数组长度、下标、标量/数组访问；函数与变量误用；`printf` 和 `switch` 的类型匹配。
- `f/g/h`：`void` 函数返回表达式、有返回值函数末尾缺少 `return`、修改常量或常量数组元素。
- `i/j/k`：缺少分号、右小括号、右中括号，恢复后继续进行语义分析。
- `l`：`printf` 的格式占位符与实参数量不匹配。
- `m`：`break` 仅允许出现在 `while` 或 `switch` 中；`continue` 仅允许出现在 `while` 中。
- `n`：同一 `switch` 中重复的 `case` 值或 `default`，嵌套 `switch` 独立检查。

符号按作用域编号升序、同一作用域按声明顺序输出。全局作用域编号为 1，空的 `Block` 也占用编号。函数形参与函数体最外层局部变量共用作用域。`main` 和运行时库符号不输出。

`g` 按文法要求检查函数体最后一个顶层语句是否为 `return`，不进行控制流推断，报错位置是函数体末尾的 `}`。表达式已返回 `TYPE_ERROR` 时，外层检查避免再产生连带类型错误。

运行时库签名依据课程 PDF 第 1—2 页登记。PDF 的字符串输出名称栏写作 `put_string`，原型及后文写作 `put_str`；这两个名称都按 `void (char[])` 识别。`printf` 单独按 `%d → int`、`%c → char`、`%s → char[]` 检查。

## 输出开关

```powershell
gcc -Wall -Wextra -Werror -std=c11 -DPARSER_OUTPUT_ENABLED=0 -DSYMBOL_OUTPUT_ENABLED=0 parser.c token.c syntax_tree.c error_list.c symbol.c semantic.c -o semantic.exe
```

关闭符号输出后仍会进行语义分析并生成 `error.txt`。调试输出由 `PARSER_DEBUG_ENABLED` 控制，默认关闭。

## 本地验证

运行：

```powershell
python .\tests\run_regression.py --source-dir . --report .\测试报告.json
```

测试只使用 Python 标准库和 GCC，在临时目录编译及运行，不覆盖本目录中的输入、输出或可执行文件。

当前回归包含 64 项检查：官方正确/错误样例、全部 15 种符号输出名称、`a` 到 `n` 的全部错误类别、作用域遮蔽与编号、重名函数内部检查、参数与赋值类型组合、字符转义标签、循环和 `switch` 嵌套、输出开关及命令行参数兼容性。

这些结果是本地测试证据。尚未登录希冀平台进行评测，不代表平台得分。

## 提交

上传 `semantic.zip`。ZIP 根目录为 11 个 C 源码/头文件及 `config.json`，不包含测试程序、可执行文件、输入输出文件或 AI 交互记录。

`源码校验清单.json` 记录这 12 个文件的 SHA-256，可用于核对工作源码、提交目录与 ZIP 内容是否一致。
