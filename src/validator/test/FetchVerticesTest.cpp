/* Copyright (c) 2020 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#include "planner/Logic.h"
#include "planner/Query.h"
#include "util/ObjectPool.h"
#include "validator/FetchVerticesValidator.h"
#include "validator/test/ValidatorTestBase.h"

namespace nebula {
namespace graph {

class FetchVerticesValidatorTest : public ValidatorTestBase {};

TEST_F(FetchVerticesValidatorTest, FetchVerticesProp) {
    auto src = std::make_unique<VariablePropertyExpression>(
        new std::string("_VARNAME_"), new std::string(kVid));
    {
        auto plan = toPlan("FETCH PROP ON person \"1\"");

        ExecutionPlan expectedPlan(pool_.get());
        auto *start = StartNode::make(&expectedPlan);

        auto tagIdResult = schemaMng_->toTagID(1, "person");
        ASSERT_TRUE(tagIdResult.ok());
        auto tagId = tagIdResult.value();
        storage::cpp2::VertexProp prop;
        prop.set_tag(tagId);
        auto *gv = GetVertices::make(&expectedPlan,
                                     start,
                                     1,
                                     src.get(),
                                     std::vector<storage::cpp2::VertexProp>{std::move(prop)},
                                     {});
        gv->setColNames({kVid, "person.name", "person.age"});
        expectedPlan.setRoot(gv);
        auto result = Eq(plan->root(), gv);
        ASSERT_TRUE(result.ok()) << result;
    }
    // With YIELD
    {
        auto plan = toPlan("FETCH PROP ON person \"1\" YIELD person.name, person.age");

        ExecutionPlan expectedPlan(pool_.get());
        auto *start = StartNode::make(&expectedPlan);

        auto tagIdResult = schemaMng_->toTagID(1, "person");
        ASSERT_TRUE(tagIdResult.ok());
        auto tagId = tagIdResult.value();
        storage::cpp2::VertexProp prop;
        prop.set_tag(tagId);
        prop.set_props(std::vector<std::string>{"name", "age"});
        storage::cpp2::Expr expr1;
        expr1.set_expr(
            TagPropertyExpression(new std::string("person"), new std::string("name")).encode());
        storage::cpp2::Expr expr2;
        expr2.set_expr(
            TagPropertyExpression(new std::string("person"), new std::string("age")).encode());
        auto *gv =
            GetVertices::make(&expectedPlan,
                              start,
                              1,
                              src.get(),
                              std::vector<storage::cpp2::VertexProp>{std::move(prop)},
                              std::vector<storage::cpp2::Expr>{std::move(expr1), std::move(expr2)});
        gv->setColNames({kVid, "person.name", "person.age"});

        // project
        auto yieldColumns = std::make_unique<YieldColumns>();
        yieldColumns->addColumn(new YieldColumn(
            new InputPropertyExpression(new std::string(kVid)), new std::string(kVid)));
        yieldColumns->addColumn(new YieldColumn(
            new TagPropertyExpression(new std::string("person"), new std::string("name"))));
        yieldColumns->addColumn(new YieldColumn(
            new TagPropertyExpression(new std::string("person"), new std::string("age"))));
        auto *project = Project::make(&expectedPlan, gv, yieldColumns.get());
        project->setColNames({kVid, "person.name", "person.age"});

        expectedPlan.setRoot(project);
        auto result = Eq(plan->root(), project);
        ASSERT_TRUE(result.ok()) << result;
    }
    // With YIELD const expression
    {
        auto plan = toPlan("FETCH PROP ON person \"1\" YIELD person.name, 1 > 1, person.age");

        ExecutionPlan expectedPlan(pool_.get());
        auto *start = StartNode::make(&expectedPlan);

        // get vertices
        auto tagIdResult = schemaMng_->toTagID(1, "person");
        ASSERT_TRUE(tagIdResult.ok());
        auto tagId = tagIdResult.value();
        storage::cpp2::VertexProp prop;
        prop.set_tag(tagId);
        prop.set_props(std::vector<std::string>{"name", "age"});
        storage::cpp2::Expr expr1;
        expr1.set_expr(
            TagPropertyExpression(new std::string("person"), new std::string("name")).encode());
        storage::cpp2::Expr expr2;
        expr2.set_expr(
            TagPropertyExpression(new std::string("person"), new std::string("age")).encode());
        auto *gv =
            GetVertices::make(&expectedPlan,
                              start,
                              1,
                              src.get(),
                              std::vector<storage::cpp2::VertexProp>{std::move(prop)},
                              std::vector<storage::cpp2::Expr>{std::move(expr1), std::move(expr2)});
        gv->setColNames({kVid, "person.name", "(1>1)", "person.age"});  // TODO(shylock) fix

        // project
        auto yieldColumns = std::make_unique<YieldColumns>();
        yieldColumns->addColumn(new YieldColumn(
            new InputPropertyExpression(new std::string(kVid)), new std::string(kVid)));
        yieldColumns->addColumn(new YieldColumn(
            new TagPropertyExpression(new std::string("person"), new std::string("name"))));
        yieldColumns->addColumn(new YieldColumn(new RelationalExpression(
            Expression::Kind::kRelGT, new ConstantExpression(1), new ConstantExpression(1))));
        yieldColumns->addColumn(new YieldColumn(
            new TagPropertyExpression(new std::string("person"), new std::string("age"))));
        auto *project = Project::make(&expectedPlan, gv, yieldColumns.get());
        project->setColNames({kVid, "person.name", "(1>1)", "person.age"});

        expectedPlan.setRoot(project);

        auto result = Eq(plan->root(), project);
        ASSERT_TRUE(result.ok()) << result;
    }
    // With YIELD combine properties
    {
        auto plan = toPlan("FETCH PROP ON person \"1\" YIELD person.name + person.age");

        ExecutionPlan expectedPlan(pool_.get());
        auto *start = StartNode::make(&expectedPlan);

        auto tagIdResult = schemaMng_->toTagID(1, "person");
        ASSERT_TRUE(tagIdResult.ok());
        auto tagId = tagIdResult.value();
        storage::cpp2::VertexProp prop;
        prop.set_tag(tagId);
        prop.set_props(std::vector<std::string>{"name", "age"});
        storage::cpp2::Expr expr1;
        expr1.set_expr(
            ArithmeticExpression(
                Expression::Kind::kAdd,
                new TagPropertyExpression(new std::string("person"), new std::string("name")),
                new TagPropertyExpression(new std::string("person"), new std::string("age")))
                .encode());

        auto *gv = GetVertices::make(&expectedPlan,
                                     start,
                                     1,
                                     src.get(),
                                     std::vector<storage::cpp2::VertexProp>{std::move(prop)},
                                     std::vector<storage::cpp2::Expr>{std::move(expr1)});
        gv->setColNames({kVid, "(person.name+person.age)"});  // TODO(shylock) fix

        // project, TODO(shylock) could push down to storage is it supported
        auto yieldColumns = std::make_unique<YieldColumns>();
        yieldColumns->addColumn(new YieldColumn(
            new InputPropertyExpression(new std::string(kVid)), new std::string(kVid)));
        yieldColumns->addColumn(new YieldColumn(new ArithmeticExpression(
            Expression::Kind::kAdd,
            new TagPropertyExpression(new std::string("person"), new std::string("name")),
            new TagPropertyExpression(new std::string("person"), new std::string("age")))));
        auto *project = Project::make(&expectedPlan, gv, yieldColumns.get());
        project->setColNames({kVid, "(person.name+person.age)"});

        expectedPlan.setRoot(project);

        auto result = Eq(plan->root(), project);
        ASSERT_TRUE(result.ok()) << result;
    }
    // With YIELD distinct
    {
        auto plan = toPlan("FETCH PROP ON person \"1\" YIELD distinct person.name, person.age");

        ExecutionPlan expectedPlan(pool_.get());
        auto *start = StartNode::make(&expectedPlan);

        auto tagIdResult = schemaMng_->toTagID(1, "person");
        ASSERT_TRUE(tagIdResult.ok());
        auto tagId = tagIdResult.value();
        storage::cpp2::VertexProp prop;
        prop.set_tag(tagId);
        prop.set_props(std::vector<std::string>{"name", "age"});
        storage::cpp2::Expr expr1;
        expr1.set_expr(
            TagPropertyExpression(new std::string("person"), new std::string("name")).encode());
        storage::cpp2::Expr expr2;
        expr2.set_expr(
            TagPropertyExpression(new std::string("person"), new std::string("age")).encode());
        auto *gv =
            GetVertices::make(&expectedPlan,
                              start,
                              1,
                              src.get(),
                              std::vector<storage::cpp2::VertexProp>{std::move(prop)},
                              std::vector<storage::cpp2::Expr>{std::move(expr1), std::move(expr2)});

        std::vector<std::string> colNames{kVid, "person.name", "person.age"};
        gv->setColNames(colNames);

        // project
        auto yieldColumns = std::make_unique<YieldColumns>();
        yieldColumns->addColumn(new YieldColumn(
            new InputPropertyExpression(new std::string(kVid)), new std::string(kVid)));
        yieldColumns->addColumn(new YieldColumn(
            new TagPropertyExpression(new std::string("person"), new std::string("name"))));
        yieldColumns->addColumn(new YieldColumn(
            new TagPropertyExpression(new std::string("person"), new std::string("age"))));
        auto *project = Project::make(&expectedPlan, gv, yieldColumns.get());
        project->setColNames(colNames);

        // dedup
        auto *dedup = Dedup::make(&expectedPlan, project);
        dedup->setColNames(colNames);

        // data collect
        auto *dataCollect = DataCollect::make(
            &expectedPlan, dedup, DataCollect::CollectKind::kRowBasedMove, {dedup->varName()});
        dataCollect->setColNames(colNames);

        expectedPlan.setRoot(dataCollect);

        auto result = Eq(plan->root(), dataCollect);
        ASSERT_TRUE(result.ok()) << result;
    }
    // ON *
    {
        auto plan = toPlan("FETCH PROP ON * \"1\"");

        ExecutionPlan expectedPlan(pool_.get());
        auto *start = StartNode::make(&expectedPlan);

        auto *gv = GetVertices::make(
            &expectedPlan, start, 1, src.get(), {}, {});
        gv->setColNames({kVid, "person.name", "person.age"});
        expectedPlan.setRoot(gv);
        auto result = Eq(plan->root(), gv);
        ASSERT_TRUE(result.ok()) << result;
    }
}

TEST_F(FetchVerticesValidatorTest, FetchVerticesInputOutput) {
    // pipe
    {
        const std::string query = "FETCH PROP ON person \"1\" YIELD person.name AS name"
                                  " | FETCH PROP ON person $-.name";
        EXPECT_TRUE(checkResult(query,
                                {
                                    PlanNode::Kind::kGetVertices,
                                    PlanNode::Kind::kProject,
                                    PlanNode::Kind::kGetVertices,
                                    PlanNode::Kind::kStart,
                                }));
    }
    // Variable
    {
        const std::string query = "$a = FETCH PROP ON person \"1\" YIELD person.name AS name;"
                                  "FETCH PROP ON person $a.name";
        EXPECT_TRUE(checkResult(query,
                                {
                                    PlanNode::Kind::kGetVertices,
                                    PlanNode::Kind::kProject,
                                    PlanNode::Kind::kGetVertices,
                                    PlanNode::Kind::kStart,
                                }));
    }

    // with project
    // pipe
    {
        const std::string query = "FETCH PROP ON person \"1\" YIELD person.name + 1 AS name"
                                  " | FETCH PROP ON person $-.name YIELD person.name + 1";
        EXPECT_TRUE(checkResult(query,
                                {
                                    PlanNode::Kind::kProject,
                                    PlanNode::Kind::kGetVertices,
                                    PlanNode::Kind::kProject,
                                    PlanNode::Kind::kGetVertices,
                                    PlanNode::Kind::kStart,
                                }));
    }
    // Variable
    {
        const std::string query = "$a = FETCH PROP ON person \"1\" YIELD person.name + 1 AS name;"
                                  "FETCH PROP ON person $a.name YIELD person.name + 1 ";
        EXPECT_TRUE(checkResult(query,
                                {
                                    PlanNode::Kind::kProject,
                                    PlanNode::Kind::kGetVertices,
                                    PlanNode::Kind::kProject,
                                    PlanNode::Kind::kGetVertices,
                                    PlanNode::Kind::kStart,
                                }));
    }
}

TEST_F(FetchVerticesValidatorTest, FetchVerticesPropFailed) {
    // mismatched tag
    ASSERT_FALSE(validate("FETCH PROP ON tag1 \"1\" YIELD tag2.prop2"));

    // not exist tag
    ASSERT_FALSE(validate("FETCH PROP ON not_exist_tag \"1\" YIELD not_exist_tag.prop1"));

    // not exist property
    ASSERT_FALSE(validate("FETCH PROP ON person \"1\" YIELD person.not_exist_property"));

    // invalid yield expression
    ASSERT_FALSE(validate("$a = FETCH PROP ON person \"1\" YIELD person.name AS name;"
                          " FETCH PROP ON person \"1\" YIELD $a.name + 1"));

    ASSERT_FALSE(validate("FETCH PROP ON person \"1\" YIELD $^.person.name"));
    ASSERT_FALSE(validate("FETCH PROP ON person \"1\" YIELD $$.person.name"));
    ASSERT_FALSE(validate("FETCH PROP ON person \"1\" YIELD person.name AS name | "
                          " FETCH PROP ON person \"1\" YIELD $-.name + 1"));
    ASSERT_FALSE(validate("FETCH PROP ON person \"1\" YIELD person._src + 1"));
    ASSERT_FALSE(validate("FETCH PROP ON person \"1\" YIELD person._type"));
    ASSERT_FALSE(validate("FETCH PROP ON person \"1\" YIELD person._rank + 1"));
    ASSERT_FALSE(validate("FETCH PROP ON person \"1\" YIELD person._dst + 1"));
}

TEST_F(FetchVerticesValidatorTest, FetchVerticesInputFailed) {
    // mismatched varirable
    ASSERT_FALSE(validate("$a = FETCH PROP ON person \"1\" YIELD person.name AS name;"
                          "FETCH PROP ON person $b.name"));

    // mismatched varirable property
    ASSERT_FALSE(validate("$a = FETCH PROP ON person \"1\" YIELD person.name AS name;"
                          "FETCH PROP ON person $a.not_exist_property"));

    // mismatched input property
    ASSERT_FALSE(validate("FETCH PROP ON person \"1\" YIELD person.name AS name | "
                          "FETCH PROP ON person $-.not_exist_property"));
}

}   // namespace graph
}   // namespace nebula