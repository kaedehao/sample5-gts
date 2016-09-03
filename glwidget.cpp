#if defined(__APPLE__)
#  include <GLUT/glut.h>
#  define GL_FRAMEBUFFER_SRGB_EXT           0x8DB9
#  define GL_FRAMEBUFFER_SRGB_CAPABLE_EXT   0x8DBA
#else
#  include <GL/glew.h>
#  if defined(_WIN32)
#    include <GL/wglew.h>
#  endif
#  include <GL/glut.h>
#endif

#include "glwidget.h"
#include "mouse.h"
#include <DeviceMemoryLogger.h>
#include <NsightHelper.h>

#include <iostream>
#include <sstream>
#include <stdio.h>
#include <string>

#include <QMouseEvent>


#include <Python/Python.h>
#include "thread.h"

#include <QtWidgets>
//#include <QtCore/QVariant>


using namespace optix;
using namespace std;

//----------------------------------------------------------------------------------------------------------------------
Mouse*         glWidget::m_mouse                = 0;
PinholeCamera* glWidget::m_camera               = 0;
SampleScene*   glWidget::m_scene                = 0;

double         glWidget::m_last_frame_time      = 0.0;
unsigned int   glWidget::m_last_frame_count     = 0;
unsigned int   glWidget::m_frame_count          = 0;

bool           glWidget::m_display_fps          = true;
double         glWidget::m_fps_update_threshold = 0.5;
char           glWidget::m_fps_text[32];

float3         glWidget::m_text_color           = make_float3( 0.95f );
float3         glWidget::m_text_shadow_color    = make_float3( 0.10f );
bool           glWidget::m_print_mem_usage      = true;

glWidget::contDraw_E glWidget::m_app_continuous_mode = CDNone;
glWidget::contDraw_E glWidget::m_cur_continuous_mode = CDNone;

bool           glWidget::m_display_frames       = true;
bool           glWidget::m_save_frames_to_file  = false;
std::string    glWidget::m_save_frames_basename = "";

unsigned int   glWidget::m_texId                = 0;
bool           glWidget::m_sRGB_supported       = false;
bool           glWidget::m_use_sRGB             = false;

double         glWidget::m_progressive_timeout  = -1.;

int            glWidget::m_num_devices          = 0;

bool           glWidget::m_enable_cpu_rendering = false;
bool           glWidget::m_use_vbo_buffer       = false;

QPainter*      glWidget::painter                = 0;

ShaderWindow*  glWidget::shaderWin              = NULL;
//----------------------------------------------------------------------------------------------------------------------

glWidget::glWidget(QWidget *parent) :
    QGLWidget(parent)
{
    setMouseTracking(true);
}

glWidget::~glWidget()
{
}

