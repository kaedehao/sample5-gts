#include "sample5.h"
#include "commonStructs.h"

#include <ImageLoader.h>

#include "src/random.h"
#include <QColor>
#include <iostream>
#include <mach-o/dyld.h>
#include <vector>


#define NUM_SPHERE 2

using namespace optix;
using namespace std;

//------------------------------------------------------------------------------
//
// Sample5Scene implementation
//
//------------------------------------------------------------------------------
Sample5Scene::Sample5Scene():
  MeshScene( false, false, false),
  m_frame_number( 0 ),
  m_adaptive_aa( false ),

    m_camera_mode       ( CM_PINHOLE ),
    m_shade_mode        ( SM_PHONG ),
    m_aa_enabled        ( false ),
    m_ground_plane_enabled ( false ),
    m_ao_radius         ( 1.0f ),
    m_ao_sample_mult    ( 1 ),
    m_light_scale       ( 1.0f ),
    m_accum_enabled     ( false ),
    m_scene_epsilon     ( 1e-4f ),
    m_animation         ( false )
{
}

void Sample5Scene::initScene( InitialCameraData& camera_data )
{
  //Sample5Scene::enableCPURendering(1);
  initContext();
  initLights();
  initMaterial();
  initGeometry();
  initCamera( camera_data );
  preprocess();
}

