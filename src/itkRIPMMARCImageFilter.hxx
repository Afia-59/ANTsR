/*=========================================================================
 *
 *  Copyright Insight Software Consortium
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/
#ifndef itkRIPMMARCImageFilter_hxx
#define itkRIPMMARCImageFilter_hxx

#include "itkRIPMMARCImageFilter.h"

#include "itkArray.h"
#include "itkDiscreteGaussianImageFilter.h"
#include "itkImageRegionConstIterator.h"
#include "itkImageRegionIterator.h"
#include "itkImageRegionIteratorWithIndex.h"
#include "itkMath.h"
#include "itkMeanImageFilter.h"
#include "itkNeighborhoodIterator.h"
#include "itkProgressReporter.h"
#include "itkStatisticsImageFilter.h"
#include "itkVarianceImageFilter.h"

#include <numeric>

namespace itk {

template< class TImage >
bool IsInside( typename TImage::Pointer input, typename TImage::IndexType index )
{
  /** FIXME - should use StartIndex - */
  typedef TImage ImageType;
  enum { ImageDimension = ImageType::ImageDimension };
  bool isinside = true;
  for( unsigned int i = 0; i < ImageDimension; i++ )
    {
    float shifted = index[i];
    if( shifted < 0 || shifted >  input->GetLargestPossibleRegion().GetSize()[i] - 1  )
      {
      isinside = false;
      }
    }
  return isinside;
}

template<typename TInputImage, typename TOutputImage>
typename TInputImage::Pointer RIPMMARCImageFilter<TInputImage, TOutputImage>
::GenerateMaskImageFromPatch( )
{
  unsigned int sizeOfImage = 2 * this->m_PatchRadius +
    2 *  this->m_PaddingVoxels + 1;
  InputImagePointer maskImage = InputImageType::New();
  IndexType   start;
  IndexType   beginningOfSphereRegion;
  typename InputImageType::SizeType    sizeOfSphereRegion;
  typename InputImageType::SizeType    size;
  typename InputImageType::SpacingType spacing;
  typename InputImageType::PointType   originPoint;
  typename InputImageType::IndexType   originIndex;

  for( unsigned int dd = 0; dd < ImageDimension; dd++ )
    {
    start[ dd ]   = 0;
    size[ dd ]    = sizeOfImage;
    spacing[ dd ] = 1.0;
    originPoint[ dd ] = originIndex[ dd ]  = 0;
    // one for each side--this is correct
    beginningOfSphereRegion[ dd ] = this->m_PaddingVoxels + this->m_PatchRadius;
    sizeOfSphereRegion[ dd ] = this->m_PatchRadius * 2 + 1;
    }
  typename InputImageType::RegionType region;
  region.SetSize( size );
  region.SetIndex( originIndex );
  maskImage->SetRegions( region );
  maskImage->Allocate( );
  maskImage->SetSpacing( spacing );
  maskImage->SetOrigin( originPoint );
  typedef typename itk::ImageRegionIterator< InputImageType > RegionIteratorType;
  RegionIteratorType regionIterator( maskImage, region );
  for ( regionIterator.GoToBegin(); !regionIterator.IsAtEnd(); ++regionIterator)
    {
    regionIterator.Set( 0.0 );
    }
  typename InputImageType::RegionType sphereRegion;
  sphereRegion.SetSize( sizeOfSphereRegion );
  sphereRegion.SetIndex( beginningOfSphereRegion );
  typedef itk::NeighborhoodIterator< InputImageType > NeighborhoodIteratorType;
  typename NeighborhoodIteratorType::RadiusType radius;
  radius.Fill( this->m_PatchRadius );
  NeighborhoodIteratorType SphereRegionIterator( radius, maskImage, sphereRegion );

  for( unsigned int ii = 0; ii < this->m_IndicesWithinSphere.size(); ii++)
    {
    SphereRegionIterator.SetPixel( this->m_IndicesWithinSphere[ ii ],  1.0 );
    }

  return maskImage;
}

