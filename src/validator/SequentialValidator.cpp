/* Copyright (c) 2020 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#include "common/base/Base.h"
#include "validator/SequentialValidator.h"
#include "service/GraphFlags.h"
#include "service/PermissionCheck.h"
#include "planner/Query.h"

namespace nebula {
namespace graph {
Status SequentialValidator::validateImpl() {
    Status status;
    if (sentence_->kind() != Sentence::Kind::kSequential) {
        return Status::Error(
                "Sequential validator validates a SequentialSentences, but %ld is given.",
                static_cast<int64_t>(sentence_->kind()));
    }
    auto seqSentence = static_cast<SequentialSentences*>(sentence_);
    auto sentences = seqSentence->sentences();

    DCHECK(!sentences.empty());
    auto firstSentence = getFirstSentence(sentences.front());
    switch (firstSentence->kind()) {
        case Sentence::Kind::kLimit:
        case Sentence::Kind::kOrderBy:
        case Sentence::Kind::KGroupBy:
            return Status::SyntaxError("Could not start with the statement: %s",
                                       firstSentence->toString().c_str());
        default:
            break;
    }

    for (auto* sentence : sentences) {
        if (FLAGS_enable_authorize) {
            auto *session = qctx_->rctx()->session();
            /**
             * Skip special operations check at here. they are :
             * kUse, kDescribeSpace, kRevoke and kGrant.
             */
            if (!PermissionCheck::permissionCheck(DCHECK_NOTNULL(session), sentence)) {
                return Status::PermissionError("Permission denied");
            }
        }
        auto validator = makeValidator(sentence, qctx_);
        status = validator->validate();
        if (!status.ok()) {
            return status;
        }
        validators_.emplace_back(std::move(validator));
    }

    return Status::OK();
}

Status SequentialValidator::toPlan() {
    auto* plan = qctx_->plan();
    root_ = validators_.back()->root();
    ifBuildDataCollectForRoot(root_);
    for (auto iter = validators_.begin(); iter < validators_.end() - 1; ++iter) {
        auto status = Validator::appendPlan((iter + 1)->get()->tail(), iter->get()->root());
        if (!status.ok()) {
            return status;
        }
    }
    tail_ = StartNode::make(plan);
    Validator::appendPlan(validators_.front()->tail(), tail_);
    return Status::OK();
}

const Sentence* SequentialValidator::getFirstSentence(const Sentence* sentence) const {
    if (sentence->kind() != Sentence::Kind::kPipe) {
        return sentence;
    }
    auto pipe = static_cast<const PipedSentence *>(sentence);
    return getFirstSentence(pipe->left());
}

void SequentialValidator::ifBuildDataCollectForRoot(PlanNode* root) {
    switch (root->kind()) {
        case PlanNode::Kind::kSort:
        case PlanNode::Kind::kLimit:
        case PlanNode::Kind::kDedup:
        case PlanNode::Kind::kUnion:
        case PlanNode::Kind::kIntersect:
        case PlanNode::Kind::kMinus: {
            auto* dc = DataCollect::make(qctx_->plan(), root,
                DataCollect::CollectKind::kRowBasedMove, {root->varName()});
            dc->setColNames(root->colNames());
            root_ = dc;
            break;
        }
        default:
            break;
    }
}
}  // namespace graph
}  // namespace nebula