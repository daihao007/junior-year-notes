"""Compile in a temporary directory and check semantic-analysis file outputs."""

from pathlib import Path
import argparse
import json
import subprocess
import tempfile


SOURCE_DIR = Path(__file__).resolve().parents[2]
SOURCE_FILES = (
    "parser.c", "token.c", "syntax_tree.c", "error_list.c", "symbol.c", "semantic.c"
)
CASES = []


def add(name, rows, symbols=None):
    lines, errors = [], []
    for line_number, row in enumerate(rows, 1):
        text, code = row if isinstance(row, tuple) else (row, None)
        lines.append(text)
        if code:
            errors.append(f"{line_number} {code}\n")
    CASES.append({"name": name, "source": "\n".join(lines) + "\n",
                  "errors": "".join(errors), "symbols": symbols})


add("official_correct", [
    "const int year = 2026, month = 9;", "int day;",
    "int getDay() {", "    static int day;", "    day = get_int();",
    "    return day;", "}", "int fun() {", "    return 12345;", "}",
    "int main() {", "    day = getDay();",
    '    printf("Tody is %d-%d-%d\\n", year, month, day);',
    "    {", "        int length = fun();", "        if (length > 4) {",
    '            printf("%d\\n", length);', "        }", "    }",
    "    return 0;", "}",
], "1 year ConstInt\n1 month ConstInt\n1 day Int\n1 getDay IntFunc\n"
   "1 fun IntFunc\n2 day StaticInt\n5 length Int\n")

add("official_error", [
    "const int const1 = 1, const2 = -100;", "int change1;",
    "int gets1(int var1, int var2) {", ("    const1 = 999;", "h"),
    ("    change1 = var1 + var2          return (change1);", "i"), "}",
    "int main() {", "    change1 = 10;", '    printf("Hello World");',
    "    return 0;", "}",
])

add("all_symbol_type_names", [
    "int gi;", "const int ci = 1;", "int ia[2];",
    "const int cia[2] = {1, 2};", "char gc;", "const char cc = 'A';",
    "char ca[2];", "const char cca[2] = {'A', 'B'};",
    "void vf() { return; }", "int fi() { return 1; }",
    "char fc() { return 'A'; }", "int main() {",
    "    static int si;", "    static int sia[2];", "    static char sc;",
    "    static char sca[2];", "    return 0;", "}",
], "1 gi Int\n1 ci ConstInt\n1 ia IntArray\n1 cia ConstIntArray\n"
   "1 gc Char\n1 cc ConstChar\n1 ca CharArray\n1 cca ConstCharArray\n"
   "1 vf VoidFunc\n1 fi IntFunc\n1 fc CharFunc\n5 si StaticInt\n"
   "5 sia StaticIntArray\n5 sc StaticChar\n5 sca StaticCharArray\n")

add("empty_and_sibling_scopes", [
    "int f(int x) {", "    {}", "    { int y; }", "    return x;", "}",
    "int main() {", "    {}", "    { int z; }", "    return 0;", "}",
], "1 f IntFunc\n2 x Int\n4 y Int\n7 z Int\n")

add("redefinitions", [
    "int item;", ("char item;", "b"),
    ("int work(int x, int x) {", "b"), ("    int x;", "b"),
    "    return 0;", "}", ("int work() {", "b"), "    int local;",
    ("    char local;", "b"), "    return 0;", "}", "int main() {",
    "    int value;", ("    int value;", "b"), "    { char value; }",
    "    return 0;", "}",
])

add("undefined_and_definition_order", [
    "int first() {", ("    return later();", "c"), "}",
    "int later() { return 1; }", "int main() {",
    ("    int value = missing;", "c"), ("    value = other;", "c"),
    ("    absent[0] = 1;", "c"), ("    unknown();", "c"),
    "    { int inside; }", ("    inside = 0;", "c"), "    return 0;", "}",
])

add("recursive_function", [
    "int rec(int n) {", "    if (n > 0) { return rec(n - 1); }",
    "    return 0;", "}", "int main() {", "    rec(2);", "    return 0;", "}",
])

