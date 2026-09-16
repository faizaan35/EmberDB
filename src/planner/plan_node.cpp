#include "emberdb/planner/plan_node.h"
#include <sstream>

namespace emberdb {

std::string PlanNodeTypeToString(PlanNodeType type) {
    switch (type) {
        case PlanNodeType::SEQ_SCAN: return "SeqScan";
        case PlanNodeType::INDEX_SCAN: return "IndexScan";
        case PlanNodeType::FILTER: return "Filter";
        case PlanNodeType::PROJECTION: return "Projection";
        case PlanNodeType::SORT: return "Sort";
        case PlanNodeType::LIMIT: return "Limit";
        case PlanNodeType::AGGREGATE: return "Aggregate";
        case PlanNodeType::NESTED_LOOP_JOIN: return "NestedLoopJoin";
    }
    return "UnknownPlanNode";
}

static std::string Indent(int level) {
    return std::string(level * 2, ' ');
}

std::string SeqScanPlanNode::ToString(int indent) const {
    std::ostringstream ss;
    ss << Indent(indent) << "SeqScan [table=" << table_->GetName();
    if (filter_) {
        ss << ", filter=" << filter_->ToString();
    }
    ss << "]";
    return ss.str();
}

std::string IndexScanPlanNode::ToString(int indent) const {
    std::ostringstream ss;
    ss << Indent(indent) << "IndexScan [table=" << table_->GetName()
       << ", index=" << index_info_->GetIndexName();
    if (scan_type_ == IndexScanType::POINT_LOOKUP) {
        ss << ", key=" << lookup_key_.ToString();
    } else if (scan_type_ == IndexScanType::RANGE_SCAN) {
        ss << ", range=[" << low_key_.ToString() << " .. " << high_key_.ToString() << "]";
    } else {
        ss << ", full_scan";
    }
    if (residual_filter_) {
        ss << ", filter=" << residual_filter_->ToString();
    }
    ss << "]";
    return ss.str();
}

std::string FilterPlanNode::ToString(int indent) const {
    std::ostringstream ss;
    ss << Indent(indent) << "Filter [" << (predicate_ ? predicate_->ToString() : "none") << "]\n";
    if (!children_.empty() && children_[0]) {
        ss << children_[0]->ToString(indent + 1);
    }
    return ss.str();
}

std::string ProjectionPlanNode::ToString(int indent) const {
    std::ostringstream ss;
    ss << Indent(indent) << "Projection [";
    for (size_t i = 0; i < expressions_.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << expressions_[i]->ToString();
    }
    ss << "]\n";
    if (!children_.empty() && children_[0]) {
        ss << children_[0]->ToString(indent + 1);
    }
    return ss.str();
}

std::string SortPlanNode::ToString(int indent) const {
    std::ostringstream ss;
    ss << Indent(indent) << "Sort [";
    for (size_t i = 0; i < order_by_.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << order_by_[i].expr->ToString() << (order_by_[i].is_desc ? " DESC" : " ASC");
    }
    ss << "]\n";
    if (!children_.empty() && children_[0]) {
        ss << children_[0]->ToString(indent + 1);
    }
    return ss.str();
}

std::string LimitPlanNode::ToString(int indent) const {
    std::ostringstream ss;
    ss << Indent(indent) << "Limit [" << limit_ << "]\n";
    if (!children_.empty() && children_[0]) {
        ss << children_[0]->ToString(indent + 1);
    }
    return ss.str();
}

std::string AggregatePlanNode::ToString(int indent) const {
    std::ostringstream ss;
    ss << Indent(indent) << "Aggregate [group_by=[";
    for (size_t i = 0; i < group_by_.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << group_by_[i]->ToString();
    }
    ss << "], aggregates=[";
    for (size_t i = 0; i < aggregates_.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << aggregates_[i]->ToString();
    }
    ss << "]]\n";
    if (!children_.empty() && children_[0]) {
        ss << children_[0]->ToString(indent + 1);
    }
    return ss.str();
}

std::string NestedLoopJoinPlanNode::ToString(int indent) const {
    std::ostringstream ss;
    ss << Indent(indent) << "NestedLoopJoin [type=" << (join_type_ == JoinType::LEFT ? "LEFT" : "INNER");
    if (on_condition_) {
        ss << ", on=" << on_condition_->ToString();
    }
    ss << "]\n";
    if (children_.size() > 0 && children_[0]) {
        ss << children_[0]->ToString(indent + 1) << "\n";
    }
    if (children_.size() > 1 && children_[1]) {
        ss << children_[1]->ToString(indent + 1);
    }
    return ss.str();
}

} // namespace emberdb