void glWidget::initializeGL()
{
#ifdef LINUX
    glewExperimental = GL_TRUE;
    GLenum error = glewInit();
    if(error != GLEW_OK){
        std::cerr<<"GLEW IS NOT OK!!! "<<std::endl;
    }
#endif
    glClearColor( 0.5f, 0.5f, 0.5f, 1.0f);
    // enable depth testing for drawing
    glEnable(GL_DEPTH_TEST);
    // enable multisampling for smoother drawing
    glEnable(GL_MULTISAMPLE);

    // as re-size is not explicitly called we need to do this.
    glViewport(0,0,width(),height());

    bool adaptive_aa = false;
    Sample5Scene* scene = new Sample5Scene();
    scene->setAdaptiveAA( adaptive_aa );
    m_scene = scene;
    m_scene->setUseVBOBuffer( m_use_vbo_buffer );
    m_scene->enableCPURendering( m_enable_cpu_rendering );
    m_scene->setNumDevices( m_num_devices );

    if( m_print_mem_usage ) {
      DeviceMemoryLogger::logDeviceDescription(m_scene->getContext(), std::cerr);
      DeviceMemoryLogger::logCurrentMemoryUsage(m_scene->getContext(), std::cerr, "Initial memory available: " );
      std::cerr << std::endl;
    }

    // If m_app_continuous_mode was already set to CDBenchmark* on the command line then preserve it.
    contDraw_E continuous_mode = adaptive_aa ? glWidget::CDProgressive : glWidget::CDNone;
    setContinuousMode( m_app_continuous_mode == CDNone ? continuous_mode : m_app_continuous_mode );

    int buffer_width;
    int buffer_height;
    try {
      // Set up scene
      SampleScene::InitialCameraData camera_data;
      m_scene->initScene( camera_data );

      //if( m_initial_window_width > 0 && m_initial_window_height > 0)
        //m_scene->resize( m_initial_window_width, m_initial_window_height );

      //if ( !m_camera_pose.empty() )
        //camera_data = Sample5::InitialCameraData( m_camera_pose );

      // Initialize camera according to scene params
      m_camera = new PinholeCamera( camera_data.eye,
                                    camera_data.lookat,
                                    camera_data.up,
                                    -1.0f, // hfov is ignored when using keep vertical
                                    camera_data.vfov,
                                    PinholeCamera::KeepVertical );

      Buffer buffer = m_scene->getOutputBuffer();
      RTsize buffer_width_rts, buffer_height_rts;
      buffer->getSize( buffer_width_rts, buffer_height_rts );
      buffer_width  = static_cast<int>(buffer_width_rts);
      buffer_height = static_cast<int>(buffer_height_rts);
      m_mouse = new Mouse( m_camera, buffer_width, buffer_height );
    } catch( Exception& e ){
      sutilReportError( e.getErrorString().c_str() );
      std::cout<<"Error Expection Caught # 1"<<std::endl;
      exit(2);
    }

    // Initialize state
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 1, 0, 1, -1, 1 );
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glViewport(0, 0, buffer_width, buffer_height);

}

void glWidget::resizeGL(int width, int height)
{
    // disallow size 0
    width  = max(1, width);
    height = max(1, height);
    //qDebug()<<width<<","<<height;

    //sutilCurrentTime( &m_start_time );
    m_scene->signalCameraChanged();
    m_mouse->handleResize( width, height );

    try {
      m_scene->resize(width, height);

    } catch( Exception& e ){
      sutilReportError( e.getErrorString().c_str() );
      exit(2);
    }

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 1, 0, 1, -1, 1);
    glViewport(0, 0, width, height);
//    if (m_app_continuous_mode == CDProgressive) {
//      setCurContinuousMode(CDProgressive);
//    }
    //glutPostRedisplay();
    updateGL();
}

std::string glWidget::exec(char* cmd) {
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return "ERROR";
    char buffer[128];
    std::string result = "";
    while(!feof(pipe)) {
        if(fgets(buffer, 128, pipe) != NULL)
            result += buffer;
    }
    pclose(pipe);
    return result;
}

void glWidget::mousePressEvent ( QMouseEvent * event )
{
    //sutilCurrentTime( &m_start_time );
    int state = 0; // GLUT_DOWN
    m_mouse->handleMouseFunc( event->button(), state, event->x(), event->y(), event->modifiers() );
    m_scene->signalCameraChanged();
    updateGL();
}

