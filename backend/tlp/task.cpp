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

using nlohmann::json;

namespace tlp {
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

// Given a Stmt, find the first tlp::task in its children.
const ExprWithCleanups* GetTlpTask(const Stmt* stmt) {
  for (auto child : stmt->children()) {
    if (auto expr = dyn_cast<ExprWithCleanups>(child)) {
      if (expr->getType().getAsString() == "struct tlp::task") {
        return expr;
      }
    }
  }
  return nullptr;
}

// Given a Stmt, find all tlp::task::invoke's via DFS and update invokes.
void GetTlpInvokes(const Stmt* stmt,
                   vector<const CXXMemberCallExpr*>& invokes) {
  for (auto child : stmt->children()) {
    GetTlpInvokes(child, invokes);
  }
  if (const auto invoke = dyn_cast<CXXMemberCallExpr>(stmt)) {
    if (invoke->getRecordDecl()->getQualifiedNameAsString() == "tlp::task" &&
        invoke->getMethodDecl()->getNameAsString() == "invoke") {
      invokes.push_back(invoke);
    }
  }
}

// Given a Stmt, return all tlp::task::invoke's via DFS.
vector<const CXXMemberCallExpr*> GetTlpInvokes(const Stmt* stmt) {
  vector<const CXXMemberCallExpr*> invokes;
  GetTlpInvokes(stmt, invokes);
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

// Apply tlp s2s transformations on a function.
bool Visitor::VisitFunctionDecl(FunctionDecl* func) {
  if (func->hasBody() && func->isGlobal() &&
      context_.getSourceManager().isWrittenInMainFile(func->getBeginLoc())) {
    if (rewriters_.size() == 0) {
      funcs_.push_back(func);
    } else {
      if (rewriters_.count(func) > 0) {
        if (func == current_task) {
          if (auto task = GetTlpTask(func->getBody())) {
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
  return GetRewriter().InsertTextAfterToken(loc, line);
}

// Apply tlp s2s transformations on a upper-level task.
void Visitor::ProcessUpperLevelTask(const ExprWithCleanups* task,
                                    const FunctionDecl* func) {
  const auto func_body = func->getBody();
  // TODO: implement qdma streams
  vector<StreamInfo> streams;
  for (const auto param : func->parameters()) {
    const string param_name = param->getNameAsString();
    if (IsTlpType(param, "(async_)?mmap")) {
      GetRewriter().ReplaceText(
          param->getTypeSourceInfo()->getTypeLoc().getSourceRange(),
          "uint64_t");
    } else if (IsTlpType(param, "(async_)?mmaps")) {
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
    if (IsTlpType(param, "(async_)?mmaps")) {
      for (int i = 0; i < GetArraySize(param); ++i) {
        add_pragma(GetArrayElem(param_name, i));
      }
    } else {
      add_pragma();
    }
  }
  replaced_body +=
      "#pragma HLS interface s_axilite port = return bundle = control\n\n";

  for (const auto param : func->parameters()) {
    auto param_name = param->getNameAsString();
    if (IsStreamInterface(param)) {
      // TODO (maybe?)
    } else if (IsTlpType(param, "(async_)?mmaps")) {
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
  // tasks: {task_name: [{step, {args: var_name: {var_type, port_name}}}]}
  // fifos: {fifo_name: {depth, produced_by, consumed_by}}
  auto& metadata = GetMetadata();
  metadata["fifos"] = json::object();

  if (*top_name == func->getNameAsString()) {
    for (const auto param : func->parameters()) {
      const auto param_name = param->getNameAsString();
      auto add_mmap_meta = [&](const string& name) {
        metadata["ports"].push_back(
            {{"name", name},
             {"cat", IsTlpType(param, "async_mmaps?") ? "async_mmap" : "mmap"},
             {"width",
              context_
                  .getTypeInfo(param->getType()
                                   ->getAs<clang::TemplateSpecializationType>()
                                   ->getArg(0)
                                   .getAsType())
                  .Width},
             {"type", GetMmapElemType(param) + "*"}});
      };
      if (IsTlpType(param, "(async_)?mmap")) {
        add_mmap_meta(param_name);
      } else if (IsTlpType(param, "(async_)?mmaps")) {
        for (int i = 0; i < GetArraySize(param); ++i) {
          add_mmap_meta(param_name + "[" + to_string(i) + "]");
        }
      } else if (IsStreamInterface(param)) {
        // TODO
      } else {
        metadata["ports"].push_back(
            {{"name", param_name},
             {"cat", "scalar"},
             {"width", context_.getTypeInfo(param->getType()).Width},
             {"type", param->getType().getAsString()}});
      }
    }
  }

  // Process stream declarations.
  unordered_map<string, const VarDecl*> fifo_decls;
  for (const auto child : func_body->children()) {
    if (const auto decl_stmt = dyn_cast<DeclStmt>(child)) {
      if (const auto var_decl = dyn_cast<VarDecl>(*decl_stmt->decl_begin())) {
        if (auto decl = GetTlpStreamDecl(var_decl->getType())) {
          const auto args = decl->getTemplateArgs().asArray();
          const string elem_type = GetTemplateArgName(args[0]);
          const uint64_t fifo_depth{*args[1].getAsIntegral().getRawData()};
          const string var_name{var_decl->getNameAsString()};
          metadata["fifos"][var_name]["depth"] = fifo_depth;
          fifo_decls[var_name] = var_decl;
        } else if (auto decl = GetTlpStreamsDecl(var_decl->getType())) {
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
  vector<const CXXMemberCallExpr*> invokes = GetTlpInvokes(task);

  for (auto invoke : invokes) {
    int step = -1;
    bool has_name = false;
    bool is_vec = false;
    uint64_t vec_length = 1;
    if (const auto method = dyn_cast<CXXMethodDecl>(invoke->getCalleeDecl())) {
      auto args = method->getTemplateSpecializationArgs()->asArray();
      step =
          *reinterpret_cast<const int*>(args[0].getAsIntegral().getRawData());
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
                        bool enable = true) -> string {
      return enable && is_vec ? ArrayNameAt(name, i) : name;
    };
    for (uint64_t i_vec = 0; i_vec < vec_length; ++i_vec) {
      for (unsigned i = 0; i < invoke->getNumArgs(); ++i) {
        const auto arg = invoke->getArg(i);
        const auto decl_ref = dyn_cast<DeclRefExpr>(arg);  // a variable
        const bool arg_is_async_mmaps = IsTlpType(decl_ref, "async_mmaps");
        const bool arg_is_streams = IsTlpType(decl_ref, "streams");
        const auto op_call =
            dyn_cast<CXXOperatorCallExpr>(arg);  // element in an array
        if (decl_ref || op_call) {
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
              (*metadata["tasks"][task_name].rbegin())["args"][arg] = {
                  {"cat", param_cat}, {"port", port}};
            };

            // regsiter stream info to task
            auto register_consumer = [&](string arg = "") {
              // use global arg_name by default
              if (arg.empty()) arg = arg_name;
              if (metadata["fifos"][arg].contains("consumed_by")) {
                auto diagnostics_builder =
                    ReportError(param->getLocation(),
                                "tlp::stream '%0' consumed more than once");
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
                                "tlp::stream '%0' produced more than once");
                diagnostics_builder.AddString(arg);
                diagnostics_builder.AddSourceRange(
                    CharSourceRange::getCharRange(param->getSourceRange()));
              }
              metadata["fifos"][arg]["produced_by"] = {
                  task_name, metadata["tasks"][task_name].size() - 1};
            };
            if (IsTlpType(param, "mmap")) {
              param_cat = "mmap";
              register_arg();
            } else if (IsTlpType(param, "async_mmap")) {
              param_cat = "async_mmap";
              // vector invocation can map async_mmaps to async_mmap
              register_arg(get_name(arg_name, i_vec, arg_is_async_mmaps));
            } else if (IsTlpType(param, "istream")) {
              param_cat = "istream";
              // vector invocation can map istreams to istream
              auto arg = get_name(arg_name, i_vec, arg_is_streams);
              register_consumer(arg);
              register_arg(arg);
            } else if (IsTlpType(param, "ostream")) {
              param_cat = "ostream";
              // vector invocation can map ostreams to ostream
              auto arg = get_name(arg_name, i_vec, arg_is_streams);
              register_producer(arg);
              register_arg(arg);
            } else if (IsTlpType(param, "istreams")) {
              param_cat = "istream";
              for (int i = 0; i < GetArraySize(param); ++i) {
                auto arg = ArrayNameAt(arg_name, i);
                register_consumer(arg);
                register_arg(arg, ArrayNameAt(param_name, i));
              }
            } else if (IsTlpType(param, "ostreams")) {
              param_cat = "ostream";
              for (int i = 0; i < GetArraySize(param); ++i) {
                auto arg = ArrayNameAt(arg_name, i);
                register_producer(arg);
                register_arg(arg, ArrayNameAt(param_name, i));
              }
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
    if (fifo.value().size() < 3) {
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

  // SDAccel only works with extern C kernels.
  GetRewriter().InsertText(func->getBeginLoc(), "extern \"C\" {\n\n");
  GetRewriter().InsertTextAfterToken(func->getEndLoc(),
                                     "\n\n}  // extern \"C\"\n");
}

// Apply tlp s2s transformations on a lower-level task.
void Visitor::ProcessLowerLevelTask(const FunctionDecl* func) {
  // Find interface streams.
  vector<StreamInfo> streams;
  vector<MmapInfo> mmaps;
  vector<AsyncMmapInfo> async_mmaps;
  for (const auto param : func->parameters()) {
    if (IsStreamInterface(param)) {
      auto elem_type = GetStreamElemType(param);
      streams.emplace_back(param->getNameAsString(), elem_type);
    } else if (IsMmap(param)) {
      auto elem_type = GetMmapElemType(param);
      mmaps.emplace_back(param->getNameAsString(), elem_type);
    } else if (IsAsyncMmap(param)) {
      auto elem_type = GetMmapElemType(param);
      AsyncMmapInfo async_mmap(param->getNameAsString(), elem_type);
      async_mmap.GetAsyncMmapInfo(func->getBody(), context_.getDiagnostics());
      async_mmaps.push_back(async_mmap);
    } else if (IsTlpType(param, "(i|o)streams")) {
      InsertHlsPragma(func->getBody()->getBeginLoc(), "data_pack",
                      {{"variable", param->getNameAsString() + "._[0].fifo"}});
      if (IsTlpType(param, "istreams")) {
        InsertHlsPragma(
            func->getBody()->getBeginLoc(), "data_pack",
            {{"variable", param->getNameAsString() + "._[0].peek_val"}});
        InsertHlsPragma(
            func->getBody()->getBeginLoc(), "array_partition",
            {{"variable", param->getNameAsString() + "._[0].peek_val"},
             {"complete", ""}});
      }
    }
  }

  // Retrieve stream information.
  const auto func_body = func->getBody();
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
    auto stream_ops = GetTlpStreamOps(loop_stmt);
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
    static const auto tlp_stream_note = diagnostics_engine.getCustomDiagID(
        clang::DiagnosticsEngine::Note, "on tlp::stream '%0'");
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
              diagnostics_engine.Report(expr->getBeginLoc(), tlp_stream_note);
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
              diagnostics_engine.Report(expr->getBeginLoc(), tlp_stream_note);
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

    auto mmap_ops = GetTlpMmapOps(loop_stmt);
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
}  // namespace tlp