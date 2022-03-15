#pragma once

#include <ATen/core/interned_strings.h>

#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/hash/hash.h"
#include "absl/types/span.h"
#include "tensorflow/compiler/xla/client/xla_builder.h"
#include "tensorflow/compiler/xla/xla_client/types.h"
#include "tensorflow/core/lib/gtl/inlined_vector.h"
#include "torch/csrc/lazy/core/hash.h"
#include "torch/csrc/lazy/core/ir.h"
#include "torch_xla/csrc/python_util.h"

namespace torch_xla {
namespace ir {

class Node;
class LoweringContext;

using NodePtr = std::shared_ptr<Node>;

using XlaOpVector = tensorflow::gtl::InlinedVector<xla::XlaOp, 1>;

// The base class for user defined metadata which is possible to attach to IR
// nodes.
struct UserMetaData {
  virtual ~UserMetaData() {}
};

struct MetaData {
  std::string scope;
  std::vector<SourceLocation> frame_info;
};

// Represents a use of the output of a given node.
// If use U is within node N, it means that node U.node is using the output
// U.index of the node N.
struct Use {
  Use() = default;
  Use(Node* node, size_t operand_index, size_t index)
      : node(node), operand_index(operand_index), index(index) {}

  bool operator<(const Use& rhs) const;

  std::string ToString() const;

  // The node using the output of the node this use belongs to.
  Node* node = nullptr;
  // The operand index, within node's operands, which this use refers to.
  size_t operand_index = 0;
  // The index within output the user node refers to.
  size_t index = 0;
};

inline std::ostream& operator<<(std::ostream& stream, const Use& use) {
  stream << use.ToString();
  return stream;
}

template <typename T>
using OutputMap =
    std::unordered_map<torch::lazy::Output, T, torch::lazy::Output::Hasher>;

// Represents an input/operand for a Node object.
struct Value {
  Value() = default;
  Value(NodePtr node, size_t index = 0) : node(std::move(node)), index(index) {}

  // Retrieves the shape of this value. If the IR Node generating the value is a
  // multi-output node, the shape returned by this API will not be the full
  // tuple shape, but only the shape at index referred by this value.
  // To retrieve the full tuple shape in that case, use the node_shape() API.
  const xla::Shape& shape() const;
  const xla::Shape& node_shape() const;

  torch::lazy::hash_t hash() const;
  torch::lazy::hash_t hash_with_sizes() const;
  torch::lazy::hash_t hash_without_sizes() const;

  operator bool() const { return node != nullptr; }

  Node* operator->() const { return node.get(); }

  NodePtr node;
  size_t index = 0;
};

using OpList = absl::Span<const Value>;

// A node in the graph. Nodes for operations which requires extra data to be
// stored for lowering, should inherit from this class and add operation
// specific member there. For example, a constant might create a new
// NodeConstant class (inheriting from Node) with an extra xla::Literal field,
// or a tensor value might create a new NodeTensor with computation client data
// handle in it.
class Node : public torch::lazy::Node {
 public:
  // Creates a new node with the given op name. The op is a unique identifier
  // for the operation. The num_outputs tells how many outputs a given operation
  // generates.
  Node(torch::lazy::OpKind op, OpList operands, xla::Shape shape,
       size_t num_outputs = 1,
       torch::lazy::hash_t hash_seed = (uint32_t)0x5a2d296e9);

  // Same as the constructor above, but the shape is generated by a function,
  // only if needed (shape cache miss).
  Node(torch::lazy::OpKind op, OpList operands,
       const std::function<xla::Shape()>& shape_fn, size_t num_outputs = 1,
       torch::lazy::hash_t hash_seed = (uint32_t)0x5a2d296e9);

  // Contructor used to create leaf nodes.
  Node(torch::lazy::OpKind op, xla::Shape shape, size_t num_outputs,
       torch::lazy::hash_t hash_seed);

  virtual ~Node();

  // Retrieves the full shape of the IR Node. Note that if this is a
  // multi-output node, the returned shape will be a tuple.
  const xla::Shape& shape() const { return shape_; }

  // Retrieves the shape of the output at a given index. If the node is not a
  // multi-output node, output_index must be zero.
  const xla::Shape& shape(size_t output_index) const;

  const std::vector<torch::lazy::Output>& operands() const {
    return operands_as_outputs_;
  }

  const torch::lazy::Output& operand(size_t i) const {
    return operands_as_outputs_.at(i);
  }

  const std::set<Use>& uses() const { return uses_; }

  const MetaData& metadata() const { return metadata_; }

  UserMetaData* user_metadata() const { return user_metadata_.get(); }

  std::shared_ptr<UserMetaData> SetUserMetadata(
      std::shared_ptr<UserMetaData> user_meta) {
    std::swap(user_metadata_, user_meta);
    return user_meta;
  }

  void ReplaceOperand(size_t operand_no, NodePtr node, size_t index = 0);

  void ReplaceAllUsesWith(NodePtr node, size_t index = 0);

  virtual std::string ToString() const;

  virtual NodePtr Clone(OpList operands) const;

  virtual XlaOpVector Lower(LoweringContext* loctx) const;

  XlaOpVector ReturnOp(xla::XlaOp op, LoweringContext* loctx) const;

  XlaOpVector ReturnOps(absl::Span<const xla::XlaOp> ops,
                        LoweringContext* loctx) const;

 private:
  // Adds node's index output number as operand.
  void AddOperand(NodePtr node, size_t index = 0);

  void AddUse(Use use) { uses_.insert(std::move(use)); }

  void RemoveUse(const Use& use) { uses_.erase(use); }

  xla::Shape GetOpShape(const std::function<xla::Shape()>& shape_fn) const;

  static torch::lazy::hash_t GetOpHash(torch::lazy::OpKind op,
                                       const xla::Shape& shape,
                                       torch::lazy::hash_t hash_seed);

  static std::vector<SourceLocation> GetFrameInfo();

  xla::Shape shape_;
  // A node holds a real reference to its operands.
  std::vector<NodePtr> operands_;
  // Outputs do not hold references on the nodes, and neither do the uses, since
  // otherwise we get into circular reference counting.
  std::vector<torch::lazy::Output> operands_as_outputs_;
  // We use a set for uses, as we want deterministic use sequencing.
  std::set<Use> uses_;
  // The IR specific metadata attached to the IR node.
  MetaData metadata_;
  // The IR framework user can attach a user defined metadata object deriving
  // from UserMetaData.
  std::shared_ptr<UserMetaData> user_metadata_;
};

// RAII data structure to be used a stack variable to enter a new IR scope. IR
// scope names will appear in the IR and will help identifying the source of the
// single IR nodes.
struct ScopePusher {
  explicit ScopePusher(const std::string& name);
  ~ScopePusher();

  static void ResetScopes();
};

inline std::ostream& operator<<(std::ostream& stream, const Node& node) {
  stream << node.ToString();
  return stream;
}

template <typename T, typename... Args>
NodePtr MakeNode(Args&&... args) {
  return std::make_shared<T>(std::forward<Args>(args)...);
}

template <typename T>
T* NodeCast(const torch::lazy::Node* node, torch::lazy::OpKind op) {
  if (op != node->op()) {
    return nullptr;
  }
  const T* casted;
#ifdef NDEBUG
  casted = static_cast<const T*>(node);
#else
  casted = &dynamic_cast<const T&>(*node);
#endif
  return const_cast<T*>(casted);
}

}  // namespace ir
}  // namespace torch_xla
