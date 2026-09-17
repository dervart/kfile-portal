#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QPushButton>

#include <KFileCustomDialog>
#include <KFileFilter>
#include <KFileWidget>

#include <iostream>
#include <string>


// ============================================================
// Debug output
// ============================================================

static void debug(const std::string &message)
{
    std::cerr << "[kfile-helper] " << message << std::endl;
}


// ============================================================
// JSON output
// ============================================================

static void writeResponse(const QJsonObject &response)
{
    const QByteArray data =
    QJsonDocument(response).toJson(QJsonDocument::Compact);

    std::cout << data.constData() << std::endl;
}


// ============================================================
// Filter conversion
// ============================================================

static QList<KFileFilter> parseFilters(
    const QJsonArray &jsonFilters,
    int &currentFilterIndex)
{
    QList<KFileFilter> filters;

    currentFilterIndex = -1;

    for (const QJsonValue &value : jsonFilters) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject object = value.toObject();

        const QString name =
        object.value("name").toString();

        QStringList globs;
        QStringList mimes;

        const QJsonArray jsonGlobs =
        object.value("globs").toArray();

        for (const QJsonValue &glob : jsonGlobs) {
            if (glob.isString()) {
                globs.append(glob.toString());
            }
        }

        const QJsonArray jsonMimes =
        object.value("mimes").toArray();

        for (const QJsonValue &mime : jsonMimes) {
            if (mime.isString()) {
                mimes.append(mime.toString());
            }
        }

        filters.append(
            KFileFilter(name, globs, mimes)
        );
    }

    return filters;
}


// ============================================================
// Filter result
// ============================================================

static QJsonObject filterToJson(const KFileFilter &filter)
{
    QJsonObject result;

    result["name"] = filter.label();

    QJsonArray globs;
    for (const QString &glob : filter.filePatterns()) {
        globs.append(glob);
    }

    QJsonArray mimes;
    for (const QString &mime : filter.mimePatterns()) {
        mimes.append(mime);
    }

    result["globs"] = globs;
    result["mimes"] = mimes;

    return result;
}


static QJsonObject emptyFilterJson()
{
    QJsonObject result;

    result["name"] = QString();
    result["globs"] = QJsonArray();
    result["mimes"] = QJsonArray();

    return result;
}


// Builds the "current_filter" JSON value for the response,
// regardless of whether any filters were supplied at all.
static QJsonObject currentFilterToJson(
    const QList<KFileFilter> &filters,
    const KFileWidget *fileWidget)
{
    if (filters.isEmpty() || !fileWidget) {
        return emptyFilterJson();
    }

    return filterToJson(fileWidget->currentFilter());
}


// ============================================================
// File name safety
// ============================================================

// Reject file names that could escape the target directory.
static bool isSafeFileName(const QString &name)
{
    if (name.isEmpty()) {
        return false;
    }

    if (QDir::isAbsolutePath(name)) {
        return false;
    }

    if (name.contains(QLatin1Char('/'))) {
        return false;
    }

    if (name == QLatin1String(".") ||
        name == QLatin1String("..")) {
        return false;
        }

        return true;
}


// ============================================================
// Local URI conversion
// ============================================================

static bool appendLocalFileUri(
    QJsonArray &jsonUrls,
    const QString &path)
{
    const QFileInfo fileInfo(path);

    if (fileInfo.isAbsolute() == false) {
        return false;
    }

    const QUrl url = QUrl::fromLocalFile(fileInfo.absoluteFilePath());

    if (!url.isValid() || !url.isLocalFile()) {
        return false;
    }

    jsonUrls.append(
        url.toString(QUrl::FullyEncoded)
    );

    return true;
}


// ============================================================
// Main
// ============================================================

