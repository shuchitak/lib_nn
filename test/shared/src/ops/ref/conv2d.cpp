
#include "RefOps.hpp"

#include "tensorflow/lite/kernels/internal/reference/integer_ops/conv.h"

#include "../src/cpp/filt2d/util/conv2d_utils.hpp"

using namespace nn::filt2d;
using namespace nn::filt2d::geom;
using namespace nn::test::ops::ref;

std::vector<int8_t> nn::test::ops::ref::Conv2dDenseReference(
    const Filter2dGeometry& filter_geometry,
    const int8_t input_img[],
    const int8_t kernel_weights[],
    const int32_t biases[],
    const float effective_output_multiplier[],
    const int8_t input_zero_point,
    const int8_t output_zero_point)
{
  tflite::ConvParams op_params;

  auto pad_initial = filter_geometry.ModelPadding(true);
  auto pad_final   = filter_geometry.ModelPadding(false);

  op_params.padding_type = tflite::PaddingType::kSame;
  op_params.padding_values.height = pad_initial.top;
  op_params.padding_values.width = pad_initial.left;
  op_params.padding_values.height_offset = pad_final.bottom;
  op_params.padding_values.width_offset = pad_final.right;

  op_params.stride_height = filter_geometry.window.stride.row;
  op_params.stride_width  = filter_geometry.window.stride.col;
  op_params.dilation_height_factor = filter_geometry.window.dilation.row;
  op_params.dilation_width_factor  = filter_geometry.window.dilation.col;

  op_params.input_offset = -input_zero_point;
  op_params.output_offset = output_zero_point;
  op_params.weights_offset = 0;

  auto output_mult = std::vector<int32_t>(filter_geometry.output.depth);
  auto output_shift = std::vector<int32_t>(filter_geometry.output.depth);

  for(int i = 0; i < filter_geometry.output.depth; ++i){
    nn::filt2d::conv2d::util::TfLiteConverter::QuantizeEffectiveOutputMultiplier(
                                                  output_mult[i], output_shift[i],
                                                  effective_output_multiplier[i]);
  }

  // When per-channel quantization is used, the following values are ignored. Needs the arrays of multipliers and 
  // shifts from ref_params, instead.

  // op_params.output_multiplier = ref_params.output.multiplier;
  // op_params.output_shift = ref_params.output.shift;

  op_params.quantized_activation_min = -128; //ref_params.output.activation.min;
  op_params.quantized_activation_max = 127; //ref_params.output.activation.max;

  struct {
    tflite::RuntimeShape input, output, bias, filter;
  } shape = {
      .input = {  1, (int) filter_geometry.input.height,  (int) filter_geometry.input.width,  (int) filter_geometry.input.depth },
      .output = { 1, (int) filter_geometry.output.height, (int) filter_geometry.output.width, (int) filter_geometry.output.depth },
      .bias = { (int) filter_geometry.output.depth },
      .filter = { (int) filter_geometry.output.depth, (int) filter_geometry.window.shape.height, 
                  (int) filter_geometry.window.shape.width, (int) filter_geometry.input.depth },
  };

  auto output_data = std::vector<int8_t>( filter_geometry.output.imageElements() );

  tflite::reference_integer_ops::ConvPerChannel(op_params, 
                              &output_mult[0], &output_shift[0],
                              shape.input, input_img,
                              shape.filter, kernel_weights,
                              shape.bias, biases,
                              shape.output, &output_data[0]);

  return output_data;
}