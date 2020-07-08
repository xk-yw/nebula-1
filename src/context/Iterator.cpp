/* Copyright (c) 2020 vesoft inc. All rights reserved.
 *
 * This source code is licensed under Apache 2.0 License,
 * attached with Common Clause Condition 1.0, found in the LICENSES directory.
 */

#include "context/Iterator.h"

#include "common/datatypes/Vertex.h"
#include "common/datatypes/Edge.h"
#include "common/interface/gen-cpp2/common_types.h"

namespace nebula {
namespace graph {

GetNeighborsIter::GetNeighborsIter(std::shared_ptr<Value> value)
    : Iterator(value, Kind::kGetNeighbors) {
    auto status = processList(value);
    if (UNLIKELY(!status.ok())) {
        LOG(ERROR) << status;
        clear();
        return;
    }
    iter_ = logicalRows_.begin();
    valid_ = true;
}

Status GetNeighborsIter::processList(std::shared_ptr<Value> value) {
    if (UNLIKELY(!value->isList())) {
        std::stringstream ss;
        ss << "Value type is not list, type: " << value->type();
        return Status::Error(ss.str());
    }
    size_t idx = 0;
    for (auto& val : value->getList().values) {
        if (UNLIKELY(!val.isDataSet())) {
            return Status::Error("There is a value in list which is not a data set.");
        }
        auto status = makeDataSetIndex(val.getDataSet(), idx++);
        NG_RETURN_IF_ERROR(status);
        dsIndices_.emplace_back(std::move(status).value());
    }
    return Status::OK();
}

StatusOr<GetNeighborsIter::DataSetIndex> GetNeighborsIter::makeDataSetIndex(const DataSet& ds,
                                                                            size_t idx) {
    DataSetIndex dsIndex;
    dsIndex.ds = &ds;
    auto buildResult = buildIndex(&dsIndex);
    NG_RETURN_IF_ERROR(buildResult);
    int64_t edgeStartIndex = std::move(buildResult).value();
    if (edgeStartIndex < 0) {
        for (auto& row : dsIndex.ds->rows) {
            logicalRows_.emplace_back(LogicalRow{idx, &row, "", nullptr});
        }
    } else {
        makeLogicalRowByEdge(edgeStartIndex, idx, dsIndex);
    }
    return dsIndex;
}

void GetNeighborsIter::makeLogicalRowByEdge(int64_t edgeStartIndex,
                                            size_t idx,
                                            const DataSetIndex& dsIndex) {
    for (auto& row : dsIndex.ds->rows) {
        auto& cols = row.values;
        for (size_t column = edgeStartIndex; column < cols.size() - 1; ++column) {
            if (!cols[column].isList()) {
                // Ignore the bad value.
                continue;
            }
            for (auto& edge : cols[column].getList().values) {
                if (!edge.isList()) {
                    // Ignore the bad value.
                    continue;
                }
                auto edgeName = dsIndex.tagEdgeNameIndices.find(column);
                DCHECK(edgeName != dsIndex.tagEdgeNameIndices.end());
                logicalRows_.emplace_back(LogicalRow{idx, &row, edgeName->second, &edge.getList()});
            }
        }
    }
}

bool checkColumnNames(const std::vector<std::string>& colNames) {
    return colNames.size() < 3 || colNames[0] != nebula::kVid || colNames[1].find("_stats") != 0 ||
           colNames.back().find("_expr") != 0;
}

StatusOr<int64_t> GetNeighborsIter::buildIndex(DataSetIndex* dsIndex) {
    auto& colNames = dsIndex->ds->colNames;
    if (UNLIKELY(checkColumnNames(colNames))) {
        return Status::Error("Bad column names.");
    }
    int64_t edgeStartIndex = -1;
    for (size_t i = 0; i < colNames.size(); ++i) {
        dsIndex->colIndices.emplace(colNames[i], i);
        auto& colName = colNames[i];
        if (colName.find("_tag") == 0) {
            NG_RETURN_IF_ERROR(buildPropIndex(colName, i, false, dsIndex));
        } else if (colName.find("_edge") == 0) {
            NG_RETURN_IF_ERROR(buildPropIndex(colName, i, true, dsIndex));
            if (edgeStartIndex < 0) {
                edgeStartIndex = i;
            }
        } else {
            // It is "_vid", "_stats", "_expr" in this situation.
        }
    }

    return edgeStartIndex;
}

Status GetNeighborsIter::buildPropIndex(const std::string& props,
                                       size_t columnId,
                                       bool isEdge,
                                       DataSetIndex* dsIndex) {
    std::vector<std::string> pieces;
    folly::split(":", props, pieces);
    if (UNLIKELY(pieces.size() < 2)) {
        return Status::Error("Bad column name format: %s", props.c_str());
    }

    PropIndex propIdx;
    // if size == 2, it is the tag defined without props.
    if (pieces.size() > 2) {
        for (size_t i = 2; i < pieces.size(); ++i) {
            propIdx.propIndices.emplace(pieces[i], i - 2);
        }
    }

    propIdx.colIdx = columnId;
    propIdx.propList.resize(pieces.size() - 2);
    std::move(pieces.begin() + 2, pieces.end(), propIdx.propList.begin());
    std::string name = pieces[1];
    if (isEdge) {
        // The first character of the tag/edge name is +/-.
        // It's not used for now.
        if (UNLIKELY(name.find("+") != 0 && name.find("-") != 0)) {
            return Status::Error("Bad edge name: %s", name.c_str());
        }
        name = name.substr(1, name.size());
        dsIndex->tagEdgeNameIndices.emplace(columnId, name);
        dsIndex->edgePropsMap.emplace(name, std::move(propIdx));
    } else {
        dsIndex->tagEdgeNameIndices.emplace(columnId, name);
        dsIndex->tagPropsMap.emplace(name, std::move(propIdx));
    }

    return Status::OK();
}

const Value& GetNeighborsIter::getColumn(const std::string& col) const {
    if (!valid()) {
        return Value::kNullValue;
    }
    auto segment = currentSeg();
    auto& index = dsIndices_[segment].colIndices;
    auto found = index.find(col);
    if (found == index.end()) {
        return Value::kNullValue;
    }
    return row()->values[found->second];
}

const Value& GetNeighborsIter::getTagProp(const std::string& tag,
                                          const std::string& prop) const {
    if (!valid()) {
        return Value::kNullValue;
    }

    auto segment = currentSeg();
    auto &tagPropIndices = dsIndices_[segment].tagPropsMap;
    auto index = tagPropIndices.find(tag);
    if (index == tagPropIndices.end()) {
        return Value::kNullValue;
    }
    auto propIndex = index->second.propIndices.find(prop);
    if (propIndex == index->second.propIndices.end()) {
        return Value::kNullValue;
    }
    auto colId = index->second.colIdx;
    auto& row = *this->row();
    DCHECK_GT(row.size(), colId);
    if (!row[colId].isList()) {
        return Value::kNullBadType;
    }
    auto& list = row[colId].getList();
    return list.values[propIndex->second];
}

const Value& GetNeighborsIter::getEdgeProp(const std::string& edge,
                                           const std::string& prop) const {
    if (!valid()) {
        return Value::kNullValue;
    }

    auto currentEdge = currentEdgeName();
    if (edge != "*" && currentEdge != edge) {
        VLOG(1) << "Current edge: " << currentEdgeName() << " Wanted: " << edge;
        return Value::kNullValue;
    }
    auto segment = currentSeg();
    auto index = dsIndices_[segment].edgePropsMap.find(currentEdge);
    if (index == dsIndices_[segment].edgePropsMap.end()) {
        VLOG(1) << "No edge found: " << edge;
        return Value::kNullValue;
    }
    auto propIndex = index->second.propIndices.find(prop);
    if (propIndex == index->second.propIndices.end()) {
        VLOG(1) << "No edge prop found: " << prop;
        return Value::kNullValue;
    }
    auto* list = currentEdgeProps();
    return list->values[propIndex->second];
}

Value GetNeighborsIter::getVertex() const {
    if (!valid()) {
        return Value::kNullValue;
    }

    auto segment = currentSeg();
    auto vidVal = getColumn(nebula::kVid);
    if (!vidVal.isStr()) {
        return Value::kNullBadType;
    }
    Vertex vertex;
    vertex.vid = vidVal.getStr();
    auto& tagPropMap = dsIndices_[segment].tagPropsMap;
    for (auto& tagProp : tagPropMap) {
        auto& row = *this->row();
        auto& tagPropNameList = tagProp.second.propList;
        auto tagColId = tagProp.second.colIdx;
        if (!row[tagColId].isList()) {
            // Ignore the bad value.
            continue;
        }
        DCHECK_GE(row.size(), tagColId);
        auto& propList = row[tagColId].getList();
        DCHECK_EQ(tagPropNameList.size(), propList.values.size());
        Tag tag;
        tag.name = tagProp.first;
        for (size_t i = 0; i < propList.size(); ++i) {
            tag.props.emplace(tagPropNameList[i], propList[i]);
        }
        vertex.tags.emplace_back(std::move(tag));
    }
    return Value(std::move(vertex));
}

Value GetNeighborsIter::getEdge() const {
    if (!valid()) {
        return Value::kNullValue;
    }

    auto segment = currentSeg();
    Edge edge;
    auto& edgeName = currentEdgeName();
    edge.name = edgeName;
    auto& src = getColumn(kVid);
    if (!src.isStr()) {
        return Value::kNullBadType;
    }
    edge.src = src.getStr();

    auto& dst = getEdgeProp(edgeName, kDst);
    if (!dst.isStr()) {
        return Value::kNullBadType;
    }
    edge.dst = dst.getStr();

    auto& rank = getEdgeProp(edgeName, kRank);
    if (!rank.isInt()) {
        return Value::kNullBadType;
    }
    edge.ranking = rank.getInt();
    edge.type = 0;

    auto& edgePropMap = dsIndices_[segment].edgePropsMap;
    auto edgeProp = edgePropMap.find(edgeName);
    if (edgeProp == edgePropMap.end()) {
        return Value::kNullValue;
    }
    auto& edgeNamePropList = edgeProp->second.propList;
    auto& propList = currentEdgeProps()->values;
    DCHECK_EQ(edgeNamePropList.size(), propList.size());
    for (size_t i = 0; i < propList.size(); ++i) {
        auto propName = edgeNamePropList[i];
        if (propName == kSrc || propName == kDst
                || propName == kRank || propName == kType) {
            continue;
        }
        edge.props.emplace(edgeNamePropList[i], propList[i]);
    }
    return Value(std::move(edge));
}
}  // namespace graph
}  // namespace nebula