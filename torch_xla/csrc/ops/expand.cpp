#include "torch_xla/csrc/ops/expand.h"

#include "absl/strings/str_join.h"
#include "tensorflow/compiler/xla/xla_client/util.h"
#include "torch_xla/csrc/data_ops.h"
#include "torch_xla/csrc/lowering_context.h"
#include "torch_xla/csrc/tensor_util.h"
#include "torch_xla/csrc/ops/infer_output_shape.h"

namespace torch_xla {
namespace ir {
namespace ops {
namespace {

xla::Shape NodeOutputShape(const Value& input,
                           const std::vector<xla::int64>& size) {
  auto lower_for_shape_fn =
      [&](absl::Span<const xla::XlaOp> operands) -> xla::XlaOp {

    for (int i = 0; i < size.size(); i++) {
        std::cout << size[i] << std::endl;
    }
    std::cout << "milad in expand::func " << input.shape() << std::endl;
    std::cout << xla::ShapeUtil::ElementsIn(input.shape()) << " " << input.shape().rank() << " " << GetShapeDimensionType(/*device=*/nullptr) << std::endl;
    xla::Shape tensor_shape = input.shape();
    std::cout << "ranks: " << tensor_shape.rank() << std::endl;
    xla::XlaOp op;
    for (int i = 0; i < tensor_shape.rank(); ++i) {
      if (tensor_shape.is_dynamic_dimension(i)) {
        std::cout << "Dynamic Dimension: " << i << std::endl;
        auto _size = xla::GetDimensionSize(operands[0], i);
        op = xla::SetDimensionSize(operands[0], _size, i);
      } else {
        std::cout << "Static Dimension: " << i << std::endl;
        op = operands[0];
      }
    }
    if (tensor_shape.rank() == 0) {
      op = operands[0];
    }

    return BuildExpand(op, size);
  };
  std::cout << "NodeOutputShape " << input.shape() << std::endl;
  return InferOutputShape({input.shape()}, lower_for_shape_fn);
}

}  // namespace

Expand::Expand(const Value& input, std::vector<xla::int64> size)
    : Node(ir::OpKind(at::aten::expand), {input},
           [&]() { return NodeOutputShape(input, size); },
           /*num_outputs=*/1, xla::util::MHash(size)),
      size_(std::move(size)) {}

NodePtr Expand::Clone(OpList operands) const {
  return MakeNode<Expand>(operands.at(0), size_);
}

XlaOpVector Expand::Lower(LoweringContext* loctx) const {
  xla::XlaOp input = loctx->GetOutputOp(operand(0));

  std::cout << "milad in expand::lower " << shape()  << std::endl;
  xla::Shape tensor_shape = shape();
  std::cout << "ranks: " << tensor_shape.rank() << std::endl;
  for (int i = 0; i < tensor_shape.rank(); ++i) {
    if (tensor_shape.is_dynamic_dimension(i)) {
      std::cout << "Dynamic Dimension: " << i << std::endl;
      auto size = xla::GetDimensionSize(input, i);
      input = xla::SetDimensionSize(input, size, i);
    } else {
      std::cout << "Static Dimension: " << i << std::endl;
    }
  }

  return ReturnOp(BuildExpand(input, size_), loctx);
}

std::string Expand::ToString() const {
  std::stringstream ss;
  ss << Node::ToString() << ", size=(" << absl::StrJoin(size_, ", ") << ")";
  return ss.str();
}

}  // namespace ops
}  // namespace ir
}  // namespace torch_xla
