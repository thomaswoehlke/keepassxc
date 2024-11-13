/*
 *  Copyright (C) 2023 KeePassXC Team <team@keepassxc.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 or (at your option)
 *  version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "ImportWizardPageReview.h"
#include "ui_ImportWizardPageReview.h"

#include "core/Database.h"
#include "core/Group.h"
#include "format/BitwardenReader.h"
#include "format/KeePass1Reader.h"
#include "format/OPUXReader.h"
#include "format/OpVaultReader.h"
#include "gui/csvImport/CsvImportWidget.h"
#include "gui/wizard/ImportWizard.h"

#include "cli/Utils.h"
#include "keys/FileKey.h"
#include "keys/PasswordKey.h"

#include <QBoxLayout>
#include <QDir>
#include <QHeaderView>
#include <QTableWidget>

#include "gui/remote/RemoteSettings.h"

struct RemoteParams;

ImportWizardPageReview::ImportWizardPageReview(QWidget* parent)
    : QWizardPage(parent)
    , m_ui(new Ui::ImportWizardPageReview)
    , m_remoteHandler(new RemoteHandler(this))
{
}

ImportWizardPageReview::~ImportWizardPageReview()
{
}

void ImportWizardPageReview::initializePage()
{
    m_db.reset();

    // Reset the widget in case we changed the import type
    for (auto child : children()) {
        delete child;
    }
    m_ui->setupUi(this);

    auto filename = field("ImportFile").toString();
    m_ui->filenameLabel->setText(filename);

    m_ui->messageWidget->hideMessage();
    m_ui->messageWidget->setAnimate(false);
    m_ui->messageWidget->setCloseButtonVisible(false);

    auto importType = field("ImportType").toInt();
    switch (importType) {
    case ImportWizard::IMPORT_CSV:
        setupCsvImport(filename);
        break;
    case ImportWizard::IMPORT_OPVAULT:
        m_db = importOPVault(filename, field("ImportPassword").toString());
        setupDatabasePreview();
        break;
    case ImportWizard::IMPORT_OPUX:
        m_db = importOPUX(filename);
        setupDatabasePreview();
        break;
    case ImportWizard::IMPORT_KEEPASS1:
        m_db = importKeePass1(filename, field("ImportPassword").toString(), field("ImportKeyFile").toString());
        setupDatabasePreview();
        break;
    case ImportWizard::IMPORT_BITWARDEN:
        m_db = importBitwarden(filename, field("ImportPassword").toString());
        setupDatabasePreview();
        break;
    case ImportWizard::IMPORT_REMOTE:
        m_db = importRemote(field("DownloadCommand").toString(),
                            field("DownloadInput").toString(),
                            field("ImportPassword").toString(),
                            field("ImportKeyFile").toString());
        setupDatabasePreview();
    default:
        break;
    }
}

bool ImportWizardPageReview::validatePage()
{
    if (m_csvWidget && field("ImportType").toInt() == ImportWizard::IMPORT_CSV) {
        m_db = m_csvWidget->buildDatabase();
    }
    return !m_db.isNull();
}

QSharedPointer<Database> ImportWizardPageReview::database()
{
    return m_db;
}

void ImportWizardPageReview::setupCsvImport(const QString& filename)
{
    // No need for this label with CSV
    m_ui->previewLabel->hide();

    m_csvWidget = new CsvImportWidget();
    connect(m_csvWidget, &CsvImportWidget::message, m_ui->messageWidget, [this](QString message) {
        m_ui->messageWidget->showMessage(message, KMessageWidget::Error, -1);
    });

    m_csvWidget->load(filename);

    // Qt does not automatically resize a QScrollWidget in a QWizard...
    m_ui->scrollAreaContents->layout()->addWidget(m_csvWidget);
    m_ui->scrollArea->setMinimumSize(m_csvWidget->width() + 50, m_csvWidget->height() + 100);
}

void ImportWizardPageReview::setupDatabasePreview()
{
    if (!m_db) {
        m_ui->scrollArea->setVisible(false);
        return;
    }

    auto entryList = m_db->rootGroup()->entriesRecursive();
    m_ui->previewLabel->setText(tr("Entry count: %1").arg(entryList.count()));

    QStringList headerLabels({tr("Group"), tr("Title"), tr("Username"), tr("Password"), tr("Url")});

    auto tableWidget = new QTableWidget(entryList.count(), headerLabels.count());
    tableWidget->setHorizontalHeaderLabels(headerLabels);

    int row = 0;
    for (auto entry : entryList) {
        QList items({new QTableWidgetItem(entry->group()->name()),
                     new QTableWidgetItem(entry->title()),
                     new QTableWidgetItem(entry->username()),
                     new QTableWidgetItem(entry->password()),
                     new QTableWidgetItem(entry->url())});
        int column = 0;
        for (auto item : items) {
            tableWidget->setItem(row, column++, item);
        }
        ++row;
    }

    tableWidget->setSortingEnabled(true);
    tableWidget->setSelectionMode(QTableWidget::NoSelection);
    tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableWidget->setWordWrap(true);
    tableWidget->horizontalHeader()->setMaximumSectionSize(200);
    tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tableWidget->horizontalHeader()->setStretchLastSection(true);

    m_ui->scrollAreaContents->layout()->addWidget(tableWidget);
}

QSharedPointer<Database> ImportWizardPageReview::importOPUX(const QString& filename)
{
    OPUXReader reader;
    auto db = reader.convert(filename);
    if (reader.hasError()) {
        m_ui->messageWidget->showMessage(reader.errorString(), KMessageWidget::Error, -1);
    }
    return db;
}

QSharedPointer<Database> ImportWizardPageReview::importBitwarden(const QString& filename, const QString& password)
{
    BitwardenReader reader;
    auto db = reader.convert(filename, password);
    if (reader.hasError()) {
        m_ui->messageWidget->showMessage(reader.errorString(), KMessageWidget::Error, -1);
    }
    return db;
}

QSharedPointer<Database> ImportWizardPageReview::importOPVault(const QString& filename, const QString& password)
{
    OpVaultReader reader;
    QDir opVault(filename);
    auto db = reader.convert(opVault, password);
    if (reader.hasError()) {
        m_ui->messageWidget->showMessage(reader.errorString(), KMessageWidget::Error, -1);
    }
    return db;
}

QSharedPointer<Database>
ImportWizardPageReview::importKeePass1(const QString& filename, const QString& password, const QString& keyfile)
{
    KeePass1Reader reader;

    // TODO: Handle case of empty password?

    auto db = reader.readDatabase(filename, password, keyfile);
    if (reader.hasError()) {
        m_ui->messageWidget->showMessage(reader.errorString(), KMessageWidget::Error, -1);
    }

    return db;
}

QSharedPointer<Database> ImportWizardPageReview::importRemote(const QString& downloadCommand,
                                                              const QString& downloadInput,
                                                              const QString& password,
                                                              const QString& keyfile)
{
    auto* params = new RemoteParams();
    params->downloadCommand = downloadCommand;
    params->downloadInput = downloadInput;

    auto result = m_remoteHandler->download(params);

    if (!result.success) {
        m_ui->messageWidget->showMessage(result.errorMessage, KMessageWidget::Error, -1);
    }

    auto key = QSharedPointer<CompositeKey>::create();

    if (!password.isEmpty()) {
        key->addKey(QSharedPointer<PasswordKey>::create(password));
    }
    if (!keyfile.isEmpty()) {
        QSharedPointer<FileKey> fileKey = QSharedPointer<FileKey>::create();
        if (Utils::loadFileKey(keyfile, fileKey)) {
            key->addKey(fileKey);
        } else {
            m_ui->messageWidget->showMessage(tr("Could not load key file."), KMessageWidget::Error, -1);
        }
    }

    QString error;
    QSharedPointer<Database> remoteDb = QSharedPointer<Database>::create();
    remoteDb->markAsTemporaryDatabase();
    if (!remoteDb->open(result.filePath, key, &error)) {
        m_ui->messageWidget->showMessage(
            tr("Could not open remote database. Password or key file may be incorrect."), KMessageWidget::Error, -1);
    }

    return remoteDb;
}