template<typename TInputImage, typename TOutputImage>
vnl_vector< double > RIPMMARCImageFilter<TInputImage, TOutputImage> // FIXME should replace double with comptype
::ReorientPatchToReferenceFrame(
  itk::ConstNeighborhoodIterator< TInputImage > GradientImageNeighborhood1,
  itk::ConstNeighborhoodIterator< TInputImage > GradientImageNeighborhood2,
  const typename TInputImage::Pointer MaskImage,
  const typename GradientImageType::Pointer GradientImage1,
  const typename GradientImageType::Pointer GradientImage2,
  InterpPointer Interpolator )
{
  /* This function takes a reference patch and a moving patch and rotates
   * the moving patch to match the reference patch.
   * It returns an image equal to Image1, but with the reoriented entries from
   * the moving patch inserted in place of the reference patch.
   * Intended usage is to feed in an eigenvector in a canonical coordinate
   * frame, generated by GenerateMaskImageFromPatch, that consists of only the
   * entries in the eigenvector on a blank background.  The output of this function
   * then is the moving neighborhood reoriented to match the input eigenvector. */

  unsigned int NumberOfIndicesWithinSphere = this->m_IndicesWithinSphere.size();
  std::vector< PointType > ImagePatch1;
  std::vector< PointType > ImagePatch2;
  VectorType VectorizedImagePatch1( NumberOfIndicesWithinSphere, 0 );
  VectorType VectorizedImagePatch2( NumberOfIndicesWithinSphere, 0 );
  vnl_matrix< RealValueType > GradientMatrix1( NumberOfIndicesWithinSphere, ImageDimension );
  vnl_matrix< RealValueType > GradientMatrix2( NumberOfIndicesWithinSphere, ImageDimension );
  GradientMatrix1.fill( 0 );
  GradientMatrix2.fill( 0 );

  /*  Calculate center of each image patch so that rotations are about the origin. */
  PointType CenterPointOfImage1;
  PointType CenterPointOfImage2;
  CenterPointOfImage1.Fill( 0 );
  CenterPointOfImage2.Fill( 0 );
  RealType MeanNormalizingConstant = 1.0 / ( RealType ) NumberOfIndicesWithinSphere;
  for( unsigned int ii = 0; ii < NumberOfIndicesWithinSphere; ii++ )
    {
    VectorizedImagePatch1[ ii ] = GradientImageNeighborhood1.GetPixel( this->m_IndicesWithinSphere[ ii ] );
    VectorizedImagePatch2[ ii ] = GradientImageNeighborhood2.GetPixel( this->m_IndicesWithinSphere[ ii ] );
    IndexType GradientImageIndex1 = GradientImageNeighborhood1.GetIndex( this->m_IndicesWithinSphere[ ii ] );
    IndexType GradientImageIndex2 = GradientImageNeighborhood2.GetIndex( this->m_IndicesWithinSphere[ ii ] );
    if( ( IsInside< GradientImageType >( GradientImage1, GradientImageIndex1) ) &&
	( IsInside< GradientImageType >( GradientImage2, GradientImageIndex2 ) ) )
    {
      GradientPixelType GradientPixel1 = GradientImage1->GetPixel( GradientImageIndex1 ) * this->m_weights[ ii ];
      GradientPixelType GradientPixel2 = GradientImage2->GetPixel( GradientImageIndex2 ) * this->m_weights[ ii ];
      for( unsigned int jj = 0; jj < ImageDimension; jj++)
        {
	      GradientMatrix1( ii, jj ) = GradientPixel1[ jj ];
	      GradientMatrix2( ii, jj ) = GradientPixel2[ jj ];
        }
      PointType Point1;
      PointType Point2;
      GradientImage1->TransformIndexToPhysicalPoint( GradientImageIndex1, Point1 );
      GradientImage2->TransformIndexToPhysicalPoint( GradientImageIndex2, Point2 );
      for( unsigned int dd = 0; dd < ImageDimension; dd++ )
        {
	      CenterPointOfImage1[ dd ] = CenterPointOfImage1[ dd ] + Point1[ dd ] * MeanNormalizingConstant;
	      CenterPointOfImage2[ dd ] = CenterPointOfImage2[ dd ] + Point2[ dd ] * MeanNormalizingConstant;
        }
      ImagePatch1.push_back( Point1 );
      ImagePatch2.push_back( Point2 );
    }
    else return vnl_vector< RealValueType > (1, 0.0 );
  }
  RealType MeanOfImagePatch1 = VectorizedImagePatch1.mean();
  RealType MeanOfImagePatch2 = VectorizedImagePatch2.mean();
  VectorType CenteredVectorizedImagePatch1 = ( VectorizedImagePatch1 - MeanOfImagePatch1 );
  VectorType CenteredVectorizedImagePatch2 = ( VectorizedImagePatch2 - MeanOfImagePatch2 );
  // RealType StDevOfImage1 = sqrt( CenteredVectorizedImagePatch1.squared_magnitude()  );
  // RealType StDevOfImage2 = sqrt( CenteredVectorizedImagePatch2.squared_magnitude() );
//  RealType correlation = inner_product( CenteredVectorizedImagePatch1,
//      CenteredVectorizedImagePatch2 ) / ( StDevOfImage1 * StDevOfImage2 ); // FIXME why is this here?

  bool OK = true;
/*  std::cout << "VectorizedImagePatch1 is (before rotation) " << VectorizedImagePatch1 << std::endl;
  std::cout << "VectorizedImagePatch2 is (before rotation) " << VectorizedImagePatch2 << std::endl;*/
/*  std::cout << "GradientMatrix1 is " << GradientMatrix1 << std::endl;
  std::cout << "GradientMatrix2 is " << GradientMatrix2 << std::endl; */
  vnl_matrix< RealValueType > CovarianceMatrixOfImage1 = GradientMatrix1.transpose() * GradientMatrix1;
  vnl_matrix< RealValueType > CovarianceMatrixOfImage2 = GradientMatrix2.transpose() * GradientMatrix2;
  vnl_symmetric_eigensystem< RealValueType > EigOfImage1( CovarianceMatrixOfImage1 );
  vnl_symmetric_eigensystem< RealValueType > EigOfImage2( CovarianceMatrixOfImage2 );
/*  std::cout << "CovarianceMatrixOfImage1 is " << CovarianceMatrixOfImage1 << std::endl;
  std::cout << "CovarianceMatrixOfImage2 is " << CovarianceMatrixOfImage2 << std::endl;*/
  int NumberOfEigenvectors = EigOfImage1.D.cols();
  // FIXME: needs bug checking to make sure this is right
  // not sure how many eigenvectors there are or how they're indexed
  vnl_vector< RealValueType > Image1Eigvec1 = EigOfImage1.get_eigenvector( NumberOfEigenvectors - 1 ); // 0-indexed
  vnl_vector< RealValueType > Image1Eigvec2 = EigOfImage1.get_eigenvector( NumberOfEigenvectors - 2 );
  vnl_vector< RealValueType > Image2Eigvec1 = EigOfImage2.get_eigenvector( NumberOfEigenvectors - 1 );
  vnl_vector< RealValueType > Image2Eigvec2 = EigOfImage2.get_eigenvector( NumberOfEigenvectors - 2 );

  /* Solve Wahba's problem using Kabsch algorithm:
   * arg_min(Q) \sum_k || w_k - Q v_k ||^2
   * Q is rotation matrix, w_k and v_k are vectors to be aligned.
   * Solution:  Denote B = \sum_k w_k v_k^T
   * Decompose B = U * S * V^T
   * Then Q = U * M * V^T, where M = diag[ 1 1 det(U) det(V) ]
   * Refs: http://journals.iucr.org/a/issues/1976/05/00/a12999/a12999.pdf
   *       http://www.control.auc.dk/~tb/best/aug23-Bak-svdalg.pdf */
  vnl_matrix< RealValueType > B = outer_product( Image1Eigvec1, Image2Eigvec1 );
  if( ImageDimension == 3)
  {
    B = outer_product( Image1Eigvec1, Image2Eigvec1 ) +
        outer_product( Image1Eigvec2, Image2Eigvec2 );
  }
  vnl_svd< RealValueType > WahbaSVD( B );
  vnl_matrix< RealValueType > Q_solution = WahbaSVD.V() * WahbaSVD.U().transpose();
  // Now rotate the points to the same frame and sample neighborhoods again.
  for( unsigned int ii = 0; ii < NumberOfIndicesWithinSphere; ii++ )
  {
    PointType RotatedPoint = ImagePatch2[ ii ];
    // We also need vector representation of the point values
    vnl_vector< RealValueType > RotatedPointVector( RotatedPoint.Size(), 0 );
    // First move center of Patch 1 to center of Patch 2
    for( unsigned int dd = 0; dd < ImageDimension; dd++ )
    {
      RotatedPoint[ dd ] -= CenterPointOfImage2[ dd ];
      RotatedPointVector[ dd ] = RotatedPoint[ dd ];
    }

    // Now rotate RotatedPoint
    RotatedPointVector = ( Q_solution ) * RotatedPointVector;
    for( unsigned int dd = 0; dd < ImageDimension; dd++ )
      {
      RotatedPoint[ dd ] = RotatedPointVector[ dd ] + CenterPointOfImage2[ dd ];
      }
    if( Interpolator->IsInsideBuffer( RotatedPoint) )
      {
      VectorizedImagePatch2[ ii ] = Interpolator->Evaluate( RotatedPoint );
      }
    else OK = false;
  }

  /* This is a nasty little detail:  Because the eigenvector is in the positive quadrant,
   * you can end up with flipped images that are negatively correlated with each other.
   * Here we check for that and correct if necessary.
   */
  MeanOfImagePatch2 = VectorizedImagePatch2.mean();
  CenteredVectorizedImagePatch2 = ( VectorizedImagePatch2 - MeanOfImagePatch2 );

  if(inner_product(CenteredVectorizedImagePatch1, CenteredVectorizedImagePatch2) < 0)
  {

	  vnl_matrix< RealValueType > B = outer_product( Image1Eigvec1, Image2Eigvec1 );
	  if( ImageDimension == 3)
	  {
		  B = outer_product( Image1Eigvec1, Image2Eigvec1 ) +
				  outer_product( Image1Eigvec2, Image2Eigvec2 );
	  }
	  vnl_svd< RealValueType > WahbaSVD( B );
	  vnl_matrix< RealValueType > Q_solution = WahbaSVD.V() * WahbaSVD.U().transpose();
          vnl_matrix< RealValueType > rotationMat;
          if(ImageDimension == 2)
            {
            const RealValueType values[4] = {-1.0,0.0,0.0,-1.0};
            rotationMat.set_size(2, 2);
            rotationMat.set(values);
            }
          else if( ImageDimension == 3)
            {
            const RealValueType values[9] = {1.0, 0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, -1.0};
            rotationMat.set_size(3, 3);
            rotationMat.set(values);
            }

          Q_solution = Q_solution * rotationMat;

	  // Now rotate the points to the same frame and sample neighborhoods again.
	  for( unsigned int ii = 0; ii < NumberOfIndicesWithinSphere; ii++ )
	  {
		  PointType RotatedPoint = ImagePatch2[ ii ];
		  // We also need vector representation of the point values
		  vnl_vector< RealValueType > RotatedPointVector( RotatedPoint.Size(), 0 );
		  // First move center of Patch 1 to center of Patch 2
		  for( unsigned int dd = 0; dd < ImageDimension; dd++ )
		  {
			  RotatedPoint[ dd ] -= CenterPointOfImage2[ dd ];
			  RotatedPointVector[ dd ] = RotatedPoint[ dd ];
		  }

		  // Now rotate RotatedPoint
		  RotatedPointVector = ( Q_solution ) * RotatedPointVector;
		  for( unsigned int dd = 0; dd < ImageDimension; dd++ )
		  {
			  RotatedPoint[ dd ] = RotatedPointVector[ dd ] + CenterPointOfImage2[ dd ];
		  }
		  if( Interpolator->IsInsideBuffer( RotatedPoint) )
		  {
			  VectorizedImagePatch2[ ii ] = Interpolator->Evaluate( RotatedPoint );
		  }
		  else OK = false;
	  }

  }
  return VectorizedImagePatch2;
}