void Sample5Scene::initContext()
{
    setAA( true );
    // context
    m_context->setRayTypeCount(3);
    m_context->setEntryPointCount(2);
    m_context->setStackSize( 2800 );

    m_context->setPrintEnabled( true );
    m_context->setPrintBufferSize( 4096 );
    //m_context->setPrintLaunchIndex(1131, 1027);


    //Buffer shader_buffer = m_context["shader_buffer"]->set( createOutputBuffer(RT_FORMAT_USER, WIDTH, HEIGHT) );
    //shader_buffer->setElementSize(sizeof(BasicLight));


    m_context["radiance_ray_type"]->setUint( 0u );
    m_context["shadow_ray_type"]->setUint( 1u );
    m_context["max_depth"]->setInt( 5u );
    m_context["ambient_light_color"]->setFloat( 0.2f, 0.2f, 0.2f );
    m_context["output_buffer"]->set( createOutputBuffer(RT_FORMAT_UNSIGNED_BYTE4, WIDTH, HEIGHT) );
    m_context[ "jitter_factor"       ]->setFloat( m_aa_enabled ? 1.0f : 0.0f );

    m_context["frame_number"]->setUint( 0u );

    m_accum_enabled = m_aa_enabled                         ||
                     m_shade_mode == SM_AO                 ||
                     m_shade_mode == SM_ONE_BOUNCE_DIFFUSE ||
                     m_shade_mode == SM_AO_PHONG;

    // Ray generation program setup
    const std::string camera_name = m_camera_mode == CM_PINHOLE ? "pinhole_camera" : "orthographic_camera";
    const std::string camera_file = m_accum_enabled             ? "accum_camera.cu" :
                                    m_camera_mode == CM_PINHOLE ? "pinhole_camera.cu"  :
                                                                 "orthographic_camera.cu";

    if( m_accum_enabled ) {
      // The raygen program needs accum_buffer
      m_accum_buffer = m_context->createBuffer( RT_BUFFER_INPUT_OUTPUT | RT_BUFFER_GPU_LOCAL, RT_FORMAT_FLOAT4,
                                              WIDTH, HEIGHT );
      m_context["accum_buffer"]->set( m_accum_buffer );
      resetAccumulation();
    }

//    const std::string camera_ptx  = ptxpath( "sample5", camera_file );
//    Program ray_gen_program = m_context->createProgramFromPTXFile( camera_ptx, camera_name );
//    m_context->setRayGenerationProgram( 0, ray_gen_program );

    // Pinhole Camera ray gen and exception program
    std::string ptx_path = ptxpath( "sample5", "pinhole_camera.cu" );
    m_context->setRayGenerationProgram( Pinhole, m_context->createProgramFromPTXFile(ptx_path, "pinhole_camera" ) );
    //std::cout<<"here"<<std::endl;
    m_context->setExceptionProgram( Pinhole, m_context->createProgramFromPTXFile(ptx_path, "exception" ) );

    // Adaptive Pinhole Camera ray gen and exception program
    ptx_path = ptxpath( "sample5", "adaptive_pinhole_camera.cu" );
    m_context->setRayGenerationProgram( AdaptivePinhole, m_context->createProgramFromPTXFile( ptx_path, "pinhole_camera" ) );
    m_context->setExceptionProgram(     AdaptivePinhole, m_context->createProgramFromPTXFile( ptx_path, "exception" ) );

//    const std::string except_ptx  = ptxpath( "sample5", camera_file );
//    m_context->setExceptionProgram( 0, m_context->createProgramFromPTXFile( except_ptx, "exception" ) );
    m_context["bad_color"]->setFloat( 0.0f, 1.0f, 0.0f);

    // Miss program constant background
//    ptx_path = ptxpath( "sample5", "constantbg.cu" );
//    m_context->setMissProgram( 0, m_context->createProgramFromPTXFile( ptx_path, "miss" ) );
//    m_context["bg_color"]->setFloat( make_float3( 0.3f, 0.3f, 0.3f ) );

    // Miss program envmap
    ptx_path = ptxpath( "sample5", "envmap.cu" );
    m_context->setMissProgram( 0, m_context->createProgramFromPTXFile( ptx_path, "envmap_miss" ) );
    const float3 default_color = make_float3(1.0f, 1.0f, 1.0f);
    m_context["envmap"]->setTextureSampler( loadTexture( m_context, datapath("autumn.ppm"), default_color) );

    //m_context["bg_shaderId"]->setUserData(sizeof(Material), 0x0);

    // Variance buffers
    Buffer variance_sum_buffer = m_context->createBuffer( RT_BUFFER_INPUT_OUTPUT | RT_BUFFER_GPU_LOCAL,
                                                          RT_FORMAT_FLOAT4,
                                                          WIDTH, HEIGHT );
    memset( variance_sum_buffer->map(), 0, WIDTH*HEIGHT*sizeof(float4) );
    variance_sum_buffer->unmap();
    m_context["variance_sum_buffer"]->set( variance_sum_buffer );

    Buffer variance_sum2_buffer = m_context->createBuffer( RT_BUFFER_INPUT_OUTPUT | RT_BUFFER_GPU_LOCAL,
                                                           RT_FORMAT_FLOAT4,
                                                           WIDTH, HEIGHT );
    memset( variance_sum2_buffer->map(), 0, WIDTH*HEIGHT*sizeof(float4) );
    variance_sum2_buffer->unmap();
    m_context["variance_sum2_buffer"]->set( variance_sum2_buffer );

    // Sample count buffer
    Buffer num_samples_buffer = m_context->createBuffer( RT_BUFFER_INPUT_OUTPUT | RT_BUFFER_GPU_LOCAL,
                                                         RT_FORMAT_UNSIGNED_INT,
                                                         WIDTH, HEIGHT );
    memset( num_samples_buffer->map(), 0, WIDTH*HEIGHT*sizeof(unsigned int) );
    num_samples_buffer->unmap();
    m_context["num_samples_buffer"]->set( num_samples_buffer);

    // RNG seed buffer
    m_rnd_seeds = m_context->createBuffer( RT_BUFFER_INPUT_OUTPUT | RT_BUFFER_GPU_LOCAL,
                                           RT_FORMAT_UNSIGNED_INT,
                                           WIDTH, HEIGHT );
    m_context["rnd_seeds"]->set( m_rnd_seeds );
    genRndSeeds( WIDTH, HEIGHT );
}

void Sample5Scene::initLights()
{
    BasicLight lights[] = {
        { make_float3( 60.0f, 40.0f, 30.0f ), make_float3( 1.0f, 1.0f, 1.0f ), 1 }
    };

    // Lights buffer
//    BasicLight lights[] = {
//      { make_float3( -60.0f,  30.0f, -120.0f ), make_float3( 0.2f, 0.2f, 0.25f )*m_light_scale, 0, 0 },
//      { make_float3( -60.0f,   0.0f,  120.0f ), make_float3( 0.1f, 0.1f, 0.10f )*m_light_scale, 0, 0 },
//      { make_float3(  60.0f,  60.0f,   60.0f ), make_float3( 0.7f, 0.7f, 0.65f )*m_light_scale, 1, 0 }
//    };

    Buffer light_buffer = m_context->createBuffer( RT_BUFFER_INPUT );
    light_buffer->setFormat( RT_FORMAT_USER );
    light_buffer->setElementSize( sizeof(BasicLight) );
    light_buffer->setSize( sizeof(lights)/sizeof(lights[0]) );
    memcpy( light_buffer->map(), lights, sizeof(lights) );
    light_buffer->unmap();

    m_context["lights"]->set( light_buffer );
}

