# 演示运行

在 PowerShell 中进入某个样例目录（如 01_scope），运行：

```powershell
gcc -Wall -Wextra -std=c11 "E:/Users/daihao/Documents/junior-year-notes/作业文件夹/编译作业/文法解读实验/词法分析/parser.c" "E:/Users/daihao/Documents/junior-year-notes/作业文件夹/编译作业/文法解读实验/词法分析/token.c" "E:/Users/daihao/Documents/junior-year-notes/作业文件夹/编译作业/文法解读实验/词法分析/syntax_tree.c" "E:/Users/daihao/Documents/junior-year-notes/作业文件夹/编译作业/文法解读实验/词法分析/error_list.c" "E:/Users/daihao/Documents/junior-year-notes/作业文件夹/编译作业/文法解读实验/词法分析/symbol.c" "E:/Users/daihao/Documents/junior-year-notes/作业文件夹/编译作业/文法解读实验/词法分析/semantic.c" -o compiler_demo.exe
$LASTEXITCODE
.\compiler_demo.exe
Get-Content .\symbol.txt
Get-Content .\error.txt
```

编译成功再运行。上述源码绝对路径对应当前工作区；移动到其他电脑时替换成实际路径。每个样例自带 testfile.txt、实际输出和预期结果。程序分析 SysY，不会执行其中循环。