template<typename TInputImage, typename TOutputImage>
typename TInputImage::Pointer RIPMMARCImageFilter<TInputImage, TOutputImage>
::ConvertVectorToSpatialImage(
  vnl_vector< double > &Vector,
  typename TInputImage::Pointer Mask )
{
  InputImagePointer VectorAsSpatialImage = InputImageType::New();
  VectorAsSpatialImage->SetOrigin(  Mask->GetOrigin() );
  VectorAsSpatialImage->SetSpacing( Mask->GetSpacing() );
  VectorAsSpatialImage->SetRegions( Mask->GetLargestPossibleRegion() );
  VectorAsSpatialImage->SetDirection( Mask-> GetDirection() );
  VectorAsSpatialImage->Allocate();
  VectorAsSpatialImage->FillBuffer( itk::NumericTraits< InputPixelType >::Zero );
  unsigned long VectorIndex = 0;
  typedef itk::ImageRegionIteratorWithIndex< InputImageType > IteratorType;
  IteratorType MaskIterator( Mask, Mask->GetLargestPossibleRegion() );
  for( MaskIterator.GoToBegin(); !MaskIterator.IsAtEnd(); ++MaskIterator)
    {
    if( MaskIterator.Get() >= 0.5 )
      {
      InputPixelType Value = 0.0;
      if( VectorIndex < Vector.size() )
        {
  	    Value = Vector(VectorIndex);
        }
      else
        {
      	std::cout << "Size of mask does not match size of vector to be written!" << std::endl;
      	std::cout << "Exiting." << std::endl;
      	std::exception();
        }
      VectorAsSpatialImage->SetPixel(MaskIterator.GetIndex(), Value);
      ++VectorIndex;
      }
    else
      {
      MaskIterator.Set( 0 );
      }
    }
  return VectorAsSpatialImage;
};


