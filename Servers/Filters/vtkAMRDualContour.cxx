/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkAMRDualContour.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkAMRDualContour.h"
#include "vtkAMRDualGridHelper.h"

#include "vtkstd/vector"

// Pipeline & VTK 
#include "vtkMarchingCubesCases.h"
#include "vtkStreamingDemandDrivenPipeline.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkMultiProcessController.h"
#include "vtkObject.h"
#include "vtkObjectFactory.h"
// PV interface
#include "vtkCallbackCommand.h"
#include "vtkMath.h"
#include "vtkDataArraySelection.h"
// Data sets
#include "vtkDataSet.h"
#include "vtkCellData.h"
#include "vtkPointData.h"
#include "vtkPolyData.h"
#include "vtkImageData.h"
#include "vtkUniformGrid.h"
#include "vtkUnstructuredGrid.h"
#include "vtkMultiBlockDataSet.h"
#include "vtkHierarchicalBoxDataSet.h"
#include "vtkMultiPieceDataSet.h"
#include "vtkAMRBox.h"
#include "vtkCellArray.h"
#include "vtkUnsignedCharArray.h"
#include <math.h>
#include <ctime>


vtkCxxRevisionMacro(vtkAMRDualContour, "1.4");
vtkStandardNewMacro(vtkAMRDualContour);




// It is working but we have some missing features.
// 1: Make a Clip Filter
// 2: Merge points.
// 3: Change degenerate quads to tris or remove.
// 4: Copy Attributes from input to output.

//============================================================================
// Used separately for each block.  This is the typical 3 edge per voxel 
// lookup.  We do not need to worry about degeneracy because it just causes
// some edges never to be used.  Locator still works.
class vtkAMRDualContourEdgeLocator
{
public:
  vtkAMRDualContourEdgeLocator();
  ~vtkAMRDualContourEdgeLocator();

  // Description:
  // Dims are for the dual cells including ghost layers. 
  // This is called multiple times to prepare for a new block.
  void Initialize(int xDualCellDim, int yDualCellDim, int zDualCellDim);
  
  // Description:
  // Lookup and seting uses this pointer. Using a pointer keeps
  // the contour filter from having to lookup a point and second
  // adding a point (both are very similar).
  // The edge index uses VTK voxel edge indexing scheme.
  vtkIdType* GetEdgePointer(int xCell, int yCell, int zCell, int edgeIdx);

  // Description:
  // Same but for corners not edges.  This uses my binary indexing of corners.
  // 0:(000) 1:(100) 2:(010) 3:(110) 4:(001) 5:(101)....
  vtkIdType* GetCornerPointer(int xCell, int yCell, int zCell, int cornerIdx);

private:

