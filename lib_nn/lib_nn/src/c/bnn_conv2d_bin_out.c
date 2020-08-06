
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "nn_operator.h"
#include "../nn_op_helper.h"
#include "nn_op_structs.h"

#include "xs3_vpu.h"

void bnn_reorder_threshold_tensor(const int32_t* thresh_boggled,
                                  const int32_t* thresholds_ref,
                                  const unsigned chans_out,
                                  const unsigned receptive_field) {
  int16_t* thresholds = (int16_t*)thresh_boggled;

  for (unsigned i = 0; i < chans_out; i++) {
    unsigned bank = i / 16;

    int32_t t = thresholds_ref[i] - ((receptive_field) / 2);
    thresholds[(bank * 32) + (i % 16)] = (t >> 0);
    thresholds[(bank * 32) + (i % 16) + 16] = (t >> 16);
  }
}

void bnn_reorder_kernel_tensor(const bnn_b256_t* K_p, const bnn_b256_t* K_ref_p,
                               const unsigned k_height, const unsigned k_width,
                               const unsigned chans_in,
                               const unsigned chans_out) {
  unsigned chan_b256_in =
      (chans_in + XS3_VPU_VREG_WIDTH_BITS - 1) / XS3_VPU_VREG_WIDTH_BITS;

  bnn_b256_t(*K_ref)[k_height][k_width][chan_b256_in] =
      (bnn_b256_t(*)[k_height][k_width][chan_b256_in])K_ref_p;

  bnn_b256_t(*K)[k_height][k_width][chan_b256_in][16] =
      (bnn_b256_t(*)[k_height][k_width][chan_b256_in][16])K_p;

  for (unsigned oc = 0; oc < chans_out / 16; oc++) {
    for (unsigned h = 0; h < k_height; h++) {
      for (unsigned w = 0; w < k_width; w++) {
        for (unsigned ic = 0; ic < chan_b256_in; ic++) {
          for (unsigned o = 0; o < 16; o++) {
            for (unsigned i = 0; i < 8; i++) {
              K[oc][h][w][ic][15 - o].d[i] = K_ref[oc * 16 + o][h][w][ic].d[i];
            }
          }
        }
      }
    }
  }
}

void bnn_conv2d_bin_out_asm_prepare(
    nn_bnn_conv2d_bin_out_asm_plan_t* plan, const bnn_b32_t* Y_p,
    const bnn_b256_t* X_p, const bnn_b256_t* K_p, const int32_t* thresholds_p,
    const nn_image_params_t* x, const nn_image_params_t* y,
    const nn_window_params_t* k, const unsigned y_loc_x, const unsigned y_loc_y,
    const unsigned x_loc_x, const unsigned x_loc_y, const unsigned k_loc_x,
    const unsigned k_loc_y, const unsigned y_full_width,
    const unsigned x_full_width, const unsigned k_full_width) {
  plan->Y = (bnn_b32_t*)Y_p;
  plan->X = (bnn_b256_t*)X_p;
  plan->K = (bnn_b256_t*)K_p;
  plan->threshold_p = (int32_t *)thresholds_p;

  unsigned bytes_per_input_channel = x->channels / 8;
  unsigned bytes_per_output_channel = y->channels / 8;

  // This is 32 to make it easier and be more compatable with larq
  const unsigned out_chans_multiplier = 32;

  assert((x->channels % XS3_VPU_VREG_WIDTH_BITS) == 0);
  assert((y->channels % out_chans_multiplier) == 0);

  unsigned k_width = k->shape.width;

  plan->k_height_loop_counter = k->shape.height - 1;
  plan->k_width_loop_counter = k_width - 1;

  plan->input_channel_loop_counter =
      (x->channels / XS3_VPU_VREG_WIDTH_BITS) - 1;
  plan->output_channel_loop_counter = (y->channels / out_chans_multiplier) - 1;
  plan->x_height_loop_counter = y->height;
  plan->x_width_loop_counter = y->width - 1;

  unsigned h_dilation = 1;
  unsigned v_dilation = 1;  // unused

  // Inner Loop
  // minus one to account for the auto increament in the loop
  plan->inner_x_h_step = bytes_per_input_channel * (h_dilation - 1);

  // TODO multiply x->width by dilation
  plan->inner_x_v_step =
      (bytes_per_input_channel * ((x->width - k_width))) - plan->inner_x_h_step;

  // Outer Loop
  plan->outer_x_h_step = bytes_per_input_channel * (k->stride.horizontal);

  unsigned remainder_to_end_of_line = x->width % k->stride.horizontal;

  // TODO this shouldn't be a loop!
  while (k_width > remainder_to_end_of_line) {
    remainder_to_end_of_line += k->stride.horizontal;
  }

  plan->outer_x_v_step =
      bytes_per_input_channel *
          (remainder_to_end_of_line + (x->width * (k->stride.vertical - 1))) -
      plan->outer_x_h_step;

  // TODO these are for implementing sub-kernels
  plan->k_v_step = 0;
  plan->k_h_step = 0;

  // TODO this will be for when writing to a sub-rectangle of an image
  plan->y_v_step = 0;
}

