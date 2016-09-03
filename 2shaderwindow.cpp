#include "2shaderwindow.h"
#include "ui_2shaderwindow.h"
#include "glwidget.h"

#include <iostream>

ShaderWindow::ShaderWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ShaderWindow)
{
    ui->setupUi(this);
    colorPickDialog = NULL;
}

ShaderWindow::~ShaderWindow()
{
    std::cout<<"shader window closed"<<std::endl;
    glWidget::resetShaderWinObj();
    delete ui;
}

void ShaderWindow::clean(){
    delete ui;
    //ui->verticalLayout = new QVBoxLayout(this);
}

void ShaderWindow::addWidget(QWidget *widget){
    ui->verticalLayout->addWidget(widget);
}

void ShaderWindow::insertWidget(int index, QWidget *widget){
    ui->verticalLayout->insertWidget(index, widget);
}

void ShaderWindow::addItem(QSpacerItem *item){
    ui->verticalLayout->addItem(item);
}

void ShaderWindow::addLayout(QBoxLayout *layout){
    ui->verticalLayout->addLayout(layout);
}

void ShaderWindow::on_pushButton_clicked()
{
    std::cout<<"test click"<<std::endl;
}

void ShaderWindow::colorDialog(QColor color, optix::Variable matlVariable, QSlider* slider){
    //QColor pickedColor = QColorDialog::getColor (color, this);
    if(!colorPickDialog)
        colorPickDialog = new QColorDialog(color, this);

    QObject::connect(colorPickDialog, &QColorDialog::currentColorChanged, [=](QColor newColor) { updateShader(newColor, matlVariable, slider); });

    //QObject::connect(colorPickDialog, &QColorDialog::rejected, [=]() { updateShader(color, matlVariable, slider); });


    colorPickDialog->setModal(false);
    colorPickDialog->show();
    colorPickDialog->raise();
    colorPickDialog->activateWindow();
}

void ShaderWindow::updateShader(QColor color, optix::Variable matlVariable, QSlider* slider){
    optix::double3 f;
    color.getRgbF(&f.x, &f.y, &f.z);
    //std::cout<<"picked color: "<<f.x<<f.y<<f.z<<std::endl;
    matlVariable->setFloat(f.x, f.y, f.z);

    slider->setValue(color.lightnessF()*100);
    //std::cout<<"color lightness: "<<color.lightnessF()*100<<std::endl;
}

void ShaderWindow::updateShader(QColor color, qreal lightnessF, optix::Variable matlVariable, QPushButton* pushButton){
    if(colorPickDialog ){

        color = colorPickDialog->currentColor();
    }
    //std::cout<<"colorPickDialog: "<<colorPickDialog<<std::endl;

    color.setHslF(color.hslHueF(), color.hslSaturationF(), lightnessF);
    optix::double3 f;
    color.getRgbF(&f.x, &f.y, &f.z);
    //std::cout<<"picked color: "<<f.x<<f.y<<f.z<<std::endl;
    matlVariable->setFloat(f.x, f.y, f.z);

    //std::cout<<"color lightness: "<<color.lightnessF()*100<<std::endl;
    QString backGroundColor = "background-color: rgb(";
    pushButton->setStyleSheet(backGroundColor+
                          QString::number(color.red())+","+
                          QString::number(color.green())+","+
                          QString::number(color.blue())+","+
                                                ");" );
}

void ShaderWindow::updateShader(double value, optix::Variable matlVariable){
    //std::cout<<"new value: "<<value<<std::endl;
    matlVariable->setFloat(value);
}

void ShaderWindow::updateShader(int value, optix::Variable matlVariable){
    //std::cout<<"new value: "<<value<<std::endl;
    matlVariable->setInt(value);
}