add("function_variable_misuse", [
    "int f() { return 1; }", "int main() {", "    int x;",
    ("    x();", "e"), ("    x = f;", "e"),
    "    { int f;", ("      f();", "e"), "    }", "    return 0;", "}",
])

add("expression_errors", [
    "int add(int a, int b) { return a + b; }", "int main() {",
    "    int values[3];", "    char ch = 'A';", ("    add(1);", "d"),
    ("    add(1, ch);", "e"), ("    values[ch];", "e"),
    ("    ch[0];", "e"), ("    1 + ch;", "e"),
    ("    missing + 1;", "c"), "    put_int((int)ch);", "    return 0;", "}",
])

add("initializers_and_lengths", [
    ("int wrong = 'a';", "e"), ("char wrongChar = 1;", "e"),
    ("int ints[2] = {1, 'a'};", "e"), ("char chars[2] = {'a', 1};", "e"),
    ("int array['a'];", "e"), ('int wrongString[2] = "ab";', "e"),
    "int main() {", ("    int x = missing;", "c"),
    ("    int y = 1 + 'a';", "e"), ("    int z[bad];", "c"),
    "    return 0;", "}",
])

add("assignments_and_returns", [
    "void badVoid() {", ("    return 1;", "f"), "}",
    "char badChar() {", ("    return 1;", "e"), "}",
    "int missingReturn() {", "    int x;", ("}", "g"),
    "int conditionalReturn(int flag) {", "    if (flag) { return 1; }", ("}", "g"),
    "int bareReturn() {", ("    return;", "e"), "}", "int main() {",
    "    const int fixed = 1;", "    const char letters[2] = {'a', 'b'};",
    "    int n = 0;", "    char ch = 'A';", ("    fixed = 2;", "h"),
    ("    letters[0] = 'c';", "h"), ("    n = ch;", "e"),
    ("    ch = n;", "e"), "    return 0;", "}",
])

add("missing_final_return", [
    "int earlier() {", "    return 1;", "    int x;", ("}", "g"),
    "int nested() {", "    { return 1; }", ("}", "g"),
    "int empty() {", ("}", "g"), "void fine() { }",
    "int main() {", ("}", "g"),
])

add("valid_printf_and_builtins", [
    "int identity(int n) { return n; }", "char letter() { return 'A'; }",
    "int main() {", '    char text[4] = "abc";',
    '    const char label[3] = "ok";', '    printf("hello\\n");',
    '    printf("");', '    printf("%d %c %s\\n", 1, \'A\', text);',
    '    printf("%s:%d:%c", label, identity(2), letter());',
    '    printf("%c%d%s%d", (char)65, (int)letter(), text, get_int());',
    '    printf("%c", get_char());', "    get_string(text, 4);",
    "    put_int(1);", "    put_char('A');", "    put_str(text);",
    "    return 0;", "}",
])

add("printf_counts", [
    "int main() {", ('    printf("%d");', "l"),
    ('    printf("hello", 1);', "l"), ('    printf("%d%c", 1);', "l"),
    ('    printf("%d", 1, 2);', "l"), ('    printf("", 1);', "l"),
    "    return 0;", "}",
])

add("documented_string_output_names", [
    "int main() {", '    char text[3] = "ok";',
    "    put_str(text);", "    put_string(text);",
    ("    put_string(1);", "e"), "    return 0;", "}",
])

add("printf_types_and_propagation", [
    "void action() { return; }", "int identity(int n) { return n; }",
    "int main() {", "    int numbers[2] = {1, 2};",
    '    char text[3] = "ab";', ('    printf("%d", \'A\');', "e"),
    ('    printf("%c", 1);', "e"), ('    printf("%s", \'A\');', "e"),
    ('    printf("%s", numbers);', "e"), ('    printf("%d", numbers);', "e"),
    ('    printf("%s", text[0]);', "e"), ('    printf("%d", action());', "e"),
    ('    printf("%d", missing);', "c"), ('    printf("%d", 1 + \'A\');', "e"),
    ('    printf("%d", identity());', "d"), "    return 0;", "}",
])

