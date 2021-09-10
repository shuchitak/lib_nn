#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <tuple>
#include <vector>

#include "AggregateFn.hpp"
#include "vpu_sim.h"

using namespace nn;

/******************************
 * AvgPoolPatchFn
 *****************************/

///////////////////
AvgPoolPatchFn::Params::Params(const avgpool_patch_params &ap_params)
    : ap_params(ap_params) {}

///////////////////
AvgPoolPatchFn::Params::Params(const nn::WindowGeometry &filter,
                               const int8_t shift) {
  this->ap_params.pixels = filter.shape.PixelCount();

  std::memset(&this->ap_params.scale[0], shift, sizeof(this->ap_params.scale));
}

///////////////////
AvgPoolPatchFn::Params::Params(std::istream &stream) {
  stream.read(reinterpret_cast<char *>(&this->ap_params.pixels),
              sizeof(this->ap_params.pixels));

  int32_t scale;
  stream.read(reinterpret_cast<char *>(&scale), sizeof(scale));

  std::memset(&this->ap_params.scale[0], scale, sizeof(this->ap_params.scale));
}

///////////////////
void AvgPoolPatchFn::Params::Serialize(std::ostream &stream) const {
  stream.write(reinterpret_cast<const char *>(&this->ap_params.pixels),
               sizeof(this->ap_params.pixels));
  stream.write(reinterpret_cast<const char *>(&this->ap_params.scale[0]),
               sizeof(this->ap_params.scale[0]));
}

///////////////////
AvgPoolPatchFn::AvgPoolPatchFn(const Params *params) : params(params) {}

///////////////////
C_API
void avgpool_patch_ref(VPURingBuffer *A, const int8_t patch[],
                       const avgpool_patch_params *params) {
  nn::VPU vpu;

  vpu.vsetc(MODE_S8);
  vpu.vclrdr();
  vpu.vldc(params->scale);

  for (int p = 0; p < params->pixels; ++p) {
    vpu.vlmacc(patch);
    patch = &patch[VPU_INT8_ACC_PERIOD];
  }

  vpu.vstd(A->vD);
  vpu.vstr(A->vR);
}

///////////////////
void AvgPoolPatchFn::aggregate_fn(VPURingBuffer *acc, int8_t *input_patch,
                                  int32_t output_channel_group) {
#if defined(NN_USE_REF) || !defined(__XS3A__)
  avgpool_patch_ref(acc, input_patch, &this->params->ap_params);
#else
  avgpool_patch_xcore(acc, input_patch, &this->params->ap_params);
#endif
}

/******************************
 * AvgPoolDirectValidFn
 *****************************/

///////////////////
AvgPoolDirectValidFn::Params::Params(
    const avgpool_direct_valid_params &ap_params)
    : ap_params(ap_params) {}

///////////////////
AvgPoolDirectValidFn::Params::Params(const nn::Filter2dGeometry &filter,
                                     const int8_t scale) {
  assert(filter.input.depth == filter.output.depth);

  this->ap_params.col_stride =
      filter.input.PixelBytes() * filter.window.dilation.col;
  this->ap_params.cols = filter.window.shape.width;
  this->ap_params.row_stride = filter.input.GetStride(
      filter.window.dilation.row,
      -filter.window.shape.width * filter.window.dilation.col, 0);
  this->ap_params.rows = filter.window.shape.height;

  std::memset(&this->ap_params.scale[0], scale, sizeof(this->ap_params.scale));
}

///////////////////
AvgPoolDirectValidFn::Params::Params(std::istream &stream) {
  stream.read(reinterpret_cast<char *>(&this->ap_params.col_stride),
              sizeof(this->ap_params.col_stride));
  stream.read(reinterpret_cast<char *>(&this->ap_params.cols),
              sizeof(this->ap_params.cols));
  stream.read(reinterpret_cast<char *>(&this->ap_params.row_stride),
              sizeof(this->ap_params.row_stride));
  stream.read(reinterpret_cast<char *>(&this->ap_params.rows),
              sizeof(this->ap_params.rows));

  stream.read(reinterpret_cast<char *>(&this->ap_params.scale[0]),
              sizeof(this->ap_params.scale[0]));
  for (int i = 1; i < VPU_INT8_ACC_PERIOD; ++i)
    this->ap_params.scale[i] = this->ap_params.scale[0];
}

///////////////////
void AvgPoolDirectValidFn::Params::Serialize(std::ostream &stream) const {
  stream.write(reinterpret_cast<const char *>(&this->ap_params.col_stride),
               sizeof(this->ap_params.col_stride));
  stream.write(reinterpret_cast<const char *>(&this->ap_params.cols),
               sizeof(this->ap_params.cols));
  stream.write(reinterpret_cast<const char *>(&this->ap_params.row_stride),
               sizeof(this->ap_params.row_stride));
  stream.write(reinterpret_cast<const char *>(&this->ap_params.rows),
               sizeof(this->ap_params.rows));

  stream.write(reinterpret_cast<const char *>(&this->ap_params.scale[0]),
               sizeof(this->ap_params.scale[0]));
}

///////////////////
AvgPoolDirectValidFn::AvgPoolDirectValidFn(const Params *params)
    : params(params) {}

///////////////////
C_API
void avgpool_direct_valid_ref(VPURingBuffer *acc, const int8_t X[],
                              const avgpool_direct_valid_params *params) {
  nn::VPU vpu;

  vpu.vsetc(MODE_S8);
  vpu.vclrdr();
  vpu.vldc(params->scale);

  for (auto row = params->rows; row; --row) {
    for (auto col = params->cols; col; --col) {
      vpu.vlmacc(X);
      X = &X[params->col_stride];
    }
    X = &X[params->row_stride];
  }

  vpu.vstd(acc->vD);
  vpu.vstr(acc->vR);
}

///////////////////
void AvgPoolDirectValidFn::aggregate_fn(VPURingBuffer *acc, int8_t *input_img,
                                        int32_t output_channel_group) {
#if defined(NN_USE_REF) || !defined(__XS3A__)
  avgpool_direct_valid_ref(acc, input_img, &this->params->ap_params);
#else
  avgpool_direct_valid_xcore(acc, input_img, &this->params->ap_params);
#endif  //
}