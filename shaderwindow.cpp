#include "shaderwindow.h"
#include "ui_shaderwindow.h"
#include "glwidget.h"

#include <iostream>

ShaderWindow::ShaderWindow(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ShaderWindow)
{
    ui->setupUi(this);
}

ShaderWindow::~ShaderWindow()
{
     glWidget::resetShaderWinObj();
    delete ui;
}

void ShaderWindow::addWidget(QWidget *widget){
    ui->verticalLayout->addWidget(widget);
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

void ShaderWindow::colorDialog(){
    //std::cout<<"color dialog"<<std::endl;
    QColor pickedColor = QColorDialog::getColor(Qt::yellow, this);
    //qDebug()<<"picked color: "<<pickedColor.rgb();
}
