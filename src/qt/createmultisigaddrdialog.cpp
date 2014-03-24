#include "createmultisigaddrdialog.h"
#include "ui_createmultisigaddrdialog.h"

#include "addresstablemodel.h"
#include "guiutil.h"

#include "wallet.h"
#include "base58.h"
#include "init.h"
#include "bitcoinrpc.h"

#include <QDataWidgetMapper>
#include <QMessageBox>

CreateMultiSigAddrDialog::CreateMultiSigAddrDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CreateMultiSigAddrDialog)
{
    ui->setupUi(this);

    connect(ui->btnCreate, SIGNAL(clicked()), this, SLOT(create()));
    connect(ui->btnCancel, SIGNAL(clicked()), this, SLOT(cancel()));

    ui->comboBoxRequire->addItem(QString("1"), QVariant(1));
    ui->comboBoxRequire->addItem(QString("2"), QVariant(2));
    ui->comboBoxRequire->setCurrentIndex(1);
    
    ui->comboBoxTotal->addItem(QString("2"), QVariant(2));
    ui->comboBoxTotal->addItem(QString("3"), QVariant(3));
    ui->comboBoxTotal->setCurrentIndex(0);
    currentPubkeyNum = 2;
    connect(ui->comboBoxTotal, SIGNAL(currentIndexChanged(int)), this, SLOT(handleSelectionChanged(int)));

    ui->pubkeyEdit2->setVisible(false);
}

CreateMultiSigAddrDialog::~CreateMultiSigAddrDialog()
{
    delete ui;
}

void CreateMultiSigAddrDialog::create()
{
    int nRequired = ui->comboBoxRequire->itemData(ui->comboBoxRequire->currentIndex()).toInt();
    int total = ui->comboBoxTotal->itemData(ui->comboBoxTotal->currentIndex()).toInt();

    QString pubkeyHex[3];
    pubkeyHex[0] = ui->pubkeyEdit0->text();
    pubkeyHex[1] = ui->pubkeyEdit1->text();
    if ( 3 == total )
        pubkeyHex[2] = ui->pubkeyEdit2->text();

    int myKeyNum = 0;
    std::vector<CPubKey> pubkeys;
    pubkeys.resize(total);
    for (unsigned int i = 0; i < total; i++)
    {
        const std::string& ks = pubkeyHex[i].toStdString();
        if (IsHex(ks))
        {
            CPubKey vchPubKey(ParseHex(ks));
            if (!vchPubKey.IsFullyValid())
            {
                QMessageBox::warning(this, windowTitle(),
                    tr("Invalid public key  \"%1\" ").arg(pubkeyHex[i]),
                    QMessageBox::Ok, QMessageBox::Ok);
                return;
            }
            pubkeys[i] = vchPubKey;
            if (IsMine(*pwalletMain, vchPubKey.GetID()))
                myKeyNum += 1;
        }
        else
        {
            QMessageBox::warning(this, windowTitle(),
                tr("Invalid public key  \"%1\" ").arg(pubkeyHex[i]),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }
    }

    if ( 0 == myKeyNum )
    {
        QMessageBox::warning(this, windowTitle(),
            tr("No public key belongs to this wallet!"),
            QMessageBox::Ok, QMessageBox::Ok);
        return;
    }
    if ( total == myKeyNum )
    {
        QMessageBox::warning(this, windowTitle(),
            tr("All public keys belong this wallet!"),
            QMessageBox::Ok, QMessageBox::Ok);
        return;
    }

    CScript inner;
    inner.SetMultisig(nRequired, pubkeys);
    CScriptID innerID = inner.GetID();
    pwalletMain->AddCScript(inner);
    std::string strAccount;
    pwalletMain->SetAddressBookName(innerID, strAccount);

    accept();
}

void CreateMultiSigAddrDialog::cancel()
{
    reject();
}

void CreateMultiSigAddrDialog::handleSelectionChanged(int idx)
{
    int num = idx + 2;
    if ( currentPubkeyNum != num )
    {
        currentPubkeyNum = num;
        if ( 3 == currentPubkeyNum )
        {
            ui->comboBoxRequire->clear();
            ui->comboBoxRequire->addItem(QString("1"), QVariant(1));
            ui->comboBoxRequire->addItem(QString("2"), QVariant(2));
            ui->comboBoxRequire->addItem(QString("3"), QVariant(3));
            ui->comboBoxRequire->setCurrentIndex(1);
            ui->pubkeyEdit2->setVisible(true);
        }
        else
        {
            ui->comboBoxRequire->clear();
            ui->comboBoxRequire->addItem(QString("1"), QVariant(1));
            ui->comboBoxRequire->addItem(QString("2"), QVariant(2));
            ui->comboBoxRequire->setCurrentIndex(1);
            ui->pubkeyEdit2->setVisible(false);
        }
    }
}

