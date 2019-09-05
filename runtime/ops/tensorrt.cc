#if CHAINER_COMPILER_ENABLE_TENSORRT

#include <NvInfer.h>
#include <NvOnnxParser.h>

#include <chainerx/routines/creation.h>

#include <runtime/chainerx_util.h>

#endif

#include <common/log.h>
#include <runtime/gen_chxvm_ops.h>

namespace chainer_compiler {
namespace runtime {

#if CHAINER_COMPILER_ENABLE_TENSORRT

namespace {

struct InferDeleter {
    template <typename T>
    void operator()(T* obj) const {
        if (obj) {
            obj->destroy();
        }
    }
};

template <typename T>
using UniquePtr = std::unique_ptr<T, InferDeleter>;

class Logger : public nvinfer1::ILogger {
public:
    ~Logger() override {
    }

    void log(Severity severity, const char* msg) override {
        std::cerr << msg << std::endl;
    }
};

chainerx::Dtype GetDtype(nvinfer1::DataType type) {
    switch (type) {
        case nvinfer1::DataType::kFLOAT:
            return chainerx::Dtype::kFloat32;
        case nvinfer1::DataType::kHALF:
            return chainerx::Dtype::kFloat16;
        case nvinfer1::DataType::kINT8:
            return chainerx::Dtype::kInt8;
        case nvinfer1::DataType::kINT32:
            return chainerx::Dtype::kInt32;
        default:
            CHECK(false) << "Not supported TensorRT dtype: " << static_cast<int>(type);
    }
}

chainerx::Shape GetShape(const int batch_size, const nvinfer1::Dims& dims) {
    chainerx::Shape shape = {batch_size};
    for (int i = 0; i < dims.nbDims; ++i) {
        shape.push_back(dims.d[i]);
    }
    return shape;
}

}  // namespace

class TensorRTOp::TensorRTImpl {
public:
    Logger logger;
    std::shared_ptr<nvinfer1::ICudaEngine> engine;
    std::vector<chainerx::Array> outputs;
};

#endif

void TensorRTOp::InitImpl() {
#if CHAINER_COMPILER_ENABLE_TENSORRT
    impl_ = new TensorRTImpl();

    auto builder = UniquePtr<nvinfer1::IBuilder>(nvinfer1::createInferBuilder(impl_->logger));
    CHECK(builder);
    auto network = UniquePtr<nvinfer1::INetworkDefinition>(builder->createNetwork());
    CHECK(network);
    auto parser = UniquePtr<nvonnxparser::IParser>(nvonnxparser::createParser(*network, impl_->logger));

    if (!parser->parse(onnx.data(), onnx.size())) {
        for (int i = 0; i < parser->getNbErrors(); ++i) {
            auto e = parser->getError(i);
            std::cerr << e->desc() << std::endl;
        }
        CHECK(false);
    }

    builder->setMaxBatchSize(batch_size);
    builder->setMaxWorkspaceSize(128 * 1000 * 1000);
    builder->setFp16Mode(use_fp16);

    impl_->engine = std::shared_ptr<nvinfer1::ICudaEngine>(builder->buildCudaEngine(*network), InferDeleter());
    CHECK(impl_->engine);

    for (int i = 0; i < network->getNbOutputs(); ++i) {
        nvinfer1::ITensor* tensor = network->getOutput(i);
        chainerx::Dtype dtype = GetDtype(tensor->getType());
        chainerx::Shape shape = GetShape(batch_size, tensor->getDimensions());
        chainerx::Array array = chainerx::Empty(shape, dtype);
        impl_->outputs.push_back(array);
    }

#endif
}

TensorRTOp::~TensorRTOp() {
#if CHAINER_COMPILER_ENABLE_TENSORRT
    delete impl_;
#endif
}

std::vector<chainerx::Array> TensorRTOp::RunImpl(
        chainer_compiler::runtime::ChxVMState* st, const std::vector<chainerx::Array>& orig_inputs) {
#if CHAINER_COMPILER_ENABLE_TENSORRT
    size_t num_inputs = orig_inputs.size();

    // Validate inputs.
    chainerx::Array inputs[num_inputs];
    for (size_t i = 0; i < num_inputs; ++i) {
        const chainerx::Array& input = orig_inputs[i];
        CHECK_EQ(input.shape()[0], batch_size);
        inputs[i] = chainerx::AsContiguous(input);
    }

    auto context = UniquePtr<nvinfer1::IExecutionContext>(impl_->engine->createExecutionContext());
    CHECK(context);

    std::vector<void*> bindings;
    for (const chainerx::Array& a : inputs) {
        bindings.push_back(RawStartPtr(a));
    }
    for (const chainerx::Array& a : impl_->outputs) {
        bindings.push_back(RawStartPtr(a));
    }

    const bool status = context->execute(batch_size, &bindings[0]);
    CHECK(status);

    return impl_->outputs;

#else
    CHECK(false) << "Set -DCHAINER_COMPILER_ENABLE_TENSORRT";
#endif
}

}  // namespace runtime
}  // namespace chainer_compiler
