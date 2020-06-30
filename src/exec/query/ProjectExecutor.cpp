/* Copyright (c) 2020 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#include "exec/query/ProjectExecutor.h"

#include "context/ExpressionContextImpl.h"
#include "parser/Clauses.h"
#include "planner/Query.h"

namespace nebula {
namespace graph {

folly::Future<Status> ProjectExecutor::execute() {
    dumpLog();
    auto* project = asNode<Project>(node());
    auto columns = project->columns()->columns();
    auto iter = ectx_->getResult(project->inputVar()).iter();
    DCHECK(!!iter);
    ExpressionContextImpl ctx(ectx_, iter.get());

    DataSet ds;
    ds.colNames = project->colNames();
    for (; iter->valid(); iter->next()) {
        Row row;
        for (auto& col : columns) {
            Value val = col->expr()->eval(ctx);
            row.values.emplace_back(std::move(val));
        }
        ds.rows.emplace_back(std::move(row));
    }
    return finish(ExecResult::buildSequential(
        Value(std::move(ds)), State(State::Stat::kSuccess, "")));
}

}  // namespace graph
}  // namespace nebula