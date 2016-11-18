// Copyright 2016 Yahoo Inc. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#pragma once

#include "basic_nodes.h"
#include "function.h"
#include <vespa/vespalib/stllike/string.h>
#include <map>

namespace vespalib {
namespace eval {
namespace nodes {

class TensorSum : public Node {
private:
    Node_UP _child;
    vespalib::string _dimension;
public:
    TensorSum(Node_UP child) : _child(std::move(child)), _dimension() {}
    TensorSum(Node_UP child, const vespalib::string &dimension_in)
        : _child(std::move(child)), _dimension(dimension_in) {}
    const vespalib::string &dimension() const { return _dimension; }
    vespalib::string dump(DumpContext &ctx) const override {
        vespalib::string str;
        str += "sum(";
        str += _child->dump(ctx);
        if (!_dimension.empty()) {
            str += ",";
            str += _dimension;
        }
        str += ")";
        return str;
    }
    void accept(NodeVisitor &visitor) const override;
    size_t num_children() const override { return 1; }
    const Node &get_child(size_t idx) const override {
        (void) idx;
        assert(idx == 0);
        return *_child;
    }
    void detach_children(NodeHandler &handler) override {
        handler.handle(std::move(_child));
    }
};

class TensorMap : public Node {
private:
    Node_UP  _child;
    Function _lambda;
public:
    TensorMap(Node_UP child, Function lambda)
        : _child(std::move(child)), _lambda(std::move(lambda)) {}
    const Function &lambda() const { return _lambda; }
    vespalib::string dump(DumpContext &ctx) const override {
        vespalib::string str;
        str += "map(";
        str += _child->dump(ctx);
        str += ",";
        str += _lambda.dump_as_lambda();
        str += ")";
        return str;
    }
    void accept(NodeVisitor &visitor) const override;
    size_t num_children() const override { return 1; }
    const Node &get_child(size_t idx) const override {
        (void) idx;
        assert(idx == 0);
        return *_child;
    }
    void detach_children(NodeHandler &handler) override {
        handler.handle(std::move(_child));
    }
};

class TensorJoin : public Node {
private:
    Node_UP  _lhs;
    Node_UP  _rhs;
    Function _lambda;
public:
    TensorJoin(Node_UP lhs, Node_UP rhs, Function lambda)
        : _lhs(std::move(lhs)), _rhs(std::move(rhs)), _lambda(std::move(lambda)) {}
    const Function &lambda() const { return _lambda; }
    vespalib::string dump(DumpContext &ctx) const override {
        vespalib::string str;
        str += "join(";
        str += _lhs->dump(ctx);
        str += ",";
        str += _rhs->dump(ctx);
        str += ",";
        str += _lambda.dump_as_lambda();
        str += ")";
        return str;
    }
    void accept(NodeVisitor &visitor) const override ;
    size_t num_children() const override { return 2; }
    const Node &get_child(size_t idx) const override {
        assert(idx < 2);
        return (idx == 0) ? *_lhs : *_rhs;
    }
    void detach_children(NodeHandler &handler) override {
        handler.handle(std::move(_lhs));
        handler.handle(std::move(_rhs));
    }
};

enum class Aggr { AVG, COUNT, PROD, SUM, MAX, MIN };
class AggrNames {
private:
    static const AggrNames _instance;
    std::map<vespalib::string,Aggr> _name_aggr_map;
    std::map<Aggr,vespalib::string> _aggr_name_map;
    void add(Aggr aggr, const vespalib::string &name);
    AggrNames();
public:
    static const vespalib::string *name_of(Aggr aggr);
    static const Aggr *from_name(const vespalib::string &name);
};

class TensorReduce : public Node {
private:
    Node_UP _child;
    Aggr _aggr;
    std::vector<vespalib::string> _dimensions;
public:
    TensorReduce(Node_UP child, Aggr aggr_in, std::vector<vespalib::string> dimensions_in)
        : _child(std::move(child)), _aggr(aggr_in), _dimensions(std::move(dimensions_in)) {}
    const std::vector<vespalib::string> &dimensions() const { return _dimensions; }
    Aggr aggr() const { return _aggr; }
    vespalib::string dump(DumpContext &ctx) const override {
        vespalib::string str;
        str += "reduce(";
        str += _child->dump(ctx);
        str += ",";
        str += *AggrNames::name_of(_aggr);
        for (const auto &dimension: _dimensions) {
            str += ",";
            str += dimension;
        }
        str += ")";
        return str;
    }
    void accept(NodeVisitor &visitor) const override;
    size_t num_children() const override { return 1; }
    const Node &get_child(size_t idx) const override {
        assert(idx == 0);
        return *_child;
    }
    void detach_children(NodeHandler &handler) override {
        handler.handle(std::move(_child));
    }
};

class TensorRename : public Node {
private:
    Node_UP _child;
    std::vector<vespalib::string> _from;
    std::vector<vespalib::string> _to;
    static vespalib::string flatten(const std::vector<vespalib::string> &list) {
        if (list.size() == 1) {
            return list[0];
        }
        vespalib::string str = "(";
        for (size_t i = 0; i < list.size(); ++i) {
            if (i > 0) {
                str += ",";
            }
            str += list[i];
        }
        str += ")";
        return str;
    }
public:
    TensorRename(Node_UP child, std::vector<vespalib::string> from_in, std::vector<vespalib::string> to_in)
        : _child(std::move(child)), _from(std::move(from_in)), _to(std::move(to_in)) {}
    const std::vector<vespalib::string> &from() const { return _from; }
    const std::vector<vespalib::string> &to() const { return _to; }
    vespalib::string dump(DumpContext &ctx) const override {
        vespalib::string str;
        str += "rename(";
        str += _child->dump(ctx);
        str += ",";
        str += flatten(_from);
        str += ",";
        str += flatten(_to);
        str += ")";
        return str;
    }
    void accept(NodeVisitor &visitor) const override;
    size_t num_children() const override { return 1; }
    const Node &get_child(size_t idx) const override {
        assert(idx == 0);
        return *_child;
    }
    void detach_children(NodeHandler &handler) override {
        handler.handle(std::move(_child));
    }
};

} // namespace vespalib::eval::nodes
} // namespace vespalib::eval
} // namespace vespalib