void Sample5Scene::initPrintEnabled()
{
    uint printEnabled = false;

    Buffer printEnabled_buffer = m_context->createBuffer(RT_BUFFER_INPUT_OUTPUT);
    printEnabled_buffer->setFormat( RT_FORMAT_USER );
    printEnabled_buffer->setElementSize( sizeof(uint) );
    printEnabled_buffer->setSize( 1 );
    memcpy( printEnabled_buffer->map(), &printEnabled, sizeof(printEnabled) );
    printEnabled_buffer->unmap();

    m_context["printEnabled"]->set( printEnabled_buffer );

}

void Sample5Scene::initMaterial()
{
    initPrintEnabled();

    Buffer shader_buffer = m_context->createBuffer( RT_BUFFER_OUTPUT );
    shader_buffer->setFormat( RT_FORMAT_USER );
    shader_buffer->setElementSize(sizeof(void*));
    shader_buffer->setSize ( WIDTH, HEIGHT );

    m_context["shader_buffer"]->set(shader_buffer);


    // Normal material
    Program normal_ch = m_context->createProgramFromPTXFile( ptxpath( "sample5", "normal_shader.cu" ), "closest_hit_radiance" );
    normal_matl = m_context->createMaterial();
    normal_matl->setClosestHitProgram( 0, normal_ch );

    // Solid meterial
    solid_matl = m_context->createMaterial();
    makeMaterialPrograms( solid_matl, "solid.cu", "closest_hit_radiance", "any_hit_shadow" );
    solid_matl["solid_color"]->setFloat( 1.0f, 0.0f, 0.0f );


    void* solid_matl_ptr = &solid_matl;
    //void* test_ptr = (void*)0x100;
    solid_matl["shaderId"]->setUserData(sizeof(solid_matl_ptr), &solid_matl_ptr);
    //solid_matl["shaderId"]->setUint(2u);

    //std::cout<<"solid_matl type: "<<typeid(ptr).name()<<std::endl;
    //std::cout<<"solid_matl address: "<<&solid_matl<<std::endl;
    //printf("solid_matl addres: %x\n", uintptr_t(&solid_matl));
    //std::cout<<"solid_matl address: "<<ptr<<std::endl;
    //cout<<"size of Material: "<<sizeof(Material)<<endl;
    //cout<<"size of void* : "<<sizeof(void*)<<endl;


    /*
    memcpy( shader_buffer->map(), &solid_matl_ptr, sizeof(solid_matl_ptr) );
    shader_buffer->unmap();

    m_context["shader_buffer"]->set( shader_buffer );

//    Material* testMaltPtr;
//    testMaltPtr = &solid_matl;
//    cout<<"testMaltPtr: "<<testMaltPtr<<endl;
//    Material testMalt = *testMaltPtr;

    void* *testMaltPtrVoidPtr = &solid_matl_ptr;
    //void* testMaltPtrVoid = solid_matl_ptr;
    void* testMaltPtrVoid = *testMaltPtrVoidPtr;
    cout<<"testMaltPtrVoidPtr: "<<*testMaltPtrVoidPtr<<endl;
    cout<<"testMaltPtrVoid's address: "<<&testMaltPtrVoid<<endl;
    cout<<"testMaltPtrVoid: "<<testMaltPtrVoid<<endl;

    Material* testMaltPtr = (Material*)*testMaltPtrVoidPtr;
    cout<<"testMaltPtr: "<<testMaltPtr<<endl;
    //Material testMalt = *testMaltPtr;
    Material testMalt = *(Material*)*testMaltPtrVoidPtr;
    testMalt["solid_color"]->setFloat( 0.0f, 0.0f, 1.0f);
    */

    // Phong material
    metal_matl = m_context->createMaterial();
    makeMaterialPrograms( metal_matl, "phong.cu", "closest_hit_radiance", "any_hit_shadow" );

    void* metal_matl_ptr = &metal_matl;
    cout<<"metal_matl address: "<<metal_matl_ptr<<endl;
    metal_matl["shaderId"]->setUserData(sizeof(metal_matl_ptr), &metal_matl_ptr);

    metal_matl["Ka"]->setFloat( 0.2f, 0.5f, 0.5f );
    metal_matl["Kd"]->setFloat( 0.2f, 0.7f, 0.8f );
    metal_matl["Ks"]->setFloat( 0.9f, 0.9f, 0.9f );
    metal_matl["phong_exp"]->setFloat( 64 );
    metal_matl["reflectivity"]->setFloat( 0.5f,  0.5f,  0.5f);

    // Glass material
    glass_matl = m_context->createMaterial();
    makeMaterialPrograms( glass_matl, "glass.cu", "closest_hit_radiance", "any_hit_shadow" );

    void* glass_matl_ptr = &glass_matl;
    glass_matl["shaderId"]->setUserData(sizeof(glass_matl_ptr), &glass_matl_ptr);

    glass_matl["importance_cutoff"]->setFloat( 01e-2f );
    glass_matl["cutoff_color"]->setFloat( 0.034f, 0.055f, 0.085f );
    glass_matl["fresnel_exponent"]->setFloat( 3.0f );
    glass_matl["fresnel_minimum"]->setFloat( 0.1f );
    glass_matl["fresnel_maximum"]->setFloat( 1.0f );
    glass_matl["refraction_index"]->setFloat( 1.4f );
    glass_matl["refraction_color"]->setFloat( 1.0f, 1.0f, 1.0f );
    glass_matl["reflection_color"]->setFloat( 1.0f, 1.0f, 1.0f );
    glass_matl["refraction_maxdepth"]->setInt( 10 );
    glass_matl["reflection_maxdepth"]->setInt( 5 );
    float3 extinction = make_float3(.83f, .83f, .83f);
    glass_matl["extinction_constant"]->setFloat( log(extinction.x), log(extinction.y), log(extinction.z) );
    glass_matl["shadow_attenuation"]->setFloat( 0.6f, 0.6f, 0.6f );

    // Checker material for floor
    floor_matl = m_context->createMaterial();
    makeMaterialPrograms( floor_matl, "checker.cu", "closest_hit_radiance", "any_hit_shadow" );

    floor_matl["Kd1"]->setFloat( 0.8f, 0.3f, 0.15f);
    floor_matl["Ka1"]->setFloat( 0.8f, 0.3f, 0.15f);
    floor_matl["Ks1"]->setFloat( 0.0f, 0.0f, 0.0f);
    floor_matl["Kd2"]->setFloat( 0.9f, 0.85f, 0.05f);
    floor_matl["Ka2"]->setFloat( 0.9f, 0.85f, 0.05f);
    floor_matl["Ks2"]->setFloat( 0.0f, 0.0f, 0.0f);
    floor_matl["inv_checker_size"]->setFloat( 32.0f, 16.0f, 1.0f );
    floor_matl["phong_exp1"]->setFloat( 0.0f );
    floor_matl["phong_exp2"]->setFloat( 0.0f );
    floor_matl["reflectivity1"]->setFloat( 0.0f, 0.0f, 0.0f);
    floor_matl["reflectivity2"]->setFloat( 0.0f, 0.0f, 0.0f);

  //Mesh Materials
  switch( m_shade_mode ) {
    case SM_PHONG: {
      // Use the default obj_material created by OptixMesh if model has no material, but use this for the ground plane, if any
      break;
    }

    case SM_NORMAL: {
      const std::string ptx_path = ptxpath("sample5", "normal_shader.cu");
      m_material = m_context->createMaterial();
      m_material->setClosestHitProgram( 0, m_context->createProgramFromPTXFile( ptx_path, "closest_hit_radiance" ) );
      break;
    }

    case SM_AO: {
      m_material = m_context->createMaterial();
      makeMaterialPrograms( m_material, "ambocc.cu", "closest_hit_radiance", "any_hit_shadow" );
      break;
    }

    case SM_AO_PHONG: {
      m_material = m_context->createMaterial();
      makeMaterialPrograms( m_material, "ambocc.cu", "closest_hit_radiance", "any_hit_shadow" );

      // the ao phong shading uses monochrome single-float values for Kd, etc.,
      // so it won't make sense to use the float4 colors from m_default_material_params
      m_context["Kd"]->setFloat(1.0f);
      m_context["Ka"]->setFloat(0.6f);
      m_context["Ks"]->setFloat(0.0f);
      m_context["Kr"]->setFloat(0.0f);
      m_context["phong_exp"]->setFloat(0.0f);
      break;
    }

    if( m_accum_enabled ) {
      //genRndSeeds( WIDTH, HEIGHT );
    }

  }
}