unsigned xor_pop(bnn_b256_t* a, bnn_b256_t* b) {
  unsigned t = sizeof(((bnn_b256_t*)0)->d[0]);
  unsigned elements = sizeof(((bnn_b256_t*)0)->d) / t;

  unsigned c = 0;
  for (unsigned e = 0; e < elements; e++) {
    uint32_t v = a->d[e] ^ b->d[e];
    v = ~v;
    for (unsigned i = 0; i < t * 8; i++) {
      c += (v & 1);
      v >>= 1;
    }
  }
  return c;
}

WEAK_FUNC
void bnn_conv2d_bin_out(bnn_b32_t* Y_p, const bnn_b256_t* X_p,
                        const bnn_b256_t* K_p,
                        int32_t* thresholds,  //[out_channel];
                        const nn_bnn_conv2d_bin_out_plan_t* plan) {
  const unsigned kernel_height = plan->k_dims[0];
  const unsigned kernel_width = plan->k_dims[1];
  const unsigned chan_b256_in = plan->x_dims[2];
  const unsigned chan_b32_out = plan->y_dims[2];
  const unsigned x_height = plan->x_dims[0];
  const unsigned x_width = plan->x_dims[1];
  const unsigned y_height = plan->y_dims[0];
  const unsigned y_width = plan->y_dims[1];
  const unsigned h_stride = plan->stride[0];
  const unsigned v_stride = plan->stride[1];

  bnn_b32_t(*Y)[y_width][chan_b32_out] =
      (bnn_b32_t(*)[y_width][chan_b32_out])Y_p;

  bnn_b256_t(*X)[x_width][chan_b256_in] =
      (bnn_b256_t(*)[x_width][chan_b256_in])X_p;

  bnn_b256_t(*K)[kernel_height][kernel_width][chan_b256_in] =
      (bnn_b256_t(*)[kernel_height][kernel_width][chan_b256_in])K_p;

  for (unsigned h = 0; h < x_height - kernel_height + 1; h += v_stride) {
    for (unsigned w = 0; w < x_width - kernel_width + 1; w += h_stride) {
      for (unsigned oc_word = 0; oc_word < chan_b32_out; oc_word += 1) {
        bnn_b32_t bitpacked_column = 0;

        for (unsigned oc_bit = 0; oc_bit < 32; oc_bit += 1) {
          unsigned oc = oc_bit + (32 * oc_word);
          int32_t sum = 0;
          for (unsigned kh = 0; kh < kernel_height; kh += 1) {
            for (unsigned kw = 0; kw < kernel_width; kw += 1) {
              for (unsigned ic = 0; ic < chan_b256_in; ic += 1) {
                sum += xor_pop(&(X[h + kh][w + kw][ic]), &(K[oc][kh][kw][ic]));
              }
            }
          }

          sum = (kernel_height * kernel_width * chan_b256_in * 256) - sum;
          unsigned bit = sum > thresholds[oc];
          if (bit) bitpacked_column |= 1ULL << oc_bit;
        }
        Y[h / v_stride][w / h_stride][oc_word] = bitpacked_column;
      }
    }
  }
}

void bnn_conv2d_bin_out_init(nn_bnn_conv2d_bin_out_plan_t* plan,
                             const nn_image_params_t* x,
                             const nn_image_params_t* y,
                             const nn_window_params_t* k) {
  plan->k_dims[0] = k->shape.height;
  plan->k_dims[1] = k->shape.width;

  plan->y_dims[0] = y->height;
  plan->y_dims[1] = y->width;
  plan->y_dims[2] = (y->channels + 32 - 1) / 32;

  plan->x_dims[0] = x->height;
  plan->x_dims[1] = x->width;
  plan->x_dims[2] =
      (x->channels + XS3_VPU_VREG_WIDTH_BITS - 1) / XS3_VPU_VREG_WIDTH_BITS;
  plan->stride[0] = k->stride.horizontal;
  plan->stride[1] = k->stride.vertical;

  plan->start_loc[0] = k->start.column;
  plan->start_loc[1] = k->start.row;
}