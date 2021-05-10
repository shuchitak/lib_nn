#pragma once

#include "nn_types.h"
#include "util.hpp"

#include <iostream>
#include <cassert>
#include <functional>

namespace nn
{

  class ImageGeometry
  {

  public:
    int height;
    int width;
    int depth;
    int channel_depth; //in bytes //asj:this might be better in bits

    constexpr ImageGeometry() : height(0), width(0), depth(0), channel_depth(1) {}

    constexpr ImageGeometry(
        int const rows,
        int const cols,
        int const chans,
        int const channel_depth_bytes = 1) noexcept
        : height(rows),
          width(cols),
          depth(chans),
          channel_depth(channel_depth_bytes) {}

    /**
     * The total number of pixels in the image
     */
    int inline const imagePixels() const { return this->height * this->width; }

    /**
     * The number of image elements per pixel
     */
    int inline const pixelElements() const { return this->depth; }

    /**
     * The number of image elements per row of the image
     */
    int inline const rowElements() const { return this->width * this->pixelElements(); }
    
    /**
     * The number of image elements per column of the image
     */
    int inline const colElements() const { return this->height * this->pixelElements(); }

    /**
     * The total number of image elements
     */
    int inline const imageElements() const { return this->height * this->rowElements(); }

    /**
     * The number of image pixels multiplied by the square of image depth
     */
    int inline const volumeElements() const { return this->depth * this->imageElements(); }

    /**
     * The number of bytes per image pixel
     */
    int inline const pixelBytes() const { return pixelElements() * channel_depth; }

    /**
     * The number of bytes per row of the image
     */
    int inline const rowBytes() const { return rowElements() * channel_depth; }

    /**
     * The number of bytes per column of the image
     */
    int inline const colBytes() const { return colElements() * channel_depth; }

    /**
     * The total number of bytes of the image
     */
    int inline const imageBytes() const { return imageElements() * channel_depth; }

    /**
       * Get the flattened index of the specified image element.
       * 
       * The "flattened" index of an element is the index of the element when the image is stored in a 1 dimensional
       * array. This is ideal, for example, when the image image is backed by a `std::vector` object.
       * 
       * This function returns -1 if the specified coordinates refer to an element in padding 
       * (i.e. beyond the bounds of the image).
       */
    int Index(const int row,
              const int col,
              const int channel) const;

    /**
     * Get the buffer index associated with the specified image coordinates, assuming the image is backed
     * by a linear buffer.
     */
    int Index(const ImageVect &input_coords) const;

    /**
     * Get the memory stride (in bytes) required to move by the specified amount within this geometry.
     */
    mem_stride_t getStride(const int rows,
                           const int cols,
                           const int chans) const;

    /**
     * Get the memory stride (in bytes) required to move by the specified amount within this geometry.
     */
    mem_stride_t getStride(const ImageVect &vect) const;

    /**
     * Get the memory stride (in bytes) required to move between two locations within this geometry.
     */
    mem_stride_t getStride(const ImageVect &from,
                           const ImageVect &to) const;

    /**
     * Check whether the specified coordinates refer to an element within this image geometry.
     */
    bool IsWithinImage(const ImageVect &coords) const;

    /**
     * Check whether the specified coordinates refer to an element within this image geometry.
     */
    bool IsWithinImage(const int row, const int col, const int channel) const;

    /**
     * Get a reference to the specified element from the provided image.
     * An assertion is made that the specified location is within the geometry's bounds.
     */
    template <typename T>
    T &Element(T *img_base,
               const int row,
               const int col,
               const int channel) const;

    /**
     * Get the value of the specified element from the provided image.
     * If the coordinates refer to a location outside the image geometry pad_value is returned.
     */
    template <typename T>
    T Get(const T *img_base,
          const ImageVect coords,
          const T pad_value = 0) const;

    /**
     * Get the value of the specified element from the provided image.
     * If the coordinates refer to a location outside the image geometry pad_value is returned.
     */
    template <typename T>
    T Get(const T *img_base,
          const int row,
          const int col,
          const int channel,
          const T pad_value = 0) const;

    /**
     * Apply an operation per element of an image.
     * 
     * void PerPixelOp(const int row, 
     *                 const int col,
     *                 const int channel,
     *                 T& value);
     */
    template <typename T>
    void ApplyOperation(T *image_base,
                        std::function<void(int, int, int, T &)> func) const;

    /**
     * Determine whether this ImageGeometry is equal to another.
     */
    bool operator==(ImageGeometry other) const;
  };




  template <typename T>
  T &ImageGeometry::Element(T *img_base,
                            const int row,
                            const int col,
                            const int channel) const
  {
    assert(IsWithinImage(row, col, channel));
    int index = row * this->width * this->depth + col * this->depth + channel;
    return img_base[index];
  }

  template <typename T>
  T ImageGeometry::Get(const T *img_base,
                       const ImageVect coords,
                       const T pad_value) const
  {
    return Get<T>(img_base, coords.row, coords.col, coords.channel, pad_value);
  }

  template <typename T>
  T ImageGeometry::Get(const T *img_base,
                       const int row,
                       const int col,
                       const int channel,
                       const T pad_value) const
  {
    if (!IsWithinImage(row, col, channel))
      return pad_value;
    return Element<T>(const_cast<T *>(img_base), row, col, channel);
  }

  template <typename T>
  void ImageGeometry::ApplyOperation(T *image_base,
                                     std::function<void(int, int, int, T &)> op) const
  {
    for (int row = 0; row < this->height; ++row)
      for (int col = 0; col < this->width; ++col)
        for (int channel = 0; channel < this->depth; ++channel)
          op(row, col, channel, this->Element<T>(image_base, row, col, channel));
  }

  inline std::ostream &operator<<(std::ostream &stream, const ImageGeometry &image)
  {
    return stream << image.height << "," << image.width << ","
                  << image.depth << "," << image.channel_depth;
  }

}