void Sample5Scene::makeMaterialPrograms( Material material, const char *filename,
                                                            const char *ch_program_name,
                                                            const char *ah_program_name )
{
  Program ch_program = m_context->createProgramFromPTXFile( ptxpath("sample5", filename), ch_program_name );
  Program ah_program = m_context->createProgramFromPTXFile( ptxpath("sample5", filename), ah_program_name );

  material->setClosestHitProgram( 0, ch_program );
  material->setAnyHitProgram( 1, ah_program );
}
//----------------------------------------------------------------------------------------------------------------------
void Sample5Scene::initGeometry()
{
    // Sphere geometry
    std::string sphere_ptx( ptxpath( "sample5", "sphere.cu") );
    Geometry sphere = m_context->createGeometry();
    sphere->setPrimitiveCount( 1u );
    sphere->setBoundingBoxProgram( m_context->createProgramFromPTXFile( sphere_ptx, "bounds" ) );
    sphere->setIntersectionProgram( m_context->createProgramFromPTXFile( sphere_ptx, "robust_intersect" ) );
    sphere["sphere"]->setFloat( 0, 0, 0, 1.0f );

    // Sphere Shell geometry
    std::string shell_ptx( ptxpath( "sample5", "sphere_shell.cu") );
    Geometry glass_sphere = m_context->createGeometry();
    glass_sphere->setPrimitiveCount( 1u );
    glass_sphere->setBoundingBoxProgram( m_context->createProgramFromPTXFile( shell_ptx, "bounds" ) );
    glass_sphere->setIntersectionProgram( m_context->createProgramFromPTXFile( shell_ptx, "intersect" ) );
    glass_sphere["center"]->setFloat( 0, 0, 0 );
    glass_sphere["radius1"]->setFloat( 0.96f );
    glass_sphere["radius2"]->setFloat( 1.0f );

    // Floor geometry
    std::string pgram_ptx( ptxpath( "sample5", "parallelogram.cu" ) );
    Geometry parallelogram = m_context->createGeometry();
    parallelogram->setPrimitiveCount( 1u );
    parallelogram->setBoundingBoxProgram( m_context->createProgramFromPTXFile( pgram_ptx, "bounds" ) );
    parallelogram->setIntersectionProgram( m_context->createProgramFromPTXFile( pgram_ptx, "intersect" ) );
    float3 anchor = make_float3( -16.0f, 0.01f, -8.0f );
    float3 v1 = make_float3( 32.0f, 0.0f, 0.0f );
    float3 v2 = make_float3( 0.0f, 0.0f, 16.0f );
    float3 normal = cross( v1, v2 );
    normal = normalize( normal );
    float d = dot( normal, anchor );
    v1 *= 1.0f/dot( v1, v1 );
    v2 *= 1.0f/dot( v2, v2 );
    float4 plane = make_float4( normal, d );
    parallelogram["plane"]->setFloat( plane );
    parallelogram["v1"]->setFloat( v1 );
    parallelogram["v2"]->setFloat( v2 );
    parallelogram["anchor"]->setFloat( anchor );

    // Initial transform matrix
    const float tx=0.0f, ty=1.0f, tz=0.0f;
    const float sx=1.0f, sy=1.0f, sz=1.0f;
    // Matrices are row-major.
    float m[16] = { sx, 0, 0, tx,
                    0, sy, 0, ty,
                    0, 0, sz, tz,
                    0, 0, 0, 1 };

    // Create GIs for each piece of geometry
    Group top_level_group = m_context->createGroup();
    Transform transform;
    GeometryGroup geometrygroup;
    GeometryInstance instance;
    for (int i =0; i<NUM_SPHERE; ++i)
    {
        // Create geometry instance
        if(i>0){ // metal sphere
            instance = m_context->createGeometryInstance( sphere, &metal_matl, &metal_matl+1 );
        //instance->addMaterial(solid_matl);
        }
        else
            instance = m_context->createGeometryInstance( glass_sphere, &glass_matl, &glass_matl+1 );

        // place instance in geometry group
        geometrygroup = m_context->createGeometryGroup();
        geometrygroup->setChildCount( 1 );
        geometrygroup->setChild( 0, instance );

        // Create acceleration oject for geometry group
        geometrygroup->setAcceleration( m_context->createAcceleration( "NoAccel",  "NoAccel" ) );
        geometrygroup->getAcceleration()->markDirty();

        // Create transform node
        transform = m_context->createTransform();
        m[3] += 3.0f;
        transform->setMatrix( 0, m, 0 );
        transform->setChild( geometrygroup );
        top_level_group->addChild( transform );
    }

    // Floor
    instance = m_context->createGeometryInstance( parallelogram, &floor_matl, &floor_matl+1 );
    geometrygroup = m_context->createGeometryGroup();
    geometrygroup->setChildCount( 1 );
    geometrygroup->setChild( 0 , instance );
    geometrygroup->setAcceleration( m_context->createAcceleration( "NoAccel",  "NoAccel" ) );
    top_level_group->addChild( geometrygroup );

    //Load OBJ model
    double start, end;
    sutilCurrentTime(&start);

    setMesh( (datapath("models/cow.obj")).c_str() );

    m_geometry_group = m_context->createGeometryGroup();
    OptixMesh loader( m_context, m_geometry_group,
                      m_accel_builder.c_str(), m_accel_traverser.c_str(),
                      m_accel_refine.c_str(), m_accel_large_mesh );

    std::string prog_path( ptxpath( "sample5", "triangle_mesh_iterative.cu") );
    Program mesh_intersect = m_context->createProgramFromPTXFile( prog_path, "mesh_intersect" );
    loader.setDefaultIntersectionProgram( mesh_intersect );
    //loader.setLoadingTransform();
    loader.loadBegin_Geometry( m_filename );

    // Override default OptixMesh material for most shade modes
    if( m_shade_mode == SM_NORMAL || m_shade_mode == SM_AO || m_shade_mode == SM_AO_PHONG
        || m_shade_mode == SM_ONE_BOUNCE_DIFFUSE )
    {
      for( size_t i = 0; i < loader.getMaterialCount(); ++i ) {
        loader.setOptixMaterial( static_cast<int>(i), m_material );
      }
    }

    //loader.setOptixMaterial(0, glass_matl);

    m_aabb = loader.getSceneBBox();

    loader.loadFinish_Materials();

    // Load acceleration structure from a file if that was enabled on the
    // command line, and if we can find a cache file. Note that the type of
    // acceleration used will be overridden by what is found in the file.
    loadAccelCache();

    //Scale Mesh
    m[0] = m[5] = m[10] += 100.0f;
    //Translate
    m[3] = -2.0f;
    m[7] = 0.0f;
    transform = m_context->createTransform();
    transform->setMatrix(0, m, 0);
    transform->setChild( m_geometry_group );
    top_level_group->addChild( transform );

    sutilCurrentTime(&end);
    std::cout << "Time to load " << (m_accel_large_mesh ? "and cluster " : "") << "geometry: " << end-start << " s."<<std::endl;

    // mark acceleration as dirty
    top_level_group->setAcceleration( m_context->createAcceleration( "Trbvh", "Bvh" ) );
    top_level_group->getAcceleration()->markDirty();

    m_context["top_object"]->set( top_level_group );
    m_context["top_shadower"]->set( top_level_group );
}