void glWidget::shaderSelection(QMouseEvent *event){
    //add object selection by color
    if (event->button() != Qt::LeftButton)
        return;

    unsigned int pos_x = event->x();
    unsigned int pos_y = event->y();
    //qDebug()<<"pos: "<<pos_x<<","<<pos_y;

    /*
    // pass pick_index to pinhole_camera.cu
    uint2 idx = make_uint2(2*pos_x, height - (2*pos_y));
    m_scene->getContext()["pick_index"]->setUint(idx);
    //m_scene->getContext()["printEnabled"]->setUint(true);

    uint printEnabled = true;
    memcpy( m_scene->getContext()["printEnabled"]->getBuffer()->map(), &printEnabled, sizeof(printEnabled) );
    m_scene->getContext()["printEnabled"]->getBuffer()->unmap();

    // Get pixel color

    Buffer buffer = m_scene->getOutputBuffer();
    GLsizei width, height;
    RTsize buffer_width, buffer_height;

    GLvoid* imageData = buffer->map();

    buffer->getSize( buffer_width, buffer_height );
    RTformat buffer_format = buffer->getFormat();

    width  = static_cast<GLsizei>(buffer_width);
    height = static_cast<GLsizei>(buffer_height);


    std::vector<unsigned char> pix(3*width * height);
    //qDebug()<<"pix size: "<<pix.size();
    //qDebug()<<"&pix[0]: "<<&pix[0];
    //qDebug()<<"&pix[1]: "<<&pix[1];
    if (buffer_format == RT_FORMAT_UNSIGNED_BYTE4){
        //qDebug()<<"RT_FORMAT_UNSIGNED_BYTE4";
        // Data is BGRA and upside down, so we need to swizzle to RGB
        for(int j = height-1; j >= 0; --j) {
          unsigned char *dst = &pix[0] + (3*width*(height-1-j));
          //qDebug()<<"dst: "<<dst;
          unsigned char *src = ((unsigned char*)imageData) + (4*width*j);
          //qDebug()<<"unsigned char *src: "<<*src;
          //qDebug()<<*src;
          for(int i = 0; i < width; i++) {
            *dst++ = *(src + 2);
            *dst++ = *(src + 1);
            *dst++ = *(src + 0);
            src += 4;
          }
        }
    }

    qDebug()<<"imageData+1: "<<*(((unsigned char*)imageData) + 1);

    qDebug()<<"Pix dim: "<<width<<","<<height;

    int pixIndex = 2*pos_x + (2*pos_y * width);
    pixIndex *= 3;
    //int pixIndex = 2*pos_y + (2*pos_x * height * 3);

    qDebug()<<"pix color: "<<pix[pixIndex++]<<","
                           <<pix[pixIndex++]<<","
                           <<pix[pixIndex];


    buffer->unmap();
    */

    // Get shader id


    Buffer shader_buffer = m_scene->getOutputShaderBuffer();
    //int width, height;
    RTsize shader_buffer_width, shader_buffer_height;
    int width, height;
    void* shaderIdData = shader_buffer->map();
          shader_buffer->unmap();


    //qDebug()<<"here";
    shader_buffer->getSize( shader_buffer_width, shader_buffer_height );

    RTformat shader_buffer_format = shader_buffer->getFormat();

    width  = static_cast<int>(shader_buffer_width);
    height = static_cast<int>(shader_buffer_height);

    // Get shader ID

    vector<Material*> shaderId(width * height);
//    qDebug()<<"shaderId size: "<<shaderId.size();
//    qDebug()<<"&shaderId[0]: "<<&shaderId[0];
//    qDebug()<<"&shaderId[1]: "<<&shaderId[1];
//    qDebug()<<"&shaderId[0] + 1: "<<&shaderId[0]+1;
    if (shader_buffer_format == RT_FORMAT_USER){
        qDebug()<<"RT_FORMAT_USER";
        // Data is upside down, so we need to swizzle to RGB
        for(int j = height-1; j >= 0; --j) {
          Material** dst = &shaderId[0] + (width*(height-1-j));
          //qDebug()<<"shaderId dst: "<<j;
          //Material* *src = (Material**)(((Material*)shaderIdData) + (width*j));
          Material* *src = ((Material**)shaderIdData) + (width*j);
          //qDebug()<<"*src: "<<*src;
          for(int i = 0; i < width; i++)
              *dst++ = *src++;
        }
    }

//    Material* src = ((Material*)shaderIdData) +1;
//    qDebug()<<"shaderIdData+1: "<<*src;
//    Material test = *src;
//    //test["solid_color"]->setFloat(0.0f, 0.0f, 0.0f);

    int shaderIndex = 2*pos_x + (2*pos_y * width);

    if (shaderId[shaderIndex]){


        Material picked_matl = *shaderId[shaderIndex];
        //qDebug()<<"shader id: "<<shaderId[shaderIndex];

        shaderPopupWindow(picked_matl);
    }
}

