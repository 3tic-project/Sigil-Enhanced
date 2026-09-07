#pragma once

#include <QDialog>
#include "BookManipulation/NavigationRepair.h"

class NavigationRepairDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NavigationRepairDialog(const NavigationRepair::Plan &plan, QWidget *parent = nullptr);
};