void Sample5Scene::initCamera( InitialCameraData& camera_data )
{
    // Set up camera
    camera_data = InitialCameraData( make_float3( 0.0f, 0.0f, 5.0f ), // eye
                                     make_float3( 0.0f, 0.0f, 0.0f ), // lookat
                                     make_float3( 0.0f, 1.0f, 0.0f ), // up
                                     60.0f );                         // vfov

     // Declare camera variables.  The values do not matter, they will be overwritten in trace.
    m_context["eye"]->setFloat( make_float3( 0.0f, 0.0f, 0.0f ) );
    m_context["U"]->setFloat( make_float3( 0.0f, 0.0f, 0.0f ) );
    m_context["V"]->setFloat( make_float3( 0.0f, 0.0f, 0.0f ) );
    m_context["W"]->setFloat( make_float3( 0.0f, 0.0f, 0.0f ) );

    // Painting camera
    //m_context->setPrintEnabled(1);
    //m_context->setPrintBufferSize(1028);

    const float3 default_color = make_float3(1.0f, 1.0f, 1.0f);
    m_context["camera_paint_map"]->setTextureSampler( loadTexture( m_context, datapath("paint_camera/magic_bg.ppm"), default_color) );
    // Posing camera
    m_context["camera_pose_map"]->setTextureSampler( loadTexture( m_context, datapath("paint_camera/cubist_b&w.ppm"), default_color) );
    // Paint or Pose or both
    m_context["paint_camera_type"]->setUint( 0u );
}

