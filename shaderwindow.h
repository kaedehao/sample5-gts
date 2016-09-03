#ifndef SHADERWINDOW_H
#define SHADERWINDOW_H

#include <QWidget>
#include <qlayoutitem.h>
#include <qboxlayout.h>
#include "qcolordialog.h"

namespace Ui {
class ShaderWindow;
}

class ShaderWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ShaderWindow(QWidget *parent = 0);
    ~ShaderWindow();

    void addWidget(QWidget *widget);
    void addItem(QSpacerItem *item);
    void addLayout(QBoxLayout* layout);


private slots:
    void on_pushButton_clicked();
    void colorDialog();


private:
    Ui::ShaderWindow *ui;
};

#endif // SHADERWINDOW_H
