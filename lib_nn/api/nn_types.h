// Copyright 2020 XMOS LIMITED. This Software is subject to the terms of the 
// XMOS Public License: Version 1
#ifndef NN_TYPES_H_
#define NN_TYPES_H_

#include "nn_api.h"

#include <stdint.h>

/**
 * Padding modes
 */
C_API 
typedef enum {
    PADDING_VALID = 0,
    PADDING_SAME = 1,
} padding_mode_t;

/**
 * Where functions in the API take an array of values, where the array index corresponds to a padding direction, the indices
 * shall be as indicated in `nn_pad_index_t`, with the order being top padding, left padding, bottom padding, right padding.
 * Rather than hard-coding those values, this enum can be used instead.
 */
C_API 
enum {
    NN_PAD_TOP = 0,
    NN_PAD_LEFT,
    NN_PAD_BOTTOM,
    NN_PAD_RIGHT,
};

/**
 * Alias for `uint16_t`.
 * 
 * `data16_t` is used to hint that a struct field or function parameter is opaque 16-bit data.
 */
C_API 
typedef uint16_t data16_t;

/**
 * Alias for `int8_t`. 
 * 
 * `nn_tensor_t*` is used to hint that a struct field or function parameter is to
 * be interpreted as a tensor.
 */
C_API 
typedef int8_t nn_tensor_t;

/** 
 * Alias for `int8_t`. 
 * 
 * `nn_image_t*` is used to hint that a struct field or function parameter will be
 * interpreted as an image-like tensor.
 */
C_API 
typedef nn_tensor_t nn_image_t;

/**
 * Alias for `int32_t`. 
 * 
 * `mem_stride_t` is used to hint that a struct field or function parameter indicates the
 * signed offset between memory addresses, expressed in bytes.
 */
C_API 
typedef int32_t mem_stride_t;

/**
 * Alias for `unsigned`. 
 * 
 * `channel_count_t` is used to hint that a struct field or function parameter indicates
 * a number of channels. 
 */
C_API 
typedef uint32_t channel_count_t;

#endif //NN_TYPES_H_