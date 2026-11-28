#include "decompile_eval.hpp"

#include <string>

namespace fo4::abc {

AbcDecompileStatus decompile_body(const AbcFile& file, std::uint32_t body_index, AbcFunction& out,
                                  AbcDecompileIssue& issue) {
  out = AbcFunction{};
  AbcStructure structure;
  if (recover_structure(file, body_index, structure, issue) != AbcDecompileStatus::Ok) {
    return issue.status;
  }
  const AbcMethodBody& body = file.bodies[body_index];
  if (body.method >= file.methods.size()) {
    issue.status = AbcDecompileStatus::BodyNotFound;
    issue.message = "the body names method " + std::to_string(body.method) + " which does not exist";
    return issue.status;
  }
  detail::Evaluator evaluator(file, structure, file.methods[body.method], body, out, issue);
  if (!evaluator.run() || issue.status != AbcDecompileStatus::Ok) {
    out = AbcFunction{};
    if (issue.status == AbcDecompileStatus::Ok) {
      issue.status = AbcDecompileStatus::UnsupportedExpression;
      issue.message = "the body could not be evaluated";
    }
    issue.method = body.method;
    issue.body = body_index;
    return issue.status;
  }
  return AbcDecompileStatus::Ok;
}

}  // namespace fo4::abc
