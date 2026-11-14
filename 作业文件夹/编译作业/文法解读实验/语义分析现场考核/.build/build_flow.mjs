import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {Presentation,PresentationFile} from '@oai/artifact-tool';
const here=path.dirname(fileURLToPath(import.meta.url));
const workspaceDir=path.dirname(here),out=path.join(workspaceDir,'交付');
const skill='C:/Users/daihao/.codex/plugins/cache/openai-primary-runtime/presentations/26.1112.11217/skills/presentations';
process.env.RUNTIME_NODE_MODULES='C:/Users/daihao/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const {finalizePresentation,resolvePresentationFont}=await import(pathToFileURL(path.join(skill,'container_tools/artifact_tool_utils.mjs')).href);
const font=resolvePresentationFont({fontFamily:'SimSun'});
const p=Presentation.create({slideSize:{width:1600,height:900}}),s=p.slides.add();
s.background.fill='#FFFFFF';
function text(name,value,x,y,w,h,size=30,bold=false){
 const sh=s.shapes.add({name,geometry:'textbox',position:{left:x,top:y,width:w,height:h},fill:'none',line:{fill:'none',width:0}});
 sh.text=value;sh.text.style={typeface:font,fontSize:size,color:'#000000',bold,alignment:'center',verticalAlignment:'middle',insets:0};return sh;
}
function box(name,value,x,y,w,h,size=30){
 const sh=s.shapes.add({name,geometry:'rect',position:{left:x,top:y,width:w,height:h},fill:'#FFFFFF',line:{fill:'#000000',width:1.6}});
 sh.text=value;sh.text.style={typeface:font,fontSize:size,color:'#000000',alignment:'center',verticalAlignment:'middle',insets:{top:5,bottom:5,left:9,right:9}};return sh;
}
function line(name,x,y,w,h,arrow=false){
 return s.shapes.add({name,geometry:'connector',kind:'straight',position:{left:x,top:y,width:w,height:h},line:{fill:'#000000',width:1.6},...(arrow?{tail:{type:'triangle',width:'med',length:'med'}}:{})});
}
text('标题','语义分析实现流程',240,29,1120,70,54,true);
box('输入语法树','语法分析生成的语法树（CompUnit）',520,127,560,58,32);
box('语义初始化','initSemanticContext：初始化上下文\nanalyzeSemantics：创建全局作用域\nregisterBuiltins：登记库函数',430,225,740,110,29);
box('递归分派','analyzeNode：遍历并分派节点',490,375,620,66,33);
text('分派说明','按节点类型选择分支\n各分支递归检查子树',1165,376,365,65,27);
line('树到初始化',800,185,0,40,true);
line('初始化到分派',800,335,0,40,true);
line('分派到支线',800,441,0,38);
line('分支总线',225,479,1150,0);
const xs=[50,433,816,1199];
const labels=[
 ['声明与函数','登记符号与函数签名\n检查同层重定义'],
 ['表达式与调用','推导类型，核对实参\n检查赋值与数组访问'],
 ['语句','检查 return、printf\n检查跳转与重复标签'],
 ['嵌套块','进入与退出作用域\n形参与外层局部同层'],
];
for(let i=0;i<4;i++){
 box('分支框'+i,'',xs[i],520,350,133);
 text('分支标题'+i,labels[i][0],xs[i]+8,527,334,42,32);
 text('分支说明'+i,labels[i][1],xs[i]+8,572,334,69,26);
 line('分支入口'+i,xs[i]+175,479,0,41,true);
 line('分支出口'+i,xs[i]+175,653,0,40);
}
line('汇合总线',225,693,1150,0);
line('汇合到完成',800,693,0,37,true);
box('检查完成','全部节点检查完成，整理符号与错误',420,730,760,58,33);
line('完成到输出',800,788,0,31,true);
box('统一输出','输出 symbol.txt、error.txt\n有错误时 symbol.txt 留空',420,819,760,68,29);
const notes='我的语义分析在语法分析生成的树上进行，主要判断名字、类型与使用位置是否合法。\n\n首先，主函数初始化 SemanticContext，再由 analyzeSemantics 创建全局作用域、登记库函数。随后 analyzeNode 按源程序顺序遍历语法树，根据节点类型交给对应函数。\n\n声明和函数分支登记名字、类型、种类和函数签名，并检查同层重定义。函数先登记再分析函数体，因此支持自递归。\n\n表达式和调用分支递归推导 DataType，用它检查赋值、数组下标以及实参数量和类型。子表达式出错返回 TYPE_ERROR，减少连带报错。\n\n语句分支结合当前返回类型、while 深度和 switch 深度，检查 return、break、continue、printf 以及重复标签。嵌套 Block 会进入新作用域，离开时恢复父层。函数形参与最外层局部变量共享一个作用域，因此函数体不再重复创建。\n\n各阶段共用 ErrorList。遍历结束后，由 main 输出：符号按作用域和声明顺序排列，错误按行号排列；有错误时清空符号输出，最后统一释放资源。\n\n来源：用户提供的黑白流程图截图作为布局参考。流程与命名核对当前词法分析/parser.c 的 main，以及 semantic.c 的 initSemanticContext、analyzeSemantics、analyzeNode、analyzeFunction、analyzeBlock 等函数。实际树保留文法包装节点，因此页面称“语法树”，未直接标为简化 AST。四类框表示分派类别，内部递归交叉调用，不表示四路并行执行。leaveScope 恢复当前作用域，freeSymbolTable 才释放其记录。';
s.speakerNotes.text=notes;
await fs.mkdir(out,{recursive:true});
await fs.writeFile(path.join(out,'语义分析流程图版讲稿.txt'),notes.split('\n\n来源：')[0],'utf8');
const candidatePath=path.join(here,'flow-candidate.pptx');
await(await PresentationFile.exportPptx(p)).save(candidatePath);
const png=await p.export({slide:s,format:'png',scale:1});
await fs.writeFile(path.join(out,'语义分析实现流程_参考图版.png'),new Uint8Array(await png.arrayBuffer()));
await fs.writeFile(path.join(here,'flow-layout.json'),await(await s.export({format:'layout'})).text());
const result=await finalizePresentation({workspaceDir,candidatePath,finalPath:path.join(out,'语义分析实现流程_参考图版.pptx'),
 pythonExecutable:'C:/Users/daihao/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe',
 integrityValidatorPath:path.join(skill,'container_tools/inspect_presentation_package_integrity.py'),
 layoutValidatorPath:path.join(skill,'container_tools/inspect_presentation_layout_geometry.py'),
 explicitTotalSlideCount:1,requiredNativeTableOwnerSlides:[],requiredNativeChartOwnerSlides:[],
 layoutArgs:['--expected-slide-size-emu','15240000,8572500','--validate-heading-fit'],
 fontPolicy:{basis:'design',families:[font]},verifyArtifactToolImport:true,
 receiptPath:path.join(here,'flow-validation.json')});
console.log(JSON.stringify({finalPath:result.finalPath,slideCount:result.packageIntegrity.slide_count,findings:result.presentationLayout.finding_count}));
