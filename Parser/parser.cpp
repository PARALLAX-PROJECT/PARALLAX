#include <iostream>
#include <sstream>

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <map>
using namespace clang;
using namespace clang::tooling;
using namespace clang::ast_matchers;
class MyASTConsumer;
class MyFrontEndAction;
class Parser;
class PrintHandler;



class MetaData {
    public:
    // source_name → dispatch_stub_name  (used by CallExprHandler for call-site replacement)
    std::map<std::string, std::string> transformedFuncs;
    // source_name → worker_stub_name    (used by matcher on the worker side)
    std::map<std::string, std::string> workerFuncs;
    // dispatch_stub_name → full C prototype string (for forward declarations)
    std::map<std::string, std::string> dispatchPrototypes;

    void addTranformedFunct(std::string source, std::string transformed) {
        transformedFuncs[source] = transformed;
    }

    void addWorkerFunct(std::string source, std::string worker) {
        workerFuncs[source] = worker;
    }

    void addDispatchPrototype(std::string dispatchName, std::string proto) {
        dispatchPrototypes[dispatchName] = proto;
    }

    void printAllTransformedFunctions() {
        for (auto &func : transformedFuncs) {
            llvm::outs() << "\n\nTransformed function " << func.first
                         << " into dispatch stub " << func.second << "\n\n";
        }
        for (auto &func : workerFuncs) {
            llvm::outs() << "Worker stub: " << func.second << "\n";
        }
    }
};


class PrintHandler : public MatchFinder::MatchCallback {
private:
    Rewriter &rewriter;
    MetaData &metadata;

public:
    PrintHandler(Rewriter &rewriter, MetaData &MetaData)
        : rewriter(rewriter), metadata(MetaData) {}

    void run(const MatchFinder::MatchResult &Result) override {
        const FunctionDecl *func =
            Result.Nodes.getNodeAs<FunctionDecl>("func");
        if (!func) return;
    }
};


class VarHandler : public MatchFinder::MatchCallback {
    Rewriter &rewriter;
    MetaData &metadata;

public:
    VarHandler(Rewriter &rewriter, MetaData &metadata)
        : rewriter(rewriter), metadata(metadata) {}

    void run(const MatchFinder::MatchResult &Result) override {
        const VarDecl *var = Result.Nodes.getNodeAs<VarDecl>("var");
        if (!var) return;

        for (const Attr *A : var->attrs()) {
            if (const auto *AA = dyn_cast<AnnotateAttr>(A)) {
                llvm::outs() << "Annotation: " << AA->getAnnotation()
                             << " at variable " << var->getNameAsString();
            }
        }
    }
};


class CallExprHandler : public MatchFinder::MatchCallback {
    Rewriter &rewriter;
    MetaData &metadata;

public:
    CallExprHandler(Rewriter &rewriter, MetaData &metadata)
        : rewriter(rewriter), metadata(metadata) {}

    void run(const MatchFinder::MatchResult &Result) override {
        const CallExpr *call = Result.Nodes.getNodeAs<CallExpr>("call");
        if (!call) return;

        const FunctionDecl *FD = call->getDirectCallee();
        if (!FD) return;

        std::string name = FD->getNameAsString();
        auto it = metadata.transformedFuncs.find(name);
        if (it == metadata.transformedFuncs.end()) return;

        const std::string &newName = it->second;
        const Expr *callee = call->getCallee();
        SourceRange calleeRange(callee->getBeginLoc(), callee->getEndLoc());
        rewriter.ReplaceText(calleeRange, newName);
    }
};


class FunctionHandler : public MatchFinder::MatchCallback {
    Rewriter &rewriter;
    MetaData &metadata;

public:
    FunctionHandler(Rewriter &rewriter, MetaData &metadata)
        : rewriter(rewriter), metadata(metadata) {}