int main(int argc, char *argv[])
{
    debug("Started");

    QApplication app(argc, argv);

    // --------------------------------------------------------
    // Read one JSON request from stdin
    // --------------------------------------------------------

    std::string line;

    if (!std::getline(std::cin, line)) {
        debug("No input received");
        return 1;
    }

    debug("Received request: " + line);

    const QByteArray jsonData(line.c_str());

    QJsonParseError parseError;

    const QJsonDocument document =
    QJsonDocument::fromJson(jsonData, &parseError);

    if (parseError.error != QJsonParseError::NoError ||
        !document.isObject()) {

        debug("JSON parse error: " +
        parseError.errorString().toStdString());

    QJsonObject response;
    response["accepted"] = false;
    response["error"] = "invalid_json";

    writeResponse(response);

    return 1;
        }

        debug("JSON parsed successfully");

        const QJsonObject request = document.object();

        // --------------------------------------------------------
        // Operation
        // --------------------------------------------------------

        const QString operation =
        request.value("operation").toString();

        debug("Operation: " + operation.toStdString());

        if (operation != "open" &&
            operation != "save" &&
            operation != "directory" &&
            operation != "save_files") {

            debug("Unknown operation");

        QJsonObject response;
        response["accepted"] = false;
        response["error"] = "unknown_operation";

        writeResponse(response);

        return 1;
            }

            // --------------------------------------------------------
            // Dialog
            // --------------------------------------------------------

            debug("Opening file dialog");

            KFileCustomDialog dialog;

            KFileWidget *fileWidget =
            dialog.fileWidget();

            if (!fileWidget) {
                debug("Failed to obtain KFileWidget");

                QJsonObject response;
                response["accepted"] = false;
                response["error"] = "kfilewidget_unavailable";

                writeResponse(response);

                return 1;
            }

            // --------------------------------------------------------
            // Dialog title
            // --------------------------------------------------------

            const QString title =
            request.value("title").toString();

            if (!title.isEmpty()) {
                debug("Title: " + title.toStdString());
                dialog.setWindowTitle(title);
            }

            // --------------------------------------------------------
            // Accept button label
            // --------------------------------------------------------

            const QString acceptLabel =
            request.value("accept_label").toString();

            if (!acceptLabel.isEmpty() && fileWidget->okButton()) {
                debug("Accept label: " + acceptLabel.toStdString());
                fileWidget->okButton()->setText(acceptLabel);
            }

            // --------------------------------------------------------
            // Selection mode
            // --------------------------------------------------------

            KFile::Modes mode;

            if (operation == "save_files") {
                debug("Mode: save multiple files / select directory");

                mode = KFile::Directory |
                KFile::ExistingOnly |
                KFile::LocalOnly;

                fileWidget->setOperationMode(
                    KFileWidget::Other
                );

            } else if (operation == "directory") {
                debug("Mode: directory");

                mode = KFile::Directory |
                KFile::ExistingOnly |
                KFile::LocalOnly;

                fileWidget->setOperationMode(
                    KFileWidget::Other
                );

            } else if (operation == "save") {
                debug("Mode: save");

                mode = KFile::File |
                KFile::LocalOnly;

                fileWidget->setOperationMode(
                    KFileWidget::Saving
                );

            } else {
                const bool multiple =
                request.value("multiple").toBool(false);

                if (multiple) {
                    debug("Mode: multiple files");

                    mode = KFile::Files |
                    KFile::ExistingOnly |
                    KFile::LocalOnly;
                } else {
                    debug("Mode: single file");

                    mode = KFile::File |
                    KFile::ExistingOnly |
                    KFile::LocalOnly;
                }

                fileWidget->setOperationMode(
                    KFileWidget::Opening
                );
            }

            fileWidget->setMode(mode);

            // --------------------------------------------------------
            // Initial directory
            // --------------------------------------------------------

            const QString directory =
            request.value("directory").toString();

            if (!directory.isEmpty()) {
                debug("Initial directory: " +
                directory.toStdString());

                const QUrl directoryUrl =
                QUrl::fromUserInput(directory);

                if (directoryUrl.isLocalFile()) {
                    fileWidget->setUrl(directoryUrl);
                }
            }

            // --------------------------------------------------------
            // Save filename
            // --------------------------------------------------------

            if (operation == "save") {
                const QString currentName =
                request.value("current_name").toString();

                if (!currentName.isEmpty()) {
                    debug("Current name: " +
                    currentName.toStdString());

                    if (!isSafeFileName(currentName)) {
                        debug(
                            "Rejected unsafe current_name, "
                            "using base name only"
                        );
                    }

                    const QString safeName =
                    isSafeFileName(currentName)
                    ? currentName
                    : QFileInfo(currentName).fileName();

                    QUrl currentUrl;

                    if (!directory.isEmpty() &&
                        !safeName.isEmpty()) {

                        const QString localPath =
                        QDir(directory).filePath(safeName);

                    currentUrl =
                    QUrl::fromLocalFile(localPath);
                        }

                        if (currentUrl.isValid() &&
                            currentUrl.isLocalFile()) {

                            fileWidget->setSelectedUrl(
                                currentUrl
                            );

                            } else if (!safeName.isEmpty()) {

                                QUrl relativeUrl;
                                relativeUrl.setPath(safeName);

                                fileWidget->setSelectedUrl(
                                    relativeUrl
                                );
                            }
                }

                // Let KDE ask before overwriting an existing file.
                fileWidget->setConfirmOverwrite(true);
            }

            // --------------------------------------------------------
            // Filters
            // --------------------------------------------------------

            QList<KFileFilter> filters;

            if (request.contains("filters") &&
                request.value("filters").isArray()) {

                int requestedCurrentFilter = -1;

            filters = parseFilters(
                request.value("filters").toArray(),
                                   requestedCurrentFilter
            );
                }

                if (filters.isEmpty()) {
                    debug("No filters supplied");
                } else {
                    debug(
                        "Received " +
                        std::to_string(filters.size()) +
                        " filter(s)"
                    );

                    int selectedFilter = 0;

                    const QString currentFilterName =
                    request.value("current_filter").toString();

                    if (!currentFilterName.isEmpty()) {
                        for (int i = 0; i < filters.size(); ++i) {
                            if (filters[i].label() ==
                                currentFilterName) {

                                selectedFilter = i;
                            break;
                                }
                        }

                        debug(
                            "Requested current filter: " +
                            currentFilterName.toStdString()
                        );
                    }

                    fileWidget->setFilters(
                        filters,
                        filters[selectedFilter]
                    );
                }

                // --------------------------------------------------------
                // Execute dialog
                // --------------------------------------------------------

                debug("Executing dialog");

                const int result = dialog.exec();

                // --------------------------------------------------------
                // Cancel
                // --------------------------------------------------------

                if (result != QDialog::Accepted) {
                    debug("Dialog rejected");

                    QJsonObject response;

                    response["accepted"] = false;

                    if (operation != "save_files") {
                        response["current_filter"] =
                        currentFilterToJson(
                            filters,
                            fileWidget
                        );
                    }

                    writeResponse(response);

                    debug("Finished");

                    return 0;
                }

                debug("Dialog accepted");

                // --------------------------------------------------------
                // SaveFiles
                // --------------------------------------------------------

                if (operation == "save_files") {
                    const QList<QUrl> selectedUrls =
                    fileWidget->selectedUrls();

                    if (selectedUrls.size() != 1 ||
                        !selectedUrls.first().isLocalFile()) {

                        debug(
                            "SaveFiles did not return a local directory"
                        );

                    QJsonObject response;
                    response["accepted"] = false;
                    response["error"] = "invalid_save_directory";

                    writeResponse(response);

                    return 1;
                        }

                        const QString selectedDirectory =
                        selectedUrls.first().toLocalFile();

                        debug(
                            "SaveFiles selected directory: " +
                            selectedDirectory.toStdString()
                        );

                        const QJsonArray requestedFiles =
                        request.value("files").toArray();

                        QJsonArray jsonUrls;

                        for (const QJsonValue &value : requestedFiles) {
                            if (!value.isString()) {
                                debug("Ignoring non-string SaveFiles entry");
                                continue;
                            }

                            const QString fileName =
                            value.toString();

                            if (!isSafeFileName(fileName)) {
                                debug(
                                    "Rejected unsafe SaveFiles filename: " +
                                    fileName.toStdString()
                                );

                                QJsonObject response;
                                response["accepted"] = false;
                                response["error"] = "invalid_filename";

                                writeResponse(response);

                                return 1;
                            }

                            const QString localPath =
                            QDir(selectedDirectory).filePath(fileName);

                            if (!appendLocalFileUri(
                                jsonUrls,
                                localPath)) {

                                debug(
                                    "Failed to construct local URI for: " +
                                    localPath.toStdString()
                                );

                            QJsonObject response;
                            response["accepted"] = false;
                            response["error"] = "invalid_file_uri";

                            writeResponse(response);

                            return 1;
                                }

                                debug(
                                    "SaveFiles URI: " +
                                    jsonUrls.last().toString().toStdString()
                                );
                        }

                        QJsonObject response;

                        response["accepted"] = true;
                        response["uris"] = jsonUrls;

                        writeResponse(response);

                        debug("Finished");

                        return 0;
                }

                // --------------------------------------------------------
                // Selected URLs
                // --------------------------------------------------------

                const QList<QUrl> urls =
                fileWidget->selectedUrls();

                debug(
                    "Selected " +
                    std::to_string(urls.size()) +
                    " URL(s)"
                );

                QJsonArray jsonUrls;

                for (const QUrl &url : urls) {
                    debug(
                        "Selected URL: " +
                        url.toString().toStdString()
                    );

                    // Portal FileChooser results must be local file:// URIs.
                    if (!url.isLocalFile()) {
                        debug(
                            "Discarding non-local selected URL: " +
                            url.toString().toStdString()
                        );

                        continue;
                    }

                    const QString localPath =
                    url.toLocalFile();

                    const QUrl localUrl =
                    QUrl::fromLocalFile(localPath);

                    if (!localUrl.isValid() ||
                        !localUrl.isLocalFile()) {

                        debug(
                            "Discarding invalid local URL: " +
                            localPath.toStdString()
                        );

                    continue;
                        }

                        jsonUrls.append(
                            localUrl.toString(
                                QUrl::FullyEncoded
                            )
                        );
                }

                // SaveFile must return exactly one URI.
                if (operation == "save" &&
                    jsonUrls.size() != 1) {

                    debug(
                        "Save operation did not produce exactly "
                        "one local URI"
                    );

                QJsonObject response;

                response["accepted"] = false;
                response["error"] = "invalid_save_result";

                writeResponse(response);

                return 1;
                    }

                    // --------------------------------------------------------
                    // Current filter
                    // --------------------------------------------------------

                    const QJsonObject currentFilter =
                    currentFilterToJson(
                        filters,
                        fileWidget
                    );

                    if (!filters.isEmpty()) {
                        debug(
                            "Current filter: " +
                            currentFilter
                            .value("name")
                            .toString()
                            .toStdString()
                        );
                    }

                    // --------------------------------------------------------
                    // Response
                    // --------------------------------------------------------

                    QJsonObject response;

                    response["accepted"] = true;
                    response["uris"] = jsonUrls;
                    response["current_filter"] = currentFilter;

                    writeResponse(response);

                    debug("Finished");

                    return 0;
}
