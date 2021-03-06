/* -----------------------------------------------------------------------------
Software Copyright License for the Fraunhofer Software Library VVenc

(c) Copyright (2019-2020) Fraunhofer-Gesellschaft zur Förderung der angewandten Forschung e.V. 

1.    INTRODUCTION

The Fraunhofer Software Library VVenc (“Fraunhofer Versatile Video Encoding Library”) is software that implements (parts of) the Versatile Video Coding Standard - ITU-T H.266 | MPEG-I - Part 3 (ISO/IEC 23090-3) and related technology. 
The standard contains Fraunhofer patents as well as third-party patents. Patent licenses from third party standard patent right holders may be required for using the Fraunhofer Versatile Video Encoding Library. It is in your responsibility to obtain those if necessary. 

The Fraunhofer Versatile Video Encoding Library which mean any source code provided by Fraunhofer are made available under this software copyright license. 
It is based on the official ITU/ISO/IEC VVC Test Model (VTM) reference software whose copyright holders are indicated in the copyright notices of its source files. The VVC Test Model (VTM) reference software is licensed under the 3-Clause BSD License and therefore not subject of this software copyright license.

2.    COPYRIGHT LICENSE

Internal use of the Fraunhofer Versatile Video Encoding Library, in source and binary forms, with or without modification, is permitted without payment of copyright license fees for non-commercial purposes of evaluation, testing and academic research. 

No right or license, express or implied, is granted to any part of the Fraunhofer Versatile Video Encoding Library except and solely to the extent as expressly set forth herein. Any commercial use or exploitation of the Fraunhofer Versatile Video Encoding Library and/or any modifications thereto under this license are prohibited.

For any other use of the Fraunhofer Versatile Video Encoding Library than permitted by this software copyright license You need another license from Fraunhofer. In such case please contact Fraunhofer under the CONTACT INFORMATION below.

3.    LIMITED PATENT LICENSE

As mentioned under 1. Fraunhofer patents are implemented by the Fraunhofer Versatile Video Encoding Library. If You use the Fraunhofer Versatile Video Encoding Library in Germany, the use of those Fraunhofer patents for purposes of testing, evaluating and research and development is permitted within the statutory limitations of German patent law. However, if You use the Fraunhofer Versatile Video Encoding Library in a country where the use for research and development purposes is not permitted without a license, you must obtain an appropriate license from Fraunhofer. It is Your responsibility to check the legal requirements for any use of applicable patents.    

Fraunhofer provides no warranty of patent non-infringement with respect to the Fraunhofer Versatile Video Encoding Library.


4.    DISCLAIMER

The Fraunhofer Versatile Video Encoding Library is provided by Fraunhofer "AS IS" and WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, including but not limited to the implied warranties fitness for a particular purpose. IN NO EVENT SHALL FRAUNHOFER BE LIABLE for any direct, indirect, incidental, special, exemplary, or consequential damages, including but not limited to procurement of substitute goods or services; loss of use, data, or profits, or business interruption, however caused and on any theory of liability, whether in contract, strict liability, or tort (including negligence), arising in any way out of the use of the Fraunhofer Versatile Video Encoding Library, even if advised of the possibility of such damage.

5.    CONTACT INFORMATION

Fraunhofer Heinrich Hertz Institute
Attention: Video Coding & Analytics Department
Einsteinufer 37
10587 Berlin, Germany
www.hhi.fraunhofer.de/vvc
vvc@hhi.fraunhofer.de
----------------------------------------------------------------------------- */


/** \file     Buffer.cpp
 *  \brief    Low-overhead class describing 2D memory layout
 */

#define DONT_UNDEF_SIZE_AWARE_PER_EL_OP

// unit needs to come first due to a forward declaration
#include "Unit.h"
#include "Slice.h"
#include "InterpolationFilter.h"
#include "../../../include/vvenc/Basics.h"

//! \ingroup CommonLib
//! \{