template <typename TInputImage, typename TOutputImage>
RIPMMARCImageFilter<TInputImage, TOutputImage>
::RIPMMARCImageFilter() :
  m_RotationInvariant( true ),
  m_MeanCenterPatches( true ),
  m_LearnPatchBasis( true ),
  m_Verbose( true ),
  m_PatchRadius( 3 ),
  m_numberOfVoxelsWithinMask( 0 ),
  m_PaddingVoxels( 2 ),
  m_NumberOfSamplePatches( 0 )
{
  this->SetNumberOfRequiredInputs( 2 ); // image of interest and mask
  this->m_TargetVarianceExplained = 0.95;
  this->m_AchievedVarianceExplained = 0.0;
  this->m_CanonicalFrame = ITK_NULLPTR;
}

template<typename TInputImage, typename TOutputImage>
void RIPMMARCImageFilter<TInputImage, TOutputImage>
::GetSamplePatchLocations()
{
  this->m_patchSeedPoints.set_size( this->m_NumberOfSamplePatches , ImageDimension );
	vnl_vector< int > testPatchSeed( ImageDimension );
	int  patchSeedIterator = 0;
	int  patchSeedAttemptIterator = 0;
	typename InputImageType::IndexType patchIndex;
	typename InputImageType::SizeType inputSize =
			this->GetInput()->GetLargestPossibleRegion().GetSize();
  const MaskImageType* mask = this->GetMaskImage();
	if( this->m_Verbose )
	  {
		std::cout << "Attempting to find seed points. Looking for " << this->m_NumberOfSamplePatches <<
				" points out of " << inputSize << " possible points." << std::endl;
	  }

	srand( time( NULL) );
	while( patchSeedIterator < this->m_NumberOfSamplePatches )
	  {
		for( int i = 0; i < ImageDimension; ++i)
		  {
			patchIndex[ i ] = testPatchSeed( i ) = rand() % inputSize[ i ];
		  }
		if ( mask->GetPixel( patchIndex ) >= 1 )
		  {
			this->m_patchSeedPoints.set_row( patchSeedIterator, testPatchSeed );
			++patchSeedIterator;
		  }
		++patchSeedAttemptIterator;
	  }
	if( this->m_Verbose )
	  {
		std::cout << "Found " << patchSeedIterator <<
				" points in " << patchSeedAttemptIterator <<
				" attempts." << std::endl;
	  }
}