void Sample5Scene::preprocess()
{
    // Settings which rely on previous initialization
    m_scene_epsilon = 1.e-4f * m_aabb.maxExtent();
    //m_scene_epsilon = 1.e-4f;
    m_context["scene_epsilon"]->setFloat( m_scene_epsilon );
    m_context[ "occlusion_distance" ]->setFloat( m_aabb.maxExtent() * 0.3f * m_ao_radius );

    // Prepare to run
    m_context->validate();
    double start, end_compile, end_AS_build;
    sutilCurrentTime(&start);
    m_context->compile();
    sutilCurrentTime(&end_compile);
    std::cout << "Time to compile kernel: "<<end_compile-start<<" s."<<std::endl;
    m_context->launch(0,0);
    sutilCurrentTime(&end_AS_build);
    std::cout << "Time to build AS      : "<<end_AS_build-end_compile<<" s."<<std::endl;

    // Save cache file
//    saveAccelCache();
}

//----------------------------------------------------------------------------------------------------------------------
void Sample5Scene::trace( const RayGenCameraData& camera_data )
{
//    std::cout<<"eye: "
//            <<camera_data.eye.x<<", "
//            <<camera_data.eye.y<<", "
//            <<camera_data.eye.z<<", "
//            <<std::endl;

//    std::cout<<"U: "
//            <<camera_data.U.x<<", "
//            <<camera_data.U.y<<", "
//            <<camera_data.U.z<<", "
//            <<std::endl;

//    std::cout<<"V: "
//            <<camera_data.V.x<<", "
//            <<camera_data.V.y<<", "
//            <<camera_data.V.z<<", "
//            <<std::endl;

//    std::cout<<"W: "
//            <<camera_data.W.x<<", "
//            <<camera_data.W.y<<", "
//            <<camera_data.W.z<<", "
//            <<std::endl;
    if ( m_camera_changed ) {
        m_frame_number = 0u;
        m_camera_changed = false;
    }

    //launch it
    m_context["eye"]->setFloat( camera_data.eye );
    m_context["U"]->setFloat( camera_data.U );
    m_context["V"]->setFloat( camera_data.V );
    m_context["W"]->setFloat( camera_data.W );
    m_context["frame_number"]->setUint( m_frame_number++ );

    Buffer buffer = m_context["output_buffer"]->getBuffer();
    RTsize buffer_width, buffer_height;
    buffer->getSize( buffer_width, buffer_height );

    if( m_accum_enabled && !m_camera_changed ) {
      // Use more AO samples if the camera is not moving, for increased !/$.
      // Do this above launch to avoid overweighting the first frame
      m_context["sqrt_occlusion_samples"]->setInt( 3 * m_ao_sample_mult );
      m_context["sqrt_diffuse_samples"]->setInt( 3 );
    }

    m_context->launch( 0,//getEntryPoint(),
                       static_cast<unsigned int>(buffer_width),
                       static_cast<unsigned int>(buffer_height) );

    if( m_accum_enabled ) {
      // Update frame number for accumulation.
      ++m_frame;
      if( m_camera_changed ) {
        m_camera_changed = false;
        resetAccumulation();
      }

      // The frame number is used as part of the random seed.
      m_context["frame"]->setInt( m_frame );
    }

//    QImage img(WIDTH,HEIGHT,QImage::Format_RGB32);
//    QColor color;
//    int idx;
//    void* data = getOutputBuffer()->map();
//    typedef struct { float r; float g; float b; float a;} rgb;
//    rgb* rgb_data = (rgb*)data;
//    for(unsigned int i=0; i<WIDTH*HEIGHT; ++i){
//        //std::cout<<rgb_data[i].r<<","<<rgb_data[i].g<<","<<rgb_data[i].b<<std::endl;


////        if(rgb_data[i].r == NULL || rgb_data[i].g == NULL || rgb_data[i].b == NULL || rgb_data[i].a == NULL){
////           // std::cout<<"NULL"<<std::endl;
////            rgb_data[i].r = 1.0f;
////            rgb_data[i].g = 0.0f;
////            rgb_data[i].b = 0.0f;
////            rgb_data[i].a = 1.0f;
////        }

////        if(float(rgb_data[i].r) > 1.0f || float(rgb_data[i].r) < 0.0f || float(rgb_data[i].b) > 1.0f || float(rgb_data[i].b) < 0.0f || float(rgb_data[i].g) > 1.0f || float(rgb_data[i].g) < 0.0f || float(rgb_data[i].a) > 1.0f || float(rgb_data[i].a) < 0.0f){
////           // std::cout<<"OUT"<<std::endl;
////            rgb_data[i].r = 0.0f;
////            rgb_data[i].g = 1.0f;
////            rgb_data[i].b = 0.0f;
////            rgb_data[i].a = 1.0f;
////        }

////        float red = rgb_data[i].r; if(red>1.0) red=1.0;
////        float green = rgb_data[i].g; if(green>1.0) green=1.0;
////        float blue = rgb_data[i].b; if(blue>1.0) blue=1.0;
////        float alpha = rgb_data[i].a; if(alpha>1.0) alpha=1.0;
////        color.setRgbF(red,green,blue,alpha);
//        color.setRgbF(rgb_data[i].r,rgb_data[i].g,rgb_data[i].b,rgb_data[i].a);
//        idx = floor((float)i/WIDTH);

//        img.setPixel(i-(idx*WIDTH), idx, color.rgb());

//    }
//    getOutputBuffer()->unmap();
//    img.save("sample5.png","PNG");
//    std::cout<<"sample5 image saved!"<<std::endl;

//    return img;
}