namespace vvenc {

void weightCiipCore( Pel* res, const Pel* src, const int numSamples, int numIntra )
{
  if( numIntra == 1 )
  {
    for (int n = 0; n < numSamples; n+=2)
    {
      res[n  ] = (res[n  ] + src[n  ] + 1) >> 1;
      res[n+1] = (res[n+1] + src[n+1] + 1) >> 1;
    }
  }
  else
  {
    const Pel* scale   = numIntra ? src : res;
    const Pel* unscale = numIntra ? res : src;

    for (int n = 0; n < numSamples; n+=2)
    {
      res[n  ] = (unscale[n  ] + 3*scale[n  ] + 2) >> 2;
      res[n+1] = (unscale[n+1] + 3*scale[n+1] + 2) >> 2;
    }
  }
}

template< unsigned inputSize, unsigned outputSize >
void mipMatrixMulCore( Pel* res, const Pel* input, const uint8_t* weight, const int maxVal, const int inputOffset, bool transpose )
{
  Pel buffer[ outputSize*outputSize];

  int sum = 0;
  for( int i = 0; i < inputSize; i++ ) 
  { 
    sum += input[i]; 
  }
  const int offset = (1 << (MIP_SHIFT_MATRIX - 1)) - MIP_OFFSET_MATRIX * sum + (inputOffset << MIP_SHIFT_MATRIX);
  CHECK( inputSize != 4 * (inputSize >> 2), "Error, input size not divisible by four" );
 
  Pel* mat = transpose ? buffer : res;
  unsigned posRes = 0;
  for( unsigned n = 0; n < outputSize*outputSize; n++ )
  {
    int tmp0 = input[0] * weight[0];
    int tmp1 = input[1] * weight[1];
    int tmp2 = input[2] * weight[2];
    int tmp3 = input[3] * weight[3];
    if( 8 == inputSize )
    {
      tmp0 += input[4] * weight[4];
      tmp1 += input[5] * weight[5];
      tmp2 += input[6] * weight[6];
      tmp3 += input[7] * weight[7];
    }
    mat[posRes++] = Clip3<int>( 0, maxVal, ((tmp0 + tmp1 + tmp2 + tmp3 + offset) >> MIP_SHIFT_MATRIX) );

    weight += inputSize;
  }

  if( transpose )
  {
    for( int j = 0; j < outputSize; j++ )
    {
      for( int i = 0; i < outputSize; i++ )
      {
        res[j * outputSize + i] = buffer[i * outputSize + j];
      }
    }
  }
}

template<bool PAD = true>
void gradFilterCore(const Pel* pSrc, int srcStride, int width, int height, int gradStride, Pel* gradX, Pel* gradY, const int bitDepth)
{
  const Pel* srcTmp = pSrc + srcStride + 1;
  Pel* gradXTmp = gradX + gradStride + 1;
  Pel* gradYTmp = gradY + gradStride + 1;
  int  shift1 = std::max<int>(6, (bitDepth - 6));

  for (int y = 0; y < (height - 2 * BDOF_EXTEND_SIZE); y++)
  {
    for (int x = 0; x < (width - 2 * BDOF_EXTEND_SIZE); x++)
    {
      gradYTmp[x] = (srcTmp[x + srcStride] >> shift1) - (srcTmp[x - srcStride] >> shift1);
      gradXTmp[x] = (srcTmp[x + 1] >> shift1) - (srcTmp[x - 1] >> shift1);
    }
    gradXTmp += gradStride;
    gradYTmp += gradStride;
    srcTmp += srcStride;
  }

  if (PAD)
  {
    gradXTmp = gradX + gradStride + 1;
    gradYTmp = gradY + gradStride + 1;
    for (int y = 0; y < (height - 2 * BDOF_EXTEND_SIZE); y++)
    {
      gradXTmp[-1] = gradXTmp[0];
      gradXTmp[width - 2 * BDOF_EXTEND_SIZE] = gradXTmp[width - 2 * BDOF_EXTEND_SIZE - 1];
      gradXTmp += gradStride;

      gradYTmp[-1] = gradYTmp[0];
      gradYTmp[width - 2 * BDOF_EXTEND_SIZE] = gradYTmp[width - 2 * BDOF_EXTEND_SIZE - 1];
      gradYTmp += gradStride;
    }

    gradXTmp = gradX + gradStride;
    gradYTmp = gradY + gradStride;
    ::memcpy(gradXTmp - gradStride, gradXTmp, sizeof(Pel)*(width));
    ::memcpy(gradXTmp + (height - 2 * BDOF_EXTEND_SIZE)*gradStride, gradXTmp + (height - 2 * BDOF_EXTEND_SIZE - 1)*gradStride, sizeof(Pel)*(width));
    ::memcpy(gradYTmp - gradStride, gradYTmp, sizeof(Pel)*(width));
    ::memcpy(gradYTmp + (height - 2 * BDOF_EXTEND_SIZE)*gradStride, gradYTmp + (height - 2 * BDOF_EXTEND_SIZE - 1)*gradStride, sizeof(Pel)*(width));
  }
}


void applyPROFCore(Pel* dst, int dstStride, const Pel* src, int srcStride, int width, int height, const Pel* gradX, const Pel* gradY, int gradStride, const int* dMvX, const int* dMvY, int dMvStride, const bool& bi, int shiftNum, Pel offset, const ClpRng& clpRng)
{
  int idx = 0;
  const int dILimit = 1 << std::max<int>(clpRng.bd + 1, 13);
  for (int h = 0; h < height; h++)
  {
    for (int w = 0; w < width; w++)
    {
      int32_t dI = dMvX[idx] * gradX[w] + dMvY[idx] * gradY[w];
      dI = Clip3(-dILimit, dILimit - 1, dI);
      dst[w] = src[w] + dI;
      if (!bi)
      {
        dst[w] = (dst[w] + offset) >> shiftNum;
        dst[w] = ClipPel(dst[w], clpRng);
      }
      idx++;
    }
    gradX += gradStride;
    gradY += gradStride;
    dst += dstStride;
    src += srcStride;
  }
}

template< typename T >
void addAvgCore( const T* src1, int src1Stride, const T* src2, int src2Stride, T* dest, int dstStride, int width, int height, unsigned rshift, int offset, const ClpRng& clpRng )
{
#define ADD_AVG_CORE_OP( ADDR ) dest[ADDR] = ClipPel( rightShiftU( ( src1[ADDR] + src2[ADDR] + offset ), rshift ), clpRng )
#define ADD_AVG_CORE_INC    \
  src1 += src1Stride;       \
  src2 += src2Stride;       \
  dest +=  dstStride;       \

  SIZE_AWARE_PER_EL_OP( ADD_AVG_CORE_OP, ADD_AVG_CORE_INC );

#undef ADD_AVG_CORE_OP
#undef ADD_AVG_CORE_INC
}

void removeHighFreq(int16_t* dst, int dstStride, const int16_t* src, int srcStride, int width, int height)
{
#define REM_HF_INC  \
 src += srcStride; \
 dst += dstStride; \

#define REM_HF_OP( ADDR )      dst[ADDR] =             2 * dst[ADDR] - src[ADDR]

 SIZE_AWARE_PER_EL_OP(REM_HF_OP, REM_HF_INC);

#undef REM_HF_INC
#undef REM_HF_OP
#undef REM_HF_OP_CLIP
}

template<typename T>
void reconstructCore( const T* src1, int src1Stride, const T* src2, int src2Stride, T* dest, int dstStride, int width, int height, const ClpRng& clpRng )
{
#define RECO_CORE_OP( ADDR ) dest[ADDR] = ClipPel( src1[ADDR] + src2[ADDR], clpRng )
#define RECO_CORE_INC     \
  src1 += src1Stride;     \
  src2 += src2Stride;     \
  dest +=  dstStride;     \

  SIZE_AWARE_PER_EL_OP( RECO_CORE_OP, RECO_CORE_INC );

#undef RECO_CORE_OP
#undef RECO_CORE_INC
}

template<typename T>
void recoCore( const T* src1, const T* src2, T* dest, int numSamples, const ClpRng& clpRng )
{
  for( int n = 0; n < numSamples; n+=2) 
  {
    dest[n]   = ClipPel( src1[n]   + src2[n], clpRng );
    dest[n+1] = ClipPel( src1[n+1] + src2[n+1], clpRng );
  }
}

template<typename T>
void copyClipCore( const T* src, Pel* dst, int numSamples, const ClpRng& clpRng )
{
  for( int n = 0; n < numSamples; n+=2) 
  {
    dst[n]   = ClipPel( src[n]   , clpRng );
    dst[n+1] = ClipPel( src[n+1] , clpRng );
  }
}

template< typename T >
void addAvgCore( const T* src1, const T* src2, T* dest, int numSamples, unsigned rshift, int offset, const ClpRng& clpRng )
{
  for( int n = 0; n < numSamples; n+=2) 
  {
    dest[n]   = ClipPel( rightShiftU( ( src1[n]   + src2[n]   + offset ), rshift ), clpRng );
    dest[n+1] = ClipPel( rightShiftU( ( src1[n+1] + src2[n+1] + offset ), rshift ), clpRng );
  }
}

template< typename T >
void roundGeoCore( const T* src, T* dest, const int numSamples, unsigned rshift, int offset, const ClpRng &clpRng)
{
  for( int i = 0; i < numSamples; i+=2)
  {
    dest[i]   = ClipPel(rightShiftU(src[i  ] + offset, rshift), clpRng);
    dest[i+1] = ClipPel(rightShiftU(src[i+1] + offset, rshift), clpRng);
  }
}

template<typename T>
void linTfCore( const T* src, int srcStride, Pel* dst, int dstStride, int width, int height, int scale, unsigned shift, int offset, const ClpRng& clpRng, bool bClip )
{
#define LINTF_CORE_INC  \
  src += srcStride;     \
  dst += dstStride;     \

  if( bClip )
  {
#define LINTF_CORE_OP( ADDR ) dst[ADDR] = ( Pel ) ClipPel( rightShiftU( scale * src[ADDR], shift ) + offset, clpRng )

  SIZE_AWARE_PER_EL_OP( LINTF_CORE_OP, LINTF_CORE_INC );

#undef LINTF_CORE_OP
  }
  else
  {
#define LINTF_CORE_OP( ADDR ) dst[ADDR] = ( Pel ) ( rightShiftU( scale * src[ADDR], shift ) + offset )

  SIZE_AWARE_PER_EL_OP( LINTF_CORE_OP, LINTF_CORE_INC );

#undef LINTF_CORE_OP
  }
#undef LINTF_CORE_INC
}

template<typename T, int N>
void transposeNxNCore( const Pel* src, int srcStride, Pel* dst, int dstStride )
{
  for( int i = 0; i < N; i++ )
  {
    for( int j = 0; j < N; j++ )
    {
      dst[j * dstStride] = src[j];
    }

    dst++;
    src += srcStride;
  }
}

template<typename T>
void copyClipCore( const T* src, int srcStride, Pel* dst, int dstStride, int width, int height, const ClpRng& clpRng )
{
#define RECO_OP( ADDR ) dst[ADDR] = ClipPel( src[ADDR], clpRng )
#define RECO_INC      \
    src += srcStride; \
    dst += dstStride; \

  SIZE_AWARE_PER_EL_OP( RECO_OP, RECO_INC );

#undef RECO_OP
#undef RECO_INC
}

void copyBufferCore( const char* src, int srcStride, char* dst, int dstStride, int numBytes, int height)
{
  for( int i = 0; i < height; i++, src += srcStride, dst += dstStride )
  {
    memcpy( dst, src, numBytes );
  }
}

void paddingCore(Pel* ptr, int stride, int width, int height, int padSize)
{
  /*left and right padding*/
  Pel* ptrTemp1 = ptr;
  Pel* ptrTemp2 = ptr + (width - 1);
  int offset = 0;
  for (int i = 0; i < height; i++)
  {
    offset = stride * i;
    for (int j = 1; j <= padSize; j++)
    {
      *(ptrTemp1 - j + offset) = *(ptrTemp1 + offset);
      *(ptrTemp2 + j + offset) = *(ptrTemp2 + offset);
    }
  }
  /*Top and Bottom padding*/
  int numBytes = (width + padSize + padSize) * sizeof(Pel);
  ptrTemp1 = (ptr - padSize);
  ptrTemp2 = (ptr + (stride * (height - 1)) - padSize);
  for (int i = 1; i <= padSize; i++)
  {
    memcpy(ptrTemp1 - (i * stride), (ptrTemp1), numBytes);
    memcpy(ptrTemp2 + (i * stride), (ptrTemp2), numBytes);
  }
}

void applyLutCore( const Pel* src, const ptrdiff_t srcStride, Pel* dst, const ptrdiff_t dstStride, int width, int height, const Pel* lut )
{
#define RSP_SGNL_OP( ADDR ) dst[ADDR] = lut[src[ADDR]]
#define RSP_SGNL_INC        src      += srcStride; dst += dstStride;

  SIZE_AWARE_PER_EL_OP( RSP_SGNL_OP, RSP_SGNL_INC )

#undef RSP_SGNL_OP
#undef RSP_SGNL_INC
}

void fillMapPtr_Core( void** ptrMap, const ptrdiff_t mapStride, int width, int height, void* val )
{
  if( width == mapStride )
  {
    std::fill_n( ptrMap, width * height, val );
  }
  else
  {
    while( height-- )
    {
      std::fill_n( ptrMap, width, val );
      ptrMap += mapStride;
    }
  }
}

PelBufferOps::PelBufferOps()
{
  isInitX86Done = false;

  addAvg            = addAvgCore<Pel>;
  reco              = recoCore<Pel>;
  copyClip          = copyClipCore<Pel>;
  roundGeo          = roundGeoCore<Pel>;

  addAvg4           = addAvgCore<Pel>;
  addAvg8           = addAvgCore<Pel>;
  addAvg16          = addAvgCore<Pel>;

  copyClip4         = copyClipCore<Pel>;
  copyClip8         = copyClipCore<Pel>;

  reco4             = reconstructCore<Pel>;
  reco8             = reconstructCore<Pel>;

  linTf4            = linTfCore<Pel>;
  linTf8            = linTfCore<Pel>;

  copyBuffer        = copyBufferCore;
  padding           = paddingCore;
#if ENABLE_SIMD_OPT_BCW
  removeHighFreq8   = removeHighFreq;
  removeHighFreq4   = removeHighFreq;
#endif

  transpose4x4      = transposeNxNCore<Pel,4>;
  transpose8x8      = transposeNxNCore<Pel,8>;
  profGradFilter    = gradFilterCore <false>;
  applyPROF         = applyPROFCore;
  mipMatrixMul_4_4  = mipMatrixMulCore<4,4>;
  mipMatrixMul_8_4  = mipMatrixMulCore<8,4>;
  mipMatrixMul_8_8  = mipMatrixMulCore<8,8>;
  weightCiip        = weightCiipCore;
  roundIntVector    = nullptr;

  applyLut          = applyLutCore;

  fillPtrMap        = fillMapPtr_Core;
}

PelBufferOps g_pelBufOP = PelBufferOps();

template<>
void AreaBuf<Pel>::addWeightedAvg(const AreaBuf<const Pel>& other1, const AreaBuf<const Pel>& other2, const ClpRng& clpRng, const int8_t BcwIdx)
{
  const int8_t w0 = 4;//getBcwWeight(BcwIdx, REF_PIC_LIST_0);
  const int8_t w1 = 4; //getBcwWeight(BcwIdx, REF_PIC_LIST_1);
  const int8_t log2WeightBase = 3; //g_BCWLog2WeightBase;
  //
  const Pel* src0 = other1.buf;
  const Pel* src2 = other2.buf;
  Pel* dest = buf;

  const unsigned src1Stride = other1.stride;
  const unsigned src2Stride = other2.stride;
  const unsigned destStride = stride;
  const int      clipbd     = clpRng.bd;
  const unsigned shiftNum   = std::max<int>(2, (IF_INTERNAL_PREC - clipbd)) + log2WeightBase;
  const int      offset     = (1 << (shiftNum - 1)) + (IF_INTERNAL_OFFS << log2WeightBase);

#define ADD_AVG_OP( ADDR ) dest[ADDR] = ClipPel( rightShiftU( ( src0[ADDR]*w0 + src2[ADDR]*w1 + offset ), shiftNum ), clpRng )
#define ADD_AVG_INC     \
    src0 += src1Stride; \
    src2 += src2Stride; \
    dest += destStride; \

  SIZE_AWARE_PER_EL_OP(ADD_AVG_OP, ADD_AVG_INC);

#undef ADD_AVG_OP
#undef ADD_AVG_INC
}

template<>
void AreaBuf<Pel>::rspSignal( const Pel* pLUT)
{
  g_pelBufOP.applyLut( buf, stride, buf, stride, width, height, pLUT );
}


template<>
void AreaBuf<Pel>::rspSignal( const AreaBuf<const Pel>& other, const Pel* pLUT)
{
  g_pelBufOP.applyLut( other.buf, other.stride, buf, stride, width, height, pLUT );
}

template<>
void AreaBuf<Pel>::scaleSignal(const int scale, const bool dir, const ClpRng& clpRng)
{
        Pel* dst = buf;
  const Pel* src = buf;
  const int maxAbsclipBD = (1<<clpRng.bd) - 1;

  if (dir) // forward
  {
    if (width == 1)
    {
      THROW("Blocks of width = 1 not supported");
    }
    else
    {
      for (unsigned y = 0; y < height; y++)
      {
        for (unsigned x = 0; x < width; x++)
        {
          int sign = src[x] >= 0 ? 1 : -1;
          int absval = sign * src[x];
          dst[x] = (Pel)Clip3(-maxAbsclipBD, maxAbsclipBD, sign * (((absval << CSCALE_FP_PREC) + (scale >> 1)) / scale));
        }
        dst += stride;
        src += stride;
      }
    }
  }
  else // inverse
  {
    for (unsigned y = 0; y < height; y++)
    {
      for (unsigned x = 0; x < width; x++)
      {
        int val    = Clip3<int>((-maxAbsclipBD - 1), maxAbsclipBD, (int)src[x]);
        int sign   = src[x] >= 0 ? 1 : -1;
        int absval = sign * val;
               val = sign * ((absval * scale + (1 << (CSCALE_FP_PREC - 1))) >> CSCALE_FP_PREC);
        if (sizeof(Pel) == 2) // avoid overflow when storing data
        {
          val = Clip3<int>(-32768, 32767, val);
        }
        dst[x] = (Pel)val;
      }
      dst += stride;
      src += stride;
    }
  }
}

template<>
void AreaBuf<Pel>::addAvg( const AreaBuf<const Pel>& other1, const AreaBuf<const Pel>& other2, const ClpRng& clpRng)
{
  const Pel* src0 = other1.buf;
  const Pel* src2 = other2.buf;
        Pel* dest =        buf;

  const unsigned src1Stride = other1.stride;
  const unsigned src2Stride = other2.stride;
  const unsigned destStride =        stride;
  const int      clipbd     = clpRng.bd;
  const unsigned shiftNum   = std::max<int>(2, (IF_INTERNAL_PREC - clipbd)) + 1;
  const int      offset     = (1 << (shiftNum - 1)) + 2 * IF_INTERNAL_OFFS;

#if ENABLE_SIMD_OPT_BUFFER && defined(TARGET_SIMD_X86)
  if( destStride == width )
  {
    g_pelBufOP.addAvg(src0, src2, dest, width * height, shiftNum, offset, clpRng);
  }
  else if ((width & 15) == 0)
  {
    g_pelBufOP.addAvg16(src0, src1Stride, src2, src2Stride, dest, destStride, width, height, shiftNum, offset, clpRng);
  }
  else if( width > 2 && height > 2 && width == destStride ) 
  {
    g_pelBufOP.addAvg16(src0, src1Stride<<2, src2, src2Stride<<2, dest, destStride<<2, width<<2, height>>2, shiftNum, offset, clpRng);
  }
  else if( ( width & 7 ) == 0 )
  {
    g_pelBufOP.addAvg8( src0, src1Stride, src2, src2Stride, dest, destStride, width, height, shiftNum, offset, clpRng );
  }
  else if( ( width & 3 ) == 0 )
  {
    g_pelBufOP.addAvg4( src0, src1Stride, src2, src2Stride, dest, destStride, width, height, shiftNum, offset, clpRng );
  }
  else
#endif
  {
#define ADD_AVG_OP( ADDR ) dest[ADDR] = ClipPel( rightShiftU( ( src0[ADDR] + src2[ADDR] + offset ), shiftNum ), clpRng )
#define ADD_AVG_INC     \
    src0 += src1Stride; \
    src2 += src2Stride; \
    dest += destStride; \

    SIZE_AWARE_PER_EL_OP( ADD_AVG_OP, ADD_AVG_INC );

#undef ADD_AVG_OP
#undef ADD_AVG_INC
  }
}

template<>
void AreaBuf<Pel>::copyClip( const AreaBuf<const Pel>& src, const ClpRng& clpRng )
{
  const Pel* srcp = src.buf;
        Pel* dest =     buf;

  const unsigned srcStride  = src.stride;
  const unsigned destStride = stride;

  if( destStride == width)
  {
    g_pelBufOP.copyClip(srcp, dest, width * height, clpRng);
  }
  else if ((width & 7) == 0)
  {
    g_pelBufOP.copyClip8(srcp, srcStride, dest, destStride, width, height, clpRng);
  }
  else if ((width & 3) == 0)
  {
    g_pelBufOP.copyClip4(srcp, srcStride, dest, destStride, width, height, clpRng);
  }
  else
  {
    for( int y = 0; y < height; y++ )
    {
      dest[0] = ClipPel( srcp[0], clpRng);
      dest[1] = ClipPel( srcp[1], clpRng);
      srcp += srcStride;
      dest += destStride;
    }                                                         \
  }
}

template<>
void AreaBuf<Pel>::reconstruct( const AreaBuf<const Pel>& pred, const AreaBuf<const Pel>& resi, const ClpRng& clpRng )
{
  const Pel* src1 = pred.buf;
  const Pel* src2 = resi.buf;
        Pel* dest =      buf;

  const unsigned src1Stride = pred.stride;
  const unsigned src2Stride = resi.stride;
  const unsigned destStride =      stride;

  if( src2Stride == width )
  {
    g_pelBufOP.reco( pred.buf, resi.buf, buf, width * height, clpRng );
  }
  else if( ( width & 7 ) == 0 )
  {
    g_pelBufOP.reco8( src1, src1Stride, src2, src2Stride, dest, destStride, width, height, clpRng );
  }
  else if( ( width & 3 ) == 0 )
  {
    g_pelBufOP.reco4( src1, src1Stride, src2, src2Stride, dest, destStride, width, height, clpRng );
  }
  else
  {
    for( int y = 0; y < height; y++ )
    {
      dest[0] = ClipPel( src1[0] + src2[0], clpRng);
      dest[1] = ClipPel( src1[1] + src2[1], clpRng);
      src1 += src1Stride;
      src2 += src2Stride;
      dest += destStride;
    }                                                         \
  }
}

template<>
void AreaBuf<Pel>::linearTransform( const int scale, const unsigned shift, const int offset, bool bClip, const ClpRng& clpRng )
{
  const Pel* src = buf;
        Pel* dst = buf;

  if( stride == width)
  {
    if( width > 2 && height > 2 )
    {
      g_pelBufOP.linTf8( src, stride<<2, dst, stride<<2, width<<2, height>>2, scale, shift, offset, clpRng, bClip );
    }
    else
    {
      g_pelBufOP.linTf4( src, stride<<1, dst, stride<<1, width<<1, height>>1, scale, shift, offset, clpRng, bClip );
    }
  }
  else if( ( width & 7 ) == 0 )
  {
    g_pelBufOP.linTf8( src, stride, dst, stride, width, height, scale, shift, offset, clpRng, bClip );
  }
  else if( ( width & 3 ) == 0 )
  {
    g_pelBufOP.linTf4( src, stride, dst, stride, width, height, scale, shift, offset, clpRng, bClip );
  }
  else
  {
    if( bClip )
    {
      for( int y = 0; y < height; y++ )
      {
        dst[0] = ( Pel ) ClipPel( rightShiftU( scale * src[0], shift ) + offset, clpRng );
        dst[1] = ( Pel ) ClipPel( rightShiftU( scale * src[1], shift ) + offset, clpRng );
        src += stride;
        dst += stride;
      } 
    }
    else
    {
      for( int y = 0; y < height; y++ )
      {
        dst[0] = ( Pel ) ( rightShiftU( scale * src[0], shift ) + offset );
        dst[1] = ( Pel ) ( rightShiftU( scale * src[1], shift ) + offset );
        src += stride;
        dst += stride;
      } 
    }
  }
}

#if ENABLE_SIMD_OPT_BUFFER && defined(TARGET_SIMD_X86)

template<>
void AreaBuf<Pel>::transposedFrom( const AreaBuf<const Pel>& other )
{
  CHECK( width != other.height || height != other.width, "Incompatible size" );

  if( ( width & 3 ) != 0 || ( height & 3 ) != 0 )
  {
          Pel* dst =       buf;
    const Pel* src = other.buf;
    width          = other.height;
    height         = other.width;
    stride         = stride < width ? width : stride;

    for( unsigned y = 0; y < other.height; y++ )
    {
      for( unsigned x = 0; x < other.width; x++ )
      {
        dst[y + x*stride] = src[x + y * other.stride];
      }
    }
  }
  else if( ( width & 7 ) != 0 || ( height & 7 ) != 0 )
  {
    const Pel* src = other.buf;

    for( unsigned y = 0; y < other.height; y += 4 )
    {
      Pel* dst = buf + y;

      for( unsigned x = 0; x < other.width; x += 4 )
      {
        g_pelBufOP.transpose4x4( &src[x], other.stride, dst, stride );

        dst += 4 * stride;
      }

      src += 4 * other.stride;
    }
  }
  else
  {
    const Pel* src = other.buf;

    for( unsigned y = 0; y < other.height; y += 8 )
    {
      Pel* dst = buf + y;

      for( unsigned x = 0; x < other.width; x += 8 )
      {
        g_pelBufOP.transpose8x8( &src[x], other.stride, dst, stride );

        dst += 8 * stride;
      }

      src += 8 * other.stride;
    }
  }
}
#endif

template<>
void AreaBuf<Pel>::weightCiip( const AreaBuf<const Pel>& intra, const int numIntra )
{
  CHECK(width == 2, "Width of 2 is not supported");
  g_pelBufOP.weightCiip( buf, intra.buf, width * height, numIntra );
}

template<>
void AreaBuf<MotionInfo>::fill( const MotionInfo& val )
{
  if( width == stride )
  {
    std::fill_n( buf, width * height, val );
  }
  else
  {
    MotionInfo* dst = buf;

    for( int y = 0; y < height; y++, dst += stride )
    {
      std::fill_n( dst, width, val );
    }
  }
}

PelStorage::PelStorage()
{
  for( uint32_t i = 0; i < MAX_NUM_COMP; i++ )
  {
    m_origin[i] = nullptr;
  }
}

PelStorage::~PelStorage()
{
  destroy();
}

void PelStorage::create( const UnitArea& _UnitArea )
{
  create( _UnitArea.chromaFormat, _UnitArea.blocks[0] );
}

void PelStorage::create( const ChromaFormat &_chromaFormat, const Area& _area )
{
  CHECK( !bufs.empty(), "Trying to re-create an already initialized buffer" );

  chromaFormat = _chromaFormat;

  const uint32_t numComp = getNumberValidComponents( _chromaFormat );

  uint32_t bufSize = 0;
  for( uint32_t i = 0; i < numComp; i++ )
  {
    const ComponentID compID = ComponentID( i );
    const unsigned totalWidth  = _area.width  >> getComponentScaleX( compID, _chromaFormat );
    const unsigned totalHeight = _area.height >> getComponentScaleY( compID, _chromaFormat );

    const uint32_t area = totalWidth * totalHeight;
    CHECK( !area, "Trying to create a buffer with zero area" );
    bufSize += area;
  }

  //allocate one buffer
  m_origin[0] = ( Pel* ) xMalloc( Pel, bufSize );

  Pel* topLeft = m_origin[0];
  for( uint32_t i = 0; i < numComp; i++ )
  {
    const ComponentID compID = ComponentID( i );
    const unsigned totalWidth  = _area.width  >> getComponentScaleX( compID, _chromaFormat );
    const unsigned totalHeight = _area.height >> getComponentScaleY( compID, _chromaFormat );
    const uint32_t area = totalWidth * totalHeight;

    bufs.push_back( PelBuf( topLeft, totalWidth, totalWidth, totalHeight ) );
    topLeft += area;
  }
}

void PelStorage::create( const ChromaFormat &_chromaFormat, const Area& _area, const unsigned _maxCUSize, const unsigned _margin, const unsigned _alignment, const bool _scaleChromaMargin )
{
  CHECK( !bufs.empty(), "Trying to re-create an already initialized buffer" );

  chromaFormat = _chromaFormat;

  const uint32_t numComp = getNumberValidComponents( _chromaFormat );

  unsigned extHeight = _area.height;
  unsigned extWidth  = _area.width;

  if( _maxCUSize )
  {
    extHeight = ( ( _area.height + _maxCUSize - 1 ) / _maxCUSize ) * _maxCUSize;
    extWidth  = ( ( _area.width  + _maxCUSize - 1 ) / _maxCUSize ) * _maxCUSize;
  }

  for( uint32_t i = 0; i < numComp; i++ )
  {
    const ComponentID compID = ComponentID( i );
    const unsigned scaleX = getComponentScaleX( compID, _chromaFormat );
    const unsigned scaleY = getComponentScaleY( compID, _chromaFormat );

    unsigned scaledHeight = extHeight >> scaleY;
    unsigned scaledWidth  = extWidth  >> scaleX;
    unsigned ymargin      = _margin >> (_scaleChromaMargin?scaleY:0);
    unsigned xmargin      = _margin >> (_scaleChromaMargin?scaleX:0);
    unsigned totalWidth   = scaledWidth + 2*xmargin;
    unsigned totalHeight  = scaledHeight +2*ymargin;

    if( _alignment )
    {
      // make sure buffer lines are align
      CHECK( _alignment != MEMORY_ALIGN_DEF_SIZE, "Unsupported alignment" );
      totalWidth = ( ( totalWidth + _alignment - 1 ) / _alignment ) * _alignment;
    }
    uint32_t area = totalWidth * totalHeight;
    CHECK( !area, "Trying to create a buffer with zero area" );

    m_origin[i] = ( Pel* ) xMalloc( Pel, area );
    Pel* topLeft = m_origin[i] + totalWidth * ymargin + xmargin;
    bufs.push_back( PelBuf( topLeft, totalWidth, _area.width >> scaleX, _area.height >> scaleY ) );
  }
}

void PelStorage::createFromBuf( PelUnitBuf buf )
{
  chromaFormat = buf.chromaFormat;

  const uint32_t numCh = getNumberValidComponents( chromaFormat );

  bufs.resize(numCh);

  for( uint32_t i = 0; i < numCh; i++ )
  {
    PelBuf cPelBuf = buf.get( ComponentID( i ) );
    bufs[i] = PelBuf( cPelBuf.bufAt( 0, 0 ), cPelBuf.stride, cPelBuf.width, cPelBuf.height );
  }
}

void PelStorage::takeOwnership( PelStorage& other )
{
  chromaFormat = other.chromaFormat;

  const uint32_t numCh = getNumberValidComponents( chromaFormat );

  bufs.resize(numCh);

  for( uint32_t i = 0; i < numCh; i++ )
  {
    PelBuf cPelBuf = other.get( ComponentID( i ) );
    bufs[i] = PelBuf( cPelBuf.bufAt( 0, 0 ), cPelBuf.stride, cPelBuf.width, cPelBuf.height );
    std::swap( m_origin[i], other.m_origin[i]);
  }
  other.destroy();
}


void PelStorage::swap( PelStorage& other )
{
  const uint32_t numCh = getNumberValidComponents( chromaFormat );

  for( uint32_t i = 0; i < numCh; i++ )
  {
    // check this otherwise it would turn out to get very weird
    CHECK( chromaFormat                   != other.chromaFormat                  , "Incompatible formats" );
    CHECK( get( ComponentID( i ) )        != other.get( ComponentID( i ) )       , "Incompatible formats" );
    CHECK( get( ComponentID( i ) ).stride != other.get( ComponentID( i ) ).stride, "Incompatible formats" );

    std::swap( bufs[i].buf,    other.bufs[i].buf );
    std::swap( bufs[i].stride, other.bufs[i].stride );
    std::swap( m_origin[i],    other.m_origin[i] );
  }
}

void PelStorage::destroy()
{
  chromaFormat = NUM_CHROMA_FORMAT;
  for( uint32_t i = 0; i < MAX_NUM_COMP; i++ )
  {
    if( m_origin[i] )
    {
      xFree( m_origin[i] );
      m_origin[i] = nullptr;
    }
  }
  bufs.clear();
}

PelBuf PelStorage::getBuf( const ComponentID CompID )
{
  return bufs[CompID];
}

const CPelBuf PelStorage::getBuf( const ComponentID CompID ) const
{
  return bufs[CompID];
}

PelBuf PelStorage::getBuf( const CompArea& blk )
{
  const PelBuf& r = bufs[blk.compID];

  CHECKD( rsAddr( blk.bottomRight(), r.stride ) >= ( ( r.height - 1 ) * r.stride + r.width ), "Trying to access a buf outside of bound!" );

  return PelBuf( r.buf + rsAddr( blk, r.stride ), r.stride, blk );
}

const CPelBuf PelStorage::getBuf( const CompArea& blk ) const
{
  const PelBuf& r = bufs[blk.compID];
  return CPelBuf( r.buf + rsAddr( blk, r.stride ), r.stride, blk );
}

PelUnitBuf PelStorage::getBuf( const UnitArea& unit )
{
  return ( chromaFormat == CHROMA_400 ) ? PelUnitBuf( chromaFormat, getBuf( unit.Y() ) ) : PelUnitBuf( chromaFormat, getBuf( unit.Y() ), getBuf( unit.Cb() ), getBuf( unit.Cr() ) );
}

const CPelUnitBuf PelStorage::getBuf( const UnitArea& unit ) const
{
  return ( chromaFormat == CHROMA_400 ) ? CPelUnitBuf( chromaFormat, getBuf( unit.Y() ) ) : CPelUnitBuf( chromaFormat, getBuf( unit.Y() ), getBuf( unit.Cb() ), getBuf( unit.Cr() ) );
}

PelUnitBuf PelStorage::getBuf(const int strY, const int strCb, const int strCr, const UnitArea& unit)
{
  CHECKD( unit.Y().width > bufs[COMP_Y].width && unit.Y().height > bufs[COMP_Y].height, "unsuported request" );
  CHECKD( strY > bufs[COMP_Y].stride, "unsuported request" );
  CHECKD( strCb > bufs[COMP_Cb].stride, "unsuported request" );
  CHECKD( strCr > bufs[COMP_Cr].stride, "unsuported request" );
  return (chromaFormat == CHROMA_400) ? PelUnitBuf(chromaFormat, PelBuf( bufs[COMP_Y].buf, strY, unit.Y())) : PelUnitBuf(chromaFormat, PelBuf( bufs[COMP_Y].buf, strY, unit.Y()), PelBuf( bufs[COMP_Cb].buf, strCb, unit.Cb()), PelBuf( bufs[COMP_Cr].buf, strCr, unit.Cr()));
}

const CPelUnitBuf PelStorage::getBuf(const int strY, const int strCb, const int strCr, const UnitArea& unit) const
{
  CHECKD( unit.Y().width > bufs[COMP_Y].width && unit.Y().height > bufs[COMP_Y].height, "unsuported request" );
  CHECKD( strY > bufs[COMP_Y].stride, "unsuported request" );
  CHECKD( strCb > bufs[COMP_Cb].stride, "unsuported request" );
  CHECKD( strCr > bufs[COMP_Cr].stride, "unsuported request" );
  return (chromaFormat == CHROMA_400) ? CPelUnitBuf(chromaFormat, CPelBuf( bufs[COMP_Y].buf, strY, unit.Y())) : CPelUnitBuf(chromaFormat, CPelBuf( bufs[COMP_Y].buf, strY, unit.Y()), CPelBuf( bufs[COMP_Cb].buf, strCb, unit.Cb()), CPelBuf( bufs[COMP_Cr].buf, strCr, unit.Cr()));
}

PelUnitBuf PelStorage::getBufPart(const UnitArea& unit)
{
  CHECKD( unit.Y().width > bufs[COMP_Y].width && unit.Y().height > bufs[COMP_Y].height, "unsuported request" );
  return (chromaFormat == CHROMA_400) ? PelUnitBuf(chromaFormat, PelBuf( bufs[COMP_Y].buf, bufs[COMP_Y].stride, unit.Y())) : PelUnitBuf(chromaFormat, PelBuf( bufs[COMP_Y].buf, bufs[COMP_Y].stride, unit.Y()), PelBuf( bufs[COMP_Cb].buf, bufs[COMP_Cb].stride, unit.Cb()), PelBuf( bufs[COMP_Cr].buf, bufs[COMP_Cr].stride, unit.Cr()));
}

const CPelUnitBuf PelStorage::getBufPart(const UnitArea& unit) const
{
  CHECKD(unit.Y().width > bufs[COMP_Y].width && unit.Y().height > bufs[COMP_Y].height, "unsuported request");
  return (chromaFormat == CHROMA_400) ? CPelUnitBuf(chromaFormat, CPelBuf(bufs[COMP_Y].buf, unit.Y().width, unit.Y())) : CPelUnitBuf(chromaFormat, CPelBuf(bufs[COMP_Y].buf, unit.Y().width, unit.Y()), CPelBuf(bufs[COMP_Cb].buf, unit.Cb().width, unit.Cb()), CPelBuf(bufs[COMP_Cr].buf, unit.Cr().width, unit.Cr()));
}

const CPelUnitBuf PelStorage::getCompactBuf(const UnitArea& unit) const
{
  CHECK( unit.Y().width > bufs[COMP_Y].width && unit.Y().height > bufs[COMP_Y].height, "unsuported request" );
  return (chromaFormat == CHROMA_400) ? CPelUnitBuf(chromaFormat, CPelBuf( bufs[COMP_Y].buf, unit.Y().width, unit.Y())) : CPelUnitBuf(chromaFormat, CPelBuf( bufs[COMP_Y].buf, unit.Y().width, unit.Y()), CPelBuf( bufs[COMP_Cb].buf, unit.Cb().width, unit.Cb()), CPelBuf( bufs[COMP_Cr].buf, unit.Cr().width, unit.Cr()));
}

PelUnitBuf PelStorage::getCompactBuf(const UnitArea& unit)
{
  CHECK( unit.Y().width > bufs[COMP_Y].width && unit.Y().height > bufs[COMP_Y].height, "unsuported request" );
  return (chromaFormat == CHROMA_400) ? PelUnitBuf(chromaFormat, PelBuf( bufs[COMP_Y].buf, unit.Y().width, unit.Y())) : PelUnitBuf(chromaFormat, PelBuf( bufs[COMP_Y].buf, unit.Y().width, unit.Y()), PelBuf( bufs[COMP_Cb].buf, unit.Cb().width, unit.Cb()), PelBuf( bufs[COMP_Cr].buf, unit.Cr().width, unit.Cr()));
}

const CPelBuf PelStorage::getCompactBuf(const CompArea& carea) const
{
  return CPelBuf( bufs[carea.compID].buf, carea.width, carea);
}

PelBuf PelStorage::getCompactBuf(const CompArea& carea)
{
  return PelBuf( bufs[carea.compID].buf, carea.width, carea);
}

void setupPelUnitBuf( const YUVBuffer& yuvBuffer, PelUnitBuf& pelUnitBuf, const ChromaFormat& chFmt )
{
  CHECK( pelUnitBuf.bufs.size() != 0, "pelUnitBuf already in use" );
  pelUnitBuf.chromaFormat = chFmt;
  const int numComp = getNumberValidComponents( chFmt );
  for ( int i = 0; i < numComp; i++ )
  {
    const YUVPlane& yuvPlane = yuvBuffer.yuvPlanes[ i ];
    CHECK( yuvPlane.planeBuf == nullptr, "yuvBuffer not setup" );
    PelBuf area( yuvPlane.planeBuf, yuvPlane.stride, yuvPlane.width, yuvPlane.height );
    pelUnitBuf.bufs.push_back( area );
  }
}

void setupYuvBuffer ( const PelUnitBuf& pelUnitBuf, YUVBuffer& yuvBuffer, const Window* confWindow )
{
  const ChromaFormat chFmt = pelUnitBuf.chromaFormat;
  const int numComp        = getNumberValidComponents( chFmt );
  const Window  zeroWindow;
  const Window* cw = confWindow && confWindow->enabledFlag ? confWindow : &zeroWindow;
  for ( int i = 0; i < numComp; i++ )
  {
    const ComponentID compId = ComponentID( i );
          PelBuf area        = pelUnitBuf.get( compId );
    const int sx             = getComponentScaleX( compId, chFmt );
    const int sy             = getComponentScaleY( compId, chFmt );
    YUVPlane& yuvPlane       = yuvBuffer.yuvPlanes[ i ];
    CHECK( yuvPlane.planeBuf != nullptr, "yuvBuffer already in use" );
    yuvPlane.planeBuf        = area.bufAt( cw->winLeftOffset >> sx, cw->winTopOffset >> sy );
    yuvPlane.width           = ( ( area.width  << sx ) - ( cw->winLeftOffset + cw->winRightOffset  ) ) >> sx;
    yuvPlane.height          = ( ( area.height << sy ) - ( cw->winTopOffset  + cw->winBottomOffset ) ) >> sy;
    yuvPlane.stride          = area.stride;
  }
}

} // namespace vvenc

//! \}