    void run(const MatchFinder::MatchResult &Result) override {
        const FunctionDecl *func =
            Result.Nodes.getNodeAs<FunctionDecl>("func");
        if (!func) return;

        int vcpus = 1;
        bool has_annotation = false;

        for (const Attr *A : func->attrs()) {
            if (const auto *AA = dyn_cast<AnnotateAttr>(A)) {
                std::string annotation = AA->getAnnotation().str();
                llvm::outs() << "Annotation: " << annotation
                             << " at function " << func->getNameAsString() << "\n";
                if (annotation.find("vcpus:") == 0) {
                    vcpus = std::stoi(annotation.substr(6));
                    has_annotation = true;
                } else if (annotation.find("goat:") == 0) {
                    has_annotation = true;
                }
            }
        }

        if (!has_annotation) return;

        std::string sourceFunction = func->getNameAsString();
        std::string returnType     = func->getReturnType().getAsString();
        std::string dispatchName   = sourceFunction + "_generated"; // replaces call sites
        std::string workerName     = sourceFunction + "_worker";    // runs on worker node

        // ── Build original parameter signature string ──────────────────────────
        std::string params;
        for (unsigned i = 0; i < func->getNumParams(); ++i) {
            const ParmVarDecl *P = func->getParamDecl(i);
            params += P->getType().getAsString() + " " + P->getNameAsString();
            if (i + 1 < func->getNumParams()) params += ", ";
        }
        std::string paramCount = std::to_string(func->getNumParams());

        // ── Look-ahead pass: find the "size companion" for each pointer param ──
        // Convention: if param[i] is a pointer AND param[i+1] is an integer type,
        // then param[i+1] is the byte-size of the data param[i] points to.
        // sizeCompanion[i] holds the index of the size param, or -1 if none.
        std::vector<int> sizeCompanion(func->getNumParams(), -1);
        for (unsigned i = 0; i + 1 < func->getNumParams(); ++i) {
            const ParmVarDecl *P    = func->getParamDecl(i);
            const ParmVarDecl *Next = func->getParamDecl(i + 1);
            if (P->getType()->isPointerType() && Next->getType()->isIntegerType()) {
                sizeCompanion[i] = (int)(i + 1);
            }
        }

        // ── 1. DISPATCH STUB (_generated) ─────────────────────────────────────
        // Replaces every call site on the master side.
        // Packs original args into ParallaxParam[] and hands off to execute_fxn.
        // The fxn_name given to execute_fxn is the WORKER stub name so the worker
        // node can look it up via matcher().
        std::string dispatchStub =
            "\n" + returnType + " " + dispatchName + "(" + params + ") {\n"
            "    ParallaxParam __parallax_params[" + paramCount + "];\n";

        bool firstScatterDone = false;
        for (unsigned i = 0; i < func->getNumParams(); ++i) {
            const ParmVarDecl *P = func->getParamDecl(i);
            std::string pName = P->getNameAsString();
            std::string pType = P->getType().getAsString();
            std::string idx   = std::to_string(i);
            bool isPtr = P->getType()->isPointerType();

            std::string dist;
            if (isPtr && !firstScatterDone) {
                dist = "PARALLAX_SCATTER";
                firstScatterDone = true;
            } else if (!isPtr && i > 0 && sizeCompanion[i - 1] == (int)i) {
                // this param is the size companion of the previous pointer param
                dist = "PARALLAX_SIZE_OF";
            } else {
                dist = "PARALLAX_BROADCAST";
            }

            dispatchStub += "    __parallax_params[" + idx + "].data = (void *)";
            dispatchStub += (isPtr ? pName : ("&" + pName)) + ";\n";

            // size: for pointer params use the look-ahead companion if available,
            //       for value params use sizeof, for size-companion params use 0
            dispatchStub += "    __parallax_params[" + idx + "].size = ";
            if (isPtr) {
                if (sizeCompanion[i] >= 0) {
                    // size is the value of the next (companion) param
                    dispatchStub += func->getParamDecl(sizeCompanion[i])->getNameAsString() + ";\n";
                } else {
                    dispatchStub += "0;\n";
                }
            } else {
                dispatchStub += "sizeof(" + pName + ");\n";
            }

            dispatchStub += "    __parallax_params[" + idx + "].distribution = " + dist + ";\n";
            dispatchStub += "    __parallax_params[" + idx + "].index = " + idx + ";\n";
            dispatchStub += "    strncpy(__parallax_params[" + idx + "].type_name, \""
                            + pType + "\", 63);\n";
        }


        // fxn_name → worker stub, not the original function
        dispatchStub +=
            "    execute_fxn(__parallax_params, " + paramCount +
            ", \"" + workerName + "\", " + std::to_string(vcpus) +
            ", __parallax_prog_code__, __parallax_prog_name__);\n";
        if (returnType != "void")
            dispatchStub += "    return (" + returnType + ")NULL;\n";
        dispatchStub += "}\n";

        // ── 2. WORKER STUB (_worker) ───────────────────────────────────────────
        // Executed on the worker node. Receives a ParallaxParam*, deserializes
        // every argument back to its original type, then calls the real function.
        std::string workerStub =
            "\nvoid *" + workerName + "(void *__arg) {\n"
            "    ParallaxParam *__p = (ParallaxParam *)__arg;\n";

        std::string callArgs;
        for (unsigned i = 0; i < func->getNumParams(); ++i) {
            const ParmVarDecl *P = func->getParamDecl(i);
            std::string pName = P->getNameAsString();
            std::string pType = P->getType().getAsString();
            std::string idx   = std::to_string(i);
            bool isPtr = P->getType()->isPointerType();

            if (isPtr) {
                // pointer — cast data directly
                workerStub += "    " + pType + " " + pName +
                              " = (" + pType + ")__p[" + idx + "].data;\n";
            } else {
                // value — dereference through a typed pointer
                workerStub += "    " + pType + " " + pName +
                              " = *(" + pType + " *)__p[" + idx + "].data;\n";
            }

            if (i > 0) callArgs += ", ";
            callArgs += pName;
        }

        if (returnType == "void") {
            workerStub += "    " + sourceFunction + "(" + callArgs + ");\n";
            workerStub += "    return NULL;\n";
        } else {
            workerStub += "    return (void *)" + sourceFunction + "(" + callArgs + ");\n";
        }
        workerStub += "}\n";

        clang::SourceLocation endOfFile =
            rewriter.getSourceMgr().getLocForEndOfFile(
                rewriter.getSourceMgr().getMainFileID());
        rewriter.InsertText(endOfFile, dispatchStub, true, true);
        rewriter.InsertText(endOfFile, workerStub,   true, true);

        metadata.addTranformedFunct(sourceFunction, dispatchName);
        metadata.addWorkerFunct(sourceFunction, workerName);
        // Store the prototype so EndSourceFileAction can forward-declare it
        metadata.addDispatchPrototype(dispatchName, returnType + " " + dispatchName + "(" + params + ");");
    }
};