  int DualCellDimensions[3];
  // Increments for translating 3d to 1d.  XIncrement = 1;
  int YIncrement;
  int ZIncrement;
  int ArrayLength;
  // I am just going to use 3 separate arrays for edges on the 3 axes.
  vtkIdType* XEdges;
  vtkIdType* YEdges;
  vtkIdType* ZEdges;
  vtkIdType* Corners;
};
//----------------------------------------------------------------------------
vtkAMRDualContourEdgeLocator::vtkAMRDualContourEdgeLocator()
{
  this->DualCellDimensions[0] = 0;
  this->DualCellDimensions[1] = 0;
  this->DualCellDimensions[2] = 0;
  this->YIncrement = this->ZIncrement = 0;
  this->ArrayLength = 0;
  this->XEdges = this->YEdges = this->ZEdges = 0;
  this->Corners = 0;
}
//----------------------------------------------------------------------------
vtkAMRDualContourEdgeLocator::~vtkAMRDualContourEdgeLocator()
{
  this->Initialize(0,0,0);
}
//----------------------------------------------------------------------------
void vtkAMRDualContourEdgeLocator::Initialize(
  int xDualCellDim, 
  int yDualCellDim, 
  int zDualCellDim)
{
  if (xDualCellDim != this->DualCellDimensions[0] 
          || yDualCellDim != this->DualCellDimensions[1] 
          || zDualCellDim != this->DualCellDimensions[2])
    {
    if (this->XEdges)
      { // They are all allocated at once, so separate checks are not necessary.
      delete [] this->XEdges;
      delete [] this->YEdges;
      delete [] this->ZEdges;
      delete [] this->Corners;
      }
    if (xDualCellDim > 0 && yDualCellDim > 0 && zDualCellDim > 0)
      {
      this->DualCellDimensions[0] = xDualCellDim;
      this->DualCellDimensions[1] = yDualCellDim;
      this->DualCellDimensions[2] = zDualCellDim;
      // We have to increase dimensions by one to capture edges on the max faces.
      this->YIncrement = this->DualCellDimensions[0]+1;
      this->ZIncrement = this->YIncrement * (this->DualCellDimensions[1]+1);
      this->ArrayLength = this->ZIncrement * (this->DualCellDimensions[2]+1);
      this->XEdges = new vtkIdType[this->ArrayLength];
      this->YEdges = new vtkIdType[this->ArrayLength];
      this->ZEdges = new vtkIdType[this->ArrayLength];
      this->Corners = new vtkIdType[this->ArrayLength];
      }
    else
      {
      this->YIncrement = this->ZIncrement = 0;
      this->ArrayLength = 0;
      this->DualCellDimensions[0] = 0;
      this->DualCellDimensions[1] = 0;
      this->DualCellDimensions[2] = 0;
      }
    }
      
  for (int idx = 0; idx < this->ArrayLength; ++idx)
    {
    this->XEdges[idx] = this->YEdges[idx] = this->ZEdges[idx] = -1;
    this->Corners[idx] = -1;
    }
}
//----------------------------------------------------------------------------
// No bounds checking.
vtkIdType* vtkAMRDualContourEdgeLocator::GetEdgePointer(
  int xCell, int yCell, int zCell, 
  int edgeIdx)
{
  switch (edgeIdx)
    {
    case 0:  // edge 0   X00
      return this->XEdges + (xCell+(yCell*this->YIncrement)+(zCell*this->ZIncrement));
      break;
    case 1:  // edge 1   1Y0
      return this->YEdges + (++xCell+(yCell*this->YIncrement)+(zCell*this->ZIncrement));
      break;
    case 2:  // edge 2   X10
      return this->XEdges + (xCell+(++yCell*this->YIncrement)+(zCell*this->ZIncrement));
      break;
    case 3:  // edge 3   0Y0
      return this->YEdges + (xCell+(yCell*this->YIncrement)+(zCell*this->ZIncrement));
      break;
    case 4:  // edge 4   X01
      return this->XEdges + (xCell+(yCell*this->YIncrement)+(++zCell*this->ZIncrement));
      break;
    case 5:  // edge 5   1Y1
      return this->YEdges + (++xCell+(yCell*this->YIncrement)+(++zCell*this->ZIncrement));
      break;
    case 6:  // edge 6   X11
      return this->XEdges + (xCell+(++yCell*this->YIncrement)+(++zCell*this->ZIncrement));
      break;
    case 7:  // edge 7   0Y1
      return this->YEdges + (xCell+(yCell*this->YIncrement)+(++zCell*this->ZIncrement));
      break;
    case 8:  // edge 8   00Z
      return this->ZEdges + (xCell+(yCell*this->YIncrement)+(zCell*this->ZIncrement));
      break;
    case 9:  // edge 9   10Z
      return this->ZEdges + (++xCell+(yCell*this->YIncrement)+(zCell*this->ZIncrement));
      break;
    case 10: // edge 10  01Z
      return this->ZEdges + (xCell+(++yCell*this->YIncrement)+(zCell*this->ZIncrement));
      break;
    case 11: // edge 11  11Z
      return this->ZEdges + (++xCell+(++yCell*this->YIncrement)+(zCell*this->ZIncrement));
      break;
    default:
      assert( 0 && "Invalid edge index." );
      return 0;
    }
}

//----------------------------------------------------------------------------
// No bounds checking.
vtkIdType* vtkAMRDualContourEdgeLocator::GetCornerPointer(
  int xCell, int yCell, int zCell, 
  int cornerIdx)
{
  xCell += cornerIdx & 1;
  yCell += (cornerIdx & 2) >> 1;
  zCell += (cornerIdx & 4) >> 2;
  
  return this->Corners + (xCell+(yCell*this->YIncrement)+(zCell*this->ZIncrement));
}
  

//============================================================================
//----------------------------------------------------------------------------
// Description:
// Construct object with initial range (0,1) and single contour value
// of 0.0. ComputeNormal is on, ComputeGradients is off and ComputeScalars is on.
vtkAMRDualContour::vtkAMRDualContour()
{
  this->IsoValue = 100.0;
  
  this->EnableDegenerateCells = 1;
  this->EnableCapping = 1;
  this->EnableMultiProcessCommunication = 1;
  this->Controller = vtkMultiProcessController::GetGlobalController();

  // Pipeline
  this->SetNumberOfOutputPorts(1);
  

  this->BlockIdCellArray = 0;
  this->Helper = 0;

  this->BlockLocator = new vtkAMRDualContourEdgeLocator;
}

//----------------------------------------------------------------------------
vtkAMRDualContour::~vtkAMRDualContour()
{
  delete this->BlockLocator;
  this->BlockLocator = 0;
}

//----------------------------------------------------------------------------
void vtkAMRDualContour::PrintSelf(ostream& os, vtkIndent indent)
{
  // TODO print state
  this->Superclass::PrintSelf(os,indent);

  os << indent << "IsoValue: " << this->IsoValue << endl;
}

//----------------------------------------------------------------------------
int vtkAMRDualContour::FillInputPortInformation(int /*port*/,
                                                    vtkInformation *info)
{
  info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkDataObject");

  return 1;
}

//----------------------------------------------------------------------------
int vtkAMRDualContour::FillOutputPortInformation(int port, vtkInformation *info)
{
  switch (port)
    {
    case 0:
      info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkMultiBlockDataSet");
      break;
    default:
      assert( 0 && "Invalid output port." );
      break;
    }

  return 1;
}

