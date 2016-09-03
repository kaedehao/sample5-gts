
/*
 * Copyright (c) 2008 - 2009 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and proprietary
 * rights in and to this software, related documentation and any modifications thereto.
 * Any use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation is strictly
 * prohibited.
 *
 * TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THIS SOFTWARE IS PROVIDED *AS IS*
 * AND NVIDIA AND ITS SUPPLIERS DISCLAIM ALL WARRANTIES, EITHER EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE.  IN NO EVENT SHALL NVIDIA OR ITS SUPPLIERS BE LIABLE FOR ANY
 * SPECIAL, INCIDENTAL, INDIRECT, OR CONSEQUENTIAL DAMAGES WHATSOEVER (INCLUDING, WITHOUT
 * LIMITATION, DAMAGES FOR LOSS OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF
 * BUSINESS INFORMATION, OR ANY OTHER PECUNIARY LOSS) ARISING OUT OF THE USE OF OR
 * INABILITY TO USE THIS SOFTWARE, EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGES
 */

#include <optix_world.h>
#include "helpers.h"
#include "paint_camera.h"
#include <vector>
//#include <optixpp_namespace.h>

using namespace optix;
using namespace std;

struct PerRayData_radiance
{
  float3 result;
  float  importance;
  int    depth;
  void*   shaderIdx;
};

rtBuffer<void*, 2>        shader_buffer;
rtBuffer<uint>          printEnabled;

rtDeclareVariable(float3,        eye, , );
rtDeclareVariable(float3,        U, , );
rtDeclareVariable(float3,        V, , );
rtDeclareVariable(float3,        W, , );
rtDeclareVariable(float3,        bad_color, , );
rtDeclareVariable(float,         scene_epsilon, , );
rtBuffer<uchar4, 2>              output_buffer;
rtDeclareVariable(rtObject,      top_object, , );
rtDeclareVariable(unsigned int,  radiance_ray_type, , );

rtDeclareVariable(uint2, launch_index, rtLaunchIndex, );
rtDeclareVariable(uint2, launch_dim,   rtLaunchDim, );
rtDeclareVariable(float, time_view_scale, , ) = 1e-6f;

rtDeclareVariable(unsigned int,  paint_camera_type, , );

rtDeclareVariable(uint2, pick_index, , );
//rtDeclareVariable(int, printEnabled, , ) = 0;
//#define TIME_VIEW

RT_PROGRAM void pinhole_camera()
{
#ifdef TIME_VIEW
  clock_t t0 = clock(); 
#endif
  float2 d = make_float2(launch_index) / make_float2(launch_dim) * 2.f - 1.f;
  float3 ray_origin = eye;
  float3 ray_direction = normalize(d.x*U + d.y*V + W);

  // Posing camera
  if ( paint_camera_type == 1 || paint_camera_type == 2 )
    ray_direction = normalize( ray_direction + cameraTexture(d, camera_pose_map) );

  optix::Ray ray = optix::make_Ray(ray_origin, ray_direction, radiance_ray_type, scene_epsilon, RT_DEFAULT_MAX);

  // Painting camera
  if ( paint_camera_type == 0 || paint_camera_type == 2)
    paint_camera( &ray );
//  else if ( paint_camera_type == 1 )
//    pose_camera( &ray );
//  else{
//      paint_camera( &ray );
//      pose_camera( &ray );
//  }

  PerRayData_radiance prd;
  prd.importance = 1.f;
  prd.depth = 0;
  prd.shaderIdx = NULL;

  rtTrace(top_object, ray, prd);
  //rtPrintf( "d: (%f, %f)\n", d.x, d.y );

#ifdef TIME_VIEW
  clock_t t1 = clock(); 
 
  float expected_fps   = 1.0f;
  float pixel_time     = ( t1 - t0 ) * time_view_scale * expected_fps;
  output_buffer[launch_index] = make_color( make_float3(  pixel_time ) ); 
#else
  output_buffer[launch_index] = make_color( prd.result );
  shader_buffer[launch_index] = prd.shaderIdx;


  // debug rtPrintf
  if (launch_index.x >= pick_index.x && launch_index.y >= pick_index.y && printEnabled[0]){
      //rtPrintf( "pick_index: (%u, %u)\n", pick_index.x, pick_index.y );
      //rtPrintf( "Color: (%u, %u, %u)\n", output_buffer[launch_index].x,
      //          output_buffer[launch_index].y,
      //          output_buffer[launch_index].z );
      //rtPrintf( "shader Index: %u\n", shader_buffer[launch_index] );
      printEnabled[0] = false;
  }

#endif
}



RT_PROGRAM void exception()
{
  const unsigned int code = rtGetExceptionCode();
  rtPrintf( "Caught exception 0x%X at launch index (%d,%d)\n", code, launch_index.x, launch_index.y );
  output_buffer[launch_index] = make_color( bad_color );
}
