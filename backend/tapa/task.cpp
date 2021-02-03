#include "task.h"

#include <algorithm>
#include <array>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "clang/AST/AST.h"
#include "clang/Lex/Lexer.h"

#include "nlohmann/json.hpp"

#include "async_mmap.h"
#include "mmap.h"
#include "stream.h"

using std::array;
using std::binary_search;
using std::initializer_list;
using std::make_shared;
using std::pair;
using std::shared_ptr;
using std::sort;
using std::string;
using std::to_string;
using std::unordered_map;
using std::vector;

using clang::CharSourceRange;
using clang::CXXMemberCallExpr;
using clang::CXXMethodDecl;
using clang::CXXOperatorCallExpr;
using clang::DeclGroupRef;
using clang::DeclRefExpr;
using clang::DeclStmt;
using clang::DoStmt;
using clang::ElaboratedType;
using clang::Expr;
using clang::ExprWithCleanups;
using clang::ForStmt;
using clang::FunctionDecl;
using clang::FunctionProtoType;
using clang::IntegerLiteral;
using clang::Lexer;
using clang::LValueReferenceType;
using clang::MemberExpr;
using clang::PrintingPolicy;
using clang::RecordType;
using clang::SourceLocation;
using clang::SourceRange;
using clang::Stmt;
using clang::StringLiteral;
using clang::TemplateArgument;
using clang::TemplateSpecializationType;
using clang::VarDecl;
using clang::WhileStmt;

using llvm::dyn_cast;
using llvm::join;
using llvm::StringRef;

using nlohmann::json;

