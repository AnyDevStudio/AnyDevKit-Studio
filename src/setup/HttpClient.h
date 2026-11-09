#pragma once

#include <QObject>
#include <QUrl>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace Studio {

// One request in flight. The body arrives through takeData(); `finished` fires once, after the
// final `dataReady`. It is deleted by whoever asked for it.
class HttpTransfer : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    virtual int status() const = 0;        // HTTP status, 0 before any response
    virtual qint64 rangeStart() const = 0; // first byte of a partial (206) body, 0 otherwise
    virtual QString errorMessage() const = 0; // the transport problem, empty if the server answered
    virtual QByteArray takeData() = 0;
    virtual void abort() = 0;

signals:
    void dataReady();
    void finished();
};

class HttpClient {
public:
    virtual ~HttpClient() = default;
    // `rangeStart` above zero asks for the rest of the file; the server may answer with the whole of it.
    virtual HttpTransfer* get(const QUrl& url, const QString& accept, qint64 rangeStart, QObject* parent) = 0;
};

class NetworkHttpClient final : public HttpClient {
public:
    NetworkHttpClient();
    ~NetworkHttpClient() override;
    HttpTransfer* get(const QUrl& url, const QString& accept, qint64 rangeStart, QObject* parent) override;

private:
    std::unique_ptr<QNetworkAccessManager> manager;
};

}