//----------------------------------------------------------------------------------------------------------------------
// Return whether we processed the key or not
bool Sample5Scene::keyPressEvent( int key )
{
    switch ( key )
    {
        case Qt::Key_A:
            m_adaptive_aa = !m_adaptive_aa;
            m_camera_changed = true;
            glWidget::setContinuousMode( m_adaptive_aa ? glWidget::CDProgressive : glWidget::CDNone );
            return true;
    }
    return false;
}

Buffer Sample5Scene::getOutputBuffer()
{
    return m_context["output_buffer"]->getBuffer();
}

Buffer Sample5Scene::getOutputShaderBuffer()
{
    return m_context["shader_buffer"]->getBuffer();
}

void Sample5Scene::resetAccumulation()
{
  m_frame = 0;
  m_context[ "frame"                  ]->setInt( m_frame );
  m_context[ "sqrt_occlusion_samples" ]->setInt( 1 * m_ao_sample_mult );
  m_context[ "sqrt_diffuse_samples"   ]->setInt( 1 );
}

void Sample5Scene::genRndSeeds( unsigned int width, unsigned int height )
{
//  unsigned int* seeds = static_cast<unsigned int*>( m_rnd_seeds->map() );
//  fillRandBuffer( seeds, width*height );
//  m_rnd_seeds->unmap();

    // Init random number buffer if necessary.
    if( m_rnd_seeds.get() == 0 ) {
      m_rnd_seeds = m_context->createBuffer( RT_BUFFER_INPUT_OUTPUT | RT_BUFFER_GPU_LOCAL, RT_FORMAT_UNSIGNED_INT,
                                           WIDTH, HEIGHT);
      m_context["rnd_seeds"]->setBuffer(m_rnd_seeds);
    }

    unsigned int* seeds = static_cast<unsigned int*>( m_rnd_seeds->map() );
    fillRandBuffer(seeds, width*height);
    m_rnd_seeds->unmap();
}

std::string Sample5Scene::datapath( const std::string& base )
{
    texture_path = "/Users/haoluo/qt-workspace/sample5/data";
    return texture_path + "/" + base;
}