void glWidget::shaderPopupWindow(Material matl){
    //qDebug()<< shaderWin;
    if (shaderWin != NULL)
        return;

    shaderWin = new ShaderWindow(this);

    //picked_malt["solid_color"]->setFloat( 0.0f, 0.0f, 0.0f );
    int v_count = matl->getVariableCount();
        //qDebug()<<"variable count: "<<v_count;
    QSizePolicy sizePolicy1(QSizePolicy::Preferred, QSizePolicy::Fixed);
    sizePolicy1.setHorizontalStretch(0);
    sizePolicy1.setVerticalStretch(0);

    QFont font1;
    font1.setPointSize(10);
    for (int i=0; i<v_count; i++){
        Variable matlVariable = matl->getVariable(i);
        string variableName = matlVariable->getName();
        //cout<<"variable name: "<<variableName<<endl;
        //picked_malt[variableName]->setFloat(0.0f, 1.0f, 1.0f);

        QLabel *label = new QLabel(shaderWin);
        label->setObjectName(QStringLiteral("label") );
        sizePolicy1.setHeightForWidth(label->sizePolicy().hasHeightForWidth());
        label->setSizePolicy(sizePolicy1);
        label->setFont(font1);


        //shaderWin->addWidget(label, 0, 0, 1, 1);
        //label->setText(QApplication::translate("Shader Attribute", variableName, 0));
        label->setText(QString::fromStdString(variableName+":"));

        QHBoxLayout* horizontalLayout = new QHBoxLayout();
        horizontalLayout->setSpacing(10);
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));


        QSlider* horizontalSlider = new QSlider(shaderWin);
        horizontalSlider->setObjectName(QStringLiteral("horizontalSlider"));
        QSizePolicy sizePolicy1(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(horizontalSlider->sizePolicy().hasHeightForWidth());
        horizontalSlider->setSizePolicy(sizePolicy1);
        horizontalSlider->setOrientation(Qt::Horizontal);


        RTobjecttype variableType = matlVariable->getType();

        switch(variableType){

        case RT_OBJECTTYPE_USER:{
            QLineEdit* lineEdit = new QLineEdit(shaderWin);
            lineEdit->setObjectName(QStringLiteral("lineEdit"));
            QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
            sizePolicy.setHorizontalStretch(0);
            sizePolicy.setVerticalStretch(0);
            sizePolicy.setHeightForWidth(lineEdit->sizePolicy().hasHeightForWidth());
            lineEdit->setSizePolicy(sizePolicy);

            void* shaderId;
            matlVariable->getUserData(sizeof(void*), &shaderId);
            //qDebug()<<shaderId;

            std::stringstream addressString;
            addressString<<shaderId;
            lineEdit->setText(QString::fromStdString(addressString.str()));
            lineEdit->setEnabled(false);

            //horizontalLayout->addWidget(lineEdit);
            shaderWin->insertWidget(0, label);
            shaderWin->insertWidget(1, lineEdit);


            QFrame* line = new QFrame(shaderWin);
            line->setObjectName(QStringLiteral("line"));
            line->setFrameShape(QFrame::HLine);
            line->setFrameShadow(QFrame::Sunken);

            shaderWin->insertWidget(2, line);
            delete horizontalSlider;
            continue;
        }break;

        case RT_OBJECTTYPE_FLOAT3:{
            QColor color;
            qreal lightnessF;
            optix::float3 f;

            shaderWin->addWidget(label);
            QPushButton* pushButton = new QPushButton(shaderWin);
            pushButton->setObjectName(QStringLiteral("pushButton"));

            f = matlVariable->getFloat3();
            color.setRgbF(f.x, f.y, f.z);

            QString backGroundColor = "background-color: rgb(";
            pushButton->setStyleSheet(backGroundColor+
                                  QString::number(color.red())+","+
                                  QString::number(color.green())+","+
                                  QString::number(color.blue())+","+
                                                        ");" );

            lightnessF = color.lightnessF();
            horizontalSlider->setValue(lightnessF*100);


            QObject::connect(pushButton, &QPushButton::clicked, [=]() { shaderWin->colorDialog(color, matlVariable, horizontalSlider); });

            QObject::connect(horizontalSlider, &QSlider::valueChanged, [=](int newValue){shaderWin->updateShader(color, (float)newValue/100.0f, matlVariable, pushButton);} );

            horizontalLayout->addWidget(pushButton);
        }break;

        case RT_OBJECTTYPE_FLOAT:
        case RT_OBJECTTYPE_INT:{
            //cout<<"variable name: "<<variableName<<endl;
            shaderWin->addWidget(label);
            if (variableType == RT_OBJECTTYPE_FLOAT){
                QDoubleSpinBox* spinBox = new QDoubleSpinBox(shaderWin);
                spinBox->setObjectName(QStringLiteral("doubleSpinBox"));
                spinBox->setValue(matlVariable->getFloat());
                spinBox->setSingleStep(0.01);
                horizontalLayout->addWidget(spinBox);

                horizontalSlider->setValue((int)matlVariable->getFloat());
                QObject::connect(spinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double newValue) { horizontalSlider->setValue((int)newValue); });

                QObject::connect(horizontalSlider, &QSlider::valueChanged, [=](int newValue) { spinBox->setValue((double)newValue);});

            QObject::connect(spinBox, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), [=](double newValue) { shaderWin->updateShader(newValue, matlVariable); });
            }
            else if (variableType == RT_OBJECTTYPE_INT){
                QSpinBox* spinBox = new QSpinBox(shaderWin);
                spinBox->setObjectName(QStringLiteral("spinBox"));
                spinBox->setValue(matlVariable->getInt());
                horizontalLayout->addWidget(spinBox);

                QObject::connect(spinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [=](int newValue) { shaderWin->updateShader(newValue, matlVariable); });

                QObject::connect(spinBox, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), [=](int newValue) { horizontalSlider->setValue(newValue); });

                QObject::connect(horizontalSlider, &QSlider::valueChanged, [=](int newValue) { spinBox->setValue(newValue);});
            }
            //horizontalLayout->addWidget(spinBox);
        }break;

        default:
            qDebug()<<"unkown type variable";
            break;
        }

        horizontalLayout->addWidget(horizontalSlider);

        shaderWin->addLayout(horizontalLayout);


        QFrame* line = new QFrame(shaderWin);
        line->setObjectName(QStringLiteral("line"));
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);

        shaderWin->addWidget(line);
    }

    //QColor pickedColor = QColorDialog::getColor(Qt::yellow, this);

    QSpacerItem* verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

    shaderWin->addItem(verticalSpacer);


    //QObject::connect(shaderWin, &ShaderWindow::destroyed, [=]() { resetShaderWinObj(); });
    shaderWin->show();
    shaderWin->raise();
    shaderWin->activateWindow();
    shaderWin->setAttribute(Qt::WA_DeleteOnClose);
    //shaderWin->setWindowFlags(Qt::Dialog|Qt::WindowStaysOnTopHint|Qt::X11BypassWindowManagerHint);
    shaderWin->setAttribute(Qt::WA_AlwaysStackOnTop);
}

