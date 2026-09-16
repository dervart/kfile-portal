#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QDir>

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

        debug(
            "JSON parse error: " +
            parseError.errorString().toStdString()
        );

    QJsonObject response;
    response["accepted"] = false;
    response["error"] = "invalid_json";

    writeResponse(response);

    return 1;
        }

        debug("JSON parsed successfully");

        const QJsonObject request =
        document.object();

        // --------------------------------------------------------
        // Operation
        // --------------------------------------------------------

        const QString operation =
        request.value("operation").toString();

        debug(
            "Operation: " +
            operation.toStdString()
        );

        if (operation != "open" &&
            operation != "save" &&
            operation != "directory") {

            debug("Unknown operation");

        QJsonObject response;
        response["accepted"] = false;
        response["error"] = "unknown_operation";

        writeResponse(response);

        return 1;
            }

            // --------------------------------------------------------
            // Create dialog
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
            // Selection mode
            // --------------------------------------------------------

            KFile::Modes mode;

            if (operation == "directory") {

                debug("Mode: directory");

                mode =
                KFile::Directory |
                KFile::ExistingOnly |
                KFile::LocalOnly;

            } else if (operation == "save") {

                debug("Mode: save");

                mode =
                KFile::File |
                KFile::LocalOnly;

            } else {

                const bool multiple =
                request.value("multiple").toBool(false);

                if (multiple) {
                    debug("Mode: multiple files");

                    mode =
                    KFile::Files |
                    KFile::ExistingOnly |
                    KFile::LocalOnly;

                } else {
                    debug("Mode: single file");

                    mode =
                    KFile::File |
                    KFile::ExistingOnly |
                    KFile::LocalOnly;
                }
            }

            fileWidget->setMode(mode);

            // --------------------------------------------------------
            // Initial directory
            // --------------------------------------------------------

            const QString directory =
            request.value("directory").toString();

            if (!directory.isEmpty()) {

                debug(
                    "Initial directory: " +
                    directory.toStdString()
                );

                QUrl directoryUrl =
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

                    debug(
                        "Current name: " +
                        currentName.toStdString()
                    );

                    QUrl currentUrl;

                    if (!directory.isEmpty()) {
                        const QString localPath =
                        QDir(directory).filePath(currentName);

                        currentUrl =
                        QUrl::fromLocalFile(localPath);
                    }

                    if (currentUrl.isValid() &&
                        currentUrl.isLocalFile()) {

                        fileWidget->setSelectedUrl(currentUrl);

                        } else {

                            fileWidget->setSelectedUrl(
                                QUrl::fromUserInput(currentName)
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

            int requestedCurrentFilter = -1;

            if (request.contains("filters") &&
                request.value("filters").isArray()) {

                filters =
                parseFilters(
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
                    request
                    .value("current_filter")
                    .toString();

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

                const int result =
                dialog.exec();

                // --------------------------------------------------------
                // Cancel
                // --------------------------------------------------------

                if (result != QDialog::Accepted) {

                    debug("Dialog rejected");

                    QJsonObject response;

                    response["accepted"] = false;

                    QJsonObject currentFilter;

                    if (!filters.isEmpty()) {
                        currentFilter =
                        filterToJson(
                            fileWidget->currentFilter()
                        );
                    } else {
                        currentFilter["name"] = "";
                        currentFilter["globs"] =
                        QJsonArray();
                        currentFilter["mimes"] =
                        QJsonArray();
                    }

                    response["current_filter"] =
                    currentFilter;

                    writeResponse(response);

                    debug("Finished");

                    return 0;
                }

                debug("Dialog accepted");

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

                    jsonUrls.append(
                        url.toString()
                    );
                }

                // --------------------------------------------------------
                // Current filter
                // --------------------------------------------------------

                QJsonObject currentFilter;

                if (!filters.isEmpty()) {

                    currentFilter =
                    filterToJson(
                        fileWidget->currentFilter()
                    );

                    debug(
                        "Current filter: " +
                        currentFilter
                        .value("name")
                        .toString()
                        .toStdString()
                    );

                } else {

                    currentFilter["name"] = "";
                    currentFilter["globs"] =
                    QJsonArray();
                    currentFilter["mimes"] =
                    QJsonArray();
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