//----------------------------------------------------------------------------
int vtkAMRDualContour::RequestData(
  vtkInformation *vtkNotUsed(request),
  vtkInformationVector **inputVector,
  vtkInformationVector *outputVector)
{
  // get the data set which we are to process
  vtkInformation *inInfo = inputVector[0]->GetInformationObject(0);
  vtkHierarchicalBoxDataSet *hbdsInput=vtkHierarchicalBoxDataSet::SafeDownCast(
    inInfo->Get(vtkDataObject::DATA_OBJECT()));

  // Get the outputs
  // 0
  vtkInformation *outInfo;
  outInfo = outputVector->GetInformationObject(0);
  vtkMultiBlockDataSet *mbdsOutput0 =
    vtkMultiBlockDataSet::SafeDownCast(outInfo->Get(vtkDataObject::DATA_OBJECT()));

  mbdsOutput0->SetNumberOfBlocks(1);
  vtkMultiPieceDataSet *mpds=vtkMultiPieceDataSet::New();
  mbdsOutput0->SetBlock(0,mpds);

  mpds->SetNumberOfPieces(0);

  if ( hbdsInput==0 )
    {
    // Do not deal with rectilinear grid
    vtkErrorMacro("This filter requires a vtkHierarchicalBoxDataSet on its input.");
    return 0;
    }

  // This is a lot to go through to get the name of the array to process.
  vtkInformationVector *inArrayVec = this->GetInformation()->Get(INPUT_ARRAYS_TO_PROCESS());
  if (!inArrayVec)
    {
    vtkErrorMacro("Problem finding array to process");
    return 0;
    }
  vtkInformation *inArrayInfo = inArrayVec->GetInformationObject(0);
  if (!inArrayInfo)
    {
    vtkErrorMacro("Problem getting name of array to process.");
    return 0;
    }
  if ( ! inArrayInfo->Has(vtkDataObject::FIELD_NAME()))
    {
    vtkErrorMacro("Missing field name.");
    return 0;
    }
  const char *arrayNameToProcess = inArrayInfo->Get(vtkDataObject::FIELD_NAME());      


  this->Helper = vtkAMRDualGridHelper::New();
  this->Helper->SetEnableDegenerateCells(this->EnableDegenerateCells);
  this->Helper->SetEnableMultiProcessCommunication(this->EnableMultiProcessCommunication);
  this->Helper->Initialize(hbdsInput, arrayNameToProcess);

  vtkPolyData* mesh = vtkPolyData::New();
  this->Points = vtkPoints::New();
  this->Faces = vtkCellArray::New();
  mesh->SetPoints(this->Points);
  mesh->SetPolys(this->Faces);
  mpds->SetPiece(0, mesh);
  
  // For debugging.
  this->BlockIdCellArray = vtkIntArray::New();
  this->BlockIdCellArray->SetName("BlockIds");
  mesh->GetCellData()->AddArray(this->BlockIdCellArray);

  // Loop through blocks
  int numLevels = hbdsInput->GetNumberOfLevels();
  int numBlocks;
  int blockId;

  // Add each block.
  for (int level = 0; level < numLevels; ++level)
    {
    numBlocks = this->Helper->GetNumberOfBlocksInLevel(level);
    for (blockId = 0; blockId < numBlocks; ++blockId)
      {
      vtkAMRDualGridHelperBlock* block = this->Helper->GetBlock(level, blockId);
      this->ProcessBlock(block, blockId);
      }
    }

  this->BlockIdCellArray->Delete();
  this->BlockIdCellArray = 0;

  mesh->Delete();
  this->Points->Delete();
  this->Points = 0;
  this->Faces->Delete();
  this->Faces = 0;

  mpds->Delete();
  this->Helper->Delete();
  this->Helper = 0;

  return 1;
}