void glWidget::mouseReleaseEvent(QMouseEvent *event)
{
    //sutilCurrentTime( &m_start_time );
    int state = 1; // GLUT_UP
    m_mouse->handleMouseFunc( event->button(), state, event->x(), event->y(), event->modifiers() );
    updateGL();

    shaderSelection(event);
}

void glWidget::mouseMoveEvent ( QMouseEvent * event )
{
    if(event->buttons() != Qt::NoButton){ // GLUT_UP
        m_mouse->handleMoveFunc( event->x(), event->y() );
        m_scene->signalCameraChanged();
        //qDebug()<<"NOBUTTON";
    }
    updateGL();
}

//----------------------------------------------------------------------------------------------------------------------
//void glWidget::paintEvent(QPaintEvent *event)
//{
//    paintGL();
//}

void glWidget::getMayaCamera()
{
    //Get camera data from Maya
    if( Thread::globalDict != NULL ){
        Thread::python_retrieve_camera();
        if( Thread::camera_array_changed ){
            m_camera->eye = make_float3( Thread::camera_array[0],
                                         Thread::camera_array[1],
                                         Thread::camera_array[2]);

            m_camera->lookat = make_float3( Thread::camera_array[3],
                                            Thread::camera_array[4],
                                            Thread::camera_array[5]);

            m_camera->up = make_float3( Thread::camera_array[6],
                                        Thread::camera_array[7],
                                        Thread::camera_array[8]);

            m_camera->setup();
            m_scene->signalCameraChanged();
            //qDebug()<<"camera changed"<<Thread::camera_array_changed;
        }

        if( Thread::geometry_array_changed ){
            m_scene->updateGeometry(Thread::geometry_array[0],
                                    Thread::geometry_array[1],
                                    Thread::geometry_array[2]);
        }
    }
}

void glWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    getMayaCamera();
    display();
    update();
}

void glWidget::display()
{
    bool display_requested = true;

    try {
      // render the scene
      float3 eye, U, V, W;
      m_camera->getEyeUVW( eye, U, V, W );
      // Don't be tempted to just start filling in the values outside of a constructor,
      // because if you add a parameter it's easy to forget to add it here.

      SampleScene::RayGenCameraData camera_data( eye, U, V, W );

      {
        nvtx::ScopedRange r( "trace" );
        m_scene->trace( camera_data, display_requested );
      }

      // Always count rendered frames
      ++m_frame_count;

      if( display_requested && m_display_frames ) {
        // Only enable for debugging
        // glClearColor(1.0, 0.0, 0.0, 0.0);
        // glClear(GL_COLOR_BUFFER_BIT);

        nvtx::ScopedRange r( "displayFrame" );
        displayFrame();
      }
    } catch( Exception& e ){
      sutilReportError( e.getErrorString().c_str() );
      std::cout<<"Error Expection Caught #2"<<std::endl;
      exit(2);
    }

    // Do not draw text on 1st frame -- issue on linux causes problems with
    // glDrawPixels call if drawText glutBitmapCharacter is called on first frame.
    if ( m_display_fps && m_cur_continuous_mode != CDNone && m_frame_count > 1 ) {
      // Output fps
      double current_time;
      sutilCurrentTime( &current_time );
      double dt = current_time - m_last_frame_time;
      if( dt > m_fps_update_threshold ) {
        sprintf( m_fps_text, "fps: %7.2f", (m_frame_count - m_last_frame_count) / dt );

        m_last_frame_time = current_time;
        m_last_frame_count = m_frame_count;
      } else if( m_frame_count == 1 ) {
        sprintf( m_fps_text, "fps: %7.2f", 0.f );
      }

      drawText( m_fps_text, 10.0f, 10.0f, GLUT_BITMAP_8_BY_13 );
    }

    if( m_print_mem_usage ) {
      // Output memory
      std::ostringstream str;
      DeviceMemoryLogger::logCurrentMemoryUsage(m_scene->getContext(), str);
      drawText( str.str(), 10.0f, 26.0f, GLUT_BITMAP_8_BY_13 );
    }
}