template<typename TInputImage, typename TOutputImage>
void RIPMMARCImageFilter<TInputImage, TOutputImage>
::ExtractSamplePatches()
{
  // allocate matrix based on radial size of patch
	int i = 0;
	typename InputImageType::IndexType patchIndex;

	for( int j = 0; j < ImageDimension; ++j)
	  {
		patchIndex[ j ] = this->m_patchSeedPoints( i, j );
	  }
  typename  InputImageType::ConstPointer input  = this->GetInput();
  typedef typename itk::ConstNeighborhoodIterator< InputImageType > IteratorType;
	typename IteratorType::RadiusType radius;
	radius.Fill( this->m_PatchRadius );
	IteratorType Iterator( radius, input, input->GetRequestedRegion() );
	InputIndexType patchCenterIndex;
	patchCenterIndex.Fill( this->m_PatchRadius ); // for finding indices within sphere, pick point far enough away from edges
	Iterator.SetLocation( patchCenterIndex );

	// get indices within N-d sphere
	for( int ii = 0; ii < Iterator.Size(); ++ii)
	{
		InputIndexType index = Iterator.GetIndex( ii );
		RealType distanceFromPatchCenter = 0.0;
		for( int jj = 0; jj < ImageDimension; ++jj)
		{
			distanceFromPatchCenter +=
					( index[jj] - patchCenterIndex[jj] ) *
					( index[jj] - patchCenterIndex[jj] );
		}
		distanceFromPatchCenter = sqrt( distanceFromPatchCenter );
		if( distanceFromPatchCenter <= this->m_PatchRadius )
		{
			this->m_IndicesWithinSphere.push_back( ii );
			this->m_weights.push_back( 1.0 );
		}
	}
  if ( this->m_Verbose ) {
  	std::cout << "Iterator.Size() is " << Iterator.Size() << std::endl;
	  std::cout << "IndicesWithinSphere.size() is " << this->m_IndicesWithinSphere.size() << std::endl;
    }
	// populate matrix with patch values from points in image
	this->m_vectorizedSamplePatchMatrix.set_size(
			this->m_NumberOfSamplePatches , this->m_IndicesWithinSphere.size() );
	this->m_vectorizedSamplePatchMatrix.fill( 0 );
	for( int i = 0; i < this->m_NumberOfSamplePatches ; ++i)
	  {
		for( int j = 0; j < ImageDimension; ++j)
		  {
			patchCenterIndex[ j ] = this->m_patchSeedPoints( i, j );
		  }
		Iterator.SetLocation( patchCenterIndex );
		// get indices within N-d sphere
		for( int j = 0; j < this->m_IndicesWithinSphere.size(); ++j)
		  {
			this->m_vectorizedSamplePatchMatrix( i, j ) =
					Iterator.GetPixel( this->m_IndicesWithinSphere[ j ] );
		  }
		// mean-center all patches
		if( this->m_MeanCenterPatches ) {
			this->m_vectorizedSamplePatchMatrix.set_row(i, this->m_vectorizedSamplePatchMatrix.get_row(i) -
					this->m_vectorizedSamplePatchMatrix.get_row(i).mean());

		}
	}
}

