import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {Presentation,PresentationFile} from '@oai/artifact-tool';
const here=path.dirname(fileURLToPath(import.meta.url));
const workspaceDir=path.dirname(here);
const out=path.join(workspaceDir,'交付');
const skill='C:/Users/daihao/.codex/plugins/cache/openai-primary-runtime/presentations/26.1111.10930/skills/presentations';
const {finalizePresentation,resolvePresentationFont}=await import(pathToFileURL(path.join(skill,'container_tools/artifact_tool_utils.mjs')).href);
const font=resolvePresentationFont({fontFamily:'Microsoft YaHei'});
console.log('font',font);
const p=Presentation.create({slideSize:{width:1600,height:900}});
const s=p.slides.add();s.background.fill='#FFFFFF';
function text(name,value,x,y,w,h,size=28,color='#20384D',bold=false){
 const a=s.shapes.add({name,geometry:'textbox',position:{left:x,top:y,width:w,height:h},fill:'none',line:{fill:'none',width:0}});
 a.text=value;a.text.style={typeface:font,fontSize:size,color,bold};return a;
}
text('title','语义分析实现方案',64,37,1450,80,54,'#112E47',true);
text('architecture','词法 / 语法分析   →   语法树   →   analyzeSemantics   →   符号表 / 错误表',68,133,1460,60,30,'#136F87',true);

text('scope-title','01  符号表与作用域',68,235,850,47,32,'#112E47',true);
text('scope-body','Symbol：名字、类型、种类、作用域、函数签名\n同层查重；沿 parentId 就近查找；退出后保留记录\n形参与函数体最外层共享作用域',68,292,910,126,27);
text('type-title','02  表达式类型与函数检查',68,441,910,47,32,'#112E47',true);
text('type-body','analyzeExpression 递归返回 DataType\n检查赋值、数组下标、实参、返回值和 printf\n先登记函数以支持递归；TYPE_ERROR 抑制连带报错',68,496,950,126,27);
text('context-title','03  上下文与错误输出',68,645,910,47,32,'#112E47',true);
text('context-body','currentReturnType / loopDepth / switchDepth\n检查 return、break、continue 与重复 case / default\n统一 ErrorList；按行输出；无错误时输出符号表',68,699,960,126,27);

text('example-title','作用域遮蔽示例',1060,235,470,47,32,'#112E47',true);
text('example-code','int x;\nint f(int p) {\n    int x;\n    { char x; }\n    return x;\n}',1060,295,465,225,28);
text('example-output','符号输出（作用域 / 名字 / 类型）\n1  x  Int        1  f  IntFunc\n2  p  Int        2  x  Int\n3  x  Char',1060,540,465,151,25);
text('boundary','返回检查：末条顶层 return\n按作业规则，不做全路径证明',1060,732,465,83,25,'#526779');
s.speakerNotes.text=(await fs.readFile(path.join(out,'两分钟讲解稿.txt'),'utf8'))+'\n\n代码依据：词法分析/parser.c main；semantic.c analyzeSemantics / analyzeNode / analyzeFunction / analyzeExpression / analyzeJump / analyzeSwitch；symbol.c enterScope / findSymbol / writeSymbolOutput。对应源码定位及问答见配套讲解稿。';
await fs.mkdir(out,{recursive:true});
const candidatePath=path.join(here,'candidate.pptx');
await(await PresentationFile.exportPptx(p)).save(candidatePath);
const png=await p.export({slide:s,format:'png',scale:1});
await fs.writeFile(path.join(out,'语义分析实现方案.png'),new Uint8Array(await png.arrayBuffer()));
await fs.writeFile(path.join(here,'layout.json'),await(await s.export({format:'layout'})).text());
const result=await finalizePresentation({
 workspaceDir,candidatePath,finalPath:path.join(out,'语义分析实现方案.pptx'),
 pythonExecutable:'C:/Users/daihao/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe',
 integrityValidatorPath:path.join(skill,'container_tools/inspect_presentation_package_integrity.py'),
 layoutValidatorPath:path.join(skill,'container_tools/inspect_presentation_layout_geometry.py'),
 explicitTotalSlideCount:1,requiredNativeTableOwnerSlides:[],requiredNativeChartOwnerSlides:[],
 layoutArgs:['--expected-slide-size-emu','15240000,8572500','--validate-heading-fit'],
 fontPolicy:{basis:'design',families:[font]},verifyArtifactToolImport:true,
 receiptPath:path.join(here,'validation.json')
});console.log(JSON.stringify(result));