//----------------------------------------------------------------------------
// The only data specific stuff we need to do for the contour.
template <class T>
void vtkDualGridContourExtractCornerValues(
  T* ptr, int yInc, int zInc,
  double values[8]) 
{
  // Because of the marching cubes case table, I am stuck with
  // VTK's indexing of corners.
  values[0] = (double)(*ptr);
  values[1] = (double)(ptr[1]);
  values[2] = (double)(ptr[1+yInc]);
  values[3] = (double)(ptr[yInc]);
  values[4] = (double)(ptr[zInc]);
  values[5] = (double)(ptr[1+zInc]);
  values[6] = (double)(ptr[1+yInc+zInc]);
  values[7] = (double)(ptr[yInc+zInc]);
}
//----------------------------------------------------------------------------
void vtkAMRDualContour::ProcessBlock(vtkAMRDualGridHelperBlock* block,
                                     int blockId)
{
  vtkImageData* image = block->Image;
  if (image == 0)
    { // Remote blocks are only to setup local block bit flags.
    return;
    }
  vtkDataArray *volumeFractionArray = this->GetInputArrayToProcess(0, image);
  void* volumeFractionPtr = volumeFractionArray->GetVoidPointer(0);
  double  origin[3];
  double* spacing;
  int     extent[6];
  
  // Get the origin and point extent of the dual grid (with ghost level).
  // This is the same as the cell extent of the original grid (with ghosts).
  image->GetExtent(extent);
  --extent[1];
  --extent[3];
  --extent[5];

  // Locator merges points in this block.
  // Input the dimensions of the dual cells with ghosts.
  this->BlockLocator->Initialize(extent[1]-extent[0], extent[3]-extent[2], extent[5]-extent[4]);

  image->GetOrigin(origin);
  spacing = image->GetSpacing();
  // Dual cells are shifted half a pixel.
  origin[0] += 0.5 * spacing[0];
  origin[1] += 0.5 * spacing[1];
  origin[2] += 0.5 * spacing[2];
  
  // We deal with the various data types by copying the corner values
  // into a double array.  We have to cast anyway to compute the case.
  double cornerValues[8];

  // The templated function needs the increments for pointers 
  // cast to the correct datatype.
  int yInc = (extent[1]-extent[0]+1);
  int zInc = yInc * (extent[3]-extent[2]+1);
  // Use void pointers to march through the volume before we cast.
  int dataType = volumeFractionArray->GetDataType();
  int xVoidInc = volumeFractionArray->GetDataTypeSize();
  int yVoidInc = xVoidInc * yInc;
  int zVoidInc = xVoidInc * zInc;

  int cubeIndex;

  // Loop over all the cells in the dual grid.
  int x, y, z;
  // These are needed to handle the cropped boundary cells.
  double ox, oy, oz;
  double sx, sy, sz;
  int xMax = extent[1]-1;
  int yMax = extent[3]-1;
  int zMax = extent[5]-1;
  //-
  unsigned char* zPtr = (unsigned char*)(volumeFractionPtr);
  unsigned char* yPtr;
  unsigned char* xPtr;
  for (z = extent[4]; z < extent[5]; ++z)
    {
    int nz = 1;
    if (z == extent[4]) {nz = 0;}
    else if (z == zMax) {nz = 2;}
    sz = spacing[2];
    oz = origin[2] + (double)(z)*sz;
    yPtr = zPtr;
    for (y = extent[2]; y < extent[3]; ++y)
      {
      int ny = 1;
      if (y == extent[2]) {ny = 0;}
      else if (y == yMax) {ny = 2;}
      sy = spacing[1];
      oy = origin[1] + (double)(y)*sy;
      xPtr = yPtr;
      for (x = extent[0]; x < extent[1]; ++x)
        {
        int nx = 1;
        if (x == extent[0]) {nx = 0;}
        else if (x == xMax) {nx = 2;}
        sx = spacing[0];
        ox = origin[0] + (double)(x)*sx;
        // Skip the cell if a neighbor is already processing it.
        if ( (block->RegionBits[nx][ny][nz] & vtkAMRRegionBitOwner) )
          {
          // Get the corner values as doubles
          switch (dataType)
            {
            vtkTemplateMacro(vtkDualGridContourExtractCornerValues(
                             (VTK_TT *)(xPtr), yInc, zInc,
                             cornerValues));
            default:
              vtkGenericWarningMacro("Execute: Unknown ScalarType");
            }
          // compute the case index
          cubeIndex = 0;
          if (cornerValues[0] > this->IsoValue)
            {
            cubeIndex += 1;
            }
          if (cornerValues[1] > this->IsoValue)
            {
            cubeIndex += 2;
            }
          if (cornerValues[2] > this->IsoValue)
            {
            cubeIndex += 4;
            }
          if (cornerValues[3] > this->IsoValue)
            {
            cubeIndex += 8;
            }
          if (cornerValues[4] > this->IsoValue)
            {
            cubeIndex += 16;
            }
          if (cornerValues[5] > this->IsoValue)
            {
            cubeIndex += 32;
            }
          if (cornerValues[6] > this->IsoValue)
            {
            cubeIndex += 64;
            }
          if (cornerValues[7] > this->IsoValue)
            {
            cubeIndex += 128;
            }
          this->ProcessDualCell(block, blockId,
                                cubeIndex, x, y, z,
                                cornerValues);
          }
        xPtr += xVoidInc;
        }
      yPtr += yVoidInc;
      }
    zPtr += zVoidInc;
    }
}

static int vtkAMRDualIsoEdgeToPointsTable[12][2] =
  { {0,1}, {1,3}, {2,3}, {0,2},
    {4,5}, {5,7}, {6,7}, {4,6},
    {0,4}, {1,5}, {2,6}, {3,7}};
static int vtkAMRDualIsoEdgeToVTKPointsTable[12][2] =
  { {0,1}, {1,2}, {3,2}, {0,3},
    {4,5}, {5,6}, {7,6}, {4,7},
    {0,4}, {1,5}, {3,7}, {2,6}};