// Escape a raw C string so it can be embedded as a C string literal body.
static std::string escapeForCString(const std::string &s) {
    std::string out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\x%02x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}


class MyASTConsumer : public ASTConsumer {
private:
    MetaData &MymetaData;
    Rewriter &MyRewriter;
    PrintHandler printhandler;
    VarHandler varHandler;
    FunctionHandler FunctionHandler;
    CallExprHandler CallExprHandler;
    MatchFinder FunctionMatcher;
    MatchFinder VariableMatcher;
    MatchFinder CallExprMatcher;

public:
    MyASTConsumer(Rewriter &rewriter, MetaData &metadata)
        : MyRewriter(rewriter), MymetaData(metadata),
          printhandler(this->MyRewriter, this->MymetaData),
          varHandler(this->MyRewriter, this->MymetaData),
          FunctionHandler(this->MyRewriter, this->MymetaData),
          CallExprHandler(this->MyRewriter, this->MymetaData) {

        FunctionMatcher.addMatcher(
            functionDecl(isDefinition()).bind("func"), &printhandler);
        VariableMatcher.addMatcher(varDecl().bind("var"), &varHandler);
        FunctionMatcher.addMatcher(
            functionDecl(isDefinition()).bind("func"), &FunctionHandler);
        CallExprMatcher.addMatcher(callExpr().bind("call"), &CallExprHandler);
    }