void glWidget::displayFrame()
{
    GLboolean sRGB = GL_FALSE;
    if (m_use_sRGB && m_sRGB_supported) {
      glGetBooleanv( GL_FRAMEBUFFER_SRGB_CAPABLE_EXT, &sRGB );
      if (sRGB) {
        glEnable(GL_FRAMEBUFFER_SRGB_EXT);
      }
    }

    // Draw the resulting image
    Buffer buffer = m_scene->getOutputBuffer();
    //Buffer buffer = sample2Scene->getOutputBuffer();
    RTsize buffer_width_rts, buffer_height_rts;
    buffer->getSize( buffer_width_rts, buffer_height_rts );
    int buffer_width  = static_cast<int>(buffer_width_rts);
    int buffer_height = static_cast<int>(buffer_height_rts);
    RTformat buffer_format = buffer->getFormat();

    if( m_save_frames_to_file ) {
      static char fname[128];
      std::string basename = m_save_frames_basename.empty() ? "frame" : m_save_frames_basename;
      sprintf(fname, "%s_%05d.ppm", basename.c_str(), m_frame_count);
      //sutilDisplayFilePPM( fname, buffer->get() );
    }
    //qDebug()<<"use vbo buffer: "<<m_scene->usesVBOBuffer() ;
    unsigned int vboId = 0;
    if( m_scene->usesVBOBuffer() ){
        vboId = buffer->getGLBOId();
    }

    if (vboId)
    {
      if (!m_texId)
      {

        glGenTextures( 1, &m_texId );
        glBindTexture( GL_TEXTURE_2D, m_texId);

        // Change these to GL_LINEAR for super- or sub-sampling
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

        // GL_CLAMP_TO_EDGE for linear filtering, not relevant for nearest.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindTexture( GL_TEXTURE_2D, 0);
      }

      glBindTexture( GL_TEXTURE_2D, m_texId );

      // send pbo to texture
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER, vboId);

      RTsize elementSize = buffer->getElementSize();
      if      ((elementSize % 8) == 0) glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
      else if ((elementSize % 4) == 0) glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
      else if ((elementSize % 2) == 0) glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
      else                             glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

      {
        nvtx::ScopedRange r( "glTexImage" );
        if(buffer_format == RT_FORMAT_UNSIGNED_BYTE4) {
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, buffer_width, buffer_height, 0, GL_BGRA, GL_UNSIGNED_BYTE, 0);
        } else if(buffer_format == RT_FORMAT_FLOAT4) {
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F_ARB, buffer_width, buffer_height, 0, GL_RGBA, GL_FLOAT, 0);
        } else if(buffer_format == RT_FORMAT_FLOAT3) {
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F_ARB, buffer_width, buffer_height, 0, GL_RGB, GL_FLOAT, 0);
        } else if(buffer_format == RT_FORMAT_FLOAT) {
          glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE32F_ARB, buffer_width, buffer_height, 0, GL_LUMINANCE, GL_FLOAT, 0);
        } else {
          assert(0 && "Unknown buffer format");
        }
      }
      glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );

      glEnable(GL_TEXTURE_2D);

      // Initialize offsets to pixel center sampling.

      float u = 0.5f/buffer_width;
      float v = 0.5f/buffer_height;

      glBegin(GL_QUADS);
      glTexCoord2f(u, v);
      glVertex2f(0.0f, 0.0f);
      glTexCoord2f(1.0f, v);
      glVertex2f(1.0f, 0.0f);
      glTexCoord2f(1.0f - u, 1.0f - v);
      glVertex2f(1.0f, 1.0f);
      glTexCoord2f(u, 1.0f - v);
      glVertex2f(0.0f, 1.0f);
      glEnd();


      glDisable(GL_TEXTURE_2D);
    } else {
      GLvoid* imageData = buffer->map();
      assert( imageData );

      GLenum gl_data_type = GL_FALSE;
      GLenum gl_format = GL_FALSE;

      switch (buffer_format) {
            case RT_FORMAT_UNSIGNED_BYTE4:
              gl_data_type = GL_UNSIGNED_BYTE;
              gl_format    = GL_BGRA;
              break;

            case RT_FORMAT_FLOAT:
              gl_data_type = GL_FLOAT;
              gl_format    = GL_LUMINANCE;
              break;

            case RT_FORMAT_FLOAT3:
              gl_data_type = GL_FLOAT;
              gl_format    = GL_RGB;
              break;

            case RT_FORMAT_FLOAT4:
              gl_data_type = GL_FLOAT;
              gl_format    = GL_RGBA;
              break;

            default:
              fprintf(stderr, "Unrecognized buffer data type or format.\n");
              exit(2);
              break;
      }

      RTsize elementSize = buffer->getElementSize();
      int align = 1;
      if      ((elementSize % 8) == 0) align = 8;
      else if ((elementSize % 4) == 0) align = 4;
      else if ((elementSize % 2) == 0) align = 2;
      glPixelStorei(GL_UNPACK_ALIGNMENT, align);

      NVTX_RangePushA("glDrawPixels");
      glDrawPixels( static_cast<GLsizei>( buffer_width ), static_cast<GLsizei>( buffer_height ),
        gl_format, gl_data_type, imageData);
      NVTX_RangePop();

      buffer->unmap();

    }
    if (m_use_sRGB && m_sRGB_supported && sRGB) {
      glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    }
}