// Generic table for clipping a square.
// We can have two polygons.
// Polygons are separated by -1, and terminated by -2.
// 0-3 are the corners of the square (00,10,11,01).
// 4-7 are for points on the edges (4:(00-10),5:(10-11),6:(11-01),7:(01-00)
static int vtkAMRDualIsoCappingTable[16][8] =
  { {-2,0,0,0,0,0,0,0},  //(0000)
    {0,4,7,-2,0,0,0,0},  //(1000)
    {1,5,4,-2,0,0,0,0},  //(0100)
    {0,1,5,7,-2,0,0,0},  //(1100)
    {2,6,5,-2,0,0,0,0},  //(0010)
    {0,4,7,-1,2,6,5,-2}, //(1010)
    {1,2,6,4,-2,0,0,0},  //(0110)
    {0,1,2,6,7,-2,0,0},  //(1110)
    {3,7,6,-2,0,0,0,0},  //(0001)
    {3,0,4,6,-2,0,0,0},  //(1001)
    {1,5,4,-1,3,7,6,-2}, //(0101)
    {3,0,1,5,6,-2,0,0},  //(1101)
    {2,3,7,5,-2,0,0,0},  //(0011)
    {2,3,0,4,5,-2,0,0},  //(1011)
    {1,2,3,7,4,-2,0,0},  //(0111)
    {0,1,2,3,-2,0,0,0}}; //(1111)

// These tables map the corners and edges from the above table
// into corners and edges for the face of a cube.
// First for map 0-3 into corners 0-7 000,100,010,110,001,101,011,111
// Edges 4-7 get mapped to standard cube edge index.
// 0:(000-100),1:(100-110),2:(110-010),3:(010-000),
static int vtkAMRDualIsoNXCapEdgeMap[8] = {0,2,6,4 ,3,10,7,8};
static int vtkAMRDualIsoPXCapEdgeMap[8] = {1,5,7,3 ,9,5,11,1};

static int vtkAMRDualIsoNYCapEdgeMap[8] = {0,4,5,1 ,8,4,9,0};
static int vtkAMRDualIsoPYCapEdgeMap[8] = {2,3,7,6 ,2,11,6,10};

static int vtkAMRDualIsoNZCapEdgeMap[8] = {0,1,3,2 ,0,1,2,3};
static int vtkAMRDualIsoPZCapEdgeMap[8] = {6,7,5,4 ,6,5,4,7};


