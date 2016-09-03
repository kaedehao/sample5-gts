#ifndef SHADERWINDOW_H
#define SHADERWINDOW_H

#include <QDialog>
#include <qlayoutitem.h>
#include <qboxlayout.h>
#include "qcolordialog.h"
#include "qslider.h"
#include "qpushbutton.h"
//#include "glwidget.h"
#include <optixu/optixpp_namespace.h>
//using namespace optix;

namespace Ui {
class ShaderWindow;
}

class ShaderWindow : public QDialog
{
    Q_OBJECT

public:
    explicit ShaderWindow(QWidget *parent = 0);
    ~ShaderWindow();

    void clean();
    void addWidget(QWidget *widget);
    void insertWidget(int index, QWidget *widget);
    void addItem(QSpacerItem *item);
    void addLayout(QBoxLayout* layout);


public slots:
    void on_pushButton_clicked();
    void colorDialog(QColor color, optix::Variable matlVariable, QSlider *slider);

    void updateShader(double value, optix::Variable matlVariable);
    void updateShader(int value, optix::Variable matlVariable);
    void updateShader(QColor color, optix::Variable matlVariable, QSlider *slider);
    void updateShader(QColor color, qreal lightnessF, optix::Variable matlVariable, QPushButton *pushButton);

private:
    Ui::ShaderWindow *ui;
    QColorDialog *colorPickDialog;
};

#endif // SHADERWINDOW_H