template<typename TInputImage, typename TOutputImage>
void RIPMMARCImageFilter<TInputImage, TOutputImage>
::ExtractAllPatches()
{
  const MaskImageType* mask = this->GetMaskImage();
  typename  InputImageType::ConstPointer inputImage  = this->GetInput();
  // get indices of points within mask
	IndexType patchIndex;
	std::vector< IndexType > nonZeroMaskIndices;
  itk::ImageRegionConstIterator<MaskImageType> maskImageIterator( mask,
                                            mask->GetLargestPossibleRegion() );
	long unsigned int maskImagePointIter = 0;
	for(maskImageIterator.GoToBegin(); !maskImageIterator.IsAtEnd(); ++maskImageIterator)
	{
		if( maskImageIterator.Get() >= 1 ) // threshold at 1
		{
			nonZeroMaskIndices.push_back( maskImageIterator.GetIndex() );
			maskImagePointIter++;
		}
	}
	this->m_numberOfVoxelsWithinMask = maskImagePointIter;
	if ( this->m_Verbose ) std::cout << "Number of points within mask is " << this->m_numberOfVoxelsWithinMask << std::endl;

	this->m_PatchesForAllPointsWithinMask.set_size(
			this->m_IndicesWithinSphere.size(),  this->m_numberOfVoxelsWithinMask);
	if( this->m_Verbose )
	{
		std::cout << "PatchesForAllPointsWithinMask is " << this->m_PatchesForAllPointsWithinMask.rows() << "x" <<
				this->m_PatchesForAllPointsWithinMask.columns() << "." << std::endl;
	}
	// extract patches
	typedef typename itk::ConstNeighborhoodIterator< InputImageType > IteratorType;
	typename IteratorType::RadiusType radius;
	radius.Fill( this->m_PatchRadius );
	IteratorType iterator( radius, inputImage,
			inputImage->GetRequestedRegion() );
	this->m_PatchesForAllPointsWithinMask.fill( 0 );
	for( long unsigned int i = 0; i < this->m_numberOfVoxelsWithinMask; ++i)
	{
		patchIndex = nonZeroMaskIndices[ i ];
		iterator.SetLocation( patchIndex );
		// get indices within N-d sphere
		for( int j = 0; j < this->m_IndicesWithinSphere.size(); ++j)
		{
			this->m_PatchesForAllPointsWithinMask( j, i ) =
        iterator.GetPixel( this->m_IndicesWithinSphere[ j ] );
		}
		// mean-center
		if( this->m_MeanCenterPatches ) {
			this->m_PatchesForAllPointsWithinMask.set_column(i,
					this->m_PatchesForAllPointsWithinMask.get_column(i) -
					this->m_PatchesForAllPointsWithinMask.get_column(i).mean());
		}
	}
	if( this->m_Verbose ) std::cout << "Recorded patches for all points." << std::endl;
}

template<typename TInputImage, typename TOutputImage>
void RIPMMARCImageFilter<TInputImage, TOutputImage>
::LearnEigenPatches()
{
  if ( this->m_Verbose )
    std::cout << "Learn eigen patches with TargetVarianceExplained " <<
      this->m_TargetVarianceExplained << std::endl;
  vnl_svd< RealValueType > svd( this->m_vectorizedSamplePatchMatrix );
	vnlMatrixType patchEigenvectors = svd.V();
  RealType sumOfEigenvalues = 0.0;
  for( int i = 0; i < svd.rank(); i++)
    {
    sumOfEigenvalues += svd.W(i, i);
    }
  RealType partialSumOfEigenvalues = 0.0;
  RealType percentVarianceExplained = 0.0;
	unsigned int  i = 0;
	if ( this->m_TargetVarianceExplained < 1 ) // FIXME
	  {
		while( ( percentVarianceExplained <= this->m_TargetVarianceExplained ) && ( i < svd.rank() ) )
		  {
			partialSumOfEigenvalues += svd.W(i, i);
			percentVarianceExplained = partialSumOfEigenvalues /
											  sumOfEigenvalues;
			i++;
		  }
		unsigned int numberOfSignificantEigenvectors = i;
		if  ( this->m_Verbose )
		  {
			std::cout << "It took " << numberOfSignificantEigenvectors << " eigenvectors to reach " <<
					this->m_TargetVarianceExplained * 100 << "% variance explained." << std::endl;
		  }
	} else {
		i = static_cast< unsigned int >( this->m_TargetVarianceExplained  );
    unsigned int numberOfSignificantEigenvectors = i;
    unsigned int j = 0;
    while(  ( j < i ) && ( j < svd.rank() ) )
		  {
			partialSumOfEigenvalues += svd.W(j, j);
			percentVarianceExplained = partialSumOfEigenvalues /
											  sumOfEigenvalues;
			j++;
		  }
		if  ( this->m_Verbose )
		  {
			std::cout << "With " << numberOfSignificantEigenvectors << " eigenvectors, we have " <<
					percentVarianceExplained * 100 << "% variance explained." << std::endl;
		  }
	}
  this->m_AchievedVarianceExplained = percentVarianceExplained;
	this->m_SignificantPatchEigenvectors.set_size( patchEigenvectors.rows(), i);
	this->m_SignificantPatchEigenvectors = patchEigenvectors.get_n_columns(0, i);
}