//----------------------------------------------------------------------------
// Not implemented as optimally as we could.  It can be improved by making
// a fast path for internal cells (with no degeneracies).
void vtkAMRDualContour::ProcessDualCell(
  vtkAMRDualGridHelperBlock* block, int blockId,
  int cubeCase,
  int x, int y, int z,
  double cornerValues[8])
{
  // I am trying to exit as quick as possible if there is
  // no surface to generate.  I could also check that the index
  // is not on boundary.
  if (cubeCase == 0 || (cubeCase == 255 && block->BoundaryBits == 0))
    {
    return;
    }

  // Which boundaries does this cube/cell touch?
  unsigned char cubeBoundaryBits = 0;
  // If this cell is degenerate, then remove triangles with 2 points.
  int degenerateFlag = 0;  
  
  int nx, ny, nz; // Neighbor index [3][3][3];
  vtkIdType pointIds[6];
  vtkMarchingCubesTriangleCases *triCase, *triCases;
  EDGE_LIST  *edge;
  double k, v0, v1;
  triCases =  vtkMarchingCubesTriangleCases::GetCases();

  // Compute the spacing fro this level and one lower level;
  const double *tmp = this->Helper->GetRootSpacing();
  double spacing[3];
  double lowerSpacing[3];
  for (int ii = 0; ii < 3; ++ii)
    {
    spacing[ii] = tmp[ii] / (double)(1 << block->Level);
    lowerSpacing[ii] = 2.0 * spacing[ii];
    }
  tmp = this->Helper->GetGlobalOrigin();
  
  // Use this range to  determine which dual point index is ghost.
  int ghostDualPointIndexRange[6];
  block->Image->GetExtent(ghostDualPointIndexRange);

  ghostDualPointIndexRange[0] += block->OriginIndex[0];
  ghostDualPointIndexRange[1] += block->OriginIndex[0]-1;
  ghostDualPointIndexRange[2] += block->OriginIndex[1];
  ghostDualPointIndexRange[3] += block->OriginIndex[1]-1;
  ghostDualPointIndexRange[4] += block->OriginIndex[2];
  ghostDualPointIndexRange[5] += block->OriginIndex[2]-1;
  // Change to global index.
  int gx,gy,gz;
  gx = x + block->OriginIndex[0];
  gy = y + block->OriginIndex[1];
  gz = z + block->OriginIndex[2];

  double dx, dy, dz; // Chop cells in half at boundary.
  double cornerPoints[32]; // 4 is easier to optimize than 3.
  // Loop over the corners.
  for (int c = 0; c < 8; ++c)
    {
    // The varibles dx,dy,dz handle boundary cells. 
    // They shift point by half a pixel on the boundary.
    dx = dy = dz = 0.5;
    // Place the point in one of the 26 ghost regions.
    int px, py, pz; // Corner global xyz index.
    // CornerIndex
    px =(c & 1)?gx+1:gx;
    if (px == ghostDualPointIndexRange[0]) 
      {
      nx = 0;
      if ( (block->BoundaryBits & 1) ) 
        {
        dx = 1.0;
        cubeBoundaryBits = cubeBoundaryBits | 1;
        }
      }
    else if (px == ghostDualPointIndexRange[1]) 
      {
      nx = 2;
      if ( (block->BoundaryBits & 2) )
        {
        dx = 0.0;
        cubeBoundaryBits = cubeBoundaryBits | 2;
        }
      }
    else {nx = 1;}
    py =(c & 2)?gy+1:gy;
    if (py == ghostDualPointIndexRange[2]) 
      {
      ny = 0;
      if ( (block->BoundaryBits & 4) ) 
        {
        dy = 1.0;
        cubeBoundaryBits = cubeBoundaryBits | 4;
        }
      }
    else if (py == ghostDualPointIndexRange[3]) 
      {
      ny = 2;
      if ( (block->BoundaryBits & 8) ) 
        {
        dy = 0.0;
        cubeBoundaryBits = cubeBoundaryBits | 8;
        }
      }
    else {ny = 1;}
    pz =(c & 4)?gz+1:gz;
    if (pz == ghostDualPointIndexRange[4]) 
      {
      nz = 0;
      if ( (block->BoundaryBits & 16) )
        {
        dz = 1.0;
        cubeBoundaryBits = cubeBoundaryBits | 16;
        }
      }
    else if (pz == ghostDualPointIndexRange[5]) 
      {
      nz = 2;
      if ( (block->BoundaryBits & 32) )
        {
        dz = 0.0;
        cubeBoundaryBits = cubeBoundaryBits | 32;
        }
      }
    else {nz = 1;}
    
    if (block->RegionBits[nx][ny][nz] & vtkAMRRegionBitsDegenerateMask)
      { // point lies in lower level neighbor.
      degenerateFlag = 1;
      int levelDiff = block->RegionBits[nx][ny][nz] & vtkAMRRegionBitsDegenerateMask;
      px = px >> levelDiff;
      py = py >> levelDiff;
      pz = pz >> levelDiff;
      // Shift half a pixel to get center of cell (dual point).
      if (levelDiff == 1)
        { // This is the most common case; avoid extra multiplications.
        cornerPoints[c<<2]     = tmp[0] + lowerSpacing[0] * ((double)(px)+dx);
        cornerPoints[(c<<2)|1] = tmp[1] + lowerSpacing[1] * ((double)(py)+dy);
        cornerPoints[(c<<2)|2] = tmp[2] + lowerSpacing[2] * ((double)(pz)+dz);
        }
      else
        { // This could be the only degenerate path with a little extra cost.
        cornerPoints[c<<2]     = tmp[0] + spacing[0] * (double)(1 << levelDiff) * ((double)(px)+dx);
        cornerPoints[(c<<2)|1] = tmp[1] + spacing[1] * (double)(1 << levelDiff) * ((double)(py)+dy);
        cornerPoints[(c<<2)|2] = tmp[2] + spacing[2] * (double)(1 << levelDiff) * ((double)(pz)+dz);
        }
      }
    else
      {
      // How do I chop the cells in half on the bondaries?
      // Move the tmp origin and change spacing.
      cornerPoints[c<<2]     = tmp[0] + spacing[0] * ((double)(px)+dx); 
      cornerPoints[(c<<2)|1] = tmp[1] + spacing[1] * ((double)(py)+dy); 
      cornerPoints[(c<<2)|2] = tmp[2] + spacing[2] * ((double)(pz)+dz); 
      }
    }
  // We have the points, now contour the cell.
  // Get edges.
  triCase = triCases + cubeCase;
  edge = triCase->edges; 
  double pt[3];

  // Save the edge point ids incase we need to create a capping surface.
  vtkIdType edgePointIds[12]; // Is six the maximum?
  // For debugging
  // My capping permutations were giving me bad edges.
  //for( int ii = 0; ii < 12; ++ii)
  //  {
  //  edgePointIds[ii] = 0;
  //  }

  // loop over triangles  
  while(*edge > -1)
    {
    // I want to avoid adding degenerate triangles. 
    // Maybe the best way to do this is to have a point locator
    // merge points first.
    // Create brute force locator for a block, and resuse it.
    // Only permanently keep locator for edges shared between two blocks.
    for (int ii=0; ii<3; ++ii, ++edge) //insert triangle
      {
      vtkIdType* ptIdPtr = this->BlockLocator->GetEdgePointer(x,y,z,*edge);

      if (*ptIdPtr == -1)
        {
        // Compute the interpolation factor.
        v0 = cornerValues[vtkAMRDualIsoEdgeToVTKPointsTable[*edge][0]];
        v1 = cornerValues[vtkAMRDualIsoEdgeToVTKPointsTable[*edge][1]];
        k = (this->IsoValue-v0) / (v1-v0);
        // Add the point to the output and get the index of the point.
        int pt1Idx = (vtkAMRDualIsoEdgeToPointsTable[*edge][0]<<2);
        int pt2Idx = (vtkAMRDualIsoEdgeToPointsTable[*edge][1]<<2);
        // I wonder if this is any faster than incrementing a pointer.
        pt[0] = cornerPoints[pt1Idx] + k*(cornerPoints[pt2Idx]-cornerPoints[pt1Idx]);
        pt[1] = cornerPoints[pt1Idx|1] + k*(cornerPoints[pt2Idx|1]-cornerPoints[pt1Idx|1]);
        pt[2] = cornerPoints[pt1Idx|2] + k*(cornerPoints[pt2Idx|2]-cornerPoints[pt1Idx|2]);
        *ptIdPtr = this->Points->InsertNextPoint(pt);
        }
      edgePointIds[*edge] = pointIds[ii] = *ptIdPtr; 
      }

    this->Faces->InsertNextCell(3, pointIds);
    this->BlockIdCellArray->InsertNextValue(blockId);
    }

  if (this->EnableCapping)
    {
    this->CapCell(x,y,z, cubeBoundaryBits, cubeCase, edgePointIds, cornerPoints, blockId);
    }
}