namespace tapa {
namespace internal {

extern const string* top_name;

// Get a string representation of the function signature a stream operation.
std::string GetSignature(const CXXMemberCallExpr* call_expr) {
  auto target = call_expr->getDirectCallee();
  assert(target != nullptr);

  if (const auto instantiated = target->getTemplateInstantiationPattern()) {
    target = instantiated;
  }

  string signature{target->getQualifiedNameAsString()};

  signature += "(";

  for (auto param : target->parameters()) {
    PrintingPolicy policy{{}};
    policy.Bool = true;
    signature.append(param->getType().getAsString(policy));
    signature += ", ";
  }

  if (target->isVariadic()) {
    signature += ("...");
  } else if (target->getNumParams() > 0) {
    signature.resize(signature.size() - 2);
  }
  signature += ")";

  if (auto target_type =
          dyn_cast<FunctionProtoType>(target->getType().getTypePtr())) {
    if (target_type->isConst()) signature.append(" const");
    if (target_type->isVolatile()) signature.append(" volatile");
    if (target_type->isRestrict()) signature.append(" restrict");

    switch (target_type->getRefQualifier()) {
      case clang::RQ_LValue:
        signature.append(" &");
        break;
      case clang::RQ_RValue:
        signature.append(" &&");
        break;
      default:
        break;
    }
  }

  return signature;
}

// Given a Stmt, find the first tapa::task in its children.
const ExprWithCleanups* GetTapaTask(const Stmt* stmt) {
  for (auto child : stmt->children()) {
    if (auto expr = dyn_cast<ExprWithCleanups>(child)) {
      if (expr->getType().getAsString() == "struct tapa::task") {
        return expr;
      }
    }
  }
  return nullptr;
}

// Given a Stmt, find all tapa::task::invoke's via DFS and update invokes.
void GetTapaInvokes(const Stmt* stmt,
                    vector<const CXXMemberCallExpr*>& invokes) {
  for (auto child : stmt->children()) {
    GetTapaInvokes(child, invokes);
  }
  if (const auto invoke = dyn_cast<CXXMemberCallExpr>(stmt)) {
    if (invoke->getRecordDecl()->getQualifiedNameAsString() == "tapa::task" &&
        invoke->getMethodDecl()->getNameAsString() == "invoke") {
      invokes.push_back(invoke);
    }
  }
}

// Given a Stmt, return all tapa::task::invoke's via DFS.
vector<const CXXMemberCallExpr*> GetTapaInvokes(const Stmt* stmt) {
  vector<const CXXMemberCallExpr*> invokes;
  GetTapaInvokes(stmt, invokes);
  return invokes;
}

// Return all loops that do not contain other loops but do contain FIFO
// operations.
void GetInnermostLoops(const Stmt* stmt, vector<const Stmt*>& loops) {
  for (auto child : stmt->children()) {
    if (child != nullptr) {
      GetInnermostLoops(child, loops);
    }
  }
  if (RecursiveInnermostLoopsVisitor().IsInnermostLoop(stmt)) {
    loops.push_back(stmt);
  }
}
vector<const Stmt*> GetInnermostLoops(const Stmt* stmt) {
  vector<const Stmt*> loops;
  GetInnermostLoops(stmt, loops);
  return loops;
}

thread_local const FunctionDecl* Visitor::current_task{nullptr};

// Apply tapa s2s transformations on a function.
bool Visitor::VisitFunctionDecl(FunctionDecl* func) {
  if (func->hasBody() && func->isGlobal() &&
      context_.getSourceManager().isWrittenInMainFile(func->getBeginLoc())) {
    if (rewriters_.size() == 0) {
      funcs_.push_back(func);
    } else {
      if (rewriters_.count(func) > 0) {
        if (func == current_task) {
          if (auto task = GetTapaTask(func->getBody())) {
            // Run this before "extern C" is injected by
            // `ProcessUpperLevelTask`.
            if (*top_name == func->getNameAsString()) {
              metadata_[func]["frt_interface"] = GetFrtInterface(func);
            }
            ProcessUpperLevelTask(task, func);
          } else {
            ProcessLowerLevelTask(func);
          }
        } else {
          GetRewriter().RemoveText(func->getSourceRange());
        }
      }
    }
  }
  // Let the recursion continue.
  return true;
}

// Insert `#pragma HLS ...` after the token specified by loc.
bool Visitor::InsertHlsPragma(const SourceLocation& loc, const string& pragma,
                              const vector<pair<string, string>>& args) {
  string line{"\n#pragma HLS " + pragma};
  for (const auto& arg : args) {
    line += " " + arg.first;
    if (!arg.second.empty()) {
      line += " = " + arg.second;
    }
  }
  line += "\n";
  return GetRewriter().InsertTextAfterToken(loc, line);
}

// Apply tapa s2s transformations on a upper-level task.
void Visitor::ProcessUpperLevelTask(const ExprWithCleanups* task,
                                    const FunctionDecl* func) {
  const auto func_body = func->getBody();
  // TODO: implement qdma streams
  vector<StreamInfo> streams;
  for (const auto param : func->parameters()) {
    const string param_name = param->getNameAsString();
    if (IsTapaType(param, "(async_)?mmap")) {
      GetRewriter().ReplaceText(
          param->getTypeSourceInfo()->getTypeLoc().getSourceRange(),
          "uint64_t");
    } else if (IsTapaType(param, "(async_)?mmaps")) {
      string rewritten_text;
      for (int i = 0; i < GetArraySize(param); ++i) {
        if (!rewritten_text.empty()) rewritten_text += ", ";
        rewritten_text += "uint64_t " + GetArrayElem(param_name, i);
      }
      GetRewriter().ReplaceText(param->getSourceRange(), rewritten_text);
    }
  }

  string replaced_body{"{\n"};
  for (const auto param : func->parameters()) {
    auto param_name = param->getNameAsString();
    auto add_pragma = [&](string port = "") {
      if (port.empty()) port = param_name;
      replaced_body += "#pragma HLS interface s_axilite port = " + port +
                       " bundle = control\n";
    };
    if (IsTapaType(param, "(async_)?mmaps")) {
      for (int i = 0; i < GetArraySize(param); ++i) {
        add_pragma(GetArrayElem(param_name, i));
      }
    } else if (IsStreamInterface(param)) {
      replaced_body +=
          "#pragma HLS data_pack variable = " + param_name + ".fifo\n";
      if (IsTapaType(param, "istream")) {
        replaced_body +=
            "#pragma HLS data_pack variable = " + param_name + ".peek_val\n";
      }
    } else if (*top_name == func->getNameAsString()) {
      add_pragma();
    } else {
      // middle level scalars
    }
  }
  if (*top_name == func->getNameAsString()) {
    replaced_body +=
        "#pragma HLS interface s_axilite port = return bundle = control\n";
  }
  replaced_body += "\n";

  for (const auto param : func->parameters()) {
    auto param_name = param->getNameAsString();
    if (IsStreamInterface(param)) {
      if (IsTapaType(param, "istream")) {
        replaced_body += "{ auto val = " + param_name + ".read(); }\n";
      } else if (IsTapaType(param, "ostream")) {
        auto type = GetStreamElemType(param);
        replaced_body +=
            "{ static " + type + " val; " + param_name + ".write(val); }\n";
      }
    } else if (IsTapaType(param, "(async_)?mmaps")) {
      for (int i = 0; i < GetArraySize(param); ++i) {
        replaced_body += "{ auto val = reinterpret_cast<volatile uint8_t&>(" +
                         GetArrayElem(param_name, i) + "); }\n";
      }
    } else {
      auto elem_type = param->getType();
      const bool is_const = elem_type.isConstQualified();
      replaced_body += "{ auto val = reinterpret_cast<volatile ";
      if (is_const) {
        replaced_body += "const ";
      }
      replaced_body += "uint8_t&>(" + param_name + "); }\n";
    }
  }

  replaced_body += "}\n";

  // We need a empty shell.
  GetRewriter().ReplaceText(func_body->getSourceRange(), replaced_body);

  // Obtain the connection schema from the task.
  // metadata: {tasks, fifos}
  // tasks: {task_name: [{step, {args: port_name: {var_type, var_name}}}]}
  // fifos: {fifo_name: {depth, produced_by, consumed_by}}
  auto& metadata = GetMetadata();
  metadata["fifos"] = json::object();

  for (const auto param : func->parameters()) {
    const auto param_name = param->getNameAsString();
    auto add_mmap_meta = [&](const string& name) {
      metadata["ports"].push_back(
          {{"name", name},
           {"cat", IsTapaType(param, "async_mmaps?") ? "async_mmap" : "mmap"},
           {"width",
            context_
                .getTypeInfo(GetTemplateArg(param->getType(), 0)->getAsType())
                .Width},
           {"type", GetMmapElemType(param) + "*"}});
    };
    // TODO: extend to support streams as well
    auto add_stream_meta = [&](const string& name) {
      metadata["ports"].push_back({
        {"name", name},
        {"cat", IsTapaType(param, "istream") ? "istream" : "ostream"},
        {"width", 
          context_
              .getTypeInfo(GetTemplateArg(param->getType(), 0)->getAsType())
              .Width},
        {"type", GetStreamElemType(param)}   
      });
    };
    if (IsTapaType(param, "(async_)?mmap")) {
      add_mmap_meta(param_name);
    } else if (IsTapaType(param, "(async_)?mmaps")) {
      for (int i = 0; i < GetArraySize(param); ++i) {
        add_mmap_meta(param_name + "[" + to_string(i) + "]");
      }
    } else if (IsStreamInterface(param)) {
      add_stream_meta(param_name);
    } else {
      metadata["ports"].push_back(
          {{"name", param_name},
           {"cat", "scalar"},
           {"width", context_.getTypeInfo(param->getType()).Width},
           {"type", param->getType().getAsString()}});
    }
  }

  // Process stream declarations.
  unordered_map<string, const VarDecl*> fifo_decls;
  for (const auto child : func_body->children()) {
    if (const auto decl_stmt = dyn_cast<DeclStmt>(child)) {
      if (const auto var_decl = dyn_cast<VarDecl>(*decl_stmt->decl_begin())) {
        if (auto decl = GetTapaStreamDecl(var_decl->getType())) {
          const auto args = decl->getTemplateArgs().asArray();
          const string elem_type = GetTemplateArgName(args[0]);
          const uint64_t fifo_depth{*args[1].getAsIntegral().getRawData()};
          const string var_name{var_decl->getNameAsString()};
          metadata["fifos"][var_name]["depth"] = fifo_depth;
          fifo_decls[var_name] = var_decl;
        } else if (auto decl = GetTapaStreamsDecl(var_decl->getType())) {
          const auto args = decl->getTemplateArgs().asArray();
          const string elem_type = GetTemplateArgName(args[0]);
          const uint64_t fifo_depth = *args[2].getAsIntegral().getRawData();
          for (int i = 0; i < GetArraySize(decl); ++i) {
            const string var_name = ArrayNameAt(var_decl->getNameAsString(), i);
            metadata["fifos"][var_name]["depth"] = fifo_depth;
            fifo_decls[var_name] = var_decl;
          }
        }
      }
    }
  }

  // Instanciate tasks.
  vector<const CXXMemberCallExpr*> invokes = GetTapaInvokes(task);

  for (auto invoke : invokes) {
    int step = -1;
    bool has_name = false;
    bool is_vec = false;
    uint64_t vec_length = 1;
    if (const auto method = dyn_cast<CXXMethodDecl>(invoke->getCalleeDecl())) {
      auto args = method->getTemplateSpecializationArgs()->asArray();
      step = *reinterpret_cast<const int*>(args[0].getAsIntegral().getRawData());
      if (args.size() > 1 && args[1].getKind() == TemplateArgument::Integral) {
        is_vec = true;
        vec_length = *args[1].getAsIntegral().getRawData();
      }
      if (args.rbegin()->getKind() == TemplateArgument::Integral) {
        has_name = true;
      }
    } else {
      ReportError(invoke->getCallee()->getBeginLoc(),
                  "unexpected invocation: %0")
          .AddString(invoke->getStmtClassName());
    }
    const FunctionDecl* task = nullptr;
    string task_name;
    auto get_name = [&](const string& name, uint64_t i,
                        const DeclRefExpr* decl_ref) -> string {
      if (is_vec && IsTapaType(decl_ref, "(async_mmaps|streams)")) {
        const auto ts_type =
            decl_ref->getType()->getAs<TemplateSpecializationType>();
        assert(ts_type != nullptr);
        assert(ts_type->getNumArgs() > 1);
        const auto length = this->EvalAsInt(ts_type->getArg(1).getAsExpr());
        if (i >= length) {
          auto& diagnostics = context_.getDiagnostics();
          static const auto diagnostic_id =
              diagnostics.getCustomDiagID(clang::DiagnosticsEngine::Remark,
                                          "invocation #%0 accesses '%1[%2]'");
          auto diagnostics_builder =
              diagnostics.Report(decl_ref->getBeginLoc(), diagnostic_id);
          diagnostics_builder.AddString(to_string(i));
          diagnostics_builder.AddString(decl_ref->getNameInfo().getAsString());
          diagnostics_builder.AddString(to_string(i % length));
          diagnostics_builder.AddString(decl_ref->getType().getAsString());
          diagnostics_builder.AddSourceRange(
              GetCharSourceRange(decl_ref->getSourceRange()));
        }
        return ArrayNameAt(name, i % length);
      }
      return name;
    };
    for (uint64_t i_vec = 0; i_vec < vec_length; ++i_vec) {
      for (unsigned i = 0; i < invoke->getNumArgs(); ++i) {
        const auto arg = invoke->getArg(i);
        const auto decl_ref = dyn_cast<DeclRefExpr>(arg);  // a variable
        clang::Expr::EvalResult arg_eval_as_int_result;
        const bool arg_is_int =
            arg->EvaluateAsInt(arg_eval_as_int_result, this->context_);
        const auto op_call =
            dyn_cast<CXXOperatorCallExpr>(arg);  // element in an array
        const auto arg_is_seq = IsTapaType(arg, "seq");
        if (decl_ref || op_call || arg_is_int || arg_is_seq) {
          string arg_name;
          if (decl_ref) {
            arg_name = decl_ref->getNameInfo().getName().getAsString();
          }
          if (op_call) {
            const auto array_name = dyn_cast<DeclRefExpr>(op_call->getArg(0))
                                        ->getNameInfo()
                                        .getName()
                                        .getAsString();
            const auto array_idx = this->EvalAsInt(op_call->getArg(1));
            arg_name = ArrayNameAt(array_name, array_idx);
          }
          if (arg_is_int) {
            arg_name = "64'd" +
                       std::to_string(uint64_t(
                           arg_eval_as_int_result.Val.getInt().getExtValue()));
          }
          if (i == 0) {
            task_name = arg_name;
            metadata["tasks"][task_name].push_back({{"step", step}});
            task = decl_ref->getDecl()->getAsFunction();
          } else {
            assert(task != nullptr);
            auto param = task->getParamDecl(has_name ? i - 2 : i - 1);
            auto param_name = param->getNameAsString();
            string param_cat;

            // register this argument to task
            auto register_arg = [&](string arg = "", string port = "") {
              if (arg.empty())
                arg = arg_name;  // use global arg_name by default
              if (port.empty()) port = param_name;
              (*metadata["tasks"][task_name].rbegin())["args"][port] = {
                  {"cat", param_cat}, {"arg", arg}};
            };

            // regsiter stream info to task
            auto register_consumer = [&](string arg = "") {
              // use global arg_name by default
              if (arg.empty()) arg = arg_name;
              if (metadata["fifos"][arg].contains("consumed_by")) {
                auto diagnostics_builder =
                    ReportError(param->getLocation(),
                                "tapa::stream '%0' consumed more than once");
                diagnostics_builder.AddString(arg);
                diagnostics_builder.AddSourceRange(
                    CharSourceRange::getCharRange(param->getSourceRange()));
              }
              metadata["fifos"][arg]["consumed_by"] = {
                  task_name, metadata["tasks"][task_name].size() - 1};
            };
            auto register_producer = [&](string arg = "") {
              // use global arg_name by default
              if (arg.empty()) arg = arg_name;
              if (metadata["fifos"][arg].contains("produced_by")) {
                auto diagnostics_builder =
                    ReportError(param->getLocation(),
                                "tapa::stream '%0' produced more than once");
                diagnostics_builder.AddString(arg);
                diagnostics_builder.AddSourceRange(
                    CharSourceRange::getCharRange(param->getSourceRange()));
              }
              metadata["fifos"][arg]["produced_by"] = {
                  task_name, metadata["tasks"][task_name].size() - 1};
            };
            if (IsTapaType(param, "mmap")) {
              param_cat = "mmap";
              register_arg(get_name(arg_name, i_vec, decl_ref));
            } else if (IsTapaType(param, "async_mmap")) {
              param_cat = "async_mmap";
              // vector invocation can map async_mmaps to async_mmap
              register_arg(get_name(arg_name, i_vec, decl_ref));
            } else if (IsTapaType(param, "istream")) {
              param_cat = "istream";
              // vector invocation can map istreams to istream
              auto arg = get_name(arg_name, i_vec, decl_ref);
              register_consumer(arg);
              register_arg(arg);
            } else if (IsTapaType(param, "ostream")) {
              param_cat = "ostream";
              // vector invocation can map ostreams to ostream
              auto arg = get_name(arg_name, i_vec, decl_ref);
              register_producer(arg);
              register_arg(arg);
            } else if (IsTapaType(param, "istreams")) {
              param_cat = "istream";
              for (int i = 0; i < GetArraySize(param); ++i) {
                auto arg = ArrayNameAt(arg_name, i);
                register_consumer(arg);
                register_arg(arg, ArrayNameAt(param_name, i));
              }
            } else if (IsTapaType(param, "ostreams")) {
              param_cat = "ostream";
              for (int i = 0; i < GetArraySize(param); ++i) {
                auto arg = ArrayNameAt(arg_name, i);
                register_producer(arg);
                register_arg(arg, ArrayNameAt(param_name, i));
              }
            } else if (arg_is_seq) {
              param_cat = "scalar";
              register_arg("64'd" + std::to_string(i_vec));
            } else {
              param_cat = "scalar";
              register_arg();
            }
          }
          continue;
        } else if (const auto string_literal = dyn_cast<StringLiteral>(arg)) {
          if (i == 1 && has_name) {
            (*metadata["tasks"][task_name].rbegin())["name"] =
                string_literal->getString();
            continue;
          }
        }
        auto diagnostics_builder =
            ReportError(arg->getBeginLoc(), "unexpected argument: %0");
        diagnostics_builder.AddString(arg->getStmtClassName());
        diagnostics_builder.AddSourceRange(GetCharSourceRange(arg));
      }
    }
  }

  for (auto& fifo : metadata["fifos"].items()) {
    if (!fifo.value().contains("consumed_by") &&
        !fifo.value().contains("produced_by")) {
      auto fifo_name = fifo.key();
      auto fifo_decl = fifo_decls[fifo_name];
      auto& diagnostics = context_.getDiagnostics();
      static const auto diagnostic_id = diagnostics.getCustomDiagID(
          clang::DiagnosticsEngine::Warning, "unused stream: %0");
      auto diagnostics_builder =
          diagnostics.Report(fifo_decl->getBeginLoc(), diagnostic_id);
      diagnostics_builder.AddString(fifo_name);
      diagnostics_builder.AddSourceRange(
          GetCharSourceRange(fifo_decl->getSourceRange()));
      metadata["fifos"].erase(fifo_name);
    }
  }

  if (*top_name == func->getNameAsString()) {
    // SDAccel only works with extern C kernels.
    GetRewriter().InsertText(func->getBeginLoc(), "extern \"C\" {\n\n");
    GetRewriter().InsertTextAfterToken(func->getEndLoc(),
                                       "\n\n}  // extern \"C\"\n");
  }
}

// Apply tapa s2s transformations on a lower-level task.
void Visitor::ProcessLowerLevelTask(const FunctionDecl* func) {
  const auto func_body = func->getBody();

  // Find interface streams.
  vector<StreamInfo> streams;
  vector<MmapInfo> mmaps;
  vector<AsyncMmapInfo> async_mmaps;
  for (const auto param : func->parameters()) {
    if (IsStreamInterface(param)) {
      auto elem_type = GetStreamElemType(param);
      streams.emplace_back(param->getNameAsString(),
                           elem_type, /*is_producer= */
                           IsTapaType(param, "ostream"),
                           /* is_consumer= */
                           IsTapaType(param, "istream"));
    } else if (IsTapaType(param, "mmap")) {
      auto elem_type = GetMmapElemType(param);
      mmaps.emplace_back(param->getNameAsString(), elem_type);
    } else if (IsTapaType(param, "async_mmap")) {
      auto elem_type = GetMmapElemType(param);
      AsyncMmapInfo async_mmap(param->getNameAsString(), elem_type);
      async_mmap.GetAsyncMmapInfo(func_body, context_.getDiagnostics());
      async_mmaps.push_back(async_mmap);
    } else if (IsTapaType(param, "(i|o)streams")) {
      InsertHlsPragma(func_body->getBeginLoc(), "data_pack",
                      {{"variable", param->getNameAsString() + "._[0].fifo"}});
      if (IsTapaType(param, "istreams")) {
        auto peek_var = param->getNameAsString() + "._[0].peek_val";
        InsertHlsPragma(func_body->getBeginLoc(), "data_pack",
                        {{"variable", peek_var}});
        InsertHlsPragma(func_body->getBeginLoc(), "interface",
                        {{"ap_stable", {}}, {"port", peek_var}});
        InsertHlsPragma(func_body->getBeginLoc(), "array_partition",
                        {{"variable", peek_var}, {"complete", {}}});
      }
    }
  }

  // Retrieve stream information.
  GetStreamInfo(func_body, streams, context_.getDiagnostics());

  // Insert interface pragmas.
  for (const auto& mmap : mmaps) {
    InsertHlsPragma(func_body->getBeginLoc(), "interface",
                    {{"m_axi", {}},
                     {"port", mmap.name},
                     {"offset", "direct"},
                     {"bundle", mmap.name}});
  }

  // Before the original function body, insert data_pack pragmas.
  for (const auto& stream : streams) {
    InsertHlsPragma(func_body->getBeginLoc(), "data_pack",
                    {{"variable", stream.FifoVar()}});
    if (stream.is_consumer) {
      InsertHlsPragma(func_body->getBeginLoc(), "data_pack",
                      {{"variable", stream.PeekVar()}});
      if (!stream.need_peeking) {
        InsertHlsPragma(func_body->getBeginLoc(), "interface",
                        {{"ap_stable", {}}, {"port", stream.PeekVar()}});
      }
    }
  }
  for (const auto& mmap : mmaps) {
    InsertHlsPragma(func_body->getBeginLoc(), "data_pack",
                    {{"variable", mmap.name}});
  }
  for (const auto& async_mmap : async_mmaps) {
    if (async_mmap.is_data) {
      if (async_mmap.is_write) {
        InsertHlsPragma(func_body->getBeginLoc(), "data_pack",
                        {{"variable", async_mmap.WriteDataVar()}});
      }
      if (async_mmap.is_read) {
        InsertHlsPragma(func_body->getBeginLoc(), "data_pack",
                        {{"variable", async_mmap.ReadDataVar()}});
      }
    }
  }
  if (!streams.empty()) {
    GetRewriter().InsertTextAfterToken(func_body->getBeginLoc(), "\n\n");
  }

  // Rewrite stream operations via DFS.
  unordered_map<const CXXMemberCallExpr*, const StreamInfo*> stream_table;
  for (const auto& stream : streams) {
    for (auto call_expr : stream.call_exprs) {
      stream_table[call_expr] = &stream;
    }
  }

  // Find loops that contain FIFOs operations but do not contain sub-loops;
  // These loops will be pipelined with II = 1.
  for (auto loop_stmt : GetInnermostLoops(func_body)) {
    InsertHlsPragma(GetLoopBody(loop_stmt)->getBeginLoc(), "pipeline",
                    {{"II", "1"}});
    auto stream_ops = GetTapaStreamOps(loop_stmt);
    sort(stream_ops.begin(), stream_ops.end());
    auto is_accessed = [&stream_ops](const StreamInfo& stream) -> bool {
      for (auto expr : stream.call_exprs) {
        if (binary_search(stream_ops.begin(), stream_ops.end(), expr)) {
          return true;
        }
      }
      return false;
    };

    // Blocking reads (destructive or nondestructive) cannot be used.
    auto& diagnostics_engine = context_.getDiagnostics();
    static const auto blocking_read_in_pipeline_error =
        diagnostics_engine.getCustomDiagID(
            clang::DiagnosticsEngine::Error,
            "blocking read cannot be used in an innermost loop with peeking");
    static const auto blocking_write_in_pipeline_warning =
        diagnostics_engine.getCustomDiagID(
            clang::DiagnosticsEngine::Warning,
            "blocking write used in an innermost loop may lead to incorrect "
            "RTL code");
    static const auto tapa_stream_note = diagnostics_engine.getCustomDiagID(
        clang::DiagnosticsEngine::Note, "on tapa::stream '%0'");
    for (const auto& stream : streams) {
      decltype(stream.call_exprs) call_exprs;
      for (auto expr : stream.call_exprs) {
        auto stream_op = GetStreamOp(expr);
        if (static_cast<bool>(stream_op & StreamOpEnum::kIsBlocking) &&
            static_cast<bool>(stream_op & StreamOpEnum::kIsConsumer) &&
            binary_search(stream_ops.begin(), stream_ops.end(), expr)) {
          call_exprs.push_back(expr);
        }
      }

      if (!call_exprs.empty() && stream.need_peeking) {
        diagnostics_engine.Report(blocking_read_in_pipeline_error);
        for (auto expr : call_exprs) {
          auto diagnostics_builder =
              diagnostics_engine.Report(expr->getBeginLoc(), tapa_stream_note);
          diagnostics_builder.AddString(stream.name);
          diagnostics_builder.AddSourceRange(CharSourceRange::getCharRange(
              expr->getBeginLoc(), expr->getEndLoc().getLocWithOffset(1)));
        }
      }
    }

    for (const auto& stream : streams) {
      decltype(stream.call_exprs) call_exprs;
      for (auto expr : stream.call_exprs) {
        auto stream_op = GetStreamOp(expr);
        if (static_cast<bool>(stream_op & StreamOpEnum::kIsBlocking) &&
            static_cast<bool>(stream_op & StreamOpEnum::kIsProducer) &&
            binary_search(stream_ops.begin(), stream_ops.end(), expr)) {
          call_exprs.push_back(expr);
        }
      }

      if (!call_exprs.empty()) {
        diagnostics_engine.Report(blocking_write_in_pipeline_warning);
        for (auto expr : call_exprs) {
          auto diagnostics_builder =
              diagnostics_engine.Report(expr->getBeginLoc(), tapa_stream_note);
          diagnostics_builder.AddString(stream.name);
          diagnostics_builder.AddSourceRange(CharSourceRange::getCharRange(
              expr->getBeginLoc(), expr->getEndLoc().getLocWithOffset(1)));
        }
      }
    }

    // Is peeking buffer needed for this loop.
    bool need_peeking = false;
    for (const auto& stream : streams) {
      if (is_accessed(stream) && stream.is_consumer && stream.need_peeking) {
        need_peeking = true;
        break;
      }
    }

    auto mmap_ops = GetTapaMmapOps(loop_stmt);
    if (!mmap_ops.empty() && !need_peeking) {
      continue;
    }

    string loop_preamble;
    for (const auto& stream : streams) {
      if (is_accessed(stream) && stream.is_consumer && stream.is_blocking) {
        if (!loop_preamble.empty()) {
          loop_preamble += " && ";
        }
        loop_preamble += "!" + stream.name + ".empty()";
      }
    }
  }
}  // namespace internal

string Visitor::GetFrtInterface(const FunctionDecl* func) {
  auto func_body_source_range = func->getBody()->getSourceRange();
  auto& source_manager = context_.getSourceManager();
  auto main_file_id = source_manager.getMainFileID();

  vector<string> content;
  content.reserve(5 + func->getNumParams());

  // Content before the function body.
  content.push_back(join(
      initializer_list<StringRef>{
          "#include <sstream>",
          "#include <stdexcept>",
          "#include <frt.h>",
          "\n",
      },
      "\n"));
  content.push_back(GetRewriter().getRewrittenText(
      SourceRange(source_manager.getLocForStartOfFile(main_file_id),
                  func_body_source_range.getBegin())));

  // Function body.
  content.push_back(join(
      initializer_list<StringRef>{
          "\n#define TAPAB_APP \"TAPAB_",
          func->getNameAsString(),
          "\"\n",
      },
      ""));
  content.push_back(R"(#define TAPAB "TAPAB"
  const char* _tapa_bitstream = nullptr;
  if ((_tapa_bitstream = getenv(TAPAB_APP)) ||
      (_tapa_bitstream = getenv(TAPAB))) {
    fpga::Instance _tapa_instance(_tapa_bitstream);
    int _tapa_arg_index = 0;
    for (const auto& _tapa_arg_info : _tapa_instance.GetArgsInfo()) {
      if (false) {)");
  for (auto param : func->parameters()) {
    const auto name = param->getNameAsString();
    if (IsTapaType(param, "(async_)?mmaps?")) {
      // TODO: Leverage kernel information.
      bool write_device = true;
      bool read_device =
          !GetTemplateArg(param->getType(), 0)->getAsType().isConstQualified();
      auto direction =
          write_device ? (read_device ? "ReadWrite" : "WriteOnly") : "ReadOnly";
      auto add_param = [&content, direction](StringRef name, StringRef var) {
        content.push_back(join(
            initializer_list<StringRef>{
                R"(
      } else if (_tapa_arg_info.name == ")",
                name,
                R"(") {
        auto _tapa_arg = fpga::)",
                direction,
                R"(()",
                var,
                R"(.get(), )",
                var,
                R"(.size());
        _tapa_instance.AllocBuf(_tapa_arg_index, _tapa_arg);
        _tapa_instance.SetArg(_tapa_arg_index, _tapa_arg);)",
            },
            ""));
      };
      if (IsTapaType(param, "(async_)?mmaps")) {
        const uint64_t array_size = GetArraySize(param);
        for (uint64_t i = 0; i < array_size; ++i) {
          add_param(GetArrayElem(name, i), ArrayNameAt(name, i));
        }
      } else {
        add_param(name, name);
      }
    } else if (IsTapaType(param, "(i|o)streams?")) {
      content.push_back("\n#error stream not supported yet\n");
    } else {
      content.push_back(join(
          initializer_list<StringRef>{
              R"(
      } else if (_tapa_arg_info.name == ")",
              name,
              R"(") {
        _tapa_instance.SetArg(_tapa_arg_index, )",
              name,
              R"();)",
          },
          ""));
    }
  }
  content.push_back(
      R"(
      } else {
        std::stringstream ss;
        ss << "unknown argument: " << _tapa_arg_info;
        throw std::runtime_error(ss.str());
      }
      ++_tapa_arg_index;
    }
    _tapa_instance.WriteToDevice();
    _tapa_instance.Exec();
    _tapa_instance.ReadFromDevice();
    _tapa_instance.Finish();
  } else {
    throw std::runtime_error("no bitstream found; please set `" TAPAB_APP
                             "` or `" TAPAB "`");
  }
)");

