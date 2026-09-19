#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QUrl>

#include <KFileCustomDialog>
#include <KFileFilter>
#include <KFileWidget>

#include <iostream>
#include <string>


// ============================================================
// Debug output
// ============================================================

static bool debugEnabled()
{
    return qEnvironmentVariableIntValue(
        "KFILE_PORTAL_DEBUG"
    ) != 0;
}


static void debug(const QString &message)
{
    if (!debugEnabled()) {
        return;
    }

    std::cerr
    << "[kfile-helper] "
    << message.toStdString()
    << std::endl;
}


// ============================================================
// JSON output
// ============================================================

static void writeResponse(const QJsonObject &response)
{
    const QByteArray data =
    QJsonDocument(response).toJson(
        QJsonDocument::Compact
    );

    std::cout << data.constData() << std::endl;
}


// ============================================================
// Filter conversion
// ============================================================

static QList<KFileFilter> parseFilters(
    const QJsonArray &jsonFilters)
{
    QList<KFileFilter> filters;

    for (const QJsonValue &value : jsonFilters) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject object =
        value.toObject();

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

static QJsonObject filterToJson(
    const KFileFilter &filter)
{
    QJsonObject result;

    result["name"] = filter.label();

    QJsonArray globs;

    for (const QString &glob :
        filter.filePatterns()) {

        globs.append(glob);
        }

        QJsonArray mimes;

    for (const QString &mime :
        filter.mimePatterns()) {

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

    return filterToJson(
        fileWidget->currentFilter()
    );
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

    if (!fileInfo.isAbsolute()) {
        return false;
    }

    const QUrl url =
    QUrl::fromLocalFile(
        fileInfo.absoluteFilePath()
    );

    if (!url.isValid() || !url.isLocalFile()) {
        return false;
    }

    jsonUrls.append(
        url.toString(QUrl::FullyEncoded)
    );

    return true;
}


// ============================================================
// KIOFuse support
// ============================================================
//
// KIOFuse exposes:
//
//   org.kde.KIOFuse
//   /org/kde/KIOFuse
//   org.kde.KIOFuse.VFS
//   mountUrl(string) -> string
//
// KFileWidget works with the original KIO URL. KIOFuse is used
// only after the user has selected a remote URL.
//
// Example:
//
//   smb://server/share/file.txt
//
// becomes:
//
//   /run/user/1000/kio-fuse-.../smb/server/share/file.txt
//
// The portal ultimately receives a file:// URI pointing at that
// local FUSE path.
//

static bool localizeUrl(
    const QUrl &url,
    QUrl &localUrl)
{
    if (!url.isValid()) {
        std::cerr
        << "[kfile-helper] "
        << "Cannot localize invalid URL"
        << std::endl;

        return false;
    }

    // Local URLs do not need KIOFuse.
    if (url.isLocalFile()) {
        localUrl =
        QUrl::fromLocalFile(
            QFileInfo(
                url.toLocalFile()
            ).absoluteFilePath()
        );

        return localUrl.isValid() &&
        localUrl.isLocalFile();
    }

    debug(
        "Remote URL requires KIOFuse: " +
        url.toString()
    );

    QDBusConnection bus =
    QDBusConnection::sessionBus();

    if (!bus.isConnected()) {
        std::cerr
        << "[kfile-helper] "
        << "Session D-Bus is not connected"
        << std::endl;

        return false;
    }

    QUrl mountUrl = url;
    QString basename;

    const QString path = url.path();

    /*
     * For a remote file:
     *
     *   smb://server/share/file.txt
     *
     * mount:
     *
     *   smb://server/share
     *
     * and append:
     *
     *   file.txt
     *
     * This mirrors the approach used by
     * xdg-desktop-portal-kde.
     */
    const int lastSlash =
    path.lastIndexOf(QLatin1Char('/'));

    if (lastSlash >= 0 &&
        lastSlash < path.length() - 1) {

        basename =
        path.mid(lastSlash + 1);

    QString parentPath =
    path.left(lastSlash);

    if (parentPath.isEmpty()) {
        parentPath = QStringLiteral("/");
    }

    mountUrl.setPath(parentPath);
        }

        debug(
            "KIOFuse mount URL: " +
            mountUrl.toString()
        );

        const QDBusMessage message =
        QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.KIOFuse"),
                                       QStringLiteral("/org/kde/KIOFuse"),
                                       QStringLiteral("org.kde.KIOFuse.VFS"),
                                       QStringLiteral("mountUrl")
        );

        QDBusMessage request = message;

        request << mountUrl.toString();

        /*
         * Keep this synchronous call intentionally.
         *
         * The file dialog has already been accepted and closed before
         * localizeUrl() is called, so this does not block the visible
         * KFileWidget UI.
         *
         * 30 seconds gives KIOFuse enough time to handle a slow remote
         * filesystem while still providing a bounded wait.
         */
        const QDBusMessage reply =
        bus.call(
            request,
            QDBus::Block,
            30000
        );

        if (reply.type() ==
            QDBusMessage::ErrorMessage) {

            std::cerr
            << "[kfile-helper] "
            << "KIOFuse mountUrl failed: "
            << reply.errorName().toStdString()
            << " - "
            << reply.errorMessage().toStdString()
            << std::endl;

        return false;
            }

            const QList<QVariant> arguments =
            reply.arguments();

            if (arguments.isEmpty() ||
                !arguments.first().canConvert<QString>()) {

                std::cerr
                << "[kfile-helper] "
                << "KIOFuse mountUrl returned an invalid reply"
                << std::endl;

            return false;
                }

                const QString mountPath =
                arguments.first().toString();

                if (mountPath.isEmpty()) {
                    std::cerr
                    << "[kfile-helper] "
                    << "KIOFuse mountUrl returned an empty path"
                    << std::endl;

                    return false;
                }

                QString localPath = mountPath;

                if (!basename.isEmpty()) {
                    if (!localPath.endsWith(
                        QLatin1Char('/'))) {

                        localPath += QLatin1Char('/');
                        }

                        localPath += basename;
                }

                debug(
                    "KIOFuse local path: " +
                    localPath
                );

                localUrl =
                QUrl::fromLocalFile(localPath);

                if (!localUrl.isValid() ||
                    !localUrl.isLocalFile()) {

                    std::cerr
                    << "[kfile-helper] "
                    << "Failed to create local URL from KIOFuse path"
                    << std::endl;

                return false;
                    }

                    debug(
                        "Localized URL: " +
                        localUrl.toString(QUrl::FullyEncoded)
                    );

                    return true;
}


// ============================================================
// Localize a list of selected URLs
// ============================================================

static bool localizeUrls(
    const QList<QUrl> &urls,
    QJsonArray &jsonUrls)
{
    for (const QUrl &url : urls) {
        debug(
            "Selected URL: " +
            url.toString()
        );

        QUrl localUrl;

        if (!localizeUrl(url, localUrl)) {
            std::cerr
            << "[kfile-helper] "
            << "Failed to localize selected URL: "
            << url.toString().toStdString()
            << std::endl;

            return false;
        }

        const QString encoded =
        localUrl.toString(
            QUrl::FullyEncoded
        );

        debug(
            "Localized URL: " +
            encoded
        );

        jsonUrls.append(encoded);
    }

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
        std::cerr
        << "[kfile-helper] "
        << "No input received"
        << std::endl;

        return 1;
    }

    debug(
        "Received request: " +
        QString::fromStdString(line)
    );

    const QByteArray jsonData(
        line.c_str()
    );

    QJsonParseError parseError;

    const QJsonDocument document =
    QJsonDocument::fromJson(
        jsonData,
        &parseError
    );

    if (parseError.error !=
        QJsonParseError::NoError ||
        !document.isObject()) {

        std::cerr
        << "[kfile-helper] "
        << "JSON parse error: "
        << parseError.errorString().toStdString()
        << std::endl;

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
            operation
        );

        if (operation != "open" &&
            operation != "save" &&
            operation != "directory" &&
            operation != "save_files") {

            std::cerr
            << "[kfile-helper] "
            << "Unknown operation"
            << std::endl;

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
                std::cerr
                << "[kfile-helper] "
                << "Failed to obtain KFileWidget"
                << std::endl;

                QJsonObject response;

                response["accepted"] = false;
                response["error"] =
                "kfilewidget_unavailable";

                writeResponse(response);

                return 1;
            }

            // --------------------------------------------------------
            // Dialog title
            // --------------------------------------------------------

            const QString title =
            request.value("title").toString();

            if (!title.isEmpty()) {
                debug(
                    "Title: " +
                    title
                );

                dialog.setWindowTitle(title);
            }

            // --------------------------------------------------------
            // Accept button label
            // --------------------------------------------------------

            const QString acceptLabel =
            request.value("accept_label").toString();

            if (!acceptLabel.isEmpty() &&
                fileWidget->okButton()) {

                debug(
                    "Accept label: " +
                    acceptLabel
                );

            fileWidget->okButton()->setText(
                acceptLabel
            );
                }

                // --------------------------------------------------------
                // Selection mode
                // --------------------------------------------------------

                KFile::Modes mode;

                if (operation == "save_files") {
                    debug(
                        "Mode: save multiple files / select directory"
                    );

                    mode =
                    KFile::Directory |
                    KFile::ExistingOnly;

                    fileWidget->setOperationMode(
                        KFileWidget::Other
                    );

                } else if (operation == "directory") {
                    debug("Mode: directory");

                    mode =
                    KFile::Directory |
                    KFile::ExistingOnly;

                    fileWidget->setOperationMode(
                        KFileWidget::Other
                    );

                } else if (operation == "save") {
                    debug("Mode: save");

                    mode = KFile::File;

                    fileWidget->setOperationMode(
                        KFileWidget::Saving
                    );

                } else {
                    const bool multiple =
                    request.value("multiple")
                    .toBool(false);

                    if (multiple) {
                        debug("Mode: multiple files");

                        mode =
                        KFile::Files |
                        KFile::ExistingOnly;

                    } else {
                        debug("Mode: single file");

                        mode =
                        KFile::File |
                        KFile::ExistingOnly;
                    }

                    fileWidget->setOperationMode(
                        KFileWidget::Opening
                    );
                }

                /*
                 * IMPORTANT:
                 *
                 * Do not use KFile::LocalOnly here.
                 *
                 * KIO/KFileWidget can work with remote URLs. Remote URLs are
                 * converted to local file:// URLs after selection through
                 * KIOFuse.
                 */
                fileWidget->setMode(mode);

                // --------------------------------------------------------
                // Initial directory
                // --------------------------------------------------------

                const QString directory =
                request.value("directory").toString();

                if (!directory.isEmpty()) {
                    debug(
                        "Initial directory: " +
                        directory
                    );

                    /*
                     * Pass the URL directly to KFileWidget.
                     *
                     * KFileWidget/KIO understands remote URLs such as:
                     *
                     *   smb://server/share/
                     *   fish://server/path/
                     *   sftp://server/path/
                     *
                     * Do NOT convert remote URLs to KIOFuse paths here.
                     *
                     * KIOFuse is used only after the user has selected a
                     * remote URL, immediately before returning the result
                     * to the portal.
                     */
                    const QUrl directoryUrl =
                    QUrl::fromUserInput(directory);

                    if (!directoryUrl.isValid()) {
                        debug(
                            "Ignoring invalid initial directory URL"
                        );
                    } else {
                        debug(
                            "Setting KFileWidget URL: " +
                            directoryUrl.toString()
                        );

                        fileWidget->setUrl(directoryUrl);
                    }
                }

                // --------------------------------------------------------
                // Save filename
                // --------------------------------------------------------

                if (operation == "save") {
                    const QString currentName =
                    request.value("current_name")
                    .toString();

                    if (!currentName.isEmpty()) {
                        debug(
                            "Current name: " +
                            currentName
                        );

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

                        /*
                         * Preserve the original directory URL.
                         *
                         * This is important for remote locations:
                         *
                         *   smb://server/share/
                         *
                         * must remain an SMB URL here.
                         *
                         * KIOFuse conversion happens only after the user
                         * accepts the dialog.
                         */
                        if (!directory.isEmpty() &&
                            !safeName.isEmpty()) {

                            const QUrl directoryUrl =
                            QUrl::fromUserInput(directory);

                        if (directoryUrl.isValid()) {
                            currentUrl = directoryUrl;

                            QString path =
                            currentUrl.path();

                            if (!path.endsWith(
                                QLatin1Char('/'))) {

                                path += QLatin1Char('/');
                                }

                                path += safeName;

                            currentUrl.setPath(path);

                            debug(
                                "Save selected URL: " +
                                currentUrl.toString()
                            );
                        }
                            }

                            if (currentUrl.isValid()) {
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

                    filters =
                    parseFilters(
                        request.value("filters")
                        .toArray()
                    );
                    }

                    if (!filters.isEmpty()) {
                        debug(
                            "Received " +
                            QString::number(filters.size()) +
                            " filter(s)"
                        );

                        int selectedFilter = 0;

                        const QString currentFilterName =
                        request.value("current_filter")
                        .toString();

                        if (!currentFilterName.isEmpty()) {
                            for (int i = 0;
                                 i < filters.size();
                            ++i) {

                                if (filters[i].label() ==
                                    currentFilterName) {

                                    selectedFilter = i;
                                break;
                                    }
                            }

                            debug(
                                "Requested current filter: " +
                                currentFilterName
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

                        if (selectedUrls.size() != 1) {
                            std::cerr
                            << "[kfile-helper] "
                            << "SaveFiles did not return exactly "
                            "one directory"
                            << std::endl;

                            QJsonObject response;

                            response["accepted"] = false;
                            response["error"] =
                            "invalid_save_directory";

                            writeResponse(response);

                            return 1;
                        }

                        QUrl selectedDirectoryUrl;

                        if (!localizeUrl(
                            selectedUrls.first(),
                                         selectedDirectoryUrl)) {

                            std::cerr
                            << "[kfile-helper] "
                            << "Failed to localize SaveFiles directory"
                            << std::endl;

                        QJsonObject response;

                        response["accepted"] = false;
                        response["error"] =
                        "invalid_save_directory";

                        writeResponse(response);

                        return 1;
                                         }

                                         if (!selectedDirectoryUrl.isLocalFile()) {
                                             std::cerr
                                             << "[kfile-helper] "
                                             << "Localized SaveFiles directory is not local"
                                             << std::endl;

                                             QJsonObject response;

                                             response["accepted"] = false;
                                             response["error"] =
                                             "invalid_save_directory";

                                             writeResponse(response);

                                             return 1;
                                         }

                                         const QString selectedDirectory =
                                         selectedDirectoryUrl.toLocalFile();

                                         debug(
                                             "SaveFiles selected directory: " +
                                             selectedDirectory
                                         );

                                         const QJsonArray requestedFiles =
                                         request.value("files").toArray();

                                         QJsonArray jsonUrls;

                                         for (const QJsonValue &value :
                                             requestedFiles) {

                                             if (!value.isString()) {
                                                 debug(
                                                     "Ignoring non-string SaveFiles entry"
                                                 );

                                                 continue;
                                             }

                                             const QString fileName =
                                             value.toString();

                                             if (!isSafeFileName(fileName)) {
                                                 std::cerr
                                                 << "[kfile-helper] "
                                                 << "Rejected unsafe SaveFiles filename: "
                                                 << fileName.toStdString()
                                                 << std::endl;

                                                 QJsonObject response;

                                                 response["accepted"] = false;
                                                 response["error"] =
                                                 "invalid_filename";

                                                 writeResponse(response);

                                                 return 1;
                                             }

                                             const QString localPath =
                                             QDir(selectedDirectory).filePath(
                                                 fileName
                                             );

                                             if (!appendLocalFileUri(
                                                 jsonUrls,
                                                 localPath)) {

                                                 std::cerr
                                                 << "[kfile-helper] "
                                                 << "Failed to construct local URI for: "
                                                 << localPath.toStdString()
                                                 << std::endl;

                                             QJsonObject response;

                                             response["accepted"] = false;
                                             response["error"] =
                                             "invalid_file_uri";

                                             writeResponse(response);

                                             return 1;
                                                 }

                                                 debug(
                                                     "SaveFiles URI: " +
                                                     jsonUrls.last()
                                                     .toString()
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
                        QString::number(urls.size()) +
                        " URL(s)"
                    );

                    if (urls.isEmpty()) {
                        std::cerr
                        << "[kfile-helper] "
                        << "No selected URLs"
                        << std::endl;

                        QJsonObject response;

                        response["accepted"] = false;
                        response["error"] = "no_selection";

                        writeResponse(response);

                        return 1;
                    }

                    QJsonArray jsonUrls;

                    if (!localizeUrls(urls, jsonUrls)) {
                        std::cerr
                        << "[kfile-helper] "
                        << "Failed to localize one or more selected URLs"
                        << std::endl;

                        QJsonObject response;

                        response["accepted"] = false;
                        response["error"] =
                        "remote_url_localization_failed";

                        writeResponse(response);

                        return 1;
                    }

                    // SaveFile must return exactly one URI.
                    if (operation == "save" &&
                        jsonUrls.size() != 1) {

                        std::cerr
                        << "[kfile-helper] "
                        << "Save operation did not produce exactly "
                        "one local URI"
                        << std::endl;

                    QJsonObject response;

                    response["accepted"] = false;
                    response["error"] =
                    "invalid_save_result";

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
                            );
                        }

                        // --------------------------------------------------------
                        // Response
                        // --------------------------------------------------------

                        QJsonObject response;

                        response["accepted"] = true;
                        response["uris"] = jsonUrls;
                        response["current_filter"] =
                        currentFilter;

                        writeResponse(response);

                        debug("Finished");

                        return 0;
}
