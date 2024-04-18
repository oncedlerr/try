// WardenFort.h
#ifndef WARDENFORT_H
#define WARDENFORT_H

#include <QMainWindow>
#include <QTableWidget>
#include <QPushButton>


QT_BEGIN_NAMESPACE
namespace Ui { class WardenFort; }
QT_END_NAMESPACE

class WardenFort : public QMainWindow
{
    Q_OBJECT

public:
    void toggleButtonVisibility(QPushButton* buttonToHide, QPushButton* buttonToShow);
    WardenFort(QWidget* parent = nullptr);
    ~WardenFort();
    void settrafficAnomalies(const QString& text);
    void setcriticalAnomalies(const QString& text);
    void setOverallAlert(const QString& text);
    void setLabelText1(const QString& text);
    QTableWidget* getTableWidget(); // Getter method for tableWidget
    void scanActiveLANAdapters(); // Corrected declaration
    void setWelcomeText(const QString& text);
    void toggleButtons();
    void handleScrollBarValueChange(int value);

private slots:
    

private:
    Ui::WardenFort* ui;

    void on_passwordButton_released();
    void on_accountButton_released();
    void on_logoutButton_released();
};

#endif // WARDENFORT_H