template<typename TInputImage, typename TOutputImage>
void RIPMMARCImageFilter<TInputImage, TOutputImage>
::ReorientSamplePatches()
{
  typedef InputImageType ImageType;
  RealType gradientSigma =                          1.0;
  typename NeighborhoodIteratorType::RadiusType radius;

  GradientImageFilterPointer    movingGradientFilter = GradientImageFilterType::New();
  GradientImageFilterPointer    fixedGradientFilter =  GradientImageFilterType::New();
  InterpPointer interp1 = ScalarInterpolatorType::New();

  radius.Fill( this->m_PatchRadius );
  for( int ii = 0; ii < ImageDimension; ii++)
    {
    beginningOfSphereRegion[ii] = this->m_PaddingVoxels + this->m_PatchRadius;
    sizeOfSphereRegion[ii]      = this->m_PatchRadius * 2 + 1;
    }
  sphereRegion.SetSize( sizeOfSphereRegion );
  sphereRegion.SetIndex( beginningOfSphereRegion );

  typename ImageType::Pointer eigenvecMaskImage;
  eigenvecMaskImage = this->GenerateMaskImageFromPatch( );
  //NeighborhoodIteratorType regionIterator()
  NeighborhoodIteratorType fixedIterator(radius, this->m_CanonicalFrame, sphereRegion);
  // compute gradient of canonical frame once, outside the loop
  fixedGradientFilter->SetInput( this->m_CanonicalFrame );
  fixedGradientFilter->SetSigma( gradientSigma );
  fixedGradientFilter->Update();
  typename GradientImageType::Pointer fixedGradientImage = fixedGradientFilter->GetOutput();

  if ( this->m_Verbose )
    std::cout << "vectorizedSamplePatchMatrix is " << this->m_vectorizedSamplePatchMatrix.rows() <<
      "x" << this->m_vectorizedSamplePatchMatrix.columns() << std::endl;
  for( long int ii = 0; ii < this->m_vectorizedSamplePatchMatrix.rows(); ii++)
    {
    vnl_vector< RealValueType > vectorizedPatch =
        this->m_vectorizedSamplePatchMatrix.get_row(ii);
    typename ImageType::Pointer movingImage = this->ConvertVectorToSpatialImage(
        vectorizedPatch, eigenvecMaskImage );
    NeighborhoodIteratorType movingIterator(radius, movingImage, sphereRegion);
    movingGradientFilter->SetInput(movingImage);
    movingGradientFilter->SetSigma(gradientSigma);
    movingGradientFilter->Update();
    interp1->SetInputImage(movingImage);
    typename GradientImageType::Pointer movingGradientImage = movingGradientFilter->GetOutput();
    vnl_vector< RealValueType > rotatedPatchAsVector =
        this->ReorientPatchToReferenceFrame(
            fixedIterator, movingIterator, eigenvecMaskImage,
            fixedGradientImage,
            movingGradientImage,
            interp1 );
    this->m_vectorizedSamplePatchMatrix.set_row( ii, rotatedPatchAsVector );
    }

}

template<typename TInputImage, typename TOutputImage>
void RIPMMARCImageFilter<TInputImage, TOutputImage>
::ReorientAllPatches()
{
  typedef InputImageType ImageType;
  float gradientSigma =                          1.0;
  typename NeighborhoodIteratorType::RadiusType radius;

  GradientImageFilterPointer    movingGradientFilter = GradientImageFilterType::New();
  GradientImageFilterPointer    fixedGradientFilter =  GradientImageFilterType::New();
  InterpPointer interp1 = ScalarInterpolatorType::New();

  radius.Fill( this->m_PatchRadius );
  for( int ii = 0; ii < ImageDimension; ii++)
    {
    beginningOfSphereRegion[ii] = this->m_PaddingVoxels + this->m_PatchRadius;
    sizeOfSphereRegion[ii]      = this->m_PatchRadius * 2 + 1;
    }
  sphereRegion.SetSize( sizeOfSphereRegion );
  sphereRegion.SetIndex( beginningOfSphereRegion );

  typename ImageType::Pointer eigenvecMaskImage;
  eigenvecMaskImage = this->GenerateMaskImageFromPatch( );
  NeighborhoodIteratorType fixedIterator( radius, this->m_CanonicalFrame, sphereRegion);
  // compute gradient of canonical frame once, outside the loop
  fixedGradientFilter->SetInput( this->m_CanonicalFrame );
  fixedGradientFilter->SetSigma( gradientSigma );
  fixedGradientFilter->Update();
  typename GradientImageType::Pointer fixedGradientImage = fixedGradientFilter->GetOutput();

  for( long int ii = 0; ii < this->m_PatchesForAllPointsWithinMask.columns(); ii++)
    {
    vnl_vector< RealValueType > vectorizedPatch =
        this->m_PatchesForAllPointsWithinMask.get_column( ii );
    typename ImageType::Pointer movingImage =
      this->ConvertVectorToSpatialImage( vectorizedPatch, eigenvecMaskImage);
    NeighborhoodIteratorType movingIterator( radius, movingImage, sphereRegion );
    movingGradientFilter->SetInput( movingImage );
    movingGradientFilter->SetSigma( gradientSigma );
    movingGradientFilter->Update();
    interp1->SetInputImage( movingImage );
    typename GradientImageType::Pointer movingGradientImage = movingGradientFilter->GetOutput();
    vnl_vector< RealValueType > rotatedPatchAsVector =
        this->ReorientPatchToReferenceFrame(
            fixedIterator, movingIterator, eigenvecMaskImage,
            fixedGradientImage,
            movingGradientImage,
            interp1 );
    this->m_PatchesForAllPointsWithinMask.set_column( ii, rotatedPatchAsVector );
  }

}