add("syntax_and_semantic_errors_together", [
    "int main() {", ("    int x = 0", "i"), "    int a[2];",
    ("    x = a[0;", "k"), ('    printf("%d", x;', "j"),
    ("    if (x & 1) { ; }", "a"), ("    if (x | 1) { ; }", "a"),
    ("    absent = 1;", "c"), ('    printf("%c", 1);', "e"),
    "    return 0;", "}",
])

add("valid_loop_switch_nesting", [
    "int main() {", "    int n = 2;", "    char c = 'a';",
    "    while (n > 0) {", "        switch (n) {",
    "        case 1: continue;", "        default: break;", "        }",
    "        n = n - 1;", "        break;", "    }",
    "    switch (c) {", "    case 'a':",
    "        while (n) { continue; }", "        break;",
    "    default: break;", "    }", "    return 0;", "}",
])

add("invalid_jumps_and_depth_restoration", [
    "int main() {", ("    break;", "m"), ("    continue;", "m"),
    "    while (1) { break; continue; }", ("    break;", "m"),
    "    switch (1) {", "    case 1:", ("        continue;", "m"),
    "        break;", "    default: break;", "    }",
    ("    break;", "m"), ("    continue;", "m"), "    return 0;", "}",
])

add("switch_duplicate_labels", [
    "int main() {", "    switch (1) {", "    case 1: break;",
    ("    case 1: break;", "n"), "    default: break;",
    ("    default: break;", "n"), "    }", "    return 0;", "}",
])

add("switch_type_mismatches", [
    "int main() {", "    char c = 'A';", "    int a[2];",
    "    switch (1) {", ("    case 'A': break;", "e"), "    }",
    "    switch (c) {", ("    case 65: break;", "e"), "    }",
    ("    switch (a) {", "e"), "    case 0: break;", "    }",
    "    return 0;", "}",
])

add("nested_switch_labels_are_independent", [
    "int main() {", "    switch (1) {", "    case 1:",
    "        switch (1) {", "        case 1: break;", "        default: break;", "        }",
    "        break;", "    default: break;", "    }",
    "    switch (1) { case 1: break; default: break; }", "    return 0;", "}",
])

add("bad_label_still_checks_branch", [
    "int main() {", "    switch (1) {", "    case 1: break;",
    ("    case 1:", "n"), ("        missing();", "c"), "        break;",
    "    default: break;", ("    default:", "n"),
    ('        printf("%c", 1);', "e"), "    }", "    return 0;", "}",
])

add("invalid_conditions", [
    "void action() { return; }", "int main() {", "    int a[2];",
    ("    if (a) { ; }", "e"), ("    while (action()) { break; }", "e"),
    "    if ('A') { ; }", "    return 0;", "}",
])

add("empty_switch_and_nested_scopes", [
    "int main() {", "    switch (1) { }", "    switch (1) {",
    "    case 1: { int local; break; }", "    default: break;", "    }",
    "    { int after; }", "    return 0;", "}",
], "3 local Int\n4 after Int\n")

for escape in (r"\n", r"\t", r"\r", r"\0", r"\\", r"\'"):
    literal = "'" + escape + "'"
    add("duplicate_character_" + str(len(CASES)), [
        "int main() {", f"    switch ({literal}) {{",
        f"    case {literal}: break;", (f"    case {literal}: break;", "n"),
        "    }", "    return 0;", "}",
    ])

# All five expression types are checked against all four legal parameter types.
types = ("int", "char", "int_array", "char_array", "void")
arguments = {"int": "1", "char": "'A'", "int_array": "ints",
             "char_array": "chars", "void": "action()"}
params = {"int": "int p", "char": "char p",
          "int_array": "int p[]", "char_array": "char p[]"}
for expected in types[:-1]:
    for actual in types:
        call = f"    take({arguments[actual]});"
        add(f"argument_{expected}_from_{actual}", [
            "void action() { return; }", f"void take({params[expected]}) {{ return; }}",
            "int main() {", "    int ints[2];", "    char chars[2];",
            call if expected == actual else (call, "e"), "    return 0;", "}",
        ])

for expected in ("int", "char"):
    for actual in types:
        assignment = f"    target = {arguments[actual]};"
        add(f"assignment_{expected}_from_{actual}", [
            "void action() { return; }", "int main() {",
            "    int ints[2];", "    char chars[2];", f"    {expected} target;",
            assignment if expected == actual else (assignment, "e"),
            "    return 0;", "}",
        ])