//----------------------------------------------------------------------------
// Now generate the capping surface.
// I chose to make a generic face case table. We decided to cap 
// each face independantly.  I permute the hex index into a face case
// and I permute the face corners and edges into hex corners and endges.
// It endsup being a little long to duplicate the code 6 times,
// but it is still fast.
void vtkAMRDualContour::CapCell(
  int cellX, int cellY, int cellZ, // cell index in block coordinates.
  // Which cell faces need to be capped.
  unsigned char cubeBoundaryBits,
  // Marching cubes case for this cell
  int cubeCase,
  // Ids of the point created on edges for the internal surface
  vtkIdType edgePointIds[12],
  // Locations of 8 corners (xyz4xyz4...); 4th value is not used.
  double cornerPoints[32],
  // For block id array (for debugging).  I should just make this an ivar.
  int blockId)
{
  int cornerIdx;
  vtkIdType *ptIdPtr;
  vtkIdType pointIds[6];
  // -X
  if ( (cubeBoundaryBits & 1))
    {
    int faceCase = ((cubeCase&1)) | ((cubeCase&8)>>2) | ((cubeCase&128)>>5) | ((cubeCase&16)>>1);
    int *capPtr = vtkAMRDualIsoCappingTable[faceCase];
    while (*capPtr != -2)
      {
      int ptCount = 0;
      while (*capPtr >= 0)
        {
        if (*capPtr < 4)
          {
          cornerIdx = (vtkAMRDualIsoNXCapEdgeMap[*capPtr]);
          ptIdPtr = this->BlockLocator->GetCornerPointer(cellX,cellY,cellZ, cornerIdx);
          if (*ptIdPtr == -1)
            {
            *ptIdPtr = this->Points->InsertNextPoint(cornerPoints+(cornerIdx<<2));
            }
          pointIds[ptCount++] = *ptIdPtr; 
          }
        else
          {
          pointIds[ptCount++] = edgePointIds[vtkAMRDualIsoNXCapEdgeMap[*capPtr]];
          }
        ++capPtr;
        }
      this->Faces->InsertNextCell(ptCount, pointIds);
      this->BlockIdCellArray->InsertNextValue(blockId);
      if (*capPtr == -1) {++capPtr;} // Skip to the next triangle.
      }
    }
  // +X
  if ( (cubeBoundaryBits & 2))
    {
    int faceCase = ((cubeCase&2)>>1) | ((cubeCase&32)>>4) | ((cubeCase&64)>>4) | ((cubeCase&4)<<1);
    int *capPtr = vtkAMRDualIsoCappingTable[faceCase];
    while (*capPtr != -2)
      {
      int ptCount = 0;
      while (*capPtr >= 0)
        {
        if (*capPtr < 4)
          {
          cornerIdx = (vtkAMRDualIsoPXCapEdgeMap[*capPtr]);
          ptIdPtr = this->BlockLocator->GetCornerPointer(cellX,cellY,cellZ, cornerIdx);
          if (*ptIdPtr == -1)
            {
            *ptIdPtr = this->Points->InsertNextPoint(cornerPoints+(cornerIdx<<2));
            }
          pointIds[ptCount++] = *ptIdPtr; 
          }
        else
          {
          pointIds[ptCount++] = edgePointIds[vtkAMRDualIsoPXCapEdgeMap[*capPtr]];
          }
        ++capPtr;
        }
      this->Faces->InsertNextCell(ptCount, pointIds);
      this->BlockIdCellArray->InsertNextValue(blockId);
      if (*capPtr == -1) {++capPtr;} // Skip to the next triangle.
      }
    }
    
  // -Y
  if ( (cubeBoundaryBits & 4))
    {
    int faceCase = ((cubeCase&1)) | ((cubeCase&16)>>3) | ((cubeCase&32)>>3) | ((cubeCase&2)<<2);
    int *capPtr = vtkAMRDualIsoCappingTable[faceCase];
    while (*capPtr != -2)
      {
      int ptCount = 0;
      while (*capPtr >= 0)
        {
        if (*capPtr < 4)
          {
          cornerIdx = (vtkAMRDualIsoNYCapEdgeMap[*capPtr]);
          ptIdPtr = this->BlockLocator->GetCornerPointer(cellX,cellY,cellZ, cornerIdx);
          if (*ptIdPtr == -1)
            {
            *ptIdPtr = this->Points->InsertNextPoint(cornerPoints+(cornerIdx<<2));
            }
          pointIds[ptCount++] = *ptIdPtr; 
          }
        else
          {
          pointIds[ptCount++] = edgePointIds[vtkAMRDualIsoNYCapEdgeMap[*capPtr]];
          }
        ++capPtr;
        }
      this->Faces->InsertNextCell(ptCount, pointIds);
      this->BlockIdCellArray->InsertNextValue(blockId);
      if (*capPtr == -1) {++capPtr;} // Skip to the next triangle.
      }
    }
    
  // +Y
  if ( (cubeBoundaryBits & 8))
    {
    int faceCase = ((cubeCase&8)>>3) | ((cubeCase&4)>>1) | ((cubeCase&64)>>4) | ((cubeCase&128)>>4);
    int *capPtr = vtkAMRDualIsoCappingTable[faceCase];
    while (*capPtr != -2)
      {
      int ptCount = 0;
      while (*capPtr >= 0)
        {
        if (*capPtr < 4)
          {
          cornerIdx = (vtkAMRDualIsoPYCapEdgeMap[*capPtr]);
          ptIdPtr = this->BlockLocator->GetCornerPointer(cellX,cellY,cellZ, cornerIdx);
          if (*ptIdPtr == -1)
            {
            *ptIdPtr = this->Points->InsertNextPoint(cornerPoints+(cornerIdx<<2));
            }
          pointIds[ptCount++] = *ptIdPtr; 
          }
        else
          {
          pointIds[ptCount++] = edgePointIds[vtkAMRDualIsoPYCapEdgeMap[*capPtr]];
          }
        ++capPtr;
        }
      this->Faces->InsertNextCell(ptCount, pointIds);
      this->BlockIdCellArray->InsertNextValue(blockId);
      if (*capPtr == -1) {++capPtr;} // Skip to the next triangle.
      }
    }
    
    
  // -Z
  if ( (cubeBoundaryBits & 16))
    {
    int faceCase = (cubeCase&1) | (cubeCase&2) | (cubeCase&4) | (cubeCase&8);
    int *capPtr = vtkAMRDualIsoCappingTable[faceCase];
    while (*capPtr != -2)
      {
      int ptCount = 0;
      while (*capPtr >= 0)
        {
        if (*capPtr < 4)
          {
          cornerIdx = (vtkAMRDualIsoNZCapEdgeMap[*capPtr]);
          ptIdPtr = this->BlockLocator->GetCornerPointer(cellX,cellY,cellZ, cornerIdx);
          if (*ptIdPtr == -1)
            {
            *ptIdPtr = this->Points->InsertNextPoint(cornerPoints+(cornerIdx<<2));
            }
          pointIds[ptCount++] = *ptIdPtr; 
          }
        else
          {
          pointIds[ptCount++] = edgePointIds[vtkAMRDualIsoNZCapEdgeMap[*capPtr]];
          }
        ++capPtr;
        }
      this->Faces->InsertNextCell(ptCount, pointIds);
      this->BlockIdCellArray->InsertNextValue(blockId);
      if (*capPtr == -1) {++capPtr;} // Skip to the next triangle.
      }
    }

  // +Z
  if ( (cubeBoundaryBits & 32))
    {
    int faceCase = ((cubeCase&128)>>7) | ((cubeCase&64)>>5) | ((cubeCase&32)>>3) | ((cubeCase&16)>>1);
    int *capPtr = vtkAMRDualIsoCappingTable[faceCase];
    while (*capPtr != -2)
      {
      int ptCount = 0;
      while (*capPtr >= 0)
        {
        if (*capPtr < 4)
          {
          cornerIdx = (vtkAMRDualIsoPZCapEdgeMap[*capPtr]);
          ptIdPtr = this->BlockLocator->GetCornerPointer(cellX,cellY,cellZ, cornerIdx);
          if (*ptIdPtr == -1)
            {
            *ptIdPtr = this->Points->InsertNextPoint(cornerPoints+(cornerIdx<<2));
            }
          pointIds[ptCount++] = *ptIdPtr; 
          }
        else
          {
          pointIds[ptCount++] = edgePointIds[vtkAMRDualIsoPZCapEdgeMap[*capPtr]];
          }
        ++capPtr;
        }
      this->Faces->InsertNextCell(ptCount, pointIds);
      this->BlockIdCellArray->InsertNextValue(blockId);
      if (*capPtr == -1) {++capPtr;} // Skip to the next triangle.
      }
    }
}