    void HandleTranslationUnit(ASTContext &context) override {
        FunctionMatcher.matchAST(context);
        MymetaData.printAllTransformedFunctions();
        CallExprMatcher.matchAST(context);

        // Append the matcher() lookup function.
        // It maps WORKER stub names so the worker node can look up the right function.
        std::string matcherCode = "\n\ntypedef void *(*fn)(void *);\n\n";
        matcherCode += "fn matcher(char *name) {\n";
        for (const auto &pair : MymetaData.workerFuncs) {
            // pair.second is the worker stub name (e.g. "sum_worker")
            matcherCode += "    if (strcmp(name, \"" + pair.second + "\") == 0) {\n";
            matcherCode += "        return (fn)" + pair.second + ";\n";
            matcherCode += "    }\n";
        }
        matcherCode += "    return NULL;\n";
        matcherCode += "}\n";

        clang::SourceLocation endOfFile =
            MyRewriter.getSourceMgr().getLocForEndOfFile(
                MyRewriter.getSourceMgr().getMainFileID());
        MyRewriter.InsertText(endOfFile, matcherCode, true, true);
    }
};


class MyFrontEndAction : public ASTFrontendAction {
private:
    Rewriter MyRewriter;
    MetaData metaData;

    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                    StringRef file) override {
        MyRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
        return std::make_unique<MyASTConsumer>(MyRewriter, metaData);
    }

    void EndSourceFileAction() override {
        SourceManager &SM = MyRewriter.getSourceMgr();

        std::string originalFile =
            SM.getFileEntryForID(SM.getMainFileID())->getName().str();

        // 1. Capture the full rewritten buffer into a std::string
        std::string rewrittenCode;
        llvm::raw_string_ostream codeStream(rewrittenCode);
        MyRewriter.getEditBuffer(SM.getMainFileID()).write(codeStream);
        codeStream.flush();

        // 2. Derive prog_name from the source filename (basename, no extension)
        std::string progName = originalFile;
        size_t slashPos = progName.rfind('/');
        if (slashPos != std::string::npos) progName = progName.substr(slashPos + 1);
        size_t dotPos = progName.rfind('.');
        if (dotPos != std::string::npos) progName = progName.substr(0, dotPos);

        // 3. Build the two globals that the generated wrappers reference.
        //    We also emit forward declarations so the wrappers in the body compile.
        std::string escaped = escapeForCString(rewrittenCode);

        // Forward declarations for all generated stubs so callers in the file
        // body (e.g. main) see the correct return types before the definitions.
        std::string fwdDecls;
        for (const auto &pair : metaData.dispatchPrototypes) {
            fwdDecls += pair.second + "\n";
        }
        for (const auto &pair : metaData.workerFuncs) {
            fwdDecls += "void *" + pair.second + "(void *);\n";
        }

        std::string header =
            "/* === Parallax: embedded program source (auto-generated) === */\n"
            "#include <string.h>\n"
            "#include \"parallax/parallax_param.h\"\n"
            "extern void execute_fxn(ParallaxParam *, int, char *, int, const char *, const char *);\n"
            + fwdDecls +
            "static const char *__parallax_prog_code__ = \"" + escaped + "\";\n"
            "static const char *__parallax_prog_name__ = \"" + progName + "\";\n\n";

        // 4. Write: header globals first, then the full rewritten source
        std::string outputFile = originalFile + "_parsed.c";
        std::error_code EC;
        llvm::raw_fd_ostream outFile(outputFile, EC, llvm::sys::fs::OF_Text);
        if (EC) {
            llvm::errs() << "Cannot open output file: " << EC.message() << "\n";
            return;
        }

        outFile << header;
        outFile << rewrittenCode;

        llvm::outs() << "Written rewritten file to: " << outputFile << "\n";
    }
};


class Parser {
public:
    Parser() {}

    int init(int argc, const char **argv) {
        static llvm::cl::OptionCategory ToolCategory("ast-tool");
        auto ExpectedParser = CommonOptionsParser::create(argc, argv, ToolCategory);
        if (!ExpectedParser) {
            llvm::errs() << ExpectedParser.takeError();
            return 1;
        }
        CommonOptionsParser &OptionsParser = ExpectedParser.get();
        ClangTool Tool(OptionsParser.getCompilations(),
                       OptionsParser.getSourcePathList());
        return Tool.run(newFrontendActionFactory<MyFrontEndAction>().get());
    }
};


int main(int argc, const char **argv) {
    Parser parser;
    parser.init(argc, argv);
}