def build(executable, defines=(), source_dir=SOURCE_DIR):
    command = ["gcc", "-Wall", "-Wextra", "-Werror", "-std=c11"]
    command += [f"-D{define}" for define in defines]
    command += [str(source_dir / name) for name in SOURCE_FILES]
    command += ["-o", str(executable)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)


def check_case(executable, directory, case, arguments=()):
    directory.mkdir()
    (directory / "testfile.txt").write_text(case["source"], encoding="utf-8")
    result = subprocess.run([str(executable), *arguments], cwd=directory,
                            capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, (case["name"], result.returncode, result.stderr)
    assert not result.stdout and not result.stderr, (case["name"], result.stdout, result.stderr)
    errors = (directory / "error.txt").read_text(encoding="utf-8")
    symbols = (directory / "symbol.txt").read_text(encoding="utf-8")
    assert errors == case["errors"], (case["name"], errors, case["errors"])
    if errors:
        assert symbols == "", (case["name"], symbols)
        assert (directory / "parser.txt").read_text(encoding="utf-8") == "", case["name"]
    else:
        if case["symbols"] is not None:
            assert symbols == case["symbols"], (case["name"], symbols, case["symbols"])
        rows = [line.split(" ") for line in symbols.splitlines()]
        assert all(len(row) == 3 for row in rows), case["name"]
        ids = [int(row[0]) for row in rows]
        assert ids == sorted(ids), case["name"]
        builtins = {"main", "get_int", "get_char", "get_string", "put_int", "put_char", "put_str", "put_string"}
        assert all(row[1] not in builtins for row in rows), case["name"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=SOURCE_DIR)
    parser.add_argument("--report", type=Path)
    options = parser.parse_args()
    source_dir = options.source_dir.resolve()
    covered = {line.split()[1] for case in CASES for line in case["errors"].splitlines()}
    assert covered == set("abcdefghijklmn"), covered
    with tempfile.TemporaryDirectory(prefix="sysy-semantic-regression-") as task_dir:
        temporary = Path(task_dir)
        executable = temporary / "semantic.exe"
        build(executable, source_dir=source_dir)
        for case in CASES:
            check_case(executable, temporary / case["name"], case)
            print("PASS " + case["name"])

        # Input/output paths must remain relative to the process working directory.
        check_case(executable, temporary / "command_line_compile", CASES[0], ("--compile",))
        check_case(executable, temporary / "command_line_interp", CASES[0], ("--interp",))

        # Disabling debug outputs must not disable semantic analysis or error output.
        quiet = temporary / "quiet.exe"
        build(quiet, ("PARSER_OUTPUT_ENABLED=0", "SYMBOL_OUTPUT_ENABLED=0"), source_dir)
        quiet_dir = temporary / "disabled_outputs"
        quiet_dir.mkdir()
        (quiet_dir / "testfile.txt").write_text(CASES[1]["source"], encoding="utf-8")
        (quiet_dir / "symbol.txt").write_text("sentinel\n", encoding="utf-8")
        result = subprocess.run([str(quiet)], cwd=quiet_dir, capture_output=True, timeout=10)
        assert result.returncode == 0 and not result.stdout and not result.stderr
        assert (quiet_dir / "error.txt").read_text(encoding="utf-8") == CASES[1]["errors"]
        assert (quiet_dir / "symbol.txt").read_text(encoding="utf-8") == "sentinel\n"
        assert not (quiet_dir / "parser.txt").exists()

    summary = {"cases_passed": len(CASES) + 3, "errors_covered": sorted(covered),
               "compiler_flags": "-Wall -Wextra -Werror -std=c11",
               "official_samples": "correct symbol output and error output matched",
               "platform_evaluation": "not run",
               "source_directory": str(source_dir),
               "cases": [case["name"] for case in CASES] + [
                   "command_line_compile", "command_line_interp", "disabled_outputs"]}
    if options.report:
        options.report.parent.mkdir(parents=True, exist_ok=True)
        options.report.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
