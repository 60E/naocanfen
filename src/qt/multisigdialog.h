#ifndef MULTISIGDIALOG_H
#define MULTISIGDIALOG_H

#include <QDialog>
#include <QString>

namespace Ui {
    class MultiSigDialog;
}

class CWallet;
class WalletModel;
class SendCoinsEntry;
class SendCoinsRecipient;

QT_BEGIN_NAMESPACE
class QUrl;
QT_END_NAMESPACE

/** Dialog for sending bitcoins */
class MultiSigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MultiSigDialog(QWidget *parent = 0);
    ~MultiSigDialog();

    void setModel(WalletModel *model);

    /** Set up the tab chain manually, as Qt messes up the tab chain by default in some cases (issue https://bugreports.qt-project.org/browse/QTBUG-10907).
     */
    QWidget *setupTabChain(QWidget *prev);

    void setAddress(const QString &address);
    void pasteEntry(const SendCoinsRecipient &rv);
    bool handleURI(const QString &uri);

public slots:
    void clear();
    void reject();
    void accept();
    SendCoinsEntry *addEntry();
    void updateRemoveEnabled();
    void setSharedBalance(qint64 balance, qint64 unconfirmedBalance, qint64 immatureBalance);

private:
    Ui::MultiSigDialog *ui;
    WalletModel *model;
    CWallet *wallet;
    
    bool fNewRecipientAllowed;
    void updateAddressList();
    void updateAddressDetail();
    void updateAddressBalance();

    int currentIndex;

private slots:
    void on_sendButton_clicked();
    void removeEntry(SendCoinsEntry* entry);
    void updateDisplayUnit();
private slots:
    void handleAddrSelectionChanged(int idx);
};

#endif // MULTISIGDIALOG_H