template<typename TInputImage, typename TOutputImage>
void RIPMMARCImageFilter<TInputImage, TOutputImage>
::ProjectOnEigenPatches()
{
  // perform regression from eigenvectors to images
  // Ax = b, whProjectOnEigenPatchesere A is eigenvector matrix (number of indices
  // within patch x number of eigenvectors), x is coefficients
  // (number of eigenvectors x 1), b is patch values for a given index
  // (number of indices within patch x 1).
  // output, eigenvectorCoefficients, is then number of eigenvectors
  // x number of patches ('x' solutions for all patches).
  if ( this->m_Verbose ) std::cout << "Computing regression." << std::endl;
  this->m_EigenvectorCoefficients.set_size( this->m_SignificantPatchEigenvectors.columns(),
    this->m_numberOfVoxelsWithinMask );
  this->m_EigenvectorCoefficients.fill( 0 );
  vnl_svd< RealValueType > RegressionSVD( this->m_SignificantPatchEigenvectors );
  //  EigenvectorCoefficients =  RegressionSVD.solve(PatchesForAllPointsWithinMask);
  //  not feasible for large matrices
  for( long unsigned int i = 0; i < this->m_numberOfVoxelsWithinMask; ++i )
    {
    VectorType PatchOfInterest =
        this->m_PatchesForAllPointsWithinMask.get_column( i );
    VectorType x( this->m_SignificantPatchEigenvectors.columns() );
    x.fill( 0 );
    x = RegressionSVD.solve( PatchOfInterest );
    this->m_EigenvectorCoefficients.set_column( i, x );
    }
  vnl_matrix< RealValueType > reconstructedPatches =
      this->m_SignificantPatchEigenvectors * this->m_EigenvectorCoefficients;
  vnl_matrix< RealValueType > error =
      reconstructedPatches - this->m_PatchesForAllPointsWithinMask;
  vnl_vector< RealValueType > percentError(error.columns() );
  for( int i = 0; i < error.columns(); ++i)
    {
    percentError(i) = error.get_column(i).two_norm() /
        (this->m_PatchesForAllPointsWithinMask.get_column(i).two_norm() + 1e-10);
    }
  if( this->m_Verbose )
    {
    std::cout << "Average percent error is " << percentError.mean() * 100 << "%, with max of " <<
        percentError.max_value() * 100 << "%." <<  std::endl;
    }
}


template<typename TInputImage, typename TOutputImage>
void RIPMMARCImageFilter<TInputImage, TOutputImage>
::GenerateData(  )
{
// FIXME - the logic below could be cleaned up e.g. do we really need sample
// patches if we already have a basis?
  unsigned int canonicalEvecIndex = 1;
  if ( this->m_MeanCenterPatches ) canonicalEvecIndex = 0;
  this->GetSamplePatchLocations( ); // identify points from random mask
  this->ExtractSamplePatches( );     // convert sample points to the matrix
	if ( this->m_LearnPatchBasis )  // determines if we are learning or not
	  {
  	this->LearnEigenPatches( );  // learn the patches
    // the SECOND eigenvector is canonical--1st is constant
    this->m_CanonicalFrame = this->GetCanonicalFrameK( canonicalEvecIndex );
	  }
  else
    {
    // use existing significantPatchEigenvectors as reference
    // check the eigenpatches have the correct dimensionality then
    // just apply the learning, given the eigenpatch
    // FIXME - need to implement these checks
	  }
	this->ExtractAllPatches( );
	// because all patches are reoriented to the first (non-rotationally invariant)
	// eigenpatch, we must learn the eigenpatches even if we will in the end use
	// rotationally invariant features.
	if ( this->m_RotationInvariant )
	  {
    this->ReorientSamplePatches();
		this->ReorientAllPatches();
		if ( this->m_LearnPatchBasis )
      {
			this->LearnEigenPatches(); // learn the patches after reorientation
      // the SECOND eigenvector is canonical--1st is constant if we do not mean center
      this->m_CanonicalFrame = this->GetCanonicalFrameK( canonicalEvecIndex );
      }
	  }
  this->ProjectOnEigenPatches( ); // in practice, we might prefer this in R
  this->SetNthOutput( 0, this->GetCanonicalFrame() );
}

template<typename TInputImage, typename TOutputImage>
void
RIPMMARCImageFilter<TInputImage, TOutputImage>
::PrintSelf( std::ostream &os, Indent indent ) const
{
  Superclass::PrintSelf( os, indent );

  if( this->m_RotationInvariant )
    {
    os << indent << "Using RotationInvariant model." << std::endl;
    }
  else
    {
    os << indent << "Using non-RotationInvariant model." << std::endl;
    }

  if( this->m_MeanCenterPatches )
    {
    os << indent << "We will MeanCenterPatches." << std::endl;
    }
  else
    {
    os << indent << "Do not MeanCenterPatches." << std::endl;
    }

  os << indent << "PatchRadius = " << this->m_PatchRadius << std::endl;

  os << indent << "TargetVarianceExplained = " << this->m_TargetVarianceExplained << std::endl;
}







} // end namespace itk

#endif