void glWidget::drawText( const std::string& text, float x, float y, void* font )
{
  // Save state
  glPushAttrib( GL_CURRENT_BIT | GL_ENABLE_BIT );

  glDisable( GL_TEXTURE_2D );
  glDisable( GL_LIGHTING );
  glDisable( GL_DEPTH_TEST);

  glColor3fv( &( m_text_shadow_color.x) ); // drop shadow
  // Shift shadow one pixel to the lower right.
  glWindowPos2f(x + 1.0f, y - 1.0f);
  for( std::string::const_iterator it = text.begin(); it != text.end(); ++it )
    glutBitmapCharacter( font, *it );

  glColor3fv( &( m_text_color.x) );        // main text
  glWindowPos2f(x, y);
  for( std::string::const_iterator it = text.begin(); it != text.end(); ++it )
    glutBitmapCharacter( font, *it );

  // Restore state
  glPopAttrib();

//    QPen textPen;
//    QFont textFont;


//    painter->setRenderHint(QPainter::Antialiasing);

//    textPen = QPen(Qt::white);
//    textFont.setPixelSize(50);

//    painter->setPen(textPen);
//    painter->setFont(textFont);


//    painter->drawText(QRect(-50, -50, 100, 100), Qt::AlignCenter, QStringLiteral("Qt"));
//    //painter.drawText(0, 0, QStringLiteral("Qt"));

//    painter->end();
}

// This is an internal function that does the actual work.
void glWidget::setCurContinuousMode(contDraw_E continuous_mode)
{
  m_cur_continuous_mode = continuous_mode;

  //sutilCurrentTime( &m_start_time );
  //glutIdleFunc( m_cur_continuous_mode!=CDNone ? idle : 0 );
}

void glWidget::quit(int return_code)
{
  try {
    if(m_scene)
    {
      m_scene->cleanUp();
      if (m_scene->getContext().get() != 0)
      {
        sutilReportError( "Derived scene class failed to call Sample5::cleanUp()" );
        exit(2);
      }
    }
    exit(return_code);
  } catch( Exception& e ) {
    sutilReportError( e.getErrorString().c_str() );
    exit(2);
  }
}

// This is an API function for restaring the progressive timeout timer.
void glWidget::restartProgressiveTimer()
{
  // Unless the user has overridden it, progressive implies a finite continuous drawing timeout.
  if(m_app_continuous_mode == CDProgressive && m_progressive_timeout < 0.0) {
    m_progressive_timeout = 10.0;
  }
}

// This is an API function for the app to specify its desired mode.
void glWidget::setContinuousMode(contDraw_E continuous_mode)
{
  m_app_continuous_mode = continuous_mode;

  // Unless the user has overridden it, progressive implies a finite continuous drawing timeout.
  restartProgressiveTimer();

  setCurContinuousMode(m_app_continuous_mode);
}
