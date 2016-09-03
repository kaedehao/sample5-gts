#pragma once

#ifndef MOUSE_H
#define MOUSE_H

#include <optixu/optixu_matrix_namespace.h>

class PinholeCamera;


//-----------------------------------------------------------------------------
//
// Mouse class -- handles user mouse events. This class defines the mouse
//                interaction that is modeled off of Maya.
//
//-----------------------------------------------------------------------------

class Mouse {
public:
  Mouse(PinholeCamera* camera, int xres, int yres);
  void handleMouseFunc(int button, int state, int x, int y, int modifier);
  void handleMoveFunc(int x, int y);
  void handlePassiveMotionFunc(int x, int y);
  void handleResize(int new_xres, int new_yres);

private:
  struct InteractionState {
    InteractionState() {}
    InteractionState(int modifier, int button, int state)
      : modifier(modifier), button(button), state(state)
    {}
    int modifier; // Shift,Ctrl,Alt
    int button;   // Left,Middle,Right
    int state;    // Down,Up
    int last_x, last_y;
    optix::float3 rotate_from;
  };

  void call_func(int x, int y);

  void fov(int x, int y);
  void translate(int x, int y);
  void dolly(int x, int y);
  void rotate(int x, int y);
  void track_and_pan(int x, int y);
  void track(int x, int y);
  void pan(int x, int y);
  void transform( const optix::Matrix4x4& trans );

  // Data
  PinholeCamera* camera;
  InteractionState current_interaction;
  int xres, yres;
  float fov_speed;
  float dolly_speed;
  float translate_speed;
};

//-----------------------------------------------------------------------------
//
// PinholeCamera -- performs view transformations
//
//-----------------------------------------------------------------------------

class PinholeCamera {
  typedef optix::float3 float3;
  typedef optix::float2 float2;
public:
  enum AspectRatioMode {
    KeepVertical,
    KeepHorizontal,
    KeepNone
  };

  PinholeCamera(float3 eye, float3 lookat, float3 up, float hfov=60, float vfov=60,
                         AspectRatioMode arm = KeepVertical);

  void setup();

  void getEyeUVW(float3& eye, float3& U, float3& V, float3& W);

  void getEyeLookUpFOV(float3& eye, float3& lookat, float3& up, float& HFOV, float& VFOV);

  void scaleFOV(float);
  void translate(float2);
  void dolly(float);
  void transform( const optix::Matrix4x4& trans );
  void setAspectRatio(float ratio);

  void setParameters(float3 eye_in, float3 lookat_in, float3 up_in, float hfov_in, float vfov_in, PinholeCamera::AspectRatioMode aspectRatioMode_in);

  enum TransformCenter {
    LookAt,
    Eye,
    Origin
  };

  float3 eye, lookat, up;
  float hfov, vfov;
private:
  float3 lookdir, camera_u, camera_v;
  AspectRatioMode aspectRatioMode;
};

#endif // MOUSE_H