  // Content after the function body.
  content.push_back(GetRewriter().getRewrittenText(
      SourceRange(func_body_source_range.getEnd(),
                  source_manager.getLocForEndOfFile(main_file_id))));

  // Join everything together (without excessive copying).
  return llvm::join(content.begin(), content.end(), "");
}

SourceLocation Visitor::GetEndOfLoc(SourceLocation loc) {
  return loc.getLocWithOffset(Lexer::MeasureTokenLength(
      loc, GetRewriter().getSourceMgr(), GetRewriter().getLangOpts()));
}
CharSourceRange Visitor::GetCharSourceRange(SourceRange range) {
  return CharSourceRange::getCharRange(range.getBegin(),
                                       GetEndOfLoc(range.getEnd()));
}
CharSourceRange Visitor::GetCharSourceRange(const Stmt* stmt) {
  return GetCharSourceRange(stmt->getSourceRange());
}

int64_t Visitor::EvalAsInt(const Expr* expr) {
  clang::Expr::EvalResult result;
  if (expr->EvaluateAsInt(result, this->context_)) {
    return result.Val.getInt().getExtValue();
  }
  this->ReportError(expr->getBeginLoc(),
                    "fail to evaluate as integer at compile time")
      .AddSourceRange(this->GetCharSourceRange(expr));
  return -1;
}

}  // namespace internal
}  // namespace tapa
